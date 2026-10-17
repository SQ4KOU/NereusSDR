// no-port-check: NereusSDR-original test for the workspace look's mark of
// "this is where the keyboard is" on controls the platform style draws: a
// dialog's default button, a push button with keyboard focus, and the
// current cell of a list or table (JJ, 2026-10-10, option 1).
//
// Every case draws real widgets twice: once as the application draws them
// with the look on (applyWorkspacePalette, as main.cpp does with the
// switch on), and once with a plain Fusion style object set on the same
// widgets, which is what the platform style draws without the look's style
// object. The difference between the two drawings is what the look adds,
// so the cases can say that the rim is the selection rim #3d78b0, that
// nothing but the rim changed, and that the shape is Fusion's own.
//
// The LAN scan dialog is built through its test seam
// (LanScanDialog::setDiscoveryStartSkippedForTest): no scan starts and no
// socket is bound. Nothing connects to a radio.
//
// Modification history (NereusSDR):
//   2026-10-10 - Written for the default button's rim and the current-row
//                 outline. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.

#include <QtTest/QtTest>

#include <QApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <memory>

#include "fakes/RemoteWindowHarness.h"
#include "gui/LanScanDialog.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;

namespace {

// The selection rim (Workspace::kGlassSelBorder).
constexpr auto kRim = "#3d78b0";
// The outline Fusion draws on a default button from the look's Highlight
// (task 46 report, "Stopped" item 2).
constexpr auto kFusionDefaultOutline = "#264c75";

constexpr int kButtonWidth = 90;
constexpr int kButtonHeight = 24;

QImage draw(QWidget* w, double dpr)
{
    // Fusion keeps a drawn button panel in the pixmap cache, and a panel
    // cached at one scale would be stretched into a drawing at another.
    QPixmapCache::clear();
    QImage img(w->size() * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    w->render(&p);
    p.end();
    img.setDevicePixelRatio(1.0);
    return img;
}

QRect scaled(const QRect& r, double dpr)
{
    return QRect(qRound(r.x() * dpr), qRound(r.y() * dpr), qRound(r.width() * dpr), qRound(r.height() * dpr));
}

// A dialog as the application's confirmations are built: plain push
// buttons no sheet styles, Cancel the default one.
struct Confirm {
    QDialog dialog;
    QPushButton* cancel{nullptr};
    QPushButton* confirm{nullptr};

    Confirm()
    {
        auto* column = new QVBoxLayout(&dialog);
        column->addWidget(new QLabel(QStringLiteral("Attenuator, ADC 1: 0 dB to 20 dB"), &dialog));
        auto* row = new QHBoxLayout;
        row->addStretch(1);
        cancel = new QPushButton(QStringLiteral("Cancel"), &dialog);
        confirm = new QPushButton(QStringLiteral("Confirm"), &dialog);
        for (QPushButton* b : {cancel, confirm}) {
            b->setFixedSize(kButtonWidth, kButtonHeight);
            b->setAutoDefault(false);
            row->addWidget(b);
        }
        cancel->setDefault(true);
        column->addLayout(row);
        dialog.show();
        QApplication::processEvents();
        QTest::qWait(30);
        QApplication::processEvents();
    }

    QRect rectOf(const QPushButton* b) const { return QRect(b->mapTo(&dialog, QPoint(0, 0)), b->size()); }
};

// The pixels of `r` that are not the colour `ground`, as a mask.
QVector<bool> shape(const QImage& img, const QRect& r, const QColor& ground)
{
    QVector<bool> out;
    out.reserve(r.width() * r.height());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            out.append(img.pixelColor(x, y) != ground);
        }
    }
    return out;
}

int countOf(const QImage& img, const QRect& r, const QColor& colour)
{
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            if (img.pixelColor(x, y) == colour) {
                ++n;
            }
        }
    }
    return n;
}

// The pixels of `r` where the two images differ: how many, and how far the
// deepest of them is from the edge of `r` (0 on the edge).
struct Difference {
    int pixels{0};
    int deepest{-1};
};

Difference difference(const QImage& a, const QImage& b, const QRect& r)
{
    Difference d;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            if (a.pixel(x, y) == b.pixel(x, y)) {
                continue;
            }
            ++d.pixels;
            const int depth = qMin(qMin(x - r.left(), r.right() - x), qMin(y - r.top(), r.bottom() - y));
            d.deepest = qMax(d.deepest, depth);
        }
    }
    return d;
}

} // namespace

class TstWorkspaceDefaultButton : public QObject
{
    Q_OBJECT

    // A plain Fusion style, never the application's: a widget given it is
    // drawn without the look's style object.
    std::unique_ptr<QStyle> m_fusion;

    void usePlainFusion(const QList<QWidget*>& widgets)
    {
        for (QWidget* w : widgets) {
            w->setStyle(m_fusion.get());
        }
        QApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(NereusSDR::Test::RemoteWindowHarness::useIsolatedProfile(QStringLiteral("workspace-default-button")));
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(app != nullptr);
        applyDarkPalette(*app);
        applyAppBaselineQss(*app);

        // With the switch off the look installs nothing: the application
        // keeps the style object it has.
        WorkspacePreview::setEnabledForTest(false);
        const QPointer<QStyle> before = QApplication::style();
        WorkspacePreview::installLiveTheming(*app);
        QCOMPARE(QApplication::style(), before.data());
        QVERIFY(!before.isNull());

        WorkspacePreview::setEnabledForTest(true);
        applyWorkspacePalette(*app);
        m_fusion.reset(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(m_fusion != nullptr);
    }

    void defaultButtonRim_data()
    {
        QTest::addColumn<double>("dpr");
        QTest::newRow("1x") << 1.0;
        QTest::newRow("2x") << 2.0;
    }

    // The default button's rim is the selection rim; its fill, its text
    // and its shape are what Fusion draws, and the button beside it is
    // untouched.
    void defaultButtonRim()
    {
        QFETCH(double, dpr);
        Confirm c;
        QVERIFY(c.cancel->isDefault());
        QVERIFY(c.cancel->styleSheet().isEmpty());
        const QRect cancel = scaled(c.rectOf(c.cancel), dpr);
        const QRect confirm = scaled(c.rectOf(c.confirm), dpr);

        const QImage look = draw(&c.dialog, dpr);
        usePlainFusion({c.cancel, c.confirm});
        const QImage fusion = draw(&c.dialog, dpr);
        const QColor ground = fusion.pixelColor(2, 2);

        // The reference is what task 46 measured.
        QVERIFY2(countOf(fusion, cancel, QColor::fromString(QLatin1StringView(kFusionDefaultOutline))) > 0,
                 "the plain Fusion drawing has no #264c75 outline: the reference is not what the test assumes");

        // Rim colour: the selection rim along every side, the dim outline gone.
        const QColor rim = QColor::fromString(QLatin1StringView(kRim));
        const int straight = qRound(2 * (kButtonWidth + kButtonHeight - 12) * dpr * dpr);
        const int rimPixels = countOf(look, cancel, rim);
        QVERIFY2(rimPixels >= straight,
                 qPrintable(QStringLiteral("%1 pixels of %2 on the default button, at least %3 expected")
                                .arg(rimPixels).arg(QLatin1String(kRim)).arg(straight)));
        QCOMPARE(countOf(look, cancel, QColor::fromString(QLatin1StringView(kFusionDefaultOutline))), 0);
        // The same number of full rim pixels as Fusion's own outline. At 1x
        // only: Fusion draws the panel through a pixmap at the window's
        // scale (1 in an offscreen test), so in a 2x drawing made here its
        // own outline is a stretched 1x one. The captures at 2x carry the
        // true 2x comparison.
        if (dpr == 1.0) {
            QCOMPARE(rimPixels, countOf(fusion, cancel, QColor::fromString(QLatin1StringView(kFusionDefaultOutline))));
        }

        // Nothing but the rim changed: every changed pixel is within the
        // rim's ring (the outline and its rounded corners).
        const Difference d = difference(look, fusion, cancel);
        QVERIFY(d.pixels >= rimPixels);
        QVERIFY2(d.deepest <= qRound(2 * dpr),
                 qPrintable(QStringLiteral("a changed pixel %1 px inside the button").arg(d.deepest)));

        // The neighbour is exactly what Fusion draws.
        QCOMPARE(difference(look, fusion, confirm).pixels, 0);
        QCOMPARE(countOf(look, confirm, rim), 0);

        // Shape: the same pixels are drawn as Fusion draws for a default
        // button, and the same as for the button beside it.
        QVERIFY(shape(look, cancel, ground) == shape(fusion, cancel, ground));
        if (dpr == 1.0) {
            QVERIFY(shape(look, cancel, ground) == shape(look, confirm, ground));
        }
    }

    // A push button with keyboard focus has Fusion's same dim outline, so
    // it takes the same rim; focus by the mouse draws nothing, as before.
    void keyboardFocusRim()
    {
        const QColor rim = QColor::fromString(QLatin1StringView(kRim));
        QPushButton button(QStringLiteral("Confirm"));
        button.resize(kButtonWidth, kButtonHeight);
        const auto panel = [&button](QStyle* style, QStyle::State extra) {
            QStyleOptionButton o;
            o.initFrom(&button);
            o.rect = button.rect();
            o.state = QStyle::State_Enabled | QStyle::State_Raised | QStyle::State_Active | extra;
            QImage img(button.size(), QImage::Format_ARGB32_Premultiplied);
            img.fill(QColor(QStringLiteral("#08080a")));
            QPainter p(&img);
            style->drawPrimitive(QStyle::PE_PanelButtonCommand, &o, &p, &button);
            p.end();
            return img;
        };
        const QRect all(QPoint(0, 0), button.size());
        const QStyle::State keyboard = QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;

        const QImage fusionFocus = panel(m_fusion.get(), keyboard);
        const int fusionOutline = countOf(fusionFocus, all, QColor::fromString(QLatin1StringView(kFusionDefaultOutline)));
        QVERIFY2(fusionOutline > 0, "Fusion draws no #264c75 outline on a keyboard-focused button");

        const QImage lookFocus = panel(QApplication::style(), keyboard);
        QCOMPARE(countOf(lookFocus, all, rim), fusionOutline);
        QCOMPARE(countOf(lookFocus, all, QColor::fromString(QLatin1StringView(kFusionDefaultOutline))), 0);

        // Focus without the keyboard, and no focus: Fusion's drawing.
        QCOMPARE(panel(QApplication::style(), QStyle::State_HasFocus), panel(m_fusion.get(), QStyle::State_HasFocus));
        QCOMPARE(panel(QApplication::style(), QStyle::State_None), panel(m_fusion.get(), QStyle::State_None));
    }

    // With the switch off the look's style object draws what Fusion draws.
    void previewOffIsFusion()
    {
        Confirm c;
        WorkspacePreview::setEnabledForTest(false);
        const QImage off = draw(&c.dialog, 1.0);
        WorkspacePreview::setEnabledForTest(true);
        usePlainFusion({c.cancel, c.confirm});
        const QImage fusion = draw(&c.dialog, 1.0);
        QCOMPARE(off, fusion);
    }

    // The LAN scan list's current cell: the outline is the selection rim,
    // a closed rectangle, with nothing tinted inside it.
    void lanScanCurrentCellOutline()
    {
        LanScanDialog::setDiscoveryStartSkippedForTest(true);
        const auto restore = qScopeGuard([] { LanScanDialog::setDiscoveryStartSkippedForTest(false); });
        LanScanDialog dialog;
        QVERIFY(QMetaObject::invokeMethod(&dialog, "onDeviceDiscovered", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("TunerGeniusXL")),
                                          Q_ARG(QString, QStringLiteral("192.168.1.60")),
                                          Q_ARG(quint16, quint16(9010)), Q_ARG(QString, QStringLiteral("1.2.11")),
                                          Q_ARG(QString, QStringLiteral("1520-1104-8823")),
                                          Q_ARG(QString, QStringLiteral("Shack TGXL"))));
        QVERIFY(QMetaObject::invokeMethod(&dialog, "onScanFinished", Qt::DirectConnection));
        QCOMPARE(dialog.rowCountForTesting(), 1);
        dialog.show();
        dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto* table = dialog.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        table->setFocus();
        // Current, not selected: the outline alone marks the cell.
        table->selectionModel()->setCurrentIndex(table->model()->index(0, 0), QItemSelectionModel::NoUpdate);
        QApplication::processEvents();
        QVERIFY(table->hasFocus());

        const QRect cell(table->viewport()->mapTo(&dialog, table->visualItemRect(table->item(0, 0)).topLeft()),
                         table->visualItemRect(table->item(0, 0)).size());
        const QColor rim = QColor::fromString(QLatin1StringView(kRim));
        const QImage look = draw(&dialog, 1.0);

        // The bounding box of the rim's pixels is a closed outline.
        QRect box;
        for (int y = cell.top(); y <= cell.bottom(); ++y) {
            for (int x = cell.left(); x <= cell.right(); ++x) {
                if (look.pixelColor(x, y) == rim) {
                    box = box.united(QRect(x, y, 1, 1));
                }
            }
        }
        QVERIFY2(!box.isEmpty(), "no selection rim pixel in the current cell");
        QVERIFY2(box.width() > 20 && box.height() > 8,
                 qPrintable(QStringLiteral("the outline is %1 x %2").arg(box.width()).arg(box.height())));
        for (int x = box.left() + 2; x <= box.right() - 2; ++x) {
            QCOMPARE(look.pixelColor(x, box.top()), rim);
            QCOMPARE(look.pixelColor(x, box.bottom()), rim);
        }
        for (int y = box.top() + 2; y <= box.bottom() - 2; ++y) {
            QCOMPARE(look.pixelColor(box.left(), y), rim);
            QCOMPARE(look.pixelColor(box.right(), y), rim);
        }

        // Inside the outline the cell is the cell without the keyboard:
        // no tint.
        table->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
        QApplication::processEvents();
        const QImage plain = draw(&dialog, 1.0);
        const QRect inside = box.adjusted(2, 2, -2, -2);
        QCOMPARE(difference(look, plain, inside).pixels, 0);
        QCOMPARE(countOf(plain, cell, rim), 0);
    }

    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
    }
};

QTEST_MAIN(TstWorkspaceDefaultButton)
#include "tst_workspace_default_button.moc"
