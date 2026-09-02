#pragma once

#include <QString>
#include <QVector>
#include <optional>

namespace ev {

enum class PileStatus { Idle, Reserved, Charging, Fault, Maintenance, Unavailable };
enum class OrderStatus { PendingReservation, Reserved, Charging, PendingSettlement, Completed, Cancelled, Exception };

QString pileStatusText(PileStatus status);
QString orderStatusText(OrderStatus status);
bool isValidPhone(const QString &phone);

struct User { QString id; QString phone; QString displayName; double walletBalance{0.0}; QString avatarPath; };
struct Station { QString id; QString name; QString address; double latitude{}; double longitude{}; double distanceKm{}; bool open{}; int availablePiles{}; double pricePerKwh{}; int totalPiles{}; };
struct Pile { QString id; QString number; QString type; PileStatus status{}; double powerKw{}; QString stationId; };
struct Route { bool mock{}; double distanceKm{}; int durationMin{}; QString summary; };
struct Order { QString id; QString stationId; QString pileId; QString stationName; QString pileNumber; OrderStatus status{}; double pricePerKwh{}; double amount{}; QString completedAt; QString stationAddress; };

template <typename T>
struct Result {
  bool ok{false};
  T value{};
  QString error;
  static Result success(const T &v) { return {true, v, {}}; }
  static Result failure(const QString &e) { return {false, {}, e}; }
};

class IUserService {
public:
  virtual ~IUserService() = default;
  virtual Result<User> login(const QString &phone, const QString &password) = 0;
  virtual Result<User> registerUser(const QString &phone, const QString &password, const QString &name) = 0;
  virtual Result<User> profile(const QString &userId) = 0;
  virtual Result<User> updateProfile(const QString &userId, const QString &displayName, const QString &avatarPath) = 0;
  virtual Result<double> recharge(const QString &userId, double amount) = 0;
  virtual Result<QVector<Station>> stations(const QString &query) = 0;
  virtual Result<QVector<Pile>> piles(const QString &stationId) = 0;
  virtual Result<Route> route(double fromLat, double fromLng, const Station &target) = 0;
  virtual Result<Order> currentOrder(const QString &userId) = 0;
  virtual Result<QVector<Order>> orderHistory(const QString &userId) = 0;
  virtual Result<Order> createOrder(const QString &userId, const Station &station, const Pile &pile) = 0;
  virtual Result<Order> cancelReservation(const QString &userId, const QString &orderId) = 0;
  virtual Result<Order> startCharging(const QString &userId, const QString &orderId) = 0;
  virtual Result<Order> stopCharging(const QString &userId, const QString &orderId) = 0;
  virtual Result<Order> settle(const QString &userId, const QString &orderId) = 0;
};

class MockUserService final : public IUserService {
public:
  MockUserService();
  Result<User> login(const QString &, const QString &) override;
  Result<User> registerUser(const QString &, const QString &, const QString &) override;
  Result<User> profile(const QString &) override;
  Result<User> updateProfile(const QString &, const QString &, const QString &) override;
  Result<double> recharge(const QString &, double) override;
  Result<QVector<Station>> stations(const QString &) override;
  Result<QVector<Pile>> piles(const QString &) override;
  Result<Route> route(double, double, const Station &) override;
  Result<Order> currentOrder(const QString &) override;
  Result<QVector<Order>> orderHistory(const QString &) override;
  Result<Order> createOrder(const QString &, const Station &, const Pile &) override;
  Result<Order> cancelReservation(const QString &, const QString &) override;
  Result<Order> startCharging(const QString &, const QString &) override;
  Result<Order> stopCharging(const QString &, const QString &) override;
  Result<Order> settle(const QString &, const QString &) override;
private:
  QVector<Station> stationData_;
  QVector<Pile> pileData_;
  QString registeredPhone_ = QStringLiteral("13800000000");
  QString registeredPassword_ = QStringLiteral("123456");
  QString activeUserId_;
  Order activeOrder_;
  QVector<Order> orderHistory_;
  int nextOrder_ = 1;
  double walletBalance_ = 0.0;
};

class SessionManager {
public:
  bool isLoggedIn() const { return user_.has_value(); }
  const User &user() const { return *user_; }
  void setUser(const User &user) { user_ = user; }
  void clear() { user_.reset(); }
  void setAvatarPath(const QString &path) { if (user_) user_->avatarPath = path; }
private:
  std::optional<User> user_;
};

} // namespace ev
