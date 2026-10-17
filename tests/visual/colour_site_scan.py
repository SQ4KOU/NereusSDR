#!/usr/bin/env python3
# no-port-check: NereusSDR-original visual review tool; lists the chrome
# colours that never pass through a stylesheet.
#
# tests/visual/colour_site_scan.py  (NereusSDR)
#
# Lists every painted, Qt global, per-widget palette and rich-text HTML
# colour site in the src/gui chrome files and reports each one as
#   mapped     - it goes through WorkspacePreview::roleColour(), or names a
#                workspace role the way the older call sites do;
#   kept       - it is on KEPT below, with its reason;
#   undecided  - neither.
# Stylesheet literals are not listed here: QssTranslator and its table
# (StylesheetColourTable) decide those.
#
# Not wired into CI or the pre-commit hook. Run from the repository root:
#   python3 tests/visual/colour_site_scan.py            summary
#   python3 tests/visual/colour_site_scan.py --list     every site
#   python3 tests/visual/colour_site_scan.py --undecided
# The exit status is 0 either way.
#
# Modification history (NereusSDR):
#   2026-10-08 - Written for the workspace visual review. J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GUI = ROOT / "src" / "gui"

# Not chrome, or not this pass's business. Each with its reason.
EXCLUDED_DIRS = {
    "src/gui/styles": "mechanism: the palette, translator and preview switch",
    "src/gui/spectrum": "data: spectrum and waterfall painting",
    "src/gui/meters": "data: meter items",
    "src/gui/instruments": "data: Longpath instruments",
    "src/gui/containers/meter_property_editors": "data: meter item colour editors",
}
EXCLUDED_FILES = {
    # Data files (colour-coverage-inventory section 3, "Data vs chrome").
    "SpectrumWidget": "data: spectrum plot (also a skipped class)",
    "SMeterWidget": "data: meter face",
    "VintageMeterFace": "data: meter face",
    "VintageModMeterWidget": "data: meter face",
    "HGauge": "data: gauge",
    "DexpPeakMeter": "data: meter",
    "ParametricEqWidget": "data: EQ curves",
    "AmpViewChart": "data: chart",
    "DiversityRadarWidget": "data: radar plot",
    "FilterPassbandWidget": "data: passband plot",
    "ImdOverlay": "data: spectrum overlay",
    "DssRenderer": "data: renderer",
    "VfoLevelBar": "data: level bar (also a skipped class)",
    "MeterSlider": "data: level slider",
    "GradientPickerWidget": "data: user gradient (also a skipped class)",
    "GradientStop": "data: user gradient",
    "PsaIndicatorWidget": "data: PureSignal banner states, as Thetis",
    "OcLedStripWidget": "data: LED strip",
    "AppearanceSetupPages": "data: display colour defaults",
    "DisplaySetupPages": "data: display colour defaults",
    "SpectrumPeaksPage": "data: peak colour defaults",
    "TimeSeriesGraphWidget": "data: graph",
    "ModMonitorApplet": "data: modulation scope",
    "BandwidthFilterPane": "data: filter plot",
    "EqApplet": "data: EQ curves",
    # The receiver flag's controls take the look through the live pass
    # (JJ, 2026-10-11); what is painted or written as rich text is data.
    "VfoWidget": "data: the flag's painted panel edge, slice colors and RADE row (controls are themed)",
    "VfoStyles": "data: the flag's painted color constants (its sheets are themed)",
}

HEX = r"#[0-9A-Fa-f]{3,8}"
GLOBALS = (r"white|black|red|green|blue|cyan|magenta|yellow|gray|darkGray|"
           r"lightGray|darkRed|darkGreen|darkBlue|darkCyan|darkMagenta|"
           r"darkYellow")

PATTERNS = [
    ("qcolor-hex", re.compile(
        r"QColor(?:\s+\w+)?\s*[({]\s*(?:QStringLiteral\(|QLatin1String\(|QString\(|"
        r"QString::fromLatin1\()?\s*\"" + HEX + "\"")),
    ("qcolor-num", re.compile(
        r"QColor(?:\s+\w+)?\s*[({]\s*(?:0x[0-9A-Fa-f]+|\d+)\s*,|"
        r"QColor::fromRgb\w*\(|\bqRgba?\(")),
    ("qcolor-style", re.compile(r"QColor(?:\s+\w+)?\s*[({]\s*Style::k\w+")),
    ("global", re.compile(r"\bQt::(?:" + GLOBALS + r")\b")),
    ("palette-role", re.compile(
        r"\.set(?:Color|Brush)\(\s*(?:QPalette::\w+\s*,\s*)?QPalette::\w+")),
    ("set-palette", re.compile(r"\bsetPalette\(")),
    ("html", re.compile(
        r"<(?:span|b|font|p|h\d|ol|ul|li|a|div|td)\b[^>]*"
        r"(?:color\s*:|color\s*=)")),
]

# How a site reaches a role.
#  - A colour site (QColor, Qt global) is mapped when it is the colour
#    argument of roleColour() (or of the older call-site forms pick()),
#    across line breaks: roleColour(QColor(...), "role").
#  - A statement site (per-widget palette, setPalette, HTML) is mapped
#    when its statement (to the next ';') calls roleColour(),
#    roleColourName() or names a role the way the older call sites do
#    (Workspace::role("..."), glassAndDepth()).
COLOUR_KINDS = {"qcolor-hex", "qcolor-num", "qcolor-style", "global"}
WRAPS_ROLE = re.compile(r"(?:roleColour|\bpick)\(\s*(?:QColor\s*[({]\s*)?$")
STATEMENT_ROLE = re.compile(
    r"WorkspacePreview::roleColour(?:Name)?\(|\broleColour(?:Name)?\(|"
    r"glassAndDepth\(\)|\brole\(\"")

# Definitions whose colour reaches a role where it is used: (file stem,
# regex on the line, where). Reported as mapped.
ROUTED = [
    ("ConnectionPanel", r"static constexpr QColor kColorOnline(?:Free|InUse)\s*\{",
     "roleColour() where the row is painted, applyRowColor()"),
    ("ConnectionPanel", r"static constexpr QColor kColorOffline\s*\{",
     "roleColour() where the offline rows are painted"),
    ("ContainerWidget", r"^\s*setPalette\(surface\);",
     "the palette is built from Workspace::role(\"panel\") on the line above"),
    ("FreeDVReporterDialog", r"QBrush\(rowText\)",
     "rowText is roleColour(white, \"text\") on the line above"),
    ("EdgeDockStrip", r"w->setPalette\(palette\);",
     "the palette is built from role(\"app-bg\") on the line above"),
]

# Kept sites: (file stem, regex on the line, reason). A site is kept
# when its file stem matches and the regex finds the line. The reasons
# follow the stylesheet kept list (StylesheetColourTable).
KEPT = [
    # Lit and checked states (render as today).
    ("TriBtn", r"isDown\(\) \? QColor\(0, 0, 0\)",
     "lit: the pressed triangle, black on the accent fill"),
    ("FreeDVReporterDialog", r"static const QColor k(?:Tx|Rx|Msg)BackgroundColor\(",
     "lit: the TX, RX and message row highlights, freedv-gui's event tints"),
    ("FreeDVReporterDialog", r"setBrush\(QPalette::Highlight, QBrush\(bg\)\)",
     "lit: the event tint as the selection colour of a highlighted row"),
    # Data shown inside chrome.
    ("ColorSwatchButton", r"QColor\(Qt::black\)",
     "data: the swatch's fallback for an invalid user colour"),
    ("MainWindow", r"txWfLowColor\(\) : QColor\(Qt::black\)",
     "data: the waterfall low colour handed to a meter item"),
    ("SpectrumOverlayPanel", r"getColor\(QColor\(\"#00e5ff\"\)",
     "data: the spectrum fill colour default offered to the colour dialog"),
    ("ContainerManager", r"QColor::fromRgb\(ControlRanges::sliceColour",
     "data: the slice colour on a source chip"),
    ("ContainerSettingsDialog", r"initial=QColor\(\"#0f0f1a\"\)",
     "data: the container background default offered to the colour dialog"),
    ("ContainerWidget", r"QColor m_backgroundColor\{",
     "data: the container background setting's default"),
    # Shadows: black with an alpha is a shade, not a colour.
    ("ContainerWidget", r"setColorAt\(\d\.\d, QColor\(0, 0, 0,",
     "shadow: the reveal shade, black with alpha"),
    ("ProfileRailDock", r"shadow->setColor\(QColor\(0, 0, 0, kShadowAlpha\)\)",
     "shadow: the popover's drop shadow"),
    # Already workspace values.
    ("EdgeDockStrip", r"QColor::fromRgba\(EdgeDockArea::kPreviewBorder\)",
     "workspace value: the drop preview edge is the selected-border value"),
    ("PillStyle", r"Style::shiftL\(QColor\(Style::k",
     "workspace value: Style here is InstrumentStyle, the Longpath palette"),
    ("WorkspaceProfilesDialog", r"<span style=\\\"color:%2;",
     "workspace value: %2 is latin(kGlassSelText), the Longpath palette"),
]


def statement(code, start):
    """The code from `start` to the end of its statement: the next ';'
    outside a string or character literal."""
    i, quote = start, None
    while i < len(code):
        ch = code[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == ";":
            return code[start:i + 1]
        i += 1
    return code[start:]


def chrome_files():
    for path in sorted(GUI.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        rel = path.relative_to(ROOT).as_posix()
        if any(rel == d or rel.startswith(d + "/") for d in EXCLUDED_DIRS):
            continue
        if path.stem in EXCLUDED_FILES:
            continue
        yield path, rel


def code_part(line):
    """The line without a trailing // comment (a crude cut that leaves
    URLs in strings alone), or "" for a comment-only line."""
    st = line.strip()
    if st.startswith("//") or st.startswith("*") or st.startswith("/*"):
        return ""
    cut = re.search(r"(?<![:\"'])//(?!/?\S*\")", line)
    return line[:cut.start()] if cut else line


def scan():
    sites = []
    for path, rel in chrome_files():
        raw_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        lines = []
        in_block = False
        for raw in raw_lines:
            if in_block:
                lines.append("")
                if "*/" in raw:
                    in_block = False
                continue
            if raw.strip().startswith("/*") and "*/" not in raw:
                in_block = True
                lines.append("")
                continue
            lines.append(code_part(raw))
        starts = []
        pos = 0
        for line in lines:
            starts.append(pos)
            pos += len(line) + 1
        code = "\n".join(lines)
        for i, line in enumerate(lines):
            if not line:
                continue
            for kind, rx in PATTERNS:
                for m in rx.finditer(line):
                    at = starts[i] + m.start()
                    status, reason = "undecided", ""
                    if kind in COLOUR_KINDS:
                        before = code[max(0, at - 160):at]
                        if WRAPS_ROLE.search(before):
                            status = "mapped"
                    elif STATEMENT_ROLE.search(statement(code, starts[i])):
                        status = "mapped"
                    if status == "undecided":
                        for stem, rrx, why in ROUTED:
                            if path.stem == stem and re.search(rrx, line):
                                status, reason = "mapped", why
                                break
                    if status == "undecided":
                        for stem, krx, why in KEPT:
                            if path.stem == stem and re.search(krx, line):
                                status, reason = "kept", why
                                break
                    sites.append((rel, i + 1, kind, status, reason,
                                  raw_lines[i].strip()))
    return sites


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--list", action="store_true", help="print every site")
    ap.add_argument("--undecided", action="store_true",
                    help="print the undecided sites")
    args = ap.parse_args()

    sites = scan()
    kinds = [k for k, _ in PATTERNS]
    totals = {s: 0 for s in ("mapped", "kept", "undecided")}
    by_kind = {k: {s: 0 for s in totals} for k in kinds}
    for _, _, kind, status, _, _ in sites:
        totals[status] += 1
        by_kind[kind][status] += 1

    for rel, ln, kind, status, reason, text in sites:
        if args.list or (args.undecided and status == "undecided"):
            extra = f"  [{reason}]" if reason else ""
            print(f"{status:9} {kind:12} {rel}:{ln}: {text[:140]}{extra}")

    print(f"{'kind':12} {'mapped':>7} {'kept':>6} {'undecided':>9}")
    for k in kinds:
        b = by_kind[k]
        print(f"{k:12} {b['mapped']:7} {b['kept']:6} {b['undecided']:9}")
    print(f"{'total':12} {totals['mapped']:7} {totals['kept']:6} "
          f"{totals['undecided']:9}  ({len(sites)} sites)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
