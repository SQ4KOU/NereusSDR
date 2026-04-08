#pragma once

#include <QObject>
#include <QString>

namespace NereusSDR {

// Represents a single receiver slice.
// In NereusSDR, slices are a client-side abstraction — the radio has
// no concept of slices. Each slice owns a WDSP channel for independent
// DSP processing.
class SliceModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(double  frequency   READ frequency   WRITE setFrequency   NOTIFY frequencyChanged)
    Q_PROPERTY(QString mode        READ mode        WRITE setMode        NOTIFY modeChanged)
    Q_PROPERTY(int     filterLow   READ filterLow   WRITE setFilterLow   NOTIFY filterChanged)
    Q_PROPERTY(int     filterHigh  READ filterHigh  WRITE setFilterHigh  NOTIFY filterChanged)
    Q_PROPERTY(bool    active      READ isActive     NOTIFY activeChanged)
    Q_PROPERTY(bool    txSlice     READ isTxSlice    NOTIFY txSliceChanged)

public:
    explicit SliceModel(QObject* parent = nullptr);
    ~SliceModel() override;

    double frequency() const { return m_frequency; }
    void setFrequency(double freq);

    QString mode() const { return m_mode; }
    void setMode(const QString& mode);

    int filterLow() const { return m_filterLow; }
    void setFilterLow(int low);

    int filterHigh() const { return m_filterHigh; }
    void setFilterHigh(int high);

    bool isActive() const { return m_active; }
    void setActive(bool active);

    bool isTxSlice() const { return m_txSlice; }
    void setTxSlice(bool tx);

    int wdspChannelId() const { return m_wdspChannelId; }
    void setWdspChannelId(int id) { m_wdspChannelId = id; }

signals:
    void frequencyChanged(double freq);
    void modeChanged(const QString& mode);
    void filterChanged();
    void activeChanged(bool active);
    void txSliceChanged(bool tx);

private:
    double m_frequency{14225000.0};  // Default: 14.225 MHz (20m USB)
    QString m_mode{"USB"};
    int m_filterLow{-2850};
    int m_filterHigh{-150};
    bool m_active{false};
    bool m_txSlice{false};
    int m_wdspChannelId{-1};
};

} // namespace NereusSDR
