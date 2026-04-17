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

*(Subsequent entries will be appended as omissions are discovered and cured.)*
