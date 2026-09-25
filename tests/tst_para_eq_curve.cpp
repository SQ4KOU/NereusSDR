// =================================================================
// tests/tst_para_eq_curve.cpp  (NereusSDR)
// =================================================================
//
// R-R3-49 (parity Task 4): characterisation of the TX EQ parametric
// curve that reaches the TX channel. The curve was sampled inside
// TxEqDialog from a ParametricEqWidget; it now comes from
// ParaEqCurve (src/core), which the Core runs from txEqParaEqData.
// The numbers below were pinned from the dialog's own sampling
// (TxEqDialog::pushParametricCurveToWdsp at de1f4d54, run through a
// real TxEqDialog holding each saved curve) BEFORE the move; the
// move must give the same numbers.
//
// NereusSDR-original test. J.J. Boyd (KG4VCF), AI-assisted via
// Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <vector>

#include "core/ParaEqCurve.h"
#include "core/ParaEqEnvelope.h"
#include "gui/applets/TxEqDialog.h"
#include "gui/widgets/ParametricEqWidget.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {

struct Pt { double f; double g; double q; };

QString curveJson(int bands, bool parametric, double globalDb,
                  double minHz, double maxHz, const QList<Pt>& pts)
{
    QJsonObject root;
    root.insert(QStringLiteral("band_count"), bands);
    root.insert(QStringLiteral("parametric_eq"), parametric);
    root.insert(QStringLiteral("global_gain_db"), globalDb);
    root.insert(QStringLiteral("frequency_min_hz"), minHz);
    root.insert(QStringLiteral("frequency_max_hz"), maxHz);
    QJsonArray arr;
    for (const Pt& p : pts) {
        QJsonObject o;
        o.insert(QStringLiteral("frequency_hz"), p.f);
        o.insert(QStringLiteral("gain_db"), p.g);
        o.insert(QStringLiteral("q"), p.q);
        arr.append(o);
    }
    root.insert(QStringLiteral("points"), arr);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// The four saved curves the numbers are pinned for.
QList<QString> savedCurves()
{
    QList<QString> out;
    // 1. Ten bands, parametric (Q factors on), a preamp.
    out.append(curveJson(10, true, 3.5, 0.0, 2700.0, {
        {0, -3, 4}, {200, 2, 2.5}, {400, 4.5, 1}, {650, -6, 6}, {900, 0, 4},
        {1200, 3, 0.7}, {1500, -2, 10}, {1900, 5, 3}, {2300, 1.5, 4}, {2700, -1, 4}}));
    // 2. Five bands, parametric, another envelope: the curve is sampled at
    //    ten evenly spaced frequencies.
    out.append(curveJson(5, true, -2.0, 100.0, 3000.0, {
        {100, 6, 1.5}, {700, -4, 3}, {1500, 8, 0.5}, {2200, -9, 12}, {3000, 2, 4}}));
    // 3. Eighteen bands, Q factors off (graphic, straight lines between points).
    QList<Pt> eighteen;
    for (int i = 0; i < 18; ++i) {
        const double t = double(i) / 17.0;
        eighteen.append({50.0 + t * 3950.0, double((i * 7) % 13) - 6.0, 4.0});
    }
    out.append(curveJson(18, false, 1.0, 50.0, 4000.0, eighteen));
    // 4. Ten bands out of order and out of range: gains and Q clamp, the
    //    ends lock to the envelope, the rest sort and keep their spacing.
    out.append(curveJson(10, true, 30.0, 0.0, 2700.0, {
        {2700, 30, 50}, {1000, -40, 0.01}, {500, 5, 4}, {1002, 7, 4}, {3000, 1, 4},
        {-50, 2, 4}, {800, 3, 4}, {1800, -4, 4}, {1200, 0, 4}, {0, 9, 4}}));
    return out;
}

// Pinned from TxEqDialog::pushParametricCurveToWdsp at de1f4d54 (before
// the move), printed with 17 significant digits.
struct Pinned { std::vector<double> f; std::vector<double> g; };
QList<Pinned> pinned()
{
    return {
        {{0, 200, 400, 650, 900, 1200, 1500, 1900, 2300, 2700},
         {3.5, -3, 2, 4.5, -6, 0, 3, -2, 5, 1.5, -1}},
        {{100, 422.22222222222223, 744.44444444444446, 1066.6666666666667,
          1388.8888888888889, 1711.1111111111111, 2033.3333333333335,
          2355.5555555555557, 2677.7777777777778, 3000},
         {-2, 7.8690312715866551, 5.8701500243595177, 1.8187832568119338,
          6.8611603163400794, 7.9271517531686335, 7.7398493717980701,
          6.4781704526738384, 4.6479124265524447, 2.8734930051485543,
          3.5074939370104947}},
        {{50, 488.88888888888891, 927.77777777777783, 1366.6666666666667,
          1805.5555555555557, 2244.4444444444443, 2683.3333333333335,
          3122.2222222222226, 3561.1111111111113, 4000},
         {1, -6, -4.333333333333333, -2.6666666666666679, -0.99999999999999978,
          0.66666666666666319, 2.3333333333333335, 4, -4.4444444444444304,
          -4.222222222222209, -4}},
        {{0, 5, 500, 800, 1000, 1005, 1200, 1800, 2695, 2700},
         {24, 24, 2, 5, 3, -24, 7, 0, -4, 1, 9}},
    };
}

void compareCurve(const std::vector<double>& f, const std::vector<double>& g,
                  const Pinned& want, int curve)
{
    QCOMPARE(f.size(), want.f.size());
    QCOMPARE(g.size(), want.g.size());
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (!qFuzzyCompare(1.0 + f[i], 1.0 + want.f[i])) {
            QFAIL(qPrintable(QStringLiteral("curve %1 F[%2] %3 != %4")
                                 .arg(curve).arg(i).arg(f[i], 0, 'g', 17)
                                 .arg(want.f[i], 0, 'g', 17)));
        }
    }
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (!qFuzzyCompare(1.0 + g[i], 1.0 + want.g[i])) {
            QFAIL(qPrintable(QStringLiteral("curve %1 G[%2] %3 != %4")
                                 .arg(curve).arg(i).arg(g[i], 0, 'g', 17)
                                 .arg(want.g[i], 0, 'g', 17)));
        }
    }
}

} // namespace

class TestParaEqCurve : public QObject {
    Q_OBJECT
private slots:
    void theCoreGetsThePinnedCurveFromTheSavedValue();
    void theDialogStillSamplesThePinnedCurve();
    void aValueWithNoCurveGivesNone();
};

// The Core's path: the saved txEqParaEqData straight to the ten bands.
void TestParaEqCurve::theCoreGetsThePinnedCurveFromTheSavedValue()
{
    const QList<QString> curves = savedCurves();
    const QList<Pinned> want = pinned();
    QCOMPARE(curves.size(), want.size());
    for (int c = 0; c < curves.size(); ++c) {
        ParaEqCurve::Curve curve;
        QVERIFY(ParaEqCurve::txEqCurveFromParaEqData(
            ParaEqEnvelope::encode(curves.at(c)), curve));
        std::vector<double> f;
        std::vector<double> g;
        ParaEqCurve::sampleTxEqProfile(curve, f, g);
        compareCurve(f, g, want.at(c), c);
        if (QTest::currentTestFailed()) { return; }
        // Raw JSON saved by an early build reads the same.
        ParaEqCurve::Curve raw;
        QVERIFY(ParaEqCurve::txEqCurveFromParaEqData(curves.at(c), raw));
        ParaEqCurve::sampleTxEqProfile(raw, f, g);
        compareCurve(f, g, want.at(c), c);
        if (QTest::currentTestFailed()) { return; }
    }
}

// The widget's path: a real TxEqDialog holding each curve, sampled the way
// the dialog did before the move (the generator of the pinned numbers).
void TestParaEqCurve::theDialogStillSamplesThePinnedCurve()
{
    RadioModel radio;
    const QList<QString> curves = savedCurves();
    const QList<Pinned> want = pinned();
    for (int c = 0; c < curves.size(); ++c) {
        radio.transmitModel().setTxEqParaEqData(ParaEqEnvelope::encode(curves.at(c)));
        TxEqDialog dlg(&radio);
        auto* w = dlg.findChild<ParametricEqWidget*>(QStringLiteral("TxEqParametricWidget"));
        QVERIFY(w != nullptr);
        // Verbatim the sampling of TxEqDialog::pushParametricCurveToWdsp
        // at de1f4d54, on the widget (its response curve is now
        // ParaEqCurve::responseDb).
        std::vector<double> f(10);
        std::vector<double> g(11);
        g[0] = w->globalGainDb();
        const int n = w->bandCount();
        if (n == 10) {
            for (int i = 0; i < 10; ++i) {
                double pf = 0.0, pg = 0.0, pq = 0.0;
                w->getPointData(i, pf, pg, pq);
                f[i]   = pf;
                g[i+1] = pg;
            }
        } else {
            const double minHz = w->frequencyMinHz();
            const double maxHz = w->frequencyMaxHz();
            const double step  = (maxHz > minHz) ? (maxHz - minHz) / 9.0 : 0.0;
            for (int i = 0; i < 10; ++i) {
                const double hz = minHz + step * i;
                f[i]   = hz;
                g[i+1] = w->responseDbAtFrequency(hz);
            }
        }
        compareCurve(f, g, want.at(c), c);
        if (QTest::currentTestFailed()) { return; }
    }
}

void TestParaEqCurve::aValueWithNoCurveGivesNone()
{
    ParaEqCurve::Curve curve;
    QVERIFY(!ParaEqCurve::txEqCurveFromParaEqData(QString(), curve));
    QVERIFY(!ParaEqCurve::txEqCurveFromParaEqData(QStringLiteral("not a curve"), curve));
    QVERIFY(!ParaEqCurve::txEqCurveFromParaEqData(
        QStringLiteral("{\"points\":[{\"frequency_hz\":1}]}"), curve));
}

QTEST_MAIN(TestParaEqCurve)
#include "tst_para_eq_curve.moc"
