# How to Port a File from Thetis / mi0bot-Thetis / any GPL Upstream

When you (or an AI agent) port code from a GPL-licensed upstream into
NereusSDR, the file's license header is handled this way:

1. Locate the upstream source file that you ported from.
2. Copy the upstream file's **entire header block** — from the top of the
   file through the end of the copyright / GPL / dual-license region —
   **verbatim, character-for-character**. No substitutions, no
   reformatting, no address modernization, no contributor consolidation.
3. Prepend a short NereusSDR port-citation block stating the source
   file(s) followed by a Modification-History block stating the port
   date, human author, and AI tooling (if any). Format:

   ```cpp
   // =================================================================
   // <repo-relative path>  (NereusSDR)
   // =================================================================
   //
   // Ported from <upstream> source:
   //   <path>, original licence from <upstream> source is included below
   //
   // =================================================================
   // Modification history (NereusSDR):
   //   YYYY-MM-DD — Reimplemented in C++20/Qt6 for NereusSDR by <name>
   //                 (<callsign>), with AI-assisted transformation via
   //                 <tool> if applicable.
   // =================================================================
   ```

4. For multi-source files: stack each cited source's verbatim header
   block in citation order, blank comment line between.
5. For sources with no header (e.g. NetworkIO.cs): NereusSDR block +
   note `Upstream source has no top-of-file GPL header — project-level
   LICENSE applies`. No fabrication.
6. For upstream projects with no per-file headers (e.g. AetherSDR):
   reference the project's URL and primary author at NereusSDR block
   level; there is no verbatim block to copy.

Additionally: every ported function / block / inline constant that
carries an inline attribution marker in the upstream source
(`//-W2PA`, `//MW0LGE [x.y.z]`, `// added by G8NJJ for X`, etc.) MUST
preserve that marker at the corresponding position in the NereusSDR
port. When NereusSDR itself makes post-port modifications inside such a
marked region, add a NereusSDR marker in the same style using the
NereusSDR release version tag: `//-KG4VCF [v0.2.0] description`.

See `docs/attribution/THETIS-PROVENANCE.md` for the file mapping and
`docs/attribution/REMEDIATION-LOG.md` for historical cure entries.

This is a merge-blocking requirement, not a style preference. A PR that
ports without preserving the source's verbatim header will not be
merged.

## Mechanization

The script `scripts/rewrite-verbatim-headers.py` applies rules 1–5
automatically for all files listed in the PROVENANCE derivative tables.
`scripts/verify-thetis-headers.py` is the merge-gate check — it
confirms each file carries the required anchor markers (`Ported from`,
`Thetis`, `Copyright (C)`, `General Public License`, `Modification
history (NereusSDR)`).

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

The verifier enforces stamps on cites whose upstream file ends in
`.cs`, `.c`, `.h`, or `.cpp` — code sources where upstream drift
meaningfully affects NereusSDR logic. `.resx` cites (Thetis resource
strings, e.g. tooltip copy) are deliberately out of scope; they
reference display text that doesn't drift the same way.

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

## Pre-port checklist (Ring 1, authoring-time)

Moved from CLAUDE.md. Before reading any upstream source file, state:

1. **Upstream file** you're about to read.
2. **NereusSDR file(s)** the port will touch (new or existing).
3. **Provenance status** of each NereusSDR file:
    ```
    grep -l "<nereussdr-path>" docs/attribution/THETIS-PROVENANCE.md
    ```
   For freedv-gui ports, also check `docs/attribution/FREEDV-GUI-PROVENANCE.md`.
   If the file is not registered, the port is a **new attribution event**.
4. **Plan**: if (3) returned nothing, add the verbatim upstream header AND a
   PROVENANCE row in the same commit that introduces the ported logic.

If you cannot answer (3) confidently, stop and grep before continuing.

This applies equally to new files that port upstream logic, edits to
NereusSDR-original files that **add** ported logic (e.g. a new
Thetis-derived constant or formula), and ports from non-Thetis upstreams
(`../mi0bot-Thetis/`, `../AetherSDR/`, `../freedv-gui/`, WDSP): same
protocol, different PROVENANCE table.

Verifier scripts (`scripts/verify-thetis-headers.py`,
`scripts/verify-freedv-headers.py`, `scripts/check-new-ports.py`) are the
safety net (Ring 3, in CI). The local pre-commit hook installed via
`scripts/install-hooks.sh` runs the same scripts (Ring 2). The primary
control is this checklist.

## Inline comment preservation: what counts and how it is enforced

Moved from CLAUDE.md. A real incident (2026-04-21) shipped with a
`//DH1KLM` tag silently dropped during a `computeAlexFwdPower` port, caught
only because someone eyeballed the PR.

All inline comments from upstream source within ported logic must be
preserved verbatim. This includes:

- **Developer attribution tags**: `//DH1KLM`, `//MW0LGE`, `//W2PA`,
  `//G8NJJ`, `//MI0BOT`, etc. Canonical list in
  `docs/attribution/thetis-author-tags.json`, built by
  `scripts/discover-thetis-author-tags.py`.
- **Dash-prefix attribution**: `//-W2PA`, `// -W2PA`
- **Version-tagged attribution**: `//[2.10.3.13]MW0LGE`, `//MW0LGE [2.9.0.7]`
- **Underscored variants**: `//MW0LGE_21k5 change to rx2`
- **Behavioral notes**: `// only cleared by getAndResetADC_Overload()`
- **TODO / FIXME / XXX / HACK** annotations
- Any `//` comment on or above a ported line of logic

When the translation restructures code so the comment no longer sits on the
same line, place it on the nearest equivalent line with a note:
```cpp
// MW0LGE_21k5 change to rx2  [original inline comment from display.cs:10079]
```

**Mechanical enforcement:** `scripts/verify-inline-tag-preservation.py` runs
in the pre-commit hook chain and in CI. For every `// From Thetis X:N [@sha]`
cite in the diff, it opens `../Thetis/X` (or `../mi0bot-Thetis/X`) at line N,
extracts any author tag within ±5 source lines, and fails the commit if a
corresponding tag is not present within ±10 port lines. If it fires,
re-insert the verbatim tag exactly as it appears upstream.

**Corpus drift:** after re-syncing Thetis (`git -C ../Thetis pull`), run
`python3 scripts/discover-thetis-author-tags.py` to refresh the corpus. CI's
`--drift` check fails the PR if new upstream contributors aren't in the
committed corpus.

## Thetis source layout

```
../Thetis/Project Files/Source/
├── Console/          Main UI, radio logic, state management
│   ├── console.cs    Monster file: VFO, band, mode, DSP, display
│   ├── setup.cs      Setup dialog (hardware config, DSP params)
│   ├── display.cs    Spectrum/waterfall rendering
│   ├── audio.cs      Audio engine, VAC, portaudio
│   ├── cmaster.cs    Channel master (WDSP channel management)
│   ├── dsp.cs        WDSP P/Invoke declarations
│   ├── NetworkIO.cs  Protocol 1/2 network I/O
│   └── protocol2.cs  Protocol 2 specific handling
└── wdsp/             WDSP C source: channel.c, RXA.c, TXA.c, ...
```

freedv-gui: `../freedv-gui/src/reporting/` (FreeDVReporter, pskreporter),
`src/pipeline/` (RADE RX/TX steps, rade_text, EQ, AGC). Full tree in
`docs/development/project-status.md`.

## Gateware citations (n1gp-Anvelina_PROIII)

Moved from CLAUDE.md. The gateware is GPLv3, the same licence NereusSDR ships
under, so there is no licence conflict. The constraint is scope and
correctness:

* **Normal use**: cite a *fact* the gateware establishes (receiver count,
  board-type byte, clock rate, register width), e.g.
  `// From n1gp-Anvelina_PROIII Orion.v:958 [@8e86a61] — NR = 8`. Prefer this
  over a Thetis cite whenever the claim is about hardware. PROVENANCE kind
  `reference`, not `port`.
* **Stop and ask first** before translating Verilog *logic*. Gateware logic
  runs on the radio, not the client, so needing it usually means the design
  took a wrong turn. If genuinely needed, the full port protocol applies
  (verbatim header, PROVENANCE kind `port`, author tags preserved).
* The gateware carries its own author tags (`Yurij-eu2av` in `Orion.v`);
  quoted gateware comments follow the inline-comment-preservation rule.
* `NR` is a compile-time constant that changed between firmware releases
  (2, 4, 7, 8 on the same board). No static per-board DDC count is correct
  across firmware versions.

## Which Thetis features are ported

Moved from CLAUDE.md (the Thetis entry in its reference-repository list).

Feature source: when NereusSDR ports a Thetis feature, Thetis defines how it behaves.
Not every Thetis capability is ported: NereusSDR keeps its own design (slices, VAX,
the remote Core), and a Thetis feature comes in when it fits that design and serves
operators. Features built around Thetis's VFO A/B or RX1/RX2 structure are not forced in.

## piHPSDR and deskhpsdr citations

Moved from CLAUDE.md (reference-repository entry 8 and the rule under its
gateware section).

* **piHPSDR (dl1ycf)** - `https://github.com/dl1ycf/pihpsdr`
   * **Clone to `../pihpsdr/` relative to NereusSDR root.** Pinned at SHA
     `4aa95c5` (2026-08-06). GPLv3-or-later, the same licence NereusSDR ships
     under, so there is no compatibility blocker.
   * A mature C/GTK OpenHPSDR console with its own client/server remote mode.
     Added 2026-08-08 after the maintainer evaluated it as a possible
     alternative base and **decided against it**. That evaluation is the entry's
     value: it is a second independent implementation of problems we are
     solving, useful as design evidence and as a source of field-proven numbers.
   * **The architecture decision is settled: we keep ours.** Their core is not
     GUI-free (`src/receiver.h:190-192` holds `GtkWidget *panel`, `*panadapter`
     and `*waterfall` inside `struct RECEIVER`; 150 of 219 source files include
     `gtk/gtk.h`; no automated test suite). NereusSDR is already split into
     `NereusCore` / `NereusGui` with `nereusd` as a real target. Do not propose
     adopting their structure.
   * Do **not** port their command set. Our generic `StateMirror` over 144
     `Q_PROPERTY` declarations is deliberately better than their 114
     hand-enumerated `CMD_*` types, because a new property mirrors for free
     instead of costing two handlers.
   * Useful facts cited so far: the remote-mode socket timeouts at
     `src/server_thread.c:925-934` (15 s heartbeat, 30 s receive, 5 s send, a
     shipping configuration on real internet links), and four R3 display-codec
     observations recorded in
     `docs/architecture/2026-08-03-remote-daemon-r2-r3-design-addendum.md` §12.
   * **`dl1bz/deskhpsdr`**, a piHPSDR desktop fork, is also cloned at
     `../deskhpsdr/` (SHA `f3d857c`) and falls under the same rules.

**The same rule governs piHPSDR and deskhpsdr.** Both are GPLv3-or-later, so
again the constraint is scope and correctness rather than law. Cite a fact (a
timeout, a packet layout, a bitrate, a quantiser step) with a
`// From piHPSDR src/server_thread.c:930 [@4aa95c5]` style cite and a PROVENANCE
row of kind `reference`. **Stop and ask before translating any of their logic**,
because the maintainer has already ruled that we keep our architecture, so
needing their code almost certainly means a design took a wrong turn. Quoting one
of their comments verbatim triggers the inline-comment-preservation rule exactly
as a Thetis tag would.
