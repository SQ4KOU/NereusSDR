// =================================================================
// tests/tst_pan_models.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Display pill plan Task 9 (D29, D19, S9, R-DSP-27, R-DSP-38, R-DSP-49):
// PanModels gives every pan of a window a PanadapterModel of its own. The
// pan's band follows the pan's centre frequency, the band's stored dB Max
// and Min set the pan's receive range, the store is shared by every pan,
// and the per-band 3D Floor is recalled and saved. The old per-pan range
// keys are ignored. The models are the window's: RadioModel's pan list
// stays empty.
// =================================================================
#include <QtTest/QtTest>

#include <QLoggingCategory>
#include <QSignalSpy>
#include <QStandardPaths>

#include "core/AppSettings.h"
#include "fakes/MainWindowTestSettings.h"
#include "gui/MainWindow.h"
#include "gui/PanModels.h"
#include "gui/PanadapterStack.h"
#include "gui/SpectrumWidget.h"
#include "models/Band.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
using namespace Qt::StringLiterals;

namespace {
constexpr double k20m = 14'200'000.0;
constexpr double k40m = 7'100'000.0;
constexpr double k2m  = 144'300'000.0;

float rangeMin(const SpectrumWidget& w) { return w.refLevel() - w.dynamicRange(); }
float rangeMax(const SpectrumWidget& w) { return w.refLevel(); }

int countFor(const QSignalSpy& spy, const QString& panId)
{
    int n = 0;
    for (const QList<QVariant>& args : spy) {
        if (args.value(0).toString() == panId) { ++n; }
    }
    return n;
}
} // namespace

class TestPanModels : public QObject {
    Q_OBJECT
    QString m_isolatedSettingsPath;

private slots:
    void initTestCase()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        const QString profile =
            QStringLiteral("pan-models-%1").arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        m_isolatedSettingsPath = AppSettings::resolveSettingsPath(profile);
        QCOMPARE(AppSettings::instance().filePath(), m_isolatedSettingsPath);
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        if (AppSettings::instance().filePath() == m_isolatedSettingsPath) {
            AppSettings::instance().clear();
        }
    }

    // One model per pan; retire deletes it and every connection with it.
    void ensureTwiceIsOneModel_retireRemovesItAndItsConnections()
    {
        SpectrumWidget w0;
        w0.setCenterFrequency(k20m);
        PanModels models;
        PanadapterModel* first = models.ensure(u"pan-0"_s, &w0);
        QVERIFY(first);
        QCOMPARE(first->parent(), &models);
        QVERIFY(first->followsBandGrid());
        QCOMPARE(models.ensure(u"pan-0"_s, &w0), first);
        QCOMPARE(models.model(u"pan-0"_s), first);
        QCOMPARE(models.all(), QList<PanadapterModel*>{first});

        QVERIFY(!models.ensure(QString(), &w0));
        QVERIFY(!models.ensure(u"pan-9"_s, nullptr));
        QVERIFY(!models.model(u"pan-9"_s));

        QSignalSpy grid(&models, &PanModels::bandGridChanged);
        models.retire(u"pan-0"_s);
        QVERIFY(!models.model(u"pan-0"_s));
        QVERIFY(models.all().isEmpty());
        // Nothing of the retired model is left on the widget.
        const float before = rangeMax(w0);
        w0.setCenterFrequency(k40m);
        w0.setDssFloorDepth(9);
        QCOMPARE(grid.count(), 0);
        QCOMPARE(rangeMax(w0), before);
        QVERIFY(!AppSettings::instance().contains(u"Display3DFloorDepth_40m"_s));
        models.retire(u"pan-0"_s);   // unknown now: nothing happens
    }

    // The same pan id on a new widget (the pan was rebuilt) gets a model
    // wired to the new widget.
    void ensureWithANewWidgetRewires()
    {
        PanModels models;
        auto old = std::make_unique<SpectrumWidget>();
        old->setCenterFrequency(k20m);
        QVERIFY(models.ensure(u"pan-0"_s, old.get()));
        SpectrumWidget replacement;
        replacement.setCenterFrequency(k40m);
        PanadapterModel* model = models.ensure(u"pan-0"_s, &replacement);
        QVERIFY(model);
        QCOMPARE(model->band(), Band::Band40m);
        QCOMPARE(models.all().size(), 1);
        old.reset();
        replacement.setCenterFrequency(k20m);
        QCOMPARE(model->band(), Band::Band20m);
    }

    // The old native-global propagation contract is superseded. Legacy
    // PanadapterModel callers still share their stored step, while already
    // loaded native widgets retain their own picture choices.
    void legacyModelStepSharingDoesNotReplaceNativePictureChoices()
    {
        SpectrumWidget w0;
        SpectrumWidget w1;
        w0.setPanIndex(0); w0.loadSettings();
        w1.setPanIndex(1); w1.loadSettings();
        w0.setDbGridStep(7);
        w0.setCenterFrequency(k20m);
        w1.setCenterFrequency(k40m);
        PanModels models;
        PanadapterModel* m0 = models.ensure(u"pan-0"_s, &w0);
        PanadapterModel* m1 = models.ensure(u"pan-1"_s, &w1);
        QVERIFY(m0 && m1);
        QCOMPARE(w0.dbGridStep(), 7);
        QCOMPARE(w1.dbGridStep(), 10);

        m0->setGridStep(5);
        QCOMPARE(m1->gridStep(), 5);
        QCOMPARE(w0.dbGridStep(), 7);
        QCOMPARE(w1.dbGridStep(), 10);
        QCOMPARE(AppSettings::instance().value(u"DisplayGridStep"_s).toInt(), 5);

        // A pan opened after the change reads the bare master from its
        // native settings load, rather than a push from its band model.
        SpectrumWidget later;
        later.setPanIndex(2); later.loadSettings();
        later.setCenterFrequency(k20m);
        QCOMPARE(later.dbGridStep(), 5);
        QVERIFY(models.ensure(u"pan-2"_s, &later));
        QCOMPARE(later.dbGridStep(), 5);

        // A second window's models retain the legacy sharing contract.
        SpectrumWidget inSecondWindow;
        inSecondWindow.setPanIndex(3); inSecondWindow.loadSettings();
        inSecondWindow.setCenterFrequency(k40m);
        PanModels secondWindow;
        PanadapterModel* other = secondWindow.ensure(u"pan-0"_s, &inSecondWindow);
        QVERIFY(other);
        QCOMPARE(other->gridStep(), 5);
        QCOMPARE(inSecondWindow.dbGridStep(), 5);
        other->setGridStep(20);
        QCOMPARE(m0->gridStep(), 20);
        QCOMPARE(m1->gridStep(), 20);
        QCOMPARE(w0.dbGridStep(), 7);
        QCOMPARE(w1.dbGridStep(), 10);
        QCOMPARE(later.dbGridStep(), 5);
        QCOMPARE(inSecondWindow.dbGridStep(), 5);

        // A step the model refuses changes nothing.
        m1->setGridStep(0);
        QCOMPARE(m1->gridStep(), 20);
        QCOMPARE(w1.dbGridStep(), 10);

        // A retired pan's widget is no longer driven.
        models.retire(u"pan-1"_s);
        m0->setGridStep(8);
        QCOMPARE(m0->gridStep(), 8);
        QCOMPARE(other->gridStep(), 8);
        QCOMPARE(w0.dbGridStep(), 7);
        QCOMPARE(w1.dbGridStep(), 10);
    }

    void nativeDbStepOverridesSurviveBandModelBindingAndDrawOwnSpacing()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(u"DisplayGridStep"_s, u"10"_s);
        settings.setValue(u"DisplayGridStep_1"_s, u"5"_s);
        SpectrumWidget first, second;
        first.setPanIndex(1); first.loadSettings();
        second.setPanIndex(2); second.loadSettings();
        first.setCenterFrequency(k20m);
        second.setCenterFrequency(k40m);
        PanModels models;
        QVERIFY(models.ensure(u"pan-1"_s, &first));
        QVERIFY(models.ensure(u"pan-2"_s, &second));
        QCOMPARE(first.dbGridStep(), 5);
        QCOMPARE(second.dbGridStep(), 10);
        first.setDbmRange(-140.0f, -40.0f);
        second.setDbmRange(-140.0f, -40.0f);
        QCOMPARE(first.dbGridLinesForTest().size(), 19);
        QCOMPARE(second.dbGridLinesForTest().size(), 9);
        first.setDbGridStep(7);
        first.saveSettings();
        QCOMPARE(first.dbGridLinesForTest().size(), 14);
        QCOMPARE(second.dbGridLinesForTest().size(), 9);
        const QVector<float> lines = first.dbGridLinesForTest();
        for (int i = 1; i < lines.size(); ++i) { QCOMPARE(lines[i] - lines[i - 1], 7.0f); }
        first.setCenterFrequency(k40m);
        QCOMPARE(first.dbGridStep(), 7);
        QCOMPARE(settings.value(u"DisplayGridStep"_s).toString(), u"10"_s);
        QCOMPARE(settings.value(u"DisplayGridStep_1"_s).toString(), u"7"_s);
    }

    // The stored step is what a pan restored at startup draws with.
    void storedDbStepIsReadAtStartup()
    {
        AppSettings::instance().setValue(u"DisplayGridStep"_s, 5);
        SpectrumWidget w0;
        w0.loadSettings();
        w0.setCenterFrequency(k20m);
        PanModels models;
        PanadapterModel* m0 = models.ensure(u"pan-0"_s, &w0);
        QVERIFY(m0);
        QCOMPARE(m0->gridStep(), 5);
        QCOMPARE(w0.dbGridStep(), 5);
        w0.setDbmRange(-140.0f, -40.0f);
        QCOMPARE(w0.dbGridLinesForTest().size(), 19);
    }

    void sparseIdsKeepTheirOwnModels()
    {
        SpectrumWidget w0;
        SpectrumWidget w3;
        w0.setCenterFrequency(k20m);
        w3.setCenterFrequency(k40m);
        PanModels models;
        PanadapterModel* m0 = models.ensure(u"pan-0"_s, &w0);
        PanadapterModel* m3 = models.ensure(u"pan-3"_s, &w3);
        QVERIFY(m0 != m3);
        QCOMPARE(models.all().size(), 2);
        models.retire(u"pan-0"_s);
        QCOMPARE(models.model(u"pan-3"_s), m3);
        QCOMPARE(m3->band(), Band::Band40m);
        w3.setCenterFrequency(k20m);
        QCOMPARE(m3->band(), Band::Band20m);
    }

    // The pan's band is the band of the pan's centre, seeded and followed.
    void bandFollowsTheWidgetCentre()
    {
        SpectrumWidget w0;
        w0.setCenterFrequency(k40m);
        PanModels models;
        PanadapterModel* model = models.ensure(u"pan-0"_s, &w0);
        QCOMPARE(model->band(), Band::Band40m);   // seeded on creation

        QSignalSpy grid(&models, &PanModels::bandGridChanged);
        QSignalSpy centre(&w0, &SpectrumWidget::centerFrequencyChanged);
        w0.setCenterFrequency(k20m);
        QCOMPARE(centre.count(), 1);
        QCOMPARE(centre.first().first().toDouble(), k20m);
        QCOMPARE(model->band(), Band::Band20m);
        QVERIFY(countFor(grid, u"pan-0"_s) >= 1);

        grid.clear();
        w0.setCenterFrequency(k40m);
        QCOMPARE(model->band(), Band::Band40m);
        QVERIFY(countFor(grid, u"pan-0"_s) >= 1);

        // A same-value set says nothing; a move through setFrequencyRange
        // (a pan drag, a band jump) is followed too.
        centre.clear();
        w0.setCenterFrequency(k40m);
        QCOMPARE(centre.count(), 0);
        w0.setFrequencyRange(k20m, w0.bandwidth());
        QCOMPARE(centre.count(), 1);
        QCOMPARE(model->band(), Band::Band20m);
    }

    // The band's stored dB Max and Min are the pan's range; a band with
    // nothing stored shows the range a pan showed before it had a band
    // store (the widget's own default, -116 to -48).
    void storedBandRangeSetsTheWidgetRange()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayGridMax_40m"_s, -30);
        s.setValue(u"DisplayGridMin_40m"_s, -120);
        SpectrumWidget w0;
        w0.setCenterFrequency(k20m);
        PanModels models;
        models.ensure(u"pan-0"_s, &w0);
        // Applied once on creation: 20 m has nothing stored.
        QCOMPARE(rangeMin(w0), -116.0f);
        QCOMPARE(rangeMax(w0), -48.0f);

        w0.setCenterFrequency(k40m);
        QCOMPARE(rangeMin(w0), -120.0f);
        QCOMPARE(rangeMax(w0), -30.0f);

        w0.setCenterFrequency(k20m);
        QCOMPARE(rangeMin(w0), -116.0f);
        QCOMPARE(rangeMax(w0), -48.0f);
    }

    // Arriving on a band shows its stored range even when that equals the
    // last band's and the live range was moved in between.
    void arrivingOnABandShowsItsRangeAfterALiveMove()
    {
        SpectrumWidget w0;
        w0.setCenterFrequency(k20m);
        PanModels models;
        models.ensure(u"pan-0"_s, &w0);
        w0.setDbmRange(-100.0f, -20.0f);   // a drag, or the grid follow
        w0.setCenterFrequency(k40m);       // 40 m: the same default pair
        QCOMPARE(rangeMin(w0), -116.0f);
        QCOMPARE(rangeMax(w0), -48.0f);
    }

    // One store for every pan: a change through one model moves every pan
    // on that band and no other.
    void perBandStoreIsSharedByEveryPan()
    {
        SpectrumWidget w0;
        SpectrumWidget w1;
        SpectrumWidget w2;
        w0.setCenterFrequency(k40m);
        w1.setCenterFrequency(k40m);
        w2.setCenterFrequency(k20m);
        PanModels models;
        PanadapterModel* m0 = models.ensure(u"pan-0"_s, &w0);
        models.ensure(u"pan-1"_s, &w1);
        models.ensure(u"pan-2"_s, &w2);
        QSignalSpy grid(&models, &PanModels::bandGridChanged);

        m0->setPerBandDbMax(Band::Band40m, -25);
        QCOMPARE(rangeMax(w0), -25.0f);
        QCOMPARE(rangeMax(w1), -25.0f);
        QCOMPARE(rangeMin(w1), -116.0f);
        QCOMPARE(rangeMax(w2), -48.0f);
        QCOMPARE(AppSettings::instance().value(u"DisplayGridMax_40m"_s).toInt(), -25);
        // The edit stores the pair that is drawn, not another default.
        QCOMPARE(AppSettings::instance().value(u"DisplayGridMin_40m"_s).toInt(), -116);
        QVERIFY(!AppSettings::instance().contains(u"DisplayGridMax_20m"_s));
        QCOMPARE(countFor(grid, u"pan-0"_s), 1);
        QCOMPARE(countFor(grid, u"pan-1"_s), 1);
        QCOMPARE(countFor(grid, u"pan-2"_s), 0);

        // The pan on 20 m has the new value when it gets to 40 m.
        w2.setCenterFrequency(k40m);
        QCOMPARE(rangeMax(w2), -25.0f);
    }

    // Fix wave I2: the band keys are the station's, so an edit writes the
    // key of the end that changed, once, however many pans there are. Each
    // model used to save both keys, so with two pans one end was four
    // writes and a two-ended edit eight, and the end that had not changed
    // was sent again with its old value.
    void oneBandRangeEditWritesOnlyWhatChanged()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayGridMax_40m"_s, -30);
        s.setValue(u"DisplayGridMin_40m"_s, -120);
        SpectrumWidget w0;
        SpectrumWidget w1;
        w0.setCenterFrequency(k40m);
        w1.setCenterFrequency(k40m);
        PanModels models;
        PanadapterModel* m0 = models.ensure(u"pan-0"_s, &w0);
        PanadapterModel* m1 = models.ensure(u"pan-1"_s, &w1);

        QStringList written;
        s.setChangeHook([&written](const QString& key) { written.append(key); });
        const auto unhook = qScopeGuard([&s]() { s.setChangeHook(nullptr); });

        // One end.
        m0->setPerBandDbMax(Band::Band40m, -25);
        QCOMPARE(written, QStringList{u"DisplayGridMax_40m"_s});
        QCOMPARE(s.value(u"DisplayGridMax_40m"_s).toInt(), -25);
        QCOMPARE(m1->perBandGrid(Band::Band40m).dbMax, -25);
        QCOMPARE(rangeMax(w1), -25.0f);
        QCOMPARE(rangeMin(w1), -120.0f);

        written.clear();
        m1->setPerBandDbMin(Band::Band40m, -125);
        QCOMPARE(written, QStringList{u"DisplayGridMin_40m"_s});
        QCOMPARE(m0->perBandGrid(Band::Band40m).dbMin, -125);
        QCOMPARE(rangeMin(w0), -125.0f);

        // Both ends, one step: two writes, one for each key.
        written.clear();
        m0->setPerBandDbRange(Band::Band40m, -20, -110);
        written.sort();
        QCOMPARE(written, (QStringList{u"DisplayGridMax_40m"_s, u"DisplayGridMin_40m"_s}));
        QCOMPARE(rangeMax(w0), -20.0f);
        QCOMPARE(rangeMin(w0), -110.0f);
        QCOMPARE(rangeMax(w1), -20.0f);
        QCOMPARE(rangeMin(w1), -110.0f);

        // Both ends asked for, one moved: one write.
        written.clear();
        m0->setPerBandDbRange(Band::Band40m, -20, -115);
        QCOMPARE(written, QStringList{u"DisplayGridMin_40m"_s});
        // The stored pair again, and an upside-down pair: nothing.
        written.clear();
        m1->setPerBandDbRange(Band::Band40m, -20, -115);
        m1->setPerBandDbMax(Band::Band40m, -20);
        m0->setPerBandDbRange(Band::Band40m, -130, -100);
        QVERIFY2(written.isEmpty(), qPrintable(written.join(u", "_s)));
        QCOMPARE(m0->perBandGrid(Band::Band40m).dbMax, -20);

        // A pair that sits wholly under the old one never shows crossed.
        QList<QPair<float, float>> seen;
        connect(&models, &PanModels::bandGridChanged, &w1, [&seen, &w1](const QString&) {
            seen.append({rangeMin(w1), rangeMax(w1)});
        });
        m0->setPerBandDbRange(Band::Band40m, -130, -150);
        QCOMPARE(rangeMax(w1), -130.0f);
        QCOMPARE(rangeMin(w1), -150.0f);
        for (const auto& range : std::as_const(seen)) {
            QVERIFY2(range.second > range.first,
                     qPrintable(QStringLiteral("%1 to %2").arg(range.first).arg(range.second)));
        }

        // A band with nothing stored: its first edit stores the pair that
        // is drawn (Task 9), then one key an edit like any other band.
        written.clear();
        m0->setPerBandDbMax(Band::Band20m, -45);
        written.sort();
        QCOMPARE(written, (QStringList{u"DisplayGridMax_20m"_s, u"DisplayGridMin_20m"_s}));
        QCOMPARE(s.value(u"DisplayGridMin_20m"_s).toInt(), -116);
        written.clear();
        m1->setPerBandDbMax(Band::Band20m, -44);
        QCOMPARE(written, QStringList{u"DisplayGridMax_20m"_s});
    }

    // A value changed underneath (it arrived from the Core): pans on that
    // band apply it, others do not.
    void stationGridSettingReachesPansOnThatBand()
    {
        SpectrumWidget w0;
        SpectrumWidget w1;
        w0.setCenterFrequency(k40m);
        w1.setCenterFrequency(k20m);
        PanModels models;
        models.ensure(u"pan-0"_s, &w0);
        PanadapterModel* m1 = models.ensure(u"pan-1"_s, &w1);

        AppSettings::instance().setValue(u"DisplayGridMax_40m"_s, -35);
        QCOMPARE(rangeMax(w0), -48.0f);   // nothing told the models yet
        models.applyStationGridSetting(u"DisplayGridMax_40m"_s);
        QCOMPARE(rangeMax(w0), -35.0f);
        QCOMPARE(rangeMin(w0), -116.0f);  // the half with no key
        QCOMPARE(rangeMax(w1), -48.0f);
        QCOMPARE(m1->perBandGrid(Band::Band40m).dbMax, -35);

        // An empty key is a whole snapshot.
        AppSettings::instance().setValue(u"DisplayGridMin_20m"_s, -125);
        models.applyStationGridSetting(QString());
        QCOMPARE(rangeMin(w1), -125.0f);
        QCOMPARE(rangeMax(w1), -48.0f);
        QCOMPARE(rangeMax(w0), -35.0f);

        // The key going away again (the Core removed it) is the unset pair.
        AppSettings::instance().remove(u"DisplayGridMax_40m"_s);
        models.applyStationGridSetting(u"DisplayGridMax_40m"_s);
        QCOMPARE(rangeMax(w0), -48.0f);
    }

    // Nothing stored and no key from an older install: every band shows
    // the widget's old default, the models report that same pair, and no
    // key is written until an edit.
    void unsetBandShowsTheOldWidgetDefault_andWritesNothing()
    {
        auto& s = AppSettings::instance();
        SpectrumWidget w0;
        const float defaultMax = rangeMax(w0);
        const float defaultMin = rangeMin(w0);
        QCOMPARE(defaultMax, -48.0f);    // the widget's own default
        QCOMPARE(defaultMin, -116.0f);
        w0.setCenterFrequency(k20m);
        PanModels models;
        PanadapterModel* model = models.ensure(u"pan-0"_s, &w0);
        QCOMPARE(rangeMax(w0), defaultMax);
        QCOMPARE(rangeMin(w0), defaultMin);
        QCOMPARE(model->dBmCeiling(), -48);
        QCOMPARE(model->dBmFloor(), -116);
        for (const Band b : { Band::Band20m, Band::Band40m, Band::GEN, Band::XVTR,
                              Band::Band2m }) {
            QCOMPARE(model->perBandGrid(b).dbMax, -48);
            QCOMPARE(model->perBandGrid(b).dbMin, -116);
        }
        w0.setCenterFrequency(k40m);
        QCOMPARE(rangeMax(w0), -48.0f);
        QCOMPARE(rangeMin(w0), -116.0f);
        w0.setDbmRange(-100.0f, -20.0f);   // a live move is not an edit
        w0.saveSettings();
        w0.setCenterFrequency(k2m);
        QCOMPARE(rangeMax(w0), -48.0f);
        for (const QString& key : s.allKeys()) {
            QVERIFY2(!key.startsWith(u"DisplayGridMax"_s), qPrintable(key));
            QVERIFY2(!key.startsWith(u"DisplayGridMin"_s), qPrintable(key));
        }

        // An edit writes that band's pair and no other band's.
        model->setPerBandDbMin(Band::Band40m, -130);
        QCOMPARE(s.value(u"DisplayGridMin_40m"_s).toInt(), -130);
        QCOMPARE(s.value(u"DisplayGridMax_40m"_s).toInt(), -48);
        QVERIFY(!s.contains(u"DisplayGridMax_20m"_s));
        QVERIFY(!s.contains(u"DisplayGridMin_XVTR"_s));

        // A model of the class outside a window keeps the class default.
        PanadapterModel plain;
        QCOMPARE(plain.perBandGrid(Band::Band20m).dbMax, -40);
        QCOMPARE(plain.perBandGrid(Band::Band20m).dbMin, -140);
        QCOMPARE(plain.perBandGrid(Band::Band40m).dbMin, -130);
    }

    // An install from before the band store kept the range in the bare
    // keys: that pair is what every band with nothing stored shows, a
    // stored band still wins, and the bare keys are left as they were.
    void legacyBareRangeIsEveryUnsetBandsRange_storedBandWins()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayGridMax"_s, u"-30"_s);
        s.setValue(u"DisplayGridMin"_s, u"-110"_s);
        s.setValue(u"DisplayGridMax_40m"_s, -25);
        s.setValue(u"DisplayGridMin_40m"_s, -125);
        SpectrumWidget w0;
        SpectrumWidget w1;
        w0.setCenterFrequency(k20m);
        w1.setCenterFrequency(k40m);
        PanModels models;
        PanadapterModel* m0 = models.ensure(u"pan-0"_s, &w0);
        PanadapterModel* m1 = models.ensure(u"pan-1"_s, &w1);
        QCOMPARE(rangeMax(w0), -30.0f);
        QCOMPARE(rangeMin(w0), -110.0f);
        QCOMPARE(m0->dBmCeiling(), -30);
        QCOMPARE(m0->dBmFloor(), -110);
        QCOMPARE(m1->perBandGrid(Band::GEN).dbMax, -30);
        QCOMPARE(m1->perBandGrid(Band::Band2m).dbMin, -110);
        // The stored band wins, on both pans.
        QCOMPARE(rangeMax(w1), -25.0f);
        QCOMPARE(rangeMin(w1), -125.0f);
        QCOMPARE(m0->perBandGrid(Band::Band40m).dbMax, -25);
        w0.setCenterFrequency(k40m);
        QCOMPARE(rangeMax(w0), -25.0f);
        w0.setCenterFrequency(k2m);
        QCOMPARE(rangeMax(w0), -30.0f);
        QCOMPARE(rangeMin(w0), -110.0f);

        w0.saveSettings();
        w1.saveSettings();
        QCOMPARE(s.value(u"DisplayGridMax"_s).toString(), u"-30"_s);
        QCOMPARE(s.value(u"DisplayGridMin"_s).toString(), u"-110"_s);
        QVERIFY(!s.contains(u"DisplayGridMax_20m"_s));
        QVERIFY(!s.contains(u"DisplayGridMax_XVTR"_s));
        QVERIFY(!s.contains(u"DisplayGridMin_XVTR"_s));
    }

    // One bare key, or a pair that is no range, falls back per value.
    void legacyBareRange_oneKeyOrABadPair()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayGridMax"_s, u"-43.5"_s);   // the store is whole dB
        {
            SpectrumWidget w;
            w.setCenterFrequency(k20m);
            PanModels models;
            models.ensure(u"pan-0"_s, &w);
            QCOMPARE(rangeMax(w), -44.0f);
            QCOMPARE(rangeMin(w), -116.0f);
        }
        s.setValue(u"DisplayGridMin"_s, u"-10"_s);     // above the max
        {
            SpectrumWidget w;
            w.setCenterFrequency(k20m);
            PanModels models;
            models.ensure(u"pan-0"_s, &w);
            QCOMPARE(rangeMax(w), -48.0f);
            QCOMPARE(rangeMin(w), -116.0f);
        }
        s.setValue(u"DisplayGridMin"_s, u"junk"_s);
        {
            SpectrumWidget w;
            w.setCenterFrequency(k20m);
            PanModels models;
            models.ensure(u"pan-0"_s, &w);
            QCOMPARE(rangeMax(w), -44.0f);
            QCOMPARE(rangeMin(w), -116.0f);
        }
        QCOMPARE(s.value(u"DisplayGridMax"_s).toString(), u"-43.5"_s);
        QCOMPARE(s.value(u"DisplayGridMin"_s).toString(), u"junk"_s);
    }

    // 2 m shows the transverter slot, so a change to that slot's key
    // reaches a pan on 2 m.
    void panOnTwoMetresAppliesTheTransverterSlot()
    {
        SpectrumWidget w0;
        w0.setCenterFrequency(k2m);
        PanModels models;
        PanadapterModel* model = models.ensure(u"pan-0"_s, &w0);
        QCOMPARE(model->band(), Band::Band2m);
        AppSettings::instance().setValue(u"DisplayGridMax_XVTR"_s, -55);
        models.applyStationGridSetting(u"DisplayGridMax_XVTR"_s);
        QCOMPARE(rangeMax(w0), -55.0f);
    }

    // While keyed the live pair is the transmit grid: the band's range
    // waits in the receive store and shows on the return to receive.
    void rangeArrivingWhileKeyedGoesToTheReceiveStore()
    {
        SpectrumWidget w0;
        w0.setCenterFrequency(k40m);
        PanModels models;
        PanadapterModel* model = models.ensure(u"pan-0"_s, &w0);
        w0.setMoxOverlay(true);
        const float txMax = rangeMax(w0);
        const float txMin = rangeMin(w0);
        model->setPerBandDbMax(Band::Band40m, -25);
        model->setPerBandDbMin(Band::Band40m, -115);
        QCOMPARE(rangeMax(w0), txMax);
        QCOMPARE(rangeMin(w0), txMin);
        w0.setMoxOverlay(false);
        QCOMPARE(rangeMax(w0), -25.0f);
        QCOMPARE(rangeMin(w0), -115.0f);
    }

    // The per-band 3D Floor: recalled on arrival, stored under the pan's
    // band alone, followed by a second pan on that band.
    void threeDFloorIsRecalledAndStoredPerBand()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"Display3DFloorDepth_40m"_s, 12);
        SpectrumWidget w0;
        SpectrumWidget w1;
        w0.setCenterFrequency(k20m);
        w1.setCenterFrequency(k40m);
        PanModels models;
        models.ensure(u"pan-0"_s, &w0);
        models.ensure(u"pan-1"_s, &w1);
        QCOMPARE(w0.dssFloorDepth(), 6);    // 20 m: the default
        QCOMPARE(w1.dssFloorDepth(), 12);   // recalled on creation

        w0.setCenterFrequency(k40m);
        QCOMPARE(w0.dssFloorDepth(), 12);

        w0.setDssFloorDepth(15);
        QCOMPARE(s.value(u"Display3DFloorDepth_40m"_s).toInt(), 15);
        QVERIFY(!s.contains(u"Display3DFloorDepth_20m"_s));
        // The second pan on 40 m follows at once, and recalls it later.
        QCOMPARE(w1.dssFloorDepth(), 15);
        w1.setCenterFrequency(k20m);
        QCOMPARE(w1.dssFloorDepth(), 6);
        w1.setCenterFrequency(k40m);
        QCOMPARE(w1.dssFloorDepth(), 15);
    }

    // An old install's range keys: the widget and its model never read
    // them, and nothing rewrites or deletes them. The bare pair is read by
    // PanModels alone, as the range of a band with nothing stored; the
    // per-pan forms are not read at all. The per-band keys are untouched.
    void oldPerPanRangeKeysAreIgnored()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayGridMax"_s, u"-10"_s);
        s.setValue(u"DisplayGridMin"_s, u"-90"_s);
        s.setValue(u"DisplayGridMax_0"_s, u"-11"_s);
        s.setValue(u"DisplayGridMin_0"_s, u"-91"_s);
        s.setValue(u"DisplayGridMax_1"_s, u"-12"_s);
        s.setValue(u"DisplayGridMin_1"_s, u"-92"_s);
        s.setValue(u"DisplayGridMax_20m"_s, -33);

        SpectrumWidget w0;
        SpectrumWidget w1;
        w0.setPanIndex(0);
        w1.setPanIndex(1);
        w0.loadSettings();
        w1.loadSettings();
        // Without a model the widget shows its own default, not the keys.
        QCOMPARE(rangeMax(w0), -48.0f);
        QCOMPARE(rangeMin(w0), -116.0f);
        QCOMPARE(rangeMax(w1), -48.0f);
        QCOMPARE(rangeMin(w1), -116.0f);
        for (const SpectrumWidget* w : { &w0, &w1 }) {
            QVERIFY(!w->perPanDisplayKeyBases().contains(u"DisplayGridMax"_s));
            QVERIFY(!w->perPanDisplayKeyBases().contains(u"DisplayGridMin"_s));
        }

        w0.setCenterFrequency(k20m);
        w1.setCenterFrequency(k20m);
        PanModels models;
        models.ensure(u"pan-0"_s, &w0);
        models.ensure(u"pan-1"_s, &w1);
        QCOMPARE(rangeMax(w0), -33.0f);
        QCOMPARE(rangeMax(w1), -33.0f);
        // The half of 20 m with no key, and a band with none: the bare
        // pair, on pan 1 too (not its own DisplayGridMax_1 / Min_1).
        QCOMPARE(rangeMin(w0), -90.0f);
        QCOMPARE(rangeMin(w1), -90.0f);
        QCOMPARE(models.model(u"pan-1"_s)->perBandGrid(Band::Band40m).dbMax, -10);

        // A changed range, a save and a reset write and clear none of them.
        w1.setDbmRange(-100.0f, -20.0f);
        w0.saveSettings();
        w1.saveSettings();
        w1.resetPanDisplayOverrides();
        QCOMPARE(s.value(u"DisplayGridMax"_s).toString(), u"-10"_s);
        QCOMPARE(s.value(u"DisplayGridMin"_s).toString(), u"-90"_s);
        QCOMPARE(s.value(u"DisplayGridMax_0"_s).toString(), u"-11"_s);
        QCOMPARE(s.value(u"DisplayGridMin_0"_s).toString(), u"-91"_s);
        QCOMPARE(s.value(u"DisplayGridMax_1"_s).toString(), u"-12"_s);
        QCOMPARE(s.value(u"DisplayGridMin_1"_s).toString(), u"-92"_s);
        QCOMPARE(s.value(u"DisplayGridMax_20m"_s).toInt(), -33);
        QVERIFY(!s.contains(u"DisplayGridMin_20m"_s));
        // The reset leaves the live range where it was.
        QCOMPARE(rangeMax(w1), -20.0f);
        QCOMPARE(rangeMin(w1), -100.0f);
    }

    // MainWindow gives each of its pans a model, retires it with the pan,
    // passes a per-band value from RadioModel on, and leaves RadioModel's
    // own pan list empty (its readers stay dormant).
    void mainWindowOwnsAModelPerPan_radioModelListStaysEmpty()
    {
        Test::markAudioFirstRunDone();
        AppSettings::instance().setValue(u"PanLayoutId"_s, u"1"_s);
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        window.resize(1280, 800);
        window.show();
        RadioModel* radio = window.radioModel();
        QVERIFY(radio);
        QVERIFY(QMetaObject::invokeMethod(&window, "applyPanLayout",
                                          Q_ARG(QString, u"2v"_s)));
        PanadapterStack* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        SpectrumWidget* sw0 = stack->spectrum(u"pan-0"_s);
        SpectrumWidget* sw1 = stack->spectrum(u"pan-1"_s);
        QVERIFY(sw0);
        QVERIFY(sw1);

        PanModels* models = window.panModels();
        QVERIFY(models);
        PanadapterModel* m0 = models->model(u"pan-0"_s);
        PanadapterModel* m1 = models->model(u"pan-1"_s);
        QVERIFY(m0);
        QVERIFY(m1);
        QVERIFY(m0 != m1);
        QCOMPARE(models->all().size(), 2);
        QVERIFY(radio->panadapters().isEmpty());

        // Each pan follows its own centre and shows its band's range.
        sw0->setCenterFrequency(k40m);
        sw1->setCenterFrequency(k20m);
        QCOMPARE(m0->band(), Band::Band40m);
        QCOMPARE(m1->band(), Band::Band20m);
        QCOMPARE(rangeMax(*sw0), -48.0f);
        QCOMPARE(rangeMin(*sw0), -116.0f);
        QCOMPARE(m0->dBmCeiling(), -48);
        QCOMPARE(m0->dBmFloor(), -116);

        // A per-band value RadioModel reports reaches the window's pans.
        AppSettings::instance().setValue(u"DisplayGridMax_40m"_s, -30);
        radio->applyPanGridSetting(u"DisplayGridMax_40m"_s);
        QCOMPARE(rangeMax(*sw0), -30.0f);
        QCOMPARE(rangeMax(*sw1), -48.0f);

        // Back to one pan: the second pan's model goes with it.
        QVERIFY(QMetaObject::invokeMethod(&window, "applyPanLayout",
                                          Q_ARG(QString, u"1"_s)));
        QVERIFY(!models->model(u"pan-1"_s));
        QCOMPARE(models->model(u"pan-0"_s), m0);
        QVERIFY(radio->panadapters().isEmpty());
    }
};

QTEST_MAIN(TestPanModels)
#include "tst_pan_models.moc"
