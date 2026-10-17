#pragma once
// no-port-check: NereusSDR-original GUI-only accepted-row annotation history.
// Modification history (NereusSDR):
//   2026-10-13: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

#include "gui/NereusGuiExport.h"
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace NereusSDR {

struct ImdWaterfallEvent {
    quint64 rowSequence{0};
    quint64 contextRevision{0};
    bool correctionsApplied{false};
};

class NEREUS_GUI_EXPORT ImdWaterfallAnnotations {
public:
    void record(quint64 rowSequence, quint64 contextRevision, bool correctionsApplied);
    void expireBefore(quint64 oldestRetainedSequence);
    void clear();
    const QVector<ImdWaterfallEvent>& events() const { return m_events; }

    // A retained row's centre in waterfall-local pixels. GPU glide is the
    // same fractional row displacement supplied to the existing shader.
    static std::optional<qreal> visibleRowY(int ageRows, int offsetRows,
                                            int visibleRows, qreal glideRows);

private:
    QVector<ImdWaterfallEvent> m_events;
};

} // namespace NereusSDR
