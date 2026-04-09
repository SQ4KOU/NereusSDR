#pragma once

#include "SliceModel.h"
#include "PanadapterModel.h"
#include "MeterModel.h"
#include "TransmitModel.h"
#include "core/RadioDiscovery.h"
#include "core/RadioConnection.h"

#include <QObject>
#include <QString>
#include <QList>
#include <QThread>

namespace NereusSDR {

class ReceiverManager;
class AudioEngine;
class WdspEngine;

// RadioModel is the central data model for a connected radio.
// It owns the RadioConnection (on a worker thread), ReceiverManager,
// and all sub-models. It routes signals between components.
//
// Thread architecture:
//   Main thread: RadioModel, ReceiverManager, all sub-models, GUI
//   Connection thread: RadioConnection (sockets, protocol I/O)
//   Audio thread: AudioEngine + WdspEngine (future)
class RadioModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString name        READ name        NOTIFY infoChanged)
    Q_PROPERTY(QString model       READ model       NOTIFY infoChanged)
    Q_PROPERTY(QString version     READ version     NOTIFY infoChanged)
    Q_PROPERTY(bool    connected   READ isConnected NOTIFY connectionStateChanged)

public:
    explicit RadioModel(QObject* parent = nullptr);
    ~RadioModel() override;

    // Sub-components
    RadioConnection*  connection()       { return m_connection; }
    const RadioConnection* connection() const { return m_connection; }
    RadioDiscovery*   discovery()        { return m_discovery; }
    ReceiverManager*  receiverManager()  { return m_receiverManager; }
    AudioEngine*      audioEngine()      { return m_audioEngine; }
    WdspEngine*       wdspEngine()       { return m_wdspEngine; }

    // Sub-models
    MeterModel&       meterModel()       { return m_meterModel; }
    TransmitModel&    transmitModel()    { return m_transmitModel; }

    // Slice management (client-side — radio has no slice concept)
    QList<SliceModel*> slices() const { return m_slices; }
    SliceModel* sliceAt(int index) const;
    SliceModel* activeSlice() const { return m_activeSlice; }
    int addSlice();
    void removeSlice(int index);
    void setActiveSlice(int index);

    // Panadapter management (client-side)
    QList<PanadapterModel*> panadapters() const { return m_panadapters; }
    int addPanadapter();
    void removePanadapter(int index);

    // Radio info
    QString name() const { return m_name; }
    QString model() const { return m_model; }
    QString version() const { return m_version; }
    bool isConnected() const;

    // Connection
    void connectToRadio(const RadioInfo& info);
    void disconnectFromRadio();

signals:
    void infoChanged();
    void connectionStateChanged();
    void sliceAdded(int index);
    void sliceRemoved(int index);
    void activeSliceChanged(int index);
    void panadapterAdded(int index);
    void panadapterRemoved(int index);

    // Raw interleaved I/Q for spectrum display (tapped before WDSP processing)
    void rawIqData(const QVector<float>& interleavedIQ);

private slots:
    void onConnectionStateChanged(NereusSDR::ConnectionState state);

private:
    void wireConnectionSignals();
    void teardownConnection();

    // Sub-components (owned, main thread)
    RadioDiscovery*  m_discovery{nullptr};
    ReceiverManager* m_receiverManager{nullptr};
    AudioEngine*     m_audioEngine{nullptr};
    WdspEngine*      m_wdspEngine{nullptr};

    // Connection (owned, lives on m_connThread)
    RadioConnection* m_connection{nullptr};
    QThread*         m_connThread{nullptr};

    // Sub-models
    MeterModel    m_meterModel;
    TransmitModel m_transmitModel;

    // Slices and panadapters (client-managed)
    QList<SliceModel*> m_slices;
    QList<PanadapterModel*> m_panadapters;
    SliceModel* m_activeSlice{nullptr};

    // Radio info
    QString m_name;
    QString m_model;
    QString m_version;

    // Reconnect state
    RadioInfo m_lastRadioInfo;
    bool m_intentionalDisconnect{false};

    // I/Q accumulation buffer for WDSP (accumulates 238-sample P2 packets
    // until we have 1024 samples for one fexchange2 call)
    static constexpr int kWdspBufSize = 1024;
    QVector<float> m_iqAccumI;
    QVector<float> m_iqAccumQ;
};

} // namespace NereusSDR
