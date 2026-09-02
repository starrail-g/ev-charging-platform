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
constexpr int kLoginTimeoutMs = 10000; // Socket 接入后的传输超时保护（P1 review）
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

    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &LoginPage::onLoginTimeout);

    connect(m_loginButton, &QPushButton::clicked, this, &LoginPage::attemptLogin);
    connect(m_usernameEdit, &QLineEdit::returnPressed, this, &LoginPage::attemptLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginPage::attemptLogin);
}

void LoginPage::clearCredentials()
{
    m_usernameEdit->clear();
    m_passwordEdit->clear();
    m_errorLabel->clear();
    m_errorLabel->setVisible(false);
    // 作废在飞登录请求（防御性：正常流程登出时无在飞请求）；
    // 迟到的回调会因 m_loginPending=false 被 onLoginFinished 丢弃。
    m_loginPending = false;
    m_timeoutTimer.stop();
    resetLoginButton();
}

void LoginPage::attemptLogin()
{
    // 防重入：在飞请求期间按钮已禁用，理论上不可达
    if (m_loginPending)
        return;

    // 输入校验：非空 + 长度上限（QLineEdit::setMaxLength 已兜底长度）
    const QString username = m_usernameEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入账号和密码"));
        m_errorLabel->setVisible(true);
        return;
    }

    // 提交中状态：异步请求在飞，期间禁用按钮（防连点产生多个请求）
    m_loginPending = true;
    m_loginButton->setEnabled(false);
    m_loginButton->setText(QStringLiteral("登录中…"));
    m_timeoutTimer.start(kLoginTimeoutMs);

    // 异步调用：结果经回调投递；context=this 保证页面销毁后不再回调（生命周期保护）
    m_repository->login(username, password, this,
                        [this](const ev::LoginResult &result) { onLoginFinished(result); });
}

void LoginPage::onLoginFinished(const ev::LoginResult &result)
{
    if (!m_loginPending)
        return; // 已超时或已取消：丢弃迟到的结果
    m_loginPending = false;
    m_timeoutTimer.stop();
    resetLoginButton();

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

void LoginPage::onLoginTimeout()
{
    if (!m_loginPending)
        return;
    m_loginPending = false; // 作废在飞请求：迟到的回调将被 onLoginFinished 丢弃
    resetLoginButton();
    m_errorLabel->setText(QStringLiteral("登录超时，请稍后重试"));
    m_errorLabel->setVisible(true);
}

void LoginPage::resetLoginButton()
{
    m_loginButton->setEnabled(true);
    m_loginButton->setText(QStringLiteral("登 录"));
}
