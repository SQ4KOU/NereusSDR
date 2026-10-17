// no-port-check: NereusSDR-original test for where a DSP parameter popup
// (DspParamPopup, the quick controls a right-click on an NR button opens)
// ends up when it is opened against the lower edge of the screen.
//
// With the workspace look on, showing the popup restyles its thin sliders
// to the decided handle's height, so the popup is taller once it is shown
// than adjustSize() made it before. showAt() has to place the popup from
// the size it ends at: a popup that is placed first and grows afterwards
// runs past the screen's lower edge by the pixels it grew.
//
// Each case opens the quick controls of one NR button the way the flag
// does (the button's customContextMenuRequested signal), with the flag
// moved so that the asked-for point is two pixels lower than where the
// popup's final size would still fit, then lets the event loop run. The
// popup must not change size after showAt() returns, and must lie inside
// the screen's available geometry. With the look off the popup does not
// grow, and it stays exactly where it is placed today.
//
// The test runs at macOS's logical DPI (72): at the offscreen platform's
// own 96 the sliders are already thicker than the look's handle and the
// popup does not grow, so the cases would pass without proving anything.
// The look-on cases therefore also require that the popup is taller than
// with the look off.
//
// Nothing here connects to a radio: the flag has a RadioModel that was
// never connected and one slice.
//
// Modification history (NereusSDR):
//   2026-10-10 - Written for the DSP popup that ran past the screen's
//                 lower edge in the workspace look. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.

#include <QtTest/QtTest>

#include <QApplication>
#include <QHash>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QStyleFactory>

#include "fakes/RemoteWindowHarness.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/DspParamPopup.h"
#include "gui/widgets/VfoWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr int kSettleTurns = 5;
constexpr int kSettleTurnMs = 30;
// Where on the button the right-click lands.
constexpr QPoint kClick(1, 1);
// How far below the last fitting point the popup is asked for. One pixel
// is not enough: showAt() treats a popup that ends exactly on the screen's
// last row as not fitting.
constexpr int kPastTheEdgePx = 2;

QPushButton* buttonNamed(const QWidget& widget, const QString& text)
{
    for (QPushButton* button : widget.findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

void settle()
{
    for (int i = 0; i < kSettleTurns; ++i) {
        QTest::qWait(kSettleTurnMs);
        QCoreApplication::sendPostedEvents();
    }
}

// One opening of a button's quick controls: the popup's place when
// showAt() returned and after the event loop ran.
struct Opened {
    bool ok{false};
    QRect shown;
    QRect settled;
};

Opened open(VfoWidget& vfo, QPushButton* button)
{
    Opened o;
    emit button->customContextMenuRequested(kClick);
    const QList<DspParamPopup*> popups = vfo.findChildren<DspParamPopup*>();
    if (popups.isEmpty()) {
        return o;
    }
    const QPointer<DspParamPopup> popup = popups.last();
    o.shown = popup->geometry();
    settle();
    o.settled = popup->geometry();
    popup->close();
    // WA_DeleteOnClose: gone before the next opening looks for "the last".
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    o.ok = true;
    return o;
}

// Moves the flag so that a right-click on `button` asks for `at`. The
// flag lays itself out when its first popup opens, which moves the button
// inside it, so the aim is taken again until it holds.
constexpr int kAimPasses = 4;
bool aimAt(VfoWidget& vfo, QPushButton* button, const QPoint& at)
{
    for (int pass = 0; pass < kAimPasses && button->mapToGlobal(kClick) != at; ++pass) {
        vfo.move(vfo.pos() + at - button->mapToGlobal(kClick));
        QCoreApplication::sendPostedEvents();
    }
    return button->mapToGlobal(kClick) == at;
}

QString rectText(const QRect& r)
{
    return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

} // namespace

class TstDspPopupStaysOnScreen : public QObject
{
    Q_OBJECT

    // Each button's popup height with the look off.
    QHash<QString, int> m_plainHeight;

    void addRows()
    {
        QTest::addColumn<QString>("button");
        QTest::newRow("NR1") << QStringLiteral("NR1");
        QTest::newRow("NR2") << QStringLiteral("NR2");
        QTest::newRow("NR4") << QStringLiteral("NR4");
    }

private slots:
    void initTestCase()
    {
        QVERIFY(NereusSDR::Test::RemoteWindowHarness::useIsolatedProfile(QStringLiteral("dsp-popup-stays-on-screen")));
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(app != nullptr);
        applyDarkPalette(*app);
        applyAppBaselineQss(*app);
        WorkspacePreview::setEnabledForTest(false);
#ifdef Q_OS_MAC
        // The font the window gets on macOS.
        QApplication::setFont(QFont(QStringLiteral(".AppleSystemUIFont"), 13));
#endif
    }

    void lookOffPopupStaysAsPlaced_data() { addRows(); }

    // With the look off the popup does not grow after the show, and it is
    // placed exactly where it is placed today: at the asked-for point when
    // it ends on the screen's last row but one.
    void lookOffPopupStaysAsPlaced()
    {
        QFETCH(QString, button);
        QVERIFY(!WorkspacePreview::enabled());
        const QRect screen = QApplication::primaryScreen()->availableGeometry();
        RadioModel model;
        model.addSlice();
        VfoWidget vfo;
        vfo.setRadioModel(&model);
        vfo.setSlice(model.activeSlice());
        QPushButton* btn = buttonNamed(vfo, button);
        QVERIFY(btn != nullptr);

        QVERIFY(aimAt(vfo, btn, screen.topLeft() + QPoint(40, 40)));
        const Opened first = open(vfo, btn);
        QVERIFY(first.ok);
        QCOMPARE(first.settled, first.shown);

        const QPoint anchor(screen.left() + 40, screen.bottom() - first.settled.height());
        QVERIFY(aimAt(vfo, btn, anchor));
        const Opened low = open(vfo, btn);
        QVERIFY(low.ok);
        QCOMPARE(low.shown, QRect(anchor, first.settled.size()));
        QCOMPARE(low.settled, low.shown);
        m_plainHeight.insert(button, low.settled.height());
    }

    void switchTheLookOn()
    {
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        WorkspacePreview::setEnabledForTest(true);
        applyWorkspacePalette(*app);
    }

    void lookOnPopupEndsInsideTheScreen_data() { addRows(); }

    // With the look on the popup has its final size when showAt() returns,
    // and, asked for past the point where that size fits, it lies inside
    // the screen.
    void lookOnPopupEndsInsideTheScreen()
    {
        QFETCH(QString, button);
        QVERIFY(WorkspacePreview::enabled());
        const QRect screen = QApplication::primaryScreen()->availableGeometry();
        RadioModel model;
        model.addSlice();
        VfoWidget vfo;
        vfo.setRadioModel(&model);
        vfo.setSlice(model.activeSlice());
        QPushButton* btn = buttonNamed(vfo, button);
        QVERIFY(btn != nullptr);

        QVERIFY(aimAt(vfo, btn, screen.topLeft() + QPoint(40, 40)));
        const Opened first = open(vfo, btn);
        QVERIFY(first.ok);
        // The case means something only while the look makes the popup
        // taller than adjustSize() does before the show.
        QVERIFY(m_plainHeight.contains(button));
        QVERIFY2(first.settled.height() >= m_plainHeight.value(button) + kPastTheEdgePx,
                 qPrintable(QStringLiteral("the popup is %1 px high with the look off and %2 px with it on")
                                .arg(m_plainHeight.value(button))
                                .arg(first.settled.height())));

        const QPoint anchor(screen.left() + 40, screen.bottom() - first.settled.height() + kPastTheEdgePx);
        QVERIFY(aimAt(vfo, btn, anchor));
        const Opened low = open(vfo, btn);
        QVERIFY(low.ok);
        QVERIFY2(screen.contains(low.settled),
                 qPrintable(QStringLiteral("the popup ends at y %1; the screen's available area ends at y %2")
                                .arg(low.settled.bottom())
                                .arg(screen.bottom())));
        QVERIFY2(low.settled == low.shown,
                 qPrintable(QStringLiteral("asked for at y %1 the popup was %2 when showAt() returned and %3 after the "
                                           "event loop ran")
                                .arg(anchor.y())
                                .arg(rectText(low.shown), rectText(low.settled))));
        QVERIFY2(first.settled.size() == first.shown.size(),
                 qPrintable(QStringLiteral("with room to spare the popup was %1 when showAt() returned and %2 after the "
                                           "event loop ran")
                                .arg(rectText(first.shown), rectText(first.settled))));
    }
};

int main(int argc, char** argv)
{
    // macOS's logical DPI (72), where Fusion makes a slider 15 px thick,
    // one pixel thinner than the look's decided handle, so the look raises
    // each of the popup's sliders by a pixel; at the offscreen platform's
    // 96 the same slider is 20 px and the popup never grows.
    qputenv("QT_FONT_DPI", "72");
    QApplication app(argc, argv);
    TstDspPopupStaysOnScreen test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_dsp_popup_stays_on_screen.moc"
