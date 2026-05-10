#pragma once

#include "gui/SetupPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// CAT > Serial Ports
// Four identical port sections, each with: port combo, baud combo,
// enable toggle, and status label. All controls are NYI / disabled.
// ---------------------------------------------------------------------------
class CatSerialPortsPage : public SetupPage {
    Q_OBJECT

public:
    explicit CatSerialPortsPage(QWidget* parent = nullptr);

private:
    struct PortRow {
        QComboBox*  portCombo{nullptr};
        QComboBox*  baudCombo{nullptr};
        QCheckBox*  enableCheck{nullptr};
        QLabel*     statusLabel{nullptr};
    };

    PortRow m_ports[4];

    void buildUI();
};

// ---------------------------------------------------------------------------
// CAT > TCI Server
// 6 group boxes: Server / Compatibility / IQ Stream / Audio Stream /
//   Sensors / VFO Quirks.  All 17 AppSettings keys bound.
// Phase 20 (Phase 3J-1): Setup → Network → TCI Server page rewrite.
// ---------------------------------------------------------------------------
class CatTciServerPage : public SetupPage {
    Q_OBJECT

public:
    explicit CatTciServerPage(QWidget* parent = nullptr);

private:
    // Group 1: Server
    QCheckBox*   m_enableCheck{nullptr};
    QLabel*      m_bindIpLabel{nullptr};
    QSpinBox*    m_portSpin{nullptr};
    QPushButton* m_portDefaultBtn{nullptr};
    QCheckBox*   m_sendInitialStateCheck{nullptr};
    QSpinBox*    m_rateLimitSpin{nullptr};
    QPushButton* m_showLogBtn{nullptr};
    QLabel*      m_statusLabel{nullptr};

    // Group 2: Compatibility
    QCheckBox*   m_emulateExpertSdr3Check{nullptr};
    QCheckBox*   m_emulateSunSdr2Check{nullptr};
    QCheckBox*   m_cwluBecomesCwCheck{nullptr};
    QCheckBox*   m_cwBecomesCwuCheck{nullptr};

    // Group 3: IQ Stream
    QCheckBox*   m_iqSwapCheck{nullptr};
    QCheckBox*   m_alwaysStreamIqCheck{nullptr};

    // Group 4: Audio Stream
    QSpinBox*    m_audioBlockSpin{nullptr};
    QComboBox*   m_txChannelCombo{nullptr};

    // Group 5: Sensors
    QSpinBox*    m_rxSensorSpin{nullptr};
    QSpinBox*    m_txSensorSpin{nullptr};

    // Group 6: VFO Quirks
    QCheckBox*   m_forgetRx2VfoBCheck{nullptr};
    QCheckBox*   m_useRx1VfoaForRx2Check{nullptr};
    QCheckBox*   m_copyRx2VfobToVfoaCheck{nullptr};

    void buildUI();
    void buildServerGroup();
    void buildCompatibilityGroup();
    void buildIqStreamGroup();
    void buildAudioStreamGroup();
    void buildSensorsGroup();
    void buildVfoQuirksGroup();
};

// ---------------------------------------------------------------------------
// CAT > TCP/IP CAT
// Enable toggle, bind IP, port spinner, status label.
// All controls are NYI / disabled.
// ---------------------------------------------------------------------------
class CatTcpIpPage : public SetupPage {
    Q_OBJECT

public:
    explicit CatTcpIpPage(QWidget* parent = nullptr);

private:
    QCheckBox* m_enableCheck{nullptr};
    QLineEdit* m_bindIpEdit{nullptr};
    QSpinBox*  m_portSpin{nullptr};
    QLabel*    m_statusLabel{nullptr};

    void buildUI();
};

// ---------------------------------------------------------------------------
// CAT > MIDI Control
// Enable toggle, device combo, mapping table placeholder, learn button.
// All controls are NYI / disabled.
// ---------------------------------------------------------------------------
class CatMidiControlPage : public SetupPage {
    Q_OBJECT

public:
    explicit CatMidiControlPage(QWidget* parent = nullptr);

private:
    QCheckBox*  m_enableCheck{nullptr};
    QComboBox*  m_deviceCombo{nullptr};
    QLabel*     m_mappingLabel{nullptr};
    QPushButton* m_learnButton{nullptr};

    void buildUI();
};

} // namespace NereusSDR
