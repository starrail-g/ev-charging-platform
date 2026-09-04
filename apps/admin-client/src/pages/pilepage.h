#ifndef PILEPAGE_H
#define PILEPAGE_H

#include <QWidget>

#include <QHash>
#include <memory>

#include "data/adminrepository.h"
#include "data/mockdataset.h"

class QComboBox;
class QLabel;
class QTableWidget;

// 充电桩工作页（统一 UI Task 6）：
//   桩列表（桩编号 / 站点 / 类型 / 额定功率 / 单价 / 状态五态标签）
//   + 状态筛选（all / idle / reserved / charging / fault / offline / attention，
//     attention = 故障 + 离线）
//   + 异常聚焦入口 focusPile()（清除筛选 → 定位行 → 选中并确保可见）。
// 数据一律经 AdminRepository 异步链路（fetchPiles + fetchStations 取站点名），
// 不直接触达数据源；refresh 的演示模式参数只驱动 Mock 特有接口
// （同 OverviewPage 约定，9/6 Socket 接入后由数据层自动驱动）。
class PilePage : public QWidget
{
    Q_OBJECT

public:
    explicit PilePage(ev::AdminRepository *repository = nullptr,
                      QWidget *parent = nullptr);

    // 经 Repository 异步加载桩与站点数据；mode 仅第一阶段驱动 Mock 演示层
    void refresh(ev::mockdata::DataMode mode = ev::mockdata::DataMode::Normal);

    // 状态筛选：all|idle|reserved|charging|fault|offline|attention
    void setStatusFilter(const QString &filter);
    QString statusFilter() const { return m_statusFilter; }

    // 聚焦指定桩编号：清除筛选、定位行、选中并滚动到可见；
    // 编号不存在时不改变当前选择，显示非阻塞提示"未找到充电桩"
    void focusPile(const QString &pileCode);
    // 当前选中行的桩编号（无选中返回空串）
    QString currentPileCode() const;

    // 当前筛选下可见行数（测试/自动化用）
    int visibleRowCount() const;

private:
    void onFilterIndexChanged(int index);
    void applyFilter();
    // 底部提示行：加载中/空/错误/未找到等非阻塞提示
    void showHint(const QString &text);
    void clearHint();
    void rebuildRows();

    ev::AdminRepository *m_repository = nullptr;
    std::unique_ptr<ev::AdminRepository> m_ownedRepository;
    int m_loadGeneration = 0;
    int m_pendingFetches = 0;

    QList<ev::PileInfo> m_piles;
    QHash<int, QString> m_stationNames;
    bool m_pilesOk = false;
    bool m_stationsOk = false;
    QString m_pilesError;
    QString m_stationsError;

    QComboBox *m_filterCombo = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_hintLabel = nullptr;
    QString m_statusFilter = QStringLiteral("all");
    QString m_pendingFocus; // 数据未到齐时挂起的定位请求（到齐后自动执行）
};

#endif // PILEPAGE_H
