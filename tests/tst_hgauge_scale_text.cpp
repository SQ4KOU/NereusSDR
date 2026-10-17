// no-port-check: test-only. NereusSDR's own bar gauge (src/gui/HGauge.cpp).
//
// The gauge draws its scale numbers at 7 points. That is a 7 px font on the
// Mac and a 9 px font at 96 dpi (Windows at 100%, Linux). The widget was a
// fixed 30 px tall, which fits the 7 px numbers and cut the bottom two rows
// off the 9 px ones ("500" read like "5nn" in the amplifier panels).
//
// These cases render the gauge with grab(), never shown, at both sizes
// through the gauge's scale font seam, so the 96 dpi case runs on any
// platform:
//   - at 9 px every scale number is drawn whole, with a clear row under it;
//   - at 7 px the gauge keeps its 30 px height and the numbers fit as before;
//   - at the platform's own 7 point size the numbers are whole as well;
//   - a gauge without scale numbers stays 30 px at either size.
//
// The title in the bar is 8 points bold: 8 px on the Mac and 11 px at
// 96 dpi, where its line is taller than the 12 px bar. The text was clipped
// to the bar, so with a font whose line is as tall as Segoe UI's the tail
// of the "p" in "Temp" lost its last row or two. The title cases check that
// every gauge's title covers the same rows as the same text drawn with
// nothing to cut it, at both sizes and at larger ones with taller lines.
//
// Each number was also drawn centered on its tick, and the first and last
// ticks are the bar's ends, 2 px inside the gauge, so the gauge's left edge
// cut the first number and its right edge cut the last ("2k" lost its "k")
// on every platform. The end-number cases check, at both sizes:
//   - the first and last numbers are as wide in ink as the same text drawn
//     with room to spare, with a clear column at each edge of the gauge and
//     a clear column between them and their neighbors;
//   - the numbers between them are, pixel for pixel, where centering on
//     the tick puts them.
// The text these cases compare the gauge with is drawn by a widget, the
// way the gauge draws its own, and taken with grab() as well. Drawn on a
// QImage instead it went through another painter and picture format, and
// on Linux (FreeType) that text was not the gauge's text pixel for pixel:
// a whole "1" measured 4 columns on the image and 5 in the gauge.

#include <QtTest/QtTest>

#include <QEvent>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QWidget>

#include "gui/HGauge.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/StyleConstants.h"

using namespace NereusSDR;

namespace {

constexpr int kGaugeWidth = 252;   // the gauge's width in a 260 px applet
constexpr int kScaleTop = 22;      // first row under the bar and its gap
constexpr int kMacScalePx = 7;     // 7 points at the Mac's 72 dpi
constexpr int kWindowsScalePx = 9; // 7 points at 96 dpi
constexpr int kMacTitlePx = 8;      // the title's 8 points at 72 dpi
constexpr int kWindowsTitlePx = 11; // 8 points at 96 dpi
// Segoe UI at 11 px has a 15 px line (ascent 12, descent 3). The font the
// offscreen platform gives the Mac has a 13 px line at 11 px, which does
// not reach past the bar, and Segoe UI's 15 px line at 12 px.
constexpr int kTallLineTitlePx = 12;

// The scale numbers of every gauge the app builds with a scale: the two
// amplifier panels (AmpApplet.cpp, Rf2ksApplet.cpp), TxApplet, PhoneCwApplet
// and ModMonitorApplet.
QList<QStringList> appLabelSets()
{
    return {
        {"0", "500", "1000", "1.5k", "2k"},
        {"1", "1.5", "2", "2.5", "3"},
        {"0", "30", "55", "80", "100"},
        {"1", "1.5", "2.5", "3"},
        {"0", "33", "67", "100", "120"},
        {"0.0", "1.7", "3.3", "5.0", "6.0"},
        {"-40dB", "-30", "-20", "-10", "0", "+5", "+10"},
        {"-25dB", "-20", "-15", "-10", "-5", "0"},
        {"0", "50", "100", "125", "160"},
        {"0", "25", "50", "75", "100"},
    };
}

// A widget's picture, never shown.
QImage picture(QWidget& widget)
{
    return widget.grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

QImage render(HGauge& gauge)
{
    gauge.resize(kGaugeWidth, gauge.height());
    return picture(gauge);
}

// Ink is a pixel that differs from the background by more than the faint
// haze antialiasing leaves on the row under a glyph (a few levels of 255).
constexpr int kInkThreshold = 32;

bool isInk(QRgb pixel, QRgb background)
{
    return qAbs(qRed(pixel) - qRed(background)) > kInkThreshold
        || qAbs(qGreen(pixel) - qGreen(background)) > kInkThreshold
        || qAbs(qBlue(pixel) - qBlue(background)) > kInkThreshold;
}

bool rowHasInk(const QImage& image, int y)
{
    // The top-left pixel is background: the bar starts two pixels in and
    // nine rows down.
    const QRgb background = image.pixel(0, 0);
    for (int x = 0; x < image.width(); ++x) {
        if (isInk(image.pixel(x, y), background)) { return true; }
    }
    return false;
}

// The last row at or below fromRow that holds ink, or -1 when none does.
int lastInkRow(const QImage& image, int fromRow)
{
    for (int y = image.height() - 1; y >= fromRow; --y) {
        if (rowHasInk(image, y)) { return y; }
    }
    return -1;
}

int firstInkRow(const QImage& image, int fromRow)
{
    for (int y = fromRow; y < image.height(); ++y) {
        if (rowHasInk(image, y)) { return y; }
    }
    return -1;
}

// How many rows the labels' ink covers when nothing can cut it: the same
// text in the same font on a canvas with room to spare. The canvas takes
// the gauge's dpi so a font sized in points comes out the same pixel size.
int wholeInkRows(const QStringList& labels, const QFont& font, int dpi)
{
    QImage canvas(400, 60, QImage::Format_ARGB32);
    const int dotsPerMeter = qRound(dpi / 0.0254);
    canvas.setDotsPerMeterX(dotsPerMeter);
    canvas.setDotsPerMeterY(dotsPerMeter);
    canvas.fill(Qt::black);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRect(4, 20, 392, 40), Qt::AlignLeft | Qt::AlignTop,
               labels.join(QLatin1Char(' ')));
    p.end();
    const int first = firstInkRow(canvas, 1);
    const int last = lastInkRow(canvas, 1);
    return (first < 0) ? 0 : last - first + 1;
}

bool columnHasInk(const QImage& image, int x, int fromRow)
{
    const QRgb background = image.pixel(0, 0);
    for (int y = fromRow; y < image.height(); ++y) {
        if (isInk(image.pixel(x, y), background)) { return true; }
    }
    return false;
}

// The first and last column in [fromColumn, toColumn) that holds ink at or
// below fromRow; both -1 when none does.
struct InkSpan {
    int first = -1;
    int last = -1;
    int width() const { return (first < 0) ? 0 : last - first + 1; }
};

InkSpan inkSpan(const QImage& image, int fromColumn, int toColumn, int fromRow)
{
    InkSpan span;
    for (int x = qMax(0, fromColumn); x < qMin(toColumn, image.width()); ++x) {
        if (columnHasInk(image, x, fromRow)) {
            if (span.first < 0) { span.first = x; }
            span.last = x;
        }
    }
    return span;
}

// The gauge's scale font: 7 points, or the seam's pixel size.
QFont scaleFontOf(const HGauge& gauge, int pixelSize)
{
    QFont font(gauge.font().family(), 7);
    if (pixelSize > 0) { font.setPixelSize(pixelSize); }
    return font;
}

// One piece of scale text: its box, how it sits in the box, its color.
struct ScaleText {
    QRect box;
    Qt::Alignment alignment;
    QString text;
    QColor color;
};

// Draws scale text as HGauge::paintEvent does: a painter on the widget,
// antialiasing on, the scale font made from the painter's own family, the
// text measured and then drawn into a box with drawText(). It has the
// gauge's size and font, and its picture is taken with grab() like the
// gauge's, so the text goes through the same painter, paint device and
// picture format as the gauge's own numbers.
class ScaleTextReference : public QWidget
{
public:
    ScaleTextReference(const HGauge& gauge, int pixelSize, const QList<ScaleText>& texts)
        : m_pixelSize(pixelSize), m_texts(texts)
    {
        setFont(gauge.font());
        resize(gauge.size());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont font(p.font().family(), 7);
        if (m_pixelSize > 0) { font.setPixelSize(m_pixelSize); }
        p.setFont(font);
        const QFontMetrics fm = p.fontMetrics();
        for (const ScaleText& t : m_texts) {
            p.setPen(t.color);
            fm.horizontalAdvance(t.text);
            p.drawText(t.box, t.alignment, t.text);
        }
    }

private:
    int m_pixelSize;
    QList<ScaleText> m_texts;
};

// Workspace glyphs use the same QWidget paint device, background and colors
// as the gauge. White on black at a fixed ink threshold has different faint
// antialias rows from cyan/yellow/red on the widget's actual background.
int wholeColoredInkRows(const HGauge& gauge,int pixelSize,const QStringList& labels)
{
    QList<ScaleText> texts;
    const QFontMetrics metrics(scaleFontOf(gauge,pixelSize),&gauge);
    int x=4;
    for(int i=0;i<labels.size();++i) {
        const double value=2000.0*i/(labels.size()-1);
        const QColor color(value>=1500?Style::kGaugeDanger:value>=1000?Style::kGaugeWarning:Style::kGaugeNormal);
        const int width=metrics.horizontalAdvance(labels[i])+8;
        texts.append({QRect(x,kScaleTop,width,40),Qt::AlignLeft|Qt::AlignTop,labels[i],color});
        x+=width;
    }
    ScaleTextReference reference(gauge,pixelSize,texts);
    reference.setPalette(gauge.palette());
    reference.resize(qMax(400,x+4),kScaleTop+40);
    const QImage image=picture(reference);
    return lastInkRow(image,kScaleTop)-firstInkRow(image,kScaleTop)+1;
}

// A label with nothing to cut it: drawn in the middle of a widget the
// size of the gauge, flush to the side the gauge aligns it to. The box
// starts and ends on a whole pixel, as the gauge's does.
QImage wholeLabel(const HGauge& gauge, int pixelSize, const QString& label,
                  const QColor& color, Qt::Alignment side)
{
    ScaleTextReference reference(
        gauge, pixelSize,
        {{QRect(60, kScaleTop, 80, gauge.height() - kScaleTop), side | Qt::AlignTop, label, color}});
    return picture(reference);
}

// The pixels of columns [fromColumn, toColumn] from the scale's first row
// down, as text for a failure message: one line a row, each pixel as how
// far its red, green and blue are from the background's.
QString pixelsOf(const QImage& image, int fromColumn, int toColumn)
{
    const QRgb background = image.pixel(0, 0);
    QString out;
    for (int y = kScaleTop; y < image.height(); ++y) {
        out += QLatin1Char('\n');
        for (int x = qMax(0, fromColumn); x <= toColumn && x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            const int far = qMax(qAbs(qRed(pixel) - qRed(background)),
                                 qMax(qAbs(qGreen(pixel) - qGreen(background)),
                                      qAbs(qBlue(pixel) - qBlue(background))));
            out += QStringLiteral("%1 ").arg(far, 2, 16, QLatin1Char('0'));
        }
    }
    return out;
}

// Where the gauge puts tick i of n: the bar runs from 2 px inside the left
// edge to 2 px inside the right edge.
int tickX(int i, int n, int gaugeWidth)
{
    return 2 + static_cast<int>(static_cast<double>(i) / (n - 1) * (gaugeWidth - 4));
}

// The titles of every gauge the app builds with one.
QStringList appTitles()
{
    return {"Fwd Pwr", "RF Pwr", "SWR", "Temp", "Level", "Compression",
            "FB Level", "Correction", "+PK", "-PK"};
}

void setUpGauge(HGauge& gauge, const QStringList& labels)
{
    gauge.setRange(0.0, 2000.0);
    gauge.setYellowStart(1000.0);
    gauge.setRedStart(1500.0);
    gauge.setTitle(QStringLiteral("Fwd Pwr"));
    gauge.setTickLabels(labels);
}

} // namespace

class TestHGaugeScaleText : public QObject
{
    Q_OBJECT

private:
    // Every label set: the numbers are whole and one row under them is clear.
    void scaleNumbersAreWhole(int pixelSize);
    // Every label set: the first and last numbers are whole and inside the
    // gauge, clear of its edges and of their neighbors.
    void endNumbersAreInsideTheGauge(int pixelSize);
    // Every label set: the numbers between the first and last are where
    // centering on the tick puts them.
    void innerNumbersAreCenteredOnTheirTicks(int pixelSize);
    // Every title: on the same rows as the same text drawn unclipped.
    void titleIsWhole(int pixelSize);

private slots:
    void init() { WorkspacePreview::setEnabledForTest(false); }
    void classicExternalMinimumSurvivesPresentationEvents()
    {
        HGauge gauge;
        gauge.setMinimumWidth(120); // AudioTxInputPage's no-scale VU allocation.
        for (QEvent::Type type : {QEvent::FontChange, QEvent::StyleChange, QEvent::PaletteChange}) {
            QEvent event(type);
            QCoreApplication::sendEvent(&gauge, &event);
            QCOMPARE(gauge.minimumWidth(), 120);
        }
    }
    void workspaceExtraMinimumRestoresCallerBaseline()
    {
        HGauge gauge;
        gauge.setMinimumWidth(120);
        gauge.setTickLabels({"-40dB", "-30", "-20", "-10", "0", "+5", "+10"});
        QCOMPARE(gauge.minimumWidth(), 120);
        WorkspacePreview::setEnabledForTest(true);
        QEvent workspaceFont(QEvent::FontChange);
        QCoreApplication::sendEvent(&gauge, &workspaceFont);
        QVERIFY(gauge.minimumWidth() > 120);
        WorkspacePreview::setEnabledForTest(false);
        QEvent classicStyle(QEvent::StyleChange);
        QCoreApplication::sendEvent(&gauge, &classicStyle);
        QCOMPARE(gauge.minimumWidth(), 120);

        WorkspacePreview::setEnabledForTest(true);
        QEvent secondWorkspaceFont(QEvent::FontChange);
        QCoreApplication::sendEvent(&gauge, &secondWorkspaceFont);
        const int externalMinimum = gauge.minimumWidth() + 20;
        gauge.setMinimumWidth(externalMinimum);
        WorkspacePreview::setEnabledForTest(false);
        QEvent classicPalette(QEvent::PaletteChange);
        QCoreApplication::sendEvent(&gauge, &classicPalette);
        QCOMPARE(gauge.minimumWidth(), externalMinimum);
    }
    void workspaceMinimumLeavesWholeScaleNumbers() {
        WorkspacePreview::setEnabledForTest(true);
        scaleNumbersAreWhole(11);
        endNumbersAreInsideTheGauge(11);
        innerNumbersAreCenteredOnTheirTicks(11);
        titleIsWhole(11);
        WorkspacePreview::setEnabledForTest(false);
    }
    void windowsSizeNumbersAreWholeWithAClearRowUnder();
    void macSizeNumbersAreWholeWithAClearRowUnder();
    void platformSizeNumbersAreWholeWithAClearRowUnder();
    void macSizeGaugeKeepsItsThirtyPixels();
    void windowsSizeGaugeGrowsOnlyByWhatTheNumbersNeed();
    void gaugeWithoutScaleStaysThirtyPixels();
    void barDoesNotMoveWhenTheGaugeGrows();
    void windowsSizeEndNumbersAreInsideTheGauge();
    void macSizeEndNumbersAreInsideTheGauge();
    void platformSizeEndNumbersAreInsideTheGauge();
    void windowsSizeInnerNumbersDidNotMove();
    void macSizeInnerNumbersDidNotMove();
    void platformSizeInnerNumbersDidNotMove();
    void windowsSizeTitleIsWhole();
    void macSizeTitleIsWhole();
    void platformSizeTitleIsWhole();
    void tallLineTitleIsWhole();
};
void TestHGaugeScaleText::scaleNumbersAreWhole(int pixelSize)
{
    const QList<QStringList> sets = appLabelSets();
    for (const QStringList& labels : sets) {
        HGauge gauge;
        setUpGauge(gauge, labels);
        gauge.setScaleFontPixelSize(pixelSize);
        const QImage image = render(gauge);
        const QByteArray what = labels.join(QLatin1Char(' ')).toUtf8();

        const int first = firstInkRow(image, kScaleTop);
        const int last = lastInkRow(image, kScaleTop);
        QVERIFY2(first >= 0, what.constData());
        // Nothing in the widget's last row, so the ink ends at least one
        // row above the bottom edge.
        QVERIFY2(!rowHasInk(image, image.height() - 1),
                 qPrintable(QStringLiteral("%1: scale ink in the last row of a %2 px gauge")
                                .arg(QString::fromUtf8(what)).arg(image.height())));
        QVERIFY2(last <= image.height() - 2,
                 qPrintable(QStringLiteral("%1: scale ink reaches row %2 of a %3 px gauge")
                                .arg(QString::fromUtf8(what)).arg(last).arg(image.height())));

        // Whole: as many rows of ink as the same text has with nothing to
        // cut it.
        QFont font(gauge.font().family(), 7);
        if (pixelSize > 0) { font.setPixelSize(pixelSize); }
        const int whole=WorkspacePreview::enabled() ? wholeColoredInkRows(gauge,pixelSize,labels)
            : wholeInkRows(labels,font,gauge.logicalDpiY());
        QCOMPARE(last-first+1,whole);
    }
}

void TestHGaugeScaleText::windowsSizeNumbersAreWholeWithAClearRowUnder()
{
    scaleNumbersAreWhole(kWindowsScalePx);
}

void TestHGaugeScaleText::macSizeNumbersAreWholeWithAClearRowUnder()
{
    scaleNumbersAreWhole(kMacScalePx);
}

void TestHGaugeScaleText::platformSizeNumbersAreWholeWithAClearRowUnder()
{
    // 0 is the gauge's default: 7 points at this platform's dpi.
    scaleNumbersAreWhole(0);
}

void TestHGaugeScaleText::macSizeGaugeKeepsItsThirtyPixels()
{
    const QList<QStringList> sets = appLabelSets();
    for (const QStringList& labels : sets) {
        HGauge gauge;
        setUpGauge(gauge, labels);
        gauge.setScaleFontPixelSize(kMacScalePx);
        QCOMPARE(gauge.height(), 30);
        QCOMPARE(gauge.minimumHeight(), 30);
        QCOMPARE(gauge.maximumHeight(), 30);
        QCOMPARE(gauge.sizeHint().height(), 30);
        QCOMPARE(render(gauge).height(), 30);
    }
}

void TestHGaugeScaleText::windowsSizeGaugeGrowsOnlyByWhatTheNumbersNeed()
{
    HGauge gauge;
    setUpGauge(gauge, {"0", "500", "1000", "1.5k", "2k"});
    gauge.setScaleFontPixelSize(kWindowsScalePx);
    const QImage image = render(gauge);
    QVERIFY(gauge.height() > 30);
    QCOMPARE(gauge.minimumHeight(), gauge.height());
    QCOMPARE(gauge.maximumHeight(), gauge.height());
    QCOMPARE(gauge.sizeHint().height(), gauge.height());
    // No more than the font's own headroom under the digits: the ink ends
    // within three rows of the bottom edge.
    QVERIFY(lastInkRow(image, kScaleTop) >= image.height() - 4);

    // Back at the Mac's size the same gauge is 30 px again.
    gauge.setScaleFontPixelSize(kMacScalePx);
    QCOMPARE(gauge.height(), 30);
}

void TestHGaugeScaleText::gaugeWithoutScaleStaysThirtyPixels()
{
    HGauge gauge;
    gauge.setTitle(QStringLiteral("Level"));
    gauge.setScaleFontPixelSize(kWindowsScalePx);
    QCOMPARE(gauge.height(), 30);
    gauge.setScaleFontPixelSize(kMacScalePx);
    QCOMPARE(gauge.height(), 30);
}

void TestHGaugeScaleText::barDoesNotMoveWhenTheGaugeGrows()
{
    HGauge small;
    setUpGauge(small, {"0", "500", "1000", "1.5k", "2k"});
    small.setScaleFontPixelSize(kMacScalePx);
    HGauge tall;
    setUpGauge(tall, {"0", "500", "1000", "1.5k", "2k"});
    tall.setScaleFontPixelSize(kWindowsScalePx);

    const QImage a = render(small);
    const QImage b = render(tall);
    QVERIFY(b.height() > a.height());
    // The rows above the scale (the bar and its title) are the same picture.
    QCOMPARE(b.copy(0, 0, b.width(), kScaleTop), a.copy(0, 0, a.width(), kScaleTop));
}

void TestHGaugeScaleText::endNumbersAreInsideTheGauge(int pixelSize)
{
    const QList<QStringList> sets = appLabelSets();
    for (const QStringList& labels : sets) {
        HGauge gauge;
        setUpGauge(gauge, labels);
        gauge.setScaleFontPixelSize(pixelSize);
        const QImage image = render(gauge);
        const QString what = labels.join(QLatin1Char(' '));
        const QFont font = scaleFontOf(gauge, pixelSize);
        const QFontMetrics metrics(font, &gauge);
        const int n = labels.size();
        const int w = image.width();

        // A clear column at each edge of the gauge.
        QVERIFY2(!columnHasInk(image, 0, kScaleTop),
                 qPrintable(QStringLiteral("%1: scale ink in the gauge's first column").arg(what)));
        QVERIFY2(!columnHasInk(image, w - 1, kScaleTop),
                 qPrintable(QStringLiteral("%1: scale ink in the gauge's last column").arg(what)));

        // The neighbors are centered on their ticks; their text boxes
        // bound the room the first and last numbers have.
        const int secondLeft = tickX(1, n, w) - (metrics.horizontalAdvance(labels[1]) + 1) / 2 - 1;
        const int beforeLastRight =
            tickX(n - 2, n, w) + (metrics.horizontalAdvance(labels[n - 2]) + 1) / 2 + 1;

        // With setUpGauge's thresholds the first number is in the normal
        // color and the last in the danger color.
        const InkSpan first = inkSpan(image, 0, secondLeft, kScaleTop);
        QVERIFY2(first.first >= 1, qPrintable(what));
        QVERIFY2(first.last <= secondLeft - 2,
                 qPrintable(QStringLiteral("%1: the first number reaches column %2, its neighbor starts at %3")
                                .arg(what).arg(first.last).arg(secondLeft)));
        const QImage firstAlone = wholeLabel(gauge, pixelSize, labels.first(),
                                             QColor(Style::kGaugeNormal), Qt::AlignLeft);
        const InkSpan firstWhole = inkSpan(firstAlone, 1, w, kScaleTop);
        QVERIFY2(first.width() == firstWhole.width(),
                 qPrintable(QStringLiteral("%1: \"%2\" shows %3 of its %4 columns of ink\n"
                                           "in the gauge:%5\nwhole:%6")
                                .arg(what, labels.first()).arg(first.width()).arg(firstWhole.width())
                                .arg(pixelsOf(image, first.first - 2, first.last + 2),
                                     pixelsOf(firstAlone, firstWhole.first - 2, firstWhole.last + 2))));

        const InkSpan last = inkSpan(image, beforeLastRight + 1, w, kScaleTop);
        QVERIFY2(last.first >= beforeLastRight + 2,
                 qPrintable(QStringLiteral("%1: the last number starts at column %2, its neighbor ends at %3")
                                .arg(what).arg(last.first).arg(beforeLastRight)));
        QVERIFY2(last.last <= w - 2, qPrintable(what));
        const QImage lastAlone = wholeLabel(gauge, pixelSize, labels.last(),
                                            QColor(Style::kGaugeDanger), Qt::AlignRight);
        const InkSpan lastWhole = inkSpan(lastAlone, 1, w, kScaleTop);
        QVERIFY2(last.width() == lastWhole.width(),
                 qPrintable(QStringLiteral("%1: \"%2\" shows %3 of its %4 columns of ink\n"
                                           "in the gauge:%5\nwhole:%6")
                                .arg(what, labels.last()).arg(last.width()).arg(lastWhole.width())
                                .arg(pixelsOf(image, last.first - 2, last.last + 2),
                                     pixelsOf(lastAlone, lastWhole.first - 2, lastWhole.last + 2))));
    }
}

void TestHGaugeScaleText::innerNumbersAreCenteredOnTheirTicks(int pixelSize)
{
    const QList<QStringList> sets = appLabelSets();
    for (const QStringList& labels : sets) {
        HGauge gauge;
        // One color for every number, so the reference needs no thresholds.
        gauge.setRange(0.0, 2000.0);
        gauge.setYellowStart(5000.0);
        gauge.setRedStart(6000.0);
        gauge.setTickLabels(labels);
        gauge.setScaleFontPixelSize(pixelSize);
        const QImage image = render(gauge);
        const QFont font = scaleFontOf(gauge, pixelSize);
        const QFontMetrics metrics(font, &gauge);
        const int n = labels.size();
        const int w = image.width();

        // The numbers as the gauge has always drawn them: each centered on
        // its tick in a 40 px slot.
        QList<ScaleText> centered;
        for (int i = 1; i < n - 1; ++i) {
            centered.append({QRect(tickX(i, n, w) - 20, kScaleTop, 40, image.height() - kScaleTop),
                             Qt::AlignHCenter | Qt::AlignTop, labels[i], QColor(Style::kGaugeNormal)});
        }
        ScaleTextReference drawn(gauge, pixelSize, centered);
        const QImage reference = picture(drawn);
        QCOMPARE(reference.size(), image.size());

        for (int i = 1; i < n - 1; ++i) {
            const int half = (metrics.horizontalAdvance(labels[i]) + 1) / 2 + 1;
            const QRect box(tickX(i, n, w) - half, kScaleTop, 2 * half, image.height() - kScaleTop);
            QVERIFY2(inkSpan(image, box.left(), box.right() + 1, kScaleTop).width() > 0,
                     qPrintable(labels[i]));
            QVERIFY2(image.copy(box) == reference.copy(box),
                     qPrintable(QStringLiteral("%1: \"%2\" is not where centering on its tick puts it\n"
                                               "the gauge:%3\ncentered on the tick:%4")
                                    .arg(labels.join(QLatin1Char(' ')), labels[i],
                                         pixelsOf(image, box.left(), box.right()),
                                         pixelsOf(reference, box.left(), box.right()))));
        }
    }
}

void TestHGaugeScaleText::windowsSizeEndNumbersAreInsideTheGauge()
{
    endNumbersAreInsideTheGauge(kWindowsScalePx);
}

void TestHGaugeScaleText::macSizeEndNumbersAreInsideTheGauge()
{
    endNumbersAreInsideTheGauge(kMacScalePx);
}

void TestHGaugeScaleText::platformSizeEndNumbersAreInsideTheGauge()
{
    endNumbersAreInsideTheGauge(0);
}

void TestHGaugeScaleText::windowsSizeInnerNumbersDidNotMove()
{
    innerNumbersAreCenteredOnTheirTicks(kWindowsScalePx);
}

void TestHGaugeScaleText::macSizeInnerNumbersDidNotMove()
{
    innerNumbersAreCenteredOnTheirTicks(kMacScalePx);
}

void TestHGaugeScaleText::platformSizeInnerNumbersDidNotMove()
{
    innerNumbersAreCenteredOnTheirTicks(0);
}

void TestHGaugeScaleText::titleIsWhole(int pixelSize)
{
    constexpr int kBarTop = 9;      // the bar's first row in the gauge
    constexpr int kBarHeight = 12;
    constexpr int kCanvasBarTop = 24;

    const QStringList titles = appTitles();
    for (const QString& title : titles) {
        HGauge titled;
        titled.setTitle(title);
        titled.setTitleFontPixelSize(pixelSize);
        HGauge bare;
        const QImage with = render(titled);
        const QImage without = render(bare);
        QCOMPARE(with.size(), without.size());

        // The title's rows are the rows the title changes in the picture,
        // by any amount: the tail of a "p" on the bar's border row is
        // nearly the border's own color.
        int first = -1;
        int last = -1;
        for (int y = 0; y < with.height(); ++y) {
            for (int x = 0; x < with.width(); ++x) {
                if (with.pixel(x, y) != without.pixel(x, y)) {
                    if (first < 0) { first = y; }
                    last = y;
                    break;
                }
            }
        }
        QVERIFY2(first >= 0, qPrintable(title));

        // The same text, centered on a bar of the same height, with
        // nothing to cut it.
        QImage canvas(with.width(), 60, QImage::Format_ARGB32_Premultiplied);
        const int dotsPerMeter = qRound(titled.logicalDpiY() / 0.0254);
        canvas.setDotsPerMeterX(dotsPerMeter);
        canvas.setDotsPerMeterY(dotsPerMeter);
        const QColor barColor(Style::kInsetBg);
        canvas.fill(barColor);
        QFont font(titled.font().family(), 8, QFont::Bold);
        if (pixelSize > 0) { font.setPixelSize(pixelSize); }
        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(font);
        p.setPen(QColor(Style::kTextSecondary));
        p.drawText(QRect(2, kCanvasBarTop, canvas.width() - 4, kBarHeight),
                   Qt::AlignCenter | Qt::TextDontClip, title);
        p.end();
        int wholeFirst = -1;
        int wholeLast = -1;
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 0; x < canvas.width(); ++x) {
                if (canvas.pixel(x, y) != barColor.rgb()) {
                    if (wholeFirst < 0) { wholeFirst = y; }
                    wholeLast = y;
                    break;
                }
            }
        }
        QVERIFY2(wholeFirst >= 0, qPrintable(title));

        // Rows counted from the top of the bar.
        const QString where =
            QStringLiteral("\"%1\" is drawn on rows %2 to %3 of the bar; whole, it is rows %4 to %5")
                .arg(title).arg(first - kBarTop).arg(last - kBarTop)
                .arg(wholeFirst - kCanvasBarTop).arg(wholeLast - kCanvasBarTop);
        QVERIFY2(first - kBarTop == wholeFirst - kCanvasBarTop, qPrintable(where));
        QVERIFY2(last - kBarTop == wholeLast - kCanvasBarTop, qPrintable(where));
    }
}

void TestHGaugeScaleText::windowsSizeTitleIsWhole()
{
    titleIsWhole(kWindowsTitlePx);
}

void TestHGaugeScaleText::macSizeTitleIsWhole()
{
    titleIsWhole(kMacTitlePx);
}

void TestHGaugeScaleText::platformSizeTitleIsWhole()
{
    titleIsWhole(0);
}

void TestHGaugeScaleText::tallLineTitleIsWhole()
{
    titleIsWhole(kTallLineTitlePx);
    titleIsWhole(kTallLineTitlePx + 1);
}

QTEST_MAIN(TestHGaugeScaleText)
#include "tst_hgauge_scale_text.moc"
