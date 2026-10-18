#include "MeterModel.h"

namespace NereusSDR {

MeterModel::MeterModel(QObject* parent)
    : QObject(parent)
{
}

MeterModel::~MeterModel() = default;

void MeterModel::setForwardPower(float value)
{
    if (!qFuzzyCompare(m_forwardPower, value)) {
        m_forwardPower = value;
        emit metersChanged();
    }
}

void MeterModel::setSwr(float value)
{
    if (!qFuzzyCompare(m_swr, value)) {
        m_swr = value;
        emit metersChanged();
    }
}

void MeterModel::setAlc(float value)
{
    if (!qFuzzyCompare(m_alc, value)) {
        m_alc = value;
        emit metersChanged();
    }
}

void MeterModel::setSMeter(float value)
{
    if (!qFuzzyCompare(m_sMeter, value)) {
        m_sMeter = value;
        emit metersChanged();
    }
}

} // namespace NereusSDR
