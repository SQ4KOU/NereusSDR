#include "ReceiverManager.h"
#include "LogCategories.h"

namespace NereusSDR {

const ReceiverConfig ReceiverManager::kInvalidConfig{};

ReceiverManager::ReceiverManager(QObject* parent)
    : QObject(parent)
{
}

ReceiverManager::~ReceiverManager() = default;

void ReceiverManager::setMaxReceivers(int max)
{
    m_maxReceivers = qBound(1, max, 7);
}

int ReceiverManager::createReceiver()
{
    if (m_receivers.size() >= m_maxReceivers) {
        qCWarning(lcReceiver) << "Cannot create receiver: at maximum" << m_maxReceivers;
        return -1;
    }

    int index = m_nextWdspChannel++;

    ReceiverConfig config;
    config.receiverIndex = index;
    config.wdspChannel = index;
    config.active = false;

    m_receivers.insert(index, config);
    qCDebug(lcReceiver) << "Created receiver" << index;
    emit receiverCreated(index);
    return index;
}

void ReceiverManager::destroyReceiver(int receiverIndex)
{
    if (!m_receivers.contains(receiverIndex)) {
        return;
    }

    bool wasActive = m_receivers[receiverIndex].active;
    m_receivers.remove(receiverIndex);

    if (wasActive) {
        rebuildHardwareMapping();
    }

    qCDebug(lcReceiver) << "Destroyed receiver" << receiverIndex;
    emit receiverDestroyed(receiverIndex);
}

void ReceiverManager::activateReceiver(int receiverIndex)
{
    if (!m_receivers.contains(receiverIndex)) {
        return;
    }
    if (m_receivers[receiverIndex].active) {
        return;
    }

    m_receivers[receiverIndex].active = true;
    rebuildHardwareMapping();

    qCDebug(lcReceiver) << "Activated receiver" << receiverIndex;
    emit receiverActivated(receiverIndex);
}

void ReceiverManager::deactivateReceiver(int receiverIndex)
{
    if (!m_receivers.contains(receiverIndex)) {
        return;
    }
    if (!m_receivers[receiverIndex].active) {
        return;
    }

    m_receivers[receiverIndex].active = false;
    rebuildHardwareMapping();

    qCDebug(lcReceiver) << "Deactivated receiver" << receiverIndex;
    emit receiverDeactivated(receiverIndex);
}

int ReceiverManager::activeReceiverCount() const
{
    int count = 0;
    for (auto it = m_receivers.constBegin(); it != m_receivers.constEnd(); ++it) {
        if (it->active) {
            ++count;
        }
    }
    return count;
}

bool ReceiverManager::isReceiverActive(int receiverIndex) const
{
    auto it = m_receivers.constFind(receiverIndex);
    if (it != m_receivers.constEnd()) {
        return it->active;
    }
    return false;
}

ReceiverConfig ReceiverManager::receiverConfig(int receiverIndex) const
{
    return m_receivers.value(receiverIndex, kInvalidConfig);
}

void ReceiverManager::setReceiverFrequency(int receiverIndex, quint64 frequencyHz)
{
    if (!m_receivers.contains(receiverIndex)) {
        return;
    }

    m_receivers[receiverIndex].frequencyHz = frequencyHz;
    emit receiverFrequencyChanged(receiverIndex, frequencyHz);

    // If active, notify the hardware
    if (m_receivers[receiverIndex].active && m_receivers[receiverIndex].hardwareRx >= 0) {
        emit hardwareFrequencyChanged(m_receivers[receiverIndex].hardwareRx, frequencyHz);
    }
}

void ReceiverManager::setReceiverSampleRate(int receiverIndex, int sampleRate)
{
    if (!m_receivers.contains(receiverIndex)) {
        return;
    }
    m_receivers[receiverIndex].sampleRate = sampleRate;
}

void ReceiverManager::feedIqData(int hwReceiverIndex, const QVector<float>& samples)
{
    auto it = m_hwToLogical.constFind(hwReceiverIndex);
    if (it == m_hwToLogical.constEnd()) {
        return;
    }

    int logicalIndex = it.value();
    auto rxIt = m_receivers.constFind(logicalIndex);
    if (rxIt != m_receivers.constEnd() && rxIt->wdspChannel >= 0) {
        emit iqDataForChannel(rxIt->wdspChannel, samples);
    }
}

void ReceiverManager::rebuildHardwareMapping()
{
    m_hwToLogical.clear();

    // Assign sequential hardware DDC indices to active receivers
    int hwIndex = 0;
    for (auto it = m_receivers.begin(); it != m_receivers.end(); ++it) {
        if (it->active) {
            it->hardwareRx = hwIndex;
            m_hwToLogical.insert(hwIndex, it->receiverIndex);
            ++hwIndex;
        } else {
            it->hardwareRx = -1;
        }
    }

    int count = hwIndex;
    qCDebug(lcReceiver) << "Hardware mapping rebuilt:" << count << "active receivers";

    emit activeReceiverCountChanged(count);
    emit hardwareReceiverCountChanged(count);

    // Re-emit frequency for each active receiver
    for (auto it = m_receivers.constBegin(); it != m_receivers.constEnd(); ++it) {
        if (it->active && it->hardwareRx >= 0) {
            emit hardwareFrequencyChanged(it->hardwareRx, it->frequencyHz);
        }
    }
}

} // namespace NereusSDR
