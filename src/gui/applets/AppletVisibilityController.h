#pragma once

// =================================================================
// src/gui/applets/AppletVisibilityController.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original. No Thetis equivalent — Thetis exposes
// container-level show/hide via setup checkboxes, not per-applet
// toggles. AetherSDR has no equivalent.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-16 - Bench fix: isVisible() is on only while the applet's
//                 container is shown; setVisible(true) shows that container
//                 and emits bringOnScreenRequested; setAvailable carries a
//                 reason; setTransmitting picks the transmit or receive
//                 flag, as the containers do. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-08 — Bench fix: a registered applet missing from a loaded
//                 workspace is added to it, and setVisible() on a missing
//                 entry adds it, by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-02 — Mixed container ownership, persistence and source routing by
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-05-10 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code. Backs the Containers >
//                 Applets menu and the right-side panel ☰ menu.
// =================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QPointer>
#include "gui/NereusGuiExport.h"

namespace NereusSDR {

class ContainerWorkspaceStore;
class ContainerContentRegistry;
struct WorkspaceDocument;
class NEREUS_GUI_EXPORT AppletVisibilityController : public QObject {
    Q_OBJECT
public:
    explicit AppletVisibilityController(QObject* parent = nullptr);

    void setWorkspaceAdapter(ContainerWorkspaceStore* store, ContainerContentRegistry* registry);
    QString storageError() const { return m_storageError; }
    // Register an applet's id, display name, and default visibility.
    // If an AppSettings key for this id already exists, the persisted
    // value wins over defaultVisible. Idempotent on the same id; later
    // calls overwrite the display name but preserve current state.
    void registerApplet(const QString& id,
                        const QString& displayName,
                        bool defaultVisible);

    // User preference. With a workspace: on in its container, and that
    // container not hidden by its own flags (hidden, disabled, hidden by a
    // macro, hidden on receive, or on transmit while transmitting), so a
    // tick never claims an applet nobody sees.
    bool isVisible(const QString& id) const;
    bool isAvailable(const QString& id) const;        // capability gate
    bool isEffectivelyVisible(const QString& id) const; // visible && available
    QStringList registeredIds() const;       // insertion order preserved
    QString displayName(const QString& id) const;
    // Why the applet is unavailable, as setAvailable was given it.
    QString unavailableReason(const QString& id) const;

public slots:
    // User preference - persisted to AppSettings, or to the workspace.
    // With a workspace, true also shows the container holding the applet
    // (clears its hidden, disabled and macro flags, and its receive flag,
    // or its transmit flag while transmitting) and emits
    // bringOnScreenRequested; false touches only the applet's entry.
    void setVisible(const QString& id, bool visible);

    // The radio's transmit state, as ContainerManager::setTransmitting
    // has it: which of a container's showOnRx and showOnTx counts.
    void setTransmitting(bool transmitting);

    // Capability gate - NOT persisted. Lets the controller gray an
    // applet with its reason (and gray its menu entry; disabled, never
    // hidden, since 2026-10-16) when an external feature flag
    // says the applet shouldn't be reachable. Example: when the 4O3A
    // master toggle is off, the Amplifier and Tuner applets become
    // unavailable. The user's persisted visibility preference is
    // preserved across availability changes, so re-enabling 4O3A pops
    // the applet back if the user wanted it visible.
    // `reason` is what the container shows on the grayed applet (plain
    // English for the operator); empty gives the generic one.
    void setAvailable(const QString& id, bool available, const QString& reason = {});

signals:
    void persistenceFailed(const QString& error);
    // User preference changed (e.g., user clicked the menu).
    void visibilityChanged(const QString& id, bool visible);

    // Capability gate changed (e.g., 4O3A master toggle flipped).
    void availabilityChanged(const QString& id, bool available);

    // Convenience signal: fires when EITHER axis changes if it caused
    // the effective visibility to flip. Consumers that just want to
    // show/hide the wrapper should connect here.
    void effectiveVisibilityChanged(const QString& id, bool effective);

    // An applet was ticked on (setVisible true): the container holding
    // `typeId` should be brought where the operator can reach it.
    void bringOnScreenRequested(const QString& typeId);

private:
    void syncWorkspace();
    // Bench fix: places `type` in `document` with `visible` when it has no
    // entry there (a workspace saved before the applet existed): at the
    // end of the panel holding the station accessory applets, else the
    // main container. False when the document has nowhere to put it.
    bool placeInWorkspace(WorkspaceDocument& document, const QString& type, bool visible) const;
    void commitVisibility(const QString& id, const WorkspaceDocument& document);
    QPointer<ContainerWorkspaceStore> m_store;
    QPointer<ContainerContentRegistry> m_registry;
    QString m_storageError;
    bool m_transmitting = false;
    static QString settingsKey(const QString& id);  // "AppletRxVisible" etc.

    struct Entry {
        QString displayName;
        bool visible{true};       // user pref (persisted)
        bool available{true};     // capability gate (runtime)
        QString unavailableReason; // shown on the grayed applet
    };
    QHash<QString, Entry> m_entries;   // id -> entry
    QStringList m_order;               // registration order
};

} // namespace NereusSDR
