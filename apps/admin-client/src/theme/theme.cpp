#include "theme.h"

#include <QApplication>
#include <QFile>

namespace ev {

bool Theme::s_motionEnabled = qEnvironmentVariableIntValue("EV_UI_REDUCED_MOTION") != 1;

QString Theme::loadDayStyleSheet()
{
    QFile file(QStringLiteral(":/theme.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

void Theme::applyDayTheme(QApplication &app)
{
    app.setStyleSheet(loadDayStyleSheet());
}

bool Theme::motionEnabled()
{
    return s_motionEnabled;
}

void Theme::setMotionEnabled(bool enabled)
{
    s_motionEnabled = enabled;
}

} // namespace ev
