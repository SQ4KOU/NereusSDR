// =================================================================
// src/core/audio/CaptureStandby.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See CaptureStandby.h.
//
// Modification history (NereusSDR):
//   2026-10-16: the persistent capture helper, held for the whole
//               application.  J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
// =================================================================

#include "core/audio/CaptureStandby.h"

#include "core/LogCategories.h"
#include "core/audio/AudioDelayProbe.h"

#include <QMetaObject>
#include <QTimer>

namespace NereusSDR {

CaptureStandby::CaptureStandby(CaptureSupervisor::Options options, int warmWaitMs,
                               QObject* parent)
    : QObject(parent)
    , m_supervisor(std::make_unique<CaptureSupervisor>(std::move(options)))
    , m_bound(new QTimer(this))
    , m_startNs(audioProbeNowNs())
{
    m_bound->setSingleShot(true);
    m_bound->setTimerType(Qt::PreciseTimer);
    connect(m_bound, &QTimer::timeout, this, [this, warmWaitMs]() {
        release(QStringLiteral("at the %1 ms bound; the helper had not warmed").arg(warmWaitMs));
    });
    connect(m_supervisor.get(), &CaptureSupervisor::helperWarmed, this, [this](qint64 pid) {
        m_warmed = true;
        release(QStringLiteral("once helper %1 had warmed").arg(pid));
    });
    if (!m_supervisor->helperAvailable()) {
        qCWarning(lcAudio) << "capture: no helper program to stand by; nothing waits for one";
        m_released = true;
        return;
    }
    m_standby = m_supervisor->acquire(CaptureSupervisor::Demand::Standby);
    m_bound->start(warmWaitMs);
}

CaptureStandby::~CaptureStandby()
{
    m_bound->stop();
    m_held.clear();
    m_standby.release();
    m_supervisor->shutdown();
}

void CaptureStandby::whenWarm(QObject* context, std::function<void()> work)
{
    if (context == nullptr || !work) {
        return;
    }
    if (m_released) {
        QMetaObject::invokeMethod(context, std::move(work), Qt::QueuedConnection);
        return;
    }
    if (m_held.empty()) {
        m_heldSinceNs = audioProbeNowNs();
    }
    m_held.emplace_back(QPointer<QObject>(context), std::move(work));
}

void CaptureStandby::release(const QString& why)
{
    if (m_released) {
        return;
    }
    m_released = true;
    m_bound->stop();
    const std::int64_t nowNs = audioProbeNowNs();
    const auto ms = [](std::int64_t ns) { return QString::number(double(ns) / 1e6, 'f', 1); };
    // The hold is what the start of the first window waited (whenWarm()).
    const QString hold = m_held.empty()
                             ? QStringLiteral("no work was waiting")
                             : QStringLiteral("work held for it waited %1 ms")
                                   .arg(ms(nowNs - m_heldSinceNs));
    qCInfo(lcAudio).noquote()
        << QStringLiteral("capture: the application's audio starts %1 ms after the standing "
                          "helper was asked for, %2; %3")
               .arg(ms(nowNs - m_startNs), why, hold);
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> held;
    held.swap(m_held);
    for (auto& [context, work] : held) {
        if (!context.isNull()) {
            QMetaObject::invokeMethod(context.data(), std::move(work), Qt::QueuedConnection);
        }
    }
}

} // namespace NereusSDR
