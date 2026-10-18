// tests/GridHeaderAlignment.h  (NereusSDR)
//
// Shared helpers for the Setup grid tests: where a column header's text
// centre sits, and where a check box or radio button draws its indicator,
// both in one reference widget's coordinates. A header sits over its
// column when the two centres meet. AppLook puts the application in the
// look the product starts in (Fusion, dark palette, baseline sheet) or in
// the workspace look on top of it, and restores it afterwards.
//
// no-port-check: NereusSDR-original test helper.

#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPalette>
#include <QRadioButton>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QTabWidget>

#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"

namespace NereusSDR::GridHeaderAlignment {

// Horizontal centre of the label (the tests' headers are centre-aligned,
// so its text centre), in ref's coordinates.
inline qreal labelCentreX(const QLabel* label, const QWidget* ref)
{
    return label->mapTo(ref, QPoint(0, 0)).x() + label->width() / 2.0;
}

// Horizontal centre of the button's indicator, in ref's coordinates, from
// the style that draws it.
inline qreal indicatorCentreX(const QAbstractButton* button, const QWidget* ref)
{
    QStyleOptionButton option;
    option.initFrom(button);
    const bool radio = qobject_cast<const QRadioButton*>(button) != nullptr;
    const QRect indicator = button->style()->subElementRect(
        radio ? QStyle::SE_RadioButtonIndicator : QStyle::SE_CheckBoxIndicator,
        &option, button);
    return button->mapTo(ref, QPoint(indicator.x(), 0)).x() + indicator.width() / 2.0;
}

// Selects the tab titled `title` in whichever QTabWidget under root has it.
inline bool selectTab(QWidget* root, const QString& title)
{
    for (QTabWidget* tabs : root->findChildren<QTabWidget*>()) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == title) {
                tabs->setCurrentIndex(i);
                return true;
            }
        }
    }
    return false;
}

// The product's application look for the guard's lifetime: today's
// (workspace false) or the workspace look (true). Call apply(root) on the
// built page before it is shown so the workspace pass reaches it.
class AppLook
{
public:
    explicit AppLook(bool workspace)
        : m_app(qobject_cast<QApplication*>(QCoreApplication::instance()))
        , m_palette(m_app->palette())
        , m_sheet(m_app->styleSheet())
        , m_workspace(workspace)
    {
        m_app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        applyDarkPalette(*m_app);
        applyAppBaselineQss(*m_app);
        if (m_workspace) {
            WorkspacePreview::setEnabledForTest(true);
            applyWorkspacePalette(*m_app);
        }
    }
    ~AppLook()
    {
        m_app->setStyleSheet(m_sheet);
        m_app->setPalette(m_palette);
        if (m_workspace) {
            WorkspacePreview::setEnabledForTest(false);
        }
    }
    AppLook(const AppLook&) = delete;
    AppLook& operator=(const AppLook&) = delete;

    void apply(QWidget* root) const
    {
        if (m_workspace) {
            WorkspacePreview::themeSubtree(root);
        }
    }

private:
    QApplication* m_app;
    QPalette m_palette;
    QString m_sheet;
    bool m_workspace;
};

} // namespace NereusSDR::GridHeaderAlignment
