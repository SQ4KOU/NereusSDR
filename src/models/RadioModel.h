#pragma once

#include "SliceModel.h"
#include "PanadapterModel.h"
#include "MeterModel.h"
#include "TransmitModel.h"

#include <QObject>
#include <QString>
#include <QList>
#include <QTimer>

namespace NereusSDR {

class RadioConnection;
class RadioDiscovery;
class AudioEngine;
class WdspEngine;
struct RadioInfo;

// RadioModel is the central data model for a connected radio.
// It owns the RadioConnection, processes incoming data, and exposes
// the radio's current state to the GUI via Qt properties/signals.
//
// Unlike AetherSDR's RadioModel (which mirrors radio-side state),
// NereusSDR's RadioModel manages more client-side state because
// OpenHPSDR radios don't have concepts like "slices" or "panadapters".
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
    RadioConnection*  connection()  { return m_connection; }
    RadioDiscovery*   discovery()   { return m_discovery; }
    AudioEngine*      audioEngine() { return m_audioEngine; }
    WdspEngine*       wdspEngine()  { return m_wdspEngine; }

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

private slots:
    void onConnectionStateChanged();
    void onDataReceived(const QByteArray& data);

private:
    // Sub-components (owned)
    RadioConnection* m_connection{nullptr};
    RadioDiscovery*  m_discovery{nullptr};
    AudioEngine*     m_audioEngine{nullptr};
    WdspEngine*      m_wdspEngine{nullptr};

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
};

} // namespace NereusSDR
