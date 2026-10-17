// =================================================================
// src/gui/styles/WorkspacePreview.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. The workspace preview switch and
// the scoped stylesheet pass used to preview the Longpath look.
//
// The switch is on by default. It is off only when the environment
// variable NEREUS_WORKSPACE_PREVIEW is "0" at the first call to enabled(),
// or after setEnabledForTest(false). Unset, empty and "1" are on; any
// other value is treated as unset, so it is on too
// (enabledFromEnvironmentValue() is the rule). There is no settings key
// and no UI. Every visual change of the workspace look branches on
// enabled(); with the switch off the application has the classic look.
//
// themeSubtree() is preview scaffolding, not the final migration: it
// rewrites the retained stylesheets of a widget subtree through
// QssTranslator. The final design migrates call sites one by one (LT
// "Theme interfaces and migration").
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Written by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - passInputThrough(): the preview's overlays let clicks
//                 through when they are native views, by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - installLiveTheming(): one application-wide pass that
//                 themes every widget when it is shown, so no window,
//                 page, menu or popup needs a call site. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - roleColour() and roleColourName(): the one helper for
//                 chrome colours that never pass through a stylesheet.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - StatusTones and DataColours: the status and data colour
//                 choices, statusColour(), dataBackground() and
//                 meterInk(), each through roleColour(). J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - JJ's decisions (B, B) are the product: the choice seams,
//                 the unchosen branches and meterInk() are gone;
//                 dataBackground() shows a container background the user
//                 set (kUserBackgroundProperty). J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-09 - kFilledSliderProperty and kVisibleTrackProperty: the
//                 speaker volume fill and the visible zoom track (D24).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - kVisibleTrackProperty describes JJ's D27 track.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10 - kVisibleTrackProperty: the rim is JJ's D29 rim.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10 - kLitWhenOnProperty and kChoiceProperty: OPERATE on is
//                 the lit green (JJ's D30) and the tuner's chosen antenna
//                 the selection glass (D31), on buttons that stay plain
//                 push buttons. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-10 - installLiveTheming() also installs the look's style
//                 object. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - kThemedIslandProperty: the slice flag takes the look
//                 under the skipped SpectrumWidget. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-12 - The switch is on unless NEREUS_WORKSPACE_PREVIEW is "0";
//                 enabledFromEnvironmentValue() is the rule. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Shared 11 px text floor, J.J. Boyd (KG4VCF),
//                 AI-assisted via OpenAI Codex.
// =================================================================

#pragma once

#include <QColor>
#include <QHash>
#include <QFont>
#include <QString>

class QApplication;
class QObject;
class QWidget;
class QPaintDevice;

namespace NereusSDR::WorkspacePreview {

// JJ's option B (2026-10-12): an 11 logical-pixel text floor.
// Point fonts are compared using Qt's logical font DPI, without device
// pixel ratio scaling. Fonts at or above the floor are returned exactly;
// smaller fonts retain their other attributes and become pixel-sized.
// Both helpers return the original value with the classic look.
QFont readableFont(const QFont& font);
// Painter callsites pass their actual device to compare point sizes at
// that device's logical DPI (e.g. a 96 DPI image versus a 72 DPI widget).
QFont readableFont(const QFont& font, const QPaintDevice* device);
int readableTextPixelSize(int pixels);

bool enabled();
void setEnabledForTest(bool on);
// The rule enabled() applies, once, to the value of
// NEREUS_WORKSPACE_PREVIEW: false only for "0". A null or empty value (the
// variable unset or empty), "1" and any other value are true. It reads
// nothing, so a test can cover it without the process environment.
bool enabledFromEnvironmentValue(const QString& value);

// Dynamic property names used by themeSubtree().
inline constexpr auto kOriginalQssProperty = "_nereusWorkspaceOriginalQss";
inline constexpr auto kThemedQssProperty = "_nereusWorkspaceThemedQss";
// Set to true on a widget to keep the scoped pass out of its subtree.
inline constexpr auto kSkipProperty = "nereusWorkspaceSkip";
// Set to true by the owner on a widget that takes the look although an
// ancestor is skipped (the slice flag, its floating buttons and its folded
// frequency label, all children of the skipped SpectrumWidget; JJ,
// 2026-10-11). The live pass themes it and what it holds; a skipped class
// or kSkipProperty inside it is still left alone.
inline constexpr auto kThemedIslandProperty = "nereusWorkspaceThemedIsland";
// Set to true by the owner on a slider whose filled side carries meaning
// (the PC and RADIO speaker volume, JJ's D24): the workspace look keeps
// the filled side from the minimum to the handle, today's colour through
// the translation, on the decided groove and handle.
inline constexpr auto kFilledSliderProperty = "nereusWorkspaceFilledSlider";
// Set to true by the owner on a slider whose track must read on its own
// (the zoom bar under the pans, JJ's D27): the decided groove stays dark,
// 8 px overall, with the D29 text-inactive rim on every side.
inline constexpr auto kVisibleTrackProperty = "nereusWorkspaceVisibleTrack";
// Set to true by the owner on a push button that is not checkable and
// shows "on" by the sheet it is given (OPERATE on the amplifier, tuner and
// RF2K-S applets, JJ's D30): while its sheet is today's engaged green the
// workspace look draws the raised lit green of a checked processing
// button (D15), under the pointer too.
inline constexpr auto kLitWhenOnProperty = "nereusWorkspaceLitWhenOn";
// Set to true by the owner on a push button that is one of a set of
// choices, is not checkable and shows "chosen" by the sheet it is given
// (the tuner's ANT buttons, JJ's D31): while its sheet is today's engaged
// green the workspace look draws the blue selection glass. Clicking is
// unchanged: the button only asks, and the sheet follows the device.
inline constexpr auto kChoiceProperty = "nereusWorkspaceChoice";

// Rewrites every stylesheet under `root` (root included) from its
// retained original. The first time a widget is seen, or when its
// stylesheet changed since the last pass, the current stylesheet is
// kept as the original. Skips the slice flag's S-meter bar (VfoLevelBar),
// SpectrumWidget's subtree, color pickers that show a user color, and
// anything carrying kSkipProperty. Does nothing when the
// switch is off.
void themeSubtree(QWidget* root);

// Installs a filter that runs themeSubtree(dialog) each time the dialog
// is shown. Does nothing when the switch is off.
void themeOnShow(QWidget* dialog);

// Installs the live pass on the application: every widget gets the
// scoped pass of themeSubtree() (translation and component treatment,
// the same skip list and kSkipProperty) when it is shown, and again,
// once, when a shown widget sets a new stylesheet. A window (a dialog,
// menu, popup, tooltip or toast) is its own surface: a skipped widget
// keeps the pass out of its own window only, not out of a menu or popup
// it opens. A widget whose stylesheet has not changed since its last
// pass is not translated again. Installs once; does nothing when the
// switch is off. applyWorkspacePalette() calls it. It also makes
// Workspace::LookStyle the application's style (WorkspaceLookStyle.h): the
// default button's rim and the current cell's outline.
void installLiveTheming(QApplication& app);

// The workspace look of one stylesheet: QssTranslator (with the
// context rule for #203040) followed by the component treatment
// (raised, pressed, selected, sunken). `owner` is the widget the sheet
// is for, or null. Returns `original` unchanged when the switch is off,
// so a shared style builder can call it unconditionally.
QString lookQss(const QString& original, const QWidget* owner = nullptr);

// The colour of a chrome site that never passes through a stylesheet:
// QPainter colours, Qt global colours, per-widget palettes. With the
// switch off it returns `legacy` unchanged and looks nothing up. With it
// on it returns the workspace role `role` carrying the alpha of `legacy`
// (a translucent legacy fill stays translucent), or `legacy` itself when
// the palette has no such role or `legacy` is invalid. Cheap enough to
// call on every paint: one flag test off, one hash lookup on.
QColor roleColour(const QColor& legacy, const char* role);

// roleColour() for rich text, where the colour is written into HTML.
// With the switch off it returns `legacy` exactly as written, so the
// HTML is byte for byte what it was; with it on, roleColour()'s colour
// as "#rrggbb" ("#aarrggbb" when translucent). Call it where the HTML
// is built, so a later setText() writes the role colour again.
QString roleColourName(const char* legacy, const char* role);

// The status and data colours JJ decided on 2026-10-08 (choice B of each
// colour-choice sheet). Both act only with the switch on.
//
// Status indicators (state dots, latency and rate numbers, status words)
// take Longpath's brighter tones: ok -> live (kLiveGreen), warn ->
// measured (kAmberText), danger -> tx (kTxRed); any other role is
// roleColour()'s. `legacy` with the switch off.
QColor statusColour(const QColor& legacy, const char* role);
// statusColour() for rich text; byte for byte `legacy` with the switch
// off.
QString statusColourName(const char* legacy, const char* role);

// The DEFAULT background of a data surface (spectrum, waterfall, meters,
// graphs): `legacy` with the switch off. With it on, the background of
// the container around `surface` when the user has set one (the nearest
// widget ancestor carrying kUserBackgroundProperty; `legacy`'s alpha is
// kept when it is translucent), otherwise roleColour(legacy, role), the
// role of the surrounding container. `surface` is the widget, or an object
// owned by it (a meter item), or null. Ink is never routed here. Never call it with a
// colour the user set on the surface itself: user colours always win.
QColor dataBackground(const QColor& legacy, const char* role, const QObject* surface = nullptr);

// Set by ContainerWidget on itself: the background colour the user chose
// for the container, or an invalid QColor when it has the default one.
// dataBackground() reads the nearest ancestor's.
inline constexpr auto kUserBackgroundProperty = "nereusUserBackground";

// Unknown colour literals met by themeSubtree() since the last reset,
// lower case, with their counts.
QHash<QString, int> unknownLiteralCounts();
void resetUnknownLiteralCounts();

// Makes a drawn-over overlay (an outline, a shadow, drop bands) let every
// click through to what is under it, also when it is a native view. A
// Metal leaf (SpectrumWidget, MeterWidget) promotes its siblings to native
// views and they promote their ancestors, so on macOS the preview's
// overlays are native child views. AppKit hit-tests native views and
// passes a click through one only when its window carries
// Qt::WindowTransparentForInput; WA_TransparentForMouseEvents alone acts
// only inside Qt's own routing. Sets both, on the widget (carried into any
// native window created or re-created later) and on an existing window.
void passInputThrough(QWidget* widget);

} // namespace NereusSDR::WorkspacePreview
