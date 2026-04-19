#include <QtTest/QtTest>
#include "models/TransmitModel.h"
#include "core/AppSettings.h"

using namespace NereusSDR;

class TstTransmitModelTxOwner : public QObject {
    Q_OBJECT
private slots:
    void defaultsToMicDirect() {
        TransmitModel t;
        QCOMPARE(t.txOwnerSlot(), VaxSlot::MicDirect);
    }

    void setAndGet() {
        TransmitModel t;
        t.setTxOwnerSlot(VaxSlot::Vax2);
        QCOMPARE(t.txOwnerSlot(), VaxSlot::Vax2);
    }

    void emitsSignalOnChange() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::txOwnerSlotChanged);
        t.setTxOwnerSlot(VaxSlot::Vax3);
        QCOMPARE(spy.count(), 1);
    }

    void noSignalOnSameValue() {
        TransmitModel t;
        t.setTxOwnerSlot(VaxSlot::Vax1);
        QSignalSpy spy(&t, &TransmitModel::txOwnerSlotChanged);
        t.setTxOwnerSlot(VaxSlot::Vax1);
        QCOMPARE(spy.count(), 0);
    }

    void persistsToSettings() {
        AppSettings::instance().clear();
        TransmitModel t;
        t.setTxOwnerSlot(VaxSlot::Vax4);
        QCOMPARE(AppSettings::instance().value("tx/OwnerSlot").toString(), "Vax4");
    }

    void restoresFromSettings() {
        AppSettings::instance().clear();
        AppSettings::instance().setValue("tx/OwnerSlot", "Vax1");
        TransmitModel t;
        t.loadFromSettings();
        QCOMPARE(t.txOwnerSlot(), VaxSlot::Vax1);
    }

    void unknownStringFallsBackToMicDirect() {
        AppSettings::instance().clear();
        AppSettings::instance().setValue("tx/OwnerSlot", "Bogus");
        TransmitModel t;
        t.loadFromSettings();
        QCOMPARE(t.txOwnerSlot(), VaxSlot::MicDirect);
    }

    void vaxSlotRoundTrip() {
        for (const auto slot : {VaxSlot::None, VaxSlot::MicDirect,
                                VaxSlot::Vax1, VaxSlot::Vax2,
                                VaxSlot::Vax3, VaxSlot::Vax4}) {
            QCOMPARE(vaxSlotFromString(vaxSlotToString(slot)), slot);
        }
    }
};

QTEST_APPLESS_MAIN(TstTransmitModelTxOwner)
#include "tst_transmit_model_tx_owner.moc"
