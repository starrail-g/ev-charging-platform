#include "client_service.h"
#include <QtTest>
using namespace ev;
class ClientServiceTest : public QObject {
  Q_OBJECT
private slots:
void loginValidation(){ MockUserService s; QVERIFY(s.login("", "123456").error.contains("手机号")); QVERIFY(s.login("13800000000", "").error.contains("请输入密码")); QVERIFY(s.login("13800000000", "123").error.contains("长度")); QVERIFY(s.login("13800000000", "badbad").error.contains("密码错误")); QVERIFY(s.login("bad", "123456").error.contains("手机号格式")); QVERIFY(s.login("13800000000", "123456").ok); }
void unauthorizedAndProfilePersistence(){ MockUserService s; QVERIFY(!s.updateProfile("", "x", {}).ok); QVERIFY(!s.recharge("", 1000).ok); auto u=s.login("13800000000", "123456").value; QVERIFY(s.updateProfile(u.id,"新昵称","/tmp/avatar.png").ok); auto relogin=s.login("13800000000","123456"); QCOMPARE(relogin.value.displayName, QString("新昵称")); QCOMPARE(relogin.value.avatarPath, QString("/tmp/avatar.png")); }
void stationAndRoute(){ MockUserService s; auto list=s.stations(""); QCOMPARE(list.value.size(),3); QCOMPARE(list.value[0].totalPiles,2); QCOMPARE(list.value[0].availablePiles,1); QVERIFY(s.stations("timeout").error.contains("超时")); auto d=s.route(22.5,113.9,list.value[0],RouteMode::Driving); auto w=s.route(22.5,113.9,list.value[0],RouteMode::Walking); QVERIFY(d.value.mock); QVERIFY(w.value.mock); QVERIFY(d.value.mode!=w.value.mode); QVERIFY(d.value.distanceKm!=w.value.distanceKm); }
void reservationAndSettlement(){ MockUserService s; auto u=s.login("13800000000","123456").value; auto st=s.stations("").value[0]; auto p=s.piles(st.id).value[0]; auto pending=s.createOrder(u.id,st,p); QVERIFY(pending.ok); QCOMPARE(pending.value.status,OrderStatus::PendingReservation); auto reservation=s.confirmReservation(u.id,pending.value.id); QVERIFY(reservation.ok); QCOMPARE(reservation.value.status,OrderStatus::Reserved); QVERIFY(s.startCharging(u.id,reservation.value.id).ok); QVERIFY(s.stopCharging(u.id,reservation.value.id).ok); auto insufficient=s.settle(u.id,reservation.value.id); QVERIFY(!insufficient.ok); QCOMPARE(s.currentOrder(u.id).value.status,OrderStatus::PendingSettlement); QVERIFY(s.recharge(u.id,2000).ok); auto completed=s.settle(u.id,reservation.value.id); QVERIFY(completed.ok); QCOMPARE(completed.value.amountCents,qint64(1860)); QCOMPARE(s.profile(u.id).value.walletBalanceCents,qint64(140)); QVERIFY(!s.settle(u.id,reservation.value.id).ok); }
};
QTEST_MAIN(ClientServiceTest)
#include "client_service_test.moc"
