// =================================================================
// src/gui/SessionEndSave.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Saves the settings when the
// desktop session ends (logoff, shutdown, an installer closing the app).
// =================================================================

#pragma once

#include <QMetaObject>
#include <QObject>

#include <functional>

namespace NereusSDR {

/// Runs a save when the session manager asks the application to commit its
/// data: a logoff or shutdown, or an installer closing the app. A normal
/// close already saves; this path did not, so whatever the 500 ms save
/// timer had not written was lost (settings restore audit 2026-10-11,
/// gap 8).
///
/// The save only. The session's end can still be called off (by the
/// operator, or by another application), so nothing is torn down here: the
/// radio stays connected and the windows stay open. If the session does
/// end, Qt's ordinary quit follows and does the rest. The request is never
/// held up or refused: no question is asked and cancel() is not called.
///
/// Qt's own signal on every platform (QGuiApplication::commitDataRequest),
/// with no platform guard.
class SessionEndSave : public QObject {
    Q_OBJECT
public:
    /// `save` runs on the main thread, inside the session manager's request.
    explicit SessionEndSave(std::function<void()> save, QObject* parent = nullptr);

    /// True when the session manager's request reaches commit(). False
    /// without a QGuiApplication, or in a Qt built without session
    /// management.
    bool listening() const { return static_cast<bool>(m_connection); }

    /// What a session end runs. Public so the save can be run without a
    /// session manager (tests).
    void commit();

    /// How many times a session end has run the save.
    int commitCount() const { return m_commitCount; }

private:
    std::function<void()> m_save;
    QMetaObject::Connection m_connection;
    int m_commitCount{0};
};

} // namespace NereusSDR
