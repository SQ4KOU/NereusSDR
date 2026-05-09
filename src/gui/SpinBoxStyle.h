// src/gui/SpinBoxStyle.h
//
// Per-widget spinbox styling helper, sibling to ComboStyle.h.  Applies
// the dark-theme palette + SVG up/down arrows directly to the widget so
// the rendering doesn't depend on the QApplication-baseline cascade
// reaching the spinbox (it doesn't, reliably, when an ancestor like
// SetupDialog has its own setStyleSheet).
//
// Use this on every QSpinBox / QDoubleSpinBox in the Setup dialogs and
// any other surface that should match the app's button look.
#pragma once

#include <QAbstractSpinBox>
#include <QString>
#include <QStringLiteral>
#include "StyleConstants.h"

namespace NereusSDR {

inline void applySpinBoxStyle(QAbstractSpinBox* spin)
{
    spin->setFixedHeight(Style::kButtonH);
    spin->setStyleSheet(QStringLiteral(
        "QAbstractSpinBox {"
        "  background: %1; color: %2;"
        "  border: 1px solid %3; border-radius: 3px;"
        "  padding: 1px 4px; font-size: 10px;"
        "}"
        // Up / down buttons — explicit subcontrol-position so Fusion's
        // native rendering doesn't get a chance to clip them.  Width 16
        // is just wide enough for the 10×10 SVG to sit centered.
        "QAbstractSpinBox::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right;"
        "  width: 16px; border-left: 1px solid %3;"
        "  background: %1;"
        "}"
        "QAbstractSpinBox::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right;"
        "  width: 16px; border-left: 1px solid %3; border-top: 1px solid %3;"
        "  background: %1;"
        "}"
        "QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover {"
        "  background: %4;"
        "}"
        "QAbstractSpinBox::up-arrow {"
        "  image: url(:/icons/spin-up.svg); width: 10px; height: 10px;"
        "}"
        "QAbstractSpinBox::down-arrow {"
        "  image: url(:/icons/spin-down.svg); width: 10px; height: 10px;"
        "}"
    ).arg(QString::fromLatin1(Style::kButtonBg),
          QString::fromLatin1(Style::kTextPrimary),
          QString::fromLatin1(Style::kBorder),
          QString::fromLatin1(Style::kButtonHover)));
}

} // namespace NereusSDR
