// =================================================================
// src/gui/setup/DspOptionsPage.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/setup.cs
//   Project Files/Source/Console/setup.Designer.cs
//   original licence from Thetis source is included below
//
// Thetis version: v2.10.3.13 (git commit 501e3f5)
//
// Source-first verification (Task 4.1 Step 2):
//
//   Buffer size Items (verified from setup.Designer.cs:37848-37857 [v2.10.3.13]):
//     comboDSPPhoneRXBuf.Items = "64","128","256","512","1024"
//     comboDSPPhoneTXBuf.Items = same
//     comboDSPCWRXBuf.Items    = same (CW RX only — no TX buf combo in Thetis)
//     comboDSPDigRXBuf.Items   = same
//     comboDSPDigTXBuf.Items   = same
//     comboDSPFMRXBuf.Items    = same
//     comboDSPFMTXBuf.Items    = same
//   NereusSDR collapses RX/TX to per-mode; Task 4.2 fans out to both channels.
//
//   Filter size Items (verified from setup.Designer.cs:37596-37605 [v2.10.3.13]):
//     comboDSPPhoneRXFiltSize.Items = "1024","2048","4096","8192","16384"
//     (same for all mode/direction combos)
//
//   Filter type Items (verified from setup.Designer.cs:37347-37352 [v2.10.3.13]):
//     comboDSPPhoneRXFiltType.Items = "Linear Phase","Low Latency"
//     (same for all mode/direction combos; "Linear Phase" is index 0)
//     Note: CW has RX only — no comboDSPCWTXFiltType in Thetis.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
//                 Task 4.1: DspOptionsPage skeleton + 18 controls.
//                 Mirrors Thetis DSP Options tab (design Section 4A).
// =================================================================

//=================================================================
// setup.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to:
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Continual modifications Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
//=================================================================
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

#include "DspOptionsPage.h"

#include "core/AppSettings.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace NereusSDR {

namespace {

// ── Thetis-verbatim combo item lists ─────────────────────────────────────────
//
// Buffer size items — from Thetis setup.Designer.cs:37848-37857 [v2.10.3.13]
//   comboDSPPhoneRXBuf.Items = {"64","128","256","512","1024"}
//   toolTip: "Sets DSP internal Buffer Size -- larger yields sharper filters, more latency"
// Note: Thetis tops out at 1024; design doc said "64/128/256/512/1024/2048 (verify)" —
// Thetis source is authoritative; 2048 is NOT present.
const QStringList kBufferSizes = {"64", "128", "256", "512", "1024"};

// Filter size (taps) items — from Thetis setup.Designer.cs:37596-37605 [v2.10.3.13]
//   comboDSPPhoneRXFiltSize.Items = {"1024","2048","4096","8192","16384"}
//   toolTip: "Sets DSP internal Buffer Size -- larger yields sharper filters, more latency"
const QStringList kFilterSizes = {"1024", "2048", "4096", "8192", "16384"};

// Filter type items — from Thetis setup.Designer.cs:37347-37352 [v2.10.3.13]
//   comboDSPPhoneRXFiltType.Items = {"Linear Phase","Low Latency"}
//   toolTip: "Select 'Low Latency' (Minimum Phase) or 'Linear Phase' Filters"
// Note: "Linear Phase" is index 0 in Thetis (not "Low Latency" as in some docs).
const QStringList kFilterTypes = {"Linear Phase", "Low Latency"};

// ── Local helper functions ────────────────────────────────────────────────────

QComboBox* makeCombo(QWidget* parent, const QStringList& items)
{
    auto* combo = new QComboBox(parent);
    combo->addItems(items);
    return combo;
}

QLabel* makeWarningIcon(QWidget* parent)
{
    auto* lbl = new QLabel(parent);
    lbl->setText(QStringLiteral("⚠"));  // U+26A0 WARNING SIGN
    lbl->setStyleSheet(QStringLiteral("color: #FFC800; font-weight: bold;"));
    lbl->setVisible(false);
    return lbl;
}

void loadCombo(QComboBox* combo, const QString& key, const QString& def)
{
    const QString stored = AppSettings::instance().value(key, def).toString();
    const int idx = combo->findText(stored);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    }
}

void wireComboPersist(QComboBox* combo, const QString& key)
{
    QObject::connect(combo, &QComboBox::currentTextChanged,
        combo, [key](const QString& v) {
            AppSettings::instance().setValue(key, v);
        });
}

void loadCheck(QCheckBox* check, const QString& key, bool def)
{
    const QString stored =
        AppSettings::instance().value(key, def ? "True" : "False").toString();
    check->setChecked(stored == "True");
}

void wireCheckPersist(QCheckBox* check, const QString& key)
{
    QObject::connect(check, &QCheckBox::toggled,
        check, [key](bool v) {
            AppSettings::instance().setValue(key, v ? "True" : "False");
        });
}

}  // namespace

// ── Construction ──────────────────────────────────────────────────────────────

DspOptionsPage::DspOptionsPage(RadioModel* model, QWidget* parent)
    : SetupPage("Options", model, parent)
{
    buildUI();
}

// ── UI build ──────────────────────────────────────────────────────────────────

void DspOptionsPage::buildUI()
{
    // SetupPage provides a QVBoxLayout via contentLayout(); we add our groups
    // to it directly to match Thetis's tpDSPOptions tab layout (Section 4A).
    QVBoxLayout* layout = contentLayout();

    // =========================================================================
    // Group 1: Buffer Size (IQcomp)
    // From Thetis setup.Designer.cs grpDSPBufferSize [v2.10.3.13]:
    //   grpDSPBufferSize.Text = "Buffer Size (IQcomp)"
    //   Contains sub-groups: grpDSPBufPhone("SSB/AM"), grpDSPBufCW("CW"),
    //                        grpDSPBufDig("Digital"), grpDSPBufFM("FM")
    // NereusSDR collapses RX+TX to a single per-mode combo.
    // =========================================================================
    auto* bufGroup = new QGroupBox(tr("Buffer Size (IQcomp)"), this);
    auto* bufGrid  = new QGridLayout(bufGroup);
    bufGrid->setColumnStretch(1, 1);

    int row = 0;

    // warning icon placed in column 2 of first row only
    m_warnBufferSize = makeWarningIcon(bufGroup);

    auto addBufRow = [&](const QString& modeLabel, QComboBox*& combo,
                         const QString& key, const QString& def) {
        bufGrid->addWidget(new QLabel(modeLabel, bufGroup), row, 0);
        combo = makeCombo(bufGroup, kBufferSizes);
        // toolTip from Thetis: "Sets DSP internal Buffer Size -- larger yields sharper filters, more latency"
        combo->setToolTip(tr("Sets DSP internal buffer size — larger values yield sharper "
                             "filters but add latency."));
        bufGrid->addWidget(combo, row, 1);
        if (row == 0) {
            bufGrid->addWidget(m_warnBufferSize, row, 2);
        }
        loadCombo(combo, key, def);
        wireComboPersist(combo, key);
        row++;
    };

    // From Thetis grpDSPBufPhone.Text = "SSB/AM" [v2.10.3.13]
    // Default DisplayMember = "64" (Phone TX) / "64" (Phone RX); NereusSDR default 256.
    addBufRow(tr("Phone (SSB/AM)"), m_bufPhone, "DspOptionsBufferSizePhone", "256");
    // From Thetis grpDSPBufCW.Text = "CW" [v2.10.3.13]; CW is RX-only in Thetis buffer group
    addBufRow(tr("CW"),             m_bufCw,    "DspOptionsBufferSizeCw",    "256");
    // From Thetis grpDSPBufDig.Text = "Digital" [v2.10.3.13]
    addBufRow(tr("Digital"),        m_bufDig,   "DspOptionsBufferSizeDig",   "256");
    // From Thetis grpDSPBufFM.Text = "FM" [v2.10.3.13]
    addBufRow(tr("FM"),             m_bufFm,    "DspOptionsBufferSizeFm",    "256");

    layout->addWidget(bufGroup);

    // =========================================================================
    // Group 2: Filter Size (taps)
    // From Thetis setup.Designer.cs grpDSPFilterSize [v2.10.3.13]:
    //   grpDSPFilterSize (outer container), sub-groups per mode.
    //   comboDSPPhoneRXFiltSize.DisplayMember = "2048" (Phone RX default)
    // NereusSDR collapses RX+TX to per-mode.
    // =========================================================================
    auto* filtGroup = new QGroupBox(tr("Filter Size (taps)"), this);
    auto* filtGrid  = new QGridLayout(filtGroup);
    filtGrid->setColumnStretch(1, 1);

    row = 0;
    m_warnFilterSize = makeWarningIcon(filtGroup);

    auto addFiltRow = [&](const QString& modeLabel, QComboBox*& combo,
                          const QString& key, const QString& def) {
        filtGrid->addWidget(new QLabel(modeLabel, filtGroup), row, 0);
        combo = makeCombo(filtGroup, kFilterSizes);
        // toolTip from Thetis (same tooltip as buffer): "Sets DSP internal Buffer Size..."
        combo->setToolTip(tr("Sets the FIR filter length — larger values yield sharper "
                             "filter skirts but add CPU and latency."));
        filtGrid->addWidget(combo, row, 1);
        if (row == 0) {
            filtGrid->addWidget(m_warnFilterSize, row, 2);
        }
        loadCombo(combo, key, def);
        wireComboPersist(combo, key);
        row++;
    };

    // From Thetis grpDSPFiltSizePhone.Text = "SSB/AM" [v2.10.3.13]
    // comboDSPPhoneRXFiltSize.DisplayMember = "2048"; NereusSDR default "4096"
    addFiltRow(tr("Phone (SSB/AM)"), m_filtSizePhone, "DspOptionsFilterSizePhone", "4096");
    // From Thetis grpDSPFiltSizeCW.Text = "CW" [v2.10.3.13]
    addFiltRow(tr("CW"),             m_filtSizeCw,    "DspOptionsFilterSizeCw",    "4096");
    // From Thetis grpDSPFiltSizeDig.Text = "Digital" [v2.10.3.13]
    addFiltRow(tr("Digital"),        m_filtSizeDig,   "DspOptionsFilterSizeDig",   "4096");
    // From Thetis grpDSPFiltSizeFM.Text = "FM" [v2.10.3.13]
    addFiltRow(tr("FM"),             m_filtSizeFm,    "DspOptionsFilterSizeFm",    "4096");

    layout->addWidget(filtGroup);

    // =========================================================================
    // Group 3: Filter Type
    // From Thetis setup.Designer.cs grpDSPFiltTypePhone/CW/Dig/FM [v2.10.3.13]:
    //   comboDSPPhoneRXFiltType.Items = {"Linear Phase","Low Latency"}
    //   toolTip: "Select 'Low Latency' (Minimum Phase) or 'Linear Phase' Filters"
    //   CW has RX only — no comboDSPCWTXFiltType in Thetis.
    // =========================================================================
    auto* filtTypeGroup = new QGroupBox(tr("Filter Type"), this);
    auto* filtTypeGrid  = new QGridLayout(filtTypeGroup);
    filtTypeGrid->setColumnStretch(1, 1);

    row = 0;
    m_warnBufferType = makeWarningIcon(filtTypeGroup);

    auto addTypeRow = [&](const QString& rowLabel, QComboBox*& combo,
                          const QString& key, const QString& def) {
        filtTypeGrid->addWidget(new QLabel(rowLabel, filtTypeGroup), row, 0);
        combo = makeCombo(filtTypeGroup, kFilterTypes);
        // toolTip from Thetis: "Select 'Low Latency' (Minimum Phase) or 'Linear Phase' Filters"
        combo->setToolTip(tr("Select 'Low Latency' (minimum phase) or 'Linear Phase' filters. "
                             "Linear Phase has symmetric delay; Low Latency uses minimum-phase "
                             "design with less group delay."));
        filtTypeGrid->addWidget(combo, row, 1);
        if (row == 0) {
            filtTypeGrid->addWidget(m_warnBufferType, row, 2);
        }
        loadCombo(combo, key, def);
        wireComboPersist(combo, key);
        row++;
    };

    // From Thetis grpDSPFiltTypePhone — comboDSPPhoneRXFiltType / comboDSPPhoneTXFiltType [v2.10.3.13]
    addTypeRow(tr("Phone RX"), m_filtTypePhoneRx, "DspOptionsFilterTypePhoneRx", "Low Latency");
    addTypeRow(tr("Phone TX"), m_filtTypePhoneTx, "DspOptionsFilterTypePhoneTx", "Linear Phase");
    // From Thetis grpDSPFiltTypeCW — comboDSPCWRXFiltType only (no TX) [v2.10.3.13]
    addTypeRow(tr("CW RX"),    m_filtTypeCwRx,    "DspOptionsFilterTypeCwRx",    "Low Latency");
    // From Thetis grpDSPFiltTypeDig — comboDSPDigRXFiltType / comboDSPDigTXFiltType [v2.10.3.13]
    addTypeRow(tr("Digital RX"), m_filtTypeDigRx, "DspOptionsFilterTypeDigRx",   "Linear Phase");
    addTypeRow(tr("Digital TX"), m_filtTypeDigTx, "DspOptionsFilterTypeDigTx",   "Linear Phase");
    // From Thetis grpDSPFiltTypeFM — comboDSPFMRXFiltType / comboDSPFMTXFiltType [v2.10.3.13]
    addTypeRow(tr("FM RX"),    m_filtTypeFmRx,    "DspOptionsFilterTypeFmRx",    "Low Latency");
    addTypeRow(tr("FM TX"),    m_filtTypeFmTx,    "DspOptionsFilterTypeFmTx",    "Linear Phase");

    layout->addWidget(filtTypeGroup);

    // =========================================================================
    // Group 4: Filter Impulse Cache
    // Task 4.3 wires these to the WDSP impulse cache API.
    // =========================================================================
    auto* cacheGroup  = new QGroupBox(tr("Filter Impulse Cache"), this);
    auto* cacheLayout = new QVBoxLayout(cacheGroup);

    m_cacheImpulse = new QCheckBox(tr("Enable WDSP impulse caching"), cacheGroup);
    m_cacheImpulse->setToolTip(
        tr("Cache filter impulse responses in memory for faster channel rebuilds. "
           "Trades memory for first-rebuild latency."));

    m_cacheImpulseSaveRestore = new QCheckBox(
        tr("Persist impulse cache to disk between sessions"), cacheGroup);
    m_cacheImpulseSaveRestore->setToolTip(
        tr("Save the impulse cache to disk on shutdown and reload on next launch. "
           "Eliminates the first-rebuild cost after restarting NereusSDR."));

    loadCheck(m_cacheImpulse,            "DspOptionsCacheImpulse",            false);
    loadCheck(m_cacheImpulseSaveRestore, "DspOptionsCacheImpulseSaveRestore",  false);
    wireCheckPersist(m_cacheImpulse,            "DspOptionsCacheImpulse");
    wireCheckPersist(m_cacheImpulseSaveRestore, "DspOptionsCacheImpulseSaveRestore");

    cacheLayout->addWidget(m_cacheImpulse);
    cacheLayout->addWidget(m_cacheImpulseSaveRestore);

    layout->addWidget(cacheGroup);

    // =========================================================================
    // Standalone: High-resolution filter characteristics
    // Task 4.4 wires this to FilterDisplayItem rendering.
    // =========================================================================
    m_highResFilterChars = new QCheckBox(
        tr("High-resolution filter characteristics in filter graph"), this);
    m_highResFilterChars->setToolTip(
        tr("When enabled, the filter graph displays the actual computed FIR "
           "magnitude response. When disabled, a simplified box-shape passband "
           "is shown instead."));

    loadCheck(m_highResFilterChars, "DspOptionsHighResFilterCharacteristics", false);
    wireCheckPersist(m_highResFilterChars, "DspOptionsHighResFilterCharacteristics");

    layout->addWidget(m_highResFilterChars);

    // =========================================================================
    // Time-to-last-change readout
    // Task 4.6 subscribes to RadioModel::dspChangeMeasured(qint64).
    // Placeholder text shown until the first rebuild occurs.
    // =========================================================================
    m_timeToLastChangeLabel = new QLabel(tr("Time to last change: — (no change yet)"), this);
    m_timeToLastChangeLabel->setStyleSheet(QStringLiteral("color: #888;"));

    // Wire to RadioModel::dspChangeMeasured if model is available.
    // The signal is emitted by RadioModel::rebuildDsp() (Task 1.8) with the
    // elapsed milliseconds of the last WDSP channel rebuild.
    if (model()) {
        connect(model(), &RadioModel::dspChangeMeasured, this,
            [this](qint64 ms) {
                m_timeToLastChangeLabel->setText(
                    tr("Time to last change: %1 ms").arg(ms));
                m_timeToLastChangeLabel->setStyleSheet(
                    QStringLiteral("color: #c8d8e8;"));
            });
    }

    layout->addWidget(m_timeToLastChangeLabel);
}

}  // namespace NereusSDR
