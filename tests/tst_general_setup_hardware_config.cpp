// tests/tst_general_setup_hardware_config.cpp  (NereusSDR)
//
// Phase 3M-0 Task 12 — Hardware Configuration group box on Setup → General.
// no-port-check: test fixture — no Thetis attribution required.
//
// Verifies grpHardwareConfig contains the 4 controls + 1 warning label per
// Thetis tpGeneralHardware (setup.designer.cs:8045-8396 [v2.10.3.13]).

#include <QtTest>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include "gui/setup/GeneralOptionsPage.h"
#include "OperatorWording.h"

using namespace NereusSDR;

class TestGeneralSetupHardwareConfig : public QObject
{
    Q_OBJECT
private slots:
    void regionCombo_24Entries_defaultUnitedStates();
    void chkExtended_present_withWarningLabel();
    void chkGeneralRXOnly_hiddenByDefault();
    void chkNetworkWDT_present_defaultChecked();
};

void TestGeneralSetupHardwareConfig::regionCombo_24Entries_defaultUnitedStates()
{
    // From Thetis setup.designer.cs:8084-8108 [v2.10.3.13] — 24-entry Region combo.
    GeneralOptionsPage page(/*model=*/nullptr);
    auto* group = page.findChild<QGroupBox*>("grpHardwareConfig");
    QVERIFY2(group, "grpHardwareConfig not found");

    auto* combo = group->findChild<QComboBox*>("comboFRSRegion");
    QVERIFY2(combo, "comboFRSRegion not found");
    QCOMPARE(combo->count(), 24);
    QCOMPARE(combo->currentText(), QString("United States"));
    QCOMPARE(combo->toolTip(), QString("Select Region for your location"));

    // Spot-check: Australia/Japan/Germany are findable
    QVERIFY(combo->findText("Australia") >= 0);
    QVERIFY(combo->findText("Japan") >= 0);
    QVERIFY(combo->findText("Germany") >= 0);
}

void TestGeneralSetupHardwareConfig::chkExtended_present_withWarningLabel()
{
    // From Thetis setup.designer.cs:8065-8074 [v2.10.3.13] — Extended checkbox.
    // From Thetis setup.designer.cs:8045-8054 [v2.10.3.13] — warning label.
    GeneralOptionsPage page(/*model=*/nullptr);
    auto* group = page.findChild<QGroupBox*>("grpHardwareConfig");
    QVERIFY2(group, "grpHardwareConfig not found");

    auto* chk = group->findChild<QCheckBox*>("chkExtended");
    QVERIFY2(chk, "chkExtended not found");
    QCOMPARE(chk->text(), QString("Extended"));
    QCOMPARE(chk->toolTip(), QString("Enable extended TX (out of band)"));
    QCOMPARE(chk->isChecked(), false);

    auto* lbl = group->findChild<QLabel*>("lblWarningRegionExtended");
    QVERIFY2(lbl, "lblWarningRegionExtended not found");
    QCOMPARE(lbl->text(), QString("Changing this setting will reset your band stack entries"));
    // Verify red bold styling is applied (stylesheet contains "red" or "bold")
    QString ss = lbl->styleSheet();
    QVERIFY2(ss.contains("red", Qt::CaseInsensitive) || ss.contains("bold", Qt::CaseInsensitive),
             "Warning label must have red/bold styling");
}

void TestGeneralSetupHardwareConfig::chkGeneralRXOnly_hiddenByDefault()
{
    // From Thetis setup.designer.cs:8535-8544 [v2.10.3.13] — Visible=false by default.
    GeneralOptionsPage page(/*model=*/nullptr);
    auto* group = page.findChild<QGroupBox*>("grpHardwareConfig");
    QVERIFY2(group, "grpHardwareConfig not found");

    auto* chk = group->findChild<QCheckBox*>("chkGeneralRXOnly");
    QVERIFY2(chk, "chkGeneralRXOnly not found");
    QCOMPARE(chk->text(), QString("Receive Only"));
    QCOMPARE(chk->toolTip(), QString("Check to disable transmit functionality."));
    QVERIFY2(!chk->isVisible(), "chkGeneralRXOnly must be hidden by default");
}

void TestGeneralSetupHardwareConfig::chkNetworkWDT_present_defaultChecked()
{
    // From Thetis setup.designer.cs:8385-8395 [v2.10.3.13] — Checked=true default.
    GeneralOptionsPage page(/*model=*/nullptr);
    auto* group = page.findChild<QGroupBox*>("grpHardwareConfig");
    QVERIFY2(group, "grpHardwareConfig not found");

    auto* chk = group->findChild<QCheckBox*>("chkNetworkWDT");
    QVERIFY2(chk, "chkNetworkWDT not found");
    QCOMPARE(chk->text(), QString("Network Watchdog"));
    // R-R3-49: the tooltip says what the box does in NereusSDR (the wait
    // before the radio is treated as lost), not Thetis's wording, and claims
    // the safety timer only where it is established (P2 radios and HL2).
    QCOMPARE(chk->toolTip(),
             QString("How long NereusSDR waits for data from the radio before it treats "
                     "the radio as lost. On: three seconds. Off: it keeps waiting. On a "
                     "Hermes Lite 2, or a radio on the newer network link, the radio's "
                     "own safety timer stays on either way."));
    QVERIFY(OperatorWording::isPlain(chk->toolTip()));
    QVERIFY2(chk->isChecked(), "chkNetworkWDT must default to checked");
}

QTEST_MAIN(TestGeneralSetupHardwareConfig)
#include "tst_general_setup_hardware_config.moc"
