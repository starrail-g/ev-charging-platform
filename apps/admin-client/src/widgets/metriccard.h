#ifndef METRICCARD_H
#define METRICCARD_H

#include <QFrame>

class QLabel;

// 概览指标卡：标题（小字）+ 主数值（大字，对象名由调用方传入供自动化定位）
// + 可选补充说明（setHint，空则不显示）。
// 仅负责展示：数值与口径由页面层（数据经 Repository 链路）计算后传入，
// 组件内不派生任何业务指标，不触碰数据源。
class MetricCard : public QFrame
{
    Q_OBJECT

public:
    explicit MetricCard(const QString &label, const QString &valueObjectName,
                        QWidget *parent = nullptr);

    void setValue(const QString &value);
    void setHint(const QString &hint);

private:
    QLabel *m_value = nullptr;
    QLabel *m_hint = nullptr;
};

#endif // METRICCARD_H
