// =================================================================
// src/gui/PanModels.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See PanModels.h.
//
// Modification history (NereusSDR):
//   2026-10-10 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - The model's dB Step reaches the pan's widget. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "PanModels.h"

#include "MainWindow.h"
#include "SpectrumWidget.h"
#include "core/AppSettings.h"
#include "models/Band.h"
#include "models/PanadapterModel.h"

#include <cmath>
#include <utility>

namespace NereusSDR {

namespace {

// The range a pan showed before it had a band store: the widget's own
// default (SpectrumWidget's m_refLevel -48 and m_dynamicRange 68, the pair
// DisplaySettingsModel held as kDefaultGridMax / kDefaultGridMin).
constexpr int kLegacyDefaultDbMax = -48;
constexpr int kLegacyDefaultDbMin = -116;

// The pair for a band with nothing stored. An install from before the band
// store kept the pan's range in the bare DisplayGridMax / DisplayGridMin
// keys: those are read here and nowhere else, never rewritten and never
// deleted, so the operator's old range is what every band shows until it
// gets a range of its own. The band store holds whole dB.
std::pair<int, int> unsetBandRange()
{
    const auto& s = AppSettings::instance();
    const auto legacy = [&s](const QString& key, int fallback) {
        if (!s.contains(key)) {
            return fallback;
        }
        bool ok = false;
        const float v = s.value(key).toString().toFloat(&ok);
        return ok && std::isfinite(v) ? qRound(v) : fallback;
    };
    const int dbMax = legacy(QStringLiteral("DisplayGridMax"), kLegacyDefaultDbMax);
    const int dbMin = legacy(QStringLiteral("DisplayGridMin"), kLegacyDefaultDbMin);
    if (dbMax <= dbMin) {
        return { kLegacyDefaultDbMax, kLegacyDefaultDbMin };
    }
    return { dbMax, dbMin };
}

} // namespace

PanModels::PanModels(QObject* parent)
    : QObject(parent)
{
}

PanModels::~PanModels() = default;

PanadapterModel* PanModels::ensure(const QString& panId, SpectrumWidget* widget)
{
    if (panId.isEmpty() || widget == nullptr) {
        return nullptr;
    }
    if (const auto it = m_entries.find(panId); it != m_entries.end()) {
        if (it->second.widget == widget) {
            return it->second.model.get();
        }
        // The pan was rebuilt under its old id: the old model is wired to a
        // widget that is gone or going.
        m_entries.erase(it);
    }

    Entry entry;
    entry.model = std::make_unique<PanadapterModel>(this);
    entry.widget = widget;
    PanadapterModel* model = entry.model.get();
    m_entries.emplace(panId, std::move(entry));

    // A band with nothing stored shows what the pan showed before this
    // store was its range; no key is written for it until it is edited.
    const auto [unsetDbMax, unsetDbMin] = unsetBandRange();
    model->setUnsetBandGridDefault(unsetDbMax, unsetDbMin);

    // Every connection below has the model as its sender or its context,
    // so retire() (which deletes the model) removes them all.

    // The band's stored range is the pan's receive range. A band change
    // moves the ceiling and the floor one after the other; the pair between
    // the two is not a range anyone chose, and when it is upside down it is
    // not applied.
    const auto applyRange = [model, widget]() {
        const int floor = model->dBmFloor();
        const int ceiling = model->dBmCeiling();
        if (ceiling <= floor) {
            return;
        }
        widget->setReceiveDbmRange(static_cast<float>(floor), static_cast<float>(ceiling));
    };

    connect(model, &PanadapterModel::levelChanged, widget, applyRange);
    connect(model, &PanadapterModel::levelChanged, this,
            [this, panId]() { emit bandGridChanged(panId); });

    connect(model, &PanadapterModel::bandChanged, widget,
            [widget, applyRange](NereusSDR::Band) {
        // Arriving on a band shows that band's stored range, also when it
        // equals the last band's (the live range may have been moved since
        // by a drag or by the noise-floor grid follow).
        applyRange();
        // The noise floor's fast attack on a band change.
        // From Thetis display.cs:825-831 [v2.10.3.15] (OnBandChangeHandler):
        //   if (rx == 1)
        //   {
        //       FastAttackNoiseFloorRX1 = true;
        // NereusSDR, based on Thetis: the fast attack runs on the pan whose
        // band changed (Thetis has one display per receiver).
        widget->setNoiseFloorFastAttack(true);
    });
    connect(model, &PanadapterModel::bandChanged, this,
            [this, panId](NereusSDR::Band) { emit bandGridChanged(panId); });

    // The pan's band is the band of the pan's receive centre.
    model->setCenterFrequency(widget->centerFrequency());
    connect(widget, &SpectrumWidget::centerFrequencyChanged, model,
            &PanadapterModel::setCenterFrequency);

    applyRange();

    // Approved rebuild supersedes the historical wiring below: each
    // native widget loads and persists its own step, independently of the
    // legacy model's shared step API. No model step push is made here.
    // Setup's dB Step is the spacing of the pan's horizontal dB grid lines:
    // the stored step now, and every change after. The model shares a
    // change with every other pan's model, so each pan's widget gets it
    // from its own model.
    // The per-band 3D Floor: recalled now and on every band change, saved
    // under the pan's current band on an edit.
    MainWindow::wireDss3DFloorRecallForTest(model, widget);
    // The store is one for every pan: an edit on another pan showing this
    // band reaches this pan's widget too. The edit's own pan already holds
    // the value, and the widget's echo back into the store is the same
    // value, which the store ignores.
    connect(model, &PanadapterModel::dss3DFloorDepthStored, widget,
            [model, widget](NereusSDR::Band, int) {
        const int stored = model->dss3DFloorDepthForBand(model->band());
        if (widget->dssFloorDepth() != stored) {
            widget->setDssFloorDepth(stored);
        }
    });

    return model;
}

PanadapterModel* PanModels::model(const QString& panId) const
{
    const auto it = m_entries.find(panId);
    return it == m_entries.end() ? nullptr : it->second.model.get();
}

void PanModels::retire(const QString& panId)
{
    m_entries.erase(panId);
}

QList<PanadapterModel*> PanModels::all() const
{
    QList<PanadapterModel*> models;
    for (const auto& [id, entry] : m_entries) {
        models.append(entry.model.get());
    }
    return models;
}

void PanModels::applyStationGridSetting(const QString& key)
{
    for (PanadapterModel* model : all()) {
        model->applyStationGridSetting(key);
    }
}

} // namespace NereusSDR
