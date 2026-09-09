#ifndef STATICMAPIMAGEPROVIDER_H
#define STATICMAPIMAGEPROVIDER_H

#include <QImage>
#include <QObject>

#include <functional>

#include "widgets/staticmapviewport.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace ev {

// 静态图底图获取抽象（T2，docs/role-c-admin-map-renderer-plan.md §3.2）：
// widget 只依赖该接口（将来可切服务端代理实现）；fetch 结果异步回调，seq 透传。
class MapImageProvider
{
public:
    // 失败原因分类（供 widget 决定降级标注与重试策略）
    enum class Failure {
        None,          // 成功（ok=true）
        NotConfigured, // 未配置（key 空）：未发起任何请求——静默拓扑，不算降级尝试
        Network,       // 传输层失败（断连/超时/DNS）
        Http,          // HTTP 非 200 且非配额业务拒绝（110 等）
        Quota,         // 业务配额拒绝（腾讯 status 121）
        BadImage,      // 200 但响应体不是可解码图像
    };
    using Callback =
        std::function<void(int seq, bool ok, const QImage &image, Failure failure)>;

    virtual ~MapImageProvider() = default;

    // 是否具备拉图能力（凭据配置完整）。false 时调用方不得进入真图路径（静默拓扑）。
    virtual bool canFetch() const = 0;
    // 异步拉取 view 对应静态图，回调携带原 seq；cancelAll 后旧回调不得再触发。
    virtual void fetch(const StaticMapView &view, int seq, Callback callback) = 0;
    // 取消全部在途请求（回调不再触发）；之后可继续 fetch。
    virtual void cancelAll() = 0;
};

// 腾讯静态图 WebService 实现。构造显式收 key（不读环境变量——注入点负责，
// 计划 §3.2 网络隔离）；key 空 → canFetch()=false、不建网络对象、fetch 同步
// 回 NotConfigured。5s 传输超时（测试可 setTransferTimeoutMs 注入）。URL/key
// 仅内存构造与传递，禁止打印/落盘。
class StaticMapImageProvider : public QObject, public MapImageProvider
{
    Q_OBJECT

public:
    // baseUrl 默认腾讯官方端点；测试传本地假服务器地址（零真实网络）
    explicit StaticMapImageProvider(QString key,
                                    QObject *parent = nullptr,
                                    QString baseUrl =
                                        QStringLiteral("https://apis.map.qq.com/ws/staticmap/v2/"));
    ~StaticMapImageProvider() override;

    bool canFetch() const override;
    void fetch(const StaticMapView &view, int seq, Callback callback) override;
    void cancelAll() override;

    // 测试注入：传输超时毫秒（默认 5000）
    void setTransferTimeoutMs(int ms) { m_timeoutMs = ms; }

private:
    void onReplyFinished(QNetworkReply *reply, int seq, Callback callback);

    QString m_key;
    QString m_baseUrl;
    int m_timeoutMs = 5000;
    QNetworkAccessManager *m_nam = nullptr; // key 非空才建（无 key = 无网络对象）
    QList<QNetworkReply *> m_active;         // 在途请求（cancelAll 目标）
};

// 纯函数（单测直测）：拼静态图 URL。center 打印 1e-5 度（'f'5，与 requestFingerprint
// 量化一致：同指纹 ⇒ 同 URL）、size 用 `*` 分隔。URL 含 key——仅内存使用，禁打印/落盘。
QString buildStaticMapUrl(const QString &key, const StaticMapView &view,
                          const QString &baseUrl =
                              QStringLiteral("https://apis.map.qq.com/ws/staticmap/v2/"));

} // namespace ev

#endif // STATICMAPIMAGEPROVIDER_H
