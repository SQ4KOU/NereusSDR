// NereusSDR-original one-way Core LAN discovery multicast sender.
#pragma once

#include "StationLanAnnouncement.h"

#include <QObject>
#include <QTimer>

namespace NereusSDR {

bool stationLanListenerServesAddress(const QHostAddress& listener,
                                     const QHostAddress& source);

class StationLanAnnouncer : public QObject {
    Q_OBJECT

public:
    explicit StationLanAnnouncer(QObject* parent = nullptr);

    void update(const QHostAddress& listenerAddress,
                const StationLanAnnouncement& announcement);
    void stop();
    bool isActive() const { return m_active; }
    void announceNow();

private:
    void onTimer();

    QTimer m_timer;
    QHostAddress m_listenerAddress;
    StationLanAnnouncement m_announcement;
    bool m_active = false;
};

} // namespace NereusSDR
