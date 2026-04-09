#pragma once

#include "core/WdspTypes.h"
#include "models/SliceModel.h"

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPointer>

namespace NereusSDR {

// Floating VFO flag widget — AetherSDR pattern.
// Each slice gets one VfoWidget, parented to SpectrumWidget.
// Positioned at the VFO marker via move() from updatePosition().
//
// Layout (top to bottom):
//   Header:    [RX ANT] [TX ANT] [Filter] ... [TX] [A]
//   Frequency: "14.225.000  MHz"  (26px mono, click-edit, wheel-tune)
//   S-Meter:   [████████░░░]  -85 dBm
//   Tab bar:   Audio | DSP | Mode | X/RIT
//   Tab pages: (collapsible content)
//
// From AetherSDR VfoWidget.h pattern.
class VfoWidget : public QWidget {
    Q_OBJECT

public:
    // Direction hint for positioning the flag relative to the VFO marker.
    enum class FlagDir { Auto, ForceLeft, ForceRight };

    explicit VfoWidget(QWidget* parent = nullptr);
    ~VfoWidget() override;

    // --- State setters (called from model, guarded against re-emit) ---

    void setFrequency(double hz);
    void setMode(DSPMode mode);
    void setFilter(int low, int high);
    void setAgcMode(AGCMode mode);
    void setAfGain(int gain);
    void setRfGain(int gain);
    void setRxAntenna(const QString& ant);
    void setTxAntenna(const QString& ant);
    void setStepHz(int hz);
    void setSliceIndex(int index);
    void setTxSlice(bool isTx);
    void setAntennaList(const QStringList& ants);
    void setSmeter(double dbm);

    // --- Positioning ---

    // Reposition the flag at the given pixel x of the VFO marker.
    // specTop is the y of the spectrum widget's top edge.
    void updatePosition(int vfoX, int specTop, FlagDir dir = FlagDir::Auto);

    int sliceIndex() const { return m_sliceIndex; }

signals:
    void frequencyChanged(double hz);
    void modeChanged(NereusSDR::DSPMode mode);
    void filterChanged(int low, int high);
    void agcModeChanged(NereusSDR::AGCMode mode);
    void afGainChanged(int gain);
    void rfGainChanged(int gain);
    void rxAntennaChanged(const QString& ant);
    void txAntennaChanged(const QString& ant);
    void sliceActivationRequested(int sliceIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void buildUI();
    void buildHeaderRow();
    void buildFrequencyRow();
    void buildSmeterRow();
    void buildTabBar();
    void buildAudioTab();
    void buildDspTab();
    void buildModeTab();
    void rebuildFilterButtons(DSPMode mode);
    void updateFreqLabel();
    QString formatFilterWidth(int low, int high) const;

    // Guard to prevent signal re-emission during model updates
    bool m_updatingFromModel{false};

    // Slice identity
    int m_sliceIndex{0};
    int m_stepHz{100};
    double m_frequency{14225000.0};
    DSPMode m_currentMode{DSPMode::USB};
    double m_smeterDbm{-127.0};

    // Fixed width from AetherSDR VfoWidget
    static constexpr int kWidgetW = 252;

    // --- Header row ---
    QPushButton* m_rxAntBtn{nullptr};
    QPushButton* m_txAntBtn{nullptr};
    QLabel*      m_filterWidthLbl{nullptr};
    QPushButton* m_txBadge{nullptr};
    QLabel*      m_sliceBadge{nullptr};
    QStringList  m_antennaList{QStringLiteral("ANT1"), QStringLiteral("ANT2"), QStringLiteral("ANT3")};

    // --- Frequency row ---
    QStackedWidget* m_freqStack{nullptr};
    QLabel*         m_freqLabel{nullptr};
    QLineEdit*      m_freqEdit{nullptr};

    // --- S-meter row ---
    QLabel* m_smeterLabel{nullptr};

    // --- Tab bar ---
    QList<QPushButton*> m_tabButtons;
    QStackedWidget*     m_tabStack{nullptr};
    int                 m_activeTab{0};

    // --- Mode tab ---
    QComboBox*          m_modeCmb{nullptr};
    QWidget*            m_filterBtnContainer{nullptr};
    QSlider*            m_rfGainSlider{nullptr};
    QLabel*             m_rfGainLabel{nullptr};

    // --- Audio tab ---
    QSlider*            m_afGainSlider{nullptr};
    QLabel*             m_afGainLabel{nullptr};
    QComboBox*          m_agcCmb{nullptr};

    // --- DSP tab ---
    QPushButton*        m_nbBtn{nullptr};
    QPushButton*        m_nrBtn{nullptr};
    QPushButton*        m_anfBtn{nullptr};

    // Slice color table: A=cyan, B=magenta, C=green, D=yellow
    static QColor sliceColor(int index);
};

} // namespace NereusSDR
