// tst_meter_item_scale.cpp — ScaleItem API parity with Thetis clsScaleItem.
//
// Phase B2 / B3 of the meter-parity port plan:
//   B2: ShowType + centered title render
//   B3: generalScale tick/baseline layout parity
//
// Source of truth:
//   Thetis MeterManager.cs:14785-15853  clsScaleItem class definition
//   Thetis MeterManager.cs:31879-31886  ShowType title render pass
//   Thetis MeterManager.cs:32338+       generalScale() helper

#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>

#include "gui/meters/MeterItem.h"
#include "gui/meters/MeterPoller.h"

using namespace NereusSDR;

class TstMeterItemScale : public QObject
{
    Q_OBJECT

private:
    // Count pixels in `img` within `region` whose color is close to `wanted`
    // (ignoring alpha). Used to prove a title or marker actually rendered
    // somewhere.
    static int countMatching(const QImage& img, const QRect& region,
                             const QColor& wanted, int tol = 40)
    {
        int hits = 0;
        for (int y = region.top(); y <= region.bottom(); ++y) {
            for (int x = region.left(); x <= region.right(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (std::abs(c.red()   - wanted.red())   <= tol &&
                    std::abs(c.green() - wanted.green()) <= tol &&
                    std::abs(c.blue()  - wanted.blue())  <= tol) {
                    ++hits;
                }
            }
        }
        return hits;
    }

private slots:
    // ---- Phase B2: ShowType + centered title ----

    void showType_default_is_false()
    {
        ScaleItem s;
        QCOMPARE(s.showType(), false);
    }

    void showType_roundtrip()
    {
        ScaleItem s;
        s.setShowType(true);
        QCOMPARE(s.showType(), true);
    }

    void titleColour_defaults_and_roundtrip()
    {
        // Thetis ShowType draws in scale.FontColourType — the reference
        // screenshot shows these titles in red. NereusSDR stores that as
        // a settable QColor with a red default to match the screenshot.
        ScaleItem s;
        QCOMPARE(s.titleColour(), QColor(Qt::red));
        s.setTitleColour(QColor(Qt::green));
        QCOMPARE(s.titleColour(), QColor(Qt::green));
    }

    void paint_with_showType_false_does_not_draw_title()
    {
        QImage img(200, 80, QImage::Format_ARGB32);
        img.fill(Qt::black);
        {
            QPainter p(&img);
            ScaleItem s;
            s.setRect(0.0f, 0.0f, 1.0f, 1.0f);
            s.setBindingId(MeterBinding::TxAlc);
            s.setRange(-30.0, 0.0);
            // showType defaults to false
            s.paint(p, img.width(), img.height());
        }
        // No red pixels anywhere — ticks are the default tick color
        // (#c8d8e8 — cool gray), not red.
        const int redHits = countMatching(img, img.rect(), QColor(Qt::red), 20);
        QCOMPARE(redHits, 0);
    }

    void paint_with_showType_true_draws_red_title_in_top_strip()
    {
        QImage img(200, 80, QImage::Format_ARGB32);
        img.fill(Qt::black);
        {
            QPainter p(&img);
            ScaleItem s;
            s.setRect(0.0f, 0.0f, 1.0f, 1.0f);
            s.setBindingId(MeterBinding::TxAlc);
            s.setRange(-30.0, 0.0);
            s.setShowType(true);
            // Red is the default title colour — don't override it.
            s.paint(p, img.width(), img.height());
        }

        // Title should land in the top ~30% of the widget (before the
        // tick row which Thetis places at y + h*0.85).
        const QRect topBand(0, 0, img.width(), img.height() * 30 / 100);
        const int redInTop = countMatching(img, topBand, QColor(Qt::red), 60);
        QVERIFY2(redInTop > 0,
                 "ShowType should paint the ALC title in the top band "
                 "of the ScaleItem rect");

        // And no red in the bottom band (ticks are gray, not red).
        const QRect botBand(0, img.height() * 70 / 100,
                            img.width(), img.height() * 30 / 100);
        const int redInBot = countMatching(img, botBand, QColor(Qt::red), 20);
        QCOMPARE(redInBot, 0);
    }

    void paint_with_showType_true_uses_readingName_text()
    {
        // Sanity check: the ScaleItem title should come from
        // readingName(bindingId), not from a hardcoded string. Swap the
        // binding to SignalMaxBin and expect a much wider title ("Signal
        // Max FFT Bin" is long) — the red-pixel count should go UP.
        auto redHitsForBinding = [this](int binding) {
            QImage img(200, 80, QImage::Format_ARGB32);
            img.fill(Qt::black);
            {
                QPainter p(&img);
                ScaleItem s;
                s.setRect(0.0f, 0.0f, 1.0f, 1.0f);
                s.setBindingId(binding);
                s.setRange(-140.0, 0.0);
                s.setShowType(true);
                s.paint(p, img.width(), img.height());
            }
            return countMatching(img,
                                 QRect(0, 0, img.width(), img.height() * 30 / 100),
                                 QColor(Qt::red), 60);
        };
        const int alcHits = redHitsForBinding(MeterBinding::TxAlc);
        const int maxBinHits = redHitsForBinding(MeterBinding::SignalMaxBin);
        QVERIFY2(maxBinHits > alcHits,
                 "Signal Max FFT Bin should produce more red title pixels "
                 "than ALC (longer label -> wider glyphs)");
    }

    void serialize_roundtrip_with_showType()
    {
        ScaleItem s;
        s.setShowType(true);
        s.setTitleColour(QColor(0xff, 0x99, 0x11));
        s.setBindingId(MeterBinding::TxAlc);
        s.setRange(-30.0, 0.0);

        const QString serialized = s.serialize();

        ScaleItem s2;
        QVERIFY(s2.deserialize(serialized));
        QCOMPARE(s2.showType(), true);
        QCOMPARE(s2.titleColour(), QColor(0xff, 0x99, 0x11));
    }

    void deserialize_legacy_payload_defaults_showType_false()
    {
        // Pre-B2 ScaleItem serialized 15 fields (through m_fontSize). A
        // legacy payload must still load and produce showType=false.
        const QString legacy = QStringLiteral(
            "SCALE|0|0|1|1|-1|0|H|-30|0|6|4|#ffc8d8e8|#ff8090a0|10");
        ScaleItem s;
        QVERIFY(s.deserialize(legacy));
        QCOMPARE(s.showType(), false);
    }
};

QTEST_MAIN(TstMeterItemScale)
#include "tst_meter_item_scale.moc"
