// =================================================================
// src/gui/CoreTargetStore.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 4g.
//
// The desktop-local address book for explicitly saved remote Cores.  This
// deliberately has no station-session or radio ownership: it only persists
// the operator's selection and the credentials needed to make a later
// authenticated connection attempt.
// =================================================================

#pragma once

#include "core/session/RemoteStationOptions.h"

#include <QList>
#include <QString>

#include <optional>

namespace NereusSDR {

class AppSettings;

struct SavedCoreTarget {
    QString id;
    QString label;
    RemoteStationOptions connection;
    QString lastRadioName;
    QString lastRadioMac;
};

class CoreTargetStore {
public:
    explicit CoreTargetStore(AppSettings&);

    bool load(QString* error = nullptr);
    QList<SavedCoreTarget> targets() const;
    std::optional<SavedCoreTarget> target(const QString& id) const;
    QString selectedId() const;

    bool upsert(const SavedCoreTarget&, QString* error = nullptr);
    bool remove(const QString& id, QString* error = nullptr);
    bool select(const QString& id, QString* error = nullptr);

    static QString createId();

private:
    bool persist(const QList<SavedCoreTarget>& targets, const QString& selectedId,
                 QString* error);

    AppSettings& m_settings;
    QList<SavedCoreTarget> m_targets;
    QString m_selectedId{QStringLiteral("local")};
    bool m_loaded{false};
};

} // namespace NereusSDR
