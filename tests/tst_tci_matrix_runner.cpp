// tests/tst_tci_matrix_runner.cpp  (NereusSDR)
// NereusSDR-original — no Thetis upstream port.
//
// TCI verification matrix runner.
//
// Loads tests/data/tci/matrix.csv — one row per Thetis behavior — and
// feeds each command column into TciProtocol::handleCommand(). Asserts
// that the synchronous response and the drained notification queue match
// the expected_response and expected_notifications columns.
//
// CSV format (set by Phase 1 Task 1.1):
//   command,input,expected_response,expected_notifications,thetis_cite,notes
//
// SETUP: prefix convention:
//   The "input" cell may contain one or more SETUP: directives followed
//   by the actual command, all separated by ";;". Example:
//     SETUP:TciVfoAHz=14200000;;vfo:0:0:14200000;
//   Each SETUP:Key=Value segment is applied to AppSettings before the
//   command is dispatched. The last ";;"-separated segment is the actual
//   command. This lets matrix rows set pre-conditions without needing a
//   separate setup slot or persistent fixture state.
//
// Actual data rows are added per phase (Phase 5 onward). Until then the
// CSV is header-only and this test passes vacuously.

#include <QtTest/QtTest>
#include <QFile>
#include <QTextStream>

#include "core/TciProtocol.h"
#include "core/AppSettings.h"
#include "TestMockRadioModel.h"

using namespace NereusSDR;

class TestTciMatrixRunner : public QObject {
    Q_OBJECT

    struct Row {
        QString command;
        QString input;
        QString expectedResponse;
        QString expectedNotifs;
        QString cite;
        QString notes;
    };

private:
    QList<Row> loadMatrix()
    {
        QFile f(QStringLiteral(NEREUS_TEST_DATA_DIR "/tci/matrix.csv"));
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        QTextStream ts(&f);
        ts.readLine();   // skip header
        QList<Row> rows;
        while (!ts.atEnd()) {
            const QStringList parts = ts.readLine().split(QLatin1Char(','));
            if (parts.size() < 6) {
                continue;
            }
            rows.append({parts[0], parts[1], parts[2], parts[3], parts[4], parts[5]});
        }
        return rows;
    }

private slots:
    void allRowsPass()
    {
        TestMockRadioModel mock;
        TciProtocol p(&mock);
        for (const auto& r : loadMatrix()) {
            mock.resetToBaseline();

            // SETUP: prefix handling for AppSettings preconditions.
            // Multiple SETUP: directives plus the actual command are separated
            // by ";;" within the input cell. Apply each SETUP: key/value pair
            // to AppSettings, then dispatch the final segment as the command.
            QStringList parts = r.input.split(QStringLiteral(";;"));
            for (int i = 0; i < parts.size() - 1; ++i) {
                if (parts[i].startsWith(QStringLiteral("SETUP:"))) {
                    const QString kv = parts[i].mid(6);
                    const int eq = kv.indexOf(QLatin1Char('='));
                    AppSettings::instance().setValue(kv.left(eq), kv.mid(eq + 1));
                }
            }

            const QString actualResponse = p.handleCommand(parts.last());
            QStringList notifs;
            while (p.hasPendingNotification()) {
                notifs.append(p.takePendingNotification());
            }
            QCOMPARE(actualResponse, r.expectedResponse);
            QCOMPARE(notifs.join(QStringLiteral("|")), r.expectedNotifs);
        }
    }
};

QTEST_GUILESS_MAIN(TestTciMatrixRunner)
#include "tst_tci_matrix_runner.moc"
