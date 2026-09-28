#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVariant>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace nimbus {

struct RestResponse {
    int statusCode = 0;
    QByteArray body;
    // Keys are lowercased: the CDN and the API disagree on header casing and a
    // case-sensitive lookup would silently miss the rate-limit headers.
    QMap<QString, QString> headers;

    QJsonDocument document() const { return QJsonDocument::fromJson(body); }
    QJsonObject json() const { return document().object(); }
    QJsonArray array() const { return document().array(); }

    bool ok() const { return statusCode >= 200 && statusCode < 300; }

    // Delta labels its failures: {"type":"NotFound","msg":"..."}. A bare string
    // is returned verbatim so an HTML error page is still readable in a log.
    QString errorMessage() const;
};

using RestCallback = std::function<void(const RestResponse&)>;

struct RestRequest {
    QUrl url;
    QString method = QStringLiteral("GET");
    QByteArray body;
    QString contentType;
    QMap<QString, QString> headers;
    int timeoutMs = 30000;
    int maxRetries = 4;
};

// Fully asynchronous by construction: no nested event loop, no per-request
// QNetworkAccessManager, no sleep. Throttling is a QTimer on the caller's thread,
// so a rate-limit wait cannot freeze the UI, which is the failure mode the
// previous synchronous client had on every send.
class RestClient : public QObject {
    Q_OBJECT
public:
    explicit RestClient(QObject* parent = nullptr);
    ~RestClient() override;

    void setBaseUrl(const QString& base);
    QString baseUrl() const { return m_baseUrl; }

    void setToken(const QString& token);
    QString token() const { return m_token; }

    // `path` is relative to the API root, without a leading slash needed.
    QNetworkReply* get(const QString& path, RestCallback cb = {});
    QNetworkReply* post(const QString& path, const QJsonObject& body, RestCallback cb = {});
    QNetworkReply* patch(const QString& path, const QJsonObject& body, RestCallback cb = {});
    QNetworkReply* put(const QString& path, const QJsonObject& body, RestCallback cb = {});
    QNetworkReply* del(const QString& path, RestCallback cb = {});

    // CDN and proxy hosts are not under the API root, so they take a full URL.
    QNetworkReply* getUrl(const QUrl& url, RestCallback cb = {});

    // Multipart upload against an absolute URL. Attachment upload is not an API
    // route: it is a POST to the CDN host from the instance root, which is why it
    // is absent from the published OpenAPI spec entirely.
    QNetworkReply* postFile(const QUrl& url, const QString& fieldName, const QString& filename,
                            const QString& contentType, const QByteArray& data, RestCallback cb = {});

    // Percent-encodes a path segment. Ids are ULIDs and safe, but emoji ids and
    // invite codes are not, and an unencoded slash silently changes the route.
    static QString encode(const QString& segment);

signals:
    void rateLimited(const QString& bucket, int retryAfterMs);
    void unauthorized();

private:
    QNetworkReply* dispatch(RestRequest request, RestCallback cb, int attempt);
    QNetworkRequest buildRequest(const RestRequest& request) const;
    void noteRateLimit(const QString& bucket, const RestResponse& response);
    QString bucketFor(const QString& method, const QUrl& url) const;

    QNetworkAccessManager* m_nam = nullptr;
    QString m_baseUrl = QStringLiteral("https://api.stoat.chat");
    QString m_token;
    QString m_userAgent = QStringLiteral("nimbus/0.1.0");

    struct Bucket {
        int remaining = -1; // -1: server never told us, so never throttle
        qint64 resetAtMs = 0;
        bool blocked = false;
    };
    QHash<QString, Bucket> m_buckets;
};

} // namespace nimbus
