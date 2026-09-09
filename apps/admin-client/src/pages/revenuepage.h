#ifndef REVENUEPAGE_H
#define REVENUEPAGE_H

#include <QWidget>

#include "data/adminrepository.h"
#include "models/adminmodels.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class MetricCard;
class StateStack;

namespace ev {
class RevenueChartWidget;
}

// 销售业绩页(与概览/充电桩/充电站/用户管理并列):
//   - 顶部两张 MetricCard(近 7 日 / 近 30 日合计, 来自同一 fetchOverview 摘要);
//   - 主图面板: Full RevenueChartWidget(选中范围折线 + 稀疏日期轴) + 范围下拉与刷新;
//   - "每日营收"只读表格: 日期(UTC) + 营收(元), 日期升序与曲线一致;
//   - 所有数据来自 fetchOverview 一次请求, 7/30 切换只重渲染不新增请求;
//     7d/30d 两序列各自 updatedAt, 页面显示选中范围的更新时间。
// 状态呈现(数据层显式声明, 不从数值反推):
//   ok=false             → Error + 重试(不显示旧图冒充当前)
//   hasData=false        → Empty"暂无营收统计数据"
//   选中序列 available=false → 摘要两卡照常 + 图表区错误提示 + 表格清空, 可切正常范围
//   合计 = 0(序列可用)   → 正常两卡、零线、逐日 ¥0.00 行 + "本期间暂无已结算营收"
class RevenuePage : public QWidget
{
    Q_OBJECT

public:
    // repository 由 MainWindow 注入(本页不自建 Mock, 测试显式传替身)
    explicit RevenuePage(ev::AdminRepository *repository, QWidget *parent = nullptr);

    void refresh();
    void setRange(int days);
    int selectedDays() const { return m_days; }
    void invalidatePendingLoads(); // 注销场景: 作废在途请求、清结果与选择回 7 日

private:
    void onComboChanged(int index);
    void renderResult();
    void renderCards();
    void renderSelected();
    void setLoading(bool loading);

    ev::AdminRepository *m_repository = nullptr;
    int m_generation = 0;
    bool m_loading = false;
    ev::OverviewResult m_result;
    int m_days = 7;

    StateStack *m_stateStack = nullptr;
    QComboBox *m_rangeCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    MetricCard *m_7dCard = nullptr;
    MetricCard *m_30dCard = nullptr;
    ev::RevenueChartWidget *m_chart = nullptr;
    QLabel *m_seriesErrorLabel = nullptr; // 选中序列不可用提示(默认隐藏)
    QLabel *m_zeroHintLabel = nullptr;    // 零营收辅助文案(默认隐藏)
    QTableWidget *m_table = nullptr;
    QLabel *m_updatedLabel = nullptr;
};

#endif // REVENUEPAGE_H
