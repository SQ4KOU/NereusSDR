// no-port-check: NereusSDR-original test.
//
// R3 user wording (R-R3-17, R-R3-21, R-R3-23, R-R3-35): the sweep. Every
// sentence the reason table can produce, every rule for a reason it does
// not know, the remote audio words, and the rewritten controls on the NNR
// panel and the 4O3A page are checked against the one internal-term list.
// Wire reasons themselves are never touched: the table's left column is the
// Core's and this computer's text byte for byte.

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QRegularExpression>

#include "OperatorWording.h"
#include "PanStatusSamples.h"
#include "core/dsp/NnrSettings.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "gui/OperatorReasonText.h"
#include "gui/RemoteAudioStatus.h"
#include "gui/setup/FourO3APage.h"
#include "gui/widgets/NnrControls.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

// Every string a widget and its children show a user: label and button
// text, group titles, list items and every tooltip.
QStringList shownText(const QWidget& root)
{
    QStringList texts;
    QList<const QWidget*> widgets{&root};
    for (const QWidget* child : root.findChildren<const QWidget*>()) {
        widgets.append(child);
    }
    for (const QWidget* widget : widgets) {
        texts << widget->toolTip();
        if (const auto* label = qobject_cast<const QLabel*>(widget)) {
            texts << label->text();
        } else if (const auto* button = qobject_cast<const QAbstractButton*>(widget)) {
            texts << button->text();
        } else if (const auto* group = qobject_cast<const QGroupBox*>(widget)) {
            texts << group->title();
        } else if (const auto* combo = qobject_cast<const QComboBox*>(widget)) {
            for (int i = 0; i < combo->count(); ++i) {
                texts << combo->itemText(i)
                      << combo->itemData(i, Qt::ToolTipRole).toString();
            }
        }
    }
    texts.removeAll(QString());
    return texts;
}

// A source file with adjacent string literals joined ("a" "b" -> "ab"), so
// a sentence split across lines reads as it is shown.
QString joinedSource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QString text = QString::fromUtf8(file.readAll());
    static const QRegularExpression adjacent(QStringLiteral("\"\\s*\\n\\s*\"|\"[ \\t]+\""));
    text.replace(adjacent, QString());
    return text;
}

// Every tr("...") literal in a source file, as written (escapes kept).
QStringList trLiterals(const QString& source)
{
    static const QRegularExpression literal(
        QStringLiteral("\\btr\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    QStringList found;
    QRegularExpressionMatchIterator it = literal.globalMatch(source);
    while (it.hasNext()) {
        found.append(it.next().captured(1));
    }
    return found;
}

QString sourcePath(const char* relative)
{
    return QDir(QStringLiteral(NEREUS_SOURCE_DIR)).filePath(QString::fromLatin1(relative));
}

} // namespace

class TestOperatorWordingSweep : public QObject {
    Q_OBJECT

private slots:
    void theTestsUseTheProductsTermList()
    {
        // One list: the helper hands back the product's own object.
        QCOMPARE(&OperatorWording::internalTerms(), &OperatorReasonText::internalTerms());
        for (const char* term : {"grant", "budget", "allocation", "capabilit", "minor", "slot",
                                 "epoch", "SSRC", "endpoint", "revision", "handshake",
                                 "snapshot", "codec", "payload", "peer", "protocol", "session",
                                 "telemetry", "RTP", "PCM", "WebSocket", "pong", "ledger",
                                 "reservation", "descriptor", "plane", "context", "matcher"}) {
            QVERIFY2(OperatorReasonText::internalTerms().contains(QLatin1String(term)), term);
        }
    }

    void everyTranslationIsPlainAndNotTheRawReason()
    {
        const QStringList reasons = OperatorReasonText::knownReasons();
        QVERIFY(reasons.size() > 100);
        for (const QString& reason : reasons) {
            const QString sentence = OperatorReasonText::forDisplay(reason);
            QVERIFY2(OperatorWording::isPlain(sentence),
                     qPrintable(reason + QStringLiteral(" -> ") + sentence));
            QVERIFY2(sentence != reason, qPrintable(reason));
            QVERIFY2(OperatorWording::isPlain(OperatorReasonText::shortForDisplay(reason)),
                     qPrintable(reason));
        }
    }

    void anUnknownReasonInUserWordsIsShownAsSent()
    {
        const QString plain = QStringLiteral("Connection refused");
        QCOMPARE(OperatorReasonText::forDisplay(plain), plain);
        const QString nr3 = QStringLiteral("No NR3 model file was found on this Core, so NR3 cannot run.");
        QCOMPARE(OperatorReasonText::forDisplay(nr3), nr3);
    }

    void anUnknownReasonInInternalTermsIsNotShown()
    {
        for (const QString& raw : {QStringLiteral("endpoint ledger overflow 42"),
                                   QStringLiteral("WDSPNN descriptor/data extents conflict"),
                                   QStringLiteral("Remote audio needs a fresh context after a stream gap"),
                                   QString(), QStringLiteral("   ")}) {
            const QString shown = OperatorReasonText::forDisplay(raw);
            QCOMPARE(shown, QStringLiteral("The reason is in the log."));
            QVERIFY(OperatorWording::isPlain(shown));
        }
    }

    void wordedValuesKeepTheirNumbers()
    {
        // Numbers and units are the operator's; only the words around them
        // change.
        QCOMPARE(OperatorReasonText::forDisplay(
                     QStringLiteral("Station media did not connect within 1.5 seconds")),
                 QStringLiteral("Audio and display from the Core did not connect within 1.5 "
                                "seconds. This app tries again."));
        QCOMPARE(OperatorReasonText::forDisplay(
                     QStringLiteral("Core sent no station media description within 5 seconds")),
                 QStringLiteral("The Core did not start audio and display within 5 seconds. "
                                "This app tries again."));
        QCOMPARE(OperatorReasonText::forDisplay(QStringLiteral(
                     "The station session is not established, so the TGXL disconnect was not sent.")),
                 QStringLiteral("This app is not connected to the Core right now, so the TGXL "
                                "disconnect was not sent."));
    }

    void mediaStartKeepsTheTransportsOwnWordsWhenPlain()
    {
        QCOMPARE(OperatorReasonText::forDisplay(QStringLiteral(
                     "Station media could not start on this computer: could not create the "
                     "connection")),
                 QStringLiteral("Audio and display could not start on this computer. Details: "
                                "could not create the connection"));
        const QString jargon = OperatorReasonText::forDisplay(QStringLiteral(
            "Station media could not start on this computer: could not create the peer connection"));
        QCOMPARE(jargon, QStringLiteral("Audio and display could not start on this computer. "
                                        "The reason is in the log."));
        QVERIFY(OperatorWording::isPlain(jargon));
    }

    // Fix wave I2, I3, M2: the transport, certificate and LAN discovery
    // reasons read in user words.
    void newReasonsReadInUserWords()
    {
        QCOMPARE(OperatorReasonText::forDisplay(QStringLiteral("media peer connection failed")),
                 QStringLiteral("The audio and display connection to the Core failed. This app "
                                "reconnects to the Core."));
        QCOMPARE(OperatorReasonText::forDisplay(QStringLiteral("text RTP message rejected")),
                 QStringLiteral("Audio and display stopped because an audio packet from the "
                                "Core could not be used."));
        QCOMPARE(OperatorReasonText::forDisplay(QStringLiteral(
                     "Station certificate fingerprint does not match the saved pin.")),
                 QStringLiteral("The Core's certificate does not match the certificate "
                                "fingerprint saved for it, so this app did not connect. If the "
                                "Core was set up again, save its new fingerprint from the "
                                "Core's setup."));
        const QString unsecured = OperatorReasonText::forDisplay(QStringLiteral(
            "Refusing to connect to ws://core.example:4433: a station certificate fingerprint "
            "is pinned, but \"ws\" carries no TLS, so there is nothing to compare the "
            "fingerprint against and the pairing token would travel in cleartext. Use wss://."));
        QVERIFY2(unsecured.endsWith(QStringLiteral("Use an address that starts with wss://.")),
                 qPrintable(unsecured));
        // Two LAN warnings at once: each in user words, in order.
        const QString lan = OperatorReasonText::lanDiscoveryForDisplay(QStringLiteral(
            "Station LAN discovery IPv6 is unavailable. Station LAN discovery could not join "
            "a multicast group."));
        QCOMPARE(lan, QStringLiteral("This app cannot listen for Cores on IPv6 networks. This "
                                     "app could not listen for Cores on every network."));
        QCOMPARE(OperatorReasonText::lanDiscoveryForDisplay(QStringLiteral(
                     "Station LAN announcement endpoint limit reached.")),
                 QStringLiteral("More Cores are announcing on this network than this app "
                                "lists, so some are missing. Add a missing Core by its address."));
    }

    void wireReasonsAreUnchanged()
    {
        // The Core sends these and this app compares them as text; they are
        // translated only when shown.
        QCOMPARE(QString::fromLatin1(kRetireReasonSliceRemoved), QStringLiteral("slice removed"));
        QCOMPARE(QString::fromLatin1(kRetireReasonStreamBindingChanged),
                 QStringLiteral("slice stream binding changed"));
        QCOMPARE(QString::fromLatin1(kRetireReasonSourceRetune),
                 QStringLiteral("source retune no longer covers requested crop"));
        QVERIFY(OperatorReasonText::knownReasons().contains(QStringLiteral("heartbeat timeout")));
        QVERIFY(OperatorReasonText::knownReasons().contains(
            QStringLiteral("Remote 4O3A control requires a newer station protocol.")));
    }

    void remoteAudioWordsArePlain()
    {
        using State = RemoteAudioStatus::State;
        using Fault = RemoteAudioReceiver::Fault;
        for (State state : {State::NotConnected, State::WaitingForAudio, State::MutedHere,
                            State::RadioOffline, State::CoreCouldNotStart, State::Starting,
                            State::Playing, State::Reconnecting, State::PlaybackProblem}) {
            QVERIFY(OperatorWording::isPlain(remoteAudioHeadline(state)));
            QVERIFY(OperatorWording::isPlain(remoteAudioBannerWord(state)));
        }
        for (Fault fault : {Fault::SpeakerOpenFailed, Fault::SpeakerTimingUnavailable,
                            Fault::SpeakerCallbackTooLarge, Fault::SpeakerStalled,
                            Fault::SpeakerWriteFailed, Fault::DecoderUnavailable,
                            Fault::ArrivalBurst, Fault::StreamGap, Fault::NoPackets,
                            Fault::DecodeFailed, Fault::ClockBuffer}) {
            QVERIFY(OperatorWording::isPlain(remoteAudioProblemText(fault)));
        }
        for (RemoteAudioQualityReason reason :
             {RemoteAudioQualityReason::CoreCannotSend, RemoteAudioQualityReason::CoreNotAllowed,
              RemoteAudioQualityReason::ConnectionUnavailable,
              RemoteAudioQualityReason::NetworkTooSlow}) {
            QVERIFY(OperatorWording::isPlain(remoteAudioQualityReasonText(reason)));
        }

        RemoteAudioStatus status;
        status.state = State::Playing;
        status.selectedOutput = QStringLiteral("System default");
        status.profileChoiceAvailable = true;
        status.chosenProfile = RemoteAudioProfile::Lossless;
        status.qualityReason = RemoteAudioQualityReason::NetworkTooSlow;
        status.problem = Fault::SpeakerStalled;
        RemoteAudioDelayReport delay;
        delay.measurable = true;
        const QString details = formatRemoteAudioDetails(status, {}, delay);
        QVERIFY(details.contains(QStringLiteral("Audio format: Not reported by this Core")));
        for (const QString& line : details.split(QLatin1Char('\n'))) {
            QVERIFY2(OperatorWording::isPlain(line), qPrintable(line));
        }
    }

    void nnrPanelIsInUserWords()
    {
        SliceModel slice(0);
        NnrControls full(nullptr, &slice, NnrControls::Presentation::Full);
        const QStringList shown = shownText(full);
        QVERIFY2(shown.size() >= 30, qPrintable(QString::number(shown.size())));
        for (const QString& text : shown) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        }
        // A refusal in the Core's terms reaches the panel in user words.
        slice.reportNnrEditResult(QStringLiteral("This station session does not support NNR controls."));
        const auto* status = full.findChild<QLabel*>(QStringLiteral("nnrStatusReadback"));
        QVERIFY(status);
        QVERIFY2(status->text().contains(
                     QStringLiteral("This Core does not offer NNR controls to this app.")),
                 qPrintable(status->text()));

        // Fix wave M5: the rate row names the noise reduction's own rate.
        const auto* rate = full.findChild<QLabel*>(QStringLiteral("nnrRateLatencyReadback"));
        QVERIFY(rate);
        QVERIFY2(rate->text().startsWith(QStringLiteral("NNR processing ")), qPrintable(rate->text()));

        // Fix wave I2: the rate refusal is a plain line that says what to do.
        slice.reportNnrEditResult(QStringLiteral(
            "NNR requires a DSP rate that is an integer multiple of its network rate."));
        QVERIFY2(status->text().contains(QStringLiteral(
                     "NNR cannot run at this receiver's processing rate, which must be a whole "
                     "multiple of the NNR model's rate. Use another noise reduction, or change "
                     "the processing rate.")),
                 qPrintable(status->text()));

        // Fix wave M1: the receiver-gone branch is in user words too.
        full.invalidateBinding();
        const QStringList closed = shownText(full);
        QVERIFY2(closed.contains(QStringLiteral("Reopen NNR controls to see this receiver again.")),
                 qPrintable(closed.join(QLatin1Char('|'))));
        for (const QString& text : closed) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        }
    }

    // Lane B carry (R-R3-40, R-R3-08, R-R3-37): the NNR step-back wording
    // from the DSP overload batch and the Core-busy pan wording read in user
    // words, and a Core reason about NNR passes OperatorReasonText unchanged.
    void nnrStepBackAndCoreBusyWordingArePlain()
    {
        for (NnrLimitSite site : {NnrLimitSite::ThisComputer, NnrLimitSite::CoreComputer}) {
            for (NnrLimit limit : {NnrLimit::StandardOnly, NnrLimit::Off}) {
                const QString text = nnrLimitExplanation(static_cast<int>(limit), site);
                QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
                QCOMPARE(OperatorReasonText::forDisplay(text), text);
            }
        }
        QVERIFY(nnrLimitExplanation(static_cast<int>(NnrLimit::Off), NnrLimitSite::CoreComputer)
                    .contains(QStringLiteral("The Core computer could not keep up")));

        // The limit as the NNR panel and its "Try again" row show it.
        SliceModel slice(0);
        slice.setNnrLimit(static_cast<int>(NnrLimit::StandardOnly));
        NnrControls full(nullptr, &slice, NnrControls::Presentation::Full);
        const QStringList shown = shownText(full);
        QVERIFY2(shown.contains(nnrLimitExplanation(static_cast<int>(NnrLimit::StandardOnly))),
                 qPrintable(shown.join(QLatin1Char('|'))));
        for (const QString& text : shown) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        }

        // This app's own refusal of "Try again" on an older Core.
        const QString retry = QStringLiteral("This station cannot try noise reduction again. "
                                             "Update the station software.");
        QVERIFY(OperatorWording::isPlain(retry));
        QVERIFY(OperatorWording::isPlain(OperatorReasonText::forDisplay(retry)));

        // Every Core-busy pan form and explanation.
        int busy = 0;
        for (const PanDisplayState& state : PanStatusSamples::all()) {
            if (state.budgetReason != DisplayBudgetReason::CoreBusy) {
                continue;
            }
            ++busy;
            const PanStatusText text = buildPanStatusText(state);
            QVERIFY(text.shortLine.contains(QStringLiteral("Core busy")));
            QVERIFY(text.explanation.contains(QStringLiteral("Core computer is busy")));
            for (const QString& form : text.shortForms()) {
                QVERIFY2(OperatorWording::isPlain(form), qPrintable(form));
            }
            QVERIFY2(OperatorWording::isPlain(text.explanation), qPrintable(text.explanation));
        }
        QCOMPARE(busy, 4);
    }

    // Fix wave M1: every tr() literal in the windows the R3 wording work
    // rewrote, and every remote line MainWindow shows (those naming the
    // Core, the station or remote use), is in user words. Read from the
    // sources, so a string only a live Core reaches is checked too. Each
    // file has a floor so the check cannot pass on nothing.
    void rewordedWindowsAreInUserWordsAtTheSource()
    {
        const struct {
            const char* file;
            int atLeast;
        } files[] = {
            {"src/gui/DspAssetDialog.cpp", 80},
            {"src/gui/GuiConnectionController.cpp", 40},
            {"src/gui/CoreTargetEditor.cpp", 8},
            {"src/gui/ConnectionSelector.cpp", 20},
            {"src/gui/RemoteConnectionController.cpp", 20},
            {"src/gui/RemoteTelemetryController.cpp", 40},
            {"src/gui/RemoteDiagnosticsDialog.cpp", 40},
            {"src/gui/widgets/NnrControls.cpp", 60},
            {"src/gui/setup/FourO3APage.cpp", 30},
            {"src/gui/PsForm.cpp", 80},
        };
        for (const auto& entry : files) {
            const QString source = joinedSource(sourcePath(entry.file));
            QVERIFY2(!source.isEmpty(), entry.file);
            const QStringList literals = trLiterals(source);
            QVERIFY2(literals.size() >= entry.atLeast,
                     qPrintable(QStringLiteral("%1: %2 literals")
                                    .arg(QLatin1String(entry.file)).arg(literals.size())));
            for (const QString& text : literals) {
                QVERIFY2(OperatorWording::isPlain(text),
                         qPrintable(QStringLiteral("%1: %2 [%3]")
                                        .arg(QLatin1String(entry.file), text,
                                             OperatorWording::internalTermIn(text))));
            }
        }

        const QString mainWindow = joinedSource(sourcePath("src/gui/MainWindow.cpp"));
        static const QRegularExpression remote(
            QStringLiteral("\\bCore\\b|[Ss]tation|[Rr]emote"));
        int checked = 0;
        for (const QString& text : trLiterals(mainWindow)) {
            if (!remote.match(text).hasMatch()) {
                continue;
            }
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
            ++checked;
        }
        QVERIFY2(checked >= 25, qPrintable(QString::number(checked)));
        // The lines the final review named, present as reworded.
        for (const char* line :
             {"Unavailable: this window was started for one Core with --station, so the radio "
              "list cannot change it.",
              "Unavailable: the radio's details are on the Core, not in this window.",
              "Wait until this window has the Core's settings before changing the pan layout.",
              "Link to the Core lost: %1", "Audio and display: %1"}) {
            QVERIFY2(mainWindow.contains(QLatin1String(line)), line);
        }
    }

    // Fix wave M7: every exact reason the table translates is still written,
    // byte for byte, somewhere in the sources other than the table itself.
    // A Core or app rewording then fails here instead of silently showing
    // the general sentence.
    void everyTableKeyIsStillInTheSources()
    {
        QString sources;
        QDirIterator it(sourcePath("src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        int files = 0;
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.endsWith(QLatin1String("/OperatorReasonText.cpp"))) {
                continue;
            }
            sources += joinedSource(path);
            sources += QLatin1Char('\n');
            ++files;
        }
        QVERIFY2(files >= 500, qPrintable(QString::number(files)));
        const QStringList keys = OperatorReasonText::tableKeys();
        QVERIFY2(keys.size() >= 140, qPrintable(QString::number(keys.size())));
        for (const QString& key : keys) {
            QVERIFY2(sources.contains(key), qPrintable(key));
        }
    }

    void remoteFourO3APageIsInUserWords()
    {
        RadioModel model(RadioModel::Role::Remote);
        FourO3APage page(&model);
        const auto* master = page.findChild<QAbstractButton*>(QStringLiteral("fourO3AMasterToggle"));
        const auto* status = page.findChild<QLabel*>(QStringLiteral("fourO3AListenerStatus"));
        QVERIFY(master && status);
        for (const QString& text : {master->text(), master->toolTip(), status->text(),
                                    status->toolTip()}) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        }
        QVERIFY(QMetaObject::invokeMethod(&page, "onMasterToggled", Qt::DirectConnection,
                                          Q_ARG(bool, true)));
        int checked = 0;
        for (const QLabel* label : page.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("4O3A"))
                || label->text().startsWith(QStringLiteral("Status:"))) {
                QVERIFY2(OperatorWording::isPlain(label->text()), qPrintable(label->text()));
                ++checked;
            }
        }
        QVERIFY2(checked >= 1, qPrintable(QString::number(checked)));
    }
};

QTEST_MAIN(TestOperatorWordingSweep)
#include "tst_operator_wording_sweep.moc"
