-- EV Charging Platform SQLite schema v0.1
-- The application must execute PRAGMA foreign_keys = ON for every connection.
-- Times are UTC ISO-8601 strings (for example 2026-09-01T08:00:00Z).
-- Money is an integer number of Chinese fen (CNY cents).

PRAGMA foreign_keys = ON;

BEGIN;

CREATE TABLE IF NOT EXISTS schema_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

INSERT OR IGNORE INTO schema_meta(key, value) VALUES ('schema_version', '0.1');

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
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS charging_piles (
    id INTEGER PRIMARY KEY,
    station_id INTEGER NOT NULL REFERENCES stations(id) ON DELETE CASCADE,
    pile_code TEXT NOT NULL CHECK (length(trim(pile_code)) > 0),
    pile_type TEXT NOT NULL CHECK (pile_type IN ('fast', 'slow')),
    power_kw REAL NOT NULL CHECK (power_kw > 0.0 AND power_kw <= 1000.0),
    unit_price_cents_per_kwh INTEGER NOT NULL
        CHECK (unit_price_cents_per_kwh > 0),
    status TEXT NOT NULL DEFAULT 'idle'
        CHECK (status IN ('idle', 'charging', 'fault', 'offline')),
    total_charge_count INTEGER NOT NULL DEFAULT 0 CHECK (total_charge_count >= 0),
    total_charge_seconds INTEGER NOT NULL DEFAULT 0 CHECK (total_charge_seconds >= 0),
    restart_count INTEGER NOT NULL DEFAULT 0 CHECK (restart_count >= 0),
    last_restart_at TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    UNIQUE (station_id, pile_code)
);

CREATE TABLE IF NOT EXISTS charging_orders (
    id INTEGER PRIMARY KEY,
    order_no TEXT NOT NULL UNIQUE CHECK (length(trim(order_no)) > 0),
    user_id INTEGER NOT NULL REFERENCES users(id),
    pile_id INTEGER NOT NULL REFERENCES charging_piles(id),
    status TEXT NOT NULL
        CHECK (status IN ('reserved', 'charging', 'pending_settlement',
                          'completed', 'cancelled')),
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
    CHECK (settled_at IS NULL OR status = 'completed')
);

-- A user and a pile can each have at most one active order. The service layer
-- still performs an explicit check so it can return a stable business error.
CREATE UNIQUE INDEX IF NOT EXISTS ux_orders_one_active_user
    ON charging_orders(user_id)
    WHERE status IN ('reserved', 'charging', 'pending_settlement');

CREATE UNIQUE INDEX IF NOT EXISTS ux_orders_one_active_pile
    ON charging_orders(pile_id)
    WHERE status IN ('reserved', 'charging', 'pending_settlement');

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
        OR transaction_type = 'adjustment')
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_wallet_one_charge_per_order
    ON wallet_transactions(order_id)
    WHERE transaction_type = 'charge' AND order_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS pile_restart_logs (
    id INTEGER PRIMARY KEY,
    pile_id INTEGER NOT NULL REFERENCES charging_piles(id),
    administrator_id INTEGER NOT NULL REFERENCES administrators(id),
    requested_at TEXT NOT NULL,
    result TEXT NOT NULL CHECK (result IN ('succeeded', 'rejected', 'failed')),
    reason TEXT
);

CREATE INDEX IF NOT EXISTS ix_piles_station_status
    ON charging_piles(station_id, status);
CREATE INDEX IF NOT EXISTS ix_orders_user_status
    ON charging_orders(user_id, status, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_orders_pile_status
    ON charging_orders(pile_id, status, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_orders_ended_at
    ON charging_orders(ended_at);
CREATE INDEX IF NOT EXISTS ix_wallet_user_created
    ON wallet_transactions(user_id, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_restart_logs_pile_requested
    ON pile_restart_logs(pile_id, requested_at DESC);

-- Read models used by admin-client/dashboard. They are derived from source tables.
CREATE VIEW IF NOT EXISTS station_pile_status AS
SELECT s.id AS station_id,
       s.name AS station_name,
       COUNT(p.id) AS pile_total,
       SUM(CASE WHEN p.status = 'idle' THEN 1 ELSE 0 END) AS pile_idle,
       SUM(CASE WHEN p.status = 'charging' THEN 1 ELSE 0 END) AS pile_charging,
       SUM(CASE WHEN p.status = 'fault' THEN 1 ELSE 0 END) AS pile_fault,
       SUM(CASE WHEN p.status = 'offline' THEN 1 ELSE 0 END) AS pile_offline
FROM stations AS s
LEFT JOIN charging_piles AS p ON p.station_id = s.id
GROUP BY s.id, s.name;

CREATE VIEW IF NOT EXISTS revenue_daily AS
SELECT substr(ended_at, 1, 10) AS revenue_date,
       COUNT(*) AS completed_order_count,
       SUM(total_amount_cents) AS revenue_cents,
       SUM(energy_wh) AS energy_wh
FROM charging_orders
WHERE status = 'completed' AND ended_at IS NOT NULL
GROUP BY substr(ended_at, 1, 10);

COMMIT;
