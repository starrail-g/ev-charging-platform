#include "loginpage.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(QStringLiteral("管理员登录"), this);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(QStringLiteral("管理员账号"));
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText(QStringLiteral("密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_loginButton = new QPushButton(QStringLiteral("登 录"), this);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setVisible(false);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), m_usernameEdit);
    form->addRow(QStringLiteral("密码"), m_passwordEdit);

    auto *layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(title, 0, Qt::AlignHCenter);
    layout->addLayout(form);
    layout->addWidget(m_errorLabel, 0, Qt::AlignHCenter);
    layout->addWidget(m_loginButton, 0, Qt::AlignHCenter);
    layout->addStretch();

    // 第一阶段空壳：不做输入校验，仅演示登录→业务区切换。
    // 校验与失败场景（TDD）在 9/2 tst_loginflow 中实现。
    connect(m_loginButton, &QPushButton::clicked, this, &LoginPage::loggedIn);
}
