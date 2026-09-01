#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>

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

signals:
    void loggedIn();

private:
    void attemptLogin();

    ev::AdminRepository *m_repository = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QPushButton *m_loginButton = nullptr;
    QLabel *m_errorLabel = nullptr;
};

#endif // LOGINPAGE_H
