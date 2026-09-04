#ifndef STATIONPAGE_H
#define STATIONPAGE_H

#include <QWidget>

#include <memory>

#include "data/adminrepository.h"
#include "data/mockdataset.h"

class QLabel;
class QTableWidget;

// 充电站工作页（统一 UI Task 6）：站点列表
//（名称 / 地址 / 桩数 / 运行状态）。数据经 AdminRepository 异步链路获取。
class StationPage : public QWidget
{
    Q_OBJECT

public:
    explicit StationPage(ev::AdminRepository *repository = nullptr,
                         QWidget *parent = nullptr);

    // 经 Repository 异步加载站点列表；mode 仅第一阶段驱动 Mock 演示层
    void refresh(ev::mockdata::DataMode mode = ev::mockdata::DataMode::Normal);

    int visibleRowCount() const;

private:
    void rebuildRows();
    void showHint(const QString &text);
    void clearHint();

    ev::AdminRepository *m_repository = nullptr;
    std::unique_ptr<ev::AdminRepository> m_ownedRepository;
    int m_loadGeneration = 0;

    QList<ev::StationInfo> m_stations;
    bool m_ok = false;
    QString m_error;

    QTableWidget *m_table = nullptr;
    QLabel *m_hintLabel = nullptr;
};

#endif // STATIONPAGE_H
