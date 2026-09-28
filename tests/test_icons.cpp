#include "../src/ui/icons.h"
#include "../src/ui/theme.h"

#include <QtTest/QtTest>
#include <QtGui/QPixmap>

class TestIcons : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {}
    void test_icon_load() {
        nimbus::IconLoader loader;
        QPixmap pix = loader.pixmap(":/icons/tabler/home.svg", Qt::white, QSize(16, 16));
        QVERIFY(!pix.isNull());
        QCOMPARE(pix.size(), QSize(16, 16));
    }
};

QTEST_MAIN(TestIcons)
#include "test_icons.moc"