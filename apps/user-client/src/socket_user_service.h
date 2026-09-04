#pragma once

#include "client_service.h"
#include <QJsonValue>
#include <QHash>
#include <QMutex>
#include <QHostAddress>

namespace ev {

class SocketUserService final : public IUserService {
public:
  explicit SocketUserService(QString host = {}, quint16 port = 0, int timeoutMs = 3000);
  Result<User> login(const QString &) override;
  Result<User> login(const QString &, const QString &) override;
  Result<User> registerUser(const QString &, const QString &, const QString &) override;
  Result<User> profile(const QString &) override;
  Result<User> updateProfile(const QString &, const QString &, const QString &) override;
  Result<qint64> recharge(const QString &, qint64) override;
  Result<QVector<Station>> stations(const QString &) override;
  Result<QVector<Pile>> piles(const QString &) override;
  Result<Route> route(double, double, const Station &, RouteMode) override;
  Result<Order> currentOrder(const QString &) override;
  Result<QVector<Order>> orderHistory(const QString &) override;
  Result<Order> createOrder(const QString &, const Station &, const Pile &) override;
  Result<Order> confirmReservation(const QString &, const QString &) override;
  Result<Order> cancelReservation(const QString &, const QString &) override;
  Result<Order> startCharging(const QString &, const QString &) override;
  Result<Order> startChargingDirect(const QString &userId, const QString &pileId);
  Result<Order> stopCharging(const QString &, const QString &) override;
  Result<Order> settle(const QString &, const QString &) override;
private:
  Result<QJsonObject> call(const QString &type, const QJsonObject &payload, const QString &idempotencyKey = {}) const;
  QString requestIdFor(const QString &key) const;
  static Result<User> userFrom(const QJsonObject &payload);
  static Result<Order> orderFrom(const QJsonObject &payload);
  static Result<QVector<Order>> ordersFrom(const QJsonObject &payload);
  static QString wireId(const QString &id);
  static QJsonValue wireValue(const QString &id);
  QString host_;
  quint16 port_;
  int timeoutMs_;
  mutable quint64 nextRequestId_{1};
  mutable QHash<QString, QString> pendingRequestIds_;
  mutable QMutex requestMutex_;
};

} // namespace ev
