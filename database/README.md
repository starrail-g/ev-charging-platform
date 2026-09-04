# Database contract

`schema/schema.sql` is the SQLite v0.3 schema used by the server. It is
idempotent (`CREATE IF NOT EXISTS`) and records the schema version in
`schema_meta`.

Initialize an empty database and load deterministic demo data:

```sh
mkdir -p var
sqlite3 var/ev-charging.db < database/schema/schema.sql
sqlite3 var/ev-charging.db < database/seeds/dev.sql
```

Upgrade an existing v0.1 database only after stopping the server and making a
backup:

```sh
python3 scripts/migrate_db.py var/ev-charging.db \
  database/migrations/001_v0.1_to_v0.2.sql
python3 -c 'import sqlite3; c=sqlite3.connect("var/ev-charging.db"); assert not c.execute("PRAGMA foreign_key_check").fetchall()'
```

For databases already deployed with v0.2, apply the v0.2 -> v0.3 migration
before starting the v0.3 server:

```sh
python3 scripts/migrate_db.py var/ev-charging.db \
  database/migrations/002_v0.2_to_v0.3.sql
```

The server must enable `PRAGMA foreign_keys = ON` on every new connection.
Times use UTC ISO-8601 text and monetary values use integer Chinese fen.
Business operations must use a transaction covering all related writes; see
`docs/architecture/database.md` for the required transaction boundaries.

`revenue_daily` reports revenue by `settled_at` (the financial settlement
time), not by the earlier physical charging end time. A completed order must
have `started_at`, `ended_at`, `settled_at`, and a persisted total; settlement
also creates exactly one negative `charge` ledger row for that order within
the same transaction. Database triggers reject completion without a matching
ledger row and protect the row after completion.
