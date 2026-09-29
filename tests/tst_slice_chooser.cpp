// =================================================================
// tests/tst_slice_chooser.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. The bottom RX area's all-slice
// chooser (slice control and shared listening plan Task 13): its rows and
// actions in each state, a request waiting for the Core's answer, the
// empty window's honest words, narrow widths, the two row builders and two
// same-named devices told apart. Renders every state offscreen
// (NEREUS_TASK78_SHOTS keeps them); nothing opens a window.
//
// Modification history (NereusSDR):
//   2026-09-29: created for NereusSDR by J.J. Boyd (KG4VCF), slice control
//               and shared listening plan Task 13, with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>

#include "core/session/RemoteDevicesState.h"
#include "core/session/SliceAccessMirror.h"
#include "gui/SliceChooser.h"
#include "gui/StyleConstants.h"
#include "gui/widgets/RxDashboard.h"
#include "gui/widgets/VfoWidget.h"

using namespace NereusSDR;

namespace {

using Row = SliceChooser::Row;
using Controller = SliceChooser::Controller;

Row row(int id, Controller controller, const QString& name = QString())
{
    Row r;
    r.sliceId = id;
    r.color = VfoWidget::sliceColor(id);
    r.frequencyHz = 14200000.0 + id * 10000.0;
    r.mode = QStringLiteral("USB");
    r.filter = QStringLiteral("2.4k");
    r.controller = controller;
    r.controllerName = name;
    r.listenerCount = controller == Controller::Nobody ? 0 : 1;
    return r;
}

QStringList actionWords(const SliceChooser& chooser)
{
    QStringList words;
    for (QPushButton* b : chooser.findChildren<QPushButton*>(QStringLiteral("sliceChooserAction"))) {
        if (!b->isHidden() && b->parent() != nullptr) {
            words.append(b->text());
        }
    }
    return words;
}

QPushButton* action(const SliceChooser& chooser, const QString& words)
{
    for (QPushButton* b : chooser.findChildren<QPushButton*>(QStringLiteral("sliceChooserAction"))) {
        if (b->text() == words) {
            return b;
        }
    }
    return nullptr;
}

// The bottom area as it looks: the dashboard's picker under the open
// chooser, at the chooser's width.
void render(SliceChooser& chooser, const QString& banner, const QString& name)
{
    QCoreApplication::processEvents();
    chooser.adjustSize();
    chooser.resize(std::max(chooser.sizeHint().width(), 420), chooser.sizeHint().height());
    RxDashboard dashboard;
    dashboard.setAutoFillBackground(true);
    QPalette dark = dashboard.palette();
    dark.setColor(QPalette::Window, QColor(Style::kAppBg));
    dashboard.setPalette(dark);
    dashboard.setChooserState(banner);
    dashboard.adjustSize();
    const QImage top = chooser.grab().toImage();
    const QImage bottom = dashboard.grab().toImage();
    QVERIFY(!top.isNull() && !bottom.isNull());
    const QString dir = qEnvironmentVariable("NEREUS_TASK78_SHOTS");
    if (dir.isEmpty()) {
        return;
    }
    QImage out(std::max(top.width(), bottom.width()), top.height() + bottom.height() + 6,
               QImage::Format_ARGB32);
    out.fill(QColor(Style::kAppBg));
    QPainter painter(&out);
    painter.drawImage(0, 0, top);
    painter.drawImage(0, top.height() + 6, bottom);
    painter.end();
    QDir().mkpath(dir);
    QVERIFY(out.save(QDir(dir).filePath(QStringLiteral("slice-chooser-%1.png").arg(name))));
}

MirrorUpdate text(const char* name, const QString& value)
{
    return MirrorUpdate{0, name, MirrorWireKind::Utf8, QVariant(value)};
}

MirrorUpdate count(const char* name, qint64 value)
{
    return MirrorUpdate{0, name, MirrorWireKind::Int64, QVariant(value)};
}

MirrorUpdate flag(const char* name, bool value)
{
    return MirrorUpdate{0, name, MirrorWireKind::Bool, QVariant(value)};
}

} // namespace

class TstSliceChooser : public QObject {
    Q_OBJECT

private slots:
    // Another device's slice: listen in, or take control of it.
    void anotherDevicesSliceOffersListenAndTake()
    {
        SliceChooser chooser;
        chooser.setInventory({row(0, Controller::OtherDevice, QStringLiteral("Jo's iPhone")),
                              [] { Row r = row(1, Controller::ThisWindow);
                                   r.listeningHere = true; r.activeHere = true; return r; }()});
        chooser.selectSlice(0);
        QCOMPARE(actionWords(chooser), (QStringList{QStringLiteral("Listen in"),
                                                    QStringLiteral("Take control")}));
        QSignalSpy listen(&chooser, &SliceChooser::listenRequested);
        QSignalSpy take(&chooser, &SliceChooser::takeControlRequested);
        action(chooser, QStringLiteral("Listen in"))->click();
        QCOMPARE(listen.size(), 1);
        QCOMPARE(listen.first().at(0).toInt(), 0);
        action(chooser, QStringLiteral("Take control"))->click();
        QCOMPARE(take.size(), 1);
        render(chooser, QStringLiteral("You control"), QStringLiteral("remote-another-device"));
    }

    // A slice this window listens to: select it, take control, or stop.
    void aListenedSliceOffersSelectTakeAndStop()
    {
        SliceChooser chooser;
        Row listened = row(0, Controller::OtherDevice, QStringLiteral("Jo's iPhone"));
        listened.listeningHere = true;
        listened.listenerCount = 2;
        chooser.setInventory({listened});
        QCOMPARE(actionWords(chooser),
                 (QStringList{QStringLiteral("Select RX"), QStringLiteral("Take control"),
                              QStringLiteral("Stop listening")}));
        QSignalSpy stop(&chooser, &SliceChooser::stopListeningRequested);
        QSignalSpy select(&chooser, &SliceChooser::selectRequested);
        action(chooser, QStringLiteral("Select RX"))->click();
        action(chooser, QStringLiteral("Stop listening"))->click();
        QCOMPARE(select.size(), 1);
        QCOMPARE(stop.size(), 1);
        render(chooser, QStringLiteral("Listening"), QStringLiteral("listened"));
    }

    // A slice this window controls: Release, nothing to take.
    void aControlledSliceOffersRelease()
    {
        SliceChooser chooser;
        Row mine = row(1, Controller::ThisWindow);
        mine.listeningHere = true;
        mine.activeHere = true;
        chooser.setInventory({mine});
        QCOMPARE(actionWords(chooser), QStringList{QStringLiteral("Release")});
        QSignalSpy release(&chooser, &SliceChooser::releaseRequested);
        action(chooser, QStringLiteral("Release"))->click();
        QCOMPARE(release.size(), 1);
        render(chooser, QStringLiteral("You control"), QStringLiteral("controlled"));
    }

    // On the air: Take control and Release wait until it stops.
    void aTransmittingSliceWaits()
    {
        SliceChooser chooser;
        Row onAir = row(0, Controller::OtherDevice, QStringLiteral("Jo's iPhone"));
        onAir.transmitting = true;
        Row mine = row(1, Controller::ThisWindow);
        mine.listeningHere = true;
        mine.activeHere = true;
        mine.transmitting = true;
        chooser.setInventory({onAir, mine});
        chooser.selectSlice(0);
        QVERIFY(!action(chooser, QStringLiteral("Take control"))->isEnabled());
        QVERIFY(action(chooser, QStringLiteral("Listen in"))->isEnabled());
        render(chooser, QStringLiteral("You control"), QStringLiteral("on-air"));
        chooser.selectSlice(1);
        QVERIFY(!action(chooser, QStringLiteral("Release"))->isEnabled());
    }

    // A request waits for the Core's answer, then shows it.
    void aRequestShowsWaitingThenTheAnswer()
    {
        SliceChooser chooser;
        chooser.setInventory({row(0, Controller::OtherDevice, QStringLiteral("Jo's iPhone"))});
        chooser.setPending(QStringLiteral("Asking the Core…"));
        QVERIFY(chooser.isPending());
        QVERIFY(!action(chooser, QStringLiteral("Listen in"))->isEnabled());
        QVERIFY(!chooser.findChild<QPushButton*>(QStringLiteral("sliceChooserNewSlice"))->isEnabled());
        QCOMPARE(chooser.message(), QStringLiteral("Asking the Core…"));
        render(chooser, QStringLiteral("Choose a slice"), QStringLiteral("waiting"));
        const QString refused =
            QStringLiteral("Slice A is transmitting. Take control once it stops.");
        chooser.showResult(refused);
        QVERIFY(!chooser.isPending());
        QCOMPARE(chooser.message(), refused);
        QVERIFY(action(chooser, QStringLiteral("Listen in"))->isEnabled());
    }

    // No slice: honest words, and New slice.
    void anEmptyWindowOffersNewSlice()
    {
        SliceChooser chooser;
        chooser.setInventory({});
        QVERIFY(chooser.findChild<QLabel*>(QStringLiteral("sliceChooserEmpty")) != nullptr);
        QCOMPARE(chooser.findChild<QLabel*>(QStringLiteral("sliceChooserEmpty"))->text(),
                 QStringLiteral("No slices are running. Create a slice to start listening."));
        QVERIFY(actionWords(chooser).isEmpty());
        QSignalSpy newSlice(&chooser, &SliceChooser::newSliceRequested);
        chooser.findChild<QPushButton*>(QStringLiteral("sliceChooserNewSlice"))->click();
        QCOMPARE(newSlice.size(), 1);
        QCOMPARE(SliceChooser::bannerState({}), QStringLiteral("Choose a slice"));
        render(chooser, QStringLiteral("Choose a slice"), QStringLiteral("empty"));
        for (QLabel* l : chooser.findChildren<QLabel*>()) {
            QVERIFY2(OperatorWording::isPlain(l->text()) || l->text().isEmpty(),
                     qPrintable(l->text()));
        }
    }

    // A window on its own: every slice its own.
    void aLocalWindowListsItsOwnSlices()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        model.sliceById(a)->setFrequency(7074000.0);
        const RemoteDevicesState none;
        const QList<Row> rows = SliceChooser::rowsForRemoteWindow(model, nullptr, none);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().controller, Controller::ThisWindow);
        QVERIFY(rows.first().listeningHere);
        SliceChooser chooser;
        chooser.setInventory(rows);
        render(chooser, SliceChooser::bannerState(rows), QStringLiteral("local"));
    }

    // A remote window: its own slice, a marker for another device's, and
    // the access objects saying who controls and listens.
    void aRemoteWindowListsEverySliceOnTheCore()
    {
        RadioModel model(RadioModel::Role::Remote);
        RemoteDevicesState devices;
        devices.setSelfDeviceId(QStringLiteral("me"));
        devices.applyObject("marker:0", {count("sliceId", 0), text("ownerDeviceId", "jo"),
                                         text("ownerName", "Jo's iPhone"),
                                         flag("ownerAway", true),
                                         MirrorUpdate{0, "frequency", MirrorWireKind::Float64,
                                                      QVariant(14074000.0)},
                                         count("dspMode", 1)});
        SliceAccessMirror access(nullptr, nullptr);
        access.setSelfDeviceId(QStringLiteral("me"));
        access.applyObject("access:0", {count("incarnation", 5), text("controllerDeviceId", "jo"),
                                        count("controlRevision", 2),
                                        text("listenerDeviceIds", "[\"jo\",\"me\"]"),
                                        text("activeRxDeviceIds", "[]"), flag("onAir", false)});
        const QList<Row> rows = SliceChooser::rowsForRemoteWindow(model, &access, devices);
        QCOMPARE(rows.size(), 1);
        const Row& r = rows.first();
        QCOMPARE(r.sliceId, 0);
        QCOMPARE(r.controller, Controller::OtherDevice);
        QCOMPARE(r.controllerName, QStringLiteral("Jo's iPhone"));
        QVERIFY(r.controllerAway);
        QVERIFY(r.listeningHere);
        QCOMPARE(r.listenerCount, 2);
        QCOMPARE(r.frequencyHz, 14074000.0);
        SliceChooser chooser;
        chooser.setInventory(rows);
        render(chooser, QStringLiteral("Listening"), QStringLiteral("remote-away"));
    }

    // The hosting desktop: the Core's slices, two same-named devices told
    // apart by the names the Core numbers.
    void theHostingDesktopTellsSameNamedDevicesApart()
    {
        Core core;
        core.model->configureStreamPool(5, 5, 192000);
        Device first(QStringLiteral("MacBook-Pro"), QStringLiteral("computer"));
        Device second(QStringLiteral("MacBook-Pro"), QStringLiteral("computer"));
        core.pair(first);
        core.pair(second);
        LoopbackTransport* appFirst = core.signIn(first);
        LoopbackTransport* appSecond = core.signIn(second);
        QVERIFY(admitted(appFirst) && admitted(appSecond));
        const QList<Row> rows = SliceChooser::rowsForHostingDesktop(*core.model, *core.server);
        QStringList names;
        for (const Row& r : rows) {
            if (r.controller == Controller::OtherDevice) {
                names.append(r.controllerName);
            }
        }
        QCOMPARE(names.size(), 2);
        QVERIFY2(names.at(0) != names.at(1), qPrintable(names.join(QLatin1String(" | "))));
        SliceChooser chooser;
        chooser.setInventory(rows);
        render(chooser, SliceChooser::bannerState(rows), QStringLiteral("hosting-two-names"));
    }

    // Narrow: nothing is cut off; words wrap.
    void aNarrowChooserDoesNotClip()
    {
        SliceChooser chooser;
        chooser.setInventory({row(0, Controller::OtherDevice,
                                  QStringLiteral("A very long device name from the workshop")),
                              [] { Row r = row(1, Controller::ThisWindow);
                                   r.listeningHere = true; r.activeHere = true; return r; }()});
        chooser.selectSlice(0);
        chooser.resize(chooser.minimumSizeHint().width(), chooser.heightForWidth(300) > 0
                                                             ? chooser.heightForWidth(300)
                                                             : chooser.sizeHint().height());
        chooser.resize(300, chooser.sizeHint().height());
        QCoreApplication::processEvents();
        for (QPushButton* b : chooser.findChildren<QPushButton*>(QStringLiteral("sliceChooserAction"))) {
            QVERIFY2(b->sizeHint().width() <= chooser.width(), qPrintable(b->text()));
        }
        QVERIFY(chooser.minimumSizeHint().width() <= 320);
        render(chooser, QStringLiteral("You control"), QStringLiteral("narrow"));
    }
};

QTEST_MAIN(TstSliceChooser)
#include "tst_slice_chooser.moc"
