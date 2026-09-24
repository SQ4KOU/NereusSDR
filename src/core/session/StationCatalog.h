#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/StationCatalog.h  (NereusSDR)
// =================================================================
//
// The mirrored `catalog` object (iPhone app plan Task 19, R-IOS-06,
// R-IOS-27, D40; spec section 4.10): the values the Core owns and an app
// draws its controls from, so a phone never carries a table of its own.
//
//   json      one JSON document with exactly these top-level keys:
//             modes, filterPresets, tuneSteps, agc, meters, board,
//             bandPlans, palettes, sliceColours, tools, radioItems, audio.
//             The link document's Catalogue section gives its full shape.
//   revision  moves by one each time `json` changes (serial-number
//             arithmetic, as the devices object's).
//
// Everything in it is read where the desktop reads it: the modes and the
// Core's filter presets (FilterPresetStore), the tune-step list
// (SliceModel's), the AGC and gauge ranges (ControlRanges.h), the board
// (BoardCapabilities, SkuUiProfile, SampleRateCatalog, paMaxWattsFor), the
// band plans (BandPlanManager), the waterfall palettes
// (core/spectrum/WaterfallPalettes) and the slice colours. It is the same
// for every device connected to the Core; nothing in it is per device.
//
// Built in two halves so it can be tested without a radio: inputsFrom()
// reads a RadioModel into Inputs, and build() turns Inputs into the JSON.
// setInputs() rebuilds at once; refresh() and scheduleRefresh() (coalesced
// to one rebuild per event-loop turn) read the bound model again. A rebuild
// that changes nothing leaves the revision alone.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h"
#include "models/BandPlan.h"
#include "models/FilterPresetStore.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <utility>

class QTimer;

namespace NereusSDR {

class RadioModel;

class StationCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString json READ json NOTIFY catalogChanged)
    Q_PROPERTY(quint32 revision READ revision NOTIFY catalogChanged)

public:
    /// The largest catalogue the Core sends, in bytes of `json` (UTF-8).
    static constexpr int kMaxJsonBytes = 256 * 1024;

    /// One band plan as the catalogue lists it.
    struct BandPlan {
        QString id;    // the plan file's name without .json, e.g. "arrl-us"
        QString name;  // its display name, e.g. "ARRL (US)"
        QVector<BandSegment> segments;
    };

    /// What the catalogue is built from. inputsFrom() fills it from a
    /// RadioModel; a test fills it by hand.
    struct Inputs {
        HPSDRModel model = HPSDRModel::FIRST;
        BoardCapabilities board{};
        ProtocolVersion protocol = ProtocolVersion::Protocol1;
        /// Each of the 14 modes (DSPMode 0 to 13) with its presets, in
        /// DSPMode order.
        QList<std::pair<DSPMode, QList<FilterPreset>>> filterPresets;
        QList<int> tuneStepsHz;
        QList<BandPlan> bandPlans;
        /// The band plan marked as the default.
        QString defaultBandPlanName;
    };

    explicit StationCatalog(QObject* parent = nullptr);
    ~StationCatalog() override;

    QString json() const { return m_json; }
    quint32 revision() const { return m_revision; }

    /// The catalogue for `inputs`, as the document `json` carries.
    static QJsonObject build(const Inputs& inputs);
    /// `model`'s Inputs: its board, the protocol its radio runs, the Core's
    /// filter presets, the tune-step list and the band plans.
    static Inputs inputsFrom(const RadioModel& model);

    /// Reads `model` now and again whenever a filter preset, the band plan
    /// data or the radio changes. Not owned; may be null.
    void bind(RadioModel* model);

    /// Rebuilds from `inputs` now; a change moves the revision once and
    /// notifies.
    void setInputs(const Inputs& inputs);
    /// Rebuilds from the bound model now.
    void refresh();
    /// Rebuilds from the bound model once the event loop turns, however
    /// many times this is called before then (one setPreset writes three
    /// settings).
    void scheduleRefresh();

signals:
    void catalogChanged();

private:
    QPointer<RadioModel> m_model;
    QTimer* m_refreshTimer = nullptr;
    QString m_json;
    quint32 m_revision = 0;
};

} // namespace NereusSDR
