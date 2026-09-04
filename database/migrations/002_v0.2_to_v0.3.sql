-- Upgrade the deployed EV Charging Platform SQLite schema v0.2 to v0.3.
-- The migration runner owns the transaction and rolls back on any error.

PRAGMA foreign_keys = OFF;
BEGIN IMMEDIATE;

-- v0.3 releases a pile as soon as charging.stop reaches pending_settlement.
DROP INDEX IF EXISTS ux_orders_one_active_pile;
CREATE UNIQUE INDEX ux_orders_one_active_pile
    ON charging_orders(pile_id)
    WHERE status IN ('pending_reservation', 'reserved', 'charging');

-- Request replay records are required by the v0.3 state-changing API.
CREATE TABLE IF NOT EXISTS request_records (
    request_id TEXT PRIMARY KEY CHECK (length(request_id) BETWEEN 1 AND 64),
    operation TEXT NOT NULL CHECK (length(operation) BETWEEN 1 AND 64),
    fingerprint TEXT NOT NULL CHECK (length(fingerprint) > 0),
    response_json TEXT NOT NULL CHECK (length(response_json) > 0),
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_request_records_created
    ON request_records(created_at);

-- SQLite cannot add CHECK constraints to an existing table in place. These
-- triggers provide the equivalent write-time protection without rewriting
-- deployed rows that were legal under v0.2.
CREATE TRIGGER IF NOT EXISTS trg_v03_order_state_insert
BEFORE INSERT ON charging_orders
WHEN (NEW.status IN ('pending_reservation', 'reserved')
      AND (NEW.reserved_at IS NULL OR NEW.started_at IS NOT NULL
           OR NEW.ended_at IS NOT NULL OR NEW.settled_at IS NOT NULL))
   OR (NEW.status = 'charging'
      AND (NEW.started_at IS NULL OR NEW.ended_at IS NOT NULL
           OR NEW.settled_at IS NOT NULL))
   OR (NEW.status = 'pending_settlement'
      AND (NEW.started_at IS NULL OR NEW.ended_at IS NULL
           OR NEW.settled_at IS NOT NULL OR NEW.total_amount_cents <= 0))
   OR (NEW.status = 'completed'
      AND (NEW.started_at IS NULL OR NEW.ended_at IS NULL
           OR NEW.settled_at IS NULL OR NEW.total_amount_cents <= 0))
BEGIN
    SELECT RAISE(ABORT, 'invalid v0.3 order state timestamps');
END;

CREATE TRIGGER IF NOT EXISTS trg_v03_order_state_update
BEFORE UPDATE OF status, reserved_at, started_at, ended_at,
    settled_at, total_amount_cents ON charging_orders
WHEN (NEW.status IN ('pending_reservation', 'reserved')
      AND (NEW.reserved_at IS NULL OR NEW.started_at IS NOT NULL
           OR NEW.ended_at IS NOT NULL OR NEW.settled_at IS NOT NULL))
   OR (NEW.status = 'charging'
      AND (NEW.started_at IS NULL OR NEW.ended_at IS NOT NULL
           OR NEW.settled_at IS NOT NULL))
   OR (NEW.status = 'pending_settlement'
      AND (NEW.started_at IS NULL OR NEW.ended_at IS NULL
           OR NEW.settled_at IS NOT NULL OR NEW.total_amount_cents <= 0))
   OR (NEW.status = 'completed'
      AND (NEW.started_at IS NULL OR NEW.ended_at IS NULL
           OR NEW.settled_at IS NULL OR NEW.total_amount_cents <= 0))
BEGIN
    SELECT RAISE(ABORT, 'invalid v0.3 order state timestamps');
END;

UPDATE schema_meta SET value = '0.3' WHERE key = 'schema_version';

-- The migration runner commits only after checking the resulting version and
-- foreign-key integrity. Do not add an explicit COMMIT here.
