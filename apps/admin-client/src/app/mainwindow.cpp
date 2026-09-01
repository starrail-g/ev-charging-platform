#include "mainwindow.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

#include "pages/loginpage.h"
#include "pages/overviewpage.h"
#include "pages/pilepage.h"
#include "pages/stationpage.h"
#include "pages/userpage.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("充电桩管理端"));
    resize(1024, 700);

    m_stack = new QStackedWidget(this);
    m_loginPage = new LoginPage(m_stack);
    m_businessArea = new QWidget(m_stack);
    buildBusinessArea();

    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_businessArea);
    m_stack->setCurrentWidget(m_loginPage);

    setCentralWidget(m_stack);
    statusBar()->showMessage(QStringLiteral("未登录"));

    connect(m_loginPage, &LoginPage::loggedIn,
            this, &MainWindow::onLoginSuccess);
}

void MainWindow::buildBusinessArea()
{
    m_navList = new QListWidget(m_businessArea);
    m_navList->addItem(QStringLiteral("概览"));
    m_navList->addItem(QStringLiteral("充电桩"));
    m_navList->addItem(QStringLiteral("充电站"));
    m_navList->addItem(QStringLiteral("用户管理"));
    m_navList->setFixedWidth(160);
    m_navList->setEnabled(false); // 业务页默认不可进入（未登录）

    m_overviewPage = new OverviewPage(m_businessArea);
    m_pilePage = new PilePage(m_businessArea);
    m_stationPage = new StationPage(m_businessArea);
    m_userPage = new UserPage(m_businessArea);

    auto *pageStack = new QStackedWidget(m_businessArea);
    pageStack->addWidget(m_overviewPage);
    pageStack->addWidget(m_pilePage);
    pageStack->addWidget(m_stationPage);
    pageStack->addWidget(m_userPage);

    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"), m_businessArea);

    auto *pageLayout = new QVBoxLayout;
    pageLayout->addWidget(pageStack);
    pageLayout->addWidget(logoutButton);

    auto *layout = new QHBoxLayout(m_businessArea);
    layout->addWidget(m_navList);
    layout->addLayout(pageLayout);

    connect(m_navList, &QListWidget::currentRowChanged,
            pageStack, &QStackedWidget::setCurrentIndex);
    connect(logoutButton, &QPushButton::clicked,
            this, &MainWindow::onLogout);
}

void MainWindow::onLoginSuccess()
{
    m_loggedIn = true;
    m_navList->setEnabled(true);
    m_navList->setCurrentRow(0);
    m_stack->setCurrentWidget(m_businessArea);
    statusBar()->showMessage(QStringLiteral("已登录（Mock 演示）"));
}

void MainWindow::onLogout()
{
    m_loggedIn = false;
    m_navList->setCurrentRow(-1);
    m_navList->setEnabled(false);
    m_stack->setCurrentWidget(m_loginPage);
    statusBar()->showMessage(QStringLiteral("未登录"));
}
