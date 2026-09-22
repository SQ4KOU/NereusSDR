// no-port-check: NereusSDR-original offline receive-layout lifecycle tests.
#include <QtTest>
#include <QFile>

#include "core/AppSettings.h"
#include "core/ReceiveLayoutStore.h"
#include "core/WdspEngine.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {
const QString kMac = QStringLiteral("AA:BB:CC:DD:EE:01");

ReceiveLayoutStore::LoadResult savedLayout()
{
    ReceiveLayoutStore::LoadResult layout;
    layout.state = ReceiveLayoutStore::LoadState::Loaded;
    layout.slices = {{2, QStringLiteral("pan-1"), 7200000.0, DSPMode::RADE_L},
                     {0, QStringLiteral("pan-0"), 14293200.0, DSPMode::USB}};
    layout.radeRxOwnerId = 2;
    return layout;
}

QByteArray diskSettings()
{
    QFile file(AppSettings::instance().filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
}

class TstReceiveLayoutHydration : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("receive-layout-test-%1")
                                       .arg(QCoreApplication::applicationPid()));
    }
    void cleanupTestCase() { QFile::remove(AppSettings::instance().filePath()); }
    void init() { AppSettings::instance().clear(); }

    void restoresActualSavedIdentitiesBeforePublishingNewSlices()
    {
        auto& settings = AppSettings::instance();
        const auto layout = savedLayout();
        QVERIFY(ReceiveLayoutStore::stage(settings, kMac, layout.slices,
                                          nullptr, layout.radeRxOwnerId));
        QVERIFY(settings.save());
        settings.clear();
        settings.load();
        const auto loaded = ReceiveLayoutStore::load(settings, kMac);

        RadioModel radio;
        bool sawSettledB = false;
        connect(&radio, &RadioModel::sliceAdded, &radio, [&](int id) {
            SliceModel* slice = radio.sliceById(id);
            if (id == 2) {
                sawSettledB = slice && slice->frequency() == 7200000.0
                    && slice->dspMode() == DSPMode::RADE_L
                    && slice->panKey() == QLatin1String("pan-1")
                    && slice->settingsRadioIdentity() == AppSettings::normalizedRadioMac(kMac);
            }
        });
        QString error;
        QVERIFY2(radio.hydrateReceiveLayout(kMac, loaded, &error), qPrintable(error));
        QVERIFY(sawSettledB);
        QCOMPARE(radio.slices().size(), 2);
        QCOMPARE(radio.slices().at(0)->sliceIndex(), 2);
        QCOMPARE(radio.slices().at(1)->sliceIndex(), 0);
        QVERIFY(radio.receiveLayoutPendingAdmission());
        QCOMPARE(radio.restoredRadeReceiveOwner(), std::optional<int>(2));
        for (int id : {0, 2}) {
            QCOMPARE(radio.sliceById(id)->streamIndex(), -1);
            QVERIFY(!radio.wdspEngine()->rxChannel(id));
            QVERIFY(!radio.wdspEngine()->radeChannel(id));
        }
    }

    void reconcilesSharedIdsWithoutSavingBootstrapOrRetiredSlices()
    {
        auto& settings = AppSettings::instance();
        settings.setValue("Slice0/Locked", QStringLiteral("True"));
        settings.setValue("Slice0/AfGain", 37);
        QVERIFY(settings.save());
        const QByteArray before = diskSettings();
        QVERIFY(!before.isEmpty());
        RadioModel radio;
        QCOMPARE(radio.addSlice(), 0);
        QCOMPARE(radio.addSlice(), 1);
        SliceModel* originalA = radio.sliceById(0);
        originalA->setFrequency(3500000.0); // creates a pending legacy save
        const auto memoryBefore = settings.snapshot({QString()});
        QSignalSpy added(&radio, &RadioModel::sliceAdded);
        QSignalSpy removed(&radio, &RadioModel::sliceRemoved);
        QSignalSpy hydrated(&radio, &RadioModel::receiveLayoutHydrated);
        QSignalSpy activePosition(&radio, &RadioModel::activeSliceChanged);
        QSignalSpy activeId(&radio, &RadioModel::activeSliceIdChanged);
        // Reentrant consumers must not flush a half-restored layout.
        connect(&radio, &RadioModel::sliceRemoved, &radio,
                [&radio] { radio.flushPendingSettingsSave(); });
        QString error;
        QVERIFY2(radio.hydrateReceiveLayout(kMac, savedLayout(), &error), qPrintable(error));
        QCOMPARE(radio.sliceById(0), originalA);
        QCOMPARE(originalA->frequency(), 14293200.0);
        QCOMPARE(originalA->afGain(), 37);
        QVERIFY(!radio.sliceById(1));
        QCOMPARE(added.size(), 1);
        QCOMPARE(added.first().first().toInt(), 2);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.first().first().toInt(), 1);
        QCOMPARE(hydrated.size(), 1);
        QCOMPARE(activePosition.last().first().toInt(), 1);
        QCOMPARE(activeId.last().first().toInt(), 0);
        radio.flushPendingSettingsSave();
        QCOMPARE(diskSettings(), before);
        QCOMPARE(settings.snapshot({QString()}), memoryBefore);
        QVERIFY(!settings.contains("Slice1/LastBand"));
    }

    void nonzeroOnlyMembershipPreservesLastSliceInvariant()
    {
        RadioModel radio;
        QCOMPARE(radio.addSlice(), 0);
        auto layout = savedLayout();
        layout.slices.removeLast();
        QVERIFY(radio.hydrateReceiveLayout(kMac, layout));
        QCOMPARE(radio.slices().size(), 1);
        QVERIFY(!radio.sliceById(0));
        QCOMPARE(radio.activeSlice(), radio.sliceById(2));
        QVERIFY(!radio.wdspEngine()->rxChannel(0));
        radio.removeSlice(2);
        QCOMPARE(radio.slices().size(), 1);
    }

    void refusesInvalidLayoutAtomically()
    {
        RadioModel radio;
        QCOMPARE(radio.addSlice(), 0);
        SliceModel* original = radio.sliceById(0);
        const double frequency = original->frequency();
        QSignalSpy added(&radio, &RadioModel::sliceAdded);
        QSignalSpy removed(&radio, &RadioModel::sliceRemoved);
        auto layout = savedLayout();
        layout.slices[1].id = 2;
        QString error;
        QVERIFY(!radio.hydrateReceiveLayout(kMac, layout, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(radio.sliceById(0), original);
        QCOMPARE(original->frequency(), frequency);
        QCOMPARE(added.size(), 0);
        QCOMPARE(removed.size(), 0);
        QVERIFY(!radio.receiveLayoutPendingAdmission());
        QVERIFY(!radio.hydrateReceiveLayout("bad mac", savedLayout(), &error));
        layout = savedLayout();
        layout.state = ReceiveLayoutStore::LoadState::Missing;
        QVERIFY(!radio.hydrateReceiveLayout(kMac, layout, &error));
    }

    void remoteModelsCannotHydrateLocalSettings()
    {
        RadioModel radio(RadioModel::Role::Remote);
        QString error;
        QVERIFY(!radio.hydrateReceiveLayout(kMac, savedLayout(), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(radio.slices().isEmpty());
    }

    void restoresExplicitSecondRadeOwnerWithoutStartingEitherDecoder()
    {
        auto layout = savedLayout();
        layout.slices = {{2, QStringLiteral("pan-0"), 7200000.0, DSPMode::RADE_L},
                         {4, QStringLiteral("pan-1"), 14293200.0, DSPMode::RADE_U}};
        layout.radeRxOwnerId = 4;
        RadioModel radio;
        QVERIFY(radio.hydrateReceiveLayout(kMac, layout));
        QCOMPARE(radio.restoredRadeReceiveOwner(), std::optional<int>(4));
        QCOMPARE(radio.activeSlice()->sliceIndex(), 2);
        QVERIFY(!radio.wdspEngine()->radeChannel(2));
        QVERIFY(!radio.wdspEngine()->radeChannel(4));
    }

    void refusesAlreadyBoundBootstrapWithoutChangingItsPlacement()
    {
        RadioModel radio;
        radio.configureStreamPool(2, 5, 192000);
        QCOMPARE(radio.addSlice(), 0);
        SliceModel* slice = radio.sliceById(0);
        QVERIFY(slice->streamIndex() >= 0);
        const int stream = slice->streamIndex();
        const double frequency = slice->frequency();
        QString error;
        QVERIFY(!radio.hydrateReceiveLayout(kMac, savedLayout(), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(slice->streamIndex(), stream);
        QCOMPARE(slice->frequency(), frequency);
        QVERIFY(!radio.receiveLayoutPendingAdmission());
    }
};

QTEST_MAIN(TstReceiveLayoutHydration)
#include "tst_receive_layout_hydration.moc"
