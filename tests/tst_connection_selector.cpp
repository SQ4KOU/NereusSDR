// =================================================================
// tests/tst_connection_selector.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R3 Core session presentation and actions.
// =================================================================

#include "gui/ConnectionSelector.h"
#include "gui/CoreTargetEditor.h"
#include "gui/styles/AppTheme.h"

#include <QAccessible>
#include <QCheckBox>
#include <QApplication>
#include <QDir>
#include <QStyleFactory>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

#include <algorithm>

using namespace NereusSDR;

namespace {

ConnectionTargetRow savedRow(const QString& key = QStringLiteral("saved-core"))
{
    return {key, ConnectionTargetKind::SavedCore, QStringLiteral("Shack"),
            QStringLiteral("Saturn G2"), QStringLiteral("shack.example"),
            QStringLiteral("Disconnected"), true, true, true};
}

SavedCoreTarget initialTarget()
{
    SavedCoreTarget target;
    target.id = QStringLiteral("saved-core");
    target.label = QStringLiteral("Shack");
    target.connection.url = QStringLiteral("wss://shack.example:8443");
    target.connection.token = QStringLiteral("token");
    target.connection.fingerprint = QStringLiteral("fingerprint");
    target.lastRadioName = QStringLiteral("Saturn G2");
    target.lastRadioMac = QStringLiteral("00:11:22:33:44:55");
    return target;
}

} // namespace

class ConnectionSelectorTest final : public QObject {
    Q_OBJECT

private slots:
    void selectionRefreshIsStableAndDoesNotConnect();
    void refreshPreservesSelectedRowModelIdentity();
    void explicitActionsUseTheSelectedKey();
    void disconnectRemainsIndependentOfSelectedTarget();
    void editorValidatesAndPreservesSecrets();
    void editorCancelHasNoAcceptance();
    void controlsRemainReadableAndReachable();
};

void ConnectionSelectorTest::selectionRefreshIsStableAndDoesNotConnect()
{
    ConnectionSelector selector;
    QSignalSpy connectSpy(&selector, &ConnectionSelector::connectRequested);
    QList<ConnectionTargetRow> targets{
        {QStringLiteral("local"), ConnectionTargetKind::LocalRadio, QStringLiteral("ANAN"),
         QStringLiteral("Local DSP"), QStringLiteral("192.168.1.10"),
         QStringLiteral("Available")},
        savedRow(),
    };
    selector.setTargets(targets);
    auto* tree = selector.findChild<QTreeWidget*>(QStringLiteral("connectionSelectorTargets"));
    QVERIFY(tree != nullptr);
    QTreeWidgetItem* savedRowItem = tree->topLevelItem(2)->child(0);
    QVERIFY(savedRowItem != nullptr);
    tree->setCurrentItem(savedRowItem);
    QCOMPARE(selector.selectedKey(), QStringLiteral("saved-core"));

    std::reverse(targets.begin(), targets.end());
    selector.setTargets(targets);
    QCOMPARE(selector.selectedKey(), QStringLiteral("saved-core"));
    QCOMPARE(connectSpy.count(), 0);
}

void ConnectionSelectorTest::refreshPreservesSelectedRowModelIdentity()
{
    ConnectionSelector selector;
    ConnectionTargetRow first = savedRow(QStringLiteral("saved-first"));
    first.name = QStringLiteral("First");
    ConnectionTargetRow selected = savedRow(QStringLiteral("saved-selected"));
    selected.name = QStringLiteral("Selected");
    selector.setTargets({first, selected});

    auto* tree = selector.findChild<QTreeWidget*>(QStringLiteral("connectionSelectorTargets"));
    QVERIFY(tree != nullptr);
    selector.setSelectedKey(selected.key);
    QTreeWidgetItem* selectedItem = tree->currentItem();
    QVERIFY(selectedItem != nullptr);
    selector.show();
    QCoreApplication::processEvents();
    const QPersistentModelIndex selectedIndex = tree->indexFromItem(selectedItem, 0);
    QVERIFY(selectedIndex.isValid());
    QAccessibleInterface* treeInterface = QAccessible::queryAccessibleInterface(tree);
    QVERIFY(treeInterface != nullptr);
    QAccessibleSelectionInterface* accessibleSelection = treeInterface->selectionInterface();
    QVERIFY(accessibleSelection != nullptr);
    const auto currentSelectedAccessibleId = [accessibleSelection] {
        const QList<QAccessibleInterface*> selected = accessibleSelection->selectedItems();
        return selected.isEmpty() || selected.first() == nullptr
            ? QAccessible::Id{} : QAccessible::uniqueId(selected.first());
    };
    const QList<QAccessibleInterface*> selectedInterfaces = accessibleSelection->selectedItems();
    QVERIFY(!selectedInterfaces.isEmpty());
    QVERIFY(selectedInterfaces.first() != nullptr);
    const QAccessible::Id selectedAccessibleId = QAccessible::uniqueId(selectedInterfaces.first());
    QSignalSpy resetSpy(tree->model(), &QAbstractItemModel::modelReset);

    selected.state = QStringLiteral("Connected");
    selector.setTargets({first, selected});

    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(selectedIndex.isValid());
    QCOMPARE(tree->itemFromIndex(selectedIndex), selectedItem);
    QCOMPARE(tree->currentItem(), selectedItem);
    QCOMPARE(selectedItem->text(3), QStringLiteral("Connected"));
    QCOMPARE(currentSelectedAccessibleId(), selectedAccessibleId);

    selector.setTargets({selected});

    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(selectedIndex.isValid());
    QCOMPARE(tree->itemFromIndex(selectedIndex), selectedItem);
    QCOMPARE(tree->currentItem(), selectedItem);
    QCOMPARE(selectedItem->text(3), QStringLiteral("Connected"));
    QCOMPARE(selector.selectedKey(), selected.key);
    const QList<QAccessibleInterface*> shiftedInterfaces = accessibleSelection->selectedItems();
    QVERIFY(!shiftedInterfaces.isEmpty());
    for (QAccessibleInterface* interface : shiftedInterfaces) {
        QVERIFY(interface != nullptr);
        QVERIFY(interface->isValid());
    }

    selector.setTargets({first});

    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(!selectedIndex.isValid());
    QVERIFY(accessibleSelection->selectedItems().isEmpty());
    QVERIFY(tree->currentItem() == nullptr);
    QVERIFY(selector.selectedKey().isEmpty());
    auto* details = selector.findChild<QPushButton*>(QStringLiteral("connectionSelectorDetails"));
    QVERIFY(details != nullptr);
    QVERIFY(!details->isEnabled());
}

void ConnectionSelectorTest::explicitActionsUseTheSelectedKey()
{
    ConnectionSelector selector;
    selector.setTargets({savedRow()});
    selector.setSelectedKey(QStringLiteral("saved-core"));

    QSignalSpy connectSpy(&selector, &ConnectionSelector::connectRequested);
    QSignalSpy editSpy(&selector, &ConnectionSelector::editRequested);
    QSignalSpy forgetSpy(&selector, &ConnectionSelector::forgetRequested);
    QSignalSpy detailsSpy(&selector, &ConnectionSelector::detailsRequested);
    for (const char* buttonName : {"connectionSelectorConnect", "connectionSelectorEdit",
                                   "connectionSelectorForget", "connectionSelectorDetails"}) {
        auto* button = selector.findChild<QPushButton*>(QString::fromLatin1(buttonName));
        QVERIFY(button != nullptr);
        QVERIFY(button->isEnabled());
        button->click();
    }
    QCOMPARE(connectSpy.takeFirst().at(0).toString(), QStringLiteral("saved-core"));
    QCOMPARE(editSpy.takeFirst().at(0).toString(), QStringLiteral("saved-core"));
    QCOMPARE(forgetSpy.takeFirst().at(0).toString(), QStringLiteral("saved-core"));
    QCOMPARE(detailsSpy.takeFirst().at(0).toString(), QStringLiteral("saved-core"));
}

void ConnectionSelectorTest::disconnectRemainsIndependentOfSelectedTarget()
{
    ConnectionSelector selector;
    ConnectionTargetRow unavailable = savedRow();
    unavailable.connectable = false;
    selector.setTargets({unavailable});
    selector.setSelectedKey(unavailable.key);
    auto* connectButton = selector.findChild<QPushButton*>(QStringLiteral("connectionSelectorConnect"));
    auto* disconnectButton = selector.findChild<QPushButton*>(QStringLiteral("connectionSelectorDisconnect"));
    QVERIFY(connectButton != nullptr);
    QVERIFY(disconnectButton != nullptr);
    QVERIFY(!connectButton->isEnabled());

    selector.setCurrentConnection(QStringLiteral("Retrying Shack"), QStringLiteral("Waiting"),
                                  false, true);
    QCOMPARE(disconnectButton->text(), QStringLiteral("Cancel retry"));
    QVERIFY(disconnectButton->isEnabled());
    QSignalSpy disconnectSpy(&selector, &ConnectionSelector::disconnectRequested);
    disconnectButton->click();
    QCOMPARE(disconnectSpy.count(), 1);
}

void ConnectionSelectorTest::editorValidatesAndPreservesSecrets()
{
    CoreTargetEditor editor(initialTarget());
    auto* address = editor.findChild<QLineEdit*>(QStringLiteral("coreTargetEditorAddress"));
    auto* token = editor.findChild<QLineEdit*>(QStringLiteral("coreTargetEditorToken"));
    auto* fingerprint = editor.findChild<QLineEdit*>(QStringLiteral("coreTargetEditorFingerprint"));
    auto* error = editor.findChild<QLabel*>(QStringLiteral("coreTargetEditorError"));
    auto* save = editor.findChild<QPushButton*>(QStringLiteral("coreTargetEditorSave"));
    QVERIFY(address != nullptr);
    QVERIFY(token != nullptr);
    QVERIFY(fingerprint != nullptr);
    QVERIFY(error != nullptr);
    QVERIFY(save != nullptr);
    QCOMPARE(token->echoMode(), QLineEdit::Password);

    address->setText(QStringLiteral("https://not-a-station"));
    save->click();
    QCOMPARE(editor.result(), int(QDialog::Rejected));
    QCOMPARE(error->text(), QStringLiteral("Enter a valid station address beginning with ws:// or wss://."));

    address->setText(QStringLiteral(" wss://other.example:8443 "));
    token->setText(QStringLiteral(" token with spaces "));
    fingerprint->setText(QStringLiteral(" fingerprint with spaces "));
    save->click();
    QCOMPARE(editor.result(), int(QDialog::Accepted));
    const SavedCoreTarget saved = editor.target();
    QCOMPARE(saved.id, QStringLiteral("saved-core"));
    QCOMPARE(saved.connection.url, QStringLiteral("wss://other.example:8443"));
    QCOMPARE(saved.connection.token, QStringLiteral(" token with spaces "));
    QCOMPARE(saved.connection.fingerprint, QStringLiteral(" fingerprint with spaces "));
    QVERIFY(saved.lastRadioName.isEmpty());
    QVERIFY(saved.lastRadioMac.isEmpty());
}

void ConnectionSelectorTest::editorCancelHasNoAcceptance()
{
    CoreTargetEditor editor(initialTarget());
    auto* cancel = editor.findChild<QPushButton*>(QStringLiteral("coreTargetEditorCancel"));
    QVERIFY(cancel != nullptr);
    cancel->click();
    QCOMPARE(editor.result(), int(QDialog::Rejected));
}

void ConnectionSelectorTest::controlsRemainReadableAndReachable()
{
    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    applyDarkPalette(*qApp);
    applyAppBaselineQss(*qApp);
    ConnectionSelector selector;
    selector.setTargets({
        {QStringLiteral("local"), ConnectionTargetKind::LocalRadio, QStringLiteral("This computer's Core"),
         QStringLiteral("Choose a local radio"), QStringLiteral("This computer"), QStringLiteral("Available")},
        {QStringLiteral("g2e"), ConnectionTargetKind::LocalRadio, QStringLiteral("ANAN G2E"),
         QStringLiteral("This computer's Core"), QStringLiteral("192.168.109.108"), QStringLiteral("Available"), true, true, true},
        {QStringLiteral("lan"), ConnectionTargetKind::LanCore, QStringLiteral("Rock 5C (advertised)"),
         QStringLiteral("Saturn (advertised online)"), QStringLiteral("192.168.109.106:4433"), QStringLiteral("Saved pin available")},
        savedRow()
    });
    selector.setDiscoveryStatus(QStringLiteral("LAN discovery is active. Verify new Cores in Core setup."));
    selector.setSelectedKey(QStringLiteral("saved-core"));
    selector.setCurrentConnection(QStringLiteral("Core connected"),
        QStringLiteral("Core: 192.168.109.106:4433\nRadio: Saturn G2"), true, false);
    selector.show();
    QCoreApplication::processEvents();
    QList<QRect> rectangles;
    for (QPushButton* button : selector.findChildren<QPushButton*>()) {
        if (!button->isVisible()) { continue; }
        const QRect bounds(button->mapTo(&selector, QPoint()), button->size());
        QVERIFY2(selector.rect().contains(bounds), qPrintable(button->text()));
        QVERIFY2(button->width() >= button->sizeHint().width(), qPrintable(button->text()));
        for (const QRect& previous : rectangles) { QVERIFY(!previous.intersects(bounds)); }
        rectangles.append(bounds);
    }
    QCOMPARE(rectangles.size(), 9);
    const QString captures = qEnvironmentVariable("NEREUS_SELECTOR_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        QVERIFY(QDir().mkpath(captures));
        QVERIFY(selector.grab().save(captures + QStringLiteral("/connections.png")));
        selector.setCurrentConnection(QStringLiteral("Retrying Core (attempt 3)"),
            QStringLiteral("Core: 192.168.109.106:4433\nRadio state unavailable\nRetry delay: 4 s. Disconnect cancels automatic retries."), true, true);
        selector.setNotice(QStringLiteral("The selected Core could not be reached. Saved and manual addresses remain available."));
        QCoreApplication::processEvents();
        QVERIFY(selector.grab().save(captures + QStringLiteral("/connections-retry.png")));
        CoreTargetEditor editor(initialTarget());
        editor.show();
        QCoreApplication::processEvents();
        QVERIFY(editor.grab().save(captures + QStringLiteral("/core-setup.png")));
    }
}

QTEST_MAIN(ConnectionSelectorTest)

#include "tst_connection_selector.moc"
