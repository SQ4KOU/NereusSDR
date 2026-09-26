#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/TransmitStateFacade.h  (NereusSDR)
// =================================================================
//
// The mirrored `txState` object, class TransmitState (iPhone app plan
// Task 39; D14, R-IOS-13, R-IOS-21; spec section 5.5 items 5 and 8): what
// the Core's transmitter is doing, for a remote window and the phone. Every
// property is outbound (station to client). StationServer sends it only at
// agreed minor 11 to a peer whose hello declared remoteTx 1 (txStateVersion
// 1, after remoteTxVersion), so an older peer never receives it.
//
//   keyed                  the radio is on the air (RadioModel
//                          transmitting: MOX, TUNE or two-tone)
//   tuning, twoTone        TUNE / the two-tone test is on
//   txSliceId              the slice transmit is bound to (-1: none)
//   keyedByName, keyedByKind, keyedTrigger
//                          who keyed and how (RadioModel::keyedBy: the
//                          device's name as the Core numbers it, its kind,
//                          its trigger); empty while unkeyed
//   keyedSinceMs           when the key began, on the Core's monotonic
//                          clock in milliseconds; 0 while unkeyed
//   timeOutRemainingSeconds
//                          whole seconds before the transmit time-out stops
//                          the key (Task 38), -1 when none applies
//   forwardPowerWatts, reflectedPowerWatts, swr, alcDb, micLevelDb
//                          the transmit meters (TxMeterPump)
//   txEnding               true only during a RADE end-of-over tail; no
//                          tail is built yet, so always false
//   stopReason, stopText   why the Core last stopped a transmission on its
//                          own, and that in the operator's words:
//                          "" (none yet), linkLost, micStarved, timeOut,
//                          takenOver, revoked or station
//   stopSerial             advances by one with each such stop
//
// Updates: while keyed the meters are read ten times a second (the
// transmit lane's cached readings; never a WDSP call on the event loop)
// and a reading that changed is sent; the time left is read with them.
// While unkeyed only changes are sent (the power meters follow the radio's
// power readings as they change).
//
// Stops: each key (a rising edge of `keyed`) can be stopped once. The
// first reason recorded for it wins and advances stopSerial; a later one
// for the same key changes nothing. The Core's own StopAllTx without a
// reason recorded for it counts as `station`, decided after the reasons
// raised with it (the time-out raises its own just after StopAllTx).
//
// On a remote window the object holds the Core's values as it last heard
// them (applyStationValue); nothing on it is ever written back.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 39 (D14, R-IOS-13,
//               R-IOS-21), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

#include <functional>

#include "core/meters/TxMeterPump.h"

namespace NereusSDR {

class RadioModel;

class TransmitState final : public QObject {
    Q_OBJECT
    // The wire's order (the plan's Task 39 interface). Do not reorder.
    Q_PROPERTY(bool keyed READ keyed NOTIFY stateChanged)
    Q_PROPERTY(bool tuning READ tuning NOTIFY stateChanged)
    Q_PROPERTY(bool twoTone READ twoTone NOTIFY stateChanged)
    Q_PROPERTY(int txSliceId READ txSliceId NOTIFY stateChanged)
    Q_PROPERTY(QString keyedByName READ keyedByName NOTIFY stateChanged)
    Q_PROPERTY(QString keyedByKind READ keyedByKind NOTIFY stateChanged)
    Q_PROPERTY(QString keyedTrigger READ keyedTrigger NOTIFY stateChanged)
    Q_PROPERTY(qint64 keyedSinceMs READ keyedSinceMs NOTIFY stateChanged)
    Q_PROPERTY(int timeOutRemainingSeconds READ timeOutRemainingSeconds NOTIFY timeOutChanged)
    Q_PROPERTY(double forwardPowerWatts READ forwardPowerWatts NOTIFY metersChanged)
    Q_PROPERTY(double reflectedPowerWatts READ reflectedPowerWatts NOTIFY metersChanged)
    Q_PROPERTY(double swr READ swr NOTIFY metersChanged)
    Q_PROPERTY(double alcDb READ alcDb NOTIFY metersChanged)
    Q_PROPERTY(double micLevelDb READ micLevelDb NOTIFY metersChanged)
    Q_PROPERTY(bool txEnding READ txEnding NOTIFY stateChanged)
    Q_PROPERTY(QString stopReason READ stopReason NOTIFY stopChanged)
    Q_PROPERTY(QString stopText READ stopText NOTIFY stopChanged)
    Q_PROPERTY(quint32 stopSerial READ stopSerial NOTIFY stopChanged)

public:
    // The link's stopReason values.
    static constexpr const char* kStopLinkLost = "linkLost";
    static constexpr const char* kStopMicStarved = "micStarved";
    static constexpr const char* kStopTimeOut = "timeOut";
    static constexpr const char* kStopTakenOver = "takenOver";
    static constexpr const char* kStopRevoked = "revoked";
    static constexpr const char* kStopStation = "station";

    /// Milliseconds on a monotonic clock.
    using Clock = std::function<qint64()>;

    /// Unbound: a remote window's copy, or a Core's before bind().
    explicit TransmitState(QObject* parent = nullptr);
    ~TransmitState() override;

    /// The Core's: follows `model` (a model with its own radio) from now
    /// on. Once only.
    void bind(RadioModel* model);
    /// Stops following the model (its owner is going away): no more
    /// signals from it, the pump stopped, the clock dropped.
    void unbind();
    /// The clock keyedSinceMs reads (the Core's device clock). The default
    /// is a monotonic timer started with this object.
    void setClock(Clock clock);
    /// The meters' pump (a child of this object).
    TxMeterPump* meterPump() const { return m_pump; }

    bool keyed() const { return m_keyed; }
    bool tuning() const { return m_tuning; }
    bool twoTone() const { return m_twoTone; }
    int txSliceId() const { return m_txSliceId; }
    QString keyedByName() const { return m_keyedByName; }
    QString keyedByKind() const { return m_keyedByKind; }
    QString keyedTrigger() const { return m_keyedTrigger; }
    qint64 keyedSinceMs() const { return m_keyedSinceMs; }
    int timeOutRemainingSeconds() const { return m_timeOutRemainingSeconds; }
    double forwardPowerWatts() const { return m_meters.forwardPowerWatts; }
    double reflectedPowerWatts() const { return m_meters.reflectedPowerWatts; }
    double swr() const { return m_meters.swr; }
    double alcDb() const { return m_meters.alcDb; }
    double micLevelDb() const { return m_meters.micLevelDb; }
    TxMeterReadings meters() const { return m_meters; }
    bool txEnding() const { return m_txEnding; }
    QString stopReason() const { return m_stopReason; }
    QString stopText() const { return m_stopText; }
    quint32 stopSerial() const { return m_stopSerial; }

    /// The Core stopped the current key (or the one that just ended) on its
    /// own, for `reason` (one of the kStop* values), told as `text`. The
    /// first reason for a key advances stopSerial and returns true; any
    /// later one for the same key, or one before any key, returns false.
    bool recordStop(const QByteArray& reason, const QString& text);

    /// The name and kind of whoever keyed the current key (or the last
    /// one), kept after the key ends so a stop can name them.
    QString lastKeyedByName() const { return m_lastKeyedByName; }
    QString lastKeyedByKind() const { return m_lastKeyedByKind; }

    // ---- The stop texts (plain words; the phone shows them as sent) ----

    /// "3:00": minutes and seconds.
    static QString durationText(int seconds);
    /// The transmit time-out (Task 38): `which` "mox" or "ping", the limit,
    /// and the kind of device that keyed ("phone" and "tablet" have their
    /// own time-out).
    static QString timeOutText(const QByteArray& which, int limitSeconds,
                               const QString& deviceKind);
    static QString linkLostText(const QString& deviceName);
    static QString micStarvedText(const QString& deviceName);
    static QString revokedText(const QString& deviceName);
    static QString takenOverText(const QString& takerName);
    static QString stationText();

    // ---- A remote window's copy ----

    /// A plain state apply of one of the Core's values. False for a name
    /// this class does not have.
    bool applyStationValue(const QByteArray& propertyName, const QVariant& value);
    /// Back to the values of a Core with nothing keyed (a window whose Core
    /// is gone, or does not send the object): unkeyed, no time-out, idle
    /// meters. The stop fields keep the last stop heard.
    void clearStationValues();

signals:
    /// keyed, tuning, twoTone, txSliceId, keyedBy*, keyedSinceMs, txEnding.
    void stateChanged();
    void timeOutChanged();
    void metersChanged();
    /// stopReason, stopText, stopSerial.
    void stopChanged();

private:
    void onTransmittingChanged(bool keyed);
    void onMeterReadings(const TxMeterReadings& readings);
    void onPowerChanged();
    void refreshState();
    void refreshTimeOut();
    void setMeters(const TxMeterReadings& readings);
    qint64 now() const;

    QPointer<RadioModel> m_model;
    TxMeterPump* m_pump{nullptr};
    Clock m_clock;
    QElapsedTimer m_monotonic;

    bool m_keyed{false};
    bool m_tuning{false};
    bool m_twoTone{false};
    int m_txSliceId{-1};
    QString m_keyedByName;
    QString m_keyedByKind;
    QString m_keyedTrigger;
    qint64 m_keyedSinceMs{0};
    int m_timeOutRemainingSeconds{-1};
    TxMeterReadings m_meters;
    bool m_txEnding{false};
    QString m_stopReason;
    QString m_stopText;
    quint32 m_stopSerial{0};

    // Each rising edge of keyed is a key; the first stop recorded for it
    // wins.
    quint64 m_key{0};
    bool m_keyStopped{false};
    QString m_lastKeyedByName;
    QString m_lastKeyedByKind;
};

} // namespace NereusSDR
