#include "RadioModel.h"
#include "core/RadioConnection.h"
#include "core/RadioDiscovery.h"
#include "core/ReceiverManager.h"
#include "core/AudioEngine.h"
#include "core/WdspEngine.h"
#include "core/RxChannel.h"
#include "core/LogCategories.h"

#include <QMetaObject>
#include <QStandardPaths>
#include <QVector>

namespace NereusSDR {

RadioModel::RadioModel(QObject* parent)
    : QObject(parent)
    , m_discovery(new RadioDiscovery(this))
    , m_receiverManager(new ReceiverManager(this))
    , m_audioEngine(new AudioEngine(this))
    , m_wdspEngine(new WdspEngine(this))
{
    // Connection starts null — created by connectToRadio() via factory
}

RadioModel::~RadioModel()
{
    teardownConnection();
    qDeleteAll(m_slices);
    qDeleteAll(m_panadapters);
}

bool RadioModel::isConnected() const
{
    return m_connection && m_connection->isConnected();
}

// --- Slice Management ---

SliceModel* RadioModel::sliceAt(int index) const
{
    if (index >= 0 && index < m_slices.size()) {
        return m_slices.at(index);
    }
    return nullptr;
}

int RadioModel::addSlice()
{
    auto* slice = new SliceModel(this);
    int index = m_slices.size();
    m_slices.append(slice);

    if (!m_activeSlice) {
        m_activeSlice = slice;
        emit activeSliceChanged(0);
    }

    emit sliceAdded(index);
    return index;
}

void RadioModel::removeSlice(int index)
{
    if (index < 0 || index >= m_slices.size()) {
        return;
    }

    SliceModel* slice = m_slices.takeAt(index);
    if (m_activeSlice == slice) {
        m_activeSlice = m_slices.isEmpty() ? nullptr : m_slices.first();
        emit activeSliceChanged(m_activeSlice ? 0 : -1);
    }

    delete slice;
    emit sliceRemoved(index);
}

void RadioModel::setActiveSlice(int index)
{
    if (index >= 0 && index < m_slices.size()) {
        m_activeSlice = m_slices.at(index);
        emit activeSliceChanged(index);
    }
}

// --- Panadapter Management ---

int RadioModel::addPanadapter()
{
    auto* pan = new PanadapterModel(this);
    int index = m_panadapters.size();
    m_panadapters.append(pan);
    emit panadapterAdded(index);
    return index;
}

void RadioModel::removePanadapter(int index)
{
    if (index < 0 || index >= m_panadapters.size()) {
        return;
    }

    delete m_panadapters.takeAt(index);
    emit panadapterRemoved(index);
}

// --- Connection ---

void RadioModel::connectToRadio(const RadioInfo& info)
{
    // Tear down any existing connection
    if (m_connection) {
        teardownConnection();
    }

    m_lastRadioInfo = info;
    m_intentionalDisconnect = false;

    m_name = info.displayName();
    m_model = RadioInfo::boardTypeName(info.boardType);
    m_version = QString::number(info.firmwareVersion);
    emit infoChanged();

    // Configure ReceiverManager with hardware capabilities
    m_receiverManager->setMaxReceivers(info.maxReceivers);

    // Initialize WDSP DSP engine (wisdom runs async — channel creation
    // is deferred until initializedChanged fires)
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    connect(m_wdspEngine, &WdspEngine::initializedChanged, this, [this](bool ok) {
        if (!ok) {
            return;
        }
        // Create primary RX channel once WDSP is ready
        // 1024 samples input buffer (standard WDSP size), accumulate from 238-sample P2 packets
        RxChannel* rxCh = m_wdspEngine->createRxChannel(0, 1024, 4096, 48000, 48000, 48000);
        if (rxCh) {
            rxCh->setActive(true);
        }
        // Start audio output
        m_audioEngine->start();
        qCInfo(lcDsp) << "WDSP ready — RX channel 0 active, audio started";
    }, Qt::SingleShotConnection);
    m_wdspEngine->initialize(configDir);

    // Factory-create the connection (no parent — will be moved to thread)
    auto conn = RadioConnection::create(info);
    if (!conn) {
        qCWarning(lcConnection) << "Failed to create connection for" << info.displayName();
        return;
    }
    m_connection = conn.release();

    // Create worker thread
    m_connThread = new QThread(this);
    m_connThread->setObjectName(QStringLiteral("ConnectionThread"));

    // Move connection to worker thread BEFORE wiring signals
    m_connection->moveToThread(m_connThread);

    // Wire signals (auto-queued across threads)
    wireConnectionSignals();

    // Start thread — init() will be called on the worker thread
    connect(m_connThread, &QThread::started, m_connection, &RadioConnection::init);
    m_connThread->start();

    // Dispatch connect command to worker thread
    QMetaObject::invokeMethod(m_connection, [conn = m_connection, info]() {
        conn->connectToRadio(info);
    });

    qCDebug(lcConnection) << "Connecting to" << info.displayName()
                          << "P" << static_cast<int>(info.protocol);
}

void RadioModel::disconnectFromRadio()
{
    m_intentionalDisconnect = true;
    teardownConnection();
}

void RadioModel::wireConnectionSignals()
{
    if (!m_connection) {
        return;
    }

    // Connection state → RadioModel (auto-queued: connection thread → main thread)
    connect(m_connection, &RadioConnection::connectionStateChanged,
            this, &RadioModel::onConnectionStateChanged);

    // I/Q data → accumulate → WDSP → AudioEngine
    // Bypass ReceiverManager DDC mapping for now — ANAN-G2 uses DDC2 as primary
    // receiver but ReceiverManager assigns sequential hw indices starting at 0.
    // TODO: Port full UpdateDDCs() logic from Thetis console.cs:8186
    //
    // Accumulate 238-sample P2 packets into 1024-sample WDSP buffers.
    // This reduces fexchange2 call rate from ~200/sec to ~47/sec and uses
    // a standard power-of-2 buffer size that WDSP handles reliably.
    m_iqAccumI.clear();
    m_iqAccumQ.clear();
    m_iqAccumI.reserve(kWdspBufSize);
    m_iqAccumQ.reserve(kWdspBufSize);

    connect(m_connection, &RadioConnection::iqDataReceived,
            this, [this](int ddcIndex, const QVector<float>& samples) {
        Q_UNUSED(ddcIndex);

        // Fork raw I/Q to spectrum display (before WDSP processing)
        emit rawIqData(samples);

        // Deinterleave and append to accumulation buffers
        int numSamples = samples.size() / 2;
        for (int i = 0; i < numSamples; ++i) {
            m_iqAccumI.append(samples[i * 2]);
            m_iqAccumQ.append(samples[i * 2 + 1]);
        }

        // Process when we have enough samples
        while (m_iqAccumI.size() >= kWdspBufSize) {
            RxChannel* rxCh = m_wdspEngine->rxChannel(0);
            if (!rxCh) {
                m_iqAccumI.clear();
                m_iqAccumQ.clear();
                return;
            }

            QVector<float> outI(kWdspBufSize);
            QVector<float> outQ(kWdspBufSize);
            rxCh->processIq(m_iqAccumI.data(), m_iqAccumQ.data(),
                            outI.data(), outQ.data(), kWdspBufSize);

            m_audioEngine->feedAudio(0, outI.data(), outQ.data(), kWdspBufSize);

            // Remove processed samples
            m_iqAccumI.remove(0, kWdspBufSize);
            m_iqAccumQ.remove(0, kWdspBufSize);
        }
    });

    // Meter data → MeterModel
    connect(m_connection, &RadioConnection::meterDataReceived,
            this, [this](float fwd, float rev, float voltage, float current) {
        Q_UNUSED(voltage);
        Q_UNUSED(current);
        // MeterModel update — expand when MeterModel has proper setters
        Q_UNUSED(fwd);
        Q_UNUSED(rev);
    });

    // Error handling
    connect(m_connection, &RadioConnection::errorOccurred,
            this, [this](const QString& msg) {
        qCWarning(lcConnection) << "Connection error:" << msg;
    });

    // ReceiverManager → RadioConnection (hardware updates)
    // These use QMetaObject::invokeMethod to cross from main→connection thread
    connect(m_receiverManager, &ReceiverManager::hardwareReceiverCountChanged,
            this, [this](int count) {
        if (m_connection) {
            QMetaObject::invokeMethod(m_connection, [conn = m_connection, count]() {
                conn->setActiveReceiverCount(count);
            });
        }
    });

    connect(m_receiverManager, &ReceiverManager::hardwareFrequencyChanged,
            this, [this](int hwIndex, quint64 freq) {
        if (m_connection) {
            QMetaObject::invokeMethod(m_connection, [conn = m_connection, hwIndex, freq]() {
                conn->setReceiverFrequency(hwIndex, freq);
            });
        }
    });
}

void RadioModel::teardownConnection()
{
    if (!m_connection) {
        return;
    }

    // Stop audio output
    m_audioEngine->stop();

    // Shutdown WDSP (destroys all channels, saves cache)
    m_wdspEngine->shutdown();

    // Disconnect all signals FIRST (prevents new work being queued)
    QObject::disconnect(m_connection, nullptr, this, nullptr);
    QObject::disconnect(m_connection, nullptr, m_receiverManager, nullptr);
    QObject::disconnect(m_receiverManager, nullptr, this, nullptr);

    if (m_connThread && m_connThread->isRunning()) {
        // Queue disconnect() then quit() on the worker thread.
        // disconnect() closes sockets/timers, then quit() exits the event loop.
        // Both are queued as events — they execute in order when the loop is free.
        QMetaObject::invokeMethod(m_connection, &RadioConnection::disconnect);
        m_connThread->quit();

        // Wait for thread to finish. If it doesn't stop in time, force it.
        if (!m_connThread->wait(3000)) {
            qCWarning(lcConnection) << "Worker thread did not stop in time, terminating";
            m_connThread->terminate();
            m_connThread->wait(1000);
        }
    }

    delete m_connection;
    m_connection = nullptr;

    delete m_connThread;
    m_connThread = nullptr;
}

void RadioModel::onConnectionStateChanged(ConnectionState state)
{
    emit connectionStateChanged();

    switch (state) {
    case ConnectionState::Connected:
        qCDebug(lcConnection) << "Connected to" << m_name;
        break;
    case ConnectionState::Disconnected:
        qCDebug(lcConnection) << "Disconnected from" << m_name;
        break;
    case ConnectionState::Connecting:
        qCDebug(lcConnection) << "Connecting to" << m_name << "...";
        break;
    case ConnectionState::Error:
        qCWarning(lcConnection) << "Connection error for" << m_name;
        break;
    }
}

} // namespace NereusSDR
