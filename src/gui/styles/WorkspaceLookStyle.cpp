// =================================================================
// src/gui/styles/WorkspaceLookStyle.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See WorkspaceLookStyle.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-10 - Written by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#include "WorkspaceLookStyle.h"

#include "WorkspacePalette.h"
#include "WorkspacePreview.h"

#include <QApplication>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QStyleOptionButton>

namespace NereusSDR::Workspace {

namespace {

// The geometry Fusion gives a command button's outline (Qt 6.11
// QFusionStyle::drawPrimitive, PE_PanelButtonCommand): the option's
// rectangle less its top row and right column, the painter moved half a
// pixel right and up, corners of radius 2, a pen one unit wide.
// tst_workspace_default_button holds this to Fusion's pixels: the rim
// covers exactly the pixels of Fusion's own outline.
constexpr int kButtonOutlineTopInset = 1;
constexpr int kButtonOutlineRightInset = -1;
constexpr qreal kButtonOutlineShiftX = 0.5;
constexpr qreal kButtonOutlineShiftY = -0.5;
constexpr qreal kButtonOutlineRadius = 2.0;

// The geometry Fusion gives a focus rectangle (PE_FrameFocusRect): the
// option's rectangle less its last row and column, the painter moved half
// a pixel right and down, corners of radius 1.
constexpr int kFocusRectInset = -1;
constexpr qreal kFocusRectShift = 0.5;
constexpr qreal kFocusRectRadius = 1.0;

QColor selectionRim()
{
    return QColor::fromString(QLatin1StringView(kGlassSelBorder));
}

// Where Fusion draws its highlighted outline on a command button: the
// default button of a dialog, and a button the keyboard moved the focus
// to. A flat button that is not down has no panel.
bool hasHighlightedOutline(const QStyleOption* option)
{
    if (!(option->state & QStyle::State_Enabled)) {
        return false;
    }
    bool isDefault = false;
    if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option)) {
        const bool down = (option->state & QStyle::State_Sunken) || (option->state & QStyle::State_On);
        if ((button->features & QStyleOptionButton::Flat) && !down) {
            return false;
        }
        isDefault = button->features & QStyleOptionButton::DefaultButton;
    }
    const bool keyboardFocus =
        (option->state & QStyle::State_HasFocus) && (option->state & QStyle::State_KeyboardFocusChange);
    return isDefault || keyboardFocus;
}

void drawButtonRim(const QStyleOption* option, QPainter* painter)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(kButtonOutlineShiftX, kButtonOutlineShiftY);
    painter->setPen(QPen(selectionRim()));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(option->rect.adjusted(0, kButtonOutlineTopInset, kButtonOutlineRightInset, 0),
                             kButtonOutlineRadius, kButtonOutlineRadius);
    painter->restore();
}

// The current cell of a list, table or tree: the outline alone, solid, in
// the selection rim. Fusion's translucent tint inside it is not drawn.
void drawItemFocusRect(const QStyleOption* option, QPainter* painter)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(kFocusRectShift, kFocusRectShift);
    painter->setPen(QPen(selectionRim()));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(option->rect.adjusted(0, 0, kFocusRectInset, kFocusRectInset), kFocusRectRadius,
                             kFocusRectRadius);
    painter->restore();
}

} // namespace

LookStyle::LookStyle()
    : QProxyStyle(QStringLiteral("Fusion"))
{
    setObjectName(QStringLiteral("workspaceLookStyle"));
}

void LookStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                              const QWidget* widget) const
{
    if (!WorkspacePreview::enabled() || option == nullptr || painter == nullptr) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
    switch (element) {
    case PE_PanelButtonCommand:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        if (hasHighlightedOutline(option)) {
            drawButtonRim(option, painter);
        }
        return;
    case PE_FrameFocusRect:
        // An item view marks its current cell with State_Item and always
        // asks for the rectangle (QCommonStyle, CE_ItemViewItem); any
        // other focus rectangle stays Fusion's.
        if ((option->state & State_Item) && (option->state & State_KeyboardFocusChange)) {
            drawItemFocusRect(option, painter);
            return;
        }
        break;
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void LookStyle::install(QApplication& app)
{
    if (!WorkspacePreview::enabled()) {
        return;
    }
    static QPointer<LookStyle> installed;
    if (!installed.isNull()) {
        return;
    }
    // QApplication::setStyle() takes ownership and keeps the palette the
    // application was given.
    installed = new LookStyle;
    app.setStyle(installed.data());
}

} // namespace NereusSDR::Workspace
