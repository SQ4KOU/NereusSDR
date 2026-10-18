# Phase 3M-3a-ii Follow-up — `ucParametricEq` Port Verification Matrix

**Status:** Drafted 2026-04-30. Bench verification on ANAN-G2 pending JJ.

This matrix lists every user-visible control on the rewritten
`TxCfcDialog` and `TxEqDialog` plus the underlying
`ParametricEqWidget` behaviors. Each row cites the upstream Thetis
source line or the NereusSDR test that pins the contract, then
names the manual bench step that verifies the live behavior.

The full implementation plan lives at
[`../phase3m-3a-ii-followup-parametriceq-plan.md`](../phase3m-3a-ii-followup-parametriceq-plan.md);
the original design hand-off doc is at
[`../phase3m-3a-ii-cfc-eq-parametriceq-handoff.md`](../phase3m-3a-ii-cfc-eq-parametriceq-handoff.md).

---

## Coverage summary

| Source area | Auto-tested | Manual bench |
|---|---:|---:|
| `ParametricEqWidget` — skeleton + palette | 4 slots in `tst_parametric_eq_widget_skeleton` | — |
| `ParametricEqWidget` — axis math + ordering | 10 slots in `tst_parametric_eq_widget_axis` | — |
| `ParametricEqWidget` — paint + bar chart + peak hold | 7 slots in `tst_parametric_eq_widget_paint` | side-by-side vs Thetis |
| `ParametricEqWidget` — interaction (mouse/wheel) | 10 slots in `tst_parametric_eq_widget_interaction` | drag/click/wheel by hand |
| `ParametricEqWidget` — JSON marshal/unmarshal | 16 slots in `tst_parametric_eq_widget_json` | profile round-trip via TxApplet |
| `ParaEqEnvelope` — gzip + base64url | 9 slots in `tst_para_eq_envelope` (incl. Python fixture decode) | save profile, inspect file |
| `MicProfileManager` round-trip | 8 slots in `tst_mic_profile_manager_para_eq_round_trip` | save → quit → restart → load |
| `TxChannel::getCfcDisplayCompression` | 6 slots in `tst_tx_channel_cfc_display` | live bar chart in `TxCfcDialog` during TX |
| `TxCfcDialog` controls | 21 slots in `tst_tx_cfc_dialog` | every control by hand |
| `TxEqDialog` controls | 11 slots in `tst_tx_eq_dialog` | every control by hand |
| **Total** | **102 automated test slots** across 10 test executables | — |

`ctest --output-on-failure` reports **261/261 green** at HEAD `dbbcbfa`.

---

## Detailed control matrix

Conventions:

- "Auto" = the row is fully covered by an automated test slot — bench
  re-verification is optional.
- "Bench" = needs eyeball-on-radio confirmation that the contract holds
  end-to-end (TX RF, audio, WDSP profile).
- "Auto + Bench" = automated coverage exists for the wiring, but a live
  bench pass is still recommended (typical for anything that touches the
  WDSP TX chain).

### A. `TxCfcDialog` (opened from TxApplet `[CFC]` button)

| # | Control | Thetis source | Auto-test or manual step | Expected outcome | Status |
|--:|---|---|---|---|---|
| A1 | Two `ParametricEqWidget` instances stacked vertically | `frmCFCConfig.Designer.cs:155-196,665-703 [v2.10.3.13]` | `tst_tx_cfc_dialog::constructsWithExpectedControls` (line 83) | `findChildren<ParametricEqWidget>()` returns size 2 | Auto |
| A2 | Compression curve widget (top, `m_compWidget`) — DbMax=16, DbMin=0, ParametricEQ=true, GlobalGainIsHorizLine=true | `frmCFCConfig.Designer.cs:155-196` | Manual: open dialog, confirm top widget Y axis spans 0..16 dB and shows a single horizontal line for global pre-comp | Top curve clamps in 0..16 dB range; global pre-comp shows as horizontal handle/line | Bench |
| A3 | Post-EQ curve widget (bottom, `m_postEqWidget`) — DbMax=24, DbMin=-24, GlobalGainIsHorizLine=false (default) | `frmCFCConfig.Designer.cs:665-703` | Manual: confirm bottom widget Y axis spans -24..+24 dB and global gain handle sits in left gutter | Bottom curve clamps in -24..+24 dB | Bench |
| A4 | Cross-sync — selecting a band on top widget selects same band on bottom | `frmCFCConfig.cs:121-128 [v2.10.3.13]` | `tst_tx_cfc_dialog::crossSync_compSelect_setsPostEq` (208), `crossSync_postEqSelect_setsComp` (222) | Both widgets show same selected `bandId`, edit row spinboxes drive the selected band | Auto |
| A5 | Top edit row `#` (selected band, `m_selectedBandSpin`) — read-only display, range 1..N | `frmCFCConfig.Designer.cs:360-385` | `tst_tx_cfc_dialog::selectedBandSpin_drivesBothWidgetSelections` (552) | Range matches current band-count radio (5/10/18); displays selected band; clicking a curve point updates it | Auto |
| A6 | Top edit row `f` (frequency, `m_freqSpin`) — 0..20000 Hz | `frmCFCConfig.Designer.cs:267-271` | Manual: type a value into `f` for selected band | Both widgets' frequency for that bandId updates live; bar chart re-bins under it | Bench |
| A7 | Top edit row `Pre-Comp` (`m_precompSpin`) — 0..16 dB, 0.1 step | `frmCFCConfig.Designer.cs:408-417` | `tst_tx_cfc_dialog::precompSpin_drivesTm` (316) | `TransmitModel::cfcPrecompDbChanged` emits with new value; `pushTxProcessingChain` calls `SetTXACFCOMPRun`/`Position` | Auto + Bench |
| A8 | Top edit row `Comp` (`m_compSpin`) — 0..16 dB, 0.1 step | `frmCFCConfig.Designer.cs:217-226` | `tst_tx_cfc_dialog::compSpin_drivesSelectedBand` (352) | Compression-curve gain at selected band updates; TM `cfcCompression[band]` emits | Auto + Bench |
| A9 | Top edit row `Q` (`m_compQSpin`) — 0.2..20, 0.01 step | `frmCFCConfig.Designer.cs:597-617` | Manual: spin Q value, observe top curve sharpness change | Bell width changes; curve still passes through (f, comp) anchor | Bench |
| A10 | Middle edit row `Post-EQ` (global, `m_postEqGainSpin`) — -24..+24 dB | `frmCFCConfig.Designer.cs:337-346` | `tst_tx_cfc_dialog::postEqGainSpin_drivesTm` (332) | `TransmitModel::cfcPostEqGainDbChanged` emits | Auto + Bench |
| A11 | Middle edit row `Gain` (per-band, `m_gainSpin`) — -24..+24 dB | `frmCFCConfig.Designer.cs:564-573` | `tst_tx_cfc_dialog::gainSpin_drivesSelectedBand_postEq` (381) | Post-EQ curve gain at selected band updates; TM `cfcPostEqBandGain[band]` emits | Auto + Bench |
| A12 | Middle edit row `Q` (post-EQ, `m_eqQSpin`) — 0.2..20, 0.01 step | `frmCFCConfig.Designer.cs:597-617` | Manual: spin Q on post-EQ, observe bottom curve sharpness change | Same shape change as A9 but on bottom widget | Bench |
| A13 | Bands radio group — 5-band | `frmCFCConfig.Designer.cs:470-553` | `tst_tx_cfc_dialog::bandCountRadio_5_switchesBothWidgets` (236) | Both widgets reset to 5 bands; per-band points re-spaced | Auto |
| A14 | Bands radio group — 10-band (default) | same | `tst_tx_cfc_dialog::constructsWithExpectedControls` (83) confirms default | Both widgets at 10 bands on first show | Auto |
| A15 | Bands radio group — 18-band | same | `tst_tx_cfc_dialog::bandCountRadio_18_switchesBothWidgets` (254) | Both widgets reset to 18 bands | Auto |
| A16 | Freq Range `Low` (`m_lowSpin`) — 0..20000 Hz, default 0 | `frmCFCConfig.Designer.cs:440-468` | `tst_tx_cfc_dialog::freqRangeSpinbox_clampsBothWidgets` (273) | Both widgets clamp visible range to [Low, High] | Auto |
| A17 | Freq Range `High` (`m_highSpin`) — 0..20000 Hz, default 4000 | `frmCFCConfig.Designer.cs:625-653` | same | same | Auto |
| A18 | Freq-range minimum-spread guard (1 kHz) | `frmCFCConfig.cs:122-138` | `tst_tx_cfc_dialog::freqRangeSpinbox_clampGuardEnforces1kHzSpread` (304) | Typing High <= Low + 1000 snaps back to Low + 1000 | Auto |
| A19 | `Use Q Factors` checkbox (`m_useQFactorsChk`) — default checked | `frmCFCConfig.Designer.cs:484-496` | `tst_tx_cfc_dialog::useQFactorsCheckbox_togglesParametricEqOnBothWidgets` (467) | Both widgets `setParametricEq(checked)` — when off, Q columns ignored, curve flat-band | Auto |
| A20 | `Live Update` checkbox (`m_liveUpdateChk`) — default unchecked | `frmCFCConfig.Designer.cs:90-100` | Manual: toggle on, drag a band point | When on: TM updates per drag step; when off: only on mouse-release | Bench |
| A21 | `Log scale` checkbox (`m_logScaleChk`) — default unchecked | `frmCFCConfig.Designer.cs:238-247` | `tst_tx_cfc_dialog::logScaleCheckbox_togglesBothWidgets` (483) | Both widgets switch X axis to log scale | Auto |
| A22 | `Reset Comp` button (`m_resetCompBtn`) | `frmCFCConfig.Designer.cs:508-519` | `tst_tx_cfc_dialog::resetCompButton_restoresFlatComp` (499) | Top widget bands reset to even spacing, all comp = 0 dB | Auto |
| A23 | `Reset EQ` button (`m_resetEqBtn`) | `frmCFCConfig.Designer.cs:142-153` | `tst_tx_cfc_dialog::resetEqButton_restoresFlatEq` (526) | Bottom widget bands reset to even spacing, all gain = 0 dB | Auto |
| A24 | `OG CFC Guide by W1AEX` LinkLabel | `frmCFCConfig.cs:600` (URL), `Designer.cs:78-88` (label) | `tst_tx_cfc_dialog::ogGuideLink_isClickable` (456) | Click opens `https://www.w1aex.com/anan/CFC_Audio_Tools/CFC_Audio_Tools.html` in default browser | Auto + Bench |
| A25 | Bar chart timer (50 ms cadence) — `m_barChartTimer` | `frmCFCConfig.cs:447 [v2.10.3.13]` | `tst_tx_cfc_dialog::barChartTimerStartsOnShow_StopsOnHide` (165) | Timer starts on show, stops on hide; pulls fresh `getCfcDisplayCompression` data each tick | Auto + Bench |
| A26 | Live bar chart in compression widget | `ucParametricEq.cs:1858-1880 [v2.10.3.13]` (BarValuesUpdated signal) | Manual: key TX with mic input | Bars rise during voice TX at ~33 ms paint cadence; peak hold visible after release; decays at 20 dB/s | Bench |
| A27 | Hide-on-close behavior | `frmCFCConfig.cs:CloseEvent` handler | `tst_tx_cfc_dialog::closeEventHidesInsteadOfDestroying` (184) | Closing dialog hides it; TM/state preserved on next open | Auto |
| A28 | TM-driven external setter sync | TM `cfcPrecompDbChanged`/`cfcPostEqGainDbChanged`/`cfc*Changed` signals | `tst_tx_cfc_dialog::externalSetters_updateDialog` (414) | Mutating TM via Setup → DSP page reflects in dialog without round-trip lag | Auto |

### B. `TxEqDialog` (opened from TxApplet `[EQ]` button)

| # | Control | Thetis source | Auto-test or manual step | Expected outcome | Status |
|--:|---|---|---|---|---|
| B1 | `Legacy EQ` checkbox at top (`m_legacyToggle`) — default checked | `eqform.cs:969-981 [v2.10.3.13]` | `tst_tx_eq_dialog::dialogContainsLegacyToggle` (290) | Checkbox visible at top of dialog; default state = checked | Auto |
| B2 | Legacy → parametric panel toggle | `eqform.cs:2862-2911 [v2.10.3.13]` | `tst_tx_eq_dialog::legacyTogglesBetweenLegacyAndParametricPanels` (303) | `QStackedWidget` swaps between legacy 10-band slider grid (index 0) and parametric panel (index 1) | Auto |
| B3 | `Legacy EQ` state persists across launches | `eqform.cs Common.RestoreForm` round-trip | `tst_tx_eq_dialog::legacyToggleStatePersistsInAppSettings` (327) | AppSettings `TxEqDialog/UsingLegacyEQ` stores `True`/`False`; restored on construct | Auto |
| B4 | Parametric panel hosts a `ParametricEqWidget` | `eqform.cs:235-967` ucParametricEq1 block | `tst_tx_eq_dialog::parametricPanelContainsParametricEqWidget` (346) | `findChildren<ParametricEqWidget>()` returns 1 in parametric mode | Auto |
| B5 | No profile combo (drops Thetis duplicate) | (de-scoped — TxApplet hosts profile combo) | `tst_tx_eq_dialog::dialogDoesNotContainProfileCombo` (396) | Walking the dialog widget tree finds zero `QComboBox` for profile | Auto |
| B6 | No Save / Save As / Delete buttons (drops Thetis duplicate) | (de-scoped — TxApplet hosts profile mgmt) | `tst_tx_eq_dialog::dialogDoesNotContainSaveSaveAsDelete` (417) | No `QPushButton` with text matching Save/Save As/Delete | Auto |
| B7 | Legacy band-column sliders use project style | `Style::sliderVStyle()` (Batch 9 fix) | `tst_tx_eq_dialog::legacyBandColumnSlidersUseSliderVStyle` (447) | All 10 band sliders + preamp slider have `Style::sliderVStyle()` stylesheet | Auto |
| B8 | `Enable TX EQ` checkbox (`m_enableChk`) | `eqform.cs:74` chkTXEQEnabled | Manual: toggle off, key TX | EQ stage bypassed in TX DSP chain | Bench |
| B9 | `Nc:` spinbox (filter coefficient count) — 32..8192, step 32 | `eqform.cs` setup section | Manual: change to 4096, key TX | Sharper filter shapes; CPU usage rises | Bench |
| B10 | `Mp` checkbox (minimum-phase mode) — default off | `eqform.cs` setup section | Manual: toggle on, key TX | Lower latency; slight phase non-linearity | Bench |
| B11 | `Cutoff:` combo (0=Peaking / 1=Notch) | WDSP `eq.c create_eqp() ctfmode` | Manual: switch to Notch, key TX | Sharp band-stop instead of bell | Bench |
| B12 | `Window:` combo (0=Blackman-Harris / 1=Hann) | WDSP `eq.c create_eqp() wintype` | Manual: switch to Hann, key TX | Smoother rolloff | Bench |
| B13 | Legacy panel — preamp slider+spinbox (-12..+15 dB) | `eqform.cs:1549-1561` tbTXEQPre | Manual: drag preamp slider, watch spin mirror | Slider and spinbox stay in sync; TM `txEqPreampDb` updates | Bench |
| B14 | Legacy panel — 10 band gain sliders (-12..+15 dB each) | `eqform.cs:1496-1546` tbTXEQ0..9 | Manual: drag each band slider | TM `txEqBandDb[i]` updates for each band | Bench |
| B15 | Legacy panel — 10 band frequency spinboxes (0..20000 Hz) | `eqform.cs:1329` udTXEQ0..9 | Manual: type a frequency for band 0 | TM `txEqBandHz[0]` updates | Bench |
| B16 | Legacy panel — `+15 dB / 0 dB / -12 dB` scale labels at right edge | `eqform.cs:1358-1397` lblTXEQ15db / 0dB / minus12db | Manual: visual inspection | Labels read against dark background (Batch 9 fix applies `kTextPrimary` color) | Bench |
| B17 | Parametric panel edit row — `#` (selected band, `m_paraSelectedBandSpin`) | `eqform.cs:920-925` nudParaEQ_selected_band | Manual: click a curve point, observe `#` updates | Selected band index reflects clicked point | Bench |
| B18 | Parametric panel edit row — `f` (frequency, `m_paraFreqSpin`) — 0..20000 Hz | `eqform.cs:267` nudParaEQ_f | Manual: type frequency value | Selected band's point moves to new frequency | Bench |
| B19 | Parametric panel edit row — `Gain` (`m_paraGainSpin`) — -24..+24 dB, 0.5 step | `eqform.cs:271` nudParaEQ_gain | Manual: type gain value | Selected band's point moves vertically | Bench |
| B20 | Parametric panel edit row — `Q` (`m_paraQSpin`) — 0.2..20, 0.1 step | `eqform.cs:269` nudParaEQ_q | Manual: spin Q value | Selected band's bell sharpens/widens | Bench |
| B21 | Parametric panel edit row — `Preamp` (`m_paraPreampSpin`) — -24..+24 dB, 0.5 step | `eqform.cs:671-699` nudParaEQ_preamp | Manual: spin preamp | Global gain handle moves; widget global gain line shifts | Bench |
| B22 | Parametric panel — `Reset` button (`m_paraResetBtn`) | `eqform.cs:731-740` btnParaEQReset | Manual: click Reset | All bands reset to flat curve, even spacing | Bench |
| B23 | Parametric panel right column — `Log scale` checkbox | `eqform.cs:468-478` chkLogScale | Manual: toggle on | X axis switches to log scale | Bench |
| B24 | Parametric panel right column — `Use Q Factors` checkbox (default checked) | `eqform.cs:528-540` chkUseQFactors | Manual: toggle off | Q columns ignored; curve degenerates to flat-band | Bench |
| B25 | Parametric panel right column — `Live Update` checkbox + warning icon | `eqform.cs:402-414, 388-400` chkPanaEQ_live + pbParaEQ_live_warning | Manual: toggle on; warning icon visible | When on: warning icon shows; profile updates per drag step | Bench |
| B26 | Parametric panel right column — `Low` freq spinbox | `eqform.cs:540-622` udParaEQ_low | Manual: change Low value | Visible range narrows from left | Bench |
| B27 | Parametric panel right column — `High` freq spinbox | `eqform.cs:540-622` udParaEQ_high | Manual: change High value | Visible range narrows from right | Bench |
| B28 | Parametric panel right column — Bands radios (5/10/18, default 10) | `eqform.cs:493-526` panelTS1 + radParaEQ_5/10/18 | Manual: click 18-band | Curve resets to 18 even-spaced bands | Bench |
| B29 | Hide-on-close behavior | (Qt convention) | `tst_tx_eq_dialog::closeEventHidesInsteadOfDestroying` (480) | Closing dialog hides it; state preserved on next open | Auto |

### C. `ParametricEqWidget` behaviors not exposed via dialog buttons

These are intrinsic widget contracts worth bench-checking even though
the dialogs configure them upfront.

| # | Behavior | Thetis source | Auto-test or manual step | Expected outcome | Status |
|--:|---|---|---|---|---|
| C1 | 18-color band palette (verbatim from Thetis) | `ucParametricEq.cs:1107-1131 [v2.10.3.13]` | `tst_parametric_eq_widget_skeleton::paletteHasEighteenColors`, `paletteFirstThreeRgbVerbatim`, `paletteLastThreeRgbVerbatim` | First 3 + last 3 RGB triples byte-identical with Thetis | Auto |
| C2 | Linear axis math — frequency ↔ X | `ucParametricEq.cs:1175-1230` | `tst_parametric_eq_widget_axis::linearXFromFreqMidpoint`, `linearFreqFromXRoundtrip` | Round-trip identity within ½-pixel tolerance | Auto |
| C3 | Log axis math — frequency at normalized half = sqrt(min*max) | `ucParametricEq.cs:1175-1230` | `tst_parametric_eq_widget_axis::logFrequencyAtNormalisedHalf` | Geometric midpoint matches log-axis convention | Auto |
| C4 | dB axis round-trip | `ucParametricEq.cs:1232-1260` | `tst_parametric_eq_widget_axis::dbAxisRoundtrip` | Y-from-dB / dB-from-Y round-trip identity | Auto |
| C5 | Reset produces N evenly-spaced bands | `ucParametricEq.cs:Reset()` | `tst_parametric_eq_widget_axis::resetProducesTenEvenBands`, `resetEndpointsLockedToRange` | Endpoints locked to fmin/fmax; interior bands evenly spaced | Auto |
| C6 | Drag enforces ordering — bands re-sort by frequency | `ucParametricEq.cs:EnforceOrdering()` | `tst_parametric_eq_widget_axis::enforceOrderingSortsByFreq` | After drag, band IDs re-ordered left-to-right | Auto |
| C7 | Background color = Thetis dark grey | `ucParametricEq.cs:OnPaint() background` | `tst_parametric_eq_widget_paint::backgroundColorIsThetisDarkGrey` | RGB = Thetis verbatim value | Auto |
| C8 | Zero-dB line rendered at correct Y | `ucParametricEq.cs:OnPaint() midline` | `tst_parametric_eq_widget_paint::zeroDbLineRenderedAtCorrectY` | Y position matches dB-to-Y formula at 0 dB | Auto |
| C9 | Response dB at center frequency = band gain | `ucParametricEq.cs:ResponseDb()` | `tst_parametric_eq_widget_paint::responseDbAtFrequencyAtCenterEqualsGain` | Curve passes exactly through (f, gain) anchor | Auto |
| C10 | Graphic-EQ mode uses linear interpolation between bands | `ucParametricEq.cs:GraphicEqResponse()` | `tst_parametric_eq_widget_paint::responseDbGraphicEqLinearInterpolation` | Between two adjacent bands, response is straight line | Auto |
| C11 | Bar chart values clamped to dB range | `ucParametricEq.cs:1858-1880` BarValuesUpdated | `tst_parametric_eq_widget_paint::barChartDataIsClampedToDbRange` | Out-of-range bar input clamps to [DbMin, DbMax] | Auto |
| C12 | Peak hold decays over time (20 dB/s) | `ucParametricEq.cs:peak hold timer` | `tst_parametric_eq_widget_paint::peakHoldDecaysOverTime` | After N timer ticks, peak drops linearly | Auto |
| C13 | Left-click on a band selects it | `ucParametricEq.cs:1625-1801` OnMouseDown | `tst_parametric_eq_widget_interaction::leftClickOnBandSelectsIt` | `pointSelected(bandId)` signal emits | Auto |
| C14 | Left-drag on interior band moves freq + gain | same | `tst_parametric_eq_widget_interaction::leftDragMovesBandFreqAndGain` | Band moves diagonally with cursor | Auto |
| C15 | Left-drag on endpoint moves gain only (frequency locked) | same | `tst_parametric_eq_widget_interaction::leftDragOnEndpointMovesGainOnly` | Endpoint stays at fmin/fmax; gain follows cursor | Auto |
| C16 | Right-click is ignored | same | `tst_parametric_eq_widget_interaction::rightClickIsIgnored` | No selection, no drag | Auto |
| C17 | Wheel (no modifier) multiplies Q | `ucParametricEq.cs:OnMouseWheel()` | `tst_parametric_eq_widget_interaction::wheelNoModifierMultipliesQ` | Q value multiplied by wheel factor | Auto |
| C18 | Shift+Wheel adjusts gain only | same | `tst_parametric_eq_widget_interaction::wheelShiftAdjustsGainOnly` | Only gain changes; freq/Q unchanged | Auto |
| C19 | Ctrl+Wheel adjusts frequency only | same | `tst_parametric_eq_widget_interaction::wheelCtrlAdjustsFrequencyOnly` | Only freq changes; gain/Q unchanged | Auto |
| C20 | Click-and-drag on global gain handle | `ucParametricEq.cs:OnMouseDown global gain hit-test` | `tst_parametric_eq_widget_interaction::clickOnGlobalGainHandleStartsDrag` | `globalGainChanged` signal emits during drag | Auto |
| C21 | Mouse-release fires final non-dragging signal | `ucParametricEq.cs:OnMouseUp` | `tst_parametric_eq_widget_interaction::releaseFiresFinalNonDraggingSignal` | `pointDataChanged(bandId, dragging=false)` emits | Auto |
| C22 | Click on empty area deselects | `ucParametricEq.cs:OnMouseDown empty hit-test` | `tst_parametric_eq_widget_interaction::clickOnEmptyAreaDeselects` | `pointUnselected()` signal emits | Auto |
| C23 | JSON `saveToJson` produces snake_case keys | `ucParametricEq.cs:SaveToJson()` | `tst_parametric_eq_widget_json::saveToJsonProducesSnakeCaseKeys` | Keys are `min_hz`, `max_hz`, `points` etc. — not camelCase | Auto |
| C24 | JSON `saveToJson` rounds freq to 3 decimals | `ucParametricEq.cs:SaveToJson()` | `tst_parametric_eq_widget_json::saveToJsonRoundsFreqToThreeDecimals` | Freq stored as e.g. `1234.567` not `1234.5678901` | Auto |
| C25 | JSON `saveToJson` rounds gain to 1 decimal | same | `tst_parametric_eq_widget_json::saveToJsonRoundsGainToOneDecimal` | Gain stored as `5.5` not `5.500001` | Auto |
| C26 | JSON `saveToJson` rounds Q to 2 decimals | same | `tst_parametric_eq_widget_json::saveToJsonRoundsQToTwoDecimals` | Q stored as `4.50` not `4.5000123` | Auto |
| C27 | JSON `loadFromJson` round-trip preserves data | `ucParametricEq.cs:LoadFromJson()` | `tst_parametric_eq_widget_json::loadFromJsonRoundTripPreservesData`, `loadFromJsonProducesIdenticalReSerialization` | Save → load → save produces byte-identical second blob | Auto |
| C28 | JSON `loadFromJson` rejects malformed input | same | `tst_parametric_eq_widget_json::loadFromJsonRejectsInvalidJson`, `loadFromJsonRejectsMissingPoints`, `loadFromJsonRejectsTooFewPoints`, `loadFromJsonRejectsBandCountMismatch`, `loadFromJsonRejectsBadFreqRange` | Returns false; widget state unchanged | Auto |
| C29 | JSON `loadFromJson` clamps out-of-range data | same | `tst_parametric_eq_widget_json::loadFromJsonClampsOutOfRangeData` | Out-of-range freq/gain/Q clamped to widget limits | Auto |
| C30 | JSON `loadFromJson` during drag matches Thetis (drag wins) | `ucParametricEq.cs:LoadFromJson() while m_dragging` | `tst_parametric_eq_widget_json::loadFromJsonDuringDragMatchesThetis` | Currently-dragging band's JSON values ignored; drag state survives | Auto |

### D. `ParaEqEnvelope` (gzip + base64url helper)

| # | Behavior | Thetis source | Auto-test | Expected outcome | Status |
|--:|---|---|---|---|---|
| D1 | ASCII string round-trip | `Common.cs Compress_gzip` / `Decompress_gzip [v2.10.3.13]` | `tst_para_eq_envelope::roundTripsAsciiString` | encode→decode returns input bytewise | Auto |
| D2 | Unicode string round-trip | same | `tst_para_eq_envelope::roundTripsUnicodeString` | UTF-8 bytes preserved | Auto |
| D3 | Empty input → empty encode | edge case | `tst_para_eq_envelope::emptyInputReturnsEmptyOnEncode` | encode("") returns "" | Auto |
| D4 | Empty input → nullopt decode | edge case | `tst_para_eq_envelope::emptyInputReturnsNulloptOnDecode` | decode("") returns std::nullopt | Auto |
| D5 | Large JSON payload (~100kB) round-trip | scale check | `tst_para_eq_envelope::roundTripsLargeJsonPayload` | encode→decode returns input bytewise | Auto |
| D6 | Thetis-fixture blob decodes to known JSON | wire-compat check | `tst_para_eq_envelope::thetisFixtureBlobDecodesToKnownJson` | A blob captured from real Thetis decodes to the expected JSON string | Auto |
| D7 | Invalid base64 input → nullopt | safety | `tst_para_eq_envelope::invalidBase64ReturnsNullopt` | Garbage input doesn't crash; returns nullopt | Auto |
| D8 | Valid base64 but non-gzip → nullopt | safety | `tst_para_eq_envelope::validBase64ButNonGzipReturnsNullopt` | Random bytes don't crash zlib; returns nullopt | Auto |
| D9 | Truncated gzip → nullopt | safety | `tst_para_eq_envelope::truncatedGzipReturnsNullopt` | Half a gzip stream returns nullopt cleanly | Auto |

### E. `MicProfileManager` round-trip with new TXParaEQData key

| # | Behavior | Source pin | Auto-test | Expected outcome | Status |
|--:|---|---|---|---|---|
| E1 | TXParaEQData key in bundle with empty default | `MicProfileManager.cpp` `defaultBundle()` | `tst_mic_profile_manager_para_eq_round_trip::txParaEqData_isInBundleWithEmptyDefault` | `defaultBundle()` includes `TXParaEQData`, value = `""` | Auto |
| E2 | Capture pulls live `txEqParaEqData` blob into bundle | `MicProfileManager.cpp captureFromTxChannel()` | `capture_txEqParaEqData_blobBytewise` | Capture round-trips the blob bytewise | Auto |
| E3 | Apply pushes blob from bundle into TM | `MicProfileManager.cpp applyToTxChannel()` | `apply_txEqParaEqData_blobBytewise` | Apply round-trips the blob bytewise | Auto |
| E4 | CFC and TX-EQ blobs captured independently | both blob keys | `capture_bothBlobs_independently` | Capture preserves each blob without bleed | Auto |
| E5 | CFC and TX-EQ blobs applied independently | both blob keys | `apply_bothBlobs_independently` | Apply preserves each blob without bleed | Auto |
| E6 | Empty blobs preserved (don't trip "missing" path) | edge case | `emptyBlobsArePreserved` | `""` survives capture and apply unchanged | Auto |

### F. `TxChannel::getCfcDisplayCompression` (live bar chart data)

| # | Behavior | Thetis source | Auto-test | Expected outcome | Status |
|--:|---|---|---|---|---|
| F1 | Rejects null buffer pointer | safety | `tst_tx_channel_cfc_display::rejectsNullBuffer`, `rejectsNullBuffer_withZeroSize` | Returns 0 / no crash | Auto |
| F2 | Rejects too-small buffer | safety | `rejectsTooSmallBuffer`, `rejectsZeroSizedBuffer`, `rejectsOneShortOfFullSize` | Returns 0 / no crash | Auto |
| F3 | Public size constants match Thetis WDSP `CFC_DISPLAY_FSIZE` | `cfcomp.c [v2.10.3.13]` | `publicConstantsMatchThetisFsize` | Constant value matches WDSP source | Auto |

---

## Manual bench checklist (ANAN-G2)

Run these in order; each step is independent. Tick as you confirm.

### Bench profile round-trip — CFC

- [ ] Open `TxCfcDialog` from TxApplet `[CFC]` button
- [ ] Drag any compression band to a non-default position (e.g. band 5 to +8 dB)
- [ ] Drag a post-EQ band to a non-default position (e.g. band 3 to -6 dB)
- [ ] Click `Save Profile` on the parent applet (TxApplet TX-Profile combo, "Save As…")
- [ ] Click `Reset Comp` and `Reset EQ` on TxCfcDialog (both curves go flat)
- [ ] Re-select the saved profile in the TxApplet combo — assert both curves restore byte-identical

### Bench profile round-trip — TX EQ

- [ ] Open `TxEqDialog` from TxApplet `[EQ]` button
- [ ] Toggle `Legacy EQ` off (parametric panel appears)
- [ ] Drag any band on the parametric curve
- [ ] Save / Reset / Re-select — same as CFC above

### Bench live bar chart

- [ ] In TxCfcDialog, key TX with mic input (real voice, not tone)
- [ ] Verify bar chart on compression-curve widget shows live data at ~33 ms cadence
- [ ] Stop TX — bar chart freezes (peak hold visible)
- [ ] Wait 1 second — peaks decay at ~20 dB/s
- [ ] Hide dialog, key TX again, re-show dialog — bars resume

### Bench CFC engage/disengage

- [ ] Toggle `[CFC]` button on TxApplet
- [ ] Open Setup → Transmit → CFC page
- [ ] Verify CFC checkbox state on the Setup page matches the TxApplet button state

### Bench `Legacy EQ` persistence

- [ ] Open TxEqDialog
- [ ] Toggle `Legacy EQ` off (parametric panel showing)
- [ ] Close dialog
- [ ] Quit app
- [ ] Restart app, open TxEqDialog
- [ ] Verify `Legacy EQ` is still unchecked (parametric panel still showing)

### Bench cross-sync (CFC)

- [ ] Open TxCfcDialog
- [ ] Click a point on the compression curve
- [ ] Verify the same band is selected on the post-EQ curve
- [ ] Edit the `f` spinbox in the top edit row — both widgets re-bin under the new freq
- [ ] Edit the `Comp` and `Gain` spinboxes — only the relevant widget updates

### Bench Live Update (CFC)

- [ ] Open TxCfcDialog with `Live Update` unchecked
- [ ] Key TX with mic input
- [ ] Drag a compression band — RF output should NOT change until you release the mouse
- [ ] Toggle `Live Update` checked
- [ ] Drag again — RF output should track the drag in real time (small audible artifacts acceptable per Thetis warning)

### Bench freq-range guard

- [ ] In TxCfcDialog, set Low = 0, High = 4000
- [ ] Try to set High = 800 — should snap back to Low + 1000 = 1000

### Bench band-count radios

- [ ] In TxCfcDialog, click `5-band` — both widgets reset to 5 evenly-spaced bands
- [ ] Click `18-band` — both widgets reset to 18 bands
- [ ] Click `10-band` (default) — both widgets reset to 10 bands

### Bench OG CFC Guide link

- [ ] Click `OG CFC Guide by W1AEX` link
- [ ] Default browser opens to `https://www.w1aex.com/anan/CFC_Audio_Tools/CFC_Audio_Tools.html`

### Optional cross-tool round-trip (Thetis Windows VM)

This is a compatibility sanity check — confirms the gzip+base64url envelope
encoded by NereusSDR matches what Thetis produces. Only run if you have a
Windows VM with Thetis already installed.

- [ ] Save a TX EQ profile in NereusSDR (parametric panel, non-default curve)
- [ ] Locate the encoded `TXParaEQData` blob in `~/.config/NereusSDR/MicProfiles/<MAC>.xml`
- [ ] Decode via Python:
  ```python
  import base64, gzip
  blob = "<paste blob here>"
  json_bytes = gzip.decompress(base64.urlsafe_b64decode(blob))
  print(json_bytes.decode())
  ```
- [ ] Open Thetis on Windows
- [ ] Construct an equivalent EQ profile by hand, save it
- [ ] Decode the Thetis profile's `txEqParaEqData` field the same way
- [ ] Diff the two decoded JSON strings — they should be structurally identical
  (key order may differ; values should match within rounding)

---

## Sign-off

| Date | Reviewer | Result | Notes |
|---|---|---|---|
| 2026-04-30 | (auto) | 102 / 102 automated test slots green | `ctest` reports 261/261 |
| _pending_ | JJ Boyd (KG4VCF) | _bench pending_ | ANAN-G2, all sections A.20 / A.26 / B.8-B.16 / bench checklist |
