// src/core/P1RadioConnection.cpp
//
// Empty-but-compilable stub implementations for P1RadioConnection.
// All wire-format logic lives in Tasks 7–12; this file only provides the
// skeletons needed to satisfy the linker and the factory hookup.

#include "P1RadioConnection.h"
#include "LogCategories.h"

namespace NereusSDR {

P1RadioConnection::P1RadioConnection(QObject* parent)
    : RadioConnection(parent)
{
}

P1RadioConnection::~P1RadioConnection() = default;

void P1RadioConnection::init()
{
    // Sockets and timers created lazily on connect; keep init empty for now.
    // Tasks 9 & 10 wire up m_socket, m_watchdogTimer, m_reconnectTimer here.
}

void P1RadioConnection::connectToRadio(const RadioInfo& info)
{
    m_radioInfo = info;
    m_caps = &BoardCapsTable::forBoard(info.boardType);
    qCInfo(lcConnection) << "P1RadioConnection::connectToRadio STUB for"
                         << m_caps->displayName
                         << "- real wire format coming in Phase 3I Tasks 7-9";
    setState(ConnectionState::Error);
    emit errorOccurred(QStringLiteral("P1 connection not yet implemented (Phase 3I Task 6 stub)"));
}

void P1RadioConnection::disconnect()
{
    setState(ConnectionState::Disconnected);
}

void P1RadioConnection::setReceiverFrequency(int receiverIndex, quint64 frequencyHz)
{
    if (receiverIndex >= 0 && receiverIndex < 7) {
        m_rxFreqHz[receiverIndex] = frequencyHz;
    }
}

void P1RadioConnection::setTxFrequency(quint64 frequencyHz)  { m_txFreqHz = frequencyHz; }
void P1RadioConnection::setActiveReceiverCount(int count)    { m_activeRxCount = count; }
void P1RadioConnection::setSampleRate(int sampleRate)        { m_sampleRate = sampleRate; }
void P1RadioConnection::setAttenuator(int dB)                { m_atten = dB; }
void P1RadioConnection::setPreamp(bool enabled)              { m_preamp = enabled; }
void P1RadioConnection::setTxDrive(int /*level*/)            { /* stub — Task 7 */ }
void P1RadioConnection::setMox(bool enabled)                 { m_mox = enabled; }
void P1RadioConnection::setAntenna(int antennaIndex)         { m_antennaIdx = antennaIndex; }

void P1RadioConnection::onReadyRead()        { /* stub — Task 8 */ }
void P1RadioConnection::onWatchdogTick()     { /* stub — Task 9 */ }
void P1RadioConnection::onReconnectTimeout() { /* stub — Task 10 */ }

void P1RadioConnection::sendMetisStart(bool) { /* Task 7 */ }
void P1RadioConnection::sendMetisStop()      { /* Task 7 */ }
void P1RadioConnection::sendCommandFrame()   { /* Task 7 */ }
void P1RadioConnection::parseEp6Frame(const QByteArray&) { /* Task 8 */ }

void P1RadioConnection::composeCcBank0(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcBank1(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcBank2(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcBank3(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcTxFreq(quint8*)  { /* Task 7 */ }
void P1RadioConnection::composeCcAlexRx(quint8*)  { /* Task 7 */ }
void P1RadioConnection::composeCcAlexTx(quint8*)  { /* Task 7 */ }
void P1RadioConnection::composeCcOcOutputs(quint8*) { /* Task 7 */ }

void P1RadioConnection::applyBoardQuirks(HPSDRHW) { /* Task 11 */ }
void P1RadioConnection::hl2SendIoBoardTlv(const QByteArray&) { /* Task 12 */ }
void P1RadioConnection::hl2CheckBandwidthMonitor() { /* Task 12 */ }
void P1RadioConnection::checkFirmwareMinimum(int)  { /* Task 11 */ }

// From Thetis networkproto1.c — scale 24-bit big-endian sample to [-1, 1].
// Task 8 will implement this properly.
float P1RadioConnection::scaleSample24(const quint8* /*be24*/) { return 0.0f; /* Task 8 */ }

} // namespace NereusSDR
