#include <QApplication>

#include "app/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("admin-client"));
    app.setApplicationDisplayName(QStringLiteral("充电桩管理端"));

    MainWindow window;
    window.show();
    return app.exec();
}
