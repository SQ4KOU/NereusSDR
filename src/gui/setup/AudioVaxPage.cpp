// =================================================================
// src/gui/setup/AudioVaxPage.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original Setup → Audio → VAX page.
// See AudioVaxPage.h for the full header.
//
// Sub-Phase 12 Task 12.3 (2026-04-20): Written by J.J. Boyd (KG4VCF),
// AI-assisted via Anthropic Claude Code.
// =================================================================

#include "AudioVaxPage.h"

#include "core/AppSettings.h"
#include "core/AudioDeviceConfig.h"
#include "core/AudioEngine.h"
#include "core/audio/VirtualCableDetector.h"
#include "models/RadioModel.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Style constants — same palette as DeviceCard / STYLEGUIDE.md
// ---------------------------------------------------------------------------
namespace {

static const char* kGroupStyle =
    "QGroupBox {"
    "  border: 1px solid #203040;"
    "  border-radius: 4px;"
    "  margin-top: 8px;"
    "  padding-top: 12px;"
    "  font-weight: bold;"
    "  color: #8aa8c0;"
    "}"
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }";

static const char* kBadgeStyle =
    "QLabel {"
    "  background: #2a1a00;"
    "  border: 1px solid #b87300;"
    "  border-radius: 6px;"
    "  color: #e8a030;"
    "  font-size: 10px;"
    "  padding: 2px 6px;"
    "}";

static const char* kAutoDetectStyle =
    "QPushButton {"
    "  background: #152535;"
    "  border: 1px solid #203040;"
    "  border-radius: 3px;"
    "  color: #00b4d8;"
    "  font-size: 11px;"
    "  padding: 3px 8px;"
    "}"
    "QPushButton:hover { background: #1d3045; }";

// Binding-status banner — persistent one-line indicator at the top of each
// VaxChannelCard. Colour encodes state: green = native HAL active, blue =
// BYO cable bound, amber = nothing bound (Windows case).
[[maybe_unused]] static const char* kStatusNativeStyle =
    "QLabel {"
    "  background: #0f2a1a;"
    "  border: 1px solid #2a6a3a;"
    "  border-radius: 3px;"
    "  color: #00ff88;"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "  padding: 3px 8px;"
    "}";

static const char* kStatusBYOStyle =
    "QLabel {"
    "  background: #0f2030;"
    "  border: 1px solid #2a5a8a;"
    "  border-radius: 3px;"
    "  color: #00b4d8;"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "  padding: 3px 8px;"
    "}";

static const char* kStatusUnboundStyle =
    "QLabel {"
    "  background: #2a1a00;"
    "  border: 1px solid #b87300;"
    "  border-radius: 3px;"
    "  color: #e8a030;"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "  padding: 3px 8px;"
    "}";

// Label builder for the disabled "native (bound automatically)" info
// row shown at the top of the Auto-detect menu on Mac/Linux when the
// platform-native HAL/pipe bridge is live. Exposed via a static
// VaxChannelCard::nativeHalLabelForCableForTest seam for unit coverage.
QString nativeHalLabelForCableImpl(const DetectedCable& cable)
{
    return QStringLiteral(
               "\u25ba  %1 \u00b7 NereusSDR \u00b7 native (bound automatically)")
        .arg(cable.deviceName);
}

} // namespace

QString VaxChannelCard::nativeHalLabelForCable(const DetectedCable& cable)
{
    return nativeHalLabelForCableImpl(cable);
}

// ---------------------------------------------------------------------------
// VaxChannelCard
// ---------------------------------------------------------------------------
VaxChannelCard::VaxChannelCard(int channel, QWidget* parent)
    : QGroupBox(QStringLiteral("VAX %1").arg(channel), parent)
    , m_channel(channel)
    , m_prefix(QStringLiteral("audio/Vax%1").arg(channel))
{
    setStyleSheet(QLatin1String(kGroupStyle));

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 16, 8, 8);
    outerLayout->setSpacing(6);

    // Persistent binding-status banner — always-visible one-liner telling
    // the user what this channel is currently routed through. Populated
    // in updateBindingStatus(), which is driven off currentDeviceName().
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setTextFormat(Qt::PlainText);
    outerLayout->addWidget(m_statusLabel);

    // Inner DeviceCard (7-row form + enable checkbox in title).
    m_deviceCard = new DeviceCard(m_prefix, DeviceCard::Role::Output,
                                  /*enableCheckbox=*/true, this);
    outerLayout->addWidget(m_deviceCard);

    // Amber "override — no consumer" badge (hidden by default; shown on
    // Mac/Linux when bound to non-native with consumerCount == 0).
    m_badgeLabel = new QLabel(QStringLiteral("override — no consumer"), this);
    m_badgeLabel->setStyleSheet(QLatin1String(kBadgeStyle));
    m_badgeLabel->setToolTip(QStringLiteral(
        "A non-native device is bound but no app is reading from it. "
        "If this is unintentional, pick the default platform-native "
        "HAL device."));
    m_badgeLabel->setVisible(false);
    m_badgeLabel->setWordWrap(false);
    outerLayout->addWidget(m_badgeLabel);

    // Auto-detect button (initially visible; hidden when a device is bound).
    m_autoDetectBtn = new QPushButton(QStringLiteral("Auto-detect\u2026"), this);
    m_autoDetectBtn->setStyleSheet(QLatin1String(kAutoDetectStyle));
    m_autoDetectBtn->setAutoDefault(false);
    m_autoDetectBtn->setDefault(false);
    outerLayout->addWidget(m_autoDetectBtn);

    // Wire inner card signals.
    connect(m_deviceCard, &DeviceCard::configChanged,
            this, &VaxChannelCard::onInnerConfigChanged);
    connect(m_deviceCard, &DeviceCard::enabledChanged,
            this, &VaxChannelCard::onInnerEnabledChanged);
    connect(m_autoDetectBtn, &QPushButton::clicked,
            this, &VaxChannelCard::onAutoDetectClicked);

    // Populate the status banner with the card's initial state so it reads
    // correctly even if loadFromSettings() isn't invoked (tests, previews).
    updateBadge();
}

void VaxChannelCard::loadFromSettings()
{
    m_deviceCard->loadFromSettings();
    updateBadge();
}

void VaxChannelCard::updateNegotiatedPill(const AudioDeviceConfig& negotiated,
                                          const QString& errorString)
{
    m_deviceCard->updateNegotiatedPill(negotiated, errorString);
    updateBadge();
}

QString VaxChannelCard::currentDeviceName() const
{
    // Primary source: live combo selection in the DeviceCard (reflects what
    // PortAudio enumerated). When a saved device is not in the enumerated list
    // (e.g. the cable is connected on another machine, or PortAudio returns no
    // devices in the test environment), fall back to reading AppSettings so the
    // badge and auto-detect button stay in sync with what was actually saved.
    const QString fromCard = m_deviceCard->currentConfig().deviceName;
    if (!fromCard.isEmpty()) {
        return fromCard;
    }
    return AppSettings::instance()
               .value(m_prefix + QStringLiteral("/DeviceName"), QString())
               .toString();
}

bool VaxChannelCard::isChannelEnabled() const
{
    // Delegate to the DeviceCard's checkbox live state so mid-edit callers
    // see the current toggle value rather than the last-flushed AppSettings
    // value. DeviceCard::isCheckboxEnabled() returns true for always-on cards
    // (no checkbox), which is the correct semantic here since VaxChannelCards
    // always have an enable checkbox (enableCheckbox=true in the ctor).
    //
    // Named isChannelEnabled() to avoid shadowing QWidget::isEnabled() (I2).
    return m_deviceCard->isCheckboxEnabled();
}

void VaxChannelCard::applyAutoDetectBinding(const QString& deviceName)
{
    // 1) Build config: take whatever the card's combos currently say for all
    //    other fields, then overwrite the device name with the picked cable.
    AudioDeviceConfig cfg = m_deviceCard->currentConfig();
    cfg.deviceName = deviceName;

    // 2) Persist all 10 fields to AppSettings under audio/Vax<N>/.
    cfg.saveToSettings(m_prefix);
    AppSettings::instance().save();

    // 3) Refresh the DeviceCard combos from the now-persisted settings so the
    //    device combo, negotiated pill, and badge all reflect the new state
    //    without requiring a manual reload.
    m_deviceCard->loadFromSettings();

    // 4) Emit to engine (original configChanged path).
    emit configChanged(m_channel, cfg);

    // 5) Update badge + auto-detect button visibility.
    updateBadge();
}

void VaxChannelCard::clearBinding()
{
    // 1) Build an empty config (all 10 fields reset to defaults).
    const AudioDeviceConfig empty;

    // 2) Persist the empty config, wiping all 10 AppSettings fields.
    empty.saveToSettings(m_prefix);
    AppSettings::instance().save();

    // 3) Refresh the DeviceCard UI from the now-empty settings.
    m_deviceCard->loadFromSettings();

    // 4) Tear down the engine bus: empty deviceName tells AudioEngine to close.
    emit configChanged(m_channel, empty);
    emit enabledChanged(m_channel, false);

    // 5) Update badge + auto-detect button visibility.
    updateBadge();
}

void VaxChannelCard::onInnerConfigChanged(AudioDeviceConfig cfg)
{
    updateBadge();
    emit configChanged(m_channel, cfg);
}

void VaxChannelCard::onInnerEnabledChanged(bool on)
{
    // Refresh the banner so the amber "Disabled" / green "Bound" read
    // flips in lockstep with the checkbox. Without this the user could
    // uncheck Enabled and still see a stale green "Bound: Native HAL"
    // even though AudioEngine::setVaxEnabled(false) has closed the bus.
    updateBadge();
    emit enabledChanged(m_channel, on);
}

void VaxChannelCard::setBusOpen(bool open)
{
    if (m_busOpen == open) {
        return;
    }
    m_busOpen = open;
    updateBadge();
}

void VaxChannelCard::updateBadge()
{
    // Auto-detect button: show when no device is bound (name empty).
    const QString deviceName = currentDeviceName();
    const bool hasDevice = !deviceName.isEmpty();
    m_autoDetectBtn->setVisible(!hasDevice);

    // Persistent binding-status banner — answers "what is this channel
    // routed through?" at a glance. State machine:
    //   - enabled=false                  → amber "Disabled"
    //   - busOpen=false + empty (Mac/Lx) → amber "Native HAL unavailable"
    //   - busOpen=false + BYO            → amber "Bus failed to open"
    //   - busOpen=true  + empty (Mac/Lx) → green "Bound: Native HAL · …"
    //   - busOpen=true  + BYO            → blue  "Bound: <name>"
    //   - Windows empty                  → amber "Not bound — pick a cable"
    // enabled+busOpen are tracked separately because setVaxEnabled(false)
    // closes the bus (busOpen=false) but leaves m_engine's accounting
    // intact, while a makeVaxBus() failure leaves the slot null without
    // the user ever unchecking anything.
    if (m_statusLabel) {
        const bool enabled = isChannelEnabled();
        if (!enabled) {
            m_statusLabel->setStyleSheet(QLatin1String(kStatusUnboundStyle));
            m_statusLabel->setText(QStringLiteral(
                "\u26a0  Disabled \u2014 enable to route audio"));
            m_statusLabel->setToolTip(QStringLiteral(
                "The VAX channel's Enabled checkbox is off. Check it to "
                "open the audio bus and route receiver audio through this "
                "slot."));
        } else {
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
            const QString nativeName =
                QStringLiteral("NereusSDR VAX %1").arg(m_channel);
            if (hasDevice) {
                // BYO override path.
                if (!m_busOpen) {
                    m_statusLabel->setStyleSheet(
                        QLatin1String(kStatusUnboundStyle));
                    m_statusLabel->setText(
                        QStringLiteral("\u26a0  Bus failed to open: %1")
                            .arg(deviceName));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "NereusSDR tried to open the selected 3rd-party "
                        "virtual cable but the PortAudio stream failed. "
                        "The device may be busy, unplugged, or the "
                        "Driver API / sample-rate combo may be "
                        "unsupported. Clear the Device picker to fall "
                        "back to the native HAL/pipe bridge."));
                } else {
                    m_statusLabel->setStyleSheet(
                        QLatin1String(kStatusBYOStyle));
                    m_statusLabel->setText(
                        QStringLiteral("\u2713  Bound: %1").arg(deviceName));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "VAX channel is routed through the selected "
                        "3rd-party virtual cable (BYO override). Clear "
                        "the Device picker to fall back to the native "
                        "HAL/pipe bridge."));
                }
            } else {
                // Native HAL / pipe bridge path.
                if (!m_busOpen) {
                    m_statusLabel->setStyleSheet(
                        QLatin1String(kStatusUnboundStyle));
#  if defined(Q_OS_MAC)
                    m_statusLabel->setText(QStringLiteral(
                        "\u26a0  Native HAL unavailable \u2014 reinstall "
                        "NereusSDR"));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "NereusSDR could not open the bundled CoreAudio "
                        "HAL plugin's shared-memory bridge. Reinstall "
                        "NereusSDR, or unblock NereusSDRVAX.driver in "
                        "System Settings \u2192 Privacy & Security."));
#  else
                    m_statusLabel->setText(QStringLiteral(
                        "\u26a0  Native PipeWire bridge unavailable"));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "NereusSDR could not create a PipeWire "
                        "pipe-source for this VAX slot. Check that "
                        "PipeWire is running and that pactl is "
                        "available on PATH."));
#  endif
                } else {
                    m_statusLabel->setStyleSheet(
                        QLatin1String(kStatusNativeStyle));
#  if defined(Q_OS_MAC)
                    m_statusLabel->setText(
                        QStringLiteral(
                            "\u2713  Bound: Native HAL \u00b7 %1")
                            .arg(nativeName));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "VAX channel is routed through the bundled "
                        "CoreAudio HAL plugin. Consumer apps (WSJT-X, "
                        "FLDIGI, etc.) can select \"%1\" as their "
                        "audio input device.")
                            .arg(nativeName));
#  else
                    m_statusLabel->setText(
                        QStringLiteral(
                            "\u2713  Bound: Native (PipeWire) \u00b7 %1")
                            .arg(nativeName));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "VAX channel is routed through a native "
                        "PipeWire pipe source. Consumer apps can select "
                        "\"%1\" as their audio input device.")
                            .arg(nativeName));
#  endif
                }
            }
#else  // Q_OS_WIN
            if (hasDevice) {
                if (!m_busOpen) {
                    m_statusLabel->setStyleSheet(
                        QLatin1String(kStatusUnboundStyle));
                    m_statusLabel->setText(
                        QStringLiteral("\u26a0  Bus failed to open: %1")
                            .arg(deviceName));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "NereusSDR tried to open the selected virtual "
                        "cable but the PortAudio stream failed. The "
                        "device may be busy, unplugged, or the Driver "
                        "API / sample-rate combo may be unsupported."));
                } else {
                    m_statusLabel->setStyleSheet(
                        QLatin1String(kStatusBYOStyle));
                    m_statusLabel->setText(
                        QStringLiteral("\u2713  Bound: %1").arg(deviceName));
                    m_statusLabel->setToolTip(QStringLiteral(
                        "VAX channel is routed through the selected "
                        "virtual cable."));
                }
            } else {
                m_statusLabel->setStyleSheet(
                    QLatin1String(kStatusUnboundStyle));
                m_statusLabel->setText(QStringLiteral(
                    "\u26a0  Not bound \u2014 pick a virtual cable"));
                m_statusLabel->setToolTip(QStringLiteral(
                    "Windows has no built-in virtual audio cable. "
                    "Install VB-CABLE, Voicemeeter, or VAC and pick it "
                    "in the Device dropdown to enable this VAX channel."));
            }
#endif
        }
    }

#if defined(Q_OS_WIN)
    // Windows has no native HAL fallback — empty deviceName on setVaxConfig
    // resolves to PortAudio's platform default, which on a box without a
    // virtual cable is the speakers device (raw pre-master-volume bleed).
    // Gate the Enabled checkbox until the user picks a real device.  On
    // Mac/Linux the engine falls back to the native HAL bus automatically
    // (AudioEngine::setVaxConfig), so no gate is needed there.
    m_deviceCard->setEnableAllowed(hasDevice);
#endif

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    // Amber badge: show only when bound to non-native and consumerCount == 0.
    // The native device name contains "NereusSDR" (reserved for the NereusSDR
    // HAL plugin). If the device is native, badge is always suppressed.
    if (hasDevice) {
        const bool isNative = deviceName.contains(QStringLiteral("NereusSDR"),
                                                   Qt::CaseInsensitive);
        const bool badgeOn = !isNative
            && (VirtualCableDetector::consumerCount(deviceName) == 0);
        m_badgeLabel->setVisible(badgeOn);
    } else {
        m_badgeLabel->setVisible(false);
    }
#else
    m_badgeLabel->setVisible(false);
#endif
}

void VaxChannelCard::onAutoDetectClicked()
{
    // Gather current assignments (all 4 channels) to detect already-assigned
    // cables and report "→ VAX N" in the menu.
    QMap<QString, int> assignedDeviceToChannel;
    for (int ch = 1; ch <= 4; ++ch) {
        const QString key = QStringLiteral("audio/Vax%1/DeviceName").arg(ch);
        const QString dev = AppSettings::instance()
                                .value(key, QString()).toString();
        if (!dev.isEmpty()) {
            assignedDeviceToChannel[dev] = ch;
        }
    }

    // Scan virtual cables (or use injected test vector if the seam is active).
#ifdef NEREUS_BUILD_TESTS
    const QVector<DetectedCable> cables =
        m_useTestCables ? m_testCables : VirtualCableDetector::scan();
#else
    const QVector<DetectedCable> cables = VirtualCableDetector::scan();
#endif

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #0f0f1a; color: #c8d8e8; border: 1px solid #203040; }"
        "QMenu::item:selected { background: #203040; }"
        "QMenu::item:disabled { color: #506070; }");

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    // Surface the platform-native HAL/pipe VAX devices as disabled info
    // rows. Binding is automatic (AudioEngine::makeVaxBus sets up the shm
    // bridge at start()) so these rows are not selectable — they exist so
    // users can confirm the native plugin is alive. Absent when the plugin
    // is not installed; the honest empty menu that follows is informative.
    int nativeHalCount = 0;
    for (const DetectedCable& cable : cables) {
        if (cable.product != VirtualCableProduct::NereusSdrVax) {
            continue;
        }
        // HAL plugin publishes VAX N as input-only on macOS; Linux pipe
        // module analog is also input-only. Accept either side to future-
        // proof against a dual-sided native bridge.
        QAction* act = menu.addAction(nativeHalLabelForCable(cable));
        act->setEnabled(false);
        ++nativeHalCount;
    }
    if (nativeHalCount > 0) {
        menu.addSeparator();
    }
#endif

    if (cables.isEmpty()) {
        // No cables case.
        QAction* noCablesAct = menu.addAction(
            QStringLiteral("No virtual cables detected"));
        noCablesAct->setEnabled(false);

        menu.addSeparator();

        QAction* installAct = menu.addAction(
            QStringLiteral("Install virtual cables\u2026"));
        connect(installAct, &QAction::triggered, this, []() {
            // Open generic VB-Audio landing page as default.
            QDesktopServices::openUrl(
                QUrl(VirtualCableDetector::installUrl(
                    VirtualCableProduct::VbCable)));
        });
    } else {
        // Build one entry per detected cable (output/render side only —
        // those are the cables that a consumer app reads from).
        bool hasAny = false;
        for (const DetectedCable& cable : cables) {
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
            // Native HAL/pipe entries were already surfaced above as info
            // rows. Don't re-offer them as BYO targets: the HAL plugin
            // publishes input-only devices on Mac, so setVaxConfig →
            // makeBus(capture=false) can't open them via PortAudio.
            if (cable.product == VirtualCableProduct::NereusSdrVax) {
                continue;
            }
#endif
            if (cable.isInput) {
                continue; // skip capture side
            }
            hasAny = true;

            const int alreadyAssignedToChannel =
                assignedDeviceToChannel.value(cable.deviceName, 0);
            const bool alreadyAssigned = (alreadyAssignedToChannel > 0)
                && (alreadyAssignedToChannel != m_channel);

            // Addendum §2.3: format is "► deviceName · vendor".
            // Spec §2.3 also calls for a sample-rate subtitle; deferred until
            // DetectedCable carries sampleRate (tracked by
            // TODO(sub-phase-12-menu-sample-rate)).
            const QString vendor =
                VirtualCableDetector::vendorDisplayName(cable.product);
            QString label = QStringLiteral("\u25ba  ") + cable.deviceName;
            if (!vendor.isEmpty()) {
                label += QStringLiteral(" \u00b7 ") + vendor;
            }
            if (alreadyAssigned) {
                label += QStringLiteral("  \u2192 VAX %1")
                             .arg(alreadyAssignedToChannel);
            }

            QAction* act = menu.addAction(label);

            if (alreadyAssigned) {
                const int srcChannel    = alreadyAssignedToChannel;
                const QString devName   = cable.deviceName;
                connect(act, &QAction::triggered, this,
                        [this, devName, srcChannel]() {
                    // Reassign confirm modal.
                    QMessageBox confirm(this);
                    confirm.setWindowTitle(QStringLiteral("Reassign cable?"));
                    confirm.setText(
                        QStringLiteral("Reassign %1 from VAX %2 to VAX %3?"
                                       " VAX %2 will become unassigned.")
                            .arg(devName)
                            .arg(srcChannel)
                            .arg(m_channel));
                    confirm.setStandardButtons(
                        QMessageBox::Ok | QMessageBox::Cancel);
                    confirm.setDefaultButton(QMessageBox::Cancel);
                    if (confirm.exec() != QMessageBox::Ok) {
                        return;
                    }

                    // C3: Full source teardown — wipe all 10 AppSettings fields,
                    // refresh the source card's UI, and close the engine bus.
                    // Walk up the widget hierarchy to find the AudioVaxPage that
                    // owns both cards (VaxChannelCard → container → scrollArea
                    // viewport → scrollArea → AudioVaxPage).
                    AudioVaxPage* page = nullptr;
                    for (QObject* p = parent(); p; p = p->parent()) {
                        if (auto* vp = qobject_cast<AudioVaxPage*>(p)) {
                            page = vp;
                            break;
                        }
                    }
                    if (page) {
                        VaxChannelCard* srcCard = page->channelCard(srcChannel);
                        if (srcCard) {
                            srcCard->clearBinding();
                        }
                    }

                    // C1+C2: Persist, refresh UI, emit to engine.
                    applyAutoDetectBinding(devName);
                });
            } else {
                const QString devName = cable.deviceName;
                connect(act, &QAction::triggered, this,
                        [this, devName]() {
                    // C1+C2: Persist, refresh UI, emit to engine.
                    applyAutoDetectBinding(devName);
                });
            }
        }

        if (!hasAny) {
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
            if (nativeHalCount > 0) {
                // Native HAL/pipe is already surfaced above. No 3rd-party
                // BYO cables are installed — hint that this is optional.
                QAction* hintAct = menu.addAction(
                    QStringLiteral(
                        "No 3rd-party cables available (BYO override optional)"));
                hintAct->setEnabled(false);
            } else {
                QAction* noCablesAct = menu.addAction(
                    QStringLiteral("No output-side virtual cables detected"));
                noCablesAct->setEnabled(false);
            }
#else
            // Only input-side cables found; surface a message.
            QAction* noCablesAct = menu.addAction(
                QStringLiteral("No output-side virtual cables detected"));
            noCablesAct->setEnabled(false);
#endif
        }
    }

    // Show menu anchored to the button (addendum §2.3: inline QMenu::exec
    // at button position). Qt handles ↑/↓ / Enter / Esc natively.
    menu.exec(m_autoDetectBtn->mapToGlobal(
        QPoint(0, m_autoDetectBtn->height())));
}

// ---------------------------------------------------------------------------
// AudioVaxPage
// ---------------------------------------------------------------------------
AudioVaxPage::AudioVaxPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("VAX"), model, parent)
    , m_engine(model ? model->audioEngine() : nullptr)
{
    buildPage();
    wirePillFeedback();
}

void AudioVaxPage::buildPage()
{
    // SetupPage base already wraps contentLayout() in a QScrollArea with a
    // trailing addStretch(1) — insert widgets before that stretch so the
    // content stacks from the top of the viewport (pattern from
    // SetupPage::addSection).
    auto insertBeforeStretch = [this](QWidget* w) {
        const int stretchIndex = contentLayout()->count() - 1;
        contentLayout()->insertWidget(stretchIndex, w);
    };

    // Section header.
    auto* headerLabel = new QLabel(
        QStringLiteral("Virtual Audio eXchange — channel bindings"), this);
    headerLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #8aa8c0; font-size: 12px; }"));
    insertBeforeStretch(headerLabel);

    // Four VAX channel cards (1–4).
    //
    // Addendum §2.2 calls for default-on-Mac/Linux to bind to the native HAL
    // plugin entry named "NereusSDR VAX N (native)". That plugin does not yet
    // exist; VirtualCableProduct::NereusSdrVax is reserved for it. Until the
    // plugin lands, Mac/Linux cards with an unset audio/Vax<N>/DeviceName fall
    // through to PortAudio's platform default. The "override — no consumer"
    // badge correctly stays quiescent in this state (consumerCount stub returns
    // 1, and without the named native entry there is no non-native to override
    // from).
    // Tracked by TODO(sub-phase-12-native-hal-default).
    m_channelCards.reserve(4);
    for (int ch = 1; ch <= 4; ++ch) {
        auto* card = new VaxChannelCard(ch, this);
        card->loadFromSettings();
        m_channelCards.append(card);
        insertBeforeStretch(card);

        // Wire configChanged to AudioEngine.
        if (m_engine) {
            // Seed the card with the engine's current bus-open state so the
            // banner reflects reality from the first paint (green bound /
            // amber "Native HAL unavailable" / amber "Bus failed to open").
            card->setBusOpen(m_engine->isVaxBusOpen(ch));

            connect(card, &VaxChannelCard::configChanged, this,
                    [this](int channel, AudioDeviceConfig cfg) {
                m_engine->setVaxConfig(channel, cfg);
                // setVaxConfig may have torn down + rebuilt the slot's bus
                // (BYO path) or fallen back to the native bridge (empty
                // deviceName). Reflect the post-op open state immediately
                // so the banner doesn't lag.
                if (auto* c = channelCard(channel)) {
                    c->setBusOpen(m_engine->isVaxBusOpen(channel));
                }
            });
            connect(card, &VaxChannelCard::enabledChanged, this,
                    [this](int channel, bool on) {
                m_engine->setVaxEnabled(channel, on);
                // setVaxEnabled(false) closes the bus; setVaxEnabled(true)
                // re-mints the native or BYO bus. Push the resulting state
                // back onto the card so the green/amber banner matches.
                if (auto* c = channelCard(channel)) {
                    c->setBusOpen(m_engine->isVaxBusOpen(channel));
                }
            });
        }
    }

    // TX row — informational; actual TX → VAX routing lands in Phase 3M.
    auto* txGroup = new QGroupBox(QStringLiteral("TX Monitor"), this);
    txGroup->setStyleSheet(QLatin1String(kGroupStyle));
    auto* txLayout = new QVBoxLayout(txGroup);
    auto* txLabel = new QLabel(
        QStringLiteral("TX → VAX routing is configured in the Transmit section. "
                       "Phase 3M (SendIqToVax / TxMonitorToVax) will add "
                       "per-band override here."),
        txGroup);
    txLabel->setStyleSheet(QStringLiteral("QLabel { color: #607080; font-size: 11px; }"));
    txLabel->setWordWrap(true);
    txLayout->addWidget(txLabel);
    insertBeforeStretch(txGroup);
}

void AudioVaxPage::wirePillFeedback()
{
    if (!m_engine) {
        return;
    }

    // Connect AudioEngine::vaxConfigChanged back to each card's pill and
    // banner. vaxConfigChanged is also emitted on the resetAudioSettings
    // path (factory-reset wipe + native-bus re-mint), so the banner has
    // to re-read isVaxBusOpen here too.
    connect(m_engine, &AudioEngine::vaxConfigChanged, this,
            [this](int channel, AudioDeviceConfig cfg) {
        const int idx = channel - 1;
        if (idx >= 0 && idx < m_channelCards.size()) {
            m_channelCards[idx]->updateNegotiatedPill(cfg);
            m_channelCards[idx]->setBusOpen(
                m_engine->isVaxBusOpen(channel));
        }
    });
}

} // namespace NereusSDR
