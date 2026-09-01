#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>

#include "app/mainwindow.h"
#include "data/mockdataset.h"
#include "pages/loginpage.h"
#include "pages/overviewpage.h"
#include "widgets/statestack.h"

class TestLaunchSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void windowCreatesWithoutCrash();
    void businessAreaLockedUntilLogin();
    void overviewFourStatesShowMockData();
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

    // 登录后：导航可用
    QVERIFY(window.isLoggedIn());
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
    OverviewPage standalone;
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
    QVERIFY(window.isLoggedIn());

    auto *overview = window.findChild<OverviewPage *>();
    QVERIFY(overview);
    auto *combo = overview->findChild<QComboBox *>("dataModeCombo");
    auto *stack = overview->findChild<StateStack *>();
    QVERIFY(combo && stack);

    // 正常：Content 态 + 摘要含营收
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Normal));
    QCOMPARE(int(stack->currentState()), int(StateStack::State::Content));

    // 空：Empty 态
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Empty));
    QCOMPARE(int(stack->currentState()), int(StateStack::State::Empty));

    // 错误：Error 态 + 重试按钮存在且可点击（触发 refresh，不崩溃）
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Error));
    QCOMPARE(int(stack->currentState()), int(StateStack::State::Error));
    auto *retry = stack->findChild<QPushButton *>("retryButton");
    QVERIFY2(retry, "错误态必须提供重试按钮");
    QTest::mouseClick(retry, Qt::LeftButton);
    QCOMPARE(int(stack->currentState()), int(StateStack::State::Error)); // 重试后仍为错误态（数据未变）

    // 切回正常：恢复 Content
    combo->setCurrentIndex(int(ev::mockdata::DataMode::Normal));
    QCOMPARE(int(stack->currentState()), int(StateStack::State::Content));
}

QTEST_MAIN(TestLaunchSmoke)
#include "tst_launchsmoke.moc"
