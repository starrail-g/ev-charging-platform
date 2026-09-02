#include "client_service.h"
#include <QtTest>

using namespace ev;
class ClientServiceTest : public QObject {
  Q_OBJECT
private slots:
  void loginValidation(){ MockUserService s; QVERIFY(s.login("", "123456").error.contains("手机号")); QVERIFY(s.login("13800000000", "").ok); QVERIFY(s.login("13800000000", "123").ok); QVERIFY(!s.login("bad", "123456").ok); QVERIFY(s.login("13800000000", "123456").ok); auto autoRegistered=s.login("13900000000", ""); QVERIFY(autoRegistered.ok); }
  void stationAndRoute(){ MockUserService s; auto list=s.stations(""); QCOMPARE(list.value.size(),3); QCOMPARE(list.value[0].totalPiles,2); QCOMPARE(list.value[0].availablePiles,1); QVERIFY(s.stations("timeout").error.contains("超时")); QVERIFY(s.route(0,0,list.value[0]).value.mock); }
  void orderTransitionsAndDuplicate(){ MockUserService s; auto u=s.login("13800000000","123456").value; auto st=s.stations("").value[0]; auto p=s.piles(st.id).value[0]; auto reservation=s.createOrder(u.id,st,p); QVERIFY(reservation.ok); QCOMPARE(s.piles(st.id).value[0].status, PileStatus::Reserved); QVERIFY(s.cancelReservation(u.id,reservation.value.id).ok); QCOMPARE(s.piles(st.id).value[0].status, PileStatus::Idle); QCOMPARE(s.currentOrder(u.id).value.status, OrderStatus::Cancelled); auto o=s.createOrder(u.id,st,p); QVERIFY(o.ok); QCOMPARE(s.piles(st.id).value[0].status, PileStatus::Reserved); QVERIFY(!s.createOrder(u.id,st,p).ok); QVERIFY(s.startCharging(u.id,o.value.id).ok); QCOMPARE(s.piles(st.id).value[0].status, PileStatus::Charging); QVERIFY(s.stopCharging(u.id,o.value.id).ok); QCOMPARE(s.piles(st.id).value[0].status, PileStatus::Idle); QVERIFY(s.settle(u.id,o.value.id).ok); QVERIFY(!s.settle(u.id,o.value.id).ok); auto second=s.createOrder(u.id,st,p); QVERIFY(second.ok); QVERIFY(s.startCharging(u.id,second.value.id).ok); QVERIFY(s.stopCharging(u.id,second.value.id).ok); QVERIFY(s.settle(u.id,second.value.id).ok); auto history=s.orderHistory(u.id).value; QCOMPARE(history.size(),2); QCOMPARE(history.first().id,second.value.id); QVERIFY(!history.first().completedAt.isEmpty()); QCOMPARE(history.first().stationAddress,st.address); auto registered=s.registerUser("13900000000","abcdef","新用户"); QVERIFY(registered.ok); QVERIFY(s.currentOrder(registered.value.id).value.id.isEmpty()); QVERIFY(s.orderHistory(registered.value.id).value.isEmpty()); }
};
QTEST_MAIN(ClientServiceTest)
#include "client_service_test.moc"
