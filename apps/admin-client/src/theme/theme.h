#pragma once

#include <QString>

class QApplication;

namespace ev {

class Theme
{
public:
    static QString loadDayStyleSheet();
    static void applyDayTheme(QApplication &app);
    static bool motionEnabled();
    static void setMotionEnabled(bool enabled);

private:
    static bool s_motionEnabled;
};

} // namespace ev
