#!/usr/bin/env python3
"""发布覆盖窗口计算：数仓 → 导出 → API 的窗口定义统一（PR #24 评审 P2）。

数仓按设计保留 settled_at ≥ 生成窗口末端的结算行（build_warehouse.grid_day/grid_hour 的
UNION 追加分支），导出也包含这些收入；但发布快照若继续只暴露生成窗口，API 默认查询会把
追加记录过滤掉、显式扩大窗口又被 400 out_of_coverage 拒绝——收入可查不可达。

本模块把“发布覆盖窗口”定义为：生成窗口 ∪ （实际数据日 + 1 天），右开区间。
导出层据此写入 meta.available_start / available_end_exclusive，API 以此作为默认可查询窗口，
前端筛选控件的 min/max 与“重置”也回到该窗口。
"""
from __future__ import annotations

from datetime import date, datetime, timedelta


def _as_date(value: str) -> date:
    return datetime.strptime(str(value)[:10], "%Y-%m-%d").date()


def coverage_end_exclusive(generation_end_iso: str, data_dates) -> str:
    """右开覆盖末端 'YYYY-MM-DD'：max(生成窗口末端, 各数据日 + 1 天)。

    data_dates 传 revenueDaily 的 stat_date 与 loadHourly 的 hour_start（任意 ISO 前缀均可）；
    None/空值跳过。生成窗口末端本身是下界——覆盖窗口不会小于生成窗口。
    """
    end = _as_date(generation_end_iso)
    for value in data_dates:
        if value is None or value == "":
            continue
        end = max(end, _as_date(value) + timedelta(days=1))
    return end.isoformat()
