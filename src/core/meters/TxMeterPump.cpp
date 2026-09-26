// no-port-check: NereusSDR-original.
// =================================================================
// src/core/meters/TxMeterPump.cpp  (NereusSDR)
// =================================================================
//
// See TxMeterPump.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 39 (D14, R-IOS-13), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/meters/TxMeterPump.h"

#include "core/RadioStatus.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "core/WdspTypes.h"
#include "models/RadioModel.h"

#include <QTimer>

namespace NereusSDR {

TxMeterPump::TxMeterPump(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kIntervalMs);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &TxMeterPump::poll);
}

TxMeterPump::~TxMeterPump() = default;

void TxMeterPump::setModel(RadioModel* model)
{
    m_model = model;
}

void TxMeterPump::setSource(Source source)
{
    m_source = std::move(source);
}

TxMeterReadings TxMeterPump::read(const RadioStatus& status, const TxChannel* tx)
{
    TxMeterReadings readings;
    readings.forwardPowerWatts = status.forwardPowerWatts();
    readings.reflectedPowerWatts = status.reflectedPowerWatts();
    readings.swr = status.swrRatio();
    if (tx != nullptr) {
        // D14, R-R3-49: Thetis's ALC and MIC readings, as the desktop's
        // meters show them (MeterPoller: TxAlc and TxMic). txMeter reads the
        // transmit lane's last reading (R-R3-39).
        const auto readRaw = [tx](TxMeterType meter) { return tx->txMeter(meter); };
        readings.alcDb = thetisTxReading(ThetisTxReading::Alc, readRaw);
        readings.micLevelDb = thetisTxReading(ThetisTxReading::Mic, readRaw);
    }
    return readings;
}

TxMeterReadings TxMeterPump::readNow() const
{
    if (m_source) {
        return m_source();
    }
    if (m_model.isNull()) {
        return {};
    }
    WdspEngine* wdsp = m_model->wdspEngine();
    const TxChannel* tx = wdsp != nullptr ? wdsp->txChannel(WdspEngine::kTxChannelId) : nullptr;
    return read(m_model->radioStatus(), tx);
}

void TxMeterPump::start()
{
    if (!m_timer->isActive()) {
        m_timer->start();
    }
}

void TxMeterPump::stop()
{
    m_timer->stop();
}

bool TxMeterPump::isRunning() const
{
    return m_timer->isActive();
}

void TxMeterPump::poll()
{
    emit readingsTaken(readNow());
}

} // namespace NereusSDR
