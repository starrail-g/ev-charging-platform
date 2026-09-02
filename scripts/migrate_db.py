#!/usr/bin/env python3
"""Apply one SQLite migration atomically and stop on the first error."""

import argparse
import sqlite3
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("migration", type=Path)
    args = parser.parse_args()

    connection = sqlite3.connect(args.database)
    try:
        connection.execute("PRAGMA foreign_keys = OFF")
        connection.executescript(args.migration.read_text(encoding="utf-8"))
        version = connection.execute(
            "SELECT value FROM schema_meta WHERE key='schema_version'"
        ).fetchone()
        if version != ("0.2",):
            raise sqlite3.IntegrityError(
                f"migration did not set schema version 0.2: {version!r}"
            )
        violations = connection.execute("PRAGMA foreign_key_check").fetchall()
        if violations:
            raise sqlite3.IntegrityError(
                f"foreign key validation failed: {violations!r}"
            )
        connection.commit()
        connection.execute("PRAGMA foreign_keys = ON")
    except (OSError, sqlite3.Error) as error:
        connection.rollback()
        print(f"migration failed; rolled back: {error}", file=sys.stderr)
        return 1
    finally:
        connection.close()
    print("migration applied successfully")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
