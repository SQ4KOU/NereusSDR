// no-port-check: NereusSDR-original test.
// =================================================================
// tests/tst_visual_preview_quiet_layout.cpp  (NereusSDR)
// =================================================================
//
// The visual preview's waitForQuietLayout (tests/visual/PreviewData.h). A
// capture waits for the layout to stop moving, not for a fixed time, before
// it stops the timers, feeds the spectra and grabs. With a fixed wait, a
// layout step that a loaded machine runs late (a debounced resize, a
// layout still settling) landed after the freeze or the feed in some runs
// and not in others, so one scene rendered two ways. Each case below is
// such a step, made later than the old fixed wait of 900 ms.
//
// Modification history (NereusSDR):
//   2026-10-09 - Written for the workspace visual review. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QTimer>
#include <QWidget>

#include <memory>

#include "visual/PreviewData.h"

using namespace NereusSDR::Preview;

namespace {

// A debounce that ends after the fixed wait the scenes used (900 ms), as a
// shorter one does when a loaded machine starts it late.
constexpr int kDebounceMs = 950;
// A step of a layout that keeps moving, and how many steps it takes.
constexpr int kStepMs = 100;
constexpr int kSteps = 15;
constexpr int kStepGrowPx = 4;
// A timer far longer than a debounce (a tooltip hide, an idle timeout).
constexpr int kLongTimerMs = 8000;
// Generous ceilings on a loaded machine; the waits themselves are shorter.
constexpr int kPromptMs = 5000;
constexpr int kGiveUpMs = 1500;

std::unique_ptr<QWidget> makeRoot(QWidget*& child)
{
    auto root = std::make_unique<QWidget>();
    root->resize(400, 300);
    child = new QWidget(root.get());
    child->setGeometry(0, 0, 100, 100);
    root->show();
    return root;
}

} // namespace

class TstVisualPreviewQuietLayout : public QObject {
    Q_OBJECT
private slots:
    void aPendingDebouncedResizeIsWaitedFor()
    {
        QWidget* child = nullptr;
        auto root = makeRoot(child);
        auto* debounce = new QTimer(root.get());
        debounce->setSingleShot(true);
        debounce->setInterval(kDebounceMs);
        connect(debounce, &QTimer::timeout, child, [child] { child->resize(180, 120); });
        debounce->start();

        const LayoutQuiet q = waitForQuietLayout({root.get()});
        QVERIFY2(!debounce->isActive(), "the wait returned with the debounced resize still to run");
        QCOMPARE(child->size(), QSize(180, 120));
        QVERIFY(q.quiet);
    }

    void aLayoutStillMovingIsWaitedFor()
    {
        QWidget* child = nullptr;
        auto root = makeRoot(child);
        // A repeating timer: only the geometry shows that the layout moves.
        auto* step = new QTimer(root.get());
        step->setInterval(kStepMs);
        int done = 0;
        connect(step, &QTimer::timeout, child, [child, step, &done] {
            child->resize(child->width() + kStepGrowPx, child->height());
            if (++done == kSteps) {
                step->stop();
            }
        });
        step->start();

        const LayoutQuiet q = waitForQuietLayout({root.get()});
        QCOMPARE(done, kSteps);
        QCOMPARE(child->width(), 100 + kSteps * kStepGrowPx);
        QVERIFY(q.quiet);
        QVERIFY(q.moves > 0);
    }

    void aLayoutThatNeverSettlesGivesUpAndSaysWhy()
    {
        QWidget* child = nullptr;
        auto root = makeRoot(child);
        auto* again = new QTimer(root.get());
        again->setObjectName(QStringLiteral("rearming"));
        again->setSingleShot(true);
        again->setInterval(kStepMs);
        connect(again, &QTimer::timeout, child, [child, again] {
            child->move(child->x() == 0 ? 1 : 0, 0);
            again->start();
        });
        again->start();

        const LayoutQuiet q = waitForQuietLayout({root.get()}, kGiveUpMs);
        QVERIFY(!q.quiet);
        QVERIFY2(q.waitedMs >= kGiveUpMs, qPrintable(QString::number(q.waitedMs)));
        QVERIFY2(q.pending.join(QLatin1Char(' ')).contains(QLatin1String("rearming")),
                 qPrintable(q.pending.join(QLatin1String(", "))));
        again->stop();
    }

    void aQuietLayoutReturnsPromptly()
    {
        QWidget* child = nullptr;
        auto root = makeRoot(child);
        const LayoutQuiet q = waitForQuietLayout({root.get()});
        QVERIFY(q.quiet);
        QCOMPARE(q.moves, 0);
        QVERIFY2(q.waitedMs < kPromptMs, qPrintable(QString::number(q.waitedMs)));
    }

    void aLongTimerIsNotWaitedFor()
    {
        QWidget* child = nullptr;
        auto root = makeRoot(child);
        auto* idle = new QTimer(root.get());
        idle->setSingleShot(true);
        idle->setInterval(kLongTimerMs);
        idle->start();

        const LayoutQuiet q = waitForQuietLayout({root.get()});
        QVERIFY(q.quiet);
        QVERIFY2(q.waitedMs < kPromptMs, qPrintable(QString::number(q.waitedMs)));
        QVERIFY(idle->isActive());
    }
};

QTEST_MAIN(TstVisualPreviewQuietLayout)
#include "tst_visual_preview_quiet_layout.moc"
