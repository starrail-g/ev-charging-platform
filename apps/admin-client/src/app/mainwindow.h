#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <QString>

#include <memory>

namespace ev {
class AdminRepository;
}

class QStackedWidget;
class QListWidget;
class QLabel;
class LoginPage;
class OverviewPage;
class PilePage;
class StationPage;
class UserPage;
class MockAdminRepository;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // repository 为空时自建 MockAdminRepository（第一阶段默认）；
    // 注入实例生命周期归调用方。
    explicit MainWindow(ev::AdminRepository *repository = nullptr,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

    bool isLoggedIn() const { return m_loggedIn; }

public slots:
    void onLogout();

private slots:
    // 登录唯一入口：由 LoginPage::loggedIn 信号触发（认证成功后）。
    // 保持 private：禁止外部直接绕过认证置为已登录。
    void onLoginSuccess();

private:
    void buildBusinessArea(ev::AdminRepository *repository);
    // 状态栏登录态文案：数据来源标识来自 Repository（dataSourceName），
    // 不硬编码具体实现名（P2 review 修复）
    QString loggedInStatusText(const QString &page) const;

    std::unique_ptr<ev::AdminRepository> m_ownedRepository;
    QString m_dataSourceLabel; // 状态栏来源标识（构造时取自已注入的 Repository）
    QStackedWidget *m_stack = nullptr;
    LoginPage *m_loginPage = nullptr;
    QWidget *m_businessArea = nullptr;
    QListWidget *m_navList = nullptr;
    QLabel *m_sessionBadge = nullptr;
    QLabel *m_pageTitle = nullptr;
    OverviewPage *m_overviewPage = nullptr;
    PilePage *m_pilePage = nullptr;
    StationPage *m_stationPage = nullptr;
    UserPage *m_userPage = nullptr;
    bool m_loggedIn = false;
};

#endif // MAINWINDOW_H
