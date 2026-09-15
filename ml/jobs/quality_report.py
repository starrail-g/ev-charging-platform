#!/usr/bin/env python3
"""Produce a lightweight, dependency-free report for a Schema v0.4 ODS batch."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_orders(path: Path) -> dict[str, object]:
    with path.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    ids = [row.get("id", "") for row in rows]
    duplicates = len(ids) - len(set(ids))
    invalid = {"NULL_REQUIRED": 0, "NEGATIVE_OR_RANGE": 0, "BAD_TIMESTAMP": 0}
    for row in rows:
        if not row.get("pile_id") or not row.get("user_id") or not row.get("order_no"):
            invalid["NULL_REQUIRED"] += 1
        try:
            energy = int(row.get("energy_wh", ""))
            if energy < 0 or int(row.get("total_amount_cents", "")) < 0:
                invalid["NEGATIVE_OR_RANGE"] += 1
        except (TypeError, ValueError):
            invalid["NEGATIVE_OR_RANGE"] += 1
        for key in ("created_at", "started_at", "ended_at", "settled_at"):
            try:
                datetime.strptime(row.get(key, ""), "%Y-%m-%dT%H:%M:%SZ")
            except (TypeError, ValueError):
                invalid["BAD_TIMESTAMP"] += 1
                break
    return {"rows": len(rows), "duplicate_order_ids": duplicates, "quality_counts": invalid, "sha256": sha256(path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "source": str(args.input),
        "charging_orders": inspect_orders(args.input / "charging_orders_raw.csv"),
        "raw_event_stream": str(args.input / "quality_events.jsonl"),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
