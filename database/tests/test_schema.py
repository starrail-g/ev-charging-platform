#!/usr/bin/env python3

import sqlite3
import subprocess
import tempfile
import unittest
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = (ROOT / "database/schema/schema.sql").read_text(encoding="utf-8")
SEED = (ROOT / "database/seeds/dev.sql").read_text(encoding="utf-8")
MIGRATION = (
    ROOT / "database/migrations/001_v0.1_to_v0.2.sql"
).read_text(encoding="utf-8")
MIGRATION_V03 = (
    ROOT / "database/migrations/002_v0.2_to_v0.3.sql"
).read_text(encoding="utf-8")
V02_SCHEMA = SCHEMA.replace("schema_version', '0.3'", "schema_version', '0.2'")
V02_SCHEMA = V02_SCHEMA.replace(
    "WHERE status IN ('pending_reservation', 'reserved', 'charging');",
    "WHERE status IN ('pending_reservation', 'reserved', 'charging', 'pending_settlement');",
)
V02_SCHEMA = re.sub(
    r"\nCREATE TABLE IF NOT EXISTS request_records \(.*?\n\nCREATE INDEX IF NOT EXISTS ix_piles_station_status",
    "\nCREATE INDEX IF NOT EXISTS ix_piles_station_status",
    V02_SCHEMA,
    flags=re.DOTALL,
)
V02_SCHEMA = re.sub(
    r"\n    CHECK \(status NOT IN \('pending_reservation'.*?\n            AND total_amount_cents > 0\)\),",
    ",",
    V02_SCHEMA,
    flags=re.DOTALL,
)
V02_SCHEMA = re.sub(
    r"\nCREATE INDEX IF NOT EXISTS ix_request_records_created\n    ON request_records\(created_at\);",
    "",
    V02_SCHEMA,
)
V02_FIXTURE = V02_SCHEMA + "\n" + SEED

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


class SchemaV03Test(unittest.TestCase):
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
            ("0.3",),
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

    def test_pending_settlement_releases_pile_but_locks_user(self):
        self.db.execute("UPDATE charging_piles SET status='idle' WHERE id=202")
        self.db.execute("""
            INSERT INTO users
                (id, phone, nickname, balance_cents, status, created_at, updated_at)
            VALUES (10, '13000000010', 'test-user-10', 0, 'active', 't0', 't0'),
                   (11, '13000000011', 'test-user-11', 0, 'active', 't0', 't0')
        """)
        self.db.execute("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status, started_at, ended_at,
                 energy_wh, unit_price_cents_per_kwh, total_amount_cents,
                 created_at, updated_at)
            VALUES (9200, 'PENDING-SETTLEMENT', 10, 202, 'pending_settlement',
                    '2026-09-01T00:00:00Z', '2026-09-01T00:01:00Z',
                    1, 95, 1, 't0', 't1')
        """)
        # The pile index excludes pending_settlement, so a different user can
        # start a replacement session on the released pile.
        self.db.execute("""
            INSERT INTO charging_orders
                (id, order_no, user_id, pile_id, status, started_at,
                 unit_price_cents_per_kwh, created_at, updated_at)
            VALUES (9201, 'REPLACEMENT-CHARGING', 11, 202, 'charging',
                    '2026-09-01T00:02:00Z', 95, 't0', 't1')
        """)
        with self.assertRaises(sqlite3.IntegrityError):
            self.db.execute("""
                INSERT INTO charging_orders
                    (id, order_no, user_id, pile_id, status, started_at,
                     unit_price_cents_per_kwh, created_at, updated_at)
                VALUES (9202, 'SECOND-USER-10', 10, 101, 'charging',
                        '2026-09-01T00:03:00Z', 95, 't0', 't1')
            """)
        self.db.rollback()


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
            self.assertIsNone(db.execute(
                "SELECT name FROM sqlite_master WHERE type='table' AND name='request_records'"
            ).fetchone())
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
            indexes = {
                name: sql for name, sql in db.execute(
                    "SELECT name, sql FROM sqlite_master WHERE type='index' "
                    "AND name IN ('ux_orders_one_active_user', 'ux_orders_one_active_pile')"
                )
            }
            self.assertIn("'pending_settlement'", indexes["ux_orders_one_active_user"])
            self.assertIn("'pending_settlement'", indexes["ux_orders_one_active_pile"])
            db.execute("UPDATE charging_piles SET status='reserved' WHERE id=1")

    def test_deployed_v02_upgrades_to_v03_and_releases_pile_index(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "deployed-v02.sqlite"
            db = sqlite3.connect(database)
            db.executescript(V02_FIXTURE)
            db.close()

            result = subprocess.run(
                ["python3", str(ROOT / "scripts/migrate_db.py"), str(database),
                 str(ROOT / "database/migrations/002_v0.2_to_v0.3.sql")],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            db = sqlite3.connect(database)
            self.addCleanup(db.close)
            self.assertEqual(db.execute(
                "SELECT value FROM schema_meta WHERE key='schema_version'"
            ).fetchone(), ("0.3",))
            self.assertIsNotNone(db.execute(
                "SELECT name FROM sqlite_master WHERE type='table' AND name='request_records'"
            ).fetchone())
            db.execute("INSERT INTO users(id, phone, nickname, balance_cents, status, created_at, updated_at) VALUES (5, '13500135000', 'pending', 0, 'active', 't', 't')")
            db.execute("""
                INSERT INTO charging_orders
                    (id, order_no, user_id, pile_id, status, started_at, ended_at,
                     energy_wh, unit_price_cents_per_kwh, total_amount_cents,
                     created_at, updated_at)
                VALUES (2000, 'OLD-PENDING', 5, 101, 'pending_settlement',
                        '2026-09-01T00:00:00Z', '2026-09-01T00:30:00Z',
                        1000, 100, 100, 't', 't')
            """)
            db.commit()
            db.execute("INSERT INTO users(id, phone, nickname, balance_cents, status, created_at, updated_at) VALUES (6, '13400134000', 'new', 0, 'active', 't', 't')")
            db.execute("""
                INSERT INTO charging_orders
                    (id, order_no, user_id, pile_id, status, started_at,
                     unit_price_cents_per_kwh, created_at, updated_at)
                VALUES (2, 'NEW-ORDER', 6, 1, 'charging', '2026-09-01T01:00:00Z',
                        100, '2026-09-01T01:00:00Z', '2026-09-01T01:00:00Z')
            """)
            self.assertEqual(db.execute(
                "SELECT status FROM charging_orders WHERE id=2"
            ).fetchone(), ("charging",))

    def test_v03_migration_rolls_back_index_change_on_error(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "broken-v02.sqlite"
            db = sqlite3.connect(database)
            db.executescript(V02_FIXTURE)
            db.execute("INSERT INTO schema_meta(key, value) VALUES ('migration_guard', 'x')")
            db.commit()
            db.close()
            migration = Path(directory) / "002_v0.2_to_v0.3.sql"
            migration.write_text(MIGRATION_V03.replace(
                "UPDATE schema_meta SET value = '0.3' WHERE key = 'schema_version';",
                "INSERT INTO no_such_table VALUES (1);\nUPDATE schema_meta SET value = '0.3' WHERE key = 'schema_version';",
            ), encoding="utf-8")
            result = subprocess.run(
                ["python3", str(ROOT / "scripts/migrate_db.py"), str(database), str(migration)],
                capture_output=True, text=True, check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            db = sqlite3.connect(database)
            self.addCleanup(db.close)
            self.assertEqual(db.execute(
                "SELECT value FROM schema_meta WHERE key='schema_version'"
            ).fetchone(), ("0.2",))
            index_sql = db.execute(
                "SELECT sql FROM sqlite_master WHERE type='index' AND name='ux_orders_one_active_pile'"
            ).fetchone()[0]
            self.assertIn("'pending_settlement'", index_sql)

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
