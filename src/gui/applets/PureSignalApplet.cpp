// =================================================================
// src/gui/applets/PureSignalApplet.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/PSForm.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-18 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//                 Layout ports Thetis PSForm.cs (PureSignal feedback/correction controls). All controls NYI — wired in later phase (3M-4).
//   2026-05-06 — Phase 3M-4 Task 13: replaced NyiOverlay::markNyi calls with
//                 live wiring to the PureSignal coordinator (Task 7).  Save /
//                 Restore use QFileDialog with default folder
//                 ~/.config/NereusSDR/PureSignal/.  Right-click on every
//                 control emits openPureSignalDialogRequested.  J.J. Boyd
//                 (KG4VCF), with AI-assisted source-first protocol via
//                 Anthropic Claude Code.
// =================================================================

/*  PSForm.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2000-2025 Original authors
Copyright (C) 2020-2025 Richard Samphire MW0LGE

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

mw0lge@grange-lane.co.uk
*/
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

#include "PureSignalApplet.h"

#include "core/PureSignal.h"
#include "gui/HGauge.h"
#include "gui/StyleConstants.h"
#include "models/RadioModel.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <cmath>

namespace NereusSDR {

namespace {

// LED active style — green-on-dark (mirrors PsaIndicatorWidget palette).
constexpr const char* kLedActiveStyle =
    "QLabel {"
    "  background: #20c060; border-radius: 4px;"
    "  color: #000000; font-size: 8px; font-weight: bold;"
    "  padding: 0px 2px;"
    "}";

// LED inactive style — desaturated dark grey (matches the original
// styling baked into PureSignalApplet's buildUI).
constexpr const char* kLedInactiveStyle =
    "QLabel {"
    "  background: #405060; border-radius: 4px;"
    "  color: #6080a0; font-size: 8px; font-weight: bold;"
    "  padding: 0px 2px;"
    "}";

} // namespace

PureSignalApplet::PureSignalApplet(RadioModel* model, QWidget* parent)
    : AppletWidget(model, parent)
{
    buildUI();
    wireRightClicks();

    // Wire the coordinator immediately if it already exists (test injection
    // path may set m_ps via RadioModel::pureSignal() before construction).
    // Otherwise wait for the late-bind signal.
    if (m_model) {
        if (PureSignal* ps = m_model->pureSignal()) {
            wireCoordinator(ps);
        }
        connect(m_model, &RadioModel::pureSignalCoordinatorReady, this,
                &PureSignalApplet::setPureSignal);
    }
}

void PureSignalApplet::setPureSignal(PureSignal* coordinator)
{
    if (m_ps == coordinator) {
        return;
    }
    if (m_ps) {
        disconnect(m_ps, nullptr, this, nullptr);
    }
    m_ps = coordinator;
    if (m_ps) {
        wireCoordinator(m_ps);
    } else {
        // Disconnected — reset displayed state to defaults so the applet
        // doesn't show stale FB / iteration counters.
        if (m_feedbackGauge)   { m_feedbackGauge->setValue(0.0); }
        if (m_correctionGauge) { m_correctionGauge->setValue(0.0); }
        if (m_iterations)      { m_iterations->setText(tr("Iterations: 0")); }
        if (m_feedbackDb)      { m_feedbackDb->setText(tr("Feedback: — dB")); }
        if (m_correctionDb)    { m_correctionDb->setText(tr("Correction: — dB")); }
        if (m_saveBtn)         { m_saveBtn->setEnabled(false); }
        for (QLabel* led : m_led) { setLedActive(led, false); }
        if (m_autoCalBtn) {
            QSignalBlocker blk(m_autoCalBtn);
            m_autoCalBtn->setChecked(false);
        }
    }
}

void PureSignalApplet::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    // Phase 3M-4 bench-fix: AppletPanelWidget::wrapWithTitleBar (AppletPanelWidget.cpp:155)
    // already prepends a host-side title bar from appletTitle().  Adding our own
    // appletTitleBar() here resulted in a double header.  Same-shape bug exists
    // in DiversityApplet / DigitalApplet / TunerApplet / CwxApplet / CatApplet /
    // DvkApplet — fix here is PureSignal-scoped; flag for follow-up sweep.

    auto* body = new QWidget(this);
    auto* vbox = new QVBoxLayout(body);
    vbox->setContentsMargins(4, 2, 4, 4);
    vbox->setSpacing(2);

    // --- Control 1+2: Calibrate + Auto-cal row ---
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        // Control 1: calibrate (non-toggle push button)
        m_calibrateBtn = styledButton(QStringLiteral("Calibrate"));
        m_calibrateBtn->setObjectName(QStringLiteral("PsAppletCalibrateBtn"));
        m_calibrateBtn->setToolTip(tr(
            "Run a single PureSignal calibration sweep. "
            "Right-click to open PureSignal..."));
        row->addWidget(m_calibrateBtn);

        // Control 2: auto-cal (green toggle)
        m_autoCalBtn = greenToggle(QStringLiteral("Auto"));
        m_autoCalBtn->setObjectName(QStringLiteral("PsAppletAutoCalBtn"));
        m_autoCalBtn->setCheckable(true);
        m_autoCalBtn->setToolTip(tr(
            "Toggle continuous PureSignal auto-calibration. "
            "Right-click to open PureSignal..."));
        row->addWidget(m_autoCalBtn);
        row->addStretch();

        vbox->addLayout(row);
    }

    // --- Control 3: Feedback level gauge (0-100, yellow@70, red@90) ---
    m_feedbackGauge = new HGauge(this);
    m_feedbackGauge->setObjectName(QStringLiteral("PsAppletFeedbackGauge"));
    m_feedbackGauge->setRange(0.0, 100.0);
    m_feedbackGauge->setYellowStart(70.0);
    m_feedbackGauge->setRedStart(90.0);
    m_feedbackGauge->setTitle(QStringLiteral("FB Level"));
    m_feedbackGauge->setToolTip(tr(
        "PureSignal feedback level. Right-click to open PureSignal..."));
    vbox->addWidget(m_feedbackGauge);

    // --- Control 4: Correction magnitude gauge (0-100, yellow@80, red@95) ---
    m_correctionGauge = new HGauge(this);
    m_correctionGauge->setObjectName(QStringLiteral("PsAppletCorrectionGauge"));
    m_correctionGauge->setRange(0.0, 100.0);
    m_correctionGauge->setYellowStart(80.0);
    m_correctionGauge->setRedStart(95.0);
    m_correctionGauge->setTitle(QStringLiteral("Correction"));
    m_correctionGauge->setToolTip(tr(
        "PureSignal correction magnitude. Right-click to open PureSignal..."));
    vbox->addWidget(m_correctionGauge);

    // --- Control 5+6+7: Save / Restore / Two-tone row ---
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        // Control 5: save coefficients
        m_saveBtn = styledButton(QStringLiteral("Save"));
        m_saveBtn->setObjectName(QStringLiteral("PsAppletSaveBtn"));
        m_saveBtn->setEnabled(false);  // gated on correctingChanged
        m_saveBtn->setToolTip(tr(
            "Save the current PureSignal corrections to a file. "
            "Right-click to open PureSignal..."));
        row->addWidget(m_saveBtn);

        // Control 6: restore coefficients
        m_restoreBtn = styledButton(QStringLiteral("Restore"));
        m_restoreBtn->setObjectName(QStringLiteral("PsAppletRestoreBtn"));
        m_restoreBtn->setToolTip(tr(
            "Load a previously saved PureSignal corrections file. "
            "Right-click to open PureSignal..."));
        row->addWidget(m_restoreBtn);

        // Control 7: two-tone test (green toggle)
        m_twoToneBtn = greenToggle(QStringLiteral("2-Tone"));
        m_twoToneBtn->setObjectName(QStringLiteral("PsAppletTwoToneBtn"));
        m_twoToneBtn->setCheckable(true);
        m_twoToneBtn->setToolTip(tr(
            "Inject a two-tone test signal for PureSignal calibration. "
            "Right-click to open PureSignal..."));
        row->addWidget(m_twoToneBtn);
        row->addStretch();

        vbox->addLayout(row);
    }

    // --- Control 8: Status LEDs row — "Cal", "Run", "Fbk" ---
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);

        const QString ledNames[3] = {
            QStringLiteral("Cal"),
            QStringLiteral("Run"),
            QStringLiteral("Fbk")
        };
        const QString ledObjectNames[3] = {
            QStringLiteral("PsAppletCalLed"),
            QStringLiteral("PsAppletRunLed"),
            QStringLiteral("PsAppletFbkLed")
        };

        for (int i = 0; i < 3; ++i) {
            m_led[i] = new QLabel(ledNames[i], this);
            m_led[i]->setObjectName(ledObjectNames[i]);
            m_led[i]->setFixedSize(24, 14);
            m_led[i]->setAlignment(Qt::AlignCenter);
            m_led[i]->setStyleSheet(QString::fromLatin1(kLedInactiveStyle));
            row->addWidget(m_led[i]);
        }
        row->addStretch();
        vbox->addLayout(row);
    }

    // --- Info readout labels ---
    m_iterations   = new QLabel(QStringLiteral("Iterations: 0"), this);
    m_iterations->setObjectName(QStringLiteral("PsAppletIterationsLabel"));
    m_feedbackDb   = new QLabel(QStringLiteral("Feedback: — dB"), this);
    m_feedbackDb->setObjectName(QStringLiteral("PsAppletFeedbackDbLabel"));
    m_correctionDb = new QLabel(QStringLiteral("Correction: — dB"), this);
    m_correctionDb->setObjectName(QStringLiteral("PsAppletCorrectionDbLabel"));

    const QString infoStyle = QStringLiteral(
        "QLabel { font-size: 10px; color: %1; }").arg(Style::kTextSecondary);
    for (QLabel* lbl : {m_iterations, m_feedbackDb, m_correctionDb}) {
        lbl->setStyleSheet(infoStyle);
        vbox->addWidget(lbl);
    }

    vbox->addStretch();
    root->addWidget(body);
}

void PureSignalApplet::setLedActive(QLabel* led, bool active)
{
    if (!led) {
        return;
    }
    led->setStyleSheet(QString::fromLatin1(active ? kLedActiveStyle
                                                  : kLedInactiveStyle));
}

void PureSignalApplet::setupRightClick(QWidget* widget)
{
    if (!widget) {
        return;
    }
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widget, &QWidget::customContextMenuRequested, this,
            [this](const QPoint&) {
        emit openPureSignalDialogRequested();
    });
}

void PureSignalApplet::wireRightClicks()
{
    // Right-click pattern unification — every control routes to PsForm.
    // Mirrors the Thetis right-click-to-associated-window pattern (e.g.
    // chkFWCATUBypass_MouseDown at console.cs:46149-46152 [v2.10.3.13]).
    // Wired unconditionally (not gated on coordinator) so the seam exists
    // even when no coordinator is bound — critical for both the test
    // harness and the pre-connect (no PureSignal yet) UX.
    setupRightClick(m_calibrateBtn);
    setupRightClick(m_autoCalBtn);
    setupRightClick(m_saveBtn);
    setupRightClick(m_restoreBtn);
    setupRightClick(m_twoToneBtn);
    setupRightClick(m_feedbackGauge);
    setupRightClick(m_correctionGauge);

    // Save / Restore left-click handlers — file-dialog opens are gated
    // inside the lambda on m_ps presence so they're safe pre-coordinator.
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ps) { return; }
        const QString defaultDir = QStandardPaths::writableLocation(
                QStandardPaths::AppConfigLocation)
            + QStringLiteral("/PureSignal/");
        QDir().mkpath(defaultDir);
        const QString filename = QFileDialog::getSaveFileName(
            this, tr("Save PureSignal corrections"), defaultDir,
            tr("PureSignal corrections (*.psk *.dat);;All files (*)"));
        if (!filename.isEmpty()) {
            m_ps->saveCorrections(filename);
        }
    });

    connect(m_restoreBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ps) { return; }
        const QString defaultDir = QStandardPaths::writableLocation(
                QStandardPaths::AppConfigLocation)
            + QStringLiteral("/PureSignal/");
        const QString filename = QFileDialog::getOpenFileName(
            this, tr("Restore PureSignal corrections"), defaultDir,
            tr("PureSignal corrections (*.psk *.dat);;All files (*)"));
        if (!filename.isEmpty()) {
            m_ps->restoreCorrections(filename);
        }
    });

    // Calibrate / Auto / Two-tone left-click handlers — guarded on m_ps
    // for the same reason.
    connect(m_calibrateBtn, &QPushButton::clicked, this, [this]() {
        if (m_ps) { m_ps->singleCalibrate(); }
    });
    connect(m_autoCalBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_ps) { m_ps->setAutoCalEnabled(on); }
    });
    connect(m_twoToneBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_ps) { m_ps->setTwoToneOn(on); }
    });
}

void PureSignalApplet::wireCoordinator(PureSignal* ps)
{
    if (!ps) { return; }

    m_ps = ps;

    // Auto echo back — model → UI only.  UI → model goes through the
    // m_autoCalBtn::toggled lambda above.  Echo loops are prevented by
    // the QSignalBlocker on the toggled→model side and by the
    // setAutoCalEnabled idempotency check on the model→UI side.
    connect(ps, &PureSignal::autoCalEnabledChanged, this, [this](bool on) {
        if (!m_autoCalBtn) { return; }
        QSignalBlocker block(m_autoCalBtn);
        m_autoCalBtn->setChecked(on);
    });

    // Feedback gauge ← feedbackLevelChanged (info[4] mapped to 0..100 from
    // raw 0..255 per PSForm.cs:1120-1122 [v2.10.3.13]).
    connect(ps, &PureSignal::feedbackLevelChanged, this, [this](int level) {
        if (m_feedbackGauge) {
            m_feedbackGauge->setValue(level * 100.0 / 255.0);
        }
        // Info label: dB-converted FB level (informational; ratio of raw FB
        // level to nominal max 255 mapped to dB).  Floor at -120 dB to
        // avoid log10(0) → -inf in the printf path.
        if (m_feedbackDb) {
            const double db = level > 0
                ? 20.0 * std::log10(static_cast<double>(level) / 255.0)
                : -120.0;
            m_feedbackDb->setText(tr("Feedback: %1 dB").arg(db, 0, 'f', 1));
        }
    });

    // Correction gauge ← correctionPeakChanged (TxChannel HW peak, 0..1
    // mapped to 0..100).  See PureSignal.cpp pollTimerTick [Task 13].
    connect(ps, &PureSignal::correctionPeakChanged, this,
            [this](double peak) {
        if (m_correctionGauge) {
            const double v = peak * 100.0;
            const double clamped = (v < 0.0) ? 0.0 : (v > 100.0 ? 100.0 : v);
            m_correctionGauge->setValue(clamped);
        }
        if (m_correctionDb) {
            const double db = peak > 0.0
                ? 20.0 * std::log10(peak)
                : -120.0;
            m_correctionDb->setText(tr("Correction: %1 dB").arg(db, 0, 'f', 1));
        }
    });

    // Save enabled state ← correctionsBeingAppliedChanged.  Mirrors PSForm.cs:
    // 574-590 btnPSSave gating [v2.10.3.13]:
    //   if (puresignal.CorrectionsBeingApplied) btnPSSave.Enabled = true;
    // Codex Fix D: route from correctionsBeingAppliedChanged (info[14]==1
    // predicate), NOT correctingChanged (FeedbackLevel > 90 predicate).
    connect(ps, &PureSignal::correctionsBeingAppliedChanged,
            m_saveBtn, &QPushButton::setEnabled);
    m_saveBtn->setEnabled(ps->correctionsBeingApplied());

    // LEDs:
    //   Cal LED ← calStateChanged: active during LSETUP(3)/LCOLLECT(4)/LCALC(6).
    //   Run LED ← calStateChanged: active during LSTAYON(8).
    //   Fbk LED ← feedbackActiveChanged.
    // Engine state values match Thetis PSForm.cs:1140-1151 EngineState enum
    // [v2.10.3.13].
    connect(ps, &PureSignal::calStateChanged, this, [this](int state) {
        const bool inCal = (state == 3 /*LSETUP*/)
                        || (state == 4 /*LCOLLECT*/)
                        || (state == 6 /*LCALC*/);
        const bool inRun = (state == 8 /*LSTAYON*/);
        setLedActive(m_led[0], inCal);
        setLedActive(m_led[1], inRun);
    });

    connect(ps, &PureSignal::feedbackActiveChanged, this, [this](bool active) {
        setLedActive(m_led[2], active);
    });

    // Iterations info label ← calibrationCountChanged (PSForm.cs:1103-1105
    // CalibrationCount [v2.10.3.13]).
    connect(ps, &PureSignal::calibrationCountChanged, this, [this](int n) {
        if (m_iterations) {
            m_iterations->setText(tr("Iterations: %1").arg(n));
        }
    });

    // Initial sync from current coordinator state.
    if (m_autoCalBtn) {
        QSignalBlocker block(m_autoCalBtn);
        m_autoCalBtn->setChecked(ps->isAutoCalEnabled());
    }
    if (m_iterations) {
        m_iterations->setText(tr("Iterations: %1").arg(ps->calibrationCount()));
    }
}

void PureSignalApplet::syncFromModel()
{
    // Coordinator drives state via signals connected in wireCoordinator();
    // explicit sync is rarely needed.  Provided for AppletWidget's interface
    // contract (called by the Container framework on visibility change).
    if (!m_ps) { return; }
    if (m_autoCalBtn) {
        QSignalBlocker block(m_autoCalBtn);
        m_autoCalBtn->setChecked(m_ps->isAutoCalEnabled());
    }
    if (m_saveBtn) {
        m_saveBtn->setEnabled(m_ps->correctionsBeingApplied());
    }
    if (m_iterations) {
        m_iterations->setText(
            tr("Iterations: %1").arg(m_ps->calibrationCount()));
    }
}

} // namespace NereusSDR
