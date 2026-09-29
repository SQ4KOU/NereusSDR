// =================================================================
// tests/tst_vfo_widget_slice_access.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Slice control and shared listening plan, Task 14a: a slice flag says
// who controls the slice. A flag this window listens to (another device
// controls the slice) keeps its letter and color, says who controls it,
// disables the shared tuning controls with that reason, never writes the
// slice, and offers Take control and Stop listening. A flag this window
// controls says "You control" and offers Release.
// =================================================================
#include <QtTest/QtTest>
#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QWheelEvent>
#include "core/AppSettings.h"
#include "gui/widgets/VfoWidget.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

VfoWidget::SliceAccess listened()
{
    VfoWidget::SliceAccess access;
    access.state = VfoWidget::SliceAccess::State::Listening;
    access.line = QStringLiteral("Listening · controlled by Shack iPad");
    access.heldReason = QStringLiteral("Shack iPad controls this slice");
    return access;
}

VfoWidget::SliceAccess controlled()
{
    VfoWidget::SliceAccess access;
    access.state = VfoWidget::SliceAccess::State::Controlled;
    access.line = QStringLiteral("You control");
    return access;
}

QAction* findAction(QMenu& menu, const QString& text)
{
    for (QAction* action : menu.actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

// The same three writes MainWindow wires from a flag to its slice.
void wireLikeMainWindow(VfoWidget& flag, SliceModel& slice)
{
    QObject::connect(&flag, &VfoWidget::frequencyChanged,
                     &slice, &SliceModel::setFrequency);
    QObject::connect(&flag, &VfoWidget::afGainChanged,
                     &slice, &SliceModel::setAfGain);
    QObject::connect(&flag, &VfoWidget::muteChanged,
                     &slice, &SliceModel::setMuted);
}

void wheel(QWidget& target)
{
    const QPointF pos(target.width() / 2.0, target.height() / 2.0);
    QWheelEvent event(pos, target.mapToGlobal(pos), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&target, &event);
}

} // namespace

class TestVfoWidgetSliceAccess : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { AppSettings::instance().clear(); }
    void cleanup()      { AppSettings::instance().clear(); }

    void unshared_flag_shows_no_access_line()
    {
        VfoWidget flag;
        QCOMPARE(flag.sliceAccess().state, VfoWidget::SliceAccess::State::Unshared);
        QVERIFY(flag.accessLineText().isEmpty());
        QVERIFY(!flag.isListening());
    }

    void listened_flag_names_the_controller_and_disables_tuning()
    {
        VfoWidget flag;
        flag.setSliceIndex(1);
        flag.setSliceAccess(listened());

        QCOMPARE(flag.accessLineText(),
                 QStringLiteral("Listening · controlled by Shack iPad"));
        QVERIFY(flag.isListening());

        const QString reason = QStringLiteral("Shack iPad controls this slice");
        const QStringList held = {QStringLiteral("m_rxAntBtn")};
        for (const QString& name : held) {
            auto* control = flag.findChild<QWidget*>(name);
            QVERIFY2(control, qPrintable(name));
            QVERIFY2(!control->isEnabled(), qPrintable(name));
            QCOMPARE(control->toolTip(), reason);
        }
        const QList<QWidget*> tuning = flag.heldControlsForTest();
        QVERIFY(!tuning.isEmpty());
        for (QWidget* control : tuning) {
            QVERIFY(!control->isEnabled());
            QCOMPARE(control->toolTip(), reason);
            QCOMPARE(control->accessibleDescription(), reason);
        }
    }

    void access_change_restores_the_controls()
    {
        VfoWidget flag;
        flag.setSliceIndex(1);
        auto* ant = flag.findChild<QWidget*>(QStringLiteral("m_rxAntBtn"));
        QVERIFY(ant);
        const QString ownTip = ant->toolTip();

        flag.setSliceAccess(listened());
        QVERIFY(!ant->isEnabled());
        flag.setSliceAccess(controlled());
        QVERIFY(ant->isEnabled());
        QCOMPARE(ant->toolTip(), ownTip);
        QCOMPARE(flag.accessLineText(), QStringLiteral("You control"));
        for (QWidget* control : flag.heldControlsForTest()) {
            QVERIFY(control->isEnabled());
        }
    }

    void listened_flag_never_writes_the_slice()
    {
        SliceModel slice(1);
        slice.setFrequency(14'200'000.0);
        VfoWidget flag;
        flag.setSliceIndex(1);
        flag.setFrequency(14'200'000.0);
        wireLikeMainWindow(flag, slice);
        flag.setSliceAccess(listened());

        QSignalSpy freq(&slice, &SliceModel::frequencyChanged);
        QSignalSpy af(&slice, &SliceModel::afGainChanged);
        QSignalSpy mute(&slice, &SliceModel::mutedChanged);
        QSignalSpy flagFreq(&flag, &VfoWidget::frequencyChanged);

        wheel(flag);
        QTest::mouseDClick(&flag, Qt::LeftButton, Qt::NoModifier,
                           flag.frequencyAreaForTest().center());

        QCOMPARE(flagFreq.count(), 0);
        QCOMPARE(freq.count(), 0);
        QCOMPARE(af.count(), 0);
        QCOMPARE(mute.count(), 0);
        QVERIFY(!flag.frequencyEditOpen());
        QCOMPARE(slice.frequency(), 14'200'000.0);
    }

    void controlled_flag_still_tunes()
    {
        SliceModel slice(1);
        slice.setFrequency(14'200'000.0);
        VfoWidget flag;
        flag.setSliceIndex(1);
        flag.setFrequency(14'200'000.0);
        wireLikeMainWindow(flag, slice);
        flag.setSliceAccess(controlled());

        QSignalSpy freq(&slice, &SliceModel::frequencyChanged);
        wheel(flag);
        QCOMPARE(freq.count(), 1);
    }

    void listened_menu_offers_take_control_and_stop_listening()
    {
        VfoWidget flag;
        flag.setSliceIndex(2);
        flag.setSliceAccess(listened());

        QMenu menu;
        flag.populateContextMenu(menu);
        const QList<QAction*> actions = menu.actions();
        QVERIFY(actions.size() >= 2);
        QCOMPARE(actions.at(0)->text(), QStringLiteral("Take control"));
        QCOMPARE(actions.at(1)->text(), QStringLiteral("Stop listening"));
        QVERIFY(actions.at(0)->isEnabled());
        QVERIFY(actions.at(1)->isEnabled());
        QVERIFY(!findAction(menu, QStringLiteral("Release")));

        // Everything else on a listened flag changes the shared slice, so
        // it is shown disabled with the reason.
        for (QAction* action : actions.mid(2)) {
            if (action->isSeparator()) {
                continue;
            }
            QVERIFY2(!action->isEnabled(), qPrintable(action->text()));
            QCOMPARE(action->toolTip(), QStringLiteral("Shack iPad controls this slice"));
        }

        QSignalSpy take(&flag, &VfoWidget::takeControlRequested);
        QSignalSpy stop(&flag, &VfoWidget::stopListeningRequested);
        actions.at(0)->trigger();
        actions.at(1)->trigger();
        QCOMPARE(take.count(), 1);
        QCOMPARE(take.first().first().toInt(), 2);
        QCOMPARE(stop.count(), 1);
        QCOMPARE(stop.first().first().toInt(), 2);
    }

    void controlled_menu_offers_release()
    {
        VfoWidget flag;
        flag.setSliceIndex(3);
        flag.setSliceAccess(controlled());

        QMenu menu;
        flag.populateContextMenu(menu);
        QAction* release = menu.actions().value(0);
        QVERIFY(release);
        QCOMPARE(release->text(), QStringLiteral("Release"));
        QVERIFY(!findAction(menu, QStringLiteral("Take control")));

        QSignalSpy spy(&flag, &VfoWidget::releaseRequested);
        release->trigger();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toInt(), 3);
    }

    void unshared_menu_has_no_access_actions()
    {
        VfoWidget flag;
        QMenu menu;
        flag.populateContextMenu(menu);
        QVERIFY(!findAction(menu, QStringLiteral("Take control")));
        QVERIFY(!findAction(menu, QStringLiteral("Release")));
        QVERIFY(!findAction(menu, QStringLiteral("Stop listening")));
        QCOMPARE(menu.actions().value(0)->text(),
                 QStringLiteral("Make this the TX slice"));
    }

    void pending_request_shows_and_disables_access_actions()
    {
        VfoWidget flag;
        flag.setSliceIndex(1);
        flag.setSliceAccess(listened());
        flag.setSliceAccessPending(QStringLiteral("Asking the Core..."));
        QCOMPARE(flag.accessLineText(), QStringLiteral("Asking the Core..."));

        QMenu menu;
        flag.populateContextMenu(menu);
        QVERIFY(!menu.actions().at(0)->isEnabled());
        QVERIFY(!menu.actions().at(1)->isEnabled());

        flag.setSliceAccessPending(QString());
        QCOMPARE(flag.accessLineText(),
                 QStringLiteral("Listening · controlled by Shack iPad"));
    }

    void close_on_a_listened_flag_stops_listening()
    {
        QWidget pan;
        pan.resize(800, 400);
        auto* flag = new VfoWidget(&pan);
        flag->setSliceIndex(0);  // even Slice A: stopping listening is fine
        flag->setSliceAccess(listened());
        flag->updatePosition(400, 20);
        QPushButton* close = flag->closeButtonForTest();
        QVERIFY(close);
        QCOMPARE(close->toolTip(), QStringLiteral("Stop listening"));

        QSignalSpy stop(flag, &VfoWidget::stopListeningRequested);
        QSignalSpy closeReq(flag, &VfoWidget::closeRequested);
        close->click();
        QCOMPARE(stop.count(), 1);
        QCOMPARE(stop.first().first().toInt(), 0);
        QCOMPARE(closeReq.count(), 0);
    }

    void tx_badge_stays_red_on_a_listened_flag_on_the_air()
    {
        VfoWidget flag;
        flag.setSliceIndex(1);
        flag.setSliceAccess(listened());
        flag.setTxSlice(true);
        QVERIFY(flag.txSliceShown());

        QSignalSpy spy(&flag, &VfoWidget::txHandoffRequested);
        flag.simulateTxBadgeClick();
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestVfoWidgetSliceAccess)
#include "tst_vfo_widget_slice_access.moc"
