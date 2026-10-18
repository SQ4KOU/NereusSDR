// =================================================================
// tests/tst_vfo_flag_fold.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Workspace visual review Task 26: the receiver flag folds to its letter
// and TX badges (the fold ported from AetherSDR's collapsed flag). A click
// on the letter folds it, a click on the folded flag opens it, the wheel
// tunes the folded flag by the step, a locked receiver reads LOCKED, the
// accessible name says it is folded, and each receiver keeps its own choice
// in the isolated test settings. JJ's ruling (2026-10-08): the flag stays on
// top of the pan's corner badges (CH, TX, WIDE) wherever they meet.
// =================================================================
#include <QtTest/QtTest>
#include <QAccessible>
#include <QDir>
#include <QSignalSpy>
#include <QWheelEvent>

#include "core/AppSettings.h"
#include "core/settings/SettingsScope.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumWidget.h"
#include "gui/widgets/SpectrumStatusOverlay.h"
#include "gui/widgets/VfoWidget.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr int kReceivers = 4;

// The flag's letter badge: the 18 x 18 direct child label showing the
// receiver letter.
QLabel* letterBadge(VfoWidget& flag, const QString& letter)
{
    for (QLabel* label : flag.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (label->text() == letter && label->size() == QSize(18, 18)) {
            return label;
        }
    }
    return nullptr;
}

struct Host {
    QWidget spectrum;
    VfoWidget* flag{nullptr};
    explicit Host(int sliceIndex)
    {
        spectrum.resize(900, 400);
        flag = new VfoWidget(&spectrum);
        flag->setSliceIndex(sliceIndex);
        spectrum.show();
        flag->show();
        flag->updatePosition(400, 0);
        QCoreApplication::processEvents();
    }
};

void removeFoldKeys()
{
    for (int i = 0; i < kReceivers; ++i) {
        AppSettings::instance().remove(VfoWidget::collapsedSettingsKey(i));
    }
}

} // namespace

class TestVfoFlagFold : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // The settings file is the isolated test profile, never the
        // operator's own NereusSDR settings.
        const QString path = AppSettings::instance().filePath();
        const QString home = QDir::homePath();
        QVERIFY2(!path.startsWith(home + QStringLiteral("/.config/NereusSDR/")), qPrintable(path));
        QVERIFY2(!path.startsWith(home + QStringLiteral("/Library/Preferences/NereusSDR/")),
                 qPrintable(path));
        QVERIFY2(path.contains(QStringLiteral("qttest"), Qt::CaseInsensitive), qPrintable(path));
        removeFoldKeys();
    }
    void init() { removeFoldKeys(); }
    void cleanup() { removeFoldKeys(); }

    void letter_click_folds_after_deferred_call()
    {
        Host h(1);
        QLabel* badge = letterBadge(*h.flag, QStringLiteral("B"));
        QVERIFY(badge);
        QVERIFY(!h.flag->isCollapsed());
        QSignalSpy folded(h.flag, &VfoWidget::collapsedChanged);

        QTest::mouseClick(h.flag, Qt::LeftButton, {}, badge->geometry().center());
        // Deferred, as upstream: the click itself does not fold.
        QVERIFY(!h.flag->isCollapsed());
        QTRY_VERIFY(h.flag->isCollapsed());
        QCOMPARE(folded.count(), 1);
        QCOMPARE(folded.first().first().toBool(), true);
        QCOMPARE(h.flag->width(), 34);
        QCOMPARE(h.flag->height(), 44);
        // The folded flag shows the letter badge in the receiver's colour
        // and the TX badge under it.
        const QImage shot = h.flag->grab().toImage();
        const QColor letter = shot.pixelColor(10, 6);
        QVERIFY2(letter == VfoWidget::sliceColor(1),
                 qPrintable(QStringLiteral("letter badge pixel %1, slice colour %2")
                     .arg(letter.name(), VfoWidget::sliceColor(1).name())));
        const QRect tx = h.flag->collapsedTxBadgeRect();
        QCOMPARE(shot.pixelColor(tx.left() + 3, tx.center().y()), QColor(0x1a, 0x2a, 0x3a));
        // Side buttons hide while folded.
        QVERIFY(h.flag->lockButtonForTest());
        QVERIFY(h.flag->lockButtonForTest()->isHidden());
        QVERIFY(h.flag->closeButtonForTest()->isHidden());
    }

    void folded_click_opens_and_tx_area_does_not()
    {
        Host h(1);
        const QSize openSize = h.flag->size();
        const bool lockShownOpen = !h.flag->lockButtonForTest()->isHidden();
        const bool closeShownOpen = !h.flag->closeButtonForTest()->isHidden();
        h.flag->setCollapsed(true);
        QVERIFY(h.flag->isCollapsed());

        // A click on the painted TX badge does what the TX badge does and
        // keeps the flag folded.
        QSignalSpy handoff(h.flag, &VfoWidget::txHandoffRequested);
        const QRect tx = h.flag->collapsedTxBadgeRect();
        QVERIFY(!tx.isEmpty());
        QTest::mouseClick(h.flag, Qt::LeftButton, {}, tx.center());
        QCoreApplication::processEvents();
        QCOMPARE(handoff.count(), 1);
        QVERIFY(h.flag->isCollapsed());

        // Anywhere else opens it, deferred.
        QTest::mouseClick(h.flag, Qt::LeftButton, {}, QPoint(17, 10));
        QVERIFY(h.flag->isCollapsed());
        QTRY_VERIFY(!h.flag->isCollapsed());
        QCOMPARE(h.flag->width(), openSize.width());
        QCOMPARE(h.flag->height(), openSize.height());
        QCOMPARE(!h.flag->lockButtonForTest()->isHidden(), lockShownOpen);
        QCOMPARE(!h.flag->closeButtonForTest()->isHidden(), closeShownOpen);
        QVERIFY(letterBadge(*h.flag, QStringLiteral("B"))->isVisible());
    }

    void folded_state_round_trips_per_receiver()
    {
        const QString keyB = VfoWidget::collapsedSettingsKey(1);
        QCOMPARE(keyB, QStringLiteral("ReceiverFlag_1_Collapsed"));
        // A client-side preference: never sent to the Core.
        for (int i = 0; i < kReceivers; ++i) {
            QCOMPARE(classifySettingsKey(VfoWidget::collapsedSettingsKey(i)),
                     SettingsScope::OperatorLocal);
        }

        SliceModel sliceB(1);
        {
            Host h(1);
            h.flag->setSlice(&sliceB);
            h.flag->setCollapsed(true);
        }
        QCOMPARE(AppSettings::instance().value(keyB).toString(), QStringLiteral("True"));
        for (int i : {0, 2, 3}) {
            QVERIFY(AppSettings::instance().value(VfoWidget::collapsedSettingsKey(i)).isNull());
        }

        // Across a restart: saved to the file and read back.
        AppSettings::instance().save();
        AppSettings::instance().load();
        QCOMPARE(AppSettings::instance().value(keyB).toString(), QStringLiteral("True"));

        for (int i = 0; i < kReceivers; ++i) {
            SliceModel slice(i);
            Host h(i);
            h.flag->setSlice(&slice);
            QCOMPARE(h.flag->isCollapsed(), i == 1);
        }

        {
            Host h(1);
            h.flag->setSlice(&sliceB);
            QVERIFY(h.flag->isCollapsed());
            h.flag->setCollapsed(false);
        }
        QCOMPARE(AppSettings::instance().value(keyB).toString(), QStringLiteral("False"));
        Host again(1);
        again.flag->setSlice(&sliceB);
        QVERIFY(!again.flag->isCollapsed());
    }

    void wheel_on_folded_flag_tunes_one_step()
    {
        SliceModel slice(0);
        slice.setFrequency(14225000.0);
        Host h(0);
        h.flag->setSlice(&slice);
        h.flag->setFrequency(slice.frequency());
        h.flag->setStepHz(100);
        // MainWindow's edge (MainWindow.cpp, "VfoWidget -> SliceModel").
        QObject::connect(h.flag, &VfoWidget::frequencyChanged, &slice,
                         [&slice](double hz) { slice.setFrequency(hz); });
        h.flag->setCollapsed(true);
        QVERIFY(h.flag->isCollapsed());

        const QPointF at(17, 22);
        QWheelEvent up(at, h.flag->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(h.flag, &up);
        QCOMPARE(slice.frequency(), 14225100.0);
        QWheelEvent down(at, h.flag->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton,
                         Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(h.flag, &down);
        QCoreApplication::sendEvent(h.flag, &down);
        QCOMPARE(slice.frequency(), 14225000.0 - 100.0);
        QCOMPARE(h.flag->collapsedFrequencyLabelForTest()->text(), QStringLiteral("14.224.900"));
    }

    void locked_folded_flag_reads_locked()
    {
        Host h(2);
        h.flag->setFrequency(7074000.0);
        h.flag->setCollapsed(true);
        QLabel* label = h.flag->collapsedFrequencyLabelForTest();
        QVERIFY(label);
        QVERIFY(!label->isHidden());
        QCOMPARE(label->text(), QStringLiteral("7.074.000"));
        h.flag->setLocked(true);
        QCOMPARE(label->text(), QStringLiteral("LOCKED"));
        h.flag->setLocked(false);
        QCOMPARE(label->text(), QStringLiteral("7.074.000"));
        h.flag->setLocked(true);
        h.flag->setCollapsed(false);
        QVERIFY(label->isHidden());
        h.flag->setCollapsed(true);
        QCOMPARE(label->text(), QStringLiteral("LOCKED"));
        // The label sits beside the folded flag, on the side away from the
        // marker, vertically centred on it.
        const QRect f = h.flag->geometry();
        const QRect l = label->geometry();
        QVERIFY(l.right() < f.left() || l.left() > f.right());
        QVERIFY2(qAbs(l.center().y() - f.center().y()) <= 1,
                 qPrintable(QStringLiteral("label y %1, flag y %2").arg(l.center().y()).arg(f.center().y())));
    }

    void accessible_name_says_folded()
    {
        Host h(1);
        h.flag->setFrequency(14074000.0);
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(h.flag);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::Grouping);
        QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("VFO slice B, 14.074000 MHz"));
        h.flag->setCollapsed(true);
        QCOMPARE(iface->text(QAccessible::Name),
                 QStringLiteral("VFO slice B, 14.074000 MHz, collapsed"));
        h.flag->setTxSlice(true);
        QCOMPARE(iface->text(QAccessible::Name),
                 QStringLiteral("VFO slice B, 14.074000 MHz, transmit slice, collapsed"));
    }

    void flag_stays_over_pan_corner_badges()
    {
        PanadapterApplet applet(QStringLiteral("0"));
        applet.resize(900, 260);
        applet.show();
        SpectrumWidget* sw = applet.spectrumWidget();
        sw->setFrequencyRange(14200000.0, 200000.0);
        applet.setWideBpf(true, QStringLiteral("test"));
        applet.statusOverlay()->setTxBound(true);
        VfoWidget* flag = sw->addVfoWidget(0);
        flag->setMode(DSPMode::USB);  // the flag sits left of its marker
        flag->setFrequency(14290000.0);  // near the pan's right side
        sw->updateVfoPositions();
        QCoreApplication::processEvents();

        QLabel* badge = letterBadge(*flag, QStringLiteral("A"));
        QVERIFY(badge);
        const QPoint centre = badge->mapTo(&applet, badge->rect().center());
        // The layout this guards: the badge strip's box covers the letter.
        QVERIFY2(applet.statusOverlay()->geometry().contains(centre),
                 qPrintable(QStringLiteral("strip %1,%2 %3x%4, letter at %5,%6")
                     .arg(applet.statusOverlay()->x()).arg(applet.statusOverlay()->y())
                     .arg(applet.statusOverlay()->width()).arg(applet.statusOverlay()->height())
                     .arg(centre.x()).arg(centre.y())));

        QWidget* top = applet.childAt(centre);
        QVERIFY2(top == badge, top ? top->metaObject()->className() : "nothing");

        QSignalSpy pressed(flag, &VfoWidget::sliceActivationRequested);
        // Through the window, so Qt picks the receiver under the pointer the
        // way a real click does.
        QVERIFY(QTest::qWaitForWindowExposed(&applet));
        QTest::mouseClick(applet.windowHandle(), Qt::LeftButton, {}, centre);
        QVERIFY2(pressed.count() == 1, "the press did not reach the flag");
        QTRY_VERIFY(flag->isCollapsed());

        // Folded: its frequency label stays over the strip too, and the
        // strip shows again where the open flag was.
        QLabel* label = flag->collapsedFrequencyLabelForTest();
        QVERIFY(label && !label->isHidden());
        const QPoint labelCentre = label->mapTo(&applet, label->rect().center());
        if (applet.statusOverlay()->geometry().contains(labelCentre)) {
            QCOMPARE(applet.childAt(labelCentre), label);
        }
        flag->setCollapsed(false);
    }
};

QTEST_MAIN(TestVfoFlagFold)
#include "tst_vfo_flag_fold.moc"
