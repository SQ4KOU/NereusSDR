# NereusSDR Attribution Remediation Log

Chronological record of every attribution gap discovered — during audit
passes, via external reports, or through self-review — together with
how and when each was cured.

This log is the transparent artifact that shows the project takes
compliance as a standing practice rather than a one-time event. Entries
are appended chronologically; nothing is ever deleted or edited.

Reporting a new omission? See
[`REPORTING-OMISSIONS.md`](REPORTING-OMISSIONS.md).

---

## Entry format

```
## YYYY-MM-DD — <short title>

**Discovered by:** <name / role>
**Reported via:** <GitHub issue # / private email / internal audit / external notice>
**Affected files:** <paths or count>
**Gap:** <what was missing or wrong>
**Root cause:** <why the first-pass audit missed it>
**Fix:** <commit SHA or branch>
**Process improvement:** <what changed to prevent recurrence>
```

---

## 2026-04-16 — Initial compliance posture failure (formal notice)

**Discovered by:** Richard Samphire (MW0LGE), Thetis principal contributor
**Reported via:** Private Discord message + draft formal notice
**Affected files:** ~170 NereusSDR source files derived from Thetis / mi0bot/Thetis-HL2 / WDSP, plus 3 graphical assets and the README framing
**Gap:**
- No per-file Thetis copyright, GPL permission block, warranty disclaimer, or contributor attribution preserved (root `LICENSE` only — GPL §1 "keep intact" violation)
- ANAN multimeter face artwork derivative of OE3IDE work with watermark removed
- README positioned NereusSDR as "independent" implementation despite extensive source-first porting
- v0.1.1–v0.1.7 binaries distributed in non-compliant state
**Root cause:** First-pass development relied on root-level LICENSE file and one-line `// From Thetis ...` comments without reproducing the upstream per-file license block; AI-assisted porting protocol (CLAUDE.md) did not mandate header preservation; artwork was AI-regenerated without understanding the original's attribution requirements
**Fix:** 26-commit compliance sweep on branch `compliance/v0.2.0-remediation` — see `docs/architecture/2026-04-16-gpl-compliance-remediation-design.md` for full work
**Process improvement:**
- Added non-negotiable license-preservation rule to CLAUDE.md source-first protocol
- Added merge-blocking requirement to CONTRIBUTING.md
- Established `docs/attribution/THETIS-PROVENANCE.md` as authoritative inventory
- Introduced `scripts/verify-thetis-headers.py` as gating test

---

## 2026-04-17 — Second pass: missing inline contributor attributions

**Discovered by:** Internal audit (after successful first pass)
**Reported via:** Self-initiated second-pass review — concern that fixed-template variants (samphire / no-samphire / mi0bot) only named 2–4 contributors while actual Thetis sources named 12+
**Affected files:** 157 NereusSDR files (112 thetis-samphire + 2 no-samphire + 4 mi0bot + 39 multi-source)
**Gap:** Per-file headers did not include inline-modification contributors named in the upstream source (W2PA Codella, G8NJJ Barker, NR0V Pratt, WD5Y, W4WMT Rambo, VK6APH Harman, KD5TFD Tracey, MI0BOT) — only the copyright-block contributors made it into the first pass
**Root cause:** Phase 1 applied fixed-template variants mechanically rather than extracting per-file contributor unions; only multi-source variant did unions
**Fix:** Commits `84e2ee7` (contributor index), `6aa8c0a` (reconciliation diff), `d436cd0` (4 template mismatches), `ef84eac` (fixed-template additions), `0857eb8` (multi-source touch-ups)
**Process improvement:**
- Added `docs/attribution/thetis-contributor-index.md` as reusable reference
- Inline mods now treated as first-class attribution, not commentary
- Verifier classifier bugs (substring match on "samphire", regex missing digit in "mi0bot") fixed

---

## 2026-04-17 — Second pass concern: MI0BOT full name unverifiable (inverted later)

**Discovered by:** Internal audit (Phase 2 subagent)
**Reported via:** Phase 2 concerns list
**Affected files:** 5 mi0bot-variant files + HEADER-TEMPLATES.md
**Gap:** Phase 1 template read "Reid Campbell (MI0BOT)" but `ramdor/Thetis` source nowhere spells "Campbell" — only "Reid" (first name) or "MI0BOT" (callsign). Internal audit couldn't verify the surname.
**Root cause:** First pass wrote the template without cross-checking Thetis source for the name
**Interim fix:** Commit `1f6b7de` normalized to callsign-only "MI0BOT" across the 5 files and HEADER-TEMPLATES.md for source-first strictness
**Later correction:** See next entry.

---

## 2026-04-17 — Third pass: MI0BOT full name verified from fork source

**Discovered by:** Internal audit (after cloning mi0bot/OpenHPSDR-Thetis locally)
**Reported via:** Self-initiated sanity pass on mi0bot fork
**Affected files:** 5 mi0bot-variant files, HEADER-TEMPLATES.md, THETIS-PROVENANCE.md, `scripts/verify-thetis-headers.py`
**Gap:** mi0bot fork's own `Project Files/Source/Console/HPSDR/IoBoardHl2.cs` header literally reads `Copyright (C) 2025 Reid Campbell, MI0BOT, mi0bot@trom.uk` — verifying the surname from an authoritative source. Also: `IoBoardHl2.cs` is a **fork-unique** file (not in ramdor/Thetis) authored solely by Reid Campbell, so `Hl2IoBoardTab.cpp/.h` had been incorrectly over-attributed via the generic mi0bot template (FlexRadio + Wigley + Samphire added).
**Root cause:** mi0bot fork had not been locally cloned during first or second pass; attribution work relied on ramdor/Thetis upstream only, which doesn't contain the fork-unique files or the fork's own copyright headers
**Fix:** Commit `a825a7f` — restored "Reid Campbell (MI0BOT)" full form for RadioDiscovery files (fork contributions), rewrote Hl2IoBoardTab headers to Reid-Campbell-solo form (no FlexRadio/Wigley/Samphire), added Variant 5 (`mi0bot-solo`) template, tightened verifier classifier
**Process improvement:**
- mi0bot/OpenHPSDR-Thetis now locally cloned at `/Users/j.j.boyd/mi0bot-Thetis/` for future reference
- New `mi0bot-solo` variant documented in HEADER-TEMPLATES.md for fork-unique files
- Lesson: any future claim about upstream contributor identity or file provenance MUST be verified against the actual upstream source, not inferred from secondary mentions

---

## 2026-04-17 — Audit-doc prose used stale names

**Discovered by:** J.J. Boyd (maintainer self-review)
**Reported via:** Visual spot-check of `second-pass-reconciliation.md` on GitHub
**Affected files:** `docs/attribution/second-pass-reconciliation.md` (30 instances), `docs/attribution/thetis-contributor-index.md` (14 instances)
**Gap:** Phase 1b audit doc recorded "John Melton (G8NJJ)", "Ron Holcomb (WD5Y)", "Bob Tracy (W4WMT)" as provisional names. Phase 2 verified and corrected names (Laurence Barker, Joe, Bryan Rambo respectively) when applying headers, but never back-propagated the corrections to the audit documents themselves. Readers of the audit docs would see wrong names.
**Root cause:** Phase 2's scope was "apply corrections to source files", not "back-fix audit documentation"
**Fix:** Commit `40558bc` — `sed`-style replacement across both audit docs
**Process improvement:** When Phase N subagents verify and correct names/facts that earlier phases recorded provisionally, the same subagent (or an explicit follow-up) must back-fix the provisional record so the audit trail doesn't drift from the applied reality

---

## 2026-04-17 — Phase 4 deep-audit pass (Tasks 24, 26, 27, 30, 31)

**Discovered by:** Internal deep-dive review after third pass
**Reported via:** Self-initiated Phase 4 audit covering GPL notice completeness, non-Thetis upstream provenance, binary-level surfaces, and process hardening
**Gaps addressed in this wave:**

1. **Permission-notice 3rd paragraph missing** (GPL §1 "keep intact") —
   Phase 1's templates included only paragraphs 1–2 of the standard GPL
   permission notice. Paragraph 3 ("You should have received a copy…")
   was omitted across ~170 derivative files; only `Hl2IoBoardTab.*`
   (hand-written verbatim from `IoBoardHl2.cs`) had the full form.
   Task 24a audit of the 27 cited Thetis sources found 60/40 variance
   between old (59 Temple Place) and current (51 Franklin Street) FSF
   addresses. Picked the current FSF-canonical form for tree-wide
   consistency (documented decision, not an arbitrary pick). Fix:
   commit `d53976c` — 169 files updated + 4 templates extended; 2 files
   (Hl2IoBoardTab.cpp/.h) preserved verbatim from source (59 Temple
   Place) as a documented source-verbatim exception.

2. **WDSP license specificity unverified** — `third_party/wdsp/` had
   never been grepped to confirm GPLv2-or-later vs v2-only. If v2-only,
   our GPLv3 wrappers would have been incompatible. Task 26 survey:
   128/138 files GPLv2-or-later, zero v2-only, 10 files no header
   (data tables / generated). Fully compatible with root GPLv3
   distribution. Documented in `docs/attribution/WDSP-PROVENANCE.md`.
   Fix: commit `0e8ef76`.

3. **Style-7 / Digital-7 font credit** — Thetis `console.cs` credits
   Sizenko Alexander / Style-7 for the Digital-7 font. If NereusSDR
   used the font, the credit would need to propagate. Task 27 grep:
   zero hits for font name/author; zero `.ttf`/`.otf` files. Font is
   not used. Documented absence in `docs/attribution/ASSETS.md` so
   the attribution record is explicit. Fix: commit `1c1e664`.

4. **CI verifier not enforced** — `scripts/verify-thetis-headers.py`
   ran only via manual invocation. Any PR could introduce a ported
   file without a proper header and nothing would catch it until the
   next manual sweep. Task 30 wired the verifier into the Ubuntu job
   of `.github/workflows/ci.yml`, runs post-checkout and pre-compile
   so failures happen fast. Fix: commit `0088a0a`.

5. **No reverse-sync check between tree and PROVENANCE** — tree could
   drift out of sync with `THETIS-PROVENANCE.md` (file renamed or new
   port added without updating PROVENANCE). Task 31 added
   `scripts/verify-provenance-sync.py` — detects files with "Ported
   from Thetis" marker not listed in PROVENANCE, and PROVENANCE rows
   pointing to files no longer on disk. Current tree: 148/148 clean.
   Fix: commit `acf85d7`.

**Root cause (shared across #1–#3):** Phase 1's audit was scoped to
contributor-identity and per-file header presence. Ancillary
completeness questions (full permission notice, third-party license
specifics, font credits) were not part of that scope and remained
unaudited until the Phase 4 deep-dive.

**Process improvements:**
- Task 30: CI now catches missing-header regressions at PR time.
- Task 31: CI can also catch PROVENANCE drift (wiring into CI will
  happen alongside Task 30's follow-up).
- Task 26: WDSP-PROVENANCE.md now serves as reference for the
  third_party upstream we incorporate by vendoring.
- Phase 4 plan in `docs/architecture/2026-04-17-gpl-compliance-remediation-plan.md`
  documents the remaining Phase 4 tasks (AetherSDR audit at Task 25,
  binary metadata at Task 28, third-party license bundle at Task 29).

---

## 2026-04-17 — AetherSDR provenance audit (Phase 4 Task 25)

**Discovered by:** Internal Phase 4 deep-dive — the three earlier passes had audited only Thetis / mi0bot derivation. ~170 Modification-History blocks in the compliance-branch tree cited AetherSDR (ten9876/AetherSDR) as the structural template, but AetherSDR itself had never been checked. Direct parallel to the original Thetis omission Richie flagged.
**Reported via:** Self-initiated Phase 4 audit
**Affected files:** 176 NereusSDR files (142 source + 18 tests + 16 with only inline AetherSDR references)
**Gaps:**

1. **AetherSDR attribution missing where the derivation is real.** 33 NereusSDR files genuinely port an AetherSDR counterpart (SpectrumWidget, VfoWidget + mode containers, RadioModel/SliceModel/PanadapterModel, MainWindow signal-routing hub, ConnectionPanel, FloatingContainer/ContainerManager, AudioEngine, etc.) — these carried no AetherSDR Copyright line until now.
2. **False AetherSDR citations.** 126 files claimed "Structural template follows AetherSDR (ten9876/AetherSDR) Qt6 conventions" in their Modification-History block but have no actual AetherSDR counterpart — they are Thetis-derived or NereusSDR-native. Over-attribution.
3. **Mixed-lineage files under-cited.** 10-12 files genuinely have both Thetis and AetherSDR contributions (pattern-inspired) but the citation was unspecific.

**Root cause:**
- Task 25a audit found AetherSDR has **zero per-file copyright headers** — all attribution is centralised in the LICENSE file and About dialog (primary author Jeremy/KK7GWY). Because there was no per-file source-verbatim mandate (nothing to "keep intact" at the file level), Phase 1's templates didn't know how to attribute AetherSDR and defaulted to either the generic boilerplate (overused) or omission (underused).
- AetherSDR is GPLv3 (full compatibility with NereusSDR root) — no license incompatibility; just an attribution gap.

**Fix (4 commits):**
- `e1c95ec` — Task 25a: `docs/attribution/aethersdr-contributor-index.md` — upstream survey catalogues the 235 AetherSDR sources, confirms Jeremy/KK7GWY primary authorship, GPLv3 licensing, and no per-file headers
- `4fff136` — Task 25b: `docs/attribution/aethersdr-reconciliation.md` — per-file classification (A/B/C/D buckets)
- `d9f9e79` — Task 25c commit A: add AetherSDR Copyright line to 33 genuine-derivation files
- `aed081a` — Task 25c commit B: remove false boilerplate AetherSDR line from 126 files
- `7c041bf` — Task 25c commit C: tighten AetherSDR citation on 10 mixed-lineage files (specific counterparts named)
- `e0142e6` — Task 25c commit D: defer 5 files to human review (MainWindow.h/.cpp have no formal port header, WdspEngine/AboutDialog test files are incidental)

**Process improvement:**
- Upstream projects with no per-file headers still need an attribution decision documented at the project level — `HEADER-TEMPLATES.md` now has guidance for AetherSDR-derived work (project-level reference only, not per-file header mirroring).
- Lesson: any future upstream dependency audit should first check whether upstream has per-file headers before deciding on the form of preservation. A project-level LICENSE + central About attribution is the minimum strong reference when per-file headers don't exist.

---

## 2026-04-17 — Binary metadata and release-artifact attribution (Phase 4 Tasks 28 + 29)

**Discovered by:** Internal Phase 4 deep-dive
**Reported via:** Self-initiated Phase 4 audit
**Affected surfaces:** binary-metadata slots (macOS Info.plist, Windows installer, Linux AppImage desktop file) + release artifact contents (license texts shipped to end users)

**Gap 1 — Binary metadata under-attributed (Task 28):**
Release binaries historically embedded NereusSDR-only copyright strings at the OS level. A user inspecting the macOS bundle's Info.plist, the Windows installer's LegalCopyright, or the Linux desktop file's Comment would see no hint of the Thetis / FlexRadio / OpenHPSDR lineage. Over 35 derivative attribution work at the source-file level was strong, but the binary-side attribution didn't match.

**Fix (Task 28 — commit `0871935`):**
- `CMakeLists.txt`: added `MACOSX_BUNDLE_COPYRIGHT` (Thetis-lineage string), `MACOSX_BUNDLE_SHORT_VERSION_STRING`, `MACOSX_BUNDLE_BUNDLE_VERSION` (previously unset)
- `scripts/windows/installer.nsi`: updated `VIAddVersionKey LegalCopyright`, added `CompanyName`, updated `Publisher` registry key
- `.github/workflows/release.yml`: extended Linux AppImage `.desktop` `Comment=` field with GPLv3 / Thetis / FlexRadio lineage parenthetical
- Locally verified: `defaults read build/NereusSDR.app/Contents/Info.plist NSHumanReadableCopyright` returns the Thetis-lineage string

**Gap 2 — Release artifacts didn't ship third-party license texts (Task 29):**
Binary aggregation of GPL-covered dependencies (Qt6 LGPLv3, FFTW3 GPLv2-or-later, WDSP GPLv2-or-later) requires that the recipient receive the applicable license texts. NereusSDR release artifacts shipped none of these — a downstream user unpacking an AppImage, DMG, or Windows installer would see only NereusSDR's own LICENSE, not the dependencies' licenses.

**Fix (Task 29 — commit `5ba06e0`):**
- New `packaging/third-party-licenses/` directory with pointer files for Qt6, FFTW3, WDSP, plus an index `README.md` naming each dependency, its role, its license, and the file where its license text lives
- `CMakeLists.txt`: new `install(DIRECTORY ...)` rule places the directory at `share/doc/nereussdr/licenses/` in any install prefix; new `install(FILES LICENSE ...)` rule also installs NereusSDR's own LICENSE at `share/doc/nereussdr/`
- `.github/workflows/release.yml`: macOS step copies licenses into `NereusSDR.app/Contents/Resources/licenses/` before codesign; Windows Deploy-Qt step copies licenses into the ZIP-and-NSIS root alongside `NereusSDR.exe`
- `scripts/windows/installer.nsi`: `SecMain` now installs licenses to `$INSTDIR\licenses\`
- Linux AppImage: no workflow edit needed — the CMake install rule places files under `AppDir/usr/share/doc/nereussdr/licenses/` and linuxdeploy preserves that path automatically
- Locally verified: `cmake --install build --prefix /tmp/nereus-install` places all 5 files correctly

**Current pointer-file choice:** each of `qt6.txt`, `fftw3.txt`, `wdsp.txt` points to the canonical upstream URL for the full license text rather than embedding multi-page GPL/LGPL bodies. This meets the GPL obligation (the recipient can obtain the license) while keeping the repository readable. If a compliance reviewer prefers full-text bodies shipped in-tree, follow-up commit can drop the canonical texts into these files without structural changes.

**Root cause (shared across Tasks 28 + 29):**
Release-pipeline infrastructure was built before the compliance-lineage was explicitly scoped. Binary-metadata and third-party-license bundling were never specified as release requirements because the project framed itself as "independent" at the time.

**Process improvement:**
Both of these are now part of the release pipeline and will stay consistent automatically for all future releases. Future additions to `third_party/` should also add a pointer file in `packaging/third-party-licenses/` and confirm the install rule reaches it.

---

*(Subsequent entries will be appended as omissions are discovered and cured.)*
