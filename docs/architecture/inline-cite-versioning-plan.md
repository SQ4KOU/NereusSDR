# Inline Thetis Version Stamping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Require every new or modified `// From Thetis <file>:<line>` inline cite to carry a bracketed version stamp (`[v2.10.3.13]` or `[@abc1234]`) so upstream drift becomes visible at the point of use.

**Architecture:** A single regex pair in `check-new-ports.py` (diff mode only) enforces the rule; `HOW-TO-PORT.md` gains a grammar subsection; `CLAUDE.md` is refactored in three places so both human and AI contributors learn the rule during authoring; a project memory captures the expectation for future sessions. Grandfathering is automatic — the verifier only scans files in the PR diff, so pre-policy cites are never re-examined.

**Tech Stack:** Python 3 stdlib (`re`, `pathlib`), Markdown.

---

## File Structure

| Action | File | Responsibility |
|---|---|---|
| Create | `docs/architecture/inline-cite-versioning-plan.md` | This plan. |
| Create | `tests/compliance/test_cite_versioning.py` | Standalone pytest-free test: constructs fixture source files with known-good / known-bad cites, imports `check-new-ports.py` via `importlib`, asserts the cite-versioning scan flags only the bad ones. |
| Modify | `scripts/check-new-ports.py` | Add `RE_THETIS_CITE` + `RE_HAS_VERSION_STAMP` regexes; add a cite-scan pass that runs only in diff mode (FULL_TREE is False). |
| Modify | `docs/attribution/HOW-TO-PORT.md` | Append new subsection "Inline cite versioning" with grammar rules and three worked examples. |
| Modify | `CLAUDE.md` | Refactor three sections: "Constants and Magic Numbers", "Thetis origin comments" bullet in the C++ Style Guide, and the "READ" step of the SOURCE-FIRST PORTING PROTOCOL. |
| Create | `~/.claude/projects/-Users-j-j-boyd/memory/feedback_inline_cite_versioning.md` | Feedback memory describing the rule, why, and how to apply it. |
| Modify | `~/.claude/projects/-Users-j-j-boyd/memory/MEMORY.md` | Add one-line index entry pointing at the new memory. |

---

## Task 1: Add the failing test

**Files:**
- Create: `tests/compliance/test_cite_versioning.py`

- [ ] **Step 1: Write the failing test**

```python
#!/usr/bin/env python3
"""Test cite-versioning scan in scripts/check-new-ports.py.

Runs without pytest: `python3 tests/compliance/test_cite_versioning.py`.
Exit 0 on pass, 1 on failure. Designed to live alongside the other
compliance verifiers so it can be wired into pre-commit / CI later.
"""

import importlib.util
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SCRIPT = REPO / "scripts" / "check-new-ports.py"


def load_module():
    spec = importlib.util.spec_from_file_location("cnp", SCRIPT)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


FIXTURE_BAD = """\
// NereusSDR-native file (not registered — so heuristics can fire).
// From Thetis console.cs:4821 -- original value 0.98f
static constexpr float kAgcDecay = 0.98f;
"""

FIXTURE_GOOD_TAG = """\
// From Thetis console.cs:4821 [v2.10.3.13] -- original value 0.98f
static constexpr float kAgcDecay = 0.98f;
"""

FIXTURE_GOOD_SHA = """\
// From Thetis setup.cs:847 [@abc1234] -- per-protocol rate list
static constexpr int kDefaultRate = 192000;
"""

FIXTURE_GOOD_TAG_PLUS_SHA = """\
// From Thetis cmaster.cs:491 [v2.10.3.13+abc1234] -- CMCreateCMaster
void createCMaster() {}
"""

FIXTURE_MULTI_FILE = """\
// From Thetis console.cs:4821, setup.cs:847 [v2.10.3.13] -- ...
static constexpr float kX = 1.0f;
"""


def write_tmp(body: str) -> Path:
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".cpp", delete=False, dir=REPO
    )
    tmp.write(body)
    tmp.close()
    return Path(tmp.name)


def test_case(cnp, body: str, expect_flag: bool) -> bool:
    tmp = write_tmp(body)
    try:
        rel = str(tmp.relative_to(REPO))
        findings = cnp.check_file(rel, listed=set())
        has_cite_finding = any(
            "cite missing version stamp" in label
            for _line, label, _m in findings
        )
        ok = has_cite_finding is expect_flag
        label = "flagged" if has_cite_finding else "clean"
        want = "flag" if expect_flag else "clean"
        print(f"  {'PASS' if ok else 'FAIL'}: want {want}, got {label}")
        return ok
    finally:
        tmp.unlink()


def main() -> int:
    cnp = load_module()
    cnp.FULL_TREE = False  # cite-scan runs only in diff mode

    print("Bad: unstamped cite → should flag")
    bad = test_case(cnp, FIXTURE_BAD, expect_flag=True)

    print("Good: [v2.10.3.13] tag → should pass")
    g1 = test_case(cnp, FIXTURE_GOOD_TAG, expect_flag=False)

    print("Good: [@abc1234] sha → should pass")
    g2 = test_case(cnp, FIXTURE_GOOD_SHA, expect_flag=False)

    print("Good: [v2.10.3.13+abc1234] tag+sha → should pass")
    g3 = test_case(cnp, FIXTURE_GOOD_TAG_PLUS_SHA, expect_flag=False)

    print("Good: multi-file cite with single tag → should pass")
    g4 = test_case(cnp, FIXTURE_MULTI_FILE, expect_flag=False)

    passed = all([bad, g1, g2, g3, g4])
    print(f"\n{'ALL PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run test to verify it fails**

```bash
python3 tests/compliance/test_cite_versioning.py
```

Expected: FAIL — `cnp.check_file` does not yet emit any `cite missing version stamp` label. All "Good" cases would pass by accident (no flagging), but the "Bad" case will fail (expects a flag that the code doesn't produce).

- [ ] **Step 3: Commit the test**

```bash
git add tests/compliance/test_cite_versioning.py
git commit -m "test(compliance): failing test for Thetis inline-cite version stamping

Establishes the TDD baseline: cite-scan does not yet exist in
check-new-ports.py, so the unstamped-cite fixture passes through
without a finding. The upcoming verifier patch turns this red-to-green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Implement the cite-versioning scan in check-new-ports.py

**Files:**
- Modify: `scripts/check-new-ports.py` (add two regexes near the existing pattern block, extend `check_file` with a cite-scan loop that runs only in diff mode)

- [ ] **Step 1: Add the two regexes near the existing pattern block**

Locate the block (around line 100–110, just after `RE_AETHER_COMMENT`):

```python
RE_AETHER_COMMENT = re.compile(
    r"//\s*(Source|From|Ported from|Layout from|Pattern from).*\bAetherSDR\b",
    re.IGNORECASE,
)
```

Add immediately after:

```python
# Diff-mode-only: enforce that every new/modified `// From Thetis
# <file>.<ext>:<line>` cite carries a version stamp — either a Thetis
# release tag `[v2.10.3.13]` or a short SHA `[@abc1234]` (optionally
# combined `[v2.10.3.13+abc1234]` for post-tag fixes pulled before the
# next release).
RE_THETIS_CITE = re.compile(
    r"//\s*From\s+Thetis\s+[\w./-]+\.(?:cs|c|h|cpp)(?::\d+(?:[,\s]+\d+)*)"
)
RE_HAS_VERSION_STAMP = re.compile(
    r"\[(?:v\d+(?:\.\d+)+(?:\+[0-9a-f]{7,})?|@[0-9a-f]{7,})\]"
)
```

- [ ] **Step 2: Extend `check_file` with the cite-scan loop**

Locate `check_file` (around line 150). After the existing heuristic-pattern loop (ends with `return findings`), replace:

```python
    return findings
```

with:

```python
    # Cite-versioning scan (diff mode only — grandfathers pre-policy
    # cites by never running in full-tree mode). Runs INDEPENDENTLY of
    # the heuristic match above: a line can carry a Thetis cite AND a
    # separate Thetis tell; we want to report both if both apply.
    if not FULL_TREE:
        for i, line in enumerate(text.splitlines(), start=1):
            m = RE_THETIS_CITE.search(line)
            if not m:
                continue
            # Multi-line cite blocks: check the cite's own line AND the
            # next line (common pattern is cite-then-explanation split
            # across two lines). The stamp MUST be on the cite line.
            if RE_HAS_VERSION_STAMP.search(line):
                continue
            findings.append((
                i,
                "Thetis cite missing version stamp",
                m.group(0).strip(),
            ))

    return findings
```

- [ ] **Step 3: Extend the cure message to mention the new failure mode**

Locate the `Cure: add a PROVENANCE row...` print block (around line 200). Replace:

```python
        print(
            f"  Cure: add a PROVENANCE row + verbatim header per "
            f"docs/attribution/HOW-TO-PORT.md, OR add a "
            f"`// {OPT_OUT_MARKER} <X>.h interface` comment if "
            f"genuinely independent, OR add `// {NO_PORT_CHECK_MARKER} "
            f"<reason>` in the file head to suppress this check for a "
            f"legitimate false positive."
        )
```

with:

```python
        has_cite_issue = any(
            "cite missing version stamp" in label
            for _i, label, _m in findings
        )
        if has_cite_issue:
            print(
                f"  Cite cure: append `[vX.Y.Z.W]` (e.g. [v2.10.3.13])"
                f" or `[@shortsha]` (e.g. [@abc1234]) to the cite line"
                f" — see docs/attribution/HOW-TO-PORT.md §Inline cite"
                f" versioning. Run `git -C ../Thetis describe --tags`"
                f" or `git -C ../Thetis rev-parse --short HEAD` to get"
                f" the stamp."
            )
        print(
            f"  Attribution cure: add a PROVENANCE row + verbatim"
            f" header per docs/attribution/HOW-TO-PORT.md, OR add a"
            f" `// {OPT_OUT_MARKER} <X>.h interface` comment if"
            f" genuinely independent, OR add `// {NO_PORT_CHECK_MARKER}"
            f" <reason>` in the file head to suppress this check for a"
            f" legitimate false positive."
        )
```

- [ ] **Step 4: Run the test to verify it now passes**

```bash
python3 tests/compliance/test_cite_versioning.py
```

Expected output:
```
Bad: unstamped cite → should flag
  PASS: want flag, got flagged
Good: [v2.10.3.13] tag → should pass
  PASS: want clean, got clean
Good: [@abc1234] sha → should pass
  PASS: want clean, got clean
Good: [v2.10.3.13+abc1234] tag+sha → should pass
  PASS: want clean, got clean
Good: multi-file cite with single tag → should pass
  PASS: want clean, got clean

ALL PASS
```

- [ ] **Step 5: Regression-check the real tree (both modes)**

```bash
python3 scripts/verify-thetis-headers.py
python3 scripts/verify-provenance-sync.py
python3 scripts/check-new-ports.py --full-tree
```

Expected: all three green. The full-tree sweep must NOT flag any unstamped existing cite because `FULL_TREE is True` suppresses the new scan. If any existing cite is flagged, stop and investigate — the scan is leaking into full-tree mode.

- [ ] **Step 6: Commit the verifier change**

```bash
git add scripts/check-new-ports.py
git commit -m "feat(compliance): verifier check for Thetis inline-cite version stamps

Adds a diff-mode-only scan to check-new-ports.py that flags any
\`// From Thetis <file>.<ext>:<line>\` comment missing a bracketed
version stamp. The stamp is either a Thetis release tag
(\`[v2.10.3.13]\`), a short commit SHA (\`[@abc1234]\`), or the
combined tag+fix form (\`[v2.10.3.13+abc1234]\`).

Enforcement is PR-diff-only — pre-existing cites are grandfathered
and only re-checked when a PR modifies their line. Full-tree sweep
deliberately skips the cite scan so historical cites don't flood the
output. Once a file is touched for any reason, the cites on modified
lines get stamped opportunistically.

Why a per-cite stamp (vs header-level): one NereusSDR file frequently
ports bits from multiple Thetis sources at different upstream SHAs
(BoardCapabilities.cpp cites seven sources). A header-level stamp
loses that fidelity — the cite stamp captures which function/constant
was verified against which upstream commit, so Samphire's next
refactor of a ported constant is visible at the cite, not buried in
a cross-repo diff.

Test: tests/compliance/test_cite_versioning.py runs five fixtures
(one bad, four good including multi-file and tag+sha forms). All pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Document the grammar in HOW-TO-PORT.md

**Files:**
- Modify: `docs/attribution/HOW-TO-PORT.md` (append new subsection after the existing "Mechanization" section)

- [ ] **Step 1: Append the "Inline cite versioning" subsection**

Locate the end of the file (the "Mechanization" section is the final block today). Append:

```markdown

## Inline cite versioning

Every new or modified `// From Thetis <file>:<line>` comment in a
NereusSDR source file must carry a bracketed version stamp. This gives
upstream drift a visible anchor at the point of use — if Samphire
later changes the ported constant, function body, or behaviour, the
diff between our stamp and the latest Thetis release tells you exactly
how far behind we are.

### Grammar

```
// From Thetis <path>.<ext>:<line[, line…]> [<stamp>] — <explanation>
```

The stamp takes one of three forms:

| Form | When to use |
|---|---|
| `[vX.Y.Z.W]` | The port was verified against a tagged Thetis release. Grab from `git -C ../Thetis describe --tags`. Example: `[v2.10.3.13]`. |
| `[@shortsha]` | The port was verified against a between-tags commit. Grab from `git -C ../Thetis rev-parse --short HEAD`. Example: `[@abc1234]`. Minimum seven hex chars. |
| `[vX.Y.Z.W+shortsha]` | Rare: a tagged release has post-release fixes you pulled before the next tag landed. Example: `[v2.10.3.13+abc1234]`. |

### Placement

The stamp goes **immediately after the line number(s)**, before the
em-dash that introduces the explanation.

Correct:

```cpp
// From Thetis console.cs:4821 [v2.10.3.13] — original value 0.98f
static constexpr float kAgcDecay = 0.98f;
```

Wrong (stamp in explanation — verifier won't parse it):

```cpp
// From Thetis console.cs:4821 — v2.10.3.13 original 0.98f
```

Wrong (stamp before cite body):

```cpp
// From Thetis [v2.10.3.13] console.cs:4821 — original value 0.98f
```

### Multi-file cites

If a single cite references multiple Thetis files pulled at the same
version, one stamp applies to all of them:

```cpp
// From Thetis console.cs:4821, setup.cs:847 [v2.10.3.13] — …
```

If the files were pulled at **different** versions, split into two
cites — one per version — on consecutive lines:

```cpp
// From Thetis console.cs:4821 [v2.10.3.13] — original value 0.98f
// From Thetis setup.cs:847 [v2.10.3.15] — refreshed when rate-list
//    picked up the 44.1 kHz entry
static constexpr float kAgcDecay = 0.98f;
```

### Grandfathering

Pre-policy cites (shipped before this rule existed) are NOT rewritten
as a sweep — the verifier runs only on files changed in a PR, so
untouched cites stay as-is. When a file is edited for any reason, any
cite on a modified line must be stamped before the PR merges. This
keeps the cost proportional to churn.

### Header vs cite

The file's top-of-file header mod-history block does NOT carry a
version stamp. Headers record who/when/what-capability ("Reimplemented
in C++20/Qt6 … layout ports FM tab"); versions live on the cites so
per-function fidelity is preserved even when one file draws from
multiple Thetis versions over its lifetime.
```

- [ ] **Step 2: Commit the doc update**

```bash
git add docs/attribution/HOW-TO-PORT.md
git commit -m "docs(compliance): HOW-TO-PORT §Inline cite versioning

Adds a grammar reference for the new \`[vX.Y.Z.W]\` / \`[@shortsha]\`
/ \`[vX.Y.Z.W+shortsha]\` stamp format that check-new-ports.py now
enforces on diff-mode new cites.

Covers: the three stamp forms, required placement (after line numbers,
before the em-dash), multi-file cite handling (same version = one
stamp, different versions = split cites), grandfathering policy, and
the header-vs-cite responsibility split.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Refactor CLAUDE.md — Constants and Magic Numbers

**Files:**
- Modify: `CLAUDE.md` (section "Constants and Magic Numbers" inside SOURCE-FIRST PORTING PROTOCOL)

- [ ] **Step 1: Update the example and add the version-stamp sentence**

Locate the section (currently reads):

```markdown
### Constants and Magic Numbers

Preserve ALL constants, thresholds, scaling factors, and magic numbers exactly
as they appear in Thetis. If Thetis uses `0.98f`, NereusSDR uses `0.98f`. If
Thetis uses `2048` as a buffer size, document where it came from and keep it.
Give constants a `constexpr` name but note the Thetis origin in a comment:

```cpp
// From Thetis console.cs:4821 — original value 0.98f
static constexpr float kAgcDecayFactor = 0.98f;
```
```

Replace the code block and add a follow-on sentence so the section reads:

```markdown
### Constants and Magic Numbers

Preserve ALL constants, thresholds, scaling factors, and magic numbers exactly
as they appear in Thetis. If Thetis uses `0.98f`, NereusSDR uses `0.98f`. If
Thetis uses `2048` as a buffer size, document where it came from and keep it.
Give constants a `constexpr` name but note the Thetis origin — with a
version stamp — in a comment:

```cpp
// From Thetis console.cs:4821 [v2.10.3.13] — original value 0.98f
static constexpr float kAgcDecayFactor = 0.98f;
```

The `[v2.10.3.13]` tag records the Thetis release the value was verified
against. Use `[@shortsha]` when no tagged release applies, and refresh the
stamp whenever you re-port from a newer upstream. Full grammar:
`docs/attribution/HOW-TO-PORT.md` §Inline cite versioning.
```

- [ ] **Step 2: Do NOT commit yet** — next task touches another CLAUDE.md section; we batch CLAUDE.md edits into one commit.

---

## Task 5: Refactor CLAUDE.md — Thetis origin comments bullet

**Files:**
- Modify: `CLAUDE.md` (bullet inside "C++ Style Guide")

- [ ] **Step 1: Expand the bullet**

Locate the bullet (currently reads):

```markdown
* **Thetis origin comments**: when porting logic, add `// From Thetis [file]:[line or function]` comments
```

Replace with:

```markdown
* **Thetis origin comments**: when porting logic, add `// From Thetis [file]:[line or function] [v<version>|@<shortsha>]` comments. The bracketed stamp records the upstream release or commit the port was verified against; grab it from `git -C ../Thetis describe --tags` (or `rev-parse --short HEAD`) at the moment of porting. Full grammar and placement rules: `docs/attribution/HOW-TO-PORT.md` §Inline cite versioning
```

- [ ] **Step 2: Do NOT commit yet** — next task touches the third CLAUDE.md section.

---

## Task 6: Refactor CLAUDE.md — SOURCE-FIRST PORTING PROTOCOL READ step

**Files:**
- Modify: `CLAUDE.md` (step 1 of "The Rule: READ → SHOW → TRANSLATE")

- [ ] **Step 1: Add the version-capture instruction to the READ step**

Locate (currently reads):

```markdown
1. **READ** the relevant Thetis source file(s). Use `find`, `grep`, or `rg`
   to locate the C# code. The Thetis repo should be cloned at
   `../Thetis/` (relative to the NereusSDR root).
```

Replace with:

```markdown
1. **READ** the relevant Thetis source file(s). Use `find`, `grep`, or `rg`
   to locate the C# code. The Thetis repo should be cloned at
   `../Thetis/` (relative to the NereusSDR root). Capture the Thetis
   version tag once at the start of the session — `git -C ../Thetis
   describe --tags` (release) or `git -C ../Thetis rev-parse --short
   HEAD` (between releases) — every inline cite you write in this
   session gets that stamp.
```

- [ ] **Step 2: Commit all three CLAUDE.md edits together**

```bash
git add CLAUDE.md
git commit -m "docs(claude): teach inline-cite version stamping rule

Three surgical edits to CLAUDE.md so both human and AI contributors
learn the rule during the authoring flow, not at merge time:

1. SOURCE-FIRST PORTING PROTOCOL, READ step — adds the
   \`git -C ../Thetis describe --tags\` / \`rev-parse --short HEAD\`
   capture instruction, so the stamp is known before the first cite is
   written.
2. Constants and Magic Numbers — updates the worked example to include
   [v2.10.3.13] and adds a short sentence explaining when to use
   [@shortsha] + when to refresh the stamp.
3. C++ Style Guide, 'Thetis origin comments' bullet — expands to cover
   the bracketed stamp form and points at HOW-TO-PORT.md for full
   grammar.

All three sections defer the grammar spec to
docs/attribution/HOW-TO-PORT.md §Inline cite versioning (added in the
previous commit) to avoid duplication drift.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Commit the plan doc itself

**Files:**
- Already created: `docs/architecture/inline-cite-versioning-plan.md` (this file)

- [ ] **Step 1: Stage and commit**

```bash
git add docs/architecture/inline-cite-versioning-plan.md
git commit -m "docs(plan): inline-cite version stamping plan

Implementation plan for the Thetis-cite version-stamping initiative —
motivates the per-cite stamp over a header-only stamp, specifies the
three stamp forms ([vX.Y.Z.W] / [@shortsha] / [vX.Y.Z.W+shortsha]),
enumerates all touched files, spells out the verifier regex + cure
message, and captures the grandfathering policy (PR-diff-only).

Committed alongside the change set so future audits can trace the
rationale back to the original spec. Follows feedback_plans_in_architecture
memory — plans live in docs/architecture/, not docs/superpowers/plans/.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Add the project memory

**Files:**
- Create: `/Users/j.j.boyd/.claude/projects/-Users-j-j-boyd/memory/feedback_inline_cite_versioning.md`
- Modify: `/Users/j.j.boyd/.claude/projects/-Users-j-j-boyd/memory/MEMORY.md` (add index line)

- [ ] **Step 1: Write the memory file**

```markdown
---
name: Inline Thetis cite versioning
description: Every new/modified `// From Thetis` cite must carry `[vX.Y.Z.W]` or `[@shortsha]` stamp; grandfathered cites are untouched
type: feedback
---

Every new or modified `// From Thetis <file>:<line>` inline cite in a
NereusSDR source file must carry a bracketed version stamp:

- `[vX.Y.Z.W]` for tagged Thetis releases
- `[@shortsha]` for between-tags commits (seven-char minimum)
- `[vX.Y.Z.W+shortsha]` for post-release fixes pulled before the next tag

Placement: immediately after the `:<line>` token, before the em-dash.

**Why:** One NereusSDR file often ports bits from multiple Thetis files
at different upstream commits (BoardCapabilities.cpp cites seven
sources; RxChannel.cpp cites seven). A header-level version stamp loses
per-function fidelity. The per-cite stamp makes upstream drift visible
at the point of use — when Samphire changes a constant or refactors a
function, the difference between our stamp and the latest Thetis
release tells you exactly how far behind we are.

**How to apply:**
- At the start of any porting session, run `git -C ../Thetis describe --tags`
  (or `rev-parse --short HEAD` if no tag applies) once; use that stamp
  on every cite written in the session.
- When editing existing cites, bring the modified cite up to the new
  standard. Untouched cites are grandfathered — no mass backfill PR.
- Verifier: `scripts/check-new-ports.py` (diff mode only) flags any
  unstamped cite in a PR-changed file. Full-tree mode deliberately
  skips the check so grandfathered cites don't flood CI.
- Grammar reference: `docs/attribution/HOW-TO-PORT.md` §Inline cite
  versioning.
- Header mod-history does NOT get a version stamp — versions live on
  the cite, because header entries span multiple upstream commits.
```

- [ ] **Step 2: Add the MEMORY.md index line**

Locate the existing index line for `Byte-for-byte Thetis attribution`:

```markdown
- [Byte-for-byte Thetis attribution](feedback_thetis_attribution_rules.md) — exact headers per source file, multi-file needs all headers, preserve all inline comments verbatim
```

Insert immediately after:

```markdown
- [Inline Thetis cite versioning](feedback_inline_cite_versioning.md) — `// From Thetis file:line` cites need `[vX.Y.Z.W]` or `[@sha]` stamp; PR-diff enforced, grandfathered otherwise
```

- [ ] **Step 3: Memory edits do not get committed (live outside the repo)**

No `git add` — these paths live in `~/.claude/projects/-Users-j-j-boyd/memory/`, not the NereusSDR repo.

---

## Task 9: Full verifier sweep + push + open PR

**Files:**
- None modified; just validation and push.

- [ ] **Step 1: Run every verifier one more time**

```bash
python3 scripts/verify-thetis-headers.py
python3 scripts/verify-provenance-sync.py
python3 scripts/check-new-ports.py --full-tree
python3 tests/compliance/test_cite_versioning.py
```

Expected: four green lines (205/205, 203/203, 320/320 + cite test ALL PASS).

- [ ] **Step 2: Build check**

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(sysctl -n hw.ncpu)
```

Expected: `[246/246] Linking CXX executable NereusSDR.app/...` (or whatever the current link count is on post-PR-#51 main). No errors.

- [ ] **Step 3: Push the branch**

```bash
git push -u origin chore/inline-cite-versioning
```

- [ ] **Step 4: Open PR (draft body — show to user for approval before posting per `feedback_review_public_posts`)**

Show the drafted PR body in chat, wait for explicit "post it" / "go" before running `gh pr create`. Body template:

```markdown
## Summary

Follow-up to PR #51. One NereusSDR file frequently ports bits from
multiple Thetis sources at different upstream commits — today's cite
format (`// From Thetis console.cs:4821 — original value 0.98f`)
captures where but not when, so upstream drift is invisible until
someone cross-reads both repos.

This PR introduces a bracketed version stamp on every new/modified
`// From Thetis` cite and enforces it via diff-mode
`check-new-ports.py`. Pre-policy cites are grandfathered.

## What's in here

- **6 commits, all GPG-signed**
- Plan doc: `docs/architecture/inline-cite-versioning-plan.md`
- Verifier: `scripts/check-new-ports.py` + `tests/compliance/test_cite_versioning.py`
- Grammar: `docs/attribution/HOW-TO-PORT.md` §Inline cite versioning
- CLAUDE.md: three surgical refactors (READ step, constants example,
  style-guide bullet)

## Stamp grammar

| Form | When |
|---|---|
| `[v2.10.3.13]` | Tagged Thetis release |
| `[@abc1234]` | Between-tags commit |
| `[v2.10.3.13+abc1234]` | Post-release fix pulled before next tag |

Placement: after `:<line>`, before the em-dash. Full rules and
examples in HOW-TO-PORT.md.

## Test plan

- [x] `tests/compliance/test_cite_versioning.py` — 5 fixtures pass
- [x] `verify-thetis-headers.py` — 205/205
- [x] `verify-provenance-sync.py` — 203/203
- [x] `check-new-ports.py --full-tree` — 320/320 (cite scan skipped
      in full-tree mode; no regression on grandfathered cites)
- [x] `cmake --build build` — clean
- [ ] CI green
- [ ] First follow-up PR touches a couple of existing cites to verify
      the enforcement loop works end-to-end on real change

J.J. Boyd ~ KG4VCF
```

- [ ] **Step 5: After approval, create the PR and open it in the browser**

```bash
gh pr create --title "compliance: inline Thetis-cite version stamping + verifier" \
    --body "$(...heredoc from Step 4...)"
open "$(gh pr view --json url -q .url)"
```

---

## Notes on execution

- **Branch + worktree already set up**: `chore/inline-cite-versioning` on `~/NereusSDR-cite-ver/`, tracking `origin/main` at `86d1b3c` (post-PR-#51 merge).
- **Commit cadence**: each task commits independently (7 commits total — Task 4/5/6 batch into one CLAUDE.md commit; Task 8 touches memory files that live outside the repo and doesn't commit). Pre-commit hook will run all verifiers on every commit.
- **GPG signing**: standing config (`commit.gpgsign=true`, key `20C284473F97D2B3`) applies — no extra flags needed.
- **If the verifier flags a real unstamped cite in step 5 of Task 2**: that's a real historical-gap case leaking into full-tree mode; stop and investigate before continuing. The expectation is zero flags in full-tree mode.
