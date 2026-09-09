-- EV Charging Platform SQLite schema v0.4
-- The application must execute PRAGMA foreign_keys = ON for every connection.
-- Times are UTC ISO-8601 strings (for example 2026-09-01T08:00:00Z).
-- Money is an integer number of Chinese fen (CNY cents).

PRAGMA foreign_keys = ON;

BEGIN;

CREATE TABLE IF NOT EXISTS schema_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

INSERT OR IGNORE INTO schema_meta(key, value) VALUES ('schema_version', '0.4');

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY,
    phone TEXT NOT NULL UNIQUE
        CHECK (length(phone) = 11 AND phone NOT GLOB '*[^0-9]*'),
    nickname TEXT NOT NULL CHECK (length(trim(nickname)) > 0),
    avatar_path TEXT,
    balance_cents INTEGER NOT NULL DEFAULT 0 CHECK (balance_cents >= 0),
    status TEXT NOT NULL DEFAULT 'active'
        CHECK (status IN ('active', 'frozen')),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS administrators (
    id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE
        CHECK (length(trim(username)) BETWEEN 1 AND 64),
    password_hash_sha256 TEXT NOT NULL
        CHECK (length(password_hash_sha256) = 64
               AND password_hash_sha256 NOT GLOB '*[^0-9a-fA-F]*'),
    role TEXT NOT NULL DEFAULT 'operator'
        CHECK (role IN ('operator', 'super_admin')),
    status TEXT NOT NULL DEFAULT 'active'
        CHECK (status IN ('active', 'disabled')),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS stations (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    address TEXT NOT NULL CHECK (length(trim(address)) > 0),
    latitude REAL NOT NULL CHECK (latitude BETWEEN -90.0 AND 90.0),
    longitude REAL NOT NULL CHECK (longitude BETWEEN -180.0 AND 180.0),
    status TEXT NOT NULL DEFAULT 'active'
        CHECK (status IN ('active', 'inactive')),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    provider TEXT NOT NULL DEFAULT 'internal'
        CHECK (length(trim(provider)) > 0),
    provider_poi_id TEXT,
    map_synced_at TEXT,
    map_content_hash TEXT
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_stations_provider_poi
    ON stations(provider, provider_poi_id)
    WHERE provider_poi_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS charging_piles (
    id INTEGER PRIMARY KEY,
    station_id INTEGER NOT NULL REFERENCES stations(id) ON DELETE CASCADE,
    pile_code TEXT NOT NULL CHECK (length(trim(pile_code)) > 0),
    pile_type TEXT NOT NULL CHECK (pile_type IN ('fast', 'slow')),
    power_kw REAL NOT NULL CHECK (power_kw > 0.0 AND power_kw <= 1000.0),
    unit_price_cents_per_kwh INTEGER NOT NULL
        CHECK (unit_price_cents_per_kwh > 0),
    status TEXT NOT NULL DEFAULT 'idle'
        CHECK (status IN ('idle', 'reserved', 'charging', 'fault', 'offline')),
    total_charge_count INTEGER NOT NULL DEFAULT 0 CHECK (total_charge_count >= 0),
    total_charge_seconds INTEGER NOT NULL DEFAULT 0 CHECK (total_charge_seconds >= 0),
    restart_count INTEGER NOT NULL DEFAULT 0 CHECK (restart_count >= 0),
    last_restart_at TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    simulated INTEGER NOT NULL DEFAULT 0 CHECK (simulated IN (0, 1)),
    status_source TEXT NOT NULL DEFAULT 'seed'
        CHECK (status_source IN ('seed', 'business', 'simulation', 'admin')),
    status_updated_at TEXT NOT NULL DEFAULT '',
    UNIQUE (station_id, pile_code)
);

CREATE TABLE IF NOT EXISTS charging_orders (
    id INTEGER PRIMARY KEY,
    order_no TEXT NOT NULL UNIQUE CHECK (length(trim(order_no)) > 0),
    user_id INTEGER NOT NULL REFERENCES users(id),
    pile_id INTEGER NOT NULL REFERENCES charging_piles(id),
    status TEXT NOT NULL
        CHECK (status IN ('pending_reservation', 'reserved', 'charging',
                          'pending_settlement', 'completed', 'cancelled',
                          'exception')),
    reserved_at TEXT,
    started_at TEXT,
    ended_at TEXT,
    energy_wh INTEGER NOT NULL DEFAULT 0 CHECK (energy_wh >= 0),
    unit_price_cents_per_kwh INTEGER NOT NULL CHECK (unit_price_cents_per_kwh > 0),
    service_fee_cents INTEGER NOT NULL DEFAULT 0 CHECK (service_fee_cents >= 0),
    total_amount_cents INTEGER NOT NULL DEFAULT 0 CHECK (total_amount_cents >= 0),
    settled_at TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    CHECK (ended_at IS NULL OR started_at IS NOT NULL),
    CHECK (settled_at IS NULL OR status = 'completed'),
    CHECK (status NOT IN ('pending_reservation', 'reserved')
        OR (reserved_at IS NOT NULL AND started_at IS NULL
            AND ended_at IS NULL AND settled_at IS NULL)),
    CHECK (status <> 'charging'
        OR (started_at IS NOT NULL AND ended_at IS NULL AND settled_at IS NULL)),
    CHECK (status <> 'pending_settlement'
        OR (started_at IS NOT NULL AND ended_at IS NOT NULL
            AND settled_at IS NULL AND total_amount_cents > 0)),
    CHECK (status <> 'completed'
        OR (started_at IS NOT NULL
            AND ended_at IS NOT NULL
            AND settled_at IS NOT NULL
            AND total_amount_cents > 0))
);

-- A user and a pile can each have at most one active order. The service layer
-- still performs an explicit check so it can return a stable business error.
CREATE UNIQUE INDEX IF NOT EXISTS ux_orders_one_active_user
    ON charging_orders(user_id)
    WHERE status IN ('pending_reservation', 'reserved', 'charging',
                     'pending_settlement');

CREATE UNIQUE INDEX IF NOT EXISTS ux_orders_one_active_pile
    ON charging_orders(pile_id)
    WHERE status IN ('pending_reservation', 'reserved', 'charging');

CREATE TABLE IF NOT EXISTS wallet_transactions (
    id INTEGER PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id),
    order_id INTEGER REFERENCES charging_orders(id),
    transaction_type TEXT NOT NULL
        CHECK (transaction_type IN ('recharge', 'charge', 'refund', 'adjustment')),
    amount_cents INTEGER NOT NULL CHECK (amount_cents <> 0),
    balance_after_cents INTEGER NOT NULL CHECK (balance_after_cents >= 0),
    idempotency_key TEXT UNIQUE,
    created_at TEXT NOT NULL,
    CHECK ((transaction_type = 'recharge' AND amount_cents > 0)
        OR (transaction_type = 'charge' AND amount_cents < 0)
        OR (transaction_type = 'refund' AND amount_cents > 0)
        OR transaction_type = 'adjustment'),
    CHECK (transaction_type <> 'charge' OR order_id IS NOT NULL)
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_wallet_one_charge_per_order
    ON wallet_transactions(order_id)
    WHERE transaction_type = 'charge' AND order_id IS NOT NULL;

CREATE TRIGGER IF NOT EXISTS trg_completed_order_insert_requires_charge
BEFORE INSERT ON charging_orders
WHEN NEW.status = 'completed'
 AND NOT EXISTS (
     SELECT 1 FROM wallet_transactions AS w
     WHERE w.order_id = NEW.id
       AND w.user_id = NEW.user_id
       AND w.transaction_type = 'charge'
       AND w.amount_cents = -NEW.total_amount_cents
       AND w.created_at = NEW.settled_at
 )
BEGIN
    SELECT RAISE(ABORT, 'completed order requires matching charge transaction');
END;

CREATE TRIGGER IF NOT EXISTS trg_completed_order_update_requires_charge
BEFORE UPDATE OF status, user_id, total_amount_cents, settled_at ON charging_orders
WHEN NEW.status = 'completed'
 AND NOT EXISTS (
     SELECT 1 FROM wallet_transactions AS w
     WHERE w.order_id = NEW.id
       AND w.user_id = NEW.user_id
       AND w.transaction_type = 'charge'
       AND w.amount_cents = -NEW.total_amount_cents
       AND w.created_at = NEW.settled_at
 )
BEGIN
    SELECT RAISE(ABORT, 'completed order requires matching charge transaction');
END;

CREATE TRIGGER IF NOT EXISTS trg_completed_charge_delete_guard
BEFORE DELETE ON wallet_transactions
WHEN OLD.transaction_type = 'charge'
 AND EXISTS (
     SELECT 1 FROM charging_orders AS o
     WHERE o.id = OLD.order_id AND o.status = 'completed'
 )
BEGIN
    SELECT RAISE(ABORT, 'cannot delete charge transaction for completed order');
END;

CREATE TRIGGER IF NOT EXISTS trg_completed_charge_update_guard
BEFORE UPDATE OF user_id, order_id, transaction_type, amount_cents, created_at
ON wallet_transactions
WHEN OLD.transaction_type = 'charge'
 AND EXISTS (
     SELECT 1 FROM charging_orders AS o
     WHERE o.id = OLD.order_id
       AND o.status = 'completed'
       AND (NEW.order_id IS NOT o.id
            OR NEW.user_id IS NOT o.user_id
            OR NEW.transaction_type IS NOT 'charge'
            OR NEW.amount_cents IS NOT -o.total_amount_cents
            OR NEW.created_at IS NOT o.settled_at)
 )
BEGIN
    SELECT RAISE(ABORT, 'cannot invalidate charge transaction for completed order');
END;

CREATE TABLE IF NOT EXISTS pile_restart_logs (
    id INTEGER PRIMARY KEY,
    pile_id INTEGER NOT NULL REFERENCES charging_piles(id),
    administrator_id INTEGER NOT NULL REFERENCES administrators(id),
    requested_at TEXT NOT NULL,
    result TEXT NOT NULL CHECK (result IN ('succeeded', 'rejected', 'failed')),
    reason TEXT
);

-- Successful state-changing requests are retained so a client can safely
-- replay the same request ID and receive the original response.
CREATE TABLE IF NOT EXISTS request_records (
    request_id TEXT PRIMARY KEY CHECK (length(request_id) BETWEEN 1 AND 64),
    operation TEXT NOT NULL CHECK (length(operation) BETWEEN 1 AND 64),
    fingerprint TEXT NOT NULL CHECK (length(fingerprint) > 0),
    response_json TEXT NOT NULL CHECK (length(response_json) > 0),
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS map_request_logs (
    id INTEGER PRIMARY KEY,
    request_id TEXT NOT NULL CHECK (length(request_id) BETWEEN 1 AND 64),
    operation TEXT NOT NULL CHECK (operation IN ('map.station.search', 'map.route.plan')),
    user_id INTEGER REFERENCES users(id),
    normalized_query_json TEXT NOT NULL CHECK (length(normalized_query_json) > 0),
    cache_key TEXT,
    result_status TEXT NOT NULL CHECK (result_status IN ('success', 'error', 'degraded', 'mock')),
    data_source TEXT CHECK (data_source IS NULL OR data_source IN
        ('tencent_live', 'tencent_cache', 'tencent_stale', 'server_mock')),
    error_code INTEGER,
    warning_code INTEGER,
    created_at TEXT NOT NULL,
    completed_at TEXT
);
CREATE INDEX IF NOT EXISTS ix_map_request_logs_created
    ON map_request_logs(created_at DESC, id DESC);
CREATE INDEX IF NOT EXISTS ix_map_request_logs_operation_status
    ON map_request_logs(operation, result_status, created_at DESC, id DESC);

CREATE TABLE IF NOT EXISTS map_upstream_call_logs (
    id INTEGER PRIMARY KEY,
    map_request_log_id INTEGER NOT NULL REFERENCES map_request_logs(id) ON DELETE CASCADE,
    call_type TEXT NOT NULL CHECK (call_type IN ('geocode', 'poi_search', 'poi_detail', 'driving', 'walking')),
    result_status TEXT NOT NULL CHECK (result_status IN ('success', 'error')),
    http_status INTEGER,
    latency_ms INTEGER CHECK (latency_ms IS NULL OR latency_ms >= 0),
    error_code INTEGER,
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_map_upstream_logs_request
    ON map_upstream_call_logs(map_request_log_id, id);
CREATE INDEX IF NOT EXISTS ix_map_upstream_logs_created
    ON map_upstream_call_logs(created_at);

CREATE TABLE IF NOT EXISTS map_cache_entries (
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
CREATE INDEX IF NOT EXISTS ix_map_cache_expiry ON map_cache_entries(expires_at, stale_until);

CREATE TABLE IF NOT EXISTS pile_status_events (
    id INTEGER PRIMARY KEY,
    pile_id INTEGER NOT NULL REFERENCES charging_piles(id) ON DELETE CASCADE,
    station_id INTEGER NOT NULL REFERENCES stations(id) ON DELETE CASCADE,
    from_status TEXT NOT NULL CHECK (from_status IN ('idle', 'reserved', 'charging', 'fault', 'offline')),
    to_status TEXT NOT NULL CHECK (to_status IN ('idle', 'reserved', 'charging', 'fault', 'offline')),
    source TEXT NOT NULL CHECK (source IN ('business', 'simulation', 'admin', 'seed')),
    reason TEXT,
    tick_id INTEGER,
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_pile_status_events_pile_created
    ON pile_status_events(pile_id, created_at DESC, id DESC);
CREATE INDEX IF NOT EXISTS ix_pile_status_events_created ON pile_status_events(created_at);

CREATE TABLE IF NOT EXISTS simulation_state (
    station_id INTEGER PRIMARY KEY REFERENCES stations(id) ON DELETE CASCADE,
    seed_id TEXT NOT NULL CHECK (length(seed_id) BETWEEN 1 AND 128),
    last_tick_id INTEGER,
    last_run_at TEXT,
    snapshot_version INTEGER NOT NULL DEFAULT 0 CHECK (snapshot_version >= 0)
);

CREATE TABLE IF NOT EXISTS simulation_tick_records (
    simulator_id TEXT NOT NULL CHECK (length(simulator_id) BETWEEN 1 AND 128),
    tick_id INTEGER NOT NULL CHECK (tick_id >= 0),
    fingerprint TEXT NOT NULL CHECK (length(fingerprint) > 0),
    result_status TEXT NOT NULL CHECK (result_status IN ('accepted', 'conflict', 'rejected')),
    response_json TEXT NOT NULL CHECK (length(response_json) > 0),
    created_at TEXT NOT NULL,
    PRIMARY KEY (simulator_id, tick_id)
);
CREATE INDEX IF NOT EXISTS ix_simulation_tick_records_created ON simulation_tick_records(created_at);

CREATE INDEX IF NOT EXISTS ix_piles_station_status
    ON charging_piles(station_id, status);
CREATE INDEX IF NOT EXISTS ix_orders_user_status
    ON charging_orders(user_id, status, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_orders_pile_status
    ON charging_orders(pile_id, status, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_orders_ended_at
    ON charging_orders(ended_at);
CREATE INDEX IF NOT EXISTS ix_orders_settled_at
    ON charging_orders(settled_at);
CREATE INDEX IF NOT EXISTS ix_wallet_user_created
    ON wallet_transactions(user_id, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_restart_logs_pile_requested
    ON pile_restart_logs(pile_id, requested_at DESC);
CREATE INDEX IF NOT EXISTS ix_request_records_created
    ON request_records(created_at);

-- Legacy/demo seed scripts may omit the additive status timestamp; the empty
-- default is normalized by the seed and by the v0.3 -> v0.4 migration.

-- Read models used by admin-client/dashboard. They are derived from source tables.
CREATE VIEW IF NOT EXISTS station_pile_status AS
SELECT s.id AS station_id,
       s.name AS station_name,
       COUNT(p.id) AS pile_total,
       SUM(CASE WHEN p.status = 'idle' THEN 1 ELSE 0 END) AS pile_idle,
       SUM(CASE WHEN p.status = 'reserved' THEN 1 ELSE 0 END) AS pile_reserved,
       SUM(CASE WHEN p.status = 'charging' THEN 1 ELSE 0 END) AS pile_charging,
       SUM(CASE WHEN p.status = 'fault' THEN 1 ELSE 0 END) AS pile_fault,
       SUM(CASE WHEN p.status = 'offline' THEN 1 ELSE 0 END) AS pile_offline
FROM stations AS s
LEFT JOIN charging_piles AS p ON p.station_id = s.id
GROUP BY s.id, s.name;

CREATE VIEW IF NOT EXISTS revenue_daily AS
SELECT substr(settled_at, 1, 10) AS revenue_date,
       COUNT(*) AS completed_order_count,
       SUM(total_amount_cents) AS revenue_cents,
       SUM(energy_wh) AS energy_wh
FROM charging_orders
WHERE status = 'completed' AND settled_at IS NOT NULL
GROUP BY substr(settled_at, 1, 10);

COMMIT;
