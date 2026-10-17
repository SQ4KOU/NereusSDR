// =================================================================
// src/gui/instruments/LongpathContent.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original container-content registration for
// the ported Longpath instruments (workspace preview only). The trace
// sampling values in LongpathContent.cpp come from Longpath and are
// attributed there.
//
// Registers four per-instance widget types with ContainerContentRegistry:
//
//   longpath.frequency  FrequencyInstrument, NumberOnly form
//   longpath.needle     NeedleInstrument
//   longpath.bar        BarInstrument, compact
//   longpath.filter     BandwidthFilterPane
//
// and binds each instance to its entry's effective source context:
// slice frequency and filter from SliceModel, readings from
// MeterPoller::readingUpdated, the passband trace from the pan that
// shows the slice. Nothing here tunes on its own: the only tuning path
// is the operator's own edit in FrequencyInstrument (or a filter drag
// in the pane), which calls the slice's existing setter, the same path
// the VFO flag uses.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - kCompactKey and kLabelKey; declared editor properties.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

namespace NereusSDR {

class ContainerContentRegistry;
class MeterPoller;
class RadioModel;
class SliceModel;
class SpectrumWidget;

namespace LongpathContent {

inline constexpr auto kFrequencyType = "longpath.frequency";
inline constexpr auto kNeedleType    = "longpath.needle";
inline constexpr auto kBarType       = "longpath.bar";
inline constexpr auto kFilterType    = "longpath.filter";

// Entry config keys. "binding" / "secondaryBinding" are MeterBinding ids;
// "direction" ("rx" or "tx") picks the needle default when "binding" is
// absent; "form" ("numberOnly", "bandStrip", "flatArc") is the frequency
// form; "compact" (bool, default true) is the bar's footerless form;
// "label" overrides the filter pane's slice label. Each type declares the
// ones the Container Settings property editor offers
// (ContentDescriptor::properties).
inline constexpr auto kBindingKey   = "binding";
inline constexpr auto kSecondaryKey = "secondaryBinding";
inline constexpr auto kDirectionKey = "direction";
inline constexpr auto kFormKey      = "form";
inline constexpr auto kCompactKey   = "compact";
inline constexpr auto kLabelKey     = "label";

struct Sources {
    MeterPoller* poller = nullptr;
    RadioModel* radio = nullptr;
    // Effective source context to slice; nullptr when no slice is open.
    std::function<SliceModel*(const QJsonObject& context)> sliceForContext;
    // The pan that shows a slice; nullptr leaves the trace empty.
    std::function<SpectrumWidget*(SliceModel*)> spectrumForSlice;
};

// Registers the four types. Call only with the workspace preview on.
void registerAll(ContainerContentRegistry& registry, const Sources& sources);

// The primary binding an entry asks for (config "binding", else the
// type's default: SignalPeak for an RX needle, TxPower for a TX needle,
// TxSwr for a bar).
int primaryBinding(const QString& typeId, const QJsonObject& config);

// The narrow trace accessor: bucket maxima of a pan's rendered dBm
// pixels (centre +- bandwidth/2 across the pixel row) over
// [lowHz, highHz], `points` buckets. Empty when the range is not fully
// on the pan or the inputs are degenerate.
QVector<float> traceFromRendered(const QVector<float>& pixels, double centreHz,
                                 double bandwidthHz, double lowHz, double highHz,
                                 int points);

} // namespace LongpathContent
} // namespace NereusSDR
