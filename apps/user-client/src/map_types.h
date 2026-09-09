#pragma once

#include "client_service.h"

#include <QString>
#include <QVector>

namespace ev {

enum class MapSource { Tencent, Server, Mock, Offline };
enum class MapErrorCategory { InvalidInput, MissingKey, Timeout, Network, Api, Parse };

struct GeoCoordinate {
  double latitude{};
  double longitude{};
};

struct MapPoi {
  QString id;
  QString title;
  QString address;
  GeoCoordinate coordinate{};
  qint64 distanceMeters{-1};
  MapSource source{MapSource::Tencent};
};

struct MapRoute {
  RouteMode mode{RouteMode::Driving};
  qint64 distanceMeters{};
  int durationSeconds{};
  QVector<GeoCoordinate> polyline;
  MapSource source{MapSource::Mock};
  QString summary;
};

struct MapError {
  MapErrorCategory category{MapErrorCategory::Parse};
  QString userMessage;
  bool retryable{false};
  int httpStatus{0};
  int apiStatus{0};
};

template <typename T>
struct MapResult {
  bool ok{false};
  T value{};
  MapError error{};
  QString notice;
  static MapResult success(const T &value, const QString &notice = {}) { return {true, value, {}, notice}; }
  static MapResult failure(const MapError &error) { return {false, {}, error, {}}; }
};

} // namespace ev
