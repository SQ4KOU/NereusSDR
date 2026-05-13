// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - SpotModel: TCI-keyed spot sink implementation.
//
// Ported from AetherSDR src/models/SpotModel.cpp [@0cd4559].
// AetherSDR is (C) its contributors and is licensed GPL-3.0-or-later
// (see https://github.com/ten9876/AetherSDR/blob/main/LICENSE).
//
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task D1. Initial port.
//                                    AetherSDR's "AetherSDR" namespace
//                                    becomes "NereusSDR". applySpotStatus
//                                    isNew detection (first call for a
//                                    given index sets addedMs and emits
//                                    spotAdded; subsequent calls emit
//                                    spotUpdated) preserved verbatim.
//                                    All 12 TCI keys dispatched
//                                    verbatim. 0x7F (DEL) -> space
//                                    decoding on callsign and comment
//                                    preserved verbatim. timestamp
//                                    key parsed as seconds-since-epoch
//                                    (UTC) via QDateTime::fromSecsSinceEpoch
//                                    only when the toLongLong conversion
//                                    succeeds. removeSpot, clear, and
//                                    refresh preserved verbatim. AI
//                                    tooling: Anthropic Claude Code.

#include "SpotModel.h"
#include <QDateTime>

namespace NereusSDR {

// From AetherSDR src/models/SpotModel.cpp:6-52 [@0cd4559]
void SpotModel::applySpotStatus(int index, const QMap<QString, QString>& kvs)
{
    bool isNew = !m_spots.contains(index);
    auto& spot = m_spots[index];
    spot.index = index;
    if (isNew)
        spot.addedMs = QDateTime::currentMSecsSinceEpoch();

    for (auto it = kvs.constBegin(); it != kvs.constEnd(); ++it) {
        const QString& key = it.key();
        const QString& val = it.value();

        if (key == "callsign")
            spot.callsign = QString(val).replace(QChar(0x7f), ' ');
        else if (key == "rx_freq")
            spot.rxFreqMhz = val.toDouble();
        else if (key == "tx_freq")
            spot.txFreqMhz = val.toDouble();
        else if (key == "mode")
            spot.mode = val;
        else if (key == "color")
            spot.color = val;
        else if (key == "background_color")
            spot.backgroundColor = val;
        else if (key == "source")
            spot.source = val;
        else if (key == "spotter_callsign")
            spot.spotterCallsign = val;
        else if (key == "comment")
            spot.comment = QString(val).replace(QChar(0x7f), ' ');
        else if (key == "timestamp") {
            bool ok;
            qint64 ts = val.toLongLong(&ok);
            if (ok)
                spot.timestamp = QDateTime::fromSecsSinceEpoch(ts, Qt::UTC);
        }
        else if (key == "lifetime_seconds")
            spot.lifetimeSeconds = val.toInt();
        else if (key == "priority")
            spot.priority = val.toInt();
    }

    if (isNew)
        emit spotAdded(spot);
    else
        emit spotUpdated(spot);
}

// From AetherSDR src/models/SpotModel.cpp:54-58 [@0cd4559]
void SpotModel::removeSpot(int index)
{
    if (m_spots.remove(index))
        emit spotRemoved(index);
}

// From AetherSDR src/models/SpotModel.cpp:60-64 [@0cd4559]
void SpotModel::clear()
{
    m_spots.clear();
    emit spotsCleared();
}

// From AetherSDR src/models/SpotModel.cpp:66-69 [@0cd4559]
void SpotModel::refresh()
{
    emit spotsRefreshed();
}

} // namespace NereusSDR
