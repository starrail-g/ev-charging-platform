#!/usr/bin/env python3
"""Apply one SQLite migration atomically and stop on the first error."""

import argparse
import re
import sqlite3
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("migration", type=Path)
    parser.add_argument(
        "--expected-version",
        help="expected schema version (default: inferred from migration filename)",
    )
    args = parser.parse_args()
    expected_version = args.expected_version
    if expected_version is None:
        match = re.search(r"to_v(\d+\.\d+)", args.migration.name)
        expected_version = match.group(1) if match else "0.2"
    source_match = re.search(r"v(\d+\.\d+)_to_v\d+\.\d+", args.migration.name)
    source_version = source_match.group(1) if source_match else None

    connection = sqlite3.connect(args.database)
    try:
        connection.execute("PRAGMA foreign_keys = OFF")
        if source_version is not None:
            current_version = connection.execute(
                "SELECT value FROM schema_meta WHERE key='schema_version'"
            ).fetchone()
            if current_version != (source_version,):
                raise sqlite3.IntegrityError(
                    f"migration expects schema version {source_version}: {current_version!r}"
                )
        connection.executescript(args.migration.read_text(encoding="utf-8"))
        version = connection.execute(
            "SELECT value FROM schema_meta WHERE key='schema_version'"
        ).fetchone()
        if version != (expected_version,):
            raise sqlite3.IntegrityError(
                f"migration did not set schema version {expected_version}: {version!r}"
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
