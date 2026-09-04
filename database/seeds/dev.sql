-- Deterministic development/demo data for schema v0.3.
PRAGMA foreign_keys = ON;
BEGIN;

INSERT OR IGNORE INTO administrators
    (id, username, password_hash_sha256, role, status, created_at, updated_at)
VALUES
    (1, 'admin',
     '8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92',
     'super_admin', 'active', '2026-09-01T00:00:00Z', '2026-09-01T00:00:00Z');

INSERT OR IGNORE INTO users
    (id, phone, nickname, balance_cents, status, created_at, updated_at)
VALUES
    (1, '13800138000', '用户8000', 16950, 'active',
     '2026-08-20T03:00:00Z', '2026-09-01T00:00:00Z'),
    (2, '13900139000', '用户9000', 5000, 'active',
     '2026-08-21T03:00:00Z', '2026-09-01T00:00:00Z'),
    (3, '13700137000', '演示冻结用户', 0, 'frozen',
     '2026-08-22T03:00:00Z', '2026-09-01T00:00:00Z'),
    (4, '13600136000', '演示预约用户', 10000, 'active',
     '2026-08-23T03:00:00Z', '2026-09-01T00:00:00Z');

INSERT OR IGNORE INTO stations
    (id, name, address, latitude, longitude, status, created_at, updated_at)
VALUES
    (1, '软件园一号站', '沈阳市浑南区软件园路1号', 41.7192, 123.4315,
     'active', '2026-08-20T01:00:00Z', '2026-09-01T00:00:00Z'),
    (2, '市府广场站', '沈阳市沈河区市府大路1号', 41.8057, 123.4290,
     'active', '2026-08-20T01:00:00Z', '2026-09-01T00:00:00Z');

INSERT OR IGNORE INTO charging_piles
    (id, station_id, pile_code, pile_type, power_kw,
     unit_price_cents_per_kwh, status, total_charge_count,
     total_charge_seconds, created_at, updated_at)
VALUES
    (101, 1, 'A-01', 'fast', 120.0, 120, 'idle', 18, 64800,
     '2026-08-20T01:10:00Z', '2026-09-01T00:00:00Z'),
    (102, 1, 'A-02', 'fast', 120.0, 120, 'charging', 21, 75600,
     '2026-08-20T01:10:00Z', '2026-09-01T00:00:00Z'),
    (103, 1, 'A-03', 'slow', 7.0, 100, 'fault', 11, 79200,
     '2026-08-20T01:10:00Z', '2026-09-01T00:00:00Z'),
    (201, 2, 'B-01', 'fast', 60.0, 135, 'reserved', 32, 115200,
     '2026-08-20T01:10:00Z', '2026-09-01T00:00:00Z'),
    (202, 2, 'B-02', 'slow', 7.0, 95, 'offline', 7, 50400,
     '2026-08-20T01:10:00Z', '2026-09-01T00:00:00Z'),
    (203, 2, 'B-03', 'fast', 60.0, 135, 'reserved', 4, 14400,
     '2026-08-20T01:10:00Z', '2026-09-01T00:00:00Z');

INSERT OR IGNORE INTO charging_orders
    (id, order_no, user_id, pile_id, status, reserved_at, started_at,
     ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents,
     total_amount_cents, settled_at, created_at, updated_at)
VALUES
    (1001, 'DEMO-20260831-0001', 1, 101, 'pending_settlement',
     '2026-08-31T05:00:00Z', '2026-08-31T05:02:00Z', '2026-08-31T06:02:00Z',
     25000, 120, 50, 3050, NULL,
     '2026-08-31T05:00:00Z', '2026-08-31T06:03:00Z'),
    (1002, 'DEMO-20260901-0001', 2, 102, 'charging',
     '2026-09-01T00:30:00Z', '2026-09-01T00:32:00Z', NULL,
     5000, 120, 0, 600, NULL,
     '2026-09-01T00:30:00Z', '2026-09-01T00:40:00Z'),
    (1003, 'DEMO-20260901-0002', 4, 203, 'reserved',
     '2026-09-01T00:50:00Z', NULL, NULL,
     0, 135, 50, 0, NULL,
     '2026-09-01T00:50:00Z', '2026-09-01T00:50:00Z'),
    (1004, 'DEMO-20260901-0003', 1, 201, 'pending_reservation',
     '2026-09-01T00:55:00Z', NULL, NULL,
     0, 135, 0, 0, NULL,
     '2026-09-01T00:55:00Z', '2026-09-01T00:55:00Z'),
    (1005, 'DEMO-20260901-0004', 3, 103, 'exception',
     '2026-08-31T07:00:00Z', NULL, NULL,
     0, 100, 0, 0, NULL,
     '2026-08-31T07:00:00Z', '2026-08-31T07:01:00Z');

INSERT OR IGNORE INTO wallet_transactions
    (id, user_id, order_id, transaction_type, amount_cents,
     balance_after_cents, idempotency_key, created_at)
VALUES
    (5001, 1, NULL, 'recharge', 20000, 20000, 'seed-recharge-u1',
     '2026-08-20T03:05:00Z'),
    (5002, 1, 1001, 'charge', -3050, 16950, 'seed-charge-1001',
     '2026-08-31T06:03:00Z'),
    (5003, 2, NULL, 'recharge', 5000, 5000, 'seed-recharge-u2',
     '2026-08-21T03:05:00Z'),
    (5004, 4, NULL, 'recharge', 10000, 10000, 'seed-recharge-u4',
     '2026-08-23T03:05:00Z');

UPDATE charging_orders
SET status = 'completed',
    settled_at = '2026-08-31T06:03:00Z',
    updated_at = '2026-08-31T06:03:00Z'
WHERE id = 1001 AND status = 'pending_settlement';

INSERT OR IGNORE INTO pile_restart_logs
    (id, pile_id, administrator_id, requested_at, result, reason)
VALUES
    (9001, 103, 1, '2026-08-30T04:00:00Z', 'succeeded', 'demo fault recovery');

COMMIT;
