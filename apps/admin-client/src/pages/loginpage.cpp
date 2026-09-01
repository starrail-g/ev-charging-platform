#include "loginpage.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kCodeUnauthorized = 1100;
constexpr int kMaxUsernameLength = 64; // 对齐 administrators.username CHECK 1..64
constexpr int kMaxPasswordLength = 64; // Mock 层约定，与后端哈希输入口径一致
} // namespace

LoginPage::LoginPage(ev::AdminRepository *repository, QWidget *parent)
    : QWidget(parent)
    , m_repository(repository)
{
    auto *title = new QLabel(QStringLiteral("管理员登录"), this);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setObjectName(QStringLiteral("usernameEdit"));
    m_usernameEdit->setPlaceholderText(QStringLiteral("管理员账号"));
    m_usernameEdit->setMaxLength(kMaxUsernameLength);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
    m_passwordEdit->setPlaceholderText(QStringLiteral("密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setMaxLength(kMaxPasswordLength);

    m_loginButton = new QPushButton(QStringLiteral("登 录"), this);
    m_loginButton->setObjectName(QStringLiteral("loginButton"));

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
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

    connect(m_loginButton, &QPushButton::clicked, this, &LoginPage::attemptLogin);
    connect(m_usernameEdit, &QLineEdit::returnPressed, this, &LoginPage::attemptLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginPage::attemptLogin);
}

void LoginPage::attemptLogin()
{
    // 输入校验：非空 + 长度上限（QLineEdit::setMaxLength 已兜底长度）
    const QString username = m_usernameEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入账号和密码"));
        m_errorLabel->setVisible(true);
        return;
    }

    // 提交中状态（Mock 同步返回；状态机保留给 9/6 Socket 异步接入）
    m_loginButton->setEnabled(false);
    m_loginButton->setText(QStringLiteral("登录中…"));

    const ev::LoginResult result = m_repository->login(username, password);

    m_loginButton->setEnabled(true);
    m_loginButton->setText(QStringLiteral("登 录"));

    if (result.ok) {
        m_errorLabel->setVisible(false);
        emit loggedIn();
        return;
    }

    // 按错误码分支（协议约定：客户端不按 message 文本分支）
    QString text;
    if (result.networkError) {
        text = QStringLiteral("服务不可用，请稍后重试");
    } else if (result.errorCode == kCodeUnauthorized) {
        text = QStringLiteral("账号或密码错误");
    } else {
        text = result.message.isEmpty()
            ? QStringLiteral("登录失败，请稍后重试")
            : result.message;
    }
    m_errorLabel->setText(text);
    m_errorLabel->setVisible(true);
}
