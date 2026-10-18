// no-port-check: NereusSDR-original visual preview tool; no upstream logic.
// =================================================================
// tests/visual/VisualPreviewMain.cpp  (NereusSDR)
// =================================================================
//
// nereus_visual_preview: builds the real MainWindow on a test board with no
// radio, poses one named scene with fixed illustrative data, and writes
// full-resolution PNGs plus a JSON sidecar per PNG. One scene per process.
// Every window stays off screen (Qt::WA_DontShowOnScreen) and is captured
// in-process with QWidget::grab(), which includes the QRhiWidget spectrum.
//
//   nereus_visual_preview --probe
//   nereus_visual_preview --list
//   nereus_visual_preview --scene <name> --size <W>x<H> --dpr <d> --out <dir>
//                         [--profile <name>] [--workspace-preview]
//                         [--dump-geometry <file>]
//
// A scene named look-<x> is the Task 1 scene <x> (baseline-<x>, or
// dialog-<x> for look-dialog-<x>) with the workspace preview switch on;
// --workspace-preview turns the switch on for any scene. Every other scene
// runs with the switch off: the binary sets it both ways and never reads
// NEREUS_WORKSPACE_PREVIEW, whose default in the app is on.
// --dump-geometry <file> writes the rectangle of every visible widget in
// the right panel and the first pan's toolbar (window scenes only).
//
// tests/visual/capture.py drives it; see tests/visual/README.md.
//
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace visual review, replacing the
//                 feasibility spike. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-07 - Workspace look scenes (look-*), --workspace-preview and
//                 the look coverage sidecar fields. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-07 - Scenes that carry their own workspace preview switch,
//                 provenance and proposed values. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Look variants only for dialog-* scenes (setup-look-*
//                 are looks already). J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - The export-import scene runs on a fixed profile name, so
//                 the settings path it shows is the same every run.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - No re-theme before the grab (rethemeBeforeGrab removed):
//                 the live pass themes every window, so the images show
//                 what the app shows. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - unknown_literals: the whole unknown-literal count beside
//                 the top 30; the coverage notes name the colour forms the
//                 translator reads. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - NEREUS_PREVIEW_DIAL_PLATE=b turns SMeterWidget's test-only
//                 plate limit on for any scene (the identity run proving
//                 variant B leaves the Main Panel's dial unchanged).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - NEREUS_PREVIEW_DIAL_PLATE removed: the plate limit is the
//                 product. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-08 - <name>.geometry.tsv beside every image that carries a
//                 widget geometry dump. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - Look pairs for the menus, popups, toasts and tooltips
//                 scenes (look-menu-*, look-tooltip-*, ...), preview-only
//                 ones excepted. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-09 - D25: the Export / Import size variant (--min), where the
//                 settings path wraps onto a line of its own, runs on the
//                 fixed-name profile too. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-12 - The binary sets the workspace preview switch both ways,
//                 now that the app's default is on. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-14 - look-connected, the look pair of baseline-connected.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-15 - look-connected-tx, the look pair of baseline-connected-tx.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QSysInfo>
#include <QMenuBar>
#include <QTextStream>
#include <QWindow>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

#include "NereusBuildTag.h"
#include "PreviewScenes.h"
#include "core/AppSettings.h"
#include "core/BuildIdentity.h"
#include "core/HpsdrModel.h"
#include "fakes/MainWindowTestSettings.h"
#include "fakes/RemoteWindowHarness.h"
#include "gui/GuiApplication.h"
#include "gui/MainWindow.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumWidget.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include <algorithm>
#include "models/RadioModel.h"

using namespace NereusSDR;
using namespace NereusSDR::Preview;

namespace {

constexpr int kExitBadArgs = 2;
constexpr int kExitMismatch = 3;
constexpr int kExitSceneFailed = 4;
constexpr int kExitUnsafeProfile = 5;
constexpr int kSliceWaitMs = 10000;
constexpr int kStartupSettleMs = 1500;
constexpr double kDprTolerance = 0.001;

// Sets Qt::WA_DontShowOnScreen on every window before it reaches the screen,
// so no capture ever shows a real window (dialogs and floating containers
// included).
class OffScreenFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish || event->type() == QEvent::Show) {
            if (auto* w = qobject_cast<QWidget*>(watched); w != nullptr && w->isWindow()) {
                w->setAttribute(Qt::WA_DontShowOnScreen, true);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

struct Args {
    bool probe{false};
    bool list{false};
    QString scene;
    QSize size{1512, 869};
    double dpr{2.0};
    QString out;
    QString profile;
    bool workspacePreview{false};
    QString geometryDump;
};

bool parseArgs(const QStringList& argv, Args& args, QString& error)
{
    for (int i = 1; i < argv.size(); ++i) {
        const QString& a = argv.at(i);
        auto value = [&](QString& out) {
            if (i + 1 >= argv.size()) {
                error = QStringLiteral("%1 needs a value").arg(a);
                return false;
            }
            out = argv.at(++i);
            return true;
        };
        QString v;
        if (a == QLatin1String("--probe")) {
            args.probe = true;
        } else if (a == QLatin1String("--list")) {
            args.list = true;
        } else if (a == QLatin1String("--workspace-preview")) {
            args.workspacePreview = true;
        } else if (a == QLatin1String("--scene")) {
            if (!value(args.scene)) { return false; }
        } else if (a == QLatin1String("--dump-geometry")) {
            if (!value(args.geometryDump)) { return false; }
        } else if (a == QLatin1String("--out")) {
            if (!value(args.out)) { return false; }
        } else if (a == QLatin1String("--profile")) {
            if (!value(args.profile)) { return false; }
        } else if (a == QLatin1String("--size")) {
            if (!value(v)) { return false; }
            const QStringList wh = v.split(QLatin1Char('x'));
            bool okW = false;
            bool okH = false;
            if (wh.size() == 2) {
                args.size = QSize(wh[0].toInt(&okW), wh[1].toInt(&okH));
            }
            if (!okW || !okH || args.size.isEmpty()) {
                error = QStringLiteral("bad --size %1 (want WxH)").arg(v);
                return false;
            }
        } else if (a == QLatin1String("--dpr")) {
            if (!value(v)) { return false; }
            bool ok = false;
            args.dpr = v.toDouble(&ok);
            if (!ok || args.dpr <= 0.0) {
                error = QStringLiteral("bad --dpr %1").arg(v);
                return false;
            }
        } else {
            error = QStringLiteral("unknown argument %1").arg(a);
            return false;
        }
    }
    if (!args.probe && !args.list && (args.scene.isEmpty() || args.out.isEmpty())) {
        error = QStringLiteral("need --probe, --list, or --scene <name> --out <dir>");
        return false;
    }
    return true;
}

void applyAppTheme(QApplication& app)
{
    // The startup sequence of src/main.cpp that gives the app its look.
    app.setApplicationName(QStringLiteral("NereusSDR"));
    app.setApplicationVersion(QStringLiteral(NEREUSSDR_VERSION));
    app.setOrganizationName(QStringLiteral("NereusSDR"));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    applyDarkPalette(app);
    applyAppBaselineQss(app);
    if (WorkspacePreview::enabled()) {
        applyWorkspacePalette(app);
    }
}

// ---- settings profile ------------------------------------------------------

// The scenes that show the settings file path (the page scene and its size
// variants, named from it), and the fixed profile they run on (see main()).
constexpr auto kFixedProfileScene = "dialog-setup-export-import";
constexpr auto kFixedProfileName = "visual-preview-fixed";
// Longer than one capture run of the scene; capture.py stops a run at 180 s.
constexpr int kFixedProfileLockWaitMs = 170000;

// ---- workspace look --------------------------------------------------------

constexpr auto kLookPrefix = "look-";

bool isLookScene(const QString& name)
{
    return name.startsWith(QLatin1String(kLookPrefix));
}

// The menus, popups, toasts and tooltips scenes (Task 24): each one that
// is not preview-only has a look pair named look-<name>.
bool isMenuSceneName(const QString& name)
{
    for (const char* prefix : {"menu-", "menubar-", "popup-", "toast-", "tooltip-"}) {
        if (name.startsWith(QLatin1String(prefix))) {
            return true;
        }
    }
    return false;
}

// look-two-band -> baseline-two-band; look-dialog-tx-eq -> dialog-tx-eq;
// look-menu-speaker -> menu-speaker.
QString baseSceneFor(const QString& name)
{
    if (!isLookScene(name)) {
        return name;
    }
    const QString rest = name.mid(int(qstrlen(kLookPrefix)));
    if (rest.startsWith(QLatin1String("dialog-")) || isMenuSceneName(rest)) {
        return rest;
    }
    return QStringLiteral("baseline-") + rest;
}

// The look name for a Task 1 scene, or empty when it has no look variant.
QString lookSceneFor(const Scene& s)
{
    // Only the Task 1 dialog-* scenes have look variants (no scene named
    // otherwise gets one: the edit-* and other workspace scenes are looks
    // already).
    if (s.dialog) {
        return s.name.startsWith(QLatin1String("dialog-")) ? QLatin1String(kLookPrefix) + s.name : QString();
    }
    if (isMenuSceneName(s.name)) {
        return s.workspacePreview ? QString() : QLatin1String(kLookPrefix) + s.name;
    }
    for (const char* base : {"baseline-two-band", "baseline-four-rx", "baseline-tx", "baseline-connected",
                             "baseline-connected-tx"}) {
        if (s.name == QLatin1String(base)) {
            return QLatin1String(kLookPrefix) + s.name.mid(int(qstrlen("baseline-")));
        }
    }
    return QString();
}

QJsonObject lookCoverageJson()
{
    auto arr = [](std::initializer_list<const char*> items) {
        QJsonArray a;
        for (const char* i : items) {
            a.append(QString::fromUtf8(i));
        }
        return a;
    };
    return QJsonObject{
        {QStringLiteral("explicit_roles"), arr({
             "application palette and baseline QSS (applyWorkspacePalette)",
             "main window stylesheet (MainWindow::applyDarkTheme, #203040 as border-subtle)",
             "top status strip: TitleBar, menu bar, connection segment colours, UTC clock in monoFont",
             "main splitter handle (#203040 as border-subtle, width unchanged)",
             "pan toolbar column buttons and pan corner zoom buttons (SpectrumOverlayPanel)",
             "pan CH tag and TX / TAKE TX / WIDE / DIV / PS HOLD badges (SpectrumStatusOverlay)",
             "bottom status bar frame",
             "ContainerWidget title bar, buttons, border, content background, resize grip",
             "ContainerContentHost insertion line, scroll bar, applets banner button, entry grip"})},
        {QStringLiteral("scoped_pass"), arr({
             "every widget when it is shown: windows, dialogs, Setup pages and tabs, menus, "
             "popups, tooltips and toasts, and once more when a shown widget sets a new "
             "stylesheet (WorkspacePreview::installLiveTheming); the grab does not re-theme",
             "container content (also after every reconcile)",
             "bottom status bar content (also at startup)",
             "pan toolbar flyouts (also when built)"})},
        {QStringLiteral("not_migrated"), arr({
             "slice flag: its S-meter bar, frequency digits, slice letter and slice colors: excluded "
             "(its controls take the look since 2026-10-11)",
             "spectrum and waterfall plot colours, filter overlays, band-plan strip: excluded",
             "meter face artwork and user meter colours: excluded (their surrounds change)",
             "EQ/CFC graph band colours: excluded",
             "spectrum chrome on the GPU path (grid, scale text): not migrated",
             "SpectrumWidget children other than the toolbar, corner buttons and badges "
             "(dBm range arrows, clarity badge): not migrated",
             "connection segment state dot, RTT and audio pip colours (state meaning): kept",
             "native dialogs (file, colour) and the macOS native menu bar: outside Qt's widgets",
             "painted colours: not stylesheets",
             "stylesheet colours on the kept list (lit states, indicators, data colours, "
             "two jobs, no role for the shade): kept; every other hex, rgb(), rgba() and "
             "named colour resolves to a role",
             "#203040 in the scoped pass: by context (groove, hover background, else border-subtle)"})},
    };
}

QJsonArray unknownLiteralsJson(int limit)
{
    const QHash<QString, int> counts = WorkspacePreview::unknownLiteralCounts();
    QVector<QPair<QString, int>> rows;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        rows.append({it.key(), it.value()});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    QJsonArray out;
    for (int i = 0; i < rows.size() && i < limit; ++i) {
        out.append(QJsonObject{{QStringLiteral("literal"), rows[i].first},
                               {QStringLiteral("count"), rows[i].second}});
    }
    return out;
}

// Look scenes name their images after the look scene, not the base.
void renameImages(SceneContext& ctx, const QString& from, const QString& to)
{
    auto renamed = [&](const QString& n) {
        return n.startsWith(from) ? to + n.mid(from.size()) : n;
    };
    for (CapturedImage& img : ctx.images) {
        img.name = renamed(img.name);
        if (img.extra.contains(QStringLiteral("source"))) {
            img.extra[QStringLiteral("source")] = renamed(img.extra.value(QStringLiteral("source")).toString());
        }
    }
}

// The settings file must be the isolated test profile under Qt's test-mode
// locations, never the operator's real NereusSDR settings.
bool profileIsSafe(QString& why)
{
    const QString path = AppSettings::instance().filePath();
    const QString home = QDir::homePath();
    const QStringList forbidden = {home + QStringLiteral("/.config/NereusSDR/"),
                                   home + QStringLiteral("/Library/Preferences/NereusSDR/")};
    for (const QString& f : forbidden) {
        if (path.startsWith(f, Qt::CaseInsensitive)) {
            why = QStringLiteral("settings path %1 is the operator's real profile").arg(path);
            return false;
        }
    }
    if (!path.contains(QStringLiteral("qttest"), Qt::CaseInsensitive)) {
        why = QStringLiteral("settings path %1 is not under Qt's test-mode locations").arg(path);
        return false;
    }
    return true;
}

bool waitForSlices(MainWindow* window, int panCount)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < kSliceWaitMs) {
        int ready = 0;
        for (PanadapterApplet* applet : window->findChildren<PanadapterApplet*>()) {
            if (window->radioModel()->sliceById(applet->activeSliceIndex()) != nullptr) {
                ++ready;
            }
        }
        if (ready >= panCount) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return false;
}

int panCountForLayout(const QString& layout)
{
    if (layout == QLatin1String("1")) { return 1; }
    if (layout == QLatin1String("2v") || layout == QLatin1String("2h")) { return 2; }
    if (layout == QLatin1String("2h1") || layout == QLatin1String("12h")
        || layout == QLatin1String("3v")) {
        return 3;
    }
    if (layout == QLatin1String("2x2") || layout == QLatin1String("4v")) { return 4; }
    if (layout == QLatin1String("3h2")) { return 5; }
    return 1;
}

std::unique_ptr<MainWindow> openWindow(const QString& layout, const QSize& size)
{
    AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), layout);
    auto window = std::make_unique<MainWindow>(RemoteStationOptions{}, nullptr,
                                               MainWindow::ConnectionStartup::Deferred);
    RadioModel* model = window->radioModel();
    model->setBoardForTest(HPSDRHW::Saturn);
    model->configureStreamPool(5, 5, 192000);
    window->setAttribute(Qt::WA_DontShowOnScreen, true);
    // A window that is never ordered on screen gets a 32 px top safe-area
    // margin on macOS at QT_SCALE_FACTOR 1 (none at other factors), which
    // QWidget turns into a contents margin and an empty band above the
    // title strip. A shown, titled window has no such margin, so the
    // capture ignores it to match the app on screen.
    window->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    window->resize(size);
    window->show();
    window->resize(size);
    model->setConnectionStateForTest(ConnectionState::Connected);
    return window;
}

// The window's safe-area margins (left, top, right, bottom), which the
// capture does not let become contents margins.
QString safeAreaText(MainWindow* window)
{
    if (window == nullptr || window->windowHandle() == nullptr) {
        return QString();
    }
    const QMargins m = window->windowHandle()->safeAreaMargins();
    return QStringLiteral("%1 %2 %3 %4").arg(m.left()).arg(m.top()).arg(m.right()).arg(m.bottom());
}

// Where the menus are: the macOS global menu bar (native) or in the window.
QString menuBarText(MainWindow* window)
{
    QMenuBar* bar = window != nullptr ? window->findChild<QMenuBar*>() : nullptr;
    if (bar == nullptr) {
        return QStringLiteral("none");
    }
    return QStringLiteral("%1, %2 menus, in-window geometry %3x%4")
        .arg(bar->isNativeMenuBar() ? QStringLiteral("native") : QStringLiteral("in-window"))
        .arg(bar->actions().size()).arg(bar->width()).arg(bar->height());
}

QString spectrumBackend(MainWindow* window)
{
#ifdef NEREUS_GPU_SPECTRUM
    // QRhiWidget::rhi() is protected; the requested API is what is public.
    if (SpectrumWidget* sw = window->activeSpectrumWidget()) {
        switch (sw->api()) {
        case QRhiWidget::Api::Null:       return QStringLiteral("QRhiWidget api Null");
        case QRhiWidget::Api::OpenGL:     return QStringLiteral("QRhiWidget api OpenGL");
        case QRhiWidget::Api::Metal:      return QStringLiteral("QRhiWidget api Metal");
        case QRhiWidget::Api::Vulkan:     return QStringLiteral("QRhiWidget api Vulkan");
        case QRhiWidget::Api::Direct3D11: return QStringLiteral("QRhiWidget api Direct3D11");
        case QRhiWidget::Api::Direct3D12: return QStringLiteral("QRhiWidget api Direct3D12");
        }
    }
    return QStringLiteral("unknown");
#else
    Q_UNUSED(window);
    return QStringLiteral("QPainter (NEREUS_GPU_SPECTRUM off)");
#endif
}

QJsonObject cropJson(const CropRect& c)
{
    auto rect = [](const QRect& r) {
        return QJsonArray{r.x(), r.y(), r.width(), r.height()};
    };
    return QJsonObject{{QStringLiteral("name"), c.name},
                       {QStringLiteral("logical"), rect(c.logical)},
                       {QStringLiteral("device"), rect(c.device)}};
}

QJsonObject environmentJson(QApplication& app, MainWindow* window, const Args& args)
{
    const QFontInfo fi(app.font());
    QScreen* screen = QGuiApplication::primaryScreen();
    return QJsonObject{
        {QStringLiteral("qt_scale_factor"), qEnvironmentVariable("QT_SCALE_FACTOR")},
        {QStringLiteral("primary_screen_dpr_scaled"), screen != nullptr ? screen->devicePixelRatio() : 0.0},
        {QStringLiteral("build_tag"), BuildIdentity::buildTag()},
        {QStringLiteral("version"), QStringLiteral(NEREUSSDR_VERSION)},
        {QStringLiteral("qt_runtime"), QString::fromLatin1(qVersion())},
        {QStringLiteral("qt_compiled"), QStringLiteral(QT_VERSION_STR)},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("ignored_safe_area_margins"), safeAreaText(window)},
        {QStringLiteral("menu_bar"), menuBarText(window)},
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
        // The app sets Fusion, then its stylesheet wraps it in a
        // QStyleSheetStyle, whose own name() is empty.
        {QStringLiteral("style"), app.style() != nullptr
             ? QStringLiteral("Fusion + app stylesheet (%1)")
                   .arg(QString::fromLatin1(app.style()->metaObject()->className()))
             : QString()},
        {QStringLiteral("font"), QJsonObject{
             {QStringLiteral("family"), fi.family()},
             {QStringLiteral("point_size"), fi.pointSizeF()},
             {QStringLiteral("pixel_size"), fi.pixelSize()}}},
        {QStringLiteral("spectrum_rhi_backend"), spectrumBackend(window)},
        {QStringLiteral("window_title"), window->windowTitle()},
        {QStringLiteral("settings_file"), AppSettings::instance().filePath()},
        {QStringLiteral("requested_size"), QJsonArray{args.size.width(), args.size.height()}},
        {QStringLiteral("requested_dpr"), args.dpr},
    };
}

bool writeImages(QApplication& app, MainWindow* window, const Args& args, SceneContext& ctx,
                 const QString& sceneName)
{
    QDir().mkpath(args.out);
    const QJsonObject env = environmentJson(app, window, args);
    for (const CapturedImage& img : ctx.images) {
        const QString pngPath = QDir(args.out).filePath(img.name + QStringLiteral(".png"));
        if (!img.image.save(pngPath, "PNG")) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(pngPath));
            return false;
        }
        QFile f(pngPath);
        QByteArray sha;
        if (f.open(QIODevice::ReadOnly)) {
            sha = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
        }
        QJsonArray crops;
        for (const CropRect& c : img.crops) {
            crops.append(cropJson(c));
        }
        QJsonObject side{
            {QStringLiteral("scene"), sceneName},
            {QStringLiteral("profile"), args.profile},
            {QStringLiteral("image"), img.name + QStringLiteral(".png")},
            {QStringLiteral("kind"), img.kind},
            {QStringLiteral("logical_size"), QJsonArray{img.logicalSize.width(), img.logicalSize.height()}},
            {QStringLiteral("dpr"), img.dpr},
            {QStringLiteral("device_size"), QJsonArray{img.image.width(), img.image.height()}},
            {QStringLiteral("sha256"), QString::fromLatin1(sha)},
            {QStringLiteral("crops"), crops},
            {QStringLiteral("not_shown"), ctx.notShown},
            {QStringLiteral("unfrozen"), ctx.unfrozen},
            {QStringLiteral("frozen"), ctx.frozen},
            {QStringLiteral("stopped_timers"), ctx.stoppedTimers},
            {QStringLiteral("spectrum_checks"), ctx.checks},
            {QStringLiteral("notes"), ctx.notes},
            {QStringLiteral("provenance"), !ctx.provenance.isEmpty() ? ctx.provenance
                 : WorkspacePreview::enabled() ? QStringLiteral("proposed-look")
                                               : QStringLiteral("retained-current-app")},
            {QStringLiteral("environment"), env},
        };
        if (!ctx.fedReadings.isEmpty()) {
            side.insert(QStringLiteral("fed_readings"), ctx.fedReadings);
        }
        if (!ctx.proposedValues.isEmpty()) {
            side.insert(QStringLiteral("proposed_values"), ctx.proposedValues);
        }
        if (WorkspacePreview::enabled()) {
            side.insert(QStringLiteral("look_coverage"), lookCoverageJson());
            side.insert(QStringLiteral("unknown_literals_top30"), unknownLiteralsJson(30));
            side.insert(QStringLiteral("unknown_literals"),
                        unknownLiteralsJson(std::numeric_limits<int>::max()));
        }
        for (auto it = img.extra.constBegin(); it != img.extra.constEnd(); ++it) {
            side.insert(it.key(), it.value());
        }
        QFile sf(QDir(args.out).filePath(img.name + QStringLiteral(".json")));
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "cannot write sidecar for %s\n", qPrintable(img.name));
            return false;
        }
        sf.write(QJsonDocument(side).toJson(QJsonDocument::Indented));
        if (!img.geometry.isEmpty()) {
            QFile gf(QDir(args.out).filePath(img.name + QStringLiteral(".geometry.tsv")));
            if (!gf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                std::fprintf(stderr, "cannot write the geometry dump for %s\n", qPrintable(img.name));
                return false;
            }
            gf.write(img.geometry.join(QLatin1Char('\n')).toUtf8());
            gf.write("\n");
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    BuildIdentity::setBuildTag(QString::fromUtf8(NEREUSSDR_BUILD_TAG));
    GuiApplication app(argc, argv);

    Args args;
    QString error;
    if (!parseArgs(app.arguments(), args, error)) {
        std::fprintf(stderr, "nereus_visual_preview: %s\n", qPrintable(error));
        return kExitBadArgs;
    }
    if (args.probe) {
        QScreen* screen = QGuiApplication::primaryScreen();
        std::printf("primary_screen_dpr=%.4f\n", screen != nullptr ? screen->devicePixelRatio() : 0.0);
        std::printf("qt_scale_factor=%s\n", qPrintable(qEnvironmentVariable("QT_SCALE_FACTOR")));
        return 0;
    }

    // A look scene, or --workspace-preview, turns the workspace preview
    // switch on before anything is themed or constructed. Every other
    // scene turns it off: the app's default is on (WorkspacePreview.h), and
    // the baseline-* and dialog-* scenes show the classic look.
    const QString sceneName = args.scene;
    args.scene = baseSceneFor(sceneName);
    bool sceneWantsPreview = false;
    for (const Scene& s : fixedScenes()) {
        sceneWantsPreview = sceneWantsPreview || (s.name == args.scene && s.workspacePreview);
    }
    WorkspacePreview::setEnabledForTest(args.workspacePreview || isLookScene(sceneName)
                                        || sceneWantsPreview);

    applyAppTheme(app);
    OffScreenFilter offScreen;
    app.installEventFilter(&offScreen);

    // A scene that shows the settings path (Setup > Diagnostics > Export /
    // Import) runs on a profile with a fixed name, so its text and layout do
    // not change with the process id. One process at a time holds that name:
    // the lock is taken before the profile is emptied and released after it
    // is removed (declared before profileCleanup, so destroyed after it).
    const bool fixedProfile = args.scene.startsWith(QLatin1String(kFixedProfileScene));
    const QString profileName = fixedProfile
        ? QString::fromLatin1(kFixedProfileName)
        : QStringLiteral("visual-preview-%1").arg(QCoreApplication::applicationPid());
    std::unique_ptr<QLockFile> fixedProfileLock;
    if (fixedProfile) {
        const QDir profileDir = QFileInfo(AppSettings::resolveSettingsPath(profileName)).absoluteDir();
        QDir parent = profileDir;
        parent.cdUp();
        parent.mkpath(QStringLiteral("."));
        fixedProfileLock = std::make_unique<QLockFile>(
            parent.filePath(QString::fromLatin1(kFixedProfileName) + QStringLiteral(".lock")));
        fixedProfileLock->setStaleLockTime(0);
        if (!fixedProfileLock->tryLock(kFixedProfileLockWaitMs)) {
            std::fprintf(stderr, "nereus_visual_preview: another process holds the %s profile\n",
                         kFixedProfileName);
            return kExitUnsafeProfile;
        }
    }
    if (!Test::RemoteWindowHarness::useNamedIsolatedProfile(profileName)) {
        std::fprintf(stderr, "nereus_visual_preview: the isolated profile is not active\n");
        return kExitUnsafeProfile;
    }
    // Removes this process's profile when main returns, after the window
    // (declared later, so destroyed first) has saved into it: the settings
    // file, then the profile's own folder (the DSP asset folders the app
    // creates in it), but only a folder named for this process.
    struct ProfileCleanup {
        QString ownName;
        ~ProfileCleanup()
        {
            const QString settingsPath = AppSettings::instance().filePath();
            if (!Test::RemoteWindowHarness::removeIsolatedProfile()) {
                return;
            }
            QDir dir = QFileInfo(settingsPath).absoluteDir();
            if (dir.dirName() == ownName && dir.absolutePath().contains(QLatin1String("/.qttest/"))) {
                dir.removeRecursively();
            }
        }
    } profileCleanup{profileName};
    QString why;
    if (!profileIsSafe(why)) {
        std::fprintf(stderr, "nereus_visual_preview: %s\n", qPrintable(why));
        return kExitUnsafeProfile;
    }
    Test::markAudioFirstRunDone();

    QVector<Scene> scenes = fixedScenes();
    const bool wantsSetupPage = args.scene.startsWith(QLatin1String("dialog-setup-"));

    Scene scene;
    if (!args.list && !wantsSetupPage) {
        bool found = false;
        for (const Scene& s : scenes) {
            if (s.name == args.scene) {
                scene = s;
                found = true;
            }
        }
        if (!found) {
            std::fprintf(stderr, "nereus_visual_preview: unknown scene %s\n", qPrintable(args.scene));
            return kExitBadArgs;
        }
    }

    const QString layout = (args.list || wantsSetupPage) ? QStringLiteral("2v") : scene.panLayout;
    std::unique_ptr<MainWindow> window = openWindow(layout, args.size);
    if (!waitForSlices(window.get(), panCountForLayout(layout))) {
        std::fprintf(stderr, "nereus_visual_preview: slices did not appear on every pan\n");
        return kExitSceneFailed;
    }
    pump(kStartupSettleMs);

    if (args.list || wantsSetupPage) {
        const QVector<Scene> setup = setupPageScenes(window.get());
        if (args.list) {
            QTextStream out(stdout);
            for (const Scene& s : scenes) {
                out << s.name << (s.dialog ? "\tdialog" : "\twindow") << '\n';
            }
            for (const Scene& s : setup) {
                out << s.name << "\tdialog\n";
            }
            for (const Scene& s : scenes) {
                const QString look = lookSceneFor(s);
                if (!look.isEmpty()) {
                    out << look << (s.dialog ? "\tdialog" : "\twindow") << '\n';
                }
            }
            for (const Scene& s : setup) {
                out << lookSceneFor(s) << "\tdialog\n";
            }
            out.flush();
            window.reset();
            return 0;
        }
        bool found = false;
        for (const Scene& s : setup) {
            if (s.name == args.scene) {
                scene = s;
                found = true;
            }
        }
        if (!found) {
            std::fprintf(stderr, "nereus_visual_preview: unknown scene %s\n", qPrintable(args.scene));
            return kExitBadArgs;
        }
        pump(kStartupSettleMs);
    }

    // The window must be exactly the profile: logical client size and DPR.
    const double actualDpr = window->devicePixelRatioF();
    if (std::abs(actualDpr - args.dpr) > kDprTolerance) {
        std::fprintf(stderr,
                     "nereus_visual_preview: window DPR %.4f differs from --dpr %.4f "
                     "(QT_SCALE_FACTOR=%s)\n",
                     actualDpr, args.dpr, qPrintable(qEnvironmentVariable("QT_SCALE_FACTOR")));
        return kExitMismatch;
    }
    if (window->size() != args.size) {
        std::fprintf(stderr,
                     "nereus_visual_preview: client size %dx%d differs from --size %dx%d "
                     "(minimum %dx%d)\n",
                     window->width(), window->height(), args.size.width(), args.size.height(),
                     window->minimumSizeHint().width(), window->minimumSizeHint().height());
        return kExitMismatch;
    }

    SceneContext ctx;
    ctx.window = window.get();
    ctx.model = window->radioModel();
    ctx.size = args.size;
    ctx.dpr = args.dpr;
    ctx.crops = !scene.dialog && args.profile == QLatin1String("mac-1512x869@2");
    // No re-theme before the grab: every image shows what the live pass
    // (WorkspacePreview::installLiveTheming) made of the window.
    ctx.geometryDump = args.geometryDump;
    scene.run(ctx);
    if (!ctx.failure.isEmpty()) {
        std::fprintf(stderr, "nereus_visual_preview: %s: %s\n", qPrintable(scene.name),
                     qPrintable(ctx.failure));
        return kExitSceneFailed;
    }
    if (window->size() != args.size) {
        std::fprintf(stderr, "nereus_visual_preview: the scene resized the window to %dx%d\n",
                     window->width(), window->height());
        return kExitMismatch;
    }
    if (sceneName != scene.name) {
        renameImages(ctx, scene.name, sceneName);
    }
    if (!writeImages(app, window.get(), args, ctx, sceneName)) {
        return kExitSceneFailed;
    }
    std::printf("%s: %lld images\n", qPrintable(sceneName), static_cast<long long>(ctx.images.size()));
    window.reset();
    return 0;
}
