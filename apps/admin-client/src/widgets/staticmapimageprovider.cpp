#include "widgets/staticmapimageprovider.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <utility>

namespace ev {
namespace {

// 腾讯 WebService 错误 JSON 中表示"当日配额用尽"的 status 值（T0 实测：status 121）
constexpr int kTencentQuotaStatus = 121;

// 提取响应体 JSON 的 status 字段；非 JSON 返回 -1
int jsonStatus(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return -1;
    return doc.object().value(QLatin1String("status")).toInt(-1);
}

} // namespace

QString buildStaticMapUrl(const QString &key, const StaticMapView &view,
                          const QString &baseUrl)
{
    // 腾讯静态图 v2 参数（T0 探针实证）：center=纬度,经度 & zoom & size=宽*高 & key。
    // 值域全为 URL 安全字符（数字/逗号/点/星号/字母），手工拼接避免编码二义。
    QString url = baseUrl;
    if (!url.endsWith(QLatin1Char('/')))
        url += QLatin1Char('/');
    url += QLatin1String("?center=");
    url += QString::number(view.centerLat, 'f', 5);
    url += QLatin1Char(',');
    url += QString::number(view.centerLng, 'f', 5);
    url += QLatin1String("&zoom=");
    url += QString::number(view.zoom);
    url += QLatin1String("&size=");
    url += QString::number(view.width);
    url += QLatin1Char('*');
    url += QString::number(view.height);
    url += QLatin1String("&key=");
    url += key;
    return url;
}

StaticMapImageProvider::StaticMapImageProvider(QString key, QObject *parent, QString baseUrl)
    : QObject(parent)
    , m_key(std::move(key))
    , m_baseUrl(std::move(baseUrl))
{
}

StaticMapImageProvider::~StaticMapImageProvider() = default;

bool StaticMapImageProvider::canFetch() const
{
    return !m_key.isEmpty();
}

void StaticMapImageProvider::fetch(const StaticMapView &view, int seq, Callback callback)
{
    if (!canFetch()) {
        if (callback)
            callback(seq, false, QImage(), Failure::NotConfigured);
        return; // 空转：不建网络对象、不发请求
    }
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest request;
    request.setUrl(QUrl(buildStaticMapUrl(m_key, view, m_baseUrl)));
    request.setTransferTimeout(m_timeoutMs);
    request.setRawHeader("Accept", "image/png,image/jpeg,*/*");

    QNetworkReply *reply = m_nam->get(request);
    m_active.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seq, callback] {
        onReplyFinished(reply, seq, callback);
    });

    // 总时长上限（评审 P2）：QNAM transferTimeout 只在"数据转移停滞"时触发——
    // 持续滴答的慢传输可无限拖长，不构成总超时。这里用 deadline 定时器保证
    // 请求在 m_timeoutMs 内必然结束（到期 abort → finished → 归 Network 失败）。
    // 定时器挂 reply 名下，reply 释放即自动销毁。
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply] { reply->abort(); });
    deadline->start(m_timeoutMs);
}

void StaticMapImageProvider::cancelAll()
{
    // 评审 P1：abort 可能同步触发 finished → onReplyFinished 会修改 m_active——
    // 遍历中改容器是 UB。先快照并摘除容器，再逐个打标记 + abort；
    // 回调路径 removeOne 变成空操作，aborted 标记使其直接返回。
    const QList<QNetworkReply *> active = std::exchange(m_active, {});
    for (QNetworkReply *reply : active)
        reply->setProperty("_mapFetchAborted", true);
    for (QNetworkReply *reply : active)
        reply->abort();
}

void StaticMapImageProvider::onReplyFinished(QNetworkReply *reply, int seq, Callback callback)
{
    m_active.removeOne(reply);
    reply->deleteLater();

    // 主动取消（cancelAll/析构路径）→ 回调不得触发
    if (reply->property("_mapFetchAborted").toBool())
        return;

    const bool ok = reply->error() == QNetworkReply::NoError;
    const QByteArray body = reply->readAll();
    // 腾讯 WebService 业务拒绝以 HTTP 200 + JSON {"status":121} 返回（T0 实证）——
    // 成功分支先查配额码，再尝试解码图像，顺序不可颠倒
    if (jsonStatus(body) == kTencentQuotaStatus) {
        if (callback)
            callback(seq, false, QImage(), Failure::Quota);
        return;
    }
    if (ok) {
        QImage image;
        if (!image.loadFromData(body)) {
            if (callback)
                callback(seq, false, QImage(), Failure::BadImage);
            return;
        }
        if (callback)
            callback(seq, true, image, Failure::None);
        return;
    }

    // 失败分类（评审 P2 修正：不能按 httpStatus 有无分——响应头已收到但传输
    // 被本地中断（deadline abort/断连）时 httpStatus 也存在，那是传输层失败）：
    //   本地/传输层错误 → Network；服务端响应类（4xx/5xx/畸形协议等 QNAM 语义码）
    //   → Http。deadline/cancelAll abort = OperationCanceled → Network。
    Failure failure = Failure::Http;
    switch (reply->error()) {
    // 网络层/传输层（1-99、101-199 代理层本地问题）
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::SslHandshakeFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::BackgroundRequestNotAllowedError:
    case QNetworkReply::TooManyRedirectsError:
    case QNetworkReply::InsecureRedirectError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
    case QNetworkReply::UnknownProxyError:
        failure = Failure::Network;
        break;
    default:
        break; // Content*/Protocol*/AuthenticationRequired/4xx/5xx → Http
    }
    if (callback)
        callback(seq, false, QImage(), failure);
}

} // namespace ev
