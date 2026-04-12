// tst_container_persistence.cpp — meter items inside containers must
// survive a ContainerManager save/restore round-trip.
//
// RED test: with current code, ContainerWidget::serialize() persists
// 25 fields of metadata but never serializes its inner MeterWidget's
// items, and ContainerManager::restoreState never materializes a
// MeterWidget for restored containers (the constructor only installs
// a placeholder QLabel). After restart, user-created containers come
// back empty.

#include <QtTest/QtTest>
#include <QSplitter>
#include <QStringList>
#include <QWidget>

#include "core/AppSettings.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/meters/MeterItem.h"
#include "gui/meters/MeterWidget.h"

using namespace NereusSDR;

class TstContainerPersistence : public QObject
{
    Q_OBJECT

private:
    static void clearContainerKeys()
    {
        auto& s = AppSettings::instance();
        const QStringList keys = s.allKeys();
        for (const QString& k : keys) {
            if (k.startsWith(QStringLiteral("Container"))) {
                s.remove(k);
            }
        }
    }

private slots:
    void init() { clearContainerKeys(); }
    void cleanup() { clearContainerKeys(); }

    void userContainerItemsSurviveSaveRestore()
    {
        QWidget dockParent;
        QSplitter splitter;
        QString savedId;

        // --- Phase 1: create a user container, populate, save ---
        {
            ContainerManager mgr(&dockParent, &splitter);
            ContainerWidget* c = mgr.createContainer(1, DockMode::Floating);
            QVERIFY(c);
            savedId = c->id();

            auto* meter = new MeterWidget();
            c->setContent(meter);

            meter->addItem(new BarItem());
            meter->addItem(new TextItem());
            QCOMPARE(meter->items().size(), 2);

            mgr.saveState();
        }

        // --- Phase 2: fresh manager, restore, verify items survived ---
        {
            ContainerManager mgr2(&dockParent, &splitter);
            mgr2.restoreState();

            ContainerWidget* c = mgr2.container(savedId);
            QVERIFY2(c != nullptr, "container missing after restoreState");

            auto* meter = qobject_cast<MeterWidget*>(c->content());
            QVERIFY2(meter != nullptr,
                     "restored container has no MeterWidget content "
                     "(ContainerManager::restoreState did not materialize one)");
            QCOMPARE(meter->items().size(), 2);
        }
    }
};

QTEST_MAIN(TstContainerPersistence)
#include "tst_container_persistence.moc"
