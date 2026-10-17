// no-port-check: NereusSDR-original offscreen regression for the free canvas extent.
// Modification history (NereusSDR):
//   2026-10-09 - The free canvas minimum follows where its views are now:
//                a view's earlier, larger boundary (a native window's
//                geometry before it settles) no longer keeps the canvas
//                larger until the next projection, which made the
//                profile-contest strips' minimum vary between launches.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - A view moved after the surface placed it (a native child
//                window's late, rounded geometry report under a scale
//                factor below one) goes back where it was placed, and a
//                view that keeps moving is put back a few times at most.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest/QtTest>

#include <QPointer>
#include <QWidget>

#include "gui/containers/ContainerDocument.h"
#include "gui/containers/FreeCanvasSurface.h"

using namespace NereusSDR;

namespace {

// The surface's margin around its content (FreeCanvasSurface.cpp kMargin).
constexpr int kMargin = 20;

// How often the surface puts a moved view back after one placement
// (FreeCanvasSurface.cpp kMaxPutBacks).
constexpr int kMaxPutBacks = 3;
// Passes that let a chain of queued presentation syncs run out.
constexpr int kSettlePasses = 50;

void settle()
{
    for (int pass = 0; pass < 4; ++pass) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

void settleLong()
{
    for (int pass = 0; pass < kSettlePasses; ++pass) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

// One meter entry at (0, 0) 420x49, projected onto a shown surface.
struct Canvas {
    QWidget host;
    FreeCanvasSurface* surface = nullptr;
    QWidget* view = nullptr;

    Canvas()
    {
        host.resize(1600, 400);
        surface = new FreeCanvasSurface(&host);
        surface->resize(1600, 400);
        view = new QWidget(surface);
        ContainerDocument document;
        document.id = QStringLiteral("extent");
        document.layout = ContentLayout::FreeCanvas;
        ContentEntry entry;
        entry.id = QStringLiteral("meter");
        entry.typeId = QStringLiteral("meter.sMeter");
        entry.setFreeCanvasRect(QRectF(0, 0, 420, 49));
        document.contents = {entry};
        host.show();
        surface->project(document, {{entry.id, QPointer<QWidget>(view)}});
        settle();
    }
};

// Moves its view `offset` from `home` whenever it is anywhere else, as a
// native window whose rounded report never matches the placed position.
class Stubborn : public QObject
{
public:
    Stubborn(QWidget* view, QPoint home, QPoint offset) : m_view(view), m_home(home), m_offset(offset)
    {
        view->installEventFilter(this);
    }
    int putBacks = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_view && event->type() == QEvent::Move && m_view->pos() != m_home + m_offset) {
            if (m_view->pos() == m_home) {
                ++putBacks;
            }
            QMetaObject::invokeMethod(m_view, [this] { m_view->move(m_home + m_offset); }, Qt::QueuedConnection);
        }
        return false;
    }

private:
    QWidget* m_view;
    QPoint m_home;
    QPoint m_offset;
};

} // namespace

class TstFreeCanvasExtent : public QObject
{
    Q_OBJECT

private slots:
    void minimumFollowsTheViewsWhereTheyAreNow()
    {
        QWidget host;
        host.resize(1600, 400);
        auto* surface = new FreeCanvasSurface(&host);
        surface->resize(1600, 400);
        auto* view = new QWidget(surface);

        ContainerDocument document;
        document.id = QStringLiteral("extent");
        document.layout = ContentLayout::FreeCanvas;
        ContentEntry entry;
        entry.id = QStringLiteral("meter");
        entry.typeId = QStringLiteral("meter.sMeter");
        entry.setFreeCanvasRect(QRectF(0, 0, 420, 49));
        document.contents = {entry};

        host.show();
        surface->project(document, {{entry.id, QPointer<QWidget>(view)}});
        settle();
        const QSize placed = surface->minimumSize();
        // The view at its rect, with the margin beyond it.
        QCOMPARE(view->size(), QSize(420, 49));
        QVERIFY(placed.width() >= view->geometry().right() + 1 + kMargin);
        QVERIFY(placed.height() >= view->geometry().bottom() + 1 + kMargin);

        // A boundary reported larger for a while (a native child window
        // before its geometry settles), then back where the view belongs.
        const QRect home = view->geometry();
        view->setGeometry(home.adjusted(0, 0, 600, 30));
        settle();
        QVERIFY(surface->minimumHeight() > placed.height());
        view->setGeometry(home);
        settle();
        QVERIFY2(surface->minimumSize() == placed,
                 qPrintable(QStringLiteral("the canvas minimum stays %1x%2 after the view returned to %3x%4 "
                                           "(placed %5x%6)")
                                .arg(surface->minimumWidth()).arg(surface->minimumHeight())
                                .arg(home.width()).arg(home.height())
                                .arg(placed.width()).arg(placed.height())));
    }

    // Under a scale factor below one (the visual preview's lower-DPR
    // profiles) a native child window rounds its geometry and reports it
    // back, and a report made for an earlier parent can land after the
    // surface placed the view: profile-contest at laptop-1280x800@1.25 had
    // its meters 1 px above or below their place, by launch, and the
    // canvas minimum 90 or 92 with them.
    void aViewMovedAfterItsPlacementGoesBack()
    {
        Canvas c;
        const QRect home = c.view->geometry();
        const QSize placed = c.surface->minimumSize();
        c.view->move(home.topLeft() + QPoint(0, 1));
        settleLong();
        QCOMPARE(c.view->pos(), home.topLeft());
        QCOMPARE(c.surface->minimumSize(), placed);
    }

    // A report that rounds the same way every time can never match the
    // placed position: the surface stops putting the view back.
    void aViewThatKeepsMovingIsPutBackAFewTimesAtMost()
    {
        Canvas c;
        const QRect home = c.view->geometry();
        Stubborn stubborn(c.view, home.topLeft(), QPoint(0, 1));
        c.view->move(home.topLeft() + QPoint(0, 1));
        settleLong();
        QVERIFY2(stubborn.putBacks >= 1, "the surface never put the view back");
        QVERIFY2(stubborn.putBacks <= kMaxPutBacks, qPrintable(QString::number(stubborn.putBacks)));
        QCOMPARE(c.view->pos(), home.topLeft() + QPoint(0, 1));
        // The canvas still covers the view where it is.
        QVERIFY(c.surface->minimumHeight() >= c.view->geometry().bottom() + 1 + kMargin);
    }
};

QTEST_MAIN(TstFreeCanvasExtent)
#include "tst_free_canvas_extent.moc"
