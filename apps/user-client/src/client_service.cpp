#include "client_service.h"

#include <QRegularExpression>

namespace ev {

QString pileStatusText(PileStatus status) {
  switch (status) {
  case PileStatus::Idle: return QStringLiteral("闲置");
  case PileStatus::Reserved: return QStringLiteral("已预约");
  case PileStatus::Charging: return QStringLiteral("充电中");
  case PileStatus::Fault: return QStringLiteral("故障");
  case PileStatus::Maintenance: return QStringLiteral("维护");
  case PileStatus::Unavailable: return QStringLiteral("暂不可用");
  case PileStatus::Offline: return QStringLiteral("离线");
  }
  return QStringLiteral("未知");
}

QString orderStatusText(OrderStatus status) {
  switch (status) {
  case OrderStatus::PendingReservation: return QStringLiteral("待预约");
  case OrderStatus::Reserved: return QStringLiteral("已预约");
  case OrderStatus::Charging: return QStringLiteral("充电中");
  case OrderStatus::PendingSettlement: return QStringLiteral("待结算");
  case OrderStatus::Completed: return QStringLiteral("已完成");
  case OrderStatus::Cancelled: return QStringLiteral("已取消");
  case OrderStatus::Exception: return QStringLiteral("异常");
  }
  return QStringLiteral("未知");
}

MockUserService::MockUserService() {
  stationData_ = {
      {QStringLiteral("S001"), QStringLiteral("科技园充电站"), QStringLiteral("科苑路 1 号"), 22.5401, 113.9345, 1.2, true, 2, 120, 2},
      {QStringLiteral("S002"), QStringLiteral("软件园南区"), QStringLiteral("学府大道 88 号"), 22.5268, 113.9432, 3.8, true, 1, 135, 1},
      {QStringLiteral("S003"), QStringLiteral("滨海停车场"), QStringLiteral("后海大道 6 号"), 22.5154, 113.9461, 6.1, false, 0, 110, 0}};
  pileData_ = {
      {QStringLiteral("P001"), QStringLiteral("A01"), QStringLiteral("直流快充"), PileStatus::Idle, 120, 120, QStringLiteral("S001")},
      {QStringLiteral("P002"), QStringLiteral("A02"), QStringLiteral("交流慢充"), PileStatus::Charging, 7, 120, QStringLiteral("S001")},
      {QStringLiteral("P003"), QStringLiteral("B01"), QStringLiteral("直流快充"), PileStatus::Fault, 60, 135, QStringLiteral("S002")},
      {QStringLiteral("P004"), QStringLiteral("C01"), QStringLiteral("直流快充"), PileStatus::Idle, 90, 135, QStringLiteral("S002")}};
}

bool isValidPhone(const QString &phone) { return QRegularExpression(QStringLiteral("^[0-9]{11}$")).match(phone).hasMatch(); }

Result<User> MockUserService::login(const QString &phone) {
  if (phone == QStringLiteral("timeout")) return Result<User>::failure(1500, QStringLiteral("请求超时，请稍后重试"));
  if (phone == QStringLiteral("server-error")) return Result<User>::failure(1500, QStringLiteral("服务暂不可用"));
  if (phone.trimmed().isEmpty()) return Result<User>::failure(1002, QStringLiteral("请输入手机号"));
  if (!isValidPhone(phone)) return Result<User>::failure(1002, QStringLiteral("手机号格式错误"));
  if (phone != registeredPhone_) { registeredPhone_ = phone; registeredDisplayName_ = QStringLiteral("用户") + phone.right(4); registeredAvatarPath_.clear(); walletBalanceCents_ = 0; activeOrder_ = Order{}; orderHistory_.clear(); }
  activeUserId_ = QStringLiteral("U001");
  return Result<User>::success({activeUserId_, registeredPhone_, registeredDisplayName_, walletBalanceCents_, registeredAvatarPath_});
}
Result<User> MockUserService::login(const QString &phone, const QString &password) { Q_UNUSED(password); return login(phone); }
Result<User> MockUserService::profile(const QString &userId) { if (userId.isEmpty() || userId != activeUserId_) return Result<User>::failure(QStringLiteral("会话已失效，请重新登录")); return Result<User>::success({userId, registeredPhone_, registeredDisplayName_, walletBalanceCents_, registeredAvatarPath_}); }
Result<User> MockUserService::updateProfile(const QString &userId, const QString &displayName, const QString &avatarPath) { if (userId.isEmpty() || userId != activeUserId_) return Result<User>::failure(QStringLiteral("会话已失效，请重新登录")); if (displayName.trimmed().isEmpty()) return Result<User>::failure(QStringLiteral("昵称不能为空")); registeredDisplayName_ = displayName.trimmed(); registeredAvatarPath_ = avatarPath; return Result<User>::success({userId, registeredPhone_, registeredDisplayName_, walletBalanceCents_, registeredAvatarPath_}); }
Result<qint64> MockUserService::recharge(const QString &userId, qint64 amountCents) { if (userId.isEmpty() || userId != activeUserId_) return Result<qint64>::failure(QStringLiteral("会话已失效，请重新登录")); if (amountCents <= 0 || amountCents > 1000000) return Result<qint64>::failure(QStringLiteral("充值金额应在 0 到 10000 元之间")); walletBalanceCents_ += amountCents; return Result<qint64>::success(walletBalanceCents_); }
Result<QVector<Station>> MockUserService::stations(const QString &query) {
  if (query == QStringLiteral("timeout")) return Result<QVector<Station>>::failure(QStringLiteral("查询超时，请重试"));
  if (query == QStringLiteral("error")) return Result<QVector<Station>>::failure(QStringLiteral("站点服务暂不可用"));
  QVector<Station> result;
  for (auto station : stationData_) {
    if (!query.trimmed().isEmpty() && !station.name.contains(query, Qt::CaseInsensitive) && !station.address.contains(query, Qt::CaseInsensitive)) continue;
    station.totalPiles = 0;
    station.availablePiles = 0;
    for (const auto &pile : pileData_) {
      if (pile.stationId != station.id) continue;
      ++station.totalPiles;
      if (pile.status == PileStatus::Idle) ++station.availablePiles;
    }
    result.push_back(station);
  }
  return Result<QVector<Station>>::success(result);
}
Result<QVector<Pile>> MockUserService::piles(const QString &stationId) {
  if (stationId == QStringLiteral("error")) return Result<QVector<Pile>>::failure(QStringLiteral("充电桩查询失败"));
  QVector<Pile> result;
  for (const auto &pile : pileData_) if (pile.stationId == stationId) result.push_back(pile);
  return Result<QVector<Pile>>::success(result);
}
Result<Route> MockUserService::route(double, double, const Station &target, RouteMode mode) { if (target.id == QStringLiteral("S003")) return Result<Route>::failure(QStringLiteral("路线暂无结果")); const bool driving = mode == RouteMode::Driving; const QString modeText = driving ? QStringLiteral("驾车") : QStringLiteral("步行"); return Result<Route>::success({true, mode, target.distanceKm * (driving ? 1.0 : 1.25) + 0.4, static_cast<int>(target.distanceKm * (driving ? 3 : 12) + (driving ? 5 : 12)), QStringLiteral("离线 Mock %1 路线：沿主干道前往 %2").arg(modeText, target.name)}); }
Result<Order> MockUserService::currentOrder(const QString &userId) { if (userId.isEmpty() || userId != activeUserId_) return Result<Order>::failure(QStringLiteral("未授权")); return Result<Order>::success(activeOrder_); }
Result<QVector<Order>> MockUserService::orderHistory(const QString &userId) {
  if (userId.isEmpty() || userId != activeUserId_) return Result<QVector<Order>>::failure(QStringLiteral("未授权"));
  QVector<Order> latestFirst;
  for (auto it = orderHistory_.crbegin(); it != orderHistory_.crend(); ++it) latestFirst.push_back(*it);
  return Result<QVector<Order>>::success(latestFirst);
}
Result<Order> MockUserService::createOrder(const QString &userId, const Station &station, const Pile &pile) {
  if (userId.isEmpty() || userId != activeUserId_) return Result<Order>::failure(QStringLiteral("请先登录"));
  if (!activeOrder_.id.isEmpty() && activeOrder_.status != OrderStatus::Completed && activeOrder_.status != OrderStatus::Cancelled) return Result<Order>::failure(QStringLiteral("已有活动订单，请先完成结算"));
  if (pile.status != PileStatus::Idle) return Result<Order>::failure(QStringLiteral("该充电桩当前不可用"));
  for (auto &storedPile : pileData_) if (storedPile.id == pile.id && storedPile.stationId == station.id) storedPile.status = PileStatus::Reserved;
  const qint64 price = pile.priceCentsPerKwh > 0 ? pile.priceCentsPerKwh : station.priceCentsPerKwh;
  activeOrder_ = {QStringLiteral("O%1").arg(nextOrder_++), station.id, pile.id, station.name, pile.number, OrderStatus::PendingReservation, price, 0, {}, {}}; activeOrder_.stationAddress = station.address; return Result<Order>::success(activeOrder_);
}
Result<Order> MockUserService::confirmReservation(const QString &userId, const QString &orderId) { if (userId.isEmpty() || userId != activeUserId_ || activeOrder_.id != orderId) return Result<Order>::failure(QStringLiteral("订单不存在")); if (activeOrder_.status != OrderStatus::PendingReservation) return Result<Order>::failure(QStringLiteral("当前订单不在待确认状态")); activeOrder_.status = OrderStatus::Reserved; return Result<Order>::success(activeOrder_); }
Result<Order> MockUserService::cancelReservation(const QString &userId, const QString &orderId) {
  if (userId.isEmpty() || userId != activeUserId_ || activeOrder_.id != orderId) return Result<Order>::failure(QStringLiteral("订单不存在"));
  if (activeOrder_.status != OrderStatus::Reserved && activeOrder_.status != OrderStatus::PendingReservation) return Result<Order>::failure(QStringLiteral("当前订单不是预约状态"));
  for (auto &pile : pileData_) if (pile.id == activeOrder_.pileId) pile.status = PileStatus::Idle;
  activeOrder_.status = OrderStatus::Cancelled;
  return Result<Order>::success(activeOrder_);
}
Result<Order> MockUserService::startCharging(const QString &userId, const QString &orderId) { if (userId.isEmpty() || userId != activeUserId_ || activeOrder_.id != orderId) return Result<Order>::failure(QStringLiteral("订单不存在")); if (activeOrder_.status != OrderStatus::Reserved) return Result<Order>::failure(QStringLiteral("订单状态不允许开始充电")); for (auto &pile : pileData_) if (pile.id == activeOrder_.pileId) pile.status = PileStatus::Charging; activeOrder_.status = OrderStatus::Charging; return Result<Order>::success(activeOrder_); }
Result<Order> MockUserService::startChargingDirect(const QString &userId, const QString &pileId) {
  if (userId.isEmpty() || userId != activeUserId_) return Result<Order>::failure(QStringLiteral("请先登录"));
  if (!activeOrder_.id.isEmpty() && activeOrder_.status != OrderStatus::Completed && activeOrder_.status != OrderStatus::Cancelled) return Result<Order>::failure(QStringLiteral("已有活动订单，请先完成结算"));
  for (auto &pile : pileData_) {
    if (pile.id != pileId) continue;
    if (pile.status != PileStatus::Idle) return Result<Order>::failure(QStringLiteral("该充电桩当前不可用"));
    pile.status = PileStatus::Charging;
    activeOrder_ = {QStringLiteral("O%1").arg(nextOrder_++), pile.stationId, pile.id, {}, pile.number, OrderStatus::Charging, pile.priceCentsPerKwh, 0, {}, {}};
    for (const auto &station : stationData_) if (station.id == pile.stationId) { activeOrder_.stationName = station.name; activeOrder_.stationAddress = station.address; break; }
    return Result<Order>::success(activeOrder_);
  }
  return Result<Order>::failure(QStringLiteral("充电桩不存在"));
}
Result<Order> MockUserService::stopCharging(const QString &userId, const QString &orderId) { if (userId.isEmpty() || userId != activeUserId_ || activeOrder_.id != orderId) return Result<Order>::failure(QStringLiteral("订单不存在")); if (activeOrder_.status != OrderStatus::Charging) return Result<Order>::failure(QStringLiteral("当前未在充电")); for (auto &pile : pileData_) if (pile.id == activeOrder_.pileId) pile.status = PileStatus::Idle; activeOrder_.status = OrderStatus::PendingSettlement; activeOrder_.amountCents = 1860; return Result<Order>::success(activeOrder_); }
Result<Order> MockUserService::settle(const QString &userId, const QString &orderId) { if (userId.isEmpty() || userId != activeUserId_ || activeOrder_.id != orderId) return Result<Order>::failure(QStringLiteral("订单不存在")); if (activeOrder_.status != OrderStatus::PendingSettlement) return Result<Order>::failure(QStringLiteral("订单尚未达到结算条件")); if (walletBalanceCents_ < activeOrder_.amountCents) return Result<Order>::failure(QStringLiteral("余额不足，无法结算 ¥ %1").arg(activeOrder_.amountCents / 100.0, 0, 'f', 2)); walletBalanceCents_ -= activeOrder_.amountCents; activeOrder_.status = OrderStatus::Completed; activeOrder_.completedAt = QStringLiteral("2026-09-02T12:%1:00Z").arg(orderHistory_.size(), 2, 10, QChar('0')); if (orderHistory_.isEmpty() || orderHistory_.last().id != activeOrder_.id) orderHistory_.push_back(activeOrder_); return Result<Order>::success(activeOrder_); }

} // namespace ev
