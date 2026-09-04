#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>

#include <memory>

#include "data/adminrepository.h"
#include "data/mockdataset.h"

class QComboBox;
class QLabel;
class QListWidget;
class MetricCard;
class StateStack;
class StationTopologyWidget;

namespace ev {

// 网络可用率 = (总桩数 - 需关注桩数(故障+离线)) / 总桩数；空列表返回 0。
// 独立纯函数：口径在 tst_ui 中锁定，页面只负责展示不各自计算（统一 UI Task 5）。
double availabilityRate(const QList<PileInfo> &piles);

} // namespace ev

// 概览页：指标卡 + 需关注异常列表 + 站点态势图（统一 UI Task 5）。
// 数据一律经 AdminRepository 链路获取（不直接触达数据源，P2 review 修复）：
//   fetchOverview → 统计（营收/利用率/更新时间）
//   fetchPiles/fetchStations → 桩级明细与站点坐标（异常列表/拓扑图）
// 三路并行异步；演示模式下拉驱动 Mock 数据层（9/6 Socket 接入后自动驱动）。
// 空态/错误态由数据层显式声明（hasData / ok），不从数值反推。
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    // repository 由调用方（MainWindow）注入；为空时自建 MockAdminRepository
    // （与 MainWindow 同策略，供无参构造与测试使用），生命周期归本页
    explicit OverviewPage(ev::AdminRepository *repository = nullptr,
                          QWidget *parent = nullptr);

    // 经 Repository 异步加载概览数据并展示对应状态
    void refresh();

    ev::mockdata::DataMode dataMode() const;

signals:
    // 异常（故障/离线）列表项被激活：携带桩编号；
    // MainWindow 收到后切到充电桩页（列表内定位/筛选在 Task 6 focusPile 完成）
    void pileAttentionRequested(const QString &pileCode);

private:
    void onModeChanged(int index);
    void onAllReady();
    void renderContent();

    ev::AdminRepository *m_repository = nullptr;
    std::unique_ptr<ev::AdminRepository> m_ownedRepository;
    int m_loadGeneration = 0; // 过期请求丢弃（与 login 防回退同款策略）
    int m_pendingFetches = 0;

    // 最近一次三路结果（全部到达后统一渲染）
    ev::OverviewResult m_lastOverview;
    ev::ListResult<ev::PileInfo> m_lastPiles;
    ev::ListResult<ev::StationInfo> m_lastStations;

    StateStack *m_stateStack = nullptr;
    QComboBox *m_modeCombo = nullptr;
    MetricCard *m_pileTotalCard = nullptr;
    MetricCard *m_availabilityCard = nullptr;
    MetricCard *m_utilizationCard = nullptr;
    MetricCard *m_revenueCard = nullptr;
    QLabel *m_faultCountValue = nullptr; // "需关注" 计数（对象名 metricFaultCount）
    QListWidget *m_attentionList = nullptr;
    StationTopologyWidget *m_topology = nullptr;
    QLabel *m_updatedLabel = nullptr;
};

#endif // OVERVIEWPAGE_H
