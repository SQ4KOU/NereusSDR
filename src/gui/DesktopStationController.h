#pragma once
// no-port-check: NereusSDR-original. Task 48 desktop hosting over one borrowed local model.

#include "core/station/StationHost.h"

#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>

namespace NereusSDR {

class RadioModel;
class StationServer;

class DesktopStationController final : public QObject {
public:
    enum class Key { Mox, Tune };
    enum class RequestState { Refused, NoChange, Pending, Ask };

    struct TakeQuestion {
        Key key{Key::Mox};
        quint64 holderEpoch{0};
        bool holderKeyed{false};
        QString holderName;
        QString holderShortName;
        quint64 questionId{0};
    };
    struct RequestResult {
        RequestState state{RequestState::Refused};
        std::optional<TakeQuestion> question;
        QString reason;
    };

    // The model, settings and chosen profile are caller-owned. Construction
    // never loads settings or station identity. start(true) is permitted only
    // after the caller has acquired exclusive ownership of that profile.
    DesktopStationController(RadioModel* localModel, StationHostOptions options,
                             QObject* parent = nullptr);
    ~DesktopStationController() override;

    bool start(bool profileOwnershipEstablished);
    void stop();
    StationHost* host() const { return m_host.get(); }
    StationServer* server() const;
    bool enabled() const;

    RequestResult requestMox(bool on);
    RequestResult requestTune(bool on);
    RequestResult confirmTake(const TakeQuestion& shown);

private:
    RequestResult request(Key key, bool on);
    RequestResult takeAndKey(Key key, std::optional<quint64> shownEpoch,
                             std::optional<bool> shownKeyed);
    RequestResult ask(Key key);
    void keyNow(Key key);
    bool stationHoldsTransmit() const;

    QPointer<RadioModel> m_model;
    StationHostOptions m_options;
    std::unique_ptr<StationHost> m_host;
    std::optional<TakeQuestion> m_question;
    quint64 m_intentGeneration{0};
    quint64 m_nextQuestionId{0};
    bool m_moxRequested{false};
    bool m_tuneRequested{false};
    bool m_stopQueued{false};
};

} // namespace NereusSDR
