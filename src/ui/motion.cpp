#include "motion.h"

#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QEasingCurve>
#include <QtCore/QVariantAnimation>

namespace nimbus {

Motion::Motion(QObject* parent)
    : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(16); // ~60fps
    connect(m_timer, &QTimer::timeout, [this]() {
        const int now = QDateTime::currentDateTime().toMSecsSinceEpoch();
        const int elapsed = now - m_lastTick;
        m_lastTick = now;
        emit tick(elapsed);
    });
    m_timer->start();
}

Motion::~Motion() = default;

QEasingCurve Motion::standardEasing() {
    return QEasingCurve::OutCubic;
}

QEasingCurve Motion::emphasisEasing() {
    return QEasingCurve::OutExpo;
}

int Motion::durationMs(int baseMs, bool reduceMotion) {
    return reduceMotion ? 0 : baseMs;
}

QVariantAnimation* Motion::tween(QObject* target, const QByteArray& property,
                                  const QVariant& from, const QVariant& to,
                                  int durationMs, bool reduceMotion) {
    auto* anim = new QVariantAnimation(target);
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->setDuration(Motion::durationMs(durationMs, reduceMotion));
    anim->setEasingCurve(standardEasing());
    connect(anim, &QVariantAnimation::valueChanged, target, [target, property](const QVariant& v) {
        target->setProperty(property, v);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return anim;
}

} // namespace nimbus