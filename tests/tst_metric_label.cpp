#include <QtTest/QtTest>
#include "gui/widgets/MetricLabel.h"

using namespace NereusSDR;

class TstMetricLabel : public QObject {
    Q_OBJECT
private slots:
    void constructorStoresLabelAndValue() {
        MetricLabel m(QStringLiteral("PSU"), QStringLiteral("13.8V"));
        QCOMPARE(m.label(), QStringLiteral("PSU"));
        QCOMPARE(m.value(), QStringLiteral("13.8V"));
    }

    void setLabelStores() {
        MetricLabel m(QStringLiteral("X"), QStringLiteral("Y"));
        m.setLabel(QStringLiteral("PA"));
        QCOMPARE(m.label(), QStringLiteral("PA"));
    }

    void setValueStores() {
        MetricLabel m(QStringLiteral("CPU"), QStringLiteral("0%"));
        m.setValue(QStringLiteral("19%"));
        QCOMPARE(m.value(), QStringLiteral("19%"));
    }

    void identicalSetterShortCircuits() {
        // Just verify no crash; the short-circuit is internal.
        MetricLabel m(QStringLiteral("X"), QStringLiteral("1"));
        m.setValue(QStringLiteral("1"));
        m.setValue(QStringLiteral("1"));
        QCOMPARE(m.value(), QStringLiteral("1"));
    }
};
QTEST_MAIN(TstMetricLabel)
#include "tst_metric_label.moc"
