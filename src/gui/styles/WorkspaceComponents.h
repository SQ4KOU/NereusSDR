// src/gui/styles/WorkspaceComponents.h

// --- From src/gui/StyleConstants.h ---
// =================================================================
// src/gui/StyleConstants.h  (Longpath)
// =================================================================
//
// Source attribution (AetherSDR — GPLv3):
//
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       — per https://github.com/ten9876/AetherSDR (GPLv3; see LICENSE
//       and About dialog for the live contributor list)
//
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   Longpath is also GPLv3. Attribution follows GPLv3 §5 requirements.
//
// =================================================================
// Modification history (Longpath):
//   2026-04-16 — Ported/adapted in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//                 Theme palette imported from AetherSDR `src/gui/ComboStyle.h` /
//                 `HGauge.h` / `SliceColors.h` and inline QColor calls
//                 in `MainWindow.cpp` / `VfoWidget.cpp`.
// =================================================================

// =================================================================
// src/gui/styles/WorkspaceComponents.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/StyleConstants.h (the raised, pressed, selected and sunken
//   fills and edges of buttonBaseStyle, blueCheckedStyle, sunkenFill,
//   sliderHStyle, glassFieldStyle, insetValueStyle and kComboStyle; the
//   three-stop raisedFill; sliderHStyle, sliderVStyle and kSliderStyle
//   as they are, geometry included; the hover, checked and radius rules
//   of kCheckBoxStyle and kRadioButtonStyle; the roles of
//   greenCheckedStyle, dspToggleStyle and amberCheckedStyle) at
//   revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banner is copied above.
//
//   The upstream file carries the AetherSDR copyright and GPLv3
//   notice reproduced above. Longpath's project LICENSE is GPLv3.
//
//   Only the colour values and fills are ported, and the slider's
//   geometry. Longpath's padding and font sizes are not, nor its check box
//   row height (min-height 20 px): the workspace preview keeps today's
//   sizes. The slider is the exception JJ chose (2026-10-08): Longpath's
//   6 px groove and 12x16 handle as they are. The radius rule is JJ's
//   choice (2026-10-08): Longpath's 7 px on its 26 px field, scaled to
//   the control's height, round(h x 7 / 26). The check box off state
//   (brighter rim and fill, the disabled states), the raised green and
//   the raised amber of the lit buttons are JJ's choices from Longpath's
//   roles, not Longpath's sheets.
//
//   componentize() is NereusSDR-original: it rewrites the declarations
//   of a stylesheet in place, by control class and state, so the
//   result keeps the original selectors and their specificity.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - The licence note corrected.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - tableRules(): the grid and header colours of Longpath's
//                 tableStyle(). J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - generatedColours(): the colours componentize() writes.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - JJ's decided controls: raisedFillOf(), indicatorRules(),
//                 the radius round(h x 7 / 26).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - groupBoxIndicatorRules() and sliderRulesFor() for the
//                 live pass; componentize() takes the remaining dialog and
//                 menu colours. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-09 - kSliderHandleSpanPx: no slider thinner than the decided
//                 handle. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-10 - sliderRulesFor() draws JJ's D29 track and its disabled
//                 state. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-10 - progressChunkRules(): a progress bar's filled part is
//                 the selection glass. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#pragma once

#include <QString>
#include <QStringList>

class QColor;
class QWidget;

namespace NereusSDR::Workspace::Components {

// Raised: light top, dark bottom (buttons, selectors, tabs).
QString raisedFill();
QString raisedHoverFill();
// Longpath's three-stop raisedFill of `base`: lifted 16 at the top, the
// base at 55 %, dropped 12 at the bottom (the lit green and amber).
QString raisedFillOf(const QColor& base);
// Pressed: the raised fill flipped.
QString pressedFill();
// Selected: the glass gradient.
QString glassFill();
// Sunken: dark top, lighter bottom (fields, value chips, grooves).
QString sunkenFill(const char* baseHex, int deepen, int lift);
QString fieldFill();

// The radius for a control of the given height in device-independent
// pixels: Longpath's 7 px on its 26 px field, scaled, round(h x 7 / 26).
int radiusFor(int heightPx);

// Rules for check box and radio indicators in the workspace look: the
// off state, hover, checked, disabled off and disabled on. Colours and
// the radius only, so an indicator keeps the platform style's size. The
// live pass gives them to each box no sheet styles the indicator of
// (WorkspacePreview::themeSubtree), with padding that keeps its size.
QString indicatorRulesFor(bool radio);
// Both classes' rules.
QString indicatorRules();

// The same rules for a checkable group box's indicator, whose outer size
// is the platform style's `widthPx` x `heightPx`: the rim comes off it.
QString groupBoxIndicatorRules(int widthPx, int heightPx);

// Longpath's slider (the decided D5 handle, hover and pressed, JJ's D29
// track and disabled track, no filled side) for a slider no sheet styles. The live pass
// gives them to each such slider with what keeps its size.
QString sliderRulesFor(bool vertical);
// The filled part of a progress bar no sheet styles: the selection glass
// (settled 2026-10-10). The groove stays the platform style's.
QString progressChunkRules();

// The decided handle's span across the slider (14 px plus a 1 px rim on
// each side). A slider thinner than this cuts the handle off, so the look
// never lets a slider be thinner (the live pass gives such a slider a
// min-height or min-width; WorkspacePreview.cpp sliderFit()).
inline constexpr int kSliderHandleSpanPx = 16;

// Application-wide rules for tables and header sections in the
// workspace look: grid lines, the line under the titles and the edges
// between sections, from the workspace roles. Colours only, plus the
// header's horizontal padding.
QString tableRules();

// Rewrites the background, border colours, text colour and radius of
// every control block in `qss` (already translated to palette values)
// into the raised, pressed, selected, sunken or disabled treatment.
// Selectors, widths, paddings, fonts and sizes are kept, so geometry
// does not change, except a slider's: its groove and handle take
// Longpath's sizes, and no side of the groove is filled. Check box and
// radio indicators take the decided states. A checked button in today's
// green or amber takes the raised lit form of that meaning. Other
// semantic colours (red, slice colours) and translucent or transparent
// backgrounds are kept. `owner` is the
// widget the sheet is set on (may be null); it resolves selector-less
// and id-only blocks. Applying it to its own output changes nothing.
QString componentize(const QString& qss, const QWidget* owner);

// Every colour componentize() writes that is not a palette value (the
// shifted gradient stops, the raised hover pair, the glass edges), as
// QssTranslator reports a literal. A sheet the program copies from an
// already themed widget brings them back to the translator; the preview
// leaves them out of its unknown-literal count.
QStringList generatedColours();

} // namespace NereusSDR::Workspace::Components
