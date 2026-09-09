#include "map_web_view.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <functional>

namespace ev {
namespace {

class MapPage final : public QWebEnginePage {
public:
  MapPage(std::function<void(const QString &)> activated, QObject *parent)
      : QWebEnginePage(parent), activated_(std::move(activated)) {}

protected:
  bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override {
    if (url.scheme() == QStringLiteral("evstation") && url.host() == QStringLiteral("open")) {
      const QString id = QUrl::fromPercentEncoding(url.path().mid(1).toUtf8());
      if (!id.isEmpty()) activated_(id);
      return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
  }

private:
  std::function<void(const QString &)> activated_;
};

QString scriptSafeJson(const QJsonDocument &document) {
  QString json = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
  json.replace(QStringLiteral("<"), QStringLiteral("\\u003c"));
  json.replace(QStringLiteral(">"), QStringLiteral("\\u003e"));
  json.replace(QStringLiteral("&"), QStringLiteral("\\u0026"));
  json.replace(QChar(0x2028), QStringLiteral("\\u2028"));
  json.replace(QChar(0x2029), QStringLiteral("\\u2029"));
  return json;
}

} // namespace

MapWebView::MapWebView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  modeLabel_ = new QLabel(this);
  modeLabel_->setStyleSheet(QStringLiteral("background:#132541;color:#bfeeff;padding:5px;border-radius:6px;"));
  modeLabel_->setAlignment(Qt::AlignCenter);
  modeLabel_->setWordWrap(true);
  view_ = new QWebEngineView(this);
  view_->setPage(new MapPage([this](const QString &id) { emit markerActivated(id); }, view_));
  view_->setMinimumHeight(220);
  layout->addWidget(modeLabel_);
  layout->addWidget(view_);

  connect(view_, &QWebEngineView::loadProgress, this, [this](int progress) {
    if (realRequested_ && !realPageLoaded_)
      modeLabel_->setText(QStringLiteral("正在加载真实腾讯地图… %1%").arg(progress));
  });
  connect(view_, &QWebEngineView::loadFinished, this, [this](bool ok) {
    if (!realRequested_) {
      offlinePageLoaded_ = ok;
      emit pageLoadFinished(false, ok);
      return;
    }
    if (!ok) {
      realPageLoaded_ = false;
      emit pageLoadFinished(true, false);
      showOffline(QStringLiteral("腾讯地图页面加载失败，当前显示离线地图"));
      emit realPageFailed();
      return;
    }
    view_->page()->runJavaScript(QStringLiteral("Boolean(window.TMap && window.evMapReady)"),
                                 [this](const QVariant &value) {
      if (!realRequested_) return;
      if (!value.toBool()) {
        realPageLoaded_ = false;
        emit pageLoadFinished(true, false);
        showOffline(QStringLiteral("腾讯地图页面未能初始化，可能是额度、权限或网络问题"));
        emit realPageFailed();
        return;
      }
      realPageLoaded_ = true;
      offlinePageLoaded_ = false;
      modeLabel_->setText(QStringLiteral("真实腾讯地图页面"));
      emit pageLoadFinished(true, true);
    });
  });
  showOffline(QStringLiteral("尚未加载真实页面"));
}

void MapWebView::setMarkers(const QVector<MapPoi> &markers) {
  markers_ = markers;
  if (realRequested_) renderRealPage();
  else view_->setHtml(makeHtml(false), QUrl(QStringLiteral("qrc:/map/")));
}

void MapWebView::setRoute(const MapRoute &route) {
  route_ = route;
  if (realRequested_) renderRealPage();
  else view_->setHtml(makeHtml(false), QUrl(QStringLiteral("qrc:/map/")));
}

QString MapWebView::makeHtml(bool real) const {
  QJsonArray markerArray;
  for (const auto &marker : markers_) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), marker.id);
    object.insert(QStringLiteral("title"), marker.title);
    object.insert(QStringLiteral("address"), marker.address);
    object.insert(QStringLiteral("lat"), marker.coordinate.latitude);
    object.insert(QStringLiteral("lng"), marker.coordinate.longitude);
    markerArray.push_back(object);
  }
  QJsonArray lineArray;
  for (const auto &point : route_.polyline) {
    QJsonArray pair;
    pair.push_back(point.latitude);
    pair.push_back(point.longitude);
    lineArray.push_back(pair);
  }
  QJsonObject payload;
  payload.insert(QStringLiteral("markers"), markerArray);
  payload.insert(QStringLiteral("line"), lineArray);
  if (!markers_.isEmpty()) {
    payload.insert(QStringLiteral("centerLat"), markers_.first().coordinate.latitude);
    payload.insert(QStringLiteral("centerLng"), markers_.first().coordinate.longitude);
  } else if (!route_.polyline.isEmpty()) {
    payload.insert(QStringLiteral("centerLat"), route_.polyline.first().latitude);
    payload.insert(QStringLiteral("centerLng"), route_.polyline.first().longitude);
  } else {
    payload.insert(QStringLiteral("centerLat"), 22.5300);
    payload.insert(QStringLiteral("centerLng"), 113.9300);
  }

  QFile templateFile(QStringLiteral(":/map/map.html"));
  if (!templateFile.open(QIODevice::ReadOnly | QIODevice::Text))
    return QStringLiteral("<!doctype html><meta charset='utf-8'><body style='background:#10243d;color:white'>地图模板加载失败，已降级。</body>");
  QString html = QString::fromUtf8(templateFile.readAll());
  const QString script = real
      ? QStringLiteral("<script src=\"https://map.qq.com/api/gljs?v=1.exp&key=%1\"></script>")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(apiKey_)))
      : QString();
  html.replace(QStringLiteral("__TENCENT_SCRIPT__"), script);
  html.replace(QStringLiteral("__MAP_PAYLOAD__"), scriptSafeJson(QJsonDocument(payload)));
  html.replace(QStringLiteral("__REAL_MODE__"), real ? QStringLiteral("true") : QStringLiteral("false"));
  return html;
}

void MapWebView::renderRealPage() {
  if (apiKey_.isEmpty()) return;
  offlinePageLoaded_ = false;
  view_->stop();
  view_->setHtml(makeHtml(true), QUrl(QStringLiteral("https://map.qq.com/")));
}

void MapWebView::loadTencent(const QString &apiKey) {
  Q_UNUSED(apiKey);
  // Tencent credentials and JavaScript map loading belong to the server after
  // the PR #15 boundary change. Keep this compatibility entry point offline.
  showOffline(QStringLiteral("腾讯地图由服务端处理，客户端显示服务端结果/离线地图"));
}

void MapWebView::showOffline(const QString &reason) {
  view_->stop();
  realRequested_ = false;
  realPageLoaded_ = false;
  offlinePageLoaded_ = false;
  modeLabel_->setText(reason.isEmpty() ? QStringLiteral("Mock/离线地图")
      : QStringLiteral("Mock/离线地图 · %1").arg(reason));
  view_->setHtml(makeHtml(false), QUrl(QStringLiteral("qrc:/map/")));
}

void MapWebView::deactivate() {
  showOffline(QStringLiteral("地图页面已暂停"));
}

} // namespace ev
