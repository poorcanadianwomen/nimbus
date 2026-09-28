#include "avatar_cache.h"

#include "core/rest.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QFileInfo>
#include <QtGui/QPainter>

#include <algorithm>

namespace nimbus {

namespace {

// Rendering into a square rather than scaling: avatars arrive at whatever size
// the CDN has, and a non-square source stretched to a square is the usual cause
// of a blurry rail.
QPixmap scaledSquare(const QPixmap& source, int size) {
    if (source.isNull()) return {};
    QPixmap target(size, size);
    target.fill(Qt::transparent);
    QPainter painter(&target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(target.rect(), source);
    return target;
}

// QPixmap has no cheap byte-count accessor in this Qt, and toImage() would
// materialise a full copy just to measure it.
qint64 pixelBytes(const QPixmap& pixmap) {
    return qint64(pixmap.width()) * pixmap.height() * (pixmap.depth() / 8);
}

QString cacheKey(const QString& url, int size) {
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(url.toUtf8());
    hash.addData(QByteArray::number(size));
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

AvatarCache::AvatarCache(QObject* parent)
    : QObject(parent) {
    // Eviction runs on a timer rather than inside store(), so a burst of
    // completions does not each pay for a full pass over the map.
    m_evictionTimer = new QTimer(this);
    m_evictionTimer->setInterval(2000);
    connect(m_evictionTimer, &QTimer::timeout, this, &AvatarCache::evict);
    m_evictionTimer->start();
}

AvatarCache::~AvatarCache() = default;

void AvatarCache::setRestClient(RestClient* rest) {
    if (m_rest == rest) return;
    m_rest = rest;
    // The token lives in the rest client, so a logout must not leave cached
    // images fetched under the old session still on disk.
    if (rest) {
        connect(rest, &RestClient::unauthorized, this, [this]() {
            clear();
            m_failed.clear();
        });
    }
}

QPixmap AvatarCache::image(const QString& url, int size) {
    if (url.isEmpty()) return {};

    const QString key = cacheKey(url, size);
    const auto cached = m_memory.constFind(key);
    if (cached != m_memory.constEnd()) {
        touch(key);
        return cached.value();
    }

    // A miss means the caller paints its resting placeholder and gets imageReady
    // later. Growing the row when a bitmap lands would reflow the whole
    // transcript, so the placeholder has to be the row's real size.
    if (!m_failed.contains(key)) fetch(url, size);
    return {};
}

void AvatarCache::prefetch(const QString& url, int size) {
    if (url.isEmpty()) return;
    const QString key = cacheKey(url, size);
    if (m_memory.contains(key) || m_inFlight.contains(key) || m_failed.contains(key)) return;
    fetch(url, size);
}

QString AvatarCache::cachePath(const QString& url, int size) const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                        QStringLiteral("/images");
    return dir + QLatin1Char('/') + cacheKey(url, size);
}

void AvatarCache::loadFromDisk(const QString& url, int size) {
    const QString path = cachePath(url, size);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QPixmap pixmap;
    if (!pixmap.loadFromData(file.readAll())) return;
    store(url, scaledSquare(pixmap, size));
}

void AvatarCache::fetch(const QString& url, int size) {
    if (!m_rest) return;

    const QString key = cacheKey(url, size);
    if (m_inFlight.contains(key)) return;

    // Try the on-disk copy before spending a request: on a relaunch most of the
    // rail is already cached and re-fetching it is pure latency.
    loadFromDisk(url, size);
    if (m_memory.contains(key)) return;

    m_inFlight.insert(key, url);
    m_rest->getUrl(QUrl(url), [this, url, size, key](const RestResponse& response) {
        m_inFlight.remove(key);
        if (!response.ok()) {
            // Remember the failure so a broken url is not retried on every
            // repaint. Avatars are behind authentication, so a 401 here means the
            // whole cache is stale rather than this one image.
            if (response.statusCode != 404) m_failed.insert(key);
            return;
        }
        QPixmap pixmap;
        if (!pixmap.loadFromData(response.body)) {
            m_failed.insert(key);
            return;
        }

        const QPixmap square = scaledSquare(pixmap, size);
        if (square.isNull()) return;

        QDir().mkpath(QFileInfo(cachePath(url, size)).absolutePath());
        QFile file(cachePath(url, size));
        if (file.open(QIODevice::WriteOnly)) {
            square.save(&file, "PNG");
            file.close();
        }

        m_memory.insert(key, square);
        touch(key);
        evict();
        emit imageReady(url);
    });
}

void AvatarCache::touch(const QString& key) {
    m_used.insert(key, ++m_clock);
}

void AvatarCache::store(const QString& url, const QPixmap& pixmap) {
    if (pixmap.isNull()) return;
    const QString key = cacheKey(url, pixmap.width());
    m_memory.insert(key, pixmap);
    touch(key);
}

qint64 AvatarCache::memoryBytes() const {
    qint64 total = 0;
    for (const QPixmap& pixmap : m_memory) {
        total += pixelBytes(pixmap);
    }
    return total;
}

void AvatarCache::evict() {
    if (m_memory.size() <= kMaxEntries && memoryBytes() <= kMaxBytes) return;

    // Least recently used first. Sorting a few hundred keys on a timer is not
    // worth optimising, and getting it wrong means a rail the user is looking at
    // blanks out while they scroll.
    QList<QPair<quint64, QString>> byAge;
    byAge.reserve(m_memory.size());
    for (auto it = m_memory.constBegin(); it != m_memory.constEnd(); ++it) {
        byAge.append({m_used.value(it.key()), it.key()});
    }
    std::sort(byAge.begin(), byAge.end(),
              [](const QPair<quint64, QString>& a, const QPair<quint64, QString>& b) {
                  return a.first < b.first;
              });

    int live = m_memory.size();
    qint64 bytes = memoryBytes();
    for (const auto& entry : byAge) {
        if (live <= kMaxEntries && bytes <= kMaxBytes) break;
        const auto victim = m_memory.constFind(entry.second);
        if (victim == m_memory.constEnd()) continue;
        bytes -= pixelBytes(*victim);
        m_memory.erase(m_memory.find(entry.second));
        m_used.remove(entry.second);
        --live;
    }
}

void AvatarCache::clear() {
    m_memory.clear();
    m_inFlight.clear();
    m_failed.clear();
    m_used.clear();
}

} // namespace nimbus
