// no-port-check: NereusSDR-original test for the workspace preview's
// decided slider (src/gui/styles/WorkspacePreview sliderBlock() and
// WorkspaceComponents sliderGroove() / sliderHandle()).
//
// With the workspace look on, opens every Setup page and every tab on it,
// the main-window applets that hold sliders, the pan overlay panel and the
// title bar's speaker volumes, and checks that every visible QSlider's
// handle (QStyle::subControlRect, SC_SliderHandle) lies inside the slider
// and is not clipped by an ancestor. JJ saw the 3D View and MNR handles
// lose their rounded top: the look pinned a slider to today's thickness,
// thinner than the decided handle. A slider already as thick as the
// handle keeps its size.
//
// Modification history (NereusSDR):
//   2026-10-09 - Written for the clipped slider handles. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - thickerSlidersKeepTheirSize: the first fix made every
//                 slider 16 px, thinning the 18 px applet sliders and the
//                 20 px zoom bar. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-09 - D28: neighboursKeepTheirSize: a slider the look raised
//                 to the handle takes its pixels from the panel, not from
//                 the widgets beside it. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.

//   2026-10-12 - Include label text and font metrics in no-shrink diagnostics.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

#include <QtTest/QtTest>

#include <QAbstractScrollArea>
#include <QApplication>
#include <QLabel>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionSlider>
#include <QTabWidget>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

#include "core/AppSettings.h"
#include "gui/SetupDialog.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/applets/CwxApplet.h"
#include "gui/applets/DigitalApplet.h"
#include "gui/applets/DisplayApplet.h"
#include "gui/applets/DiversityApplet.h"
#include "gui/applets/DvkApplet.h"
#include "gui/applets/EqApplet.h"
#include "gui/applets/FmApplet.h"
#include "gui/applets/PhoneCwApplet.h"
#include "gui/applets/RxApplet.h"
#include "gui/applets/TxApplet.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/RadioSpeakerWidget.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

// QSlider::initStyleOption() is protected; the same fields, filled here.
QRect handleOf(const QSlider* s)
{
    QStyleOptionSlider o;
    o.initFrom(s);
    o.subControls = QStyle::SC_None;
    o.activeSubControls = QStyle::SC_None;
    o.orientation = s->orientation();
    o.maximum = s->maximum();
    o.minimum = s->minimum();
    o.tickPosition = s->tickPosition();
    o.tickInterval = s->tickInterval();
    o.upsideDown = (s->orientation() == Qt::Horizontal)
                       ? (s->invertedAppearance() != (o.direction == Qt::RightToLeft))
                       : (!s->invertedAppearance());
    o.direction = Qt::LeftToRight;
    o.sliderPosition = s->sliderPosition();
    o.sliderValue = s->value();
    o.singleStep = s->singleStep();
    o.pageStep = s->pageStep();
    if (s->orientation() == Qt::Horizontal) {
        o.state |= QStyle::State_Horizontal;
    }
    return s->style()->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderHandle, s);
}

QString nameOf(const QWidget* w)
{
    return w->objectName().isEmpty() ? QString::fromLatin1(w->metaObject()->className())
                                     : w->objectName();
}

// Why `s`'s handle is cut off, or an empty string. Stops at a scroll area's
// viewport: content scrolled out of view is not a clipped handle.
QString clipOf(QSlider* s)
{
    const QRect h = handleOf(s);
    if (!s->rect().contains(h)) {
        return QStringLiteral("handle %1,%2 %3x%4 outside the slider %5x%6")
            .arg(h.x()).arg(h.y()).arg(h.width()).arg(h.height())
            .arg(s->width()).arg(s->height());
    }
    QRect r = h;
    for (QWidget* w = s; w->parentWidget() != nullptr && !w->isWindow(); w = w->parentWidget()) {
        QWidget* p = w->parentWidget();
        r.translate(w->pos());
        if (qobject_cast<QAbstractScrollArea*>(p->parentWidget()) != nullptr
            && static_cast<QAbstractScrollArea*>(p->parentWidget())->viewport() == p) {
            break;
        }
        if (!p->rect().contains(r)) {
            return QStringLiteral("handle clipped by %1 (%2x%3, handle at %4,%5 %6x%7)")
                .arg(nameOf(p)).arg(p->width()).arg(p->height())
                .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        }
    }
    return {};
}

class WorkspaceApp
{
public:
    WorkspaceApp()
        : m_app(qobject_cast<QApplication*>(QCoreApplication::instance()))
        , m_palette(m_app->palette())
        , m_sheet(m_app->styleSheet())
    {
        // The startup sequence of main.cpp that gives the app its look.
        WorkspacePreview::setEnabledForTest(true);
        applyDarkPalette(*m_app);
        applyAppBaselineQss(*m_app);
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

// Today's look: the startup sequence of main.cpp with the preview off.
class TodayApp
{
public:
    TodayApp()
        : m_app(qobject_cast<QApplication*>(QCoreApplication::instance()))
        , m_palette(m_app->palette())
        , m_sheet(m_app->styleSheet())
    {
        WorkspacePreview::setEnabledForTest(false);
        applyDarkPalette(*m_app);
        applyAppBaselineQss(*m_app);
    }
    ~TodayApp()
    {
        m_app->setStyleSheet(m_sheet);
        m_app->setPalette(m_palette);
    }
    TodayApp(const TodayApp&) = delete;
    TodayApp& operator=(const TodayApp&) = delete;

private:
    QApplication* m_app;
    QPalette m_palette;
    QString m_sheet;
};

// The applets whose fixed-height rows hold sliders the look may raise.
std::vector<std::pair<QString, QWidget*>> sliderApplets(RadioModel* model, QWidget* host)
{
    return {
        { QStringLiteral("RxApplet"), new RxApplet(nullptr, model, host) },
        { QStringLiteral("TxApplet"), new TxApplet(model, host) },
        { QStringLiteral("PhoneCwApplet"), new PhoneCwApplet(model, host) },
        { QStringLiteral("EqApplet"), new EqApplet(model, host) },
        { QStringLiteral("FmApplet"), new FmApplet(model, host) },
        { QStringLiteral("DigitalApplet"), new DigitalApplet(model, host) },
        { QStringLiteral("DisplayApplet"), new DisplayApplet(model, host) },
        { QStringLiteral("DiversityApplet"), new DiversityApplet(model, host) },
        { QStringLiteral("CwxApplet"), new CwxApplet(model, host) },
        { QStringLiteral("DvkApplet"), new DvkApplet(model, host) },
    };
}

// Every widget under `root` by its path of child indexes, with its class,
// name and size. Two copies built the same way have the same paths.
struct Placed {
    QString name;
    QSize size;
    bool visible = false;
    QString detail;
};

void collect(QWidget* w, const QString& path, QHash<QString, Placed>& out)
{
    QString detail;
    if (auto* label = qobject_cast<QLabel*>(w)) {
        detail = QStringLiteral("text=%1 font=%2 hint=%3x%4 min=%5x%6 parent=%7x%8")
            .arg(label->text(), label->font().toString())
            .arg(label->sizeHint().width()).arg(label->sizeHint().height())
            .arg(label->minimumWidth()).arg(label->minimumHeight())
            .arg(label->parentWidget()->width()).arg(label->parentWidget()->height());
    }
    out.insert(path, Placed{ nameOf(w), w->size(), w->isVisible(), detail });
    int i = 0;
    for (QObject* o : w->children()) {
        if (auto* c = qobject_cast<QWidget*>(o)) {
            collect(c, path + QLatin1Char('/') + QString::number(i), out);
        }
        ++i;
    }
}

// Lays the applets out as the applet column does: fixed width, the height
// they ask for.
void layOut(std::vector<std::pair<QString, QWidget*>>& parts)
{
    int y = 0;
    for (auto& [name, w] : parts) {
        w->setFixedWidth(260);
        w->adjustSize();
        w->move(0, y);
        y += w->height() + 8;
    }
}

void settleLayouts(QWidget* host)
{
    for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents();
        for (QWidget* w : host->findChildren<QWidget*>()) {
            if (w->layout() != nullptr) {
                w->layout()->activate();
            }
        }
    }
}

// Checks every visible slider under `root`, then every tab of every tab
// widget under it in turn. `where` names the place in the findings.
void sweep(QWidget* root, const QString& where, QStringList& found, int& checked)
{
    const auto check = [&](const QString& place) {
        WorkspacePreview::themeSubtree(root);
        QCoreApplication::processEvents();
        for (QSlider* s : root->findChildren<QSlider*>()) {
            if (!s->isVisible()) {
                continue;
            }
            ++checked;
            const QString why = clipOf(s);
            if (!why.isEmpty()) {
                found << QStringLiteral("%1: %2 (%3x%4): %5")
                             .arg(place, nameOf(s)).arg(s->width()).arg(s->height()).arg(why);
            }
        }
    };
    check(where);
    for (QTabWidget* tabs : root->findChildren<QTabWidget*>()) {
        if (!tabs->isVisible()) {
            continue;
        }
        const int was = tabs->currentIndex();
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            check(where + QStringLiteral(" > ") + tabs->tabText(i));
        }
        tabs->setCurrentIndex(was);
    }
}

} // namespace

class TestWorkspaceSliderHandles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("workspace-slider-handles-%1")
                                            .arg(QCoreApplication::applicationPid()));
        AppSettings::instance().clear();
        // The app's style (main.cpp); the platform default sizes sliders
        // differently.
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }

    // Every Setup page and tab: no handle cut off.
    void setupHandlesAreWhole()
    {
        WorkspaceApp look;
        RadioModel model;
        SetupDialog dialog(&model);
        dialog.resize(1100, 760);
        dialog.show();
        auto* stack = dialog.findChild<QStackedWidget*>();
        QVERIFY(stack != nullptr);
        const QStringList labels = dialog.pageLabelsForTest();
        QStringList found;
        int checked = 0;
        for (int i = 0; i < dialog.registeredPageCountForTest(); ++i) {
            QWidget* page = dialog.realizePageAtForTest(i);
            if (page == nullptr) {
                continue;
            }
            stack->setCurrentWidget(page);
            sweep(&dialog, labels.at(i), found, checked);
        }
        qInfo("Setup: %d visible sliders checked, %d cut off", checked, int(found.size()));
        QVERIFY(checked > 0);
        QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    }

    // The main-window applets that hold sliders, at the applet column's
    // width, the pan overlay panel and the title bar's RADIO volume.
    void appletHandlesAreWhole()
    {
        WorkspaceApp look;
        RadioModel model;
        QWidget host;
        host.resize(300, 4000);
        std::vector<std::pair<QString, QWidget*>> parts = {
            { QStringLiteral("RxApplet"), new RxApplet(nullptr, &model, &host) },
            { QStringLiteral("TxApplet"), new TxApplet(&model, &host) },
            { QStringLiteral("PhoneCwApplet"), new PhoneCwApplet(&model, &host) },
            { QStringLiteral("EqApplet"), new EqApplet(&model, &host) },
            { QStringLiteral("FmApplet"), new FmApplet(&model, &host) },
            { QStringLiteral("DigitalApplet"), new DigitalApplet(&model, &host) },
            { QStringLiteral("DisplayApplet"), new DisplayApplet(&model, &host) },
            { QStringLiteral("DiversityApplet"), new DiversityApplet(&model, &host) },
            { QStringLiteral("CwxApplet"), new CwxApplet(&model, &host) },
            { QStringLiteral("DvkApplet"), new DvkApplet(&model, &host) },
            { QStringLiteral("SpectrumOverlayPanel"), new SpectrumOverlayPanel(&host) },
            { QStringLiteral("RadioSpeakerWidget"), new RadioSpeakerWidget(&model, &host) },
        };
        int y = 0;
        for (auto& [name, w] : parts) {
            w->setFixedWidth(260);
            w->adjustSize();
            w->move(0, y);
            y += w->height() + 8;
        }
        host.show();
        QStringList found;
        int checked = 0;
        for (auto& [name, w] : parts) {
            sweep(w, name, found, checked);
        }
        qInfo("Applets: %d visible sliders checked, %d cut off", checked, int(found.size()));
        QVERIFY(checked > 0);
        QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    }

    // A slider already as thick as the handle keeps its thickness; a
    // thinner one becomes exactly as thick as the handle. Each slider's
    // thickness with the look on is compared with the same slider's with
    // the fix taken out: the look's sheets, measured before the live
    // pass adds anything for thickness, are what decide it.
    void thickerSlidersKeepTheirSize()
    {
        WorkspaceApp look;
        RadioModel model;
        QWidget host;
        host.resize(300, 4000);
        std::vector<std::pair<QString, QWidget*>> parts = {
            { QStringLiteral("RxApplet"), new RxApplet(nullptr, &model, &host) },
            { QStringLiteral("TxApplet"), new TxApplet(&model, &host) },
            { QStringLiteral("PhoneCwApplet"), new PhoneCwApplet(&model, &host) },
            { QStringLiteral("EqApplet"), new EqApplet(&model, &host) },
            { QStringLiteral("DisplayApplet"), new DisplayApplet(&model, &host) },
        };
        // The pan's zoom bar: a slider the layout sizes from its hint.
        auto* zoom = new QSlider(Qt::Horizontal, &host);
        zoom->setObjectName(QStringLiteral("zoomLike"));
        zoom->setMinimumHeight(20);
        parts.push_back({ QStringLiteral("zoomLike"), zoom });
        int y = 0;
        for (auto& [name, w] : parts) {
            w->setFixedWidth(260);
            w->adjustSize();
            w->move(0, y);
            y += w->height() + 8;
        }
        const auto thickness = [](const QSlider* s) {
            return s->orientation() == Qt::Horizontal ? s->height() : s->width();
        };
        const auto settle = [&host] {
            QCoreApplication::processEvents();
            for (QWidget* w : host.findChildren<QWidget*>()) {
                if (w->layout() != nullptr) {
                    w->layout()->activate();
                }
            }
            QCoreApplication::processEvents();
        };
        host.show();
        WorkspacePreview::themeSubtree(&host);
        settle();
        const int span = 16;
        QStringList found;
        int checked = 0;
        int raised = 0;
        for (QSlider* s : host.findChildren<QSlider*>()) {
            if (!s->isVisible()) {
                continue;
            }
            ++checked;
            // What the slider asks for without a thickness rule: the
            // program's fixed size, else its hint under the look's sheet.
            const bool fixed = s->orientation() == Qt::Horizontal
                                   ? s->property("_nereusWorkspaceSliderFit").toString().isEmpty()
                                         && s->minimumHeight() == s->maximumHeight()
                                   : s->property("_nereusWorkspaceSliderFit").toString().isEmpty()
                                         && s->minimumWidth() == s->maximumWidth();
            const int got = thickness(s);
            if (!s->property("_nereusWorkspaceSliderFit").toString().isEmpty()) {
                ++raised;
                if (got != span) {
                    found << QStringLiteral("%1: raised to %2, not %3").arg(nameOf(s)).arg(got).arg(span);
                }
            } else if (got < span) {
                found << QStringLiteral("%1: %2 px, thinner than the handle").arg(nameOf(s)).arg(got);
            } else if (fixed && got != (s->orientation() == Qt::Horizontal ? s->maximumHeight()
                                                                          : s->maximumWidth())) {
                found << QStringLiteral("%1: fixed size not kept (%2)").arg(nameOf(s)).arg(got);
            }
        }
        // The program's own sizes that are thicker than the handle.
        auto* rf = host.findChild<QSlider*>(QStringLiteral("TxRfPowerSlider"));
        QVERIFY(rf != nullptr);
        if (rf->height() != 18) {
            found << QStringLiteral("TxRfPowerSlider: %1 px, the program fixes 18").arg(rf->height());
        }
        if (zoom->height() < 20) {
            found << QStringLiteral("zoomLike: %1 px, its minimum is 20").arg(zoom->height());
        }
        qInfo("Thickness: %d visible sliders checked, %d raised to the handle, %d wrong",
              checked, raised, int(found.size()));
        QVERIFY(checked > 0);
        QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    }

    // D28 (JJ, 2026-10-09): a slider the look raises to the handle takes
    // its pixels from the panel, which grows; no widget beside it shrinks.
    // Each applet is built in today's look and in the workspace look and
    // laid out the same way; no visible widget other than a slider (or a
    // widget that holds one) is shorter with the look on.
    void neighboursKeepTheirSize()
    {
        QHash<QString, QHash<QString, Placed>> today;
        {
            TodayApp look;
            RadioModel model;
            QWidget host;
            host.resize(300, 6000);
            auto parts = sliderApplets(&model, &host);
            layOut(parts);
            host.show();
            settleLayouts(&host);
            layOut(parts);
            settleLayouts(&host);
            for (auto& [name, w] : parts) {
                collect(w, name, today[name]);
            }
        }
        WorkspaceApp look;
        RadioModel model;
        QWidget host;
        host.resize(300, 6000);
        auto parts = sliderApplets(&model, &host);
        layOut(parts);
        host.show();
        WorkspacePreview::themeSubtree(&host);
        settleLayouts(&host);
        layOut(parts);
        settleLayouts(&host);
        QStringList found;
        int compared = 0;
        for (auto& [name, w] : parts) {
            QHash<QString, Placed> now;
            collect(w, name, now);
            for (auto it = now.cbegin(); it != now.cend(); ++it) {
                const auto before = today.value(name).constFind(it.key());
                if (before == today.value(name).cend() || !it->visible || !before->visible) {
                    continue;
                }
                // Sliders, and the widgets that hold them, may grow; the
                // rest keep today's height.
                if (it->name.contains(QLatin1String("Slider")) || it->name == QLatin1String("QSlider")) {
                    continue;
                }
                ++compared;
                if (it->size.height() < before->size.height()) {
                    found << QStringLiteral("%1 %2: %3 px tall, %4 in today's look")
                                 .arg(it.key(), it->name).arg(it->size.height())
                                 .arg(before->size.height());
                    qInfo().noquote() << "NEIGHBOUR classic" << before->detail
                                     << "workspace" << it->detail;
                }
            }
        }
        qInfo("Neighbours: %d widgets compared, %d shorter with the look on", compared,
              int(found.size()));
        for (const QString& f : found) {
            qInfo().noquote() << "SHORTER" << f;
        }
        QVERIFY(compared > 0);
        QVERIFY2(found.isEmpty(), qPrintable(QStringLiteral("%1 widgets shorter").arg(found.size())));
    }

    // With the look off, a slider the program fixed keeps its sheet and
    // its size.
    void lookOffKeepsTodaysSliders()
    {
        WorkspacePreview::setEnabledForTest(false);
        QWidget host;
        auto* s = new QSlider(Qt::Horizontal, &host);
        const QString sheet = QStringLiteral(
            "QSlider::groove:horizontal { background: #1a2a3a; height: 4px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #00b4d8; width: 10px; margin: -3px 0; border-radius: 5px; }");
        s->setFixedHeight(14);
        s->setStyleSheet(sheet);
        auto* bare = new QSlider(Qt::Horizontal, &host);
        bare->move(0, 40);
        host.show();
        const int bareHeight = bare->sizeHint().height();
        WorkspacePreview::themeSubtree(&host);
        QCoreApplication::processEvents();
        QCOMPARE(s->styleSheet(), sheet);
        QCOMPARE(s->maximumHeight(), 14);
        QVERIFY(bare->styleSheet().isEmpty());
        QCOMPARE(bare->sizeHint().height(), bareHeight);
    }

    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
    }
};

int main(int argc, char** argv)
{
    // macOS's logical DPI (72), where Fusion makes a slider 15 px thick,
    // one pixel thinner than the decided handle; at the offscreen
    // platform's 96 the same slider is 20 px and the cut never shows.
    qputenv("QT_FONT_DPI", "72");
    QApplication app(argc, argv);
    TestWorkspaceSliderHandles test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_workspace_slider_handles.moc"
