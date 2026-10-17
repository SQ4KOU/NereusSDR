// no-port-check: NereusSDR-original. A window saved maximized creates its
// native window only once its spectrum widgets exist (issue #525).

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// tests/tst_main_window_maximized_restore.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port.
//
// Issue #525 (Linux AppImage): closed while maximized, the next start
// showed a blank panadapter and waterfall and logged "QRhiWidget: No QRhi"
// every display tick. Restoring a maximized geometry calls
// QWidget::setWindowState(), which creates a hidden top-level's native
// window at once. MainWindow restored its geometry at the top of buildUI(),
// before any SpectrumWidget existed, so Qt decided the window's composition
// with no QRhiWidget in its tree. Qt checks again when a widget is
// parented into the window (QWidget::setParent, Qt 6.11
// qwidget.cpp:10943-10960), but a QRhiWidget turns its QRhi setting on
// only in QRhiWidgetPrivate::init() (qrhiwidget.cpp:196), after the
// QWidget base constructor has parented it, so that check found nothing
// to enable. On Linux the SpectrumWidget is not a native
// window of its own (on macOS and Windows it is), so it then had no QRhi.
// A window saved at normal size is created at show(), after the pans exist,
// which is why only the maximized start failed.
//
// The test watches every widget's WinIdChange (sent when Qt gives a widget
// its native window) and records how many SpectrumWidgets the MainWindow
// holds at that moment.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-16: issue #525. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QEvent>
#include <QWidget>

#include "core/AppSettings.h"
#include "fakes/MainWindowTestSettings.h"
#include "gui/MainWindow.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

namespace {

// Records, for the first MainWindow given a native window, how many
// SpectrumWidgets it held when that happened.
class NativeWindowCreationProbe : public QObject {
public:
    bool seen{false};
    int spectrumWidgetsAtCreation{-1};

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!seen && event->type() == QEvent::WinIdChange) {
            if (auto* window = qobject_cast<MainWindow*>(watched)) {
                seen = true;
                spectrumWidgetsAtCreation =
                    int(window->findChildren<SpectrumWidget*>().size());
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

QString savedGeometry(Qt::WindowState state)
{
    // The offscreen platform's screen is 800 x 600, and the window's
    // minimum size is the same, so the saved normal size fills it exactly.
    QWidget saved;
    saved.resize(800, 600);
    saved.move(0, 0);
    saved.setWindowState(state);
    return QString::fromLatin1(saved.saveGeometry().toBase64());
}

} // namespace

class TstMainWindowMaximizedRestore : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        AppSettings::instance().clear();
        Test::markAudioFirstRunDone();
    }

    void savedMaximized_nativeWindowComesAfterTheSpectrum()
    {
        AppSettings::instance().setValue(QStringLiteral("MainWindowGeometry"),
                                         savedGeometry(Qt::WindowMaximized));

        NativeWindowCreationProbe probe;
        qApp->installEventFilter(&probe);
        MainWindow window(RemoteStationOptions{}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        window.show();
        qApp->removeEventFilter(&probe);

        QVERIFY(probe.seen);
        QVERIFY2(probe.spectrumWidgetsAtCreation > 0,
                 qPrintable(QStringLiteral(
                     "the native window was created with %1 SpectrumWidgets in it")
                     .arg(probe.spectrumWidgetsAtCreation)));
        // Issue #206 still holds: the saved maximized state comes back.
        QVERIFY(window.windowState().testFlag(Qt::WindowMaximized));
    }

    void savedNormal_nativeWindowComesAfterTheSpectrum()
    {
        AppSettings::instance().setValue(QStringLiteral("MainWindowGeometry"),
                                         savedGeometry(Qt::WindowNoState));

        NativeWindowCreationProbe probe;
        qApp->installEventFilter(&probe);
        MainWindow window(RemoteStationOptions{}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        window.show();
        qApp->removeEventFilter(&probe);

        QVERIFY(probe.seen);
        QVERIFY(probe.spectrumWidgetsAtCreation > 0);
        QVERIFY(!window.windowState().testFlag(Qt::WindowMaximized));
        QCOMPARE(window.size(), QSize(800, 600));
    }
};

QTEST_MAIN(TstMainWindowMaximizedRestore)
#include "tst_main_window_maximized_restore.moc"
