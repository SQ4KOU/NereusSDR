#include <QtTest/QtTest>
#include "gui/widgets/ScrollableLabel.h"
using namespace NereusSDR;

class TestScrollableLabel : public QObject {
    Q_OBJECT
private slots:
    void wheelIncrementsByStep() {
        ScrollableLabel lbl;
        lbl.setRange(-10000, 10000);
        lbl.setStep(10);
        lbl.setValue(0);
        QSignalSpy spy(&lbl, &ScrollableLabel::valueChanged);
        QWheelEvent up(QPointF(5,5), QPointF(), QPoint(), QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&lbl, &up);
        QCOMPARE(lbl.value(), 10);
        QCOMPARE(spy.count(), 1);
    }
    void wheelDecrementsByStep() {
        ScrollableLabel lbl;
        lbl.setRange(-10000, 10000); lbl.setStep(10); lbl.setValue(0);
        QWheelEvent down(QPointF(5,5), QPointF(), QPoint(), QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&lbl, &down);
        QCOMPARE(lbl.value(), -10);
    }
    void clampsAtMin() {
        ScrollableLabel lbl;
        lbl.setRange(-100, 100); lbl.setStep(50); lbl.setValue(-80);
        QWheelEvent down(QPointF(), QPointF(), QPoint(), QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&lbl, &down);
        QCOMPARE(lbl.value(), -100);
    }
    void parsesInlineEdit() {
        ScrollableLabel lbl;
        lbl.setRange(-10000, 10000);
        QCOMPARE(lbl.parseValue("+120"), 120);
        QCOMPARE(lbl.parseValue("-3500"), -3500);
        QCOMPARE(lbl.parseValue("abc"), std::optional<int>{});
    }
};
QTEST_MAIN(TestScrollableLabel)
#include "tst_scrollable_label.moc"
