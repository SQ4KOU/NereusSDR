// src/gui/styles/AppTheme.h (NereusSDR)
//
// Application-wide dark-theme bootstrap. Installs a QPalette + minimal
// baseline QSS on the QApplication so every widget — even ones that
// don't carry their own stylesheet — picks up the NereusSDR dark theme
// instead of falling through to the host platform's Fusion defaults.
//
// Why this exists:
//   On Linux/Ubuntu (Yaru theme), bare Fusion produces light backgrounds
//   and an orange highlight color. Per-page stylesheets covered most of
//   the UI but missed popups, group-box titles, tooltips, and any
//   widget added without an explicit `setStyleSheet`. This file is the
//   single source of truth that fills those gaps.
//
// Usage (main.cpp, after `app.setStyle(QStyleFactory::create("Fusion"))`):
//
//     NereusSDR::applyDarkPalette(app);
//     NereusSDR::applyAppBaselineQss(app);
//
// Per-page stylesheets remain authoritative — the baseline only paints
// what they don't.

#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>
#include <QStringLiteral>

#include "../StyleConstants.h"

namespace NereusSDR {

// Build a dark QPalette from STYLEGUIDE.md constants and install it on
// the QApplication. Run AFTER QApplication::setStyle("Fusion") and
// BEFORE any widgets are constructed so children inherit the right
// defaults from creation.
inline void applyDarkPalette(QApplication& app)
{
    QPalette p = app.palette();

    auto c = [](const char* hex) { return QColor(QString::fromLatin1(hex)); };

    const QColor appBg        = c(Style::kAppBg);          // #0f0f1a
    const QColor panelBg      = c(Style::kPanelBg);        // #0a0a18
    const QColor buttonBg     = c(Style::kButtonBg);       // #1a2a3a
    const QColor textPrimary  = c(Style::kTextPrimary);    // #c8d8e8
    const QColor textInactive = c(Style::kTextInactive);   // #405060
    const QColor accent       = c(Style::kAccent);         // #00b4d8
    const QColor border       = c(Style::kBorder);         // #205070
    const QColor disabledBg   = c(Style::kDisabledBg);     // #1a1a2a
    const QColor disabledText = c(Style::kDisabledText);   // #556070

    // Active and Inactive groups share colors so unfocused windows
    // still match the dark theme.
    for (auto group : { QPalette::Active, QPalette::Inactive }) {
        p.setColor(group, QPalette::Window,          appBg);
        p.setColor(group, QPalette::WindowText,      textPrimary);
        p.setColor(group, QPalette::Base,            buttonBg);
        p.setColor(group, QPalette::AlternateBase,   panelBg);
        p.setColor(group, QPalette::Text,            textPrimary);
        p.setColor(group, QPalette::Button,          buttonBg);
        p.setColor(group, QPalette::ButtonText,      textPrimary);
        p.setColor(group, QPalette::ToolTipBase,     buttonBg);
        p.setColor(group, QPalette::ToolTipText,     textPrimary);
        p.setColor(group, QPalette::Highlight,       accent);
        p.setColor(group, QPalette::HighlightedText, appBg);
        p.setColor(group, QPalette::Link,            accent);
        p.setColor(group, QPalette::LinkVisited,     accent);
        p.setColor(group, QPalette::PlaceholderText, textInactive);
        p.setColor(group, QPalette::BrightText,      QColor(Qt::white));
        // Frame shading roles — used by Fusion to draw bevels/grooves.
        p.setColor(group, QPalette::Light,           border);
        p.setColor(group, QPalette::Midlight,        border);
        p.setColor(group, QPalette::Dark,            panelBg);
        p.setColor(group, QPalette::Mid,             panelBg);
        p.setColor(group, QPalette::Shadow,          appBg);
    }

    // Disabled group — dim everything so disabled controls read as dim
    // against the dark surface instead of vanishing or flipping bright.
    p.setColor(QPalette::Disabled, QPalette::WindowText,      disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text,            disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      disabledText);
    p.setColor(QPalette::Disabled, QPalette::Button,          disabledBg);
    p.setColor(QPalette::Disabled, QPalette::Base,            disabledBg);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabledText);

    app.setPalette(p);
}

// Minimal app-wide QSS baseline for things QPalette can't fully cover:
//   - QGroupBox::title  — subcontrol; doesn't reliably inherit color from
//                         the parent QGroupBox rule on Fusion
//   - QToolTip          — Qt ignores the palette ToolTip roles unless
//                         a stylesheet is also present
//   - QMenu             — so any future menu added without the explicit
//                         kPopupMenu helper still renders dark, not
//                         dark-on-dark or white-on-light (issue #98 family)
//
// Per-page setStyleSheet calls override these whenever they want; this
// only fills the gap when nothing more specific applies.
inline void applyAppBaselineQss(QApplication& app)
{
    app.setStyleSheet(QStringLiteral(
        "QToolTip {"
        "  color: %1; background: %2; border: 1px solid %3;"
        "  padding: 3px; border-radius: 3px;"
        "}"
        "QGroupBox::title { color: %4; }"
        "QMenu { background: %2; color: %1; border: 1px solid %3; }"
        "QMenu::item { padding: 4px 20px; }"
        "QMenu::item:selected { background: #2a5a8a; color: #ffffff; }"
        "QMenu::item:disabled { color: %5; }"
        // Spinbox arrows: Fusion paints these with palette.foreground()
        // darkened, which is unreadable against our dark surfaces. Point
        // QSS at the bundled SVG triangles (kTextPrimary fill) so every
        // spinbox in the app gets the same legible arrow regardless of
        // whether the parent page has its own QSpinBox styling. The
        // buttons are widened so the arrow image isn't clipped to
        // Fusion's narrow default subcontrol width.
        "QSpinBox::up-button, QDoubleSpinBox::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right;"
        "  width: 20px; border: none;"
        "}"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right;"
        "  width: 20px; border: none;"
        "}"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        "  image: url(:/icons/spin-up.svg); width: 14px; height: 14px;"
        "}"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        "  image: url(:/icons/spin-down.svg); width: 14px; height: 14px;"
        "}"
    ).arg(QString::fromLatin1(Style::kTextPrimary),
          QString::fromLatin1(Style::kButtonBg),
          QString::fromLatin1(Style::kBorder),
          QString::fromLatin1(Style::kTitleText),
          QString::fromLatin1(Style::kTextInactive)));
}

} // namespace NereusSDR
