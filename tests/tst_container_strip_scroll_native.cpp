// no-port-check: NereusSDR-original native strip scroll characterization.
// Modification history (NereusSDR):
//   2026-10-09 - A docked stack (an applet over GPU meters) scrolled part
//                 way shows no black band over its meters on the composited
//                 window by J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - The window is shortened until the stack overflows it: under
//                 Fusion with the look off the stack fits 520 px whole
//                 (Linux CI). J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - The scroll range is used only once it has held one value:
//                 a resize changes it at once, and the host's deferred
//                 projection of the bar rows changes it again a turn later
//                 (Linux CI read 40, then scrolled a range of 2). The layout
//                 half also runs offscreen. J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
//   2026-10-11 - The test says when it could not see its window. The window
//                 is raised and activated, it paints a border of colored
//                 blocks around the strip, and every capture must show that
//                 border (and the redrawn one the applet stand-in where the
//                 scroll put it) before it is judged. A covered or unexposed
//                 window skips the row with the step and the reason. J.J.
//                 Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//
// Native only: QWidget::grab() renders a QRhiWidget through Qt's readback,
// not through the window server, so an offscreen or grabbed image cannot
// show a compositing fault. This test captures its own window from the
// screen (never the desktop) after each scroll step, before any forced
// redraw, and compares it with a capture taken after every meter has
// rendered a fresh frame. The two must match inside the scroll viewport,
// and no row of the viewport may turn black where the fresh capture has
// content.
//
// What the capture is: on macOS QScreen::grabWindow copies the screen
// rectangle the window occupies, not the window, so a window lying over this
// one lands in the picture. A capture is therefore judged only once it is
// proven to be of this window: the test's top-level paints a border of
// colored blocks in a known order outside the strip, and every capture must
// show each block. The capture after the redraw must also show the applet
// stand-in's fill where the scroll offset puts it, which covers the middle
// of the window; the capture after the scroll is not held to that, because
// the middle of it is what is under test.
//
// What a result means:
//   PASS - every capture was proven to be of this window, and none showed a
//          black band.
//   FAIL - a capture proven to be of this window showed a black band (or the
//          layout half failed).
//   SKIP - the test could not see its window at some step: another window
//          was over it, it was not exposed (fully covered, on another
//          desktop, display off), or the screen gave no picture. The message
//          names the step and the reason. A skip proves nothing either way;
//          run it again with the window clear. A black band already found at
//          an earlier, proven step is still reported as a failure.
#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWindow>
#include <memory>
#include <optional>

#include "core/AppSettings.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/containers/ContainerContentHost.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/meters/MeterPoller.h"
#include "gui/meters/MeterWidget.h"

using namespace NereusSDR;

namespace {
constexpr int kFrameTimeoutMs = 3000;
// How long the window server gets to show a submitted frame.
constexpr int kCompositeSettleMs = 120;
// A row is black when nearly all of it is darker than this.
constexpr int kBlackLuma = 10;
constexpr double kBlackRowShare = 0.95;
// The scroll steps below go to fixed offsets up to 31, so the stack must
// overflow its viewport by more than that.
constexpr int kLeastScrollRange = 32;
// How the window is shortened where the stack fits it whole.
constexpr int kShortenStep = 40;
constexpr int kShortestWindow = 200;
// The scroll range counts as settled once it has held one value this long,
// and must get there within the timeout.
constexpr int kRangeSettleMs = 250;
constexpr int kRangeSettleTimeoutMs = 5000;
constexpr int kRangePollMs = 10;
// How long the window gets to become the active one. It is asked for, not
// required: the border below is the proof that the window was seen.
constexpr int kActivateTimeoutMs = 2000;
// The border the top-level paints around the strip: blocks of three strong
// colors in a fixed order along all four edges. The corners are left out
// because the window server rounds them.
constexpr int kSentinelBand = 8;
constexpr int kSentinelCell = 20;
constexpr int kSentinelCornerClear = 40;
constexpr QRgb kSentinelColors[] = {0xffff00ff, 0xffffff00, 0xff00ffff};
// A captured block may sit this far (RGB distance) from the painted color:
// the capture is in the display's color space, not sRGB. It must also be
// nearer its own color than either of the other two.
constexpr int kSentinelReach = 150;
// The applet stand-in's fill, and how far a captured sample may sit from it.
constexpr QRgb kAppletFill = 0xff5a6470;
constexpr int kAppletReach = 48;
constexpr int kAppletSampleStep = 6;
constexpr int kAppletSampleInset = 4;
constexpr int kAppletLeastSamples = 8;
constexpr double kAppletSeenShare = 0.9;

// The scroll range once it has stopped changing; nothing when it never held
// one value for kRangeSettleMs. A resize moves the range twice: the scroll
// area re-ranges at once against the stack's old height, then the host's
// deferred projection (ContainerContentHost::scheduleStackProjection, a
// zero timer started by the resize) fits the bar rows to the new viewport
// and the range changes again. Bar rows stretch to fill a viewport taller
// than the stack and stop shrinking at their saved height, so a range read
// straight after shortening the window is the old stretch, not an overflow.
std::optional<int> settledScrollRange(const QScrollBar* bar)
{
    QElapsedTimer total;
    total.start();
    QElapsedTimer held;
    held.start();
    int range = bar->maximum();
    while (held.elapsed() < kRangeSettleMs) {
        if (total.elapsed() >= kRangeSettleTimeoutMs) {
            return std::nullopt;
        }
        QTest::qWait(kRangePollMs);
        if (bar->maximum() != range) {
            range = bar->maximum();
            held.restart();
        }
    }
    return range;
}

struct SentinelCell {
    QRect rect;
    QRgb color;
};

// The border blocks for a top-level of this size, in logical pixels. Both
// the painting and the check read this one list.
QList<SentinelCell> sentinelCells(const QSize& size)
{
    QList<SentinelCell> cells;
    const auto next = [&cells](const QRect& rect) {
        cells.append({rect, kSentinelColors[cells.size() % std::size(kSentinelColors)]});
    };
    for (int x = kSentinelCornerClear; x + kSentinelCell <= size.width() - kSentinelCornerClear; x += kSentinelCell) {
        next(QRect(x, 0, kSentinelCell, kSentinelBand));
        next(QRect(x, size.height() - kSentinelBand, kSentinelCell, kSentinelBand));
    }
    for (int y = kSentinelCornerClear; y + kSentinelCell <= size.height() - kSentinelCornerClear; y += kSentinelCell) {
        next(QRect(0, y, kSentinelBand, kSentinelCell));
        next(QRect(size.width() - kSentinelBand, y, kSentinelBand, kSentinelCell));
    }
    return cells;
}

// The test's top-level: an ordinary widget that paints the border.
class SentinelWindow : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        for (const SentinelCell& cell : sentinelCells(size())) {
            painter.fillRect(cell.rect, QColor::fromRgb(cell.color));
        }
    }
};

struct WindowCapture {
    QImage image;
    // Why there is no image; empty when there is one.
    QString unavailable;
};

WindowCapture ownWindowCapture(QWidget& window)
{
    QWindow* handle = window.windowHandle();
    if (handle == nullptr || window.internalWinId() == 0) {
        return {{}, QStringLiteral("the window has no native handle")};
    }
    if (handle->screen() == nullptr) {
        return {{}, QStringLiteral("the window is on no screen")};
    }
    if (!handle->isExposed()) {
        return {{}, QStringLiteral("the window is not exposed (fully covered by other windows, on another "
                                   "desktop, or the display is off)")};
    }
    // Nonzero, own fixture window only; never grabWindow(0), which would
    // capture the operator's desktop.
    QImage image = handle->screen()->grabWindow(window.internalWinId(), 0, 0, window.width(), window.height()).toImage();
    if (image.isNull()) {
        return {{}, QStringLiteral("the screen gave no picture of the window (screen recording may not be granted)")};
    }
    return {image, {}};
}

QPoint pixelPoint(const QPoint& logical, const QImage& image, const QSize& logicalSize)
{
    const qreal xScale = qreal(image.width()) / logicalSize.width();
    const qreal yScale = qreal(image.height()) / logicalSize.height();
    return QPoint(qBound(0, int((logical.x() + 0.5) * xScale), image.width() - 1),
                  qBound(0, int((logical.y() + 0.5) * yScale), image.height() - 1));
}

int colorDistanceSquared(QRgb a, QRgb b)
{
    const int red = qRed(a) - qRed(b);
    const int green = qGreen(a) - qGreen(b);
    const int blue = qBlue(a) - qBlue(b);
    return red * red + green * green + blue * blue;
}

QString colorName(QRgb color)
{
    return QColor::fromRgb(color).name();
}

// Empty when the capture shows every border block; otherwise what is wrong.
// The border lies outside the strip's viewport, so nothing under test can
// change it: a wrong block means the picture is not of this window there.
QString sentinelFault(const QImage& capture, const QSize& logicalSize)
{
    const QList<SentinelCell> cells = sentinelCells(logicalSize);
    if (cells.isEmpty()) {
        return QStringLiteral("the window is too small to carry its border blocks");
    }
    int wrong = 0;
    QString firstWrong;
    for (const SentinelCell& cell : cells) {
        const QRgb seen = capture.pixel(pixelPoint(cell.rect.center(), capture, logicalSize));
        const int distance = colorDistanceSquared(seen, cell.color);
        bool matches = distance <= kSentinelReach * kSentinelReach;
        for (const QRgb other : kSentinelColors) {
            if (other != cell.color && colorDistanceSquared(seen, other) <= distance) {
                matches = false;
            }
        }
        if (!matches) {
            if (wrong == 0) {
                firstWrong = QStringLiteral("the block at %1, %2 read %3 where the window paints %4")
                    .arg(cell.rect.center().x()).arg(cell.rect.center().y()).arg(colorName(seen), colorName(cell.color));
            }
            ++wrong;
        }
    }
    if (wrong == 0) {
        return {};
    }
    return QStringLiteral("%1 of %2 border blocks are not in the picture (%3): another window is over this one, "
                          "or it is not on screen")
        .arg(wrong).arg(cells.size()).arg(firstWrong);
}

// Empty when the capture shows the applet stand-in's fill over the part of
// it the scroll leaves in the viewport (or too little of it is in view to
// say); otherwise what is wrong. Samples sit right of the labels' text.
QString appletFault(const QImage& capture, const QRect& appletInView, const QSize& logicalSize)
{
    int samples = 0;
    int seenFill = 0;
    QRgb firstWrong = 0;
    QPoint firstWrongAt;
    for (int y = appletInView.top() + kAppletSampleInset; y <= appletInView.bottom() - kAppletSampleInset;
         y += kAppletSampleStep) {
        for (const int x : {appletInView.left() + appletInView.width() * 3 / 5,
                            appletInView.left() + appletInView.width() * 4 / 5}) {
            const QRgb seen = capture.pixel(pixelPoint(QPoint(x, y), capture, logicalSize));
            ++samples;
            if (colorDistanceSquared(seen, kAppletFill) <= kAppletReach * kAppletReach) {
                ++seenFill;
            } else if (samples - seenFill == 1) {
                firstWrong = seen;
                firstWrongAt = QPoint(x, y);
            }
        }
    }
    if (samples < kAppletLeastSamples || seenFill >= kAppletSeenShare * samples) {
        return {};
    }
    return QStringLiteral("the applet stand-in is not where the scroll put it in the picture: %1 of %2 samples "
                          "show its fill %3 (at %4, %5 the picture reads %6): another window is over this one")
        .arg(seenFill).arg(samples).arg(colorName(kAppletFill)).arg(firstWrongAt.x()).arg(firstWrongAt.y())
        .arg(colorName(firstWrong));
}

QRect pixelRect(const QRect& logical, const QImage& image, const QSize& logicalSize)
{
    const qreal xScale = qreal(image.width()) / logicalSize.width();
    const qreal yScale = qreal(image.height()) / logicalSize.height();
    return QRect(qRound(logical.x() * xScale), qRound(logical.y() * yScale),
                 qRound(logical.width() * xScale), qRound(logical.height() * yScale));
}

bool rowIsBlack(const QImage& image, int y, const QRect& pixels)
{
    int dark = 0;
    for (int x = pixels.left(); x <= pixels.right(); ++x) {
        if (qGray(image.pixel(x, y)) < kBlackLuma) {
            ++dark;
        }
    }
    return dark >= kBlackRowShare * pixels.width();
}

// Rows black in the first capture where the fresh capture has content.
int blackBandRows(const QImage& stale, const QImage& fresh, const QRect& pixels)
{
    int rows = 0;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
        if (rowIsBlack(stale, y, pixels) && !rowIsBlack(fresh, y, pixels)) {
            ++rows;
        }
    }
    return rows;
}

qsizetype changedPixels(const QImage& a, const QImage& b, const QRect& pixels)
{
    qsizetype count = 0;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
        for (int x = pixels.left(); x <= pixels.right(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++count;
            }
        }
    }
    return count;
}
} // namespace

class TstContainerStripScrollNative : public QObject {
    Q_OBJECT
private slots:
    void scrolledStripShowsNoBlackBand_data()
    {
        QTest::addColumn<bool>("look");
        QTest::newRow("today") << false;
        QTest::newRow("workspace") << true;
    }

    void scrolledStripShowsNoBlackBand()
    {
        QFETCH(bool, look);
        if (QGuiApplication::platformName() == QStringLiteral("minimal")) {
            QSKIP("Needs NATIVE_WINDOW registration and a capture of its own window.");
        }
        WorkspacePreview::setEnabledForTest(look);
        const auto restoreLook = qScopeGuard([] { WorkspacePreview::setEnabledForTest(false); });
        QTemporaryDir settingsDirectory;
        QVERIFY(settingsDirectory.isValid());
        const QString captureDirectory = qEnvironmentVariable("STRIP_SCROLL_NATIVE_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
        }
        AppSettings settings(settingsDirectory.filePath("settings"));
        ContainerWorkspaceStore store(settings);
        ContainerContentRegistry registry;
        MeterPoller poller;
        // The window takes focus and comes to the front (below): shown
        // without activation it can open behind another application's
        // window, and the capture is then of that window.
        SentinelWindow root;
        auto* rootLayout = new QVBoxLayout(&root);
        // The border blocks are painted in this margin, outside the strip.
        rootLayout->setContentsMargins(kSentinelBand, kSentinelBand, kSentinelBand, kSentinelBand);
        auto* splitter = new QSplitter(&root);
        rootLayout->addWidget(splitter);
        ContainerManager manager(&root, splitter);
        manager.setWorkspaceAdapter(&store, &registry);
        manager.setPreviewPoller(&poller);

        // Stands in for the RX applet: an ordinary raster widget with
        // content in every row, above the meters in the same stack.
        auto* applet = new QFrame(&root);
        applet->setObjectName(QStringLiteral("stripScrollApplet"));
        applet->setFixedHeight(320);
        applet->setStyleSheet(QStringLiteral("QFrame#stripScrollApplet { background: %1; }").arg(colorName(kAppletFill)));
        auto* appletLayout = new QVBoxLayout(applet);
        for (int i = 0; i < 8; ++i) {
            appletLayout->addWidget(new QLabel(QStringLiteral("Applet row %1").arg(i + 1), applet));
        }
        registry.attachSingleton(QStringLiteral("applet:rx"), applet);

        WorkspaceDocument document;
        document.mainContainerId = QStringLiteral("strip-scroll");
        ContainerDocument container;
        container.id = document.mainContainerId;
        container.name = QStringLiteral("Receive");
        container.layout = ContentLayout::VerticalStack;
        container.contents.append(registry.makeEntry(QStringLiteral("applet:rx")));
        for (const QString& type : {QStringLiteral("meter.leveler"), QStringLiteral("meter.alcGain"),
                                    QStringLiteral("meter.comp"), QStringLiteral("meter.cfc")}) {
            ContentEntry entry = registry.makeEntry(type);
            QVERIFY2(registry.validateEntry(entry).isEmpty(), qPrintable(type));
            container.contents.append(entry);
        }
        document.containers = {container};
        QCOMPARE(manager.commitWorkspace(document, 0).status, CommitStatus::Saved);
        ContainerContentHost* host = manager.contentHost(container.id);
        QVERIFY(host);
        QScrollArea* scroll = host->findChild<QScrollArea*>();
        QVERIFY(scroll);
        const QList<MeterWidget*> meters = scroll->findChildren<MeterWidget*>();
        QVERIFY(!meters.isEmpty());

        root.resize(360, 520);
        root.show();
        // Only this window is touched: nothing else on the screen is moved,
        // hidden or closed.
        root.raise();
        root.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&root));
        // The stack must overflow its viewport by at least the largest fixed
        // offset below. How tall the stack is follows the platform's style
        // and fonts and the look's entry headers: with the look off under
        // Fusion (Linux) it fits a 520 px window whole and nothing scrolls,
        // so the window is shortened until it does. Only a settled range
        // counts (see settledScrollRange).
        QScrollBar* bar = scroll->verticalScrollBar();
        const auto geometry = [&root, scroll, bar] {
            return QStringLiteral("window %1 x %2, viewport %3 px high, stack %4 px high, scroll range %5")
                .arg(root.width()).arg(root.height()).arg(scroll->viewport()->height())
                .arg(scroll->widget() != nullptr ? scroll->widget()->height() : -1).arg(bar->maximum());
        };
        std::optional<int> settled = settledScrollRange(bar);
        qInfo().noquote() << QTest::currentDataTag() << "settled:" << geometry();
        QVERIFY2(settled.has_value(), qPrintable(QStringLiteral("the scroll range never settled: ") + geometry()));
        while (*settled < kLeastScrollRange && root.height() - kShortenStep >= kShortestWindow) {
            root.resize(root.width(), root.height() - kShortenStep);
            qInfo().noquote() << QTest::currentDataTag() << "shortened, before settling:" << geometry();
            settled = settledScrollRange(bar);
            qInfo().noquote() << QTest::currentDataTag() << "settled:" << geometry();
            QVERIFY2(settled.has_value(), qPrintable(QStringLiteral("the scroll range never settled: ") + geometry()));
        }
        QVERIFY2(*settled >= kLeastScrollRange, qPrintable(geometry()));
        const int maximum = *settled;
        QCOMPARE(bar->maximum(), maximum);
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QSKIP("Needs NATIVE_WINDOW registration and a capture of its own window.");
        }
        // The system may refuse activation to a process started in the
        // background; the run goes on, and the border decides what was seen.
        const bool active = QTest::qWaitForWindowActive(&root, kActivateTimeoutMs);
        qInfo().noquote() << QTest::currentDataTag() << "window active:" << (active ? "yes" : "no");
        QTest::qWait(kCompositeSettleMs);
        {
            const WindowCapture first = ownWindowCapture(root);
            const QString unseen = first.image.isNull() ? first.unavailable : sentinelFault(first.image, root.size());
            if (!unseen.isEmpty()) {
                if (!first.image.isNull() && !captureDirectory.isEmpty()) {
                    QVERIFY(first.image.save(captureDirectory + QLatin1Char('/') + QLatin1String(QTest::currentDataTag())
                                             + QStringLiteral("-before-first-step.png")));
                }
                QSKIP(qPrintable(QStringLiteral("Could not see the window before the first scroll step: ") + unseen));
            }
        }

        const QRect viewport(scroll->viewport()->mapTo(&root, QPoint()), scroll->viewport()->size());
        const QList<int> offsets{1, 19, maximum / 3, maximum, maximum - 17, maximum / 2, 0, 31, maximum, 3, 0};
        QStringList failures;
        // A step whose captures are not proven to be of this window is not
        // judged: the row is skipped, unless an earlier, proven step already
        // found a black band, which is then reported as the failure it is.
        const auto unseenAt = [](const QString& stage, const QString& when, const QString& reason) {
            return QStringLiteral("Could not see the window at %1, %2: %3").arg(stage, when, reason);
        };
        for (qsizetype step = 0; step < offsets.size(); ++step) {
            const QString stage = QStringLiteral("%1-step-%2-offset-%3")
                .arg(QLatin1String(QTest::currentDataTag())).arg(step, 2, 10, QLatin1Char('0')).arg(offsets[step]);
            // The strip's content height must not move under the scroll:
            // the range is the settled one before and after every step.
            QVERIFY2(bar->maximum() == maximum,
                     qPrintable(QStringLiteral("%1: the scroll range left its settled %2 before this step: %3")
                                    .arg(stage).arg(maximum).arg(geometry())));
            bar->setValue(offsets[step]);
            QVERIFY2(bar->value() == offsets[step],
                     qPrintable(QStringLiteral("%1: the bar went to %2: %3").arg(stage).arg(bar->value()).arg(geometry())));
            QCoreApplication::processEvents();
            QTest::qWait(kCompositeSettleMs);
            // The first capture precedes any forced redraw: it is what the
            // operator sees after scrolling.
            const WindowCapture staleCapture = ownWindowCapture(root);
            const QImage stale = staleCapture.image;
            if (!stale.isNull() && !captureDirectory.isEmpty()) {
                QVERIFY(stale.save(captureDirectory + QLatin1Char('/') + stage + QStringLiteral("-after-scroll.png")));
            }
            QString unseen = stale.isNull() ? staleCapture.unavailable : sentinelFault(stale, root.size());
            if (!unseen.isEmpty()) {
                const QString message = unseenAt(stage, QStringLiteral("after the scroll"), unseen);
                if (!failures.isEmpty()) {
                    QFAIL(qPrintable(failures.join(QLatin1Char('\n')) + QLatin1Char('\n') + message));
                }
                QSKIP(qPrintable(message));
            }
#ifdef NEREUS_GPU_SPECTRUM
            for (MeterWidget* meter : meters) {
                QSignalSpy frames(meter, &QRhiWidget::frameSubmitted);
                meter->update();
                if (meter->visibleRegion().isEmpty()) {
                    continue;
                }
                if (QTest::qWaitFor([&frames] { return frames.count() > 0; }, kFrameTimeoutMs)) {
                    continue;
                }
                // No frame: a window that stopped being exposed renders none,
                // and that is not the strip's fault.
                const QWindow* handle = root.windowHandle();
                if (handle == nullptr || !handle->isExposed()) {
                    const QString message = unseenAt(stage, QStringLiteral("while the meters redrew"),
                                                     ownWindowCapture(root).unavailable);
                    if (!failures.isEmpty()) {
                        QFAIL(qPrintable(failures.join(QLatin1Char('\n')) + QLatin1Char('\n') + message));
                    }
                    QSKIP(qPrintable(message));
                }
                QFAIL(qPrintable(stage + QStringLiteral(": a visible meter in an exposed window submitted no frame")));
            }
#endif
            root.repaint();
            QCoreApplication::processEvents();
            QTest::qWait(kCompositeSettleMs);
            const WindowCapture freshCapture = ownWindowCapture(root);
            const QImage fresh = freshCapture.image;
            if (!fresh.isNull() && !captureDirectory.isEmpty()) {
                QVERIFY(fresh.save(captureDirectory + QLatin1Char('/') + stage + QStringLiteral("-after-redraw.png")));
            }
            QString figures;
            if (fresh.isNull()) {
                unseen = freshCapture.unavailable;
            } else {
                QCOMPARE(fresh.size(), stale.size());
                unseen = sentinelFault(fresh, root.size());
                if (unseen.isEmpty() && root.isAncestorOf(applet)) {
                    const QRect appletInView =
                        QRect(applet->mapTo(&root, QPoint()), applet->size()).intersected(viewport);
                    unseen = appletFault(fresh, appletInView, root.size());
                }
            }
            int band = 0;
            if (!fresh.isNull()) {
                const QRect pixels = pixelRect(viewport, stale, root.size()).intersected(stale.rect());
                band = blackBandRows(stale, fresh, pixels);
                const qsizetype changed = changedPixels(stale, fresh, pixels);
                figures = QStringLiteral("black rows: %1 changed pixels: %2").arg(band).arg(changed);
                qInfo().noquote() << stage << figures << "scroll range:" << bar->maximum()
                                  << "window seen:" << (unseen.isEmpty() ? "yes" : "no");
            }
            if (!unseen.isEmpty()) {
                QString message = unseenAt(stage, QStringLiteral("after the redraw"), unseen);
                if (!figures.isEmpty()) {
                    message += QStringLiteral(" (not judged; ") + figures + QLatin1Char(')');
                }
                if (!failures.isEmpty()) {
                    QFAIL(qPrintable(failures.join(QLatin1Char('\n')) + QLatin1Char('\n') + message));
                }
                QSKIP(qPrintable(message));
            }
            QVERIFY2(bar->maximum() == maximum,
                     qPrintable(QStringLiteral("%1: the scroll range left its settled %2 during this step: %3")
                                    .arg(stage).arg(maximum).arg(geometry())));
            if (band > 0) {
                failures.append(QStringLiteral("%1: %2 viewport rows black after scrolling, filled after a redraw")
                                    .arg(stage).arg(band));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TstContainerStripScrollNative)
#include "tst_container_strip_scroll_native.moc"
