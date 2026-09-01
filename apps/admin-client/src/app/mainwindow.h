#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QStackedWidget;
class QListWidget;
class LoginPage;
class OverviewPage;
class PilePage;
class StationPage;
class UserPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool isLoggedIn() const { return m_loggedIn; }

public slots:
    void onLoginSuccess();
    void onLogout();

private:
    void buildBusinessArea();

    QStackedWidget *m_stack = nullptr;
    LoginPage *m_loginPage = nullptr;
    QWidget *m_businessArea = nullptr;
    QListWidget *m_navList = nullptr;
    OverviewPage *m_overviewPage = nullptr;
    PilePage *m_pilePage = nullptr;
    StationPage *m_stationPage = nullptr;
    UserPage *m_userPage = nullptr;
    bool m_loggedIn = false;
};

#endif // MAINWINDOW_H
