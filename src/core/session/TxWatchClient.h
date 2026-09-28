// no-port-check: NereusSDR-original direct auxiliary watch transport.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>
#include <utility>

class QTimer;
class QWebSocket;

namespace NereusSDR {

// Owns only an independent, direct WSS watch socket. The caller has already
// authenticated the primary and supplies its current verified Core TLS digest,
// a fresh Core ticket, and the immutable logical primary generation.
// This class has no authority over the primary session or transmit state.
class TxWatchClient final : public QObject {
    Q_OBJECT
public:
    explicit TxWatchClient(QObject* parent = nullptr);
    ~TxWatchClient() override;

    // Deadlines may be shortened by an in-process test, never extended in
    // production. Call before openDirect().
    void setDeadlinesForTesting(int openingMs, int acknowledgementMs);
#ifdef NEREUS_BUILD_TESTS
    void setBacklogBytesForTesting(qint64 bytes) { m_testBacklogBytes = bytes; }
    using BinaryWriterForTesting = std::function<qint64(QWebSocket*, const QByteArray&)>;
    void setBinaryWriterForTesting(BinaryWriterForTesting writer)
    {
        m_binaryWriterForTesting = std::move(writer);
    }
#endif

    bool openDirect(const QUrl& verifiedPrimaryUrl, const QByteArray& actualCorePinSha256,
                    const QByteArray& rawTicket, quint64 primaryGeneration);
    void close();
    bool isReady() const { return m_ready; }
    quint64 generation() const { return m_generation; }
    bool sendKeepalive(quint64 sequence, quint32 epoch);

signals:
    void ready(quint64 primaryGeneration);
    void closed(quint64 primaryGeneration, const QString& reason);

private:
    void finish(const QString& reason);
    bool freshPinMatches(QWebSocket* socket) const;
    bool current(QWebSocket* socket, quint64 generation) const;

    QPointer<QWebSocket> m_socket;
    QTimer* m_deadline = nullptr;
    QByteArray m_pin;
    QByteArray m_ticket;
    quint64 m_generation = 0;
    quint64 m_revision = 0;
    bool m_active = false;
    bool m_ready = false;
    int m_openingMs = 10000;
    int m_acknowledgementMs = 5000;
#ifdef NEREUS_BUILD_TESTS
    qint64 m_testBacklogBytes = -1;
    BinaryWriterForTesting m_binaryWriterForTesting;
#endif
};

} // namespace NereusSDR
