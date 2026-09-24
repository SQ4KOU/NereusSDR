#pragma once
// no-port-check: NereusSDR-original mirrored presentation of the Core's HL2
// I/O board state. All board logic stays in IoBoardHl2.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/core/IoBoardHl2Facade.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. The Core's Hermes Lite 2 I/O board
// as one read-only mirrored object, `ioBoard` (R-R3-46,
// radioHardwareVersion 3).
//
// Bound to an IoBoardHl2 (the Core) it follows the board: whether it was
// detected, its hardware version and its register mirror, which the board
// fills asynchronously from ep2 read responses after a probe.
//
// Unbound (a remote window) it holds the Core's values as they arrive and
// writes them into the window's own IoBoardHl2 (setTargetBoard), so Setup's
// HL2 I/O board tab shows the Core's board through the signals it already
// follows.
//
// Wire values: `registers` is the 256-byte register mirror as 512 upper-case
// hex digits, register 0 first.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  Created (R-R3-46 fix wave). AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

#include <array>

namespace NereusSDR {

class IoBoardHl2;

class IoBoardHl2Facade final : public QObject {
    Q_OBJECT
    // Reported by the Core; never written from a window.
    Q_PROPERTY(bool detected READ detected NOTIFY detectedChanged)
    Q_PROPERTY(int hardwareVersion READ hardwareVersion NOTIFY hardwareVersionChanged)
    Q_PROPERTY(QString registers READ registers NOTIFY registersChanged)

public:
    /// The size of IoBoardHl2's register mirror.
    static constexpr int kRegisterCount = 256;

    /// The plain reason a write to this object is refused.
    static QString readOnlyReason();

    explicit IoBoardHl2Facade(QObject* parent = nullptr);
    ~IoBoardHl2Facade() override;

    /// The Core: follow `board` (nullptr unbinds; the last values stay).
    void bindBoard(IoBoardHl2* board);
    bool isBound() const;

    /// A remote window: the board the Core's values are written into.
    void setTargetBoard(IoBoardHl2* board);

    /// A remote window: a value the Core reports. False for any other name,
    /// and always false while bound.
    bool applyRemoteProperty(const QByteArray& property, const QVariant& value);

    bool detected() const { return m_detected; }
    int hardwareVersion() const { return m_hardwareVersion; }
    QString registers() const;

signals:
    void detectedChanged(bool detected);
    void hardwareVersionChanged(int version);
    void registersChanged(const QString& registers);

private:
    using Registers = std::array<quint8, kRegisterCount>;

    static bool decodeRegisters(const QString& text, Registers* out);
    /// Re-read the bound board and emit each property that changed.
    void refresh();
    /// Write the held values into the target board (a remote window).
    void pushToTarget();

    QPointer<IoBoardHl2> m_board;
    QPointer<IoBoardHl2> m_target;
    QList<QMetaObject::Connection> m_boardConnections;
    bool m_detected{false};
    int m_hardwareVersion{0};
    Registers m_registers{};
};

} // namespace NereusSDR
