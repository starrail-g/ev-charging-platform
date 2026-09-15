#!/usr/bin/env python3
"""发布材料完整性与哈希验证（不依赖 Spark）。

用法（VM）:
  python3 scripts/stage2/verify_release.py --analytics-root ~/ev-stage2-artifacts \
      --batch-id s2-smoke-20260915

校验: 批次目录文件齐全 / manifest 表哈希逐字对质 / 清洗守恒 / 发布快照结构 /
      latest 指针一致 / dashboard 汇总与发布数据自洽。退出码 0=PASS。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

OK = "PASS"
BAD = "FAIL"
issues: list[str] = []


def check(cond, name, detail=""):
    tag = OK if cond else BAD
    print(f"[{tag}] {name}{(' — ' + detail) if detail else ''}")
    if not cond:
        issues.append(name)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--analytics-root", required=True)
    ap.add_argument("--batch-id", required=True)
    args = ap.parse_args(argv)

    root = Path(args.analytics_root).expanduser()
    batch = root / "batches" / args.batch_id
    pub = root / "published" / args.batch_id

    # 1. 批次材料
    check(batch.is_dir(), "batch directory exists", str(batch))
    manifest_p = batch / "manifest.json"
    check(manifest_p.exists(), "manifest.json exists")
    if not manifest_p.exists():
        return finish()
    manifest = json.loads(manifest_p.read_text(encoding="utf-8"))
    check(manifest.get("batch_id") == args.batch_id, "manifest batch_id matches")

    # 2. 输入文件哈希逐字对质
    for table, meta in sorted(manifest.get("tables", {}).items()):
        f = batch / meta["file"]
        if not f.exists():
            check(False, f"input file {table}", "missing")
            continue
        digest = sha256(f)
        check(digest == meta["sha256"], f"sha256 {table}",
              "" if digest == meta["sha256"] else f"{digest[:12]} != {meta['sha256'][:12]}")
        rows = sum(1 for _ in open(f, encoding="utf-8")) - 1
        check(rows == meta["rows"], f"row count {table}", f"{rows} vs {meta['rows']}")

    # 3. 清洗守恒
    cr_p = batch / "quality" / "clean_report.json"
    check(cr_p.exists(), "clean_report.json exists")
    clean = json.loads(cr_p.read_text(encoding="utf-8")) if cr_p.exists() else {"tables": {}}
    for t, rep in sorted(clean.get("tables", {}).items()):
        check(rep.get("conservation_ok") is True, f"conservation {t}",
              f"in={rep.get('input')} keep={rep.get('kept')} dup={rep.get('duplicate')} "
              f"quar={rep.get('quarantine')}")
    for stage in ("profile_before", "profile_after"):
        check((batch / "quality" / f"{stage}.json").exists(), f"{stage}.json exists")

    # 4. 发布快照
    for name in ("dashboard.json", "quality.json", "manifest.json"):
        check((pub / name).exists(), f"published {args.batch_id}/{name}")
    latest_p = root / "published" / "latest.json"
    check(latest_p.exists(), "published/latest.json exists")
    if latest_p.exists():
        latest = json.loads(latest_p.read_text(encoding="utf-8"))
        check(latest.get("batch_id") == args.batch_id, "latest points to batch",
              f"latest={latest.get('batch_id')}")
    dash_p = pub / "dashboard.json"
    if dash_p.exists():
        dash = json.loads(dash_p.read_text(encoding="utf-8"))
        data = dash.get("data", {})
        check(dash.get("meta", {}).get("batch_id") == args.batch_id, "dashboard meta batch")
        rev_daily = sum(r["revenueCents"] for r in data.get("revenueDaily", []))
        rev_over = data.get("overview", {}).get("revenueCents")
        check(rev_daily == rev_over, "overview.revenueCents == Σ revenueDaily",
              f"{rev_over} vs {rev_daily}")
        check(bool(data.get("loadHourly")) or rev_over == 0, "loadHourly non-empty",
              f"rows={len(data.get('loadHourly', []))}")
        quality = data.get("quality", {})
        if quality:
            check(quality.get("input", 0) == quality.get("kept", 0) + quality.get("duplicate", 0)
                  + quality.get("quarantine", 0), "dashboard quality conservation")
    return finish()


def finish():
    if issues:
        print(f"\nVERIFY_RELEASE_FAIL ({len(issues)} issues)")
        return 1
    print("\nVERIFY_RELEASE_PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
