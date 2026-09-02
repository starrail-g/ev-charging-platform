#include <QtTest>

#include "models/adminmodels.h"
#include "theme/theme.h"
#include "widgets/statuspulsewidget.h"
#include "widgets/statustag.h"

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void statusTagsExposeProtocolState();
    void chargingAndFaultAnimateWhenMotionEnabled();
    void reducedMotionStopsAnimation();
    void themeLoadsGeneratedQss();
};

void TestUi::statusTagsExposeProtocolState()
{
    const QList<ev::PileStatus> states = {
        ev::PileStatus::Idle,
        ev::PileStatus::Reserved,
        ev::PileStatus::Charging,
        ev::PileStatus::Fault,
        ev::PileStatus::Offline,
    };
    for (const auto state : states) {
        StatusTag tag(state);
        QCOMPARE(tag.property("state").toString(), ev::pileStatusToProtocol(state));
        QCOMPARE(tag.text(), ev::pileStatusToDisplay(state));
    }
}

void TestUi::chargingAndFaultAnimateWhenMotionEnabled()
{
    ev::Theme::setMotionEnabled(true);
    StatusPulseWidget charging(ev::PileStatus::Charging);
    QVERIFY(charging.isAnimationRunning());
    StatusPulseWidget fault(ev::PileStatus::Fault);
    QVERIFY(fault.isAnimationRunning());
    StatusPulseWidget idle(ev::PileStatus::Idle);
    QVERIFY(!idle.isAnimationRunning());
}

void TestUi::reducedMotionStopsAnimation()
{
    StatusPulseWidget widget(ev::PileStatus::Charging);
    widget.setMotionEnabled(false);
    QVERIFY(!widget.isAnimationRunning());
}

void TestUi::themeLoadsGeneratedQss()
{
    const QString qss = ev::Theme::loadDayStyleSheet();
    QVERIFY(!qss.isEmpty());
    QVERIFY(qss.contains(QStringLiteral("#2A7442")));
    QVERIFY(qss.contains(QStringLiteral("#A94B38")));
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
