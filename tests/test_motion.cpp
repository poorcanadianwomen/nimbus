#include "../src/ui/motion.h"
#include "../src/ui/theme.h"

#include <QtTest/QtTest>
#include <QtCore/QVariantAnimation>

class TestMotion : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {}
    void test_easing_curves() {
        QVERIFY(nimbus::Motion::standardEasing().type() == QEasingCurve::OutCubic);
        QVERIFY(nimbus::Motion::emphasisEasing().type() == QEasingCurve::OutExpo);
    }
    void test_reduce_motion() {
        QCOMPARE(nimbus::Motion::durationMs(100, true), 0);
        QCOMPARE(nimbus::Motion::durationMs(100, false), 100);
    }
};

QTEST_MAIN(TestMotion)
#include "test_motion.moc"