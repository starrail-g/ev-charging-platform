#pragma once

#include "map_service.h"

#include <QWidget>

class QLabel;
class QWebEngineView;

namespace ev {

class MapWebView final : public QWidget {
  Q_OBJECT
public:
  explicit MapWebView(QWidget *parent = nullptr);

  void setMarkers(const QVector<MapPoi> &markers);
  void setRoute(const MapRoute &route);
  void loadTencent(const QString &apiKey);
  void showOffline(const QString &reason = {});
  void deactivate();
  bool isRealPageLoaded() const { return realPageLoaded_; }
  bool isOfflinePageLoaded() const { return offlinePageLoaded_; }

signals:
  void realPageFailed();
  void markerActivated(const QString &id);
  void pageLoadFinished(bool realMode, bool ok);

private:
  void renderRealPage();
  QString makeHtml(bool real) const;

  QWebEngineView *view_{};
  QLabel *modeLabel_{};
  QVector<MapPoi> markers_;
  MapRoute route_{};
  QString apiKey_;
  bool realRequested_{false};
  bool realPageLoaded_{false};
  bool offlinePageLoaded_{false};
};

} // namespace ev
