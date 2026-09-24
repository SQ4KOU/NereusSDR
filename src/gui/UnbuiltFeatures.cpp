// no-port-check: NereusSDR-original. See header.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/UnbuiltFeatures.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. See header for full
// Modification history (NereusSDR).
// =================================================================

#include "gui/UnbuiltFeatures.h"

#include <QAction>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLayout>
#include <QSet>
#include <QWidget>

namespace NereusSDR::UnbuiltFeatures {

namespace {

using F = UnbuiltFeature;

// Features a test has marked built. Empty in the app: nothing on the list
// is built.
QSet<int>& builtForTest()
{
    static QSet<int> built;
    return built;
}

// The layout that holds `w` directly, searched from its parent widget's
// top layout down through nested layouts.
QLayout* findHolder(QLayout* layout, const QWidget* w)
{
    if (layout == nullptr) { return nullptr; }
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (item == nullptr) { continue; }
        if (item->widget() == w) { return layout; }
        if (QLayout* found = findHolder(item->layout(), w)) { return found; }
    }
    return nullptr;
}

QLayout* holderOf(const QWidget* w)
{
    const QWidget* parent = w->parentWidget();
    return parent != nullptr ? findHolder(parent->layout(), w) : nullptr;
}

void hideAllIn(QLayout* layout)
{
    if (layout == nullptr) { return; }
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (item == nullptr) { continue; }
        if (QWidget* w = item->widget()) {
            w->setVisible(false);
        } else if (QLayout* inner = item->layout()) {
            hideAllIn(inner);
        }
    }
}

// Hide every item on the grid row that holds `index`.
void hideGridRow(QGridLayout* grid, int index)
{
    int row = 0;
    int col = 0;
    int rowSpan = 0;
    int colSpan = 0;
    grid->getItemPosition(index, &row, &col, &rowSpan, &colSpan);
    for (int i = 0; i < grid->count(); ++i) {
        int r = 0;
        int c = 0;
        int rs = 0;
        int cs = 0;
        grid->getItemPosition(i, &r, &c, &rs, &cs);
        if (r != row) { continue; }
        QLayoutItem* item = grid->itemAt(i);
        if (QWidget* w = item->widget()) {
            w->setVisible(false);
        } else {
            hideAllIn(item->layout());
        }
    }
}

// The index of `unit` (a widget, or a nested layout) in `layout`.
int indexIn(QLayout* layout, const QWidget* widget, const QLayout* inner)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (widget != nullptr && item->widget() == widget) { return i; }
        if (inner != nullptr && item->layout() == inner) { return i; }
    }
    return -1;
}

} // namespace

const QList<Entry>& all()
{
    static const QList<Entry> entries = {
        {F::DisplayMode, QStringLiteral("display-mode"),
         QStringLiteral("View > Display Mode")},
        {F::UiScale, QStringLiteral("ui-scale"),
         QStringLiteral("View > UI Scale; Setup > General > UI Scale & Theme")},
        {F::MinimalMode, QStringLiteral("minimal-mode"),
         QStringLiteral("View > Minimal Mode; Setup > Appearance > Collapsible Display")},
        {F::Keyboard, QStringLiteral("keyboard"),
         QStringLiteral("View > Keyboard Shortcuts; Setup > Keyboard > Shortcuts")},
        {F::Equalizer, QStringLiteral("equalizer"),
         QStringLiteral("DSP > Equalizer")},
        {F::Transverters, QStringLiteral("transverters"),
         QStringLiteral("Radio > Transverters; Band > VHF; Hardware > XVTR; OC Outputs VHF tab")},
        {F::BandStack, QStringLiteral("band-stack"),
         QStringLiteral("Band > Band Stacking; the band stack dots on the status bar")},
        {F::Cwx, QStringLiteral("cwx"),
         QStringLiteral("Tools > CWX; Phone/CW applet CW page; the CW keyer settings; "
                        "CWX on the status bar")},
        {F::Memories, QStringLiteral("memories"),
         QStringLiteral("Tools > Memory Manager; the Memories spot option")},
        {F::Cat, QStringLiteral("cat"),
         QStringLiteral("Tools > CAT Control; Setup > Serial Ports and TCP/IP CAT; "
                        "CAT on the status bar")},
        {F::Midi, QStringLiteral("midi"),
         QStringLiteral("Tools > MIDI Mapping; Setup > MIDI Control")},
        {F::Help, QStringLiteral("help"),
         QStringLiteral("Help > Getting Started, Help, Data Modes")},
        {F::Acc, QStringLiteral("acc"),
         QStringLiteral("Phone/CW applet +ACC and the microphone source ACC item")},
        {F::PhoneMon, QStringLiteral("phone-mon"),
         QStringLiteral("Phone/CW applet MON and its level")},
        {F::FmPage, QStringLiteral("fm-page"),
         QStringLiteral("Phone/CW applet FM page")},
        {F::RfkitTune, QStringLiteral("rfkit-tune"),
         QStringLiteral("RF-Kit applet TUNE and BYPASS")},
        {F::ContainerButtons, QStringLiteral("macro-buttons"),
         QStringLiteral("Container function buttons with nothing behind them: RX2, SUB RX, "
                        "Pan Swap, AVG, and the macro buttons")},
        {F::VariableFilters, QStringLiteral("variable-filters"),
         QStringLiteral("The variable filter slots: the container filter Var1 and Var2 buttons")},
        {F::AntennaRxTxSplit, QStringLiteral("antenna-rx-tx"),
         QStringLiteral("The antenna box's receive/transmit split: the container antenna Rx/Tx "
                        "button")},
        {F::Voice, QStringLiteral("voice"),
         QStringLiteral("Voice record and play container control; VFO flag record and play; "
                        "DVK on the status bar")},
        {F::Fdx, QStringLiteral("fdx"),
         QStringLiteral("Status bar FDX")},
        {F::Navigation, QStringLiteral("navigation"),
         QStringLiteral("Setup > General > Navigation")},
        {F::Sam, QStringLiteral("sam"),
         QStringLiteral("Setup > DSP > AM/SAM synchronous AM options")},
        {F::Skins, QStringLiteral("skins"),
         QStringLiteral("Setup > Appearance > Skins")},
        {F::TxProfilesLeaf, QStringLiteral("tx-profiles-leaf"),
         QStringLiteral("Setup > Transmit > TX Profiles")},
        {F::BandwidthMonitor, QStringLiteral("bw-monitor"),
         QStringLiteral("Setup > Hardware > Bandwidth Monitor")},
        {F::Hl2SecondI2cBus, QStringLiteral("hl2-i2c"),
         QStringLiteral("Setup > Hardware > HL2 Options second I2C bus")},
        {F::ConnectionHistory, QStringLiteral("conn-history"),
         QStringLiteral("Setup > Diagnostics > Connection Quality 60 second history")},
        {F::Logging, QStringLiteral("logging"),
         QStringLiteral("Setup > Diagnostics > Logging: log level, open and clear, categories")},
        {F::SignalGenerator, QStringLiteral("siggen"),
         QStringLiteral("Setup > Diagnostics > Signal Generator and Hardware Tests")},
        {F::LocalNetworkStats, QStringLiteral("netdiag-local"),
         QStringLiteral("Network Diagnostics with a local radio: Jitter, Loss, Gap")},
        {F::DspRate, QStringLiteral("dsp-rate"),
         QStringLiteral("Setup > Audio > Advanced DSP rate and block size")},
        {F::IqToVax, QStringLiteral("iq-to-vax"),
         QStringLiteral("Setup > Audio > Advanced Send IQ to VAX, TX Monitor to VAX")},
        {F::MuteVaxDuringTx, QStringLiteral("mute-vax-tx"),
         QStringLiteral("Setup > Audio > Advanced Mute VAX during transmit on another slice")},
        {F::AntennaConflict, QStringLiteral("ant-conflict"),
         QStringLiteral("Setup > Hardware > Antenna conflict policy")},
        {F::OcExtras, QStringLiteral("oc-extras"),
         QStringLiteral("Setup > Hardware > OC Outputs hot switching, USB BCD, external PA")},
        {F::MultimeterHolds, QStringLiteral("multimeter"),
         QStringLiteral("Setup > Multimeter peak hold, text hold, digital delay, history")},
        {F::WsjtxFilters, QStringLiteral("wsjtx-filters"),
         QStringLiteral("Spot Hub WSJT-X filters (three)")},
        {F::RbnRateLimit, QStringLiteral("rbn-rate"),
         QStringLiteral("Spot Hub RBN rate limit")},
        {F::FreeDvToPsk, QStringLiteral("freedv-psk"),
         QStringLiteral("Spot Hub report FreeDV decodes to PSK Reporter")},
        {F::TciExtras, QStringLiteral("tci-extras"),
         QStringLiteral("TCI rate limit, CW to CWU, TX channel, sensor intervals, the three "
                        "RX2 VFO options, stream channels")},
        {F::SmallFilter, QStringLiteral("small-filter"),
         QStringLiteral("Setup > Appearance small filter display on the VFO flag")},
        {F::ApfParams, QStringLiteral("apf-params"),
         QStringLiteral("Setup > DSP > CW peak filter bandwidth and gain")},
        {F::AmSquelchTail, QStringLiteral("am-tail"),
         QStringLiteral("Setup > DSP > AM/SAM maximum squelch tail")},
        {F::FmDeviation, QStringLiteral("fm-dev"),
         QStringLiteral("Setup > DSP > FM deviation and de-emphasis")},
    };
    return entries;
}

QString key(UnbuiltFeature feature)
{
    for (const Entry& entry : all()) {
        if (entry.feature == feature) { return entry.key; }
    }
    return {};
}

bool isBuilt(UnbuiltFeature feature)
{
    return builtForTest().contains(static_cast<int>(feature));
}

void hideUnlessBuilt(QWidget* widget, UnbuiltFeature feature)
{
    if (widget == nullptr || isBuilt(feature)) { return; }
    if (auto* form = qobject_cast<QFormLayout*>(holderOf(widget))) {
        form->setRowVisible(widget, false);
        return;
    }
    widget->setVisible(false);
}

void hideRowUnlessBuilt(QWidget* control, UnbuiltFeature feature, QLayout* searchFrom)
{
    if (control == nullptr || isBuilt(feature)) { return; }
    QLayout* holder = searchFrom != nullptr ? findHolder(searchFrom, control)
                                            : holderOf(control);
    const QWidget* unitWidget = control;
    const QLayout* unitLayout = nullptr;
    while (holder != nullptr) {
        if (auto* form = qobject_cast<QFormLayout*>(holder)) {
            if (unitLayout != nullptr) {
                form->setRowVisible(const_cast<QLayout*>(unitLayout), false);
            } else {
                form->setRowVisible(const_cast<QWidget*>(unitWidget), false);
            }
            return;
        }
        if (auto* grid = qobject_cast<QGridLayout*>(holder)) {
            const int index = indexIn(grid, unitLayout ? nullptr : unitWidget, unitLayout);
            if (index >= 0) {
                hideGridRow(grid, index);
                return;
            }
            break;
        }
        auto* box = qobject_cast<QBoxLayout*>(holder);
        const bool horizontal = box != nullptr
            && (box->direction() == QBoxLayout::LeftToRight
                || box->direction() == QBoxLayout::RightToLeft);
        if (!horizontal) { break; }
        // A row layout. When it is itself a cell of a form or a grid, the
        // row's label sits beside it there: hide that whole row.
        auto* outer = qobject_cast<QLayout*>(holder->parent());
        if (outer != nullptr && (qobject_cast<QFormLayout*>(outer) != nullptr
                                 || qobject_cast<QGridLayout*>(outer) != nullptr)) {
            unitLayout = holder;
            unitWidget = nullptr;
            holder = outer;
            continue;
        }
        hideAllIn(holder);
        return;
    }
    control->setVisible(false);
}

void hideLayoutUnlessBuilt(QLayout* layout, UnbuiltFeature feature)
{
    if (layout == nullptr || isBuilt(feature)) { return; }
    hideAllIn(layout);
}

void hideUnlessBuilt(QAction* action, UnbuiltFeature feature)
{
    if (action == nullptr || isBuilt(feature)) { return; }
    action->setVisible(false);
}

void setBuiltForTest(UnbuiltFeature feature, bool built)
{
    if (built) {
        builtForTest().insert(static_cast<int>(feature));
    } else {
        builtForTest().remove(static_cast<int>(feature));
    }
}

void resetForTest()
{
    builtForTest().clear();
}

} // namespace NereusSDR::UnbuiltFeatures
