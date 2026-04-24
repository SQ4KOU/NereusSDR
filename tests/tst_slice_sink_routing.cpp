// =================================================================
// tests/tst_slice_sink_routing.cpp  (NereusSDR)
// Author: J.J. Boyd (KG4VCF), AI-assisted via Claude Code. 2026-04-24.
// =================================================================
#include <QtTest/QtTest>
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestSliceSinkRouting : public QObject {
    Q_OBJECT
private slots:
    void defaultIsEmpty() {
        SliceModel s;
        QCOMPARE(s.sinkNodeName(), QString());
    }
    void setterStoresValue() {
        SliceModel s;
        s.setSinkNodeName(QStringLiteral("alsa_output.usb-headphones"));
        QCOMPARE(s.sinkNodeName(), QStringLiteral("alsa_output.usb-headphones"));
    }
    void setterEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::sinkNodeNameChanged);
        s.setSinkNodeName(QStringLiteral("x"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("x"));
    }
    void noSignalOnSameValue() {
        SliceModel s;
        s.setSinkNodeName(QStringLiteral("x"));
        QSignalSpy spy(&s, &SliceModel::sinkNodeNameChanged);
        s.setSinkNodeName(QStringLiteral("x"));
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestSliceSinkRouting)
#include "tst_slice_sink_routing.moc"
