#include "WdspEngine.h"

#include <QDebug>

namespace NereusSDR {

WdspEngine::WdspEngine(QObject* parent)
    : QObject(parent)
{
}

WdspEngine::~WdspEngine()
{
    // Destroy all remaining channels
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        qDebug() << "WdspEngine: destroying channel" << it.key();
    }
    m_channels.clear();
}

int WdspEngine::createChannel(int sampleRate, int fftSize)
{
    int id = m_nextChannelId++;
    ChannelState state;
    state.sampleRate = sampleRate;
    state.fftSize = fftSize;
    m_channels.insert(id, state);

#ifdef HAVE_WDSP
    // TODO: Call WDSP OpenChannel() and SetInputSamplerate()
#endif

    qDebug() << "WdspEngine: created channel" << id
             << "sampleRate=" << sampleRate << "fftSize=" << fftSize;
    return id;
}

void WdspEngine::destroyChannel(int channelId)
{
    if (!m_channels.contains(channelId)) {
        return;
    }

#ifdef HAVE_WDSP
    // TODO: Call WDSP CloseChannel()
#endif

    m_channels.remove(channelId);
    qDebug() << "WdspEngine: destroyed channel" << channelId;
}

void WdspEngine::processIq(int channelId, const float* iData, const float* qData,
                           float* outLeft, float* outRight, int sampleCount)
{
    Q_UNUSED(channelId);
    Q_UNUSED(iData);
    Q_UNUSED(qData);
    Q_UNUSED(outLeft);
    Q_UNUSED(outRight);
    Q_UNUSED(sampleCount);

#ifdef HAVE_WDSP
    // TODO: Call WDSP fexchange0() or fexchange2() to process I/Q through the DSP chain
#endif
}

void WdspEngine::setMode(int channelId, int mode)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].mode = mode;
#ifdef HAVE_WDSP
        // TODO: Call WDSP SetRXAMode()
#endif
    }
}

void WdspEngine::setFilter(int channelId, int lowCut, int highCut)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].filterLow = lowCut;
        m_channels[channelId].filterHigh = highCut;
#ifdef HAVE_WDSP
        // TODO: Call WDSP SetRXABandpassFreqs()
#endif
    }
}

void WdspEngine::setAgcMode(int channelId, int mode)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].agcMode = mode;
#ifdef HAVE_WDSP
        // TODO: Call WDSP SetRXAAGCMode()
#endif
    }
}

void WdspEngine::setAgcThreshold(int channelId, int threshold)
{
    Q_UNUSED(channelId);
    Q_UNUSED(threshold);
#ifdef HAVE_WDSP
    // TODO: Call WDSP SetRXAAGCTop()
#endif
}

void WdspEngine::setNrEnabled(int channelId, bool enabled)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].nrEnabled = enabled;
#ifdef HAVE_WDSP
        // TODO: Call WDSP SetRXAANRRun()
#endif
    }
}

void WdspEngine::setNr2Enabled(int channelId, bool enabled)
{
    Q_UNUSED(channelId);
    Q_UNUSED(enabled);
#ifdef HAVE_WDSP
    // TODO: Call WDSP SetRXAEMNRRun()
#endif
}

void WdspEngine::setNbEnabled(int channelId, bool enabled)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].nbEnabled = enabled;
#ifdef HAVE_WDSP
        // TODO: Call WDSP SetRXAANBRun()
#endif
    }
}

void WdspEngine::setNb2Enabled(int channelId, bool enabled)
{
    Q_UNUSED(channelId);
    Q_UNUSED(enabled);
#ifdef HAVE_WDSP
    // TODO: Call WDSP SetRXANOBRun()
#endif
}

void WdspEngine::setAnfEnabled(int channelId, bool enabled)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].anfEnabled = enabled;
#ifdef HAVE_WDSP
        // TODO: Call WDSP SetRXAANFRun()
#endif
    }
}

void WdspEngine::setSquelchEnabled(int channelId, bool enabled)
{
    Q_UNUSED(channelId);
    Q_UNUSED(enabled);
#ifdef HAVE_WDSP
    // TODO: Call WDSP SetRXAAMSQRun()
#endif
}

void WdspEngine::setSquelchLevel(int channelId, int level)
{
    Q_UNUSED(channelId);
    Q_UNUSED(level);
#ifdef HAVE_WDSP
    // TODO: Call WDSP SetRXAAMSQThreshold()
#endif
}

void WdspEngine::setTxCompression(int channelId, bool enabled, float level)
{
    Q_UNUSED(channelId);
    Q_UNUSED(enabled);
    Q_UNUSED(level);
#ifdef HAVE_WDSP
    // TODO: Call WDSP SetTXACompressorRun() and SetTXACompressorGain()
#endif
}

void WdspEngine::setTxEqEnabled(int channelId, bool enabled)
{
    Q_UNUSED(channelId);
    Q_UNUSED(enabled);
#ifdef HAVE_WDSP
    // TODO: Call WDSP SetTXAEQRun()
#endif
}

void WdspEngine::getSpectrum(int channelId, float* buffer, int size)
{
    Q_UNUSED(channelId);
    Q_UNUSED(buffer);
    Q_UNUSED(size);
#ifdef HAVE_WDSP
    // TODO: Call WDSP GetSpectrum() or Spectrum0()
#endif
}

} // namespace NereusSDR
