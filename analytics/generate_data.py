#!/usr/bin/env python3
"""第二阶段模拟数据生成器（确定性、纯标准库）。

用法:
  python3 analytics/generate_data.py --profile smoke --seed 20260914 \
      --batch-id s2-smoke-20260915 --out-root ~/ev-stage2-artifacts

产物: <out-root>/batches/<batch-id>/
  manifest.json         批次元数据（行数、sha256、污染统计、代码 commit）
  input/<table>.csv     五张源表 ODS 原始记录（全部字符串列 + 信封列）
  dirty_labels.jsonl    污染真值标签（不送入规则判断）

设计要点（与 docs/architecture/analytics.md 一致）:
- 先生成合法底稿（单桩区间不重叠、单用户活动订单唯一、completed 计费公式一致且有匹配 charge 流水），
  再注入 DQ01-DQ12 受控污染；同 seed 重跑业务内容逐字一致（generated_at 除外）。
- 每条注入记录只命中一条规则（touched 集合防止多规则互相叠加导致真值歧义）；
  清洗侧由关联级联产生的额外隔离（如孤儿扣款）不在标签内，属预期行为。
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import random
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

UTC = timezone.utc
SCHEMA_VERSION = "0.4"
GENERATOR_VERSION = "1.0.0"
RULES_VERSION = "dq-v1"
SOURCE_TYPE = "synthetic_warehouse"

PROFILES = {
    # 站/桩/用户/订单 + 窗口（UTC，[start, end_exclusive)）
    "smoke": dict(stations=2, piles_per_station=6, users=100, orders=1000,
                  data_start="2026-09-01", data_end_exclusive="2026-09-15"),
    "standard": dict(stations=30, piles_per_station=10, users=5000, orders=50000,
                     data_start="2026-06-16", data_end_exclusive="2026-09-14"),
    "low": dict(stations=10, piles_per_station=10, users=500, orders=10000,
                data_start="2026-06-16", data_end_exclusive="2026-09-14"),
}

TABLE_COLUMNS = {
    "users": ["id", "phone", "nickname", "balance_cents", "status", "created_at", "updated_at"],
    "stations": ["id", "name", "address", "latitude", "longitude", "status", "created_at", "updated_at"],
    "charging_piles": ["id", "station_id", "pile_code", "pile_type", "power_kw",
                       "unit_price_cents_per_kwh", "status", "total_charge_count",
                       "total_charge_seconds", "created_at", "updated_at"],
    "charging_orders": ["id", "order_no", "user_id", "pile_id", "status", "reserved_at",
                        "started_at", "ended_at", "energy_wh", "unit_price_cents_per_kwh",
                        "service_fee_cents", "total_amount_cents", "settled_at",
                        "created_at", "updated_at"],
    "wallet_transactions": ["id", "user_id", "order_id", "transaction_type", "amount_cents",
                            "balance_after_cents", "created_at"],
}

ORDERS_RULE_FRACTION = 0.008   # 订单类规则正例数 r = max(3, round(K * fraction))
SMALL_RULE_FRACTION = 0.02     # 小表类规则正例数 r = max(3, round(N * fraction))

PLACE_NAMES = [
    "南山科技园", "福田中心区", "罗湖口岸", "宝安中心", "龙华清湖", "龙岗大运",
    "前海自贸区", "光明科学城", "坪山高铁站", "盐田港", "西丽大学城", "坂田华为",
    "车公庙枢纽", "深圳湾公园", "华侨城创意园", "蛇口海上世界", "坂田五和", "布吉关",
    "民治文体中心", "观澜湖", "沙井中心", "松岗街道", "福永码头", "石岩水库",
    "大浪时尚小镇", "平湖物流园", "横岗茂盛世居", "葵涌街道", "南澳海滨", "深汕合作区",
    "梅林关", "华强北商圈", "皇岗口岸", "莲塘口岸", "南头古城", "新安街道",
]
ADDRESS_ROADS = ["科技南路", "福华三路", "春风路", "创业一路", "龙华大道", "龙翔大道",
                 "听海大道", "光侨路", "站前路", "明珠大道", "学苑大道", "五和大道"]

PILE_POWER_FAST = [60.0, 120.0, 150.0]
PILE_POWER_SLOW = [7.0, 11.0, 22.0]
PILE_PRICES = [90, 100, 120, 150, 180]


def iso(dt: datetime) -> str:
    return dt.astimezone(UTC).replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_iso(s: str) -> datetime:
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)


def parse_date(s: str) -> datetime:
    return datetime.strptime(s, "%Y-%m-%d").replace(tzinfo=UTC)


def billing_cents(energy_wh: int, price_cents: int, service_fee_cents: int) -> int:
    return (energy_wh * price_cents + 999) // 1000 + service_fee_cents


def git_commit(repo_root: Path) -> str:
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(repo_root),
                              capture_output=True, text=True, timeout=10,
                              check=False).stdout.strip() or "unknown"
    except Exception:  # noqa: BLE001
        return "unknown"


class Generator:
    def __init__(self, profile: str, seed: int, batch_id: str, out_root: Path, repo_root: Path):
        assert profile in PROFILES, f"unknown profile {profile}"
        self.prof = PROFILES[profile]
        self.profile_name = profile
        self.seed = seed
        self.batch_id = batch_id
        self.out_root = out_root
        self.repo_root = repo_root
        self.rng = random.Random(seed)
        self.t0 = parse_date(self.prof["data_start"])
        self.t1 = parse_date(self.prof["data_end_exclusive"])
        self.window_seconds = int((self.t1 - self.t0).total_seconds())

        self.users: list[dict] = []
        self.stations: list[dict] = []
        self.piles: list[dict] = []
        self.orders: list[dict] = []
        self.wallet: list[dict] = []

        self.labels: list[dict] = []
        self.collection_gaps: list[dict] = []
        self._touched: set[int] = set()   # id(row)，防止同一记录被多条规则叠加
        self._order_seq = 0

    # ------------------------------------------------------------------ 基础数据
    def gen_users(self) -> None:
        n = self.prof["users"]
        prefixes = ["138", "139", "151", "158", "186"]
        for i in range(1, n + 1):
            created = self.t0 - timedelta(days=self.rng.randint(30, 360),
                                          seconds=self.rng.randint(0, 86399))
            updated = created + timedelta(days=self.rng.randint(0, 60),
                                          seconds=self.rng.randint(0, 86399))
            self.users.append({
                "id": i,
                "phone": prefixes[(i - 1) % len(prefixes)] + f"{(i - 1):08d}",
                "nickname": f"用户{i:05d}",
                "balance_cents": self.rng.randint(0, 30000),
                "status": "frozen" if self.rng.random() < 0.008 else "active",
                "created_at": iso(created),
                "updated_at": iso(updated),
            })

    def gen_stations(self) -> None:
        n = self.prof["stations"]
        for i in range(1, n + 1):
            place = PLACE_NAMES[(i - 1) % len(PLACE_NAMES)]
            road = ADDRESS_ROADS[(i - 1) % len(ADDRESS_ROADS)]
            created = self.t0 - timedelta(days=self.rng.randint(60, 500),
                                          seconds=self.rng.randint(0, 86399))
            updated = created + timedelta(days=self.rng.randint(0, 90))
            self.stations.append({
                "id": i,
                "name": f"{place}充电站",
                "address": f"深圳市{place[:2]}区{road}{self.rng.randint(1, 288)}号",
                "latitude": round(22.47 + self.rng.uniform(0.0, 0.14), 6),
                "longitude": round(113.85 + self.rng.uniform(0.0, 0.16), 6),
                "status": "inactive" if self.rng.random() < 0.10 else "active",
                "created_at": iso(created),
                "updated_at": iso(updated),
            })

    def gen_piles(self) -> None:
        m = self.prof["piles_per_station"]
        pid = 0
        for st in self.stations:
            for j in range(1, m + 1):
                pid += 1
                fast = j <= max(1, int(m * 0.6))
                power = self.rng.choice(PILE_POWER_FAST if fast else PILE_POWER_SLOW)
                r = self.rng.random()
                if r < 0.60:
                    status = "idle"
                elif r < 0.72:
                    status = "charging"
                elif r < 0.78:
                    status = "reserved"
                elif r < 0.89:
                    status = "fault"
                else:
                    status = "offline"
                created = self.t0 - timedelta(days=self.rng.randint(30, 400),
                                              seconds=self.rng.randint(0, 86399))
                self.piles.append({
                    "id": pid,
                    "station_id": st["id"],
                    "pile_code": f"P-{st['id']:02d}-{chr(64 + j)}",
                    "pile_type": "fast" if fast else "slow",
                    "power_kw": power,
                    "unit_price_cents_per_kwh": self.rng.choice(PILE_PRICES),
                    "status": status,
                    "total_charge_count": 0,   # 订单生成后回填
                    "total_charge_seconds": 0,
                    "created_at": iso(created),
                    "updated_at": iso(self.t1 - timedelta(days=self.rng.randint(0, 3),
                                                          seconds=self.rng.randint(0, 86399))),
                })

    # ------------------------------------------------------------------ 订单与流水
    def _new_order(self, pile: dict, status: str, reserved: datetime | None,
                   started: datetime | None, ended: datetime | None,
                   energy_wh: int, service_fee: int, amount: int,
                   settled: datetime | None, user_id: int) -> dict:
        self._order_seq += 1
        events = [x for x in (reserved, started, ended, settled) if x is not None]
        created = min(events) if events else self.t0 + timedelta(seconds=60)
        updated = max(events) if events else created
        return {
            "id": self._order_seq,
            "order_no": f"SO{self._order_seq:08d}",
            "user_id": user_id,
            "pile_id": pile["id"],
            "status": status,
            "reserved_at": iso(reserved) if reserved else "",
            "started_at": iso(started) if started else "",
            "ended_at": iso(ended) if ended else "",
            "energy_wh": energy_wh,
            "unit_price_cents_per_kwh": pile["unit_price_cents_per_kwh"],
            "service_fee_cents": service_fee,
            "total_amount_cents": amount,
            "settled_at": iso(settled) if settled else "",
            "created_at": iso(created),
            "updated_at": iso(updated),
        }

    def gen_orders(self) -> None:
        n_users = len(self.users)
        piles = self.piles
        k = self.prof["orders"]
        p = len(piles)
        per = [k // p] * p
        for i in range(k - (k // p) * p):
            per[i] += 1

        # 标准/低配：1 个站点有 2 天采集缺口（DQ12 缺测时段，manifest 记录，不冒充零）
        gap_station = None
        gap_start = gap_end = None
        if self.profile_name in ("standard", "low"):
            gap_station = min(3, len(self.stations))
            gap_start = self.t0 + timedelta(days=40)
            gap_end = gap_start + timedelta(days=2)
            self.collection_gaps.append({
                "station_id": gap_station,
                "gap_start": iso(gap_start),
                "gap_end_exclusive": iso(gap_end),
                "note": "模拟采集停摆 2 天；该窗口缺测，不得补零冒充",
            })

        new_active: list[dict] = []      # 新增的活动订单（charging / reserved / pending_reservation）
        modified_active: list[dict] = [] # 就地改造（pending_settlement）

        for idx, pile in enumerate(piles):
            count = per[idx]
            if count == 0:
                continue
            step = self.window_seconds / count
            max_dur = int(min(7200, max(600, step * 0.5)))
            open_last = self.rng.random() < 0.10
            st_gap = (gap_station is not None and pile["station_id"] == gap_station)

            for kk in range(count):
                slot0 = self.t0 + timedelta(seconds=kk * step)
                if st_gap and gap_start <= slot0 < gap_end:
                    continue  # 采集缺口：不出行
                is_last = kk == count - 1
                if is_last and open_last:
                    st_time = self.t1 - timedelta(seconds=self.rng.randint(600, 5400))
                    if st_gap and gap_start <= st_time < gap_end:
                        continue
                    reserved = st_time - timedelta(seconds=self.rng.randint(120, 3600))
                    o = self._new_order(pile, "charging", reserved, st_time, None,
                                        0, 0, 0, None, 0)
                    new_active.append(o)
                    continue

                dur = self.rng.randint(600, max_dur)
                start = slot0 + timedelta(seconds=self.rng.uniform(
                    0, max(0.0, step - dur - 300)))
                end = start + timedelta(seconds=dur)
                reserved = start - timedelta(seconds=self.rng.randint(120, 7200))

                r = self.rng.random()
                if r < 0.060:
                    o = self._new_order(pile, "cancelled", reserved, None, None,
                                        0, 0, 0, None, self.rng.randint(1, n_users))
                    self.orders.append(o)
                    continue
                if r < 0.075:
                    factor = self.rng.uniform(0.3, 0.8)
                    energy = max(1, int(pile["power_kw"] * 1000 * dur / 3600 * factor))
                    o = self._new_order(pile, "exception", reserved, start, end,
                                        energy, 0, 0, None, self.rng.randint(1, n_users))
                    self.orders.append(o)
                    continue

                factor = self.rng.uniform(0.45, 0.95)
                energy = max(1, int(pile["power_kw"] * 1000 * dur / 3600 * factor))
                fee = self.rng.choice([0, 100, 150, 200])
                amount = billing_cents(energy, pile["unit_price_cents_per_kwh"], fee)
                settled = end + timedelta(seconds=self.rng.randint(30, 1800))
                o = self._new_order(pile, "completed", reserved, start, end,
                                    energy, fee, amount, settled,
                                    self.rng.randint(1, n_users))
                self.orders.append(o)

        # 末端活动订单：reserved / pending_reservation（新增）
        used_piles: set[int] = set()

        def pick_free_pile() -> dict | None:
            cand = [pl for pl in piles if pl["id"] not in used_piles]
            if not cand:
                return None
            pl = self.rng.choice(cand)
            used_piles.add(pl["id"])
            return pl

        for _ in range(max(1, round(0.03 * p))):
            pl = pick_free_pile()
            if pl is None:
                break
            rt = self.t1 - timedelta(seconds=self.rng.randint(600, 21600))
            status = "reserved" if self.rng.random() < 0.5 else "pending_reservation"
            new_active.append(self._new_order(pl, status, rt, None, None, 0, 0, 0, None, 0))
        for _ in range(max(1, round(0.03 * p))):
            pl = pick_free_pile()
            if pl is None:
                break
            rt = self.t1 - timedelta(seconds=self.rng.randint(600, 21600))
            new_active.append(self._new_order(pl, "pending_reservation", rt, None, None,
                                              0, 0, 0, None, 0))

        # pending_settlement：就地改造已完成的末尾订单（保留区间/电量，去 settled）
        done = 0
        target = max(1, round(0.05 * p))
        for pl in sorted(piles, key=lambda x: x["id"]):
            if done >= target:
                break
            if pl["id"] in used_piles:
                continue
            pile_orders = [o for o in self.orders if o["pile_id"] == pl["id"]
                           and o["status"] == "completed"]
            if not pile_orders:
                continue
            o = pile_orders[-1]
            o["status"] = "pending_settlement"
            o["settled_at"] = ""
            o["total_amount_cents"] = billing_cents(
                o["energy_wh"], o["unit_price_cents_per_kwh"], o["service_fee_cents"])
            used_piles.add(pl["id"])
            modified_active.append(o)
            done += 1

        # 单用户活动订单约束：活动订单用户互不相同
        all_active = sorted(new_active + modified_active, key=lambda x: x["id"])
        active_users = self.rng.sample(range(1, n_users + 1),
                                       min(len(all_active), n_users))
        for o, uid in zip(all_active, active_users):
            o["user_id"] = uid

        self.orders.extend(new_active)
        self.orders.sort(key=lambda o: o["id"])

    def gen_wallet(self) -> None:
        """按用户模拟余额流水：completed 订单必有匹配 charge；余额不足前先充值。"""
        by_user: dict[int, list[dict]] = {u["id"]: [] for u in self.users}
        for o in self.orders:
            if o["status"] == "completed":
                by_user[o["user_id"]].append(o)

        txn_rows: list[tuple[int, str, int | None, int, int, datetime]] = []
        for u in self.users:
            uc = parse_iso(u["created_at"])
            n_recharge = self.rng.randint(1, 3)
            recharges = []
            for _ in range(n_recharge):
                rt = uc + timedelta(seconds=self.rng.uniform(
                    60, max(120, (self.t1 - uc).total_seconds() - 60)))
                recharges.append((rt, self.rng.randint(2000, 30000)))
            queue: list[tuple[datetime, str, object]] = [
                (rt, "recharge", amt) for rt, amt in recharges]
            for o in by_user[u["id"]]:
                queue.append((parse_iso(o["settled_at"]), "charge", o))
            queue.sort(key=lambda x: (x[0], 0 if x[1] == "recharge" else 1))

            balance = u["balance_cents"]
            for ts, kind, ref in queue:
                if kind == "recharge":
                    balance += int(ref)
                    txn_rows.append((u["id"], "recharge", None, int(ref), balance, ts))
                else:
                    o = ref
                    amount = int(o["total_amount_cents"])
                    if balance - amount < 0:
                        refill = (amount - balance) + self.rng.randint(1000, 20000)
                        ts2 = ts - timedelta(seconds=3600)
                        if ts2 < uc:
                            ts2 = uc + timedelta(seconds=60)
                        balance += refill
                        txn_rows.append((u["id"], "recharge", None, refill, balance, ts2))
                    balance -= amount
                    txn_rows.append((u["id"], "charge", o["id"], -amount, balance, ts))
            u["balance_cents"] = balance

        txn_rows.sort(key=lambda r: (r[5], r[0], 0 if r[1] == "recharge" else 1,
                                     r[2] if r[2] is not None else 0))
        for i, (uid, kind, oid, amount, bal_after, ts) in enumerate(txn_rows, start=1):
            self.wallet.append({
                "id": i,
                "user_id": uid,
                "order_id": oid if oid is not None else "",
                "transaction_type": kind,
                "amount_cents": amount,
                "balance_after_cents": bal_after,
                "created_at": iso(ts),
            })

    def backfill_pile_totals(self) -> None:
        agg: dict[int, tuple[int, int]] = {}
        for o in self.orders:
            if o["status"] != "completed":
                continue
            st = parse_iso(o["started_at"])
            en = parse_iso(o["ended_at"])
            cnt, secs = agg.get(o["pile_id"], (0, 0))
            agg[o["pile_id"]] = (cnt + 1, secs + int((en - st).total_seconds()))
        for pl in self.piles:
            cnt, secs = agg.get(pl["id"], (0, 0))
            pl["total_charge_count"] = cnt
            pl["total_charge_seconds"] = secs

    # ------------------------------------------------------------------ 污染注入
    def _pick(self, pool: list[dict], n: int) -> list[dict]:
        avail = [x for x in pool if id(x) not in self._touched]
        n = min(n, len(avail))
        picked = self.rng.sample(avail, n) if n else []
        for x in picked:
            self._touched.add(id(x))
        return picked

    def _label(self, table: str, row: dict, rule: str, field: str,
               original: str, polluted: str, action: str, note: str = "") -> None:
        self.labels.append({
            "_row": row, "_table": table, "rule": rule, "field": field,
            "original_value": str(original), "polluted_value": str(polluted),
            "action": action, "note": note,
        })

    def inject_pollution(self) -> None:
        rng = self.rng
        k = self.prof["orders"]
        r = max(3, round(k * ORDERS_RULE_FRACTION))
        half = max(1, r // 2)
        quarter = max(1, r // 4)
        r_st = max(3, round(len(self.stations) * SMALL_RULE_FRACTION))
        r_pl = max(3, round(len(self.piles) * SMALL_RULE_FRACTION))
        r_us = max(3, round(len(self.users) * SMALL_RULE_FRACTION))
        completed = [o for o in self.orders if o["status"] == "completed"]

        # DQ01 完全重复（订单）：原样复制追加；保留最小 source_record_id
        for o in self._pick(completed, r):
            d = dict(o)
            self.orders.append(d)
            self._touched.add(id(d))
            self._label("charging_orders", d, "DQ01", "*", o["order_no"],
                        f"exact copy of {o['order_no']}", "duplicate", "完全重复副本")

        # DQ02 同订单号内容冲突 → 整组隔离
        for o in self._pick(completed, r):
            d = dict(o)
            d["total_amount_cents"] = int(o["total_amount_cents"]) + rng.randint(100, 900)
            self.orders.append(d)
            self._touched.add(id(d))
            self._label("charging_orders", d, "DQ02", "total_amount_cents",
                        str(o["total_amount_cents"]), str(d["total_amount_cents"]),
                        "quarantine(group)", "同主键内容冲突，整组隔离")

        # DQ03 关键字段缺失（订单）：pile_id / user_id / total_amount / order_no
        fields = ["pile_id"] * quarter + ["user_id"] * quarter + \
                 ["total_amount_cents"] * quarter + ["order_no"] * quarter
        for o, field in zip(self._pick(completed, len(fields)), fields):
            orig = str(o[field])
            o[field] = ""
            self._label("charging_orders", o, "DQ03", field, orig, "", "quarantine",
                        "关键字段缺失")

        # DQ04 非关键缺失/空白 → 修复 "未知"
        for st in self._pick(self.stations, r_st):
            orig = st["address"]
            st["address"] = "   " if rng.random() < 0.5 else ""
            self._label("stations", st, "DQ04", "address", orig, st["address"],
                        "repair", "非关键描述缺失→未知")
        for u in self._pick(self.users, r_us):
            orig = u["nickname"]
            u["nickname"] = "  "
            self._label("users", u, "DQ04", "nickname", orig, u["nickname"],
                        "repair", "非关键描述缺失→未知")

        # DQ05 类型/单位污染 → 白名单修复
        for o in self._pick(completed, half):
            orig = str(o["energy_wh"])
            s = f"{int(orig):,}"
            if "," not in s:
                s = f"{s[0]},{s[1:]}"
            o["energy_wh"] = s
            self._label("charging_orders", o, "DQ05", "energy_wh", orig, s,
                        "repair", "千分位/分隔符污染")
        for o in self._pick(completed, half):
            orig = str(o["total_amount_cents"])
            o["total_amount_cents"] = "%.2f元" % (int(orig) / 100.0)
            self._label("charging_orders", o, "DQ05", "total_amount_cents", orig,
                        o["total_amount_cents"], "repair", "显式 unit=元 → 换算分")
        for pl in self._pick(self.piles, r_pl):
            orig = str(pl["power_kw"])
            pl["power_kw"] = f" {pl['power_kw']} "
            self._label("charging_piles", pl, "DQ05", "power_kw", orig,
                        pl["power_kw"], "repair", "去空格")

        # DQ06 状态码变体
        variants = ["COMPLETED"] * max(1, r // 4) + ["UNKNOWN_STATE"] * max(1, r // 4)
        for o, variant in zip(self._pick(completed, len(variants)), variants):
            orig = o["status"]
            o["status"] = variant
            action = "repair" if variant == "COMPLETED" else "quarantine"
            self._label("charging_orders", o, "DQ06", "status", orig, variant, action,
                        "显式字典映射" if action == "repair" else "未知状态隔离")
        for pl in self._pick(self.piles, r_pl):
            orig = pl["status"]
            pl["status"] = "IDLE"
            self._label("charging_piles", pl, "DQ06", "status", orig, "IDLE",
                        "repair", "大写状态→小写")

        # DQ07 时间格式/逻辑
        for o in self._pick(completed, quarter):
            orig = o["started_at"]
            o["started_at"] = orig.replace("T", " ").rstrip("Z")
            self._label("charging_orders", o, "DQ07", "started_at", orig,
                        o["started_at"], "repair", "声明格式解析（空格分隔）")
        for o in self._pick(completed, quarter):
            orig = o["started_at"]
            st = parse_iso(orig)
            o["started_at"] = (st + timedelta(hours=8)).strftime("%Y-%m-%dT%H:%M:%S+08:00")
            self._label("charging_orders", o, "DQ07", "started_at", orig,
                        o["started_at"], "repair", "带时区偏移 → 归一 UTC")
        for o in self._pick(completed, quarter):
            orig = o["ended_at"]
            st = parse_iso(o["started_at"])
            o["ended_at"] = iso(st - timedelta(seconds=600))
            self._label("charging_orders", o, "DQ07", "ended_at", orig,
                        o["ended_at"], "quarantine", "结束早于开始")
        for o in self._pick(completed, quarter):
            orig = o["settled_at"]
            en = parse_iso(o["ended_at"])
            o["settled_at"] = iso(en - timedelta(seconds=600))
            self._label("charging_orders", o, "DQ07", "settled_at", orig,
                        o["settled_at"], "quarantine", "结算早于结束")

        # DQ08 负值/超范围
        for o in self._pick(completed, quarter):
            orig = str(o["energy_wh"])
            o["energy_wh"] = -abs(int(orig))
            self._label("charging_orders", o, "DQ08", "energy_wh", orig,
                        str(o["energy_wh"]), "quarantine", "负电量")
        for st in self._pick(self.stations, min(3, r_st)):
            orig = str(st["latitude"])
            st["latitude"] = 200.0
            self._label("stations", st, "DQ08", "latitude", orig, "200.0",
                        "quarantine", "坐标越界")
        for pl in self._pick(self.piles, min(3, r_pl)):
            orig = str(pl["power_kw"])
            pl["power_kw"] = 0
            self._label("charging_piles", pl, "DQ08", "power_kw", orig, "0",
                        "quarantine", "功率≤0")

        # DQ09 外键缺失
        for o in self._pick(completed, quarter):
            orig = str(o["pile_id"])
            o["pile_id"] = 99999
            self._label("charging_orders", o, "DQ09", "pile_id", orig, "99999",
                        "quarantine", "桩外键缺失")
        for o in self._pick(completed, quarter):
            orig = str(o["user_id"])
            o["user_id"] = 99999
            self._label("charging_orders", o, "DQ09", "user_id", orig, "99999",
                        "quarantine", "用户外键缺失")
        # DQ09 外键缺失（钱包）：追加一笔孤儿扣款（order_id 指向不存在订单），
        # 不破坏既有完成订单的真实匹配流水（避免误伤级联）
        orphan_users = self._pick([dict(u) for u in self.users], min(3, r_st))
        for u in orphan_users:
            amount = -self.rng.randint(100, 5000)
            self.wallet.append({
                "id": 10 ** 7 + len(self.labels),   # 临时 id，写盘前统一重排
                "user_id": u["id"],
                "order_id": 99999,
                "transaction_type": "charge",
                "amount_cents": amount,
                "balance_after_cents": max(0, int(u["balance_cents"])),
                "created_at": iso(self.t1 - timedelta(seconds=self.rng.randint(3600, 86400))),
            })
            self._touched.add(id(self.wallet[-1]))
            self._label("wallet_transactions", self.wallet[-1], "DQ09", "order_id", "order",
                        "99999", "quarantine", "扣款→订单外键缺失（孤儿流水）")

        # DQ10 计费/流水不一致
        for o in self._pick(completed, quarter):
            orig = str(o["total_amount_cents"])
            o["total_amount_cents"] = int(orig) + 123
            self._label("charging_orders", o, "DQ10", "total_amount_cents", orig,
                        str(o["total_amount_cents"]), "quarantine",
                        "不满足向上取整计费公式")
        # completed 的扣款流水整行删除（源数据缺失该流水）→ 订单侧隔离
        del_targets = self._pick(completed, quarter)
        del_ids = {o["id"] for o in del_targets}
        self.wallet = [w for w in self.wallet
                       if not (w["transaction_type"] == "charge"
                               and w["order_id"] in del_ids)]
        for o in del_targets:
            self._label("charging_orders", o, "DQ10", "*", o["order_no"],
                        "missing charge txn", "quarantine", "completed 无匹配扣款流水")

        # DQ11 时间重叠 / 超物理上限
        overlap_src = self._pick(completed, quarter)
        for o in overlap_src:
            d = dict(o)
            st = parse_iso(d["started_at"])
            en = parse_iso(d["ended_at"])
            self._order_seq += 1
            d["id"] = self._order_seq
            d["order_no"] = f"SO{self._order_seq:08d}"
            d["reserved_at"] = iso(st - timedelta(seconds=600))
            d["started_at"] = iso(st + timedelta(seconds=300))
            d["ended_at"] = iso(en + timedelta(seconds=300))
            d["settled_at"] = iso(en + timedelta(seconds=900))
            self.orders.append(d)
            self._touched.add(id(d))
            self._label("charging_orders", d, "DQ11", "started_at", o["started_at"],
                        d["started_at"], "quarantine", "同桩时间重叠")
        for o in self._pick(completed, quarter):
            pl = next(x for x in self.piles if x["id"] == o["pile_id"])
            st = parse_iso(o["started_at"])
            en = parse_iso(o["ended_at"])
            # DQ05 可能已把该桩 power_kw 改写成带空格字符串（脏值），此处按数字语义还原
            load_max = float(pl["power_kw"]) * 1000 * (en - st).total_seconds() / 3600.0
            orig = str(o["energy_wh"])
            o["energy_wh"] = int(load_max * 1.5)
            o["total_amount_cents"] = billing_cents(
                o["energy_wh"], o["unit_price_cents_per_kwh"], o["service_fee_cents"])
            self._label("charging_orders", o, "DQ11", "energy_wh", orig,
                        str(o["energy_wh"]), "quarantine", "超出功率×时长上限 1.5 倍")

    # ------------------------------------------------------------------ 输出
    def write_outputs(self) -> dict:
        batch_dir = self.out_root / "batches" / self.batch_id
        input_dir = batch_dir / "input"
        input_dir.mkdir(parents=True, exist_ok=True)

        tables = {
            "users": self.users,
            "stations": self.stations,
            "charging_piles": self.piles,
            "charging_orders": self.orders,
            "wallet_transactions": self.wallet,
        }
        for rows in tables.values():
            rows.sort(key=lambda r: r["id"])   # 稳定排序：原行先于注入副本
        # 钱包 id 统一重排为最终行序（DQ09 孤儿流水注入使用过临时 id）
        for i, row in enumerate(tables["wallet_transactions"], start=1):
            row["id"] = i

        prefix = {"users": "users", "stations": "stations", "charging_piles": "piles",
                  "charging_orders": "orders", "wallet_transactions": "wallet"}
        row_to_srid: dict[int, str] = {}
        for key, rows in tables.items():
            for i, row in enumerate(rows, start=1):
                srid = f"{prefix[key]}-{i:06d}"
                row["_source_record_id"] = srid
                row_to_srid[id(row)] = srid

        table_meta = {}
        for key, rows in tables.items():
            path = input_dir / f"{key}.csv"
            header = ["source_record_id", "batch_id", "source_file", "source_line"] + \
                TABLE_COLUMNS[key]
            with open(path, "w", newline="", encoding="utf-8") as fh:
                writer = csv.writer(fh)
                writer.writerow(header)
                for line, row in enumerate(rows, start=2):
                    writer.writerow([row["_source_record_id"], self.batch_id,
                                     f"{key}.csv", line]
                                    + [row[c] for c in TABLE_COLUMNS[key]])
            sha = hashlib.sha256(path.read_bytes()).hexdigest()
            table_meta[key] = {"rows": len(rows), "sha256": sha, "file": f"input/{key}.csv"}

        labels_out = []
        for lb in self.labels:
            srid = row_to_srid.get(id(lb["_row"]))
            labels_out.append({
                "source_record_id": srid,
                "table": lb["_table"],
                "rule": lb["rule"],
                "field": lb["field"],
                "original_value": lb["original_value"],
                "polluted_value": lb["polluted_value"],
                "expected_action": lb["action"],
                "note": lb["note"],
            })
        labels_path = input_dir / "dirty_labels.jsonl"
        with open(labels_path, "w", encoding="utf-8") as fh:
            for lb in labels_out:
                fh.write(json.dumps(lb, ensure_ascii=False, sort_keys=True) + "\n")

        by_rule: dict[str, int] = {}
        polluted_srids: set[str] = set()
        for lb in labels_out:
            by_rule[lb["rule"]] = by_rule.get(lb["rule"], 0) + 1
            if lb["source_record_id"]:
                polluted_srids.add(lb["source_record_id"])
        total_records = sum(len(v) for v in tables.values())

        manifest = {
            "batch_id": self.batch_id,
            "seed": self.seed,
            "profile": self.profile_name,
            "schema_version": SCHEMA_VERSION,
            "source_type": SOURCE_TYPE,
            "timezone": "UTC",
            "data_start": iso(self.t0),
            "data_end_exclusive": iso(self.t1),
            "generated_at": iso(datetime.now(UTC)),
            "generator_version": GENERATOR_VERSION,
            "rules_version": RULES_VERSION,
            "code_commit": git_commit(self.repo_root),
            "tables": table_meta,
            "pollution": {
                "rule_count": len(by_rule),
                "injected_label_rows": len(labels_out),
                "distinct_polluted_records": len(polluted_srids),
                "rate": round(len(polluted_srids) / total_records, 6) if total_records else 0.0,
                "by_rule": dict(sorted(by_rule.items())),
                "labels_file": "input/dirty_labels.jsonl",
            },
            "collection_gaps": self.collection_gaps,
            "notes": [
                "污染标签为真值，不参与规则判断与模型特征",
                "DQ02 注入导致同主键整组隔离（含原始行），属预期行为",
                "清洗侧由关联级联产生的额外隔离（如被隔离订单的扣款流水）不在标签内",
            ],
        }
        manifest_path = batch_dir / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
        return manifest


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Stage2 deterministic data generator")
    ap.add_argument("--profile", required=True, choices=sorted(PROFILES))
    ap.add_argument("--seed", type=int, default=20260914)
    ap.add_argument("--batch-id", required=True)
    ap.add_argument("--out-root", default=str(Path.home() / "ev-stage2-artifacts"))
    ap.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    args = ap.parse_args(argv)

    out_root = Path(args.out_root).expanduser()
    g = Generator(args.profile, args.seed, args.batch_id, out_root, Path(args.repo_root))
    g.gen_users()
    g.gen_stations()
    g.gen_piles()
    g.gen_orders()
    g.gen_wallet()
    g.backfill_pile_totals()
    g.inject_pollution()
    manifest = g.write_outputs()

    print(f"generated batch {args.batch_id}: profile={args.profile} seed={args.seed}")
    for t, meta in manifest["tables"].items():
        print(f"  {t}: {meta['rows']} rows sha256={meta['sha256'][:16]}...")
    print(f"  pollution: {manifest['pollution']['distinct_polluted_records']} records "
          f"({manifest['pollution']['rate']:.4%}), labels={manifest['pollution']['injected_label_rows']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
