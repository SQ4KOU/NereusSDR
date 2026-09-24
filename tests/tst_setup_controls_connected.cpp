// =================================================================
// tests/tst_setup_controls_connected.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test. It drives real Setup pages
// and a real MainWindow; no upstream logic is ported here.
//
// R3 controls that work, Task 2 (R-R3-21): Setup controls that front an
// existing setting or slice control now reach it: Startup & Preferences
// auto-connect, callsign and grid; DSP > NR/ANF Enable ANF, DSP > CW peak
// filter, DSP > AM/SAM and FM squelch; Appearance > Meter Styles S-meter
// group; Diagnostics > Logs; Audio > VAX level readout.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  R3 controls that work, Task 2.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>

#include "OperatorWording.h"
#include "core/AppSettings.h"
#include "core/BuildIdentity.h"
#include "core/RadioDiscovery.h"
#include "gui/GuiSessionCoordinator.h"
#include "gui/MainWindow.h"
#include "gui/SMeterWidget.h"
#include "gui/SetupDialog.h"
#include "gui/SupportDialog.h"
#include "gui/diagnostics/DiagnosticsPhaseHPages.h"
#include "gui/setup/AppearanceSetupPages.h"
#include "gui/setup/AudioVaxPage.h"
#include "gui/setup/DspSetupPages.h"
#include "gui/setup/GeneralSetupPages.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

SliceModel* ensureSlice(RadioModel& model)
{
    if (model.activeSlice() == nullptr) {
        model.addSlice();
        model.setActiveSlice(0);
    }
    return model.activeSlice();
}

QAction* actionByText(const QObject* root, const QString& text)
{
    for (QAction* action : root->findChildren<QAction*>()) {
        if (action->text() == text) { return action; }
    }
    return nullptr;
}

RadioInfo savedRadioInfo()
{
    RadioInfo info;
    info.macAddress = QStringLiteral("00:1c:c0:a2:13:dd");
    info.address = QHostAddress(QStringLiteral("192.168.1.20"));
    info.boardType = HPSDRHW::OrionMKII;
    info.protocol = ProtocolVersion::Protocol2;
    return info;
}

} // namespace

class TstSetupControlsConnected : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("setup-controls-connected-%1")
                                            .arg(QCoreApplication::applicationPid()));
    }

    void init()
    {
        QVERIFY(!AppSettings::instance().remoteBackend());
        AppSettings::instance().clear();
        AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"),
                                         QStringLiteral("True"));
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery discovery;
        discovery.holdOffScans(std::chrono::minutes{5});
        BuildIdentity::setBuildTag(QString());
    }

    void cleanup()
    {
        RadioDiscovery::clearHoldOffForTest();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // Auto-connect to last radio is that radio's own auto-connect choice,
    // the one the Connection panel sets.
    void autoConnectSetsTheLastRadiosChoice()
    {
        auto& s = AppSettings::instance();
        const RadioInfo info = savedRadioInfo();
        s.saveRadio(info, /*pinToMac=*/true, /*autoConnect=*/false);
        s.setLastConnected(info.macAddress);

        StartupPrefsPage page(nullptr);
        auto* toggle = page.findChild<QPushButton*>(QStringLiteral("startupAutoConnect"));
        QVERIFY2(toggle != nullptr, "no auto-connect toggle");
        QVERIFY2(toggle->isEnabled(), "the auto-connect toggle is greyed");
        QVERIFY(!toggle->isChecked());
        toggle->setChecked(true);
        const auto saved = s.savedRadio(info.macAddress);
        QVERIFY(saved.has_value());
        QVERIFY(saved->autoConnect);
        QVERIFY(saved->pinToMac);  // the rest of the entry is kept
    }

    // With no radio to connect to, it says why it is off.
    void autoConnectWithoutARadioSaysWhy()
    {
        StartupPrefsPage page(nullptr);
        auto* toggle = page.findChild<QPushButton*>(QStringLiteral("startupAutoConnect"));
        QVERIFY(toggle != nullptr);
        QVERIFY(!toggle->isEnabled());
        QVERIFY(OperatorWording::isPlain(toggle->toolTip()));
        QVERIFY(!toggle->toolTip().contains(QStringLiteral("NYI")));
    }

    // Callsign and grid are the operator identity the Spot Hub edits.
    void callsignAndGridAreTheSpotIdentity()
    {
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("User/Callsign"), QStringLiteral("N0CALL"));
        s.setValue(QStringLiteral("User/GridSquare"), QStringLiteral("FN31"));
        RadioModel model;
        StartupPrefsPage page(&model);
        auto* call = page.findChild<QLineEdit*>(QStringLiteral("startupCallsign"));
        auto* grid = page.findChild<QLineEdit*>(QStringLiteral("startupGridSquare"));
        auto* error = page.findChild<QLabel*>(QStringLiteral("startupIdentityError"));
        QVERIFY2(call && grid && error, "no identity fields");
        QVERIFY(call->isEnabled());
        QVERIFY(grid->isEnabled());
        QCOMPARE(call->text(), QStringLiteral("N0CALL"));
        QCOMPARE(grid->text(), QStringLiteral("FN31"));

        grid->setText(QStringLiteral("EM7"));
        emit grid->editingFinished();
        QVERIFY(!error->isHidden());
        QCOMPARE(s.value(QStringLiteral("User/GridSquare")).toString(), QStringLiteral("FN31"));

        call->setText(QStringLiteral("kg4vcf"));
        grid->setText(QStringLiteral("em73"));
        emit call->editingFinished();
        QVERIFY(error->isHidden());
        QCOMPARE(s.value(QStringLiteral("User/Callsign")).toString(), QStringLiteral("KG4VCF"));
        QCOMPARE(s.value(QStringLiteral("User/GridSquare")).toString(), QStringLiteral("EM73"));
        // The spot sources' own copies follow, as the Spot Hub saves them.
        QCOMPARE(s.value(QStringLiteral("PskReporter/Callsign")).toString(), QStringLiteral("KG4VCF"));
        QCOMPARE(s.value(QStringLiteral("DxClusterCallsign")).toString(), QStringLiteral("KG4VCF"));
        QCOMPARE(s.value(QStringLiteral("FreeDvReporter/GridSquare")).toString(),
                 QStringLiteral("EM73"));
    }

    // DSP > NR/ANF > ANF: Enable ANF is the slice's ANF, both ways.
    void enableAnfIsTheSlicesAnf()
    {
        RadioModel model;
        SliceModel* slice = ensureSlice(model);
        QVERIFY(slice != nullptr);
        NrAnfSetupPage page(&model);
        auto* anf = page.findChild<QCheckBox*>(QStringLiteral("anfEnableCheck"));
        QVERIFY2(anf != nullptr, "no Enable ANF control");
        QVERIFY2(anf->isEnabled(), "Enable ANF is greyed");
        anf->setChecked(true);
        QVERIFY(slice->anfEnabled());
        slice->setAnfEnabled(false);
        QVERIFY(!anf->isChecked());
    }

    // DSP > CW > APF: Enable and Center Freq are the slice's APF and its
    // tune offset from the 600 Hz pitch.
    void cwPeakFilterIsTheSlicesApf()
    {
        RadioModel model;
        SliceModel* slice = ensureSlice(model);
        CwSetupPage page(&model);
        auto* enable = page.findChild<QPushButton*>(QStringLiteral("apfEnableButton"));
        auto* center = page.findChild<QSlider*>(QStringLiteral("apfCenterSlider"));
        QVERIFY2(enable && center, "no APF controls");
        QVERIFY2(enable->isEnabled() && enable->isEnabledTo(&page), "APF Enable is greyed");
        QVERIFY2(center->isEnabledTo(&page), "APF Center Freq is greyed");
        enable->click();
        QVERIFY(slice->apfEnabled());
        center->setValue(700);
        QCOMPARE(slice->apfTuneHz(), 100);
        slice->setApfTuneHz(-50);
        QCOMPARE(center->value(), 550);
    }

    // DSP > AM/SAM and DSP > FM: the squelch thresholds are the slice's.
    void amAndFmSquelchAreTheSlicesSquelch()
    {
        RadioModel model;
        SliceModel* slice = ensureSlice(model);
        {
            AmSamSetupPage page(&model);
            auto* am = page.findChild<QSlider*>(QStringLiteral("amSquelchThresholdSlider"));
            QVERIFY2(am != nullptr, "no AM squelch control");
            QVERIFY2(am->isEnabledTo(&page), "AM squelch is greyed");
            am->setValue(-80);
            QCOMPARE(slice->amsqThresh(), -80.0);
            slice->setAmsqThresh(-100.0);
            QCOMPARE(am->value(), -100);
        }
        {
            FmSetupPage page(&model);
            auto* fm = page.findChild<QSlider*>(QStringLiteral("fmSquelchThresholdSlider"));
            QVERIFY2(fm != nullptr, "no FM squelch control");
            QVERIFY2(fm->isEnabledTo(&page), "FM squelch is greyed");
            fm->setValue(-70);
            QCOMPARE(slice->fmsqThresh(), -70.0);
        }
    }

    // Appearance > Meter Styles: the S-meter group is the S-meter's own
    // face, peak hold and decay, saved under its keys and applied to the
    // S-meter on screen.
    void meterStylesSetTheSMeter()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        auto* sMeter = window->findChild<SMeterWidget*>();
        QVERIFY(sMeter != nullptr);
        QAction* settings = actionByText(window, QStringLiteral("&Settings..."));
        QVERIFY(settings != nullptr);
        settings->trigger();
        auto* dialog = window->findChild<SetupDialog*>();
        QVERIFY(dialog != nullptr);
        dialog->selectPage(QStringLiteral("Meter Styles"));

        auto* face = dialog->findChild<QComboBox*>(QStringLiteral("sMeterFaceCombo"));
        auto* peak = dialog->findChild<QCheckBox*>(QStringLiteral("sMeterPeakHoldCheck"));
        auto* decay = dialog->findChild<QComboBox*>(QStringLiteral("sMeterDecayCombo"));
        QVERIFY2(face && peak && decay, "no S-meter controls");
        QVERIFY(face->isEnabled() && peak->isEnabled() && decay->isEnabled());

        const int classic = static_cast<int>(SMeterWidget::FaceStyle::Classic);
        face->setCurrentIndex(face->findData(classic));
        QCOMPARE(static_cast<int>(sMeter->faceStyle()), classic);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("SMeter_FaceStyle")).toString(),
                 SMeterWidget::faceStyleKey(SMeterWidget::FaceStyle::Classic));

        peak->setChecked(!peak->isChecked());
        QCOMPARE(AppSettings::instance().value(QStringLiteral("PeakHoldEnabled")).toString(),
                 peak->isChecked() ? QStringLiteral("True") : QStringLiteral("False"));

        decay->setCurrentIndex(decay->findData(QStringLiteral("Slow")));
        QCOMPARE(AppSettings::instance().value(QStringLiteral("PeakDecayRate")).toString(),
                 QStringLiteral("Slow"));
        delete dialog;
        QVERIFY(sessions.replace({}, false));
    }

    // Diagnostics > Logs shows the log file, as Help > Support does.
    void logsPageShowsTheLogFile()
    {
        LogsPage page;
        auto* view = page.findChild<QPlainTextEdit*>();
        QVERIFY(view != nullptr);
        QVERIFY2(view->isEnabled(), "the log view is greyed");
        QVERIFY(!view->toolTip().contains(QStringLiteral("NYI")));
        QCOMPARE(view->toPlainText(), SupportDialog::logTailText());
    }

    // Audio > VAX: each card's level shows the channel's audio level.
    void vaxCardShowsTheChannelLevel()
    {
        VaxChannelCard card(1);
        card.setLevel(0.5f);
        QVERIFY(qAbs(card.levelDbForTest() - (-6.0206)) < 0.01);
        card.setLevel(0.0f);
        QCOMPARE(card.levelDbForTest(), -60.0);
        auto* gauge = card.findChild<QWidget*>(QStringLiteral("vaxLevelGauge"));
        QVERIFY(gauge != nullptr);
        QVERIFY(OperatorWording::isPlain(gauge->toolTip()));
    }

    // The words the connected Setup controls show are plain.
    void connectedSetupControlsUsePlainWords()
    {
        RadioModel model;
        ensureSlice(model);
        QStringList texts;
        {
            StartupPrefsPage page(&model);
            for (const char* name : {"startupAutoConnect", "startupCallsign", "startupGridSquare"}) {
                auto* w = page.findChild<QWidget*>(QLatin1String(name));
                QVERIFY(w != nullptr);
                texts << w->toolTip();
            }
        }
        {
            NrAnfSetupPage page(&model);
            texts << page.findChild<QCheckBox*>(QStringLiteral("anfEnableCheck"))->toolTip();
        }
        {
            MeterStylesPage page(&model);
            for (const char* name : {"sMeterFaceCombo", "sMeterPeakHoldCheck", "sMeterDecayCombo"}) {
                auto* w = page.findChild<QWidget*>(QLatin1String(name));
                QVERIFY(w != nullptr);
                texts << w->toolTip();
            }
        }
        {
            LogsPage page;
            for (QWidget* w : page.findChildren<QWidget*>()) {
                if (!w->toolTip().isEmpty()) { texts << w->toolTip(); }
            }
        }
        for (const QString& text : std::as_const(texts)) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
            QVERIFY2(!text.contains(QStringLiteral("NYI")), qPrintable(text));
            QVERIFY2(!text.contains(QChar(0x2014)), qPrintable(text));
        }
    }
};

QTEST_MAIN(TstSetupControlsConnected)
#include "tst_setup_controls_connected.moc"
