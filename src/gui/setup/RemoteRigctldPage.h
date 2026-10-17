#pragma once
// =================================================================
// src/gui/setup/RemoteRigctldPage.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Setup > CAT & Network > rigctld, in a
// remote window only: this computer's rigctld, which acts as the window
// (RemoteRigctlServer). The Core's own CAT pages set up the Core's CAT.
//
// AppSettings (this computer's, OperatorLocal): RemoteRigctld/Enabled,
// RemoteRigctld/BindAddress, RemoteRigctld/Port.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - Created for WSJT-X CAT and VAX through a remote window.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/SetupPage.h"

#include <QPointer>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace NereusSDR {

class RemoteRigctlServer;

class RemoteRigctldPage : public SetupPage {
    Q_OBJECT
public:
    // The rigctld port Hamlib's rigctld uses by default (rigctld(1), -t).
    static constexpr int kDefaultPort = 4532;

    explicit RemoteRigctldPage(QWidget* parent = nullptr);

    /// The window's server, for the status line; may be null.
    void setServer(RemoteRigctlServer* server);

    QString statusForTesting() const;

signals:
    /// The page wrote new settings; MainWindow applies them.
    void settingsChanged();

private:
    void save();
    void refreshStatus();

    QPointer<RemoteRigctlServer> m_server;
    QCheckBox* m_enable{nullptr};
    QLineEdit* m_address{nullptr};
    QSpinBox* m_port{nullptr};
    QLabel* m_status{nullptr};
};

} // namespace NereusSDR
