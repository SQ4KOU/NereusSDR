// =================================================================
// src/core/NoiseFloorEstimator.cpp  (NereusSDR)
// =================================================================
//
// Independently implemented from NoiseFloorEstimator.h interface.
// This .cpp implements NereusSDR's percentile-based noise-floor
// estimator, which deliberately replaces (does not port) Thetis's
// processNoiseFloor algorithm in display.cs:5866. The .h carries the
// Thetis citation for contrast; this implementation is original
// NereusSDR work licensed under GPLv3.
// =================================================================

#include "NoiseFloorEstimator.h"

#include <QtNumeric>

#include <algorithm>
#include <limits>

namespace NereusSDR {

NoiseFloorEstimator::NoiseFloorEstimator(float percentile)
    : m_percentile(percentile)
{
}

void NoiseFloorEstimator::setPercentile(float p)
{
    m_percentile = p;
}

float NoiseFloorEstimator::estimate(const QVector<float>& bins)
{
    if (bins.isEmpty()) {
        return qQNaN();
    }
    m_workBuf = bins;
    const int   n = m_workBuf.size();
    const float p = std::clamp(m_percentile, 0.0f, 1.0f);
    const int   k = static_cast<int>(p * (n - 1));
    std::nth_element(m_workBuf.begin(),
                     m_workBuf.begin() + k,
                     m_workBuf.end());
    return m_workBuf[k];
}

void NoiseFloorEstimator::prime(double initialDb)
{
    // NereusSDR-original — no Thetis equivalent.
    // Seed the estimator with a known noise-floor value so that the next
    // ClarityController poll cycle starts from this floor rather than
    // performing a cold-start ramp from an uninitialised state.
    // Task 2.10 calls this on band change to eliminate the visual jump.
    m_primedValue = static_cast<float>(initialDb);
}

}  // namespace NereusSDR
