#include <QtTest/QtTest>
#include "core/audio/PortAudioBus.h"
#include <portaudio.h>

using namespace NereusSDR;

class TstPortAudioBus : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Pa_Initialize();
    }
    void cleanupTestCase() {
        Pa_Terminate();
    }

    void constructsClosed() {
        PortAudioBus bus;
        QVERIFY(!bus.isOpen());
    }

    void openSucceedsOnDefaultDevice() {
        PortAudioBus bus;
        AudioFormat f;
        QVERIFY2(bus.open(f), qPrintable(bus.errorString()));
        QVERIFY(bus.isOpen());
        bus.close();
        QVERIFY(!bus.isOpen());
    }

    void negotiatedFormatReflectsDevice() {
        PortAudioBus bus;
        AudioFormat f;
        f.sampleRate = 48000;
        f.channels = 2;
        bus.open(f);
        QVERIFY(bus.negotiatedFormat().sampleRate > 0);
        bus.close();
    }

    void backendNameIdentifiesAPI() {
        PortAudioBus bus;
        bus.open(AudioFormat{});
        const QString n = bus.backendName();
        QVERIFY(!n.isEmpty());
        bus.close();
    }

    void hostApisEnumerateNonEmpty() {
        const auto apis = PortAudioBus::hostApis();
        QVERIFY(!apis.isEmpty());
    }

    void outputDevicesEnumerateForFirstApi() {
        const auto apis = PortAudioBus::hostApis();
        QVERIFY(!apis.isEmpty());
        const auto devices = PortAudioBus::outputDevicesFor(apis.first().index);
        // Host system should have at least one output; skip if headless CI.
        if (devices.isEmpty()) { QSKIP("No output devices on test host"); }
    }

    void openInputSucceedsOnDefaultDevice() {
        PortAudioBus bus;
        PortAudioConfig cfg;
        cfg.direction = AudioDirection::Input;
        bus.setConfig(cfg);
        AudioFormat f;
        if (!bus.open(f)) {
            QSKIP(qPrintable(QStringLiteral("No default input device: ") + bus.errorString()));
        }
        QVERIFY(bus.isOpen());
        QVERIFY(bus.negotiatedFormat().sampleRate > 0);
        QVERIFY(!bus.backendName().isEmpty());
        bus.close();
        QVERIFY(!bus.isOpen());
    }
};

QTEST_APPLESS_MAIN(TstPortAudioBus)
#include "tst_port_audio_bus.moc"
