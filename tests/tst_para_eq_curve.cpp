// no-port-check: NereusSDR-original test; the cites name the Thetis code the
// hand-worked expected values come from. No Thetis logic is copied here.
// =================================================================
// tests/tst_para_eq_curve.cpp  (NereusSDR)
// =================================================================
//
// R-R3-49 (group A fix wave): the TX EQ arrays the Core hands its TX
// channel, as Thetis hands them to WDSP's SetTXAEQProfile. Every expected
// value below was worked by hand from Thetis's code [v2.10.3.15], not from
// ParaEqCurve's:
//   - the legacy EQ: eqform.cs:2777-2816 setTXEQProfile (F[0] = 0, the ten
//     centres; G[0] = the preamp, the ten gains; Q null);
//   - the parametric panel: eqform.cs:3268-3320 ParaEQTXData's setter
//     (Decompress_gzip, ucParametricEq.cs:1392-1452 PointsFromJson with the
//     panel's -24..24 dB and 0.2..20 Q limits, eqform.cs:959-970; GetDefaults,
//     ucParametricEq.cs:1107-1131, when it fails), then eqform.cs:3041-3072
//     sendTXDspUpdate (F[0] = 0, G[0] = TX_Preamp, Q[0] = 0, every point;
//     Q only when TX_ParametricEQ).
// PointsFromJson clamps, rounds (F to 3 places, G and the preamp to 1, Q to
// 2, .NET's round half to even), keeps the saved order and locks the first
// and last points to the range's ends.
//
// Modification history (NereusSDR):
//   2026-09-25  J.J. Boyd / KG4VCF  Rewritten for the Thetis arrays in
//                                    place of the ten-point sampling it
//                                    pinned before. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-28  J.J. Boyd / KG4VCF  R-IOS-13 / R-R3-49: txEqCurveJson,
//                                    the read-only curve on the link, and
//                                    TransmitModel's txEqCurve. AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cmath>
#include <vector>

#include "core/ParaEqCurve.h"
#include "core/ParaEqEnvelope.h"
#include "models/TransmitModel.h"

#include <QSignalSpy>
#include <QVector>

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

ParaEqCurve::TxEqProfile fromSaved(const QString& json)
{
    return ParaEqCurve::txEqProfileFromPoints(
        ParaEqCurve::txEqPointsFromParaEqData(ParaEqEnvelope::encode(json)));
}

void compare(const std::vector<double>& got, const std::vector<double>& want, const char* what)
{
    QVERIFY2(got.size() == want.size(),
             qPrintable(QStringLiteral("%1: %2 values, want %3")
                            .arg(QLatin1String(what)).arg(got.size()).arg(want.size())));
    for (std::size_t i = 0; i < got.size(); ++i) {
        QVERIFY2(qFuzzyCompare(1.0 + got[i], 1.0 + want[i]),
                 qPrintable(QStringLiteral("%1[%2] %3, want %4").arg(QLatin1String(what))
                                .arg(i).arg(got[i], 0, 'g', 17).arg(want[i], 0, 'g', 17)));
    }
}

} // namespace

class TestParaEqCurve : public QObject {
    Q_OBJECT
private slots:
    void legacyProfile();
    void fivePointParametricProfile();
    void eighteenPointParametricProfile();
    void qFactorsOffSendsNoQ();
    void blankOrBrokenValueSendsThetisDefaults();
    void earlyBuildRawJsonReadsTheSame();

    // R-IOS-13 / R-R3-49: transmit.txEqCurve, the read-only curve on the
    // link (the station link document, "The TX EQ curve").
    void curveOfAnEmptyValueIsThetisDefaults();
    void curveOfASavedValueIsTheWorkedExample();
    void workedExampleResponse();
    void curveOrdersPointsAsThePanelDraws();
    void curveOfAnUnreadableValueIsUnavailable();
    void transmitModelCurveFollowsTheBlob();
};

// setTXEQProfile: preamp 3, gains {-12,-12,-12,-1,1,4,9,12,-10,-10},
// centres {32,63,...,16000}.
void TestParaEqCurve::legacyProfile()
{
    const ParaEqCurve::TxEqProfile p = ParaEqCurve::legacyTxEqProfile(
        3, {-12, -12, -12, -1, 1, 4, 9, 12, -10, -10},
        {32, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000});
    compare(p.f, {0, 32, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000}, "F");
    compare(p.g, {3, -12, -12, -12, -1, 1, 4, 9, 12, -10, -10}, "G");
    QVERIFY(p.q.empty());  // null
}

// Five points, Q factors on, range 100.0004..3000, preamp 1.26:
//   preamp: Round(1.26, 1) = 1.3
//   p0 f 50 -> first point = the range's start 100.0004 -> Round 3 = 100.0;
//      g 6.04 -> 6.0; q 1.237 -> 1.24
//   p1 f 700.1234 -> 700.123; g -4.44 -> -4.4; q 3 -> 3
//   p2 f 50 -> clamp to 100.0004 -> 100.0 (the saved order is kept);
//      g 30 -> clamp 24; q 50 -> clamp 20
//   p3 f 2200; g -30 -> -24; q 0.01 -> 0.2
//   p4 f 3500 -> last point = the range's end 3000; g 2; q 4
void TestParaEqCurve::fivePointParametricProfile()
{
    const QString json = curveJson(5, true, 1.26, 100.0004, 3000.0, {
        {50, 6.04, 1.237}, {700.1234, -4.44, 3}, {50, 30, 50},
        {2200, -30, 0.01}, {3500, 2, 4}});
    const ParaEqCurve::TxEqProfile p = fromSaved(json);
    compare(p.f, {0, 100, 700.123, 100, 2200, 3000}, "F");
    compare(p.g, {1.3, 6, -4.4, 24, -24, 2}, "G");
    compare(p.q, {0, 1.24, 3, 20, 0.2, 4}, "Q");
}

// Eighteen points, Q factors on, band_count 0 (so it is the point count,
// 18), range 0..2700, preamp -3: point i at 150*i Hz (the last locked to
// 2700), gain (i % 5) - 2, Q 1 + 0.25*i.
void TestParaEqCurve::eighteenPointParametricProfile()
{
    QList<Pt> pts;
    for (int i = 0; i < 18; ++i) {
        pts.append({150.0 * i, double(i % 5) - 2.0, 1.0 + 0.25 * i});
    }
    const ParaEqCurve::TxEqProfile p = fromSaved(curveJson(0, true, -3.0, 0.0, 2700.0, pts));
    compare(p.f, {0, 0, 150, 300, 450, 600, 750, 900, 1050, 1200, 1350, 1500, 1650,
                  1800, 1950, 2100, 2250, 2400, 2700}, "F");
    compare(p.g, {-3, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0}, "G");
    compare(p.q, {0, 1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3, 3.25, 3.5, 3.75, 4,
                  4.25, 4.5, 4.75, 5, 5.25}, "Q");
}

// Q factors off (parametric_eq false): every point's F and G, Q null.
void TestParaEqCurve::qFactorsOffSendsNoQ()
{
    const ParaEqCurve::TxEqProfile p = fromSaved(curveJson(3, false, 0.0, 0.0, 2000.0, {
        {0, -6, 4}, {1000, 3, 4}, {2000, 1, 4}}));
    compare(p.f, {0, 0, 1000, 2000}, "F");
    compare(p.g, {0, -6, 3, 1}, "G");
    QVERIFY(p.q.empty());
}

// A blank or broken value: GetDefaults (10 points, 0..4000 Hz, gain 0, Q 4,
// parametric, no preamp): F = 4000*i/9.
void TestParaEqCurve::blankOrBrokenValueSendsThetisDefaults()
{
    const std::vector<double> wantF{0, 0, 4000.0 / 9, 8000.0 / 9, 12000.0 / 9,
                                    16000.0 / 9, 20000.0 / 9, 24000.0 / 9,
                                    28000.0 / 9, 32000.0 / 9, 4000};
    const std::vector<double> wantG(11, 0.0);
    const std::vector<double> wantQ{0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
    const QString broken[] = {
        QString(),
        QStringLiteral("not a curve"),
        ParaEqEnvelope::encode(QStringLiteral("{\"points\":[{\"frequency_hz\":1}]}")),
        // band_count disagrees with the points
        ParaEqEnvelope::encode(curveJson(4, true, 0, 0, 1000, {{0, 1, 1}, {500, 1, 1}, {1000, 1, 1}})),
        // an empty range
        ParaEqEnvelope::encode(curveJson(2, true, 0, 1000, 1000, {{0, 1, 1}, {1000, 1, 1}})),
    };
    for (const QString& value : broken) {
        ParaEqCurve::TxEqPoints points;
        QVERIFY2(!ParaEqCurve::loadTxEqPoints(value, points), qPrintable(value));
        const ParaEqCurve::TxEqProfile p = ParaEqCurve::txEqProfileFromPoints(
            ParaEqCurve::txEqPointsFromParaEqData(value));
        compare(p.f, wantF, "F");
        compare(p.g, wantG, "G");
        compare(p.q, wantQ, "Q");
    }
}

// Raw JSON saved by an early NereusSDR build reads as its envelope does.
void TestParaEqCurve::earlyBuildRawJsonReadsTheSame()
{
    const QString json = curveJson(3, true, 2.0, 0.0, 2000.0, {
        {0, -6, 2}, {1000, 3, 2}, {2000, 1, 2}});
    const ParaEqCurve::TxEqProfile raw = ParaEqCurve::txEqProfileFromPoints(
        ParaEqCurve::txEqPointsFromParaEqData(json));
    compare(raw.f, {0, 0, 1000, 2000}, "F");
    compare(raw.g, {2, -6, 3, 1}, "G");
    compare(raw.q, {0, 2, 2, 2}, "Q");
}

// ── R-IOS-13 / R-R3-49: the read-only curve on the link ────────────────
// The expected strings are the wire bytes (compact JSON; QJsonObject
// writes keys in sorted order, and the link document says key order is
// not significant). The points are what Thetis's panel draws: PointsFromJson
// as above, then ucParametricEq.cs:3223-3312 enforceOrdering(true) with the
// TX panel's reorder on and 5 Hz spacing (eqform.cs:946, 966).

// An empty value: GetDefaults' flat curve (ucParametricEq.cs:1107-1131),
// ten points at (i / 9) x 4000 Hz, Q 4, which the Core applies in its place.
void TestParaEqCurve::curveOfAnEmptyValueIsThetisDefaults()
{
    QString want = QStringLiteral(
        "{\"maxHz\":4000,\"minHz\":0,\"parametric\":true,\"points\":[");
    for (int i = 0; i < 10; ++i) {
        const double f = 0.0 + (static_cast<double>(i) / 9.0) * 4000.0;
        if (i > 0) { want += QLatin1Char(','); }
        want += QStringLiteral("{\"frequencyHz\":%1,\"gainDb\":0,\"q\":4}")
                    .arg(QString::fromUtf8(QJsonDocument(QJsonArray{f}).toJson(
                        QJsonDocument::Compact)).mid(1).chopped(1));
    }
    want += QStringLiteral("],\"preampDb\":0,\"state\":\"default\"}");
    QCOMPARE(ParaEqCurve::txEqCurveJson(QString()), want);
    // Point i sits at (i / 9) x 4000 Hz, as GetDefaults works it: point 1
    // is 444.4444444444444 on the wire (the double, printed shortest).
    QVERIFY2(want.contains(QStringLiteral("\"frequencyHz\":444.4444444444444,")), qPrintable(want));
}

// The link document's worked example: a five-point curve saved by the
// panel (gzip, base64url), read back as saved.
void TestParaEqCurve::curveOfASavedValueIsTheWorkedExample()
{
    const QString blob = ParaEqEnvelope::encode(curveJson(5, true, -2.5, 50.0, 3000.0, {
        {50, -6, 1.5}, {300, 3, 2}, {1200, -1.5, 4}, {2400, 4, 3}, {3000, 0, 1}}));
    QCOMPARE(ParaEqCurve::txEqCurveJson(blob),
             QStringLiteral("{\"maxHz\":3000,\"minHz\":50,\"parametric\":true,\"points\":["
                            "{\"frequencyHz\":50,\"gainDb\":-6,\"q\":1.5},"
                            "{\"frequencyHz\":300,\"gainDb\":3,\"q\":2},"
                            "{\"frequencyHz\":1200,\"gainDb\":-1.5,\"q\":4},"
                            "{\"frequencyHz\":2400,\"gainDb\":4,\"q\":3},"
                            "{\"frequencyHz\":3000,\"gainDb\":0,\"q\":1}],"
                            "\"preampDb\":-2.5,\"state\":\"saved\"}"));
    // A value rounded as PointsFromJson rounds it (G to 0.1 dB, F to
    // 0.001 Hz, Q to 0.01) reads rounded; an out-of-range gain clamps.
    const QString rounded = ParaEqEnvelope::encode(curveJson(3, false, 30.0, 0.0, 1000.0, {
        {0, 1.26, 0.123}, {500.0004, -30, 7.777}, {1000, 0.05, 25}}));
    QCOMPARE(ParaEqCurve::txEqCurveJson(rounded),
             QStringLiteral("{\"maxHz\":1000,\"minHz\":0,\"parametric\":false,\"points\":["
                            "{\"frequencyHz\":0,\"gainDb\":1.3,\"q\":0.2},"
                            "{\"frequencyHz\":500,\"gainDb\":-24,\"q\":7.78},"
                            "{\"frequencyHz\":1000,\"gainDb\":0,\"q\":20}],"
                            "\"preampDb\":24,\"state\":\"saved\"}"));
}

// The example's drawn line, from ucParametricEq.cs:2694-2748 (the response)
// and :2358-2359 (plus the preamp): the values the link document quotes.
void TestParaEqCurve::workedExampleResponse()
{
    struct P { double frequencyHz; double gainDb; double q; };
    const QVector<P> points{{50, -6, 1.5}, {300, 3, 2}, {1200, -1.5, 4}, {2400, 4, 3}, {3000, 0, 1}};
    const auto drawn = [&](double hz) {
        return ParaEqCurve::responseDb(points, true, 50.0, 3000.0, ParaEqCurve::kTxEqQMin,
                                       ParaEqCurve::kTxEqQMax, hz) + (-2.5);
    };
    QCOMPARE(std::round(drawn(50) * 100.0) / 100.0, -7.04);
    QCOMPARE(std::round(drawn(300) * 100.0) / 100.0, -3.51);
    QCOMPARE(std::round(drawn(1200) * 100.0) / 100.0, -4.0);
    QCOMPARE(std::round(drawn(2400) * 100.0) / 100.0, 1.5);
    QCOMPARE(std::round(drawn(3000) * 100.0) / 100.0, -2.5);
}

// Out of order, and closer than 5 Hz: sorted by frequency, then spaced.
// {0, 600, 598, 1000} -> {0, 598, 603, 1000}: 603 = 598 + 5. A tie keeps
// the saved order: {0, 500 (g 1), 500 (g 2), 1000} -> the second at 505.
void TestParaEqCurve::curveOrdersPointsAsThePanelDraws()
{
    const QString unsorted = ParaEqEnvelope::encode(curveJson(4, true, 0.0, 0.0, 1000.0, {
        {0, 1, 1}, {600, 2, 2}, {598, 3, 3}, {1000, 4, 4}}));
    QCOMPARE(ParaEqCurve::txEqCurveJson(unsorted),
             QStringLiteral("{\"maxHz\":1000,\"minHz\":0,\"parametric\":true,\"points\":["
                            "{\"frequencyHz\":0,\"gainDb\":1,\"q\":1},"
                            "{\"frequencyHz\":598,\"gainDb\":3,\"q\":3},"
                            "{\"frequencyHz\":603,\"gainDb\":2,\"q\":2},"
                            "{\"frequencyHz\":1000,\"gainDb\":4,\"q\":4}],"
                            "\"preampDb\":0,\"state\":\"saved\"}"));
    const QString tie = ParaEqEnvelope::encode(curveJson(4, true, 0.0, 0.0, 1000.0, {
        {0, 0, 1}, {500, 1, 1}, {500, 2, 1}, {1000, 0, 1}}));
    QCOMPARE(ParaEqCurve::txEqCurveJson(tie),
             QStringLiteral("{\"maxHz\":1000,\"minHz\":0,\"parametric\":true,\"points\":["
                            "{\"frequencyHz\":0,\"gainDb\":0,\"q\":1},"
                            "{\"frequencyHz\":500,\"gainDb\":1,\"q\":1},"
                            "{\"frequencyHz\":505,\"gainDb\":2,\"q\":1},"
                            "{\"frequencyHz\":1000,\"gainDb\":0,\"q\":1}],"
                            "\"preampDb\":0,\"state\":\"saved\"}"));
    // The Core still sends WDSP the saved order (sendTXDspUpdate); the
    // panel's order is only what is drawn.
    const ParaEqCurve::TxEqProfile p = fromSaved(curveJson(4, true, 0.0, 0.0, 1000.0, {
        {0, 1, 1}, {600, 2, 2}, {598, 3, 3}, {1000, 4, 4}}));
    compare(p.f, {0, 0, 600, 598, 1000}, "F");
}

// A value that is not empty and holds no curve Thetis would load: the
// curve says so, and never crashes or claims the flat default.
void TestParaEqCurve::curveOfAnUnreadableValueIsUnavailable()
{
    const QString broken[] = {
        QStringLiteral("not a curve"),
        QStringLiteral("   "),
        QStringLiteral("{"),
        ParaEqEnvelope::encode(QStringLiteral("not json")),
        ParaEqEnvelope::encode(QStringLiteral("[1,2,3]")),
        ParaEqEnvelope::encode(QStringLiteral("{\"points\":[{\"frequency_hz\":1}]}")),
        ParaEqEnvelope::encode(QStringLiteral("{\"frequency_max_hz\":10,\"points\":[1,2]}")),
        ParaEqEnvelope::encode(curveJson(4, true, 0, 0, 1000, {{0, 1, 1}, {500, 1, 1}, {1000, 1, 1}})),
        ParaEqEnvelope::encode(curveJson(2, true, 0, 1000, 1000, {{0, 1, 1}, {1000, 1, 1}})),
        // A valid envelope cut short.
        ParaEqEnvelope::encode(curveJson(2, true, 0, 0, 1000, {{0, 1, 1}, {1000, 1, 1}})).left(20),
    };
    for (const QString& value : broken) {
        QCOMPARE(ParaEqCurve::txEqCurveJson(value), QStringLiteral("{\"state\":\"unavailable\"}"));
    }
}

// TransmitModel's txEqCurve follows txEqParaEqData: the flat default from
// construction, one change per new curve, none when a new value holds the
// same curve.
void TestParaEqCurve::transmitModelCurveFollowsTheBlob()
{
    TransmitModel tx;
    QCOMPARE(tx.txEqCurve(), ParaEqCurve::txEqCurveJson(QString()));
    QSignalSpy spy(&tx, &TransmitModel::txEqCurveChanged);

    const QString json = curveJson(3, true, 1.0, 0.0, 2000.0, {{0, -6, 2}, {1000, 3, 2}, {2000, 1, 2}});
    tx.setTxEqParaEqData(ParaEqEnvelope::encode(json));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(tx.txEqCurve(), ParaEqCurve::txEqCurveJson(ParaEqEnvelope::encode(json)));
    QCOMPARE(spy.at(0).at(0).toString(), tx.txEqCurve());

    // The same curve as raw JSON (an early build's value): the blob
    // changes, the curve does not.
    tx.setTxEqParaEqData(json);
    QCOMPARE(spy.count(), 1);

    tx.setTxEqParaEqData(QStringLiteral("not a curve"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(tx.txEqCurve(), QStringLiteral("{\"state\":\"unavailable\"}"));

    tx.setTxEqParaEqData(QString());
    QCOMPARE(spy.count(), 3);
    QCOMPARE(tx.txEqCurve(), ParaEqCurve::txEqCurveJson(QString()));
}

QTEST_MAIN(TestParaEqCurve)
#include "tst_para_eq_curve.moc"
