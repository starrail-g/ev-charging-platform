#include "socket_user_service.h"

#include "ev_protocol/frame_codec.h"
#include "ev_protocol/message.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QUuid>
#include <QTcpSocket>
#include <QMutexLocker>
#include <QtMath>

#include <cmath>

namespace ev {
using namespace ev::protocol;

SocketUserService::SocketUserService(QString host, quint16 port, int timeoutMs)
    : host_(std::move(host)), port_(port), timeoutMs_(qMax(1, timeoutMs)) {
  if (host_.trimmed().isEmpty()) {
    host_ = qEnvironmentVariable("EV_SERVER_HOST", "127.0.0.1");
    if (host_.trimmed().isEmpty()) host_ = QStringLiteral("127.0.0.1");
  }
  if (port_ == 0) {
    bool ok = false;
    const uint configuredPort = qEnvironmentVariable("EV_SERVER_PORT", "45454").toUInt(&ok);
    port_ = (ok && configuredPort > 0 && configuredPort <= 65535)
                ? static_cast<quint16>(configuredPort)
                : static_cast<quint16>(45454);
  }
}

QString SocketUserService::requestIdFor(const QString &key) const {
  QMutexLocker locker(&requestMutex_);
  if (!key.isEmpty()) {
    const auto existing = pendingRequestIds_.value(key);
    if (!existing.isEmpty()) return existing;
  }
  const QString id = QStringLiteral("user-%1-%2").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)).arg(nextRequestId_++);
  if (!key.isEmpty()) {
    if (pendingRequestIds_.size() >= 256) pendingRequestIds_.erase(pendingRequestIds_.begin());
    pendingRequestIds_.insert(key, id);
  }
  return id;
}

Result<QJsonObject> SocketUserService::call(const QString &type, const QJsonObject &payload, const QString &idempotencyKey) const {
  QTcpSocket socket;
  const QString id = requestIdFor(idempotencyKey);
  socket.connectToHost(host_, port_);
  if (!socket.waitForConnected(timeoutMs_)) return Result<QJsonObject>::failure(1500, QStringLiteral("服务连接失败，请稍后重试"));
  const QByteArray frame = encodeFrame(Message{kProtocolVersion, id, type, payload});
  qint64 written = 0;
  while (written < frame.size()) {
    const qint64 chunk = socket.write(frame.constData() + written, frame.size() - written);
    if (chunk <= 0) return Result<QJsonObject>::failure(1500, QStringLiteral("请求发送失败，请稍后重试"));
    written += chunk;
    if (!socket.waitForBytesWritten(timeoutMs_)) return Result<QJsonObject>::failure(1500, QStringLiteral("请求发送失败，请稍后重试"));
  }
  FrameDecoder decoder;
  QElapsedTimer timer; timer.start();
  while (timer.elapsed() < timeoutMs_) {
    if (!socket.waitForReadyRead(qMax(1, timeoutMs_ - int(timer.elapsed())))) break;
    QString frameError; ErrorCode frameCode = ErrorCode::Ok;
    const auto messages = decoder.feed(socket.readAll(), &frameError, &frameCode);
    if (!frameError.isEmpty()) return Result<QJsonObject>::failure(static_cast<int>(frameCode), QStringLiteral("协议帧错误"));
    for (const auto &message : messages) {
      if (message.id != id) continue;
      if (message.type == QStringLiteral("error")) {
        const int code = message.payload.value(QStringLiteral("code")).toInt(static_cast<int>(ErrorCode::InternalError));
        QString messageText = message.payload.value(QStringLiteral("message")).toString(QStringLiteral("服务端请求失败"));
        if (code == 1101) messageText = QStringLiteral("账号已冻结，当前操作不可用");
        else if (code == 1100) messageText = QStringLiteral("请先登录或会话已失效");
        else if (code == 1202) messageText = QStringLiteral("余额不足，无法结算");
        return Result<QJsonObject>::failure(code, messageText);
      }
      const QString expectedType = type + QStringLiteral(".result");
      if (message.type != expectedType) {
        return Result<QJsonObject>::failure(static_cast<int>(ErrorCode::InvalidRequest), QStringLiteral("服务端响应类型错误"));
      }
      if (!idempotencyKey.isEmpty()) { QMutexLocker locker(&requestMutex_); pendingRequestIds_.remove(idempotencyKey); }
      return Result<QJsonObject>::success(message.payload);
    }
  }
  return Result<QJsonObject>::failure(1500, QStringLiteral("请求超时，请稍后重试"));
}

QString SocketUserService::wireId(const QString &id) { QString digits = id; if (!digits.isEmpty() && digits.front().isLetter()) digits.remove(0, 1); bool ok = false; const qint64 value = digits.toLongLong(&ok); return ok && value > 0 ? QString::number(value) : QString(); }
QJsonValue SocketUserService::wireValue(const QString &id) {
  bool ok = false;
  const qint64 value = wireId(id).toLongLong(&ok);
  return ok && value > 0 ? QJsonValue(value) : QJsonValue();
}

Result<User> SocketUserService::userFrom(const QJsonObject &payload) {
  const auto object = payload.value(QStringLiteral("user")).toObject();
  if (object.isEmpty()) return Result<User>::failure(1002, QStringLiteral("响应缺少用户信息"));
  if (!object.contains(QStringLiteral("id")) || !object.contains(QStringLiteral("phone"))
      || !object.contains(QStringLiteral("nickname")) || !object.contains(QStringLiteral("balance_cents"))
      || !object.contains(QStringLiteral("status"))) {
    return Result<User>::failure(1002, QStringLiteral("响应缺少必要的用户字段"));
  }
  const QString status = object.value(QStringLiteral("status")).toString();
  UserStatus userStatus = UserStatus::Unknown;
  if (status == QStringLiteral("active")) userStatus = UserStatus::Active;
  else if (status == QStringLiteral("frozen")) userStatus = UserStatus::Frozen;
  return Result<User>::success({QString::number(object.value(QStringLiteral("id")).toInteger()), object.value(QStringLiteral("phone")).toString(), object.value(QStringLiteral("nickname")).toString(), object.value(QStringLiteral("balance_cents")).toInteger(), object.value(QStringLiteral("avatar_path")).toString(), userStatus});
}

Result<Order> SocketUserService::orderFrom(const QJsonObject &payload) {
  const auto orderValue = payload.value(QStringLiteral("order"));
  if (orderValue.isNull()) return Result<Order>::success({});
  if (!orderValue.isObject()) return Result<Order>::failure(1002, QStringLiteral("响应中的订单信息无效"));
  const auto object = orderValue.toObject();
  if (object.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("响应缺少订单信息"));
  const QString status = object.value(QStringLiteral("status")).toString();
  OrderStatus state = OrderStatus::Exception;
  if (status == QStringLiteral("pending_reservation")) state = OrderStatus::PendingReservation; else if (status == QStringLiteral("reserved")) state = OrderStatus::Reserved; else if (status == QStringLiteral("charging")) state = OrderStatus::Charging; else if (status == QStringLiteral("pending_settlement")) state = OrderStatus::PendingSettlement; else if (status == QStringLiteral("completed")) state = OrderStatus::Completed; else if (status == QStringLiteral("cancelled")) state = OrderStatus::Cancelled;
  const qint64 price = object.value(QStringLiteral("unit_price_cents_per_kwh")).toInteger();
  const QString completedAt = object.value(QStringLiteral("settled_at")).toString(object.value(QStringLiteral("ended_at")).toString());
  const QString pileNumber = object.value(QStringLiteral("pile_code")).toString();
  return Result<Order>::success({QString::number(object.value(QStringLiteral("id")).toInteger()), {}, QString::number(object.value(QStringLiteral("pile_id")).toInteger()), object.value(QStringLiteral("station_name")).toString(), pileNumber, state, price, object.value(QStringLiteral("total_amount_cents")).toInteger(), completedAt, object.value(QStringLiteral("station_address")).toString()});
}

Result<QVector<Order>> SocketUserService::ordersFrom(const QJsonObject &payload) {
  QVector<Order> result;
  const QJsonValue value = payload.value(QStringLiteral("orders"));
  if (!value.isArray()) return Result<QVector<Order>>::failure(1002, QStringLiteral("响应缺少订单列表"));
  for (const QJsonValue &entry : value.toArray()) {
    if (!entry.isObject()) return Result<QVector<Order>>::failure(1002, QStringLiteral("响应中的订单信息无效"));
    const auto parsed = orderFrom(QJsonObject{{QStringLiteral("order"), entry}});
    if (!parsed.ok) return Result<QVector<Order>>::failure(parsed.code, parsed.error);
    result.push_back(parsed.value);
  }
  return Result<QVector<Order>>::success(result);
}

Result<User> SocketUserService::login(const QString &phone) {
  const QString normalizedPhone = phone.trimmed();
  if (normalizedPhone.isEmpty()) return Result<User>::failure(1002, QStringLiteral("请输入手机号"));
  if (!isValidPhone(normalizedPhone)) return Result<User>::failure(1002, QStringLiteral("手机号格式错误"));
  const auto response = call(QStringLiteral("user.login"), {{QStringLiteral("phone"), normalizedPhone}});
  return response.ok ? userFrom(response.value) : Result<User>::failure(response.code, response.error);
}
Result<User> SocketUserService::login(const QString &phone, const QString &password) { Q_UNUSED(password); return login(phone); }
Result<User> SocketUserService::profile(const QString &id) {
  const QString wire = wireId(id);
  if (id.trimmed().isEmpty()) return Result<User>::failure(1100, QStringLiteral("请先登录"));
  if (wire.isEmpty()) return Result<User>::failure(1002, QStringLiteral("用户标识无效"));
  const auto response = call(QStringLiteral("user.profile.get"), {{QStringLiteral("user_id"), wireValue(id)}});
  return response.ok ? userFrom(response.value) : Result<User>::failure(response.code, response.error);
}
Result<User> SocketUserService::updateProfile(const QString &id, const QString &name, const QString &avatar) {
  const QString wire = wireId(id);
  if (id.trimmed().isEmpty()) return Result<User>::failure(1100, QStringLiteral("请先登录"));
  if (wire.isEmpty()) return Result<User>::failure(1002, QStringLiteral("用户标识无效"));
  QJsonObject payload{{QStringLiteral("user_id"), wireValue(id)}};
  if (!name.trimmed().isEmpty()) payload.insert(QStringLiteral("nickname"), name.trimmed());
  if (!avatar.isEmpty()) payload.insert(QStringLiteral("avatar_path"), avatar);
  const auto response = call(QStringLiteral("user.profile.update"), payload, QStringLiteral("profile:%1:%2:%3").arg(id, name.trimmed(), avatar));
  return response.ok ? userFrom(response.value) : Result<User>::failure(response.code, response.error);
}
Result<qint64> SocketUserService::recharge(const QString &id, qint64 cents) {
  const QString wire = wireId(id);
  if (id.trimmed().isEmpty()) return Result<qint64>::failure(1100, QStringLiteral("请先登录"));
  if (wire.isEmpty()) return Result<qint64>::failure(1002, QStringLiteral("用户标识无效"));
  if (cents <= 0) return Result<qint64>::failure(1002, QStringLiteral("充值金额必须大于 0"));
  const auto response = call(QStringLiteral("wallet.recharge"), {{QStringLiteral("user_id"), wireValue(id)}, {QStringLiteral("amount_cents"), cents}}, QStringLiteral("recharge:%1:%2").arg(id).arg(cents));
  return response.ok ? Result<qint64>::success(response.value.value(QStringLiteral("balance_cents")).toInteger()) : Result<qint64>::failure(response.code, response.error);
}
Result<QVector<Station>> SocketUserService::stations(const QString &query) {
  QJsonObject payload;
  if (!query.trimmed().isEmpty()) payload.insert(QStringLiteral("query"), query.trimmed());
  const auto response = call(QStringLiteral("station.list"), payload);
  if (!response.ok) return Result<QVector<Station>>::failure(response.code, response.error);
  const QJsonValue stationsValue = response.value.value(QStringLiteral("stations"));
  if (!stationsValue.isArray()) return Result<QVector<Station>>::failure(1002, QStringLiteral("响应缺少站点列表"));
  const QString filter = query.trimmed();
  QVector<Station> result;
  for (const auto value : stationsValue.toArray()) {
    const auto object = value.toObject();
    const QString name = object.value(QStringLiteral("name")).toString();
    const QString address = object.value(QStringLiteral("address")).toString();
    if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) && !address.contains(filter, Qt::CaseInsensitive)) continue;
    const int total = object.value(QStringLiteral("pile_total")).toInt();
    const int idle = object.value(QStringLiteral("pile_idle")).toInt();
    const bool open = object.value(QStringLiteral("status")).toString() == QStringLiteral("active");
    const double distance = object.contains(QStringLiteral("distance_km")) ? object.value(QStringLiteral("distance_km")).toDouble(-1.0) : -1.0;
    result.push_back({QString::number(object.value(QStringLiteral("id")).toInteger()), name, address, object.value(QStringLiteral("latitude")).toDouble(), object.value(QStringLiteral("longitude")).toDouble(), distance, open, idle, 0, total});
  }
  return Result<QVector<Station>>::success(result);
}
Result<QVector<Pile>> SocketUserService::piles(const QString &id) {
  const QString wire = wireId(id); if (id.trimmed().isEmpty()) return Result<QVector<Pile>>::failure(1002, QStringLiteral("站点标识不能为空")); if (wire.isEmpty()) return Result<QVector<Pile>>::failure(1002, QStringLiteral("站点标识无效"));
  const auto response = call(QStringLiteral("pile.list"), {{QStringLiteral("station_id"), wireValue(id)}}); if (!response.ok) return Result<QVector<Pile>>::failure(response.code, response.error);
  const QJsonValue pilesValue = response.value.value(QStringLiteral("piles")); if (!pilesValue.isArray()) return Result<QVector<Pile>>::failure(1002, QStringLiteral("响应缺少充电桩列表"));
  QVector<Pile> result; for (const auto value : pilesValue.toArray()) { const auto object = value.toObject(); const QString status = object.value(QStringLiteral("status")).toString(); PileStatus state = PileStatus::Offline; if (status == QStringLiteral("idle")) state = PileStatus::Idle; else if (status == QStringLiteral("reserved")) state = PileStatus::Reserved; else if (status == QStringLiteral("charging")) state = PileStatus::Charging; else if (status == QStringLiteral("fault")) state = PileStatus::Fault; const qint64 price = object.value(QStringLiteral("unit_price_cents_per_kwh")).toInteger(); result.push_back({QString::number(object.value(QStringLiteral("id")).toInteger()), object.value(QStringLiteral("pile_code")).toString(), object.value(QStringLiteral("pile_type")).toString(), state, object.value(QStringLiteral("power_kw")).toDouble(), price, QString::number(object.value(QStringLiteral("station_id")).toInteger())}); }
  return Result<QVector<Pile>>::success(result);
}
Result<Route> SocketUserService::route(double fromLat, double fromLng, const Station &target, RouteMode mode) {
  if (!std::isfinite(fromLat) || !std::isfinite(fromLng) || fromLat < -90.0 || fromLat > 90.0 || fromLng < -180.0 || fromLng > 180.0
      || target.id.isEmpty() || !std::isfinite(target.latitude) || !std::isfinite(target.longitude)
      || target.latitude < -90.0 || target.latitude > 90.0 || target.longitude < -180.0 || target.longitude > 180.0) {
    return Result<Route>::failure(1002, QStringLiteral("路线坐标无效"));
  }
  const double latScale = 111.0;
  const double lngScale = 111.0 * qCos(qDegreesToRadians(fromLat));
  const double dx = (target.longitude - fromLng) * lngScale;
  const double dy = (target.latitude - fromLat) * latScale;
  const double distance = qMax(0.1, qSqrt(dx * dx + dy * dy));
  const bool driving = mode == RouteMode::Driving;
  const int duration = qMax(1, qRound(driving ? distance / 40.0 * 60.0 : distance / 5.0 * 60.0));
  const QString modeText = driving ? QStringLiteral("驾车") : QStringLiteral("步行");
  return Result<Route>::success({true, mode, distance, duration, QStringLiteral("离线 Mock %1 路线：前往 %2").arg(modeText, target.name)});
}
Result<Order> SocketUserService::currentOrder(const QString &id) { const QString wire = wireId(id); if (id.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (wire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户标识无效")); const auto response = call(QStringLiteral("order.active.get"), {{QStringLiteral("user_id"), wireValue(id)}}); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }
Result<QVector<Order>> SocketUserService::orderHistory(const QString &userId) { const QString wire = wireId(userId); if (userId.trimmed().isEmpty()) return Result<QVector<Order>>::failure(1100, QStringLiteral("请先登录")); if (wire.isEmpty()) return Result<QVector<Order>>::failure(1002, QStringLiteral("用户标识无效")); const auto response = call(QStringLiteral("order.history.list"), {{QStringLiteral("user_id"), wireValue(userId)}}); return response.ok ? ordersFrom(response.value) : Result<QVector<Order>>::failure(response.code, response.error); }
Result<Order> SocketUserService::createOrder(const QString &userId, const Station &, const Pile &pile) { const QString userWire = wireId(userId); const QString pileWire = wireId(pile.id); if (userId.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (userWire.isEmpty() || pileWire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户或充电桩标识无效")); const auto response = call(QStringLiteral("reservation.create"), {{QStringLiteral("user_id"), wireValue(userId)}, {QStringLiteral("pile_id"), wireValue(pile.id)}}, QStringLiteral("reservation.create:%1:%2").arg(userId, pile.id)); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }
Result<Order> SocketUserService::confirmReservation(const QString &userId, const QString &orderId) { const QString userWire = wireId(userId); const QString orderWire = wireId(orderId); if (userId.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (userWire.isEmpty() || orderWire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户或订单标识无效")); const auto response = call(QStringLiteral("reservation.confirm"), {{QStringLiteral("user_id"), wireValue(userId)}, {QStringLiteral("order_id"), wireValue(orderId)}}, QStringLiteral("reservation.confirm:%1:%2").arg(userId, orderId)); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }
Result<Order> SocketUserService::cancelReservation(const QString &userId, const QString &orderId) { const QString userWire = wireId(userId); const QString orderWire = wireId(orderId); if (userId.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (userWire.isEmpty() || orderWire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户或订单标识无效")); const auto response = call(QStringLiteral("reservation.cancel"), {{QStringLiteral("user_id"), wireValue(userId)}, {QStringLiteral("order_id"), wireValue(orderId)}}, QStringLiteral("reservation.cancel:%1:%2").arg(userId, orderId)); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }
Result<Order> SocketUserService::startCharging(const QString &userId, const QString &orderId) { const QString userWire = wireId(userId); const QString orderWire = wireId(orderId); if (userId.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (userWire.isEmpty() || orderWire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户或订单标识无效")); const auto response = call(QStringLiteral("charging.start"), {{QStringLiteral("user_id"), wireValue(userId)}, {QStringLiteral("order_id"), wireValue(orderId)}}, QStringLiteral("charging.start.order:%1:%2").arg(userId, orderId)); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }
Result<Order> SocketUserService::startChargingDirect(const QString &userId, const QString &pileId) { const QString userWire = wireId(userId); const QString pileWire = wireId(pileId); if (userId.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (userWire.isEmpty() || pileWire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户或充电桩标识无效")); const auto response = call(QStringLiteral("charging.start"), {{QStringLiteral("user_id"), wireValue(userId)}, {QStringLiteral("pile_id"), wireValue(pileId)}}, QStringLiteral("charging.start.pile:%1:%2").arg(userId, pileId)); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }
Result<Order> SocketUserService::stopCharging(const QString &userId, const QString &orderId) { const QString userWire = wireId(userId); const QString orderWire = wireId(orderId); if (userId.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (userWire.isEmpty() || orderWire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户或订单标识无效")); const auto response = call(QStringLiteral("charging.stop"), {{QStringLiteral("user_id"), wireValue(userId)}, {QStringLiteral("order_id"), wireValue(orderId)}}, QStringLiteral("charging.stop:%1:%2").arg(userId, orderId)); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }
Result<Order> SocketUserService::settle(const QString &userId, const QString &orderId) { const QString userWire = wireId(userId); const QString orderWire = wireId(orderId); if (userId.trimmed().isEmpty()) return Result<Order>::failure(1100, QStringLiteral("请先登录")); if (userWire.isEmpty() || orderWire.isEmpty()) return Result<Order>::failure(1002, QStringLiteral("用户或订单标识无效")); const auto response = call(QStringLiteral("charging.settle"), {{QStringLiteral("user_id"), wireValue(userId)}, {QStringLiteral("order_id"), wireValue(orderId)}}, QStringLiteral("charging.settle:%1:%2").arg(userId, orderId)); return response.ok ? orderFrom(response.value) : Result<Order>::failure(response.code, response.error); }

} // namespace ev
