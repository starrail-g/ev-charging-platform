#include "statestack.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

StateStack::StateStack(QWidget *parent)
    : QStackedWidget(parent)
{
    // 加载态
    m_loadingPage = new QWidget(this);
    m_loadingLabel = new QLabel(QStringLiteral("加载中…"), m_loadingPage);
    auto *loadingLayout = new QVBoxLayout(m_loadingPage);
    loadingLayout->addStretch();
    loadingLayout->addWidget(m_loadingLabel, 0, Qt::AlignHCenter);
    loadingLayout->addStretch();

    // 空态
    m_emptyPage = new QWidget(this);
    m_emptyLabel = new QLabel(QStringLiteral("暂无数据"), m_emptyPage);
    auto *emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_emptyLabel, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();

    // 错误态：文案 + 重试按钮
    m_errorPage = new QWidget(this);
    m_errorLabel = new QLabel(QStringLiteral("加载失败"), m_errorPage);
    m_retryButton = new QPushButton(QStringLiteral("重试"), m_errorPage);
    m_retryButton->setObjectName(QStringLiteral("retryButton"));
    auto *errorLayout = new QVBoxLayout(m_errorPage);
    errorLayout->addStretch();
    errorLayout->addWidget(m_errorLabel, 0, Qt::AlignHCenter);
    errorLayout->addWidget(m_retryButton, 0, Qt::AlignHCenter);
    errorLayout->addStretch();

    // 内容态：占位容器，setContentWidget 时替换
    m_contentPage = new QWidget(this);

    addWidget(m_loadingPage);
    addWidget(m_emptyPage);
    addWidget(m_errorPage);
    addWidget(m_contentPage);

    connect(m_retryButton, &QPushButton::clicked, this, [this] {
        if (m_retryHandler)
            m_retryHandler();
    });
}

void StateStack::showState(State state, const QString &text)
{
    m_state = state;
    switch (state) {
    case State::Loading:
        if (!text.isEmpty())
            m_loadingLabel->setText(text);
        setCurrentWidget(m_loadingPage);
        break;
    case State::Empty:
        if (!text.isEmpty())
            m_emptyLabel->setText(text);
        setCurrentWidget(m_emptyPage);
        break;
    case State::Error:
        if (!text.isEmpty())
            m_errorLabel->setText(text);
        setCurrentWidget(m_errorPage);
        break;
    case State::Content:
        setCurrentWidget(m_contentPage);
        break;
    }
}

void StateStack::setContentWidget(QWidget *content)
{
    delete m_contentPage; // 替换旧内容容器
    m_contentPage = content;
    m_contentPage->setParent(this);
    addWidget(m_contentPage);
    if (m_state == State::Content)
        setCurrentWidget(m_contentPage);
}

void StateStack::setRetryHandler(std::function<void()> handler)
{
    m_retryHandler = std::move(handler);
}
