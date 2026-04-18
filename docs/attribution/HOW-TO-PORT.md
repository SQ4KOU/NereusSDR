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
