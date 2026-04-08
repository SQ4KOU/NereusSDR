#include "RadioModel.h"
#include "core/RadioConnection.h"
#include "core/RadioDiscovery.h"
#include "core/AudioEngine.h"
#include "core/WdspEngine.h"

#include <QDebug>

namespace NereusSDR {

RadioModel::RadioModel(QObject* parent)
    : QObject(parent)
    , m_connection(new RadioConnection(this))
    , m_discovery(new RadioDiscovery(this))
    , m_audioEngine(new AudioEngine(this))
    , m_wdspEngine(new WdspEngine(this))
{
    connect(m_connection, &RadioConnection::connectionStateChanged,
            this, &RadioModel::onConnectionStateChanged);
    connect(m_connection, &RadioConnection::dataReceived,
            this, &RadioModel::onDataReceived);
}

RadioModel::~RadioModel()
{
    qDeleteAll(m_slices);
    qDeleteAll(m_panadapters);
}

bool RadioModel::isConnected() const
{
    return m_connection && m_connection->isConnected();
}

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

void RadioModel::connectToRadio(const RadioInfo& info)
{
    m_name = info.name;
    m_model = info.model;
    m_version = QString::number(info.firmwareVersion);
    emit infoChanged();

    m_connection->connectToRadio(info);
}

void RadioModel::disconnectFromRadio()
{
    m_connection->disconnect();
}

void RadioModel::onConnectionStateChanged()
{
    emit connectionStateChanged();
    if (isConnected()) {
        qDebug() << "Connected to" << m_name;
    } else {
        qDebug() << "Disconnected from" << m_name;
    }
}

void RadioModel::onDataReceived(const QByteArray& data)
{
    Q_UNUSED(data);
    // TODO: Parse incoming OpenHPSDR protocol data
    // - Extract I/Q samples and route to WdspEngine
    // - Extract meter data and route to MeterModel
    // - Handle C&C feedback data
}

} // namespace NereusSDR
