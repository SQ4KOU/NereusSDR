#pragma once
// no-port-check: NereusSDR-original mirrored presentation of the Core's Alex
// antenna settings. All antenna logic stays in AlexController.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/core/accessories/AlexAntennaFacade.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. The Core's Alex antenna settings as
// one mirrored object, `alexAntennas` (R-R3-46, radioHardwareVersion 2).
//
// Bound to an AlexController (the Core, and a local window) it follows
// every controller change and applies each receive edit through the
// controller, so the radio, the per-band memory and the saved settings stay
// the controller's. A value the controller cannot take as asked settles on
// the value it kept, and settleReason() says why in plain words.
//
// Unbound (a remote window) it only holds values: the Core's, as they
// arrive, and the window's receive edits, which an edit gate may refuse.
//
// Settable (receive): the RX antenna for each band, the RX-only antenna for
// each band and "use the TX antenna for RX". Reported (transmit, R4): the TX
// antenna for each band, the two Block-TX switches and the four TX relay
// switches.
//
// Wire values: a per-band list is 14 comma-separated whole numbers in Band
// order (160 m .. 6 m, GEN, WWV, XVTR). RX and TX antennas are 1..3; the
// RX-only antenna is 0 (none) .. 3.
//
// The object also carries a remote window's Hardware Config availability
// (can its hardware edits reach the Core, and if not why): MainWindow sets
// it from the session and Setup follows it.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 fix wave: one band's antenna at
//                                    a time (setBandEditSender, the Core's
//                                    setRxAntForBand). AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-21 (radioHardwareVersion
//                                    4): the Core's filter policy for a
//                                    remote window (setBpfModeForChain).
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include "models/Band.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

#include <array>
#include <functional>

namespace NereusSDR {

class AlexController;

class AlexAntennaFacade final : public QObject {
    Q_OBJECT
    // Settable (receive).
    Q_PROPERTY(QString rxAntennas READ rxAntennas WRITE setRxAntennas NOTIFY rxAntennasChanged)
    Q_PROPERTY(QString rxOnlyAntennas READ rxOnlyAntennas WRITE setRxOnlyAntennas
               NOTIFY rxOnlyAntennasChanged)
    Q_PROPERTY(bool useTxAntennaForRx READ useTxAntennaForRx WRITE setUseTxAntennaForRx
               NOTIFY useTxAntennaForRxChanged)
    // Reported by the Core (transmit settings, changed only on the Core).
    Q_PROPERTY(QString txAntennas READ txAntennas NOTIFY txAntennasChanged)
    Q_PROPERTY(bool blockTxAnt2 READ blockTxAnt2 NOTIFY blockTxAnt2Changed)
    Q_PROPERTY(bool blockTxAnt3 READ blockTxAnt3 NOTIFY blockTxAnt3Changed)
    Q_PROPERTY(bool rxOutOnTx READ rxOutOnTx NOTIFY rxOutOnTxChanged)
    Q_PROPERTY(bool ext1OutOnTx READ ext1OutOnTx NOTIFY ext1OutOnTxChanged)
    Q_PROPERTY(bool ext2OutOnTx READ ext2OutOnTx NOTIFY ext2OutOnTxChanged)
    Q_PROPERTY(bool rxOutOverride READ rxOutOverride NOTIFY rxOutOverrideChanged)

public:
    /// True when an edit may go ahead; otherwise false with a plain reason.
    using EditGate = std::function<bool(QString* reason)>;
    /// A remote window: send one band's RX (rxOnly false) or RX-only
    /// antenna to the Core. False, with a plain reason, when not sent.
    using BandEditSender = std::function<bool(Band band, int antenna, bool rxOnly,
                                              QString* reason)>;

    /// The bands AlexController keeps antennas for (160 m .. XVTR).
    static constexpr int kBandCount = static_cast<int>(Band::SwlFirst);

    explicit AlexAntennaFacade(QObject* parent = nullptr);
    ~AlexAntennaFacade() override;

    /// Follow `controller` (nullptr unbinds; the last values stay).
    void bindController(AlexController* controller);
    AlexController* controller() const;
    bool isBound() const;

    void setEditGate(EditGate gate) { m_editGate = std::move(gate); }

    /// R-R3-46 fix wave (radioHardwareVersion 3): a remote window whose Core
    /// takes one band's antenna at a time sets this; setRxAnt and
    /// setRxOnlyAnt then send only that band (the Core's delta brings the
    /// value back), so a list built before the Core changed another band
    /// cannot put that band back. Without it they send the whole list.
    void setBandEditSender(BandEditSender sender) { m_bandEditSender = std::move(sender); }
    bool hasBandEditSender() const { return static_cast<bool>(m_bandEditSender); }
    /// A remote window: the Core refused a band edit sent by the sender;
    /// views re-read the held values (bandEditRefused).
    void reportBandEditRefused() { emit bandEditRefused(); }

    /// The Core (bound): one band's RX antenna (1..3) or RX-only antenna
    /// (0..3), through the controller. Empty when taken as asked; otherwise
    /// the plain reason the band kept another value.
    QString setRxAntForBand(Band band, int antenna);
    QString setRxOnlyAntForBand(Band band, int antenna);

    /// The receive filter chains AlexController keeps a filter policy for
    /// (Alex0 / ADC0 and Alex1 / ADC1).
    static constexpr int kFilterChainCount = 2;

    /// R-R3-46 / R-R3-21 (radioHardwareVersion 4). The Core (bound): one
    /// chain's filter policy, 0 Auto, 1 Force filter, 2 Force bypass
    /// (AlexController::BpfMode), through the controller's setBpfMode, the
    /// call the local filter policy dialog makes. Empty when taken as
    /// asked; otherwise the plain reason it was not. The controller's
    /// bpfModeChanged then has the Core save it for its radio.
    QString setBpfModeForChain(int chain, int mode);

    /// A remote window: whether its Hardware Config edits can reach the Core
    /// now and, when they cannot, why, in plain words. Starts unavailable,
    /// with the "connect to the Core" reason.
    void setWindowAvailability(bool available, const QString& reason);
    bool windowAvailable() const { return m_windowAvailable; }
    QString windowUnavailableReason() const { return m_windowReason; }

    /// Why the last edit of `property` settled on another value; empty when
    /// it was taken as asked or was never edited.
    QString settleReason(const QByteArray& property) const;

    /// A remote window: a value the Core reports (txAntennas, blockTxAnt2,
    /// blockTxAnt3, rxOutOnTx, ext1OutOnTx, ext2OutOnTx, rxOutOverride).
    /// False for any other name, and always false while bound.
    bool applyRemoteProperty(const QByteArray& property, const QVariant& value);

    QString rxAntennas() const;
    QString rxOnlyAntennas() const;
    QString txAntennas() const;
    bool useTxAntennaForRx() const { return m_values.useTxAntForRx; }
    bool blockTxAnt2() const { return m_values.blockTxAnt2; }
    bool blockTxAnt3() const { return m_values.blockTxAnt3; }
    bool rxOutOnTx() const { return m_values.rxOutOnTx; }
    bool ext1OutOnTx() const { return m_values.ext1OutOnTx; }
    bool ext2OutOnTx() const { return m_values.ext2OutOnTx; }
    bool rxOutOverride() const { return m_values.rxOutOverride; }

    /// One band's antenna; the band's default for a band outside 160 m .. XVTR.
    int rxAnt(Band band) const;
    int rxOnlyAnt(Band band) const;
    int txAnt(Band band) const;

    void setRxAntennas(const QString& list);
    void setRxOnlyAntennas(const QString& list);
    void setUseTxAntennaForRx(bool on);

    /// One band's edit, as the whole list with that band changed.
    void setRxAnt(Band band, int ant);
    void setRxOnlyAnt(Band band, int ant);

signals:
    void rxAntennasChanged(const QString& list);
    void rxOnlyAntennasChanged(const QString& list);
    void useTxAntennaForRxChanged(bool on);
    void txAntennasChanged(const QString& list);
    void blockTxAnt2Changed(bool on);
    void blockTxAnt3Changed(bool on);
    void rxOutOnTxChanged(bool on);
    void ext1OutOnTxChanged(bool on);
    void ext2OutOnTxChanged(bool on);
    void rxOutOverrideChanged(bool on);
    /// An edit the gate refused, with its plain reason.
    void editRejected(const QString& reason);
    /// setWindowAvailability() changed the availability or its reason.
    void windowAvailabilityChanged(bool available);
    /// A band edit did not reach the Core or the Core refused it; the held
    /// values are unchanged, so a view that showed the click re-reads them.
    void bandEditRefused();

private:
    using BandList = std::array<int, kBandCount>;

    struct Values {
        BandList rxAnt{};
        BandList rxOnlyAnt{};
        BandList txAnt{};
        bool useTxAntForRx{false};
        bool blockTxAnt2{false};
        bool blockTxAnt3{false};
        bool rxOutOnTx{false};
        bool ext1OutOnTx{false};
        bool ext2OutOnTx{false};
        bool rxOutOverride{false};
    };

    static Values defaults();
    static QString encode(const BandList& list);
    /// Parse `text` into `out`, clamping each band to [lo, hi]. False when
    /// the text is not 14 whole numbers. `clamped` is set when a value was
    /// moved into range.
    static bool decode(const QString& text, int lo, int hi, BandList* out, bool* clamped);

    bool beginEdit(const char* property);
    /// A remote window with a band edit sender: send one band's edit and
    /// return true (whatever the outcome); false when the whole list goes.
    bool sendBandEdit(const char* property, Band band, int ant, bool rxOnly);
    void settle(const char* property, const QString& reason);
    /// Re-read the bound controller and emit each property that changed.
    void refresh();
    void publish(const Values& next);

    QPointer<AlexController> m_controller;
    QList<QMetaObject::Connection> m_controllerConnections;
    EditGate m_editGate;
    BandEditSender m_bandEditSender;
    bool m_windowAvailable{false};
    QString m_windowReason;
    QHash<QByteArray, QString> m_settleReasons;
    Values m_values;
};

} // namespace NereusSDR
