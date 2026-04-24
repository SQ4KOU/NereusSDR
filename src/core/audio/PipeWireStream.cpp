// =================================================================
// src/core/audio/PipeWireStream.cpp  (NereusSDR)
//   Copyright (C) 2026 J.J. Boyd (KG4VCF) — GPLv2-or-later.
//   2026-04-23 — created. AI-assisted via Claude Code.
// =================================================================
#ifdef NEREUS_HAVE_PIPEWIRE
#include "core/audio/PipeWireStream.h"

#include <QLoggingCategory>
#include <pipewire/keys.h>

Q_DECLARE_LOGGING_CATEGORY(lcPw)

namespace NereusSDR {

pw_properties* configToProperties(const StreamConfig& cfg)
{
    pw_properties* p = pw_properties_new(
        PW_KEY_NODE_NAME,        cfg.nodeName.toUtf8().constData(),
        PW_KEY_NODE_DESCRIPTION, cfg.nodeDescription.toUtf8().constData(),
        PW_KEY_MEDIA_TYPE,       "Audio",
        PW_KEY_MEDIA_CATEGORY,
            cfg.direction == StreamConfig::Output ? "Playback" : "Capture",
        PW_KEY_MEDIA_ROLE,       cfg.mediaRole.toUtf8().constData(),
        PW_KEY_MEDIA_CLASS,      cfg.mediaClass.toUtf8().constData(),
        nullptr);

    if (!cfg.targetNodeName.isEmpty()) {
        pw_properties_set(p, PW_KEY_TARGET_OBJECT,
                          cfg.targetNodeName.toUtf8().constData());
    }

    pw_properties_setf(p, PW_KEY_NODE_RATE, "1/%u", cfg.rate);
    pw_properties_setf(p, PW_KEY_NODE_LATENCY, "%u/%u",
                       cfg.quantum, cfg.rate);

    return p;
}

PipeWireStream::PipeWireStream(PipeWireThreadLoop* loop,
                               StreamConfig cfg, QObject* parent)
    : QObject(parent), m_loop(loop), m_cfg(std::move(cfg)) {}

PipeWireStream::~PipeWireStream() { close(); }

bool PipeWireStream::open()  { return false; }  // Task 9
void PipeWireStream::close() {}                 // Task 9

qint64 PipeWireStream::push(const char*, qint64) { return 0; }    // Task 9
qint64 PipeWireStream::pull(char*, qint64)       { return 0; }    // Task 11

PipeWireStream::Telemetry PipeWireStream::telemetry() const {
    Telemetry t;
    t.streamStateName = m_stateName;
    t.xrunCount = m_xruns.load();
    t.processCbCpuPct = m_cpuPct.load();
    t.measuredLatencyMs = m_latencyMs.load();
    t.deviceLatencyMs = m_deviceLatencyMs.load();
    return t;
}

}  // namespace NereusSDR

#endif  // NEREUS_HAVE_PIPEWIRE
