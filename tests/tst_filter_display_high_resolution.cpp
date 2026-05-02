// NereusSDR-original infrastructure — no Thetis source ported here.
// No upstream attribution required (NereusSDR FilterDisplayItem high-res test).
//
// Design note (Task 4.4):
//   FilterDisplayItem::setHighResolution(bool) / highResolution() control whether
//   paintFilterEdges() renders the actual FIR magnitude curve via
//   RxChannel::filterResponseMagnitudes() (high-res ON) or the simplified
//   box-edge passband markers only (high-res OFF).
//
//   bindRxChannel(RxChannel*) sets the non-owning channel pointer; the item
//   is safe to call even with a nullptr channel (the high-res path skips
//   paintHighResolutionFilterCurve() gracefully).

#include <QtTest/QtTest>
#include "core/RxChannel.h"
#include "gui/meters/FilterDisplayItem.h"

using namespace NereusSDR;

class TestFilterDisplayHighResolution : public QObject {
    Q_OBJECT

private slots:
    // setHighResolution / highResolution round-trip
    void high_resolution_round_trips()
    {
        FilterDisplayItem item;
        QCOMPARE(item.highResolution(), false);  // default off

        item.setHighResolution(true);
        QCOMPARE(item.highResolution(), true);

        item.setHighResolution(false);
        QCOMPARE(item.highResolution(), false);
    }

    // setHighResolution is idempotent (no double-update on same value)
    void high_resolution_idempotent()
    {
        FilterDisplayItem item;
        item.setHighResolution(false);
        QCOMPARE(item.highResolution(), false);  // still off, no change

        item.setHighResolution(true);
        QCOMPARE(item.highResolution(), true);

        item.setHighResolution(true);   // same value — should not crash
        QCOMPARE(item.highResolution(), true);
    }

    // bindRxChannel with nullptr must not crash even when high-res is on
    void bind_nullptr_channel_does_not_crash()
    {
        FilterDisplayItem item;
        item.bindRxChannel(nullptr);
        item.setHighResolution(true);
        QCOMPARE(item.highResolution(), true);  // mode unaffected by bind
    }

    // bind-and-clear lifecycle: bindRxChannel(nullptr) clears cleanly
    void bind_and_clear_channel()
    {
        FilterDisplayItem item;
        item.setHighResolution(true);

        // Bind a temporary channel, then clear via nullptr
        {
            RxChannel ch(98, 64, 48000);
            item.bindRxChannel(&ch);
        }
        // Channel has been destroyed — clear the pointer
        item.bindRxChannel(nullptr);
        QCOMPARE(item.highResolution(), true);  // mode unaffected by bind/clear
    }
};

QTEST_MAIN(TestFilterDisplayHighResolution)
#include "tst_filter_display_high_resolution.moc"
