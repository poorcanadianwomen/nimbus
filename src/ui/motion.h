#pragma once

#include <QtCore/QObject>
#include <QtCore/QEasingCurve>
#include <QtCore/QTimer>
#include <QtCore/QVariantAnimation>

namespace nimbus {

// Shared ticker and easing helpers for motion/reduce-motion
class Motion : public QObject {
    Q_OBJECT
public:
    explicit Motion(QObject* parent = nullptr);
    ~Motion();

    static QEasingCurve standardEasing();
    static QEasingCurve emphasisEasing();
    static int durationMs(int baseMs, bool reduceMotion);

    // Create a tween animation for a property
    static QVariantAnimation* tween(QObject* target, const QByteArray& property,
                                     const QVariant& from, const QVariant& to,
                                     int durationMs, bool reduceMotion = false);

signals:
    void tick(int elapsedMs);

private:
    QTimer* m_timer = nullptr;
    int m_lastTick = 0;
};

} // namespace nimbus