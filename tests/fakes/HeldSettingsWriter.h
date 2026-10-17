// no-port-check: NereusSDR-original test helper.
// =================================================================
// tests/fakes/HeldSettingsWriter.h  (NereusSDR)
// =================================================================
// Tells a settings save made on the caller's thread from one handed to the
// settings writer thread (AppSettings::saveInBackground()).
//
// The writer thread is held at the start of its first job, so while it is
// held the only thing that can put the settings file on disk is a blocking
// AppSettings::save() on the test's own thread. A control change under
// test passes when the file is still absent after the change, a background
// save is waiting on the writer thread, and the file holds the change once
// the writer is let go.
// =================================================================

#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>

#include "core/AppSettings.h"
#include "core/SettingsFileWriter.h"

namespace NereusSDR {

class HeldSettingsWriter {
public:
    static constexpr int kWaitMs = 10000;

    /// Removes the settings file and holds the writer thread from its next
    /// job on. Construct it after the objects under test are set up and
    /// right before the change, so it is destroyed (and the writer let go)
    /// before they are.
    explicit HeldSettingsWriter(AppSettings& settings = AppSettings::instance())
        : m_settings(settings)
        , m_finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished)
    {
        QFile::remove(m_settings.filePath());
        m_settings.setBackgroundSaveStartHookForTesting(
            [entered = m_entered, release = m_release, thread = m_writerThread] {
                thread->store(QThread::currentThread());
                entered->release();
                release->acquire();
            });
    }

    ~HeldSettingsWriter()
    {
        m_release->release(kManyJobs);
        m_settings.setBackgroundSaveStartHookForTesting({});
    }

    /// True when this thread has written the settings file since
    /// construction: with the writer held, nothing else can have.
    bool callerWroteTheFile() const { return QFile::exists(m_settings.filePath()); }

    /// Waits for a background save to reach the writer thread. False when
    /// none does within kWaitMs, or when this thread wrote the file first.
    bool backgroundSaveReachedTheWriter() const
    {
        QElapsedTimer timer;
        timer.start();
        while (m_entered->available() == 0 && !callerWroteTheFile()
               && timer.elapsed() < kWaitMs) {
            QTest::qWait(5);
        }
        return m_entered->available() > 0 && !callerWroteTheFile()
            && m_writerThread->load() != nullptr
            && m_writerThread->load() != QThread::currentThread();
    }

    /// Lets the writer go and waits for one save to report. Returns what it
    /// reported (false also when nothing was reported within kWaitMs).
    bool letTheWriterFinish()
    {
        m_release->release(kManyJobs);
        if (m_finished.isEmpty() && !m_finished.wait(kWaitMs)) {
            return false;
        }
        return m_finished.last().at(1).toBool();
    }

    /// The value the file on disk holds for `key`, read afresh.
    QString onDisk(const QString& key) const
    {
        AppSettings fresh(m_settings.filePath());
        fresh.load();
        return fresh.value(key, QStringLiteral("<unset>")).toString();
    }

    QString onDiskHardware(const QString& mac, const QString& key) const
    {
        AppSettings fresh(m_settings.filePath());
        fresh.load();
        return fresh.hardwareValue(mac, key, QStringLiteral("<unset>")).toString();
    }

private:
    static constexpr int kManyJobs = 1024;

    AppSettings& m_settings;
    QSignalSpy m_finished;
    // Shared with the hook, which may still run on the writer thread while
    // this object goes away.
    std::shared_ptr<QSemaphore> m_entered{std::make_shared<QSemaphore>()};
    std::shared_ptr<QSemaphore> m_release{std::make_shared<QSemaphore>()};
    std::shared_ptr<std::atomic<QThread*>> m_writerThread{
        std::make_shared<std::atomic<QThread*>>(nullptr)};
};

} // namespace NereusSDR

/// The change just made was saved by the writer thread, never by this one,
/// and `expr` (read from the file on disk) holds afterward.
#define NEREUS_VERIFY_SAVED_IN_BACKGROUND(held, actual, expected)                       \
    do {                                                                               \
        QVERIFY2(!(held).callerWroteTheFile(),                                         \
                 "the settings file was written on the caller's thread");              \
        QVERIFY2((held).backgroundSaveReachedTheWriter(),                              \
                 "no background save reached the writer thread, or the caller's "      \
                 "thread wrote the file");                                             \
        QVERIFY2((held).letTheWriterFinish(), "the background save did not succeed");   \
        QCOMPARE(actual, expected);                                                    \
    } while (false)
