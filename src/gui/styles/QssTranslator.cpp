// =================================================================
// src/gui/styles/QssTranslator.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See QssTranslator.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace preview by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - A slider's filled page resolves #203040 to the accent.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Reads 3/4/6/8-digit hex, rgb(), rgba() and colour names
//                 through one lookup; rows by property kind; kept list.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - The spin arrow urls take the workspace variants.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Token-aware font-size floor, J.J. Boyd (KG4VCF),
//                 AI-assisted via OpenAI Codex.
// =================================================================

#include "QssTranslator.h"

#include "StylesheetColourTable.h"
#include "WorkspacePalette.h"

#include <QColor>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace NereusSDR::QssTranslator {

namespace {

constexpr int kMaxComponent = 255;
constexpr double kPercent = 100.0;

bool isTokenChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-');
}

bool isHexDigit(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f') || (u >= u'A' && u <= u'F');
}

bool allHex(QStringView s)
{
    if (s.isEmpty()) {
        return false;
    }
    for (QChar c : s) {
        if (!isHexDigit(c)) {
            return false;
        }
    }
    return true;
}

bool allLetters(QStringView s)
{
    if (s.isEmpty()) {
        return false;
    }
    for (QChar c : s) {
        const char16_t u = c.unicode();
        if (!((u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z'))) {
            return false;
        }
    }
    return true;
}

QString indexKey(const QString& hex, const QString& kind)
{
    return hex.toLower() + QLatin1Char('|') + kind;
}

// First row wins for a duplicated key.
QHash<QString, QString> buildIndex(const Table& table)
{
    QHash<QString, QString> index;
    for (const Rule& r : table) {
        const QString key = indexKey(r.legacyHex, r.kind);
        if (!index.contains(key)) {
            index.insert(key, r.role);
        }
    }
    return index;
}

const QHash<QString, QString>& defaultIndex()
{
    static const QHash<QString, QString> index = buildIndex(defaultTable());
    return index;
}

QSet<QString> buildKept(const KeptList& list)
{
    QSet<QString> set;
    for (const Keep& k : list) {
        set.insert(indexKey(k.literal, k.kind));
    }
    return set;
}

const QSet<QString>& defaultKeptSet()
{
    static const QSet<QString> set = buildKept(defaultKept());
    return set;
}

QString contextRoleForAmbiguous(const QString& selector, const QString& property)
{
    const QString sel = selector.toLower();
    const QString prop = property.trimmed().toLower();
    // A slider's filled part is the accent, not the groove: the sub-page
    // on a horizontal slider, the add-page on a vertical one (Qt draws
    // a vertical slider's sub-page above the handle). The same split as
    // WorkspaceComponents. Scroll bar pages are track, so they stay groove.
    const bool scrollBar = sel.contains(QLatin1String("qscrollbar"));
    const bool vertical = sel.contains(QLatin1String(":vertical"));
    const bool subPage = sel.contains(QLatin1String("::sub-page"));
    const bool addPage = sel.contains(QLatin1String("::add-page"));
    if (!scrollBar && ((subPage && !vertical) || (addPage && vertical))) {
        return QStringLiteral("accent");
    }
    if (sel.contains(QLatin1String("::groove")) || addPage || subPage) {
        return QStringLiteral("groove");
    }
    if (prop.startsWith(QLatin1String("border")) || prop.startsWith(QLatin1String("outline"))) {
        return QStringLiteral("border-subtle");
    }
    if ((sel.contains(QLatin1String(":hover")) || sel.contains(QLatin1String(":pressed")))
        && prop.startsWith(QLatin1String("background"))) {
        return QStringLiteral("button-hover");
    }
    return QStringLiteral("border-subtle");
}

bool startsWithUrl(const QString& s, qsizetype i)
{
    if (i + 4 > s.size()) {
        return false;
    }
    if (i > 0 && isTokenChar(s.at(i - 1))) {
        return false;
    }
    return QStringView(s).mid(i, 4).compare(QLatin1String("url("), Qt::CaseInsensitive) == 0;
}

// How a colour literal was written, so the rewrite keeps its form.
enum class Form { Opaque, HexAlpha, FunctionAlpha };

struct Colour {
    QString literal;  // as reported
    QString key;      // "#rrggbb"
    Form form = Form::Opaque;
    QString alpha;    // two hex digits for HexAlpha, the argument for FunctionAlpha
};

QString hexKey(int r, int g, int b)
{
    return QColor(r, g, b).name(QColor::HexRgb).toLower();
}

// '#' plus 3, 4, 6 or 8 hex digits. Qt reads 8 digits as #AARRGGBB;
// 4 digits are read the same way, one digit per channel.
std::optional<Colour> readHex(const QString& token)
{
    const QString digits = token.mid(1).toLower();
    if (!allHex(digits)) {
        return std::nullopt;
    }
    Colour c;
    c.literal = token.toLower();
    switch (digits.size()) {
    case 3:
        c.key = QStringLiteral("#%1%1%2%2%3%3").arg(digits.at(0), digits.at(1), digits.at(2));
        return c;
    case 4:
        c.key = QStringLiteral("#%1%1%2%2%3%3").arg(digits.at(1), digits.at(2), digits.at(3));
        c.form = Form::HexAlpha;
        c.alpha = QString(2, digits.at(0));
        return c;
    case 6:
        c.key = QLatin1Char('#') + digits;
        return c;
    case 8:
        c.key = QLatin1Char('#') + digits.mid(2);
        c.form = Form::HexAlpha;
        c.alpha = digits.left(2);
        return c;
    default:
        return std::nullopt;
    }
}

// One rgb component: an integer 0-255 or a percentage.
std::optional<int> readComponent(QString arg)
{
    arg = arg.trimmed();
    bool ok = false;
    double v = 0.0;
    if (arg.endsWith(QLatin1Char('%'))) {
        v = arg.chopped(1).trimmed().toDouble(&ok) * kMaxComponent / kPercent;
    } else {
        v = arg.toDouble(&ok);
    }
    if (!ok || v < 0.0 || v > kMaxComponent) {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(v));
}

// rgb(r, g, b) or rgba(r, g, b, a). `literal` is the whole function.
Colour readFunction(const QString& args, const QString& literal)
{
    Colour c;
    c.literal = literal.toLower();
    c.literal.remove(QLatin1Char(' '));
    c.literal.remove(QLatin1Char('\t'));
    c.literal.remove(QLatin1Char('\n'));
    const QStringList parts = args.split(QLatin1Char(','));
    if (parts.size() != 3 && parts.size() != 4) {
        return c;  // no key: unreadable, left and reported
    }
    const auto r = readComponent(parts.at(0));
    const auto g = readComponent(parts.at(1));
    const auto b = readComponent(parts.at(2));
    if (!r || !g || !b) {
        return c;
    }
    c.key = hexKey(*r, *g, *b);
    if (parts.size() == 4) {
        c.form = Form::FunctionAlpha;
        c.alpha = parts.at(3).trimmed();
    }
    return c;
}

QString rewrittenValue(const Colour& c, const QString& roleValue)
{
    const QColor role = QColor::fromString(roleValue);
    switch (c.form) {
    case Form::Opaque:
        return roleValue;
    case Form::HexAlpha:
        if (!role.isValid()) {
            return {};
        }
        return QLatin1Char('#') + c.alpha + role.name(QColor::HexRgb).mid(1).toLower();
    case Form::FunctionAlpha:
        if (!role.isValid()) {
            return {};
        }
        return QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(role.red())
            .arg(role.green())
            .arg(role.blue())
            .arg(c.alpha);
    }
    return {};
}

} // namespace

const Table& defaultTable()
{
    static const Table table = [] {
        Table t;
        for (const auto& row : Workspace::WorkspacePalette::legacyRows()) {
            t.push_back({QString::fromLatin1(row.legacyHex), QString::fromLatin1(row.role), {}});
        }
        for (const auto& row : Workspace::StylesheetColours::rows()) {
            t.push_back({QString::fromLatin1(row.hex), QString::fromLatin1(row.role),
                         row.kind != nullptr ? QString::fromLatin1(row.kind) : QString()});
        }
        return t;
    }();
    return table;
}

const KeptList& defaultKept()
{
    static const KeptList list = [] {
        KeptList l;
        for (const auto& k : Workspace::StylesheetColours::kept()) {
            l.push_back({QString::fromLatin1(k.literal),
                         k.kind != nullptr ? QString::fromLatin1(k.kind) : QString()});
        }
        return l;
    }();
    return list;
}

QString propertyKind(const QString& property)
{
    const QString p = property.trimmed().toLower();
    if (p.startsWith(QLatin1String("background")) || p == QLatin1String("alternate-background-color")
        || p == QLatin1String("selection-background-color")) {
        return QStringLiteral("background");
    }
    if (p.startsWith(QLatin1String("border")) || p.startsWith(QLatin1String("outline"))
        || p == QLatin1String("gridline-color")) {
        return QStringLiteral("border");
    }
    if (p == QLatin1String("color") || p == QLatin1String("selection-color")
        || p == QLatin1String("placeholder-text-color")) {
        return QStringLiteral("text");
    }
    return {};
}

// The spin arrows drawn in #c8d8e8 (resources/icons/spin-up.svg and
// spin-down.svg) have workspace variants in the text colour.
QString workspaceUrl(QStringView url)
{
    static const QString up = QStringLiteral(":/icons/spin-up.svg");
    static const QString down = QStringLiteral(":/icons/spin-down.svg");
    QString u = url.toString();
    if (u.contains(up)) {
        u.replace(up, QStringLiteral(":/icons/spin-up-workspace.svg"));
    } else if (u.contains(down)) {
        u.replace(down, QStringLiteral(":/icons/spin-down-workspace.svg"));
    }
    return u;
}

Result translate(const QString& original, const Options& options)
{
    Result result;
    const auto& palette = Workspace::WorkspacePalette::glassAndDepth();
    const QHash<QString, QString> customIndex =
        options.table != nullptr ? buildIndex(*options.table) : QHash<QString, QString>{};
    const QHash<QString, QString>& index =
        options.table != nullptr ? customIndex : defaultIndex();
    const QSet<QString> customKept =
        options.kept != nullptr ? buildKept(*options.kept) : QSet<QString>{};
    const QSet<QString>& keptSet = options.kept != nullptr ? customKept : defaultKeptSet();
    const QString ambiguous = QString::fromLatin1(kAmbiguousHex);

    const bool hasBraces = original.contains(QLatin1Char('{'));
    bool inBlock = !hasBraces;
    bool inValue = false;
    QString selector;
    QString property;

    QString& out = result.qss;
    out.reserve(original.size());
    const qsizetype n = original.size();

    // One lookup for every form: explicit role, kept list, the #203040
    // context rule, a row for the property's kind, a row for any
    // property. Appends the rewrite or the source text.
    auto rewrite = [&](const Colour& c, QStringView source) {
        QString role;
        if (!c.key.isEmpty()) {
            const QString kind = propertyKind(property);
            role = options.explicitRoles.value(c.key);
            const bool keep = role.isEmpty()
                && (keptSet.contains(indexKey(c.literal, QString()))
                    || (!kind.isEmpty() && keptSet.contains(indexKey(c.literal, kind))));
            if (!keep) {
                if (role.isEmpty() && c.key == ambiguous && options.resolveAmbiguousByContext) {
                    role = contextRoleForAmbiguous(selector, property);
                }
                if (role.isEmpty() && !kind.isEmpty()) {
                    role = index.value(indexKey(c.key, kind));
                }
                if (role.isEmpty()) {
                    role = index.value(indexKey(c.key, QString()));
                }
            }
        }
        const QString value = role.isEmpty() ? QString() : rewrittenValue(c, palette.qss(role));
        if (value.isEmpty()) {
            result.unknownLiterals.push_back(c.literal);
            out += source;
        } else {
            out += value;
            ++result.rewritten;
        }
    };

    for (qsizetype i = 0; i < n;) {
        const QChar c = original.at(i);

        // Comments are copied untouched.
        if (c == QLatin1Char('/') && i + 1 < n && original.at(i + 1) == QLatin1Char('*')) {
            qsizetype end = original.indexOf(QLatin1String("*/"), i + 2);
            end = end < 0 ? n : end + 2;
            out += QStringView(original).mid(i, end - i);
            i = end;
            continue;
        }

        // Quoted strings are copied untouched.
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            qsizetype j = i + 1;
            while (j < n && original.at(j) != c) {
                j += (original.at(j) == QLatin1Char('\\')) ? 2 : 1;
            }
            j = std::min<qsizetype>(j + 1, n);
            out += QStringView(original).mid(i, j - i);
            if (hasBraces && !inBlock) {
                selector += QStringView(original).mid(i, j - i);
            }
            i = j;
            continue;
        }

        // url(...) is copied untouched, except the spin arrows, which have
        // a workspace variant in the text colour.
        if (inBlock && startsWithUrl(original, i)) {
            qsizetype end = original.indexOf(QLatin1Char(')'), i + 4);
            end = end < 0 ? n : end + 1;
            out += options.translateColours
                ? workspaceUrl(QStringView(original).mid(i, end - i))
                : QStringView(original).mid(i, end - i).toString();
            i = end;
            continue;
        }

        if (hasBraces && c == QLatin1Char('{')) {
            inBlock = true;
            inValue = false;
            property.clear();
            out += c;
            ++i;
            continue;
        }
        if (hasBraces && c == QLatin1Char('}')) {
            inBlock = false;
            inValue = false;
            property.clear();
            selector.clear();
            out += c;
            ++i;
            continue;
        }
        if (inBlock && c == QLatin1Char(';')) {
            inValue = false;
            property.clear();
            out += c;
            ++i;
            continue;
        }
        if (inBlock && !inValue && c == QLatin1Char(':')) {
            inValue = true;
            out += c;
            ++i;
            continue;
        }

        // Only a font-size declaration's numeric token is eligible. The
        // comment/string/url paths above preserve their contents verbatim.
        if (inBlock && inValue && options.minimumTextPixelSize > 0
            && property.trimmed().compare(QLatin1String("font-size"), Qt::CaseInsensitive) == 0
            && (c.isDigit() || c == QLatin1Char('.'))
            && (i == 0 || original.at(i - 1).isSpace() || original.at(i - 1) == QLatin1Char(':'))) {
            qsizetype end = i;
            while (end < n && (original.at(end).isDigit() || original.at(end) == QLatin1Char('.'))) {
                ++end;
            }
            bool valid = false;
            const qreal size = original.mid(i, end - i).toDouble(&valid);
            const QStringView unit = QStringView(original).mid(end, 2);
            const bool pixels = unit.compare(QLatin1String("px"), Qt::CaseInsensitive) == 0;
            const bool points = unit.compare(QLatin1String("pt"), Qt::CaseInsensitive) == 0;
            const qreal logicalPixels = points ? size * options.textLogicalDpi / 72.0 : size;
            if (valid && (pixels || points) && size > 0
                && logicalPixels < options.minimumTextPixelSize
                && (end + 2 == n || !isTokenChar(original.at(end + 2)))) {
                out += QString::number(options.minimumTextPixelSize) + QLatin1String("px");
                i = end + 2;
                continue;
            }
        }

        if (options.translateColours && c == QLatin1Char('#')) {
            qsizetype j = i + 1;
            while (j < n && isTokenChar(original.at(j))) {
                ++j;
            }
            const QString token = original.mid(i, j - i);
            if (!inBlock) {
                // A selector such as "QWidget#name" is preserved.
                selector += token;
                out += token;
                i = j;
                continue;
            }
            const std::optional<Colour> colour = readHex(token);
            if (!colour) {
                out += token;
            } else {
                rewrite(*colour, token);
            }
            i = j;
            continue;
        }

        // rgb(), rgba() and colour names, in a declaration's value.
        if (options.translateColours && inBlock && inValue && c.isLetter() && (i == 0 || !isTokenChar(original.at(i - 1)))) {
            qsizetype j = i + 1;
            while (j < n && isTokenChar(original.at(j))) {
                ++j;
            }
            const QString ident = original.mid(i, j - i);
            const QString lower = ident.toLower();
            if (j < n && original.at(j) == QLatin1Char('(')) {
                if (lower == QLatin1String("rgb") || lower == QLatin1String("rgba")) {
                    qsizetype close = original.indexOf(QLatin1Char(')'), j + 1);
                    close = close < 0 ? n : close + 1;
                    const QString literal = original.mid(i, close - i);
                    const QString args = original.mid(j + 1, std::max<qsizetype>(0, close - j - 2));
                    rewrite(readFunction(args, literal), literal);
                    i = close;
                    continue;
                }
                // Another function: its name is copied, its arguments
                // are read like any other value text.
                out += ident;
                i = j;
                continue;
            }
            if (!propertyKind(property).isEmpty() && allLetters(ident)
                && lower != QLatin1String("transparent") && lower != QLatin1String("none")
                && QColor::isValidColorName(lower)) {
                const QColor named = QColor::fromString(lower);
                Colour colour;
                colour.literal = lower;
                colour.key = named.name(QColor::HexRgb).toLower();
                rewrite(colour, ident);
                i = j;
                continue;
            }
            out += ident;
            i = j;
            continue;
        }

        if (hasBraces && !inBlock) {
            selector += c;
        } else if (inBlock && !inValue) {
            property += c;
        }
        out += c;
        ++i;
    }
    return result;
}

Result translateWithAmbiguousRole(const QString& original, const QString& roleFor203040)
{
    Options options;
    options.explicitRoles.insert(QString::fromLatin1(kAmbiguousHex), roleFor203040);
    return translate(original, options);
}

} // namespace NereusSDR::QssTranslator
