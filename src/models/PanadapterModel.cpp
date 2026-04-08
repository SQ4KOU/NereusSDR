#include "PanadapterModel.h"

namespace NereusSDR {

PanadapterModel::PanadapterModel(QObject* parent)
    : QObject(parent)
{
}

PanadapterModel::~PanadapterModel() = default;

void PanadapterModel::setCenterFrequency(double freq)
{
    if (!qFuzzyCompare(m_centerFrequency, freq)) {
        m_centerFrequency = freq;
        emit centerFrequencyChanged(freq);
    }
}

void PanadapterModel::setBandwidth(double bw)
{
    if (!qFuzzyCompare(m_bandwidth, bw)) {
        m_bandwidth = bw;
        emit bandwidthChanged(bw);
    }
}

void PanadapterModel::setdBmFloor(int floor)
{
    if (m_dBmFloor != floor) {
        m_dBmFloor = floor;
        emit levelChanged();
    }
}

void PanadapterModel::setdBmCeiling(int ceiling)
{
    if (m_dBmCeiling != ceiling) {
        m_dBmCeiling = ceiling;
        emit levelChanged();
    }
}

void PanadapterModel::setFftSize(int size)
{
    if (m_fftSize != size) {
        m_fftSize = size;
        emit fftSizeChanged(size);
    }
}

void PanadapterModel::setAverageCount(int count)
{
    m_averageCount = count;
}

} // namespace NereusSDR
