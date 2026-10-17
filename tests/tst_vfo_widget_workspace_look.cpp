// =================================================================
// tests/tst_vfo_widget_workspace_look.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// The receiver flag and the workspace look (JJ, 2026-10-11: "the whole
// flag does not seem to have gotten the new theme applied"). With the look
// on, the live pass themes the flag although its parent, the
// SpectrumWidget, is skipped: its sliders, buttons and combo box carry the
// same sheets the pass gives the same controls anywhere else, its floating
// buttons and folded frequency label too, and its S-meter bar is left
// alone. With the look off no sheet of the flag changes.
//
// Modification history (NereusSDR):
//   2026-10-11 - Written by J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
// =================================================================
#include <QtTest/QtTest>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QFontMetrics>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>

#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/VfoLevelBar.h"
#include "gui/widgets/VfoWidget.h"

using namespace NereusSDR;

namespace {

// The sheet every horizontal slider of the flag's audio tab is given
// (VfoWidget.cpp buildAudioTab).
const QString kFlagSliderSheet = QStringLiteral(
    "QSlider::groove:horizontal { background: #1a2a3a; height: 6px; border-radius: 3px; }"
    "QSlider::handle:horizontal { background: #00b4d8; width: 12px; margin: -3px 0; border-radius: 6px; }");

// A stand-in for the SpectrumWidget: a parent the pass skips.
class SkippedHost : public QWidget
{
public:
    SkippedHost()
    {
        setProperty(WorkspacePreview::kSkipProperty, true);
        resize(900, 500);
    }
};

struct Flag {
    SkippedHost host;
    VfoWidget* flag{nullptr};

    Flag()
    {
        flag = new VfoWidget(&host);
        flag->setSliceIndex(1);
        host.show();
        flag->show();
        flag->updatePosition(400, 0);
        QVERIFY(QTest::qWaitForWindowExposed(&host));
    }

    QList<QPushButton*> tabs() const
    {
        QList<QPushButton*> out;
        for (QPushButton* b : flag->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (b->isCheckable() && b->toolTip().startsWith(QLatin1String("Show/hide"))) {
                out.append(b);
            }
        }
        return out;
    }

    // Every widget of the flag, its floating buttons and folded label.
    QList<QWidget*> surfaces() const
    {
        QList<QWidget*> out{flag};
        out += flag->findChildren<QWidget*>();
        for (QWidget* w : {static_cast<QWidget*>(flag->closeButtonForTest()),
                           static_cast<QWidget*>(flag->lockButtonForTest()),
                           static_cast<QWidget*>(flag->recordButtonForTest()),
                           static_cast<QWidget*>(flag->playButtonForTest()),
                           static_cast<QWidget*>(flag->collapsedFrequencyLabelForTest())}) {
            if (w != nullptr) {
                out.append(w);
            }
        }
        return out;
    }

    // Opens each tab in turn and returns every sheet met, in order.
    QStringList sheetsOverAllTabs()
    {
        QStringList out;
        const QList<QPushButton*> all = tabs();
        for (QPushButton* tab : all) {
            tab->click();
            QCoreApplication::processEvents();
            for (QWidget* w : surfaces()) {
                out.append(w->styleSheet());
            }
        }
        return out;
    }
};

// The look's sheet for `original` on a control of class T that sits in an
// ordinary themed panel, the size of `like`. A size the program fixed on
// `like` is fixed here too. A size its layout gave it is only this
// control's size: the pass gives a slider thinner than the look's handle
// a minimum height unless its height is fixed, and Fusion's slider is
// thinner than that at 96 dpi on Linux (15 px) and not on the Mac (20 px),
// so a control fixed at the flag slider's size got another sheet there.
template <typename T>
QString lookElsewhere(const QString& original, const QWidget* like)
{
    QWidget panel;
    auto* control = new T(&panel);
    if (auto* slider = qobject_cast<QSlider*>(control)) {
        slider->setOrientation(Qt::Horizontal);
    }
    if (auto* button = qobject_cast<QPushButton*>(control)) {
        button->setCheckable(true);
    }
    control->resize(like->size());
    if (like->minimumWidth() == like->maximumWidth()) {
        control->setFixedWidth(like->width());
    }
    if (like->minimumHeight() == like->maximumHeight()) {
        control->setFixedHeight(like->height());
    }
    control->setStyleSheet(original);
    panel.show();
    WorkspacePreview::themeSubtree(&panel);
    return control->styleSheet();
}

} // namespace

class TestVfoWidgetWorkspaceLook : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // The live pass is installed once, as applyWorkspacePalette() does
        // at startup; it acts only while the switch is on.
        WorkspacePreview::setEnabledForTest(true);
        WorkspacePreview::installLiveTheming(*qobject_cast<QApplication*>(QCoreApplication::instance()));
        WorkspacePreview::setEnabledForTest(false);
    }

    void cleanup() { WorkspacePreview::setEnabledForTest(false); }

    void lookOff_noSheetChanges()
    {
        Flag f;
        QCOMPARE(f.flag->afSliderForTest()->styleSheet(), kFlagSliderSheet);
        const QStringList sheets = f.sheetsOverAllTabs();
        for (QWidget* w : f.surfaces()) {
            QVERIFY2(!w->property(WorkspacePreview::kThemedQssProperty).isValid(),
                     qPrintable(QStringLiteral("%1 was themed with the look off")
                                    .arg(QLatin1String(w->metaObject()->className()))));
        }
        // A second flag gives the same sheets: nothing was rewritten.
        Flag again;
        QCOMPARE(again.sheetsOverAllTabs(), sheets);
        QCOMPARE(f.flag->afSliderForTest()->styleSheet(), kFlagSliderSheet);
    }

    void lookOn_slidersCarryTheLooksSheet()
    {
        WorkspacePreview::setEnabledForTest(true);
        Flag f;
        f.tabs().at(0)->click();
        QCoreApplication::processEvents();
        QSlider* af = f.flag->afSliderForTest();
        QVERIFY(af->isVisible());
        const QString sheet = af->styleSheet();
        QVERIFY2(!sheet.contains(QLatin1String("#00b4d8"), Qt::CaseInsensitive), qPrintable(sheet));
        QVERIFY2(!sheet.contains(QLatin1String("#1a2a3a"), Qt::CaseInsensitive), qPrintable(sheet));
        QCOMPARE(af->property(WorkspacePreview::kOriginalQssProperty).toString(), kFlagSliderSheet);
        // The same sheet the pass gives this slider in any themed panel.
        QCOMPARE(sheet, lookElsewhere<QSlider>(kFlagSliderSheet, af));
        // Every slider of the audio tab is the same slider.
        int sliders = 0;
        for (QSlider* s : f.flag->findChildren<QSlider*>()) {
            if (s->isVisible()) {
                ++sliders;
                QCOMPARE(s->styleSheet(), sheet);
            }
        }
        QCOMPARE(sliders, 4);
    }

    void lookOn_everyStyledControlIsThemed()
    {
        WorkspacePreview::setEnabledForTest(true);
        Flag f;
        const QList<QPushButton*> tabs = f.tabs();
        QCOMPARE(tabs.size(), 5);
        for (QPushButton* tab : tabs) {
            tab->click();
            QCoreApplication::processEvents();
            for (QWidget* w : f.surfaces()) {
                if (!w->isVisible() || w->styleSheet().isEmpty()) {
                    continue;
                }
                bool inBar = false;
                for (QWidget* p = w; p != nullptr && p != f.flag; p = p->parentWidget()) {
                    inBar = inBar || qobject_cast<VfoLevelBar*>(p) != nullptr;
                }
                if (inBar) {
                    continue;
                }
                QVERIFY2(w->property(WorkspacePreview::kThemedQssProperty).toString() == w->styleSheet(),
                         qPrintable(QStringLiteral("%1 '%2' keeps an unthemed sheet: %3")
                                        .arg(QLatin1String(w->metaObject()->className()), w->objectName(),
                                             w->styleSheet())));
            }
        }
        // A toggle of the flag is the toggle the pass draws elsewhere.
        tabs.at(0)->click();
        QCoreApplication::processEvents();
        QPushButton* mute = f.flag->muteButtonForTest();
        QVERIFY(mute->isVisible());
        QCOMPARE(mute->styleSheet(),
                 lookElsewhere<QPushButton>(mute->property(WorkspacePreview::kOriginalQssProperty).toString(),
                                            mute));
        // The floating lock button, a sibling of the flag, takes the look.
        QPushButton* lock = f.flag->lockButtonForTest();
        QVERIFY(lock != nullptr);
        QVERIFY(lock->isVisible());
        QVERIFY2(!lock->styleSheet().contains(QLatin1String("rgba(20,30,50,230)")),
                 qPrintable(lock->styleSheet()));
    }

    void lookOn_dataStaysData()
    {
        WorkspacePreview::setEnabledForTest(true);
        Flag f;
        // The S-meter bar is left out of the pass.
        auto* bar = f.flag->findChild<VfoLevelBar*>();
        QVERIFY(bar != nullptr);
        QVERIFY(!bar->property(WorkspacePreview::kThemedQssProperty).isValid());
        // The frequency digits and the slice letter keep their colors.
        bool digits = false;
        bool letter = false;
        for (QLabel* l : f.flag->findChildren<QLabel*>()) {
            digits = digits || (l->isVisible() && l->styleSheet().contains(QLatin1String("#00e5ff")));
            letter = letter || (l->isVisible()
                                && l->styleSheet().contains(VfoWidget::sliceColor(1).name(), Qt::CaseInsensitive));
        }
        QVERIFY(digits);
        QVERIFY(letter);
        // The flag stays a translucent widget over the spectrum.
        QVERIFY(f.flag->testAttribute(Qt::WA_TranslucentBackground));
        QVERIFY(!f.flag->autoFillBackground());
    }

    void autoBadgeFitsAfterLateStateUpdatesAndRestoresClassicBox()
    {
        WorkspacePreview::setEnabledForTest(true);
        Flag f;
        f.tabs().at(0)->click();
        QPushButton* badge = nullptr;
        for (QPushButton* button : f.flag->findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("AUTO")) { badge = button; }
        }
        QVERIFY(badge);
        for (bool active : {true, false, true}) {
            f.flag->updateAgcAutoVisuals(active, -98.0f, 20.0, true);
            WorkspacePreview::themeSubtree(f.flag);
            QCoreApplication::processEvents();
            const QFontMetrics metrics(badge->font(), badge);
            QVERIFY(badge->contentsRect().width() >= metrics.horizontalAdvance(badge->text()) + 8);
            QVERIFY(badge->height() >= metrics.height() + 4);
            if (active) {
                QVERIFY(badge->styleSheet().contains(QStringLiteral("#adff2f")));
            }
        }
        WorkspacePreview::setEnabledForTest(false);
        // Changing the test switch does not repaint or rewrite a live sheet.
        // Exercise the real late state update with the classic look active.
        f.flag->updateAgcAutoVisuals(false, -98.0f, 20.0, true);
        QCoreApplication::processEvents();
        QCOMPARE(badge->font().pixelSize(), 7);
        QCOMPARE(badge->size(), QSize(30, 14));
        Flag classic;
        QPushButton* classicBadge = nullptr;
        for (QPushButton* button : classic.flag->findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("AUTO")) { classicBadge = button; }
        }
        QVERIFY(classicBadge);
        QCOMPARE(classicBadge->font().pixelSize(), 7);
        QCOMPARE(classicBadge->size(), QSize(30, 14));
    }

    void lookOn_aSkippedParentStillSkipsWhatDidNotOptIn()
    {
        WorkspacePreview::setEnabledForTest(true);
        SkippedHost host;
        auto* plain = new QSlider(Qt::Horizontal, &host);
        plain->setStyleSheet(kFlagSliderSheet);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QCOMPARE(plain->styleSheet(), kFlagSliderSheet);
    }
};

QTEST_MAIN(TestVfoWidgetWorkspaceLook)
#include "tst_vfo_widget_workspace_look.moc"
