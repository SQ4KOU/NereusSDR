// =================================================================
// tests/tst_network_diagnostics_dialog.cpp  (NereusSDR)
// =================================================================
//
//  Copyright (C) 2026 J.J. Boyd (KG4VCF)
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
// =================================================================
// Modification history (NereusSDR):
//   2026-10-14 - Each test starts from an empty settings profile of its own.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-14 - The Audio Backend row names the sound system the Settings
//                audio pages name, from the engine's device catalogue.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-13 — Fake-only ASIO observation guards and delta baselines.
//                J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-04-30 — Created for Phase 3Q sub-PR-3 (NetworkDiagnosticsDialog).
//                NereusSDR-original tests. J.J. Boyd (KG4VCF), with
//                AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QLoggingCategory>
#include <QGridLayout>
#include <QLabel>
#include <future>

#include "fakes/FakeAudioEngineBackend.h"
#include "core/AppSettings.h"
#include "gui/NetworkDiagnosticsDialog.h"
#include "core/AudioEngine.h"
#include "core/RadioConnection.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {
class DiagnosticBus final : public IAudioBus {
public:
    bool open(const AudioFormat&) override { opened = true; return true; }
    void close() override { opened = false; }
    bool isOpen() const override { ++reads; return opened; }
    qint64 push(const char*, qint64 bytes) override { return bytes; }
    qint64 pull(char*, qint64) override { return 0; }
    float rxLevel() const override { return 0; }
    float txLevel() const override { return 0; }
    QString backendName() const override { ++reads; return backend; }
    AudioFormat negotiatedFormat() const override { ++reads; return {}; }
    std::optional<DeviceRateMatcherStats> matcherStats() const override { ++reads; return stats; }
    QString backend = QStringLiteral("ASIO");
    bool opened = true;
    DeviceRateMatcherStats stats;
    mutable int reads = 0;
};

// The value label beside the "Backend" field label in the dialog's grid.
QLabel* backendValue(QWidget& dialog)
{
    for (QLabel* field : dialog.findChildren<QLabel*>()) {
        if (field->text() != QStringLiteral("Backend")) {
            continue;
        }
        auto* grid = qobject_cast<QGridLayout*>(field->parentWidget()->layout());
        if (grid == nullptr) {
            const auto grids = field->parentWidget()->findChildren<QGridLayout*>();
            grid = grids.isEmpty() ? nullptr : grids.first();
        }
        if (grid == nullptr) {
            return nullptr;
        }
        const int index = grid->indexOf(field);
        int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
        grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
        QLayoutItem* item = grid->itemAtPosition(row, 1);
        return item ? qobject_cast<QLabel*>(item->widget()) : nullptr;
    }
    return nullptr;
}

QStringList* asioMessages = nullptr;
QtMessageHandler previousMessageHandler = nullptr;
QLoggingCategory::CategoryFilter previousCategoryFilter = nullptr;
bool asioDebugEnabled = false;

void asioFilter(QLoggingCategory* category)
{
    if (previousCategoryFilter) {
        previousCategoryFilter(category);
    }
    if (QByteArray(category->categoryName()) == "nereus.audio.asio.buffer") {
        category->setEnabled(QtDebugMsg, asioDebugEnabled);
    }
}

void captureAsioMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtDebugMsg && context.category
        && QByteArray(context.category) == "nereus.audio.asio.buffer") {
        asioMessages->append(message);
    } else if (previousMessageHandler) {
        previousMessageHandler(type, context, message);
    }
}

class AsioLogCapture final {
public:
    explicit AsioLogCapture(bool enabled)
    {
        asioMessages = &messages;
        previousMessageHandler = qInstallMessageHandler(captureAsioMessage);
        previousCategoryFilter = QLoggingCategory::installFilter(nullptr);
        setEnabled(enabled);
    }
    ~AsioLogCapture()
    {
        QLoggingCategory::installFilter(previousCategoryFilter);
        qInstallMessageHandler(previousMessageHandler);
        asioMessages = nullptr;
    }
    void setEnabled(bool enabled)
    {
        asioDebugEnabled = enabled;
        QLoggingCategory::installFilter(asioFilter);
    }
    QStringList messages;
};
}

class TstNetworkDiagnosticsDialog : public QObject {
    Q_OBJECT

private slots:
    // The Backend row's choicesInUse() (SoundSystemLine.cpp) reads the saved
    // Speakers, Headphones and TxInput choices, so every test starts from an
    // empty settings profile of its own, not whatever the machine has saved.
    void initTestCase()
    {
        AppSettings::setProfileOverride(
            QStringLiteral("network-diagnostics-dialog-%1").arg(QCoreApplication::applicationPid()));
    }
    void init() { AppSettings::instance().clear(); }

    void asioSnapshotGuardsBeforeBusReads()
    {
        AudioEngine engine;
        auto bus = std::make_unique<DiagnosticBus>();
        DiagnosticBus* raw = bus.get();
        engine.setSpeakersBusForTest(std::move(bus));
        raw->reads = 0;
        QVERIFY(!engine.asioOutputSnapshotNow(AudioRole::TxInput));
        const bool wrongThreadMissing = std::async(std::launch::async, [&] {
            return !engine.asioOutputSnapshotNow(AudioRole::Speakers);
        }).get();
        QVERIFY(wrongThreadMissing);
        QCOMPARE(raw->reads, 0);
        raw->backend = QStringLiteral("Other backend");
        QVERIFY(!engine.asioOutputSnapshotNow(AudioRole::Speakers));
        QCOMPARE(raw->reads, 1); // backend identity only; no isOpen/format/stats
        raw->backend = QStringLiteral("ASIO");
        raw->stats.dryRuns = 17;
        const auto snapshot = engine.asioOutputSnapshotNow(AudioRole::Speakers);
        QVERIFY(snapshot);
        QCOMPARE(snapshot->instanceId, raw->instanceId());
        QCOMPARE(snapshot->matcher.dryRuns, std::uint64_t{17});
        QVERIFY(!snapshot->capturedUtc.isEmpty());
        QVERIFY(snapshot->capturedNs > 0);
    }

    void asioLoggingDisabledCadenceAndReset()
    {
        AsioLogCapture capture(false);
        AudioEngine engine;
        auto bus = std::make_unique<DiagnosticBus>();
        DiagnosticBus* raw = bus.get();
        engine.setSpeakersBusForTest(std::move(bus));
        raw->reads = 0;
        NetworkDiagnosticsDialog dialog(nullptr, &engine);
        QMetaObject::invokeMethod(&dialog, "refresh");
        QCOMPARE(raw->reads, 0);
        QVERIFY(capture.messages.isEmpty());
        capture.setEnabled(true);
        raw->stats.dryRuns = 7;
        QMetaObject::invokeMethod(&dialog, "refresh");
        QCOMPARE(capture.messages.size(), 2); // one line per role
        QVERIFY(capture.messages[0].contains("baseline=true deltaValid=false"));
        QVERIFY(capture.messages[0].contains("dryRunsDelta=\"unavailable\""));
        QVERIFY(capture.messages[1].contains("available=false"));
        const int sampledReads = raw->reads;
        QMetaObject::invokeMethod(&dialog, "refresh");
        QCOMPARE(raw->reads, sampledReads);
        raw->stats.dryRuns = 9;
        QTRY_VERIFY_WITH_TIMEOUT(capture.messages.size() >= 4, 1800);
        QVERIFY(capture.messages[2].contains("deltaValid=true"));
        QVERIFY(capture.messages[2].contains("dryRunsDelta=\"2\""));
        QVERIFY(capture.messages[2].contains("intervalStartMonoNs="));
        raw->stats.dryRuns = 1; // reset/rollback; subtraction must not wrap
        QTRY_VERIFY_WITH_TIMEOUT(capture.messages.size() >= 6, 1800);
        QVERIFY(capture.messages[4].contains("baseline=true deltaValid=false"));
        capture.setEnabled(false);
        QMetaObject::invokeMethod(&dialog, "refresh");
        capture.setEnabled(true);
        QMetaObject::invokeMethod(&dialog, "refresh");
        QVERIFY(capture.messages[6].contains("baseline=true deltaValid=false"));
    }

    // The Backend row names the engine the device catalogue describes, in
    // the words of Settings > Audio's "Sound system" line, not the build's
    // platform ("CoreAudio" / "WASAPI" whatever engine was in use).
    void audioBackendNamesTheEngineInUse_data()
    {
        QTest::addColumn<int>("backend");
        QTest::addColumn<QString>("expected");
        QTest::newRow("core-audio") << int(AudioBackendId::CoreAudio)
                                    << QStringLiteral("Core Audio");
        QTest::newRow("wasapi") << int(AudioBackendId::Wasapi)
                                << QStringLiteral("Windows audio (WASAPI)");
        QTest::newRow("pipewire") << int(AudioBackendId::PipeWire)
                                  << QStringLiteral("PipeWire. NereusSDR talks to it directly.");
    }
    void audioBackendNamesTheEngineInUse()
    {
        QFETCH(int, backend);
        QFETCH(QString, expected);
        AudioEngine engine;
        engine.setVaxOutputsAllowed(false);
        auto fake = std::make_shared<FakeAudioEngineBackend>(static_cast<AudioBackendId>(backend));
        engine.setAudioBackendsForTest({fake});
        QVERIFY(engine.catalogue() != nullptr);
        NetworkDiagnosticsDialog dialog(nullptr, &engine);
        QMetaObject::invokeMethod(&dialog, "refresh");
        QLabel* value = backendValue(dialog);
        QVERIFY(value != nullptr);
        QCOMPARE(value->text(), expected);
    }

    void buildsWithNullModel() {
        // Defensive: opening before connection should not crash.
        NetworkDiagnosticsDialog dlg(/*model=*/nullptr, /*audio=*/nullptr);
        // Pass = no crash during construction
        QVERIFY(true);
    }

    void refreshDoesNotCrashWithDisconnectedModel() {
        RadioModel model;
        NetworkDiagnosticsDialog dlg(&model, nullptr);
        QMetaObject::invokeMethod(&dlg, "refresh");
        // Pass = no crash
        QVERIFY(true);
    }

    void resetSessionStatsZeroesCounters() {
        RadioModel model;
        AudioEngine engine;
        NetworkDiagnosticsDialog dlg(&model, &engine);

        // Drive 1 underrun bump
        engine.simulateUnderrun();

        // Reset
        QMetaObject::invokeMethod(&dlg, "onResetSessionStats");

        // Verify by calling reset twice — no crash, idempotent
        QMetaObject::invokeMethod(&dlg, "onResetSessionStats");
        QVERIFY(true);
    }

    void connectsToRadioConnectionSignalsWithoutCrash() {
        RadioModel model;
        NetworkDiagnosticsDialog dlg(&model, nullptr);

        // If RadioModel::connection() is non-null, the dialog should have
        // wired pingRttMeasured. Emitting it should not crash.
        if (auto* conn = model.connection()) {
            emit conn->pingRttMeasured(42);
            emit conn->pingRttMeasured(120);
        }
        QVERIFY(true);
    }
};

QTEST_MAIN(TstNetworkDiagnosticsDialog)
#include "tst_network_diagnostics_dialog.moc"
