// =================================================================
// src/gui/setup/RemoteRigctldPage.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See the header.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - Created for WSJT-X CAT and VAX through a remote window.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/setup/RemoteRigctldPage.h"

#include "core/AppSettings.h"
#include "gui/RemoteRigctlServer.h"
#include "gui/StyleConstants.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace NereusSDR {

RemoteRigctldPage::RemoteRigctldPage(QWidget* parent)
    : SetupPage(QStringLiteral("rigctld"), parent)
{
    Style::applyDarkPageStyle(this);
    const AppSettings& s = AppSettings::instance();

    QGroupBox* group = addSection(tr("rigctld on this computer"));
    auto* form = new QFormLayout;
    form->setSpacing(6);
    static_cast<QVBoxLayout*>(group->layout())->addLayout(form);

    auto* note = new QLabel(
        tr("Programs on this computer, such as WSJT-X (Hamlib NET rigctl), control the slice "
           "this window transmits on and key it as this window. While they key, what they "
           "play into NereusSDR TX goes on the air, never this computer's microphone."),
        group);
    note->setWordWrap(true);
    form->addRow(note);

    m_enable = new QCheckBox(tr("Enable rigctld"), group);
    m_enable->setObjectName(QStringLiteral("remoteRigctldEnable"));
    m_enable->setStyleSheet(QString::fromLatin1(Style::kCheckBoxStyle));
    m_enable->setChecked(s.value(QStringLiteral("RemoteRigctld/Enabled"),
                                 QStringLiteral("False")).toString()
                         == QStringLiteral("True"));
    form->addRow(QString(), m_enable);

    m_address = new QLineEdit(s.value(QStringLiteral("RemoteRigctld/BindAddress"),
                                      QStringLiteral("127.0.0.1")).toString(),
                              group);
    m_address->setObjectName(QStringLiteral("remoteRigctldAddress"));
    m_address->setStyleSheet(QString::fromLatin1(Style::kLineEditStyle));
    m_address->setToolTip(tr("127.0.0.1 accepts programs on this computer only. rigctld has "
                             "no password, so use another address only on a network you "
                             "trust."));
    form->addRow(tr("Listen on:"), m_address);

    m_port = new QSpinBox(group);
    m_port->setObjectName(QStringLiteral("remoteRigctldPort"));
    m_port->setStyleSheet(QString::fromLatin1(Style::kSpinBoxStyle));
    m_port->setRange(1, 65535);
    m_port->setValue(s.value(QStringLiteral("RemoteRigctld/Port"), kDefaultPort).toInt());
    form->addRow(tr("Port:"), m_port);

    m_status = new QLabel(group);
    m_status->setObjectName(QStringLiteral("remoteRigctldStatus"));
    form->addRow(tr("Status:"), m_status);

    connect(m_enable, &QCheckBox::toggled, this, &RemoteRigctldPage::save);
    connect(m_address, &QLineEdit::editingFinished, this, &RemoteRigctldPage::save);
    connect(m_port, &QSpinBox::editingFinished, this, &RemoteRigctldPage::save);
    refreshStatus();
}

void RemoteRigctldPage::setServer(RemoteRigctlServer* server)
{
    if (m_server) {
        disconnect(m_server, nullptr, this, nullptr);
    }
    m_server = server;
    if (m_server) {
        connect(m_server, &RemoteRigctlServer::listeningChanged, this,
                &RemoteRigctldPage::refreshStatus);
        connect(m_server, &RemoteRigctlServer::clientCountChanged, this,
                &RemoteRigctldPage::refreshStatus);
        connect(m_server, &RemoteRigctlServer::keyedChanged, this,
                &RemoteRigctldPage::refreshStatus);
    }
    refreshStatus();
}

QString RemoteRigctldPage::statusForTesting() const
{
    return m_status->text();
}

void RemoteRigctldPage::save()
{
    AppSettings& s = AppSettings::instance();
    s.setValue(QStringLiteral("RemoteRigctld/Enabled"),
               m_enable->isChecked() ? QStringLiteral("True") : QStringLiteral("False"));
    s.setValue(QStringLiteral("RemoteRigctld/BindAddress"), m_address->text().trimmed());
    s.setValue(QStringLiteral("RemoteRigctld/Port"), m_port->value());
    emit settingsChanged();
    refreshStatus();
}

void RemoteRigctldPage::refreshStatus()
{
    if (!m_server) {
        m_status->setText(tr("Not connected to a Core"));
        return;
    }
    if (!m_server->isListening()) {
        if (m_enable->isChecked() && QHostAddress(m_address->text().trimmed()).isNull()) {
            m_status->setText(tr("Not listening: \"%1\" is not an IP address")
                                  .arg(m_address->text().trimmed()));
        } else if (m_enable->isChecked()) {
            m_status->setText(tr("Not listening: %1").arg(m_server->errorString()));
        } else {
            m_status->setText(tr("Off"));
        }
        return;
    }
    QString text = tr("Listening on port %1, %n program(s) connected", nullptr,
                      m_server->clientCount())
                       .arg(m_server->port());
    if (m_server->keyed()) {
        text += tr(", transmitting");
    }
    m_status->setText(text);
}

} // namespace NereusSDR
