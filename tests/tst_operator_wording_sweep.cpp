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
#include "core/WdspTypes.h"
#include "core/dsp/NnrSettings.h"
#include "core/safety/BandPlanGuard.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "gui/OperatorReasonText.h"
#include "gui/RemoteAudioStatus.h"
#include "gui/RemoteMediaController.h"
#include "gui/applets/TxApplet.h"
#include "gui/setup/FourO3APage.h"
#include "gui/widgets/NnrControls.h"
#include "models/Band.h"
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

// A source file's code with its comments blanked and adjacent string
// literals joined ("a" "b" -> "ab"), so only text the program can produce
// is left. Literals (plain, raw and character) are kept as written; a
// comment that quotes a reason cannot stand in for the code that sends it.
QString codeWithoutComments(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    QString code;
    code.reserve(text.size());
    const qsizetype n = text.size();
    const auto identifierChar = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    qsizetype i = 0;
    qsizetype lastLiteralEnd = -1;  // Just past the last string literal's closing quote.
    while (i < n) {
        const QChar c = text.at(i);
        const QChar next = i + 1 < n ? text.at(i + 1) : QChar();
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            while (i < n && text.at(i) != QLatin1Char('\n')) {
                ++i;
            }
            code += QLatin1Char(' ');
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            const qsizetype end = text.indexOf(QStringLiteral("*/"), i + 2);
            i = end < 0 ? n : end + 2;
            code += QLatin1Char(' ');
            continue;
        }
        if (c == QLatin1Char('"') && i > 0 && text.at(i - 1) == QLatin1Char('R')
            && (i < 2 || !identifierChar(text.at(i - 2))
                || text.at(i - 2) == QLatin1Char('u') || text.at(i - 2) == QLatin1Char('L')
                || text.at(i - 2) == QLatin1Char('8'))) {
            // A raw literal: R"delim( ... )delim", copied as written.
            const qsizetype open = text.indexOf(QLatin1Char('('), i + 1);
            if (open < 0) {
                code += text.mid(i);
                break;
            }
            const QString close = QLatin1Char(')') + text.mid(i + 1, open - i - 1)
                + QLatin1Char('"');
            const qsizetype end = text.indexOf(close, open + 1);
            const qsizetype stop = end < 0 ? n : end + close.size();
            code += text.mid(i, stop - i);
            i = stop;
            lastLiteralEnd = -1;
            continue;
        }
        const bool charLiteral = c == QLatin1Char('\'')
            && (i == 0 || !text.at(i - 1).isLetterOrNumber());
        if (c == QLatin1Char('"') || charLiteral) {
            const qsizetype start = i;
            ++i;
            while (i < n && text.at(i) != c && text.at(i) != QLatin1Char('\n')) {
                i += text.at(i) == QLatin1Char('\\') ? 2 : 1;
            }
            i = qMin(i + 1, n);
            QString literal = text.mid(start, i - start);
            if (!charLiteral && lastLiteralEnd >= 0
                && code.mid(lastLiteralEnd).trimmed().isEmpty()) {
                // "a" "b" reads as "ab", as the compiler joins them.
                code.truncate(lastLiteralEnd - 1);
                literal.remove(0, 1);
            }
            code += literal;
            lastLiteralEnd = charLiteral ? -1 : code.size();
            continue;
        }
        if (!c.isSpace()) {
            lastLiteralEnd = -1;
        }
        code += c;
        ++i;
    }
    return code;
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

// Developer wording a user must never read (R-R3-17, R-R3-21): roadmap phase
// and task numbers, design-document references, environment variables,
// internal class and function names, and bug-tracker notes. The em dash is
// checked separately, on the strings this sweep names.
const QRegularExpression& developerWording()
{
    static const QRegularExpression pattern(QStringLiteral(
        "\\bPhase \\d|\\bTask \\d|\\b\\d[A-Z]-\\d|design spec|\u00a7|\\\\u00a7"
        "|\\bQT_[A-Z_]+=|Feature request|\\b(follow-up|future|later) phase"
        "|\\b[A-Z][a-z]+[A-Z][A-Za-z]*(Dialog|Toast|Widget|Applet|Page|Controller|Model)\\b"
        "|addSliceOnPan"));
    return pattern;
}

// The first piece of developer wording in `text`, an em dash included, or an
// empty string.
QString developerWordingIn(const QString& text)
{
    const QRegularExpressionMatch match = developerWording().match(text);
    if (match.hasMatch()) {
        return match.captured(0);
    }
    if (text.contains(QChar(0x2014)) || text.contains(QLatin1String("\\u2014"))) {
        return QStringLiteral("em dash");
    }
    return {};
}

// The string literals a user can read in a source file: codeLiterals()
// without the ones inside a log statement (qDebug / qInfo / qWarning /
// qCritical and their qC* forms, up to the statement's ';'), which only the
// log shows. Placeholders on the operator's decision list (the "NYI"
// strings) are left to the unfinished-controls plan.
QStringList userVisibleLiterals(const QString& path)
{
    static const QRegularExpression token(QStringLiteral(
        "(\"(?:[^\"\\\\\\n]|\\\\.)*\")|(;)|(\\bqC?(?:Debug|Info|Warning|Critical|Fatal)\\b)"));
    QString code;
    for (const QString& line : codeWithoutComments(path).split(QLatin1Char('\n'))) {
        if (!line.trimmed().startsWith(QLatin1Char('#'))) {
            code += line + QLatin1Char('\n');
        }
    }
    QStringList found;
    bool inLog = false;
    QRegularExpressionMatchIterator it = token.globalMatch(code);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (!m.captured(3).isEmpty()) {
            inLog = true;
        } else if (!m.captured(2).isEmpty()) {
            inLog = false;
        } else if (!inLog) {
            const QString text = m.captured(1).mid(1, m.captured(1).size() - 2);
            if (!text.contains(QLatin1String("NYI"))) {
                found.append(text);
            }
        }
    }
    return found;
}

// Every string literal in a source file's code: comments and preprocessor
// lines left out, adjacent literals joined, escapes kept as written.
QStringList codeLiterals(const QString& path)
{
    static const QRegularExpression literal(QStringLiteral("\"((?:[^\"\\\\\\n]|\\\\.)*)\""));
    QString code;
    for (const QString& line : codeWithoutComments(path).split(QLatin1Char('\n'))) {
        if (!line.trimmed().startsWith(QLatin1Char('#'))) {
            code += line + QLatin1Char('\n');
        }
    }
    QStringList found;
    QRegularExpressionMatchIterator it = literal.globalMatch(code);
    while (it.hasNext()) {
        found.append(it.next().captured(1));
    }
    return found;
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
        // translated only when shown. The source retune is in operator words
        // since R-IOS-01; an older Core's words still show as before.
        QCOMPARE(QString::fromLatin1(kRetireReasonSliceRemoved), QStringLiteral("slice removed"));
        QCOMPARE(QString::fromLatin1(kRetireReasonStreamBindingChanged),
                 QStringLiteral("slice stream binding changed"));
        QCOMPARE(QString::fromLatin1(kRetireReasonSourceRetune),
                 QStringLiteral("The receiver was retuned away from this view."));
        QCOMPARE(OperatorReasonText::forDisplay(
                     QStringLiteral("source retune no longer covers requested crop")),
                 OperatorReasonText::forDisplay(QString::fromLatin1(kRetireReasonSourceRetune)));
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

    // R-R3-43: why a receiver's audio for an app stopped. Every wire
    // reason a receiver audio context can carry reads in user words (never
    // as sent), this computer's own reason for an older Core is plain as it
    // is, and the remote audio status lines for receiver streams are plain.
    void receiverAudioReasonsReadInUserWords()
    {
        for (RemoteAudioOffReason reason :
             {RemoteAudioOffReason::ClientDisabled, RemoteAudioOffReason::MediaNotReady,
              RemoteAudioOffReason::RadioOffline, RemoteAudioOffReason::EncoderUnavailable,
              RemoteAudioOffReason::SliceRemoved, RemoteAudioOffReason::ReceiverLimit}) {
            const QString wire = remoteAudioOffReasonToWire(reason);
            QVERIFY(OperatorReasonText::knownReasons().contains(wire));
            const QString sentence = OperatorReasonText::forDisplay(wire);
            QVERIFY2(OperatorWording::isPlain(sentence), qPrintable(wire + " -> " + sentence));
            QVERIFY2(sentence != wire, qPrintable(wire));
        }
        QCOMPARE(OperatorReasonText::forDisplay(QStringLiteral("slice-removed")),
                 QStringLiteral("This receiver is no longer on the Core."));
        QCOMPARE(OperatorReasonText::forDisplay(QStringLiteral("receiver-limit")),
                 QStringLiteral("The Core is already sending audio for as many receivers as it "
                                "can. Stop the audio for another receiver to hear this one."));
        const QString older = QString::fromLatin1(RemoteMediaController::kReceiverAudioUnavailableReason);
        QVERIFY(OperatorWording::isPlain(older));
        QCOMPARE(OperatorReasonText::forDisplay(older), older);

        RemoteAudioStatus status;
        status.state = RemoteAudioStatus::State::MutedHere;
        status.selectedOutput = QStringLiteral("System default");
        RemoteReceiverAudioStatus receiving;
        receiving.sliceId = 0;
        receiving.state = RemoteReceiverAudioStatus::State::Receiving;
        receiving.runningProfile = RemoteAudioProfile::Lossless;
        RemoteReceiverAudioStatus waiting;
        waiting.sliceId = 1;
        RemoteReceiverAudioStatus stopped;
        stopped.sliceId = 2;
        stopped.state = RemoteReceiverAudioStatus::State::Stopped;
        stopped.stopReason = QStringLiteral("receiver-limit");
        status.receivers = {receiving, waiting, stopped};
        RemoteAudioReceiverTelemetry measured;
        measured.arrivalJitterMs = 2.4;
        measured.expectedPackets = 1250;
        measured.missingPackets = 3;
        measured.concealedPackets = 3;
        const QString details = formatRemoteAudioDetails(status, {}, {}, {{0, measured}});
        QVERIFY2(details.endsWith(QStringLiteral(
                     "Receiver A for apps: Receiving, Lossless\n"
                     "Receiver A: arrival jitter 2\u00A0ms, missing packets 3 of 1250, gaps filled 3\n"
                     "Receiver B for apps: Waiting for the Core\n"
                     "Receiver C for apps: Stopped. The Core is already sending audio for as many "
                     "receivers as it can. Stop the audio for another receiver to hear this one.")),
                 qPrintable(details));
        for (const QString& line : details.split(QLatin1Char('\n'))) {
            QVERIFY2(OperatorWording::isPlain(line), qPrintable(line));
        }
        // With no receiver streams the section reads as before.
        status.receivers.clear();
        QVERIFY(!formatRemoteAudioDetails(status, {}).contains(QStringLiteral("Receiver")));
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
              // R-R3-46: Protocol Info shows the Core's radio once this
              // window is connected, and says so until then.
              "Unavailable until this window is connected to the Core.",
              "Show the Core's radio: its name, firmware, MAC and network address",
              // R-R3-46 / R-R3-21: the attenuator rows before a Core is there.
              "Connect to the Core to change the attenuator and preamp.",
              "Wait until this window has the Core's settings before changing the pan layout.",
              "Link to the Core lost: %1", "Audio and display: %1"}) {
            QVERIFY2(mainWindow.contains(QLatin1String(line)), line);
        }
    }

    // Fix wave M7: every exact reason the table translates is still written,
    // byte for byte, somewhere in the sources other than the table itself.
    // A Core or app rewording then fails here instead of silently showing
    // the general sentence. Each key must be a whole string literal in code
    // (comments do not count; adjacent literals are joined), so a key that
    // is only part of a longer reason, or only quoted in a comment, is not
    // mistaken for the reason itself. The media-start prefix is the start of
    // a literal the transport's words are added to.
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
            sources += codeWithoutComments(path);
            sources += QLatin1Char('\n');
            ++files;
        }
        QVERIFY2(files >= 500, qPrintable(QString::number(files)));
        const QStringList keys = OperatorReasonText::tableKeys();
        // R-IOS-01 moved the Core's retired words to olderCoreKeys().
        QVERIFY2(keys.size() >= 110, qPrintable(QString::number(keys.size())));
        const QString mediaStartPrefix = keys.constLast();
        QVERIFY(mediaStartPrefix.startsWith(QLatin1String("Station media could not start")));
        QStringList missing;
        for (const QString& key : keys) {
            const QString literal = key == mediaStartPrefix
                ? QLatin1Char('"') + key
                : QLatin1Char('"') + key + QLatin1Char('"');
            if (!sources.contains(literal)) {
                missing.append(key);
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(" | "))));

        // R-IOS-01: the reasons only an older Core sends are gone from the
        // sources (the Core words them in operator words now), and each
        // still shows in user words.
        const QStringList older = OperatorReasonText::olderCoreKeys();
        QVERIFY2(older.size() >= 80, qPrintable(QString::number(older.size())));
        QStringList stillWritten;
        for (const QString& key : older) {
            QVERIFY2(!keys.contains(key), qPrintable(key));
            if (sources.contains(QLatin1Char('"') + key + QLatin1Char('"'))) {
                stillWritten.append(key);
            }
            const QString shown = OperatorReasonText::forDisplay(key);
            QVERIFY2(shown != key && OperatorWording::isPlain(shown), qPrintable(key));
        }
        QVERIFY2(stillWritten.isEmpty(), qPrintable(stillWritten.join(QStringLiteral(" | "))));
    }

    // R3 post-merge follow-ups (R-R3-17, R-R3-21): this app's own "does not
    // support" refusals, the NNR adapter's reasons and the Core's Tuner
    // Genius XL checks are read from their sources and each is shown in user
    // words that name the Core, never the station or PS3. A new or reworded
    // one there fails here until the table has it.
    void refusalsAndTunerChecksReadInUserWordsAtTheSource()
    {
        static const QRegularExpression literal(
            QStringLiteral("QStringLiteral\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\)"));
        static const QRegularExpression placeholder(QStringLiteral("%[0-9]"));
        static const QRegularExpression stationWord(
            QStringLiteral("\\bstation\\b|\\bPS3\\b|does not support"),
            QRegularExpression::CaseInsensitiveOption);
        const auto reasonsIn = [](const char* file, const QRegularExpression& keep) {
            QStringList found;
            const QString code = codeWithoutComments(sourcePath(file));
            QRegularExpressionMatchIterator it = literal.globalMatch(code);
            while (it.hasNext()) {
                const QString text = it.next().captured(1);
                if (keep.match(text).hasMatch() && !found.contains(text)) {
                    found.append(text);
                }
            }
            return found;
        };
        const struct {
            const char* file;
            const char* keep;
            int atLeast;
        } sites[] = {
            {"src/core/session/StationClient.cpp",
             "^(The station does not support |This station cannot )", 7},
            {"src/core/dsp/NnrAdapter.cpp", ".", 8},
            {"src/core/TgxlConnection.cpp", "^TGXL [a-z]+ .* ", 6},
            {"src/core/StationTgxlController.cpp",
             "^(Expected TunerGenius|No matching TGXL)", 2},
        };
        for (const auto& site : sites) {
            const QStringList reasons =
                reasonsIn(site.file, QRegularExpression(QString::fromLatin1(site.keep)));
            QVERIFY2(reasons.size() >= site.atLeast,
                     qPrintable(QStringLiteral("%1: %2 reasons")
                                    .arg(QLatin1String(site.file)).arg(reasons.size())));
            for (QString reason : reasons) {
                // Worded the way the site fills it in.
                reason.replace(placeholder, QStringLiteral("7"));
                const QString shown = OperatorReasonText::forDisplay(reason);
                QVERIFY2(shown != reason, qPrintable(reason));
                QVERIFY2(shown != QLatin1String("The reason is in the log."), qPrintable(reason));
                QVERIFY2(OperatorWording::isPlain(shown), qPrintable(reason + QStringLiteral(" -> ") + shown));
                QVERIFY2(!stationWord.match(shown).hasMatch(), qPrintable(reason + QStringLiteral(" -> ") + shown));
            }
        }
    }

    // R3 controls that work, Task 3 (R-R3-17, R-R3-21): the developer
    // wording users could read outside the unfinished-control placeholders
    // (transmit refusals, test-entry tooltips, the antenna conflict policy
    // choices, the Speech Processor tag, the TCI slice note, the VAX consumer
    // row, the overlay's IQ channel, the log view and the RF-Kit tuner
    // buttons) is gone and its replacement is in user words. Files whose
    // other strings are all plain are scanned whole, so new developer wording
    // there fails too; MainWindow and the VAX page still carry placeholders
    // the unfinished-controls plan retires, so only their named strings are
    // checked.
    void developerWordingIsGoneOutsideThePlaceholders()
    {
        const struct {
            const char* file;
            bool wholeFile;
            QStringList gone;
            QStringList shown;
        } sites[] = {
            {"src/core/safety/BandPlanGuard.cpp", true,
             {"coming in Phase", "RX/TX band mismatch \u2014"},
             {"CW transmit is not available yet", "FM transmit is not available yet",
              "DRM transmit is not available yet",
              "RX/TX band mismatch: cross-band TX disabled"}},
            {"src/gui/applets/TxApplet.cpp", true,
             {"coming in Phase"},
             {"CW transmit is not available yet", "FM transmit is not available yet",
              "DRM transmit is not available yet"}},
            {"src/gui/MainWindow.cpp", false,
             {"Phase 3F closeout:", "AntennaSwitchToast surface",
              "TxBoundConfirmDialog surface", "conflict-detection state machine ships"},
             {"Show the antenna switch notice to see how it looks. No antenna changes, "
              "and antennas do not switch on their own yet.",
              "Show the question asked before the transmit antenna moves, to see how it "
              "looks. No antenna changes, and adding a slice does not ask it yet."}},
            {"src/gui/setup/hardware/AntennaAlexAntennaControlTab.cpp", true,
             {"TxBoundConfirmDialog before", "toast on RX-only switch", "refuse add-slice"},
             {"Auto - resolve it when safe, and show a notice when only a receive antenna "
              "changes",
              "Warn - ask before moving the transmit antenna",
              "Block - do not add the slice while its antenna is in use by another slice"}},
            {"src/gui/setup/TransmitSetupPages.cpp", true, {"3M-3a-iii"}, {}},
            {"src/gui/setup/AudioTciPage.cpp", true,
             {"Phase 3J-1"}, {"Slices C and D are not available over TCI."}},
            {"src/gui/setup/AudioVaxPage.cpp", false,
             {"Task 24+", "override \u2014 no consumer", "no consumer"},
             {"Whether an app such as WSJT-X has this VAX channel open.",
              "No program is using this device"}},
            {"src/gui/SpectrumOverlayPanel.cpp", true,
             {"design spec", "reserved for future phase"},
             {"Sending I/Q to a VAX channel is not available yet."}},
            {"src/gui/diagnostics/DiagnosticsPhaseHPages.cpp", true,
             {"QT_LOGGING_TO_CONSOLE", "follow-up phase"},
             {"The 60 s history graph is not shown yet.",
              "Exporting one radio's settings is not available yet. Use 'Export All "
              "Settings' for now."}},
            // Fix wave item 2: the VFO flag's tooltips carried Thetis and
            // WDSP file cites and WDSP function names; the cites are
            // comments beside the strings now.
            // Follow-up: the HL2 throttle notice named the ep2 stream.
            {"src/core/P1RadioConnection.cpp", false,
             {"LAN throttled; pausing ep2"},
             {"The Hermes Lite 2 asked for a pause because its network link is busy."}},
            {"src/gui/widgets/VfoWidget.cpp", false,
             {"From Thetis", "patchpanel.c", "(SetRXAPanelRun)", "(SetRXAPanelBinaural)",
              "Maps to Thetis", "Alex.cs", "nob.c", "matches AetherSDR",
              "NNR: WDSP neural"},
             {"Audio pan: left/right stereo balance (\u2212100 = full left, 0 = center, "
              "+100 = full right)",
              "Mute the receive audio",
              "Binaural audio: I and Q play in separate ears, for a stereo image in headphones",
              "RX Bypass on TX: routes the receive path through the bypass relay while "
              "transmitting.",
              "NNR: neural noise reduction. Left-click activates, right-click adjusts settings"}},
            {"src/gui/applets/Rf2ksApplet.cpp", true,
             {"G200C267", "Feature request", "tuner write verb"},
             {"The amplifier's firmware does not let NereusSDR tune or bypass it.\\n"
              "Press TUNE or BYPASS on the amplifier's front panel.\\n"
              "These buttons turn on when the amplifier's firmware allows it."}},
        };
        for (const auto& site : sites) {
            const QStringList literals = codeLiterals(sourcePath(site.file));
            QVERIFY2(literals.size() >= 5, site.file);
            const QString code = literals.join(QLatin1Char('\n'));
            for (const QString& old : site.gone) {
                QVERIFY2(!code.contains(old),
                         qPrintable(QStringLiteral("%1 still shows: %2")
                                        .arg(QLatin1String(site.file), old)));
            }
            for (const QString& text : site.shown) {
                QVERIFY2(literals.contains(text),
                         qPrintable(QStringLiteral("%1 lacks: %2")
                                        .arg(QLatin1String(site.file), text)));
                QVERIFY2(OperatorWording::isPlain(text),
                         qPrintable(text + QStringLiteral(" [")
                                    + OperatorWording::internalTermIn(text)
                                    + QLatin1Char(']')));
                QVERIFY2(developerWordingIn(text).isEmpty(),
                         qPrintable(text + QStringLiteral(" [") + developerWordingIn(text)
                                    + QLatin1Char(']')));
            }
            if (!site.wholeFile) {
                continue;
            }
            for (const QString& text : literals) {
                QVERIFY2(!developerWording().match(text).hasMatch(),
                         qPrintable(QStringLiteral("%1: %2")
                                        .arg(QLatin1String(site.file), text)));
            }
        }
    }

    // Fix wave item 12: no user-visible string in the files the R3 controls
    // plan touched carries an em dash; prose uses periods, colons,
    // semicolons, parentheses or commas, and a lone empty-value mark in a
    // readout is an en dash. Log lines and the decision-list placeholders
    // are not read by the operator here and are left out.
    void noEmDashInUserVisibleStrings()
    {
        const char* files[] = {
            "src/gui/AboutDialog.cpp",
            "src/gui/MainWindow.cpp",
            "src/gui/SpectrumOverlayPanel.cpp",
            "src/gui/SpotHubDialog.cpp",
            "src/gui/applets/PhoneCwApplet.cpp",
            "src/gui/applets/TxApplet.cpp",
            "src/gui/diagnostics/DiagnosticsPhaseHPages.cpp",
            "src/gui/diagnostics/RadioStatusPage.cpp",
            "src/gui/setup/AudioVaxPage.cpp",
            "src/gui/setup/DspOptionsPage.cpp",
            "src/gui/setup/DspSetupPages.cpp",
            "src/gui/setup/TransmitSetupPages.cpp",
            "src/gui/setup/hardware/OcOutputsHfTab.cpp",
            "src/gui/widgets/VfoWidget.cpp",
            "src/gui/widgets/RxDashboard.cpp",
            "src/models/RadioModel.cpp",
            "src/core/P1RadioConnection.cpp",
        };
        for (const char* file : files) {
            const QStringList literals = userVisibleLiterals(sourcePath(file));
            QVERIFY2(literals.size() >= 5, file);
            for (const QString& text : literals) {
                QVERIFY2(!text.contains(QChar(0x2014)) && !text.contains(QLatin1String("\\u2014")),
                         qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(file), text)));
            }
        }
        // The readouts' empty-value mark is the en dash.
        const QStringList status = userVisibleLiterals(
            sourcePath("src/gui/diagnostics/RadioStatusPage.cpp"));
        QVERIFY(status.contains(QString(QChar(0x2013))));
        QVERIFY(status.contains(QString(QChar(0x2013)) + QStringLiteral(" W")));
    }

    // Fix wave items 2 and 12: the strings rewritten without em dashes or
    // developer wording read in user words.
    void rewordedStringsArePlain()
    {
        const struct { const char* file; QStringList texts; } sites[] = {
            {"src/gui/MainWindow.cpp",
             {"PA status: OK", "PA status: FAULT (the PA tripped and MOX dropped)",
              "NereusSDR: FFTW Wisdom"}},
            {"src/gui/SpotHubDialog.cpp",
             {"Enter Callsign Here (4-12 characters, required to publish spots)",
              "Enter Grid Here (Maidenhead, e.g. EM73 or EM73XY)"}},
            {"src/gui/applets/TxApplet.cpp",
             {"TX Leveler: slow speech-leveling AGC. Improves intelligibility on weak speech."}},
            {"src/gui/diagnostics/DiagnosticsPhaseHPages.cpp",
             {"\u2713 No issues: every setting is within this radio's range.", "[%1] %2: %3"}},
            {"src/gui/diagnostics/RadioStatusPage.cpp",
             {"No issues found. Settings are valid.", "[%1] %2: %3"}},
            {"src/gui/setup/AudioVaxPage.cpp",
             {"No program is using this device", "\u26a0  Not bound. Pick a virtual cable",
              "\u26a0  Disabled. Enable it to route audio"}},
            {"src/gui/setup/DspOptionsPage.cpp",
             {"Sets the internal buffer size. Larger values give sharper filters but add latency.",
              "Time to last change: none yet"}},
            {"src/gui/setup/hardware/OcOutputsHfTab.cpp",
             {"OC pin %1: shows the last OC byte sent to the radio"}},
            {"src/models/RadioModel.cpp",
             {"Band %1 ignored: the slice is locked. Unlock it to change bands."}},
            {"src/core/P1RadioConnection.cpp",
             {"No response from radio within %1 ms. Check the IP address, radio power and "
              "network.",
              "The Hermes Lite 2 asked for a pause because its network link is busy."}},
        };
        for (const auto& site : sites) {
            const QStringList literals = userVisibleLiterals(sourcePath(site.file));
            for (const QString& written : site.texts) {
                const QString text = written;
                QVERIFY2(literals.contains(text),
                         qPrintable(QStringLiteral("%1 lacks: %2")
                                        .arg(QLatin1String(site.file), text)));
                QVERIFY2(OperatorWording::isPlain(text),
                         qPrintable(text + QStringLiteral(" [")
                                    + OperatorWording::internalTermIn(text) + QLatin1Char(']')));
                QVERIFY2(developerWordingIn(text).isEmpty(),
                         qPrintable(text + QStringLiteral(" [") + developerWordingIn(text)
                                    + QLatin1Char(']')));
            }
        }
    }

    // The same task: every transmit refusal and MOX tooltip, for every mode,
    // is plain, and the DRM one names DRM rather than FM.
    void transmitRefusalsNameTheModeInUserWords()
    {
        using safety::BandPlanGuard;
        BandPlanGuard guard;
        const auto region = safety::Region::UnitedStates;
        constexpr std::int64_t kInBandHz = 14'200'000;  // US 20 m
        QStringList shown;
        for (int m = int(DSPMode::LSB); m <= int(DSPMode::RADE_L); ++m) {
            const auto mode = DSPMode(m);
            shown << guard.checkMoxAllowed(region, kInBandHz, mode, Band::Band20m,
                                           Band::Band20m, false, false).reason
                  << TxApplet::tooltipForMode(mode);
        }
        shown << guard.checkMoxAllowed(region, 14'500'000, DSPMode::USB, Band::Band20m,
                                       Band::Band20m, false, false).reason
              << guard.checkMoxAllowed(region, kInBandHz, DSPMode::USB, Band::Band20m,
                                       Band::Band40m, true, false).reason;
        shown.removeAll(QString());
        QVERIFY2(shown.size() >= 16, qPrintable(QString::number(shown.size())));
        for (const QString& text : std::as_const(shown)) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
            QVERIFY2(developerWordingIn(text).isEmpty(),
                     qPrintable(text + QStringLiteral(" [") + developerWordingIn(text)
                                + QLatin1Char(']')));
        }

        const QString drmRefusal = guard.checkMoxAllowed(region, kInBandHz, DSPMode::DRM,
                                                         Band::Band20m, Band::Band20m,
                                                         false, false).reason;
        const QString drmTip = TxApplet::tooltipForMode(DSPMode::DRM);
        static const QRegularExpression fm(QStringLiteral("\\bFM\\b"));
        for (const QString& text : {drmRefusal, drmTip}) {
            QVERIFY2(text.contains(QLatin1String("DRM")), qPrintable(text));
            QVERIFY2(!fm.match(text).hasMatch(), qPrintable(text));
        }
        QVERIFY(guard.checkMoxAllowed(region, kInBandHz, DSPMode::FM, Band::Band20m,
                                      Band::Band20m, false, false)
                    .reason.contains(QLatin1String("FM")));
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
