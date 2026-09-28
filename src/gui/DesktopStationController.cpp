// no-port-check: NereusSDR-original. Task 48 desktop hosting over one borrowed local model.
#include "gui/DesktopStationController.h"

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/SliceOwnership.h"
#include "core/safety/TransmitHolder.h"
#include "core/session/StationServer.h"
#include "models/RadioModel.h"

#include <QDir>
#include <QFileInfo>

namespace NereusSDR {

DesktopStationController::DesktopStationController(RadioModel* localModel,
                                                   StationHostOptions options, QObject* parent)
    : QObject(parent), m_model(localModel), m_options(std::move(options))
{
    if (localModel) {
        connect(localModel, &QObject::destroyed, this, [this] {
            ++m_intentGeneration;
            m_model = nullptr;
            stop();
        });
    }
}

DesktopStationController::~DesktopStationController()
{
    stop();
}

bool DesktopStationController::start(bool profileOwnershipEstablished)
{
    if (m_host || m_startingHost || m_stopping || !profileOwnershipEstablished || !m_model
        || m_model->role() != RadioModel::Role::Local || !m_options.settings
        || m_options.securityDirectory.isEmpty() || !m_options.hostingDevice
        || QDir::cleanPath(QFileInfo(m_options.settings->filePath()).absolutePath())
               != QDir::cleanPath(QFileInfo(m_options.securityDirectory).absoluteFilePath())
        || m_options.remotePort <= 0 || m_options.remotePort > 65535
        || StationHost::listenerAddressFor(m_options.remoteBind).isNull()) {
        return false;
    }
    // Keep ownership on this stack while start() can emit synchronous signals.
    // A callback may delete the controller without deleting a Host whose own
    // start() is still running.
    auto host = std::make_unique<StationHost>(m_model.data(), m_options);
#ifdef NEREUS_BUILD_TESTS
    if (m_serverCreatedForTest) { host->setServerCreatedForTest(m_serverCreatedForTest); }
#endif
    const QPointer<DesktopStationController> self(this);
    m_startingHost = host.get();
    m_stopDuringStart = false;
    const bool started = host->start();
    if (!self) { return false; }
    m_startingHost = nullptr;
    if (m_stopDuringStart || !started) {
        m_stopDuringStart = false;
        host->stop();
        return false;
    }
    m_host = std::move(host);
    emit hostingStateChanged(enabled());
    if (!self) { return false; }
    return enabled();
}

void DesktopStationController::stop()
{
    ++m_intentGeneration;
    m_question.reset();
    m_moxRequested = false;
    m_tuneRequested = false;
    if (m_stopping) { return; }
    m_stopping = true;
    const QPointer<DesktopStationController> self(this);
    // As in start(), keep the Host alive through callbacks that may delete
    // this controller while its stop() is on the stack.
    auto host = std::move(m_host);
    StationHost* const startingHost = m_startingHost;
    if (startingHost) {
        // Latch the cancel before stopAllTx() can emit a reentrant callback.
        m_stopDuringStart = true;
    }
    if (!host && !startingHost) {
        m_stopping = false;
        return;
    }
    const QPointer<RadioModel> model(m_model);
    // This is a Core shutdown, so even a different device's on-air key must
    // end before ingress closes. Ordinary button-off below is owner-scoped.
    if (model) {
        model->stopAllTx(QStringLiteral("The Core is closing."));
    }
    // Both Host lifetimes are owned by an active stack, even if stopAllTx()
    // synchronously deleted this controller. Close ingress only after TX.
    if (startingHost) {
        startingHost->quiesce();
    }
    if (host) {
        host->stop();
    }
    host.reset();
    if (self) {
        m_stopping = false;
        emit hostingStateChanged(false);
    }
}

StationServer* DesktopStationController::server() const
{
    return m_host ? m_host->server() : nullptr;
}

bool DesktopStationController::enabled() const
{
    return m_host && m_host->listenerReady();
}

DesktopStationController::RequestResult DesktopStationController::requestMox(bool on)
{
    return request(Key::Mox, on);
}

DesktopStationController::RequestResult DesktopStationController::requestTune(bool on)
{
    return request(Key::Tune, on);
}

DesktopStationController::RequestResult DesktopStationController::request(Key key, bool on)
{
    ++m_intentGeneration;
    m_question.reset();
    if (!on) {
        bool& requested = key == Key::Mox ? m_moxRequested : m_tuneRequested;
        const bool wasRequested = requested;
        requested = false;
        if (wasRequested && m_model && stationHoldsTransmit()) {
            if (key == Key::Mox) {
                MoxController* const mox = m_model->moxController();
                if (mox && mox->isManualKey() && mox->currentKeyer().isStation()
                    && mox->currentKeyer().source != PttMode::Mic) {
                    m_model->setMoxFromButton(false);
                }
            } else if (m_model->isTune()) {
                m_model->setTune(false);
            }
        }
        return {RequestState::NoChange, {}, {}};
    }
    if (!m_model || !enabled() || !server()) {
        return {RequestState::Refused, {}, QStringLiteral("This computer is not hosting a Core.")};
    }
    return takeAndKey(key, std::nullopt, std::nullopt);
}

DesktopStationController::RequestResult DesktopStationController::confirmTake(
    const TakeQuestion& shown)
{
    if (!m_question || shown.questionId != m_question->questionId
        || shown.key != m_question->key || shown.holderEpoch != m_question->holderEpoch
        || shown.holderKeyed != m_question->holderKeyed
        || shown.holderName != m_question->holderName
        || shown.holderShortName != m_question->holderShortName) {
        return {RequestState::Refused, {}, QStringLiteral("That transmit question is no longer current.")};
    }
    ++m_intentGeneration;
    m_question.reset();
    if (!m_model || !enabled() || !server()) {
        return {RequestState::Refused, {}, QStringLiteral("This computer is not hosting a Core.")};
    }
    return takeAndKey(shown.key, shown.holderEpoch, shown.holderKeyed);
}

DesktopStationController::RequestResult DesktopStationController::takeAndKey(
    Key key, std::optional<quint64> shownEpoch, std::optional<bool> shownKeyed)
{
    StationServer* const stationServer = server();
    if (!stationServer || !m_model) {
        return {RequestState::Refused, {}, QStringLiteral("This computer is not hosting a Core.")};
    }
    if (stationHoldsTransmit()) {
        keyNow(key);
        return {RequestState::Pending, {}, {}};
    }

    const QPointer<DesktopStationController> self(this);
    const QPointer<StationServer> serverRef(stationServer);
    const QPointer<RadioModel> modelRef(m_model);
    const quint64 intent = m_intentGeneration;
    const TransmitHolder::TakeVerdict verdict = stationServer->takeTransmitForStation(
        shownEpoch, shownKeyed, [self, serverRef, modelRef, intent, key](bool assigned) {
            if (!self || !serverRef || !modelRef || !assigned
                || self->m_intentGeneration != intent || self->server() != serverRef
                || self->m_model != modelRef || !self->stationHoldsTransmit()) {
                return;
            }
            self->keyNow(key);
        });
    // runTake() may complete synchronously and keyNow() may emit callbacks
    // that stop, replace, or delete this controller before this call returns.
    if (!self || !serverRef || !modelRef || self->m_intentGeneration != intent
        || self->server() != serverRef || self->m_model != modelRef) {
        return {RequestState::Refused, {}, QStringLiteral("The transmit request changed.")};
    }
    switch (verdict) {
    case TransmitHolder::TakeVerdict::AtOnce:
        return {RequestState::Pending, {}, {}};
    case TransmitHolder::TakeVerdict::AlreadyHeld:
        self->keyNow(key);
        return {RequestState::Pending, {}, {}};
    case TransmitHolder::TakeVerdict::Ask:
        return self->ask(key);
    case TransmitHolder::TakeVerdict::Refuse:
        return {RequestState::Refused, {}, serverRef->transmitHolder()
            ->askTake(SliceOwnership::stationDevice(), shownEpoch, shownKeyed).refusal.text};
    }
    return {RequestState::Refused, {}, {}};
}

DesktopStationController::RequestResult DesktopStationController::ask(Key key)
{
    StationServer* const stationServer = server();
    if (!stationServer) { return {RequestState::Refused, {}, {}}; }
    TransmitHolder* const holder = stationServer->transmitHolder();
    const QPointer<DesktopStationController> self(this);
    const QPointer<StationServer> serverRef(stationServer);
    const quint64 intent = m_intentGeneration;
    const std::optional<TransmitHolder::Holder> current = holder->holder();
    if (!self || !serverRef || self->m_intentGeneration != intent || !current) {
        return {RequestState::Refused, {}, {}};
    }
    TakeQuestion question;
    question.key = key;
    question.holderEpoch = holder->epoch();
    question.holderKeyed = current->keyed;
    question.holderName = current->name;
    question.holderShortName = current->shortName;
    question.questionId = ++m_nextQuestionId;
    m_question = question;
    return {RequestState::Ask, question, {}};
}

void DesktopStationController::keyNow(Key key)
{
    if (!m_model || !stationHoldsTransmit()) { return; }
    const QPointer<DesktopStationController> self(this);
    const QPointer<RadioModel> model(m_model);
    const quint64 intent = m_intentGeneration;
    if (key == Key::Mox) {
        model->setMoxFromButton(true);
        if (!self || !model || self->m_model != model || self->m_intentGeneration != intent) {
            return;
        }
        MoxController* const mox = model->moxController();
        self->m_moxRequested = mox && mox->isManualKey();
    } else {
        model->setTune(true);
        if (!self || !model || self->m_model != model || self->m_intentGeneration != intent) {
            return;
        }
        self->m_tuneRequested = model->isTune();
    }
}

bool DesktopStationController::stationHoldsTransmit() const
{
    StationServer* const stationServer = server();
    return stationServer && stationServer->transmitHolder()
        ->isHeldBy(SliceOwnership::stationDevice());
}

} // namespace NereusSDR
