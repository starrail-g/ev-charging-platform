#include <QtTest>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include "app/mainwindow.h"
#include "data/mockadminrepository.h"
#include "pages/loginpage.h"

// 登录流四场景（按已冻结的 admin.login 契约，docs/architecture/protocol.md）：
//  1. 成功：admin/123456（对齐 database/seeds/dev.sql）-> 进入业务区
//  2. 密码错误：-> 1100 UNAUTHORIZED，提示"账号或密码错误"，保持锁定
//  3. 服务不可用：-> 传输层错误（networkError），提示"服务不可用"，保持锁定
//  4. 空输入：前端拦截并确保不调用 Repository
// 另：5. 退出登录后凭据清空（P1 review 修复）。
// 登录接口为异步（P1 review 修复），点击后需 QTRY_* 等待回调派发。
class TestLoginFlow : public QObject
{
    Q_OBJECT

private:
    // 驱动真实 UI 登录流：填表单 -> 点击登录按钮（不走任何内部捷径）
    static void doLogin(MainWindow &window, const QString &user, const QString &pass)
    {
        window.show(); // QTest::mouseClick 需要窗口已显示才能命中
        auto *loginPage = window.findChild<LoginPage *>();
        QVERIFY2(loginPage, "MainWindow 必须包含 LoginPage");
        auto *userEdit = loginPage->findChild<QLineEdit *>("usernameEdit");
        auto *passEdit = loginPage->findChild<QLineEdit *>("passwordEdit");
        auto *button = loginPage->findChild<QPushButton *>("loginButton");
        QVERIFY2(userEdit && passEdit && button, "登录控件必须存在");
        userEdit->setText(user);
        passEdit->setText(pass);
        QTest::mouseClick(button, Qt::LeftButton);
    }

    static bool isErrorVisible(LoginPage *loginPage, QString *textOut = nullptr)
    {
        auto *error = loginPage->findChild<QLabel *>("errorLabel");
        if (!error || !error->isVisible())
            return false;
        if (textOut)
            *textOut = error->text();
        return true;
    }

private slots:
    void initTestCase();
    void loginSuccessEntersBusinessArea();
    void wrongPasswordShowsUnauthorized();
    void serviceUnavailableShowsNetworkError();
    void emptyInputShowsValidationErrorAndSkipsRepository();
    void logoutClearsCredentials();
};

void TestLoginFlow::initTestCase()
{
    // 窗口需要 show 后 QTest::mouseClick 才能命中按钮；
    // 关闭 quitOnLastWindowClosed，避免测试中途退出（offscreen 平台怪癖）。
    qApp->setQuitOnLastWindowClosed(false);
}

void TestLoginFlow::loginSuccessEntersBusinessArea()
{
    MainWindow window; // 默认 MockAdminRepository（admin/123456）
    doLogin(window, QStringLiteral("admin"), QStringLiteral("123456"));

    QTRY_VERIFY2(window.isLoggedIn(), "正确凭据应登录成功");
    auto *nav = window.findChild<QListWidget *>("navList");
    QVERIFY2(nav && nav->isEnabled(), "登录后导航应可用");
}

void TestLoginFlow::wrongPasswordShowsUnauthorized()
{
    MainWindow window;
    doLogin(window, QStringLiteral("admin"), QStringLiteral("wrong-pass"));

    auto *loginPage = window.findChild<LoginPage *>();
    QString text;
    QTRY_VERIFY2(isErrorVisible(loginPage, &text), "错误密码应显示错误提示");
    QVERIFY2(!window.isLoggedIn(), "错误密码不得进入业务区");
    QCOMPARE(text, QStringLiteral("账号或密码错误"));
}

void TestLoginFlow::serviceUnavailableShowsNetworkError()
{
    MockAdminRepository mock(MockAdminRepository::LoginMode::ServiceUnavailable);
    MainWindow window(&mock);
    doLogin(window, QStringLiteral("admin"), QStringLiteral("123456"));

    auto *loginPage = window.findChild<LoginPage *>();
    QString text;
    QTRY_VERIFY2(isErrorVisible(loginPage, &text), "服务不可用应显示错误提示");
    QVERIFY2(!window.isLoggedIn(), "服务不可用不得进入业务区");
    QCOMPARE(text, QStringLiteral("服务不可用，请稍后重试"));
}

// 空输入边界：空账号 / 全空格账号 / 空密码 均被前端拦截，
// 且不得触达 Repository（login 调用计数必须保持 0）。
// 前端校验是同步的（不发请求），故此处无需 QTRY。
void TestLoginFlow::emptyInputShowsValidationErrorAndSkipsRepository()
{
    struct Case { const char *name; QString user; QString pass; };
    const Case cases[] = {
        {"空账号+空密码", QString(), QString()},
        {"全空格账号", QStringLiteral("   "), QStringLiteral("123456")},
        {"空密码", QStringLiteral("admin"), QString()},
    };

    for (const Case &c : cases) {
        MockAdminRepository mock; // 默认 Ok 模式；若被调用会执行真实校验
        MainWindow window(&mock);
        doLogin(window, c.user, c.pass);

        QVERIFY2(!window.isLoggedIn(), qPrintable(QStringLiteral("[%1] 空输入不得登录").arg(c.name)));
        auto *loginPage = window.findChild<LoginPage *>();
        QString text;
        QVERIFY2(isErrorVisible(loginPage, &text),
                 qPrintable(QStringLiteral("[%1] 应显示输入校验提示").arg(c.name)));
        QCOMPARE(text, QStringLiteral("请输入账号和密码"));
        QCOMPARE(mock.loginCallCount(), 0); // Repository 未被调用
    }
}

// 退出登录后凭据必须清空（P1 review）：下一位使用者不得沿用上一位账号/密码
void TestLoginFlow::logoutClearsCredentials()
{
    MainWindow window;
    doLogin(window, QStringLiteral("admin"), QStringLiteral("123456"));
    QTRY_VERIFY2(window.isLoggedIn(), "正确凭据应登录成功");

    auto *loginPage = window.findChild<LoginPage *>();
    QVERIFY(loginPage);
    auto *userEdit = loginPage->findChild<QLineEdit *>("usernameEdit");
    auto *passEdit = loginPage->findChild<QLineEdit *>("passwordEdit");
    auto *error = loginPage->findChild<QLabel *>("errorLabel");
    QVERIFY2(userEdit && passEdit && error, "登录控件必须存在");

    window.onLogout();
    QVERIFY2(!window.isLoggedIn(), "退出后不得保持登录态");
    QVERIFY2(userEdit->text().isEmpty(), "退出后账号输入框必须清空");
    QVERIFY2(passEdit->text().isEmpty(), "退出后密码输入框必须清空");
    QVERIFY2(!error->isVisible(), "退出后错误提示必须隐藏");
    QVERIFY2(error->text().isEmpty(), "退出后错误提示文本必须清空");
}

QTEST_MAIN(TestLoginFlow)
#include "tst_loginflow.moc"
