#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>

#include "data/mockdataset.h"

class QComboBox;
class QLabel;
class StateStack;

// 概览页：营收摘要、桩状态摘要、站点利用率、更新时间（C-S1-003）。
// 第一阶段用 StateStack 四态组件 + Mock 数据；页面右上角提供演示模式切换
// （正常/空/错误），用于三态手工验证与截图（9/6 Socket 接入后由数据层自动驱动）。
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(QWidget *parent = nullptr);

    // 按当前演示模式加载 Mock 数据并展示对应状态
    void refresh();

    ev::mockdata::DataMode dataMode() const;

private:
    void onModeChanged(int index);

    StateStack *m_stateStack = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QComboBox *m_modeCombo = nullptr;
};

#endif // OVERVIEWPAGE_H
