#include "rest.h"

#include "json.h"
#include "log.h"

#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace nimbus {

namespace {

// Delta buckets by method plus the route with its major parameters masked. Every
// id in this API is a 26-character ULID except category ids, which are short
// free-form slugs, so both shapes are masked to keep one bucket per route rather
// than one per resource.
QString normalisePath(const QString& path) {
    static const QRegularExpression ulid(QStringLiteral("\\b[0-9A-HJKMNP-TV-Z]{26}\\b"));
    QString out = path;
    out.replace(ulid, QStringLiteral(":id"));
    return out;
}

int headerInt(const QMap<QString, QString>& headers, const char* key, int def) {
    const auto it = headers.constFind(QString::fromLatin1(key));
    if (it == headers.constEnd() || it->isEmpty()) return def;
    bool ok = false;
    const int v = it->toInt(&ok);
    return ok ? v : def;
}

double headerDouble(const QMap<QString, QString>& headers, const char* key, double def) {
    const auto it = headers.constFind(QString::fromLatin1(key));
    if (it == headers.constEnd() || it->isEmpty()) return def;
    bool ok = false;
    const double v = it->toDouble(&ok);
    return ok ? v : def;
}

QString reasonPhrase(int status) {
    switch (status) {
        case 400: return QStringLiteral("bad request");
        case 401: return QStringLiteral("unauthorized");
        case 403: return QStringLiteral("forbidden");
        case 404: return QStringLiteral("not found");
        case 409: return QStringLiteral("conflict");
        case 413: return QStringLiteral("payload too large");
        case 429: return QStringLiteral("rate limited");
        case 500: return QStringLiteral("server error");
        case 502: return QStringLiteral("bad gateway");
        case 503: return QStringLiteral("service unavailable");
        default: break;
    }
    return status > 0 ? QStringLiteral("HTTP %1").arg(status) : QString();
}

} // namespace

QString RestResponse::errorMessage() const {
    const QJsonObject o = json();
    const QString type = jsonString(o, "type");
    const QString msg = jsonString(o, "msg");
    if (!msg.isEmpty()) {
        return type.isEmpty() ? msg : QStringLiteral("%1: %2").arg(type, msg);
    }
    if (!type.isEmpty()) return type;

    // A rejection from the edge is an HTML error page, not a labelled JSON
    // failure, so there is nothing to extract and the whole page must not be
    // surfaced as though it were a message.
    const QString raw = QString::fromUtf8(body).trimmed();
    if (raw.startsWith(QLatin1Char('<')) || raw.isEmpty()) {
        return reasonPhrase(statusCode);
    }
    return raw.left(200);
}

RestClient::RestClient(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

RestClient::~RestClient() = default;

void RestClient::setBaseUrl(const QString& base) {
    m_baseUrl = base.endsWith(QLatin1Char('/')) ? base.left(base.size() - 1) : base;
}

void RestClient::setToken(const QString& token) {
    m_token = token;
}

QString RestClient::encode(const QString& segment) {
    return QString::fromLatin1(QUrl::toPercentEncoding(segment));
}

QString RestClient::bucketFor(const QString& method, const QUrl& url) const {
    return method.toUpper() + QLatin1Char(' ') + normalisePath(url.path());
}

QNetworkRequest RestClient::buildRequest(const RestRequest& request) const {
    QNetworkRequest req(request.url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    if (!m_token.isEmpty()) {
        req.setRawHeader("X-Session-Token", m_token.toUtf8());
    }
    if (!request.contentType.isEmpty()) {
        req.setRawHeader("Content-Type", request.contentType.toUtf8());
    }
    for (auto it = request.headers.constBegin(); it != request.headers.constEnd(); ++it) {
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
    return req;
}

QNetworkReply* RestClient::get(const QString& path, RestCallback cb) {
    RestRequest r;
    r.url = QUrl(m_baseUrl + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path));
    return dispatch(std::move(r), std::move(cb), 0);
}

QNetworkReply* RestClient::post(const QString& path, const QJsonObject& body, RestCallback cb) {
    RestRequest r;
    r.method = QStringLiteral("POST");
    r.url = QUrl(m_baseUrl + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path));
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    r.contentType = QStringLiteral("application/json");
    return dispatch(std::move(r), std::move(cb), 0);
}

QNetworkReply* RestClient::patch(const QString& path, const QJsonObject& body, RestCallback cb) {
    RestRequest r;
    r.method = QStringLiteral("PATCH");
    r.url = QUrl(m_baseUrl + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path));
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    r.contentType = QStringLiteral("application/json");
    return dispatch(std::move(r), std::move(cb), 0);
}

QNetworkReply* RestClient::put(const QString& path, const QJsonObject& body, RestCallback cb) {
    RestRequest r;
    r.method = QStringLiteral("PUT");
    r.url = QUrl(m_baseUrl + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path));
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    r.contentType = QStringLiteral("application/json");
    return dispatch(std::move(r), std::move(cb), 0);
}

QNetworkReply* RestClient::del(const QString& path, RestCallback cb) {
    RestRequest r;
    r.method = QStringLiteral("DELETE");
    r.url = QUrl(m_baseUrl + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path));
    return dispatch(std::move(r), std::move(cb), 0);
}

QNetworkReply* RestClient::getUrl(const QUrl& url, RestCallback cb) {
    RestRequest r;
    r.url = url;
    return dispatch(std::move(r), std::move(cb), 0);
}

QNetworkReply* RestClient::postFile(const QUrl& url, const QString& fieldName,
                                    const QString& filename, const QString& contentType,
                                    const QByteArray& data, RestCallback cb) {
    // 32 random bytes, hex encoded. The boundary has to not occur in the payload,
    // and a random 128-bit value makes a collision irrelevant at these sizes.
    QByteArray boundary(32, 0);
    for (int i = 0; i < boundary.size(); ++i) {
        boundary[i] = char(QRandomGenerator::global()->bounded(256));
    }
    boundary = boundary.toHex();

    QByteArray body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + fieldName.toUtf8() + "\"; filename=\""
            + filename.toUtf8() + "\"\r\n";
    body += "Content-Type: " + contentType.toUtf8() + "\r\n\r\n";
    body += data;
    body += "\r\n--" + boundary + "--\r\n";

    RestRequest r;
    r.method = QStringLiteral("POST");
    r.url = url;
    r.body = body;
    r.contentType = QStringLiteral("multipart/form-data; boundary=") + QString::fromLatin1(boundary);
    // Uploads are the slowest thing this client does; a 20 MB file on a poor
    // uplink needs more than the default allowance.
    r.timeoutMs = 120000;
    return dispatch(std::move(r), std::move(cb), 0);
}

void RestClient::noteRateLimit(const QString& bucket, const RestResponse& response) {
    Bucket& b = m_buckets[bucket];
    const int remaining = headerInt(response.headers, "x-ratelimit-remaining", -1);
    if (remaining >= 0) b.remaining = remaining;

    const int resetAfter = int(headerDouble(response.headers, "x-ratelimit-reset-after", -1) * 1000.0);
    if (resetAfter >= 0) {
        b.resetAtMs = QDateTime::currentMSecsSinceEpoch() + resetAfter;
    } else {
        const qint64 resetAt = qint64(headerDouble(response.headers, "x-ratelimit-reset", 0) * 1000.0);
        if (resetAt > 0) b.resetAtMs = resetAt;
    }
}

QNetworkReply* RestClient::dispatch(RestRequest request, RestCallback cb, int attempt) {
    const QString bucket = bucketFor(request.method, request.url);

    // Only delay when the server has actually told us this route is exhausted.
    // Guessing a limit where the API states none would add latency to every
    // request on routes that are not rate limited at all.
    const auto it = m_buckets.constFind(bucket);
    if (it != m_buckets.constEnd() && it->blocked && it->remaining <= 0) {
        const qint64 waitMs = it->resetAtMs - QDateTime::currentMSecsSinceEpoch();
        if (waitMs > 0) {
            RestRequest deferred = request;
            RestCallback deferredCb = std::move(cb);
            const int deferredAttempt = attempt;
            QPointer<RestClient> guard(this);
            QTimer::singleShot(int(waitMs), this, [guard, deferred, deferredCb, deferredAttempt, bucket]() mutable {
                if (!guard) {
                    if (deferredCb) deferredCb(RestResponse{});
                    return;
                }
                auto& b = guard->m_buckets[bucket];
                b.blocked = false;
                b.remaining = -1;
                guard->dispatch(std::move(deferred), std::move(deferredCb), deferredAttempt);
            });
            return nullptr;
        }
        m_buckets[bucket].blocked = false;
        m_buckets[bucket].remaining = -1;
    }

    const QNetworkRequest netRequest = buildRequest(request);
    QNetworkReply* reply = nullptr;
    const QByteArray verb = request.method.toUtf8();
    const QByteArray payload = request.body;

    if (request.method == QLatin1String("GET")) {
        reply = m_nam->get(netRequest);
    } else if (request.method == QLatin1String("POST")) {
        reply = m_nam->post(netRequest, payload);
    } else if (request.method == QLatin1String("PUT")) {
        reply = m_nam->put(netRequest, payload);
    } else if (request.method == QLatin1String("DELETE")) {
        reply = m_nam->deleteResource(netRequest);
    } else {
        reply = m_nam->sendCustomRequest(netRequest, verb, payload);
    }

    reply->setParent(this);
    if (request.timeoutMs > 0) {
        // Qt has no per-reply timeout; without this a dropped connection hangs
        // the request forever and the caller never learns.
        QTimer::singleShot(request.timeoutMs, reply, &QNetworkReply::abort);
    }

    QPointer<RestClient> guard(this);
    connect(reply, &QNetworkReply::finished, this, [guard, reply, request, cb, attempt, bucket]() mutable {
        if (!guard) {
            reply->deleteLater();
            return;
        }

        RestResponse response;
        // The HTTP status is present even when Qt reports a transport error, and
        // reading it unconditionally is the difference between a 404 the UI can
        // explain and an opaque status 0.
        response.statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        response.body = reply->readAll();
        for (const auto& pair : reply->rawHeaderPairs()) {
            response.headers.insert(QString::fromUtf8(pair.first).toLower(),
                                    QString::fromUtf8(pair.second));
        }
        const auto transportError = reply->error();
        const QString transportErrorString = reply->errorString();
        reply->deleteLater();

        guard->noteRateLimit(bucket, response);

        if (response.statusCode == 401) {
            emit guard->unauthorized();
        }

        if (response.statusCode == 429 && attempt < request.maxRetries) {
            const QJsonObject o = response.json();
            double retryAfter = jsonDouble(o, "retry_after", 0.0);
            if (retryAfter <= 0.0) {
                retryAfter = jsonDouble(jsonObject(o, "retry_after"), "value", 0.0);
            }
            if (retryAfter <= 0.0) {
                retryAfter = headerDouble(response.headers, "x-ratelimit-reset-after", 0.0);
            }
            const int waitMs = qBound(250, int(retryAfter * 1000.0) + 50, 30000);
            emit guard->rateLimited(bucket, waitMs);

            auto& b = guard->m_buckets[bucket];
            b.blocked = true;
            b.remaining = 0;
            b.resetAtMs = QDateTime::currentMSecsSinceEpoch() + waitMs;

            RestCallback nextCb = std::move(cb);
            const int nextAttempt = attempt + 1;
            QTimer::singleShot(waitMs, guard, [guard, request, nextCb, nextAttempt, bucket]() mutable {
                if (!guard) {
                    if (nextCb) nextCb(RestResponse{});
                    return;
                }
                guard->m_buckets[bucket].blocked = false;
                guard->m_buckets[bucket].remaining = -1;
                guard->dispatch(std::move(request), std::move(nextCb), nextAttempt);
            });
            return;
        }

        if (transportError != QNetworkReply::NoError && response.statusCode == 0) {
            if (cb) {
                RestResponse failed;
                failed.statusCode = 0;
                failed.body = transportErrorString.toUtf8();
                cb(failed);
            }
            return;
        }

        if (cb) cb(response);
    });

    return reply;
}

} // namespace nimbus
