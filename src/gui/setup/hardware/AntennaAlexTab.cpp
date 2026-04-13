// AntennaAlexTab.cpp
//
// Source: Thetis Setup.cs InitAlexAntTables() lines 13393-13478.
// Per-band RX antenna: radAlexR1_*, radAlexR2_*, radAlexR3_* — 11 HF bands.
// Per-band TX antenna: radAlexT1_*, radAlexT2_*, radAlexT3_* — same bands.
// Per-band RX-only checkboxes: chkAlex*R1/R2/XV.
// Bypass/relay checkboxes from Setup.cs:2892-2898:
//   chkRxOutOnTx, chkEXT1OutOnTx, chkEXT2OutOnTx, chkHFTRRelay,
//   chkBPF2Gnd, chkEnableXVTRHF.
//
// NereusSDR uses QTableWidget (14 rows × 3 cols) for both RX and TX antenna
// grids; the "exclusive" radio-button behaviour is enforced in onItemChanged.
// The 14-band set (Band::Count = 14) matches the NereusSDR Band enum.

#include "AntennaAlexTab.h"

#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include "models/Band.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace NereusSDR {

// ── Band row labels (14-band) ─────────────────────────────────────────────────
static QStringList bandRowLabels()
{
    QStringList labels;
    labels.reserve(static_cast<int>(Band::Count));
    for (int i = 0; i < static_cast<int>(Band::Count); ++i) {
        labels << bandLabel(static_cast<Band>(i));
    }
    return labels;
}

// ── Constructor ───────────────────────────────────────────────────────────────

AntennaAlexTab::AntennaAlexTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);
    outerLayout->setSpacing(6);

    // ── RX Antenna per-band grid ──────────────────────────────────────────────
    // Source: Thetis Setup.cs:13412-13423 — _AlexRxAntButtons[band][ant]
    // ANT1 / ANT2 / ANT3 per band. Exclusive selection per row.
    auto* rxGroup = new QGroupBox(tr("RX Antenna per Band"), this);
    auto* rxVBox  = new QVBoxLayout(rxGroup);

    m_rxAntTable = new QTableWidget(static_cast<int>(Band::Count), 3, rxGroup);
    m_rxAntTable->setHorizontalHeaderLabels({tr("ANT1"), tr("ANT2"), tr("ANT3")});
    m_rxAntTable->setVerticalHeaderLabels(bandRowLabels());
    m_rxAntTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_rxAntTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_rxAntTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rxAntTable->setSelectionMode(QAbstractItemView::NoSelection);

    for (int row = 0; row < static_cast<int>(Band::Count); ++row) {
        for (int col = 0; col < 3; ++col) {
            auto* item = new QTableWidgetItem();
            // First column (ANT1) checked by default — mirrors Thetis default
            item->setCheckState(col == 0 ? Qt::Checked : Qt::Unchecked);
            item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            m_rxAntTable->setItem(row, col, item);
        }
    }
    rxVBox->addWidget(m_rxAntTable);
    outerLayout->addWidget(rxGroup);

    // ── TX Antenna per-band grid ──────────────────────────────────────────────
    // Source: Thetis Setup.cs:13425-13436 — _AlexTxAntButtons[band][ant]
    auto* txGroup = new QGroupBox(tr("TX Antenna per Band"), this);
    auto* txVBox  = new QVBoxLayout(txGroup);

    m_txAntTable = new QTableWidget(static_cast<int>(Band::Count), 3, txGroup);
    m_txAntTable->setHorizontalHeaderLabels({tr("ANT1"), tr("ANT2"), tr("ANT3")});
    m_txAntTable->setVerticalHeaderLabels(bandRowLabels());
    m_txAntTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_txAntTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_txAntTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_txAntTable->setSelectionMode(QAbstractItemView::NoSelection);

    for (int row = 0; row < static_cast<int>(Band::Count); ++row) {
        for (int col = 0; col < 3; ++col) {
            auto* item = new QTableWidgetItem();
            item->setCheckState(col == 0 ? Qt::Checked : Qt::Unchecked);
            item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            m_txAntTable->setItem(row, col, item);
        }
    }
    txVBox->addWidget(m_txAntTable);
    outerLayout->addWidget(txGroup);

    // ── ALEX bypass / relay options ───────────────────────────────────────────
    // Source: Thetis Setup.cs:2892-2898 — chkRxOutOnTx, chkEXT1OutOnTx,
    //   chkEXT2OutOnTx, chkHFTRRelay, chkBPF2Gnd, chkEnableXVTRHF
    auto* optGroup = new QGroupBox(tr("ALEX Relay Options"), this);
    auto* optLayout = new QVBoxLayout(optGroup);

    m_rxOutOnTx   = new QCheckBox(tr("RX Out active during TX"), optGroup);
    m_ext1OutOnTx = new QCheckBox(tr("EXT1 Out active during TX"), optGroup);
    m_ext2OutOnTx = new QCheckBox(tr("EXT2 Out active during TX"), optGroup);
    m_hfTrRelay   = new QCheckBox(tr("HF T/R relay present"), optGroup);
    m_bpf2Gnd     = new QCheckBox(tr("BPF2 ground on TX"), optGroup);
    m_enableXvtrHf = new QCheckBox(tr("Enable XVTR HF path"), optGroup);
    m_enableXvtrHf->setToolTip(
        tr("Routes the XVTR signal through the HF path. "
           "Source: Thetis chkEnableXVTRHF (Setup.cs:18639)."));

    for (QCheckBox* chk : {m_rxOutOnTx, m_ext1OutOnTx, m_ext2OutOnTx,
                           m_hfTrRelay, m_bpf2Gnd, m_enableXvtrHf}) {
        optLayout->addWidget(chk);
    }
    outerLayout->addWidget(optGroup);
    outerLayout->addStretch();

    // ── Wire signals ─────────────────────────────────────────────────────────
    connect(m_rxAntTable, &QTableWidget::itemChanged,
            this, &AntennaAlexTab::onRxAntTableChanged);
    connect(m_txAntTable, &QTableWidget::itemChanged,
            this, &AntennaAlexTab::onTxAntTableChanged);

    auto wireCheckBox = [this](QCheckBox* chk, const QString& key) {
        connect(chk, &QCheckBox::toggled, this, [this, key](bool checked) {
            emit settingChanged(key, checked);
        });
    };
    wireCheckBox(m_rxOutOnTx,    QStringLiteral("antennaAlex/rxOutOnTx"));
    wireCheckBox(m_ext1OutOnTx,  QStringLiteral("antennaAlex/ext1OutOnTx"));
    wireCheckBox(m_ext2OutOnTx,  QStringLiteral("antennaAlex/ext2OutOnTx"));
    wireCheckBox(m_hfTrRelay,    QStringLiteral("antennaAlex/hfTrRelay"));
    wireCheckBox(m_bpf2Gnd,      QStringLiteral("antennaAlex/bpf2Gnd"));
    wireCheckBox(m_enableXvtrHf, QStringLiteral("antennaAlex/enableXvtrHf"));
}

// ── populate ──────────────────────────────────────────────────────────────────

void AntennaAlexTab::populate(const RadioInfo& /*info*/, const BoardCapabilities& caps)
{
    // If the board has no ALEX, HardwarePage hides this whole tab.
    // Defensive: if antennaInputCount < 3, disable extra antenna columns.
    // Source: Thetis Setup.cs:6185-6246 — radAlexR/T column enable per caps.
    for (int col = 0; col < 3; ++col) {
        bool colEnabled = (col < caps.antennaInputCount);
        for (int row = 0; row < static_cast<int>(Band::Count); ++row) {
            if (auto* item = m_rxAntTable->item(row, col)) {
                Qt::ItemFlags flags = Qt::ItemIsEnabled;
                if (colEnabled) {
                    flags |= Qt::ItemIsUserCheckable;
                } else {
                    item->setCheckState(Qt::Unchecked);
                }
                item->setFlags(flags);
            }
            if (auto* item = m_txAntTable->item(row, col)) {
                Qt::ItemFlags flags = Qt::ItemIsEnabled;
                if (colEnabled) {
                    flags |= Qt::ItemIsUserCheckable;
                } else {
                    item->setCheckState(Qt::Unchecked);
                }
                item->setFlags(flags);
            }
        }
    }
    // XVTR path only relevant when board has an XVTR jack
    m_enableXvtrHf->setEnabled(caps.xvtrJackCount > 0);
}

// ── private slots ─────────────────────────────────────────────────────────────

// Enforce exclusive selection per row (radio-button semantics).
// Source: Thetis _AlexRxAntButtons are RadioButtonTS — mutually exclusive per band.
void AntennaAlexTab::onRxAntTableChanged(QTableWidgetItem* changed)
{
    if (m_updating) { return; }
    if (!changed || changed->checkState() != Qt::Checked) { return; }

    int row = changed->row();
    int col = changed->column();

    m_updating = true;
    for (int c = 0; c < m_rxAntTable->columnCount(); ++c) {
        if (c == col) { continue; }
        if (auto* item = m_rxAntTable->item(row, c)) {
            item->setCheckState(Qt::Unchecked);
        }
    }
    m_updating = false;

    Band band = static_cast<Band>(row);
    emit settingChanged(
        QStringLiteral("antennaAlex/rxAnt[%1]").arg(bandKeyName(band)),
        col + 1 /* 1-based ANT number */);
}

void AntennaAlexTab::onTxAntTableChanged(QTableWidgetItem* changed)
{
    if (m_updating) { return; }
    if (!changed || changed->checkState() != Qt::Checked) { return; }

    int row = changed->row();
    int col = changed->column();

    m_updating = true;
    for (int c = 0; c < m_txAntTable->columnCount(); ++c) {
        if (c == col) { continue; }
        if (auto* item = m_txAntTable->item(row, c)) {
            item->setCheckState(Qt::Unchecked);
        }
    }
    m_updating = false;

    Band band = static_cast<Band>(row);
    emit settingChanged(
        QStringLiteral("antennaAlex/txAnt[%1]").arg(bandKeyName(band)),
        col + 1 /* 1-based ANT number */);
}

// ── restoreSettings ───────────────────────────────────────────────────────────

void AntennaAlexTab::restoreSettings(const QMap<QString, QVariant>& settings)
{
    // Restore simple boolean checkboxes. Block signals to avoid re-emitting
    // settingChanged during restore.
    struct CheckEntry { const char* key; QCheckBox* widget; };
    const CheckEntry entries[] = {
        { "rxOutOnTx",    m_rxOutOnTx    },
        { "ext1OutOnTx",  m_ext1OutOnTx  },
        { "ext2OutOnTx",  m_ext2OutOnTx  },
        { "hfTrRelay",    m_hfTrRelay    },
        { "bpf2Gnd",      m_bpf2Gnd      },
        { "enableXvtrHf", m_enableXvtrHf },
    };
    for (const auto& e : entries) {
        auto it = settings.constFind(QString::fromLatin1(e.key));
        if (it != settings.constEnd()) {
            QSignalBlocker blocker(e.widget);
            e.widget->setChecked(it.value().toBool());
        }
    }

    // Restore per-band RX antenna selections
    {
        QSignalBlocker blocker(m_rxAntTable);
        for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
            if (!it.key().startsWith(QStringLiteral("rxAnt["))) { continue; }
            // key format: "rxAnt[<bandKeyName>]"  value = 1-based ANT number
            const QString inner = it.key().mid(6, it.key().size() - 7); // strip "rxAnt[" and "]"
            for (int row = 0; row < m_rxAntTable->rowCount(); ++row) {
                Band band = static_cast<Band>(row);
                if (bandKeyName(band) != inner) { continue; }
                const int antNum = it.value().toInt(); // 1-based
                m_updating = true;
                for (int col = 0; col < m_rxAntTable->columnCount(); ++col) {
                    if (auto* item = m_rxAntTable->item(row, col)) {
                        item->setCheckState((col + 1 == antNum) ? Qt::Checked : Qt::Unchecked);
                    }
                }
                m_updating = false;
                break;
            }
        }
    }
}

} // namespace NereusSDR
