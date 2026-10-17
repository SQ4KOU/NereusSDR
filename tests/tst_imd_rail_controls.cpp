// no-port-check: NereusSDR-original IMD rail interaction regression.
#include <QtTest>

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/TwoToneController.h"
#include "gui/MainWindow.h"
#include "gui/ImdMeasurementPanel.h"
#include "gui/MoxDisplayController.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/PsForm.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

constexpr auto kMeasurementsKey = "puresignal/showTwoToneMeasurements";

void showDuplexRequired(PanadapterApplet& pan)
{
    SpectrumWidget* spectrum = pan.spectrumWidget();
    spectrum->setShowIMDMeasurements(true);
    spectrum->setMoxOverlay(true);
    spectrum->setTestingIMD(true);
    spectrum->setDisplayDuplex(false);
    QCOMPARE(spectrum->imdMeasurementFrame().status, ImdStatus::DuplexRequired);
}

QPushButton* button(PanadapterApplet& pan, const char* name)
{
    return pan.findChild<QPushButton*>(QString::fromLatin1(name));
}

} // namespace

class TstImdRailControls : public QObject {
    Q_OBJECT

    QTemporaryDir m_directory;
    std::unique_ptr<AppSettings> m_settings;

private slots:
    void initTestCase()
    {
        m_settings = std::make_unique<AppSettings>(
            m_directory.filePath(QStringLiteral("imd-rail-controls.settings")));
    }

    void railButtonsEmitPresentationIntentsAndKeepUnavailableReason()
    {
        PanadapterApplet pan(QStringLiteral("rail-controls"));
        showDuplexRequired(pan);
        pan.imdRail()->setDisplayDuplexCapability(true, {});
        pan.resize(736, 520);
        pan.show();
        QCoreApplication::processEvents();

        QPushButton* close = button(pan, "ImdHideMeasurements");
        QPushButton* enableDuplex = button(pan, "ImdEnableDuplex");
        QVERIFY(close != nullptr);
        QVERIFY(enableDuplex != nullptr);
        QVERIFY(close->isVisibleTo(&pan));
        QVERIFY(enableDuplex->isVisibleTo(&pan));

        QSignalSpy hideRequested(pan.imdRail(),
                                 &ImdMeasurementPanel::hideMeasurementsRequested);
        QSignalSpy duplexRequested(pan.imdRail(),
                                   &ImdMeasurementPanel::enableDisplayDuplexRequested);
        close->click();
        QCOMPARE(hideRequested.count(), 1);
        QCOMPARE(duplexRequested.count(), 0);

        QVERIFY(enableDuplex->isEnabled());
        QVERIFY(enableDuplex->isVisibleTo(&pan));
        const QString evidence = qEnvironmentVariable("NEREUS_IMD_RAIL_EVIDENCE");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(pan.imdRail()->grab().save(
                evidence + QStringLiteral("/imd-rail-dup-available.png")));
        }
        enableDuplex->click();
        QCOMPARE(duplexRequested.count(), 1);

        RadioModel remote(RadioModel::Role::Remote);
        remote.setStationTxDisplayVersion(2);
        const QString reason =
            MoxDisplayController::displayDuplexUnavailableReason(&remote);
        QVERIFY(!reason.isEmpty());
        pan.imdRail()->setDisplayDuplexCapability(false, reason);
        QVERIFY(!enableDuplex->isVisibleTo(&pan));
        QLabel* source = pan.findChild<QLabel*>(QStringLiteral("ImdSource"));
        QVERIFY(source != nullptr);
        QCOMPARE(source->text(), reason);
        QCOMPARE(source->toolTip(), reason);
        if (!evidence.isEmpty()) {
            QCoreApplication::processEvents();
            QVERIFY(pan.imdRail()->grab().save(
                evidence + QStringLiteral("/imd-rail-dup-unavailable.png")));
        }
    }

    void closePersistsAndSynchronizesBenchAndEveryPan()
    {
        AppSettings::instance().setValue(QLatin1String(kMeasurementsKey),
                                         QStringLiteral("False"));
        MainWindow window(RemoteStationOptions{}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        QVERIFY(QMetaObject::invokeMethod(&window, "openPureSignalDialog",
                                          Qt::DirectConnection));

        PsForm* bench = window.findChild<PsForm*>();
        MoxDisplayController* controller =
            window.findChild<MoxDisplayController*>();
        RadioModel* radio = window.findChild<RadioModel*>();
        PanadapterStack* stack = window.findChild<PanadapterStack*>();
        PanadapterApplet* pan = window.findChild<PanadapterApplet*>();
        QVERIFY(bench != nullptr);
        QVERIFY(controller != nullptr);
        QVERIFY(radio != nullptr);
        QVERIFY(stack != nullptr);
        QVERIFY(pan != nullptr);
        PanadapterApplet* second = stack->addPanadapter(QStringLiteral("imd-rail-second"));
        QVERIFY(second != nullptr);
        QSignalSpy mox(radio->moxController(), &MoxController::moxStateChanged);
        QSignalSpy twoTone(radio->twoToneController(),
                           &TwoToneController::twoToneActiveChanged);

        QCheckBox* showMeasurements =
            bench->findChild<QCheckBox*>(QStringLiteral("chkShow2ToneMeasurements"));
        QVERIFY(showMeasurements != nullptr);
        showMeasurements->setChecked(true);
        QVERIFY(controller->showIMDMeasurements());
        QCOMPARE(AppSettings::instance().value(QLatin1String(kMeasurementsKey)).toString(),
                 QStringLiteral("True"));

        QPushButton* close = button(*second, "ImdHideMeasurements");
        QVERIFY(close != nullptr);
        QVERIFY(close->isVisibleTo(second));
        close->click();

        QVERIFY(!controller->showIMDMeasurements());
        QVERIFY(!showMeasurements->isChecked());
        QCOMPARE(AppSettings::instance().value(QLatin1String(kMeasurementsKey)).toString(),
                 QStringLiteral("False"));
        const QList<PanadapterApplet*> pans =
            window.findChildren<PanadapterApplet*>();
        QVERIFY(!pans.isEmpty());
        for (PanadapterApplet* candidate : pans) {
            QVERIFY(!candidate->spectrumWidget()->imdMeasurementFrame()
                         .context.measurementsEnabled);
            QVERIFY(!candidate->imdRail()->isVisible());
        }
        QCOMPARE(mox.count(), 0);
        QCOMPARE(twoTone.count(), 0);
    }

    void enableDuplexUsesWindowSettingAndExistingAction()
    {
        AppSettings::instance().setValue(QStringLiteral("DisplayDuplex"),
                                         QStringLiteral("False"));
        MainWindow window(RemoteStationOptions{}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        MoxDisplayController* controller =
            window.findChild<MoxDisplayController*>();
        RadioModel* radio = window.findChild<RadioModel*>();
        PanadapterApplet* pan = window.findChild<PanadapterApplet*>();
        QAction* action =
            window.findChild<QAction*>(QStringLiteral("actionDisplayDuplex"));
        QVERIFY(controller != nullptr);
        QVERIFY(radio != nullptr);
        QVERIFY(pan != nullptr);
        QVERIFY(action != nullptr);
        QVERIFY(action->isEnabled());
        QVERIFY(!action->isChecked());
        QSignalSpy mox(radio->moxController(), &MoxController::moxStateChanged);
        QSignalSpy twoTone(radio->twoToneController(),
                           &TwoToneController::twoToneActiveChanged);

        showDuplexRequired(*pan);
        QPushButton* enableDuplex = button(*pan, "ImdEnableDuplex");
        QVERIFY(enableDuplex != nullptr);
        QVERIFY(enableDuplex->isEnabled());
        enableDuplex->click();

        QVERIFY(action->isChecked());
        QVERIFY(controller->displayDuplex());
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayDuplex")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(mox.count(), 0);
        QCOMPARE(twoTone.count(), 0);
    }
};

QTEST_MAIN(TstImdRailControls)
#include "tst_imd_rail_controls.moc"
