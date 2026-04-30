// src/gui/widgets/StatusBadge.h
#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QHBoxLayout;

namespace NereusSDR {

// StatusBadge — small badge with icon prefix + label, color-coded by variant.
// Used in the redesigned status bar (RX dashboard + right-side strip) and
// emits clicked / rightClicked so the host wires actions per design spec
// docs/architecture/2026-04-30-shell-chrome-redesign-design.md §Affordances.
class StatusBadge : public QWidget {
    Q_OBJECT

public:
    enum class Variant {
        Info,    // blue (#5fa8ff)   — type/mode badges (USB, AGC-S)
        On,      // green (#5fff8a)  — active toggle (NR2, NB, ANF, SQL)
        Off,     // dim grey (#404858) — inactive toggle (rendered only when
                 //                       host opts to show the off state)
        Warn,    // yellow (#ffd700) — degraded (jitter, high CPU)
        Tx,      // solid red (#ff6060) — TX MOX badge
    };

    explicit StatusBadge(QWidget* parent = nullptr);

    // The icon character (e.g. "~", "⨎", "⌁"). Single-character glyphs only.
    void setIcon(const QString& icon);
    // Label text (e.g. "USB", "2.4k", "NR2").
    void setLabel(const QString& label);
    void setVariant(Variant v);

    QString icon() const noexcept { return m_icon; }
    QString label() const noexcept { return m_label; }
    Variant variant() const noexcept { return m_variant; }

signals:
    void clicked();
    void rightClicked(const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void applyStyle();

    QString  m_icon;
    QString  m_label;
    Variant  m_variant{Variant::Info};
    QLabel*  m_iconLabel{nullptr};
    QLabel*  m_textLabel{nullptr};
};

} // namespace NereusSDR
