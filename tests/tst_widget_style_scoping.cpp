// no-port-check: NereusSDR-original offscreen regression for container stylesheets that boxed their children.
// Modification history (NereusSDR):
//   2026-10-09 - A container's own border stays on the container: the
//                antenna switch toast draws one bar on its left edge and
//                its check mark is U+2713, and the floating pan's title
//                bar and the VAX first-run rows draw their bottom line
//                once, not under every label. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
//   2026-10-09 - The info, warning and error status toasts draw the
//                severity bar their author wrote, one bar on the left edge
//                in the accentFor() colour. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
//   2026-10-09 - The pan toolbar's ANT flyout rows and ATT check box are
//                not boxed by the flyout's own frame rule. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - The Alex-2 Filters status bar's labels are not boxed by
//                the bar's own frame rule. J.J. Boyd (KG4VCF), AI-assisted
//                via Anthropic Claude Code.
#include <QtTest/QtTest>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QStyleOptionButton>
#include <QVector>

#include "core/audio/VirtualCableDetector.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/StyleConstants.h"
#include "gui/VaxFirstRunDialog.h"
#include "gui/widgets/AntennaSwitchToast.h"
#include "gui/widgets/StatusToast.h"
#include "gui/setup/hardware/AntennaAlexAlex2Tab.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

// The antenna switch toast's accent (AntennaSwitchToast.cpp).
constexpr QRgb kToastAccent = 0xff00ff88;
// A border edge: most of the edge's pixels are the border colour. A glyph
// that touches an edge covers far less than this.
constexpr double kEdgeShare = 0.6;
constexpr int kColourTolerance = 8;

enum class Edge { Left, Right, Top, Bottom };

QImage imageOf(QWidget* w)
{
    w->ensurePolished();
    return w->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

bool sameColour(QRgb a, QRgb b)
{
    return qAlpha(a) > 200 && std::abs(qRed(a) - qRed(b)) <= kColourTolerance
           && std::abs(qGreen(a) - qGreen(b)) <= kColourTolerance
           && std::abs(qBlue(a) - qBlue(b)) <= kColourTolerance;
}

// The share of the pixels along one edge line of `img` (`inset` pixels in)
// that are `colour`.
double edgeShare(const QImage& img, Edge edge, QRgb colour, int inset = 0)
{
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const int length = vertical ? img.height() : img.width();
    if (length == 0 || img.width() <= inset || img.height() <= inset) {
        return 0.0;
    }
    int hits = 0;
    for (int i = 0; i < length; ++i) {
        int x = i;
        int y = i;
        switch (edge) {
        case Edge::Left: x = inset; break;
        case Edge::Right: x = img.width() - 1 - inset; break;
        case Edge::Top: y = inset; break;
        case Edge::Bottom: y = img.height() - 1 - inset; break;
        }
        if (sameColour(img.pixel(x, y), colour)) {
            ++hits;
        }
    }
    return double(hits) / double(length);
}

// The first edge of `w`'s own rendering that is drawn in `colour`, or an
// empty string.
QString borderEdgeIn(QWidget* w, QRgb colour)
{
    const QImage img = imageOf(w);
    const QList<QPair<Edge, const char*>> edges = {
        {Edge::Left, "left"}, {Edge::Right, "right"}, {Edge::Top, "top"}, {Edge::Bottom, "bottom"}};
    for (const auto& [edge, name] : edges) {
        if (edgeShare(img, edge, colour) >= kEdgeShare) {
            return QString::fromLatin1(name);
        }
    }
    return {};
}

QRgb rgbOf(const char* hex)
{
    return QColor(QString::fromLatin1(hex)).rgba();
}

QVector<DetectedCable> cablesPayload()
{
    QVector<DetectedCable> v;
    v.push_back({VirtualCableProduct::VbCableA, QStringLiteral("CABLE-A Input (VB-Audio Virtual Cable)"), true, 0});
    v.push_back({VirtualCableProduct::VbCableB, QStringLiteral("CABLE-B Input (VB-Audio Virtual Cable)"), true, 0});
    return v;
}

} // namespace

class TstWidgetStyleScoping : public QObject
{
    Q_OBJECT

private slots:
    void antennaToastCheckMarkIsU2713()
    {
        AntennaSwitchToast toast(QStringLiteral("Slice A moved from ANT1 to ANT2."));
        QStringList texts;
        bool found = false;
        for (QLabel* label : toast.findChildren<QLabel*>()) {
            texts << label->text();
            found = found || label->text() == QString(QChar(0x2713));
        }
        QVERIFY2(found, qPrintable(QStringLiteral("no label reads U+2713; labels: [%1]")
                                       .arg(texts.join(QStringLiteral("], [")))));
    }

    void antennaToastDrawsItsOwnBar()
    {
        AntennaSwitchToast toast(QStringLiteral("Slice A moved from ANT1 to ANT2."));
        toast.layout()->activate();
        const QImage img = imageOf(&toast);
        QCOMPARE(img.size(), QSize(380, 50));
        // The 3 px accent bar is the toast's left edge.
        for (int x = 0; x < 3; ++x) {
            const double share = edgeShare(img, Edge::Left, kToastAccent, x);
            QVERIFY2(share >= kEdgeShare,
                     qPrintable(QStringLiteral("column %1 of the toast is %2% accent").arg(x).arg(share * 100.0)));
        }
        // Nowhere else is there a bar: no other column is mostly accent.
        for (int x = 3; x < img.width(); ++x) {
            const double share = edgeShare(img, Edge::Left, kToastAccent, x);
            QVERIFY2(share < kEdgeShare,
                     qPrintable(QStringLiteral("column %1 of the toast is %2% accent").arg(x).arg(share * 100.0)));
        }
    }

    void antennaToastChildrenAreNotBoxed()
    {
        AntennaSwitchToast toast(QStringLiteral("Slice A moved from ANT1 to ANT2."));
        toast.layout()->activate();
        toast.ensurePolished();
        for (QLabel* label : toast.findChildren<QLabel*>()) {
            for (const QRgb colour : {kToastAccent, rgbOf(Style::kBorder)}) {
                const QString edge = borderEdgeIn(label, colour);
                QVERIFY2(edge.isEmpty(), qPrintable(QStringLiteral("label \"%1\" draws a %2 border on its %3 edge")
                                                        .arg(label->text(), QColor(colour).name(), edge)));
            }
        }
    }

    // Alex-2 Filters' status bar: "QFrame { border: ... }" on the bar also
    // matched its labels (a QLabel is a QFrame), so "Alex-2 board: Not
    // detected" and the "Currently selected" summary each drew a box.
    void alex2StatusLabelsAreNotBoxed()
    {
        RadioModel model;
        AntennaAlexAlex2Tab tab(&model);
        tab.resize(1200, 700);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        int checked = 0;
        for (QLabel* label : tab.findChildren<QLabel*>()) {
            if (!label->text().startsWith(QStringLiteral("Currently selected"))
                && !label->text().startsWith(QStringLiteral("Alex-2 board"))) {
                continue;
            }
            ++checked;
            const QRgb mid = label->palette().color(QPalette::Mid).rgba();
            const QString edge = borderEdgeIn(label, mid);
            QVERIFY2(edge.isEmpty(), qPrintable(QStringLiteral("label \"%1\" draws a %2 border on its %3 edge")
                                                    .arg(label->text(), QColor(mid).name(), edge)));
        }
        QCOMPARE(checked, 2);
    }

    void statusToastDrawsItsSeverityBar_data()
    {
        QTest::addColumn<int>("severity");
        QTest::addColumn<QString>("accent");
        // StatusToast.cpp accentFor(): the bottom bar's badge palette.
        QTest::newRow("info") << int(ToastSeverity::Info) << QString::fromLatin1(Style::kAccent);
        QTest::newRow("warning") << int(ToastSeverity::Warning) << QString::fromLatin1(Style::kAmberText);
        QTest::newRow("error") << int(ToastSeverity::Error) << QString::fromLatin1(Style::kRedBorder);
    }

    void statusToastDrawsItsSeverityBar()
    {
        QFETCH(int, severity);
        QFETCH(QString, accent);
        StatusToast toast(QStringLiteral("TX moved to slice B."), ToastSeverity(severity), 60000);
        toast.layout()->activate();
        const QImage img = imageOf(&toast);
        QCOMPARE(img.width(), 380);
        const QRgb colour = QColor(accent).rgba();
        // The 3 px severity bar is the toast's left edge.
        for (int x = 0; x < 3; ++x) {
            const double share = edgeShare(img, Edge::Left, colour, x);
            QVERIFY2(share >= kEdgeShare,
                     qPrintable(QStringLiteral("column %1 of the toast is %2% %3").arg(x).arg(share * 100.0).arg(accent)));
        }
        for (int x = 3; x < img.width(); ++x) {
            const double share = edgeShare(img, Edge::Left, colour, x);
            QVERIFY2(share < kEdgeShare,
                     qPrintable(QStringLiteral("column %1 of the toast is %2% %3").arg(x).arg(share * 100.0).arg(accent)));
        }
        // The message label is not boxed.
        for (QLabel* label : toast.findChildren<QLabel*>()) {
            for (const QRgb edgeColour : {colour, rgbOf(Style::kBorder)}) {
                const QString edge = borderEdgeIn(label, edgeColour);
                QVERIFY2(edge.isEmpty(), qPrintable(QStringLiteral("label \"%1\" draws a %2 border on its %3 edge")
                                                        .arg(label->text(), QColor(edgeColour).name(), edge)));
            }
        }
    }

    // The flyout frame (SpectrumOverlayPanel kPanelStyle, #304050) belongs to
    // the flyout: the RX and TX antenna rows and the step attenuator check
    // box do not draw it again.
    void overlayFlyoutRowsAndCheckBoxAreNotBoxed()
    {
        QWidget host;
        host.resize(600, 400);
        auto* panel = new SpectrumOverlayPanel(&host);
        Q_UNUSED(panel);
        const QRgb frame = rgbOf("#304050");
        auto* check = host.findChild<QCheckBox*>(QStringLiteral("attEnableCheck"));
        QVERIFY(check != nullptr);
        check->parentWidget()->layout()->activate();
        const QString edge = borderEdgeIn(check, frame);
        QVERIFY2(edge.isEmpty(), qPrintable(QStringLiteral("the step attenuator check box draws the flyout frame "
                                                           "on its %1 edge").arg(edge)));
        // Its indicator still draws its square over the flyout: the
        // unchecked box must show. The square is faint (Fusion's outline
        // from the inherited fill), so the comparison is exact.
        check->setChecked(false);
        QStyleOptionButton option;
        option.initFrom(check);
        const QRect indicator = check->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, check);
        QVERIFY(!indicator.isEmpty());
        QWidget* attFlyout = check->parentWidget();
        const QImage flyoutImage = imageOf(attFlyout);
        const QPoint origin = check->mapTo(attFlyout, indicator.topLeft());
        const QRgb flyoutFill = flyoutImage.pixel(origin.x() + indicator.width() + 2, check->y() - 1);
        int drawn = 0;
        for (int y = 0; y < indicator.height(); ++y) {
            for (int x = 0; x < indicator.width(); ++x) {
                drawn += flyoutImage.pixel(origin.x() + x, origin.y() + y) != flyoutFill ? 1 : 0;
            }
        }
        QVERIFY2(drawn > 0, "the unchecked step attenuator indicator draws nothing over the flyout");
        int rows = 0;
        for (const char* name : {"m_rxAntCmb", "m_txAntCmb"}) {
            auto* combo = host.findChild<QComboBox*>(QString::fromLatin1(name));
            QVERIFY2(combo != nullptr, name);
            QWidget* row = combo->parentWidget();
            QWidget* flyout = row->parentWidget();
            flyout->layout()->activate();
            row->ensurePolished();
            // Above the row's label (the combo draws its own frame): no
            // frame line along the row's top edge.
            QLabel* label = row->findChild<QLabel*>();
            QVERIFY(label != nullptr);
            const QImage rowImage = imageOf(row);
            int hits = 0;
            int span = 0;
            for (int x = label->x() + 3; x < label->x() + label->width() - 3; ++x, ++span) {
                hits += sameColour(rowImage.pixel(x, 0), frame) ? 1 : 0;
            }
            QVERIFY(span > 0);
            const double share = double(hits) / double(span);
            QVERIFY2(share < kEdgeShare, qPrintable(QStringLiteral("the %1 row draws the flyout frame above its "
                                                                   "label (%2%)").arg(QString::fromLatin1(name)).arg(share * 100.0)));
            ++rows;
        }
        QCOMPARE(rows, 2);
    }

    void floatingPanTitleLabelsHaveNoBottomLine()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        const QRgb line = rgbOf("#203040");
        QLabel* title = nullptr;
        for (QLabel* label : applet.findChildren<QLabel*>()) {
            if (label->text() == QStringLiteral("Pan pan-0")) {
                title = label;
            }
        }
        QVERIFY(title != nullptr);
        QWidget* bar = title->parentWidget();
        QVERIFY(bar != nullptr);
        bar->ensurePolished();
        // The bar keeps its own bottom line.
        QCOMPARE(borderEdgeIn(bar, line), QStringLiteral("bottom"));
        const QList<QLabel*> labels = bar->findChildren<QLabel*>();
        QVERIFY(labels.size() >= 2);
        for (QLabel* label : labels) {
            const QString edge = borderEdgeIn(label, line);
            QVERIFY2(edge.isEmpty(), qPrintable(QStringLiteral("label \"%1\" draws the bar's line on its %2 edge")
                                                    .arg(label->text(), edge)));
        }
    }

    void vaxFirstRunRowLabelsHaveNoBottomLine()
    {
        VaxFirstRunDialog dlg(FirstRunScenario::WindowsCablesFound, cablesPayload());
        dlg.ensurePolished();
        const QRgb line = rgbOf(Style::kInsetBorder);
        int rows = 0;
        for (QLabel* label : dlg.findChildren<QLabel*>()) {
            if (!label->text().startsWith(QStringLiteral("VAX "))) {
                continue;
            }
            // The row is the VAX label's parent; it keeps its own line, and
            // nothing in it draws a second one.
            QWidget* row = label->parentWidget();
            QCOMPARE(borderEdgeIn(row, line), QStringLiteral("bottom"));
            for (QLabel* inner : row->findChildren<QLabel*>()) {
                const QString edge = borderEdgeIn(inner, line);
                QVERIFY2(edge.isEmpty(), qPrintable(QStringLiteral("label \"%1\" draws the row's line on its %2 edge")
                                                        .arg(inner->text(), edge)));
            }
            ++rows;
        }
        QVERIFY(rows >= 2);
    }
};

QTEST_MAIN(TstWidgetStyleScoping)
#include "tst_widget_style_scoping.moc"
