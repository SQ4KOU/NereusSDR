#pragma once
// no-port-check: NereusSDR-original list of the screens the containers code consults.
// Modification history (NereusSDR):
//   2026-10-12 - desktopSize(): how wide and tall all the screens are
//                 together. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - The screens floating and popped-out windows are placed
//                 on, read in one place so tests can stand in for
//                 monitors that are not there, by J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-11 - matching() chooses among several screens with one serial.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#include "ContainerDocument.h"
#include <QRect>
#include <QString>
#include <QVector>
#include <optional>

namespace NereusSDR {

// One monitor as the containers code sees it. `available` is `geometry`
// less the system's own bars.
struct ScreenInfo {
    QString name;
    QString serial;
    QRect geometry;
    QRect available;
    bool operator==(const ScreenInfo&) const = default;
};

// A screen matches a saved one when the serial is non-empty and equal, or
// else when name and geometry are both equal. (A serial is often empty:
// Qt reports none for this Mac's built-in display.)
bool screenMatches(const ScreenInfo& screen, const SavedScreen& saved);

// What a window on `screen` saves beside its geometry.
SavedScreen savedScreenOf(const ScreenInfo& screen);

// The screens present now. The default reads QGuiApplication; a test
// overrides screens() and primary().
class ScreenSource {
public:
    virtual ~ScreenSource();
    virtual QVector<ScreenInfo> screens() const;
    virtual ScreenInfo primary() const;

    // The present screen that matches `saved`, if any. Two monitors of one
    // model can report one serial, so when several match it is the one
    // with the saved geometry, else the first that `place` (the window's
    // remembered rect, when given) is on, else the first.
    std::optional<ScreenInfo> matching(const SavedScreen& saved, const QRect& place = QRect()) const;
    // The screen holding most of `rect`; none when it is on no screen.
    std::optional<ScreenInfo> screenHolding(const QRect& rect) const;
    // screenHolding(), or the primary screen.
    ScreenInfo screenFor(const QRect& rect) const;
    // True when any part of `rect` is on a present screen.
    bool onAnyScreen(const QRect& rect) const;
    // How wide and how tall the present screens are together (the size of
    // the rect around all of them). Not valid with no screen.
    QSize desktopSize() const;

    // The application's own screens.
    static ScreenSource* system();
};

} // namespace NereusSDR
