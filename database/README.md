# Database contract

`schema/schema.sql` is the SQLite v0.1 schema used by the server. It is
idempotent (`CREATE IF NOT EXISTS`) and records the schema version in
`schema_meta`.

Initialize an empty database and load deterministic demo data:

```sh
mkdir -p var
sqlite3 var/ev-charging.db < database/schema/schema.sql
sqlite3 var/ev-charging.db < database/seeds/dev.sql
```

The server must enable `PRAGMA foreign_keys = ON` on every new connection.
Times use UTC ISO-8601 text and monetary values use integer Chinese fen.
Business operations must use a transaction covering all related writes; see
`docs/architecture/database.md` for the required transaction boundaries.
