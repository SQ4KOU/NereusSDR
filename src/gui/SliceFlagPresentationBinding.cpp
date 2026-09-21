// =================================================================
// src/gui/SliceFlagPresentationBinding.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-native lifetime boundary for a secondary slice's model-to-flag
// presentation wiring. There is no upstream AetherSDR equivalent for the
// multi-pan flag rehoming lifecycle exercised here (no port check applies).
//
// Modification history (NereusSDR):
//   2026-09-21 -- Extracted by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via OpenAI Codex.
// =================================================================

#include "SliceFlagPresentationBinding.h"

#include "models/SliceModel.h"
#include "widgets/VfoWidget.h"

#include <QObject>

namespace NereusSDR {

QList<QMetaObject::Connection> wireSliceFlagPresentation(
    SliceModel* slice, VfoWidget* flag)
{
    if (!slice || !flag) {
        return {};
    }

    QList<QMetaObject::Connection> connections;
    connections.reserve(14);
    connections.append(QObject::connect(slice, &SliceModel::ritEnabledChanged,
                                         flag, &VfoWidget::setRitEnabled));
    connections.append(QObject::connect(slice, &SliceModel::ritHzChanged,
                                         flag, &VfoWidget::setRitHz));
    connections.append(QObject::connect(slice, &SliceModel::xitEnabledChanged,
                                         flag, &VfoWidget::setXitEnabled));
    connections.append(QObject::connect(slice, &SliceModel::xitHzChanged,
                                         flag, &VfoWidget::setXitHz));
    connections.append(QObject::connect(slice, &SliceModel::snbEnabledChanged,
                                         flag, &VfoWidget::setSnbEnabled));
    connections.append(QObject::connect(slice, &SliceModel::apfEnabledChanged,
                                         flag, &VfoWidget::setApfEnabled));
    connections.append(QObject::connect(slice, &SliceModel::apfTuneHzChanged,
                                         flag, &VfoWidget::setApfTuneHz));
    connections.append(QObject::connect(slice, &SliceModel::mutedChanged,
                                         flag, &VfoWidget::setMuted));
    connections.append(QObject::connect(slice, &SliceModel::audioPanChanged,
                                         flag, &VfoWidget::setAudioPan));
    connections.append(QObject::connect(slice, &SliceModel::ssqlEnabledChanged,
                                         flag, &VfoWidget::setSsqlEnabled));
    connections.append(QObject::connect(slice, &SliceModel::ssqlThreshChanged,
                                         flag, &VfoWidget::setSsqlThresh));
    connections.append(QObject::connect(slice, &SliceModel::agcThresholdChanged,
                                         flag, &VfoWidget::setAgcThreshold));
    connections.append(QObject::connect(slice, &SliceModel::binauralEnabledChanged,
                                         flag, &VfoWidget::setBinauralEnabled));
    connections.append(QObject::connect(slice, &SliceModel::lockedChanged,
                                         flag, &VfoWidget::setLocked));
    return connections;
}

}  // namespace NereusSDR
