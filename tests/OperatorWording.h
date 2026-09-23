// no-port-check: NereusSDR-original test helper.
//
// OperatorWording: the shared check that words a user reads are written for
// the user (R-R3-21, R-R3-37; operator directive of 2026-09-23). A string
// passes when it is not empty and names none of the internal terms below.
// Log lines and source comments may use them; nothing a user reads may.
#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace NereusSDR::OperatorWording {

/// Internal subsystem, roadmap and wire terms. Each matches at the start of
/// a word, case-insensitively, so "capabilit" also catches "capabilities"
/// and "peer" catches "peers", while "plane" does not catch "airplane".
inline const QStringList& internalTerms()
{
    static const QStringList terms{
        // Subsystem and roadmap names.
        QStringLiteral("DSP"), QStringLiteral("WDSP"), QStringLiteral("txPermitted"),
        QStringLiteral("R3"), QStringLiteral("R4"), QStringLiteral("Role"),
        // Internal terms from the R3 user wording plan's Global Constraints.
        QStringLiteral("grant"), QStringLiteral("budget"), QStringLiteral("allocation"),
        QStringLiteral("capabilit"), QStringLiteral("minor"), QStringLiteral("slot"),
        QStringLiteral("epoch"), QStringLiteral("SSRC"), QStringLiteral("endpoint"),
        QStringLiteral("revision"), QStringLiteral("handshake"), QStringLiteral("snapshot"),
        QStringLiteral("codec"), QStringLiteral("payload"), QStringLiteral("peer"),
        QStringLiteral("protocol"), QStringLiteral("session"), QStringLiteral("telemetry"),
        QStringLiteral("RTP"), QStringLiteral("PCM"), QStringLiteral("WebSocket"),
        QStringLiteral("pong"), QStringLiteral("ledger"), QStringLiteral("reservation"),
        QStringLiteral("descriptor"), QStringLiteral("plane"), QStringLiteral("context"),
        QStringLiteral("matcher"),
    };
    return terms;
}

/// The first internal term `text` names, or an empty string.
inline QString internalTermIn(const QString& text)
{
    for (const QString& term : internalTerms()) {
        const QRegularExpression word(
            QStringLiteral("\\b") + QRegularExpression::escape(term),
            QRegularExpression::CaseInsensitiveOption);
        if (word.match(text).hasMatch()) {
            return term;
        }
    }
    return {};
}

/// True when `text` is written for the user: not empty, no internal term.
inline bool isPlain(const QString& text)
{
    return !text.trimmed().isEmpty() && internalTermIn(text).isEmpty();
}

} // namespace NereusSDR::OperatorWording
