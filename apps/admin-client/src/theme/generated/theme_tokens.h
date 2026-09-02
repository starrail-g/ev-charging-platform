#pragma once
#include <QColor>
namespace ev::theme {
inline const QColor kDayBackground{"#EDF0EE"};
inline const QColor kDaySurface{"#F7F8F7"};
inline const QColor kDayText{"#18201D"};
inline const QColor kDayMutedText{"#5F7068"};
inline const QColor kDayDecorativeStructure{"#CBD2CE"};
inline const QColor kDayTopologyLine{"#5F7068"};
inline const QColor kDayFocusBlue{"#0E6E8C"};
inline const QColor kDayIdle{"#2A7442"};
inline const QColor kDayReserved{"#8A5A00"};
inline const QColor kDayCharging{"#0E6E8C"};
inline const QColor kDayFault{"#A94B38"};
inline const QColor kDayOffline{"#5B666B"};
inline const QColor kDayUnknown{"#4E5A55"};
inline const QColor kNightBackground{"#0A1110"};
inline const QColor kNightSurface{"#101D19"};
inline const QColor kNightText{"#E9F5ED"};
inline const QColor kNightMutedText{"#A6B0B4"};
inline const QColor kNightDecorativeStructure{"#294239"};
inline const QColor kNightTopologyLine{"#6B9184"};
inline const QColor kNightFocusBlue{"#4DD7FF"};
inline const QColor kNightDeepBlue{"#0A3552"};
inline const QColor kNightIdle{"#B7F36A"};
inline const QColor kNightReserved{"#F5C451"};
inline const QColor kNightCharging{"#4DD7FF"};
inline const QColor kNightFault{"#FF806D"};
inline const QColor kNightOffline{"#A6B0B4"};
inline const QColor kNightUnknown{"#93A19B"};
inline constexpr int kMotionMicroMs{150};
inline constexpr int kMotionPanelMs{200};
inline constexpr int kMotionChargingPulseMs{2600};
inline constexpr int kMotionFaultPulseMs{2800};
inline constexpr int kMotionAuroraMs{11000};
} // namespace ev::theme
