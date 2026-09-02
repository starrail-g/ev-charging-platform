#include "mainwindow.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

#include "data/adminrepository.h"
#include "data/mockadminrepository.h"
#include "pages/loginpage.h"
#include "pages/overviewpage.h"
#include "pages/pilepage.h"
#include "pages/stationpage.h"
#include "pages/userpage.h"

MainWindow::MainWindow(ev::AdminRepository *repository, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("充电桩管理端"));
    resize(1024, 700);

    if (!repository) {
        m_ownedRepository = std::make_unique<MockAdminRepository>();
        repository = m_ownedRepository.get();
    }
    // 数据来源标识来自 Repository 自身（不硬编码 "Mock 演示"，P2 review 修复）
    m_dataSourceLabel = repository->dataSourceName();
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("mainStack"));
    m_loginPage = new LoginPage(repository, m_stack);
    m_businessArea = new QWidget(m_stack);
    m_businessArea->setObjectName(QStringLiteral("businessArea"));
    buildBusinessArea(repository);

    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_businessArea);
    m_stack->setCurrentWidget(m_loginPage);

    setCentralWidget(m_stack);
    statusBar()->showMessage(QStringLiteral("未登录"));

    connect(m_loginPage, &LoginPage::loggedIn,
            this, &MainWindow::onLoginSuccess);
}

// out-of-line：unique_ptr<AdminRepository> 需要完整类型（见 mainwindow.h）
MainWindow::~MainWindow() = default;

void MainWindow::buildBusinessArea(ev::AdminRepository *repository)
{
    auto *navigationRail = new QFrame(m_businessArea);
    navigationRail->setObjectName(QStringLiteral("navigationRail"));

    auto *productMark = new QLabel(QStringLiteral("GRID SHIFT\n充电运营中心"), navigationRail);
    productMark->setObjectName(QStringLiteral("productMark"));

    m_sessionBadge = new QLabel(
        m_dataSourceLabel.isEmpty()
            ? QStringLiteral("● 已登录")
            : QStringLiteral("● %1").arg(m_dataSourceLabel),
        navigationRail);
    m_sessionBadge->setObjectName(QStringLiteral("sessionBadge"));

    m_navList = new QListWidget(navigationRail);
    m_navList->setObjectName(QStringLiteral("navList"));
    m_navList->addItem(QStringLiteral("概览"));
    m_navList->addItem(QStringLiteral("充电桩"));
    m_navList->addItem(QStringLiteral("充电站"));
    m_navList->addItem(QStringLiteral("用户管理"));
    m_navList->setFixedWidth(184);
    m_navList->setEnabled(false); // 业务页默认不可进入（未登录）

    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"), navigationRail);
    logoutButton->setObjectName(QStringLiteral("logoutButton"));

    auto *navigationLayout = new QVBoxLayout(navigationRail);
    navigationLayout->setContentsMargins(18, 22, 18, 18);
    navigationLayout->setSpacing(12);
    navigationLayout->addWidget(productMark);
    navigationLayout->addWidget(m_sessionBadge);
    navigationLayout->addSpacing(12);
    navigationLayout->addWidget(m_navList, 1);
    navigationLayout->addWidget(logoutButton);

    m_overviewPage = new OverviewPage(repository, m_businessArea);
    m_pilePage = new PilePage(m_businessArea);
    m_stationPage = new StationPage(m_businessArea);
    m_userPage = new UserPage(m_businessArea);

    auto *pageStack = new QStackedWidget(m_businessArea);
    pageStack->setObjectName(QStringLiteral("pageStack"));
    pageStack->addWidget(m_overviewPage);
    pageStack->addWidget(m_pilePage);
    pageStack->addWidget(m_stationPage);
    pageStack->addWidget(m_userPage);

    m_pageTitle = new QLabel(QStringLiteral("概览"), m_businessArea);
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *pageContext = new QLabel(QStringLiteral("运营工作台 · %1").arg(m_dataSourceLabel),
                                   m_businessArea);
    pageContext->setObjectName(QStringLiteral("pageContext"));

    auto *headerLayout = new QHBoxLayout;
    headerLayout->addWidget(m_pageTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(pageContext);

    auto *pageLayout = new QVBoxLayout;
    pageLayout->setContentsMargins(24, 20, 24, 18);
    pageLayout->setSpacing(14);
    pageLayout->addLayout(headerLayout);
    pageLayout->addWidget(pageStack, 1);

    auto *layout = new QHBoxLayout(m_businessArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(navigationRail);
    layout->addLayout(pageLayout, 1);

    connect(m_navList, &QListWidget::currentRowChanged,
            pageStack, &QStackedWidget::setCurrentIndex);
    // 标题联动：状态栏显示当前页面（来源标识来自 Repository，见 loggedInStatusText）
    connect(m_navList, &QListWidget::currentRowChanged, this, [this](int row) {
        const QString page = (row >= 0 && row < m_navList->count())
            ? m_navList->item(row)->text()
            : QString();
        m_pageTitle->setText(page);
        statusBar()->showMessage(page.isEmpty()
            ? QStringLiteral("未登录")
            : loggedInStatusText(page));
    });
    connect(logoutButton, &QPushButton::clicked,
            this, &MainWindow::onLogout);
}

void MainWindow::onLoginSuccess()
{
    m_loggedIn = true;
    m_sessionBadge->setText(
        m_dataSourceLabel.isEmpty()
            ? QStringLiteral("● 已登录")
            : QStringLiteral("● %1").arg(m_dataSourceLabel));
    m_navList->setEnabled(true);
    m_navList->setCurrentRow(0);
    m_stack->setCurrentWidget(m_businessArea);
    statusBar()->showMessage(loggedInStatusText(QStringLiteral("概览")));
    m_overviewPage->refresh(); // 概览页加载数据（经 Repository 链路）
}

void MainWindow::onLogout()
{
    m_loggedIn = false;
    m_navList->setCurrentRow(-1);
    m_navList->setEnabled(false);
    m_sessionBadge->setText(QStringLiteral("● 会话已结束"));
    m_stack->setCurrentWidget(m_loginPage);
    // 清空登录页凭据：下一位使用者不得沿用上一位账号/密码（P1 review 修复）
    m_loginPage->clearCredentials();
    statusBar()->showMessage(QStringLiteral("未登录"));
}

QString MainWindow::loggedInStatusText(const QString &page) const
{
    if (m_dataSourceLabel.isEmpty())
        return QStringLiteral("已登录 · %1").arg(page);
    return QStringLiteral("已登录（%1）· %2").arg(m_dataSourceLabel, page);
}
