// no-port-check: NereusSDR-original unit-test file.
// =================================================================
// tests/tst_mic_profile_manager_para_eq_round_trip.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-3a-ii follow-up Batch 6 — round-trip tests for the new
// TXParaEQData bundle key alongside the existing CFCParaEQData blob.
//
// Closes the gap between TransmitModel.txEqParaEqData (which lands in
// the same commit) and the MicProfileManager bundle:
//   - Capture path       — TM.txEqParaEqData -> bundle key TXParaEQData
//   - Apply path         — bundle key TXParaEQData -> TM.txEqParaEqData
//   - Both blobs together — CFCParaEQData and TXParaEQData are
//                            independent; capturing/applying one must
//                            not perturb the other (regression
//                            insurance — AppSettings is a flat XML
//                            store and key-name typos can collide).
//   - Empty preservation — the empty-blob default round-trips back to
//                           empty, not to a stringified null.
//   - Bundle-key presence — defaultProfileValues() lists TXParaEQData
//                            with an empty default.
//
// Mirror-of: tst_mic_profile_manager_cfc_live_path.cpp §1 capture /
// §2 apply / §4 boolean-serialization-sanity scaffolding.
// =================================================================

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/MicProfileManager.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

static const QString kMacA = QStringLiteral("aa:bb:cc:11:22:33");

static QString profileKey(const QString& mac, const QString& name, const QString& field)
{
    return QStringLiteral("hardware/%1/tx/profile/%2/%3").arg(mac, name, field);
}

class TstMicProfileManagerParaEqRoundTrip : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        AppSettings::instance().clear();
    }

    void init()
    {
        AppSettings::instance().clear();
    }

    // =========================================================================
    // §1  Bundle-key presence + empty default
    //
    // defaultProfileValues() must contain TXParaEQData with an empty
    // string default (no Thetis database.cs row — column ships empty).
    // The total bundle size jumps from 91 to 92 keys (validated in the
    // sister test tst_mic_profile_manager.cpp).
    // =========================================================================

    void txParaEqData_isInBundleWithEmptyDefault()
    {
        const QHash<QString, QVariant> defs = MicProfileManager::defaultProfileValues();
        QVERIFY(defs.contains(QStringLiteral("TXParaEQData")));
        QCOMPARE(defs.value("TXParaEQData").toString(), QString());
    }

    // =========================================================================
    // §2  Capture round-trip — TM.txEqParaEqData -> AppSettings
    //
    // Set the model's blob to a non-empty string, save the profile
    // through the public MicProfileManager API (which calls
    // captureLiveValues internally), and assert the AppSettings key
    // contains the same string verbatim.
    // =========================================================================

    void capture_txEqParaEqData_blobBytewise()
    {
        TransmitModel tx;
        tx.loadFromSettings(kMacA);
        const QString kBlob = QStringLiteral("H4sIAAAAAAAC_6tWSkrMS4lPzi_NK1Gy");
        tx.setTxEqParaEqData(kBlob);

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        QVERIFY(mgr.saveProfile(QStringLiteral("Blob"), &tx));

        auto& s = AppSettings::instance();
        QCOMPARE(s.value(profileKey(kMacA, "Blob", "TXParaEQData")).toString(), kBlob);
    }

    // =========================================================================
    // §3  Apply round-trip — AppSettings -> TM.txEqParaEqData
    //
    // Save a profile with a known blob, mutate the model to an empty
    // baseline, then setActiveProfile() and assert the model's getter
    // returns the saved blob.
    // =========================================================================

    void apply_txEqParaEqData_blobBytewise()
    {
        TransmitModel tx;
        tx.loadFromSettings(kMacA);
        const QString kBlob = QStringLiteral(
            "H4sIAAAAAAAC_6tWSkksSVWyMtVRyklMr1ZQqgUAU3aRdRMAAAA");
        tx.setTxEqParaEqData(kBlob);

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        QVERIFY(mgr.saveProfile(QStringLiteral("BlobApply"), &tx));

        // Wipe the model state and re-apply the profile.
        tx.setTxEqParaEqData(QString());
        QCOMPARE(tx.txEqParaEqData(), QString());

        QVERIFY(mgr.setActiveProfile(QStringLiteral("BlobApply"), &tx));
        QCOMPARE(tx.txEqParaEqData(), kBlob);
    }

    // =========================================================================
    // §4  Independence — TXParaEQData and CFCParaEQData don't collide
    //
    // The two blobs have different bundle keys (TXParaEQData vs
    // CFCParaEQData) and different TM properties (txEqParaEqData vs
    // cfcParaEqData).  Setting one must not perturb the other.  Catches
    // any future hash-key typo, swapped-getter regression, or shared-
    // backing-store mistake.
    // =========================================================================

    void capture_bothBlobs_independently()
    {
        TransmitModel tx;
        tx.loadFromSettings(kMacA);
        const QString kTxBlob  = QStringLiteral("TX-blob-payload-XYZ");
        const QString kCfcBlob = QStringLiteral("CFC-blob-payload-ABC");
        tx.setTxEqParaEqData(kTxBlob);
        tx.setCfcParaEqData(kCfcBlob);

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        QVERIFY(mgr.saveProfile(QStringLiteral("Both"), &tx));

        auto& s = AppSettings::instance();
        QCOMPARE(s.value(profileKey(kMacA, "Both", "TXParaEQData")).toString(),  kTxBlob);
        QCOMPARE(s.value(profileKey(kMacA, "Both", "CFCParaEQData")).toString(), kCfcBlob);
    }

    void apply_bothBlobs_independently()
    {
        TransmitModel tx;
        tx.loadFromSettings(kMacA);
        const QString kTxBlob  = QStringLiteral("apply-tx-blob-1");
        const QString kCfcBlob = QStringLiteral("apply-cfc-blob-2");
        tx.setTxEqParaEqData(kTxBlob);
        tx.setCfcParaEqData(kCfcBlob);

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        QVERIFY(mgr.saveProfile(QStringLiteral("BothApply"), &tx));

        // Reset both, then reapply via setActiveProfile.
        tx.setTxEqParaEqData(QString());
        tx.setCfcParaEqData(QString());

        QVERIFY(mgr.setActiveProfile(QStringLiteral("BothApply"), &tx));
        QCOMPARE(tx.txEqParaEqData(), kTxBlob);
        QCOMPARE(tx.cfcParaEqData(), kCfcBlob);
    }

    // =========================================================================
    // §5  Empty-blob preservation
    //
    // The default-empty case must round-trip cleanly: setting the blob
    // to empty, saving, and reloading must yield empty (not the literal
    // string "null" or some platform-specific representation).
    // =========================================================================

    void emptyBlobsArePreserved()
    {
        TransmitModel tx;
        tx.loadFromSettings(kMacA);
        tx.setTxEqParaEqData(QString());
        tx.setCfcParaEqData(QString());

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        QVERIFY(mgr.saveProfile(QStringLiteral("Empty"), &tx));

        // Mutate to non-empty, then re-apply -> back to empty.
        tx.setTxEqParaEqData(QStringLiteral("dirty"));
        tx.setCfcParaEqData(QStringLiteral("dirty"));
        QVERIFY(mgr.setActiveProfile(QStringLiteral("Empty"), &tx));

        QCOMPARE(tx.txEqParaEqData(), QString());
        QCOMPARE(tx.cfcParaEqData(), QString());
    }
};

QTEST_MAIN(TstMicProfileManagerParaEqRoundTrip)
#include "tst_mic_profile_manager_para_eq_round_trip.moc"
