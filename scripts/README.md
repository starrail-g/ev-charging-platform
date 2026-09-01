# Project scripts

## Database migration

Apply a versioned SQLite migration with fail-fast, atomic execution:

```sh
python3 scripts/migrate_db.py var/ev-charging.db \
  database/migrations/001_v0.1_to_v0.2.sql
```

The runner commits only after the migration exits without an error, confirms
the expected schema version, and passes `PRAGMA foreign_key_check`. Any error
causes rollback and a non-zero exit status.
