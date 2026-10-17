// src/gui/styles/WorkspaceComponents.cpp

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
// src/gui/styles/WorkspaceComponents.cpp  (NereusSDR)
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
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//                 componentize() is NereusSDR-original.
//   2026-10-08 - Upstream comments restored on the ported fills and edges;
//                 the full header block; the licence note corrected.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - tableRules(): the grid and header colours of Longpath's
//                 tableStyle(). J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - generatedColours(): the colours componentize() writes.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - JJ's decided controls: Longpath's slider as it is (no
//                 filled side), check box and radio indicators with the
//                 brighter off state, the lit green, blue and amber, the
//                 radius round(h x 7 / 26), indicatorRules().
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - Check boxes and radios keep today's size: the label
//                 margins an indicator rule drops come back as padding,
//                 and a rim added to a sized indicator comes off its size.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - The remaining dialog and menu colours: one selection
//                 colour for menu, list, table and tree items and text
//                 selection; header sections in the header text; blue
//                 action buttons become the raised button, the default
//                 one rimmed in the selection border; a toggle lit by
//                 its base rule and dimmed by :!checked takes the lit
//                 green and the raised button; sliderRulesFor() and
//                 groupBoxIndicatorRules() for the live pass.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - JJ's D24: the speaker volume sliders keep their filled
//                 side and the zoom bar's groove keeps today's colour, each
//                 through the translation, on the decided groove and handle
//                 (kFilledSliderProperty, kVisibleTrackProperty).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - JJ's D27: the zoom bar's track is the dark inset with a
//                 grey rim all round, 8 px overall, the handle 16 px.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - A value chip whose rule set dark neutral text on a light
//                 fill takes the chip's text colour with the chip fill (the
//                 HL2 I2C read boxes drew black on black); dim text on a
//                 dark fill keeps its colour. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10 - JJ's D29: every slider track is the button fill inside a
//                 1 px text-inactive rim, the same on all four sides; a
//                 disabled track the inset inside a border-muted rim; the
//                 zoom track keeps its inset fill and takes the same rim.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10 - JJ's D30 and D31: a push button that is not checkable
//                 and is given today's engaged green as its sheet takes
//                 the lit green (OPERATE on, kLitWhenOnProperty) or the
//                 selection glass (the tuner's chosen antenna,
//                 kChoiceProperty). A checked radio whose sheet draws a
//                 rim wider than 1 px keeps the selection rim all round;
//                 progressChunkRules() for the live pass. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "WorkspaceComponents.h"

#include "QssTranslator.h"
#include "WorkspacePalette.h"
#include "WorkspacePreview.h"
#include "WorkspaceStyle.h"

#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTabBar>
#include <QVector>
#include <QWidget>

#include <cmath>

namespace NereusSDR::Workspace::Components {

namespace {

// Hover of a raised control.
// From Longpath src/gui/StyleConstants.h:1119 [@551576e3]
constexpr auto kRaisedHoverTop = "#2a2a30";
constexpr auto kRaisedHoverBot = "#1a1a1e";
// Fields and value chips: sunkenFill(kInsetBg, 8, 4).
// [original comment from StyleConstants.h:1000-1005 is at sunkenEdges() below]
// From Longpath src/gui/StyleConstants.h:1020 [@551576e3]
constexpr int kFieldDeepen = 8;
constexpr int kFieldLift = 4;
// The light top edge of the selected border: shiftL(kGlassSelBorder, 16).
// [original comment from StyleConstants.h:808-816 is at glassTreatment() below]
// From Longpath src/gui/StyleConstants.h:823 [@551576e3]
constexpr int kGlassBorderTopLift = 16;
// The light top edge of the slider handle border: shiftL(kAccent, 18).
// [original comment from StyleConstants.h:899-902 is at the Handle case below]
// From Longpath src/gui/StyleConstants.h:918 [@551576e3]
constexpr int kHandleBorderTopLift = 18;

// The slider as it is: groove 4 px plus a 1 px border, radius 3; handle
// 10x14 plus a 1 px border, overhanging the groove by 6 px, radius 5.
// [original comment from StyleConstants.h:899-902 is at sliderGroove() below]
// From Longpath src/gui/StyleConstants.h:904-914, :927-937 [@551576e3]
constexpr auto kGrooveThickness = "4px";
constexpr auto kGrooveRadius = "3px";
constexpr auto kHandleLength = "10px";
constexpr auto kHandleThickness = "14px";
constexpr auto kHandleMarginH = "-6px 0";
constexpr auto kHandleMarginV = "0 -6px";
constexpr auto kHandleRadius = "5px";

// NereusSDR, JJ's D27 (2026-10-09): the zoom bar's track is the dark inset
// with a rim on every side (the D29 rim since 2026-10-10), 6 px inside a
// 1 px rim (8 px overall), and the handle overhangs it by 4 px so it stays
// 16 px overall, as the decided handle is.
constexpr auto kVisibleTrackThickness = "6px";
constexpr auto kVisibleTrackHandleMarginH = "-4px 0";

// Check box and radio: radius 4 (box) and 7 (radio), the accent rim on
// hover, the selection gradient when checked with a lighter top edge.
// [original comment from StyleConstants.h:1097-1100 is at indicatorTreatment() below]
// From Longpath src/gui/StyleConstants.h:1140-1145, :1153-1158 [@551576e3]
constexpr auto kBoxRadius = "4px";
constexpr auto kRadioRadius = "7px";
constexpr auto kIndicatorCheckedTopEdge = "#4b88c1";
// NereusSDR: the rim width a sheet's indicator gets when it has none.
constexpr int kIndicatorRimPx = 1;

// Longpath's raisedFill: the base lifted at the top, the base at 55 %,
// dropped by three quarters of the lift at the bottom.
// From Longpath src/gui/StyleConstants.h:712-718 [@551576e3]
constexpr int kRaisedLift = 16;
constexpr int kRaisedMidStopPercent = 55;

// The lit (checked) buttons, JJ's choice (2026-10-08), from Longpath's
// roles (greenCheckedStyle, dspToggleStyle and amberCheckedStyle at
// Longpath src/gui/StyleConstants.h:799-804, :890-895 and :844-849
// [@551576e3]): a raised fill in the form of blueCheckedStyle and a rim
// with a lighter top edge.
//   green: raisedFill(shiftL(kGreenBg, +14)), text shiftL(kLiveGreen, +80),
//          rim shiftL(kDspToggleBorder, +16);
//   amber: raisedFill(kAmberBg), text kAmberText, rim kAmberBorder.
// Longpath's own green and amber are flat; the raised forms are JJ's.
constexpr int kLitGreenBaseLift = 14;
constexpr int kLitGreenTextLift = 80;
constexpr int kLitGreenRimLift = 16;
constexpr int kLitEdgeLift = 16;

// NereusSDR workspace design, JJ's choice (2026-10-08): the radius is
// Longpath's 7 px on its 26 px field (the sizeHint of Longpath's line
// edit and combo; kComboStyle at Longpath src/gui/StyleConstants.h:1117
// [@551576e3]), scaled to the control's height: round(h x 7 / 26).
constexpr int kLongpathRadiusPx = 7;
constexpr int kLongpathFieldHeightPx = 26;
// NereusSDR Style::kButtonH, used when the control's height is unknown.
constexpr int kDefaultControlHeight = 22;
// Marks the indicator state rules componentize() appends, so a second
// pass does not append them again.
constexpr auto kIndicatorStatesMarker = "/* workspace: indicator states */";
// Marks the default button rule componentize() appends.
constexpr auto kDefaultMarker = "/* workspace: default button */";

QString hex(const char* h)
{
    return QString::fromLatin1(h);
}

QString vgrad(const QString& top, const QString& bottom)
{
    return QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
        .arg(top, bottom);
}

QString handleTopEdge()
{
    return shiftL(QColor(hex(kAccent)), kHandleBorderTopLift);
}

// The brighter off state of a check box or radio, JJ's choice
// (2026-10-08, variant "Brighter rim and fill"): kInsetBg to kButtonBg.
QString indicatorOffFill()
{
    return vgrad(hex(kInsetBg), hex(kButtonBg));
}

QString litGreenFill()
{
    return raisedFillOf(QColor(shiftL(QColor(hex(kGreenBg)), kLitGreenBaseLift)));
}

QString litGreenText()
{
    return shiftL(QColor(hex(kLiveGreen)), kLitGreenTextLift);
}

QString litGreenRim()
{
    return shiftL(QColor(hex(kDspToggleBorder)), kLitGreenRimLift);
}

QString litAmberFill()
{
    return raisedFillOf(QColor(hex(kAmberBg)));
}

QString squeeze(QString s)
{
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('\t'));
    s.remove(QLatin1Char('\n'));
    return s.toLower();
}

enum class Ours { None, Raised, Hover, Pressed, Glass, Field };

const QHash<QString, Ours>& ourFills()
{
    static const QHash<QString, Ours> fills = {
        { squeeze(raisedFill()), Ours::Raised },
        { squeeze(raisedHoverFill()), Ours::Hover },
        { squeeze(pressedFill()), Ours::Pressed },
        { squeeze(glassFill()), Ours::Glass },
        { squeeze(fieldFill()), Ours::Field },
    };
    return fills;
}

const QSet<QString>& selectionColours()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        const auto& p = WorkspacePalette::glassAndDepth();
        for (const char* r : { "sel-bg", "sel-hover", "sel-border", "accent", "selected-top",
                               "selected-bottom", "selected-border" }) {
            s.insert(p.color(QString::fromLatin1(r)).name());
        }
        return s;
    }();
    return set;
}

QString stripComments(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i) == QLatin1Char('/') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('*')) {
            const qsizetype end = s.indexOf(QLatin1String("*/"), i + 2);
            if (end < 0) {
                break;
            }
            i = end + 1;
            continue;
        }
        out += s.at(i);
    }
    return out;
}

QColor parseColour(const QString& raw)
{
    const QString v = raw.trimmed();
    if (v.startsWith(QLatin1Char('#'))) {
        return QColor(v);
    }
    const QString lower = v.toLower();
    if (lower.startsWith(QLatin1String("rgb"))) {
        const qsizetype open = v.indexOf(QLatin1Char('('));
        const qsizetype close = v.lastIndexOf(QLatin1Char(')'));
        if (open < 0 || close <= open) {
            return {};
        }
        const QStringList parts = v.mid(open + 1, close - open - 1).split(QLatin1Char(','));
        if (parts.size() < 3) {
            return {};
        }
        QColor c(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt());
        if (parts.size() >= 4) {
            const QString a = parts[3].trimmed();
            c.setAlpha(a.contains(QLatin1Char('.')) ? int(a.toDouble() * 255.0) : a.toInt());
        }
        return c;
    }
    if (QColor::isValidColorName(v)) {
        return QColor::fromString(v);
    }
    return {};
}

enum class Fill { None, Neutral, Selection, Semantic, Clear, Ours, OtherGradient, Unknown };

struct FillInfo {
    Fill fill = Fill::None;
    Ours ours = Ours::None;
};

FillInfo classifyFill(const QString& rawValue)
{
    const QString v = stripComments(rawValue).trimmed();
    if (v.isEmpty()) {
        return {};
    }
    const QString lower = v.toLower();
    if (lower.startsWith(QLatin1String("qlineargradient")) || lower.startsWith(QLatin1String("qradialgradient"))
        || lower.startsWith(QLatin1String("qconicalgradient"))) {
        const Ours o = ourFills().value(squeeze(v), Ours::None);
        return { o == Ours::None ? Fill::OtherGradient : Fill::Ours, o };
    }
    if (lower == QLatin1String("transparent") || lower == QLatin1String("none")) {
        return { Fill::Clear, Ours::None };
    }
    const QColor c = parseColour(v);
    if (!c.isValid()) {
        return { Fill::Unknown, Ours::None };
    }
    if (c.alpha() < 255) {
        return { Fill::Clear, Ours::None };
    }
    if (selectionColours().contains(c.name())) {
        return { Fill::Selection, Ours::None };
    }
    const float s = c.hsvSaturationF();
    const float val = c.valueF();
    const int h = c.hsvHue();
    if (s < 0.25f) {
        return { Fill::Neutral, Ours::None };
    }
    // Dark legacy blues (#1a3a5a, #203040) are surfaces, not meaning.
    if (h >= 185 && h <= 255 && val < 0.45f) {
        return { Fill::Neutral, Ours::None };
    }
    return { Fill::Semantic, Ours::None };
}

// ---- Declarations ---------------------------------------------------

// A declaration list kept as the pieces between ';' so that untouched
// declarations come out byte-for-byte.
class Decls
{
public:
    explicit Decls(const QString& body)
    {
        int depth = 0;
        QChar quote;
        QString cur;
        for (qsizetype i = 0; i < body.size(); ++i) {
            const QChar c = body.at(i);
            if (!quote.isNull()) {
                cur += c;
                if (c == quote) {
                    quote = QChar();
                }
                continue;
            }
            if (c == QLatin1Char('/') && i + 1 < body.size() && body.at(i + 1) == QLatin1Char('*')) {
                qsizetype end = body.indexOf(QLatin1String("*/"), i + 2);
                end = end < 0 ? body.size() : end + 2;
                cur += body.mid(i, end - i);
                i = end - 1;
                continue;
            }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                quote = c;
            } else if (c == QLatin1Char('(')) {
                ++depth;
            } else if (c == QLatin1Char(')')) {
                depth = std::max(0, depth - 1);
            } else if (c == QLatin1Char(';') && depth == 0) {
                m_pieces.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        m_pieces.push_back(cur);
    }

    QString render() const { return m_pieces.join(QLatin1Char(';')); }

    static QString nameOf(const QString& piece)
    {
        const QString s = stripComments(piece);
        const qsizetype colon = s.indexOf(QLatin1Char(':'));
        return colon < 0 ? QString() : s.left(colon).trimmed().toLower();
    }

    static QString valueOf(const QString& piece)
    {
        const qsizetype colon = piece.indexOf(QLatin1Char(':'));
        return colon < 0 ? QString() : piece.mid(colon + 1).trimmed();
    }

    // Index of the last declaration with one of the names, or -1.
    int findLast(std::initializer_list<const char*> names) const
    {
        for (int i = int(m_pieces.size()) - 1; i >= 0; --i) {
            const QString n = nameOf(m_pieces[i]);
            for (const char* want : names) {
                if (n == QLatin1String(want)) {
                    return i;
                }
            }
        }
        return -1;
    }

    QString value(int i) const { return valueOf(m_pieces[i]); }

    void replaceValue(int i, const QString& v)
    {
        m_pieces[i] = QStringLiteral(" %1: %2").arg(nameOf(m_pieces[i]), v);
    }

    // Sets `name` to `v`: replaces the last declaration of that name, or
    // adds one.
    void set(const char* name, const QString& v)
    {
        const int i = findLast({ name });
        if (i >= 0) {
            if (value(i) != v) {
                m_pieces[i] = QStringLiteral(" %1: %2").arg(QLatin1String(name), v);
            }
            return;
        }
        const QString piece = QStringLiteral(" %1: %2").arg(QLatin1String(name), v);
        if (stripComments(m_pieces.last()).trimmed().isEmpty()) {
            m_pieces.insert(m_pieces.size() - 1, piece);
        } else {
            m_pieces.push_back(piece);
            m_pieces.push_back(QString());
        }
    }

    bool has(const char* name) const { return findLast({ name }) >= 0; }

private:
    QStringList m_pieces;
};

struct BorderInfo {
    bool has = false;
    Fill colour = Fill::Unknown;
};

BorderInfo borderInfo(const Decls& d)
{
    BorderInfo b;
    const int i = d.findLast({ "border" });
    if (i >= 0) {
        const QString v = stripComments(d.value(i)).trimmed().toLower();
        if (!v.startsWith(QLatin1String("none")) && v != QLatin1String("0")
            && v != QLatin1String("0px")) {
            b.has = true;
            // Find the colour token.
            int depth = 0;
            QString tok;
            QStringList toks;
            for (QChar c : v) {
                if (c == QLatin1Char('(')) {
                    ++depth;
                } else if (c == QLatin1Char(')')) {
                    --depth;
                }
                if (c.isSpace() && depth == 0) {
                    if (!tok.isEmpty()) {
                        toks << tok;
                    }
                    tok.clear();
                } else {
                    tok += c;
                }
            }
            if (!tok.isEmpty()) {
                toks << tok;
            }
            for (const QString& t : toks) {
                const FillInfo f = classifyFill(t);
                if (f.fill != Fill::Unknown) {
                    b.colour = f.fill;
                }
            }
        }
    }
    const int bw = d.findLast({ "border-width" });
    if (bw >= 0) {
        const QString v = d.value(bw).trimmed();
        b.has = !(v == QLatin1String("0") || v == QLatin1String("0px"));
    }
    const int bc = d.findLast({ "border-color" });
    if (bc >= 0) {
        b.colour = classifyFill(d.value(bc)).fill;
    }
    return b;
}

// ---- Selectors ------------------------------------------------------

enum class Base { None, Button, Combo, Field, Label, Slider, TabBar, TabWidget, Toggle, Items, MenuBar, Header };
enum class Kind {
    None, Button, Combo, Tab, Field, Label, SpinButton, Groove, SliderFill, SliderRecess, Handle, Indicator, Pane,
    Item, Section
};
enum class State { Normal, Hover, Pressed, Checked, Disabled, Other };

Base baseOfTypeName(const QString& t)
{
    static const QHash<QString, Base> m = {
        { QStringLiteral("QPushButton"), Base::Button },
        { QStringLiteral("QToolButton"), Base::Button },
        { QStringLiteral("QComboBox"), Base::Combo },
        { QStringLiteral("QLineEdit"), Base::Field },
        { QStringLiteral("QSpinBox"), Base::Field },
        { QStringLiteral("QDoubleSpinBox"), Base::Field },
        { QStringLiteral("QAbstractSpinBox"), Base::Field },
        { QStringLiteral("QLabel"), Base::Label },
        { QStringLiteral("QSlider"), Base::Slider },
        { QStringLiteral("QTabBar"), Base::TabBar },
        { QStringLiteral("QTabWidget"), Base::TabWidget },
        { QStringLiteral("QCheckBox"), Base::Toggle },
        { QStringLiteral("QRadioButton"), Base::Toggle },
        { QStringLiteral("QMenu"), Base::Items },
        { QStringLiteral("QListView"), Base::Items },
        { QStringLiteral("QListWidget"), Base::Items },
        { QStringLiteral("QTreeView"), Base::Items },
        { QStringLiteral("QTreeWidget"), Base::Items },
        { QStringLiteral("QTableView"), Base::Items },
        { QStringLiteral("QTableWidget"), Base::Items },
        { QStringLiteral("QAbstractItemView"), Base::Items },
        { QStringLiteral("QMenuBar"), Base::MenuBar },
        { QStringLiteral("QHeaderView"), Base::Header },
    };
    return m.value(t, Base::None);
}

Base baseOfWidget(const QWidget* w)
{
    if (w == nullptr) {
        return Base::None;
    }
    if (w->inherits("QPushButton") || w->inherits("QToolButton")) {
        return Base::Button;
    }
    if (w->inherits("QComboBox")) {
        return Base::Combo;
    }
    if (w->inherits("QAbstractSpinBox") || w->inherits("QLineEdit")) {
        return Base::Field;
    }
    if (w->inherits("QLabel")) {
        return Base::Label;
    }
    if (w->inherits("QSlider")) {
        return Base::Slider;
    }
    if (w->inherits("QTabBar")) {
        return Base::TabBar;
    }
    if (w->inherits("QTabWidget")) {
        return Base::TabWidget;
    }
    if (w->inherits("QCheckBox") || w->inherits("QRadioButton")) {
        return Base::Toggle;
    }
    if (w->inherits("QHeaderView")) {
        return Base::Header;
    }
    if (w->inherits("QMenu") || w->inherits("QAbstractItemView")) {
        return Base::Items;
    }
    if (w->inherits("QMenuBar")) {
        return Base::MenuBar;
    }
    return Base::None;
}

bool isIdentChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-');
}

struct Compound {
    QString type;
    QString id;
    QString sub;
    QStringList states;
};

QString lastCompound(const QString& selector)
{
    const QString s = stripComments(selector).trimmed();
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('[') || c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(']') || c == QLatin1Char(')')) {
            --depth;
        } else if (depth == 0 && (c.isSpace() || c == QLatin1Char('>'))) {
            start = i + 1;
        }
    }
    return s.mid(start);
}

Compound parseCompound(const QString& s)
{
    Compound c;
    qsizetype i = 0;
    const qsizetype n = s.size();
    auto readIdent = [&]() {
        const qsizetype b = i;
        while (i < n && isIdentChar(s.at(i))) {
            ++i;
        }
        return s.mid(b, i - b);
    };
    if (i < n && s.at(i) == QLatin1Char('*')) {
        ++i;
    } else {
        c.type = readIdent();
    }
    while (i < n) {
        const QChar ch = s.at(i);
        if (ch == QLatin1Char('#')) {
            ++i;
            c.id = readIdent();
        } else if (ch == QLatin1Char('.')) {
            ++i;
            c.type = readIdent();
        } else if (ch == QLatin1Char('[')) {
            const qsizetype end = s.indexOf(QLatin1Char(']'), i);
            i = end < 0 ? n : end + 1;
        } else if (ch == QLatin1Char(':')) {
            if (i + 1 < n && s.at(i + 1) == QLatin1Char(':')) {
                i += 2;
                c.sub = readIdent().toLower();
            } else {
                ++i;
                bool neg = false;
                if (i < n && s.at(i) == QLatin1Char('!')) {
                    neg = true;
                    ++i;
                }
                const QString st = readIdent().toLower();
                c.states << (neg ? QLatin1Char('!') + st : st);
            }
        } else {
            ++i;
        }
    }
    return c;
}

struct Target {
    Kind kind = Kind::None;
    State state = State::Normal;
    int height = kDefaultControlHeight;
    bool vertical = false;
    bool horizontal = false;
    // Checked, also when the state is Disabled (check box and radio).
    bool checked = false;
    // A radio button's indicator (round), not a check box's.
    bool radio = false;
    // A radio whose sheet draws its rim wider than 1 px.
    bool wideRim = false;
    // The block's control is the widget the sheet is set on.
    bool isOwner = false;
    // A push button that cannot be checked: a blue fill on it is an
    // action colour, not a selection.
    bool action = false;
    // A QPushButton (the :default state applies).
    bool pushButton = false;
    // The selector says :!checked.
    bool notChecked = false;
    // The sheet lights the button in its plain rule and dims it in a
    // :!checked rule, so the plain rule is the checked state.
    bool baseIsChecked = false;
    // No widget answers for the selector (a type rule on a container).
    bool unresolved = false;
    // The slider's filled side carries meaning (the speaker volume, D24).
    bool filled = false;
    // The slider's track must read on its own (the zoom bar, D27).
    bool visibleTrack = false;
    // A button that is not checkable and shows "on" by its sheet (OPERATE,
    // D30).
    bool litWhenOn = false;
    // A button that is not checkable and shows "chosen" by its sheet (the
    // tuner's antenna, D31).
    bool choice = false;

    bool operator==(const Target& o) const
    {
        return kind == o.kind && state == o.state;
    }
};

int heightOf(const QWidget* w)
{
    if (w == nullptr) {
        return kDefaultControlHeight;
    }
    if (w->minimumHeight() == w->maximumHeight() && w->maximumHeight() > 0) {
        return w->maximumHeight();
    }
    if (w->isVisible() && w->height() > 0) {
        return w->height();
    }
    return kDefaultControlHeight;
}

Target classify(const Compound& c, Base base, const QWidget* resolved)
{
    Target t;
    bool horizontal = false;
    bool dis = false;
    bool press = false;
    bool chk = false;
    bool hov = false;
    bool other = false;
    for (const QString& st : c.states) {
        if (st == QLatin1String("!checked") || st == QLatin1String("!on")) {
            t.notChecked = true;
        }
        if (st.startsWith(QLatin1Char('!'))) {
            continue;
        }
        if (st == QLatin1String("disabled")) {
            dis = true;
        } else if (st == QLatin1String("pressed")) {
            press = true;
        } else if (st == QLatin1String("on")) {
            (base == Base::Combo ? press : chk) = true;
        } else if (st == QLatin1String("checked") || st == QLatin1String("selected")) {
            chk = true;
        } else if (st == QLatin1String("hover")) {
            hov = true;
        } else if (st == QLatin1String("focus") || st == QLatin1String("indeterminate")
                   || st == QLatin1String("default")) {
            other = true;
        } else if (st == QLatin1String("vertical")) {
            t.vertical = true;
        } else if (st == QLatin1String("horizontal")) {
            horizontal = true;
        }
    }
    t.horizontal = horizontal;
    t.checked = chk;
    t.state = dis     ? State::Disabled
            : other   ? State::Other
            : press   ? State::Pressed
            : chk     ? State::Checked
            : hov     ? State::Hover
                      : State::Normal;

    const QString& sub = c.sub;
    switch (base) {
    case Base::Button:
        t.kind = sub.isEmpty() ? Kind::Button : Kind::None;
        break;
    case Base::Combo:
        t.kind = sub.isEmpty() ? Kind::Combo : Kind::None;
        break;
    case Base::Field:
        if (sub.isEmpty()) {
            t.kind = Kind::Field;
        } else if (sub == QLatin1String("up-button") || sub == QLatin1String("down-button")) {
            t.kind = Kind::SpinButton;
        }
        break;
    case Base::Label:
        t.kind = sub.isEmpty() ? Kind::Label : Kind::None;
        break;
    case Base::Slider:
        if (sub == QLatin1String("groove")) {
            t.kind = Kind::Groove;
        } else if (sub == QLatin1String("handle")) {
            t.kind = Kind::Handle;
        } else if (sub == QLatin1String("sub-page")) {
            t.kind = t.vertical ? Kind::SliderRecess : Kind::SliderFill;
        } else if (sub == QLatin1String("add-page")) {
            t.kind = t.vertical ? Kind::SliderFill : Kind::SliderRecess;
        }
        break;
    case Base::TabBar:
        t.kind = sub == QLatin1String("tab") ? Kind::Tab : Kind::None;
        break;
    case Base::TabWidget:
        t.kind = sub == QLatin1String("pane") ? Kind::Pane : Kind::None;
        break;
    case Base::Toggle:
        t.kind = sub == QLatin1String("indicator") ? Kind::Indicator : Kind::None;
        break;
    case Base::Items:
    case Base::MenuBar:
        t.kind = sub == QLatin1String("item") ? Kind::Item : Kind::None;
        // A menu bar's open title is :selected or :pressed; both are the
        // selection.
        if (base == Base::MenuBar && t.state == State::Pressed) {
            t.state = State::Checked;
        }
        break;
    case Base::Header:
        t.kind = sub == QLatin1String("section") ? Kind::Section : Kind::None;
        break;
    case Base::None:
        break;
    }
    if (resolved != nullptr && sub.isEmpty()) {
        t.height = heightOf(resolved);
    }
    return t;
}

// The height of the tab bar a tab or pane rule draws: the resolved
// widget if it is a tab bar, else the owner's first tab bar.
int tabBarHeight(const QWidget* owner, const QWidget* resolved)
{
    for (const QWidget* w : { resolved, owner }) {
        if (w != nullptr && w->inherits("QTabBar")) {
            return heightOf(w);
        }
    }
    const QWidget* bar = owner == nullptr ? nullptr : owner->findChild<QTabBar*>();
    return heightOf(bar);
}

// Fills in what the selector leaves to the widget: a slider's
// orientation, a radio button, a tab bar's height.
void resolveTarget(Target& t, const QString& type, const QWidget* owner, const QWidget* resolved)
{
    if (t.kind == Kind::Tab || t.kind == Kind::Pane) {
        t.height = tabBarHeight(owner, resolved);
    }
    if (t.kind == Kind::Indicator) {
        t.radio = type == QLatin1String("QRadioButton")
            || (type.isEmpty() && resolved != nullptr && resolved->inherits("QRadioButton"));
    }
    if (!t.vertical && !t.horizontal && resolved != nullptr && resolved->inherits("QAbstractSlider")) {
        const int o = resolved->property("orientation").toInt();
        t.horizontal = o == Qt::Horizontal;
        t.vertical = o == Qt::Vertical;
    }
    if (resolved != nullptr && resolved->inherits("QAbstractSlider")) {
        t.filled = resolved->property(WorkspacePreview::kFilledSliderProperty).toBool();
        t.visibleTrack = resolved->property(WorkspacePreview::kVisibleTrackProperty).toBool();
    }
    if (resolved != nullptr && resolved->inherits("QAbstractButton")) {
        t.litWhenOn = resolved->property(WorkspacePreview::kLitWhenOnProperty).toBool();
        t.choice = resolved->property(WorkspacePreview::kChoiceProperty).toBool();
    }
}

Target classifySelector(const QString& selector, const QWidget* owner)
{
    const Compound c = parseCompound(lastCompound(selector));
    const QWidget* resolved = nullptr;
    Base base = Base::None;
    if (!c.type.isEmpty()) {
        base = baseOfTypeName(c.type);
        if (owner != nullptr && base != Base::None && owner->inherits(c.type.toLatin1().constData())
            && (c.id.isEmpty() || owner->objectName() == c.id)) {
            resolved = owner;
        } else if (owner != nullptr && !c.id.isEmpty()) {
            resolved = owner->findChild<QWidget*>(c.id);
        }
    } else {
        if (!c.id.isEmpty()) {
            if (owner != nullptr) {
                resolved = owner->objectName() == c.id ? owner : owner->findChild<QWidget*>(c.id);
            }
        } else {
            resolved = owner;
        }
        base = baseOfWidget(resolved);
    }
    Target t = classify(c, base, resolved);
    t.isOwner = resolved != nullptr && resolved == owner;
    resolveTarget(t, c.type, owner, resolved);
    if (t.kind == Kind::Button) {
        if (resolved != nullptr) {
            t.pushButton = resolved->inherits("QPushButton");
            t.action = t.pushButton && !resolved->property("checkable").toBool();
        } else {
            // A type rule on a container: a QPushButton is an action
            // button unless the sheet itself is about checking
            // (componentize() looks at the whole sheet).
            t.pushButton = c.type == QLatin1String("QPushButton");
            t.action = t.pushButton;
            t.unresolved = true;
        }
    }
    return t;
}

Target classifySelectorless(const QWidget* owner)
{
    Target t = classify(Compound{}, baseOfWidget(owner), owner);
    t.isOwner = owner != nullptr;
    resolveTarget(t, QString(), owner, owner);
    if (t.kind == Kind::Button && owner != nullptr) {
        t.pushButton = owner->inherits("QPushButton");
        t.action = t.pushButton && !owner->property("checkable").toBool();
    }
    return t;
}

// ---- Treatment ------------------------------------------------------

void setBackground(Decls& d, int bgIndex, const QString& v)
{
    if (bgIndex >= 0) {
        d.replaceValue(bgIndex, v);
    } else {
        d.set("background", v);
    }
}

void raisedEdges(Decls& d)
{
    // Glas & Tiefe (2026-09-17): ein erhabener Knopf hat oben eine
    // Lichtkante und unten eine dunkle Kante — der Rahmen ist
    // nicht rundum gleich, sondern das Licht kommt von oben.
    // From Longpath src/gui/StyleConstants.h:778 [@551576e3]
    d.set("border-top-color", hex(kGlassEdgeLight));
    d.set("border-bottom-color", hex(kGlassEdgeDark));
}

void pressedEdges(Decls& d)
{
    // Gedrueckt: der Verlauf kippt. Ein Knopf, der sich beim
    // Druecken nicht bewegt, fuehlt sich tot an — und das kostet
    // hier kein einziges Pixel Verschiebung.
    // From Longpath src/gui/StyleConstants.h:786 [@551576e3]
    d.set("border-top-color", hex(kGlassEdgeDark));
    d.set("border-bottom-color", hex(kGlassEdgeLight));
}

void sunkenEdges(Decls& d)
{
    /// Ein Zahlenfeld hinter Glas (Glas & Tiefe, 2026-09-17): schwarz,
    /// feiner Rahmen, oben die dunkle Kante des Innenschattens, unten eine
    /// Lichtkante — und KEINE Pfeile. Mausrad, Pfeiltasten und Tippen
    /// bleiben; die Pfeilknoepfe waren Qt-Optik und nahmen dem Feld die
    /// Ruhe. Die Schrift setzt der Aufrufer per setFont (Monospace, Regel
    /// 4) — Groesse gehoert nicht ins Stylesheet (HAUSSTIL.md, die Falle).
    // From Longpath src/gui/StyleConstants.h:1014-1015 [@551576e3]
    d.set("border-top-color", hex(kGlassShade));
    d.set("border-bottom-color", hex(kGlassLight));
}

void glassTreatment(Decls& d)
{
    // raisedFill statt einer flachen Fuellung: der aktive Knopf ist
    // dieselbe aufgesetzte Flaeche wie jeder andere, nur in der
    // Akzentfarbe. Zwei verschiedene Bauarten fuer denselben Knopf
    // waeren genau die Art Unstimmigkeit, die man als „unfertig" liest.
    // Glas & Tiefe (2026-09-17): der gedeckte Auswahlverlauf der
    // Kopfleisten-Pillen (kGlassSel*) statt des flachen kBlueBg — auf
    // den Applet-Blaettern leuchteten "Stereo", "Phone", "5.0k", "ON/RX/
    // TX" und die Scheibenmarke "A" in einem Blau, das die Kopfleiste
    // laengst nicht mehr trug. Eine Auswahl, eine Farbe.
    // From Longpath src/gui/StyleConstants.h:815-823 [@551576e3]
    setBackground(d, d.findLast({ "background", "background-color" }), glassFill());
    if (d.has("color")) {
        d.set("color", hex(kGlassSelText));
    }
    d.set("border-color", hex(kGlassSelBorder));
    d.set("border-top-color", shiftL(QColor(hex(kGlassSelBorder)), kGlassBorderTopLift));
}

void disabledTreatment(Decls& d, int bg, const BorderInfo& b)
{
    setBackground(d, bg, hex(kDisabledBg));
    if (d.has("color")) {
        d.set("color", hex(kDisabledText));
    }
    if (b.has || d.has("border-color")) {
        d.set("border-color", hex(kDisabledBorder));
    }
}

// ---- Slider ----

void sliderGroove(Decls& d, int bg, const Target& t)
{
    // Glas & Tiefe (2026-09-17): die Rinne ist VERSENKT (schwarz, oben
    // die dunkle Kante, unten Licht), der Griff ERHABEN — eine Pille im
    // Knopfverlauf mit Akzentrand, nicht mehr die blaue Kugel von Qt.
    // Blau bleibt am Rand: der Griff ist anfassbar.
    // From Longpath src/gui/StyleConstants.h:899-910 [@551576e3]
    // Wie sliderHStyle(), nur stehend: Rinne versenkt (Licht kommt von
    // oben, also Schattenkante oben), Griff eine liegende Pille.
    // From Longpath src/gui/StyleConstants.h:924-935 [@551576e3]
    if (t.horizontal) {
        d.set("height", hex(kGrooveThickness));
    } else if (t.vertical) {
        d.set("width", hex(kGrooveThickness));
    }
    // NereusSDR, JJ's D29 (2026-10-09): the track must be seen. Not
    // Longpath's sunken black with the shade above and the light below:
    // the button fill, flat, inside a 1 px rim in the unticked box's rim
    // colour, the same on all four sides. Thickness and radius stay.
    setBackground(d, bg, hex(kButtonBg));
    d.set("border", QStringLiteral("1px solid %1").arg(hex(kTextInactive)));
    d.set("border-color", hex(kTextInactive));
    d.set("border-top-color", hex(kTextInactive));
    d.set("border-bottom-color", hex(kTextInactive));
    d.set("border-radius", hex(kGrooveRadius));
}

// NereusSDR, settled with JJ's D29 (2026-10-09): a disabled track is the
// disabled unticked box (D10), the inset inside a border-muted rim on all
// four sides.
void sliderGrooveDisabled(Decls& d, int bg)
{
    setBackground(d, bg, hex(kInsetBg));
    d.set("border-color", hex(kBorderMuted));
    d.set("border-top-color", hex(kBorderMuted));
    d.set("border-bottom-color", hex(kBorderMuted));
}

// True when sliderBody() draws the groove block `body` as the track.
bool grooveTreated(const QString& body)
{
    Decls d(body);
    const int bg = d.findLast({ "background", "background-color" });
    return bg >= 0 && classifyFill(d.value(bg)).fill != Fill::Clear;
}

QString grooveDisabledDecls()
{
    Decls d(QString{});
    sliderGrooveDisabled(d, -1);
    return d.render();
}

void sliderHandle(Decls& d, int bg, const Target& t)
{
    // [original comment from StyleConstants.h:899-902 is at sliderGroove() above]
    // From Longpath src/gui/StyleConstants.h:911-914, :936-939 [@551576e3]
    if (t.horizontal) {
        d.set("width", hex(kHandleLength));
        d.set("height", hex(kHandleThickness));
        d.set("margin", hex(kHandleMarginH));
    } else if (t.vertical) {
        d.set("height", hex(kHandleLength));
        d.set("width", hex(kHandleThickness));
        d.set("margin", hex(kHandleMarginV));
    }
    setBackground(d, bg, raisedFill());
    d.set("border", QStringLiteral("1px solid %1").arg(hex(kAccent)));
    d.set("border-color", hex(kAccent));
    d.set("border-top-color", handleTopEdge());
    d.set("border-radius", hex(kHandleRadius));
}

// Longpath's slider as it is, JJ's choice (2026-10-08): the groove and
// handle take Longpath's colours and geometry, and no side of the groove
// is filled. Disabled rules keep their colours (Longpath has none), except
// the groove's (JJ's D29, 2026-10-09).
QString sliderBody(Decls& d, int bg, const FillInfo& f, const Target& t)
{
    switch (t.kind) {
    case Kind::SliderFill:
        // NereusSDR, JJ's D24 (2026-10-09): the speaker volume keeps its
        // filled side, today's colour through the translation, inside the
        // decided groove. Disabled, today's sheet fills it in the groove's
        // colour (no fill shows), so the decided groove shows.
        if (t.filled && t.state == State::Normal && bg >= 0 && f.fill != Fill::Clear) {
            d.set("border-radius", hex(kGrooveRadius));
            break;
        }
        [[fallthrough]];
    case Kind::SliderRecess:
        // NereusSDR, JJ's choice (2026-10-08): no filled side, in every state.
        if (bg >= 0) {
            setBackground(d, bg, QStringLiteral("transparent"));
        }
        break;
    case Kind::Groove:
        if (t.state == State::Normal && bg >= 0 && f.fill != Fill::Clear) {
            sliderGroove(d, bg, t);
            if (t.visibleTrack && t.horizontal) {
                // NereusSDR, JJ's D27 (2026-10-09): the zoom bar's track
                // stays dark and taller, with a brighter grey rim all the
                // way round, so both long edges read against the bar.
                // JJ's D29 (2026-10-09): the rim is the D29 rim; the fill
                // stays the dark inset.
                d.set("height", hex(kVisibleTrackThickness));
                d.replaceValue(d.findLast({ "background", "background-color" }), hex(kInsetBg));
            }
        } else if (t.state == State::Disabled && (bg < 0 || f.fill != Fill::Clear)) {
            sliderGrooveDisabled(d, bg);
        }
        break;
    case Kind::Handle:
        if (t.state == State::Normal && bg >= 0 && f.fill != Fill::Clear) {
            sliderHandle(d, bg, t);
            if (t.visibleTrack && t.horizontal) {
                // NereusSDR, JJ's D27 (2026-10-09): 16 px overall on the
                // 8 px track.
                d.set("margin", hex(kVisibleTrackHandleMarginH));
            }
        } else if ((t.state == State::Hover || t.state == State::Pressed) && bg >= 0) {
            // From Longpath src/gui/StyleConstants.h:916, :1239 [@551576e3]
            setBackground(d, bg, hex(kGlassBtnTop));
        }
        break;
    default:
        break;
    }
    return d.render();
}

// ---- Check box and radio ----

// One indicator block, by state. Colours and the radius only: widths,
// heights and border widths stay, so the row keeps its size.
QString indicatorBody(Decls& d, int bg, const Target& t)
{
    //   Haekchen und Wahlpunkte sind VERSENKT (schwarz, Schattenkante
    //   oben, Lichtkante unten); angehakt fuellt sie derselbe gedeckte
    //   Blauverlauf wie jede andere Auswahl. Radius 4 statt 2 — die
    //   Regel "nie 3" gilt auch fuer Kleines.
    // [original comment from StyleConstants.h:1097-1100]
    // From Longpath src/gui/StyleConstants.h:1138-1145, :1151-1158 [@551576e3]
    // NereusSDR, JJ's choice (2026-10-08, "Brighter rim and fill"): the off
    // rim kTextInactive all round, the fill kInsetBg to kButtonBg; disabled
    // off rim kBorderMuted on kInsetBg; disabled on kDisabledText.
    const BorderInfo b = borderInfo(d);
    switch (t.state) {
    case State::Normal:
        if (bg < 0 && !b.has) {
            return d.render();
        }
        setBackground(d, bg, indicatorOffFill());
        if (b.has) {
            // A rim already in the off colour (this rewrite's own, on a
            // second pass) stays as it is.
            const int shorthand = d.findLast({ "border" });
            const bool alreadyOff = d.findLast({ "border-color" }) < 0 && shorthand >= 0
                && d.value(shorthand).contains(hex(kTextInactive), Qt::CaseInsensitive);
            if (!alreadyOff) {
                d.set("border-color", hex(kTextInactive));
            }
        } else {
            // The rim is drawn outside a given width and height: take it
            // off them, so the indicator keeps its outer size.
            for (const char* side : { "width", "height" }) {
                const int i = d.findLast({ side });
                static const QRegularExpression px(QStringLiteral("^(\\d+)px$"));
                const QRegularExpressionMatch m =
                    i < 0 ? QRegularExpressionMatch() : px.match(stripComments(d.value(i)).trimmed());
                if (m.hasMatch()) {
                    const int inner = std::max(0, m.captured(1).toInt() - 2 * kIndicatorRimPx);
                    d.set(side, QStringLiteral("%1px").arg(inner));
                }
            }
            d.set("border", QStringLiteral("%1px solid %2").arg(kIndicatorRimPx).arg(hex(kTextInactive)));
        }
        d.set("border-radius", hex(t.radio ? kRadioRadius : kBoxRadius));
        break;
    case State::Hover:
        d.set("border-color", hex(kAccent));
        break;
    case State::Checked: {
        setBackground(d, bg, glassFill());
        d.set("border-color", hex(kGlassSelBorder));
        // NereusSDR (settled 2026-10-10): the light top edge is a 1 px
        // edge. On a radio whose sheet draws a wider rim (the NR2 page's
        // 2 px, DspSetupPages.cpp:877-880) it became a second ring that
        // met the rim in a blend of the two; that rim is the checked
        // radio's rim colour all round (D6, D10).
        d.set("border-top-color", hex(t.wideRim ? kGlassSelBorder : kIndicatorCheckedTopEdge));
        break;
    }
    case State::Disabled:
        if (t.checked) {
            setBackground(d, bg, hex(kDisabledText));
            d.set("border-color", hex(kDisabledText));
            d.set("border-top-color", hex(kDisabledText));
        } else {
            setBackground(d, bg, hex(kInsetBg));
            d.set("border-color", hex(kBorderMuted));
        }
        break;
    case State::Pressed:
    case State::Other:
        break;
    }
    return d.render();
}

// The hover, checked and disabled states for an indicator selector `s`
// (no state of its own), as rules that follow the sheet's own: a widget
// sheet's plain indicator rule would otherwise paint every state off.
QString indicatorStateRules(const QString& s, bool wideRim = false)
{
    return QStringLiteral(
        "\n%1:hover { border-color: %2; }"
        "\n%1:checked { background: %3; border-color: %4; border-top-color: %5; }"
        "\n%1:disabled { background: %6; border-color: %7; }"
        "\n%1:checked:disabled { background: %8; border-color: %8; border-top-color: %8; }")
        .arg(s, hex(kAccent), glassFill(), hex(kGlassSelBorder),
             hex(wideRim ? kGlassSelBorder : kIndicatorCheckedTopEdge), hex(kInsetBg), hex(kBorderMuted),
             hex(kDisabledText));
}

// ---- Lit buttons ----

enum class Lit { None, Green, Amber };

Lit litOf(const QString& rawBackground)
{
    const QColor c = parseColour(stripComments(rawBackground));
    if (!c.isValid()) {
        return Lit::None;
    }
    const auto& p = WorkspacePalette::glassAndDepth();
    const QString name = c.name();
    if (name == p.color(QStringLiteral("ok-bg")).name() || name == p.color(QStringLiteral("dsp-on-bg")).name()) {
        return Lit::Green;
    }
    if (name == p.color(QStringLiteral("measured-bg")).name()) {
        return Lit::Amber;
    }
    return Lit::None;
}

// A checked button in today's green (greenCheckedStyle, dspToggleStyle) or
// amber (amberCheckedStyle): JJ's raised forms. Blue is the glass
// treatment; red and every other meaning keep their colours.
void litTreatment(Decls& d, int bg, Lit lit)
{
    // [original comment from StyleConstants.h:808-816 is at glassTreatment() above]
    if (lit == Lit::Green) {
        setBackground(d, bg, litGreenFill());
        d.set("color", litGreenText());
        d.set("border-color", litGreenRim());
        d.set("border-top-color", shiftL(QColor(litGreenRim()), kLitEdgeLift));
    } else {
        setBackground(d, bg, litAmberFill());
        d.set("color", hex(kAmberText));
        d.set("border-color", hex(kAmberBorder));
        d.set("border-top-color", shiftL(QColor(hex(kAmberBorder)), kLitEdgeLift));
    }
}

// ---- Selection ----

// The one selection colour (the application's: QPalette::Highlight and
// the menu's selected item) for a text or item selection a sheet
// colours itself. A meaning colour (green, red) stays.
void selectionDecls(Decls& d)
{
    const int sbg = d.findLast({ "selection-background-color" });
    bool ours = sbg < 0;
    if (sbg >= 0) {
        const Fill f = classifyFill(d.value(sbg)).fill;
        if (f == Fill::Neutral || f == Fill::Selection || f == Fill::Ours) {
            d.replaceValue(sbg, hex(kGlassSelTop));
            ours = true;
        }
    }
    const int sc = d.findLast({ "selection-color" });
    if (sc >= 0 && ours && d.value(sc) != hex(kGlassSelText)) {
        d.replaceValue(sc, hex(kGlassSelText));
    }
}

// A menu, list, table or tree item in its selected state: the
// application's menu selection, flat (AppTheme applyWorkspacePalette).
void itemSelection(Decls& d, int bg)
{
    setBackground(d, bg, hex(kGlassSelTop));
    d.set("color", hex(kGlassSelText));
}

// ---- Action buttons ----

// A rim of 1 px on a button that has none, out of its padding so the
// button keeps its size. False when the padding is not plain pixels of
// at least 1 on every side.
bool rimFromPadding(Decls& d)
{
    const int i = d.findLast({ "padding" });
    if (i < 0 || d.findLast({ "padding-top", "padding-right", "padding-bottom", "padding-left" }) > i) {
        return false;
    }
    const QStringList parts =
        stripComments(d.value(i)).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.size() > 4) {
        return false;
    }
    static const QRegularExpression px(QStringLiteral("^(\\d+)(px)?$"));
    QStringList less;
    for (const QString& p : parts) {
        const QRegularExpressionMatch m = px.match(p);
        if (!m.hasMatch() || m.captured(1).toInt() < 1) {
            return false;
        }
        less << QStringLiteral("%1px").arg(m.captured(1).toInt() - 1);
    }
    d.replaceValue(i, less.join(QLatin1Char(' ')));
    d.set("border", QStringLiteral("1px solid %1").arg(hex(kBorder)));
    return true;
}

// A blue action button (Save & Propagate, Connect, Start, OK): the
// workspace's raised button with today's ink in the text colour.
// The default button is marked by defaultRules().
void actionTreatment(Decls& d, int bg, const BorderInfo& b)
{
    // [original comment from StyleConstants.h:778 is at raisedEdges() above]
    setBackground(d, bg, raisedFill());
    d.set("color", hex(kTextPrimary));
    if (b.has || rimFromPadding(d)) {
        d.set("border-color", hex(kBorder));
        raisedEdges(d);
    }
}

// The off state of a toggle its sheet dims with :!checked: the raised
// button.
void raisedOff(Decls& d, int bg)
{
    setBackground(d, bg, raisedFill());
    d.set("color", hex(kTextPrimary));
    d.set("border-color", hex(kBorder));
    raisedEdges(d);
}

// Marks the default button among the action buttons: the rim in the
// selection's border, the light top edge of the glass. Longpath marks
// its primary action in the selection blue (VaxFirstRunDialog
// primaryButtonStyle, Longpath src/gui/VaxFirstRunDialog.cpp:96-105
// [@551576e3]); NereusSDR keeps the fill raised and puts the blue on the
// rim, so the default button does not read as checked.
QString defaultRules(const QStringList& selectors)
{
    // [original comment from StyleConstants.h:808-816 is at glassTreatment() above]
    QStringList sels;
    for (const QString& s : selectors) {
        sels << s + QStringLiteral(":default");
    }
    return QStringLiteral("\n%1\n%2 { border-color: %3; border-top-color: %4; }")
        .arg(QLatin1String(kDefaultMarker), sels.join(QStringLiteral(", ")), hex(kGlassSelBorder),
             shiftL(QColor(hex(kGlassSelBorder)), kGlassBorderTopLift));
}

QString processBody(const QString& rawBody, const Target& t, bool* restyledOut = nullptr,
                    bool* actionOut = nullptr)
{
    if (actionOut != nullptr) {
        *actionOut = false;
    }
    QString body = rawBody;
    if (body.contains(QLatin1String("selection-"), Qt::CaseInsensitive)) {
        Decls sd(body);
        selectionDecls(sd);
        body = sd.render();
    }
    if (restyledOut != nullptr) {
        *restyledOut = false;
    }
    if (t.kind == Kind::None || t.state == State::Other) {
        return body;
    }
    Decls d(body);
    const int bg = d.findLast({ "background", "background-color" });
    const FillInfo f = bg >= 0 ? classifyFill(d.value(bg)) : FillInfo{};
    switch (t.kind) {
    case Kind::Groove:
    case Kind::Handle:
    case Kind::SliderFill:
    case Kind::SliderRecess:
        return sliderBody(d, bg, f, t);
    case Kind::Indicator:
        return indicatorBody(d, bg, t);
    case Kind::Pane:
        if (t.state == State::Normal && borderInfo(d).has) {
            d.set("border-radius", QStringLiteral("%1px").arg(radiusFor(t.height)));
        }
        return d.render();
    case Kind::Item:
        if (t.state == State::Checked && bg >= 0
            && (f.fill == Fill::Neutral || f.fill == Fill::Selection || f.fill == Fill::Ours
                || f.fill == Fill::OtherGradient)) {
            itemSelection(d, bg);
        }
        return d.render();
    case Kind::Section:
        // Header sections that are blue for decoration take the header
        // text of the application's sections (tableRules()).
        if (t.state == State::Normal) {
            const int ci = d.findLast({ "color" });
            if (ci >= 0 && classifyFill(d.value(ci)).fill == Fill::Selection) {
                d.replaceValue(ci, hex(kTextPrimary));
            }
        }
        return d.render();
    default:
        break;
    }
    // JJ's D30 and D31 (2026-10-10): a button that is not checkable and
    // is given today's engaged green as its whole sheet while it is on.
    // OPERATE on is the raised lit green of a checked processing button
    // (D15) and stays lit under the pointer; the tuner's chosen antenna is
    // the selection glass. Both take the button's radius (D12).
    if (t.kind == Kind::Button && (t.litWhenOn || t.choice) && bg >= 0) {
        if (t.state == State::Normal && litOf(d.value(bg)) == Lit::Green) {
            if (t.choice) {
                glassTreatment(d);
            } else {
                litTreatment(d, bg, Lit::Green);
            }
            d.set("border-radius", QStringLiteral("%1px").arg(radiusFor(t.height)));
            return d.render();
        }
        if (t.state == State::Hover && t.litWhenOn) {
            const QColor hover = parseColour(stripComments(d.value(bg)));
            if (hover.isValid()
                && hover.name() == WorkspacePalette::glassAndDepth().color(QStringLiteral("dsp-on-border")).name()) {
                setBackground(d, bg, litGreenFill());
                return d.render();
            }
        }
    }
    if (t.kind == Kind::Button && t.state == State::Normal && t.notChecked) {
        if (restyledOut != nullptr) {
            *restyledOut = true;
        }
        raisedOff(d, bg);
        return d.render();
    }
    if (t.kind == Kind::Button && t.baseIsChecked && t.state == State::Normal && f.fill == Fill::Semantic) {
        const Lit lit = litOf(d.value(bg));
        if (lit != Lit::None) {
            litTreatment(d, bg, lit);
            return d.render();
        }
    }
    if (t.kind == Kind::Button && t.state == State::Checked && f.fill == Fill::Semantic) {
        const Lit lit = litOf(d.value(bg));
        if (lit != Lit::None) {
            litTreatment(d, bg, lit);
            return d.render();
        }
        return body;
    }
    // The hover and pressed states of a blue action button follow it to
    // the raised button.
    if (t.kind == Kind::Button && t.action && f.fill == Fill::Semantic
        && (t.state == State::Hover || t.state == State::Pressed)) {
        const QColor c = parseColour(stripComments(d.value(bg)));
        if (c.isValid() && c.hsvHue() >= 180 && c.hsvHue() <= 260) {
            if (restyledOut != nullptr) {
                *restyledOut = true;
            }
            if (t.state == State::Hover) {
                setBackground(d, bg, raisedHoverFill());
            } else {
                setBackground(d, bg, pressedFill());
                pressedEdges(d);
            }
            if (d.has("color")) {
                d.set("color", hex(kTextPrimary));
            }
            return d.render();
        }
    }
    const bool restyle = f.fill == Fill::Neutral || f.fill == Fill::Selection || f.fill == Fill::Ours;
    if (!restyle) {
        return body;
    }
    if (restyledOut != nullptr) {
        *restyledOut = true;
    }
    const bool selected = f.fill == Fill::Selection || f.ours == Ours::Glass;
    const BorderInfo b = borderInfo(d);
    const bool neutralBorder = b.colour != Fill::Semantic;
    const QString radius = QStringLiteral("%1px").arg(radiusFor(t.height));

    switch (t.kind) {
    case Kind::Button:
    case Kind::Combo:
    case Kind::Tab:
        switch (t.state) {
        case State::Normal:
            if (selected && t.kind == Kind::Button && t.action) {
                actionTreatment(d, bg, b);
                if (actionOut != nullptr) {
                    *actionOut = true;
                }
            } else if (selected) {
                glassTreatment(d);
            } else {
                setBackground(d, bg, raisedFill());
                if (b.has && neutralBorder) {
                    raisedEdges(d);
                }
            }
            break;
        case State::Hover:
            setBackground(d, bg, raisedHoverFill());
            break;
        case State::Pressed:
            setBackground(d, bg, pressedFill());
            pressedEdges(d);
            if (d.has("color")) {
                d.set("color", hex(kTextPrimary));
            }
            break;
        case State::Checked:
            glassTreatment(d);
            break;
        case State::Disabled:
            disabledTreatment(d, d.findLast({ "background", "background-color" }), b);
            break;
        case State::Other:
            break;
        }
        if (t.kind != Kind::Tab && (t.state == State::Normal || d.has("border-radius"))) {
            d.set("border-radius", radius);
        }
        // A tab's top corners take the tab bar's radius (JJ, 2026-10-08).
        if (t.kind == Kind::Tab && t.state == State::Normal) {
            d.set("border-top-left-radius", radius);
            d.set("border-top-right-radius", radius);
        }
        break;
    case Kind::Field:
    case Kind::Label:
        if (t.kind == Kind::Label && (t.state != State::Normal || !b.has)) {
            return body;
        }
        if (t.state == State::Normal || t.state == State::Hover) {
            // A gradient background makes Qt draw a spin box without its
            // native buttons, which changes its size hint (measured by
            // tst_workspace_qss_translator). Fields take the flat inset
            // colour; the sunken edges carry the depth. Value chips
            // (labels) have no subcontrols and take the sunken fill.
            const QColor ruleFill = bg >= 0 ? parseColour(stripComments(d.value(bg))) : QColor();
            setBackground(d, bg, t.kind == Kind::Label ? fieldFill() : hex(kInsetBg));
            // Text chosen dark for a light neutral fill (the HL2 I2C read
            // boxes' black on white) vanishes on the dark chip: it takes
            // the chip's text colour with the fill. Text a dark fill
            // already carries (a dim "NO CARRIER" lamp) keeps its colour.
            if (t.kind == Kind::Label && ruleFill.isValid() && ruleFill.lightnessF() >= 0.5f) {
                const int ci = d.findLast({ "color" });
                const QColor text = ci >= 0 ? parseColour(stripComments(d.value(ci))) : QColor();
                if (text.isValid() && text.alpha() == 255 && text.lightnessF() < 0.5f
                    && text.hsvSaturationF() < 0.25f) {
                    d.replaceValue(ci, hex(kTextPrimary));
                }
            }
            if (b.has && neutralBorder) {
                if (t.kind == Kind::Label) {
                    // VERSENKT: oben dunkel, unten heller — siehe die Notiz bei
                    // buttonBaseStyle. Dasselbe Mittel wie bei der Platte, nur
                    // umgedreht, und das Feld liegt drin statt drauf.
                    // Glas & Tiefe (2026-09-17): ein Glaschip wie im Bandfilter — oben
                    // die dunkle Kante, unten Licht, Monospace fuer die Zahl (Regel 4).
                    // From Longpath src/gui/StyleConstants.h:1045 [@551576e3]
                    d.set("border-color", hex(kInsetBorder));
                }
                sunkenEdges(d);
            }
            d.set("border-radius", radius);
        } else if (t.state == State::Disabled) {
            disabledTreatment(d, bg, b);
        }
        break;
    case Kind::SpinButton:
        if (t.state == State::Normal) {
            setBackground(d, bg, raisedFill());
        } else if (t.state == State::Hover) {
            setBackground(d, bg, raisedHoverFill());
        } else if (t.state == State::Pressed) {
            setBackground(d, bg, pressedFill());
        }
        break;
    default:
        break;
    }
    return d.render();
}

bool isCheckableButton(const QWidget* w)
{
    return w != nullptr && w->inherits("QAbstractButton") && w->property("checkable").toBool();
}

// Declarations of the pressed and selected states, for a checkable
// button whose sheet gives every state the same background: without
// them the look would draw checked and unchecked alike.
QString pressedDecls()
{
    Decls d(QString{});
    setBackground(d, -1, pressedFill());
    pressedEdges(d);
    return d.render();
}

QString checkedDecls()
{
    Decls d(QString{});
    glassTreatment(d);
    d.set("color", hex(kGlassSelText));
    return d.render();
}

QStringList splitSelectors(const QString& selectorText)
{
    QStringList out;
    int depth = 0;
    QString cur;
    for (QChar c : stripComments(selectorText)) {
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(')') || c == QLatin1Char(']')) {
            --depth;
        }
        if (c == QLatin1Char(',') && depth == 0) {
            out << cur.trimmed();
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.trimmed().isEmpty()) {
        out << cur.trimmed();
    }
    return out;
}

} // namespace

QString raisedFill()
{
    // From Longpath src/gui/StyleConstants.h:1114 [@551576e3]
    return vgrad(hex(kGlassBtnTop), hex(kGlassBtnBot));
}

QString raisedHoverFill()
{
    // From Longpath src/gui/StyleConstants.h:1119 [@551576e3]
    return vgrad(hex(kRaisedHoverTop), hex(kRaisedHoverBot));
}

QString raisedFillOf(const QColor& base)
{
    /// AUFGESETZT: oben heller, unten dunkler — Licht von oben, die
    /// Flaeche bekommt Dicke. Fuer Platten, Kopfleisten, Knoepfe.
    // From Longpath src/gui/StyleConstants.h:705-718 [@551576e3]
    // [original inline comment from StyleConstants.h:709-711: the depth
    //  from the theme; NereusSDR has no theme form value, so the lift is
    //  Longpath's default 16 and the drop three quarters of it]
    // Tiefe aus dem Thema, sofern es eine Meinung hat. Die Aufrufer
    // duerfen weiter eigene Werte setzen — der aktive Knopf etwa ist
    // absichtlich etwas kraeftiger als eine ruhende Platte.
    const int drop = kRaisedLift * 3 / 4;
    return QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:0.%2 %3, stop:1 %4)")
        .arg(shiftL(base, kRaisedLift))
        .arg(kRaisedMidStopPercent)
        .arg(base.name(), shiftL(base, -drop));
}

QString pressedFill()
{
    // From Longpath src/gui/StyleConstants.h:1121 [@551576e3]
    // aufgeklappt: gedrueckt, der Verlauf kippt
    return vgrad(hex(kGlassBtnBot), hex(kGlassBtnTop));
}

QString glassFill()
{
    // [original comment from StyleConstants.h:808-816 is at glassTreatment()]
    // From Longpath src/gui/StyleConstants.h:819 [@551576e3]
    return vgrad(hex(kGlassSelTop), hex(kGlassSelBot));
}

QString sunkenFill(const char* baseHex, int deepen, int lift)
{
    // From Longpath src/gui/StyleConstants.h:723-731 [@551576e3]
    /// VERSENKT: genau andersherum — oben dunkel, unten heller. Dasselbe
    /// Mittel, umgedreht, und die Flaeche liegt drin statt drauf.
    const QColor base(hex(baseHex));
    return vgrad(shiftL(base, -deepen), shiftL(base, lift));
}

QString fieldFill()
{
    return sunkenFill(kInsetBg, kFieldDeepen, kFieldLift);
}

/// Eine Tabelle hinter Glas (Glas & Tiefe, 2026-09-17): versenkt wie
/// ein Zahlenfeld, Kopfzeile ohne Kaesten — nur eine Haarlinie unter
/// den Titeln —, Auswahl im gedeckten Blauverlauf, Gitter leise. Keine
/// Schriftangaben: Groesse und Versalien setzt der Aufrufer per
/// setFont (Tabelle 11 px, Kopf capsFont 9 px), sonst kaskadiert eine
/// font-size aus dem Stylesheet in den Kopf und schlaegt sie tot.
// [original comment from StyleConstants.h:956-961]
// Ported: the grid colour (gridline-color, kBorderSubtle) and the hairline
// under the titles (border-bottom, kBorder). Not ported: the sunken table
// fill, selection gradient, zebra colour, transparent header and its
// 3 px padding.
// From Longpath src/gui/StyleConstants.h:968, :984, :988, :992 [@551576e3]
// NereusSDR: Fusion draws the grid in Window darker 120, which on the
// workspace app-bg is the colour of the cells, so the grid vanished
// (FreeDV Reporter). The header keeps a fill (button) and gets an edge in
// border between sections, so the sections stay apart.
QString tableRules()
{
    const auto& wp = WorkspacePalette::glassAndDepth();
    const auto r = [&wp](const char* role) { return wp.qss(QString::fromLatin1(role)); };
    return QStringLiteral(
        "QTableView, QTableWidget { gridline-color: %1; }"
        "QHeaderView::section {"
        "  background: %2; color: %3; border: none;"
        "  border-right: 1px solid %4; border-bottom: 1px solid %4;"
        "  padding: 0px 4px;"
        "}")
        .arg(r("border-subtle"), r("button"), r("text"), r("border"));
}

int radiusFor(int heightPx)
{
    return static_cast<int>(
        std::lround(static_cast<double>(heightPx) * kLongpathRadiusPx / kLongpathFieldHeightPx));
}

QString indicatorRulesFor(bool radio)
{
    // [original comment from StyleConstants.h:1097-1100 is at indicatorBody() above]
    // From Longpath src/gui/StyleConstants.h:1138-1145, :1151-1158 [@551576e3]
    const QString s = radio ? QStringLiteral("QRadioButton::indicator") : QStringLiteral("QCheckBox::indicator");
    QString out = QStringLiteral("%1 { background: %2; border: 1px solid %3; border-radius: %4; }")
                      .arg(s, indicatorOffFill(), hex(kTextInactive), hex(radio ? kRadioRadius : kBoxRadius));
    out += indicatorStateRules(s);
    out += QLatin1Char('\n');
    return out;
}

QString indicatorRules()
{
    return indicatorRulesFor(false) + indicatorRulesFor(true);
}

QString groupBoxIndicatorRules(int widthPx, int heightPx)
{
    // [original comment from StyleConstants.h:1097-1100 is at indicatorBody() above]
    // From Longpath src/gui/StyleConstants.h:1138-1145 [@551576e3]
    // NereusSDR: the rim comes off the platform style's indicator size, so
    // the indicator keeps its outer size.
    const QString s = QStringLiteral("QGroupBox::indicator");
    QString out = QStringLiteral("%1 { width: %2px; height: %3px; background: %4; border: %5px solid %6;"
                                 " border-radius: %7; }")
                      .arg(s)
                      .arg(std::max(0, widthPx - 2 * kIndicatorRimPx))
                      .arg(std::max(0, heightPx - 2 * kIndicatorRimPx))
                      .arg(indicatorOffFill())
                      .arg(kIndicatorRimPx)
                      .arg(hex(kTextInactive), hex(kBoxRadius));
    out += indicatorStateRules(s);
    out += QLatin1Char('\n');
    return out;
}

QString progressChunkRules()
{
    // NereusSDR (settled 2026-10-10): the filled part of a progress bar
    // is the selection, the same glass as every other selection; the
    // platform style drew it from a lightened QPalette::Highlight. The
    // groove stays the platform style's, and the chunk stays inside its
    // 1 px rim. A sheet on a progress bar left-aligns the percent text,
    // so the rule says where the platform style drew it: the middle.
    // [original comment from StyleConstants.h:808-816 is at glassTreatment() above]
    return QStringLiteral("QProgressBar { text-align: center; }\n"
                          "QProgressBar::chunk { background: %1; margin: 1px; }\n").arg(glassFill());
}

QString sliderRulesFor(bool vertical)
{
    // [original comment from StyleConstants.h:899-902 is at sliderGroove() above]
    // From Longpath src/gui/StyleConstants.h:899-939 [@551576e3]
    Target t;
    t.horizontal = !vertical;
    t.vertical = vertical;
    const QString orient = vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal");
    Decls groove(QString{});
    sliderGroove(groove, -1, t);
    Decls handle(QString{});
    sliderHandle(handle, -1, t);
    // From Longpath src/gui/StyleConstants.h:916, :1239 [@551576e3]
    return QStringLiteral("QSlider::groove:%1 {%2}\n"
                          "QSlider::handle:%1 {%3}\n"
                          "QSlider::handle:%1:hover, QSlider::handle:%1:pressed { background: %4; }\n"
                          "QSlider::groove:%1:disabled {%5}\n")
        .arg(orient, groove.render(), handle.render(), hex(kGlassBtnTop), grooveDisabledDecls());
}

QString componentize(const QString& qss, const QWidget* owner)
{
    if (!qss.contains(QLatin1Char('{'))) {
        const Target t = classifySelectorless(owner);
        bool restyled = false;
        bool action = false;
        const QString body = processBody(qss, t, &restyled, &action);
        if (action && t.pushButton) {
            return QStringLiteral("* {%1}").arg(body) + defaultRules({ QStringLiteral("*") });
        }
        if (restyled && t.kind == Kind::Button && t.state == State::Normal
            && isCheckableButton(owner)) {
            // A brace-less sheet is a "*" rule (specificity 0), so the
            // state rules below win over it.
            return QStringLiteral("* {%1}\n*:pressed {%2}\n*:checked {%3}")
                .arg(body, pressedDecls(), checkedDecls());
        }
        return body;
    }

    QString out;
    out.reserve(qss.size() + qss.size() / 2);
    QStringList handleSelectors;
    QStringList grooveSelectors;
    QStringList disabledGrooveSelectors;
    bool handleHover = false;
    bool handlePressed = false;
    QStringList indicatorSelectors;
    // An indicator rule of the sheet draws a rim wider than 1 px (the NR2
    // page's radios, 2 px): a radio's checked rim then keeps one colour.
    static const QRegularExpression wideIndicatorRim(
        QStringLiteral("::indicator[^{}]*\\{[^}]*border(?:-width)?\\s*:\\s*(?:[2-9]|\\d{2,})px"),
        QRegularExpression::CaseInsensitiveOption);
    const bool sheetWideIndicatorRim = wideIndicatorRim.match(qss).hasMatch();
    bool wideRimSelectors = false;
    QStringList ownerButtonSelectors;
    QStringList defaultSelectors;
    const QString lowerSheet = qss.toLower();
    const bool sheetChecks = lowerSheet.contains(QLatin1String(":checked")) || lowerSheet.contains(QLatin1String(":on"));
    const bool sheetDims = lowerSheet.contains(QLatin1String(":!checked"));
    const qsizetype n = qss.size();
    qsizetype i = 0;
    qsizetype selStart = 0;
    while (i < n) {
        const QChar c = qss.at(i);
        if (c == QLatin1Char('/') && i + 1 < n && qss.at(i + 1) == QLatin1Char('*')) {
            const qsizetype end = qss.indexOf(QLatin1String("*/"), i + 2);
            i = end < 0 ? n : end + 2;
            continue;
        }
        if (c != QLatin1Char('{')) {
            ++i;
            continue;
        }
        const QString selectorText = qss.mid(selStart, i - selStart);
        // Find the closing brace, skipping comments and strings.
        qsizetype j = i + 1;
        QChar quote;
        while (j < n) {
            const QChar d = qss.at(j);
            if (!quote.isNull()) {
                if (d == quote) {
                    quote = QChar();
                }
            } else if (d == QLatin1Char('"') || d == QLatin1Char('\'')) {
                quote = d;
            } else if (d == QLatin1Char('/') && j + 1 < n && qss.at(j + 1) == QLatin1Char('*')) {
                const qsizetype end = qss.indexOf(QLatin1String("*/"), j + 2);
                j = end < 0 ? n : end + 1;
            } else if (d == QLatin1Char('}')) {
                break;
            }
            ++j;
        }
        const QString body = qss.mid(i + 1, j - i - 1);

        const QStringList sels = splitSelectors(selectorText);
        QStringList blockGrooves;
        Target t;
        bool first = true;
        bool agree = !sels.isEmpty();
        for (const QString& s : sels) {
            Target st = classifySelector(s, owner);
            if (st.kind == Kind::Button && st.unresolved && sheetChecks) {
                st.action = false;
            }
            if (st.kind == Kind::Button && st.state == State::Normal && !st.notChecked && sheetDims
                && isCheckableButton(st.isOwner ? owner : nullptr)) {
                st.baseIsChecked = true;
            }
            if (st.kind == Kind::Indicator && st.radio && sheetWideIndicatorRim) {
                st.wideRim = true;
                wideRimSelectors = true;
            }
            if (first) {
                t = st;
                first = false;
            } else if (!(st == t)) {
                agree = false;
            }
            if (st.kind == Kind::Groove && st.state == State::Normal) {
                blockGrooves << s.trimmed();
            } else if (st.kind == Kind::Groove && st.state == State::Disabled) {
                disabledGrooveSelectors << s.trimmed();
            }
            if (st.kind == Kind::Handle) {
                if (st.state == State::Normal) {
                    handleSelectors << s;
                } else if (st.state == State::Hover) {
                    handleHover = true;
                } else if (st.state == State::Pressed) {
                    handlePressed = true;
                }
            }
            if (st.kind == Kind::Indicator && st.state == State::Normal
                && !lastCompound(s).contains(QLatin1Char(':') + QLatin1String("unchecked"))) {
                indicatorSelectors << s;
            }
        }
        out += selectorText;
        out += QLatin1Char('{');
        bool restyled = false;
        bool action = false;
        out += agree ? processBody(body, t, &restyled, &action) : body;
        if (agree && grooveTreated(body)) {
            grooveSelectors << blockGrooves;
        }
        if (agree && restyled && t.kind == Kind::Button && t.state == State::Normal && t.isOwner) {
            ownerButtonSelectors << sels;
        }
        if (agree && action && t.pushButton) {
            defaultSelectors << sels;
        }
        if (j < n) {
            out += QLatin1Char('}');
        }
        i = j + 1;
        selStart = i;
    }
    out += qss.mid(std::min(selStart, n));

    const QString lower = qss.toLower();

    // A checkable button whose own sheet has no pressed or checked rule.
    if (!ownerButtonSelectors.isEmpty() && isCheckableButton(owner)
        && !lower.contains(QLatin1String(":checked")) && !lower.contains(QLatin1String(":on"))) {
        QStringList pressed;
        QStringList checked;
        for (const QString& s : ownerButtonSelectors) {
            pressed << s + QStringLiteral(":pressed");
            checked << s + QStringLiteral(":checked");
        }
        if (!lower.contains(QLatin1String(":pressed"))) {
            out += QStringLiteral("\n%1 {%2}").arg(pressed.join(QStringLiteral(", ")), pressedDecls());
        }
        out += QStringLiteral("\n%1 {%2}").arg(checked.join(QStringLiteral(", ")), checkedDecls());
    }

    // The handle's hover and pressed states, when the sheet has none:
    // flat, as Longpath's hover.
    // From Longpath src/gui/StyleConstants.h:916, :1239 [@551576e3]
    const auto appendHandleState = [&](const char* state) {
        QStringList sels;
        for (const QString& s : handleSelectors) {
            sels << s + QLatin1Char(':') + QLatin1String(state);
        }
        out += QStringLiteral("\n%1 { background: %2; }").arg(sels.join(QStringLiteral(", ")), hex(kGlassBtnTop));
    };
    if (!handleSelectors.isEmpty() && !handleHover) {
        appendHandleState("hover");
    }
    if (!handleSelectors.isEmpty() && !handlePressed) {
        appendHandleState("pressed");
    }

    // NereusSDR, JJ's D29 (2026-10-09): a track the look draws gets the
    // disabled track when its sheet has no disabled groove rule (Qt would
    // draw the enabled track on a disabled slider).
    QStringList disabledGrooves;
    for (const QString& s : grooveSelectors) {
        const QString ds = s + QStringLiteral(":disabled");
        if (!disabledGrooveSelectors.contains(ds)) {
            disabledGrooves << ds;
        }
    }
    disabledGrooves.removeDuplicates();
    if (!disabledGrooves.isEmpty()) {
        out += QStringLiteral("\n%1 {%2}").arg(disabledGrooves.join(QStringLiteral(", ")), grooveDisabledDecls());
    }

    // The default button's rim, once.
    defaultSelectors.removeDuplicates();
    if (!defaultSelectors.isEmpty() && !qss.contains(QLatin1String(kDefaultMarker))) {
        out += defaultRules(defaultSelectors);
    }

    // The indicator's states after the sheet's own rules, once.
    indicatorSelectors.removeDuplicates();
    if (!indicatorSelectors.isEmpty() && !qss.contains(QLatin1String(kIndicatorStatesMarker))) {
        out += QLatin1Char('\n') + QLatin1String(kIndicatorStatesMarker);
        for (const QString& s : indicatorSelectors) {
            out += indicatorStateRules(s, wideRimSelectors);
        }
    }
    return out;
}

QStringList generatedColours()
{
    const QStringList values = {
        raisedFill(),
        raisedHoverFill(),
        pressedFill(),
        glassFill(),
        fieldFill(),
        shiftL(QColor(hex(kGlassSelBorder)), kGlassBorderTopLift),
        handleTopEdge(),
        hex(kGlassShade),
        hex(kGlassLight),
        indicatorOffFill(),
        hex(kIndicatorCheckedTopEdge),
        litGreenFill(),
        litGreenText(),
        litGreenRim(),
        shiftL(QColor(litGreenRim()), kLitEdgeLift),
        litAmberFill(),
        shiftL(QColor(hex(kAmberBorder)), kLitEdgeLift),
    };
    QString decls;
    for (const QString& v : values) {
        decls += QStringLiteral("background: %1;").arg(v);
    }
    // No table and no kept list: every colour comes back as reported.
    const QssTranslator::Table noRows;
    const QssTranslator::KeptList noKept;
    QssTranslator::Options options;
    options.table = &noRows;
    options.kept = &noKept;
    QStringList out = QssTranslator::translate(decls, options).unknownLiterals;
    out.removeDuplicates();
    return out;
}

} // namespace NereusSDR::Workspace::Components
