#pragma once

#include <QString>
#include <QVector>
#include <optional>
#include <QtGlobal>

namespace ev {

enum class PileStatus { Idle, Reserved, Charging, Fault, Maintenance, Unavailable, Offline };
enum class OrderStatus { PendingReservation, Reserved, Charging, PendingSettlement, Completed, Cancelled, Exception };
enum class RouteMode { Driving, Walking };
enum class UserStatus { Active, Frozen, Unknown };

QString pileStatusText(PileStatus status);
QString orderStatusText(OrderStatus status);
bool isValidPhone(const QString &phone);

struct User { QString id; QString phone; QString displayName; qint64 walletBalanceCents{0}; QString avatarPath; UserStatus status{UserStatus::Active}; };
struct Station { QString id; QString name; QString address; double latitude{}; double longitude{}; double distanceKm{}; bool open{}; int availablePiles{}; qint64 priceCentsPerKwh{}; int totalPiles{}; };
// Price is a pile property in Protocol v1 (unit_price_cents_per_kwh), not a station property.
struct Pile { QString id; QString number; QString type; PileStatus status{}; double powerKw{}; qint64 priceCentsPerKwh{}; QString stationId; };
struct Route { bool mock{}; RouteMode mode{RouteMode::Driving}; double distanceKm{}; int durationMin{}; QString summary; };
struct Order { QString id; QString stationId; QString pileId; QString stationName; QString pileNumber; OrderStatus status{}; qint64 priceCentsPerKwh{}; qint64 amountCents{}; QString completedAt; QString stationAddress; };

template <typename T>
struct Result {
  bool ok{false};
  T value{};
  int code{0};
  QString error;
  static Result success(const T &v) { return {true, v, 0, {}}; }
  static Result failure(const QString &e) { return {false, {}, -1, e}; }
  static Result failure(int c, const QString &e) { return {false, {}, c, e}; }
};

class IUserService {
public:
  virtual ~IUserService() = default;
  virtual Result<User> login(const QString &phone) = 0;
  virtual Result<User> login(const QString &phone, const QString &password) { Q_UNUSED(password); return login(phone); }
  virtual Result<User> profile(const QString &userId) = 0;
  virtual Result<User> updateProfile(const QString &userId, const QString &displayName, const QString &avatarPath) = 0;
  virtual Result<qint64> recharge(const QString &userId, qint64 amountCents) = 0;
  virtual Result<QVector<Station>> stations(const QString &query) = 0;
  virtual Result<QVector<Pile>> piles(const QString &stationId) = 0;
  virtual Result<Route> route(double fromLat, double fromLng, const Station &target, RouteMode mode) = 0;
  virtual Result<Order> currentOrder(const QString &userId) = 0;
  virtual Result<QVector<Order>> orderHistory(const QString &userId) = 0;
  virtual Result<Order> createOrder(const QString &userId, const Station &station, const Pile &pile) = 0;
  virtual Result<Order> confirmReservation(const QString &userId, const QString &orderId) = 0;
  virtual Result<Order> cancelReservation(const QString &userId, const QString &orderId) = 0;
  virtual Result<Order> startCharging(const QString &userId, const QString &orderId) = 0;
  virtual Result<Order> startChargingDirect(const QString &userId, const QString &pileId) = 0;
  virtual Result<Order> stopCharging(const QString &userId, const QString &orderId) = 0;
  virtual Result<Order> settle(const QString &userId, const QString &orderId) = 0;
};

class MockUserService final : public IUserService {
public:
  MockUserService();
  Result<User> login(const QString &) override;
  Result<User> login(const QString &, const QString &) override;
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
  Result<Order> startChargingDirect(const QString &, const QString &) override;
  Result<Order> stopCharging(const QString &, const QString &) override;
  Result<Order> settle(const QString &, const QString &) override;
private:
  QVector<Station> stationData_;
  QVector<Pile> pileData_;
  QString registeredPhone_ = QStringLiteral("13800000000");
  QString registeredDisplayName_ = QStringLiteral("??0000");
  QString registeredAvatarPath_;
  QString activeUserId_;
  Order activeOrder_;
  QVector<Order> orderHistory_;
  int nextOrder_ = 1;
  qint64 walletBalanceCents_ = 0;
};

class SessionManager {
public:
  bool isLoggedIn() const { return user_.has_value(); }
  const User &user() const { return *user_; }
  quint64 generation() const { return generation_; }
  void beginSession(const User &user) {
    const bool identityChanged = !user_.has_value() || user_->id != user.id;
    user_ = user;
    if (identityChanged) ++generation_;
  }
  void updateUser(const User &user) { user_ = user; }
  void clear() { if (user_) ++generation_; user_.reset(); }
  void setAvatarPath(const QString &path) { if (user_) user_->avatarPath = path; }
private:
  std::optional<User> user_;
  quint64 generation_{0};
};

} // namespace ev
