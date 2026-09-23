// no-port-check: NereusSDR-original. R3 Core session presentation and actions.
#include "RemoteConnectionController.h"
#include "core/session/StationClient.h"
#include "gui/OperatorReasonText.h"
#include "gui/RemoteAudioStatus.h"
#include "gui/RemoteMediaController.h"
#include "models/RadioModel.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

namespace NereusSDR {
RemoteConnectionController::RemoteConnectionController(
    StationClient* client, RadioModel* model, RemoteStationOptions options, QObject* parent)
    : QObject(parent), m_client(client), m_model(model), m_options(std::move(options))
{
    connect(client, &StationClient::connectionActivityChanged,
            this, &RemoteConnectionController::changed);
    connect(client, &StationClient::handshakeComplete, this, [this] {
        m_operatorDisconnected = false;
        m_pendingMediaRecoveryEpoch = 0;
        m_retryAttempt = 0;
        emit changed();
    });
    connect(client, &StationClient::sessionEnded, this, [this](const QString&) {
        m_pendingMediaRecoveryEpoch = 0;
        emit changed();
    });
    connect(client, &StationClient::reconnectScheduled, this,
            [this](int attempt, int delayMs) {
        m_retryAttempt = attempt;
        m_retryDelayMs = delayMs;
        emit changed();
    });
    connect(model, &RadioModel::connectionStateChanged,
            this, &RemoteConnectionController::changed);
    connect(model, &RadioModel::infoChanged,
            this, &RemoteConnectionController::changed);
    connect(model, &RadioModel::receiveLayoutRestoreStatusChanged,
            this, &RemoteConnectionController::changed);
    connect(client, &StationClient::stateSnapshotApplied,
            this, &RemoteConnectionController::changed);
}

QString RemoteConnectionController::endpointText() const
{
    const QUrl url(m_options.url);
    // Never display URL user-info/query/fragment or the pairing token.
    QString host = url.host();
    if (host.contains(QLatin1Char(':'))) { host = QLatin1Char('[') + host + QLatin1Char(']'); }
    return url.port() >= 0 ? host + QLatin1Char(':') + QString::number(url.port()) : host;
}

bool RemoteConnectionController::canConnect() const
{
    return m_client && m_options.isRemote() && !m_client->isConnectionActive();
}

bool RemoteConnectionController::canDisconnect() const
{
    return m_client && m_client->isConnectionActive();
}

ConnectionState RemoteConnectionController::state() const
{
    if (!m_client) { return ConnectionState::Disconnected; }
    if (m_client->isHandshakeComplete()) { return ConnectionState::Connected; }
    if (m_client->isReconnectPending()) { return ConnectionState::LinkLost; }
    if (m_client->isConnectionActive()) { return ConnectionState::Connecting; }
    return ConnectionState::Disconnected;
}

QString RemoteConnectionController::statusText() const
{
    switch (state()) {
    case ConnectionState::Connected: return tr("Core connected");
    case ConnectionState::Connecting:
    case ConnectionState::Probing: return tr("Connecting to Core");
    case ConnectionState::LinkLost: return tr("Retrying Core (attempt %1)").arg(m_retryAttempt);
    case ConnectionState::Disconnected:
        return m_operatorDisconnected ? tr("Core disconnected")
             : m_client && !m_client->lastError().isEmpty() ? tr("Core connection failed")
             : tr("Core disconnected");
    }
    return {};
}

QString RemoteConnectionController::radioText() const
{
    if (state() != ConnectionState::Connected) { return tr("Radio state unavailable"); }
    if (!m_model || !m_model->isConnected()) { return tr("Radio offline"); }
    const QString name = m_model->name().isEmpty() ? m_model->model() : m_model->name();
    return name.isEmpty() ? tr("Radio connected") : tr("Radio: %1").arg(name);
}

QString RemoteConnectionController::detailText() const
{
    QString text = tr("Core: %1\n%2\n%3")
        .arg(endpointText(), statusText(), radioText());
    if (state() == ConnectionState::Connected && m_model
        && !m_model->receiveLayoutRestoreMessage().isEmpty()) {
        text += tr("\nReceivers: %1").arg(m_model->receiveLayoutRestoreMessage());
    }
    if (m_client && m_client->isReconnectPending()) {
        text += tr("\nRetry delay: %1 s. Disconnect cancels automatic retries.")
            .arg((m_retryDelayMs + 999) / 1000);
    }
    if (m_client && !m_client->isHandshakeComplete() && !m_operatorDisconnected
        && !m_client->lastError().isEmpty()) {
        // The raw reason is in the log; shown here in user words (R-R3-17).
        text += tr("\nLast failure: %1")
                    .arg(OperatorReasonText::forDisplay(m_client->lastError()));
    }
    return text;
}

void RemoteConnectionController::connectToStation()
{
    if (!canConnect()) { return; }
    m_operatorDisconnected = false;
    m_retryAttempt = 0;
    m_client->connectToStation(QUrl(m_options.url), m_options.token,
                               m_options.fingerprint, m_options.allowUnpinned);
    emit changed();
}

void RemoteConnectionController::disconnectFromStation()
{
    if (!m_client) { return; }
    // Latch before synchronous teardown emits state and retained-radio signals.
    m_operatorDisconnected = true;
    m_pendingMediaRecoveryEpoch = 0;
    m_client->disconnectFromStation(QStringLiteral("operator disconnect"));
    emit changed();
}

void RemoteConnectionController::recoverMediaSession(quint32 expectedEpoch,
                                                      const QString& reason)
{
    if (!m_client || m_operatorDisconnected || expectedEpoch == 0
        || !m_client->isHandshakeComplete()
        || m_client->sessionEpoch() != expectedEpoch
        || m_pendingMediaRecoveryEpoch == expectedEpoch) {
        return;
    }
    m_pendingMediaRecoveryEpoch = expectedEpoch;
    QMetaObject::invokeMethod(this, [this, expectedEpoch, reason] {
        if (m_pendingMediaRecoveryEpoch != expectedEpoch) { return; }
        m_pendingMediaRecoveryEpoch = 0;
        if (!m_client || m_operatorDisconnected
            || !m_client->isHandshakeComplete()
            || m_client->sessionEpoch() != expectedEpoch) {
            return;
        }
        m_client->disconnectFromStation(
            reason.isEmpty() ? QStringLiteral("station media connection failed") : reason,
            true);
    }, Qt::QueuedConnection);
}

RemoteConnectionPanel::RemoteConnectionPanel(RemoteConnectionController* controller,
                                           QWidget* parent, RemoteMediaController* media)
    : QDialog(parent)
{
    setWindowTitle(tr("Core connection"));
    auto* layout = new QVBoxLayout(this);
    auto* details = new QLabel(this);
    details->setObjectName(QStringLiteral("coreConnectionDetails"));
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(details);

    // The "Remote audio" section: only with a media controller, so a panel
    // built without one (there is no such call site today, but tests build
    // one directly) stays exactly as it was before R-R3-23.
    QLabel* audioDetails = nullptr;
    QPushButton* retryButton = nullptr;
    QComboBox* qualityChoice = nullptr;
    if (media) {
        audioDetails = new QLabel(this);
        audioDetails->setObjectName(QStringLiteral("remoteAudioDetails"));
        audioDetails->setTextFormat(Qt::PlainText);
        audioDetails->setWordWrap(true);
        audioDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(audioDetails);
        // R-R3-23: the operator's audio quality choice, stored on this
        // computer. What the Core actually runs, and why when it is not the
        // choice, is in the section text above.
        auto* qualityRow = new QHBoxLayout;
        auto* qualityLabel = new QLabel(tr("Audio quality:"), this);
        qualityChoice = new QComboBox(this);
        qualityChoice->setObjectName(QStringLiteral("remoteAudioQuality"));
        qualityChoice->addItem(tr("Opus"), QVariant::fromValue(int(RemoteAudioProfile::Opus)));
        qualityChoice->addItem(tr("Lossless"), QVariant::fromValue(int(RemoteAudioProfile::Lossless)));
        qualityChoice->setToolTip(tr("Opus is compressed and needs 24 to 48 kbit/s. Lossless "
                                     "plays the station's audio unchanged, for digital modes, "
                                     "and needs about 1.6 Mbit/s. If the network cannot carry "
                                     "it, audio stays on Opus. Saved on this computer."));
        qualityLabel->setBuddy(qualityChoice);
        qualityRow->addWidget(qualityLabel);
        qualityRow->addWidget(qualityChoice, 1);
        layout->addLayout(qualityRow);
        qualityChoice->setCurrentIndex(
            qualityChoice->findData(int(media->audioProfileChoice())));
        QPointer<RemoteMediaController> choiceMedia(media);
        connect(qualityChoice, &QComboBox::currentIndexChanged, this,
                [qualityChoice, choiceMedia](int index) {
            if (!choiceMedia || index < 0) { return; }
            choiceMedia->setAudioProfileChoice(
                static_cast<RemoteAudioProfile>(qualityChoice->itemData(index).toInt()));
        });
        retryButton = new QPushButton(tr("Retry audio"), this);
        retryButton->setObjectName(QStringLiteral("retryRemoteAudio"));
        retryButton->setAutoDefault(false);
        layout->addWidget(retryButton);
        connect(retryButton, &QPushButton::clicked,
                media, &RemoteMediaController::retryAudio);
    }

    auto* buttons = new QDialogButtonBox(this);
    auto* dial = buttons->addButton(tr("Connect"), QDialogButtonBox::ActionRole);
    dial->setObjectName(QStringLiteral("connectCore"));
    auto* stop = buttons->addButton(tr("Disconnect"), QDialogButtonBox::ActionRole);
    stop->setObjectName(QStringLiteral("disconnectCore"));
    auto* close = buttons->addButton(QDialogButtonBox::Close);
    for (auto* button : {dial, stop, close}) { button->setAutoDefault(false); }
    layout->addWidget(buttons);
    connect(dial, &QPushButton::clicked, controller, &RemoteConnectionController::connectToStation);
    connect(stop, &QPushButton::clicked, controller, &RemoteConnectionController::disconnectFromStation);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    const auto refresh = [this, controller, details, dial, stop] {
        const QString text = controller->detailText();
        const bool textChanged = details->text() != text;
        details->setText(text);
        dial->setEnabled(controller->canConnect());
        stop->setEnabled(controller->canDisconnect());
        if (textChanged) { fitHeightToContent(); }
    };
    connect(controller, &RemoteConnectionController::changed, this, refresh);
    refresh();

    if (media) {
        QPointer<RemoteMediaController> guardedMedia(media);
        m_refreshAudio = [this, guardedMedia, audioDetails, retryButton, qualityChoice] {
            if (!guardedMedia) { return; }
            const QString text = formatRemoteAudioDetails(guardedMedia->audioStatus(),
                                                          guardedMedia->audioTelemetry(),
                                                          guardedMedia->audioDelay());
            const bool textChanged = audioDetails->text() != text;
            audioDetails->setText(text);
            retryButton->setEnabled(guardedMedia->audioStatus().retryAvailable);
            {
                const QSignalBlocker blocker(qualityChoice);
                qualityChoice->setCurrentIndex(
                    qualityChoice->findData(int(guardedMedia->audioProfileChoice())));
            }
            if (textChanged) { fitHeightToContent(); }
        };
        connect(media, &RemoteMediaController::audioStatusChanged, this,
                [this] { m_refreshAudio(); });
        // audioStatusChanged() fires only when the derived status changes;
        // the numeric health measurements move continuously while playing,
        // so this section also polls once a second, but only while the
        // panel is shown (showEvent / hideEvent start and stop it).
        m_audioTimer = new QTimer(this);
        m_audioTimer->setObjectName(QStringLiteral("remoteAudioPanelTimer"));
        m_audioTimer->setInterval(1000);
        connect(m_audioTimer, &QTimer::timeout, this, [this] { m_refreshAudio(); });
        m_refreshAudio();
    }

    // Width is a starting size the operator may change; the height always
    // follows the wrapped text, so nothing is clipped at larger fonts and no
    // fixed gap is left below short content.
    resize(440, height());
    fitHeightToContent();
}

void RemoteConnectionPanel::fitHeightToContent()
{
    QLayout* const top = layout();
    if (!top) { return; }
    top->activate();
    const int contentHeight = top->totalHeightForWidth(width());
    if (contentHeight > 0) {
        resize(width(), contentHeight);
    } else {
        adjustSize();
    }
}

void RemoteConnectionPanel::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_audioTimer) {
        m_refreshAudio();
        m_audioTimer->start();
    }
    fitHeightToContent();
}

void RemoteConnectionPanel::hideEvent(QHideEvent* event)
{
    if (m_audioTimer) {
        m_audioTimer->stop();
    }
    QDialog::hideEvent(event);
}
} // namespace NereusSDR
