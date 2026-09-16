"""Build filter-aware workbench metrics from published, cleaned ADS facts (UTC)."""
from collections import Counter, defaultdict
from datetime import datetime, timedelta


def build_workbench(data, filtered, start, end, station_id):
    facts = data.get("workbenchFacts")
    piles = filtered["piles"]
    ranks = filtered["stationRank"]
    names = {s["id"]: s["name"] for s in filtered["stations"]}
    recent_start = (datetime.fromisoformat(end) - timedelta(days=30)).date().isoformat()
    daily = [r for r in filtered["revenueDaily"] if r["date"] >= recent_start]
    amounts = [r["revenueCents"] for r in daily]
    result = {
        "version": 2,
        "scope": {"start": start, "end_exclusive": end, "station_id": station_id,
                  "equipment": "批次末设备快照，不随日期回溯",
                  "orders": "按创建日筛选；完成/取消为批次末状态",
                  "users": "按结算日筛选已完成订单；R 相对窗口末日（UTC）",
                  "energy": "沿用 ADS 按小时重叠分摊电量"},
        "equipment": {"status_counts": dict(Counter(p["status"] for p in piles)),
                      "type_counts": dict(Counter(p.get("type", "unknown") for p in piles)),
                      "simulated_count": None, "restart_count": None},
        "revenue": {"daily": [{"date": r["date"], "revenue_cents": r["revenueCents"]} for r in daily],
                    "total_30d_cents": sum(amounts),
                    "avg_daily_cents": sum(amounts) / len(amounts) if amounts else None},
        "stations": [{"station_id": r["stationId"], "name": names[r["stationId"]],
                      "revenue_cents": r["revenueCents"], "energy_kwh": r["energyWh"] / 1000,
                      "utilization": r["utilization"]} for r in ranks],
        "unavailable": {"equipment_mining": "本批次未发布设备异常评分",
                        "station_mining": "本批次未发布站点聚类",
                        "revenue_trend": "本批次未计算营收回归",
                        "duration_buckets": "本批次未发布履约时长分桶",
                        "service_control": "本批次未计算控制图",
                        "ratings": "源数据没有评价事实"},
    }
    hourly = [{"hour": h, "energy_kwh": 0.0, "order_count": None} for h in range(24)]
    for row in filtered["loadHourly"]:
        hourly[int(row["hourStart"][11:13])]["energy_kwh"] += row["allocatedWh"] / 1000
    energy = sum(row["energy_kwh"] for row in hourly)
    completed = filtered["overview"]["completedOrders"]
    peak = max(hourly, key=lambda row: row["energy_kwh"])
    result["energy"] = {
        "total_kwh": energy,
        "avg_session_kwh": filtered["overview"]["energyWh"] / 1000 / completed if completed else None,
        "peak_hour": peak["hour"] if energy else None,
        "peak_share": peak["energy_kwh"] / energy if energy else None,
        "order_energy_correlation": None, "hourly": hourly,
        "time_bands": [{"label": label, "energy_kwh": sum(hourly[h]["energy_kwh"] for h in hours)}
                       for label, hours in [("凌晨 00–05", range(6)), ("早高峰 07–09", range(7, 10)),
                                            ("午间 11–13", range(11, 14)), ("晚高峰 17–20", range(17, 21)),
                                            ("其余时段", [6, 10, 14, 15, 16, 21, 22, 23])]],
    }
    if facts is None:
        result["unavailable"]["users"] = "旧批次未发布用户分析事实，请重新构建数仓"
        result["unavailable"]["orders"] = "旧批次未发布订单分析事实，请重新构建数仓"
        return result

    def selected(row):
        return start <= row["stat_date"] < end and (station_id is None or row["station_id"] == station_id)

    users = {}
    for row in facts["users"]:
        if not selected(row):
            continue
        acc = users.setdefault(row["user_id"], {"user_id": row["user_id"], "frequency": 0,
                                               "monetary": 0, "last_settled_at": row["last_settled_at"]})
        acc["frequency"] += row["frequency"]
        acc["monetary"] += row["monetary"]
        acc["last_settled_at"] = max(acc["last_settled_at"], row["last_settled_at"])
    # 全网站点用清洗后的用户维表为分母；选站时用该站窗口内已完成订单用户为分母。
    total = facts["totalUsers"] if station_id is None else len(users)
    repeat = sum(u["frequency"] >= 2 for u in users.values())
    buckets = {"0 单": total - len(users), "1 单": 0, "2–4 单": 0, "5 单及以上": 0}
    for row in users.values():
        count = row["frequency"]
        buckets["1 单" if count == 1 else "2–4 单" if count < 5 else "5 单及以上"] += 1
        row["recency_days"] = (datetime.fromisoformat(end).date() - timedelta(days=1)
                               - datetime.fromisoformat(row.pop("last_settled_at").replace("Z", "+00:00")).date()).days
        row["segment"] = "高价值" if count >= 8 and row["monetary"] >= 100000 else "成长" if count >= 3 else "低频"
    segments = [{"label": label, "count": count} for label, count in buckets.items()]
    result["users"] = {"total": total, "repeat_users": repeat, "repeat_rate": repeat / total if total else None,
                       "segments": segments, "frequency_distribution": segments,
                       "denominator": "清洗后全网用户" if station_id is None else "所选站点窗口内消费用户"}
    result["user_mining"] = {"method": "RFM", "sample_count": len(users),
                             "segments": [{"segment": label, "count": sum(u["segment"] == label for u in users.values())}
                                          for label in ["高价值", "成长", "低频"]],
                             "top_users": sorted(users.values(), key=lambda u: (-u["monetary"], u["user_id"]))[:10]}
    counts = Counter()
    order_days = defaultdict(Counter)
    seconds = 0
    for row in hourly:
        row["order_count"] = 0
    for row in facts["orders"]:
        if not selected(row):
            continue
        n = row["order_count"]
        counts[row["status"]] += n
        order_days[row["stat_date"]][row["status"]] += n
        seconds += row["duration_seconds"] or 0
        if row["status"] == "completed" and row["start_hour"] is not None:
            hourly[row["start_hour"]]["order_count"] += n
    total_orders = sum(counts.values())
    done, cancelled = counts["completed"], counts["cancelled"]
    orders = {"total": total_orders, "completed": done, "cancelled": cancelled,
              "active": total_orders - done - cancelled,
              "completion_rate": done / total_orders if total_orders else None,
              "cancel_rate": cancelled / total_orders if total_orders else None,
              "avg_duration_minutes": seconds / done / 60 if done else None,
              "status_counts": [{"label": k, "count": v} for k, v in sorted(counts.items())],
              "daily": [{"date": date, "total": sum(c.values()), "completed": c["completed"],
                         "cancelled": c["cancelled"]} for date, c in sorted(order_days.items())]}
    result["orders"] = orders
    result["service"] = {"rating_available": False, "rating_note": "源数据未包含评价；显示订单代理指标",
                         "completion_rate": orders["completion_rate"], "cancel_rate": orders["cancel_rate"],
                         "repeat_user_rate": result["users"]["repeat_rate"],
                         "avg_duration_minutes": orders["avg_duration_minutes"]}
    return result
