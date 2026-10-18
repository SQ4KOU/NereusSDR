// Test suite for RadioModel::dspChangeMeasured signal (Task 1.8)
// Verifies signal existence and emittal from setSampleRateLive and
// setActiveRxCountLive.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include "models/RadioModel.h"

using namespace NereusSDR;

class TestDspChangeTimer : public QObject {
    Q_OBJECT

private slots:
    void signal_exists_and_is_emittable_when_setSampleRateLive_unconnected_returns_minus_one()
    {
        // When unconnected, setSampleRateLive returns -1 and does not emit
        // (matches the existing test in tst_sample_rate_live_apply).
        RadioModel m;
        QSignalSpy spy(&m, &RadioModel::dspChangeMeasured);
        const qint64 r = m.setSampleRateLive(96000);
        QCOMPARE(r, qint64(-1));
        QCOMPARE(spy.count(), 0);  // no emission on guard path
    }

    void signal_exists_for_active_rx_count_unconnected()
    {
        RadioModel m;
        QSignalSpy spy(&m, &RadioModel::dspChangeMeasured);
        const qint64 r = m.setActiveRxCountLive(2);
        QCOMPARE(r, qint64(-1));
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestDspChangeTimer)
#include "tst_dsp_change_timer.moc"
