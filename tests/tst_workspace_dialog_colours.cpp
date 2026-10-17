// no-port-check: NereusSDR-original test for the remaining dialog, menu and
// control colours of the workspace preview (src/gui/styles/WorkspaceComponents
// componentize(), WorkspacePreview's live pass, QssTranslator and the app
// sheet in src/gui/styles/AppTheme.h).
//
// Covers, with the switch on: one selection colour for a translated menu
// sheet and the app theme's menu; the decided slider (D5) on a slider no
// sheet styles, at its size; the decided indicator (D10) on a checkable
// group box no sheet styles, at its size; the spin arrows in the text role;
// one mapping per meaning family (green, amber, red); blue action buttons
// becoming the raised button, the default one marked. With the switch off,
// today's colours exactly.
//
// Modification history (NereusSDR):
//   2026-10-09 - Written for the remaining dialog, menu and control
//                 colours. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-10 - The unstyled slider's groove is JJ's D29 track.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-11 - The unstyled slider is never thinner than the decided
//                 handle: Fusion's 15 px slider (Linux) grows to it.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QFile>
#include <QGroupBox>
#include <QImage>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionGroupBox>
#include <QStyleOptionSlider>
#include <QWidget>

#include "gui/StyleConstants.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/PopupMenuStyle.h"
#include "gui/styles/WorkspaceComponents.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;
namespace WC = NereusSDR::Workspace::Components;

namespace {

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

class ProbeGroupBox : public QGroupBox
{
public:
    using QGroupBox::QGroupBox;
    QRect indicatorRect()
    {
        QStyleOptionGroupBox o;
        initStyleOption(&o);
        return style()->subControlRect(QStyle::CC_GroupBox, &o, QStyle::SC_GroupBoxCheckBox, this);
    }
};

QColor role(const char* r)
{
    return Workspace::WorkspacePalette::glassAndDepth().color(QString::fromLatin1(r));
}

// The application's palette and sheet in the workspace look, put back
// when the guard goes.
class WorkspaceApp
{
public:
    WorkspaceApp()
        : m_app(qobject_cast<QApplication*>(QCoreApplication::instance()))
        , m_palette(m_app->palette())
        , m_sheet(m_app->styleSheet())
    {
        WorkspacePreview::setEnabledForTest(true);
        applyWorkspacePalette(*m_app);
    }
    ~WorkspaceApp()
    {
        m_app->setStyleSheet(m_sheet);
        m_app->setPalette(m_palette);
        WorkspacePreview::setEnabledForTest(false);
    }
    WorkspaceApp(const WorkspaceApp&) = delete;
    WorkspaceApp& operator=(const WorkspaceApp&) = delete;

private:
    QApplication* m_app;
    QPalette m_palette;
    QString m_sheet;
};

// The highlighted row's colour of a menu with one active action, sampled
// left of its text.
QColor menuHighlight(QMenu& menu)
{
    QAction* a = menu.addAction(QStringLiteral("Spot one"));
    menu.addAction(QStringLiteral("Spot two"));
    menu.ensurePolished();
    menu.resize(menu.sizeHint());
    menu.setActiveAction(a);
    const QRect r = menu.actionGeometry(a);
    const QImage img = menu.grab().toImage();
    return img.pixelColor(r.left() + 3, r.center().y());
}

// The sliders' and group boxes' size as a layout gives it.
QSize laidOut(QWidget& w)
{
    return w.sizeHint().expandedTo(w.minimumSize()).boundedTo(w.maximumSize());
}

} // namespace

class TestWorkspaceDialogColours : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        WorkspacePreview::setEnabledForTest(false);
    }

    // Rule 2: a menu with its own translated sheet (the spot cluster menu,
    // a #00b4d8 highlight) and a menu with only the app theme's rules draw
    // the selected item in one colour, the glass selection.
    void menuSelectionIsTheAppThemes()
    {
        WorkspaceApp look;
        QMenu themeMenu;
        WorkspacePreview::themeSubtree(&themeMenu);
        const QColor theme = menuHighlight(themeMenu);
        QCOMPARE(theme, role("selected-top"));

        for (const QString& sheet :
             { QStringLiteral("QMenu { background: #0f0f1a; color: #c8d8e8; border: 1px solid #305070; }"
                              "QMenu::item:selected { background: #1a3a5a; color: #00b4d8; }"),
               QStringLiteral("QMenu { background: #1a2a3a; color: #c8d8e8; border: 1px solid #205070; }"
                              "QMenu::item:selected { background: #00b4d8; color: #0f0f1a; }") }) {
            QMenu own;
            own.setStyleSheet(sheet);
            WorkspacePreview::themeSubtree(&own);
            QCOMPARE(menuHighlight(own), theme);
            QVERIFY2(own.styleSheet().contains(QStringLiteral("color: %1").arg(role("selected-text").name())),
                     qPrintable(own.styleSheet()));
        }
        // A field's text selection takes it too.
        const QString field = WorkspacePreview::lookQss(QStringLiteral(
            "QLineEdit { background: #1a2a3a; selection-background-color: #00b4d8; selection-color: #0f0f1a; }"));
        QVERIFY2(field.contains(QStringLiteral("selection-background-color: %1").arg(role("selected-top").name())),
                 qPrintable(field));
        QVERIFY2(field.contains(QStringLiteral("selection-color: %1").arg(role("selected-text").name())),
                 qPrintable(field));
    }

    // Rule 4, D5: a slider no sheet styles gets the decided groove and
    // handle and keeps the size a layout gives it.
    void unstyledSliderTakesTheDecidedSlider()
    {
        ProbeSlider todayH(Qt::Horizontal);
        ProbeSlider todayV(Qt::Vertical);
        const QSize sizeH = laidOut(todayH);
        const QSize sizeV = laidOut(todayV);

        WorkspaceApp look;
        QWidget host;
        auto* h = new ProbeSlider(Qt::Horizontal, &host);
        auto* v = new ProbeSlider(Qt::Vertical, &host);
        for (ProbeSlider* s : { h, v }) {
            s->setRange(0, 100);
            s->setValue(60);
        }
        WorkspacePreview::themeSubtree(&host);
        // Its length, and across it today's thickness, never thinner than
        // the decided handle (kSliderHandleSpanPx, or the handle is cut
        // off). The macOS style's slider is thicker than the handle and
        // keeps its size exactly; Fusion's horizontal one (15 px, Linux)
        // grows to the handle.
        const int span = Workspace::Components::kSliderHandleSpanPx;
        qInfo() << "style" << QApplication::style()->name() << "unstyled slider today" << sizeH << sizeV
                << "in the look" << laidOut(*h) << laidOut(*v);
        QCOMPARE(laidOut(*h), QSize(sizeH.width(), qMax(sizeH.height(), span)));
        QCOMPARE(laidOut(*v), QSize(qMax(sizeV.width(), span), sizeV.height()));
        const QString sheet = h->styleSheet();
        QVERIFY2(sheet.contains(QStringLiteral("QSlider::handle:horizontal")), qPrintable(sheet));
        // A second pass changes nothing.
        WorkspacePreview::themeSubtree(&host);
        QCOMPARE(h->styleSheet(), sheet);

        h->resize(sizeH.expandedTo(QSize(140, 0)));
        const QRect hr = h->handleRect();
        const QImage img = h->grab().toImage();
        const QColor rim = img.pixelColor(hr.left(), hr.center().y());
        QVERIFY2(rim == role("accent"), qPrintable(rim.name()));
        // No filled side: the groove left of the handle is the D29 track's
        // button fill.
        const QColor groove = img.pixelColor(hr.left() / 2, hr.center().y());
        QVERIFY2(groove == role("button"), qPrintable(groove.name()));
    }

    // Rule 4, D10: a checkable group box no sheet styles gets the decided
    // indicator at today's size.
    void checkableGroupBoxTakesTheDecidedIndicator()
    {
        const auto build = [](QWidget& host) {
            auto* g = new ProbeGroupBox(QStringLiteral("Details"), &host);
            g->setCheckable(true);
            g->setChecked(false);
            auto* l = new QVBoxLayout(g);
            l->addWidget(new QWidget(g));
            return g;
        };
        QWidget todayHost;
        ProbeGroupBox* today = build(todayHost);
        const QSize todaySize = laidOut(*today);
        today->resize(todaySize.expandedTo(QSize(200, 60)));
        const QRect todayInd = today->indicatorRect();

        WorkspaceApp look;
        QWidget host;
        ProbeGroupBox* g = build(host);
        WorkspacePreview::themeSubtree(&host);
        QCOMPARE(laidOut(*g), todaySize);
        g->resize(todaySize.expandedTo(QSize(200, 60)));
        const QRect ind = g->indicatorRect();
        QCOMPARE(ind.size(), todayInd.size());
        const QImage img = g->grab().toImage();
        const QColor rim = img.pixelColor(ind.left(), ind.center().y());
        QVERIFY2(rim == role("text-inactive"), qPrintable(rim.name()));
        const QString sheet = g->styleSheet();
        WorkspacePreview::themeSubtree(&host);
        QCOMPARE(g->styleSheet(), sheet);
    }

    // Rule 1: the spin arrows are drawn in the text role with the switch
    // on, and in today's #c8d8e8 with it off.
    void spinArrowsFollowTheTextRole()
    {
        const auto arrowColour = [](const QString& path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                return QString();
            }
            const QString svg = QString::fromUtf8(f.readAll());
            const qsizetype at = svg.indexOf(QStringLiteral("fill=\""));
            return at < 0 ? QString() : svg.mid(at + 6, 7);
        };
        QCOMPARE(arrowColour(QStringLiteral(":/icons/spin-up.svg")), QStringLiteral("#c8d8e8"));
        QCOMPARE(arrowColour(QStringLiteral(":/icons/spin-down.svg")), QStringLiteral("#c8d8e8"));
        const QString text = role("text").name();
        QCOMPARE(arrowColour(QStringLiteral(":/icons/spin-up-workspace.svg")), text);
        QCOMPARE(arrowColour(QStringLiteral(":/icons/spin-down-workspace.svg")), text);

        const QString sheet = QStringLiteral(
            "QSpinBox::up-arrow { image: url(:/icons/spin-up.svg); width: 10px; height: 10px; }"
            "QSpinBox::down-arrow { image: url(:/icons/spin-down.svg); width: 10px; height: 10px; }");
        QCOMPARE(WorkspacePreview::lookQss(sheet), sheet);
        {
            WorkspaceApp look;
            const QString on = WorkspacePreview::lookQss(sheet);
            QVERIFY2(on.contains(QStringLiteral("url(:/icons/spin-up-workspace.svg)")), qPrintable(on));
            QVERIFY2(on.contains(QStringLiteral("url(:/icons/spin-down-workspace.svg)")), qPrintable(on));
            const QString app = qApp->styleSheet();
            QVERIFY2(app.contains(QStringLiteral("url(:/icons/spin-up-workspace.svg)")), qPrintable(app));
            QVERIFY(!app.contains(QStringLiteral("url(:/icons/spin-up.svg)")));
        }
    }

    // Rule 3, green: a checkable toggle whose sheet lights it green and
    // marks the off state (Spot Hub's Auto-Connect, Spots Enabled) takes the
    // raised lit green when on and the raised button when off.
    void meaningGreenToggle()
    {
        const QString legacy = QStringLiteral(
            "QPushButton { background: #206030; color: white; border: 1px solid #305040; padding: 4px 10px; }"
            "QPushButton:!checked { background: #603020; }");
        WorkspacePreview::setEnabledForTest(true);
        QPushButton b(QStringLiteral("ON"));
        b.setCheckable(true);
        const QString out = WorkspacePreview::lookQss(legacy, &b);
        // On: the lit green of D15.
        QVERIFY2(out.contains(QStringLiteral("stop:0 #306247, stop:0.55 #254d38, stop:1 #1d3d2c")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("color: #bbe1c6")), qPrintable(out));
        // Off: the raised button, no brown.
        const qsizetype off = out.indexOf(QStringLiteral(":!checked"));
        QVERIFY2(off > 0, qPrintable(out));
        const QString offRule = out.mid(off);
        QVERIFY2(offRule.contains(WC::raisedFill()), qPrintable(offRule));
        QVERIFY2(offRule.contains(QStringLiteral("color: %1").arg(role("text").name())), qPrintable(offRule));
        QVERIFY(!out.contains(QStringLiteral("#603020")));
        QCOMPARE(WorkspacePreview::lookQss(out, &b), out);
    }

    // Rule 3, amber: a first-time and warning banner takes the amber family.
    void meaningAmberBanner()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString out = WorkspacePreview::lookQss(QStringLiteral(
            "QLabel { background: #2a2010; color: #ffcc66; border: 1px solid #806020; }"));
        QVERIFY2(out.contains(role("measured-bg").name()), qPrintable(out));
        QVERIFY(out.contains(role("measured").name()));
        QVERIFY(out.contains(role("measured-border").name()));
    }

    // Rule 3, red: a destructive action takes the red family.
    void meaningRedDestructive()
    {
        WorkspacePreview::setEnabledForTest(true);
        QPushButton b(QStringLiteral("Disconnect"));
        const QString out = WorkspacePreview::lookQss(QStringLiteral(
            "QPushButton { background: #4a2020; color: #c8a8a8; border: 1px solid #6a3030; }"
            "QPushButton:hover { background: #602020; }"), &b);
        QVERIFY2(out.contains(role("badge-tx-bg").name()), qPrintable(out));
        QVERIFY(out.contains(role("danger-text").name()));
        QVERIFY(out.contains(role("danger").name()));
        QVERIFY(out.contains(role("danger-bg").name()));
    }

    // Rule 3: a blue action button becomes the raised button with today's
    // ink in the text role; the default button keeps its size and carries
    // the selection rim.
    void blueActionButtonsAreRaised()
    {
        const QString legacy = QStringLiteral(
            "QPushButton { background: #00b4d8; color: #0f0f1a; border-radius: 4px; padding: 6px 16px;"
            " font-weight: bold; }"
            "QPushButton:hover { background: #33c8e8; }");
        QWidget todayHost;
        auto* today = new QPushButton(QStringLiteral("OK"), &todayHost);
        today->setDefault(true);
        today->setStyleSheet(legacy);
        const QSize todaySize = today->sizeHint();

        WorkspacePreview::setEnabledForTest(true);
        QWidget host;
        auto* ok = new QPushButton(QStringLiteral("OK"), &host);
        ok->setDefault(true);
        const QString out = WorkspacePreview::lookQss(legacy, ok);
        ok->setStyleSheet(out);
        QVERIFY2(out.contains(QStringLiteral("QPushButton {")) && out.contains(WC::raisedFill()), qPrintable(out));
        QVERIFY2(!out.contains(WC::glassFill()), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("color: %1").arg(role("text").name())), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral(":default")), qPrintable(out));
        QVERIFY2(out.contains(role("selected-border").name()), qPrintable(out));
        QCOMPARE(ok->sizeHint(), todaySize);
        QCOMPARE(WorkspacePreview::lookQss(out, ok), out);

        // A checkable button lit blue by its own sheet keeps the glass.
        QPushButton lit(QStringLiteral("2.7K"));
        lit.setCheckable(true);
        const QString litOut = WorkspacePreview::lookQss(QStringLiteral(
            "QPushButton { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }"), &lit);
        QVERIFY2(litOut.contains(WC::glassFill()), qPrintable(litOut));
    }

    // With the switch off: today's sheets and colours exactly.
    void switchOffKeepsTodaysColours()
    {
        const QString menu = QString::fromLatin1(kPopupMenu);
        QCOMPARE(WorkspacePreview::lookQss(menu), menu);
        QWidget host;
        auto* s = new QSlider(Qt::Horizontal, &host);
        auto* g = new QGroupBox(QStringLiteral("Details"), &host);
        g->setCheckable(true);
        WorkspacePreview::themeSubtree(&host);
        QVERIFY(s->styleSheet().isEmpty());
        QVERIFY(g->styleSheet().isEmpty());
        QCOMPARE(WorkspacePreview::roleColour(QColor(QStringLiteral("#00b4d8")), "accent"),
                 QColor(QStringLiteral("#00b4d8")));
    }

    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
    }
};

QTEST_MAIN(TestWorkspaceDialogColours)
#include "tst_workspace_dialog_colours.moc"
