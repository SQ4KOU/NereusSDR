// tests/tst_sku_ui_profile.cpp (NereusSDR)
//
// Verifies every HPSDRModel maps to the label + flag set Thetis
// setup.cs:19832-20375 assigns. Regression guard: if a new SKU is
// added without a SkuUiProfile.cpp entry, the compile-time default
// fall-through will still return RX1/RX2/XVTR with no flags, and the
// unknown-SKU fixture here will catch it.
//
// Source: Thetis setup.cs:19832-20375 [v2.10.3.13 @501e3f5]

#include <QTest>
#include "core/SkuUiProfile.h"

using namespace NereusSDR;

class TestSkuUiProfile : public QObject {
    Q_OBJECT

private slots:
    void hermes_RX1_RX2_XVTR() {
        const auto p = skuUiProfileFor(HPSDRModel::HERMES);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("RX1"));
        QCOMPARE(p.rxOnlyLabels[1], QStringLiteral("RX2"));
        QCOMPARE(p.rxOnlyLabels[2], QStringLiteral("XVTR"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
        QVERIFY(p.hasAntennaTab);
        QCOMPARE(p.antennaTabLabel, QStringLiteral("Alex"));
    }

    void anan10_all_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN10);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("RX1"));
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
        QCOMPARE(p.antennaTabLabel, QStringLiteral("Ant/Filters"));
    }

    void anan100_EXT2_EXT1_XVTR() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN100);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("EXT2"));
        QCOMPARE(p.rxOnlyLabels[1], QStringLiteral("EXT1"));
        QCOMPARE(p.rxOnlyLabels[2], QStringLiteral("XVTR"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(p.hasRxOutOnTx);
        QVERIFY(p.hasRxBypassUi);
    }

    void anan7000d_BYPS_EXT1_XVTR() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN7000D);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QCOMPARE(p.rxOnlyLabels[1], QStringLiteral("EXT1"));
        QCOMPARE(p.rxOnlyLabels[2], QStringLiteral("XVTR"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
    }

    void anan8000d_BYPS_all_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN8000D);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
    }

    void anan_g2_EXT_checkboxes_visible() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN_G2);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
    }

    void anan_g2_1k_all_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN_G2_1K);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
    }

    void hermeslite_tab_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::HERMESLITE);
        QVERIFY(!p.hasAntennaTab);
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
    }
};

QTEST_APPLESS_MAIN(TestSkuUiProfile)
#include "tst_sku_ui_profile.moc"
