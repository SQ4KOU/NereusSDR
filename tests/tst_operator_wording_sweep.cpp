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
#include <QGroupBox>
#include <QLabel>

#include "OperatorWording.h"
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
        for (const QString& text : shownText(full)) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        }
        // A refusal in the Core's terms reaches the panel in user words.
        slice.reportNnrEditResult(QStringLiteral("This station session does not support NNR controls."));
        const auto* status = full.findChild<QLabel*>(QStringLiteral("nnrStatusReadback"));
        QVERIFY(status);
        QVERIFY2(status->text().contains(
                     QStringLiteral("This Core does not offer NNR controls to this app.")),
                 qPrintable(status->text()));
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
        for (const QLabel* label : page.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("4O3A"))
                || label->text().startsWith(QStringLiteral("Status:"))) {
                QVERIFY2(OperatorWording::isPlain(label->text()), qPrintable(label->text()));
            }
        }
    }
};

QTEST_MAIN(TestOperatorWordingSweep)
#include "tst_operator_wording_sweep.moc"
