#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/gui/multidevice/MultiDeviceController.h  (NereusSDR)
// =================================================================
//
// The remote window's half of several devices on one Core (iPhone app plan
// Task 78, R-IOS-02, R-IOS-30; the several-devices design, section 12).
// One per remote window, owned by MainWindow. It turns what the Core says
// (StationClient::remoteDevices(): the question, the notices, the markers,
// txState's holder) into the window's screens, and the operator's answers
// into the Core's verbs:
//
//   askTakeTransmit()   "Take transmit from <short name>?" from txState,
//                       then tx.take with what was shown (ruling 8.7)
//   confirm.request     takeTransmit: the same question from its holder;
//                       sharedSetting / panMove: ConfirmChangeDialog;
//                       takeReceiver / takeSlice: TakeReceiverDialog;
//                       each answers confirm.proceed or confirm.cancel
//   notice              a NoticeCard on the band, Take it back when offered
//   session.held        the Core is full: ReplaceDeviceDialog, answered
//                       with session.takeover (Task 78 item 7, G-53)
//   markers             foreignMarkers() for each panadapter
//
// Nothing here keys the radio: a take never keys (the link document,
// section 18.9); the operator presses MOX after it.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 78 (R-IOS-02, R-IOS-30), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-28: the fifth-device choice (Task 78 item 7, G-53). J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "gui/SpectrumWidget.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

class QDialog;
class QWidget;

namespace NereusSDR {

class NoticeCard;
class RemoteDevicesState;
class StationClient;

class MultiDeviceController : public QObject {
    Q_OBJECT

public:
    MultiDeviceController(StationClient* client, QWidget* dialogParent,
                          QObject* parent = nullptr);
    ~MultiDeviceController() override;

    /// Where notice cards go (the band: the active panadapter). Cards move
    /// with it.
    void setNoticeHost(QWidget* host);
    QWidget* noticeHost() const { return m_noticeHost; }
    /// Re-place the cards (the host was resized).
    void layoutNoticeCards();

    /// The operator asked to take transmit (the pan's TAKE TX pill, the TX
    /// applet's button). Asks first; nothing when transmit is not held
    /// elsewhere or this window cannot take it.
    void askTakeTransmit();

    /// Other devices' slices, as a panadapter draws them.
    static QVector<SpectrumWidget::ForeignSliceMarker> foreignMarkers(
        const RemoteDevicesState& devices);

    /// The dialog open now (a question, or the window's own take question),
    /// or null.
    QDialog* openDialog() const { return m_dialog.data(); }
    QList<NoticeCard*> noticeCards() const;

signals:
    /// A refusal to show the operator (a take, an answer, Take it back).
    void refusal(const QString& reason);
    /// tx.take was accepted: this window holds transmit.
    void transmitTaken();
    /// The markers changed.
    void markersChanged();

private:
    void onQuestionChanged();
    void onHeldChanged();
    void onNoticesChanged();
    void onCommandFinished(const QByteArray& verb, quint32 commandId, bool accepted,
                           const QString& reason, bool awaitingConfirmation);
    void showDialog(QDialog* dialog);
    void closeDialogQuietly();

    QPointer<StationClient> m_client;
    QPointer<QWidget> m_dialogParent;
    QPointer<QWidget> m_noticeHost;
    QPointer<QDialog> m_dialog;
    /// The question the open dialog answers (0 for the window's own ask).
    qint64 m_dialogQuestionId = 0;
    /// The open dialog is the fifth-device choice.
    bool m_dialogIsHeld = false;
    QHash<qint64, QPointer<NoticeCard>> m_cards;
};

} // namespace NereusSDR
