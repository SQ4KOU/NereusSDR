// no-port-check: NereusSDR-original visual preview tool; scene table.
// =================================================================
// tests/visual/PreviewScenes.h  (NereusSDR)
// =================================================================
//
// The scene table of the workspace visual preview tool. A scene is a named
// function that poses an already-shown MainWindow (or one of its dialogs)
// and adds the images to capture. Later tasks add proposed-design scenes
// by appending rows to sceneTable().
//
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace visual review. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-07 - Proposed header scenes: per-scene workspace preview
//                 switch, extra crops, provenance and proposed values.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - fedReadings for the sidecar. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-11 - nothingToScroll, set with the failure of a scrolled
//                 scene whose page fits. J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QWidget;

namespace NereusSDR {

class MainWindow;
class RadioModel;

namespace Preview {

// One crop of a full-window image, in logical and device pixels.
struct CropRect {
    QString name;
    QRect logical;
    QRect device;
};

// One image a scene produces.
struct CapturedImage {
    QString name;          // file stem, e.g. "baseline-two-band" or "...--flag"
    QString kind;          // "window", "crop", "crop-3x", "floating", "dialog"
    QImage image;
    QSize logicalSize;     // logical size of what was grabbed
    double dpr{1.0};
    QVector<CropRect> crops;   // for a full-window image: every crop cut from it
    QJsonObject extra;         // crop rectangle for a crop, widget class, ...
    // Window and dialog grabs: one line per visible widget (path, class,
    // object name, control kind, rectangle in the grabbed widget's
    // coordinates), written next to the image as <name>.geometry.tsv.
    QStringList geometry;
};

struct SceneContext {
    MainWindow* window{nullptr};
    RadioModel* model{nullptr};
    QSize size;          // requested logical client size
    double dpr{1.0};     // requested device pixel ratio
    bool crops{false};   // write detail crops (the mac-1512x869@2 profile)
    QVector<CapturedImage> images;
    QJsonArray notShown;
    QJsonArray unfrozen;
    QJsonArray frozen;
    QJsonArray stoppedTimers;
    QJsonArray checks;
    QJsonArray notes;
    // Readings fed to Longpath instruments through the display-only seam.
    QJsonArray fedReadings;
    QString failure;     // non-empty: the scene could not run; exit non-zero
    // Set with `failure` by a --scrolled scene whose page has nothing to
    // scroll: the fonts of this platform let the whole page fit, so the
    // page's own scene already shows all of it. The preview tool still
    // fails on it (the scene list is for the Mac's fonts); a run that only
    // reads the grabs can tell this from a scene that could not run.
    bool nothingToScroll{false};
    // Called with the window or dialog right before it is grabbed (the
    // workspace look re-runs its scoped stylesheet pass here). Empty for
    // the retained baseline, which then captures exactly as before.
    std::function<void(QWidget*)> beforeGrab;
    // Non-empty: captureWindow writes the rectangle of every visible
    // widget in the right panel and the first pan's toolbar to this
    // file, so two runs can be compared for moved or resized widgets.
    QString geometryDump;
    // Proposed-design scenes: extra detail crops at the crop profile, the
    // sidecar provenance ("proposed") and the values the scene shows.
    std::function<void(SceneContext&, QVector<CropRect>&, double dpr)> extraCrops;
    QString provenance;
    QJsonObject proposedValues;
};

struct Scene {
    QString name;
    // "1", "2v", "2x2", ...: the PanLayoutId set before the window exists.
    QString panLayout;
    bool dialog{false};
    std::function<void(SceneContext&)> run;
    // true: the scene always runs with the workspace preview switch on.
    bool workspacePreview{false};
};

// Every fixed scene (non-dialog and fixed dialogs).
QVector<Scene> fixedScenes();

// Setup-page dialog scenes, one per pageLabelsForTest() leaf of a
// SetupDialog the window creates (and closes again).
QVector<Scene> setupPageScenes(MainWindow* window);

// Pumps the event loop for `ms` milliseconds.
void pump(int ms);

} // namespace Preview
} // namespace NereusSDR
