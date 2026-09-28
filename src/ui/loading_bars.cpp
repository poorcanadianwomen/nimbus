#include "loading_bars.h"

#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtCore/QTimer>

namespace nimbus {

LoadingBar::LoadingBar(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(2);
}

LoadingBar::~LoadingBar() = default;

void LoadingBar::setProgress(float progress) {
    m_progress = qBound(0.0f, progress, 1.0f);
    update();
}

void LoadingBar::setIndeterminate(bool indeterminate) {
    m_indeterminate = indeterminate;
    if (indeterminate) {
        static QTimer* timer = nullptr;
        if (!timer) {
            timer = new QTimer(this);
            timer->setInterval(50);
            connect(timer, &QTimer::timeout, [this]() {
                m_animOffset = (m_animOffset + 10) % 100;
                update();
            });
            timer->start();
        }
    }
    update();
}

void LoadingBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::transparent);
    // Simplified - just draw a bar
    QRectF bar(0, 0, width() * m_progress, height());
    painter.fillRect(bar, QColor("#89b4fa"));
}

} // namespace nimbus