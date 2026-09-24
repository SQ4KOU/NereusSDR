// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_spake_exchange.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 14 (R-IOS-08, D37): the SPAKE2+EE exchange behind
// the pairing code, and its confirmation boxes.
//
// Refusals first: a wrong code fails at the device's step 3 and at the
// Core's step 4 (a device that answers step 2 without the code); a peer
// naming password hash parameters other than the fixed ones is refused
// before the code is hashed; steps out of order, repeated or malformed are
// refused; a box opens only under the other side's key and only untouched.
// Then the admit path: the same code on both sides agrees keys, and each
// side opens the other's box.
//
// Codes are made at run time and never printed.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QRandomGenerator>

#include "core/security/PairingCode.h"
#include "core/security/SpakeExchange.h"

using namespace NereusSDR;

namespace {

QString freshCode()
{
    return PairingCode::generate(7);
}

// A code other than `code`: another second word.
QString otherCode(const QString& code)
{
    const QStringList parts = code.split(QLatin1Char('-'));
    const QStringList& words = PairingCode::wordList();
    const QString replacement = words.at((words.indexOf(parts.at(2)) + 1) % words.size());
    return QStringLiteral("%1-%2-%3").arg(parts.at(0), parts.at(1), replacement);
}

QByteArray randomBytes(int size)
{
    QByteArray bytes(size, '\0');
    for (char& b : bytes) {
        b = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return bytes;
}

// The exchange up to the device's step 3 with the two codes given.
struct Run {
    SpakeExchange station{SpakeExchange::Role::Station};
    SpakeExchange device{SpakeExchange::Role::Device};
    QByteArray stored;
    QByteArray step0;
    QByteArray step1;
    QByteArray step2;
    QByteArray step3;

    Run(const QString& stationCode, const QString& deviceCode)
    {
        stored = SpakeExchange::storedData(stationCode);
        step0 = station.stationStep0(stored);
        step1 = device.deviceStep1(step0, deviceCode);
        step2 = station.stationStep2(stored, step1);
        step3 = device.deviceStep3(step2);
    }
};

} // namespace

class TstSpakeExchange : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(SpakeExchange::isAvailable());
    }

    // ── Refusals ────────────────────────────────────────────────────────

    void aWrongCodeFailsAtTheDevicesStepThree()
    {
        const QString code = freshCode();
        Run run(code, otherCode(code));
        QCOMPARE(run.step0.size(), SpakeExchange::kPublicDataBytes);
        QCOMPARE(run.step1.size(), SpakeExchange::kResponse1Bytes);
        QCOMPARE(run.step2.size(), SpakeExchange::kResponse2Bytes);
        QVERIFY(run.step3.isEmpty());
        QVERIFY(!run.device.isComplete());
        QVERIFY(run.device.sealConfirmation("x").isEmpty());
    }

    void aWrongCodeFailsAtTheCoresStepFour()
    {
        // A device without the code that answers step 2 anyway.
        const QString code = freshCode();
        Run run(code, otherCode(code));
        QVERIFY(!run.station.stationStep4(randomBytes(SpakeExchange::kResponse3Bytes)));
        QVERIFY(!run.station.isComplete());
        QVERIFY(!run.station.openConfirmation(randomBytes(64)).has_value());
        // Nor can it try again against the same step 2.
        Run right(code, code);
        QVERIFY(!right.station.stationStep4(randomBytes(SpakeExchange::kResponse3Bytes)));
        QVERIFY(!right.station.stationStep4(right.step3));
    }

    void otherHashParametersAreRefusedBeforeHashing()
    {
        const QString code = freshCode();
        const QByteArray stored = SpakeExchange::storedData(code);
        SpakeExchange station(SpakeExchange::Role::Station);
        const QByteArray step0 = station.stationStep0(stored);
        QCOMPARE(step0.size(), SpakeExchange::kPublicDataBytes);
        // Layout: version (2), algorithm (2), opslimit (8), memlimit (8),
        // salt (16), little-endian. A weaker opslimit, a smaller memlimit
        // and another algorithm are each refused.
        for (const int offset : {2, 4, 12}) {
            QByteArray weaker = step0;
            weaker[offset] = static_cast<char>(weaker.at(offset) ^ 0x01);
            SpakeExchange device(SpakeExchange::Role::Device);
            QVERIFY2(device.deviceStep1(weaker, code).isEmpty(),
                     qPrintable(QString::number(offset)));
        }
        SpakeExchange device(SpakeExchange::Role::Device);
        QVERIFY(!device.deviceStep1(step0, code).isEmpty());
    }

    void stepsOutOfOrderOrMalformedAreRefused()
    {
        const QString code = freshCode();
        const QByteArray stored = SpakeExchange::storedData(code);
        QCOMPARE(stored.size(), SpakeExchange::kStoredBytes);

        SpakeExchange station(SpakeExchange::Role::Station);
        // Step 2 before step 0.
        QVERIFY(station.stationStep2(stored, randomBytes(SpakeExchange::kResponse1Bytes)).isEmpty());
        QVERIFY(station.stationStep0(QByteArray(10, 'x')).isEmpty());
        const QByteArray step0 = station.stationStep0(stored);
        QVERIFY(!step0.isEmpty());
        QVERIFY(station.stationStep0(stored).isEmpty());   // once
        QVERIFY(station.stationStep2(stored, QByteArray(31, 'x')).isEmpty());
        // Any share of the right length spends the Core's one step 2,
        // whatever it answers.
        static_cast<void>(
            station.stationStep2(stored, randomBytes(SpakeExchange::kResponse1Bytes)));
        QVERIFY(station.stationStep2(stored, randomBytes(SpakeExchange::kResponse1Bytes)).isEmpty());
        QVERIFY(!station.stationStep4(QByteArray(3, 'x')));

        SpakeExchange device(SpakeExchange::Role::Device);
        QVERIFY(device.deviceStep3(randomBytes(SpakeExchange::kResponse2Bytes)).isEmpty());
        QVERIFY(device.deviceStep1(step0.left(20), code).isEmpty());
        QVERIFY(device.deviceStep1(step0, QString()).isEmpty());
        QVERIFY(!device.deviceStep1(step0, code).isEmpty());
        QVERIFY(device.deviceStep1(step0, code).isEmpty());   // once
        QVERIFY(device.deviceStep3(QByteArray(63, 'x')).isEmpty());

        // The wrong role for each step.
        SpakeExchange wrong(SpakeExchange::Role::Device);
        QVERIFY(wrong.stationStep0(stored).isEmpty());
        SpakeExchange wrongStation(SpakeExchange::Role::Station);
        QVERIFY(wrongStation.deviceStep1(step0, code).isEmpty());

        QVERIFY(SpakeExchange::storedData(QString()).isEmpty());
    }

    void aBoxOpensOnlyUntouchedAndUnderTheOtherSidesKey()
    {
        const QString code = freshCode();
        Run run(code, code);
        QVERIFY(run.station.stationStep4(run.step3));
        const QByteArray plain("{\"name\":\"Shack iPhone\"}");
        const QByteArray deviceBox = run.device.sealConfirmation(plain);
        QCOMPARE(deviceBox.size(),
                 SpakeExchange::kNonceBytes + plain.size() + SpakeExchange::kTagBytes);
        // Not under the sender's own key: each direction has its own.
        QVERIFY(!run.device.openConfirmation(deviceBox).has_value());
        for (const int at : {0, SpakeExchange::kNonceBytes, int(deviceBox.size()) - 1}) {
            QByteArray tampered = deviceBox;
            tampered[at] = static_cast<char>(tampered.at(at) ^ 0x40);
            QVERIFY2(!run.station.openConfirmation(tampered).has_value(),
                     qPrintable(QString::number(at)));
        }
        QVERIFY(!run.station.openConfirmation(deviceBox.left(30)).has_value());
        // A box from another exchange with the same code does not open.
        Run other(code, code);
        QVERIFY(other.station.stationStep4(other.step3));
        QVERIFY(!other.station.openConfirmation(deviceBox).has_value());
    }

    // ── The admit path ──────────────────────────────────────────────────

    void theSameCodeAgreesKeysAndBoxesOpen()
    {
        const QString code = freshCode();
        Run run(code, code);
        QCOMPARE(run.step3.size(), SpakeExchange::kResponse3Bytes);
        QVERIFY(run.device.isComplete());
        QVERIFY(run.station.stationStep4(run.step3));
        QVERIFY(run.station.isComplete());

        const QByteArray fromDevice("{\"kind\":\"phone\"}");
        const std::optional<QByteArray> opened =
            run.station.openConfirmation(run.device.sealConfirmation(fromDevice));
        QVERIFY(opened.has_value());
        QCOMPARE(*opened, fromDevice);

        const QByteArray fromStation("{\"label\":\"KG4VCF/shack\"}");
        const std::optional<QByteArray> back =
            run.device.openConfirmation(run.station.sealConfirmation(fromStation));
        QVERIFY(back.has_value());
        QCOMPARE(*back, fromStation);
        // A fresh nonce each time.
        QVERIFY(run.device.sealConfirmation(fromDevice) != run.device.sealConfirmation(fromDevice));
    }

    void storedDataHasAFreshSaltEachTime()
    {
        const QString code = freshCode();
        QVERIFY(SpakeExchange::storedData(code) != SpakeExchange::storedData(code));
        QByteArray bytes = SpakeExchange::storedData(code);
        SpakeExchange::wipe(bytes);
        QVERIFY(bytes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstSpakeExchange)
#include "tst_spake_exchange.moc"
