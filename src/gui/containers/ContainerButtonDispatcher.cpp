// no-port-check: NereusSDR-original. Maps each container button to the
// NereusSDR feature it fronts, on the container's own slice.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/containers/ContainerButtonDispatcher.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. See the header.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  Created (R-R3-49, R-R3-21). AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49 (parity Task 2): MON follows
//                                    the transmit settings gate in a
//                                    remote window (the Core's monEnabled).
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/containers/ContainerButtonDispatcher.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/MoxController.h"
#include "core/TwoToneController.h"
#include "core/session/PureSignalSessionFacade.h"
#include "gui/SpectrumWidget.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/meters/BandButtonItem.h"
#include "gui/meters/ButtonBoxItem.h"
#include "models/Band.h"
#include "models/NotchModel.h"
#include "models/PureSignalSettings.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <utility>

namespace NereusSDR {

namespace {

using Id = ContainerButtonDispatcher::Id;

// The function buttons connected to a NereusSDR feature. Every other core
// button is hidden through UnbuiltFeatures (OtherButtonItem).
constexpr Id kConnected[] = {
    Id::Power, Id::Mon, Id::Tun, Id::Mox, Id::TwoTon, Id::PsA,
    Id::Anf, Id::Snb, Id::Mnf, Id::PeakHold, Id::Ctun,
    Id::Vac1, Id::Vac2, Id::Mute, Id::Bin,
};

int vaxChannelOf(Id id)
{
    return id == Id::Vac1 ? 1 : 2;
}

QString vaxEnabledKey(int channel)
{
    // The key Setup > Audio > VAX's channel "On" switch saves.
    return QStringLiteral("audio/Vax%1/Enabled").arg(channel);
}

} // namespace

ContainerButtonDispatcher::ContainerButtonDispatcher(RadioModel* model, Hooks hooks)
    : m_model(model)
    , m_hooks(std::move(hooks))
{
}

SliceModel* ContainerButtonDispatcher::sliceFor(int rxSource) const
{
    if (!m_model) { return nullptr; }
    return m_model->sliceById(ContainerWidget::sliceIdForRxSource(rxSource));
}

QString ContainerButtonDispatcher::noSliceReason(int rxSource)
{
    return QStringLiteral("%1 is not open. Open it, or choose another slice in this "
                          "container's settings.")
        .arg(ContainerWidget::sliceNameForRxSource(rxSource));
}

QString ContainerButtonDispatcher::noRadioTransmitReason()
{
    return QStringLiteral("Connect a radio to transmit.");
}

QString ContainerButtonDispatcher::noPowerTargetReason()
{
    return QStringLiteral("There is no radio to reconnect to. Choose one under "
                          "Radio > Manage Radios.");
}

bool ContainerButtonDispatcher::transmitBlockedRemotely() const
{
    if (!m_model || m_model->ownsLocalDsp()) { return false; }
    return !(m_hooks.transmitPermitted && m_hooks.transmitPermitted());
}

SpectrumWidget* ContainerButtonDispatcher::spectrumOf(int rxSource) const
{
    SliceModel* slice = sliceFor(rxSource);
    if (!slice || !m_hooks.spectrumFor) { return nullptr; }
    return m_hooks.spectrumFor(slice);
}

ContainerButtonDispatcher::State
ContainerButtonDispatcher::stateOf(Id id, int rxSource) const
{
    State st;
    if (!m_model) {
        st.available = false;
        st.reason = noRadioTransmitReason();
        return st;
    }
    const auto unavailable = [&st](const QString& reason) {
        st.available = false;
        st.reason = reason;
    };
    const QString remoteReason = m_hooks.remoteTransmitReason.isEmpty()
        ? QStringLiteral("Remote transmit controls are not available from this Core yet.")
        : m_hooks.remoteTransmitReason;

    switch (id) {
    case Id::Power:
        st.on = m_hooks.powerOn && m_hooks.powerOn();
        if (!st.on && !(m_hooks.powerCanToggle && m_hooks.powerCanToggle())) {
            unavailable(noPowerTargetReason());
        }
        break;
    case Id::Mon:
        st.on = m_model->transmitModel().monEnabled();
        // R-R3-49 (parity Task 2): a transmit setting, not a key. A remote
        // window toggles the Core's monEnabled while the Core takes it.
        if (!m_model->ownsLocalDsp()
            && !(m_hooks.transmitSettingsPermitted && m_hooks.transmitSettingsPermitted())) {
            const QString reason = m_hooks.transmitSettingsReason
                ? m_hooks.transmitSettingsReason() : QString();
            unavailable(reason.isEmpty() ? remoteReason : reason);
        }
        break;
    case Id::Tun:
    case Id::Mox:
    case Id::TwoTon:
        if (id == Id::Tun) {
            st.on = m_model->isTune();
        } else if (id == Id::Mox) {
            st.on = m_model->moxController() && m_model->moxController()->isMox();
        } else {
            st.on = m_model->twoToneController() && m_model->twoToneController()->isActive();
        }
        if (transmitBlockedRemotely()) {
            unavailable(remoteReason);
        } else if (!m_model->isConnected()) {
            unavailable(noRadioTransmitReason());
        } else if ((id == Id::Mox && !m_model->moxController())
                   || (id == Id::TwoTon && !m_model->twoToneController())) {
            unavailable(noRadioTransmitReason());
        }
        break;
    case Id::PsA: {
        PureSignalSessionFacade* ps = m_model->pureSignalFacade();
        st.on = ps && ps->settings() && ps->settings()->autoCalEnabled();
        if (transmitBlockedRemotely()) {
            unavailable(remoteReason);
        } else if (!ps || !ps->available() || !ps->canActuate()) {
            unavailable(QStringLiteral("PureSignal needs a connected radio that supports it."));
        }
        break;
    }
    case Id::Anf:
    case Id::Snb:
    case Id::Mute:
    case Id::Bin: {
        SliceModel* slice = sliceFor(rxSource);
        if (!slice) {
            unavailable(noSliceReason(rxSource));
            break;
        }
        st.on = id == Id::Anf    ? slice->anfEnabled()
              : id == Id::Snb    ? slice->snbEnabled()
              : id == Id::Mute   ? slice->muted()
                                 : slice->binauralEnabled();
        break;
    }
    case Id::Mnf:
        st.on = m_model->notchModel() && m_model->notchModel()->globalEnabled();
        if (!m_model->notchModel()) {
            unavailable(QStringLiteral("Notches are not ready yet."));
        }
        break;
    case Id::PeakHold:
    case Id::Ctun: {
        if (!sliceFor(rxSource)) {
            unavailable(noSliceReason(rxSource));
            break;
        }
        SpectrumWidget* sw = spectrumOf(rxSource);
        if (!sw) {
            unavailable(QStringLiteral("No panadapter shows this slice."));
            break;
        }
        if (id == Id::PeakHold) {
            st.on = sw->peakHoldEnabled();
        } else {
            st.on = sw->ctunEnabled();
            if (!sw->ctunAvailable()) {
                unavailable(QStringLiteral("C-Tune needs a connected Core that supports it."));
            }
        }
        break;
    }
    case Id::Vac1:
    case Id::Vac2:
        if (!m_hooks.vaxDevices) {
            unavailable(QStringLiteral("This computer's sound devices are not ready."));
            break;
        }
        st.on = m_hooks.vaxDevices->isVaxBusOpen(vaxChannelOf(id));
        break;
    default:
        // Hidden until its feature is built (OtherButtonItem).
        unavailable(QStringLiteral("This button does nothing yet."));
        break;
    }
    return st;
}

void ContainerButtonDispatcher::apply(OtherButtonItem* item, int rxSource) const
{
    if (!item) { return; }
    for (Id id : kConnected) {
        const State st = stateOf(id, rxSource);
        item->setButtonState(id, st.on);
        item->setButtonAvailable(id, st.available, st.reason);
    }
}

QString ContainerButtonDispatcher::click(Id id, int rxSource)
{
    const State st = stateOf(id, rxSource);
    if (!st.available) { return st.reason; }
    const bool turnOn = !st.on;

    switch (id) {
    case Id::Power:
        if (m_hooks.togglePower) { m_hooks.togglePower(); }
        break;
    case Id::Mon:
        // TxApplet's MON button (TransmitModel::setMonEnabled).
        m_model->transmitModel().setMonEnabled(turnOn);
        break;
    case Id::Tun:
        // TxApplet's TUNE button (RadioModel::setTune).
        m_model->setTune(turnOn);
        break;
    case Id::Mox:
        // TxApplet's MOX button (MoxController::setMox).
        m_model->moxController()->setMox(turnOn);
        break;
    case Id::TwoTon:
        // TxApplet's 2-TONE button (TwoToneController::setActive).
        m_model->twoToneController()->setActive(turnOn);
        break;
    case Id::PsA:
        // TxApplet's PS-A button: automatic calibration on, or Off/reset.
        m_model->pureSignalFacade()->requestAction(
            turnOn ? Ps3Action::StartAutomatic : Ps3Action::OffReset);
        break;
    case Id::Anf:
        sliceFor(rxSource)->setAnfEnabled(turnOn);
        break;
    case Id::Snb:
        sliceFor(rxSource)->setSnbEnabled(turnOn);
        break;
    case Id::Mute:
        sliceFor(rxSource)->setMuted(turnOn);
        break;
    case Id::Bin:
        sliceFor(rxSource)->setBinauralEnabled(turnOn);
        break;
    case Id::Mnf:
        m_model->notchModel()->setGlobalEnabled(turnOn);
        break;
    case Id::PeakHold:
        spectrumOf(rxSource)->setPeakHoldEnabled(turnOn);
        break;
    case Id::Ctun:
        // The pan overlay's C-Tune switch; a remote window routes it to
        // the Core (RemoteMediaController follows ctunEnabledChanged).
        spectrumOf(rxSource)->setCtunEnabled(turnOn);
        break;
    case Id::Vac1:
    case Id::Vac2: {
        // Setup > Audio > VAX's channel "On" switch: saved, then the
        // channel opened or closed on this computer.
        const int channel = vaxChannelOf(id);
        AppSettings::instance().setValue(vaxEnabledKey(channel),
            turnOn ? QStringLiteral("True") : QStringLiteral("False"));
        AppSettings::instance().save();
        m_hooks.vaxDevices->setVaxEnabled(channel, turnOn);
        break;
    }
    default:
        return st.reason;
    }
    return QString();
}

void ContainerButtonDispatcher::applySliceAvailability(ButtonBoxItem* box, int rxSource) const
{
    if (!box) { return; }
    if (sliceFor(rxSource)) {
        box->setAllButtonsAvailable(true);
    } else {
        box->setAllButtonsAvailable(false, noSliceReason(rxSource));
    }
}

void ContainerButtonDispatcher::applyBand(BandButtonItem* item, int rxSource) const
{
    if (!item) { return; }
    applySliceAvailability(item, rxSource);
    SliceModel* slice = sliceFor(rxSource);
    item->setActiveBand(slice ? uiIndexFromBand(bandFromFrequency(slice->frequency())) : -1);
}

QString ContainerButtonDispatcher::clickBand(int bandUiIndex, int rxSource)
{
    SliceModel* slice = sliceFor(rxSource);
    if (!slice) { return noSliceReason(rxSource); }
    m_model->onBandButtonClicked(slice, bandFromUiIndex(bandUiIndex));
    return QString();
}

} // namespace NereusSDR
