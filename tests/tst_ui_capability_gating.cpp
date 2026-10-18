// no-port-check: widget-level capability-gating test (Phase 3P-I-a T21)
//
// Verifies that setBoardCapabilities() on VfoWidget and RxApplet actually
// hides the ANT buttons when the connected board has no Alex front-end
// (HL2 / Atlas), and shows them when hasAlex is true with an adequate
// antennaInputCount. This is a widget-level integration test — it
// constructs the real widget and asserts against the child QPushButton
// states via findChild<QPushButton*>("m_rxAntBtn" / "m_txAntBtn").
//
// The objectName strings must stay in sync with the setObjectName calls
// in VfoWidget::buildHeaderRow and RxApplet::buildUi; the test will
// FAIL LOUDLY if those names change (which is the intended coupling).
//
// Related:
//   tests/tst_rxapplet_antenna_buttons.cpp — per-band AlexController sync
//   tests/tst_antenna_routing_model.cpp    — RadioModel pump integration

#include <QtTest/QtTest>
#include <QPushButton>

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "gui/applets/RxApplet.h"
#include "gui/widgets/VfoWidget.h"

using namespace NereusSDR;

class TestUiCapabilityGating : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { AppSettings::instance().clear(); }

    // --- VfoWidget ---

    void vfoWidget_hides_antButtons_when_noAlex() {
        VfoWidget w;
        BoardCapabilities caps;  // defaults: hasAlex=false, antennaInputCount=0
        w.setBoardCapabilities(caps);

        auto* rxBtn = w.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"));
        auto* txBtn = w.findChild<QPushButton*>(QStringLiteral("m_txAntBtn"));
        QVERIFY2(rxBtn, "VfoWidget missing m_rxAntBtn objectName");
        QVERIFY2(txBtn, "VfoWidget missing m_txAntBtn objectName");

        // setVisible(false) propagates isHidden() regardless of parent
        // visibility, which is what we want to verify here.
        QVERIFY(rxBtn->isHidden());
        QVERIFY(txBtn->isHidden());
    }

    void vfoWidget_shows_antButtons_when_hasAlex() {
        VfoWidget w;
        BoardCapabilities caps;
        caps.hasAlex = true;
        caps.antennaInputCount = 3;
        w.setBoardCapabilities(caps);

        auto* rxBtn = w.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"));
        auto* txBtn = w.findChild<QPushButton*>(QStringLiteral("m_txAntBtn"));
        QVERIFY(rxBtn && !rxBtn->isHidden());
        QVERIFY(txBtn && !txBtn->isHidden());
    }

    void vfoWidget_hides_antButtons_when_antennaInputCount_too_low() {
        // Phase 3P-I-a §4.1 — T15 gates on hasAlex AND antennaInputCount>=3.
        // A board that somehow claims hasAlex but only reports 2 inputs
        // should still hide the buttons (belt + suspenders).
        VfoWidget w;
        BoardCapabilities caps;
        caps.hasAlex = true;
        caps.antennaInputCount = 2;  // below threshold
        w.setBoardCapabilities(caps);

        auto* rxBtn = w.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"));
        auto* txBtn = w.findChild<QPushButton*>(QStringLiteral("m_txAntBtn"));
        QVERIFY(rxBtn && rxBtn->isHidden());
        QVERIFY(txBtn && txBtn->isHidden());
    }

    // --- RxApplet ---

    void rxApplet_hides_antButtons_when_noAlex() {
        RxApplet w(nullptr, nullptr, nullptr);
        BoardCapabilities caps;  // defaults: hasAlex=false
        w.setBoardCapabilities(caps);

        auto* rxBtn = w.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"));
        auto* txBtn = w.findChild<QPushButton*>(QStringLiteral("m_txAntBtn"));
        QVERIFY2(rxBtn, "RxApplet missing m_rxAntBtn objectName");
        QVERIFY2(txBtn, "RxApplet missing m_txAntBtn objectName");
        QVERIFY(rxBtn->isHidden());
        QVERIFY(txBtn->isHidden());
    }

    void rxApplet_shows_antButtons_when_hasAlex() {
        RxApplet w(nullptr, nullptr, nullptr);
        BoardCapabilities caps;
        caps.hasAlex = true;
        caps.antennaInputCount = 3;
        w.setBoardCapabilities(caps);

        auto* rxBtn = w.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"));
        auto* txBtn = w.findChild<QPushButton*>(QStringLiteral("m_txAntBtn"));
        QVERIFY(rxBtn && !rxBtn->isHidden());
        QVERIFY(txBtn && !txBtn->isHidden());
    }
};

QTEST_MAIN(TestUiCapabilityGating)
#include "tst_ui_capability_gating.moc"
