// no-port-check: NereusSDR-original test for the workspace preview's
// colours by meaning on Settings pages, dialogs and the applets' lit
// states (the colour round's items, each at its cause).
//
// Every case runs with the workspace look on and the live pass installed,
// as main.cpp does with the switch on. A case reads the sheet a widget
// ends up with, or a pixel of its grab, and checks the colour its meaning
// takes: the decided status tones (D21: ok #4caf6a, warn #d8a55f, danger
// #c25a5c), the container's surface for panels and data backgrounds
// (D22), the secondary text for notes, the glass for a selection (D31:
// the tuner's chosen antenna), the lit green (D15) for a checked
// processing button and for OPERATE on (D30), and the status lights by
// meaning.
//
// The applet cases pose their states after the window is shown and the
// live pass has run, through the applets' own setters or a tuner model
// fed injected report lines; nothing connects to an amplifier, a tuner or
// a radio, and nothing starts discovery.
//
// Modification history (NereusSDR):
//   2026-10-09 - Written for the colour round on Settings pages, dialogs
//                 and applet states. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-10 - JJ's D30 (OPERATE on is the lit green) and D31 (the
//                 tuner's chosen antenna is the selection glass), the
//                 status lights by meaning and the settled items of the
//                 colour round. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-10 - The connection light's four states (stale is the warn
//                 colour). J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-15 - PS-A's lit fill is sampled beside its label, not at the
//                 center, now that DUP narrows it; the unlit button fails
//                 the same check. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.

#include <QtTest/QtTest>

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStyleFactory>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <optional>

#include "core/AppSettings.h"
#include "core/TgxlConnection.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h"
#include "fakes/RemoteWindowHarness.h"
#include "gui/ConnectionPanel.h"
#include "gui/PsaIndicatorWidget.h"
#include "gui/VaxFirstRunDialog.h"
#include "gui/applets/AmpApplet.h"
#include "gui/applets/Rf2ksApplet.h"
#include "gui/applets/TunerApplet.h"
#include "gui/applets/TxApplet.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerPreviewWidget.h"
#include "gui/diagnostics/RadioStatusPage.h"
#include "gui/meters/MeterPoller.h"
#include "gui/setup/hardware/AntennaAlexAlex2Tab.h"
#include "gui/setup/TransmitSetupPages.h"
#include "gui/setup/hardware/Hl2IoBoardTab.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspaceComponents.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/DiversityRadarWidget.h"
#include "models/RadioModel.h"
#include "models/TunerModel.h"

using namespace NereusSDR;

namespace {

QString role(const char* name)
{
    return Workspace::WorkspacePalette::glassAndDepth().color(QString::fromLatin1(name)).name();
}

// The decided status tones (D21).
constexpr auto kOk = "#4caf6a";
constexpr auto kWarn = "#d8a55f";
constexpr auto kDanger = "#c25a5c";

int distance(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green())
        + std::abs(a.blue() - b.blue());
}

// Shown, with the live pass done.
void showAndSettle(QWidget& w)
{
    w.show();
    QApplication::processEvents();
    QTest::qWait(30);
    QApplication::processEvents();
}

QColor centre(QWidget* w)
{
    const QImage img = w->grab().toImage();
    return img.pixelColor(img.width() / 2, img.height() / 2);
}

// A pixel of a button's fill at mid-height, halfway between its left edge
// and where its centered label starts, so a narrow button's label never
// covers the sample.
QColor fillBesideLabel(QPushButton* b)
{
    const QImage img = b->grab().toImage();
    const int labelLeft = (b->width() - b->fontMetrics().horizontalAdvance(b->text())) / 2;
    return img.pixelColor(qMax(2, labelLeft / 2), img.height() / 2);
}

QPushButton* buttonWithText(QWidget& root, const QString& text)
{
    for (auto* b : root.findChildren<QPushButton*>()) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

} // namespace

class TstWorkspaceSettingsColours : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(NereusSDR::Test::RemoteWindowHarness::useIsolatedProfile(QStringLiteral("workspace-settings-colours")));
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        WorkspacePreview::setEnabledForTest(true);
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(app != nullptr);
        applyDarkPalette(*app);
        applyAppBaselineQss(*app);
        applyWorkspacePalette(*app);
    }

    // The mechanism: a sheet a shown widget sets later, on a state
    // change, is translated by the live pass (WorkspacePreview.cpp
    // LiveThemingFilter, StyleChange). Holds before this round too.
    void sheetSetAfterShowIsTranslated()
    {
        QWidget host;
        auto* button = new QPushButton(QStringLiteral("state"), &host);
        showAndSettle(host);
        button->setStyleSheet(QStringLiteral("QPushButton { background: #172534; }"));
        QApplication::processEvents();
        QVERIFY2(!button->styleSheet().contains(QLatin1String("#172534")),
                 qPrintable(button->styleSheet()));
        // The row's role (button), raised by componentize().
        QVERIFY2(button->styleSheet().contains(QLatin1String("qlineargradient")), qPrintable(button->styleSheet()));
    }

    // Item 1: a disabled 4O3A page paints the workspace page, not the
    // platform's disabled window (AppTheme.h applyWorkspacePalette).
    void disabledPageKeepsTheWorkspaceSurface()
    {
        const QPalette p = QApplication::palette();
        QCOMPARE(p.color(QPalette::Disabled, QPalette::Window).name(), role("app-bg"));
        QWidget page;
        page.setAutoFillBackground(true);
        page.resize(40, 40);
        page.setEnabled(false);
        showAndSettle(page);
        QCOMPARE(centre(&page).name(), role("app-bg"));
    }

    // Item 1: the PGXL and TGXL warnings are the decided warn.
    void fourO3AWarningsAreWarn()
    {
        for (const char* sheet : { "color: #e8c01e;", "color: #e87c1e; font-style: italic;" }) {
            const QString look = WorkspacePreview::lookQss(QString::fromLatin1(sheet));
            QVERIFY2(look.contains(QLatin1String(kWarn)), qPrintable(look));
        }
    }

    // Item 2: the NNR session chip is a warning badge: warn text on the
    // badge's warn ground, no edge of its own.
    void nnrChipIsAWarningBadge()
    {
        const QString look = WorkspacePreview::lookQss(QStringLiteral(
            "QLabel { color: #ffd166; background: #302810; border: 1px solid #806820;"
            " border-radius: 3px; padding: 3px; font-size: 10px; font-weight: bold; }"));
        QVERIFY2(look.contains(QLatin1String(kWarn)), qPrintable(look));
        QVERIFY2(look.contains(QStringLiteral("background: %1").arg(role("badge-warn-bg"))), qPrintable(look));
        QVERIFY2(look.contains(QStringLiteral("solid %1").arg(role("badge-warn-bg"))), qPrintable(look));
    }

    // Item 3: Reset is danger; the lit state-machine step is ok, the
    // others the surface; the status bar the surface.
    void hl2IoResetAndStepsTakeTheirMeaning()
    {
        RadioModel radio;
        Hl2IoBoardTab tab(&radio);
        showAndSettle(tab);
        QPushButton* reset = buttonWithText(tab, QStringLiteral("Reset I/O board"));
        QVERIFY(reset != nullptr);
        QVERIFY2(reset->styleSheet().contains(QLatin1String(kDanger)), qPrintable(reset->styleSheet()));
        int lit = 0;
        int off = 0;
        for (auto* f : tab.findChildren<QFrame*>()) {
            const QString s = f->styleSheet();
            if (!s.contains(QLatin1String("padding: 2px"))) {
                continue;
            }
            QVERIFY2(!s.contains(QLatin1String("#22cc44")) && !s.contains(QLatin1String("#222")),
                     qPrintable(s));
            if (s.contains(QStringLiteral("2px solid %1").arg(QLatin1String(kOk)))) {
                ++lit;
            } else if (s.contains(role("panel"))) {
                ++off;
            }
        }
        QCOMPARE(lit, 1);
        QCOMPARE(off, 11);
    }

    // Item 4: the PA Gain note is the secondary text.
    void paGainNoteIsSecondaryText()
    {
        const QString look = WorkspacePreview::lookQss(QStringLiteral("QLabel { color: #80b0d0; padding: 8px; }"));
        QVERIFY2(look.contains(role("text-secondary")), qPrintable(look));
    }

    // Item 5: the Radio Status bar is a panel on the page; "No issues
    // found" is ok.
    void radioStatusBarIsAPanel()
    {
        RadioModel radio;
        RadioStatusPage page(&radio);
        showAndSettle(page);
        bool bar = false;
        for (auto* f : page.findChildren<QFrame*>()) {
            const QString s = f->styleSheet();
            QVERIFY2(!s.contains(QLatin1String("#0a1a10")) && !s.contains(role("ok-bg")), qPrintable(s));
            bar = bar || (s.contains(QStringLiteral("background: %1").arg(role("panel")))
                          && s.contains(QStringLiteral("solid %1").arg(role("border"))));
        }
        QVERIFY(bar);
        for (auto* list : page.findChildren<QListWidget*>()) {
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->text().startsWith(QLatin1String("No issues found"))) {
                    QCOMPARE(list->item(i)->foreground().color().name(), QString::fromLatin1(kOk));
                }
            }
        }
    }

    // Item 6: the polar plot's background is the container's surface (D22).
    // Inside the sensitivity lobe the sample is the lobe's translucent ink
    // (0,220,255,60; DiversityRadarWidget.cpp:220-222) over that surface;
    // the ink itself is unchanged.
    void diversityPlotBackgroundIsTheSurface()
    {
        DiversityRadarWidget radar;
        radar.resize(200, 200);
        showAndSettle(radar);
        const QImage img = radar.grab().toImage();
        const QColor panel(role("panel"));
        const double a60 = 60.0 / 255.0;
        const QColor lobeOverPanel(int(std::lround(panel.red() * (1 - a60))),
                                   int(std::lround(panel.green() * (1 - a60) + 220 * a60)),
                                   int(std::lround(panel.blue() * (1 - a60) + 255 * a60)));
        const double r = (200 - 4) * 0.5;
        int surface = 0;
        QStringList seen;
        for (double k : { 0.4, 0.8 }) {
            for (double deg : { 30.0, 150.0, 210.0, 330.0 }) {
                const double a = qDegreesToRadians(deg);
                const QColor px = img.pixelColor(int(100 + std::cos(a) * r * k),
                                                 int(100 + std::sin(a) * r * k));
                surface += (distance(px, panel) <= 3 || distance(px, lobeOverPanel) <= 3) ? 1 : 0;
                seen << px.name();
            }
        }
        QVERIFY2(surface >= 7, qPrintable(QStringLiteral("%1 of 8: %2").arg(surface).arg(seen.join(u' '))));
    }

    // Item 7: the VAX product captions are the secondary text, not the
    // selection blue; the "not detected" emphasis is warn.
    void vaxCaptionsAndEmphasis()
    {
        VaxFirstRunDialog dialog(FirstRunScenario::MacNative, {});
        showAndSettle(dialog);
        int captions = 0;
        bool warn = false;
        for (auto* l : dialog.findChildren<QLabel*>()) {
            const QString s = l->styleSheet();
            if (s.contains(QLatin1String("letter-spacing"))) {
                QVERIFY2(!s.contains(role("accent")), qPrintable(s));
                captions += s.contains(role("text-secondary")) ? 1 : 0;
            }
            warn = warn || l->text().contains(QStringLiteral("color:%1'>The HAL plugin").arg(QLatin1String(kWarn)));
        }
        QVERIFY(captions > 0);
        QVERIFY(warn);
    }

    // Item 8: "Currently selected" reads as the page's secondary text.
    void alex2SelectedLabelIsSecondaryText()
    {
        RadioModel radio;
        AntennaAlexAlex2Tab tab(&radio);
        showAndSettle(tab);
        int found = 0;
        for (auto* l : tab.findChildren<QLabel*>()) {
            if (l->text().startsWith(QLatin1String("Currently selected"))) {
                QVERIFY2(l->styleSheet().contains(role("text-secondary")), qPrintable(l->styleSheet()));
                ++found;
            }
        }
        QCOMPARE(found, 1);
    }

    // Item 9: the Container Settings preview paints the window's content
    // background (ContainerWidget::contentBackground()), and a preview
    // tile lets it show through.
    void containerPreviewPaintsTheWindowBackground()
    {
        ContainerContentRegistry registry;
        MeterPoller poller;
        ContainerPreviewWidget preview(registry, poller);
        ContainerDocument doc;
        doc.layout = ContentLayout::VerticalStack;
        doc.contents.append(registry.makeEntry(QStringLiteral("applet:s_meter")));
        preview.setDocument(doc);
        preview.resize(300, 240);
        showAndSettle(preview);
        QVERIFY2(preview.styleSheet().contains(role("panel")), qPrintable(preview.styleSheet()));
        bool tile = false;
        for (auto* w : preview.findChildren<QWidget*>()) {
            QVERIFY2(!w->styleSheet().contains(QLatin1String("#172534")), qPrintable(w->styleSheet()));
            tile = tile || w->styleSheet().startsWith(QLatin1String("background:transparent"));
        }
        QVERIFY(tile);
        QCOMPARE(centre(&preview).name(), role("panel"));
    }

    // JJ's live build: PS-A checked after the pass is the lit green (D15).
    void psaCheckedAfterShowIsLit()
    {
        RadioModel radio;
        TxApplet tx(&radio);
        showAndSettle(tx);
        auto* psa = tx.findChild<QPushButton*>(QStringLiteral("TxAppletPsaBtn"));
        QVERIFY(psa != nullptr);
        psa->setVisible(true);
        QApplication::processEvents();
        // The same sample on the unlit button is not green, so the checks
        // below can fail.
        const QColor unlit = fillBesideLabel(psa);
        QVERIFY2(!(unlit.green() > unlit.red() && unlit.green() > unlit.blue()),
                 qPrintable(unlit.name()));
        {
            const QSignalBlocker block(psa);
            psa->setChecked(true);
        }
        QApplication::processEvents();
        QVERIFY2(!psa->styleSheet().contains(QLatin1String("#006030")), qPrintable(psa->styleSheet()));
        const QColor px = fillBesideLabel(psa);
        QVERIFY2(distance(px, QColor(0x00, 0x60, 0x30)) > 20, qPrintable(px.name()));
        QVERIFY2(px.green() > px.red() && px.green() > px.blue(), qPrintable(px.name()));
    }

    // D30 (JJ, 2026-10-10): OPERATE on, posted after the pass, is the
    // raised lit green of PS-A on (D15), in all three applets; it stays
    // lit under the pointer, and the buttons stay plain push buttons.
    void operateOnAfterShowIsLitGreen()
    {
        const auto isLit = [](QPushButton* op) {
            const QString s = op->styleSheet();
            for (const char* part : { "#306247", "#254d38", "#1d3d2c", "color: #bbe1c6",
                                      "border-color: #3e7e5c", "border-top-color: #48946b" }) {
                if (!s.contains(QLatin1String(part))) {
                    return QStringLiteral("no %1 in: %2").arg(QLatin1String(part), s);
                }
            }
            if (s.contains(QLatin1String("#006030")) || s.contains(role("dsp-on-border"))) {
                return QStringLiteral("flat engaged green in: %1").arg(s);
            }
            if (op->isCheckable()) {
                return QStringLiteral("checkable");
            }
            const QColor px = op->grab().toImage().pixelColor(3, op->height() / 2);
            if (distance(px, QColor(QStringLiteral("#254d38"))) > 24) {
                return QStringLiteral("pixel %1").arg(px.name());
            }
            return QString();
        };
        {
            TgxlConnection connection;
            TunerModel tuner;
            tuner.bindConnection(&connection);
            TunerApplet applet(nullptr, &tuner);
            showAndSettle(applet);
            connection.injectLineForTesting(QStringLiteral("V1.2.17"));
            connection.injectLineForTesting(QStringLiteral("S0|state state=1 bypass=0"));
            QApplication::processEvents();
            QPushButton* op = applet.operateButtonForTesting();
            QCOMPARE(op->text(), QStringLiteral("OPERATE"));
            QVERIFY2(isLit(op).isEmpty(), qPrintable(isLit(op)));
            // Standby again: the lit green goes with the state.
            connection.injectLineForTesting(QStringLiteral("S0|state state=0 bypass=0"));
            QApplication::processEvents();
            QCOMPARE(op->text(), QStringLiteral("STANDBY"));
            QVERIFY2(!op->styleSheet().contains(QLatin1String("#306247")), qPrintable(op->styleSheet()));
        }
        {
            AmpApplet applet(nullptr);
            showAndSettle(applet);
            applet.setState(QStringLiteral("OPERATE"));
            QApplication::processEvents();
            QPushButton* op = buttonWithText(applet, QStringLiteral("OPERATE"));
            QVERIFY(op != nullptr);
            QVERIFY2(isLit(op).isEmpty(), qPrintable(isLit(op)));
            applet.setState(QStringLiteral("STANDBY"));
            QApplication::processEvents();
            QVERIFY2(!op->styleSheet().contains(QLatin1String("#306247")), qPrintable(op->styleSheet()));
        }
        {
            Rf2ksApplet applet(nullptr);
            showAndSettle(applet);
            applet.setOperateMode(QStringLiteral("OPERATE"));
            QApplication::processEvents();
            QPushButton* op = buttonWithText(applet, QStringLiteral("OPERATE"));
            QVERIFY(op != nullptr);
            QVERIFY2(isLit(op).isEmpty(), qPrintable(isLit(op)));
            applet.setOperateMode(QStringLiteral("STANDBY"));
            QApplication::processEvents();
            QVERIFY2(!op->styleSheet().contains(QLatin1String("#306247")), qPrintable(op->styleSheet()));
        }
    }

    // D31 (JJ, 2026-10-10): the tuner's chosen antenna, posted after the
    // pass, is the blue selection glass; the others are raised buttons.
    // The buttons stay plain push buttons: a click on the chosen one
    // keeps it chosen, and the chosen state follows the tuner's report.
    void chosenTunerAntennaIsSelectionGlass()
    {
        TgxlConnection connection;
        TunerModel tuner;
        tuner.bindConnection(&connection);
        TunerApplet applet(nullptr, &tuner);
        showAndSettle(applet);
        // The antenna row shows for a TGXL with the switch, reached
        // here as a station state: model fields only, no connection
        // (TunerModel.cpp:39-80).
        TunerModel::StationConnectionState station;
        station.phase = TunerModel::ConnectionPhase::Connected;
        tuner.setStationConnectionState(station);
        connection.injectLineForTesting(QStringLiteral("V1.2.17"));
        connection.injectLineForTesting(QStringLiteral("S0|state state=1 bypass=0 antA=1 3way=1"));
        QApplication::processEvents();
        const auto glass = [](QPushButton* b) {
            const QString s = b->styleSheet();
            return s.contains(QLatin1String("#2f5f92")) && s.contains(QLatin1String("color: #dbe9f8"))
                && s.contains(QLatin1String("border-color: #3d78b0")) && !s.contains(QLatin1String("#006030"));
        };
        QPushButton* ant[3] = { applet.antennaButtonForTesting(1), applet.antennaButtonForTesting(2),
                                applet.antennaButtonForTesting(3) };
        for (auto* b : ant) {
            QVERIFY(b != nullptr);
            QVERIFY(!b->isCheckable());
        }
        QVERIFY(ant[1]->isVisibleTo(&applet));
        QVERIFY2(glass(ant[1]), qPrintable(ant[1]->styleSheet()));
        QVERIFY2(!glass(ant[0]) && !glass(ant[2]), qPrintable(ant[0]->styleSheet()));
        QVERIFY2(!ant[1]->styleSheet().contains(role("dsp-on-bg")), qPrintable(ant[1]->styleSheet()));
        // Rendered: the glass runs #2f5f92 to #1e3d5f, blue leading.
        const QColor px = ant[1]->grab().toImage().pixelColor(3, ant[1]->height() / 2);
        QVERIFY2(px.blue() > px.green() && px.green() > px.red() && px.blue() >= 0x5f, qPrintable(px.name()));
        const QColor other = ant[0]->grab().toImage().pixelColor(3, ant[0]->height() / 2);
        QVERIFY2(other.blue() < 0x40, qPrintable(other.name()));
        // A click on the chosen antenna asks the tuner for it again and
        // changes nothing here.
        ant[1]->click();
        QApplication::processEvents();
        QVERIFY2(glass(ant[1]), qPrintable(ant[1]->styleSheet()));
        QVERIFY(!glass(ant[0]) && !glass(ant[2]));
        // A click on another antenna only asks: the glass moves when the
        // tuner reports the change, not before.
        ant[2]->click();
        QApplication::processEvents();
        QVERIFY(glass(ant[1]) && !glass(ant[2]));
        connection.injectLineForTesting(QStringLiteral("S0|state state=1 bypass=0 antA=2 3way=1"));
        QApplication::processEvents();
        QVERIFY2(glass(ant[2]), qPrintable(ant[2]->styleSheet()));
        QVERIFY2(!glass(ant[0]) && !glass(ant[1]), qPrintable(ant[1]->styleSheet()));
    }

    // The status lights by meaning (D21, settled 2026-10-10): on or
    // connected is ok, disconnected or a fault is danger, off is the
    // inactive text colour. Every state of each light.
    void statusLightsTakeTheirMeaning()
    {
        const QString off = role("text-inactive");
        QCOMPARE(off, QStringLiteral("#58585e"));
        // The RF2K-S connection dot.
        {
            Rf2ksApplet applet(nullptr);
            showAndSettle(applet);
            int dots = 0;
            for (bool connected : { true, false }) {
                applet.setConnectedState(connected);
                QApplication::processEvents();
                for (auto* l : applet.findChildren<QLabel*>()) {
                    const QString s = l->styleSheet();
                    if (s.contains(QLatin1String("border-radius:5px")) || s.contains(QLatin1String("border-radius: 5px"))) {
                        QVERIFY2(s.contains(QLatin1String(connected ? kOk : kDanger)), qPrintable(s));
                        ++dots;
                    }
                }
            }
            QCOMPARE(dots, 2);
        }
        // The sheets the other lights set, state by state, through the
        // translation every sheet takes (WorkspacePreview::lookQss).
        struct Light {
            const char* what;
            const char* sheet;
            QString want;
        };
        const Light lights[] = {
            { "sound system dot, working", "QLabel { background: #33dd88; border-radius: 4px; }",
              QLatin1String(kOk) },
            { "sound system dot, problem", "QLabel { background: #e04848; border-radius: 4px; }",
              QLatin1String(kDanger) },
            { "Alex-2 board LED, active", "QLabel { background: #00cc00; border-radius: 5px; }",
              QLatin1String(kOk) },
            { "Alex-2 board LED, not detected", "QLabel { background: #666666; border-radius: 5px; }", off },
            { "Alex filter LED, lit", "QFrame { background: #00cc44; border-radius: 5px; }",
              QLatin1String(kOk) },
            { "Alex filter LED, off", "QFrame { background: #444444; border-radius: 5px; }", off },
            { "HL2 I/O LED, on", "QFrame { background: #22cc44; border-radius: 6px; }", QLatin1String(kOk) },
            { "HL2 I/O LED, off", "QFrame { background: #606060; border-radius: 6px; }", off },
            { "HL2 not throttled", "font-size: 10px; color: #22cc44;", QLatin1String(kOk) },
            { "speech chain dot, on", "QLabel { color: #20e070; font-size: 14px; font-weight: bold; }",
              QLatin1String(kOk) },
        };
        for (const Light& l : lights) {
            const QString look = WorkspacePreview::lookQss(QString::fromLatin1(l.sheet));
            QVERIFY2(look.contains(l.want), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(l.what), look)));
        }
    }

    // The HL2 throttle line: throttled is a fault (danger), not throttled
    // is ok, and "Unavailable" carries no colour.
    void hl2ThrottleLineTakesItsMeaning()
    {
        RadioModel radio;
        Hl2IoBoardTab tab(&radio);
        showAndSettle(tab);
        tab.showThrottleStateForTest(std::nullopt);
        QApplication::processEvents();
        QCOMPARE(tab.throttleStatusTextForTest(), QStringLiteral("Unavailable"));
        QVERIFY2(!tab.throttleStatusSheetForTest().contains(QLatin1String("color")),
                 qPrintable(tab.throttleStatusSheetForTest()));
        tab.showThrottleStateForTest(true);
        QApplication::processEvents();
        QVERIFY2(tab.throttleStatusSheetForTest().contains(QStringLiteral("color: %1").arg(QLatin1String(kDanger))),
                 qPrintable(tab.throttleStatusSheetForTest()));
        tab.showThrottleStateForTest(false);
        QApplication::processEvents();
        QVERIFY2(tab.throttleStatusSheetForTest().contains(QStringLiteral("color: %1").arg(QLatin1String(kOk))),
                 qPrintable(tab.throttleStatusSheetForTest()));
    }

    // The speech chain's stage dots: a stage that is on is ok, one that
    // is off is the inactive text colour.
    void speechChainDotsTakeTheirMeaning()
    {
        RadioModel radio;
        SpeechProcessorPage page(&radio);
        showAndSettle(page);
        int on = 0;
        int offDots = 0;
        for (auto* l : page.findChildren<QLabel*>()) {
            if (!l->objectName().startsWith(QLatin1String("dot_"))) {
                continue;
            }
            const QString s = l->styleSheet();
            QVERIFY2(!s.contains(QLatin1String("#20e070")) && !s.contains(QLatin1String("#607080")), qPrintable(s));
            if (s.contains(QLatin1String(kOk))) {
                ++on;
            } else if (s.contains(role("text-inactive"))) {
                ++offDots;
            } else {
                QFAIL(qPrintable(l->objectName() + QLatin1String(": ") + s));
            }
        }
        QVERIFY2(on >= 1 && offDots >= 1, qPrintable(QStringLiteral("on %1 off %2").arg(on).arg(offDots)));
    }

    // Settled 2026-10-10: an output pin that is off (the HL2 tabs' pin
    // LEDs and the OC strip) is filled with the border colour, not the
    // legacy #222.
    void offPinLedsAreTheBorderColour()
    {
        const QString look = WorkspacePreview::lookQss(QStringLiteral(
            "QFrame { background: #222; border: 1px solid #555; border-radius: 5px; }"));
        QVERIFY2(look.contains(QStringLiteral("background: %1").arg(role("border"))), qPrintable(look));
        QCOMPARE(role("border"), QStringLiteral("#2c2c31"));
        RadioModel radio;
        Hl2IoBoardTab tab(&radio);
        showAndSettle(tab);
        int pins = 0;
        for (auto* f : tab.findChildren<QFrame*>()) {
            const QString s = f->styleSheet();
            if (s.contains(QLatin1String("border-radius: 5px")) && s.contains(QLatin1String("1px solid"))) {
                QVERIFY2(s.contains(QStringLiteral("background: %1").arg(role("border"))), qPrintable(s));
                ++pins;
            }
        }
        QVERIFY(pins > 0);
    }

    // Settled 2026-10-10: a checked radio whose sheet draws a 2 px rim
    // (the NR2 page, DspSetupPages.cpp:875-880) has the checked radio's
    // rim all round; the 1 px light top edge on it drew a second ring
    // and a blend where the two met (#4885be).
    void wideRadioRimIsTheCheckedRim()
    {
        QWidget host;
        auto* radio = new QRadioButton(QStringLiteral("Pre-AGC"), &host);
        radio->setStyleSheet(QStringLiteral(
            "QRadioButton { color: #c8d8e8; font-size: 12px; }"
            "QRadioButton::indicator { width: 14px; height: 14px; }"
            "QRadioButton::indicator:unchecked { border: 2px solid #304050; "
            "border-radius: 7px; background: #1a2a3a; }"
            "QRadioButton::indicator:checked { border: 2px solid #00b4d8; "
            "border-radius: 7px; background: #00b4d8; }"));
        radio->setChecked(true);
        host.resize(120, 40);
        showAndSettle(host);
        const QString s = radio->styleSheet();
        QVERIFY2(s.contains(QLatin1String("border-color: #3d78b0")), qPrintable(s));
        QVERIFY2(!s.contains(QLatin1String("#4b88c1")), qPrintable(s));
        const QImage img = radio->grab().toImage();
        int rim = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QString px = img.pixelColor(x, y).name();
                QVERIFY2(px != QLatin1String("#4885be") && px != QLatin1String("#4b88c1"),
                         qPrintable(QStringLiteral("%1 at %2,%3").arg(px).arg(x).arg(y)));
                rim += px == QLatin1String("#3d78b0") ? 1 : 0;
            }
        }
        QVERIFY2(rim > 20, qPrintable(QString::number(rim)));
        // The decided 1 px radio keeps its light top edge.
        const QString decided = Workspace::Components::indicatorRulesFor(true);
        QVERIFY2(decided.contains(QLatin1String("border-top-color: #4b88c1")), qPrintable(decided));
    }

    // Settled 2026-10-10: the filled part of a progress bar no sheet
    // styles (the LAN scan's) is the selection glass, not the platform
    // style's lightened highlight (#3870ad at the top); the bar keeps its
    // size.
    void progressChunkIsTheSelectionGlass()
    {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* bar = new QProgressBar(&host);
        bar->setRange(0, 100);
        bar->setValue(100);
        bar->setTextVisible(false);
        layout->addWidget(bar);
        host.resize(300, 60);
        const QSize before = bar->sizeHint();
        showAndSettle(host);
        QCOMPARE(bar->sizeHint(), before);
        QVERIFY2(bar->styleSheet().contains(QLatin1String("QProgressBar::chunk")), qPrintable(bar->styleSheet()));
        const QImage img = bar->grab().toImage();
        const QColor top(QStringLiteral("#2f5f92"));
        const QColor bottom(QStringLiteral("#1e3d5f"));
        int glass = 0;
        int lightened = 0;
        const int x = img.width() / 2;
        for (int y = 0; y < img.height(); ++y) {
            const QColor px = img.pixelColor(x, y);
            lightened += distance(px, QColor(QStringLiteral("#3870ad"))) <= 6 ? 1 : 0;
            glass += (px.blue() <= top.blue() + 1 && px.blue() >= bottom.blue() - 1
                      && px.red() <= top.red() + 1 && px.red() >= bottom.red() - 1
                      && px.blue() > px.green() && px.green() > px.red())
                ? 1 : 0;
        }
        QCOMPARE(lightened, 0);
        QVERIFY2(glass >= img.height() - 4, qPrintable(QStringLiteral("%1 of %2").arg(glass).arg(img.height())));
    }

    // The percent text stays where the platform style drew it, in the
    // middle of the bar; a sheet on a QProgressBar left-aligns it unless
    // told otherwise.
    void progressTextStaysCentred()
    {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* bar = new QProgressBar(&host);
        bar->setRange(0, 100);
        bar->setValue(0);
        layout->addWidget(bar);
        host.resize(400, 60);
        showAndSettle(host);
        QVERIFY2(bar->styleSheet().contains(QLatin1String("QProgressBar::chunk")), qPrintable(bar->styleSheet()));
        const QImage img = bar->grab().toImage();
        int left = img.width();
        int right = -1;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (img.pixelColor(x, y).lightness() > 120) {
                    left = qMin(left, x);
                    right = qMax(right, x);
                }
            }
        }
        QVERIFY2(right >= 0, "no percent text drawn");
        const int centre = (left + right) / 2;
        QVERIFY2(qAbs(centre - img.width() / 2) <= 6,
                 qPrintable(QStringLiteral("text spans %1..%2 of %3").arg(left).arg(right).arg(img.width())));
    }

    // The PureSignal banner's status fills are the decided tones (D21).
    void pureSignalBadgeIsStatus()
    {
        PsaIndicatorWidget badge(nullptr);
        showAndSettle(badge);
        badge.setPsEnabled(true);
        badge.setMox(false);
        QApplication::processEvents();
        QCOMPARE(badge.psBackgroundColor().name(), QString::fromLatin1(kOk));
    }

    // The connection light (ConnectionPanel's state pill), every state:
    // connected and online are ok, offline is danger (D21), and stale is
    // the decided warn colour (settled 2026-10-10, listed to JJ for veto).
    // The sheet a row writes keeps today's literals, so the look off is
    // unchanged; with the look on the translation gives the meaning.
    void connectionLightTakesItsMeaning()
    {
        // Warn is the palette's constant, not a literal of this test.
        const QString warn = role("measured");
        QCOMPARE(warn, QString::fromLatin1(Workspace::kAmberText));
        QCOMPARE(warn, QString::fromLatin1(kWarn));
        struct State {
            ConnectionPanel::StatePill pill;
            const char* what;
            const char* written;
            QString want;
        };
        const State states[] = {
            { ConnectionPanel::StatePill::Connected, "connected", "#39c167", QLatin1String(kOk) },
            { ConnectionPanel::StatePill::Online, "online", "#39c167", QLatin1String(kOk) },
            { ConnectionPanel::StatePill::Stale, "stale", "#d39c2a", warn },
            { ConnectionPanel::StatePill::Offline, "offline", "#c14848", QLatin1String(kDanger) },
        };
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        showAndSettle(host);
        for (const State& s : states) {
            const QString sheet = ConnectionPanel::pillStyleSheet(s.pill);
            // As written (what the look off draws).
            QVERIFY2(sheet.contains(QLatin1String(s.written)), qPrintable(sheet));
            // Computed: the translation every sheet takes.
            const QString look = WorkspacePreview::lookQss(sheet);
            QVERIFY2(look.contains(s.want), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(s.what), look)));
            // Rendered: a pill made after the window shows, as a row's
            // refresh makes it.
            auto* line = new QHBoxLayout;
            layout->addLayout(line);
            auto* pill = new QWidget(&host);
            line->addWidget(pill);
            line->addWidget(new QLabel(QString::fromLatin1(s.what), &host), 1);
            pill->setStyleSheet(sheet);
            showAndSettle(host);
            QVERIFY2(pill->styleSheet().contains(s.want),
                     qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(s.what), pill->styleSheet())));
            const QColor drawn = centre(pill);
            QVERIFY2(distance(drawn, QColor(s.want)) <= 6,
                     qPrintable(QStringLiteral("%1 draws %2, wants %3").arg(QLatin1String(s.what), drawn.name(), s.want)));
        }

        // NEREUS_CONNECTION_LIGHT_IMAGES names a folder the four states are
        // saved in, with the look on and as written (the look off).
        const QString folder = qEnvironmentVariable("NEREUS_CONNECTION_LIGHT_IMAGES");
        if (!folder.isEmpty()) {
            QVERIFY(host.grab().save(folder + QStringLiteral("/connection-light--look-on.png")));
            WorkspacePreview::setEnabledForTest(false);
            QWidget written;
            auto* writtenLayout = new QVBoxLayout(&written);
            for (const State& s : states) {
                auto* line = new QHBoxLayout;
                writtenLayout->addLayout(line);
                auto* pill = new QWidget(&written);
                pill->setStyleSheet(ConnectionPanel::pillStyleSheet(s.pill));
                line->addWidget(pill);
                line->addWidget(new QLabel(QString::fromLatin1(s.what), &written), 1);
            }
            showAndSettle(written);
            const bool saved = written.grab().save(folder + QStringLiteral("/connection-light--look-off.png"));
            WorkspacePreview::setEnabledForTest(true);
            QVERIFY(saved);
        }

        // The panel itself, discovery skipped by its seam: a radio just
        // seen is online, and its row's pill is ok.
        ConnectionPanel::setDiscoveryStartSkippedForTest(true);
        {
            RadioModel model;
            ConnectionPanel panel(&model);
            showAndSettle(panel);
            RadioInfo info;
            info.name = QStringLiteral("ANAN-G2");
            info.macAddress = QStringLiteral("00:1C:C0:4E:21:0A");
            info.address = QHostAddress(QStringLiteral("192.168.1.20"));
            info.boardType = HPSDRHW::Saturn;
            info.protocol = ProtocolVersion::Protocol2;
            info.firmwareVersion = 27;
            info.adcCount = RadioInfo::adcCountForBoard(info.boardType);
            info.maxReceivers = RadioInfo::maxReceiversForBoard(info.boardType);
            panel.onRadioDiscovered(info);
            showAndSettle(panel);
            int pills = 0;
            for (auto* w : panel.findChildren<QWidget*>()) {
                const QString sheet = w->styleSheet();
                if (sheet.contains(QLatin1String("max-width: 14px"))) {
                    QVERIFY2(sheet.contains(QLatin1String(kOk)), qPrintable(sheet));
                    QVERIFY2(!sheet.contains(QLatin1String("#39c167")), qPrintable(sheet));
                    ++pills;
                }
            }
            QCOMPARE(pills, 1);
        }
        ConnectionPanel::setDiscoveryStartSkippedForTest(false);
    }

    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
    }
};

QTEST_MAIN(TstWorkspaceSettingsColours)
#include "tst_workspace_settings_colours.moc"
