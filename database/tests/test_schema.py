#!/usr/bin/env python3

import sqlite3
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = (ROOT / "database/schema/schema.sql").read_text(encoding="utf-8")
SEED = (ROOT / "database/seeds/dev.sql").read_text(encoding="utf-8")
MIGRATION = (
    ROOT / "database/migrations/001_v0.1_to_v0.2.sql"
).read_text(encoding="utf-8")

V01_FIXTURE = """
PRAGMA foreign_keys = ON;
CREATE TABLE schema_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
INSERT INTO schema_meta VALUES ('schema_version', '0.1');
CREATE TABLE users (
    id INTEGER PRIMARY KEY, phone TEXT NOT NULL, nickname TEXT NOT NULL,
    avatar_path TEXT, balance_cents INTEGER NOT NULL, status TEXT NOT NULL,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL
);
CREATE TABLE stations (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL, address TEXT NOT NULL,
    latitude REAL NOT NULL, longitude REAL NOT NULL, status TEXT NOT NULL,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL
);
CREATE TABLE charging_piles (
    id INTEGER PRIMARY KEY, station_id INTEGER NOT NULL, pile_code TEXT NOT NULL,
    pile_type TEXT NOT NULL, power_kw REAL NOT NULL,
    unit_price_cents_per_kwh INTEGER NOT NULL, status TEXT NOT NULL,
    total_charge_count INTEGER NOT NULL, total_charge_seconds INTEGER NOT NULL,
    restart_count INTEGER NOT NULL, last_restart_at TEXT,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL
);
CREATE TABLE charging_orders (
    id INTEGER PRIMARY KEY, order_no TEXT NOT NULL, user_id INTEGER NOT NULL,
    pile_id INTEGER NOT NULL, status TEXT NOT NULL, reserved_at TEXT,
    started_at TEXT, ended_at TEXT, energy_wh INTEGER NOT NULL,
    unit_price_cents_per_kwh INTEGER NOT NULL,
    service_fee_cents INTEGER NOT NULL, total_amount_cents INTEGER NOT NULL,
    settled_at TEXT, created_at TEXT NOT NULL, updated_at TEXT NOT NULL
);
CREATE TABLE wallet_transactions (
    id INTEGER PRIMARY KEY, user_id INTEGER NOT NULL, order_id INTEGER,
    transaction_type TEXT NOT NULL, amount_cents INTEGER NOT NULL,
    balance_after_cents INTEGER NOT NULL, idempotency_key TEXT,
    created_at TEXT NOT NULL
);
INSERT INTO users VALUES
    (1, '13800138000', 'fixture', NULL, 900, 'active', 't0', 't0');
INSERT INTO stations VALUES
    (1, 'fixture', 'fixture', 0, 0, 'active', 't0', 't0');
INSERT INTO charging_piles VALUES
    (1, 1, 'P-1', 'fast', 60, 100, 'idle', 1, 3600, 0, NULL, 't0', 't1');
INSERT INTO charging_orders VALUES
    (1, 'ORDER-1', 1, 1, 'completed', 't0', '2026-08-31T23:00:00Z',
     '2026-08-31T23:30:00Z', 10000, 100, 0, 1000,
     '2026-09-01T00:01:00Z', 't0', 't1');
INSERT INTO wallet_transactions VALUES
    (1, 1, 1, 'charge', -1000, 900, 'charge-1', '2026-09-01T00:01:00Z');
"""


class SchemaV02Test(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.db.executescript(SCHEMA)
        self.db.executescript(SEED)

    def tearDown(self):
        self.db.close()

    def assert_rejected(self, sql):
        with self.assertRaises(sqlite3.IntegrityError):
            self.db.execute(sql)
        self.db.rollback()

    def test_seed_is_repeatable_and_covers_required_states(self):
        self.db.executescript(SCHEMA)
        self.db.executescript(SEED)
        self.assertEqual(
            self.db.execute(
                "SELECT value FROM schema_meta WHERE key='schema_version'"
            ).fetchone(),
            ("0.2",),
        )
        self.assertEqual(
            {row[0] for row in self.db.execute(
                "SELECT DISTINCT status FROM charging_piles"
            )},
            {"idle", "reserved", "charging", "fault", "offline"},
        )
        order_states = {row[0] for row in self.db.execute(
            "SELECT DISTINCT status FROM charging_orders"
        )}
        self.assertTrue(
            {"pending_reservation", "reserved", "charging", "completed",
             "exception"}.issubset(order_states)
        )
        self.assertEqual(
            self.db.execute(
                "SELECT pile_reserved FROM station_pile_status WHERE station_id=2"
            ).fetchone(),
            (2,),
        )

    def test_revenue_uses_settlement_date(self):
        self.assertEqual(
            self.db.execute(
                "SELECT revenue_date, revenue_cents FROM revenue_daily"
            ).fetchall(),
            [("2026-08-31", 3050)],
        )

    def test_incomplete_settlement_rows_are_rejected(self):
        self.assert_rejected("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status,
                 unit_price_cents_per_kwh, created_at, updated_at)
            VALUES (9100, 'BAD-RESERVATION', 3, 202, 'pending_reservation',
                    95, 't', 't')
        """)
        self.assert_rejected("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status, reserved_at,
                 started_at, unit_price_cents_per_kwh, created_at, updated_at)
            VALUES (9099, 'BAD-RESERVED-TIME', 3, 202, 'reserved', 't0',
                    't1', 95, 't', 't')
        """)
        self.assert_rejected("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status,
                 unit_price_cents_per_kwh, created_at, updated_at)
            VALUES (9098, 'BAD-CHARGING-TIME', 3, 202, 'charging',
                    95, 't', 't')
        """)
        self.assert_rejected("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status,
                 unit_price_cents_per_kwh, created_at, updated_at)
            VALUES (9101, 'BAD-COMPLETED', 3, 202, 'completed', 95, 't', 't')
        """)
        self.assert_rejected("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status,
                 unit_price_cents_per_kwh, created_at, updated_at)
            VALUES (9102, 'BAD-PENDING', 3, 202, 'pending_settlement', 95, 't', 't')
        """)
        self.assert_rejected("""
            INSERT INTO wallet_transactions
                (id, user_id, transaction_type, amount_cents,
                 balance_after_cents, created_at)
            VALUES (9103, 3, 'charge', -1, 0, 't')
        """)
        self.assert_rejected("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status, started_at, ended_at,
                 energy_wh, unit_price_cents_per_kwh, total_amount_cents,
                 settled_at, created_at, updated_at)
            VALUES (9104, 'NO-LEDGER', 3, 202, 'completed', 't1', 't2',
                    1, 95, 1, 't3', 't0', 't3')
        """)

    def test_completed_charge_ledger_is_protected(self):
        self.assert_rejected(
            "DELETE FROM wallet_transactions WHERE id=5002"
        )
        self.assert_rejected("""
            UPDATE wallet_transactions SET amount_cents=-1 WHERE id=5002
        """)


class MigrationTest(unittest.TestCase):
    def run_migration(self, database):
        return subprocess.run(
            [
                "python3",
                str(ROOT / "scripts/migrate_db.py"),
                str(database),
                str(ROOT / "database/migrations/001_v0.1_to_v0.2.sql"),
            ],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_v01_data_is_preserved_and_constrained(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "legacy.sqlite"
            db = sqlite3.connect(database)
            db.executescript(V01_FIXTURE)
            db.close()

            result = self.run_migration(database)
            self.assertEqual(result.returncode, 0, result.stderr)

            db = sqlite3.connect(database)
            self.addCleanup(db.close)
            self.assertEqual(
                db.execute(
                    "SELECT value FROM schema_meta WHERE key='schema_version'"
                ).fetchone(),
                ("0.2",),
            )
            self.assertEqual(
                db.execute(
                    "SELECT name FROM sqlite_master "
                    "WHERE type='table' AND name='request_records'"
                ).fetchone(),
                ("request_records",),
            )
            self.assertEqual(db.execute(
                "SELECT COUNT(*) FROM charging_orders"
            ).fetchone(), (1,))
            self.assertEqual(
                db.execute(
                    "SELECT revenue_date, revenue_cents FROM revenue_daily"
                ).fetchall(),
                [("2026-09-01", 1000)],
            )
            self.assertEqual(
                db.execute("PRAGMA foreign_key_check").fetchall(), []
            )
            db.execute("UPDATE charging_piles SET status='reserved' WHERE id=1")

    def test_migration_copy_failure_preserves_legacy_tables(self):
        """A failed INSERT into rebuilt tables must not drop v0.1 data."""
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "legacy.sqlite"
            db = sqlite3.connect(database)
            db.executescript(V01_FIXTURE)
            # This value is legal in the v0.1 table but rejected by the v0.2
            # CHECK constraint while the migration copies charging_piles.
            db.execute("UPDATE charging_piles SET status='invalid' WHERE id=1")
            db.commit()
            db.close()

            result = self.run_migration(database)
            self.assertNotEqual(result.returncode, 0)

            db = sqlite3.connect(database)
            self.addCleanup(db.close)
            self.assertEqual(
                db.execute(
                    "SELECT value FROM schema_meta WHERE key='schema_version'"
                ).fetchone(),
                ("0.1",),
            )
            self.assertEqual(
                db.execute("SELECT status FROM charging_piles WHERE id=1").fetchone(),
                ("invalid",),
            )
            self.assertEqual(
                db.execute("SELECT COUNT(*) FROM charging_orders").fetchone(),
                (1,),
            )
            self.assertIsNone(
                db.execute(
                    "SELECT name FROM sqlite_master "
                    "WHERE type='table' AND name='request_records'"
                ).fetchone()
            )

    def test_migration_rejects_completed_order_without_ledger(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "legacy.sqlite"
            db = sqlite3.connect(database)
            db.executescript(V01_FIXTURE)
            db.execute("DELETE FROM wallet_transactions")
            db.commit()
            db.close()

            result = self.run_migration(database)
            self.assertNotEqual(result.returncode, 0)

            db = sqlite3.connect(database)
            self.addCleanup(db.close)
            self.assertEqual(
                db.execute(
                    "SELECT value FROM schema_meta WHERE key='schema_version'"
                ).fetchone(),
                ("0.1",),
            )
            self.assertEqual(
                db.execute("SELECT COUNT(*) FROM charging_orders").fetchone(),
                (1,),
            )
            self.assertEqual(
                db.execute("SELECT COUNT(*) FROM charging_piles").fetchone(),
                (1,),
            )


if __name__ == "__main__":
    unittest.main()
