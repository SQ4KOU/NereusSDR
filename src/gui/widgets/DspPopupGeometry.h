// SPDX-License-Identifier: GPL-2.0-or-later
// NereusSDR-original popup placement, J.J. Boyd (KG4VCF), 2026-10-12.
// AI-assisted via OpenAI Codex.
#pragma once
#include <QRect>
#include <QSize>
#include <algorithm>

namespace NereusSDR::DspPopupGeometry {

inline QRect boundedGeometry(const QPoint& anchor, const QSize& wanted, const QRect& available)
{
    const QSize size = wanted.boundedTo(available.size());
    QPoint position = anchor;
    if (position.x() + size.width() > available.right()) {
        position.setX(available.right() - size.width());
    }
    if (position.y() + size.height() > available.bottom()) {
        position.setY(anchor.y() - size.height());
    }
    position.setX(std::clamp(position.x(), available.left(), available.right() - size.width() + 1));
    position.setY(std::clamp(position.y(), available.top(), available.bottom() - size.height() + 1));
    return QRect(position, size);
}

} // namespace NereusSDR::DspPopupGeometry
