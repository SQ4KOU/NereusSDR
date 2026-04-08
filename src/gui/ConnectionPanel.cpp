#include "ConnectionPanel.h"
#include "models/RadioModel.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFont>

namespace NereusSDR {

ConnectionPanel::ConnectionPanel(RadioModel* model, QWidget* parent)
    : QDialog(parent)
    , m_radioModel(model)
{
    setWindowTitle(QStringLiteral("Connect to Radio"));
    setMinimumSize(400, 350);
    resize(420, 400);

    buildUI();

    // Wire discovery signals
    RadioDiscovery* disc = m_radioModel->discovery();
    connect(disc, &RadioDiscovery::radioDiscovered, this, &ConnectionPanel::onRadioDiscovered);
    connect(disc, &RadioDiscovery::radioUpdated, this, &ConnectionPanel::onRadioUpdated);
    connect(disc, &RadioDiscovery::radioLost, this, &ConnectionPanel::onRadioLost);

    // Wire connection state
    connect(m_radioModel, &RadioModel::connectionStateChanged,
            this, &ConnectionPanel::onConnectionStateChanged);

    // Populate with any radios already discovered before the panel opened
    const QList<RadioInfo> existing = disc->discoveredRadios();
    for (const RadioInfo& info : existing) {
        onRadioDiscovered(info);
    }

    // Ensure discovery is running
    disc->startDiscovery();

    if (m_radioList->count() == 0) {
        setStatusText(QStringLiteral("Searching for radios..."));
    }
}

ConnectionPanel::~ConnectionPanel()
{
    // Don't stop discovery here — RadioModel owns it and it keeps running
}

void ConnectionPanel::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // --- Status ---
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #8090a0; padding: 4px; }"));
    mainLayout->addWidget(m_statusLabel);

    // --- Discovered Radios ---
    auto* radioGroup = new QGroupBox(QStringLiteral("Discovered Radios"), this);
    auto* radioLayout = new QVBoxLayout(radioGroup);

    m_radioList = new QListWidget(this);
    m_radioList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_radioList->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background: #0a0a15;"
        "  color: #c8d8e8;"
        "  border: 1px solid #203040;"
        "  font-size: 13px;"
        "}"
        "QListWidget::item {"
        "  padding: 8px;"
        "  border-bottom: 1px solid #1a2a3a;"
        "}"
        "QListWidget::item:selected {"
        "  background: #00b4d8;"
        "  color: #ffffff;"
        "}"
        "QListWidget::item:hover {"
        "  background: #1a3a4a;"
        "}"));
    connect(m_radioList, &QListWidget::itemSelectionChanged,
            this, &ConnectionPanel::onRadioSelectionChanged);
    connect(m_radioList, &QListWidget::itemDoubleClicked,
            this, &ConnectionPanel::onConnectClicked);
    radioLayout->addWidget(m_radioList);

    // Refresh button
    m_refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    m_refreshBtn->setAutoDefault(false);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        m_radioList->clear();
        m_discoveredRadios.clear();
        m_radioModel->discovery()->startDiscovery();
        setStatusText(QStringLiteral("Searching for radios..."));
    });
    radioLayout->addWidget(m_refreshBtn);

    mainLayout->addWidget(radioGroup);

    // --- Connect / Disconnect Buttons ---
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_connectBtn = new QPushButton(QStringLiteral("Connect"), this);
    m_connectBtn->setAutoDefault(false);
    m_connectBtn->setEnabled(false);
    m_connectBtn->setMinimumWidth(100);
    m_connectBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #00b4d8;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 8px 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background: #0096b7; }"
        "QPushButton:disabled { background: #303850; color: #606878; }"));
    connect(m_connectBtn, &QPushButton::clicked, this, &ConnectionPanel::onConnectClicked);
    btnLayout->addWidget(m_connectBtn);

    m_disconnectBtn = new QPushButton(QStringLiteral("Disconnect"), this);
    m_disconnectBtn->setAutoDefault(false);
    m_disconnectBtn->setEnabled(false);
    m_disconnectBtn->setMinimumWidth(100);
    m_disconnectBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #304050;"
        "  color: #c8d8e8;"
        "  border: 1px solid #405060;"
        "  border-radius: 4px;"
        "  padding: 8px 16px;"
        "}"
        "QPushButton:hover { background: #405060; }"
        "QPushButton:disabled { background: #1a2a3a; color: #404858; border-color: #203040; }"));
    connect(m_disconnectBtn, &QPushButton::clicked, this, &ConnectionPanel::onDisconnectClicked);
    btnLayout->addWidget(m_disconnectBtn);

    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // Apply dark theme to dialog
    setStyleSheet(QStringLiteral(
        "QDialog { background: #0f0f1a; }"
        "QGroupBox {"
        "  color: #8090a0;"
        "  border: 1px solid #203040;"
        "  border-radius: 4px;"
        "  margin-top: 8px;"
        "  padding-top: 12px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 10px;"
        "  padding: 0 3px;"
        "}"));

    updateButtonStates();
}

void ConnectionPanel::setStatusText(const QString& text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}

// --- Discovery Slots ---

void ConnectionPanel::onRadioDiscovered(const RadioInfo& info)
{
    m_discoveredRadios.insert(info.macAddress, info);

    // Build display text
    QString protocolStr = (info.protocol == ProtocolVersion::Protocol2)
                          ? QStringLiteral("P2") : QStringLiteral("P1");
    QString statusStr = info.inUse ? QStringLiteral(" [In Use]") : QString();
    QString text = QStringLiteral("%1  —  %2  (FW %3)%4\n%5  %6")
        .arg(info.displayName(), protocolStr)
        .arg(info.firmwareVersion)
        .arg(statusStr, info.address.toString(), info.macAddress);

    auto* item = new QListWidgetItem(text, m_radioList);
    item->setData(kMacRole, info.macAddress);

    // If this is the only radio, select it automatically
    if (m_radioList->count() == 1) {
        m_radioList->setCurrentRow(0);
    }

    setStatusText(QStringLiteral("Found %1 radio(s)").arg(m_radioList->count()));
    updateButtonStates();

    qCDebug(lcDiscovery) << "Panel: radio discovered" << info.displayName()
                         << info.address.toString();
}

void ConnectionPanel::onRadioUpdated(const RadioInfo& info)
{
    m_discoveredRadios.insert(info.macAddress, info);

    // Find and update the list item
    for (int i = 0; i < m_radioList->count(); ++i) {
        QListWidgetItem* item = m_radioList->item(i);
        if (item->data(kMacRole).toString() == info.macAddress) {
            QString protocolStr = (info.protocol == ProtocolVersion::Protocol2)
                                  ? QStringLiteral("P2") : QStringLiteral("P1");
            QString statusStr = info.inUse ? QStringLiteral(" [In Use]") : QString();
            QString text = QStringLiteral("%1  —  %2  (FW %3)%4\n%5  %6")
                .arg(info.displayName(), protocolStr)
                .arg(info.firmwareVersion)
                .arg(statusStr, info.address.toString(), info.macAddress);
            item->setText(text);
            break;
        }
    }
}

void ConnectionPanel::onRadioLost(const QString& macAddress)
{
    m_discoveredRadios.remove(macAddress);

    for (int i = 0; i < m_radioList->count(); ++i) {
        if (m_radioList->item(i)->data(kMacRole).toString() == macAddress) {
            delete m_radioList->takeItem(i);
            break;
        }
    }

    if (m_radioList->count() == 0) {
        setStatusText(QStringLiteral("Searching for radios..."));
    } else {
        setStatusText(QStringLiteral("Found %1 radio(s)").arg(m_radioList->count()));
    }

    updateButtonStates();
}

// --- Connection State ---

void ConnectionPanel::onConnectionStateChanged()
{
    if (m_radioModel->isConnected()) {
        setStatusText(QStringLiteral("Connected to %1").arg(m_radioModel->name()));
    } else {
        if (m_radioList->count() > 0) {
            setStatusText(QStringLiteral("Found %1 radio(s)").arg(m_radioList->count()));
        } else {
            setStatusText(QStringLiteral("Disconnected"));
        }
    }
    updateButtonStates();
}

// --- Button Handlers ---

void ConnectionPanel::onConnectClicked()
{
    RadioInfo info = selectedRadio();
    if (info.macAddress.isEmpty()) {
        return;
    }

    if (info.inUse) {
        setStatusText(QStringLiteral("Warning: radio reports in use — attempting connection anyway"));
    } else {
        setStatusText(QStringLiteral("Connecting to %1...").arg(info.displayName()));
    }
    m_connectBtn->setEnabled(false);

    // Save last connected radio MAC for auto-reconnect
    AppSettings::instance().setValue(QStringLiteral("LastConnectedRadioMac"),
                                    info.macAddress);

    m_radioModel->connectToRadio(info);
}

void ConnectionPanel::onDisconnectClicked()
{
    setStatusText(QStringLiteral("Disconnecting..."));
    m_radioModel->disconnectFromRadio();
}

void ConnectionPanel::onRadioSelectionChanged()
{
    updateButtonStates();
}

void ConnectionPanel::updateButtonStates()
{
    bool hasSelection = (m_radioList->currentItem() != nullptr);
    bool connected = m_radioModel->isConnected();

    m_connectBtn->setEnabled(hasSelection && !connected);
    m_disconnectBtn->setEnabled(connected);
}

RadioInfo ConnectionPanel::selectedRadio() const
{
    QListWidgetItem* item = m_radioList->currentItem();
    if (!item) {
        return {};
    }
    QString mac = item->data(kMacRole).toString();
    return m_discoveredRadios.value(mac);
}

} // namespace NereusSDR
