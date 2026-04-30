// no-port-check: NereusSDR-original test file.  All Thetis source cites
// for the underlying TransmitModel properties live in TransmitModel.h
// and the dialog source itself.
// =================================================================
// tests/tst_tx_eq_dialog.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-3a-i Batch 3 (Task A.1) — TxEqDialog scaffold smoke tests.
//
// TxEqDialog is the modeless 10-band TX EQ dialog launched from the
// TxApplet's [EQ] right-click and the Tools → TX Equalizer menu.
// Controls bidirectionally bind to RadioModel::transmitModel() with
// an m_updatingFromModel echo guard.
//
// Tests:
//   1. Dialog constructs without crash (RadioModel default ctor).
//   2. Initial values populate from TransmitModel defaults
//      (preamp=0, band[0]=-12, freq[0]=32, enable=false, Nc=2048).
//   3. Move preamp slider → TransmitModel.txEqPreampChanged emitted.
//   4. Move band 0 slider → TransmitModel.txEqBandChanged emitted with
//      idx=0 + new value.
//   5. Move freq 0 spinbox → TransmitModel.txEqFreqChanged emitted.
//   6. Toggle enable checkbox → TransmitModel.txEqEnabledChanged emitted.
//   7. setTxEqPreamp(N) external setter → dialog preamp slider/spin
//      updates to N (round-trip via syncFromModel).
//   8. Echo guard: setting a TransmitModel value that triggers UI
//      update doesn't cause a re-emit storm (no infinite loop —
//      each setter only fires its own signal once).
//   9. Singleton: TxEqDialog::instance(...) returns the same pointer
//      on repeated calls.
//
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>

#include "core/AppSettings.h"
#include "gui/applets/TxEqDialog.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TestTxEqDialog : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // ── 1. Construct ────────────────────────────────────────────────
    void constructsWithoutCrash()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        QVERIFY(dlg.findChild<QCheckBox*>(QStringLiteral("TxEqEnableChk")));
        QVERIFY(dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider")));
    }

    // ── 2. Initial values populate from TransmitModel defaults ─────
    void initialValues_matchTransmitModelDefaults()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        TransmitModel& tx = rm.transmitModel();

        // Enable default off.
        auto* en = dlg.findChild<QCheckBox*>(QStringLiteral("TxEqEnableChk"));
        QVERIFY(en);
        QCOMPARE(en->isChecked(), tx.txEqEnabled());
        QCOMPARE(en->isChecked(), false);

        // Preamp default 0.
        auto* pre = dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider"));
        auto* preSpin = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqPreampSpin"));
        QVERIFY(pre);
        QVERIFY(preSpin);
        QCOMPARE(pre->value(), tx.txEqPreamp());
        QCOMPARE(preSpin->value(), tx.txEqPreamp());
        QCOMPARE(pre->value(), 0);

        // Band 0 default -12 (matches TransmitModel m_txEqBand init).
        auto* b0 = dlg.findChild<QSlider*>(QStringLiteral("TxEqBandSlider0"));
        auto* b0s = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqBandSpin0"));
        QVERIFY(b0);
        QVERIFY(b0s);
        QCOMPARE(b0->value(), tx.txEqBand(0));
        QCOMPARE(b0s->value(), tx.txEqBand(0));
        QCOMPARE(b0->value(), -12);

        // Freq 0 default 32 Hz.
        auto* f0 = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqFreqSpin0"));
        QVERIFY(f0);
        QCOMPARE(f0->value(), tx.txEqFreq(0));
        QCOMPARE(f0->value(), 32);

        // Nc default 2048.
        auto* nc = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqNcSpin"));
        QVERIFY(nc);
        QCOMPARE(nc->value(), tx.txEqNc());
        QCOMPARE(nc->value(), 2048);

        // Mp default off.
        auto* mp = dlg.findChild<QCheckBox*>(QStringLiteral("TxEqMpChk"));
        QVERIFY(mp);
        QCOMPARE(mp->isChecked(), tx.txEqMp());
        QCOMPARE(mp->isChecked(), false);

        // Ctfmode default 0, Wintype default 0.
        auto* ctf = dlg.findChild<QComboBox*>(QStringLiteral("TxEqCtfmodeCombo"));
        auto* win = dlg.findChild<QComboBox*>(QStringLiteral("TxEqWintypeCombo"));
        QVERIFY(ctf);
        QVERIFY(win);
        QCOMPARE(ctf->currentIndex(), tx.txEqCtfmode());
        QCOMPARE(win->currentIndex(), tx.txEqWintype());
        QCOMPARE(ctf->currentIndex(), 0);
        QCOMPARE(win->currentIndex(), 0);
    }

    // ── 3. Preamp slider → txEqPreampChanged ────────────────────────
    void preampSlider_emitsTxEqPreampChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqPreampChanged);

        auto* pre = dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider"));
        QVERIFY(pre);
        pre->setValue(7);

        // Slider emits valueChanged → onPreampChanged → setTxEqPreamp → signal.
        // Note: the model→UI sync handler also fires syncFromModel which
        // re-sets the spinbox; but setTxEqPreamp itself only fires once.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 7);
        QCOMPARE(tx.txEqPreamp(), 7);
    }

    // ── 4. Band 0 slider → txEqBandChanged with idx=0 ───────────────
    void band0Slider_emitsTxEqBandChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqBandChanged);

        auto* b0 = dlg.findChild<QSlider*>(QStringLiteral("TxEqBandSlider0"));
        QVERIFY(b0);
        b0->setValue(5);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 0);
        QCOMPARE(args.at(1).toInt(), 5);
        QCOMPARE(tx.txEqBand(0), 5);
    }

    // ── 5. Freq 0 spinbox → txEqFreqChanged with idx=0 ──────────────
    void freq0Spin_emitsTxEqFreqChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqFreqChanged);

        auto* f0 = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqFreqSpin0"));
        QVERIFY(f0);
        f0->setValue(75);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 0);
        QCOMPARE(args.at(1).toInt(), 75);
        QCOMPARE(tx.txEqFreq(0), 75);
    }

    // ── 6. Enable checkbox → txEqEnabledChanged ─────────────────────
    void enableCheckbox_emitsTxEqEnabledChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqEnabledChanged);

        auto* en = dlg.findChild<QCheckBox*>(QStringLiteral("TxEqEnableChk"));
        QVERIFY(en);
        en->setChecked(true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
        QCOMPARE(tx.txEqEnabled(), true);
    }

    // ── 7. External setTxEqPreamp(N) → dialog UI updates ────────────
    void externalSetTxEqPreamp_updatesDialogPreamp()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();

        auto* pre     = dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider"));
        auto* preSpin = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqPreampSpin"));
        QVERIFY(pre && preSpin);
        QCOMPARE(pre->value(), 0);

        tx.setTxEqPreamp(11);
        QCOMPARE(pre->value(), 11);
        QCOMPARE(preSpin->value(), 11);
    }

    // ── 8. Echo guard — model setter fires signal exactly once ──────
    // If the echo guard were missing, the slider valueChanged from
    // syncFromModel would call back into setTxEqPreamp, which would
    // emit again, etc.  Verify the signal count stays at 1.
    void echoGuard_externalSetterDoesNotReEmit()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqPreampChanged);

        tx.setTxEqPreamp(4);   // single emit expected
        QCOMPARE(spy.count(), 1);

        tx.setTxEqPreamp(4);   // no-op — value unchanged, no re-emit
        QCOMPARE(spy.count(), 1);
    }

    // ── 9. Singleton — instance() returns same pointer ──────────────
    void singleton_returnsSameInstance()
    {
        RadioModel rm;
        TxEqDialog* a = TxEqDialog::instance(&rm);
        TxEqDialog* b = TxEqDialog::instance(&rm);
        QVERIFY(a != nullptr);
        QCOMPARE(a, b);
        // Cleanup — the singleton survives across tests, so delete it
        // explicitly to avoid leaks across test-method boundaries.
        delete a;
    }
};

QTEST_MAIN(TestTxEqDialog)
#include "tst_tx_eq_dialog.moc"
