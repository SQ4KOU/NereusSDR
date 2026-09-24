#include "GeneralSetupPages.h"

#include "core/AppSettings.h"
#include "gui/SpotHubDialog.h"
#include "models/FreeDVStationModel.h"
#include "models/RadioModel.h"

#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// StartupPrefsPage
// ---------------------------------------------------------------------------

StartupPrefsPage::StartupPrefsPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Startup & Preferences"), model, parent)
{
    // Section: Startup
    QGroupBox* startupSection = addSection(QStringLiteral("Startup"));

    // R-R3-21: the auto-connect choice saved with the last radio, the one
    // the Connection panel option "Auto-connect to this radio on launch" sets
    // (AppSettings saved radio, read by MainWindow::tryAutoReconnect).
    auto* autoConnect = addLabeledToggle(QStringLiteral("Auto-connect to last radio"));
    autoConnect->setObjectName(QStringLiteral("startupAutoConnect"));
    {
        auto& s = AppSettings::instance();
        const auto saved = s.savedRadio(s.lastConnected());
        QSignalBlocker block(autoConnect);
        if (saved.has_value()) {
            autoConnect->setChecked(saved->autoConnect);
            autoConnect->setToolTip(
                tr("Connect to %1 when NereusSDR starts").arg(saved->info.displayName()));
        } else {
            autoConnect->setEnabled(false);
            autoConnect->setToolTip(
                tr("Connect to a radio first; this then starts that radio with NereusSDR."));
        }
    }
    connect(autoConnect, &QPushButton::toggled, this, [](bool on) {
        auto& s = AppSettings::instance();
        const QString last = s.lastConnected();
        if (!s.savedRadio(last).has_value()) { return; }
        // Only the flag: saveRadio would also rewrite the radio's lastSeen.
        s.setRadioAutoConnect(last, on);
        s.save();
    });

    // R-R3-21: the operator identity the Spot Hub Settings tab edits
    // (User/Callsign, User/GridSquare and the copies each spot source reads).
    auto* callsign = addLabeledEdit(QStringLiteral("Callsign"),
                                    QStringLiteral("e.g. KG4VCF"));
    callsign->setObjectName(QStringLiteral("startupCallsign"));
    callsign->setToolTip(tr("Your callsign, as the spot sources send it"));
    auto* gridSquare = addLabeledEdit(QStringLiteral("Grid Square"),
                                      QStringLiteral("e.g. EM73"));
    gridSquare->setObjectName(QStringLiteral("startupGridSquare"));
    gridSquare->setToolTip(tr("Your Maidenhead grid square (4 or 6 characters)"));
    gridSquare->setMaxLength(6);
    {
        const auto& s = AppSettings::instance();
        callsign->setText(s.value(QStringLiteral("User/Callsign")).toString());
        gridSquare->setText(s.value(QStringLiteral("User/GridSquare")).toString());
    }
    auto* identityError = new QLabel;
    identityError->setObjectName(QStringLiteral("startupIdentityError"));
    identityError->setStyleSheet(QStringLiteral("QLabel { color: #ff6666; font-size: 11px; }"));
    identityError->setWordWrap(true);
    identityError->setVisible(false);
    startupSection->layout()->addWidget(identityError);
    const auto commitIdentity = [this, callsign, gridSquare, identityError]() {
        const QString call = callsign->text().trimmed().toUpper();
        const QString grid = gridSquare->text().trimmed().toUpper();
        const QString error = SpotHubDialog::identityError(call, grid);
        identityError->setText(error);
        identityError->setVisible(!error.isEmpty());
        if (!error.isEmpty()) { return; }
        auto& s = AppSettings::instance();
        if (call == s.value(QStringLiteral("User/Callsign")).toString()
            && grid == s.value(QStringLiteral("User/GridSquare")).toString()) {
            return;
        }
        const QString message = s.value(QStringLiteral("FreeDvReporter/Message")).toString();
        SpotHubDialog::saveIdentity(call, grid, message);
        if (RadioModel* m = this->model()) {
            SpotHubDialog::applyIdentityToClients(m->freeDvReporter(), m->pskReporter(),
                                                  call, grid, message);
            if (FreeDVStationModel* stations = m->freeDvStationModel()) {
                stations->setOurGridSquare(grid);
            }
        }
    };
    connect(callsign, &QLineEdit::editingFinished, this, commitIdentity);
    connect(gridSquare, &QLineEdit::editingFinished, this, commitIdentity);
    m_callsignEdit = callsign;
    m_gridEdit = gridSquare;
}

void StartupPrefsPage::setStationSettingsAvailable(bool available, const QString& reason)
{
    gateStationControls({m_callsignEdit, m_gridEdit}, available, reason);
}

// ---------------------------------------------------------------------------
// UiScalePage
// ---------------------------------------------------------------------------

UiScalePage::UiScalePage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("UI Scale & Theme"), model, parent)
{
    // Section: Scale
    addSection(QStringLiteral("Scale"));

    auto* scale = addLabeledCombo(QStringLiteral("UI Scale"),
        {QStringLiteral("100%"), QStringLiteral("125%"),
         QStringLiteral("150%"), QStringLiteral("175%"),
         QStringLiteral("200%")});
    markNyi(scale, QStringLiteral("Phase 3H"));

    // Section: Theme
    addSection(QStringLiteral("Theme"));

    auto* darkLight = addLabeledToggle(QStringLiteral("Dark mode"));
    markNyi(darkLight, QStringLiteral("Phase 3H"));

    auto* fontSize = addLabeledCombo(QStringLiteral("Font Size"),
        {QStringLiteral("Small"), QStringLiteral("Medium"),
         QStringLiteral("Large")});
    markNyi(fontSize, QStringLiteral("Phase 3H"));
}

// ---------------------------------------------------------------------------
// NavigationPage
// ---------------------------------------------------------------------------

NavigationPage::NavigationPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Navigation"), model, parent)
{
    // Section: Mouse
    addSection(QStringLiteral("Mouse"));

    auto* wheelTune = addLabeledToggle(QStringLiteral("Mouse wheel tunes VFO"));
    markNyi(wheelTune, QStringLiteral("Phase 3E"));

    auto* clickTune = addLabeledToggle(QStringLiteral("Click-to-tune on panadapter"));
    markNyi(clickTune, QStringLiteral("Phase 3E"));

    auto* scrollZoom = addLabeledToggle(QStringLiteral("Scroll zoom on panadapter"));
    markNyi(scrollZoom, QStringLiteral("Phase 3E"));

    auto* dblClick = addLabeledCombo(QStringLiteral("Double-click action"),
        {QStringLiteral("Tune"), QStringLiteral("Center"),
         QStringLiteral("None")});
    markNyi(dblClick, QStringLiteral("Phase 3E"));

    // Section: Tuning
    addSection(QStringLiteral("Tuning"));

    auto* snapTune = addLabeledToggle(QStringLiteral("Snap click-tune to step"));
    markNyi(snapTune, QStringLiteral("Phase 3E"));

    auto* wheelOutside = addLabeledToggle(QStringLiteral("Wheel tunes outside spectral display"));
    markNyi(wheelOutside, QStringLiteral("Phase 3E"));

    auto* wheelReverse = addLabeledToggle(QStringLiteral("Reverse wheel direction"));
    markNyi(wheelReverse, QStringLiteral("Phase 3E"));
}

} // namespace NereusSDR
