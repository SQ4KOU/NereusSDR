// =================================================================
// src/gui/SessionEndSave.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See SessionEndSave.h.
// =================================================================

#include "gui/SessionEndSave.h"

#include <QGuiApplication>
#ifndef QT_NO_SESSIONMANAGER
#include <QSessionManager>
#endif

#include <utility>

namespace NereusSDR {

SessionEndSave::SessionEndSave(std::function<void()> save, QObject* parent)
    : QObject(parent)
    , m_save(std::move(save))
{
#ifndef QT_NO_SESSIONMANAGER
    if (qGuiApp != nullptr) {
        // Direct, as Qt's documentation for commitDataRequest asks: the
        // session manager goes on as soon as the signal returns, so a
        // queued save would run too late.
        m_connection = connect(qGuiApp, &QGuiApplication::commitDataRequest, this,
                               [this](QSessionManager&) { commit(); },
                               Qt::DirectConnection);
    }
#endif
}

void SessionEndSave::commit()
{
    ++m_commitCount;
    if (m_save) {
        m_save();
    }
}

} // namespace NereusSDR
