#pragma once

#include <QtGui/QIcon>
#include <QtGui/QPixmap>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtCore/QString>

namespace nimbus {

struct IconCacheKey {
    QString path;
    QRgb color;
    QSize size;
    bool operator==(const IconCacheKey& other) const {
        return path == other.path && color == other.color && size == other.size;
    }
};

inline uint qHash(const IconCacheKey& key, uint seed = 0) {
    uint h = seed;
    h = qHash(key.path, h);
    h ^= key.color + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= key.size.width() + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= key.size.height() + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

class IconLoader : public QObject {
    Q_OBJECT
public:
    explicit IconLoader(QObject* parent = nullptr);

    // Load an SVG from resources, tint it, and cache the pixmap
    QPixmap pixmap(const QString& path, const QColor& color, const QSize& size);
    QIcon icon(const QString& path, const QColor& color, const QSize& size = QSize(16, 16));

    void clearCache();

private:
    QHash<IconCacheKey, QPixmap> m_cache;
};

} // namespace nimbus