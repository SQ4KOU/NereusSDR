#pragma once

#include <QObject>
#include <QVector>
#include <QMap>

namespace NereusSDR {

// Per-receiver configuration state.
struct ReceiverConfig {
    int receiverIndex{-1};       // Logical receiver index (0-based)
    int hardwareRx{-1};          // Hardware DDC index (e.g., DDC2 for ANAN-G2 RX1)
    int wdspChannel{-1};         // WDSP channel number (assigned at creation)
    int adcIndex{0};             // Which ADC feeds this receiver's DDC (0 or 1)
    int ddcIndex{-1};            // Explicit DDC mapping (-1 = auto-assign)
    quint64 frequencyHz{14225000};
    int sampleRate{48000};
    bool active{false};
    bool isDiversity{false};     // Sub-receiver for diversity combining
    bool isPureSignalFeedback{false}; // Dedicated to PA linearization
};

// Client-side receiver lifecycle management.
// Maps logical receivers to hardware DDC channels and (future) WDSP instances.
// Lives on the main thread; RadioModel owns it.
class ReceiverManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int activeReceiverCount READ activeReceiverCount
               NOTIFY activeReceiverCountChanged)

public:
    explicit ReceiverManager(QObject* parent = nullptr);
    ~ReceiverManager() override;

    // --- Configuration ---
    void setMaxReceivers(int max);
    int maxReceivers() const { return m_maxReceivers; }

    // --- Receiver Lifecycle ---
    // Create a new receiver. Returns the receiver index, or -1 if at max.
    int createReceiver();

    // Destroy a receiver and release its hardware DDC.
    void destroyReceiver(int receiverIndex);

    // Activate/deactivate a receiver.
    // Activating increments the hardware receiver count sent to the radio.
    void activateReceiver(int receiverIndex);
    void deactivateReceiver(int receiverIndex);

    // --- State Queries ---
    int activeReceiverCount() const;
    bool isReceiverActive(int receiverIndex) const;
    ReceiverConfig receiverConfig(int receiverIndex) const;

    // --- DDC frequency lock (CTUN mode) ---
    // When locked, setReceiverFrequency stores the freq but does NOT
    // emit hardwareFrequencyChanged. MainWindow manages DDC directly.
    void setDdcFrequencyLocked(bool locked) { m_ddcFreqLocked = locked; }
    bool ddcFrequencyLocked() const { return m_ddcFreqLocked; }

    // --- Receiver Configuration ---
    void setReceiverFrequency(int receiverIndex, quint64 frequencyHz);
    // Force DDC retune even when locked (used by MainWindow CTUN pan drag)
    void forceHardwareFrequency(int receiverIndex, quint64 frequencyHz);
    void setReceiverSampleRate(int receiverIndex, int sampleRate);

    // Set explicit DDC mapping for a receiver.
    // For ANAN-G2: receiver 0 → DDC 2 (from Thetis UpdateDDCs console.cs:8216).
    // If ddcIndex = -1, sequential auto-assignment is used.
    void setDdcMapping(int receiverIndex, int ddcIndex);
    int ddcIndex(int receiverIndex) const;

    // Set which ADC feeds this receiver's DDC (for 2-ADC boards).
    void setAdcForReceiver(int receiverIndex, int adcIndex);

    // --- I/Q Data Routing ---
    // Called when I/Q data arrives for a hardware DDC.
    // Routes to the correct logical receiver.
    void feedIqData(int hwReceiverIndex, const QVector<float>& samples);

signals:
    void activeReceiverCountChanged(int count);
    void receiverCreated(int receiverIndex);
    void receiverDestroyed(int receiverIndex);
    void receiverActivated(int receiverIndex);
    void receiverDeactivated(int receiverIndex);
    void receiverFrequencyChanged(int receiverIndex, quint64 frequencyHz);

    // Request RadioConnection to update hardware state.
    // RadioModel wires these to RadioConnection slots.
    void hardwareReceiverCountChanged(int count);
    void hardwareFrequencyChanged(int hardwareRx, quint64 frequencyHz);

    // I/Q data routed to the appropriate WDSP channel.
    void iqDataForChannel(int wdspChannel, const QVector<float>& samples);

    // I/Q data routed to the appropriate receiver (by logical index).
    void iqDataForReceiver(int receiverIndex, const QVector<float>& samples);

private:
    // Rebuild hardware DDC mapping after receiver changes.
    void rebuildHardwareMapping();

    int m_maxReceivers{7};
    int m_nextWdspChannel{0};
    bool m_ddcFreqLocked{false};

    // Receivers keyed by logical index
    QMap<int, ReceiverConfig> m_receivers;

    // Mapping from hardware DDC index to logical receiver index
    QMap<int, int> m_hwToLogical;

    static const ReceiverConfig kInvalidConfig;
};

} // namespace NereusSDR
