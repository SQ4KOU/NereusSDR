// src/gui/widgets/StatusBadge.cpp
#include "StatusBadge.h"

#include "gui/styles/WorkspacePreview.h"
#include <QColor>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace NereusSDR {

namespace {

// "rgba(r,g,b,a)" with 0..255 channels, as applyStyle() writes them.
QColor parseRgba(const QString& rgba)
{
    const int open = rgba.indexOf(QLatin1Char('('));
    const int close = rgba.lastIndexOf(QLatin1Char(')'));
    const QStringList parts = rgba.mid(open + 1, close - open - 1).split(QLatin1Char(','));
    if (open < 0 || close < open || parts.size() != 4) {
        return {};
    }
    return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt(),
                  parts[3].trimmed().toInt());
}

} // namespace

StatusBadge::StatusBadge(QWidget* parent) : QWidget(parent)
{
    auto* hbox = new QHBoxLayout(this);
    // 2026-04-30 — vertical padding bumped 1 → 2 px per side to give the
    // larger 12-px font room. Combined with the font-size bump in
    // applyStyle() this lifts badge height ~18 → ~22-24 px so the new
    // RxDashboard stacked-pair mode (BadgePair) is readable without
    // squinting.
    hbox->setContentsMargins(6, 2, 6, 2);
    hbox->setSpacing(3);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName(QStringLiteral("StatusBadge_Icon"));
    // Minimum / Fixed: don't shrink below the icon glyph's natural width.
    m_iconLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    hbox->addWidget(m_iconLabel);

    m_textLabel = new QLabel(this);
    m_textLabel->setObjectName(QStringLiteral("StatusBadge_Text"));
    // Critical: default Preferred lets QHBoxLayout clip "USB" → "U" when
    // the parent dashboard is constrained. Minimum / Fixed locks the label
    // at its natural text width so the full label always shows.
    m_textLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    hbox->addWidget(m_textLabel);

    setAttribute(Qt::WA_StyledBackground, true);
    // R-R3-21: the hand cursor only where a click does something
    // (setClickable); every badge used to show it with nowhere to go.

    // Size policy: claim sizeHint() worth of horizontal space so a parent
    // QHBoxLayout under pressure can't squeeze the badge below its content.
    // Without this, multi-char labels like "USB" / "2.4k" / "AGC-S" clip
    // to single chars when the dashboard is constrained on the status bar.
    // Vertical: Fixed so the badge stays at its natural 18 px height.
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    applyStyle();
}

void StatusBadge::setIcon(const QString& icon)
{
    if (m_icon == icon) { return; }
    m_icon = icon;
    // Glyph and SVG are mutually exclusive — setting one clears the other.
    if (!m_svgIcon.isEmpty()) {
        m_svgIcon.clear();
        m_iconLabel->setPixmap(QPixmap());
    }
    m_iconLabel->setText(icon);
    m_iconLabel->updateGeometry();
    recomputeMinimumWidth();
    updateGeometry();
}

void StatusBadge::setSvgIcon(const QString& resourcePath)
{
    if (m_svgIcon == resourcePath) { return; }
    m_svgIcon = resourcePath;
    // Glyph and SVG are mutually exclusive — setting one clears the other.
    if (!m_icon.isEmpty()) {
        m_icon.clear();
        m_iconLabel->setText(QString());
    }
    renderSvgIcon();
    m_iconLabel->updateGeometry();
    recomputeMinimumWidth();
    updateGeometry();
}

void StatusBadge::setLabel(const QString& label)
{
    if (m_label == label) { return; }
    m_label = label;
    m_textLabel->setText(label);
    recomputeMinimumWidth();
    m_textLabel->updateGeometry();
    updateGeometry();
}

void StatusBadge::setVariant(Variant v)
{
    if (m_variant == v) { return; }
    m_variant = v;
    applyStyle();
    // Re-tint the SVG icon (if any) — the foreground color just changed.
    // Glyph icons inherit color from the QLabel stylesheet which applyStyle
    // updated, so they don't need a re-render path.
    if (!m_svgIcon.isEmpty()) {
        renderSvgIcon();
    }
}

void StatusBadge::recomputeMinimumWidth()
{
    // Compute minimum width from font metrics for the text label and
    // QLabel sizeHint for the icon (covers both glyph text and SVG
    // pixmap paths). Without recomputing on every label/icon change,
    // QSizePolicy::Minimum locks at the construction-time sizeHint
    // (empty content → 0 px) and the badge clips to single-char on
    // first paint. This is the same fix as ebe9030 §2.
    // Padding budget: 6 left + 6 right margin + 3 spacing (between
    // icon and text, only if both present) + slack.
    const QFontMetrics fm(m_textLabel->font());
    const int textWidth = fm.horizontalAdvance(m_label);
    const int iconHint  = m_iconLabel->sizeHint().width();
    const int iconWidth = iconHint > 0 ? iconHint + 3 : 0;
    setMinimumWidth(textWidth + iconWidth + 14);
}

QColor StatusBadge::variantForegroundColor() const
{
    // Mirror the colors used by applyStyle() so the SVG tint matches the
    // text color. If applyStyle() colors change, update both.
    // Workspace preview: the roles the stylesheet translator gives the
    // same literals as text (StylesheetColourTable), so tint and text
    // still match. statusColour() gives the indicator tones, and
    // applyStyle() writes the same colours with the switch on.
    using WorkspacePreview::statusColour;
    switch (m_variant) {
        case Variant::Info: return statusColour(QColor(QStringLiteral("#5fa8ff")), "accent");
        case Variant::On:   return statusColour(QColor(QStringLiteral("#5fff8a")), "ok");
        case Variant::Off:  return statusColour(QColor(QStringLiteral("#404858")), "text-inactive");
        case Variant::Warn: return statusColour(QColor(QStringLiteral("#ffd700")), "measured");
        case Variant::Tx:   return statusColour(QColor(QStringLiteral("#ff6060")), "tx");
    }
    return statusColour(QColor(QStringLiteral("#5fa8ff")), "accent");
}

void StatusBadge::renderSvgIcon()
{
    if (m_svgIcon.isEmpty()) {
        m_iconLabel->setPixmap(QPixmap());
        return;
    }

    // Render at 14 logical px per side — matches the visual weight of
    // the 12 px text font without crowding the badge vertically. Scale
    // by devicePixelRatio so HiDPI displays get a crisp render.
    constexpr int kIconLogicalPx = 14;
    const qreal dpr = m_iconLabel->devicePixelRatioF();
    const int physicalSize = qRound(kIconLogicalPx * dpr);

    QSvgRenderer renderer(m_svgIcon);
    if (!renderer.isValid()) {
        m_iconLabel->setPixmap(QPixmap());
        return;
    }

    QImage img(physicalSize, physicalSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        renderer.render(&p, QRectF(0, 0, physicalSize, physicalSize));
    }

    // Tint pass: SourceIn keeps the alpha channel from the SVG render
    // and replaces RGB with the variant foreground color. The SVG's
    // own fill/stroke colors are therefore irrelevant — we only care
    // about the shape (alpha mask).
    {
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(img.rect(), variantForegroundColor());
    }

    QPixmap pix = QPixmap::fromImage(img);
    pix.setDevicePixelRatio(dpr);
    m_iconLabel->setPixmap(pix);
}

void StatusBadge::setClickable(bool clickable)
{
    m_clickable = clickable;
    if (clickable) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
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
    const char* role = "accent";
    switch (m_variant) {
        case Variant::Info:
            fg = QStringLiteral("#5fa8ff");
            bg = QStringLiteral("rgba(95,168,255,26)");   // 0.10 alpha
            role = "accent";
            break;
        case Variant::On:
            fg = QStringLiteral("#5fff8a");
            bg = QStringLiteral("rgba(95,255,138,26)");
            role = "ok";
            break;
        case Variant::Off:
            fg = QStringLiteral("#404858");
            bg = QStringLiteral("rgba(64,72,88,46)");      // 0.18 alpha
            role = "text-inactive";
            break;
        case Variant::Warn:
            fg = QStringLiteral("#ffd700");
            bg = QStringLiteral("rgba(255,215,0,30)");
            role = "measured";
            break;
        case Variant::Tx:
            fg = QStringLiteral("#ff6060");
            bg = QStringLiteral("rgba(255,96,96,51)");     // 0.20 alpha
            role = "tx";
            break;
    }

    // Workspace preview: the badge writes the indicator tones itself and
    // keeps the stylesheet pass (which would map today's literals to the
    // muted semantic roles) out of its subtree. The switch off leaves the
    // sheet as it always was.
    if (WorkspacePreview::enabled()) {
        const QColor text = WorkspacePreview::statusColour(QColor(fg), role);
        const QColor legacyFill = parseRgba(bg);
        const QColor fill = WorkspacePreview::statusColour(legacyFill, role);
        fg = text.name();
        bg = QStringLiteral("rgba(%1,%2,%3,%4)")
                 .arg(fill.red()).arg(fill.green()).arg(fill.blue()).arg(fill.alpha());
        setProperty(WorkspacePreview::kSkipProperty, true);
    } else if (property(WorkspacePreview::kSkipProperty).isValid()) {
        setProperty(WorkspacePreview::kSkipProperty, false);
    }

    // Font-size bumped 10 → 12 px (2026-04-30) so the badge text reads
    // cleanly when stacked into a vertical pair by RxDashboard's medium-
    // width layout. The vertical padding bump lives in the constructor.
    setStyleSheet(QStringLiteral(
        "NereusSDR--StatusBadge { background: %1; border-radius: 3px; }"
        "QLabel { color: %2; font-family: 'SF Mono', Menlo, monospace;"
        " font-size: 12px; font-weight: 600; line-height: 1.4; }"
    ).arg(bg, fg));
}

} // namespace NereusSDR
