// =================================================================
// tests/tst_connection_panel_state_pills.cpp  (NereusSDR)
// =================================================================
// NereusSDR-original — no Thetis source ported here; no attribution
// markers required.
// =================================================================

#include <QObject>
#include <QtTest>
#include "gui/ConnectionPanel.h"

using namespace NereusSDR;

class TstConnectionPanelStatePills : public QObject {
    Q_OBJECT
private slots:
    void onlineUnder60s() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now, now),         ConnectionPanel::StatePill::Online);
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 30000, now), ConnectionPanel::StatePill::Online);
    }

    void staleBetween60sAnd5min() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 90000,         now), ConnectionPanel::StatePill::Stale);
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 4 * 60 * 1000, now), ConnectionPanel::StatePill::Stale);
    }

    void offlineOver5min() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 6 * 60 * 1000,          now), ConnectionPanel::StatePill::Offline);
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 24 * 60 * 60 * 1000LL, now), ConnectionPanel::StatePill::Offline);
    }

    void offlineWhenNeverSeen() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(0, now), ConnectionPanel::StatePill::Offline);
    }
};

QTEST_MAIN(TstConnectionPanelStatePills)
#include "tst_connection_panel_state_pills.moc"
