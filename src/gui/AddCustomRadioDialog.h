#pragma once
// src/gui/AddCustomRadioDialog.h
//
// Port of Thetis frmAddCustomRadio.cs / frmAddCustomRadio.Designer.cs
//
// Thetis fields (Designer.cs):
//   comboNICS         — NIC selector (comboNICS, labelTS2 "Via NIC:")
//   txtSpecificRadio  — "Radio IP:Port" text box (default "192.168.0.155:1024")
//   comboProtocol     — "Protocol 1" / "Protocol 2"
//   txtBoard          — read-only board display
//   btnOK / btnCancel
//
// NereusSDR expands the NIC+IP:Port pair into separate fields and adds
// Name, MAC (optional), Board combo (from BoardCapsTable), and two
// convenience check boxes (Pin to MAC, Auto-connect).
//
// Source citations:
//   frmAddCustomRadio.Designer.cs:50  — labelTS2 "Via NIC:"
//   frmAddCustomRadio.Designer.cs:55  — comboNICS DropDownList
//   frmAddCustomRadio.Designer.cs:61  — txtSpecificRadio default "192.168.0.155:1024"
//   frmAddCustomRadio.Designer.cs:79  — comboProtocol items "Protocol 1" / "Protocol 2"
//   frmAddCustomRadio.Designer.cs:90  — labelTS5 "Board:", txtBoard ReadOnly
//   frmAddCustomRadio.cs:60           — comboProtocol.SelectedIndex = 0 (P1 default)
//   frmAddCustomRadio.cs:70           — Protocol property returns comboProtocol.SelectedIndex
//   frmAddCustomRadio.cs:74           — RadioIPPort property returns txtSpecificRadio.Text
//   frmAddCustomRadio.cs:77           — Board setter writes txtBoard.Text

#include "core/RadioDiscovery.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;

namespace NereusSDR {

class AddCustomRadioDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddCustomRadioDialog(QWidget* parent = nullptr);
    ~AddCustomRadioDialog() override;

    // Returns the constructed RadioInfo after the user clicks OK.
    // Call only after exec() returns QDialog::Accepted.
    RadioInfo result() const;

    // Returns whether the user ticked "Pin to MAC"
    bool pinToMac() const;

    // Whether the user ticked "Auto-connect on launch"
    bool autoConnect() const;

private slots:
    void onTestClicked();
    void onAccept();
    void validateFields();
    void onBoardChanged(int index);

private:
    void buildUi();
    void populateBoardCombo();
    void populateProtocolCombo();

    QLineEdit*   m_nameEdit{nullptr};
    QLineEdit*   m_ipEdit{nullptr};
    QSpinBox*    m_portSpin{nullptr};
    QLineEdit*   m_macEdit{nullptr};
    QComboBox*   m_boardCombo{nullptr};
    QComboBox*   m_protocolCombo{nullptr};
    QCheckBox*   m_pinToMacCheck{nullptr};
    QCheckBox*   m_autoConnectCheck{nullptr};
    QPushButton* m_testButton{nullptr};
    QLabel*      m_testResultLabel{nullptr};
    QPushButton* m_okButton{nullptr};
    QPushButton* m_cancelButton{nullptr};
};

} // namespace NereusSDR
