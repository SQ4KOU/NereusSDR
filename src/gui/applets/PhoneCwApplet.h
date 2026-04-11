#pragma once
#include "AppletWidget.h"

#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QButtonGroup>
#include <QStackedWidget>

namespace NereusSDR {

// PhoneCwApplet — Phone/CW/FM transmit voice controls applet.
// 30 controls total: Phone 13, CW 9, FM 8.
// Complete NYI shell for Phase 3I-1, 3I-2, 3I-3, 3-DAX.
class PhoneCwApplet : public AppletWidget {
    Q_OBJECT

public:
    explicit PhoneCwApplet(RadioModel* model, QWidget* parent = nullptr);
    ~PhoneCwApplet() override = default;

    QString appletId()    const override { return QStringLiteral("phonecw"); }
    QString appletTitle() const override { return QStringLiteral("Phone / CW"); }

    void syncFromModel() override;

private:
    void buildUI();
    QWidget* buildPhonePage();
    QWidget* buildCwPage();
    QWidget* buildFmPage();

    // Tab selector
    QStackedWidget* m_stack{nullptr};
    QButtonGroup*   m_tabGroup{nullptr};
    QPushButton*    m_phoneTabBtn{nullptr};
    QPushButton*    m_cwTabBtn{nullptr};
    QPushButton*    m_fmTabBtn{nullptr};

    // ---- Phone page controls ----

    // Phone #1 — Mic Level gauge (HGauge -40..+10 dBFS)
    QWidget* m_micLevelGauge{nullptr};

    // Phone #2 — Compression gauge (HGauge -25..0 dB, reversed)
    QWidget* m_compGauge{nullptr};

    // Phone #3 — Mic profile combo
    QComboBox* m_micProfileCombo{nullptr};

    // Phone #4 — Mic source combo (fixed 55px)
    QComboBox* m_micSrcCombo{nullptr};

    // Phone #5 — Mic level slider + value
    QSlider* m_micLvlSlider{nullptr};
    QLabel*  m_micLvlValue{nullptr};

    // Phone #6 — +ACC button (green toggle 48px)
    QPushButton* m_accBtn{nullptr};

    // Phone #7 — PROC button + slider
    QPushButton* m_procBtn{nullptr};
    QSlider*     m_procSlider{nullptr};
    QLabel*      m_procValue{nullptr};

    // Phone #8 — DAX button (blue toggle 48px)
    QPushButton* m_daxBtn{nullptr};

    // Phone #9 — MON button + slider
    QPushButton* m_monBtn{nullptr};
    QSlider*     m_monSlider{nullptr};
    QLabel*      m_monValue{nullptr};

    // Phone #10 — VOX toggle + level slider + delay slider
    QPushButton* m_voxBtn{nullptr};
    QSlider*     m_voxLvlSlider{nullptr};
    QLabel*      m_voxLvlValue{nullptr};
    QSlider*     m_voxDlySlider{nullptr};
    QLabel*      m_voxDlyValue{nullptr};

    // Phone #11 — DEXP toggle + level slider
    QPushButton* m_dexpBtn{nullptr};
    QSlider*     m_dexpSlider{nullptr};
    QLabel*      m_dexpValue{nullptr};

    // Phone #12 — TX filter Low/High sliders
    QSlider* m_txFiltLoSlider{nullptr};
    QLabel*  m_txFiltLoValue{nullptr};
    QSlider* m_txFiltHiSlider{nullptr};
    QLabel*  m_txFiltHiValue{nullptr};

    // Phone #13 — AM Carrier level slider
    QSlider* m_amCarSlider{nullptr};
    QLabel*  m_amCarValue{nullptr};

    // ---- CW page controls ----

    // CW #1 — ALC gauge (HGauge 0-100, yellow/red 80)
    QWidget* m_alcGauge{nullptr};

    // CW #2 — CW speed slider (1-60 WPM)
    QSlider* m_cwSpeedSlider{nullptr};
    QLabel*  m_cwSpeedValue{nullptr};

    // CW #3 — CW pitch stepper
    QPushButton* m_cwPitchDn{nullptr};
    QLabel*      m_cwPitchValue{nullptr};
    QPushButton* m_cwPitchUp{nullptr};

    // CW #4 — Delay slider (0-1000ms)
    QSlider* m_cwDelaySlider{nullptr};
    QLabel*  m_cwDelayValue{nullptr};

    // CW #5 — Sidetone toggle + slider
    QPushButton* m_sideBtn{nullptr};
    QSlider*     m_sideSlider{nullptr};
    QLabel*      m_sideValue{nullptr};

    // CW #6 — Break-in (QSK) amber toggle
    QPushButton* m_qskBtn{nullptr};

    // CW #7 — Iambic blue toggle
    QPushButton* m_iambicBtn{nullptr};

    // CW #8 — Firmware keyer green toggle
    QPushButton* m_fwKeyerBtn{nullptr};

    // CW #9 — CW pan slider (-100..100)
    QSlider* m_cwPanSlider{nullptr};
    QLabel*  m_cwPanValue{nullptr};

    // ---- FM page controls ----

    // FM #1 — FM MIC slider
    QSlider* m_fmMicSlider{nullptr};
    QLabel*  m_fmMicValue{nullptr};

    // FM #2 — Deviation buttons (5.0k / 2.5k)
    QPushButton* m_dev5kBtn{nullptr};
    QPushButton* m_dev25kBtn{nullptr};

    // FM #3 — CTCSS enable + tone combo
    QPushButton* m_ctcssBtn{nullptr};
    QComboBox*   m_ctcssCombo{nullptr};

    // FM #4 — Simplex toggle
    QPushButton* m_simplexBtn{nullptr};

    // FM #5 — Repeater offset slider (0-10000 kHz)
    QSlider* m_rptOffsetSlider{nullptr};
    QLabel*  m_rptOffsetValue{nullptr};

    // FM #6 — Offset direction buttons (- / + / Rev)
    QPushButton* m_offsetMinusBtn{nullptr};
    QPushButton* m_offsetPlusBtn{nullptr};
    QPushButton* m_offsetRevBtn{nullptr};

    // FM #7 — FM TX Profile combo
    QComboBox* m_fmProfileCombo{nullptr};

    // FM #8 — FM Memory combo + nav
    QComboBox*   m_fmMemCombo{nullptr};
    QPushButton* m_fmMemPrev{nullptr};
    QPushButton* m_fmMemNext{nullptr};
};

} // namespace NereusSDR
