// =================================================================
// src/gui/ConnectionSelector.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R3 Core session presentation and actions.
//
// Presentation-only selector for local radios, discovered Cores and the
// operator's saved Cores. Session construction and target persistence belong
// to the coordinator and CoreTargetStore respectively.
// =================================================================

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace NereusSDR {

enum class ConnectionTargetKind {
    LocalRadio,
    LanCore,
    SavedCore,
};

struct ConnectionTargetRow {
    QString key;
    ConnectionTargetKind kind;
    QString name;
    QString radioText;
    QString address;
    QString state;
    bool connectable = true;
    bool editable = false;
    bool forgettable = false;
};

class ConnectionSelector final : public QDialog {
    Q_OBJECT

public:
    explicit ConnectionSelector(QWidget* parent = nullptr);

    void setTargets(const QList<ConnectionTargetRow>& targets);
    void setCurrentConnection(const QString& summary, const QString& details,
                              bool canDisconnect, bool retrying);
    void setNotice(const QString& notice);
    void setDiscoveryStatus(const QString& text);

    QString selectedKey() const;
    void setSelectedKey(const QString& key);

signals:
    void connectRequested(QString key);
    void disconnectRequested();
    void editRequested(QString key);
    void forgetRequested(QString key);
    void detailsRequested(QString key);
    void addCoreRequested();
    void addRadioRequested();
    void scanRequested();

private:
    void addGroup(const QString& title, ConnectionTargetKind kind,
                  const QString& emptyText, const QList<ConnectionTargetRow>& targets);
    const ConnectionTargetRow* selectedTarget() const;
    void updateActions();
    QPushButton* makeButton(const QString& text, const QString& objectName);

    QTreeWidget* m_targetTree{nullptr};
    QLabel* m_discoveryStatusLabel{nullptr};
    QLabel* m_noticeLabel{nullptr};
    QLabel* m_currentSummaryLabel{nullptr};
    QLabel* m_currentDetailsLabel{nullptr};
    QPushButton* m_addCoreButton{nullptr};
    QPushButton* m_addRadioButton{nullptr};
    QPushButton* m_scanButton{nullptr};
    QPushButton* m_editButton{nullptr};
    QPushButton* m_forgetButton{nullptr};
    QPushButton* m_detailsButton{nullptr};
    QPushButton* m_disconnectButton{nullptr};
    QPushButton* m_connectButton{nullptr};
    QList<ConnectionTargetRow> m_targets;
    bool m_canDisconnect{false};
    bool m_retrying{false};
};

} // namespace NereusSDR
