# UI Audit & Polish + TX Bandwidth + Profile-Awareness — Design

**Status:** Design approved · ready for implementation plan
**Scope:** Single combined design covering three intertwined initiatives from the 2026-05-01 session: (1) UI audit and polish across right-panel applets, modal dialogs, setup pages, and the spectrum overlay; (2) TX bandwidth filtering as a per-profile field with full profile integration; (3) cross-cutting profile-awareness UI cues across every surface that hosts profile-bundled controls.
**Authoritative inventory:** `.superpowers/brainstorm/33882-1777661197/inventory-snapshot.md` (5 parallel agent reports compiled into one snapshot — kept in the brainstorm workspace; key findings copied into this doc as needed).

---

## Motivation

The right-side applet panel and surrounding live-UI surfaces have accumulated three classes of issues that compound visually and functionally:

1. **Stylesheet drift.** A canonical palette exists at `src/gui/StyleConstants.h` (two intentional sub-palettes — applet-side at lines 32–191, setup-side at lines 197–244), but ~14 files have grown their own file-local style constants that duplicate and drift from the canonical values. Four files contain byte-for-byte copies of an `applyDarkStyle()` helper that drifted to a different border colour and padding. The `SetupPage` base class itself pastes byte-for-byte duplicates of `Style::kComboStyle` / `kSliderStyle` / `kButtonStyle` / `kLineEditStyle` inline. Net: same visual element renders three different ways across pages.
2. **Cross-surface duplication.** ~20 model setters fire from 2+ live-UI surfaces. Some duplications are deliberate (the user explicitly wants RIT in both `RxApplet` and `VfoWidget`) but the *idioms* drifted — same setter, two visually inconsistent controls. Worst cases: AGC mode (4 modes in `RxApplet`, 5 in `VfoWidget`, 6 in setup); AGC-T slider (one inverted, one not); 3 antenna pickers in 3 idioms. The Display flyout in `SpectrumOverlayPanel` emits signals that no one in `MainWindow` is connected to — broken UI hidden behind a working button.
3. **NYI clutter.** Of 13 right-panel applets, 9 are entirely placeholder-only. `TxApplet` has ~7 NYI buttons in 22 controls. `RxApplet` has 4 NYI placeholders in the audio section (Pan, Mute, SQL toggle, SQL slider). Users see controls that look real but do nothing.

The user's stated goal: "polishing the right side applet — overlap, redundant buttons, scroll bar overlaps and clips items on the right, inventory and discuss and clean this up. I want to know what endpoints are needed which are redundant. This is not a hand wave — this is a deep review and polish."

This design captures every decision made during the 2026-05-01 brainstorm walk-through. 14 topics across 4 phases. All locked.

---

## PR strategy

**One big PR.** User-locked. Theme: "UI audit & polish — Phase 1." Approximately 80–100 unique files touched; many overlap (e.g. `RxApplet.cpp` is touched by A2 + B1 + B2 + B3 + B4 + B6 + B7).

Trade-offs accepted:
- Heavy single review. Mitigated by structuring the PR into commits per topic so reviewers can read changes thematically.
- One blocked topic (e.g. SWR Prot LED data not flowing) could stall the whole PR. Mitigation: any blocked topic is a single revert without affecting others; the work is parallel, not sequential.

---

## Decisions

### Phase A — Foundation (mechanical, low-risk)

#### A1. Scrollbar gutter — reserve 8 px always

**Problem.** `AppletPanelWidget` (`src/gui/applets/AppletPanelWidget.cpp:48-83`) has zero right-margin on its root and stack layouts. The 8 px vertical scrollbar lives inside the `QScrollArea` viewport. When the bar is shown (always, in practice — 13 applets stacked), the rightmost 8 px of every applet's content gets overlaid: value boxes ("72", "-90"), button borders, slider handles all clip.

**Decision.** Set `m_stackLayout->setContentsMargins(0, 0, 8, 0)` so the scrollbar always has its own gutter. Predictable layout regardless of whether the bar is visible. One-line change.

**Trade-off.** 8 px less usable width when the bar isn't shown — almost never the case with this many applets. The 8 px we "give up" was being clipped anyway.

**Rejected alternatives.**
- *Dynamic reserve* (margins toggle on bar visibility): visible 8 px jump on layout change; risk of layout-update loops.
- *Overlay/floating scrollbar* (AetherSDR style): in Qt with custom QSS, "overlay" still consumes pixels — clicks on right-edge widgets become awkward when bar is shown.
- *External scrollbar*: non-idiomatic Qt; manual sync of value/range/page-step.

#### A2. Style consolidation — pragmatic palette migration

**Problem.** Three style systems coexist:

1. The canonical applet-side helpers + constants at `StyleConstants.h:32-191`.
2. The canonical setup-side flat constants at `StyleConstants.h:197-244` (different font sizes / paddings, intentional).
3. ~14 files with **file-local** style constant blocks duplicating canonical values.

Top duplicate: raw `"#1a2a3a"` (= `kButtonBg`) appears as a string literal in 11 files. Same drift across `#c8d8e8`/`#0f0f1a`/`#205070`/`#203040`/`#00b4d8`/`#0a0a18`/`#1e2e3e`/`#8aa8c0`/`#006040`/`#00ff88`/`#00a060`/`#0070c0`/`#0090e0`/`#204060`/`#8090a0`/`#405060`.

Files with their own local blocks:
- Applets: `PhoneCwApplet.cpp` (8 constants), `EqApplet.cpp` (4), `FmApplet.cpp` (4), `VaxApplet.cpp` (5)
- Widgets: `VfoWidget.cpp` (4), `SpectrumOverlayPanel.cpp` (7+), `TitleBar.cpp` (3)
- Setup: 4 byte-for-byte copies of `applyDarkStyle()` in `TransmitSetupPages.cpp:118-151`, `DisplaySetupPages.cpp:98-128`, `AppearanceSetupPages.cpp:19-50`, `GeneralOptionsPage.cpp:79-101` — all drifted to `#203040` borders (vs `kBorder` `#205070`) and `padding-top: 4px` (vs `12px`)
- Modal dialogs: `AddCustomRadioDialog.cpp` (4 constants), `NetworkDiagnosticsDialog.cpp` (3), `SupportDialog.cpp` (per-widget inline), partial in `AboutDialog.cpp`
- Base class: `SetupPage.cpp:181-296` pastes byte-for-byte duplicates of `Style::kComboStyle`/`kSliderStyle`/`kButtonStyle`/`kLineEditStyle` inline in `addLabeledX()` helpers

Plus `Style::sliderVStyle()` at `StyleConstants.h:161-172` has zero callers (`EqApplet` defined its own `kVSliderStyle` instead).

**Decision.** Pragmatic migration:
- Replace raw hex literals matching named `Style::k*` constants with the constant.
- Replace local-constant blocks where colours are duplicates of canonical helpers.
- Promote 2-3 missing palette colours: add `Style::kLabelMid` (`#8899aa`, used 5+ places — VfoWidget AGC-T label, pan label, RxApplet AGC-T label) and a `Style::dspToggleStyle()` helper using `#1a6030` background / `#20a040` border (used in `VfoWidget` DSP toggles + `SpectrumOverlayPanel` DSP toggle styling — semantically distinct from `kGreenBg` action buttons).
- Snap one-offs: `#1a1a2a` → `kDisabledBg`, `#607080` → `kTextScale`, `#0a0a14` → `kStatusBarBg` (already exists at line 104).
- Wire dead `Style::sliderVStyle()` from `EqApplet.cpp` (replaces its local `kVSliderStyle`).
- Allow tightly-scoped local blocks for legitimate exceptions (translucent `rgba()` overlays in `SpectrumOverlayPanel` for over-spectrum compositing) but with comment justifying why local.

Defer per-control colour decisions on these one-offs — flag them when their B-series topic is hit:
- `#001a33` / `#3399ff` (TxApplet MON button) — flagged in C1 review
- `#4488ff` (RxApplet lock + RX antenna) — flagged in B7 + B3 reviews
- `#3a2a00` / `#806020` (TitleBar feature-request lightbulb) — keep as one-off amber-dark unless promoted

**Touched files.** ~50.

**Rejected alternatives.**
- *Strict* (kill all local blocks): too sweeping; risks regressions in legitimate exception cases.
- *Namespace-only* (rename local constants under per-widget namespaces): doesn't unify, just organizes.
- *Phased* (top-6 offenders only): leaves codebase in inconsistent state; project history shows partial migrations drag.

#### A3. SetupDialog regime unification — kill 4 `applyDarkStyle()` copies + fix base-class helpers

**Problem.** 47 setup pages live across three style regimes: ~30 use the `SetupPage::addLabeledX()` helpers (which paste byte-for-byte duplicates of `Style::kComboStyle` etc. inline at `SetupPage.cpp:181-296`); ~15 use one of four byte-for-byte copies of `applyDarkStyle()`; ~5-6 use the canonical `Style::k*Style` constants directly. Visible drift between Display/Transmit/Appearance/GeneralOptions pages and CAT/Keyboard/Diagnostics pages.

**Decision.** Two mechanical fixes:
1. Change `SetupPage::addLabeledCombo`, `addLabeledSlider`, `addLabeledButton`, `addLabeledEdit` to call `Style::kComboStyle` / `kSliderStyle` / `kButtonStyle` / `kLineEditStyle` instead of pasting strings.
2. Add `Style::applyDarkPageStyle(QWidget*)` to `StyleConstants.h` using canonical `Style::kBorder` (`#205070`) and `padding-top: 12px`. Replace the 4 file-local copies (`TransmitSetupPages.cpp:118-151`, `DisplaySetupPages.cpp:98-128`, `AppearanceSetupPages.cpp:19-50`, `GeneralOptionsPage.cpp:79-101`) with calls to the new shared helper.

**Architecture untouched.** `SetupPage`-helper-based pages and `applyDarkStyle()`-based pages keep their existing layout strategies — only the styling source of truth changes.

**Touched files.** ~10.

**Rejected alternatives.**
- *Migrate `applyDarkStyle()` pages to `SetupPage` helpers*: those pages were built around `applyDarkStyle()` covering everything in one shot; rewiring to per-control helpers means redoing layout code. Out of scope for this PR.
- *Eliminate per-page styling, rely on dialog-level QSS cascade*: Qt's QSS cascade has known quirks for `QGroupBox` internals, `QSpinBox` arrows, `QComboBox` dropdowns. Risk of subtle visual regressions.

---

### Phase B — Cross-surface ownership

#### B1. AGC family

**Decisions:**
- **Mode set in live UI: 5 modes** (Off / Long / Slow / Med / Fast). Sync `RxApplet` to add Long. Setup keeps Custom (a sixth setup-only mode that surfaces saved Custom parameters; live UI just renders the Custom name when active).
- **Idiom per surface** — different is fine. Combo in `RxApplet` (compact column) and Setup; button row in `VfoWidget` (high-frequency one-click). 11+ modes-as-buttons would not fit, but 5 do for the Mode tab; staying with the existing per-context idiom.
- **AGC-T slider direction** — match Thetis. Verify at the bench during implementation by comparing with Thetis's main-panel AGC-T slider; apply the same direction to both `RxApplet` and `VfoWidget`. (One of them is currently inverted; we don't yet know which is the Thetis-canonical direction.)
- **AUTO badge** — keep on both live surfaces. Identical `autoAgcToggled` semantics. Setup checkbox stays as the persistent config.

**Touched files.** ~3-5 (`RxApplet`, `VfoWidget`, possibly `DspSetupPages` if mode count needs tweaking; verify `SliceModel::AGCMode` enum has all 5).

**Bench verification.** Required for slider direction.

#### B2. Mode + Filter family

**Decisions:**
- **Mode list: 11 modes** — USB / LSB / DSB / CWU / CWL / FM / AM / SAM / DIGU / DIGL / DRM. Sync `VfoWidget` to add DSB + DRM (currently 9 modes). `RxApplet` already matches. Skip Thetis's SPEC mode (not implemented in NereusSDR yet).
- **Mode idiom** — combo in both live surfaces (11 modes too many for a button row in 252 px). MainWindow Mode menu keeps `QAction`s for keyboard access.
- **Filter preset values** — one canonical list per mode, sourced from `SliceModel::initFilterPresets()` (already a Thetis port at `SliceModel.cpp:161` per the existing `// From Thetis console.cs:5180-5575 — InitFilterPresets, F5 per mode` cite).
- **Filter preset count per surface** — `RxApplet` shows all presets per mode (current 10-button 2×5 grid flexes if the mode has fewer); `VfoWidget` Mode tab shows 5-6 most-common subset. Both surfaces draw values from the same canonical list. The "5-6 most common" subset gets defined per mode in code by referencing Thetis main-panel filter buttons (not the setup-page list).
- **FilterPassband drag widget** — `RxApplet` only. Right panel = detail; flag = quick.
- **Filter width label** ("2.9K") — keep in both (`VfoWidget` header + `RxApplet` body). Status display only. Mirrors are fine.
- **Step controls** — keep both as-is. `RxApplet` ± triangles for fine-grained set; `VfoWidget` cycle button for quick swap. Same setter, different needs.

**Touched files.** ~3-5.

#### B3. Antenna family

**Decisions:**
- **RX/TX antenna pickers in `RxApplet`** — kept (user override; original proposal was to drop them). Same applies to `VfoWidget` header, `SpectrumOverlayPanel` ANT flyout, and Setup → Antenna Control. Four surfaces total (3 live + setup).
- **EXT1 / EXT2 in live UI popups** — added. Currently only on Setup → Antenna Control.
- **XVTR in live UI popups** — listed (active when band = XVTR; greyed otherwise).
- **"RX out on TX" (BYPS)** in popup as an entry, in addition to flag's BYPS button.
- **SpectrumOverlayPanel ANT flyout** — combo also expands to include EXT1/EXT2/XVTR/BYPS for consistency.
- **Idiom per surface** — keep different per context: popup-menu button on flag and `RxApplet`; combo on overlay flyout; per-band grid on setup.
- **Implementation:** introduce a new shared `AntennaPopupBuilder` (utility, not a widget) that reads `BoardCapabilities` + `SkuUiProfile` and returns a populated `QMenu` (or list of `QAction`s). Both `VfoWidget`'s antenna popup-button and `RxApplet`'s antenna popup-button construct via this helper. Modes: "RX" or "TX" passed in to filter (TX-mode shows ANT1-3 + XVTR only; no RX-only items).
- **Capability tiers (per shipped 3P-I-a, 3P-I-b):**
  - Full Alex (ANAN-G2 / 7000DLE / 100D): 7 items in 3 sections.
  - Mid Alex (ANAN-7000 / older HPSDR): subset based on `hasExt1` / `hasExt2` / bypass-relay flags.
  - HL2 / Atlas / no-Alex: antenna UI hidden entirely (existing behaviour).

**Touched files.** ~5 (3 callers + 2 new — `AntennaPopupBuilder.{h,cpp}`).

#### B4. Audio family

**Decisions:**
- **AF gain** — drop from `RxApplet` (3 locations today: `RxApplet`, `VfoWidget` Audio tab, TitleBar `MasterOutputWidget`). Keep `VfoWidget` AF (per-slice) and TitleBar master (system master via `AudioEngine::setVolume`). 3 → 2 surfaces.
- **Pan** — keep in both `RxApplet` + `VfoWidget`. Wire `RxApplet`'s NYI placeholder at line 502 to `SliceModel::setAudioPan` (model setter exists; UI binding missing).
- **Mute** — keep in both. Wire `RxApplet`'s NYI at line 457 to `SliceModel::setMuted`.
- **SQL** (toggle + threshold slider) — keep in both. Wire `RxApplet`'s NYI at lines 525, 529 to `SliceModel::setSquelchEnabled` / `setSquelchThreshold`.
- **BIN** (Binaural) — `VfoWidget` Audio tab + MainWindow DSP menu. No change. Don't add to `RxApplet`.

**Touched files.** ~2 (`RxApplet.cpp` for AF removal + Pan/Mute/SQL wire-up; signal connects in `MainWindow` if not already routed).

**Note on scope.** Wiring NYI placeholders adds ~150 lines of UI binding to a "polish" PR. Justified because (a) leaving them as visible-but-broken NYI is the worst UX, and (b) removing them reverses the B3 "keep both editors" pattern. The model setters already exist; this is mechanical UI binding.

#### B5. DSP toggles — no changes

**Decision.** The DSP family (NR1-4 / DFNR / MNR / ANF / NB cycle / SNB / APF + tune slider) is the cleanest cross-surface story in the audit. `VfoWidget` DSP tab is the purpose-built primary editor (~140 px of vertical space, dense but legible). `MainWindow` DSP menu mirrors via `QActionGroup` for keyboard access. Setup → NR/ANF (7 sub-tabs) + NB/SNB pages give deep config. `RxDashboard` (status bar) shows active NR/NB/APF/SQL/Mode/Filter/AGC badges — at-a-glance status surface.

Adding DSP toggles to `RxApplet` would duplicate 11+ controls in the right panel for ~30 px gain and no functional improvement (`RxDashboard` already does at-a-glance status). MNR stays macOS-only.

**Touched files.** 0.

#### B6. RIT / XIT family — wire up XIT

**Decisions:**
- **RIT** — both surfaces stay. Different idioms preserved (± step triangles in `RxApplet`, scrollable label in `VfoWidget`'s X/RIT tab). These are deliberate, not drift — they serve different operating styles.
- **XIT** — wire it up. Originally I recommended removing the NYI placeholders until TX phase; the user pointed out 3M-1 SSB TX has shipped (PR #152) and asked why XIT can't be wired. Investigation found:
  - `SliceModel::xitEnabled` + `xitHz` properties + setters + signals + persistence: present (`SliceModel.h:190-191`, `:434-437`, `:676-677`; `SliceModel.cpp:496-509`, `:1079-1080`, `:1292-1295`).
  - `RadioModel::updateShiftFrequency` lambda applies `slice->ritHz()` for RX shift (`RadioModel.cpp:3084`) and connects `ritHzChanged` (`:3095`).
  - `RadioModel` connects `setTxFrequency` at two sites (`:2498`, `:3190`) sending raw `freqHz`. **No XIT offset applied.**
  - `P1RadioConnection::setTxFrequency` and `P2RadioConnection::setTxFrequency` both exist and accept any `quint64`.
  - `RxApplet` (`:782-801`) and `VfoWidget` X/RIT tab (`:1310-1330`) have NYI XIT placeholders — UI exists, just not wired.

**Implementation pattern (mirror of RIT):**
1. `RadioModel.cpp:2498` and `:3190` — apply `slice->xitHz()` offset before `setTxFrequency`. ~2 lines each.
2. `RadioModel.cpp` — add `connect(slice, &SliceModel::xitEnabledChanged, this, updateTxFrequency)` and `connect(slice, &SliceModel::xitHzChanged, this, updateTxFrequency)` near line 3095. Define `updateTxFrequency` lambda parallel to the RIT one. ~10 lines.
3. `RxApplet.cpp:782-801` — wire toggle/triangles/zero/label to `SliceModel::setXitEnabled` / `setXitHz`. ~30 lines.
4. `VfoWidget.cpp:1310-1330` — wire scrollable label + toggle + zero. ~20 lines.

**Touched files.** ~3 (`RadioModel.cpp`, `RxApplet.cpp`, `VfoWidget.cpp`).

**Bench verification.** Required:
- TX NCO actually shifts when XIT engaged (visible in spectrum, audible on receiving station).
- Split (RIT + XIT both engaged) — both offsets apply correctly without interaction.
- Persistence round-trip.
- HL2 separately if relevant (HL2 ATT/filter audit is open per CLAUDE.md but XIT doesn't touch ATT/filter logic).

#### B7. Lock — drop X/RIT tab Lock

**Problem.** Three lock toggles for one boolean property (`SliceModel::setLocked`):
1. `RxApplet` header (next to slice badge) — always visible if right panel shown.
2. `VfoWidget` X/RIT tab Lock button (`VfoWidget.cpp:1344`) — only visible when X/RIT tab is the selected tab.
3. `VfoWidget` Close-strip floating button (`VfoWidget.h:615`, the × / 🔓 / ● / ▶ row) — always visible when flag is shown.

Two of these are inside `VfoWidget` itself, and one of them (the X/RIT tab Lock) requires a tab switch to reach.

**Decision.** Drop the X/RIT tab Lock button (`VfoWidget.cpp:1344`). Keep the other two. Worst-positioned of the three, redundant within the same widget (Close-strip Lock is right there always-visible). ~5-10 lines removed.

**Touched files.** 1 (`VfoWidget.cpp`).

#### B8. Spectrum display controls (bonus topic — added during walkthrough)

**Problem.** `SpectrumOverlayPanel`'s Display flyout emits signals (`wfColorGainChanged`, `wfBlackLevelChanged`, `colorSchemeChanged`, `fillAlphaChanged`) that **no one in `MainWindow` is connected to**. Most controls in the flyout do nothing. Meanwhile, `SpectrumOverlayMenu` (the right-click menu, opened from `SpectrumWidget::mousePressEvent:2824-2851`) IS wired and works — it edits the same backing fields (`m_wfColorGain`, `m_wfBlackLevel`, `m_fillAlpha`, etc.) directly inside the spectrum widget. Two surfaces with overlapping control sets; only one functional.

Plus 4 NYI buttons in the overlay strip (`+RX`, `+TNF`, `ATT`, `MNF`) and a dead "More Display Options →" link.

**Decisions:**
1. Wire the orphaned signals (`wfColorGainChanged`, `wfBlackLevelChanged`, `colorSchemeChanged`) — connect `SpectrumOverlayPanel`'s signals to `SpectrumWidget` slots in `MainWindow`. Mirror the right-click menu's connect pattern. ~3 lines.
2. Wire **Fill Alpha** + **Grid Lines** + **Pan Fill** — `SpectrumWidget::setFillAlpha` exists; may need `setGridVisible(bool)` if not present; `panFillChanged` is already in `SpectrumOverlayMenu`'s signal set.
3a. **Cursor Freq** — currently always-on (`drawCursorInfo()` rendered unconditionally at `SpectrumWidget.cpp:1300` and again at line 3730 for paused/snapshot mode). **Wire** the existing flyout toggle: add `bool m_showCursorFreq{true};` member + `setCursorFreqVisible(bool)` setter; guard the two `drawCursorInfo()` calls; persist via existing `scheduleSettingsSave()` path. Default = on (no behavior change for users who don't touch it).
3b. **Fill Color** — `SpectrumWidget::setFillColor(QColor)` already exists at line 831. Bind the flyout's `ColorSwatchButton` to it. Same backing as Setup → Display → Spectrum Defaults' fill-color picker, so changes sync between flyout and setup page.
3c. **Remove** unbacked Heat Map toggle, Noise Floor toggle + slider, Weighted Avg toggle. No `SpectrumWidget` setter / `PanadapterModel` property exists for these; they were label-update theatre. Noise floor estimation already happens via `NoiseFloorEstimator` + `ClarityController` (per CLAUDE.md memory) — the canonical path.
4. **Strip NYI buttons** — keep visible-but-disabled (user override). `+RX`, `+TNF`, `ATT`, `MNF` stay greyed. They communicate "feature coming."
5. **"More Display Options →" link** — wire to emit `openSetupRequested("Display")`. `MainWindow` already has the routing pattern (used elsewhere for AGC-T right-click → Setup).
6. **Right-click menu** (`SpectrumOverlayMenu`) — no functional change. Just inherits A2's palette consolidation.

**Touched files.** `MainWindow` (+~10 lines connect), `SpectrumOverlayPanel` (+~15 lines emit, −~30 lines remove), `SpectrumWidget` (+~10 lines setter + guards).

---

### Phase C — Right-panel cleanup

#### C1. TxApplet density / NYI handling

**Inventory** (per `TxApplet.cpp` audit):
- 14 wired controls: Mic-source badge, Forward power gauge, RF Power slider, Tune Power slider, VOX, MON, Monitor volume slider, LEV, EQ, CFC, TUNE, MOX, TX Profile combo, 2-Tone.
- 1 hidden conditional: PS-A (gated until 3M-4).
- 7 NYI placeholders: SWR gauge, ATU, MEM, Tune Mode combo, DUP, xPA, SWR Prot LED.

**Decisions:**
1. **Wire SWR gauge** — `RadioStatus::powerChanged(forwardPower, reversePower, swr)` already emits SWR (`RadioStatus.cpp:111`, `:119`). The forward-power gauge connect at `TxApplet.cpp:707` reads only the first arg; add `m_swrGauge->setValue(swr)` inside the same lambda and remove the `NyiOverlay::markNyi(swrGauge, ...)` at line 249. ~3 lines.
2. **SWR Prot LED** — verify during implementation whether protection-tripped status flows through `RadioStatus`. If yes, wire; else remove.
3. **Remove NYI placeholders:** ATU, MEM, Tune Mode combo, DUP, xPA. Each returns when its underlying feature ships.
4. **PS-A** stays hidden until 3M-4 (already conditional). No change.
5. **Forward power** — already wired (verified at `TxApplet.cpp:707`, EMA α=0.10 smoothing, 20 Hz timer). No change.
6. **One layout reshuffle: TUNE + MOX move above VOX + MON.** Final TxApplet row order from top to bottom: Mic-source badge → FWD gauge → SWR gauge → RF Power slider → Tune Power slider → **TUNE + MOX** → VOX + MON → Monitor volume slider → LEV + EQ + CFC → Profile combo + ● modified indicator → **TX BW Lo/Hi spinboxes** (new from E1) → 2-Tone. Reads as: meters → power sliders → primary action buttons → modulation toggles → processor toggles → profile + filter cluster → test signal. Promotes the action buttons (TUNE/MOX) to a more prominent position.

**Touched files.** 1 (`TxApplet.cpp`). Net ~50 lines removed, ~3 added.

#### C2. Whole-applet NYI ghost stack

**Inventory:**
- REAL: `RxApplet`, `TxApplet`, `VaxApplet` (3 applets fully wired).
- PARTIAL: `PhoneCwApplet` — Phone tab is real, CW tab nearly all NYI.
- GHOSTS (entirely placeholder-only, 9 applets): `EqApplet`, `FmApplet`, `DigitalApplet`, `PureSignalApplet`, `DiversityApplet`, `CwxApplet`, `DvkApplet`, `CatApplet`, `TunerApplet`. Each occupies ~150-300 px of vertical space showing controls that do nothing.

**Decision.** Hide ghost applets at the construction + `panel->addApplet()` call sites in `MainWindow.cpp` around lines 1497-1514 (where `EqApplet` / `DigitalApplet` / `PureSignalApplet` / `DiversityApplet` / `CwxApplet` / `DvkApplet` / `CatApplet` / `TunerApplet` are constructed and added). Files stay in the tree; uncomment one line per applet when each feature ships. Net: panel shows 4 stacks (RX · TX · Phone/CW · VAX) instead of 13.

`PhoneCwApplet`'s CW tab body gets stripped to a single "Coming in 3M-2" message panel until CW TX (3M-2) lands. Tab switch button stays so the structure is visible.

**Touched files.** 2 (`MainWindow.cpp` for the add gates; `PhoneCwApplet.cpp` for CW tab content stub).

**Returns when:**
- EqApplet → 3I-3 (TX/RX EQ wiring)
- FmApplet → 3I-3 (FM controls)
- DigitalApplet → 3-VAX (legacy slot — may merge into VaxApplet)
- PureSignalApplet → 3M-4 (PureSignal)
- DiversityApplet → 3F (multi-RX)
- CwxApplet → 3I-2 (CW keyer)
- DvkApplet → 3I-1 (DVK)
- CatApplet → 3J/3K/3-VAX (CAT/TCI/VAX status)
- TunerApplet → ATU phase (no plan yet)

---

### Phase D — Modal dialog style cleanup

**Decision.** Apply A2's pragmatic palette to the four drift-heavy modal dialogs (`AboutDialog`, `AddCustomRadioDialog`, `NetworkDiagnosticsDialog`, `SupportDialog`) plus extract `TxEqDialog` and `TxCfcDialog`'s shared inline `QDoubleSpinBox { ... }` block into a new `Style::doubleSpinBoxStyle()` helper. `VaxFirstRunDialog` and `VaxLinuxFirstRunDialog` stay unchanged — they're the clean exemplars (every colour via `Style::k*` constants, zero raw hex).

**Specifics:**
- `AboutDialog` — finish the partial migration; replace remaining ~6 raw hex with `Style::k*`. ~10-15 line edits.
- `AddCustomRadioDialog` — replace 4 file-local style constants (`kFieldStyle`, `kPrimaryButtonStyle`, `kSecondaryButtonStyle`, `kCheckStyle`) with `Style::buttonBaseStyle()` and palette constants. ~30-40 line edits.
- `NetworkDiagnosticsDialog` — replace 3 local `constexpr const char*` with canonical constants. ~15-20 line edits.
- `SupportDialog` — replace per-widget inline strings with helpers. ~20-30 line edits.
- `TxEqDialog` + `TxCfcDialog` — extract the shared `QDoubleSpinBox` inline block (`#1a2a3a` / `#304050` / `#c8d8e8`) into `Style::doubleSpinBoxStyle()`. ~5 line edits per dialog + 1 helper added to `StyleConstants.h`.

**Touched files.** ~6.

---

### Phase E1 — TX bandwidth filtering

User-locked decisions across 9 sub-decisions (D1-D9 plus D9b color customization plus D9c additions). All TX bandwidth state is per-profile, integrating with the existing `MicProfileManager` schema (90+ fields → 91+ with FilterLow/FilterHigh added).

#### D1. Storage architecture: per-profile

`MicProfileManager` schema gains two fields: `FilterLow` (int, Hz) and `FilterHigh` (int, Hz). Activating a profile pushes its filter values into runtime; manual edits update runtime AND mark the active profile as modified (cleared on Save). Mode change does NOT auto-swap the filter — operators switch profile when context changes (the profile bundles all relevant TX state, including filter, for that operating mode).

This reverses an early proposal (per-mode storage) after investigation showed Thetis's authoritative pattern is per-profile (`setup.cs:8091 SetTXFilters(DSPMode mode, int low, int high)` is invoked at profile-activation time, not mode-change time; `setup.cs:3015-3016` saves filter as part of profile DB row).

**Touched files.** `MicProfileManager.{h,cpp}` (+~50 lines for schema + load/save), 20 factory profile assignments per D4.

#### D2. UI idiom: two spinboxes (Thetis-faithful)

TxApplet displays the active profile's TX filter as two `QSpinBox` widgets — Low (Hz) on the left, High (Hz) on the right. Matches Thetis's `udTXFilterLow` / `udTXFilterHigh` Setup form widgets exactly. Same spinbox pair appears on Setup → Audio → TX Profile page (D6).

**Touched files.** `TxApplet.{h,cpp}` (+~30 lines), `TxProfileSetupPage.cpp` (+~20 lines).

#### D3. Placement in TxApplet: directly after the Profile combo

The TX BW row sits one line below the Profile combo + ● modified indicator. Reads as a 2-row cluster: "what profile am I using" → "what's its TX BW right now." When user nudges a spinbox, the ● badge on the row above lights up — clear cause-and-effect. Final TxApplet row order documented in C1 with the TUNE/MOX promotion above VOX/MON.

#### D4. Default factory profile filter values: port verbatim from Thetis `database.cs`

Each of the 20 NereusSDR factory profiles gets its `FilterLow` / `FilterHigh` values copied verbatim from the matching Thetis profile row in `database.cs:9282-9418`, with inline `// From Thetis database.cs:<line> [v2.10.3.13]` cite. Per-profile cite required by the source-first protocol.

Sample values seen in Thetis:
- SSB default: 100/3000 (`database.cs:4546-4547`)
- SSB voice: 200/3100 (`:4778-4779`)
- Communications-narrow: 1000/2000 (`:5010-5011`)
- DX SSB compressed: 1710/2710 (`:5240-5241`)
- AM-style symmetric: 0/4000 (`:5469-5470`)
- SSB 3.5k DIGI/ESSB: 100/3500 (`:5927`, `:6156`, `:6385`)
- Ragchew: 250/3250 (`:6614-6615`)
- ESSB wide: 50/3650 (`:6843`, `:7530`, `:7759`)
- SSB 3.2k: 100/3200 (`:7988-7989`, `:8217-8218`)

For NereusSDR profiles without a direct Thetis equivalent (likely 0-2 profiles per CLAUDE.md): use the closest Thetis profile's values as a reference and cite as `// NereusSDR-original`.

#### D5. Persistence scope: per-profile (resolved by D2)

Already covered — TX BW is part of the profile bundle. AppSettings keys: `mic-profile/<profile-name>/FilterLow` and `.../FilterHigh`. Persisted via `MicProfileManager::saveProfile`.

#### D6. Setup page entry: add to `TxProfileSetupPage`

`TxProfileSetupPage` (Setup → Audio → TX Profile) gets a new "TX Filter" `QGroupBox` containing two `QSpinBox` controls (Low / High). Same setter as the TxApplet spinboxes — both surfaces stay in sync via `MicProfileManager`'s `filterChanged` signal. Profile-awareness banner (E2) shows above the group.

Future direction (out of scope here): other profile fields should migrate to `TxProfileSetupPage` over time so it becomes the canonical profile editor (similar to Thetis Setup form's structure). For this PR, just FilterLow/High.

**Touched files.** `TxProfileSetupPage.cpp` (~20 lines).

#### D7. Range limits: low [0..5000] Hz, high [200..10000] Hz

Hardware/software-achievable bounds. Covers every Thetis factory profile (lowest seen: 0; highest: 4000) plus headroom for ESSB enthusiasts (50/3650 in-range), narrow FM (5000), broadcast FM audio (up to 10000). Doesn't reach Nyquist (24 kHz at 48 kHz sample rate).

Per-mode validation lives in `TransmitModel::setFilterLow/setFilterHigh` (NOT in the spinbox range): swap inverted pairs on commit, lock DRM read-only, treat symmetric modes (AM/SAM/DSB/FM) with low=0 conventionally. Spinbox stays permissive; model enforces sense.

#### D8. Live update during TX: yes, with debounce

`TransmitModel::setFilterLow/setFilterHigh` connects directly to `TxChannel::setTxFilter` → `SetTXABandpassFreqs` on WDSP. Filter changes audible in TX trace within one block (~5 ms). Debounce ~50 ms on spinbox value-change to avoid spamming WDSP during arrow-button rapid clicks.

WDSP supports mid-stream bandpass changes; Thetis applies live. Replaces the current hardcoded `±150/±2850` mode-switch logic in `TxChannel.cpp:520-528`.

#### D9. Visual feedback: status text + panadapter overlay always + waterfall MOX-gated

**Status text in TxApplet TX BW row** — small QLabel below the spinboxes computed from `TransmitModel::filterDisplayText(mode, low, high)`:
- Asymmetric modes (SSB/DIGI): `"100-2900 Hz · 2.8k BW"`
- Symmetric modes (AM/SAM/DSB/FM): `"±2500 Hz · 5.0k BW"` or `"5.0k BW"` only

**Panadapter overlay** — translucent orange band (default fill `rgba(255, 120, 60, 46)`) spanning the TX-active passband on the spectrum widget. Always visible regardless of MOX state — tells the user where they'd transmit before they press MOX. Toggle in Setup → Display → Waterfall (`display/showTxFilter`, default true).

**Waterfall column** — vertical orange column on the waterfall during MOX (gated by `MoxController::moxStateChanged` → `m_showTxFilterOnRxWaterfall`). Default false (avoids cluttering the RX waterfall history). Toggle in Setup → Display → Waterfall.

**Wiring:** `TxApplet spinbox → TransmitModel::setFilterLow/setFilterHigh → filterChanged(low, high) → MainWindow connects → SpectrumWidget::setTxFilterRange(low, high) → paint`. Same chain drives the status label.

Most of the infrastructure already exists (per investigation): `m_showTxFilterOnRxWaterfall`/`setShowTxFilterOnRxWaterfall` are wired with persistence; `m_txFilterVisible`/`setTxFilterVisible` exist as a stub at `SpectrumWidget.cpp:2644`. We add the value pipe (`setTxFilterRange`), fill in the panadapter paint code (currently `Q_UNUSED` at `:1584`), wire MOX coupling, and add palette colors.

**Touched files.** `SpectrumWidget.{h,cpp}` (+~80 lines for member fields, setter, paint code, MOX coupling), `MainWindow.cpp` (+~5 lines for connect), `TransmitModel.cpp` (+~25 lines for `filterDisplayText` helper), `TxApplet.cpp` (+~15 lines for status QLabel), `StyleConstants.h` (+~5 lines for new palette).

**Bench verification:** with MOX on, SSB at 100/2900: (a) panadapter shows orange band 100-2900 Hz off VFO, (b) waterfall column appears during TX, (c) spinbox changes move the band live, (d) status text matches.

#### D9b. Filter overlay color + opacity customization

Both **TX filter color** and **RX filter color** become user-customizable via `ColorSwatchButton` (NereusSDR's existing color picker with alpha embedded in `QColorDialog`). Adds two ColorSwatchButton call sites (#10 and #11 in DisplaySetupPages.cpp).

**Placement:**
- **TX filter color/opacity:** Setup → Display → TX Display page, new "TX Filter Overlay" group
- **RX filter color/opacity:** Setup → Display → Spectrum Defaults page, new "RX Filter Overlay" group

**Defaults (Thetis-faithful):**
- TX: orange `#FF7838` ~18% alpha (`rgba(255, 120, 60, 46)`)
- RX: cyan `#00B4D8` ~31% alpha (matches `kAccent` palette accent)

Persistence: AppSettings `display/txFilterColor` and `display/rxFilterColor` as `"#RRGGBBAA"` hex (existing ColorSwatchButton round-trip).

**SubRX filter color** deferred to phase 3F (multi-panadapter); see D9c-4 for forward-compat scaffolding.

**Touched files.** `SpectrumWidget.{h,cpp}` (+~50 lines for `m_txFilterColor` + `m_rxFilterColor` + setters + persistence), D9 paint code reads from these (+~5 lines), `DisplaySetupPages.cpp` (+~55 lines split across TX Display + Spectrum Defaults pages).

**Note on Thetis split (color picker + alpha slider) vs NereusSDR's embedded alpha:** Thetis uses two widgets per filter (RGB picker + alpha slider). NereusSDR's `ColorSwatchButton` has alpha enabled in the dialog — one widget, same outcome. Decision: stick with NereusSDR's pattern for consistency with the 9 existing color swatches.

#### D9c. Additional color/overlay refinements

Three small additions on top of D9b (locked from a 5-suggestion proactive review):

**1. Split zero-line color RX vs TX.** Today NereusSDR has ONE shared `m_zeroLineColor` (default red) at `SpectrumWidget.h:786`. Visibility toggles are already separate (`showRxZeroLineOnWaterfall`, `showTxZeroLineOnWaterfall`). Split the color into `m_rxZeroLineColor` (default red) + `m_txZeroLineColor` (default amber) so users can distinguish RX vs TX zero-beat reference instantly during split / X-IT operation. Two new ColorSwatchButtons on Setup → Display → Spectrum Defaults page.

**2. "Reset all display colors to defaults" button.** Convenience for users who experimented and want the factory look back. Button at the bottom of Setup → Display → Spectrum Defaults page → calls a new `SpectrumWidget::resetDisplayColorsToDefaults()` method that resets all color members + saves AppSettings. ~10 lines.

**3. Reserve TNF + SubRX color scaffolding (forward-compat).** Define AppSettings keys (`display/subrxFilterColor`, `display/tnfFilterColor`) and SpectrumWidget setter signatures NOW. No paint code, no setup-page UI yet (those come with the 3F multi-RX phase and the future TNF phase). When those features land, customization persists across versions. ~15 lines.

**Skipped from the 5 suggestions:** (2) separate alpha slider next to color button — keep ColorSwatchButton's embedded alpha for consistency; (5) color theme save/load mini-Skins — defer to the dedicated Skins phase since it's broader (gradients, layouts, meter styles).

**Touched files (D9c only).** `SpectrumWidget.{h,cpp}` (+~30 lines for split zero-line + reset method + reservation scaffolding), `DisplaySetupPages.cpp` (+~25 lines for 2 zero-line ColorSwatchButtons + Reset button).

#### E1 totals

| Sub-decision | Lines (est.) |
|---|---|
| D1-D6 (model + persistence + UI + Setup) | ~250 |
| D7-D8 (range + live update + debounce) | ~10 |
| D9 (status text + panadapter overlay + waterfall MOX-gated) | ~150 |
| D9b (TX/RX color customization) | ~125 |
| D9c (zero-line split + reset button + scaffolding) | ~55 |
| **E1 total** | **~590 lines** |

**Bench verification (REQUIRED).** Engage MOX with mic; sweep TX BW from narrow (1.8k) to wide (3.5k); verify TX trace bandwidth in spectrum changes accordingly. Verify per-profile persistence (set DX-SSB to 2.7k, switch to ESSB-wide profile, switch back, confirm 2.7k restored — but only if user explicitly Saved; otherwise modified state on the original profile is preserved, see E2).

---

### Phase E2 — Profile-awareness UI cues (cross-cutting)

Cross-cutting infrastructure that decorates 91 profile-bundled controls across 11+ surfaces with visual cues showing profile membership and modified state. Locked at Option D (composite — banner + per-control border) plus Strategy A (per-band field registration for parametric blobs) plus Reset = factory defaults (re-select profile to revert; no separate "Reset to Saved" affordance).

#### Architecture

**`ProfileAwarenessRegistry`** (new class, owned by `MicProfileManager`):
- `attach(QWidget*, QString fieldName)` — registers a widget; sets `profileBundled=true` Qt property; installs context menu; connects value-change signal for modified detection.
- `detach(QWidget*)` — auto via `QPointer` + destroyed signal.
- `isModified(QWidget*)`, `isAnyModified()`, `revertToSaved(QWidget*)`, `revertAllToSaved()`, `reevaluateAll()`.
- Value-extractor dispatcher: QSpinBox/QDoubleSpinBox/QSlider → `value()`; QCheckBox/checkable QPushButton → `isChecked()`; QComboBox → `currentIndex()` or `currentData()`; QLineEdit → `text()`; custom widgets (ParametricEqWidget) via custom getter callback in attach overload.

**`ProfileBanner`** (new reusable widget):
- Layout: `[⚙] TX Profile: <name> [● modified-dot] [Saved/Modified] [stretch] [Save] [Save As...]`
- Subscribes to `MicProfileManager::activeProfileChanged` and registry `anyModifiedChanged`.
- **No Reset button** — re-selecting the profile from the combo loads its saved values fresh.
- Factory-profile mode: Save disabled with tooltip; Save As enabled.

**QSS rules** (in `StyleConstants.h`):
```
[profileBundled="true"] { border-left: 3px solid #00b4d8; padding-left: 6px; }
[profileBundled="true"][profileModified="true"] { border-left-color: #ffb800; }
```

**MicProfileManager additions:**
- `saveActiveProfile()` — atomic snapshot of TransmitModel state into the active profile's stored representation (iterates field-schema table).
- `createProfileFromCurrent(name)` — Save As.
- `compareToSaved(fieldName)` — per-field comparison used by registry.
- Signal `profileModifiedChanged(bool)`.
- Note: NO `resetActiveProfileToSaved()` method — reverting is done by re-selecting the profile from the combo, which goes through the existing profile-activation path.

#### 91-field inventory (12 functional groups)

Mic input (~10): MicGain · FMMicGain · MicMute · MicSource · MicBoost · MicXlr · MicTipRing · MicBias · MicPttDisabled · LineIn · LineInBoost
TX Filter (2 — E1): FilterLow · FilterHigh
TX EQ (~25): TXEQEnabled · TXEQPreamp · TXEQNumBands · TXEQ0..9 · TxEqFreq0..9 · Nc · Mp · Ctfmode · Wintype · TXParaEQData · RXParaEQData
TX Leveler (3): Lev_On · Lev_MaxGain · Lev_Decay
TX ALC (2): ALC_MaximumGain · ALC_Decay
CFC + Phase Rotator (~24): CFCEnabled · CFCPostEqEnabled · CFCPreComp · CFCPostEqGain · CFCFreq0..9 · CFCComp0..9 · CFCPostEq0..9 · CFCParaEqData · CFCPhaseRotatorEnabled · CFCPhaseReverseEnabled · CFCPhaseRotatorFreq · CFCPhaseRotatorStages
CESSB (1): CESSBOn
Compander (CPDR) (2): CompanderOn · CompanderLevel
VOX/DEXP (~10): VOX_On · VOX_HangTime · Dexp_On · Dexp_Threshold · Dexp_Attack · Dexp_Release · Dexp_Hysterisis · Dexp_Tau · Dexp_Attenuate
DX (1): DXOn
Two-Tone test (~7, verify which are Thetis-bundled): twoToneFreq1/2 · twoToneLevel · twoTonePower · twoToneFreq2Delay · twoToneInvert · twoTonePulsed
Pre-emphasis (deferred to 3M-3b): FMPreEmphasisOn (etc.)

Total: ~91 fields.

#### Surface map (11–12 banner placements)

| Surface | Banner | # fields | Notes |
|---|---|---|---|
| TxApplet | top of TX section | ~7 | Profile combo · TX BW Lo/Hi · LEV · EQ btn · CFC btn · VOX |
| TxProfileSetupPage | page top | ~2 | Will grow over time as more fields migrate |
| AgcAlcSetupPage | TX-section group only | 5 | Lev_On · Lev_MaxGain · Lev_Decay · ALC_MaximumGain · ALC_Decay |
| CfcSetupPage | page top | ~10 | PhRot + CFC + CESSB groups |
| AudioTxInputPage | page top | ~10 | Mic input cluster |
| SpeechProcessorPage | page top | ~5 | CFC dashboard mirrors |
| VoxDexpSetupPage | page top | ~9 | VOX + DEXP cluster |
| FmSetupPage | page top (when FM mic-gain present) | 1-2 | FMMicGain + future pre-emphasis |
| PhoneCwApplet | top of Phone tab | ~3 | MicGain · CompanderOn · CompanderLevel |
| TxEqDialog | top of dialog body | ~25 | All TX EQ fields |
| TxCfcDialog | top of dialog body | ~35 | All CFC + PhRot fields |
| TestTwoTonePage | page top (if applicable) | ~7? | Verify which are profile-bundled in Thetis first |

#### EQ and CFC parametric handling — Strategy A

The CFC dialog's two `ParametricEqWidget` instances (compression curve + post-EQ curve) edit 30 individual fields collectively (CFCFreq0..9 + CFCComp0..9 + CFCPostEq0..9) plus the compound `CFCParaEqData` blob.

**Strategy A (locked):** register each of the 30 individual fields PLUS the blob. Drag a point in the comp widget → registry re-evaluates 3 entries (freq, gain for that band index). Per-band visibility for "modified" detection. Matches Thetis's `isTXProfileSettingDifferent` per-field comparison pattern (`setup.cs:3015-3049`).

Custom getter on the ParametricEqWidget yields `point[bandIndex].freq` / `.gain`. The widget exposes a single `pointsChanged` signal that fires for any point edit; the registry maps that to per-field re-evaluation.

For TX EQ legacy panel: 20 individual scalar registrations (10 band gains + 10 band frequencies) directly on the QSlider/QSpinBox widgets. For TX EQ parametric panel: blob registration + the parametric widget's points exposed via custom getter.

#### Existing "Reset Comp" / "Reset EQ" buttons in dialogs

Keep their current behavior (reset to factory defaults, NOT to profile saved values). To revert to active profile's saved values, the user re-selects the profile from the combo — that's the canonical revert mechanism. No "Reset to Saved" button on the banner; no per-widget right-click revert. Simpler design.

#### Edge cases (covered)

| Case | Handling |
|---|---|
| Switch profile while modified | Modal: "Profile X has unsaved changes. [Save] [Discard] [Cancel]." |
| Factory profile (read-only) | Save disabled with tooltip; Save As enabled |
| Modified state on hidden surface | Registry tracks regardless of visibility; banner anywhere shows "Modified" |
| Schema migration | Missing fields fall back to factory defaults during `loadProfile`; registry compares against post-migration value |
| Programmatic value-set during profile load | Suppressed via `m_loadingProfile` flag; bulk reevaluate after |
| DRM / fixed-bandwidth mode | Filter spinboxes read-only; border still visible (still profile-bundled) |
| Custom widgets without standard signals | Custom getter + signal name in `attach()` overload |
| Widget destruction | `QPointer` auto-cleanup on destroyed signal |
| Multi-instance same field (e.g. FilterLow on TxApplet AND TxProfileSetupPage) | Registry stores vector of widgets per field; all sync via model signal |

#### E2 phased rollout (within the single PR)

| Phase | What | Lines |
|---|---|---|
| 1 | Infrastructure: ProfileAwarenessRegistry + ProfileBanner + QSS rules + MicProfileManager API additions | ~600 |
| 2 | Field-schema table (91 fields → metadata) | ~150 |
| 3 | TxApplet integration: banner + 7 field registrations | ~80 |
| 4 | Setup page integrations (TxProfile/AgcAlc/Cfc/AudioTxInput/VoxDexp/SpeechProcessor/Fm) | ~250 |
| 5 | Modal dialog integrations (TxEqDialog + TxCfcDialog with custom ParametricEqWidget getters) | ~150 |
| 6 | PhoneCwApplet integration + edge cases (modified-during-switch dialog, factory read-only, multi-instance sync) | ~100 |
| 7 | Tests + docs | ~200 |
| | **E2 total** | **~1530 lines** |

#### E2 verification

- Unit tests per widget type (QSpinBox/QSlider/QCheckBox/QComboBox/custom): attach → modified → revert → detach.
- Integration: change `Lev_On` on AgcAlcSetupPage → TxApplet banner shows Modified (cross-surface sync).
- Profile switch with unsaved: dialog appears; choosing Discard reverts; Cancel restores combo to active.
- Factory profile: Save disabled; Save As creates new user profile.
- Visual: capture {Saved, Modified, Factory-locked} state for one representative banner surface.

---

## Implementation order (within the single PR)

Suggested commit topology so reviewers can read thematically:

1. **A1** — scrollbar gutter (1 file, 1 line). Trivial.
2. **A2** — style consolidation (~50 files). Mechanical pattern-replace; the palette-promotion edits to `StyleConstants.h` go first; then snap raw hex; then replace local constant blocks. Possibly split into a few sub-commits by file group (applets / widgets / setup / dialogs).
3. **A3** — SetupDialog regime (~10 files). Stand-alone; lands cleanly after A2.
4. **D** — modal dialog cleanup (~6 files). Same pattern as A2 but for dialogs; lands as a sibling commit.
5. **B5** — no-op (just confirmed; no commit needed).
6. **B7** — drop VfoWidget X/RIT tab Lock (1 file). Tiny.
7. **B6** — XIT wire-up (~3 files). Self-contained; mirror RIT pattern.
8. **B1** — AGC family (~3-5 files). Add Long to RxApplet combo; verify slider direction at bench; align AUTO badge across surfaces.
9. **B2** — Mode + Filter family (~3-5 files). Add DSB+DRM to VfoWidget mode combo; refactor RxApplet's static filter-grid to mode-aware presets pulled from canonical SliceModel list.
10. **B3** — Antenna family (~5 files). Introduce `AntennaPopupBuilder`; rewire 3 callers; SpectrumOverlay flyout combo gains EXT/XVTR/BYPS entries.
11. **B4** — Audio family (~2 files). Drop AF gain from RxApplet; wire Pan/Mute/SQL placeholders.
12. **B8** — Spectrum display controls (~3 files). Wire orphaned flyout signals; add Cursor Freq guard + setter; bind Fill Color; wire "More Display Options →"; remove unbacked toggles.
13. **C1** — TxApplet density (1 file). Wire SWR gauge; remove ATU/MEM/Tune Mode/DUP/xPA NYI rows; verify SWR Prot LED.
14. **C2** — Ghost-applet stack (2 files). Gate ghost applets in `ContainerManager` init; stub PhoneCwApplet's CW tab body.
15. **E2 Phase 1-2** — Profile-awareness infrastructure: `ProfileAwarenessRegistry`, `ProfileBanner`, QSS rules, `MicProfileManager` API additions (`saveActiveProfile`, `createProfileFromCurrent`, `compareToSaved`), 91-field schema table. ~750 lines, no UI surfaces touched yet.
16. **E1 D1-D6** — TX bandwidth filtering core: `TransmitModel::filterLow/filterHigh` + signal + persistence; `MicProfile` schema gains FilterLow/FilterHigh; 20 factory profiles seeded with values from Thetis `database.cs`; `TxChannel` reads from model instead of hardcoded; `TxApplet` adds spinbox cluster after Profile combo; `TxProfileSetupPage` gains "TX Filter" group.
17. **E1 D7-D8** — Range limits + live update with debounce. Trivial wiring on the spinboxes built in step 16.
18. **E1 D9** — Visual feedback: status text label on TxApplet TX BW row; `SpectrumWidget::setTxFilterRange()` + `m_txFilterLow/High`; panadapter overlay paint code; waterfall MOX-gated paint code. ~150 lines.
19. **E1 D9b** — Color customization: `m_txFilterColor` + `m_rxFilterColor` + setters + persistence; ColorSwatchButton on TX Display + Spectrum Defaults pages. ~125 lines.
20. **E1 D9c** — Additions: split zero-line color RX vs TX; "Reset all display colors" button; reserve TNF/SubRX color scaffolding. ~55 lines.
21. **E2 Phase 3-7** — Profile-awareness per-surface integration: TxApplet (banner + 7 fields), Setup page integrations (TxProfile/AgcAlc/Cfc/AudioTxInput/VoxDexp/SpeechProcessor/Fm), modal dialog integrations (TxEqDialog + TxCfcDialog with custom ParametricEqWidget getters), PhoneCwApplet integration, edge case handling (modified-during-switch dialog, factory read-only, multi-instance sync), tests, docs. ~780 lines.

Final commit: any cross-cutting follow-ups discovered during integration testing.

**Aggregate scope estimate:**
- A-phase (foundation + dialog cleanup): ~135 files
- B-phase (cross-surface ownership): ~25 files
- C-phase (right-panel cleanup): ~3 files
- E1 (TX bandwidth + overlays): ~10 files, ~590 lines
- E2 (profile-awareness): ~15-20 files, ~1530 lines
- **Total: ~120-150 unique files, ~2000-2200 new lines, ~50-100 lines removed**

---

## Verification plan

### Build

- `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j$(nproc)` clean on Linux + macOS + Windows.
- All existing tests continue to pass.
- New / updated tests:
  - `AntennaPopupBuilder` unit test (covers tier-1/tier-2/tier-3 SKU behaviour).
  - `Style::applyDarkPageStyle()` smoke test (no QSS parse warnings).

### Behavioural

- **A1:** Scrollbar appears; right edge of every applet's content has 8 px breathing room. Tested at minimum panel width and after expanding via splitter drag.
- **A2 / A3 / D:** Visual diff vs `main` shows no regressions. Same theme; same paddings (with the noted +8 px on the four `applyDarkStyle()` pages where padding-top corrects from 4 → 12, which is the intended fix).
- **B1:** Open RxApplet AGC combo — sees 5 modes (Off/Long/Slow/Med/Fast). Move VfoWidget AGC button row — RxApplet combo updates. Move RxApplet AGC-T slider in same direction as VfoWidget; both produce same numeric `agcThreshold` value at the same physical position. AUTO badge toggle from either surface updates the other.
- **B2:** VfoWidget mode combo shows 11 modes including DSB and DRM. RxApplet filter buttons display correct count per mode (e.g. SSB = 10 presets in RxApplet, top-5 in VfoWidget; CW = fewer, both grids flex correctly).
- **B3:** Test against ANAN-G2 (Tier 1 — full Alex), HL2 (Tier 3 — antenna UI hidden), and an ANAN-7000 if available (Tier 2 — partial). Popup contents match capability tier. Setup → Antenna Control still works for per-band overrides.
- **B4:** RxApplet AF row gone. Pan / Mute / SQL controls in RxApplet now work and stay in sync with VfoWidget.
- **B6 — XIT bench verification (REQUIRED).** With ANAN-G2:
  - Tune to 14.200 MHz on a quiet band. Set RIT = +0 Hz, XIT = +0 Hz, both off. Transmit (mic open or 2-Tone) — verify TX trace at 14.200 MHz.
  - Enable XIT, set XIT = +500 Hz. Transmit. Verify TX trace shifted to 14.2005 MHz.
  - Enable RIT, set RIT = -500 Hz. Verify RX listening at 14.1995 MHz; TX still at 14.2005 MHz (split confirmed).
  - Disable both; verify TX/RX both at 14.200 MHz.
  - Persistence: disconnect, reconnect; XIT state restored.
- **B7:** VfoWidget X/RIT tab no longer has Lock button. RxApplet header Lock and Close-strip Lock both still toggle the same property.
- **B8:** Open Display flyout, move WF Gain — spectrum waterfall colour-gain changes. Toggle Cursor Freq — frequency-at-cursor readout disappears/reappears. Click Fill Color — colour picker shows; selected colour applies to spectrum trace fill (and persists across restart). Click "More Display Options →" — Setup → Display opens.
- **C1:** TxApplet shows SWR gauge with live data during TX. NYI buttons (ATU, MEM, etc.) gone.
- **C2:** Right panel shows 4 applets (RX, TX, Phone/CW, VAX). Other 9 applets not present in the stack. Phone/CW's CW tab shows "Coming in 3M-2" placeholder body but tab structure is intact.
- **D:** AboutDialog / AddCustomRadioDialog / NetworkDiagnosticsDialog / SupportDialog visually match the rest of the app; no raw hex literal drift from the canonical palette.
- **E1 (TX bandwidth) bench verification (REQUIRED).** Engage MOX with mic (or 2-Tone) on ANAN-G2:
  - Activate "DX SSB" profile → spinboxes show 1710/2710 (per Thetis port). Translucent orange panadapter overlay band sits at +1.71k..+2.71k Hz off VFO. Audible TX trace on the spectrum confirms range.
  - Sweep High spinbox 2710 → 3500 → 1500 → 2710. Filter changes audible within ~5 ms (live update + debounce). Status label updates: "1710-3500 Hz · 1.79k BW" etc.
  - Switch to "ESSB Wide" profile → spinboxes restore to 50/3650; panadapter band widens. Modified indicator clears.
  - Modify ESSB profile (drag a spinbox), switch to "DX SSB" → modal "Profile has unsaved changes" appears. Discard → switch happens. Save → values persist.
  - Engage MOX → waterfall shows vertical orange column. Disengage MOX → column disappears. Panadapter overlay stays visible (per D9 always-on).
  - Customize TX filter color via Setup → Display → TX Display → ColorSwatchButton (set bright magenta with 50% alpha). Verify panadapter overlay updates immediately. Restart app → magenta persists.
  - Restart app → all profile + filter + color state persists.
- **E2 (profile-awareness) verification.**
  - Open Setup → DSP → AGC/ALC. Banner at top shows "TX Profile: DX SSB · Saved." Toggle TX Leveler enabled → border on the checkbox turns amber; banner shows "Modified" with Save button enabled.
  - Click Save → banner returns to Saved; border returns to blue. Profile DB row updated.
  - Switch profile via TxApplet's Profile combo → banner everywhere updates to new profile name. All registered widgets reload from new profile's saved values.
  - Open TxEqDialog → banner shows. Drag a parametric panel point → border flag fires + banner shows Modified. Cross-check: banner on TxApplet's TX section also shows Modified (cross-surface sync via registry's `anyModifiedChanged`).
  - Activate a factory profile (e.g. "DX SSB Default" if marked read-only) → Save button is disabled with tooltip explaining factory profiles can't be overwritten. Save As is enabled.

### Source-first / attribution

- This PR is largely Qt UI work (NereusSDR-native). Source-first protocol does not apply except for B6 (XIT offset application). Cite `// From Thetis console.cs:<line> [v2.10.3.13]` if a Thetis line drives the offset formula. Initial check: XIT is a simple `vfo + xitHz` addition; no complex Thetis algorithm to port. Verify during impl.
- All commits GPG-signed (per project policy).
- Pre-commit hook runs (`scripts/install-hooks.sh` already installed).

---

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| A2's palette consolidation regresses a visually-distinct local style block (e.g. `SpectrumOverlayPanel` translucent overlays). | Allow tightly-scoped local blocks with comment justification. Visual regression check on every screen during impl. |
| `applyDarkStyle()` consolidation changes the QGroupBox `padding-top` from 4 px to 12 px on Display/Transmit/Appearance/GeneralOptions pages — visible 8 px shift. | This is the intended fix (the drift was the bug). Document in PR description; user accepted in A3 walkthrough. |
| B1's AGC-T slider direction is wrong on one of the surfaces; we don't yet know which is Thetis-correct. | Resolve at bench during impl. If unsure, leave both surfaces in their current direction and surface the question in PR review. |
| B6 XIT wire-up doesn't actually shift the TX NCO on hardware (model→protocol path silently doesn't reach the radio). | Bench verification with ANAN-G2 is required before declaring this done. If TX NCO doesn't shift, investigate `P2RadioConnection::setTxFrequency` path. |
| `SetCursorFreqVisible` guard misses a draw-call site, leaving cursor freq visible when it shouldn't be. | grep for all `drawCursorInfo` callers; guard each. Currently 2 sites (line 1300, line 3730). |
| `AntennaPopupBuilder` produces the wrong popup for an SKU we haven't tested. | Test against ANAN-G2 + HL2 at minimum. Document tier-2 SKUs as "verify when available." |
| C2 hides applets a user actually wants visible (some users may rely on visible NYI placeholders to know what's coming). | Provide a hidden config key (`AppSettings`) `UI/ShowGhostApplets` defaulting to `False`; users who want them can flip it. Or skip — accept that "hide" means hide. Discuss in PR review if pushback. |
| One topic blocks the rest (e.g. SWR Prot LED data unavailable). | Each topic's commit is independent and revertable. Don't block the PR on a single topic. |
| E1 D8's live update during TX spams `SetTXABandpassFreqs` if user holds the spinbox arrow. | 50 ms debounce on `TransmitModel::setFilterLow/setFilterHigh` value changes. |
| E1 D9's panadapter overlay coordinate mapping is wrong for LSB / DIGL (audio low cutoff = upper sideband edge in RF). | Mode-aware mapping in paint code: USB/DIGU → low+VFO..high+VFO; LSB/DIGL → VFO-high..VFO-low; symmetric → -high+VFO..+high+VFO. Bench-verify on each mode. |
| E1 D9b color picker accidentally sets alpha=0 — overlay disappears, user thinks it's broken. | Tooltip on swatch warns about alpha=0; ColorSwatchButton's right-click "Reset to default" provides recovery; D9c-3 "Reset all display colors" provides global recovery. |
| E2 registry attaches widgets that get deleted before re-evaluation (dangling pointer). | `QPointer<QWidget>` in registry's internal map; auto-cleanup on `destroyed()` signal. |
| E2 profile-load programmatic value-set triggers spurious modified=true. | `m_loadingProfile` flag in MicProfileManager; registry's value-change handler skips re-evaluation while flag is set; bulk reevaluate after load completes. |
| E2 cross-surface modified state confuses users ("why does AGC/ALC page banner say Modified when I didn't touch it?"). | Tooltip on the banner's modified indicator: "1 unsaved change — see TxApplet TX BW row" (groups changes by surface). |
| E2 Strategy A (per-band registration for parametric blobs) fires re-evaluation 30 times when user drags one point. | Batch via `QSignalBlocker` during a single mouse-drag operation; reevaluate once on mouse-release. ParametricEqWidget's `pointsChanged` signal already only fires on mouse-up per existing design. |
| E1 + E2 combined adds significant scope (~2000 lines) to a "polish" PR. | Implementation order document (above) lists 21 phased commits. Reviewer can read thematically. Each phase is revertable. If review feedback says "split", the natural split point is between #14 (C2 — UI polish complete) and #15 (E2 infrastructure — TX bandwidth + profile-awareness as a separate follow-up PR). |

---

## Out of scope

These came up during the audit but are explicitly NOT part of this PR:

1. **TxApplet layout reshuffle** (further grouping beyond C1's TUNE/MOX swap). Cosmetic improvement; deferred to a future polish pass.
2. **Wiring features that ship with their respective phases.** Each ghost applet, each NYI feature button, etc. waits for its phase. This PR doesn't accelerate phase work — only XIT (B6) and SWR gauge (C1) and the TX bandwidth/overlay/profile-awareness suite (E1+E2) are net-new features. Everything else stays gated.
3. **AetherSDR-style overlay/floating scrollbar.** Considered in A1; rejected for this PR. Could be revisited if the project commits to closer AetherSDR aesthetic parity.
4. **SubRX filter color customization** (E1 D9b). Deferred to phase 3F (multi-panadapter) — but D9c-4 reserves the AppSettings key + setter signature so user customization survives across the version that adds the feature.
5. **TNF (Tracking Notch Filter) color customization.** Deferred to whenever TNF lands — D9c-4 reserves scaffolding similarly.
6. **Color theme save/load (mini-Skins).** Considered in D9c-5; deferred to dedicated Skins phase since it's broader (gradients, layouts, meter styles bundle together).
7. **Migration of `applyDarkStyle()` pages to `SetupPage::addLabeledX()` helpers.** Considered in A3; rejected — too invasive.
8. **HL2 ATT/filter safety audit.** Open per CLAUDE.md memory. Independent of this PR. XIT specifically does not touch ATT/filter logic.
9. **Pre-emphasis profile fields** (FMPreEmphasisOn, etc.). Per CLAUDE.md, deferred to 3M-3b (FM-mode work). Registry handles missing fields gracefully.
10. **Two-Tone fields profile-bundling verification.** The 7 two-tone fields' profile-bundled status in Thetis needs explicit verification before E2 registers them. If they're not bundled (NereusSDR-only state), they're left undecorated. Verify during E2 Phase 2.
5. **Migration of `applyDarkStyle()` pages to `SetupPage::addLabeledX()` helpers.** Considered in A3; rejected — too invasive.
6. **HL2 ATT/filter safety audit.** Open per CLAUDE.md memory. Independent of this PR. XIT specifically does not touch ATT/filter logic.

---

## References

- `.superpowers/brainstorm/33882-1777661197/inventory-snapshot.md` — full inventory from 5 parallel agent runs (right-panel applets, non-applet live UI, SetupDialog 47 pages, modal dialogs, cross-cutting stylesheet+endpoint matrix).
- `CLAUDE.md` — project context, source-first protocol, current phase status.
- `CONTRIBUTING.md` — coding conventions, GPG signing, commit format.
- `src/gui/StyleConstants.h` — canonical palette source of truth.
- `docs/architecture/` — sibling design docs (e.g. `phase3g8-rx1-display-parity-plan.md` set the precedent for per-band display state — pattern reused in B2's filter-preset approach).
