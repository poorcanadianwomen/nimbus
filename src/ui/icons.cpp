#include "icons.h"

#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QHash>

namespace nimbus {

IconLoader::IconLoader(QObject* parent)
    : QObject(parent) {}

QPixmap IconLoader::pixmap(const QString& path, const QColor& color, const QSize& size) {
    IconCacheKey key{path, color.rgba(), size};
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it.value();

    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        return QPixmap();
    }

    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRectF(QPointF(0, 0), size));

    // Tint
    if (color.isValid()) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pix.rect(), color);
    }
    painter.end();

    m_cache.insert(key, pix);
    return pix;
}

QIcon IconLoader::icon(const QString& path, const QColor& color, const QSize& size) {
    return QIcon(pixmap(path, color, size));
}

void IconLoader::clearCache() {
    m_cache.clear();
}

} // namespace nimbus