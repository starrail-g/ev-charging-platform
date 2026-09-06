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
    void payloadListsSkipBadItemsButFailOnStructure();
    void adminLoginPayloadAndErrorCode();
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

QTEST_APPLESS_MAIN(TestSocketParse)
#include "tst_socketparse.moc"
