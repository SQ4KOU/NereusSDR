// =================================================================
// src/core/settings/PanDisplayKeys.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See PanDisplayKeys.h for the key
// scheme and why the bare key is the Setup default.
//
// Modification history (NereusSDR)
//   2026-10-12 - Reuse sparse-value equality for selected Setup controls.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-12 - Approved master writes and equality clear pan overrides.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-10: Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//     Claude Code.
// =================================================================

#include "core/settings/PanDisplayKeys.h"

#include "core/AppSettings.h"

#include <QChar>

#include <algorithm>
#include <cmath>

namespace NereusSDR::PanDisplayKeys {

namespace {

// Two numbers closer than this, relative to the larger, are one value.
// A float carries about seven significant figures, so six is the most
// that survives a float -> string -> float round trip written by another
// formatter (Setup writing "0.7" where the widget writes
// "0.699999988079071").
constexpr double kSameNumberRelativeTolerance = 1e-6;

// AppSettings stores strings, and booleans are "True" / "False" by
// project rule; QVariant(bool).toString() would give "true".
QString stored(const QVariant& value)
{
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    }
    return value.toString();
}

bool sameValue(const QString& a, const QString& b)
{
    if (a == b) {
        return true;
    }
    bool aIsInteger = false;
    bool bIsInteger = false;
    const qlonglong aInteger = a.toLongLong(&aIsInteger);
    const qlonglong bInteger = b.toLongLong(&bIsInteger);
    if (aIsInteger && bIsInteger) {
        return aInteger == bInteger;
    }
    bool aIsNumber = false;
    bool bIsNumber = false;
    const double aNumber = a.toDouble(&aIsNumber);
    const double bNumber = b.toDouble(&bIsNumber);
    if (aIsNumber && bIsNumber) {
        const double scale = std::max(std::abs(aNumber), std::abs(bNumber));
        return std::abs(aNumber - bNumber) <= scale * kSameNumberRelativeTolerance;
    }
    // "#ff102030" and "#FF102030" are one colour.
    if (a.startsWith(QLatin1Char('#')) && b.startsWith(QLatin1Char('#'))) {
        return a.compare(b, Qt::CaseInsensitive) == 0;
    }
    return false;
}

// The value a pan with no key of its own shows.
QString inherited(QStringView base, const QVariant& fallback)
{
    const QString value =
        AppSettings::instance().value(defaultKey(base)).toString();
    return value.isEmpty() ? stored(fallback) : value;
}

} // namespace

bool valuesEqual(const QVariant& a, const QVariant& b)
{
    return sameValue(stored(a), stored(b));
}

QString defaultKey(QStringView base)
{
    return base.toString();
}

QString panKey(QStringView base, int panIndex)
{
    return QStringLiteral("%1_%2").arg(base).arg(panIndex);
}

bool hasOverride(QStringView base, int panIndex)
{
    return AppSettings::instance().contains(panKey(base, panIndex));
}

QVariant read(QStringView base, int panIndex, const QVariant& fallback)
{
    auto& s = AppSettings::instance();
    const QString own = panKey(base, panIndex);
    if (s.contains(own)) {
        return s.value(own);
    }
    const QVariant shared = s.value(defaultKey(base));
    if (!shared.toString().isEmpty()) {
        return shared;
    }
    return fallback;
}

void writeSparse(QStringView base, int panIndex, const QVariant& value,
                 const QVariant& fallback)
{
    auto& s = AppSettings::instance();
    const QString own = panKey(base, panIndex);
    const QString text = stored(value);
    if (sameValue(text, inherited(base, fallback))) {
        // The approved master rule: equality resumes following the master.
        if (s.contains(own)) {
            s.remove(own);
        }
        return;
    }
    s.setValue(own, text);
}

void writeDefault(QStringView base, const QVariant& value)
{
    auto& settings = AppSettings::instance();
    settings.setValue(defaultKey(base), stored(value));
    // Every pan takes an explicit master change, including closed pans.
    // Only canonical pan keys belong to this setting; unrelated suffixes
    // and the other settings of each pan remain untouched.
    const QString prefix = base.toString() + QLatin1Char('_');
    const QStringList keys = settings.allKeys();
    for (const QString& key : keys) {
        if (!key.startsWith(prefix)) {
            continue;
        }
        bool isIndex = false;
        const int index = QStringView(key).mid(prefix.size()).toInt(&isIndex);
        if (isIndex && index >= 0 && key == panKey(base, index)) {
            settings.remove(key);
        }
    }
}

void clearPan(int panIndex, const QStringList& bases)
{
    auto& s = AppSettings::instance();
    for (const QString& base : bases) {
        s.remove(panKey(base, panIndex));
    }
}

} // namespace NereusSDR::PanDisplayKeys
