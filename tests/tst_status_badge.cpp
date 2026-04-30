// tests/tst_status_badge.cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "gui/widgets/StatusBadge.h"

using namespace NereusSDR;

class TstStatusBadge : public QObject {
    Q_OBJECT

private slots:
    void defaultsToInfoVariant() {
        StatusBadge b;
        QCOMPARE(b.variant(), StatusBadge::Variant::Info);
        QVERIFY(b.icon().isEmpty());
        QVERIFY(b.label().isEmpty());
    }

    void setIconStoresAndRenders() {
        StatusBadge b;
        b.setIcon(QStringLiteral("~"));
        QCOMPARE(b.icon(), QStringLiteral("~"));
    }

    void setLabelStoresAndRenders() {
        StatusBadge b;
        b.setLabel(QStringLiteral("USB"));
        QCOMPARE(b.label(), QStringLiteral("USB"));
    }

    void setVariantUpdatesStyle() {
        StatusBadge b;
        b.setVariant(StatusBadge::Variant::On);
        QCOMPARE(b.variant(), StatusBadge::Variant::On);
        // Stylesheet must contain the green-on accent
        QVERIFY(b.styleSheet().contains(QStringLiteral("#5fff8a")));
    }

    void txVariantUsesRed() {
        StatusBadge b;
        b.setVariant(StatusBadge::Variant::Tx);
        QVERIFY(b.styleSheet().contains(QStringLiteral("#ff6060")));
    }

    void leftClickEmitsClicked() {
        StatusBadge b;
        b.resize(40, 18);
        QSignalSpy spy(&b, &StatusBadge::clicked);
        QTest::mouseClick(&b, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
    }

    void rightClickEmitsRightClicked() {
        StatusBadge b;
        b.resize(40, 18);
        QSignalSpy spy(&b, &StatusBadge::rightClicked);
        QTest::mouseClick(&b, Qt::RightButton);
        QCOMPARE(spy.count(), 1);
    }

    void identityRoundTrip() {
        StatusBadge b;
        b.setIcon(QStringLiteral("⌁"));
        b.setLabel(QStringLiteral("NR2"));
        b.setVariant(StatusBadge::Variant::On);
        QCOMPARE(b.icon(), QStringLiteral("⌁"));
        QCOMPARE(b.label(), QStringLiteral("NR2"));
        QCOMPARE(b.variant(), StatusBadge::Variant::On);
    }
};

QTEST_MAIN(TstStatusBadge)
#include "tst_status_badge.moc"
