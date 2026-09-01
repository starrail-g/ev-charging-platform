#include <QtTest>
#include <QApplication>
#include <QListWidget>

#include "app/mainwindow.h"

class TestLaunchSmoke : public QObject
{
    Q_OBJECT

private slots:
    void windowCreatesWithoutCrash();
    void businessAreaLockedUntilLogin();
};

void TestLaunchSmoke::windowCreatesWithoutCrash()
{
    MainWindow window;
    QVERIFY(!window.windowTitle().isEmpty());
}

void TestLaunchSmoke::businessAreaLockedUntilLogin()
{
    MainWindow window;
    auto *nav = window.findChild<QListWidget *>();
    QVERIFY(nav);

    // 未登录：业务页不可进入
    QVERIFY(!window.isLoggedIn());
    QVERIFY(!nav->isEnabled());

    // 登录后：导航可用
    window.onLoginSuccess();
    QVERIFY(window.isLoggedIn());
    QVERIFY(nav->isEnabled());

    // 退出登录后：再次锁定
    window.onLogout();
    QVERIFY(!window.isLoggedIn());
    QVERIFY(!nav->isEnabled());
}

QTEST_MAIN(TestLaunchSmoke)
#include "tst_launchsmoke.moc"
