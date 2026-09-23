// =================================================================
// tests/tst_daemon_config.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// R1 Task 9: DaemonConfig parses nereusd's own "key = value" config file
// (default /etc/nereusd.conf, overridable with --config). Test bodies are
// the brief's own (task-9-brief.md Step 1) verbatim.
//
// R1 Task 9 fix round 1: three more slots pin resolveDaemonProfileArgument
// (--profile support, closing the gap described in DaemonConfig.h's own
// comment on that function and task-9-report.md section 5). This is new
// glue logic this task wrote, not a re-test of AppSettings::
// isValidProfileName()'s own accept/reject rules -- those already have
// dedicated coverage in tests/tst_app_settings_profile.cpp.
//
// R1 merge-blocker fix round: sampleFileKeysAndParserKeysAgree pins the
// shipped packaging/nereusd.conf.sample against the parser, because the
// sample file documented keys the daemon did nothing with.
//
// Remote Daemon R2, Task 1: resolveDaemonProfileArgument() gained a
// `wasSet` parameter and inverted its absent-flag default (nereusd's
// own reserved profile instead of silently sharing the GUI's directory).
// absentProfileArgumentResolvesToReservedDaemonProfile() below replaces
// the old emptyProfileArgumentMeansNoProfile() slot, which pinned exactly
// the contract this task inverts; explicitlyEmptyProfileArgumentStillShares()
// pins the escape hatch that keeps the old behaviour reachable on purpose.
// tests/tst_daemon_settings_profile.cpp is the fuller seam test for this
// change (constants, the settings/log directory move, the first-run seed
// marker); the slots here stay focused on resolveDaemonProfileArgument()'s
// own argument-resolution contract.
// =================================================================

#include <QtTest>
#include <QFile>
#include <QTemporaryFile>
#include <QTextStream>
#include <QRegularExpression>
#include "core/AppSettings.h"
#include "core/daemon/DaemonConfig.h"

using namespace NereusSDR;

class TstDaemonConfig : public QObject {
    Q_OBJECT
private slots:
    void rejectsMalformedRadioIdentityBeforeStartingListener()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        QString error;
        for (const QString& value : {QString(), QStringLiteral("aa:BB:01:02:03:04")}) {
            cfg.radioMac = value;
            QVERIFY(cfg.validate(&error));
        }
        for (const QString& value : {QStringLiteral("aa:bb:cc"), QStringLiteral("gg:01:02:03:04:05"),
                                    QStringLiteral("aa-bb-cc-dd-ee-ff"), QStringLiteral("aa:bb:cc:dd:ee:ff\n")}) {
            cfg.radioMac = value;
            QVERIFY(!cfg.validate(&error));
            QVERIFY(error.contains(QStringLiteral("radio_mac")));
        }
    }

    void coreNameBoundsUseUtf8Bytes()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        QString error;
        cfg.coreName = QString::fromUtf8("🛰 Shack");
        QVERIFY(cfg.validate(&error));
        cfg.coreName = QString(128, QLatin1Char('A'));
        QVERIFY(cfg.validate(&error));
        cfg.coreName = QString(65, QChar(0x00e9));
        QVERIFY(!cfg.validate(&error));
        cfg.coreName = QStringLiteral("Shack\nForged");
        QVERIFY(!cfg.validate(&error));
        cfg.coreName = QString(QChar(0xd800));
        QVERIFY(!cfg.validate(&error));
    }

    void defaultsAreValid()
    {
        QString err;
        QVERIFY(DaemonConfig::defaults().validate(&err));
        QVERIFY2(err.isEmpty(), qPrintable(err));
    }

    void parsesAWellFormedFile()
    {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("radio_mac = 00:1C:2D:05:37:2A\n"
                "sample_rate_hz = 384000\n"
                "slice_count = 3\n"
                "audio_device = hw:CARD=Device\n"
                "display_application_bytes_per_second = 2400000\n"
                "spectrum_sample_units_per_second = 1800000\n");
        f.flush();
        QString err;
        DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(c.radioMac, QStringLiteral("00:1C:2D:05:37:2A"));
        QCOMPARE(c.sampleRateHz, 384000);
        QCOMPARE(c.sliceCount, 3);
        QCOMPARE(c.audioDevice, QStringLiteral("hw:CARD=Device"));
        QVERIFY(c.sampleRateExplicit);
        QVERIFY(c.displayApplicationBytesPerSecond.has_value());
        QVERIFY(c.spectrumSampleUnitsPerSecond.has_value());
        QCOMPARE(*c.displayApplicationBytesPerSecond, quint64(2400000));
        QCOMPARE(*c.spectrumSampleUnitsPerSecond, quint64(1800000));
        const std::optional<DisplayBudgetLimits> limits = c.displayBudgetLimits();
        QVERIFY(limits.has_value());
        QCOMPARE(limits->applicationBytesPerSecond, quint64(2400000));
        QCOMPARE(limits->spectrumSampleUnitsPerSecond, quint64(1800000));
        QCOMPARE(limits->generation, quint32(1));
    }

    // sampleRateHz always holds a usable rate (validate() rejects <= 0), so
    // it cannot itself express "the operator did not ask for one".
    // DaemonApp::applyConfigToSettings writes the rate into the per-MAC
    // AppSettings key the shared connect path reads, which is persisted
    // state the GUI reads back on its next launch. Without a separate
    // "was it asked for" flag, a bare `nereusd` with no config file (a
    // non-fatal case: server_main warns and continues with defaults) would
    // stamp the 192000 struct default over a rate the operator had already
    // persisted for that radio. Pinned in both directions.
    void sampleRateExplicitOnlyWhenTheKeyIsPresent()
    {
        QTemporaryFile absent;
        QVERIFY(absent.open());
        absent.write("radio_mac = aa:bb:cc:dd:ee:ff\n"
                     "slice_count = 1\n");
        absent.flush();
        QString err;
        const DaemonConfig noKey = DaemonConfig::fromFile(absent.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!noKey.sampleRateExplicit);
        QCOMPARE(noKey.sampleRateHz, DaemonConfig::defaults().sampleRateHz);

        // Defaults, and an unreadable file that falls back to them, are
        // equally "not asked for".
        QVERIFY(!DaemonConfig::defaults().sampleRateExplicit);
        const DaemonConfig missing =
            DaemonConfig::fromFile(QStringLiteral("/nonexistent/nereusd.conf"), &err);
        QVERIFY(!missing.sampleRateExplicit);

        // A present but unparseable value keeps the default rate, and must
        // not count as explicit either.
        QTemporaryFile garbage;
        QVERIFY(garbage.open());
        garbage.write("sample_rate_hz = not-a-number\n");
        garbage.flush();
        const DaemonConfig bad = DaemonConfig::fromFile(garbage.fileName(), &err);
        QVERIFY(!bad.sampleRateExplicit);

        // And the value the operator did ask for still round-trips, even
        // when it happens to equal the default.
        QTemporaryFile same;
        QVERIFY(same.open());
        same.write("sample_rate_hz = 192000\n");
        same.flush();
        const DaemonConfig explicitDefault =
            DaemonConfig::fromFile(same.fileName(), &err);
        QVERIFY(explicitDefault.sampleRateExplicit);
        QCOMPARE(explicitDefault.sampleRateHz, 192000);
    }

    // Every key nereusd.conf.sample documents must parse, and nothing it
    // does not document may. The reason this is pinned: the sample file
    // shipped `log_level` for a while, which parsed into a struct field
    // that no production code ever read, so an operator setting it got
    // silence. Removing a key from the struct without removing it from
    // the sample file (or the reverse) now fails here.
    void sampleFileKeysAndParserKeysAgree()
    {
        const QStringList documented = {
            QStringLiteral("radio_mac"),
            QStringLiteral("sample_rate_hz"),
            QStringLiteral("slice_count"),
            QStringLiteral("audio_device"),
            // Remote Daemon R2 Task 18: both reach DaemonApp::
            // startStationServer(), which is what this pinning test is
            // for -- a key that reaches DaemonConfig must reach behaviour
            // AND the sample file.
            QStringLiteral("remote_port"),
            QStringLiteral("remote_bind"),
            QStringLiteral("core_name"),
            QStringLiteral("display_application_bytes_per_second"),
            QStringLiteral("spectrum_sample_units_per_second"),
            // R-R3-23: reaches DaemonMediaController::setAudioTargetBitrate().
            QStringLiteral("audio_bitrate"),
            // R-R3-41: reaches startDaemonThreadPlacement() in server_main.
            QStringLiteral("thread_placement"),
            // R-R3-08/37/40: reaches DaemonApp::startStationServer(), which
            // owns the display load governor.
            QStringLiteral("display_adaptive"),
            // R-R3-23: reaches DaemonMediaController::setAudioLosslessAllowed().
            QStringLiteral("audio_lossless"),
        };

        // Each documented key parses without an "unknown key" complaint.
        // fromFile logs unknown keys via qCWarning; QTest turns an
        // unexpected qWarning into a test failure only with
        // QTest::failOnWarning, so assert on the parsed value instead.
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("radio_mac = aa:bb:cc:dd:ee:ff\n"
                "sample_rate_hz = 96000\n"
                "slice_count = 2\n"
                "audio_device = default\n"
                "remote_port = 4711\n"
                "remote_bind = 0.0.0.0\n"
                "core_name = Rock 5C\n"
                "display_application_bytes_per_second = 2400000\n"
                "spectrum_sample_units_per_second = 1800000\n"
                "audio_bitrate = 48000\n"
                "thread_placement = off\n"
                "display_adaptive = off\n"
                "audio_lossless = deny\n");
        f.flush();
        QString err;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(c.radioMac, QStringLiteral("aa:bb:cc:dd:ee:ff"));
        QCOMPARE(c.sampleRateHz, 96000);
        QCOMPARE(c.sliceCount, 2);
        QCOMPARE(c.audioDevice, QStringLiteral("default"));
        QCOMPARE(c.remotePort, 4711);
        QCOMPARE(c.remoteBind, QStringLiteral("0.0.0.0"));
        QCOMPARE(c.coreName, QStringLiteral("Rock 5C"));
        QCOMPARE(c.audioBitrate, 48000);
        QCOMPARE(c.threadPlacement, false);
        QCOMPARE(c.displayAdaptive, false);
        QCOMPARE(c.audioLosslessAllowed, false);
        const std::optional<DisplayBudgetLimits> limits = c.displayBudgetLimits();
        QVERIFY(limits.has_value());
        QCOMPARE(limits->applicationBytesPerSecond, quint64(2400000));
        QCOMPARE(limits->spectrumSampleUnitsPerSecond, quint64(1800000));
        QCOMPARE(limits->generation, quint32(1));

        // And the shipped sample file documents exactly those keys, no
        // more. Parsed straight out of the packaging file so the two
        // cannot drift.
        QFile sample(QStringLiteral(NEREUS_SOURCE_DIR
                                    "/packaging/nereusd.conf.sample"));
        QVERIFY2(sample.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(sample.fileName()));
        QStringList found;
        QTextStream ts(&sample);
        while (!ts.atEnd()) {
            QString line = ts.readLine().trimmed();
            // Optional settings are documented as commented placeholder
            // assignments so copying the sample cannot accidentally enable
            // an unmeasured production limit. They still belong to the
            // parser/sample parity contract.
            if (line.startsWith(QLatin1Char('#'))) {
                line.remove(0, 1);
                line = line.trimmed();
                const int eq = line.indexOf(QLatin1Char('='));
                const QString placeholder = eq > 0 ? line.mid(eq + 1).trimmed() : QString();
                if (eq > 0 && placeholder.startsWith(QLatin1Char('<'))
                    && placeholder.endsWith(QLatin1Char('>'))) {
                    found << line.left(eq).trimmed();
                }
                continue;
            }
            const int hash = line.indexOf(QLatin1Char('#'));
            if (hash >= 0) { line.truncate(hash); }
            line = line.trimmed();
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) { found << line.left(eq).trimmed(); }
        }
        found.sort();
        QStringList expected = documented;
        expected.sort();
        QCOMPARE(found, expected);
    }

    // R-R3-23: 24000 and 48000 are the only encoder profiles. A missing key
    // is 24000; any other value logs exactly one warning and keeps 24000,
    // like remote_port's unparseable-value handling, never a startup error.
    void audioBitrateAcceptsOnlyTheTwoProfiles_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<int>("expected");
        QTest::addColumn<bool>("warns");
        QTest::newRow("missing") << QByteArray("slice_count = 1\n") << 24000 << false;
        QTest::newRow("24000") << QByteArray("audio_bitrate = 24000\n") << 24000 << false;
        QTest::newRow("48000") << QByteArray("audio_bitrate = 48000\n") << 48000 << false;
        QTest::newRow("32000") << QByteArray("audio_bitrate = 32000\n") << 24000 << true;
        QTest::newRow("zero") << QByteArray("audio_bitrate = 0\n") << 24000 << true;
        QTest::newRow("negative") << QByteArray("audio_bitrate = -48000\n") << 24000 << true;
        QTest::newRow("words") << QByteArray("audio_bitrate = fast\n") << 24000 << true;
        QTest::newRow("empty") << QByteArray("audio_bitrate =\n") << 24000 << true;
        QTest::newRow("48k-then-bad")
            << QByteArray("audio_bitrate = 48000\naudio_bitrate = 96000\n") << 24000 << true;
    }

    void audioBitrateAcceptsOnlyTheTwoProfiles()
    {
        QFETCH(QByteArray, text);
        QFETCH(int, expected);
        QFETCH(bool, warns);
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write(text);
        f.flush();
        if (warns) {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression(QStringLiteral("audio_bitrate must be 24000 or 48000, keeping 24000")));
        }
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
        QString err;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(c.audioBitrate, expected);
        QVERIFY(c.validate(&err));
    }

    // R-R3-41: thread_placement is auto (the default) or off; any other
    // value logs one warning and keeps auto.
    void threadPlacementAcceptsAutoOrOff_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<bool>("expected");
        QTest::addColumn<bool>("warns");
        QTest::newRow("missing") << QByteArray("slice_count = 1\n") << true << false;
        QTest::newRow("auto") << QByteArray("thread_placement = auto\n") << true << false;
        QTest::newRow("off") << QByteArray("thread_placement = off\n") << false << false;
        QTest::newRow("words") << QByteArray("thread_placement = yes\n") << true << true;
        QTest::newRow("off-then-bad")
            << QByteArray("thread_placement = off\nthread_placement = on\n") << true << true;
    }

    void threadPlacementAcceptsAutoOrOff()
    {
        QFETCH(QByteArray, text);
        QFETCH(bool, expected);
        QFETCH(bool, warns);
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write(text);
        f.flush();
        if (warns) {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression(QStringLiteral("thread_placement must be auto or off, keeping auto")));
        }
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
        QString err;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(c.threadPlacement, expected);
    }

    // R-R3-08/37/40: display_adaptive is on (the default) or off; any other
    // value logs one warning and keeps on.
    void displayAdaptiveAcceptsOnOrOff_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<bool>("expected");
        QTest::addColumn<bool>("warns");
        QTest::newRow("missing") << QByteArray("slice_count = 1\n") << true << false;
        QTest::newRow("on") << QByteArray("display_adaptive = on\n") << true << false;
        QTest::newRow("off") << QByteArray("display_adaptive = off\n") << false << false;
        QTest::newRow("words") << QByteArray("display_adaptive = auto\n") << true << true;
        QTest::newRow("off-then-bad")
            << QByteArray("display_adaptive = off\ndisplay_adaptive = yes\n") << true << true;
    }

    void displayAdaptiveAcceptsOnOrOff()
    {
        QFETCH(QByteArray, text);
        QFETCH(bool, expected);
        QFETCH(bool, warns);
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write(text);
        f.flush();
        if (warns) {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression(QStringLiteral("display_adaptive must be on or off, keeping on")));
        }
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
        QString err;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(c.displayAdaptive, expected);
        QCOMPARE(DaemonConfig::defaults().displayAdaptive, true);
    }

    // R-R3-23: lossless audio is allowed unless the file says deny. Any
    // other value logs exactly one warning and keeps allow, like
    // audio_bitrate, never a startup error.
    void audioLosslessIsAllowOrDeny_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<bool>("expected");
        QTest::addColumn<bool>("warns");
        QTest::newRow("missing") << QByteArray("slice_count = 1\n") << true << false;
        QTest::newRow("allow") << QByteArray("audio_lossless = allow\n") << true << false;
        QTest::newRow("deny") << QByteArray("audio_lossless = deny\n") << false << false;
        QTest::newRow("Deny") << QByteArray("audio_lossless = Deny\n") << false << false;
        QTest::newRow("comment") << QByteArray("audio_lossless = deny # digital modes off\n")
                                 << false << false;
        QTest::newRow("words") << QByteArray("audio_lossless = no\n") << true << true;
        QTest::newRow("empty") << QByteArray("audio_lossless =\n") << true << true;
        QTest::newRow("deny-then-bad")
            << QByteArray("audio_lossless = deny\naudio_lossless = maybe\n") << true << true;
    }

    void audioLosslessIsAllowOrDeny()
    {
        QFETCH(QByteArray, text);
        QFETCH(bool, expected);
        QFETCH(bool, warns);
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write(text);
        f.flush();
        if (warns) {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression(QStringLiteral("audio_lossless must be allow or deny, keeping allow")));
        }
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
        QString err;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(c.audioLosslessAllowed, expected);
        QVERIFY(c.validate(&err));
        QVERIFY(DaemonConfig::defaults().audioLosslessAllowed);
    }

    void validateRefusesAnUnsupportedAudioBitrate()
    {
        DaemonConfig c = DaemonConfig::defaults();
        QCOMPARE(c.audioBitrate, 24000);
        QString err;
        QVERIFY(c.validate(&err));
        c.audioBitrate = 48000;
        QVERIFY(c.validate(&err));
        c.audioBitrate = 96000;
        QVERIFY(!c.validate(&err));
        QVERIFY(err.contains(QStringLiteral("audio_bitrate")));
    }

    void absentDisplayBudgetPairPreservesLegacyMode()
    {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("radio_mac = aa:bb:cc:dd:ee:ff\n");
        f.flush();

        QString err;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!c.displayApplicationBytesPerSecond.has_value());
        QVERIFY(!c.spectrumSampleUnitsPerSecond.has_value());
        QVERIFY(!c.displayBudgetLimits().has_value());
        QVERIFY(c.validate(&err));
    }

    void rejectsInvalidDisplayBudgetPair_data()
    {
        QTest::addColumn<QByteArray>("contents");

        QTest::newRow("application-only")
            << QByteArray("display_application_bytes_per_second = 1\n");
        QTest::newRow("samples-only")
            << QByteArray("spectrum_sample_units_per_second = 1\n");
        QTest::newRow("zero-application")
            << QByteArray("display_application_bytes_per_second = 0\n"
                          "spectrum_sample_units_per_second = 1\n");
        QTest::newRow("zero-samples")
            << QByteArray("display_application_bytes_per_second = 1\n"
                          "spectrum_sample_units_per_second = 0\n");
        QTest::newRow("negative-application")
            << QByteArray("display_application_bytes_per_second = -1\n"
                          "spectrum_sample_units_per_second = 1\n");
        QTest::newRow("negative-samples")
            << QByteArray("display_application_bytes_per_second = 1\n"
                          "spectrum_sample_units_per_second = -1\n");
        QTest::newRow("fractional-application")
            << QByteArray("display_application_bytes_per_second = 1.5\n"
                          "spectrum_sample_units_per_second = 1\n");
        QTest::newRow("fractional-samples")
            << QByteArray("display_application_bytes_per_second = 1\n"
                          "spectrum_sample_units_per_second = 1.5\n");
        QTest::newRow("malformed-application")
            << QByteArray("display_application_bytes_per_second = many\n"
                          "spectrum_sample_units_per_second = 1\n");
        QTest::newRow("malformed-samples")
            << QByteArray("display_application_bytes_per_second = 1\n"
                          "spectrum_sample_units_per_second = many\n");
        QTest::newRow("above-json-safe-application")
            << QByteArray("display_application_bytes_per_second = 9007199254740992\n"
                          "spectrum_sample_units_per_second = 1\n");
        QTest::newRow("above-json-safe-samples")
            << QByteArray("display_application_bytes_per_second = 1\n"
                          "spectrum_sample_units_per_second = 9007199254740992\n");
        QTest::newRow("quint64-overflow")
            << QByteArray("display_application_bytes_per_second = 18446744073709551616\n"
                          "spectrum_sample_units_per_second = 1\n");
    }

    void rejectsInvalidDisplayBudgetPair()
    {
        QFETCH(QByteArray, contents);
        QTemporaryFile f;
        QVERIFY(f.open());
        QCOMPARE(f.write(contents), qint64(contents.size()));
        f.flush();

        QString parseError;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &parseError);
        QVERIFY2(parseError.isEmpty(), qPrintable(parseError));
        QString validationError;
        QVERIFY(!c.validate(&validationError));
        QVERIFY2(validationError.contains(QStringLiteral("display_application_bytes_per_second")),
                 qPrintable(validationError));
        QVERIFY(!c.displayBudgetLimits().has_value());
    }

    void acceptsJsonSafeDisplayBudgetMaximum()
    {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("display_application_bytes_per_second = 9007199254740991\n"
                "spectrum_sample_units_per_second = 9007199254740991\n");
        f.flush();

        QString err;
        const DaemonConfig c = DaemonConfig::fromFile(f.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(c.validate(&err));
        const std::optional<DisplayBudgetLimits> limits = c.displayBudgetLimits();
        QVERIFY(limits.has_value());
        QCOMPARE(limits->applicationBytesPerSecond, quint64(9007199254740991ULL));
        QCOMPARE(limits->spectrumSampleUnitsPerSecond, quint64(9007199254740991ULL));
    }

    // Remote Daemon R2 Task 18: the listener is OPT IN. A default-
    // constructed config must not bind anything, and must bind loopback
    // when it does. Pinned because flipping either default silently turns
    // every existing nereusd install into a network service.
    void remoteListenerIsOptInAndLoopbackByDefault()
    {
        const DaemonConfig d = DaemonConfig::defaults();
        QCOMPARE(d.remotePort, 0);
        QCOMPARE(d.remoteBind, QStringLiteral("127.0.0.1"));
        QString err;
        QVERIFY(d.validate(&err));
    }

    void rejectsRemotePortOutOfRange()
    {
        DaemonConfig c = DaemonConfig::defaults();
        QString err;

        // 0 is the documented "disabled" value, not an error.
        c.remotePort = 0;
        QVERIFY(c.validate(&err));

        c.remotePort = 65536;
        QVERIFY(!c.validate(&err));
        QVERIFY(!err.isEmpty());

        c.remotePort = -1;
        QVERIFY(!c.validate(&err));
        QVERIFY(!err.isEmpty());
    }

    void rejectsSliceCountBelowOne()
    {
        DaemonConfig c = DaemonConfig::defaults();
        c.sliceCount = 0;
        QString err;
        QVERIFY(!c.validate(&err));
        QVERIFY(!err.isEmpty());
    }

    void missingFileYieldsDefaultsAndAnError()
    {
        QString err;
        DaemonConfig c = DaemonConfig::fromFile("/nonexistent/nereusd.conf", &err);
        QVERIFY(!err.isEmpty());
        QCOMPARE(c.sliceCount, DaemonConfig::defaults().sliceCount);
    }

    // Remote Daemon R2, Task 1: absent (wasSet == false) now reserves
    // nereusd's own profile instead of sharing the GUI's -- this is the
    // exact contract inversion the pre-Task-1 emptyProfileArgumentMeans
    // NoProfile() slot pinned in the other direction.
    void absentProfileArgumentResolvesToReservedDaemonProfile()
    {
        QString err;
        const QString profile = resolveDaemonProfileArgument(QString(), false, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(profile, QString(AppSettings::kDaemonProfileName));
    }

    // The escape hatch: an operator who explicitly types --profile ""
    // (wasSet == true, value still empty) opts back into sharing the
    // GUI's own settings/log directory -- exactly what every --profile
    // argument did before this task.
    void explicitlyEmptyProfileArgumentStillShares()
    {
        QString err;
        const QString profile = resolveDaemonProfileArgument(QString(), true, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(profile.isEmpty());
    }

    void validProfileNameIsAccepted()
    {
        QString err;
        const QString profile =
            resolveDaemonProfileArgument(QStringLiteral("hf"), true, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(profile, QStringLiteral("hf"));
    }

    void invalidProfileNameIsRejected()
    {
        QString err;
        const QString profile =
            resolveDaemonProfileArgument(QStringLiteral("with space"), true, &err);
        QVERIFY(!err.isEmpty());
        QVERIFY(profile.isEmpty());
    }
};

QTEST_MAIN(TstDaemonConfig)
#include "tst_daemon_config.moc"
