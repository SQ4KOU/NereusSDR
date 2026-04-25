// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR — bandplan value types
//
// Ported from AetherSDR src/models/BandPlanManager.h [@0cd4559].
// AetherSDR is © its contributors and is licensed GPL-3.0-or-later.
//
// Modification history (NereusSDR):
//   2026-04-25  J.J. Boyd <jj@skyrunner.net>  Initial port for Phase 3G RX Epic sub-epic D.
//                                              Extracted Segment/Spot value types out of
//                                              BandPlanManager so consumers can include
//                                              them without QObject. AI assistance:
//                                              Anthropic Claude (claude-opus-4-7).

#pragma once

#include <QColor>
#include <QString>

namespace NereusSDR {

struct BandSegment {
    double  lowMhz{0.0};
    double  highMhz{0.0};
    QString label;
    QString license;  // "E", "E,G", "E,G,T", "T", "" = beacon/no TX
    QColor  color;
};

struct BandSpot {
    double  freqMhz{0.0};
    QString label;
};

}  // namespace NereusSDR
