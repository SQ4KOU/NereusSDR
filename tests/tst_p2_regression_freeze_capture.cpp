// no-port-check: temporary capture helper — no Thetis port; deleted in Task 7.
//
// Phase 3P-B Task 1: capture pre-refactor P2RadioConnection compose output
// for every (board × MOX × rxCount × rxFreq) tuple to JSON.
//
// Run once on the pre-refactor codebase, commit the JSON, then this file is
// deleted in Phase 3P-B Task 7 once the regression-freeze gate test ships.
//
// The shipping regression test (tst_p2_regression_freeze.cpp) loads the JSON
// and asserts the new codec output matches byte-for-byte for every captured
// region (except Saturn-with-BPF1-edges, where divergence is the new feature).

#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/P2RadioConnection.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class CaptureP2Baseline : public QObject {
    Q_OBJECT

private:
    // Capture selected byte regions from a packet buffer.
    // We capture only the regions Phase B touches rather than full 1444-byte buffers,
    // to keep the JSON manageable.
    QJsonArray captureRegions(const quint8* buf, std::initializer_list<std::pair<int,int>> ranges)
    {
        QJsonArray regions;
        for (auto [start, end] : ranges) {
            QJsonObject r;
            r["start"] = start;
            r["end"]   = end;
            QJsonArray bytes;
            for (int i = start; i < end; ++i) {
                bytes.append(int(buf[i]));
            }
            r["bytes"] = bytes;
            regions.append(r);
        }
        return regions;
    }

private slots:
    void emitBaseline()
    {
        // P2 boards in scope — only those that use Protocol 2 wire format.
        const QList<HPSDRHW> boards = {
            HPSDRHW::OrionMKII,
            HPSDRHW::Saturn,
            HPSDRHW::SaturnMKII,
        };

        // Coverage matrix: board × MOX × rxCount × rxFreq.
        // rxFreq sample: 1 MHz (160m — HPF bypass path), 14.1 MHz (20m — 13 MHz HPF + 30/20m LPF),
        //                50 MHz (6m — 6m bypass path).
        const QList<bool>     moxStates = { false, true };
        const QList<int>      rxCounts  = { 1, 2 };
        const QList<quint64>  rxFreqs   = { 1'000'000ULL, 14'100'000ULL, 50'000'000ULL };

        QJsonArray rows;
        for (HPSDRHW board : boards) {
            for (bool mox : moxStates) {
                for (int rxCount : rxCounts) {
                    for (quint64 freq : rxFreqs) {
                        P2RadioConnection conn(nullptr);
                        conn.setBoardForTest(board);
                        conn.setActiveReceiverCount(rxCount);
                        conn.setReceiverFrequency(0, freq);
                        conn.setMox(mox);

                        // Snapshot each packet into zeroed buffers.
                        quint8 cmdGeneral[60]           = {};
                        quint8 cmdHighPri[1444]         = {};
                        quint8 cmdRx[1444]              = {};
                        quint8 cmdTx[60]                = {};
                        conn.composeCmdGeneralForTest(cmdGeneral);
                        conn.composeCmdHighPriorityForTest(cmdHighPri);
                        conn.composeCmdRxForTest(cmdRx);
                        conn.composeCmdTxForTest(cmdTx);

                        QJsonObject row;
                        row["board"]    = static_cast<int>(board);
                        row["mox"]      = mox;
                        row["rxCount"]  = rxCount;
                        row["rxFreqHz"] = qint64(freq);

                        // CmdGeneral (60 bytes):
                        //   bytes 0-7   — sequence (0) + command + port config
                        //   bytes 37-38 — freq-or-phase-word flag + wdt
                        //   bytes 58-60 — PA enable + Alex enable
                        row["cmdGeneral"] = captureRegions(cmdGeneral, {
                            {0, 8}, {37, 39}, {58, 60}
                        });

                        // CmdHighPriority (1444 bytes):
                        //   bytes 0-8    — seq (0) + PTT/run + CW dash/dot/cwx + RX0 freq start
                        //   bytes 1403-1404 — Mercury/preamp byte
                        //   bytes 1428-1444 — Alex1 (1428-1431) + Alex0 (1432-1435) + gap + step-ATT
                        row["cmdHighPriority"] = captureRegions(cmdHighPri, {
                            {0, 8}, {1403, 1404}, {1428, 1444}
                        });

                        // CmdRx (1444 bytes):
                        //   bytes 0-8 — seq (0) + numAdc + dither + random + enable bitmask
                        row["cmdRx"] = captureRegions(cmdRx, {
                            {0, 8}
                        });

                        // CmdTx (60 bytes):
                        //   bytes 0-8  — seq (0) + numDac + CW mode
                        //   bytes 57-60 — TX step-ATT per ADC
                        row["cmdTx"] = captureRegions(cmdTx, {
                            {0, 8}, {57, 60}
                        });

                        rows.append(row);
                    }
                }
            }
        }

        const QString path = QStringLiteral(NEREUS_TEST_DATA_DIR) + "/p2_baseline_bytes.json";
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly),
                 qPrintable(QString("Cannot open output file: %1").arg(path)));
        f.write(QJsonDocument(rows).toJson(QJsonDocument::Indented));
        qInfo() << "Wrote P2 baseline:" << path << rows.size() << "rows";
    }
};

QTEST_APPLESS_MAIN(CaptureP2Baseline)
#include "tst_p2_regression_freeze_capture.moc"
