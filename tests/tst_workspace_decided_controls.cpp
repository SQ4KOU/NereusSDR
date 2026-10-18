// no-port-check: NereusSDR-original test for JJ's decided control look in
// the workspace preview (src/gui/styles/WorkspaceComponents componentize()
// and the app-level preview sheet in src/gui/styles/AppTheme.h).
//
// Covers, with the switch on: Longpath's slider (JJ's D29 track: the button
// fill inside a text-inactive rim on all four sides, raised pill handle
// with the accent rim, no filled sub-page), the brighter check
// box and radio off state with its disabled states, the three lit meanings
// (green, blue, amber) with red left as it was, the corner radius from the
// control's height (round(h x 7 / 26)), and 14 px scroll bars with
// Longpath's rounded handle. With the switch off, today's sheets come back
// unchanged.
//
// Modification history (NereusSDR):
//   2026-10-08 - Written for the decided control look. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-10 - The groove is JJ's D29 track, with its disabled rule.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Isolate indicator geometry from the approved font floor.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

#include "gui/StyleConstants.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/QssTranslator.h"
#include "gui/styles/WorkspaceComponents.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/styles/WorkspaceStyle.h"

using namespace NereusSDR;
namespace WC = NereusSDR::Workspace::Components;

namespace {

// The sizes of the approved choice rows (tests/visual/ControlsSheet.cpp).
constexpr int kSliderWidth = 140;
constexpr int kSliderHeight = 18;
constexpr int kIndicatorWidth = 90;
constexpr int kIndicatorHeight = 16;

class ProbeCheckBox : public QCheckBox
{
public:
    using QCheckBox::QCheckBox;
    QRect indicatorRect()
    {
        QStyleOptionButton o;
        initStyleOption(&o);
        return style()->subElementRect(QStyle::SE_CheckBoxIndicator, &o, this);
    }
};

class ProbeRadioButton : public QRadioButton
{
public:
    using QRadioButton::QRadioButton;
    QRect indicatorRect()
    {
        QStyleOptionButton o;
        initStyleOption(&o);
        return style()->subElementRect(QStyle::SE_RadioButtonIndicator, &o, this);
    }
};

class ProbeSlider : public QSlider
{
public:
    using QSlider::QSlider;
    QRect handleRect()
    {
        QStyleOptionSlider o;
        initStyleOption(&o);
        return style()->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderHandle, this);
    }
};

class ProbeScrollBar : public QScrollBar
{
public:
    using QScrollBar::QScrollBar;
    QRect handleRect()
    {
        QStyleOptionSlider o;
        initStyleOption(&o);
        return style()->subControlRect(QStyle::CC_ScrollBar, &o, QStyle::SC_ScrollBarSlider, this);
    }
};

QImage grabOf(QWidget& w)
{
    return w.grab().toImage();
}

// The rim (left edge, middle row) and the fill (centre) of an indicator.
struct IndicatorPixels {
    QColor rim;
    QColor fill;
};

IndicatorPixels indicatorPixels(QWidget& w, const QRect& ind)
{
    const QImage img = grabOf(w);
    return {img.pixelColor(ind.left(), ind.center().y()), img.pixelColor(ind.center())};
}

QString greenChecked()
{
    return Style::greenCheckedStyle();
}

} // namespace

class TestWorkspaceDecidedControls : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        WorkspacePreview::setEnabledForTest(false);
    }

    // D5: Longpath's slider as is, through the shared slider builder.
    void sliderHorizontalIsLongpaths()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString out = Style::sliderHStyle();
        // Groove (D29): 4 px plus a 1 px border, the flat button fill, the
        // text-inactive rim on all four sides, radius 3; disabled, the inset
        // inside the border-muted rim.
        QVERIFY2(out.contains(QStringLiteral("height: 4px; background: #1a1a1e; border-radius: 3px;"
                                             " border: 1px solid #58585e; border-color: #58585e;"
                                             " border-top-color: #58585e; border-bottom-color: #58585e;")),
                 qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("QSlider::groove:horizontal:disabled { background: #050507;"
                                             " border-color: #3d3d41; border-top-color: #3d3d41;"
                                             " border-bottom-color: #3d3d41;}")),
                 qPrintable(out));
        QVERIFY(!out.contains(QStringLiteral("rgba(0, 0, 0, 160)")));
        QVERIFY(!out.contains(QStringLiteral("rgba(255, 255, 255, 18)")));
        // Handle: a 10x14 pill with a 1 px accent rim and a lighter top edge.
        QVERIFY2(out.contains(QStringLiteral("width: 10px")), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("height: 14px")));
        QVERIFY(out.contains(QStringLiteral("margin: -6px 0")));
        QVERIFY(out.contains(WC::raisedFill()));
        QVERIFY(out.contains(QStringLiteral("border: 1px solid #4a7ba8")));
        // shiftL(kAccent, 18), as Longpath's sliderHStyle() computes it
        // (its kSliderStyle constant writes #5e8db8 by hand).
        QVERIFY(out.contains(QStringLiteral("border-top-color: #5e8eb8")));
        QVERIFY(out.contains(QStringLiteral("border-radius: 5px")));
        // Hover and pressed: flat #222227.
        QVERIFY2(out.contains(QStringLiteral("QSlider::handle:horizontal:hover { background: #222227; }")),
                 qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("QSlider::handle:horizontal:pressed { background: #222227; }")));
        // No filled side.
        QVERIFY2(!out.contains(QStringLiteral("::sub-page")), qPrintable(out));
        QVERIFY(!out.contains(QStringLiteral("::add-page")));

        // Rendered at the approved row's size: the groove left of the
        // handle is the flat button fill, the handle has the accent rim.
        ProbeSlider s(Qt::Horizontal);
        s.setRange(0, 100);
        s.setValue(50);
        s.setStyleSheet(out);
        s.resize(kSliderWidth, kSliderHeight);
        const QRect h = s.handleRect();
        const QImage img = grabOf(s);
        const QColor groove = img.pixelColor(h.left() / 2, h.center().y());
        QVERIFY2(groove == QColor(0x1a, 0x1a, 0x1e), qPrintable(groove.name()));
        QCOMPARE(h.height(), 16);
        QCOMPARE(h.width(), 12);
        const QColor rim = img.pixelColor(h.left(), h.center().y());
        QVERIFY2(rim == QColor(QStringLiteral("#4a7ba8")), qPrintable(rim.name()));
    }

    // A legacy sub-page fill (the dark page style) loses its colour.
    void sliderSubPageFillIsCleared()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString out = WorkspacePreview::lookQss(QStringLiteral(
            "QSlider::groove:horizontal { background: #1a1a2e; height: 4px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #00b4d8; width: 12px; margin: -4px 0;"
            " border-radius: 6px; }"
            "QSlider::sub-page:horizontal { background: #00b4d8; border-radius: 2px; }"
            "QSlider::sub-page:horizontal:disabled { background: #2a3040; }"));
        QVERIFY2(out.contains(QStringLiteral("QSlider::sub-page:horizontal { background: transparent;")),
                 qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("QSlider::sub-page:horizontal:disabled { background: transparent;")),
                 qPrintable(out));
        ProbeSlider s(Qt::Horizontal);
        s.setRange(0, 100);
        s.setValue(80);
        s.setStyleSheet(out);
        s.resize(kSliderWidth, kSliderHeight);
        const QRect h = s.handleRect();
        const QColor groove = grabOf(s).pixelColor(h.left() / 2, h.center().y());
        QVERIFY2(groove == QColor(0x1a, 0x1a, 0x1e), qPrintable(groove.name()));
    }

    void sliderVerticalIsLongpaths()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString out = Style::sliderVStyle();
        QVERIFY2(out.contains(QStringLiteral("width: 4px; background: #1a1a1e;")), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("border: 1px solid #58585e")));
        QVERIFY(out.contains(QStringLiteral("QSlider::groove:vertical:disabled { background: #050507;")));
        QVERIFY(out.contains(QStringLiteral("height: 10px")));
        QVERIFY(out.contains(QStringLiteral("width: 14px")));
        QVERIFY(out.contains(QStringLiteral("margin: 0 -6px")));
        QVERIFY(out.contains(QStringLiteral("border: 1px solid #4a7ba8")));
        QVERIFY(!out.contains(QStringLiteral("::add-page")));
        QVERIFY(!out.contains(QStringLiteral("::sub-page")));
    }

    // D6 + D10: the brighter off state, the disabled states and
    // Longpath's blue, on a widget sheet (the shared check box style).
    void checkBoxStatesOnAWidgetSheet()
    {
        // Today's indicator size, switch off.
        ProbeCheckBox today(QStringLiteral("Check box"));
        today.setStyleSheet(QString::fromLatin1(Style::kCheckBoxStyle));
        today.resize(kIndicatorWidth, kIndicatorHeight);
        const QSize todaySize = today.indicatorRect().size();

        WorkspacePreview::setEnabledForTest(true);
        const QString sheet = WorkspacePreview::lookQss(QString::fromLatin1(Style::kCheckBoxStyle));
        QWidget host;
        host.setStyleSheet(QStringLiteral("background: #0c0c0e;"));
        std::array<ProbeCheckBox*, 4> boxes{};
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            boxes[i] = new ProbeCheckBox(QStringLiteral("Check box"), &host);
            boxes[i]->setStyleSheet(sheet);
            boxes[i]->setGeometry(0, int(i) * kIndicatorHeight, kIndicatorWidth, kIndicatorHeight);
        }
        boxes[1]->setChecked(true);
        boxes[2]->setEnabled(false);
        boxes[3]->setChecked(true);
        boxes[3]->setEnabled(false);
        host.resize(kIndicatorWidth, 4 * kIndicatorHeight);

        const IndicatorPixels off = indicatorPixels(*boxes[0], boxes[0]->indicatorRect());
        QVERIFY2(off.rim == QColor(QStringLiteral("#58585e")), qPrintable(off.rim.name()));
        // The fill runs from kInsetBg to kButtonBg: the centre is between.
        QVERIFY2(off.fill.red() > 0x05 && off.fill.red() < 0x1a, qPrintable(off.fill.name()));
        const IndicatorPixels on = indicatorPixels(*boxes[1], boxes[1]->indicatorRect());
        QVERIFY2(on.rim == QColor(QStringLiteral("#3d78b0")), qPrintable(on.rim.name()));
        QVERIFY2(on.fill.blue() - on.fill.red() > 40, qPrintable(on.fill.name()));
        const IndicatorPixels dOff = indicatorPixels(*boxes[2], boxes[2]->indicatorRect());
        QVERIFY2(dOff.rim == QColor(QStringLiteral("#3d3d41")), qPrintable(dOff.rim.name()));
        QVERIFY2(dOff.fill == QColor(QStringLiteral("#050507")), qPrintable(dOff.fill.name()));
        const IndicatorPixels dOn = indicatorPixels(*boxes[3], boxes[3]->indicatorRect());
        QVERIFY2(dOn.rim == QColor(QStringLiteral("#4e4e53")), qPrintable(dOn.rim.name()));
        QVERIFY2(dOn.fill == QColor(QStringLiteral("#4e4e53")), qPrintable(dOn.fill.name()));
        // The row stays 16 px: the indicator keeps today's size.
        QCOMPARE(boxes[0]->indicatorRect().size(), todaySize);
    }

    void radioStatesOnAWidgetSheet()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString sheet = WorkspacePreview::lookQss(QString::fromLatin1(Style::kRadioButtonStyle));
        QWidget host;
        auto* off = new ProbeRadioButton(QStringLiteral("Radio"), &host);
        off->setStyleSheet(sheet);
        off->setAutoExclusive(false);
        off->setGeometry(0, 0, kIndicatorWidth, kIndicatorHeight);
        auto* dOn = new ProbeRadioButton(QStringLiteral("Radio"), &host);
        dOn->setStyleSheet(sheet);
        dOn->setAutoExclusive(false);
        dOn->setChecked(true);
        dOn->setEnabled(false);
        dOn->setGeometry(0, kIndicatorHeight, kIndicatorWidth, kIndicatorHeight);
        host.resize(kIndicatorWidth, 2 * kIndicatorHeight);
        const IndicatorPixels o = indicatorPixels(*off, off->indicatorRect());
        QVERIFY2(o.rim == QColor(QStringLiteral("#58585e")), qPrintable(o.rim.name()));
        const IndicatorPixels d = indicatorPixels(*dOn, dOn->indicatorRect());
        QVERIFY2(d.fill == QColor(QStringLiteral("#4e4e53")), qPrintable(d.fill.name()));
    }

    // D11 + D15: the three meanings and red unchanged.
    void litGreenIsRaisedBright()
    {
        WorkspacePreview::setEnabledForTest(true);
        for (const QString& out : { greenChecked(), Style::dspToggleStyle() }) {
            QVERIFY2(out.contains(QStringLiteral(
                         "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #306247, stop:0.55 #254d38,"
                         " stop:1 #1d3d2c)")),
                     qPrintable(out));
            QVERIFY2(out.contains(QStringLiteral("color: #bbe1c6")), qPrintable(out));
            QVERIFY(out.contains(QStringLiteral("border-color: #3e7e5c")));
            QVERIFY(out.contains(QStringLiteral("border-top-color: #48946b")));
        }
    }

    void litBlueIsLongpathsGlass()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString out = Style::blueCheckedStyle();
        QVERIFY2(out.contains(WC::glassFill()), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("color: #dbe9f8")));
        QVERIFY(out.contains(QStringLiteral("border-color: #3d78b0")));
        // shiftL(kGlassSelBorder, 16), as Longpath's blueCheckedStyle()
        // computes it (#4b88c1 is its check box constant, written by hand).
        QVERIFY2(out.contains(QStringLiteral("border-top-color: #4d89c1")), qPrintable(out));
    }

    void litAmberIsRaised()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString out = Style::amberCheckedStyle();
        const QColor base(QString::fromLatin1(Workspace::kAmberBg));
        const QString fill = QStringLiteral(
            "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:0.55 %2, stop:1 %3)")
            .arg(Workspace::shiftL(base, 16), base.name(), Workspace::shiftL(base, -12));
        QVERIFY2(out.contains(fill), qPrintable(out + QStringLiteral(" / ") + fill));
        QVERIFY(out.contains(QStringLiteral("color: #d8a55f")));
        QVERIFY(out.contains(QStringLiteral("border-color: #6b5426")));
        QVERIFY(out.contains(QStringLiteral("border-top-color: %1")
                                 .arg(Workspace::shiftL(QColor(QStringLiteral("#6b5426")), 16))));
    }

    void litRedIsUnchanged()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString legacy = QStringLiteral(
            "QPushButton:checked { background: %1; color: %2; border: 1px solid %3; }")
            .arg(QLatin1String(Style::kRedBg), QLatin1String(Style::kRedText), QLatin1String(Style::kRedBorder));
        QssTranslator::Options options;
        options.resolveAmbiguousByContext = true;
        const QString translated = QssTranslator::translate(legacy, options).qss;
        QCOMPARE(Style::redCheckedStyle(), translated);
    }

    // D12: the radius from the control's height.
    void radiusFollowsHeight()
    {
        QCOMPARE(WC::radiusFor(19), 5);
        QCOMPARE(WC::radiusFor(20), 5);
        QCOMPARE(WC::radiusFor(22), 6);
        QCOMPARE(WC::radiusFor(23), 6);
        QCOMPARE(WC::radiusFor(26), 7);
        QCOMPARE(WC::radiusFor(30), 8);

        WorkspacePreview::setEnabledForTest(true);
        const auto radiusOn = [](QWidget& w, const QString& qss) {
            return WorkspacePreview::lookQss(qss, &w);
        };
        QPushButton b;
        b.setFixedHeight(20);
        QVERIFY(radiusOn(b, QStringLiteral("QPushButton { background: #1a2a3a; border: 1px solid #205070; }"))
                    .contains(QStringLiteral("border-radius: 5px")));
        QComboBox c;
        c.setFixedHeight(22);
        QVERIFY(radiusOn(c, QStringLiteral("QComboBox { background: #1a2a3a; border: 1px solid #304050; }"))
                    .contains(QStringLiteral("border-radius: 6px")));
        QSpinBox s;
        s.setFixedHeight(26);
        QVERIFY(radiusOn(s, QStringLiteral("QSpinBox { background: #1a2a3a; border: 1px solid #304050; }"))
                    .contains(QStringLiteral("border-radius: 7px")));

        QTabWidget tabs;
        tabs.addTab(new QWidget, QStringLiteral("RX"));
        tabs.findChild<QTabBar*>()->setFixedHeight(30);
        const QString tabOut = radiusOn(tabs, QStringLiteral(
            "QTabWidget::pane { border: 1px solid #304050; background: #0f0f1a; }"
            "QTabBar::tab { background: #1a2a3a; color: #8aa8c0; border: 1px solid #304050;"
            " padding: 4px 10px; }"));
        QVERIFY2(tabOut.contains(QStringLiteral("border-top-left-radius: 8px")), qPrintable(tabOut));
        QVERIFY(tabOut.contains(QStringLiteral("border-top-right-radius: 8px")));
        const QRegularExpression pane(QStringLiteral("QTabWidget::pane \\{[^}]*border-radius: 8px"));
        QVERIFY2(pane.match(tabOut).hasMatch(), qPrintable(tabOut));
    }

    // With the switch off, today's sheets exactly.
    void switchOffKeepsTodaysRules()
    {
        QCOMPARE(Style::sliderHStyle(), QStringLiteral(
            "QSlider::groove:horizontal {"
            "  height: 4px; background: %1; border-radius: 2px;"
            "}"
            "QSlider::handle:horizontal {"
            "  width: 10px; height: 10px; margin: -3px 0;"
            "  background: %2; border-radius: 5px;"
            "}").arg(QLatin1String(Style::kGroove), QLatin1String(Style::kAccent)));
        QCOMPARE(greenChecked(), QStringLiteral(
            "QPushButton:checked { background: %1; color: %2; border: 1px solid %3; }")
            .arg(QLatin1String(Style::kGreenBg), QLatin1String(Style::kGreenText),
                 QLatin1String(Style::kGreenBorder)));
        const QString box = QString::fromLatin1(Style::kCheckBoxStyle);
        QCOMPARE(WorkspacePreview::lookQss(box), box);
        const QString radio = QString::fromLatin1(Style::kRadioButtonStyle);
        QCOMPARE(WorkspacePreview::lookQss(radio), radio);
    }

    // The live pass gives a box no sheet styles the indicator of JJ's
    // indicator rules, and the box keeps today's size: with text, without
    // text, and with a sheet of its own that leaves the indicator alone.
    // A second pass changes nothing. Sets the application's palette and
    // stylesheet, so it runs late.
    void zLivePassKeepsUnstyledBoxSizes()
    {
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(app != nullptr);
        const QPalette savedPalette = app->palette();
        WorkspacePreview::setEnabledForTest(false);
        // Text, no text, and a sheet of its own that leaves the indicator.
        const auto build = [](QWidget& host) {
            auto* box = new QCheckBox(QStringLiteral("Include logs"), &host);
            auto* radio = new QRadioButton(QStringLiteral("Track band"), &host);
            auto* bare = new QCheckBox(&host);
            auto* coloured = new QCheckBox(QStringLiteral("Show on RX"), &host);
            coloured->setStyleSheet(QStringLiteral("QCheckBox { color: #c8d8e8; }"));
            return QList<QAbstractButton*>{ box, radio, bare, coloured };
        };
        QWidget todayHost;
        QList<QSize> today;
        for (QAbstractButton* b : build(todayHost)) {
            today << b->sizeHint();
        }
        WorkspacePreview::setEnabledForTest(true);
        applyWorkspacePalette(*app);
        QWidget host;
        const QList<QAbstractButton*> after = build(host);
        WorkspacePreview::themeSubtree(&host);
        QList<QSize> themed;
        for (QAbstractButton* b : after) {
            themed << b->sizeHint();
        }
        const QString sheet = after.first()->styleSheet();
        WorkspacePreview::themeSubtree(&host);
        const QString again = after.first()->styleSheet();
        app->setStyleSheet(QString());
        app->setPalette(savedPalette);
        QVERIFY2(sheet.contains(QStringLiteral("QCheckBox::indicator")), qPrintable(sheet));
        QCOMPARE(again, sheet);
        QCOMPARE(themed, today);
    }

    // A widget sheet's indicator with a size and no border gets the rim
    // inside that size: the box keeps its size hint at an equal font size.
    void indicatorRimKeepsAGivenSize()
    {
        const QString sheet = QStringLiteral(
            "QCheckBox { color: #c8d8e8; font-size: 10px; }"
            "QCheckBox::indicator { width: 12px; height: 12px; background: #1a2a3a; }");
        WorkspacePreview::setEnabledForTest(false);
        ProbeCheckBox reference(QStringLiteral("Show"));
        QString referenceSheet = sheet;
        referenceSheet.replace(QStringLiteral("font-size: 10px"), QStringLiteral("font-size: 11px"));
        reference.setStyleSheet(referenceSheet);
        reference.ensurePolished();
        WorkspacePreview::setEnabledForTest(true);
        ProbeCheckBox after(QStringLiteral("Show"));
        // Keep the 10 px input: the real look must apply the 11 px floor.
        const QString look = WorkspacePreview::lookQss(sheet, &after);
        after.setStyleSheet(look);
        after.ensurePolished();
        QCOMPARE(after.font().pixelSize(), 11);
        QCOMPARE(after.font(), reference.font());
        QVERIFY2(look.contains(QStringLiteral("width: 10px")), qPrintable(look));
        QVERIFY2(look.contains(QStringLiteral("height: 10px")), qPrintable(look));
        QVERIFY2(look.contains(QStringLiteral("border: 1px solid #58585e")), qPrintable(look));
        QCOMPARE(reference.indicatorRect().size(), QSize(12, 12));
        QCOMPARE(after.indicatorRect().size(), reference.indicatorRect().size());
        QCOMPARE(after.sizeHint(), reference.sizeHint());
        QCOMPARE(WorkspacePreview::lookQss(look, &after), look);
    }

    // The preview: an unstyled check box gets the decided off state from
    // the live pass, and scroll bars stay 14 px with Longpath's rounded
    // handle from the app sheet. Runs last: it sets the application's
    // palette and stylesheet.
    void zAppSheetIndicatorsAndScrollBars()
    {
        WorkspacePreview::setEnabledForTest(true);
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(app != nullptr);
        const QPalette savedPalette = app->palette();
        applyWorkspacePalette(*app);

        QWidget host;
        auto* box = new ProbeCheckBox(QStringLiteral("Check box"), &host);
        box->setGeometry(0, 0, kIndicatorWidth, kIndicatorHeight);
        host.resize(kIndicatorWidth, kIndicatorHeight);
        WorkspacePreview::themeSubtree(&host);
        const QRect ind = box->indicatorRect();
        const IndicatorPixels off = indicatorPixels(*box, ind);
        QVERIFY2(off.rim == QColor(QStringLiteral("#58585e")), qPrintable(off.rim.name()));

        ProbeScrollBar bar(Qt::Vertical);
        bar.setRange(0, 100);
        bar.setPageStep(20);
        bar.setValue(30);
        QCOMPARE(bar.sizeHint().width(), 14);
        bar.resize(14, 200);
        const QRect h = bar.handleRect();
        const QColor handle = grabOf(bar).pixelColor(h.center());
        QVERIFY2(handle == QColor(QStringLiteral("#2c2c31")), qPrintable(handle.name()));

        app->setStyleSheet(QString());
        app->setPalette(savedPalette);
    }

    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
    }
};

QTEST_MAIN(TestWorkspaceDecidedControls)
#include "tst_workspace_decided_controls.moc"
