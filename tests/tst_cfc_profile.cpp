// no-port-check: tests the existing Thetis CFC JSON and gzip contract.
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/CfcProfile.h"
#include "core/ParaEqEnvelope.h"

using namespace NereusSDR;

namespace {
QString curve(int count, bool useQ, double global, double gain)
{
    QJsonArray points;
    for (int i = 0; i < count; ++i) {
        points.append(QJsonObject{{QStringLiteral("frequency_hz"), i * 100.0},
                                  {QStringLiteral("gain_db"), gain},
                                  {QStringLiteral("q"), 2.0 + i * 0.1}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("band_count"), count},
        {QStringLiteral("parametric_eq"), useQ},
        {QStringLiteral("global_gain_db"), global},
        {QStringLiteral("frequency_min_hz"), 0.0},
        {QStringLiteral("frequency_max_hz"), (count - 1) * 100.0},
        {QStringLiteral("points"), points}}).toJson(QJsonDocument::Compact));
}
QString blob(int count, bool compQ = true, bool eqQ = true)
{
    return ParaEqEnvelope::encode(curve(count, compQ, 6.0, 5.0)
                                  + QStringLiteral("<SEP>")
                                  + curve(count, eqQ, -7.0, -3.0));
}
}

class TstCfcProfile : public QObject {
    Q_OBJECT
private slots:
    void importedPairedThetisShape_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("five") << 5;
        QTest::newRow("ten") << 10;
        QTest::newRow("eighteen") << 18;
    }
    void importedPairedThetisShape()
    {
        QFETCH(int, count);
        CfcProfile::Profile p;
        QVERIFY(CfcProfile::decode(blob(count), p));
        QCOMPARE(static_cast<int>(p.f.size()), count);
        QCOMPARE(p.f.back(), (count - 1) * 100.0);
        QCOMPARE(p.g.at(2), 5.0);
        QCOMPARE(p.e.at(2), -3.0);
        QCOMPARE(p.qg.at(2), 2.2);
        QCOMPARE(p.qe.at(2), 2.2);
        QCOMPARE(p.precompDb, 6.0);
        QCOMPARE(p.postEqGainDb, -7.0);
        CfcProfile::Profile again;
        QVERIFY(CfcProfile::decode(CfcProfile::encode(p), again));
        QCOMPARE(again.f, p.f);
        QCOMPARE(again.qe, p.qe);
    }
    void eitherGraphicFlagDisablesBothQVectors()
    {
        CfcProfile::Profile p;
        QVERIFY(CfcProfile::decode(blob(5, true, false), p));
        QVERIFY(!p.usesQ());
        QCOMPARE(static_cast<int>(p.qg.size()), 5);
        QCOMPARE(static_cast<int>(p.qe.size()), 5);
    }
    void independentWidgetResetAxesRoundTrip()
    {
        CfcProfile::Profile p;
        QVERIFY(CfcProfile::decode(blob(10), p));
        const auto originalPostF = p.postF;
        const auto originalE = p.e;
        const auto originalQe = p.qe;
        for (std::size_t i = 1; i + 1 < p.f.size(); ++i) {
            p.f[i] += 1.0;
            p.g[i] = 0.0;
            p.qg[i] = 4.0;
        }
        CfcProfile::Profile restored;
        QVERIFY(CfcProfile::decode(CfcProfile::encode(p), restored));
        QCOMPARE(restored.f, p.f);
        QCOMPARE(restored.postF, originalPostF);
        QCOMPARE(restored.e, originalE);
        QCOMPARE(restored.qe, originalQe);
        // Resetting post-EQ later must not rewrite the compression axis.
        const auto compF = restored.f;
        restored.postF[4] += 2.0;
        restored.e[4] = 0.0;
        CfcProfile::Profile again;
        QVERIFY(CfcProfile::decode(CfcProfile::encode(restored), again));
        QCOMPARE(again.f, compF);
        QCOMPARE(again.postF, restored.postF);
    }
    void rejectsMismatchedAndOversizeWithoutMutation()
    {
        CfcProfile::Profile p;
        QVERIFY(CfcProfile::decode(blob(5), p));
        const auto oldF = p.f;
        QVERIFY(!CfcProfile::decode(ParaEqEnvelope::encode(curve(5, true, 6, 5)
            + QStringLiteral("<SEP>") + curve(10, true, -7, -3)), p));
        QCOMPARE(p.f, oldF);
        QVERIFY(!CfcProfile::decode(ParaEqEnvelope::encode(QString(100000, QLatin1Char('x'))), p));
        QCOMPARE(p.f, oldF);
    }
    void rejectsSingleTxEqJson()
    {
        CfcProfile::Profile p;
        QVERIFY(!CfcProfile::decode(ParaEqEnvelope::encode(curve(10, true, 0, 0)), p));
    }
};

QTEST_GUILESS_MAIN(TstCfcProfile)
#include "tst_cfc_profile.moc"
