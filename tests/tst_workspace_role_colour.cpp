// no-port-check: NereusSDR-original test for the workspace preview's
// painted-chrome colour helper (src/gui/styles/WorkspacePreview
// roleColour() and roleColourName()) and four call sites that use it.
//
// Covers: the helper off returns the legacy colour byte for byte, alpha
// included, and the legacy HTML text exactly as written; on it returns
// the role with the legacy alpha, and the legacy colour for an unknown
// role. LayoutThumbnail and StatusBadge render role colours with the
// switch on and legacy colours with it off; the FreeDV Reporter row
// delegate paints its row text in the text role on a highlighted row;
// NetworkDiagnosticsDialog's rich-text status keeps the role colour
// after its refresh timer sets the text again.

#include <QtTest/QtTest>

#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QTableView>

#include "gui/FreeDVReporterDialog.h"
#include "gui/NetworkDiagnosticsDialog.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/LayoutThumbnail.h"
#include "gui/widgets/StatusBadge.h"
#include "models/FreeDVStationModel.h"

using namespace NereusSDR;

namespace {

QColor role(const char* name)
{
    return Workspace::WorkspacePalette::glassAndDepth().color(QString::fromLatin1(name));
}

QImage render(QWidget& w)
{
    w.ensurePolished();
    return w.grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

// The first fully opaque pixel of an icon pixmap, or an invalid colour.
QColor firstOpaquePixel(const QPixmap& pm)
{
    const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.alpha() == 255) {
                return c;
            }
        }
    }
    return {};
}

QPixmap badgeIcon(StatusBadge& badge)
{
    for (QLabel* l : badge.findChildren<QLabel*>()) {
        if (!l->pixmap().isNull()) {
            return l->pixmap();
        }
    }
    return {};
}

int countPixels(const QImage& img, const QRect& r, const QColor& c)
{
    int n = 0;
    const QRect clip = r.intersected(img.rect());
    for (int y = clip.top(); y <= clip.bottom(); ++y) {
        for (int x = clip.left(); x <= clip.right(); ++x) {
            if (img.pixel(x, y) == c.rgba()) {
                ++n;
            }
        }
    }
    return n;
}

QString statusText(NetworkDiagnosticsDialog& dlg)
{
    for (QLabel* l : dlg.findChildren<QLabel*>()) {
        if (l->text().contains(QStringLiteral("Disconnected"))
            || l->text().contains(QStringLiteral("Connected"))) {
            if (l->text().contains(QStringLiteral("<span"))) {
                return l->text();
            }
        }
    }
    return {};
}

FreeDVStation makeStation(const QString& sid, const QString& callsign)
{
    FreeDVStation s;
    s.sid = sid;
    s.callsign = callsign;
    s.gridSquare = QStringLiteral("EM00");
    s.version = QStringLiteral("1.9.0");
    s.frequencyHz = 14236000;
    s.txMode = QStringLiteral("700D");
    s.status = QStringLiteral("Active");
    s.lastUpdate = QDateTime::currentDateTime();
    return s;
}

} // namespace

class TestWorkspaceRoleColour : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        WorkspacePreview::setEnabledForTest(false);
    }

    void offReturnsLegacyByteForByte()
    {
        WorkspacePreview::setEnabledForTest(false);
        const QColor legacy(0x1a, 0x2a, 0x3a, 0x7f);
        const QColor out = WorkspacePreview::roleColour(legacy, "button");
        QCOMPARE(out.rgba(), legacy.rgba());
        QCOMPARE(out.spec(), legacy.spec());
        QCOMPARE(out, legacy);
        const QColor white(Qt::white);
        QCOMPARE(WorkspacePreview::roleColour(white, "text"), white);
    }

    void onReturnsRoleWithLegacyAlpha()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QColor opaque = WorkspacePreview::roleColour(QColor(0x1a, 0x2a, 0x3a), "button");
        QCOMPARE(opaque.rgba(), role("button").rgba());
        QCOMPARE(opaque.alpha(), 255);

        QColor expected = role("panel");
        expected.setAlpha(240);
        const QColor translucent =
            WorkspacePreview::roleColour(QColor(20, 30, 45, 240), "panel");
        QCOMPARE(translucent.rgba(), expected.rgba());
    }

    void onUnknownRoleKeepsLegacy()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QColor legacy(0x12, 0x34, 0x56, 0x78);
        QCOMPARE(WorkspacePreview::roleColour(legacy, "no-such-role"), legacy);
        QCOMPARE(WorkspacePreview::roleColour(QColor(), "text").isValid(), false);
    }

    void nameOffIsTheLegacyText()
    {
        WorkspacePreview::setEnabledForTest(false);
        QCOMPARE(WorkspacePreview::roleColourName("#3DD068", "ok"),
                 QStringLiteral("#3DD068"));
    }

    void nameOnIsTheRoleHex()
    {
        WorkspacePreview::setEnabledForTest(true);
        QCOMPARE(WorkspacePreview::roleColourName("#3DD068", "ok"),
                 role("ok").name());
        QCOMPARE(WorkspacePreview::roleColourName("#3DD068", "no-such-role"),
                 QStringLiteral("#3dd068"));
    }

    void layoutThumbnail_data()
    {
        QTest::addColumn<bool>("on");
        QTest::newRow("off") << false;
        QTest::newRow("on") << true;
    }

    void layoutThumbnail()
    {
        QFETCH(bool, on);
        WorkspacePreview::setEnabledForTest(on);
        // "2v": two rows, so the gap between them at y 43..45 shows the
        // tile background; (10, 10) is inside cell A.
        const PanLayoutGeometry geom = kPanLayouts.at(1);
        LayoutThumbnail normal(geom, false, true);
        LayoutThumbnail current(geom, true, true);
        LayoutThumbnail disabled(geom, false, false);
        const QImage n = render(normal);
        const QImage c = render(current);
        const QImage d = render(disabled);
        const QPoint gap(60, 44);
        const QPoint cell(10, 10);
        if (on) {
            QCOMPARE(n.pixelColor(gap), role("button"));
            QCOMPARE(n.pixelColor(cell), role("border-muted"));
            QCOMPARE(c.pixelColor(gap), role("selected-top"));
            QCOMPARE(d.pixelColor(gap), role("panel"));
            QCOMPARE(d.pixelColor(cell), role("disabled-bg"));
        } else {
            QCOMPARE(n.pixelColor(gap), QColor(0x1a, 0x2a, 0x3a));
            QCOMPARE(n.pixelColor(cell), QColor(0x2a, 0x5a, 0x8a));
            QCOMPARE(c.pixelColor(gap), QColor(0x00, 0x60, 0x7a));
            QCOMPARE(d.pixelColor(gap), QColor(0x10, 0x10, 0x18));
            QCOMPARE(d.pixelColor(cell), QColor(0x1a, 0x1a, 0x2a));
        }
    }

    void statusBadgeIconTint_data()
    {
        QTest::addColumn<bool>("on");
        QTest::addColumn<int>("variant");
        QTest::addColumn<QColor>("legacy");
        QTest::addColumn<QString>("roleName");
        using V = StatusBadge::Variant;
        QTest::newRow("off info") << false << int(V::Info) << QColor(0x5f, 0xa8, 0xff) << "accent";
        QTest::newRow("off on") << false << int(V::On) << QColor(0x5f, 0xff, 0x8a) << "ok";
        QTest::newRow("off tx") << false << int(V::Tx) << QColor(0xff, 0x60, 0x60) << "tx";
        QTest::newRow("on info") << true << int(V::Info) << QColor(0x5f, 0xa8, 0xff) << "accent";
        // The decided indicator tone for ok (WorkspacePreview::statusColour).
        QTest::newRow("on on") << true << int(V::On) << QColor(0x5f, 0xff, 0x8a) << "live";
        QTest::newRow("on off") << true << int(V::Off) << QColor(0x40, 0x48, 0x58)
                                << "text-inactive";
        QTest::newRow("on warn") << true << int(V::Warn) << QColor(0xff, 0xd7, 0x00) << "measured";
        QTest::newRow("on tx") << true << int(V::Tx) << QColor(0xff, 0x60, 0x60) << "tx";
    }

    void statusBadgeIconTint()
    {
        QFETCH(bool, on);
        QFETCH(int, variant);
        QFETCH(QColor, legacy);
        QFETCH(QString, roleName);
        WorkspacePreview::setEnabledForTest(on);
        StatusBadge badge;
        badge.setVariant(static_cast<StatusBadge::Variant>(variant));
        badge.setSvgIcon(QStringLiteral(":/icons/badge-mode.svg"));
        const QColor tint = firstOpaquePixel(badgeIcon(badge));
        QVERIFY2(tint.isValid(), "the badge icon has no opaque pixel");
        const QColor expected = on ? role(roleName.toLatin1().constData()) : legacy;
        QCOMPARE(tint.rgb(), expected.rgb());
    }

    void freedvRowText_data()
    {
        QTest::addColumn<bool>("on");
        QTest::newRow("off") << false;
        QTest::newRow("on") << true;
    }

    void freedvRowText()
    {
        QFETCH(bool, on);
        WorkspacePreview::setEnabledForTest(on);
        FreeDVStationModel model;
        FreeDVReporterDialog dlg(&model, nullptr, nullptr);
        auto* table = dlg.findChild<QTableView*>();
        QVERIFY(table != nullptr);
        dlg.resize(1000, 400);
        auto s = makeStation(QStringLiteral("sid-tx"), QStringLiteral("K1TTTT"));
        model.onStationAdded(QStringLiteral("sid-tx"), s);
        s.transmitting = true;
        model.onStationUpdated(QStringLiteral("sid-tx"), s);
        QVERIFY(dlg.rowHighlightColorForTest(QStringLiteral("sid-tx")).isValid());
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        const QImage img = table->viewport()->grab().toImage()
                               .convertToFormat(QImage::Format_ARGB32);
        const QRect row = table->visualRect(table->model()->index(0, 0))
                              .united(table->visualRect(table->model()->index(0, 3)));
        QVERIFY(row.isValid());
        const int white = countPixels(img, row, QColor(Qt::white));
        const int text = countPixels(img, row, role("text"));
        if (on) {
            QCOMPARE(white, 0);
            QVERIFY2(text > 0, "no pixel of the row text is in the text role");
        } else {
            QVERIFY2(white > 0, "no pixel of the row text is white");
        }
    }

    void networkStatusHtmlAfterRefresh_data()
    {
        QTest::addColumn<bool>("on");
        QTest::newRow("off") << false;
        QTest::newRow("on") << true;
    }

    void networkStatusHtmlAfterRefresh()
    {
        QFETCH(bool, on);
        WorkspacePreview::setEnabledForTest(on);
        NetworkDiagnosticsDialog dlg(nullptr, nullptr);
        // The constructor sets the status once; the 250 ms refresh timer
        // sets it again: the later setText() must write the same colour.
        QTest::qWait(600);
        const QString text = statusText(dlg);
        QVERIFY2(text.contains(QStringLiteral("Disconnected")), qPrintable(text));
        if (on) {
            // The decided indicator tone for danger (statusColourName).
            QVERIFY2(text.contains(QStringLiteral("color:%1;").arg(role("tx").name())),
                     qPrintable(text));
            QVERIFY(!text.contains(QStringLiteral("#ff6060")));
        } else {
            QVERIFY2(text.contains(QStringLiteral("color:#ff6060;")), qPrintable(text));
        }
    }
};

QTEST_MAIN(TestWorkspaceRoleColour)
#include "tst_workspace_role_colour.moc"
