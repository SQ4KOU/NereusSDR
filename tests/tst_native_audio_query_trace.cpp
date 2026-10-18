// =================================================================
// tests/tst_native_audio_query_trace.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Fake queries only; no native device APIs.
// Modification history (NereusSDR):
//   2026-10-12: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//               Verify query marker ordering, fidelity and log privacy.
// =================================================================
#include <QtTest/QtTest>
#include <QRegularExpression>

#include "core/audio/NativeAudioQueryTrace.h"

#include <memory>
#include <optional>

using namespace NereusSDR;

namespace {

QStringList* capturedMessages = nullptr;

void captureMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtDebugMsg && context.category
        && QByteArray(context.category) == QByteArray("nereus.audio")) {
        capturedMessages->append(message);
    }
}

// Scope all global logging changes to each test, including early assertion exits.
class QueryLogCapture final {
public:
    explicit QueryLogCapture(bool enabled)
        : m_wasEnabled(lcAudio().isDebugEnabled())
    {
        capturedMessages = &messages;
        m_previousHandler = qInstallMessageHandler(captureMessage);
        const_cast<QLoggingCategory&>(lcAudio()).setEnabled(QtDebugMsg, enabled);
    }

    ~QueryLogCapture()
    {
        const_cast<QLoggingCategory&>(lcAudio()).setEnabled(QtDebugMsg, m_wasEnabled);
        qInstallMessageHandler(m_previousHandler);
        capturedMessages = nullptr;
    }

    QStringList messages;

private:
    bool m_wasEnabled;
    QtMessageHandler m_previousHandler = nullptr;
};

} // namespace

class TestNativeAudioQueryTrace final : public QObject {
    Q_OBJECT

private slots:
    void beginsBeforeQueryAndEndsAfter()
    {
        QueryLogCapture capture(true);
        int calls = 0;
        bool beginVisibleDuringQuery = false;
        const QString privateUid = QStringLiteral("private-test-device-uid");
        const QString result = tracedNativeAudioQuery("device.uid", 3, [&] {
            ++calls;
            beginVisibleDuringQuery = capture.messages.size() == 1
                && capture.messages.first().startsWith(QStringLiteral("native-audio-query begin "));
            return privateUid;
        });
        QCOMPARE(calls, 1);
        QVERIFY(beginVisibleDuringQuery);
        QCOMPARE(result, privateUid);
        QCOMPARE(capture.messages.size(), 2);
        QVERIFY(capture.messages.last().startsWith(QStringLiteral("native-audio-query end ")));
        const QRegularExpression beginPattern(QStringLiteral(
            "^native-audio-query begin stage=device\\.uid ordinal=3 pid=([0-9]+) query=([0-9]+) parent=0$"));
        const QRegularExpression endPattern(QStringLiteral(
            "^native-audio-query end stage=device\\.uid ordinal=3 pid=([0-9]+) query=([0-9]+) parent=0 elapsed-us=[0-9]+$"));
        const QRegularExpressionMatch begin = beginPattern.match(capture.messages.first());
        const QRegularExpressionMatch end = endPattern.match(capture.messages.last());
        QVERIFY(begin.hasMatch());
        QVERIFY(end.hasMatch());
        QCOMPARE(begin.captured(1), QString::number(QCoreApplication::applicationPid()));
        QCOMPARE(end.captured(1), begin.captured(1));
        QCOMPARE(end.captured(2), begin.captured(2));
        QVERIFY(begin.captured(2).toULongLong() > 0);
        QVERIFY(!capture.messages.join(' ').contains(privateUid));
    }

    void voidAndFailureResultsRemainUnchanged()
    {
        QueryLogCapture capture(true);
        int calls = 0;
        tracedNativeAudioQuery("listener.device.alive.add", 0, [&] { ++calls; });
        QCOMPARE(calls, 1);
        QCOMPARE(capture.messages.size(), 2);
        constexpr int kFailure = -987654321;
        const int failed = tracedNativeAudioQuery("device.hog", 0, [&] {
            ++calls;
            return kFailure;
        });
        QCOMPARE(failed, kFailure);
        QCOMPARE(calls, 2);
        const std::optional<int> absent = tracedNativeAudioQuery("system.default-input", -1, [&] {
            ++calls;
            return std::optional<int>{};
        });
        QVERIFY(!absent);
        QCOMPARE(calls, 3);
        QCOMPARE(capture.messages.size(), 6);
        QVERIFY(!capture.messages.join(' ').contains(QString::number(kFailure)));
        QVERIFY(!capture.messages.join(' ').contains(QStringLiteral("result=")));
    }

    void preservesReferencesAndMoveOnlyResults()
    {
        QueryLogCapture capture(true);
        int original = 7;
        int& result = tracedNativeAudioQuery("fake.reference", -1, [&]() -> int& { return original; });
        QVERIFY(&result == &original);
        const std::unique_ptr<int> moved = tracedNativeAudioQuery("fake.move", -1, [] {
            return std::make_unique<int>(17);
        });
        QCOMPARE(*moved, 17);
        QCOMPARE(capture.messages.size(), 4);
        const QRegularExpression sequencePattern(QStringLiteral(" query=([0-9]+)"));
        const QString first = sequencePattern.match(capture.messages.at(0)).captured(1);
        const QString second = sequencePattern.match(capture.messages.at(2)).captured(1);
        QVERIFY(first != second);
    }

    void nestedMarkersIdentifyTheActualCall()
    {
        QueryLogCapture capture(true);
        bool bothBeginsVisible = false;
        const int result = tracedNativeAudioQuery("device.input-channels", 2, [&] {
            return tracedNativeAudioQuery("hal.get-property-data-size", -1, [&] {
                bothBeginsVisible = capture.messages.size() == 2;
                return 29;
            });
        });
        QCOMPARE(result, 29);
        QVERIFY(bothBeginsVisible);
        QCOMPARE(capture.messages.size(), 4);
        QVERIFY(capture.messages.at(0).contains(QStringLiteral("begin stage=device.input-channels")));
        QVERIFY(capture.messages.at(1).contains(QStringLiteral("begin stage=hal.get-property-data-size")));
        QVERIFY(capture.messages.at(2).contains(QStringLiteral("end stage=hal.get-property-data-size")));
        QVERIFY(capture.messages.at(3).contains(QStringLiteral("end stage=device.input-channels")));
        const QRegularExpression queryPattern(QStringLiteral(" query=([0-9]+) parent=([0-9]+)"));
        const QRegularExpressionMatch outer = queryPattern.match(capture.messages.at(0));
        const QRegularExpressionMatch inner = queryPattern.match(capture.messages.at(1));
        const QRegularExpressionMatch innerEnd = queryPattern.match(capture.messages.at(2));
        QVERIFY(outer.hasMatch());
        QVERIFY(inner.hasMatch());
        QVERIFY(innerEnd.hasMatch());
        QCOMPARE(outer.captured(2), QStringLiteral("0"));
        QCOMPARE(inner.captured(2), outer.captured(1));
        QCOMPARE(innerEnd.captured(2), outer.captured(1));
        tracedNativeAudioQuery("system.default-input", -1, [] { return 0; });
        QCOMPARE(queryPattern.match(capture.messages.at(4)).captured(2), QStringLiteral("0"));
    }

    void finishMarksAcquisitionOnlyOnce()
    {
        QueryLogCapture capture(true);
        {
            NativeAudioQueryTrace trace("cache.mutex-acquire");
            QCOMPARE(capture.messages.size(), 1);
            trace.finish();
            QCOMPARE(capture.messages.size(), 2);
            trace.finish();
            QCOMPARE(capture.messages.size(), 2);
        }
        QCOMPARE(capture.messages.size(), 2);
    }

    void disabledLoggingStillExecutesQuery()
    {
        QueryLogCapture capture(false);
        int calls = 0;
        const int result = tracedNativeAudioQuery("device.alive", 0, [&] {
            ++calls;
            return 23;
        });
        tracedNativeAudioQuery("listener.device.hog.remove", -1, [&] { ++calls; });
        QCOMPARE(calls, 2);
        QCOMPARE(result, 23);
        QVERIFY(capture.messages.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestNativeAudioQueryTrace)
#include "tst_native_audio_query_trace.moc"
