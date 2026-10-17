#pragma once
// no-port-check: NereusSDR-original scoped presentation of the approved PureSignal design.
// Modification history (NereusSDR):
//   2026-10-13: J.J. Boyd (KG4VCF), OpenAI Codex; shared panel and marker roles.
#include "gui/styles/WorkspacePreview.h"
#include <QWidget>
namespace NereusSDR::PureSignalViewStyle {
inline QColor text() { return WorkspacePreview::roleColour(QColor("#c8d8e8"), "text"); }
inline QColor muted() { return WorkspacePreview::roleColour(QColor("#9db6cd"), "text-secondary"); }
inline QColor background(const QObject* owner) { return WorkspacePreview::dataBackground(QColor("#141c28"), "panel", owner); }
inline QColor border() { return WorkspacePreview::roleColour(QColor("#203040"), "border"); }
inline QColor product(int index) {
    if (index < 2) { return WorkspacePreview::statusColour(QColor("#ffd24a"), "measured"); }
    // These data identities belong to this measurement surface, not the global palette.
    if (index < 4) { return WorkspacePreview::enabled() ? QColor("#d28f98") : QColor("#ff8a8a"); }
    return WorkspacePreview::enabled() ? QColor("#afa0d1") : QColor("#cc6680");
}
inline QFont font(const QFont& base) {
    QFont result = WorkspacePreview::readableFont(base);
    if (result.pixelSize() > 0 && result.pixelSize() < 11) { result.setPixelSize(11); }
    if (result.pixelSize() < 0 && result.pointSizeF() < 9.0) { result.setPixelSize(11); }
    return result;
}
} // namespace NereusSDR::PureSignalViewStyle
