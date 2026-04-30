// src/gui/widgets/StatusBadge.cpp
#include "StatusBadge.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>

namespace NereusSDR {

StatusBadge::StatusBadge(QWidget* parent) : QWidget(parent)
{
    auto* hbox = new QHBoxLayout(this);
    hbox->setContentsMargins(6, 1, 6, 1);
    hbox->setSpacing(3);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName(QStringLiteral("StatusBadge_Icon"));
    hbox->addWidget(m_iconLabel);

    m_textLabel = new QLabel(this);
    m_textLabel->setObjectName(QStringLiteral("StatusBadge_Text"));
    hbox->addWidget(m_textLabel);

    setCursor(Qt::PointingHandCursor);
    applyStyle();
}

void StatusBadge::setIcon(const QString& icon)
{
    m_icon = icon;
    m_iconLabel->setText(icon);
}

void StatusBadge::setLabel(const QString& label)
{
    m_label = label;
    m_textLabel->setText(label);
}

void StatusBadge::setVariant(Variant v)
{
    if (m_variant == v) { return; }
    m_variant = v;
    applyStyle();
}

void StatusBadge::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    } else if (event->button() == Qt::RightButton) {
        emit rightClicked(event->globalPosition().toPoint());
    }
    QWidget::mousePressEvent(event);
}

void StatusBadge::applyStyle()
{
    QString fg, bg;
    switch (m_variant) {
        case Variant::Info:
            fg = QStringLiteral("#5fa8ff");
            bg = QStringLiteral("rgba(95,168,255,26)");   // 0.10 alpha
            break;
        case Variant::On:
            fg = QStringLiteral("#5fff8a");
            bg = QStringLiteral("rgba(95,255,138,26)");
            break;
        case Variant::Off:
            fg = QStringLiteral("#404858");
            bg = QStringLiteral("rgba(64,72,88,46)");      // 0.18 alpha
            break;
        case Variant::Warn:
            fg = QStringLiteral("#ffd700");
            bg = QStringLiteral("rgba(255,215,0,30)");
            break;
        case Variant::Tx:
            fg = QStringLiteral("#ff6060");
            bg = QStringLiteral("rgba(255,96,96,51)");     // 0.20 alpha
            break;
    }

    setStyleSheet(QStringLiteral(
        "NereusSDR--StatusBadge { background: %1; border-radius: 3px; }"
        "QLabel { color: %2; font-family: 'SF Mono', Menlo, monospace;"
        " font-size: 10px; font-weight: 600; line-height: 1.4; }"
    ).arg(bg, fg));
}

} // namespace NereusSDR
