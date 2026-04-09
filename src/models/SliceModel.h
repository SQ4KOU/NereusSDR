#pragma once

#include "core/WdspTypes.h"

#include <QObject>
#include <QString>

#include <utility>

namespace NereusSDR {

// Represents a single receiver slice.
// In NereusSDR, slices are a client-side abstraction — the radio has
// no concept of slices. Each slice owns a WDSP channel for independent
// DSP processing.
//
// The slice is the single source of truth for VFO state (frequency,
// mode, filter, AGC, gains, antenna). Changes propagate to WDSP and
// the radio via signals wired in RadioModel.
//
// From AetherSDR SliceModel pattern: Q_PROPERTY + signals for each state.
class SliceModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(double     frequency    READ frequency    WRITE setFrequency    NOTIFY frequencyChanged)
    Q_PROPERTY(NereusSDR::DSPMode dspMode READ dspMode   WRITE setDspMode      NOTIFY dspModeChanged)
    Q_PROPERTY(int        filterLow    READ filterLow    WRITE setFilterLow    NOTIFY filterChanged)
    Q_PROPERTY(int        filterHigh   READ filterHigh   WRITE setFilterHigh   NOTIFY filterChanged)
    Q_PROPERTY(NereusSDR::AGCMode agcMode READ agcMode   WRITE setAgcMode      NOTIFY agcModeChanged)
    Q_PROPERTY(int        stepHz       READ stepHz       WRITE setStepHz       NOTIFY stepHzChanged)
    Q_PROPERTY(int        afGain       READ afGain       WRITE setAfGain       NOTIFY afGainChanged)
    Q_PROPERTY(int        rfGain       READ rfGain       WRITE setRfGain       NOTIFY rfGainChanged)
    Q_PROPERTY(QString    rxAntenna    READ rxAntenna    WRITE setRxAntenna    NOTIFY rxAntennaChanged)
    Q_PROPERTY(QString    txAntenna    READ txAntenna    WRITE setTxAntenna    NOTIFY txAntennaChanged)
    Q_PROPERTY(bool       active       READ isActive     NOTIFY activeChanged)
    Q_PROPERTY(bool       txSlice      READ isTxSlice    NOTIFY txSliceChanged)

public:
    explicit SliceModel(QObject* parent = nullptr);
    ~SliceModel() override;

    // ---- Frequency ----

    double frequency() const { return m_frequency; }
    void setFrequency(double freq);

    // ---- Demodulation mode ----

    DSPMode dspMode() const { return m_dspMode; }
    // Sets mode AND applies the default filter for that mode.
    // Emits dspModeChanged + filterChanged.
    void setDspMode(DSPMode mode);

    // ---- Bandpass filter ----

    int filterLow() const { return m_filterLow; }
    void setFilterLow(int low);

    int filterHigh() const { return m_filterHigh; }
    void setFilterHigh(int high);

    // Set both filter edges atomically. Emits filterChanged once.
    void setFilter(int low, int high);

    // ---- AGC ----

    AGCMode agcMode() const { return m_agcMode; }
    void setAgcMode(AGCMode mode);

    // ---- Tuning step ----

    int stepHz() const { return m_stepHz; }
    void setStepHz(int hz);

    // ---- Gains ----

    int afGain() const { return m_afGain; }
    void setAfGain(int gain);

    int rfGain() const { return m_rfGain; }
    void setRfGain(int gain);

    // ---- Antenna selection ----
    // Per-slice RX/TX antenna (e.g., "ANT1", "ANT2", "ANT3").
    // Maps to Alex antenna register bits on OpenHPSDR radios.
    // From AetherSDR SliceModel::rxAntenna/txAntenna pattern.

    QString rxAntenna() const { return m_rxAntenna; }
    void setRxAntenna(const QString& ant);

    QString txAntenna() const { return m_txAntenna; }
    void setTxAntenna(const QString& ant);

    // ---- Slice identity ----

    bool isActive() const { return m_active; }
    void setActive(bool active);

    bool isTxSlice() const { return m_txSlice; }
    void setTxSlice(bool tx);

    int sliceIndex() const { return m_sliceIndex; }
    void setSliceIndex(int idx) { m_sliceIndex = idx; }

    // Panadapter assignment (-1 = unassigned)
    int panId() const { return m_panId; }
    void setPanId(int id) { m_panId = id; }

    // Which receiver/DDC this slice feeds (-1 = unassigned)
    int receiverIndex() const { return m_receiverIndex; }
    void setReceiverIndex(int idx) { m_receiverIndex = idx; }

    int wdspChannelId() const { return m_wdspChannelId; }
    void setWdspChannelId(int id) { m_wdspChannelId = id; }

    // ---- Per-mode filter defaults ----
    // Returns the F5 (default) filter low/high for a given mode.
    // Ported from Thetis console.cs:5180-5575 InitFilterPresets.
    static std::pair<int, int> defaultFilterForMode(DSPMode mode);

    // Returns human-readable mode name (e.g., DSPMode::LSB → "LSB")
    static QString modeName(DSPMode mode);

    // Returns DSPMode from name string (e.g., "LSB" → DSPMode::LSB)
    static DSPMode modeFromName(const QString& name);

signals:
    void frequencyChanged(double freq);
    void dspModeChanged(NereusSDR::DSPMode mode);
    void filterChanged(int low, int high);
    void agcModeChanged(NereusSDR::AGCMode mode);
    void stepHzChanged(int hz);
    void afGainChanged(int gain);
    void rfGainChanged(int gain);
    void rxAntennaChanged(const QString& ant);
    void txAntennaChanged(const QString& ant);
    void activeChanged(bool active);
    void txSliceChanged(bool tx);

private:
    double  m_frequency{14225000.0};     // Default: 14.225 MHz (20m USB)
    DSPMode m_dspMode{DSPMode::USB};
    int     m_filterLow{100};            // USB default from Thetis F5
    int     m_filterHigh{3000};
    AGCMode m_agcMode{AGCMode::Med};
    int     m_stepHz{100};               // From Thetis tune_step_list[5] = 100 Hz
    int     m_afGain{50};                // 0-100, maps to 0.0-1.0 volume
    int     m_rfGain{80};                // 0-100, maps to AGC gain
    QString m_rxAntenna{QStringLiteral("ANT1")};
    QString m_txAntenna{QStringLiteral("ANT1")};
    bool    m_active{false};
    bool    m_txSlice{false};
    int     m_sliceIndex{0};
    int     m_panId{-1};
    int     m_receiverIndex{-1};
    int     m_wdspChannelId{-1};
};

} // namespace NereusSDR
