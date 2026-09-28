// no-port-check: NereusSDR-original live-apply checks for described settings.
#include <QtTest>

#include "core/AppSettings.h"
#include "core/FreeDVReporterClient.h"
#include "core/settings/SettingsProxyServer.h"
#include "core/session/SessionCommandDispatcher.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class SetupDescriptionLiveTest : public QObject {
    Q_OBJECT
private slots:
    void twoTonePresetHonoursHolderAndOnAirGuards()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLiteRxOnly);
        SessionCommandDispatcher dispatcher(&model);
        QList<SessionMessage> results;
        connect(&dispatcher, &SessionCommandDispatcher::commandResultReady, this,
                [&results](const SessionMessage& result) { results.append(result); });
        bool anotherHolds = false;
        SessionCommandDispatcher::TransmitAccess access;
        access.transmitter = [&anotherHolds](const QByteArray&) -> TxRefusal {
            return anotherHolds ? TxRefusals::otherDeviceHolds(QStringLiteral("Another phone"))
                                : TxRefusal{};
        };
        dispatcher.setTransmitAccess(access);
        dispatcher.setRequester("phone");
        auto invoke = [&dispatcher, &results](const QString& name) {
            results.clear();
            dispatcher.dispatch(SessionMessages::commandInvoke("tx.twoTonePreset", 1,
                {MirrorUpdate{0, "name", MirrorWireKind::Utf8, name}}));
            return results.isEmpty() ? SessionMessage{} : results.last();
        };
        QVERIFY(invoke(QStringLiteral("stealth")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 70);
        QCOMPARE(model.transmitModel().twoToneFreq2(), 190);
        anotherHolds = true;
        QVERIFY(!invoke(QStringLiteral("defaults")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 70);
        anotherHolds = false;
        model.transmitModel().setMox(true);
        QVERIFY(!invoke(QStringLiteral("defaults")).accepted);
        model.transmitModel().setMox(false);
        model.transmitModel().setTune(true);
        QVERIFY(!invoke(QStringLiteral("defaults")).accepted);
        model.transmitModel().setTune(false);
        QVERIFY(!invoke(QStringLiteral("unknown")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 70);
        QVERIFY(invoke(QStringLiteral("defaults")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 700);
        QCOMPARE(model.transmitModel().twoToneFreq2(), 1900);
    }

    void twoTonePresetPublishesACompletePairBeforePropertySignals()
    {
        TransmitModel model;
        int otherFrequencyAtFirstSignal = 0;
        int combinedSignals = 0;
        connect(&model, &TransmitModel::twoToneFrequenciesChanged, this,
                [&combinedSignals](int, int) { ++combinedSignals; });
        connect(&model, &TransmitModel::twoToneFreq1Changed, this,
                [&model, &otherFrequencyAtFirstSignal](int) {
                    otherFrequencyAtFirstSignal = model.twoToneFreq2();
                });
        model.setTwoToneFrequencies(70, 190);
        QCOMPARE(model.twoToneFreq1(), 70);
        QCOMPARE(model.twoToneFreq2(), 190);
        QCOMPARE(otherFrequencyAtFirstSignal, 190);
        QCOMPARE(combinedSignals, 1);
    }

    void remoteIdentityWritesReachRunningReporter()
    {
        AppSettings& settings = AppSettings::instance();
        settings.clear();
        RadioModel model;
        SettingsProxyServer proxy(settings);
        auto* reporter = model.freeDvReporter();
        QVERIFY(reporter != nullptr);

        const auto call = proxy.applyInboundWrite(
            QStringLiteral("User/Callsign"), QStringLiteral("KG4VCF"), QStringLiteral("phone"));
        QVERIFY2(call.accepted, qPrintable(call.reason));
        model.applyRemoteFreedvSetting(QStringLiteral("User/Callsign"), settings);
        QCOMPARE(reporter->callsignForTest(), QStringLiteral("KG4VCF"));
        QCOMPARE(settings.value(QStringLiteral("PskReporter/Callsign")).toString(),
                 QStringLiteral("KG4VCF"));

        const auto grid = proxy.applyInboundWrite(
            QStringLiteral("User/GridSquare"), QStringLiteral("EM73"), QStringLiteral("phone"));
        QVERIFY2(grid.accepted, qPrintable(grid.reason));
        model.applyRemoteFreedvSetting(QStringLiteral("User/GridSquare"), settings);
        QCOMPARE(reporter->gridSquareForTest(), QStringLiteral("EM73"));
        QCOMPARE(settings.value(QStringLiteral("PskReporter/GridSquare")).toString(),
                 QStringLiteral("EM73"));
        settings.clear();
    }
};

QTEST_MAIN(SetupDescriptionLiveTest)
#include "tst_setup_description_live.moc"
