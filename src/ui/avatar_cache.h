#pragma once

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtGui/QPixmap>

class QNetworkAccessManager;
class QTimer;

namespace nimbus {

// Images live on the CDN host, not the API host, and every fetch is authenticated
// with the session token. That makes this more than a decoder: it holds a
// credential, so it is told which token to send and never caches anything that
// came back unauthorised.
//
// The caps are not an optimisation. A transcript that has been scrolled for an
// hour will have asked for hundreds of distinct images, and an unbounded map of
// pixmaps is the slow-client failure this client exists to avoid.
class AvatarCache : public QObject {
    Q_OBJECT
public:
    explicit AvatarCache(QObject* parent = nullptr);
    ~AvatarCache() override;

    void setRestClient(class RestClient* rest);
    void setCdnUrl(const QString& url) { m_cdnUrl = url; }

    // Returns immediately: the cached pixmap if there is one, otherwise a null
    // pixmap and an asynchronous fetch that emits avatarReady when it lands.
    QPixmap image(const QString& url, int size);

    void prefetch(const QString& url, int size);

    int cachedCount() const { return m_memory.size(); }
    qint64 memoryBytes() const;
    void clear();

signals:
    void imageReady(const QString& url);

private:
    void fetch(const QString& url, int size);
    void store(const QString& url, const QPixmap& pixmap);
    void evict();
    QString cachePath(const QString& url, int size) const;
    void loadFromDisk(const QString& url, int size);

    QHash<QString, QPixmap> m_memory;
    QHash<QString, QString> m_inFlight; // cache key -> url
    QSet<QString> m_failed;
    // QHash iterates in undefined order, so recency has to be tracked explicitly
    // or eviction cannot tell a hot avatar from one seen once an hour ago.
    QHash<QString, quint64> m_used;
    quint64 m_clock = 0;

    void touch(const QString& key);

    class RestClient* m_rest = nullptr;
    QString m_cdnUrl;
    QTimer* m_evictionTimer = nullptr;

    static const int kMaxEntries = 512;
    static const qint64 kMaxBytes = 96ll * 1024 * 1024;
};

} // namespace nimbus
