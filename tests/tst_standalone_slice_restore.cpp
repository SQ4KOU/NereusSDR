// no-port-check: NereusSDR-original test.
//
// tst_standalone_slice_restore.cpp
//
// Without a Core, receivers B to D come back at connect like A (settings
// restore audit 2026-10-11, gap 6). Their keys were always written, but
// only slice A was loaded at connect; the slices made for the other pans
// started fresh, on slice A's frequency.
//
// Drives MainWindow::populatePanSlices, the static step the connect
// handler runs for every pan that has no slice yet. No window is made, no
// radio and no audio device. The settings store is the test sandbox's
// (TestSandboxInit).

#include <QtTest/QtTest>

#include <QDir>
#include <QStandardPaths>

#include "core/AppSettings.h"
#include "core/WdspTypes.h"
#include "gui/MainWindow.h"
#include "models/Band.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr int    kRateHz       = 192000;
constexpr double kSliceAFreqHz = 14200000.0;
constexpr double kSliceBFreqHz = 7100000.0;

// What slice B was closed with: 40m LSB, its own AF gain and mute.
void saveSliceB()
{
    SliceModel saved;
    saved.setSliceIndex(1);
    saved.setFrequency(kSliceBFreqHz);
    saved.setDspMode(DSPMode::LSB);
    saved.setAfGain(33);
    saved.setMuted(true);
    saved.saveToSettings(Band::Band40m);
}

} // namespace

class TestStandaloneSliceRestore : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // The settings these tests write are the test sandbox's (Qt's test
    // mode, set up by TestSandboxInit), never the operator's.
    void theSettingsFileIsTheSandboxsNotTheOperators()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        const QString path = AppSettings::instance().filePath();
        const QString home = QDir::homePath();
        QVERIFY2(!path.startsWith(home + QStringLiteral("/.config/NereusSDR")), qPrintable(path));
        QVERIFY2(!path.startsWith(home + QStringLiteral("/Library/Preferences/NereusSDR")),
                 qPrintable(path));
    }

    void theSliceMadeForAnEmptyPanAtConnectComesBackAsItWasSaved()
    {
        saveSliceB();

        RadioModel model;
        model.configureStreamPool(2, 2, kRateHz);
        SliceModel* a = model.sliceById(model.addSlice());
        QVERIFY(a);
        a->setPanKey(QStringLiteral("pan-0"));
        a->setFrequency(kSliceAFreqHz);

        // The connect handler's call: not asked for by the operator.
        MainWindow::populatePanSlices(&model,
                                      {QStringLiteral("pan-0"), QStringLiteral("pan-1")},
                                      false, false);

        SliceModel* b = model.sliceById(1);
        QVERIFY(b);
        QCOMPARE(b->panKey(), QStringLiteral("pan-1"));
        QCOMPARE(b->frequency(), kSliceBFreqHz);
        QCOMPARE(b->dspMode(), DSPMode::LSB);
        QCOMPARE(b->afGain(), 33);
        QVERIFY(b->muted());
        // Slice A is left alone.
        QCOMPARE(a->frequency(), kSliceAFreqHz);
    }

    // Nothing saved for the slice: it starts as a new slice always has, on
    // the frequency of the slice already there.
    void aSliceWithNothingSavedStartsFresh()
    {
        RadioModel model;
        model.configureStreamPool(2, 2, kRateHz);
        SliceModel* a = model.sliceById(model.addSlice());
        QVERIFY(a);
        a->setPanKey(QStringLiteral("pan-0"));
        a->setFrequency(kSliceAFreqHz);

        MainWindow::populatePanSlices(&model,
                                      {QStringLiteral("pan-0"), QStringLiteral("pan-1")},
                                      false, false);

        SliceModel* b = model.sliceById(1);
        QVERIFY(b);
        QCOMPARE(b->frequency(), kSliceAFreqHz);
        QVERIFY(!b->muted());
    }

    // A slice the operator adds during a session (a layout change, +RX) is
    // a new slice, as before: it does not take up an old slice's state.
    void aSliceTheOperatorAddsStartsFresh()
    {
        saveSliceB();

        RadioModel model;
        model.configureStreamPool(2, 2, kRateHz);
        SliceModel* a = model.sliceById(model.addSlice());
        QVERIFY(a);
        a->setPanKey(QStringLiteral("pan-0"));
        a->setFrequency(kSliceAFreqHz);

        MainWindow::populatePanSlices(&model,
                                      {QStringLiteral("pan-0"), QStringLiteral("pan-1")},
                                      true, false);

        SliceModel* b = model.sliceById(1);
        QVERIFY(b);
        QCOMPARE(b->frequency(), kSliceAFreqHz);
        QVERIFY(!b->muted());
    }
};

QTEST_MAIN(TestStandaloneSliceRestore)
#include "tst_standalone_slice_restore.moc"
