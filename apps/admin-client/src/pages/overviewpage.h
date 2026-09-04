#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>

#include "data/adminrepository.h"
#include "data/mockdataset.h"

class QComboBox;
class QLabel;
class StateStack;

// 概览页：营收摘要、桩状态摘要、站点利用率、更新时间（C-S1-003）。
// 数据一律经 AdminRepository 链路获取（不直接触达数据源，P2 review 修复）；
// 第一阶段右上角提供演示模式下拉（正常/空/错误），驱动 Mock 数据层手工验证
// （9/6 Socket 接入后由数据层自动驱动）。
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    // repository 由调用方（MainWindow）注入，生命周期归调用方
    explicit OverviewPage(ev::AdminRepository *repository, QWidget *parent = nullptr);

    // 经 Repository 异步加载概览数据并展示对应状态
    void refresh();

    ev::mockdata::DataMode dataMode() const;

private:
    void onModeChanged(int index);
    void onOverviewReady(const ev::OverviewResult &result);

    ev::AdminRepository *m_repository = nullptr;
    StateStack *m_stateStack = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QComboBox *m_modeCombo = nullptr;
};

#endif // OVERVIEWPAGE_H
