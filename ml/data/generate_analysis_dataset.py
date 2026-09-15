#!/usr/bin/env python3
"""Generate a deterministic analysis database and an auditable ODS export.

The generated SQLite database is an analysis copy of Schema v0.4. It starts
with the repository seed and adds only valid business rows. Quality problems
are emitted to the ODS event stream, never inserted into the business tables.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import random
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path


UTC = timezone.utc


@dataclass(frozen=True)
class Config:
    seed: int = 20260912
    days: int = 90
    users: int = 120
    stations: int = 12
    piles_per_station: int = 8
    orders: int = 25000
    end_date: str = "2026-09-17"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, default=Path("database/schema/schema.sql"))
    parser.add_argument("--seed-sql", type=Path, default=Path("database/seeds/dev.sql"))
    parser.add_argument("--output", type=Path, required=True, help="output SQLite database")
    parser.add_argument("--ods-dir", type=Path, required=True, help="output ODS directory")
    parser.add_argument("--seed", type=int, default=Config.seed)
    parser.add_argument("--days", type=int, default=Config.days)
    parser.add_argument("--users", type=int, default=Config.users)
    parser.add_argument("--stations", type=int, default=Config.stations)
    parser.add_argument("--piles-per-station", type=int, default=Config.piles_per_station)
    parser.add_argument("--orders", type=int, default=Config.orders)
    parser.add_argument("--end-date", default=Config.end_date, help="UTC end date, YYYY-MM-DD")
    return parser.parse_args()


def iso(value: datetime) -> str:
    return value.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_end_date(value: str) -> datetime:
    try:
        return datetime.strptime(value, "%Y-%m-%d").replace(tzinfo=UTC) + timedelta(days=1) - timedelta(seconds=1)
    except ValueError as exc:
        raise ValueError("--end-date must be YYYY-MM-DD") from exc


def ceil_div(numerator: int, denominator: int) -> int:
    return (numerator + denominator - 1) // denominator


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_config(config: Config) -> None:
    if min(config.days, config.users, config.stations, config.piles_per_station, config.orders) <= 0:
        raise ValueError("generation sizes must be positive")
    if config.stations > 900 or config.users > 90000 or config.orders > 500000:
        raise ValueError("generation size exceeds the deliberate safety limit")


def initialize_database(output: Path, schema: Path, seed_sql: Path) -> sqlite3.Connection:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()
    connection = sqlite3.connect(output)
    connection.execute("PRAGMA foreign_keys = ON")
    connection.executescript(schema.read_text(encoding="utf-8"))
    connection.executescript(seed_sql.read_text(encoding="utf-8"))
    return connection


def insert_generated_data(connection: sqlite3.Connection, config: Config) -> dict[str, int | str]:
    rng = random.Random(config.seed)
    end = parse_end_date(config.end_date)
    start = end - timedelta(days=config.days) + timedelta(seconds=1)

    # Keep generated IDs outside the stable development seed range.
    user_ids = list(range(1001, 1001 + config.users))
    station_ids = list(range(101, 101 + config.stations))
    pile_ids: list[int] = []
    pile_meta: dict[int, tuple[int, float, int]] = {}

    initial_balance = 100_000_000
    for index, user_id in enumerate(user_ids):
        created = start - timedelta(days=30 + (index % 10))
        connection.execute(
            "INSERT INTO users(id, phone, nickname, balance_cents, status, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, 'active', ?, ?)",
            (user_id, f"1{(3800000000 + user_id):010d}", f"分析用户{user_id}", initial_balance, iso(created), iso(end)),
        )
        connection.execute(
            "INSERT INTO wallet_transactions(user_id, transaction_type, amount_cents, balance_after_cents, "
            "idempotency_key, created_at) VALUES (?, 'recharge', ?, ?, ?, ?)",
            (user_id, initial_balance, initial_balance, f"analysis-recharge-{user_id}", iso(created + timedelta(minutes=1))),
        )

    for station_offset, station_id in enumerate(station_ids):
        latitude = 22.50 + (station_offset % 4) * 0.012 + (station_offset // 4) * 0.004
        longitude = 113.88 + (station_offset % 3) * 0.015 + (station_offset // 3) * 0.003
        status = "inactive" if station_offset == config.stations - 1 else "active"
        created = start - timedelta(days=15)
        connection.execute(
            "INSERT INTO stations(id, name, address, latitude, longitude, status, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            (station_id, f"分析站点{station_offset + 1:02d}", f"广东省深圳市南山区分析路{station_offset + 1}号",
             latitude, longitude, status, iso(created), iso(end)),
        )
        for pile_offset in range(config.piles_per_station):
            pile_id = station_id * 100 + pile_offset + 1
            pile_ids.append(pile_id)
            fast = pile_offset % 3 != 0
            power = 120.0 if fast else 7.0
            price = 120 + (station_offset % 4) * 5 if fast else 95 + (station_offset % 3) * 5
            pile_meta[pile_id] = (station_id, power, price)
            connection.execute(
                "INSERT INTO charging_piles(id, station_id, pile_code, pile_type, power_kw, "
                "unit_price_cents_per_kwh, status, created_at, updated_at, simulated, status_source, status_updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, 'idle', ?, ?, 0, 'seed', ?)",
                (pile_id, station_id, f"S{station_offset + 1:02d}-{pile_offset + 1:02d}",
                 "fast" if fast else "slow", power, price, iso(created), iso(end), iso(end)),
            )

    balances = {user_id: initial_balance for user_id in user_ids}
    order_base = 100_001
    wallet_base = 200_001
    valid_piles = [pile_id for pile_id in pile_ids if pile_meta[pile_id][0] != station_ids[-1]]
    for offset in range(config.orders):
        user_id = user_ids[offset % len(user_ids)]
        pile_id = valid_piles[rng.randrange(len(valid_piles))]
        station_id, power, price = pile_meta[pile_id]
        day = rng.randrange(config.days)
        hour = rng.choices([7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21],
                           weights=[2, 4, 7, 6, 4, 3, 3, 3, 3, 4, 6, 8, 9, 7, 4])[0]
        started = start + timedelta(days=day, hours=hour, minutes=rng.randrange(60))
        duration_minutes = rng.randrange(25, 121 if power > 10 else 181)
        ended = started + timedelta(minutes=duration_minutes)
        if ended > end - timedelta(minutes=2):
            ended = end - timedelta(minutes=2)
        # Preserve a realistic funnel instead of making every generated row a
        # successful settlement. Status is deterministic for a given seed:
        # completed 78%, cancelled 15%, exception 7%.
        draw = rng.random()
        status = "completed" if draw < 0.78 else "cancelled" if draw < 0.93 else "exception"
        energy_wh = int(round(power * (duration_minutes / 60.0) * rng.uniform(0.45, 0.82) * 1000))
        energy_wh = max(500, energy_wh)
        service_fee = 50 if offset % 5 == 0 else 0
        total = ceil_div(energy_wh * price, 1000) + service_fee
        if status == "cancelled":
            energy_wh = 0
            total = 0
            settled = None
        elif status == "exception":
            # An interrupted session has telemetry and a billable estimate,
            # but remains outside completed revenue until an operator settles it.
            total = max(1, total // 2)
            settled = None
        if status == "completed" and balances[user_id] < total:
            # The deterministic recharge is deliberately large, but keep this
            # guard so future parameter changes cannot produce invalid ledgers.
            raise ValueError(f"generated balance is insufficient for user {user_id}")
        order_id = order_base + offset
        wallet_id = wallet_base + offset
        reserved = started - timedelta(minutes=2)
        settled = ended + timedelta(minutes=2) if status == "completed" else None
        connection.execute(
            "INSERT INTO charging_orders(id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, "
            "energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?)",
            (order_id, f"ANALYSIS-{started:%Y%m%d}-{offset + 1:06d}", user_id, pile_id, "pending_settlement" if status == "completed" else status, iso(reserved), iso(started),
             iso(ended) if ended else None, energy_wh, price, service_fee, total,
             iso(reserved), iso(ended) if ended else iso(reserved)),
        )
        # The schema trigger requires a matching charge ledger for completed
        # rows. Non-completed rows intentionally have no charge transaction.
        if status == "completed":
            balances[user_id] -= total
            connection.execute(
                "INSERT INTO wallet_transactions(id, user_id, order_id, transaction_type, amount_cents, "
                "balance_after_cents, idempotency_key, created_at) VALUES (?, ?, ?, 'charge', ?, ?, ?, ?)",
                (wallet_id, user_id, order_id, -total, balances[user_id], f"analysis-charge-{order_id}", iso(settled)),
            )
            connection.execute(
                "UPDATE charging_orders SET status='completed', settled_at=?, updated_at=? WHERE id=?",
                (iso(settled), iso(settled), order_id),
            )

    for user_id, balance in balances.items():
        connection.execute("UPDATE users SET balance_cents=?, updated_at=? WHERE id=?", (balance, iso(end), user_id))

    connection.commit()
    return {
        "generation_seed": config.seed,
        "analysis_start": iso(start),
        "analysis_end": iso(end),
        "users_added": len(user_ids),
        "stations_added": len(station_ids),
        "piles_added": len(pile_ids),
        "piles_per_station": config.piles_per_station,
        "orders_added": config.orders,
    }


def validate_database(connection: sqlite3.Connection, metadata: dict[str, int | str]) -> dict[str, object]:
    checks = {
        "schema_version": connection.execute("SELECT value FROM schema_meta WHERE key='schema_version'").fetchone()[0],
        "foreign_key_check": connection.execute("PRAGMA foreign_key_check").fetchall(),
        "integrity_check": connection.execute("PRAGMA integrity_check").fetchone()[0],
        "generated_users": connection.execute("SELECT COUNT(*) FROM users WHERE id >= 1001").fetchone()[0],
        "generated_stations": connection.execute("SELECT COUNT(*) FROM stations WHERE id >= 101").fetchone()[0],
        "generated_piles": connection.execute("SELECT COUNT(*) FROM charging_piles WHERE id >= 10101").fetchone()[0],
        "generated_orders": connection.execute("SELECT COUNT(*) FROM charging_orders WHERE id >= 100001").fetchone()[0],
        "generated_completed_orders": connection.execute("SELECT COUNT(*) FROM charging_orders WHERE id >= 100001 AND status='completed'").fetchone()[0],
        "generated_charge_ledger": connection.execute("SELECT COUNT(*) FROM wallet_transactions WHERE id >= 200001 AND transaction_type='charge'").fetchone()[0],
        "revenue_cents": connection.execute("SELECT COALESCE(SUM(total_amount_cents), 0) FROM charging_orders WHERE id >= 100001 AND status='completed'").fetchone()[0],
    }
    if checks["schema_version"] != "0.4" or checks["foreign_key_check"] or checks["integrity_check"] != "ok":
        raise ValueError(f"generated database validation failed: {checks}")
    expected = {
        "generated_users": metadata["users_added"],
        "generated_stations": metadata["stations_added"],
        "generated_piles": metadata["piles_added"],
        "generated_orders": metadata["orders_added"],
        # The generated funnel intentionally contains cancelled/exception rows;
        # only completed rows participate in revenue and charge-ledger checks.
        "generated_completed_orders": checks["generated_completed_orders"],
        "generated_charge_ledger": checks["generated_charge_ledger"],
    }
    for key, value in expected.items():
        if checks[key] != value:
            raise ValueError(f"{key} expected {value}, got {checks[key]}")
    checks.pop("foreign_key_check")
    return checks


def export_ods(connection: sqlite3.Connection, ods_dir: Path, metadata: dict[str, int | str], database_hash: str) -> dict[str, object]:
    ods_dir.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, object] = {"metadata": metadata, "database_sha256": database_hash, "tables": {}}
    table_specs = {
        "users": "SELECT id, phone, nickname, balance_cents, status, created_at, updated_at FROM users ORDER BY id",
        "stations": "SELECT id, name, address, latitude, longitude, status, created_at, updated_at, provider FROM stations ORDER BY id",
        "charging_piles": "SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, total_charge_count, total_charge_seconds, created_at, updated_at, simulated, status_source, status_updated_at FROM charging_piles ORDER BY id",
        "charging_orders": "SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders ORDER BY id",
        "wallet_transactions": "SELECT id, user_id, order_id, transaction_type, amount_cents, balance_after_cents, idempotency_key, created_at FROM wallet_transactions ORDER BY id",
    }
    for table, query in table_specs.items():
        target = ods_dir / f"{table}.csv"
        cursor = connection.execute(query)
        columns = [description[0] for description in cursor.description]
        count = 0
        with target.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["source_table", "source_database_sha256", "generation_seed", "source_row_id", *columns])
            for row in cursor:
                writer.writerow([table, database_hash, metadata["generation_seed"], row[0], *row])
                count += 1
        manifest["tables"][table] = {"rows": count, "sha256": sha256_file(target), "path": str(target)}

    # Raw event stream deliberately contains quality defects. It is separate
    # from the valid business-table export and is the only input that may have
    # duplicate/invalid values before DWD validation.
    valid_orders = ods_dir / "charging_orders.csv"
    raw_orders = ods_dir / "charging_orders_raw.csv"
    with valid_orders.open("r", encoding="utf-8", newline="") as handle:
        order_rows = list(csv.reader(handle))
    header, rows = order_rows[0], order_rows[1:]
    by_id = {row[3]: row for row in rows}  # source_row_id is the fourth column
    raw_rows = [list(row) for row in rows]
    raw_rows.append(list(by_id["100001"]))
    for row in raw_rows:
        if row[3] == "100002":
            row[7] = ""
        elif row[3] == "100003":
            row[12] = "-1"
        elif row[3] == "100004":
            row[10] = "not-a-timestamp"
    with raw_orders.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        writer.writerows(raw_rows)
    manifest["raw_event_stream"] = {"rows": len(raw_rows), "sha256": sha256_file(raw_orders), "path": str(raw_orders)}

    events = ods_dir / "quality_events.jsonl"
    injected = [
        {"quality_code": "DUPLICATE_ORDER", "source_table": "charging_orders", "source_row_id": 100001, "action": "duplicate first generated order"},
        {"quality_code": "NULL_REQUIRED", "source_table": "charging_orders", "source_row_id": 100002, "field": "pile_id", "action": "set null in raw event only"},
        {"quality_code": "NEGATIVE_OR_RANGE", "source_table": "charging_orders", "source_row_id": 100003, "field": "energy_wh", "value": -1, "action": "negative energy in raw event only"},
        {"quality_code": "BAD_TIMESTAMP", "source_table": "charging_orders", "source_row_id": 100004, "field": "started_at", "value": "not-a-timestamp", "action": "invalid timestamp in raw event only"},
    ]
    with events.open("w", encoding="utf-8") as handle:
        for event in injected:
            event.update({"generation_seed": metadata["generation_seed"], "source_database_sha256": database_hash})
            handle.write(json.dumps(event, ensure_ascii=False, sort_keys=True) + "\n")
    manifest["quality_events"] = {"rows": len(injected), "sha256": sha256_file(events), "path": str(events)}
    (ods_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    args = parse_args()
    config = Config(seed=args.seed, days=args.days, users=args.users, stations=args.stations,
                    piles_per_station=args.piles_per_station, orders=args.orders, end_date=args.end_date)
    validate_config(config)
    connection = initialize_database(args.output, args.schema, args.seed_sql)
    try:
        metadata = insert_generated_data(connection, config)
        checks = validate_database(connection, metadata)
        database_hash = sha256_file(args.output)
        manifest = export_ods(connection, args.ods_dir, metadata, database_hash)
    finally:
        connection.close()
    result = {"database": str(args.output), "database_sha256": database_hash, "checks": checks, "manifest": manifest}
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
