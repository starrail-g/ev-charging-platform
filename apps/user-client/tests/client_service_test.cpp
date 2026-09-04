#include "client_service.h"
#include "socket_user_service.h"
#include "ev_protocol/frame_codec.h"
#include "ev_protocol/message.h"
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <memory>
#include <thread>
using namespace ev;
using namespace ev::protocol;
class ClientServiceTest : public QObject {
  Q_OBJECT
private slots:
void loginValidation(){ MockUserService s; QVERIFY(s.login("").error.contains("手机号")); QVERIFY(s.login("bad").error.contains("手机号格式")); QVERIFY(s.login("12345678901").ok); QVERIFY(s.login("13800000000").ok); QVERIFY(s.login("13800000000", "ignored-password").ok); }
void unauthorizedAndProfilePersistence(){ MockUserService s; QVERIFY(!s.updateProfile("", "x", {}).ok); QVERIFY(!s.recharge("", 1000).ok); auto u=s.login("13800000000").value; QVERIFY(s.updateProfile(u.id,"新昵称","/tmp/avatar.png").ok); auto relogin=s.login("13800000000"); QCOMPARE(relogin.value.displayName, QString("新昵称")); QCOMPARE(relogin.value.avatarPath, QString("/tmp/avatar.png")); }
void stationAndRoute(){ MockUserService s; auto list=s.stations(""); QCOMPARE(list.value.size(),3); QCOMPARE(list.value[0].totalPiles,2); QCOMPARE(list.value[0].availablePiles,1); QCOMPARE(s.piles(list.value[0].id).value[0].priceCentsPerKwh, qint64(120)); QVERIFY(s.stations("timeout").error.contains("超时")); auto d=s.route(22.5,113.9,list.value[0],RouteMode::Driving); auto w=s.route(22.5,113.9,list.value[0],RouteMode::Walking); QVERIFY(d.value.mock); QVERIFY(w.value.mock); QVERIFY(d.value.mode!=w.value.mode); QVERIFY(d.value.distanceKm!=w.value.distanceKm); }
void socketAdapterFailureCode(){ SocketUserService s(QStringLiteral("127.0.0.1"), 1, 100); auto result=s.login(QStringLiteral("13800000000")); QVERIFY(!result.ok); QVERIFY(result.code == 1500 || result.code == 1000); }
void socketAdapterUnauthorizedGuards(){ SocketUserService s(QStringLiteral("127.0.0.1"), 1, 100); QCOMPARE(s.login("").code, 1002); QCOMPARE(s.profile("").code, 1100); QCOMPARE(s.recharge("", 100).code, 1100); QCOMPARE(s.currentOrder("").code, 1100); QCOMPARE(s.createOrder("", {}, {}).code, 1100); }
void socketMutationRetryReusesRequestId(){
  QSemaphore ready;
  quint16 port = 0;
  QStringList requestIds;
  QString serverFailure;
  std::thread serverThread([&] {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
      serverFailure = server.errorString();
      ready.release();
      return;
    }
    port = server.serverPort();
    ready.release();
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (!server.waitForNewConnection(2000)) {
        serverFailure = QStringLiteral("test server did not receive connection %1").arg(attempt + 1);
        return;
      }
      std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
      FrameDecoder decoder;
      QList<Message> messages;
      QElapsedTimer timer;
      timer.start();
      while (messages.isEmpty() && timer.elapsed() < 2000) {
        if (!socket->bytesAvailable() && !socket->waitForReadyRead(2000 - int(timer.elapsed()))) break;
        QString frameError;
        ErrorCode frameCode = ErrorCode::Ok;
        messages = decoder.feed(socket->readAll(), &frameError, &frameCode);
        if (!frameError.isEmpty()) {
          serverFailure = frameError;
          return;
        }
      }
      if (messages.size() != 1) {
        serverFailure = QStringLiteral("test server did not decode one request");
        return;
      }
      requestIds.push_back(messages.first().id);
      if (attempt == 0) {
        socket->disconnectFromHost();
        continue;
      }
      const qint64 balance = attempt == 1 ? 100 : 200;
      socket->write(encodeFrame(Message{kProtocolVersion,
                                         messages.first().id,
                                         QStringLiteral("wallet.recharge.result"),
                                         QJsonObject{{QStringLiteral("balance_cents"), balance},
                                                     {QStringLiteral("transaction_id"), attempt}}}));
      if (!socket->waitForBytesWritten(1000)) {
        serverFailure = socket->errorString();
        return;
      }
    }
  });

  ready.acquire();
  Result<qint64> lostResponse;
  Result<qint64> retry;
  Result<qint64> nextOperation;
  if (port != 0) {
    SocketUserService service(QStringLiteral("127.0.0.1"), port, 250);
    lostResponse = service.recharge(QStringLiteral("1"), 100);
    retry = service.recharge(QStringLiteral("1"), 100);
    nextOperation = service.recharge(QStringLiteral("1"), 100);
  }
  serverThread.join();

  QVERIFY2(port != 0, qPrintable(serverFailure));
  QVERIFY(!lostResponse.ok);
  QCOMPARE(lostResponse.code, 1500);
  QVERIFY2(retry.ok, qPrintable(retry.error));
  QCOMPARE(retry.value, qint64(100));
  QVERIFY2(nextOperation.ok, qPrintable(nextOperation.error));
  QCOMPARE(nextOperation.value, qint64(200));

  QVERIFY2(serverFailure.isEmpty(), qPrintable(serverFailure));
  QCOMPARE(requestIds.size(), 3);
  QCOMPARE(requestIds.at(0), requestIds.at(1));
  QVERIFY(requestIds.at(2) != requestIds.at(1));
}
void socketAdapterLifecycle(){
  if (qEnvironmentVariable("EV_RUN_SOCKET_INTEGRATION") != QStringLiteral("1"))
    QSKIP("set EV_RUN_SOCKET_INTEGRATION=1 to run against the B service");
  SocketUserService s;
  const auto user = s.login(QStringLiteral("13600136000"));
  QVERIFY2(user.ok, qPrintable(user.error));
  const auto current = s.currentOrder(user.value.id);
  QVERIFY2(current.ok, qPrintable(current.error));
  Order order = current.value;
  if (order.id.isEmpty()) {
    const auto stationResult = s.stations(QString());
    QVERIFY2(stationResult.ok, qPrintable(stationResult.error));
    bool created = false;
    for (const Station &station : stationResult.value) {
      const auto pileResult = s.piles(station.id);
      QVERIFY2(pileResult.ok, qPrintable(pileResult.error));
      for (const Pile &pile : pileResult.value) {
        if (pile.status != PileStatus::Idle) continue;
        const auto pending = s.createOrder(user.value.id, station, pile);
        QVERIFY2(pending.ok, qPrintable(pending.error));
        order = pending.value;
        created = true;
        break;
      }
      if (created) break;
    }
    QVERIFY2(created, "integration database has no idle pile");
  }
  if (order.status == OrderStatus::PendingReservation) {
    const auto confirmed = s.confirmReservation(user.value.id, order.id);
    QVERIFY2(confirmed.ok, qPrintable(confirmed.error));
    order = confirmed.value;
  }
  if (order.status == OrderStatus::Reserved) {
    const auto started = s.startCharging(user.value.id, order.id);
    QVERIFY2(started.ok, qPrintable(started.error));
    order = started.value;
  }
  QCOMPARE(order.status, OrderStatus::Charging);
  const auto stopped = s.stopCharging(user.value.id, order.id);
  QVERIFY2(stopped.ok, qPrintable(stopped.error));
  order = stopped.value;
  QCOMPARE(order.status, OrderStatus::PendingSettlement);
  const auto settled = s.settle(user.value.id, order.id);
  QVERIFY2(settled.ok, qPrintable(settled.error));
  QCOMPARE(settled.value.status, OrderStatus::Completed);
  const auto history = s.orderHistory(user.value.id);
  QVERIFY2(history.ok, qPrintable(history.error));
  QVERIFY(!history.value.isEmpty());
  QCOMPARE(history.value.first().status, OrderStatus::Completed);
}
void socketAdapterDirectStart(){
  if (qEnvironmentVariable("EV_RUN_SOCKET_INTEGRATION") != QStringLiteral("1"))
    QSKIP("set EV_RUN_SOCKET_INTEGRATION=1 to run against the B service");
  SocketUserService s;
  const auto login = s.login(QStringLiteral("13412345678"));
  QVERIFY2(login.ok, qPrintable(login.error));
  const auto current = s.currentOrder(login.value.id);
  QVERIFY2(current.ok, qPrintable(current.error));
  QVERIFY2(current.value.id.isEmpty(), "direct-start test user has an unfinished order");
  bool started = false;
  Order order;
  for (const auto &station : s.stations(QString()).value) {
    const auto piles = s.piles(station.id);
    QVERIFY2(piles.ok, qPrintable(piles.error));
    for (const auto &pile : piles.value) {
      if (pile.status != PileStatus::Idle) continue;
      const auto result = s.startChargingDirect(login.value.id, pile.id);
      if (!result.ok) continue;
      order = result.value;
      started = true;
      break;
    }
    if (started) break;
  }
  QVERIFY2(started, "integration database has no idle pile for direct start");
  QCOMPARE(order.status, OrderStatus::Charging);
  const auto stopped = s.stopCharging(login.value.id, order.id);
  QVERIFY2(stopped.ok, qPrintable(stopped.error));
  QCOMPARE(stopped.value.status, OrderStatus::PendingSettlement);
  QVERIFY2(s.recharge(login.value.id, 5000).ok, "direct-start test recharge failed");
  const auto settled = s.settle(login.value.id, order.id);
  QVERIFY2(settled.ok, qPrintable(settled.error));
  QCOMPARE(settled.value.status, OrderStatus::Completed);
}
void socketAdapterProfileWallet(){
  if (qEnvironmentVariable("EV_RUN_SOCKET_INTEGRATION") != QStringLiteral("1"))
    QSKIP("set EV_RUN_SOCKET_INTEGRATION=1 to run against the B service");
  SocketUserService s;
  const auto login = s.login(QStringLiteral("13200132000"));
  QVERIFY2(login.ok, qPrintable(login.error));
  const auto profile = s.profile(login.value.id);
  QVERIFY2(profile.ok, qPrintable(profile.error));
  QCOMPARE(profile.value.phone, QStringLiteral("13200132000"));
  const auto updated = s.updateProfile(login.value.id, QStringLiteral("Socket用户"), QStringLiteral("avatars/socket.png"));
  QVERIFY2(updated.ok, qPrintable(updated.error));
  QCOMPARE(updated.value.displayName, QStringLiteral("Socket用户"));
  QCOMPARE(updated.value.avatarPath, QStringLiteral("avatars/socket.png"));
  const auto balance = s.recharge(login.value.id, 5000);
  QVERIFY2(balance.ok, qPrintable(balance.error));
  QVERIFY(balance.value >= 5000);
}
void socketAdapterFrozenPolicy(){
  if (qEnvironmentVariable("EV_RUN_SOCKET_INTEGRATION") != QStringLiteral("1"))
    QSKIP("set EV_RUN_SOCKET_INTEGRATION=1 to run against the B service");
  SocketUserService s;
  const auto login = s.login(QStringLiteral("13700137000"));
  QVERIFY2(login.ok, qPrintable(login.error));
  QCOMPARE(login.value.status, UserStatus::Frozen);
  QVERIFY2(s.profile(login.value.id).ok, "frozen profile read must remain allowed");
  const auto current = s.currentOrder(login.value.id);
  QVERIFY2(current.ok, qPrintable(current.error));
  const auto recharge = s.recharge(login.value.id, 100);
  QVERIFY(!recharge.ok);
  QCOMPARE(recharge.code, 1101);
  QVERIFY(recharge.error.contains(QStringLiteral("冻结")));
}void directStart(){ MockUserService s; auto u=s.login("12345678901").value; auto st=s.stations("").value[0]; auto p=s.piles(st.id).value[0]; const auto started=s.startChargingDirect(u.id,p.id); QVERIFY2(started.ok,qPrintable(started.error)); QCOMPARE(started.value.status,OrderStatus::Charging); QCOMPARE(s.piles(st.id).value[0].status,PileStatus::Charging); QVERIFY(!s.startChargingDirect(u.id,p.id).ok); QVERIFY(s.stopCharging(u.id,started.value.id).ok); }
void reservationAndSettlement(){ MockUserService s; auto u=s.login("13800000000").value; auto st=s.stations("").value[0]; auto p=s.piles(st.id).value[0]; auto pending=s.createOrder(u.id,st,p); QVERIFY(pending.ok); QCOMPARE(pending.value.status,OrderStatus::PendingReservation); auto reservation=s.confirmReservation(u.id,pending.value.id); QVERIFY(reservation.ok); QCOMPARE(reservation.value.status,OrderStatus::Reserved); QVERIFY(s.startCharging(u.id,reservation.value.id).ok); QVERIFY(s.stopCharging(u.id,reservation.value.id).ok); auto insufficient=s.settle(u.id,reservation.value.id); QVERIFY(!insufficient.ok); QCOMPARE(s.currentOrder(u.id).value.status,OrderStatus::PendingSettlement); QVERIFY(s.recharge(u.id,2000).ok); auto completed=s.settle(u.id,reservation.value.id); QVERIFY(completed.ok); QCOMPARE(completed.value.amountCents,qint64(1860)); QCOMPARE(s.profile(u.id).value.walletBalanceCents,qint64(140)); QVERIFY(!s.settle(u.id,reservation.value.id).ok); }
};
QTEST_MAIN(ClientServiceTest)
#include "client_service_test.moc"
