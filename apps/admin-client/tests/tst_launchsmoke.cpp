#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>

#include <functional>

#include "app/mainwindow.h"
#include "data/adminrepository.h"
#include "data/mockadminrepository.h"
#include "data/mockdataset.h"
#include "pages/loginpage.h"
#include "pages/overviewpage.h"
#include "widgets/statestack.h"

namespace {

// 模拟未来 Socket 适配层：登录成功 + 自定义数据来源标识（P2 review 用例）
class FakeSocketRepository : public ev::AdminRepository
{
public:
    void login(const QString &, const QString &, QObject *context,
               std::function<void(const ev::LoginResult &)> callback) override
    {
        ev::LoginResult result;
        result.ok = true;
        result.errorCode = 0;
        result.admin = ev::AdminInfo{1, QStringLiteral("admin"),
                                     QStringLiteral("super_admin"), QStringLiteral("active")};
        QTimer::singleShot(0, context,
                           [result, callback = std::move(callback)] {
                               if (callback)
                                   callback(result);
                           });
    }

    // 模拟真实数据源：概览返回一份最小正常数据
    void fetchOverview(QObject *context,
                       std::function<void(const ev::OverviewResult &)> callback) override
    {
        ev::OverviewResult result;
        result.ok = true;
        result.hasData = true;
        result.stats.revenueCents = 100;
        result.stats.updatedAt = QStringLiteral("2026-09-02T00:00:00Z");
        QTimer::singleShot(0, context,
                           [result, callback = std::move(callback)] {
                               if (callback)
                                   callback(result);
                           });
    }

    QString dataSourceName() const override
    {
        return QStringLiteral("Socket 测试源");
    }
};

} // namespace

class TestLaunchSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void windowCreatesWithoutCrash();
    void businessAreaLockedUntilLogin();
    void overviewFourStatesShowMockData();
    void dataSourceLabelReflectsRepository();
};

void TestLaunchSmoke::initTestCase()
{
    qApp->setQuitOnLastWindowClosed(false);
}

void TestLaunchSmoke::windowCreatesWithoutCrash()
{
    MainWindow window;
    QVERIFY(!window.windowTitle().isEmpty());
}

void TestLaunchSmoke::businessAreaLockedUntilLogin()
{
    MainWindow window;
    // 按 objectName 确定性查找（不再用无参 findChild<QListWidget*>）
    auto *nav = window.findChild<QListWidget *>("navList");
    QVERIFY(nav);

    // 未登录：业务页不可进入
    QVERIFY(!window.isLoggedIn());
    QVERIFY(!nav->isEnabled());

    // 登录必须走真实 UI 流（onLoginSuccess 已收紧为 private，无绕过入口）
    window.show(); // QTest::mouseClick 需要窗口已显示才能命中
    auto *loginPage = window.findChild<LoginPage *>();
    QVERIFY(loginPage);
    auto *user = loginPage->findChild<QLineEdit *>("usernameEdit");
    auto *pass = loginPage->findChild<QLineEdit *>("passwordEdit");
    auto *button = loginPage->findChild<QPushButton *>("loginButton");
    QVERIFY(user && pass && button);
    user->setText(QStringLiteral("admin"));
    pass->setText(QStringLiteral("123456"));
    QTest::mouseClick(button, Qt::LeftButton);

    // 登录后：导航可用（异步登录，等待回调派发）
    QTRY_VERIFY(window.isLoggedIn());
    QVERIFY(nav->isEnabled());

    // 退出登录后：再次锁定
    window.onLogout();
    QVERIFY(!window.isLoggedIn());
    QVERIFY(!nav->isEnabled());

    // 防回退：登出后即使编程切换导航，业务区也不可达（主栈停在登录页）
    nav->setCurrentRow(2);
    auto *mainStack = window.findChild<QStackedWidget *>("mainStack");
    QVERIFY(mainStack);
    QCOMPARE(mainStack->currentIndex(), 0); // 0 = 登录页
    QVERIFY(!window.isLoggedIn());
}

// 概览页四态（C-S1-008）：初始 Loading；登录后切换数据模式下拉，
// 验证 正常/空/错误 状态与重试入口。
void TestLaunchSmoke::overviewFourStatesShowMockData()
{
    // 初始态：未刷新前必须为 Loading（StateStack 构造默认）
    MockAdminRepository mock; // 生命周期须长于 standalone（standalone 持有其指针）
    OverviewPage standalone(&mock);
    auto *standaloneStack = standalone.findChild<StateStack *>();
    QVERIFY(standaloneStack);
    QCOMPARE(int(standaloneStack->currentState()), int(StateStack::State::Loading));

    MainWindow window;
    window.show();
    auto *loginPage = window.findChild<LoginPage *>();
    QVERIFY(loginPage);
    loginPage->findChild<QLineEdit *>("usernameEdit")->setText(QStringLiteral("admin"));
    loginPage->findChild<QLineEdit *>("passwordEdit")->setText(QStringLiteral("123456"));
    QTest::mouseClick(loginPage->findChild<QPushButton *>("loginButton"), Qt::LeftButton);
    QTRY_VERIFY(window.isLoggedIn());

    auto *overview = window.findChild<OverviewPage *>();
    QVERIFY(overview);
    auto *combo = overview->findChild<QComboBox *>("dataModeCombo");
    auto *stack = overview->findChild<StateStack *>();
    QVERIFY(combo && stack);

    // 正常：Content 态（数据经 Repository 链路异步返回，QTRY 等待）
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Normal));
    QTRY_COMPARE(int(stack->currentState()), int(StateStack::State::Content));

    // 空：Empty 态（hasData=false 显式声明，不靠数值反推）
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Empty));
    QTRY_COMPARE(int(stack->currentState()), int(StateStack::State::Empty));

    // 错误：Error 态 + 重试按钮存在且可点击（触发 refresh，不崩溃）
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Error));
    QTRY_COMPARE(int(stack->currentState()), int(StateStack::State::Error));
    auto *retry = stack->findChild<QPushButton *>("retryButton");
    QVERIFY2(retry, "错误态必须提供重试按钮");
    QTest::mouseClick(retry, Qt::LeftButton);
    QTRY_COMPARE(int(stack->currentState()), int(StateStack::State::Error)); // 重试后仍为错误态（数据未变）

    // 切回正常：恢复 Content
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Normal));
    QTRY_COMPARE(int(stack->currentState()), int(StateStack::State::Content));
}

// 状态栏来源标识必须来自 Repository（P2 review）：注入非 Mock 实现时
// 不得再显示"Mock 演示"。
void TestLaunchSmoke::dataSourceLabelReflectsRepository()
{
    FakeSocketRepository repo;
    MainWindow window(&repo);
    window.show();
    auto *loginPage = window.findChild<LoginPage *>();
    QVERIFY(loginPage);
    loginPage->findChild<QLineEdit *>("usernameEdit")->setText(QStringLiteral("admin"));
    loginPage->findChild<QLineEdit *>("passwordEdit")->setText(QStringLiteral("123456"));
    QTest::mouseClick(loginPage->findChild<QPushButton *>("loginButton"), Qt::LeftButton);
    QTRY_VERIFY(window.isLoggedIn());

    const QString msg = window.statusBar()->currentMessage();
    QVERIFY2(msg.contains(QStringLiteral("Socket 测试源")),
             qPrintable(QStringLiteral("状态栏应显示 Repository 提供的来源标识，实际：%1").arg(msg)));
    QVERIFY2(!msg.contains(QStringLiteral("Mock 演示")),
             "注入真实 Repository 时状态栏不得再显示 Mock 演示");
}

QTEST_MAIN(TestLaunchSmoke)
#include "tst_launchsmoke.moc"
