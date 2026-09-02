#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>

#include <QTimer>

#include "data/adminrepository.h"

class QLineEdit;
class QPushButton;
class QLabel;

class LoginPage : public QWidget
{
    Q_OBJECT

public:
    // repository 由调用方（MainWindow）注入，生命周期归调用方；
    // 第一阶段为 MockAdminRepository，9/6 起换 Socket 适配层。
    explicit LoginPage(ev::AdminRepository *repository, QWidget *parent = nullptr);

    // 清空账号/密码输入与错误提示（退出登录时调用，防止下一位使用者
    // 沿用上一位凭据）；同时作废在飞登录请求。
    void clearCredentials();

signals:
    void loggedIn();

private:
    void attemptLogin();
    void onLoginFinished(const ev::LoginResult &result);
    void onLoginTimeout();
    void resetLoginButton();

    ev::AdminRepository *m_repository = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QPushButton *m_loginButton = nullptr;
    QLabel *m_errorLabel = nullptr;
    QTimer m_timeoutTimer;       // 单触发：登录请求超时保护
    bool m_loginPending = false; // 有在飞请求时置位（防重入 + 超时/取消判定）
};

#endif // LOGINPAGE_H
