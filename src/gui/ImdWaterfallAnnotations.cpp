#include "gui/ImdWaterfallAnnotations.h"

namespace NereusSDR {

void ImdWaterfallAnnotations::record(quint64 rowSequence, quint64 contextRevision,
                                     bool correctionsApplied)
{
    if (!m_events.isEmpty() && rowSequence <= m_events.last().rowSequence) { return; }
    m_events.append({rowSequence, contextRevision, correctionsApplied});
}

void ImdWaterfallAnnotations::expireBefore(quint64 oldestRetainedSequence)
{
    qsizetype expired = 0;
    while (expired < m_events.size()
           && m_events[expired].rowSequence < oldestRetainedSequence) {
        ++expired;
    }
    if (expired > 0) { m_events.erase(m_events.begin(), m_events.begin() + expired); }
}

void ImdWaterfallAnnotations::clear()
{
    m_events.clear();
}

std::optional<qreal> ImdWaterfallAnnotations::visibleRowY(int ageRows, int offsetRows,
                                                            int visibleRows, qreal glideRows)
{
    if (ageRows < 0 || offsetRows < 0 || visibleRows <= 0) { return std::nullopt; }
    const qreal y = ageRows - offsetRows - glideRows + 0.5;
    if (y < 0.0 || y >= visibleRows) { return std::nullopt; }
    return y;
}

} // namespace NereusSDR
