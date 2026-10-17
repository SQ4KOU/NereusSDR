// no-port-check: NereusSDR-original test helper.
//
// ExpiredAccessibleCells: the state that crashes an item view's next row
// change with accessibility on. Qt's macOS bridge deletes the cell
// interfaces of native rows it expires
// (qcocoaaccessibilityelement.mm:219-226,257-267,342-362) while the view's
// QAccessibleTable keeps their IDs; RowsInserted and RowsRemoved then read
// them (itemviews.cpp:645-741). Offscreen there is no bridge, so a test
// walks the cells the way an assistive client does and deletes them itself
// through the public QAccessible::deleteAccessibleInterface.
#pragma once

#include "gui/QtCocoaAccessibleTableCache.h"

#include <QAccessible>
#include <QAbstractItemView>
#include <QList>
#include <private/qguiapplication_p.h>
#include <qpa/qplatformaccessibility.h>
#include <qpa/qplatformintegration.h>

namespace NereusSDR::ExpiredAccessibleCells {

// Turns the platform's accessibility on or off. Qt 6.11's
// QAccessible::setActive does that; Qt 6.8's (Linux CI) only tells the
// activation observers, so there the platform's own switch is set. The
// item-view resets act on macOS only; tests let them act on every platform
// while accessibility is on, so Linux CI runs the same cases.
inline void setActive(bool active)
{
    setItemViewResetsOnEveryPlatformForTest(active);
    QAccessible::setActive(active);
    if (QAccessible::isActive() == active) {
        return;
    }
    if (QPlatformAccessibility* platform = QGuiApplicationPrivate::platformIntegration()->accessibility()) {
        platform->setActive(active);
    }
}

// Walks the view's cells the way an assistive client does. Returns their
// ids.
inline QList<QAccessible::Id> walk(QAbstractItemView* view)
{
    QList<QAccessible::Id> ids;
    QAccessibleInterface* table = QAccessible::queryAccessibleInterface(view);
    if (table == nullptr) {
        return ids;
    }
    for (int i = 0; i < table->childCount(); ++i) {
        if (QAccessibleInterface* child = table->child(i)) {
            ids.append(QAccessible::uniqueId(child));
        }
    }
    return ids;
}

// Deletes the cells as the bridge does when its autorelease pool drains
// after a table change. Ids already gone are ignored, as in Qt. Returns how
// many ids it was given.
inline int drop(const QList<QAccessible::Id>& ids)
{
    for (QAccessible::Id id : ids) {
        QAccessible::deleteAccessibleInterface(id);
    }
    return int(ids.size());
}

// Walks the view's cells, then deletes them as the bridge does. Returns
// how many were deleted.
inline int expire(QAbstractItemView* view)
{
    return drop(walk(view));
}

// Accessibility on for one test function, off again after.
class ActiveScope {
public:
    ActiveScope() { setActive(true); }
    ~ActiveScope() { setActive(false); }
    ActiveScope(const ActiveScope&) = delete;
    ActiveScope& operator=(const ActiveScope&) = delete;
};

} // namespace NereusSDR::ExpiredAccessibleCells
