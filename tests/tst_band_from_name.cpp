// tst_band_from_name.cpp
//
// no-port-check: Test file references Thetis behavior in commentary only;
// no Thetis source is translated here.
//
// Unit tests for Band::bandFromName() — string-to-Band lookup used by the
// SpectrumOverlayPanel band flyout which emits short-name strings
// ("160", "80", "WWV") instead of the label-form strings ("160m", "80m").
// See docs/architecture/band-button-auto-mode-design.md.

#include <QtTest/QtTest>

#include "models/Band.h"

using namespace NereusSDR;

class TestBandFromName : public QObject {
    Q_OBJECT

private slots:
    void shortName_maps_to_band() {
        QCOMPARE(bandFromName(QStringLiteral("160")),  Band::Band160m);
        QCOMPARE(bandFromName(QStringLiteral("80")),   Band::Band80m);
        QCOMPARE(bandFromName(QStringLiteral("60")),   Band::Band60m);
        QCOMPARE(bandFromName(QStringLiteral("40")),   Band::Band40m);
        QCOMPARE(bandFromName(QStringLiteral("30")),   Band::Band30m);
        QCOMPARE(bandFromName(QStringLiteral("20")),   Band::Band20m);
        QCOMPARE(bandFromName(QStringLiteral("17")),   Band::Band17m);
        QCOMPARE(bandFromName(QStringLiteral("15")),   Band::Band15m);
        QCOMPARE(bandFromName(QStringLiteral("12")),   Band::Band12m);
        QCOMPARE(bandFromName(QStringLiteral("10")),   Band::Band10m);
        QCOMPARE(bandFromName(QStringLiteral("6")),    Band::Band6m);
    }

    void label_form_also_maps() {
        QCOMPARE(bandFromName(QStringLiteral("160m")), Band::Band160m);
        QCOMPARE(bandFromName(QStringLiteral("80m")),  Band::Band80m);
        QCOMPARE(bandFromName(QStringLiteral("60m")),  Band::Band60m);
        QCOMPARE(bandFromName(QStringLiteral("40m")),  Band::Band40m);
        QCOMPARE(bandFromName(QStringLiteral("30m")),  Band::Band30m);
        QCOMPARE(bandFromName(QStringLiteral("20m")),  Band::Band20m);
        QCOMPARE(bandFromName(QStringLiteral("17m")),  Band::Band17m);
        QCOMPARE(bandFromName(QStringLiteral("15m")),  Band::Band15m);
        QCOMPARE(bandFromName(QStringLiteral("12m")),  Band::Band12m);
        QCOMPARE(bandFromName(QStringLiteral("10m")),  Band::Band10m);
        QCOMPARE(bandFromName(QStringLiteral("6m")),   Band::Band6m);
    }

    void special_bands_map() {
        QCOMPARE(bandFromName(QStringLiteral("GEN")),  Band::GEN);
        QCOMPARE(bandFromName(QStringLiteral("WWV")),  Band::WWV);
        QCOMPARE(bandFromName(QStringLiteral("XVTR")), Band::XVTR);
    }

    void unknown_falls_back_to_gen() {
        QCOMPARE(bandFromName(QStringLiteral("")),         Band::GEN);
        QCOMPARE(bandFromName(QStringLiteral("bogus")),    Band::GEN);
        QCOMPARE(bandFromName(QStringLiteral("9999m")),    Band::GEN);
    }

    void case_sensitive_special_names() {
        // Matches bandKeyName() output exactly. "gen"/"wwv" lowercase is
        // NOT the canonical key and should fall back to GEN.
        QCOMPARE(bandFromName(QStringLiteral("gen")),  Band::GEN);
        QCOMPARE(bandFromName(QStringLiteral("wwv")),  Band::GEN);
        // HF label form is case-sensitive too — "20M" uppercase does
        // NOT match the canonical "20m" and must fall back.
        QCOMPARE(bandFromName(QStringLiteral("20M")),  Band::GEN);
        QCOMPARE(bandFromName(QStringLiteral("160M")), Band::GEN);
        // Mixed-case special names must NOT match.
        QCOMPARE(bandFromName(QStringLiteral("Xvtr")), Band::GEN);
        QCOMPARE(bandFromName(QStringLiteral("Wwv")),  Band::GEN);
    }

    void edge_cases_fall_back_to_gen() {
        // Whitespace-padded input is not trimmed — callers must pass
        // clean strings. Pinning current behavior so a future "helpful"
        // .trimmed() call doesn't silently change semantics.
        QCOMPARE(bandFromName(QStringLiteral(" 20m")),    Band::GEN);
        QCOMPARE(bandFromName(QStringLiteral("20m ")),    Band::GEN);
        // Frequency-looking strings are not bands.
        QCOMPARE(bandFromName(QStringLiteral("14155000")), Band::GEN);
        // Null / default-constructed QString.
        QCOMPARE(bandFromName(QString()),                  Band::GEN);
    }
};

QTEST_MAIN(TestBandFromName)
#include "tst_band_from_name.moc"
