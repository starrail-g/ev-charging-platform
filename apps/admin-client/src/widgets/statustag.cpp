#include "statustag.h"

#include <QStyle>

StatusTag::StatusTag(ev::PileStatus state, QWidget *parent)
    : QLabel(parent)
{
    setState(state);
}

void StatusTag::setState(ev::PileStatus state)
{
    const QString protocolState = ev::pileStatusToProtocol(state);
    const QString displayState = ev::pileStatusToDisplay(state);
    setText(displayState);
    setProperty("state", protocolState);
    setAccessibleName(QStringLiteral("充电桩状态：%1").arg(displayState));
    style()->unpolish(this);
    style()->polish(this);
}
