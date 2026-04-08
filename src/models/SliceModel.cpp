#include "SliceModel.h"

namespace NereusSDR {

SliceModel::SliceModel(QObject* parent)
    : QObject(parent)
{
}

SliceModel::~SliceModel() = default;

void SliceModel::setFrequency(double freq)
{
    if (!qFuzzyCompare(m_frequency, freq)) {
        m_frequency = freq;
        emit frequencyChanged(freq);
    }
}

void SliceModel::setMode(const QString& mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        emit modeChanged(mode);
    }
}

void SliceModel::setFilterLow(int low)
{
    if (m_filterLow != low) {
        m_filterLow = low;
        emit filterChanged();
    }
}

void SliceModel::setFilterHigh(int high)
{
    if (m_filterHigh != high) {
        m_filterHigh = high;
        emit filterChanged();
    }
}

void SliceModel::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        emit activeChanged(active);
    }
}

void SliceModel::setTxSlice(bool tx)
{
    if (m_txSlice != tx) {
        m_txSlice = tx;
        emit txSliceChanged(tx);
    }
}

} // namespace NereusSDR
