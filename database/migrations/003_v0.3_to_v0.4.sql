-- Upgrade the deployed EV Charging Platform SQLite schema v0.3 to v0.4.
-- The migration runner owns the transaction and rolls back on any error.
-- No provider URL, credential, or raw upstream response is persisted here.

PRAGMA foreign_keys = OFF;
BEGIN IMMEDIATE;

ALTER TABLE stations ADD COLUMN provider TEXT NOT NULL DEFAULT 'internal';
ALTER TABLE stations ADD COLUMN provider_poi_id TEXT;
ALTER TABLE stations ADD COLUMN map_synced_at TEXT;
ALTER TABLE stations ADD COLUMN map_content_hash TEXT;

ALTER TABLE charging_piles ADD COLUMN simulated INTEGER NOT NULL DEFAULT 0
    CHECK (simulated IN (0, 1));
ALTER TABLE charging_piles ADD COLUMN status_source TEXT NOT NULL DEFAULT 'seed'
    CHECK (status_source IN ('seed', 'business', 'simulation', 'admin'));
ALTER TABLE charging_piles ADD COLUMN status_updated_at TEXT NOT NULL DEFAULT '';
UPDATE charging_piles SET status_updated_at = updated_at
 WHERE status_updated_at = '';

CREATE UNIQUE INDEX ux_stations_provider_poi
    ON stations(provider, provider_poi_id)
    WHERE provider_poi_id IS NOT NULL;

CREATE TABLE map_request_logs (
    id INTEGER PRIMARY KEY,
    request_id TEXT NOT NULL CHECK (length(request_id) BETWEEN 1 AND 64),
    operation TEXT NOT NULL CHECK (operation IN ('map.station.search', 'map.route.plan')),
    user_id INTEGER REFERENCES users(id),
    normalized_query_json TEXT NOT NULL CHECK (length(normalized_query_json) > 0),
    cache_key TEXT,
    result_status TEXT NOT NULL CHECK (result_status IN
        ('success', 'error', 'degraded', 'mock')),
    data_source TEXT CHECK (data_source IS NULL OR data_source IN
        ('tencent_live', 'tencent_cache', 'tencent_stale', 'server_mock')),
    error_code INTEGER,
    warning_code INTEGER,
    created_at TEXT NOT NULL,
    completed_at TEXT
);

CREATE INDEX ix_map_request_logs_created
    ON map_request_logs(created_at DESC, id DESC);
CREATE INDEX ix_map_request_logs_operation_status
    ON map_request_logs(operation, result_status, created_at DESC, id DESC);

CREATE TABLE map_upstream_call_logs (
    id INTEGER PRIMARY KEY,
    map_request_log_id INTEGER NOT NULL REFERENCES map_request_logs(id)
        ON DELETE CASCADE,
    call_type TEXT NOT NULL CHECK (call_type IN
        ('geocode', 'poi_search', 'poi_detail', 'driving', 'walking')),
    result_status TEXT NOT NULL CHECK (result_status IN ('success', 'error')),
    http_status INTEGER,
    latency_ms INTEGER CHECK (latency_ms IS NULL OR latency_ms >= 0),
    error_code INTEGER,
    created_at TEXT NOT NULL
);

CREATE INDEX ix_map_upstream_logs_request
    ON map_upstream_call_logs(map_request_log_id, id);
CREATE INDEX ix_map_upstream_logs_created
    ON map_upstream_call_logs(created_at);

CREATE TABLE map_cache_entries (
    cache_key TEXT PRIMARY KEY CHECK (length(cache_key) BETWEEN 1 AND 512),
    operation TEXT NOT NULL CHECK (operation IN ('map.station.search', 'map.route.plan')),
    payload_json TEXT NOT NULL CHECK (length(payload_json) > 0),
    provider_cursor TEXT,
    fetched_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    stale_until TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    CHECK (stale_until >= expires_at)
);

CREATE INDEX ix_map_cache_expiry
    ON map_cache_entries(expires_at, stale_until);

CREATE TABLE pile_status_events (
    id INTEGER PRIMARY KEY,
    pile_id INTEGER NOT NULL REFERENCES charging_piles(id) ON DELETE CASCADE,
    station_id INTEGER NOT NULL REFERENCES stations(id) ON DELETE CASCADE,
    from_status TEXT NOT NULL CHECK (from_status IN
        ('idle', 'reserved', 'charging', 'fault', 'offline')),
    to_status TEXT NOT NULL CHECK (to_status IN
        ('idle', 'reserved', 'charging', 'fault', 'offline')),
    source TEXT NOT NULL CHECK (source IN ('business', 'simulation', 'admin', 'seed')),
    reason TEXT,
    tick_id INTEGER,
    created_at TEXT NOT NULL
);

CREATE INDEX ix_pile_status_events_pile_created
    ON pile_status_events(pile_id, created_at DESC, id DESC);
CREATE INDEX ix_pile_status_events_created
    ON pile_status_events(created_at);

CREATE TABLE simulation_state (
    station_id INTEGER PRIMARY KEY REFERENCES stations(id) ON DELETE CASCADE,
    seed_id TEXT NOT NULL CHECK (length(seed_id) BETWEEN 1 AND 128),
    last_tick_id INTEGER,
    last_run_at TEXT,
    snapshot_version INTEGER NOT NULL DEFAULT 0 CHECK (snapshot_version >= 0)
);

CREATE TABLE simulation_tick_records (
    simulator_id TEXT NOT NULL CHECK (length(simulator_id) BETWEEN 1 AND 128),
    tick_id INTEGER NOT NULL CHECK (tick_id >= 0),
    fingerprint TEXT NOT NULL CHECK (length(fingerprint) > 0),
    result_status TEXT NOT NULL CHECK (result_status IN
        ('accepted', 'conflict', 'rejected')),
    response_json TEXT NOT NULL CHECK (length(response_json) > 0),
    created_at TEXT NOT NULL,
    PRIMARY KEY (simulator_id, tick_id)
);

CREATE INDEX ix_simulation_tick_records_created
    ON simulation_tick_records(created_at);

UPDATE schema_meta SET value = '0.4' WHERE key = 'schema_version';

-- The migration runner commits only after validating the resulting version and
-- foreign-key integrity. Do not add an explicit COMMIT here.
