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
#include <QVBoxLayout>
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

    // Container #0 wraps its MeterWidget inside an AppletPanelWidget
    // (header slot). innerMeterWidget() must find the nested meter via
    // findChild so save/restore works for the wrapped shape too. We
    // simulate the wrap with a plain QWidget hosting a MeterWidget
    // child — same QObject parent/child relationship the real
    // AppletPanelWidget creates.
    void wrappedMeterItemsSurviveSaveRestore()
    {
        QWidget dockParent;
        QSplitter splitter;
        QString savedId;

        {
            ContainerManager mgr(&dockParent, &splitter);
            mgr.setContentFactory([](const QString&, int) -> QWidget* {
                auto* wrapper = new QWidget();
                auto* layout = new QVBoxLayout(wrapper);
                layout->setContentsMargins(0, 0, 0, 0);
                auto* meter = new MeterWidget();
                layout->addWidget(meter);
                return wrapper;
            });

            ContainerWidget* c = mgr.createContainer(0, DockMode::PanelDocked);
            QVERIFY(c);
            savedId = c->id();

            // Build the wrap shape and install it as content.
            auto* wrapper = new QWidget();
            auto* layout = new QVBoxLayout(wrapper);
            layout->setContentsMargins(0, 0, 0, 0);
            auto* meter = new MeterWidget(wrapper);
            layout->addWidget(meter);
            c->setContent(wrapper);

            meter->addItem(new BarItem());
            meter->addItem(new TextItem());
            meter->addItem(new BarItem());
            QCOMPARE(meter->items().size(), 3);

            mgr.saveState();
        }

        {
            ContainerManager mgr2(&dockParent, &splitter);
            mgr2.setContentFactory([](const QString&, int) -> QWidget* {
                auto* wrapper = new QWidget();
                auto* layout = new QVBoxLayout(wrapper);
                layout->setContentsMargins(0, 0, 0, 0);
                auto* meter = new MeterWidget();
                layout->addWidget(meter);
                return wrapper;
            });
            mgr2.restoreState();

            ContainerWidget* c = mgr2.container(savedId);
            QVERIFY(c);

            // The container content is the wrapper QWidget, not the
            // meter directly — exercises findChild<MeterWidget*>().
            auto* meter = c->content() ? c->content()->findChild<MeterWidget*>() : nullptr;
            QVERIFY2(meter != nullptr, "wrapped MeterWidget not found after restore");
            QCOMPARE(meter->items().size(), 3);
        }
    }

    // ContainerManager must announce every MeterWidget it manages so
    // MainWindow can wire them into MeterPoller. Previously, only the
    // initial m_meterWidget on the panel container was registered;
    // user-created containers' meters were orphaned and never received
    // setValue() calls — bars sat at frac=0 (visually invisible) and
    // needles never moved. Symptom: "BarMeter not drawing".
    void createdContainerMeterAnnouncedForPolling()
    {
        QWidget dockParent;
        QSplitter splitter;
        ContainerManager mgr(&dockParent, &splitter);

        QList<MeterWidget*> announced;
        QObject::connect(&mgr, &ContainerManager::meterReadyForPolling,
                         [&announced](MeterWidget* m) {
            announced.append(m);
        });

        ContainerWidget* c = mgr.createContainer(1, DockMode::Floating);
        QVERIFY(c);
        auto* meter = new MeterWidget();
        c->setContent(meter);

        QCOMPARE(announced.size(), 1);
        QCOMPARE(announced.first(), meter);
    }

    // After restoreState, every container that ContainerManager
    // materialized a MeterWidget for must also be announced. The
    // restored meter is a fresh instance, not the one the test
    // populated in phase 1, so we just assert that exactly one signal
    // fired with a non-null pointer matching the container's content.
    void restoredContainerMeterAnnouncedForPolling()
    {
        QWidget dockParent;
        QSplitter splitter;
        QString savedId;

        {
            ContainerManager mgr(&dockParent, &splitter);
            ContainerWidget* c = mgr.createContainer(1, DockMode::Floating);
            savedId = c->id();
            auto* meter = new MeterWidget();
            c->setContent(meter);
            meter->addItem(new BarItem());
            mgr.saveState();
        }

        {
            ContainerManager mgr2(&dockParent, &splitter);

            QList<MeterWidget*> announced;
            QObject::connect(&mgr2, &ContainerManager::meterReadyForPolling,
                             [&announced](MeterWidget* m) {
                announced.append(m);
            });

            mgr2.restoreState();

            QCOMPARE(announced.size(), 1);
            QVERIFY(announced.first() != nullptr);

            ContainerWidget* c = mgr2.container(savedId);
            QVERIFY(c);
            QCOMPARE(announced.first(), qobject_cast<MeterWidget*>(c->content()));
        }
    }
};

QTEST_MAIN(TstContainerPersistence)
#include "tst_container_persistence.moc"
