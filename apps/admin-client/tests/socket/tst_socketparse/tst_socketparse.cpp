#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "data/socketparse.h"
#include "models/adminmodels.h"

// socketparse 纯函数数据驱动单测（设计稿 D4/D8）：
//   wire(snake_case) → C 模型；容错（缺字段/类型错/坏列表项）不崩不废整页；
//   未冻结字段按设计稿映射表（待 B 冻结校准 Q1-Q7，只改 socketparse 单点）。
class TestSocketParse : public QObject
{
    Q_OBJECT

private slots:
    void parsePileMapsAllFieldsAndUnknownStatus();
    void parsePileToleratesMissingAndBadTypes();
    void parseStationDerivesOnlineCount();
    void parseUserKeepsUtcCreatedAt();
    void parseStatisticsUsesSumFallbackAnd30dKey();
    void parseStatisticsPayloadMapsHasData();
    void payloadListsSkipBadItemsButFailOnStructure();
    void adminLoginPayloadAndErrorCode();
    void parseRevenueSeriesAcceptsValidSeries();
    void parseRevenueSeriesRejectsCorruption();
};

namespace {

QJsonObject pileObject(const QString &status = QStringLiteral("idle"))
{
    return QJsonObject{
        {QStringLiteral("id"), 3},
        {QStringLiteral("station_id"), 1},
        {QStringLiteral("pile_code"), QStringLiteral("P-101-C")},
        {QStringLiteral("pile_type"), QStringLiteral("slow")},
        {QStringLiteral("power_kw"), 60.0},
        {QStringLiteral("unit_price_cents_per_kwh"), 90},
        {QStringLiteral("status"), status},
        {QStringLiteral("total_charge_count"), 45},
        {QStringLiteral("total_charge_seconds"), 121500},
    };
}

// 独立构造合法响应(不复用被测解析函数生成预期值); 末日金额 12345, 其余 0
QJsonObject revenueFixture(const QString &range)
{
    const int count = range == QStringLiteral("7d") ? 7 : 30;
    const QDate end(2026, 9, 1);
    QJsonArray daily;
    for (int i = 0; i < count; ++i)
        daily.append(QJsonObject{
            {QStringLiteral("date"), end.addDays(i + 1 - count).toString(Qt::ISODate)},
            {QStringLiteral("revenue_cents"), i == count - 1 ? 12345 : 0}});
    return {{QStringLiteral("range"), range},
            {QStringLiteral("updated_at"), QStringLiteral("2026-09-01T10:15:00Z")},
            {QStringLiteral("revenue_cents"), 12345},
            {QStringLiteral("revenue_daily"), daily}};
}

void expectInvalidSeries(const char *label, QJsonObject statistics,
                         const QString &expectedRange)
{
    const ev::RevenueSeries bad =
        ev::socketparse::parseRevenueSeries(statistics, expectedRange);
    QVERIFY2(!bad.available, label);
    QVERIFY2(bad.days.isEmpty(), label);
    QVERIFY2(!bad.error.isEmpty(), label);
}

} // namespace

void TestSocketParse::parsePileMapsAllFieldsAndUnknownStatus()
{
    const ev::PileInfo pile = ev::socketparse::parsePile(pileObject(QStringLiteral("fault")));
    QCOMPARE(pile.id, 3);
    QCOMPARE(pile.stationId, 1);
    QCOMPARE(pile.pileCode, QStringLiteral("P-101-C"));
    QCOMPARE(pile.pileType, QStringLiteral("slow"));
    QCOMPARE(pile.powerKw, 60.0);
    QCOMPARE(pile.unitPriceCentsPerKwh, 90);
    QCOMPARE(pile.status, ev::PileStatus::Fault);
    QCOMPARE(pile.totalChargeCount, 45);
    QCOMPARE(pile.totalChargeSeconds, 121500);

    // 未知协议值 → Unknown（C-S1-019 语义，不崩）
    QCOMPARE(ev::socketparse::parsePile(pileObject(QStringLiteral("future-state"))).status,
             ev::PileStatus::Unknown);
}

void TestSocketParse::parsePileToleratesMissingAndBadTypes()
{
    QStringList issues;
    // 全缺 → 默认值不崩
    const ev::PileInfo empty = ev::socketparse::parsePile(QJsonObject{}, &issues);
    QCOMPARE(empty.id, 0);
    QCOMPARE(empty.status, ev::PileStatus::Unknown); // 缺 status → Unknown（可展示不参与排序）
    QVERIFY(empty.pileCode.isEmpty());

    issues.clear();
    // 类型错（字符串塞进数值列）→ 默认值 + issue，不崩
    QJsonObject bad = pileObject();
    bad.insert(QStringLiteral("power_kw"), QStringLiteral("not-a-number"));
    bad.insert(QStringLiteral("id"), QStringLiteral("oops"));
    const ev::PileInfo tolerated = ev::socketparse::parsePile(bad, &issues);
    QCOMPARE(tolerated.id, 0);
    QCOMPARE(tolerated.powerKw, 0.0);
    QVERIFY2(!issues.isEmpty(), "类型错应记录解析问题（限流前）");
}

void TestSocketParse::parseStationDerivesOnlineCount()
{
    // 视图全量口径：总数扣除 fault/offline（schema station_pile_status）
    QJsonObject station = QJsonObject{
        {QStringLiteral("id"), 1},
        {QStringLiteral("name"), QStringLiteral("东软园区充电站")},
        {QStringLiteral("address"), QStringLiteral("沈阳市浑南区东软软件园")},
        {QStringLiteral("latitude"), 41.7331},
        {QStringLiteral("longitude"), 123.4395},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("pile_total"), 3},
        {QStringLiteral("pile_fault"), 1},
        {QStringLiteral("pile_offline"), 0},
    };
    const ev::StationInfo full = ev::socketparse::parseStation(station);
    QCOMPARE(full.id, 1);
    QCOMPARE(full.pileCount, 3);
    QCOMPARE(full.onlinePileCount, 2); // 3 - 1 fault - 0 offline

    // 退化口径：B 聚合只给 idle/reserved/charging → 求和
    QJsonObject degraded = QJsonObject{
        {QStringLiteral("id"), 2},
        {QStringLiteral("pile_total"), 3},
        {QStringLiteral("pile_idle"), 1},
        {QStringLiteral("pile_reserved"), 0},
        {QStringLiteral("pile_charging"), 2},
    };
    const ev::StationInfo deg = ev::socketparse::parseStation(degraded);
    QCOMPARE(deg.pileCount, 3);
    QCOMPARE(deg.onlinePileCount, 3); // 1+0+2（无 fault/offline 列）
}

void TestSocketParse::parseUserKeepsUtcCreatedAt()
{
    const QJsonObject user = QJsonObject{
        {QStringLiteral("id"), 1},
        {QStringLiteral("phone"), QStringLiteral("13800138000")},
        {QStringLiteral("nickname"), QStringLiteral("用户8000")},
        {QStringLiteral("balance_cents"), 16950},
        {QStringLiteral("status"), QStringLiteral("frozen")},
        {QStringLiteral("created_at"), QStringLiteral("2026-08-15T03:24:00Z")},
    };
    const ev::UserInfo parsed = ev::socketparse::parseUser(user);
    QCOMPARE(parsed.id, 1);
    QCOMPARE(parsed.phone, QStringLiteral("13800138000"));
    QCOMPARE(parsed.balanceCents, 16950);
    QCOMPARE(parsed.status, QStringLiteral("frozen"));
    // UTC ISO-8601 原串（A-07 表格口径，不做本地化）
    QCOMPARE(parsed.createdAt, QStringLiteral("2026-08-15T03:24:00Z"));
}

void TestSocketParse::parseStatisticsUsesSumFallbackAnd30dKey()
{
    // 冻结口径(2026-09-05, B 1f157de): revenue_daily 行内键 date/revenue_cents;
    // 用例验证: 聚合键缺失时对数组求和兜底 + revenue30d_cents 防御兜底分支仍可用
    const QJsonArray daily = QJsonArray{
        QJsonObject{{QStringLiteral("date"), QStringLiteral("2026-08-26")},
                    {QStringLiteral("revenue_cents"), 35600}},
        QJsonObject{{QStringLiteral("date"), QStringLiteral("2026-08-27")},
                    {QStringLiteral("revenue_cents"), 41200}},
    };
    const QJsonObject statistics = QJsonObject{
        {QStringLiteral("revenue_daily"), daily},
        {QStringLiteral("revenue30d_cents"), 983840},
        {QStringLiteral("pile_idle"), 1},
        {QStringLiteral("pile_reserved"), 1},
        {QStringLiteral("pile_charging"), 2},
        {QStringLiteral("pile_fault"), 1},
        {QStringLiteral("pile_offline"), 1},
        {QStringLiteral("avg_station_utilization"), 0.42},
        {QStringLiteral("updated_at"), QStringLiteral("2026-09-01T10:15:00Z")},
    };
    const ev::OverviewStats stats = ev::socketparse::parseStatistics(statistics);
    QCOMPARE(stats.revenueCents, qint64(35600 + 41200)); // 求和兜底
    QCOMPARE(stats.revenue30dCents, qint64(983840));
    QCOMPARE(stats.pileIdle, 1);
    QCOMPARE(stats.pileCharging, 2);
    QCOMPARE(stats.pileFault, 1);
    QCOMPARE(stats.pileOffline, 1);
    QCOMPARE(stats.avgStationUtilization, 0.42);
    QCOMPARE(stats.updatedAt, QStringLiteral("2026-09-01T10:15:00Z"));

    // 直接给 revenue_cents 时优先它
    QJsonObject direct = statistics;
    direct.insert(QStringLiteral("revenue_cents"), 286540);
    QCOMPARE(ev::socketparse::parseStatistics(direct).revenueCents, qint64(286540));

    // 全缺 → 0 默认
    QCOMPARE(ev::socketparse::parseStatistics(QJsonObject{}).revenueCents, qint64(0));
}

void TestSocketParse::parseStatisticsPayloadMapsHasData()
{
    // has_data(冻结 2026-09-07, main getStatistics): 空库 false / 有数据 true;
    // 键缺失或类型错 → true + issue(契约漂移不把有数据误判为空库)
    QStringList issues;
    QString reason;
    bool hasData = false;

    // 缺失 → true(旧契约兼容)
    QJsonObject payload{
        {QStringLiteral("statistics"),
         QJsonObject{{QStringLiteral("revenue_cents"), 0},
                     {QStringLiteral("pile_idle"), 0}}}};
    QVERIFY(ev::socketparse::parseStatisticsPayload(payload, nullptr, &hasData,
                                                    &issues, &reason));
    QVERIFY(hasData);

    // has_data: true → true
    QJsonObject statistics{{QStringLiteral("has_data"), true},
                           {QStringLiteral("revenue_cents"), 0}};
    payload.insert(QStringLiteral("statistics"), statistics);
    issues.clear();
    QVERIFY(ev::socketparse::parseStatisticsPayload(payload, nullptr, &hasData,
                                                    &issues, &reason));
    QVERIFY(hasData);

    // has_data: false(空库)→ false
    statistics.insert(QStringLiteral("has_data"), false);
    payload.insert(QStringLiteral("statistics"), statistics);
    issues.clear();
    QVERIFY(ev::socketparse::parseStatisticsPayload(payload, nullptr, &hasData,
                                                    &issues, &reason));
    QVERIFY(!hasData);

    // 类型错(字符串塞布尔)→ true + issue
    statistics.insert(QStringLiteral("has_data"), QStringLiteral("yes"));
    payload.insert(QStringLiteral("statistics"), statistics);
    issues.clear();
    QVERIFY(ev::socketparse::parseStatisticsPayload(payload, nullptr, &hasData,
                                                    &issues, &reason));
    QVERIFY(hasData);
    QVERIFY2(!issues.isEmpty(), "类型错应记录解析问题");
}

void TestSocketParse::payloadListsSkipBadItemsButFailOnStructure()
{
    // 坏项（非对象）跳过，不废整页（同"坏帧保留好帧"哲学）
    const QJsonObject payload = QJsonObject{
        {QStringLiteral("piles"), QJsonArray{pileObject(), QJsonValue(42), pileObject()}}};
    QList<ev::PileInfo> piles;
    QString reason;
    QStringList issues;
    QVERIFY(ev::socketparse::parsePilesPayload(payload, &piles, &issues, &reason));
    QCOMPARE(piles.size(), 2); // 坏项被跳过
    QVERIFY2(!issues.isEmpty(), "跳过坏项应记录解析问题");

    // 结构错（缺数组键）→ false + reason（调用方按接口级失败处理）
    piles.clear();
    reason.clear();
    QVERIFY(!ev::socketparse::parsePilesPayload(QJsonObject{{QStringLiteral("items"),
                                                             QJsonArray{}}},
                                                &piles, &issues, &reason));
    QVERIFY(!reason.isEmpty());

    // users/stations 同构
    const QJsonObject usersPayload = QJsonObject{
        {QStringLiteral("users"), QJsonArray{QJsonObject{
             {QStringLiteral("id"), 1},
             {QStringLiteral("phone"), QStringLiteral("13800138000")}}}}};
    QList<ev::UserInfo> users;
    QVERIFY(ev::socketparse::parseUsersPayload(usersPayload, &users, &issues, &reason));
    QCOMPARE(users.size(), 1);
    QCOMPARE(users.first().phone, QStringLiteral("13800138000"));
}

void TestSocketParse::adminLoginPayloadAndErrorCode()
{
    // payload = admin 对象 + 会话 token（Q6 冻结 2026-09-06 契约）→ LoginResult
    const QJsonObject payload = QJsonObject{
        {QStringLiteral("admin"), QJsonObject{
             {QStringLiteral("id"), 1},
             {QStringLiteral("username"), QStringLiteral("admin")},
             {QStringLiteral("role"), QStringLiteral("super_admin")},
             {QStringLiteral("status"), QStringLiteral("active")},
         }},
        {QStringLiteral("token"), QStringLiteral("session-token-abc")},
        {QStringLiteral("expires_in_seconds"), 28800},
    };
    ev::LoginResult login;
    QString reason;
    QStringList issues;
    QVERIFY(ev::socketparse::parseAdminLoginPayload(payload, &login, &issues, &reason));
    QVERIFY(login.ok);
    QCOMPARE(login.errorCode, 0);
    QCOMPARE(login.admin.username, QStringLiteral("admin"));
    QCOMPARE(login.admin.role, QStringLiteral("super_admin"));
    QCOMPARE(login.token, QStringLiteral("session-token-abc")); // token 提取

    // 结构错（无 admin 对象）→ false
    QVERIFY(!ev::socketparse::parseAdminLoginPayload(
        QJsonObject{{QStringLiteral("admin"), QJsonValue(1)},
                    {QStringLiteral("token"), QStringLiteral("t")}},
        &login, &issues, &reason));

    // Q6: token 缺失/非字符串 = 响应结构错（客户端无法发起后续 admin.* 请求，
    // 早失败优于登录后全部 1100）
    QVERIFY(!ev::socketparse::parseAdminLoginPayload(
        QJsonObject{{QStringLiteral("admin"), QJsonObject{{QStringLiteral("id"), 1}}}},
        &login, &issues, &reason));
    QVERIFY(!ev::socketparse::parseAdminLoginPayload(
        QJsonObject{{QStringLiteral("admin"), QJsonObject{{QStringLiteral("id"), 1}}},
                    {QStringLiteral("token"), QJsonValue(42)}},
        &login, &issues, &reason));
    QVERIFY(reason.contains(QStringLiteral("token")));

    // error 信封 code 解析；缺失/类型错 → 1002（不冒充传输层错误）
    QCOMPARE(ev::socketparse::parseErrorCode(
                 QJsonObject{{QStringLiteral("code"), 1201}}),
             1201);
    QStringList errIssues;
    QCOMPARE(ev::socketparse::parseErrorCode(QJsonObject{}, &errIssues), 1002);
    QVERIFY(!errIssues.isEmpty());
}

void TestSocketParse::parseRevenueSeriesAcceptsValidSeries()
{
    // 7d：7 条、日期连续升序、末日 = updated_at 快照当日(2026-09-01)、合计一致
    const ev::RevenueSeries seven =
        ev::socketparse::parseRevenueSeries(revenueFixture(QStringLiteral("7d")),
                                            QStringLiteral("7d"));
    QVERIFY(seven.available);
    QVERIFY(seven.error.isEmpty());
    QCOMPARE(seven.range, QStringLiteral("7d"));
    QCOMPARE(seven.days.size(), 7);
    QCOMPARE(seven.days.first().date, QDate(2026, 8, 26));
    QCOMPARE(seven.days.last().date, QDate(2026, 9, 1));
    QCOMPARE(seven.totalCents, qint64(12345));
    QCOMPARE(seven.updatedAt, QStringLiteral("2026-09-01T10:15:00Z"));

    // 30d：30 条、首日 = 8/3（快照日 2026-09-01 前推 29 天）
    const ev::RevenueSeries thirty =
        ev::socketparse::parseRevenueSeries(revenueFixture(QStringLiteral("30d")),
                                            QStringLiteral("30d"));
    QVERIFY(thirty.available);
    QCOMPARE(thirty.days.size(), 30);
    QCOMPARE(thirty.days.first().date, QDate(2026, 8, 3));
    QCOMPARE(thirty.days.last().date, QDate(2026, 9, 1));
    QCOMPARE(thirty.totalCents, qint64(12345));

    // 全零合法：非"损坏"，是"无营收期间"（正常零线，不误报）
    QJsonObject zeros = revenueFixture(QStringLiteral("7d"));
    QJsonArray zeroRows;
    for (int i = 0; i < 7; ++i)
        zeroRows.append(QJsonObject{
            {QStringLiteral("date"), QDate(2026, 8, 26).addDays(i).toString(Qt::ISODate)},
            {QStringLiteral("revenue_cents"), 0}});
    zeros.insert(QStringLiteral("revenue_cents"), 0);
    zeros.insert(QStringLiteral("revenue_daily"), zeroRows);
    const ev::RevenueSeries zero =
        ev::socketparse::parseRevenueSeries(zeros, QStringLiteral("7d"));
    QVERIFY(zero.available);
    QCOMPARE(zero.totalCents, qint64(0));
}

void TestSocketParse::parseRevenueSeriesRejectsCorruption()
{
    const QString range7 = QStringLiteral("7d");
    const QJsonObject base = revenueFixture(range7);

    // range 回声不匹配(90d ≠ 期望 7d)
    QJsonObject wrongRange = base;
    wrongRange.insert(QStringLiteral("range"), QStringLiteral("90d"));
    expectInvalidSeries("range 回声不匹配", wrongRange, range7);

    // 缺数组
    QJsonObject noArray = base;
    noArray.remove(QStringLiteral("revenue_daily"));
    expectInvalidSeries("缺 revenue_daily 数组", noArray, range7);

    // 少一行(7 → 6)
    QJsonObject shortRows = base;
    QJsonArray shortDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    shortDaily.removeLast();
    shortRows.insert(QStringLiteral("revenue_daily"), shortDaily);
    expectInvalidSeries("日条数不足", shortRows, range7);

    // 重复日期(第 4 行复制第 3 行日期)
    QJsonObject dupDate = base;
    QJsonArray dupDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    QJsonObject row4 = dupDaily.at(3).toObject();
    row4.insert(QStringLiteral("date"),
                dupDaily.at(2).toObject().value(QStringLiteral("date")));
    dupDaily.replace(3, row4);
    dupDate.insert(QStringLiteral("revenue_daily"), dupDaily);
    expectInvalidSeries("重复日期", dupDate, range7);

    // 颠倒顺序(交换第 1/2 行)
    QJsonObject swapped = base;
    QJsonArray swapDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    const QJsonValue first = swapDaily.at(1);
    swapDaily.replace(1, swapDaily.at(2));
    swapDaily.replace(2, first);
    swapped.insert(QStringLiteral("revenue_daily"), swapDaily);
    expectInvalidSeries("日期颠倒", swapped, range7);

    // 无效日期(2026-02-30)
    QJsonObject badDate = base;
    QJsonArray badDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    QJsonObject badRow = badDaily.at(1).toObject();
    badRow.insert(QStringLiteral("date"), QStringLiteral("2026-02-30"));
    badDaily.replace(1, badRow);
    badDate.insert(QStringLiteral("revenue_daily"), badDaily);
    expectInvalidSeries("无效日期", badDate, range7);

    // 日期终点错一天(末日 +1, 与快照日不一致)
    QJsonObject shiftedEnd = base;
    QJsonArray endDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    QJsonObject endRow = endDaily.at(6).toObject();
    endRow.insert(QStringLiteral("date"), QStringLiteral("2026-09-02"));
    endDaily.replace(6, endRow);
    shiftedEnd.insert(QStringLiteral("revenue_daily"), endDaily);
    expectInvalidSeries("日期终点与快照不一致", shiftedEnd, range7);

    // 金额为字符串
    QJsonObject stringCents = base;
    QJsonArray stringDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    QJsonObject stringRow = stringDaily.at(6).toObject();
    stringRow.insert(QStringLiteral("revenue_cents"), QStringLiteral("12345"));
    stringDaily.replace(6, stringRow);
    stringCents.insert(QStringLiteral("revenue_daily"), stringDaily);
    expectInvalidSeries("金额为字符串", stringCents, range7);

    // 金额为小数
    QJsonObject fracCents = base;
    QJsonArray fracDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    QJsonObject fracRow = fracDaily.at(6).toObject();
    fracRow.insert(QStringLiteral("revenue_cents"), 12345.5);
    fracDaily.replace(6, fracRow);
    fracCents.insert(QStringLiteral("revenue_daily"), fracDaily);
    expectInvalidSeries("金额为小数", fracCents, range7);

    // 负金额
    QJsonObject negCents = base;
    QJsonArray negDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    QJsonObject negRow = negDaily.at(6).toObject();
    negRow.insert(QStringLiteral("revenue_cents"), -1);
    negDaily.replace(6, negRow);
    negCents.insert(QStringLiteral("revenue_daily"), negDaily);
    expectInvalidSeries("金额为负", negCents, range7);

    // 超出 JSON/图表可精确表示范围(2^53, 服务端/客户端都应显式拒绝)
    QJsonObject hugeCents = base;
    QJsonArray hugeDaily = base.value(QStringLiteral("revenue_daily")).toArray();
    QJsonObject hugeRow = hugeDaily.at(6).toObject();
    hugeRow.insert(QStringLiteral("revenue_cents"), 9007199254740992.0);
    hugeDaily.replace(6, hugeRow);
    hugeCents.insert(QStringLiteral("revenue_daily"), hugeDaily);
    expectInvalidSeries("金额超出精确范围", hugeCents, range7);

    // 合计与逐日之和不一致
    QJsonObject mismatch = base;
    mismatch.insert(QStringLiteral("revenue_cents"), 12346);
    expectInvalidSeries("合计与逐日和不一致", mismatch, range7);

    // updated_at 无效(缺 Z 后缀, ISO 语义不再确定为 UTC 快照)
    QJsonObject badTime = base;
    badTime.insert(QStringLiteral("updated_at"), QStringLiteral("2026-09-01T10:15:00"));
    expectInvalidSeries("updated_at 无效", badTime, range7);
}

QTEST_APPLESS_MAIN(TestSocketParse)
#include "tst_socketparse.moc"
