// tst_receiver_manager_reconnect.cpp — issue #75 regression.
//
// Reproduces the same-session disconnect/reconnect bug that silently killed
// audio and spectrum on ANAN-7000DLE/8000DLE (OrionMkII) P2. Without
// ReceiverManager::reset() the second createReceiver() returns index 1,
// both receivers claim DDC2, and rebuildHardwareMapping() routes DDC2 I/Q
// to logical 1 whose wdspChannel is 1 — but only WDSP channel 0 exists, so
// the data path dead-ends.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include <QMap>
#include <QMutex>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include "core/codec/CodecContext.h"
#include "core/HpsdrModel.h"

// Inspect lock availability without adding a production test interface.
#define private public
#include "core/ReceiverManager.h"
#undef private

using NereusSDR::ReceiverManager;
using NereusSDR::ReceiverConfig;

class TstReceiverManagerReconnect : public QObject
{
    Q_OBJECT

private:
    static bool routingMutexAvailable(ReceiverManager& rm)
    {
        bool acquired = false;
        std::thread probe([&rm, &acquired]() {
            acquired = rm.m_routingMutex.tryLock();
            if (acquired) {
                rm.m_routingMutex.unlock();
            }
        });
        probe.join();
        return acquired;
    }

private slots:

    void frequencyCallbackRemappingUsesCurrentHardware()
    {
        ReceiverManager rm;
        const int rx = rm.createReceiver();
        rm.setDdcMapping(rx, 2);
        rm.activateReceiver(rx);
        QSignalSpy hardware(&rm, &ReceiverManager::hardwareFrequencyChanged);
        connect(&rm, &ReceiverManager::receiverFrequencyChanged, &rm,
                [&rm, rx](int, quint64) { rm.setDdcMapping(rx, 3); }, Qt::DirectConnection);

        rm.setReceiverFrequency(rx, 7100000);

        // Both the mapping replay and the final retune target the new DDC.
        QCOMPARE(hardware.count(), 2);
        for (const QList<QVariant>& args : hardware) {
            QCOMPARE(args.at(0).toInt(), 3);
            QCOMPARE(args.at(1).toULongLong(), quint64(7100000));
        }
    }

    void frequencyCallbackCtunChangeSuppressesHardwareRetune()
    {
        ReceiverManager rm;
        const int rx = rm.createReceiver();
        rm.activateReceiver(rx);
        QSignalSpy hardware(&rm, &ReceiverManager::hardwareFrequencyChanged);
        connect(&rm, &ReceiverManager::receiverFrequencyChanged, &rm,
                [&rm](int, quint64) { rm.setDdcFrequencyLocked(true); }, Qt::DirectConnection);

        rm.setReceiverFrequency(rx, 7100000);

        QCOMPARE(hardware.count(), 0);
        QCOMPARE(rm.receiverConfig(rx).frequencyHz, quint64(7100000));
    }

    void frequencyCallbackDeactivationSuppressesHardwareRetune()
    {
        ReceiverManager rm;
        const int rx = rm.createReceiver();
        rm.activateReceiver(rx);
        QSignalSpy hardware(&rm, &ReceiverManager::hardwareFrequencyChanged);
        connect(&rm, &ReceiverManager::receiverFrequencyChanged, &rm,
                [&rm, rx](int, quint64) { rm.deactivateReceiver(rx); }, Qt::DirectConnection);

        rm.setReceiverFrequency(rx, 7100000);

        QCOMPARE(hardware.count(), 0);
        QVERIFY(!rm.isReceiverActive(rx));
    }

    void controlNotificationsReleaseRoutingMutex_data()
    {
        QTest::addColumn<QString>("operation");
        for (const char* operation : {"create", "activate", "deactivate", "map",
                                      "apply", "destroy", "reset", "frequency", "force"}) {
            QTest::newRow(operation) << QString::fromLatin1(operation);
        }
    }

    void controlNotificationsReleaseRoutingMutex()
    {
        QFETCH(QString, operation);
        ReceiverManager rm;
        const int rx = rm.createReceiver();
        rm.activateReceiver(rx);
        const int other = rm.createReceiver();
        int notifications = 0;
        bool available = true;
        QStringList order;
        const auto observe = [&](const QString& name) {
            ++notifications;
            available = routingMutexAvailable(rm) && available;
            order.append(name);
        };
        connect(&rm, &ReceiverManager::receiverCreated, &rm,
                [&]() { observe("created"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::activeReceiverCountChanged, &rm,
                [&]() { observe("activeCount"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::hardwareSlotsChanged, &rm,
                [&]() { observe("slots"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::hardwareReceiverCountChanged, &rm,
                [&]() { observe("hardwareCount"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::hardwareFrequencyChanged, &rm,
                [&]() { observe("hardwareFrequency"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::receiverActivated, &rm,
                [&]() { observe("activated"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::receiverDeactivated, &rm,
                [&]() { observe("deactivated"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::receiverDestroyed, &rm,
                [&]() { observe("destroyed"); }, Qt::DirectConnection);
        connect(&rm, &ReceiverManager::receiverFrequencyChanged, &rm,
                [&]() { observe("logicalFrequency"); }, Qt::DirectConnection);

        QStringList expected;
        if (operation == "create") {
            rm.createReceiver();
            expected = {"created"};
        } else if (operation == "activate") {
            rm.activateReceiver(other);
            expected = {"activeCount", "slots", "hardwareCount", "hardwareFrequency",
                        "hardwareFrequency", "activated"};
        } else if (operation == "deactivate") {
            rm.deactivateReceiver(rx);
            expected = {"activeCount", "slots", "hardwareCount", "deactivated"};
        } else if (operation == "map") {
            rm.setDdcMapping(rx, 2);
            expected = {"activeCount", "slots", "hardwareCount", "hardwareFrequency"};
        } else if (operation == "apply") {
            rm.applyDdcMapping({2, 3, -1, -1, -1}, 3);
            expected = {"activeCount", "slots", "hardwareCount", "hardwareFrequency",
                        "hardwareFrequency", "activated"};
        } else if (operation == "destroy") {
            rm.destroyReceiver(rx);
            expected = {"activeCount", "slots", "hardwareCount", "destroyed"};
        } else if (operation == "reset") {
            rm.reset();
            expected = {"destroyed", "destroyed", "activeCount", "slots", "hardwareCount"};
        } else if (operation == "frequency") {
            rm.setReceiverFrequency(rx, 7100000);
            expected = {"logicalFrequency", "hardwareFrequency"};
        } else if (operation == "force") {
            rm.forceHardwareFrequency(rx, 7100000);
            expected = {"hardwareFrequency"};
        }

        QVERIFY(notifications > 0);
        QVERIFY2(available, "A control callback retained the routing mutex");
        QCOMPARE(order, expected);
    }

    void mappingCallbackSeesCompleteRoutingPlan()
    {
        ReceiverManager rm;
        const int first = rm.createReceiver();
        const int second = rm.createReceiver();
        rm.applyDdcMapping({2, 3, -1, -1, -1}, 3);
        rm.setReceiverFrequency(first, 7100000);
        rm.setReceiverFrequency(second, 14200000);
        QSignalSpy routed(&rm, &ReceiverManager::iqDataForReceiver);
        QSignalSpy hardware(&rm, &ReceiverManager::hardwareFrequencyChanged);
        connect(&rm, &ReceiverManager::hardwareSlotsChanged, &rm, [&](quint32 mask) {
            QCOMPARE(mask, quint32(12));
            QCOMPARE(rm.receiverConfig(first).hardwareRx, 3);
            QCOMPARE(rm.receiverConfig(second).hardwareRx, 2);
            rm.feedIqData(2, {1.0f, 0.0f});
            rm.feedIqData(3, {0.0f, 1.0f});
        }, Qt::DirectConnection);

        rm.applyDdcMapping({3, 2, -1, -1, -1}, 3);

        QCOMPARE(routed.count(), 2);
        QCOMPARE(routed.at(0).at(0).toInt(), second);
        QCOMPARE(routed.at(1).at(0).toInt(), first);
        QCOMPARE(hardware.count(), 2);
        QCOMPARE(hardware.at(0).at(0).toInt(), 3);
        QCOMPARE(hardware.at(0).at(1).toULongLong(), quint64(7100000));
        QCOMPARE(hardware.at(1).at(0).toInt(), 2);
        QCOMPARE(hardware.at(1).at(1).toULongLong(), quint64(14200000));
    }

    void frequencyCallbackRemovalDoesNotConsumeReceiverCapacity()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(1);
        const int rx = rm.createReceiver();
        QCOMPARE(rx, 0);
        rm.activateReceiver(rx);
        QSignalSpy hardware(&rm, &ReceiverManager::hardwareFrequencyChanged);
        connect(&rm, &ReceiverManager::receiverFrequencyChanged, &rm,
                [&rm](int receiverIndex, quint64) {
                    rm.destroyReceiver(receiverIndex);
                }, Qt::DirectConnection);

        rm.setReceiverFrequency(rx, 7100000);

        // A post-callback lookup must not resurrect the removed receiver.
        QCOMPARE(rm.createReceiver(), 1);
        QCOMPARE(rm.receiverConfig(rx).receiverIndex, -1);
        QCOMPARE(hardware.count(), 0);
    }

    void frequencyCallbackRetuneDoesNotPublishSupersededFrequency()
    {
        ReceiverManager rm;
        const int rx = rm.createReceiver();
        rm.activateReceiver(rx);
        const int hardwareRx = rm.receiverConfig(rx).hardwareRx;
        QSignalSpy logical(&rm, &ReceiverManager::receiverFrequencyChanged);
        QSignalSpy hardware(&rm, &ReceiverManager::hardwareFrequencyChanged);
        connect(&rm, &ReceiverManager::receiverFrequencyChanged, &rm,
                [&rm, rx](int receiverIndex, quint64 frequencyHz) {
                    if (receiverIndex == rx && frequencyHz == 7100000) {
                        rm.setReceiverFrequency(rx, 14200000);
                    }
                }, Qt::DirectConnection);

        rm.setReceiverFrequency(rx, 7100000);

        QCOMPARE(logical.count(), 2);
        QCOMPARE(logical.at(0).at(1).toULongLong(), quint64(7100000));
        QCOMPARE(logical.at(1).at(1).toULongLong(), quint64(14200000));
        QCOMPARE(rm.receiverConfig(rx).frequencyHz, quint64(14200000));
        // The nested retune is the latest command; the outer notification
        // must not send its older frequency after the nested one completed.
        QCOMPARE(hardware.count(), 1);
        QCOMPARE(hardware.at(0).at(0).toInt(), hardwareRx);
        QCOMPARE(hardware.at(0).at(1).toULongLong(), quint64(14200000));
    }

    void firstConnectAssignsReceiver0ToDdc2()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        int rx = rm.createReceiver();
        QCOMPARE(rx, 0);

        rm.setDdcMapping(rx, 2);
        rm.activateReceiver(rx);

        const ReceiverConfig cfg = rm.receiverConfig(rx);
        QCOMPARE(cfg.hardwareRx, 2);
        QCOMPARE(cfg.wdspChannel, 0);
        QCOMPARE(rm.activeReceiverCount(), 1);
    }

    // Without reset(), a second "connect" sequence on the same manager
    // produces a receiver with index 1 and wdspChannel 1 — the exact
    // collision that breaks issue #75.
    void withoutResetSecondConnectLeaksReceiver()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        int rx1 = rm.createReceiver();
        rm.setDdcMapping(rx1, 2);
        rm.activateReceiver(rx1);

        // Simulate reconnect without reset — what the old teardown did.
        int rx2 = rm.createReceiver();
        QCOMPARE(rx2, 1);                  // leaked counter hands out 1

        rm.setDdcMapping(rx2, 2);
        rm.activateReceiver(rx2);

        // Both receivers now claim DDC2. Last writer wins in rebuildHardwareMapping
        // (QMap iterates ascending by key, so rx2 overwrites rx1's slot).
        const ReceiverConfig cfg2 = rm.receiverConfig(rx2);
        QCOMPARE(cfg2.wdspChannel, 1);     // but only WDSP channel 0 exists
        QCOMPARE(rm.activeReceiverCount(), 2);
    }

    // With reset(), the next connect sees a clean manager and gets
    // receiver 0 / wdspChannel 0 back — matches the first-connect state.
    void resetRestoresFreshConnectState()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        int rx1 = rm.createReceiver();
        rm.setDdcMapping(rx1, 2);
        rm.activateReceiver(rx1);

        QSignalSpy destroyedSpy(&rm, &ReceiverManager::receiverDestroyed);
        QSignalSpy countSpy(&rm, &ReceiverManager::activeReceiverCountChanged);

        rm.reset();

        QCOMPARE(rm.activeReceiverCount(), 0);
        QCOMPARE(destroyedSpy.count(), 1);
        QCOMPARE(destroyedSpy.takeFirst().at(0).toInt(), 0);
        QVERIFY(countSpy.count() >= 1);
        QCOMPARE(countSpy.last().at(0).toInt(), 0);

        int rx2 = rm.createReceiver();
        QCOMPARE(rx2, 0);                  // counter reset

        rm.setDdcMapping(rx2, 2);
        rm.activateReceiver(rx2);

        const ReceiverConfig cfg = rm.receiverConfig(rx2);
        QCOMPARE(cfg.hardwareRx, 2);
        QCOMPARE(cfg.wdspChannel, 0);      // matches WDSP channel 0
        QCOMPARE(rm.activeReceiverCount(), 1);
    }

    // I/Q fed through a just-reset manager must land on receiver 0, not
    // leak to a stale mapping. This is the end-to-end behavior issue #75
    // reporters actually see.
    void resetRoutesSubsequentIqToReceiver0()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        int rx1 = rm.createReceiver();
        rm.setDdcMapping(rx1, 2);
        rm.activateReceiver(rx1);

        rm.reset();

        int rx2 = rm.createReceiver();
        rm.setDdcMapping(rx2, 2);
        rm.activateReceiver(rx2);

        QSignalSpy forwarded(&rm, &ReceiverManager::iqDataForReceiver);

        QVector<float> samples{1.0f, 0.0f, 0.5f, 0.5f};
        rm.feedIqData(2, samples);

        QCOMPARE(forwarded.count(), 1);
        QCOMPARE(forwarded.first().at(0).toInt(), 0);   // logical 0, not 1
    }

    // ── Phase 3F Sub-Epic I closeout, defect C2 ───────────────────────────
    //
    // setDdcMapping had no change gate. Before Sub-Epic I it ran once per
    // connect; RadioModel::publishDdcAssignment now calls it for every
    // active stream on every requestDdcAssignment, which fires on every
    // slice frequencyChanged. Each redundant call rebuilt the hardware
    // mapping, and rebuildHardwareMapping re-emits every active receiver's
    // STORED frequency — so a spun encoder produced a stream of duplicate
    // P2 command frames, and under CTUN it retuned the DDC back to the
    // pre-drag centre, yanking the pan out from under the operator.

    void redundantDdcMappingDoesNotRetuneTheDdc()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        const int rx = rm.createReceiver();
        rm.setDdcMapping(rx, 2);
        rm.activateReceiver(rx);
        rm.setReceiverFrequency(rx, 14200000);

        // Spy AFTER setup so only the redundant republish is observed.
        QSignalSpy hwFreq(&rm, &ReceiverManager::hardwareFrequencyChanged);

        // publishDdcAssignment pushing the same mapping on a VFO tick.
        rm.setDdcMapping(rx, 2);
        rm.setDdcMapping(rx, 2);
        rm.setDdcMapping(rx, 2);

        QCOMPARE(hwFreq.count(), 0);
    }

    // A real mapping change must still rebuild and re-emit.
    void changedDdcMappingStillRebuilds()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        const int rx = rm.createReceiver();
        rm.setDdcMapping(rx, 2);
        rm.activateReceiver(rx);
        rm.setReceiverFrequency(rx, 14200000);

        QSignalSpy hwFreq(&rm, &ReceiverManager::hardwareFrequencyChanged);

        rm.setDdcMapping(rx, 3);

        QCOMPARE(rm.receiverConfig(rx).hardwareRx, 3);
        QVERIFY(hwFreq.count() >= 1);
        QCOMPARE(hwFreq.last().at(0).toInt(), 3);
        QCOMPARE(hwFreq.last().at(1).toULongLong(), quint64(14200000));
    }

    // The CTUN pan drag goes through forceHardwareFrequency, which bypasses
    // the DDC lock. It now stores the frequency too, so a rebuild triggered
    // later (a genuine mapping change, a receiver activation) re-emits the
    // dragged-to centre rather than the stale pre-drag one.
    void forcedFrequencySurvivesALaterRebuild()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        const int rx = rm.createReceiver();
        rm.setDdcMapping(rx, 2);
        rm.activateReceiver(rx);
        rm.setReceiverFrequency(rx, 14200000);

        // CTUN on: a VFO move inside the pinned window must not retune.
        rm.setDdcFrequencyLocked(true);

        // Operator drags the pan.
        rm.forceHardwareFrequency(rx, 14250000);

        QSignalSpy hwFreq(&rm, &ReceiverManager::hardwareFrequencyChanged);

        // Anything that rebuilds must re-assert the dragged-to centre.
        rm.setDdcMapping(rx, 3);

        QVERIFY(hwFreq.count() >= 1);
        QCOMPARE(hwFreq.last().at(1).toULongLong(), quint64(14250000));
    }

    // forceHardwareFrequency must not start announcing a logical retune:
    // the DDC moved, the receiver's tuning did not.
    void forcedFrequencyDoesNotEmitReceiverFrequencyChanged()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        const int rx = rm.createReceiver();
        rm.setDdcMapping(rx, 2);
        rm.activateReceiver(rx);

        QSignalSpy logical(&rm, &ReceiverManager::receiverFrequencyChanged);
        rm.forceHardwareFrequency(rx, 14250000);

        QCOMPARE(logical.count(), 0);
    }

    // P1 path is unaffected by the bug — auto hw assignment avoids
    // collision even without reset — but reset() must not break it.
    void resetPreservesAutoAssignForP1()
    {
        ReceiverManager rm;
        rm.setMaxReceivers(2);

        int rx1 = rm.createReceiver();   // no setDdcMapping — P1 path
        rm.activateReceiver(rx1);
        QCOMPARE(rm.receiverConfig(rx1).hardwareRx, 0);

        rm.reset();

        int rx2 = rm.createReceiver();
        rm.activateReceiver(rx2);
        QCOMPARE(rx2, 0);
        QCOMPARE(rm.receiverConfig(rx2).hardwareRx, 0);
    }
};

QTEST_MAIN(TstReceiverManagerReconnect)
#include "tst_receiver_manager_reconnect.moc"
