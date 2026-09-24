#pragma once
// =================================================================
// src/core/session/LinkVersion.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original.
//
// Link versions both ways (R-IOS-01; the iPhone app spec's D23 and D39,
// section 4.4). Each end supports its own major and the one before it,
// and the two ends agree the highest major both support. Only ends two or
// more majors apart have nothing in common; the station then refuses the
// connection with a reason that names both sides' versions in plain
// words.
//
// The minor stays where it is (kSessionProtocolMinor, 11): features added
// since then carry their own capability versions, and a feature the
// station must know about before capabilities are sent is declared in the
// hello's `features` object (SessionMessage::features).
//
// A second major does not exist yet, so the lists are injectable: the
// StationServer and StationClient constructors take one (tests pass
// theirs), and a debug build of nereusd takes --test-link-majors so the
// app's version screens can be tried against a real station.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4 (R-IOS-01): link
//                                    majors, their agreement and the
//                                    refusal wording. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QList>
#include <QString>
#include <QtGlobal>

#include <array>
#include <optional>

#include "core/session/SessionMessages.h"

namespace NereusSDR {

/// The majors this build of the station and of the desktop client
/// supports, oldest first. D23: its own major and the one before it; there
/// is no major before 1.
inline constexpr std::array<quint16, 1> kSupportedSessionMajors{1};

static_assert(kSupportedSessionMajors.back() == kSessionProtocolMajor,
              "the newest supported major is the one this build speaks");

namespace LinkVersion {

/// kSupportedSessionMajors as a list, for the hello and the constructors.
QList<quint16> supportedMajors();

/// The highest major in both lists, or nullopt when they share none.
std::optional<quint16> agreeMajor(QList<quint16> ours, QList<quint16> theirs);

/// Why the station refuses an app it shares no major with, in plain
/// words: each side's newest version, and which side to update. For
/// example "This station runs link version 1 and this app runs version 3.
/// Update the station."
QString refusalText(QList<quint16> station, QList<quint16> client);

/// Parses --test-link-majors: whole numbers from 1 to 65535 separated by
/// commas ("1,2"). Returns the list sorted oldest first without repeats,
/// or an empty list with `error` set.
QList<quint16> parseMajorList(const QString& text, QString* error);

/// True in a debug build, where nereusd accepts --test-link-majors.
bool testLinkMajorsAllowed();

/// What nereusd advertises: `requested` when --test-link-majors was given
/// and allowed, supportedMajors() when it was not given. Empty with a
/// plain `error` when it was given to a release build or does not parse.
QList<quint16> resolveTestLinkMajors(bool optionSet, const QString& value, bool allowed,
                                     QString* error);

} // namespace LinkVersion
} // namespace NereusSDR
