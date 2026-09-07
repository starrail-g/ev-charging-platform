#include <QApplication>

#include <memory>

#include "app/mainwindow.h"
#include "data/socketadminrepository.h"
#include "theme/theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("admin-client"));
    app.setApplicationDisplayName(QStringLiteral("充电桩管理端"));
    ev::Theme::applyDayTheme(app);

    // 数据源工厂切换点（2026-09-03 设计稿 §4；9/7 18:00 闸门前默认 Mock）：
    //   EV_ADMIN_DATA_SOURCE=socket → SocketAdminRepository（连 EV_SERVER_HOST:PORT，
    //   见 config/example.env）；空/其他值 → MainWindow 自建 MockAdminRepository。
    // 切换只收敛在本函数：页面层一律经 AdminRepository 抽象，不感知数据源。
    std::unique_ptr<ev::SocketAdminRepository> socketRepository;
    ev::AdminRepository *repository = nullptr;
    if (qEnvironmentVariable("EV_ADMIN_DATA_SOURCE") == QStringLiteral("socket")) {
        socketRepository = std::make_unique<ev::SocketAdminRepository>();
        repository = socketRepository.get();
    }

    MainWindow window(repository);
    window.show();
    return app.exec();
}
