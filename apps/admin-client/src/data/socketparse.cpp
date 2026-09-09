#include "socketparse.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>

#include <cmath>
#include <limits>

// wire(snake_case)→C 模型解析单点(设计稿 D4)。字段映射表与冻结状态见
// socketparse.h 头文件; B 冻结答复(2026-09-05, PR #10)已按实际构造点逐字段核对,
// 如需随服务端增补键只改本文件与头文件表格。

namespace ev {
namespace socketparse {

namespace {

// 每响应告警上限(防刷屏): 解析问题只记前几条, 由顶层函数统一 qWarning
constexpr int kMaxIssuesPerResponse = 3;

bool has(const QJsonObject &obj, const char *key)
{
    return obj.contains(QLatin1String(key));
}

// 限流记录: 每响应最多保留前 3 条(列表项批量缺失时防刷屏/防内存膨胀)
void logIssue(QStringList *issues, const QString &message)
{
    if (issues && issues->size() < kMaxIssuesPerResponse)
        issues->append(message);
}

// 缺字段/类型错 → 默认值 + 问题记录(required=true 时缺失也算问题;
// required=false 为可选键, 缺失不算问题, 供"缺省兜底"类字段使用)
qint64 readInt(const QJsonObject &obj, const char *key, bool required,
               QStringList *issues)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        if (required)
            logIssue(issues, QStringLiteral("字段缺失: %1").arg(QLatin1String(key)));
        return 0;
    }
    if (!value.isDouble()) {
        logIssue(issues, QStringLiteral("字段类型错误(应为整数): %1").arg(QLatin1String(key)));
        return 0;
    }
    return value.toInteger();
}

double readDouble(const QJsonObject &obj, const char *key, bool required,
                  QStringList *issues)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        if (required)
            logIssue(issues, QStringLiteral("字段缺失: %1").arg(QLatin1String(key)));
        return 0.0;
    }
    if (!value.isDouble()) {
        logIssue(issues, QStringLiteral("字段类型错误(应为数值): %1").arg(QLatin1String(key)));
        return 0.0;
    }
    return value.toDouble();
}

QString readString(const QJsonObject &obj, const char *key, bool required,
                   QStringList *issues)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        if (required)
            logIssue(issues, QStringLiteral("字段缺失: %1").arg(QLatin1String(key)));
        return QString();
    }
    if (!value.isString()) {
        logIssue(issues, QStringLiteral("字段类型错误(应为字符串): %1").arg(QLatin1String(key)));
        return QString();
    }
    return value.toString();
}

// 每响应问题限流: 超过 kMaxIssuesPerResponse 后丢弃
void emitIssues(const char *context, const QStringList &issues)
{
    if (issues.isEmpty())
        return;
    QStringList shown;
    for (int i = 0; i < qMin(issues.size(), kMaxIssuesPerResponse); ++i)
        shown << issues.at(i);
    qWarning().noquote() << "socketparse:" << context << "-"
                         << shown.join(QStringLiteral("; "));
}

} // namespace

PileInfo parsePile(const QJsonObject &obj, QStringList *issues)
{
    PileInfo pile;
    pile.id = int(readInt(obj, "id", true, issues));
    pile.stationId = int(readInt(obj, "station_id", true, issues));
    pile.pileCode = readString(obj, "pile_code", true, issues);
    pile.pileType = readString(obj, "pile_type", true, issues); // fast|slow, 展示用
    pile.powerKw = readDouble(obj, "power_kw", true, issues);
    // 金额一律整数分(protocol.md), 直取不换算
    pile.unitPriceCentsPerKwh = int(readInt(obj, "unit_price_cents_per_kwh", true, issues));
    // 桩五态 idle/reserved/charging/fault/offline; 未知值/缺失 → Unknown(可展示、不参与排序)
    const QString statusText = readString(obj, "status", true, issues);
    pile.status = parsePileStatus(statusText);
    pile.totalChargeCount = int(readInt(obj, "total_charge_count", true, issues));   // A-04
    pile.totalChargeSeconds = int(readInt(obj, "total_charge_seconds", true, issues)); // A-04
    // restart_count / last_restart_at: schema 有列但 C 模型无字段, 忽略(不告警)
    return pile;
}

StationInfo parseStation(const QJsonObject &obj, QStringList *issues)
{
    StationInfo station;
    station.id = int(readInt(obj, "id", true, issues));
    station.name = readString(obj, "name", true, issues);
    station.address = readString(obj, "address", true, issues);
    station.latitude = readDouble(obj, "latitude", true, issues);
    station.longitude = readDouble(obj, "longitude", true, issues);
    station.status = readString(obj, "status", true, issues); // active|inactive
    station.pileCount = int(readInt(obj, "pile_total", false, issues)); // 聚合列, 缺省 0
    // 在线桩数 = 非 故障/离线(schema station_pile_status view 与 mock 同口径, A-06):
    //   B listStations 聚合只有 idle/reserved/charging(无 fault/offline 列)时退化为求和;
    //   提供 pile_fault/pile_offline(视图全量口径)时用总数扣除。
    const bool hasFaultOffline = has(obj, "pile_fault") || has(obj, "pile_offline");
    if (hasFaultOffline) {
        const int fault = int(readInt(obj, "pile_fault", false, issues));
        const int offline = int(readInt(obj, "pile_offline", false, issues));
        station.onlinePileCount = qMax(0, station.pileCount - fault - offline);
    } else {
        const int idle = int(readInt(obj, "pile_idle", false, issues));
        const int reserved = int(readInt(obj, "pile_reserved", false, issues));
        const int charging = int(readInt(obj, "pile_charging", false, issues));
        station.onlinePileCount = qMax(0, idle + reserved + charging);
    }
    return station;
}

UserInfo parseUser(const QJsonObject &obj, QStringList *issues)
{
    UserInfo user;
    user.id = int(readInt(obj, "id", true, issues));
    user.phone = readString(obj, "phone", true, issues);
    user.nickname = readString(obj, "nickname", true, issues);
    user.balanceCents = int(readInt(obj, "balance_cents", true, issues)); // 整数分
    user.status = readString(obj, "status", true, issues);                // active|frozen
    // 注册时间 UTC ISO-8601 原串(A-07 用户表格; schema users.created_at)
    user.createdAt = readString(obj, "created_at", false, issues);
    // avatar_path / active_order_status: 模型无字段, 忽略
    return user;
}

OverviewStats parseStatistics(const QJsonObject &obj, QStringList *issues)
{
    OverviewStats stats;
    // range(7d|30d) 回声仅诊断用, 不参与解析(B 响应含 range 回声, 冻结 2026-09-05)
    // 营收: 优先 revenue_cents(该 range 合计, B 冻结口径 = 同 revenue_daily 序列和);
    // 缺失时对 revenue_daily 数组求和兜底(行内键 date/revenue_cents, 1f157de 冻结)
    if (has(obj, "revenue_cents")) {
        stats.revenueCents = readInt(obj, "revenue_cents", true, issues);
    } else {
        const QJsonValue daily = obj.value(QLatin1String("revenue_daily"));
        if (daily.isArray()) {
            qint64 sum = 0;
            const QJsonArray rows = daily.toArray();
            for (const QJsonValue &row : rows) {
                if (!row.isObject())
                    continue;
                sum += readInt(row.toObject(), "revenue_cents", false, issues);
            }
            stats.revenueCents = sum;
        } else {
            logIssue(issues, QStringLiteral("revenue_cents 与 revenue_daily 均缺失, 营收按 0"));
        }
    }
    // 近 30 日合计: B 无独立 30d 键(2026-09-05 冻结)——fetchOverview 发双请求(7d+30d),
    // 30d 响应解析出的 revenueCents 由调用方填入 revenue30dCents; 本兜底分支保留仅为
    // 防御未来服务端增补独立键(字段缺失时不影响单响应解析: revenue30dCents 保持 0)
    if (has(obj, "revenue30d_cents"))
        stats.revenue30dCents = readInt(obj, "revenue30d_cents", true, issues);
    // 桩五态计数(station_pile_status view 列名口径)
    stats.pileIdle = int(readInt(obj, "pile_idle", true, issues));
    stats.pileReserved = int(readInt(obj, "pile_reserved", true, issues));
    stats.pileCharging = int(readInt(obj, "pile_charging", true, issues));
    stats.pileFault = int(readInt(obj, "pile_fault", true, issues));
    stats.pileOffline = int(readInt(obj, "pile_offline", true, issues));
    // 站点平均利用率(冻结 2026-09-05, B 11702ae/4eb0bad 口径): 最近 7 个 UTC 自然日
    // 时间加权充电占用率(订单区间∩窗口/桩可用时长), 站均=简单平均, 与 range 无关,
    // 恒 0..1 小数(demo/mock 0.42 为演示占位口径, 语义不同); 展示乘 100
    double utilization = readDouble(obj, "avg_station_utilization", false, issues);
    if (utilization < 0.0 || utilization > 1.0) {
        logIssue(issues, QStringLiteral("avg_station_utilization 超出 0..1, 已截断"));
        utilization = qBound(0.0, utilization, 1.0);
    }
    stats.avgStationUtilization = utilization;
    // 快照时间 UTC ISO-8601 原串(冻结: 与站列表同整秒快照, 4eb0bad)
    stats.updatedAt = readString(obj, "updated_at", false, issues);
    return stats;
}

void parseAdmin(const QJsonObject &obj, AdminInfo *admin, QStringList *issues)
{
    if (!admin)
        return;
    admin->id = int(readInt(obj, "id", true, issues));
    admin->username = readString(obj, "username", true, issues);
    admin->role = readString(obj, "role", true, issues);       // operator|super_admin
    admin->status = readString(obj, "status", true, issues);   // active|disabled
}

// ── 顶层 payload 解析 ──────────────────────────────────────────────────────────

bool parsePilesPayload(const QJsonObject &payload, QList<PileInfo> *piles,
                       QStringList *issues, QString *reason)
{
    const QJsonValue value = payload.value(QLatin1String("piles"));
    if (!value.isArray()) {
        if (reason)
            *reason = QStringLiteral("响应 payload.piles 缺失或非数组");
        return false;
    }
    const QJsonArray rows = value.toArray();
    for (const QJsonValue &row : rows) {
        if (!row.isObject()) { // 坏列表项跳过, 不废整页(同"坏帧保留好帧"哲学)
            logIssue(issues, QStringLiteral("pile 列表含非对象项, 已跳过"));
            continue;
        }
        piles->append(parsePile(row.toObject(), issues));
    }
    emitIssues("parsePilesPayload", issues ? *issues : QStringList());
    return true;
}

bool parseStationsPayload(const QJsonObject &payload, QList<StationInfo> *stations,
                          QStringList *issues, QString *reason)
{
    const QJsonValue value = payload.value(QLatin1String("stations"));
    if (!value.isArray()) {
        if (reason)
            *reason = QStringLiteral("响应 payload.stations 缺失或非数组");
        return false;
    }
    const QJsonArray rows = value.toArray();
    for (const QJsonValue &row : rows) {
        if (!row.isObject()) {
            logIssue(issues, QStringLiteral("station 列表含非对象项, 已跳过"));
            continue;
        }
        stations->append(parseStation(row.toObject(), issues));
    }
    emitIssues("parseStationsPayload", issues ? *issues : QStringList());
    return true;
}

bool parseUsersPayload(const QJsonObject &payload, QList<UserInfo> *users,
                       QStringList *issues, QString *reason)
{
    const QJsonValue value = payload.value(QLatin1String("users"));
    if (!value.isArray()) {
        if (reason)
            *reason = QStringLiteral("响应 payload.users 缺失或非数组");
        return false;
    }
    const QJsonArray rows = value.toArray();
    for (const QJsonValue &row : rows) {
        if (!row.isObject()) {
            logIssue(issues, QStringLiteral("user 列表含非对象项, 已跳过"));
            continue;
        }
        users->append(parseUser(row.toObject(), issues));
    }
    emitIssues("parseUsersPayload", issues ? *issues : QStringList());
    return true;
}

bool parseStatisticsPayload(const QJsonObject &payload, OverviewStats *stats,
                            bool *hasData, QStringList *issues, QString *reason)
{
    const QJsonValue value = payload.value(QLatin1String("statistics"));
    if (!value.isObject()) {
        if (reason)
            *reason = QStringLiteral("响应 payload.statistics 缺失或非对象");
        return false;
    }
    const QJsonObject statistics = value.toObject();
    if (stats)
        *stats = parseStatistics(statistics, issues);
    // has_data(冻结 2026-09-07, main getStatistics): 服务端用该键区分空库(false)
    // 与"有数据但指标为 0"(true)。缺失/非布尔 → true + issue(契约漂移时维持旧
    // "服务端返回即视为有数据"语义, 不把有数据误判为空库)
    bool parsedHasData = true;
    const QJsonValue hasDataValue = statistics.value(QLatin1String("has_data"));
    if (hasDataValue.isBool()) {
        parsedHasData = hasDataValue.toBool();
    } else {
        logIssue(issues,
                 QStringLiteral("statistics.has_data 缺失或非布尔, 按 true 处理"));
    }
    if (hasData)
        *hasData = parsedHasData;
    emitIssues("parseStatisticsPayload", issues ? *issues : QStringList());
    return true;
}

RevenueSeries parseRevenueSeries(const QJsonObject &obj, const QString &expectedRange)
{
    RevenueSeries out;
    out.range = expectedRange;
    const auto fail = [&](const QString &message) {
        RevenueSeries bad;
        bad.range = expectedRange;
        bad.error = message;
        return bad;
    };
    const int count = expectedRange == QStringLiteral("7d") ? 7
                    : expectedRange == QStringLiteral("30d") ? 30 : 0;
    if (!count || obj.value(QStringLiteral("range")).toString() != expectedRange)
        return fail(QStringLiteral("营收时间范围不匹配"));
    out.updatedAt = obj.value(QStringLiteral("updated_at")).toString();
    const QDateTime snapshot = QDateTime::fromString(out.updatedAt, Qt::ISODate);
    if (!snapshot.isValid() || !out.updatedAt.endsWith(QLatin1Char('Z')))
        return fail(QStringLiteral("营收更新时间无效"));
    const auto readCents = [](const QJsonValue &value, qint64 *result) {
        // 与 JSON/图表可精确表示的整数范围保持一致；超范围显式报错。
        constexpr double maxExact = 9007199254740991.0;
        if (!value.isDouble()) return false;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < 0 || number > maxExact
            || std::floor(number) != number) return false;
        *result = value.toInteger(-1);
        return *result >= 0;
    };
    qint64 reported = 0;
    if (!readCents(obj.value(QStringLiteral("revenue_cents")), &reported))
        return fail(QStringLiteral("营收合计金额无效"));
    const QJsonValue daily = obj.value(QStringLiteral("revenue_daily"));
    if (!daily.isArray() || daily.toArray().size() != count)
        return fail(QStringLiteral("营收日期数量不完整"));
    const QDate first = snapshot.toUTC().date().addDays(1 - count);
    const auto rows = daily.toArray();
    for (int i = 0; i < count; ++i) {
        if (!rows.at(i).isObject())
            return fail(QStringLiteral("营收日数据格式错误"));
        const auto row = rows.at(i).toObject();
        const QString dateText = row.value(QStringLiteral("date")).toString();
        const QDate date = QDate::fromString(dateText, Qt::ISODate);
        qint64 cents = 0;
        if (!date.isValid() || date.toString(Qt::ISODate) != dateText
            || date != first.addDays(i))
            return fail(QStringLiteral("营收日期不连续或与快照不一致"));
        if (!readCents(row.value(QStringLiteral("revenue_cents")), &cents)
            || cents > std::numeric_limits<qint64>::max() - out.totalCents)
            return fail(QStringLiteral("营收日金额无效"));
        out.days.append({date, cents});
        out.totalCents += cents;
    }
    if (out.totalCents != reported)
        return fail(QStringLiteral("营收合计与逐日数据不一致"));
    out.available = true;
    return out;
}

bool parseAdminLoginPayload(const QJsonObject &payload, LoginResult *out,
                            QStringList *issues, QString *reason)
{
    const QJsonValue value = payload.value(QLatin1String("admin"));
    if (!value.isObject()) {
        if (reason)
            *reason = QStringLiteral("响应 payload.admin 缺失或非对象");
        return false;
    }
    // Q6 冻结(2026-09-06, PR #12 = main 3d015f7): admin.login.result 必须携带
    // 非空字符串 token; 缺失/类型错 = 响应结构错(客户端无 token 无法发起后续
    // admin.* 请求, 早失败比登录后全部 1100 更可诊断)。
    const QJsonValue tokenValue = payload.value(QLatin1String("token"));
    if (!tokenValue.isString() || tokenValue.toString().isEmpty()) {
        if (reason)
            *reason = QStringLiteral("响应 payload.token 缺失或非字符串(服务端会话契约不符)");
        return false;
    }
    if (out) {
        parseAdmin(value.toObject(), &out->admin, issues);
        out->token = tokenValue.toString();
        out->ok = true;
        out->errorCode = 0;
        out->networkError = false;
    }
    emitIssues("parseAdminLoginPayload", issues ? *issues : QStringList());
    return true;
}

int parseErrorCode(const QJsonObject &errorPayload, QStringList *issues)
{
    const QJsonValue value = errorPayload.value(QLatin1String("code"));
    if (value.isDouble())
        return int(value.toInteger());
    // 坏错误信封: 无 code 可分支 → 按请求类错误 1002 处理, 不冒充传输层错误
    logIssue(issues, QStringLiteral("error 信封缺 code, 按 1002 INVALID_REQUEST 处理"));
    return 1002;
}

} // namespace socketparse
} // namespace ev
