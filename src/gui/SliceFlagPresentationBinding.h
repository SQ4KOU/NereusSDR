#pragma once

// =================================================================
// src/gui/SliceFlagPresentationBinding.h  (NereusSDR)
// =================================================================
//
// NereusSDR-native lifetime boundary for a secondary slice's model-to-flag
// presentation wiring. There is no upstream AetherSDR equivalent for the
// multi-pan flag rehoming lifecycle exercised here (no port check applies).
//
// Modification history (NereusSDR):
//   2026-09-21 -- Extracted by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via OpenAI Codex.
// =================================================================

#include <QList>
#include <QMetaObject>

namespace NereusSDR {

class SliceModel;
class VfoWidget;

/// Wire model state that is rendered directly by a secondary slice flag.
///
/// The returned handles make the connection lifetime contract independently
/// testable. MainWindow does not otherwise need to retain them.
QList<QMetaObject::Connection> wireSliceFlagPresentation(
    SliceModel* slice, VfoWidget* flag);

}  // namespace NereusSDR
