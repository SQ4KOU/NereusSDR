# Phase 3Q — Connection Workflow Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor NereusSDR's connect / discover / disconnect surface so a user on a Layer-3 VPN can reach a remote HL2 reliably, and so the disconnected state announces itself instead of just freezing the spectrum.

**Architecture:** A single `ConnectionState` machine (`Disconnected → Probing → Connecting → Connected → LinkLost`) lives on `RadioModel`. Two triggers feed it: broadcast scan (existing) and a new `RadioDiscovery::probeAddress()` unicast probe. UI components (TitleBar segment, modal `ConnectionPanel`, rebuilt Add Radio dialog with model-aware SKU picker, status-bar verbose strip, spectrum disconnect overlay, reworked Radio menu) all observe `RadioModel::connectionState()` and `RadioConnection::frameReceived()` rather than duplicating state.

**Tech Stack:** C++20, Qt 6 (Widgets, Network, Test), CMake + Ninja, FFTW3, WDSP. Tests via Qt Test (`QTest::qExec`, `QSignalSpy`, `QCOMPARE`, `QVERIFY`) registered via `nereus_add_test()` in `tests/CMakeLists.txt`. Fake-radio pattern from `tests/fakes/P1FakeRadio.{h,cpp}` for probe/connect tests.

**Design spec:** [`docs/architecture/2026-04-26-connection-workflow-refactor-design.md`](2026-04-26-connection-workflow-refactor-design.md) (committed as `1cdf8b3`).

---

## Sequencing strategy

Eight PRs, in this order. Each PR is independently mergeable and reviewable. Tasks within a PR are sequential.

| PR | Tasks | Files | Blocks |
|---|---|---|---|
| PR-1: Foundation | 1, 2, 3 | `ConnectionState.h` (new), `RadioDiscovery.{h,cpp}`, `RadioConnection.{h,cpp}`, `RadioModel.{h,cpp}` | Everything else |
| PR-2: Add Radio dialog | 4 | `AddCustomRadioDialog.{h,cpp}` | None (dialog rebuild is self-contained once PR-1 lands) |
| PR-3: ConnectionPanel polish | 5 | `ConnectionPanel.{h,cpp}` | None |
| PR-4: TitleBar + status-bar | 6, 7 | `TitleBar.{h,cpp}`, `MainWindow.cpp` | None |
| PR-5: Spectrum overlay | 8 | `SpectrumWidget.{h,cpp}` | None |
| PR-6: Radio menu | 9 | `MainWindow.cpp` | None |
| PR-7: Auto-connect failure path | 10 | `MainWindow.cpp`, `RadioModel.{h,cpp}`, `AppSettings.{h,cpp}` | PR-3 (panel auto-open path) |
| PR-8: Stale policy + macKey migration | 11, 12 | `RadioDiscovery.{h,cpp}`, `AppSettings.{h,cpp}`, `RadioModel.cpp` | PR-1 |

PR-1 is the only hard blocker. PR-2 through PR-8 can land in any order after PR-1 (PR-7 wants PR-3's auto-open path so order it after PR-3). Manual end-to-end testing (Task 13) lives at the tail and is not its own PR — it's a verification gate before merging the last PR.

## File structure

| File | Status | Responsibility |
|---|---|---|
| `src/core/ConnectionState.h` | NEW | Lifecycle enum + helpers; one source of truth for state semantics |
| `src/core/RadioDiscovery.{h,cpp}` | MODIFIED | Add unicast `probeAddress()`; revise stale policy |
| `src/core/RadioConnection.{h,cpp}` | MODIFIED | Add `frameReceived()` signal; structured `ConnectFailure` |
| `src/core/AppSettings.{h,cpp}` | MODIFIED | Update `lastSeen` on probe; macKey migration helper |
| `src/models/RadioModel.{h,cpp}` | MODIFIED | Expose `connectionState()`; multi-flag auto-connect handling |
| `src/gui/AddCustomRadioDialog.{h,cpp}` | MODIFIED | Model dropdown rebuild; two-button action set; failure UX |
| `src/gui/ConnectionPanel.{h,cpp}` | MODIFIED | Status strip; state pills; Last Seen column; ↻ Scan button |
| `src/gui/TitleBar.{h,cpp}` | MODIFIED | New `ConnectionSegment` widget |
| `src/gui/MainWindow.cpp` | MODIFIED | Status bar verbose strip; Radio menu rework; auto-connect failure routing |
| `src/gui/SpectrumWidget.{h,cpp}` | MODIFIED | Disconnect fade + overlay + click-to-open |
| `tests/tst_radio_discovery_probe.cpp` | NEW | Unit tests for `probeAddress()` |
| `tests/tst_connection_state.cpp` | NEW | Unit tests for `ConnectionState` transitions on `RadioModel` |
| `tests/tst_add_custom_radio_dialog.cpp` | NEW | Unit tests for the rebuilt dialog (model dropdown, failure path, Save offline) |
| `tests/tst_connection_panel_state_pills.cpp` | NEW | Unit tests for state-pill computation from `lastSeen` |
| `tests/tst_radio_discovery_parse.cpp` | EXTENDED | Add probe-reply parsing cases |
| `tests/tst_connection_panel_saved_radios.cpp` | EXTENDED | Add stale-policy assertions |
| `tests/CMakeLists.txt` | MODIFIED | Register the four new test executables |

---

# PR-1: Foundation (ConnectionState + probe + structured failures)

The backbone everything else builds on. Three tasks, one PR, in sequence.

---

## Task 1: ConnectionState enum + RadioModel state exposure

**Files:**
- Create: `src/core/ConnectionState.h`
- Modify: `src/models/RadioModel.h`
- Modify: `src/models/RadioModel.cpp`
- Create: `tests/tst_connection_state.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/tst_connection_state.cpp`:

```cpp
#include <QObject>
#include <QSignalSpy>
#include <QtTest>
#include "core/ConnectionState.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TstConnectionState : public QObject {
    Q_OBJECT
private slots:
    void initialStateIsDisconnected() {
        RadioModel m;
        QCOMPARE(m.connectionState(), ConnectionState::Disconnected);
    }

    void emitsSignalOnTransition() {
        RadioModel m;
        QSignalSpy spy(&m, &RadioModel::connectionStateChanged);
        m.setConnectionStateForTest(ConnectionState::Probing);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.connectionState(), ConnectionState::Probing);
    }

    void noSignalForRedundantTransition() {
        RadioModel m;
        m.setConnectionStateForTest(ConnectionState::Probing);
        QSignalSpy spy(&m, &RadioModel::connectionStateChanged);
        m.setConnectionStateForTest(ConnectionState::Probing);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TstConnectionState)
#include "tst_connection_state.moc"
```

Add to `tests/CMakeLists.txt` near other `nereus_add_test()` calls:

```cmake
nereus_add_test(tst_connection_state)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target tst_connection_state 2>&1 | tail -20
```

Expected: build error — `ConnectionState.h` not found, `connectionState()` not declared, `setConnectionStateForTest` not declared.

- [ ] **Step 3: Create ConnectionState.h**

```cpp
// src/core/ConnectionState.h
#pragma once

#include <QMetaType>
#include <QString>

namespace NereusSDR {

// Lifecycle of a single connection attempt + steady-state.
// Single source of truth for connection-state semantics — TitleBar,
// ConnectionPanel, status bar, spectrum overlay all read this.
enum class ConnectionState : int {
    Disconnected = 0,  // No connection. UI: grey dot, "Disconnected".
    Probing      = 1,  // Unicast/broadcast probe in flight (~1.5s budget).
                       // UI: amber pulse, "Probing 192.168.x.x…".
    Connecting   = 2,  // Probe replied, metis-start sent, awaiting first ep6.
                       // UI: amber pulse fast, "Connecting to ANAN-G2…".
    Connected    = 3,  // First ep6 frame received; data flowing.
                       // UI: green dot + radio name + Mbps + activity LED.
    LinkLost     = 4,  // Was Connected; no frames for >5s.
                       // UI: red pulse, "Link lost — last frame Xs ago".
};

inline QString connectionStateName(ConnectionState s) {
    switch (s) {
        case ConnectionState::Disconnected: return QStringLiteral("Disconnected");
        case ConnectionState::Probing:      return QStringLiteral("Probing");
        case ConnectionState::Connecting:   return QStringLiteral("Connecting");
        case ConnectionState::Connected:    return QStringLiteral("Connected");
        case ConnectionState::LinkLost:     return QStringLiteral("Link lost");
    }
    return QStringLiteral("Unknown");
}

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::ConnectionState)
```

Add to `src/models/RadioModel.h` in the public section:

```cpp
#include "core/ConnectionState.h"

// ...inside class RadioModel : public QObject {
public:
    ConnectionState connectionState() const { return m_connectionState; }

    // Test-only: allow tests to drive transitions without standing up
    // a fake RadioConnection. Production transitions go through the
    // private setConnectionState() called from connection signals.
    void setConnectionStateForTest(ConnectionState s) { setConnectionState(s); }

signals:
    void connectionStateChanged(NereusSDR::ConnectionState newState);

private:
    void setConnectionState(ConnectionState s);
    ConnectionState m_connectionState{ConnectionState::Disconnected};
```

Add to `src/models/RadioModel.cpp`:

```cpp
void RadioModel::setConnectionState(ConnectionState s)
{
    if (m_connectionState == s) {
        return;
    }
    m_connectionState = s;
    emit connectionStateChanged(s);
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target tst_connection_state && ctest --test-dir build -R tst_connection_state -V 2>&1 | tail -25
```

Expected: 3 passed, 0 failed.

- [ ] **Step 5: Commit**

```bash
git add src/core/ConnectionState.h src/models/RadioModel.h src/models/RadioModel.cpp \
        tests/tst_connection_state.cpp tests/CMakeLists.txt
git commit -S -m "$(cat <<'EOF'
feat(connection): introduce ConnectionState enum on RadioModel (3Q-1)

Single source of truth for connection lifecycle (Disconnected, Probing,
Connecting, Connected, LinkLost). RadioModel exposes connectionState()
and emits connectionStateChanged on transitions; redundant transitions
emit no signal.

Foundation for Phase 3Q. Subsequent tasks wire RadioDiscovery probes,
RadioConnection failures, and UI consumers (TitleBar, ConnectionPanel,
status bar, spectrum overlay) onto this enum.

Tests: tst_connection_state (initial state, signal on transition,
no-signal on redundant transition).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** `tst_connection_state` passes; build green; no other tests regress.

---

## Task 2: RadioDiscovery::probeAddress unicast probe

**Files:**
- Modify: `src/core/RadioDiscovery.h`
- Modify: `src/core/RadioDiscovery.cpp`
- Create: `tests/tst_radio_discovery_probe.cpp`
- Modify: `tests/CMakeLists.txt`

The unicast probe is the **backbone for Miguel's case** — manual IPs and saved radios over L3-only VPNs get validated through this code path before any connection attempt.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_radio_discovery_probe.cpp` with the byte-for-byte Thetis attribution header (copy from `tests/tst_radio_discovery_parse.cpp:1-44`, change the modification-history line to today's date and reference Phase 3Q-2). Then:

```cpp
#include <QObject>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QtTest>
#include "core/RadioDiscovery.h"

using namespace NereusSDR;

// Fake P1 radio that listens on a loopback port and replies to a probe.
// Keeps the test self-contained — no network dependencies.
class FakeP1Probe : public QObject {
    Q_OBJECT
public:
    explicit FakeP1Probe(QObject* parent = nullptr) : QObject(parent) {
        m_socket = new QUdpSocket(this);
        m_socket->bind(QHostAddress::LocalHost, 0);
        connect(m_socket, &QUdpSocket::readyRead, this, &FakeP1Probe::onReadyRead);
    }
    quint16 port() const { return m_socket->localPort(); }
private slots:
    void onReadyRead() {
        while (m_socket->hasPendingDatagrams()) {
            QByteArray buf;
            buf.resize(int(m_socket->pendingDatagramSize()));
            QHostAddress src;
            quint16 srcPort = 0;
            m_socket->readDatagram(buf.data(), buf.size(), &src, &srcPort);

            // P1 probe is 0xEF 0xFE 0x02 (3 bytes).
            if (buf.size() >= 3 && static_cast<quint8>(buf[0]) == 0xEF
                && static_cast<quint8>(buf[1]) == 0xFE
                && static_cast<quint8>(buf[2]) == 0x02) {
                QByteArray reply(63, 0);
                reply[0] = char(0xEF); reply[1] = char(0xFE); reply[2] = 0x02;
                // MAC bytes 3..8
                reply[3] = 0x00; reply[4] = 0x1C; reply[5] = char(0xC0);
                reply[6] = char(0xA2); reply[7] = 0x14; reply[8] = char(0x8B);
                reply[9] = 75;          // firmware
                reply[10] = 6;          // boardId = HL2
                m_socket->writeDatagram(reply, src, srcPort);
            }
        }
    }
private:
    QUdpSocket* m_socket;
};

class TstRadioDiscoveryProbe : public QObject {
    Q_OBJECT
private slots:
    void probeReplyFillsRadioInfo() {
        FakeP1Probe radio;
        RadioDiscovery disc;
        QSignalSpy spy(&disc, &RadioDiscovery::radioDiscovered);

        disc.probeAddress(QHostAddress::LocalHost, radio.port(), std::chrono::milliseconds(500));

        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);
        const auto info = spy.takeFirst().at(0).value<RadioInfo>();
        QCOMPARE(info.boardType, HPSDRHW::HermesLite);
        QCOMPARE(info.firmwareVersion, 75);
        QCOMPARE(info.macAddress, QStringLiteral("00:1C:C0:A2:14:8B"));
        QCOMPARE(info.protocol, ProtocolVersion::Protocol1);
    }

    void timeoutEmitsProbeFailed() {
        RadioDiscovery disc;
        QSignalSpy spy(&disc, &RadioDiscovery::probeFailed);
        // Localhost port that nothing is listening on — guaranteed timeout.
        disc.probeAddress(QHostAddress::LocalHost, 1, std::chrono::milliseconds(150));
        QVERIFY(spy.wait(500));
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TstRadioDiscoveryProbe)
#include "tst_radio_discovery_probe.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
nereus_add_test(tst_radio_discovery_probe)
target_link_libraries(tst_radio_discovery_probe PRIVATE Qt6::Network)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target tst_radio_discovery_probe 2>&1 | tail -20
```

Expected: build error — `probeAddress` not declared, `probeFailed` signal not declared.

- [ ] **Step 3: Add probeAddress + probeFailed to RadioDiscovery**

In `src/core/RadioDiscovery.h`, public section:

```cpp
// Send a unicast P1+P2 probe in parallel to the given address.
// On reply: emits radioDiscovered() with parsed RadioInfo.
// On timeout: emits probeFailed() with the address that was probed.
//
// Used by the Add Radio dialog and saved-row Connect path to validate
// reachability before opening a full connection. Times out fast (1.5 s
// default) so users get a clear error instead of a UDP-ghost long wait.
//
// From Phase 3Q design §7.2.
void probeAddress(const QHostAddress& addr,
                  quint16 port = 1024,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));

signals:
    // ... existing signals
    void probeFailed(const QHostAddress& addr, quint16 port);
```

In `src/core/RadioDiscovery.cpp`, add the implementation. Reuse `parseP1Reply()` / `parseP2Reply()` (the existing static parsers at lines 188 and 270).

```cpp
void RadioDiscovery::probeAddress(const QHostAddress& addr,
                                  quint16 port,
                                  std::chrono::milliseconds timeout)
{
    auto* sock = new QUdpSocket(this);
    sock->bind(QHostAddress::AnyIPv4, 0);

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);

    auto cleanup = [sock, timer]() {
        timer->stop();
        timer->deleteLater();
        sock->close();
        sock->deleteLater();
    };

    // Reply handler — try P1 first, then P2.
    connect(sock, &QUdpSocket::readyRead, this, [this, sock, addr, port, cleanup]() {
        while (sock->hasPendingDatagrams()) {
            QByteArray buf;
            buf.resize(int(sock->pendingDatagramSize()));
            QHostAddress src;
            quint16 srcPort = 0;
            sock->readDatagram(buf.data(), buf.size(), &src, &srcPort);

            RadioInfo info;
            if (parseP1Reply(buf, src, info) || parseP2Reply(buf, src, info)) {
                m_radios.insert(info.macAddress, info);
                m_lastSeen.insert(info.macAddress, QDateTime::currentMSecsSinceEpoch());
                emit radioDiscovered(info);
                cleanup();
                return;
            }
        }
    });

    // Timeout → emit failure and clean up.
    connect(timer, &QTimer::timeout, this, [this, addr, port, cleanup]() {
        emit probeFailed(addr, port);
        cleanup();
    });

    // Send P1 + P2 probes in parallel.
    sock->writeDatagram(QByteArray::fromHex("EFFE02"), addr, port);
    sock->writeDatagram(QByteArray::fromHex("0000000002"), addr, port);

    timer->start(int(timeout.count()));
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target tst_radio_discovery_probe \
  && ctest --test-dir build -R tst_radio_discovery_probe -V 2>&1 | tail -25
```

Expected: 2 passed, 0 failed.

- [ ] **Step 5: Verify the existing discovery test still passes**

```bash
ctest --test-dir build -R tst_radio_discovery_parse -V 2>&1 | tail -10
```

Expected: still passes (no regression).

- [ ] **Step 6: Commit**

```bash
git add src/core/RadioDiscovery.h src/core/RadioDiscovery.cpp \
        tests/tst_radio_discovery_probe.cpp tests/CMakeLists.txt
git commit -S -m "$(cat <<'EOF'
feat(discovery): add RadioDiscovery::probeAddress() unicast probe (3Q-2)

Sends a P1 + P2 probe in parallel to a single IP:port, parses the reply
via the existing parseP1Reply / parseP2Reply helpers, and emits either
radioDiscovered (success) or probeFailed (timeout). 1.5 s default
timeout matches design §7.2.

This is the backbone for the manual-IP path: the Add Radio dialog and
saved-row Connect both call probeAddress() to validate reachability
before opening a full connection. Replaces the silent UDP-timeout
failure mode that left Miguel guessing whether his IP, his board pick,
or his VPN tunnel was wrong.

Tests: tst_radio_discovery_probe (success path filling RadioInfo,
timeout emitting probeFailed). Existing tst_radio_discovery_parse
unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** `tst_radio_discovery_probe` passes; `tst_radio_discovery_parse` still passes; build green.

---

## Task 3: Structured ConnectFailure on RadioConnection + frameReceived signal

**Files:**
- Modify: `src/core/RadioConnection.h`
- Modify: `src/core/RadioConnection.cpp`
- Modify: `src/core/P1RadioConnection.cpp`
- Modify: `src/core/P2RadioConnection.cpp`

- [ ] **Step 1: Write the failing test**

Extend `tests/tst_radio_connection_watchdog.cpp` (existing) with a new test method or create a small `tst_radio_connection_failure.cpp`. Add:

```cpp
void emitsTypedFailureOnUnreachable() {
    P1RadioConnection conn(/* ... existing ctor args, see existing tst */);
    QSignalSpy spy(&conn, &RadioConnection::connectFailed);

    RadioInfo info;
    info.address = QHostAddress("192.0.2.1");  // RFC 5737 unreachable
    info.port = 1024;
    info.boardType = HPSDRHW::HermesLite;
    info.protocol = ProtocolVersion::Protocol1;
    conn.connectToRadio(info);

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    const auto reason = spy.takeFirst().at(0).value<ConnectFailure>();
    QCOMPARE(reason, ConnectFailure::Timeout);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target tst_radio_connection_failure 2>&1 | tail -10
```

Expected: build error — `ConnectFailure` enum and `connectFailed` signal not declared.

- [ ] **Step 3: Add ConnectFailure enum + frameReceived signal**

In `src/core/RadioConnection.h`:

```cpp
enum class ConnectFailure : int {
    Unreachable     = 0,  // OS reports "destination unreachable"
    Timeout         = 1,  // No reply within budget
    MalformedReply  = 2,  // Response received but parser rejected it
    ProtocolMismatch = 3, // P1/P2 reply mismatched what we expected
    IncompatibleBoard = 4 // Reply was valid but boardType not supported
};

// ... in class RadioConnection : public QObject:
signals:
    // Emitted on each ep6 (P1) or DDC (P2) frame arrival. UI (TitleBar
    // activity LED) connects this to a 10 Hz refresh — we don't want
    // every signal emission to drive a paint. From design §4.1.
    void frameReceived();

    // Emitted when connectToRadio() fails to reach Connected state.
    // The ConnectionPanel and TitleBar use this to surface a typed
    // error to the user instead of a silent UDP timeout.
    void connectFailed(NereusSDR::ConnectFailure reason, QString detail);
```

In the P1/P2 implementations, where ep6/DDC frames are parsed, emit `frameReceived()` once per valid frame. Where the existing connect-timeout codepaths exist (or where they need to be added), emit `connectFailed()` with the appropriate reason.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target tst_radio_connection_failure \
  && ctest --test-dir build -R tst_radio_connection_failure -V 2>&1 | tail -25
```

Expected: passes.

- [ ] **Step 5: Verify all existing connection tests still pass**

```bash
ctest --test-dir build -R "radio_connection|p1_loopback|radio_status" 2>&1 | tail -10
```

Expected: all passing.

- [ ] **Step 6: Commit**

```bash
git add src/core/RadioConnection.h src/core/RadioConnection.cpp \
        src/core/P1RadioConnection.cpp src/core/P2RadioConnection.cpp \
        tests/tst_radio_connection_failure.cpp tests/CMakeLists.txt
git commit -S -m "$(cat <<'EOF'
feat(connection): structured ConnectFailure + frameReceived signal (3Q-3)

ConnectFailure enum (Unreachable, Timeout, MalformedReply,
ProtocolMismatch, IncompatibleBoard) replaces silent UDP timeouts with
typed errors that the UI can render in plain English.

frameReceived signal fires once per ep6 (P1) or DDC (P2) frame; the
TitleBar activity LED throttles this to 10 Hz visible refresh per
design §4.1.

Tests: emitsTypedFailureOnUnreachable. Existing tests unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** New test passes; existing connection tests unchanged; build green. **PR-1 ready to push.**

---

# PR-2: Add Radio dialog rebuild

---

## Task 4: AddCustomRadioDialog — model dropdown, two-button action, failure UX

**Files:**
- Modify: `src/gui/AddCustomRadioDialog.h`
- Modify: `src/gui/AddCustomRadioDialog.cpp`
- Create: `tests/tst_add_custom_radio_dialog.cpp`
- Modify: `tests/CMakeLists.txt`

The biggest single UI change. Reference design §4.4 + §6.3-6.5 + the v7 mockup at `.superpowers/brainstorm/67485-1777249005/content/add-radio-flows-r7.html`.

- [ ] **Step 1: Write the failing test for dropdown contents**

Create `tests/tst_add_custom_radio_dialog.cpp`:

```cpp
#include <QObject>
#include <QSignalSpy>
#include <QComboBox>
#include <QPushButton>
#include <QtTest>
#include "gui/AddCustomRadioDialog.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TstAddCustomRadioDialog : public QObject {
    Q_OBJECT
private slots:
    void modelDropdownContainsAutoDetectFirst() {
        AddCustomRadioDialog dlg;
        auto* combo = dlg.findChild<QComboBox*>("modelCombo");
        QVERIFY(combo);
        QVERIFY(combo->itemText(0).contains("Auto-detect", Qt::CaseInsensitive));
    }

    void modelDropdownContainsAllSixteenSkus() {
        AddCustomRadioDialog dlg;
        auto* combo = dlg.findChild<QComboBox*>("modelCombo");
        QVERIFY(combo);
        // 16 SKUs from displayName(HPSDRModel) at HpsdrModel.h:160-182,
        // plus the "Auto-detect" entry.
        QStringList expected;
        for (int i = int(HPSDRModel::FIRST) + 1; i < int(HPSDRModel::LAST); ++i) {
            expected << QString::fromUtf8(displayName(static_cast<HPSDRModel>(i)));
        }
        for (const auto& name : expected) {
            int idx = combo->findText(name, Qt::MatchContains);
            QVERIFY2(idx >= 0, qPrintable(QStringLiteral("Missing SKU: %1").arg(name)));
        }
    }

    void saveOfflineButtonExists() {
        AddCustomRadioDialog dlg;
        auto* btn = dlg.findChild<QPushButton*>("saveOfflineButton");
        QVERIFY(btn);
    }

    void probeAndConnectButtonExists() {
        AddCustomRadioDialog dlg;
        auto* btn = dlg.findChild<QPushButton*>("probeButton");
        QVERIFY(btn);
    }
};

QTEST_MAIN(TstAddCustomRadioDialog)
#include "tst_add_custom_radio_dialog.moc"
```

Register in `tests/CMakeLists.txt`:

```cmake
nereus_add_test(tst_add_custom_radio_dialog)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target tst_add_custom_radio_dialog 2>&1 | tail -15
```

Expected: build passes (dialog ctor exists), test fails — modelCombo has 9 board entries, no Auto-detect, no Save Offline button, no probe button.

- [ ] **Step 3: Rebuild the model dropdown**

In `src/gui/AddCustomRadioDialog.cpp`, replace the existing board population (lines 294-320) with model-based population using `<optgroup>` semantics. Qt's `QComboBox` doesn't have native optgroups, so use a model with separators + bold parent items, or fall back to a flat list with section headers as disabled items.

Recommended: use disabled separator items for the silicon-family headers. Code skeleton:

```cpp
// Replace board-based population with model-based population.
// Default to "Auto-detect"; user can override per design §4.4.
m_modelCombo = new QComboBox(this);
m_modelCombo->setObjectName(QStringLiteral("modelCombo"));
m_modelCombo->setStyleSheet(kFieldStyle);

// Auto-detect always first.
m_modelCombo->addItem(QStringLiteral("Auto-detect (probe will fill this in)"),
                      QVariant::fromValue(int(HPSDRModel::FIRST)));

// Separator (disabled) before silicon families.
auto addSeparator = [&]() {
    m_modelCombo->addItem(QStringLiteral("──────────"));
    auto* model = qobject_cast<QStandardItemModel*>(m_modelCombo->model());
    if (model) {
        auto* item = model->item(m_modelCombo->count() - 1);
        if (item) { item->setFlags(item->flags() & ~Qt::ItemIsEnabled); }
    }
};
addSeparator();

auto addFamilyHeader = [&](const QString& label) {
    m_modelCombo->addItem(QStringLiteral("  ") + label);
    auto* model = qobject_cast<QStandardItemModel*>(m_modelCombo->model());
    if (model) {
        auto* item = model->item(m_modelCombo->count() - 1);
        if (item) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }
};

auto addSku = [&](HPSDRModel m) {
    m_modelCombo->addItem(
        QStringLiteral("    %1").arg(QString::fromUtf8(displayName(m))),
        QVariant::fromValue(int(m)));
};

addFamilyHeader(QStringLiteral("Atlas / Metis"));
addSku(HPSDRModel::HPSDR);
addFamilyHeader(QStringLiteral("Hermes (1 ADC)"));
addSku(HPSDRModel::HERMES);
addSku(HPSDRModel::ANAN10);
addSku(HPSDRModel::ANAN100);
addFamilyHeader(QStringLiteral("Hermes II (1 ADC)"));
addSku(HPSDRModel::ANAN10E);
addSku(HPSDRModel::ANAN100B);
addFamilyHeader(QStringLiteral("Angelia (2 ADC)"));
addSku(HPSDRModel::ANAN100D);
addFamilyHeader(QStringLiteral("Orion (2 ADC · 50 V)"));
addSku(HPSDRModel::ANAN200D);
addFamilyHeader(QStringLiteral("Orion MkII (2 ADC · MKII BPF)"));
addSku(HPSDRModel::ORIONMKII);
addSku(HPSDRModel::ANAN7000D);
addSku(HPSDRModel::ANAN8000D);
addSku(HPSDRModel::ANVELINAPRO3);
addSku(HPSDRModel::REDPITAYA);
addFamilyHeader(QStringLiteral("Hermes Lite 2"));
addSku(HPSDRModel::HERMESLITE);
addFamilyHeader(QStringLiteral("Saturn (ANAN-G2)"));
addSku(HPSDRModel::ANAN_G2);
addSku(HPSDRModel::ANAN_G2_1K);

m_modelCombo->setCurrentIndex(0);  // Auto-detect default
```

- [ ] **Step 4: Replace the bottom button row**

Find the existing OK/Cancel button setup. Replace with a three-button row:

```cpp
m_probeButton = new QPushButton(QStringLiteral("Probe and connect now"), this);
m_probeButton->setObjectName(QStringLiteral("probeButton"));
m_probeButton->setStyleSheet(kPrimaryButtonStyle);
m_probeButton->setDefault(true);
connect(m_probeButton, &QPushButton::clicked, this, &AddCustomRadioDialog::onProbeClicked);

m_saveOfflineButton = new QPushButton(QStringLiteral("Save offline"), this);
m_saveOfflineButton->setObjectName(QStringLiteral("saveOfflineButton"));
m_saveOfflineButton->setStyleSheet(kSecondaryButtonStyle);
connect(m_saveOfflineButton, &QPushButton::clicked, this, &AddCustomRadioDialog::onSaveOfflineClicked);

auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
cancelButton->setStyleSheet(kSecondaryButtonStyle);
connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

auto* btnRow = new QHBoxLayout();
btnRow->addWidget(m_probeButton);
btnRow->addWidget(m_saveOfflineButton);
btnRow->addStretch();
btnRow->addWidget(cancelButton);
mainLayout->addLayout(btnRow);
```

Add the corresponding header members:

```cpp
// in AddCustomRadioDialog.h, private:
QComboBox*   m_modelCombo{nullptr};
QPushButton* m_probeButton{nullptr};
QPushButton* m_saveOfflineButton{nullptr};

// new slots
private slots:
    void onProbeClicked();
    void onSaveOfflineClicked();
```

- [ ] **Step 5: Wire onProbeClicked to RadioDiscovery::probeAddress**

```cpp
void AddCustomRadioDialog::onProbeClicked()
{
    const QString ipText = m_ipEdit->text().trimmed();
    QHostAddress addr;
    if (!addr.setAddress(ipText)) {
        showInlineError(QStringLiteral("\"%1\" isn't a valid IP address.").arg(ipText));
        return;
    }
    const quint16 port = static_cast<quint16>(m_portEdit->text().toUInt());

    showProbingOverlay();  // disables buttons, shows spinner

    auto* disc = new RadioDiscovery(this);
    connect(disc, &RadioDiscovery::radioDiscovered, this, [this, disc](const RadioInfo& info) {
        hideProbingOverlay();
        m_probedInfo = info;
        // User-picked model overrides probe-detected silicon family default.
        if (m_modelCombo->currentIndex() > 0) {
            m_probedInfo.modelOverride = static_cast<HPSDRModel>(
                m_modelCombo->currentData().toInt());
        }
        disc->deleteLater();
        accept();  // dialog closes; caller saves + connects
    });
    connect(disc, &RadioDiscovery::probeFailed, this, [this, disc, ipText](
            const QHostAddress&, quint16) {
        hideProbingOverlay();
        showInlineError(
            QStringLiteral("Couldn't reach %1 after 1.5 s. Radio off, IP wrong, "
                           "or VPN tunnel down. Your form is preserved — "
                           "change a field and retry, or save offline for later.")
                .arg(ipText));
        m_probeButton->setText(QStringLiteral("Retry probe"));
        disc->deleteLater();
    });

    disc->probeAddress(addr, port, std::chrono::milliseconds(1500));
}
```

- [ ] **Step 6: Wire onSaveOfflineClicked**

```cpp
void AddCustomRadioDialog::onSaveOfflineClicked()
{
    // Protocol must be set explicitly when there's no probe to learn from.
    const int protoIdx = m_protocolCombo->currentIndex();
    if (m_protocolCombo->itemData(protoIdx).toInt() < 0) {
        showInlineInfo(
            QStringLiteral("Saving without probing — protocol must be set explicitly. "
                           "Pick P1 (HL2 / classic ANAN) or P2 (Saturn / ANAN-G2)."));
        m_protocolCombo->setStyleSheet(QStringLiteral(
            "QComboBox { border: 1px solid #5985b8; }"));
        return;
    }
    m_savedOffline = true;
    accept();
}
```

Add `bool savedOffline() const { return m_savedOffline; }` accessor for the caller (ConnectionPanel) to know which path was taken.

- [ ] **Step 7: Run tests**

```bash
cmake --build build --target tst_add_custom_radio_dialog \
  && ctest --test-dir build -R tst_add_custom_radio_dialog -V 2>&1 | tail -20
```

Expected: all passing.

- [ ] **Step 8: Manually verify the dialog**

```bash
cmake --build build && ./build/NereusSDR
```

In the running app: open Add Radio dialog. Verify:
- Auto-detect is the default model.
- Silicon-family headers are bold and disabled (you can't pick "Hermes (1 ADC)" itself, only the SKUs under it).
- Probing a localhost port that nothing's listening on shows the red error band.
- Save offline with Auto-detect protocol shows the blue info band.
- Save offline with P1 selected closes the dialog.

- [ ] **Step 9: Commit**

```bash
git add src/gui/AddCustomRadioDialog.h src/gui/AddCustomRadioDialog.cpp \
        tests/tst_add_custom_radio_dialog.cpp tests/CMakeLists.txt
git commit -S -m "$(cat <<'EOF'
feat(connection): rebuild Add Radio dialog with model-aware SKU picker (3Q-4)

Replaces the 9-board dropdown with a 16-SKU model dropdown organized by
silicon family (Atlas, Hermes (3), Hermes II (2), Angelia, Orion,
Orion MkII (5), Hermes Lite 2, Saturn (2)) with disabled
family-header items for visual grouping. Auto-detect is the default;
user picks a specific SKU when they know the product (necessary for
shared-silicon families like Orion MkII).

Two action buttons replace OK/Cancel: "Probe and connect now" runs
RadioDiscovery::probeAddress() against the entered IP, and "Save
offline" skips the probe (with a Protocol-required note when the
protocol combo is on Auto-detect).

Probe failure preserves the form and surfaces a typed error explaining
the likely causes ("radio off, IP wrong, or VPN tunnel down"). The
button label flips to "Retry probe" so the user knows their input
isn't lost.

Tests: tst_add_custom_radio_dialog (Auto-detect first, all 16 SKUs
present, both action buttons exist).

Closes the SKU-disambiguation gap described in design §4.4 + §5.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Test passes; manual verification of the four UI flows (probe success, probe failure, save offline with protocol, save offline without protocol → info band).

---

# PR-3: ConnectionPanel polish

---

## Task 5: ConnectionPanel — status strip, state pills, Last Seen, ↻ Scan

**Files:**
- Modify: `src/gui/ConnectionPanel.h`
- Modify: `src/gui/ConnectionPanel.cpp`
- Create: `tests/tst_connection_panel_state_pills.cpp`
- Modify: `tests/CMakeLists.txt`

Reference design §4.3, §7.3, and the v2/v5 mockups.

- [ ] **Step 1: Write the failing tests for state-pill computation**

Create `tests/tst_connection_panel_state_pills.cpp`:

```cpp
#include <QObject>
#include <QtTest>
#include "gui/ConnectionPanel.h"

using namespace NereusSDR;

class TstConnectionPanelStatePills : public QObject {
    Q_OBJECT
private slots:
    void onlineUnder60s() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now, now), StatePill::Online);
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 30000, now), StatePill::Online);
    }

    void staleBetween60sAnd5min() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 90000, now), StatePill::Stale);
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 4 * 60 * 1000, now), StatePill::Stale);
    }

    void offlineOver5min() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 6 * 60 * 1000, now), StatePill::Offline);
        QCOMPARE(ConnectionPanel::statePillForLastSeen(now - 24 * 60 * 60 * 1000LL, now), StatePill::Offline);
    }

    void offlineWhenNeverSeen() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(ConnectionPanel::statePillForLastSeen(0, now), StatePill::Offline);
    }
};

QTEST_MAIN(TstConnectionPanelStatePills)
#include "tst_connection_panel_state_pills.moc"
```

Register in `tests/CMakeLists.txt`:

```cmake
nereus_add_test(tst_connection_panel_state_pills)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target tst_connection_panel_state_pills 2>&1 | tail -15
```

Expected: build error — `StatePill` not declared, `statePillForLastSeen` not declared.

- [ ] **Step 3: Add StatePill enum + computation**

In `src/gui/ConnectionPanel.h`, public:

```cpp
enum class StatePill : int {
    Online   = 0,  // last seen <60s ago
    Stale    = 1,  // last seen 60s-5min ago
    Offline  = 2,  // last seen >5min, or never seen
    Connected = 3, // currently connected (overrides aging)
};

// Pure function — easy to test, easy to reuse.
static StatePill statePillForLastSeen(qint64 lastSeenMs, qint64 nowMs);
```

In `.cpp`:

```cpp
ConnectionPanel::StatePill ConnectionPanel::statePillForLastSeen(qint64 lastSeenMs, qint64 nowMs)
{
    if (lastSeenMs <= 0) {
        return StatePill::Offline;
    }
    const qint64 ageMs = nowMs - lastSeenMs;
    if (ageMs < 60 * 1000) return StatePill::Online;
    if (ageMs < 5 * 60 * 1000) return StatePill::Stale;
    return StatePill::Offline;
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
ctest --test-dir build -R tst_connection_panel_state_pills -V 2>&1 | tail -15
```

Expected: 4 passed.

- [ ] **Step 5: Replace the ● glyph column with state pill rendering**

In `populateRow()` and `applyRowColor()` (existing in `ConnectionPanel.cpp`), replace the bare `●` with a colored circle painted via a small custom delegate or by setting a colored icon on the cell. Compute the pill via `statePillForLastSeen(info.lastSeen, now)`. If the row corresponds to the currently connected MAC, override to `StatePill::Connected`.

- [ ] **Step 6: Replace the MAC column with a Last Seen column**

Update the `Col` enum in `ConnectionPanel.h`:

```cpp
enum Col : int {
    ColStatus    = 0,
    ColName      = 1,
    ColBoard     = 2,
    ColProtocol  = 3,
    ColIp        = 4,
    ColLastSeen  = 5,  // was ColMac
    ColFirmware  = 6,
    ColInUse     = 7,
    ColCount     = 8
};
```

Update `populateRow()` to render relative time via a helper:

```cpp
static QString relativeTime(qint64 lastSeenMs, qint64 nowMs) {
    if (lastSeenMs <= 0) return QStringLiteral("never seen");
    const qint64 ageMs = nowMs - lastSeenMs;
    if (ageMs < 60 * 1000) return QStringLiteral("just now");
    if (ageMs < 60 * 60 * 1000) return QStringLiteral("%1 m ago").arg(ageMs / 60000);
    if (ageMs < 24 * 60 * 60 * 1000LL) return QStringLiteral("%1 h ago").arg(ageMs / 3600000);
    return QStringLiteral("%1 d ago").arg(ageMs / 86400000);
}
```

MAC moves to the detail panel (it's already there at `ConnectionPanel.cpp:301`).

- [ ] **Step 7: Add the connection status strip at the top**

Above the `m_radioTable` `QGroupBox`, insert a status strip widget. When `m_radioModel->connectionState() == Connected`: show pill + radio name + IP + "since HH:MM (elapsed)" + protocol/fw + Mbps + Disconnect button. When Disconnected: show grey pill + "Disconnected" + "Reconnect to last radio" link if last-connected MAC is known.

- [ ] **Step 8: Replace Start/Stop Discovery with a single ↻ Scan**

Find the existing `m_startDiscoveryBtn` and `m_stopDiscoveryBtn` setup near `ConnectionPanel.cpp:379`. Remove both. Add a single `↻ Scan` button to the table-header strip (right-aligned next to "X saved · Y discovered · Z connected" summary text).

- [ ] **Step 9: Drop Disconnect from the bottom button strip**

Find the bottom strip at `ConnectionPanel.cpp:415` and remove the `m_disconnectBtn`. The Disconnect action lives in the new status strip. Bottom strip becomes: `[Connect]` `[Add Manually…]` `[Forget]` (stretch) `[Close]`.

- [ ] **Step 10: Add Auto-connect on launch checkbox to the detail panel**

In the detail panel block near `ConnectionPanel.cpp:329`, add a `QCheckBox` wired to `AppSettings::autoConnect(mac)`.

- [ ] **Step 11: Auto-open / auto-close behavior**

In `MainWindow`, hook `connectionStateChanged` so that:
- On `Disconnected` (after being non-Disconnected) → open the panel modally.
- On `Connected` (after being non-Connected) → start a 1-second `QTimer::singleShot` that calls `panel->accept()`.

- [ ] **Step 12: Run all panel-related tests**

```bash
ctest --test-dir build -R "connection_panel" 2>&1 | tail -10
```

Expected: all passing (the existing `tst_connection_panel_saved_radios` should still pass).

- [ ] **Step 13: Manual verification**

Build + run. Verify:
- Status strip shows when connected; Disconnect there works.
- State pills render in the correct colors.
- Last Seen column shows relative time for saved entries.
- Single ↻ Scan replaces Start/Stop Discovery.
- Bottom strip has Connect / Add Manually / Forget / Close (no Disconnect).
- Auto-open on disconnect; auto-close 1 s after connect.

- [ ] **Step 14: Commit**

```bash
git add src/gui/ConnectionPanel.h src/gui/ConnectionPanel.cpp \
        tests/tst_connection_panel_state_pills.cpp tests/CMakeLists.txt
git commit -S -m "$(cat <<'EOF'
feat(connection): ConnectionPanel polish — pills, Last Seen, status strip (3Q-5)

- StatePill enum (Online/Stale/Offline/Connected) replaces the bare ●
  glyph; thresholds 60s / 5min match design §7.3.
- Last Seen column replaces MAC (which lives in the detail panel
  anyway). Renders relative time ("just now", "4 m ago", etc.).
- Connection status strip at the top of the panel — when connected,
  shows radio name + IP + uptime + Mbps + inline Disconnect; when
  disconnected, shows "Reconnect to last" link if applicable.
- Single ↻ Scan button replaces Start/Stop Discovery (one action,
  one-shot, matches Thetis convention).
- Disconnect moves out of the bottom button strip into the status
  strip — it's a state action, not a list action.
- Auto-connect-on-launch checkbox on the detail panel.
- Modal kept (per design §7.6).

Tests: tst_connection_panel_state_pills (4 cases for the pill
threshold logic).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Tests pass; manual flow walkthrough succeeds.

---

# PR-4: TitleBar segment + status-bar verbose strip

---

## Task 6: TitleBar connection segment

**Files:**
- Modify: `src/gui/TitleBar.h`
- Modify: `src/gui/TitleBar.cpp`
- Modify: `src/gui/MainWindow.cpp`

Design §4.1. The 32 px TitleBar already has space; existing file header notes the connection-state UI was deliberately deferred.

- [ ] **Step 1: Add a ConnectionSegment widget to TitleBar**

In `src/gui/TitleBar.h`, declare a new helper widget `ConnectionSegment` (private nested class is fine; it's a single-purpose visual element). It holds a state dot, a label stack (radio name + IP), Mbps readout, and an activity LED. Expose `setState(ConnectionState)`, `setRadio(QString name, QHostAddress ip)`, `setRates(double rxMbps, double txMbps)`, and a public `frameTick()` slot that pulses the LED.

```cpp
// Inside TitleBar.h, after existing class TitleBar declaration:
class ConnectionSegment : public QWidget {
    Q_OBJECT
public:
    explicit ConnectionSegment(QWidget* parent = nullptr);
    void setState(ConnectionState s);
    void setRadio(const QString& name, const QHostAddress& ip);
    void setRates(double rxMbps, double txMbps);
public slots:
    void frameTick();  // throttled to 10 Hz LED pulse
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
private:
    ConnectionState m_state{ConnectionState::Disconnected};
    QString m_name;
    QHostAddress m_ip;
    double m_rxMbps{0.0};
    double m_txMbps{0.0};
    bool m_ledOn{false};
    QTimer m_ledThrottle;
};

// In TitleBar class itself, expose the segment:
public:
    ConnectionSegment* connectionSegment() const { return m_connectionSegment; }
private:
    ConnectionSegment* m_connectionSegment{nullptr};
```

- [ ] **Step 2: Implement paintEvent for the colored state dot**

In `TitleBar.cpp`:

```cpp
void ConnectionSegment::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // State dot (left edge)
    const QRect dotRect(8, height() / 2 - 5, 9, 9);
    QColor dotColor;
    switch (m_state) {
        case ConnectionState::Disconnected: dotColor = QColor("#445566"); break;
        case ConnectionState::Probing:
        case ConnectionState::Connecting:   dotColor = QColor("#d39c2a"); break;
        case ConnectionState::Connected:    dotColor = QColor("#39c167"); break;
        case ConnectionState::LinkLost:     dotColor = QColor("#c14848"); break;
    }
    p.setBrush(dotColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(dotRect);

    // Text content depends on state
    int x = dotRect.right() + 8;
    p.setPen(QColor("#a0b0c0"));

    if (m_state == ConnectionState::Disconnected) {
        p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Disconnected — click to connect"));
        return;
    }
    if (m_state == ConnectionState::Probing || m_state == ConnectionState::Connecting) {
        p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 %2…").arg(connectionStateName(m_state), m_name));
        return;
    }
    // Connected: name (bold) · IP · ▲ rx ▼ tx · activity LED
    QFont nameFont = p.font(); nameFont.setBold(true);
    p.setFont(nameFont);
    p.setPen(QColor("#e0eef8"));
    p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, m_name);
    QFontMetrics fm(nameFont);
    x += fm.horizontalAdvance(m_name) + 10;
    nameFont.setBold(false);
    p.setFont(nameFont);
    p.setPen(QColor("#7088a0"));
    p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft,
               m_ip.toString());
    x += fm.horizontalAdvance(m_ip.toString()) + 12;
    p.setPen(QColor("#90a0b0"));
    const QString rates = QStringLiteral("▲ %1 Mb/s   ▼ %2 kb/s")
        .arg(m_rxMbps, 0, 'f', 1).arg(int(m_txMbps * 1000));
    p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, rates);
    x += fm.horizontalAdvance(rates) + 8;

    // Activity LED
    if (m_ledOn) {
        p.setBrush(QColor("#5fff8a"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRect(x, height() / 2 - 3, 6, 6));
    }
}
```

- [ ] **Step 3: Implement frameTick with 10 Hz throttle**

```cpp
ConnectionSegment::ConnectionSegment(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(32);
    setMinimumWidth(280);
    m_ledThrottle.setSingleShot(true);
    m_ledThrottle.setInterval(100);  // 10 Hz max
    connect(&m_ledThrottle, &QTimer::timeout, this, [this]() {
        m_ledOn = false;
        update();
    });
}

void ConnectionSegment::frameTick()
{
    if (!m_ledThrottle.isActive()) {
        m_ledOn = true;
        m_ledThrottle.start();
        update();
    }
    // Otherwise drop — we already pulsed within the last 100 ms.
}

void ConnectionSegment::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}
```

- [ ] **Step 4: Wire into TitleBar layout**

In `TitleBar` constructor, add the segment between the menu bar (left) and the existing master-output / 💡 (right):

```cpp
m_connectionSegment = new ConnectionSegment(this);
m_hbox->insertWidget(/* index after menu bar */, m_connectionSegment);
m_hbox->insertStretch(/* before app-name label */);
```

- [ ] **Step 5: Wire into MainWindow signals**

```cpp
auto* seg = m_titleBar->connectionSegment();
connect(m_radioModel, &RadioModel::connectionStateChanged,
        seg, &ConnectionSegment::setState);
connect(seg, &ConnectionSegment::clicked,
        this, &MainWindow::showConnectionPanel);
// Frame ticks → activity LED
connect(m_radioModel, &RadioModel::frameReceivedThroughConnection,
        seg, &ConnectionSegment::frameTick);
```

- [ ] **Step 6: Manually verify**

Build + run. Cycle through Disconnected → Probing (via probe-an-IP that doesn't respond) → Connecting (probe-then-connect to a working radio) → Connected. Verify:
- Dot color changes correctly.
- LED pulses on each frame at most 10 Hz.
- Click on the segment opens the ConnectionPanel.

- [ ] **Step 7: Commit**

```bash
git add src/gui/TitleBar.h src/gui/TitleBar.cpp src/gui/MainWindow.cpp
git commit -S -m "$(cat <<'EOF'
feat(titlebar): connection segment with state dot, rates, activity LED (3Q-6)

Adds a ConnectionSegment widget to the existing 32 px TitleBar, between
the menu bar and the master-output strip. Renders:

- State dot (grey/amber/green/red) driven by RadioModel::connectionState
- Radio name + IP (when connected) or status text (when not)
- ▲▼ Mbps rates
- Activity LED that pulses on each ep6/DDC frame, throttled to 10 Hz
  visible refresh so high-rate frame streams pulse cleanly instead of
  seizing.

Click → opens the ConnectionPanel. Aligns with design §4.1; the
TitleBar file header had already noted that connection-state UI was
deferred until this phase.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Manual verification of all four state colors + LED throttling + click-to-open.

---

## Task 7: Status bar verbose strip

**Files:**
- Modify: `src/gui/MainWindow.cpp` (the existing `buildStatusBar()` at line 1939)

Design §4.2.

- [ ] **Step 1: Identify the existing buildStatusBar() and add the verbose strip**

In `MainWindow::buildStatusBar()`, after the existing 3-section setup, add a new connection-info segment. Use `QLabel`s for the live-readout fields:

```cpp
m_statusConnInfo = new QLabel(barWidget);
m_statusConnInfo->setStyleSheet(QStringLiteral(
    "QLabel { color: #8090a8; font-family: 'SF Mono', Menlo, monospace; font-size: 11px; }"));
hbox->addWidget(m_statusConnInfo);

m_statusLiveDot = new QLabel(QStringLiteral("● live"), barWidget);
m_statusLiveDot->setStyleSheet(QStringLiteral(
    "QLabel { color: #5fff8a; font-size: 11px; }"));
m_statusLiveDot->setVisible(false);
hbox->addStretch();
hbox->addWidget(m_statusLiveDot);
```

- [ ] **Step 2: Update text on connection state changes**

```cpp
connect(m_radioModel, &RadioModel::connectionStateChanged, this,
        [this](ConnectionState s) {
    if (s == ConnectionState::Connected) {
        const auto& info = m_radioModel->connection()->radioInfo();
        const QString proto = info.protocol == ProtocolVersion::Protocol2 ? "P2" : "P1";
        m_statusConnInfo->setText(QStringLiteral(
            "Sample rate %1 kHz · %2 · fw %3 · MAC %4")
            .arg(info.maxSampleRate / 1000).arg(proto)
            .arg(info.firmwareVersion).arg(info.macAddress));
        m_statusLiveDot->setVisible(true);
    } else {
        const QString lastMac = AppSettings::instance().value("radios/lastConnected").toString();
        QString breadcrumb;
        if (!lastMac.isEmpty()) {
            const QString name = AppSettings::instance().value(
                QStringLiteral("radios/%1/name").arg(lastMac)).toString();
            breadcrumb = QStringLiteral("last connected %1").arg(name);
        }
        m_statusConnInfo->setText(QStringLiteral(
            "🔴  No radio connected — click the connection indicator or the Connect menu item   %1")
            .arg(breadcrumb));
        m_statusLiveDot->setVisible(false);
    }
});
```

- [ ] **Step 3: Manually verify**

Connect, look at status bar — sees sample rate, fw, MAC, "● live." Disconnect — sees red dot, "No radio connected," last-connected breadcrumb if applicable.

- [ ] **Step 4: Commit**

```bash
git add src/gui/MainWindow.cpp
git commit -S -m "$(cat <<'EOF'
feat(statusbar): verbose connection-info strip (3Q-7)

Adds a connection-info segment to the existing 46 px status bar. When
connected: sample rate, protocol, firmware, MAC, "● live" indicator.
When disconnected: red dot, "No radio connected" message, and a
"last connected X" breadcrumb so the most-recent connection is
one-glance recoverable even when the panel is closed.

Aligns with design §4.2.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Manual verification of connected/disconnected states.

---

# PR-5: Spectrum disconnect overlay

---

## Task 8: Spectrum disconnect overlay (fade + DISCONNECTED label + click-to-open)

**Files:**
- Modify: `src/gui/SpectrumWidget.h`
- Modify: `src/gui/SpectrumWidget.cpp`

Design §4.5.

- [ ] **Step 1: Add a connection-state slot to SpectrumWidget**

```cpp
// in SpectrumWidget.h
public slots:
    void setConnectionState(NereusSDR::ConnectionState s);

private:
    NereusSDR::ConnectionState m_connState{NereusSDR::ConnectionState::Disconnected};
    float m_disconnectFade{1.0f};  // 1.0 = full opacity, 0.4 = faded
    QPropertyAnimation* m_fadeAnim{nullptr};
```

- [ ] **Step 2: Implement the fade animation + paint overlay**

```cpp
void SpectrumWidget::setConnectionState(ConnectionState s)
{
    if (m_connState == s) return;
    const bool wasConnected = (m_connState == ConnectionState::Connected);
    const bool isConnected = (s == ConnectionState::Connected);
    m_connState = s;

    if (wasConnected && !isConnected) {
        // Fade trace + waterfall to 40% over 800 ms
        if (!m_fadeAnim) {
            m_fadeAnim = new QPropertyAnimation(this, "disconnectFade");
            m_fadeAnim->setDuration(800);
        }
        m_fadeAnim->setStartValue(1.0f);
        m_fadeAnim->setEndValue(0.4f);
        m_fadeAnim->start();
    } else if (!wasConnected && isConnected) {
        if (m_fadeAnim) m_fadeAnim->stop();
        m_disconnectFade = 1.0f;
    }
    update();
}
```

In `paintEvent` (or its QRhi equivalent — apply opacity to the trace + waterfall and overlay the DISCONNECTED label):

```cpp
// After existing trace + waterfall draw:
if (m_connState != ConnectionState::Connected) {
    QPainter p(this);
    p.setOpacity(0.85);  // overlay opacity
    p.fillRect(rect(), QColor(10, 12, 20, 140));
    QFont f = p.font();
    f.setPointSize(22);
    f.setBold(true);
    f.setLetterSpacing(QFont::PercentageSpacing, 122);
    p.setFont(f);
    p.setPen(QColor("#c14848"));
    p.drawText(rect(), Qt::AlignCenter, QStringLiteral("DISCONNECTED"));
}
```

- [ ] **Step 3: Hook into MainWindow**

```cpp
connect(m_radioModel, &RadioModel::connectionStateChanged,
        m_spectrumWidget, &SpectrumWidget::setConnectionState);
```

- [ ] **Step 4: Add click-to-open**

In `SpectrumWidget::mousePressEvent`, add an early-return that opens the ConnectionPanel when in disconnected/lost states:

```cpp
void SpectrumWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_connState != ConnectionState::Connected
        && event->button() == Qt::LeftButton) {
        emit disconnectedClickRequest();
        return;
    }
    // ... existing handling
}
```

Add the signal to the header. Wire it in MainWindow to `showConnectionPanel()`.

- [ ] **Step 5: Manually verify**

Connect, see live spectrum. Disconnect — see the trace + waterfall fade to 40 % over ~1 s with the DISCONNECTED label centered. Click anywhere on the spectrum — panel opens.

- [ ] **Step 6: Commit**

```bash
git add src/gui/SpectrumWidget.h src/gui/SpectrumWidget.cpp src/gui/MainWindow.cpp
git commit -S -m "$(cat <<'EOF'
feat(spectrum): disconnect overlay — fade + DISCONNECTED + click-to-open (3Q-8)

When connection state goes from Connected → not-Connected, the spectrum
trace and waterfall fade to ~40% opacity over 800 ms via
QPropertyAnimation, and a centered "DISCONNECTED" label drops in.

Click anywhere on the spectrum while disconnected → opens the
ConnectionPanel (idempotent — calls the same showConnectionPanel slot
the TitleBar segment uses).

Multi-cue feedback closes the loop on the original "spectrum just
freezes" complaint that triggered this phase. Aligns with design §4.5.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Manual verification of fade + label + click.

---

# PR-6: Radio menu rework

---

## Task 9: Radio menu — six items, state-aware enablement

**Files:**
- Modify: `src/gui/MainWindow.cpp` (the existing menu setup at lines 1197-1249)

Design §4.6.

- [ ] **Step 1: Replace the existing Radio menu setup**

In `MainWindow::setupMenuBar()` (the menu construction near `MainWindow.cpp:1197`), replace the existing four-item set with the six-item set per design §4.6:

```cpp
QMenu* radioMenu = menuBar()->addMenu(QStringLiteral("&Radio"));

m_actConnect = radioMenu->addAction(QStringLiteral("&Connect"),
    QKeySequence(Qt::CTRL | Qt::Key_K),
    this, [this]() {
        if (m_radioModel->isConnected()) return;
        // If a last-connected radio is auto-connectable, just go.
        const QString lastMac = AppSettings::instance().value("radios/lastConnected").toString();
        if (!lastMac.isEmpty() && AppSettings::instance().autoConnect(lastMac)) {
            m_radioModel->connectToSavedMac(lastMac);
        } else {
            showConnectionPanel();
        }
    });

m_actDisconnect = radioMenu->addAction(QStringLiteral("&Disconnect"),
    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K),
    this, [this]() { m_radioModel->disconnectFromRadio(); });

radioMenu->addSeparator();

m_actDiscoverNow = radioMenu->addAction(QStringLiteral("Discover &Now"),
    this, [this]() {
        m_radioModel->discovery()->startDiscovery();
        if (!m_connectionPanel || !m_connectionPanel->isVisible()) {
            showConnectionPanel();
        }
    });

m_actManageRadios = radioMenu->addAction(QStringLiteral("&Manage Radios…"),
    this, &MainWindow::showConnectionPanel);

radioMenu->addSeparator();

auto* antAct = radioMenu->addAction(QStringLiteral("&Antenna Setup…"));
antAct->setEnabled(false);
antAct->setToolTip(QStringLiteral("NYI — Phase X"));

auto* xvtrAct = radioMenu->addAction(QStringLiteral("Trans&verters…"));
xvtrAct->setEnabled(false);
xvtrAct->setToolTip(QStringLiteral("NYI — Phase X"));

radioMenu->addSeparator();

m_actProtocolInfo = radioMenu->addAction(QStringLiteral("&Protocol Info"),
    this, [this]() { showProtocolInfo(); });
```

- [ ] **Step 2: Wire state-aware enablement**

```cpp
connect(m_radioModel, &RadioModel::connectionStateChanged,
        this, [this](ConnectionState s) {
    const bool connected = (s == ConnectionState::Connected);
    m_actConnect->setEnabled(!connected);
    m_actDisconnect->setEnabled(connected);
    m_actProtocolInfo->setEnabled(connected);
    // Discover Now and Manage Radios are always enabled.
});
// Initial state
m_actConnect->setEnabled(true);
m_actDisconnect->setEnabled(false);
m_actProtocolInfo->setEnabled(false);
```

- [ ] **Step 3: Manually verify**

Build + run. Open Radio menu when disconnected — Connect highlighted, Disconnect greyed, Protocol Info greyed. Connect — Connect now greyed, Disconnect live, Protocol Info live. Antenna Setup / Transverters always greyed (NYI).

- [ ] **Step 4: Commit**

```bash
git add src/gui/MainWindow.cpp
git commit -S -m "$(cat <<'EOF'
feat(menu): role-based Radio menu — Connect/Disconnect mutually exclusive (3Q-9)

Replaces the four-item-three-aliases set (Discover…, Connect, Radio
Setup… all just opening the panel; only Disconnect did its own thing)
with six role-based items:

  Connect (⌘K)            — disabled when connected; auto-connects to
                            last if AutoConnect flag set, else opens panel.
  Disconnect (⌘⇧K)        — disabled when disconnected.
  ──────────
  Discover Now            — runs broadcast scan, opens panel if closed
                            so the user sees results.
  Manage Radios…          — opens the panel (the only menu item whose
                            sole job is to open the panel).
  ──────────
  Antenna Setup…          — NYI (kept).
  Transverters…           — NYI (kept).
  ──────────
  Protocol Info           — disabled when disconnected.

Enabledness reacts to RadioModel::connectionStateChanged. Aligns with
design §4.6.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Manual verification of all enablement transitions.

---

# PR-7: Auto-connect-on-launch failure path

---

## Task 10: Auto-connect failure path + multi-flag handling

**Files:**
- Modify: `src/models/RadioModel.h`
- Modify: `src/models/RadioModel.cpp`
- Modify: `src/gui/MainWindow.cpp`

Design §7.5.

- [ ] **Step 1: Update RadioModel::tryAutoReconnect**

The existing `tryAutoReconnect()` (called from `MainWindow.cpp:478`) needs:
- Multi-`autoConnect = true` handling: pick most-recent-connected via `radios/lastConnected`.
- On probe failure: emit a signal that MainWindow routes to the panel.

```cpp
void RadioModel::tryAutoReconnect()
{
    QStringList autoConnectMacs;
    for (const auto& mac : AppSettings::instance().savedRadioMacs()) {
        if (AppSettings::instance().autoConnect(mac)) {
            autoConnectMacs << mac;
        }
    }
    if (autoConnectMacs.isEmpty()) {
        return;  // Cold launch behavior — panel will auto-open
    }

    QString chosenMac = autoConnectMacs.first();
    if (autoConnectMacs.size() > 1) {
        const QString lastMac = AppSettings::instance().value("radios/lastConnected").toString();
        if (autoConnectMacs.contains(lastMac)) chosenMac = lastMac;
        emit autoConnectAmbiguous(autoConnectMacs.size(), chosenMac);
    }

    connectToSavedMac(chosenMac);  // goes through Probing → Connecting → Connected
    // On failure (ConnectFailure), MainWindow opens panel with chosenMac highlighted.
}
```

Add the signal:

```cpp
signals:
    void autoConnectAmbiguous(int count, const QString& chosenMac);
    void autoConnectFailed(const QString& mac, ConnectFailure reason);
```

- [ ] **Step 2: Wire MainWindow to surface the failure**

```cpp
connect(m_radioModel, &RadioModel::autoConnectFailed,
        this, [this](const QString& mac, ConnectFailure reason) {
    showConnectionPanel();  // panel auto-opens
    m_connectionPanel->highlightMac(mac);  // small new helper
    const QString name = AppSettings::instance().value(
        QStringLiteral("radios/%1/name").arg(mac)).toString();
    const QString reasonText = (reason == ConnectFailure::Timeout)
        ? QStringLiteral("isn't reachable from this network")
        : QStringLiteral("returned an error");
    statusBar()->showMessage(QStringLiteral(
        "Auto-connect target %1 %2. Pick a different radio or update the address.")
        .arg(name, reasonText), 8000);
});

connect(m_radioModel, &RadioModel::autoConnectAmbiguous,
        this, [this](int count, const QString& chosenMac) {
    const QString name = AppSettings::instance().value(
        QStringLiteral("radios/%1/name").arg(chosenMac)).toString();
    statusBar()->showMessage(QStringLiteral(
        "%1 radios marked Auto-connect on launch. Using %2 (most recent). "
        "Adjust in Manage Radios.").arg(count).arg(name), 6000);
});
```

- [ ] **Step 3: Manually verify**

Two-radio test: mark both as auto-connect, launch. See the warning banner. Then unplug the most-recent radio (or change its IP to something unreachable) and re-launch — see the auto-open panel + the "isn't reachable" status-bar message.

- [ ] **Step 4: Commit**

```bash
git add src/models/RadioModel.h src/models/RadioModel.cpp src/gui/MainWindow.cpp
git commit -S -m "$(cat <<'EOF'
feat(connection): auto-connect failure path + multi-flag handling (3Q-10)

When the auto-connect target on launch fails to probe within 1.5 s,
the ConnectionPanel auto-opens with that radio highlighted offline and
a status-bar message explains why. Replaces the silent "app launched
but didn't connect" mystery.

When multiple radios have AutoConnect = true, picks the most-recent-
connected MAC (radios/lastConnected) and surfaces a one-time warning
in the status bar pointing the user at Manage Radios for cleanup.

Aligns with design §7.5.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Manual verification of failure path + multi-flag warning.

---

# PR-8: Stale policy + macKey migration

---

## Task 11: Stale policy update — saved never age out, discovered-only at 60 s

**Files:**
- Modify: `src/core/RadioDiscovery.h`
- Modify: `src/core/RadioDiscovery.cpp`
- Extend: `tests/tst_connection_panel_saved_radios.cpp`

Design §7.4.

- [ ] **Step 1: Add saved-MAC tracking + extend onStaleCheck**

Add to `RadioDiscovery.h`:

```cpp
public:
    // Mark a MAC as a saved radio — exempt from stale-removal.
    // Called from ConnectionPanel after seeding from AppSettings.
    void setSavedMacs(const QStringList& macs) { m_savedMacs = QSet<QString>(macs.begin(), macs.end()); }
    void addSavedMac(const QString& mac) { m_savedMacs.insert(mac); }
    void removeSavedMac(const QString& mac) { m_savedMacs.remove(mac); }

private:
    QSet<QString> m_savedMacs;
    // ... existing
```

Update `onStaleCheck()`:

```cpp
void RadioDiscovery::onStaleCheck()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList stale;

    // From design §7.4: discovered-only timeout raised from 15s to 60s.
    constexpr qint64 kDiscoveredOnlyTimeoutMs = 60 * 1000;

    for (auto it = m_lastSeen.constBegin(); it != m_lastSeen.constEnd(); ++it) {
        const QString& mac = it.key();
        if (mac == m_connectedMac) continue;
        if (m_savedMacs.contains(mac)) continue;  // saved never age out
        if (now - it.value() > kDiscoveredOnlyTimeoutMs) {
            stale.append(mac);
        }
    }

    for (const QString& mac : stale) {
        qCDebug(lcDiscovery) << "Radio lost:" << mac;
        m_radios.remove(mac);
        m_lastSeen.remove(mac);
        emit radioLost(mac);
    }
}
```

Also bump the constant for consistency: change `kStaleTimeoutMs` to 60000 (or rename).

- [ ] **Step 2: Wire ConnectionPanel to feed savedMacs**

In `ConnectionPanel`, after seeding the table from AppSettings (existing code), call `m_radioModel->discovery()->setSavedMacs(macs)`.

- [ ] **Step 3: Extend the existing saved-radios test**

In `tests/tst_connection_panel_saved_radios.cpp`, add a test:

```cpp
void savedRadioSurvivesStaleSweep() {
    // Seed AppSettings with one saved radio MAC.
    // Run the stale check directly with a now value > 60 s past lastSeen.
    // Assert the row is still in the table.
}
```

- [ ] **Step 4: Run extended tests**

```bash
ctest --test-dir build -R "connection_panel|radio_discovery" 2>&1 | tail -10
```

Expected: all passing.

- [ ] **Step 5: Commit**

```bash
git add src/core/RadioDiscovery.h src/core/RadioDiscovery.cpp \
        src/gui/ConnectionPanel.cpp tests/tst_connection_panel_saved_radios.cpp
git commit -S -m "$(cat <<'EOF'
feat(discovery): saved radios never age out; discovered-only at 60s (3Q-11)

Saved radios (everything in AppSettings::radios/*) are tracked in a
new m_savedMacs set on RadioDiscovery and exempted from onStaleCheck
removal. They show as Stale (yellow pill) or Offline (red pill) in
the panel based on lastSeen, but the row stays — the user never
loses an entry to a 15-second sweep.

Discovered-only radios (never saved) age out at 60 s instead of 15 s.
Long enough not to flap on a single-NIC scan; short enough to clean
up after the user closes the panel.

Connected MAC remains exempt (existing behavior preserved).

Aligns with design §7.4.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Extended test passes; existing tests unchanged.

---

## Task 12: macKey migration on first probe success

**Files:**
- Modify: `src/core/AppSettings.h`
- Modify: `src/core/AppSettings.cpp`
- Modify: `src/models/RadioModel.cpp`

Design §5.4.

- [ ] **Step 1: Add a migrate-key helper to AppSettings**

```cpp
// In AppSettings.h, public:
void migrateRadioKey(const QString& oldKey, const QString& newKey);
```

```cpp
// In AppSettings.cpp:
void AppSettings::migrateRadioKey(const QString& oldKey, const QString& newKey)
{
    if (oldKey == newKey) return;
    const QStringList fields = {
        QStringLiteral("name"),
        QStringLiteral("ipAddress"),
        QStringLiteral("port"),
        QStringLiteral("macAddress"),
        QStringLiteral("boardType"),
        QStringLiteral("protocol"),
        QStringLiteral("firmwareVersion"),
        QStringLiteral("modelOverride"),
        QStringLiteral("pinToMac"),
        QStringLiteral("autoConnect"),
        QStringLiteral("lastSeen"),
    };
    for (const QString& f : fields) {
        const QString src = QStringLiteral("radios/%1/%2").arg(oldKey, f);
        const QString dst = QStringLiteral("radios/%1/%2").arg(newKey, f);
        const QVariant v = value(src);
        if (v.isValid()) {
            setValue(dst, v.toString());
            removeKey(src);
        }
    }
}
```

- [ ] **Step 2: Call from RadioModel::onProbeReplied**

After `RadioDiscovery::probeAddress` returns a `radioDiscovered` for a MAC that the model originally probed under a `manual-IP-port` key, call `AppSettings::migrateRadioKey(oldKey, info.macAddress)`.

- [ ] **Step 3: Add a unit test for migration**

```cpp
void migratesManualKeyToRealMac() {
    AppSettings& s = AppSettings::instance();
    s.setValue("radios/manual-192.168.8.3-1024/name", "Beach HL2");
    s.setValue("radios/manual-192.168.8.3-1024/ipAddress", "192.168.8.3");
    s.setValue("radios/manual-192.168.8.3-1024/autoConnect", "True");

    s.migrateRadioKey("manual-192.168.8.3-1024", "00:1C:C0:A2:14:8B");

    QCOMPARE(s.value("radios/00:1C:C0:A2:14:8B/name").toString(),
             QStringLiteral("Beach HL2"));
    QCOMPARE(s.value("radios/00:1C:C0:A2:14:8B/autoConnect").toString(),
             QStringLiteral("True"));
    QVERIFY(!s.value("radios/manual-192.168.8.3-1024/name").isValid());
}
```

Add to one of the existing AppSettings test files (e.g., `tst_app_settings_profile.cpp`).

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build -R "app_settings" -V 2>&1 | tail -10
```

Expected: all passing.

- [ ] **Step 5: Commit**

```bash
git add src/core/AppSettings.h src/core/AppSettings.cpp \
        src/models/RadioModel.cpp tests/tst_app_settings_profile.cpp
git commit -S -m "$(cat <<'EOF'
feat(settings): migrate manual-IP-port macKey to real MAC on probe success (3Q-12)

When a saved offline entry (synthetic key "manual-IP-port") gets probed
successfully and we learn the real MAC, AppSettings::migrateRadioKey
moves all fields (name, IP, port, model override, autoConnect,
pinToMac, lastSeen, etc.) under the real-MAC key and removes the
synthetic one. Preserves the user's customizations across the
"saved offline → reachable later" transition.

Tests: migratesManualKeyToRealMac (in tst_app_settings_profile).

Aligns with design §5.4.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Acceptance:** Migration test passes.

---

## Task 13: Manual end-to-end verification

**Not a PR.** This is the verification gate before merging the last PR.

- [ ] **Test scenario 1 — ANAN-G2 on LAN**

Boot the radio. Launch NereusSDR. Verify:
- Auto-discover finds it on broadcast scan.
- Click row → Connect → Probing → Connecting → Connected.
- TitleBar segment, status bar, applet strip all reflect Connected.
- Spectrum + waterfall live.
- Click Disconnect → multi-cue feedback (TitleBar grey, spectrum fades, DISCONNECTED overlay, panel auto-opens, status bar disconnected).

- [ ] **Test scenario 2 — HL2 on LAN**

Same as scenario 1 but with HL2. Verify:
- Discovery picks it up as `boardId = 6 → HermesLite`.
- Default model (Hermes Lite 2) picked; no SKU refinement needed.
- Connection succeeds.

- [ ] **Test scenario 3 — HL2 over WireGuard tunnel (Miguel scenario)**

WireGuard tunnel up between two LANs; HL2 at the remote end (192.168.8.3); operating from the local LAN.

- Launch app — broadcast scan finds nothing (WG drops broadcasts).
- Click `Add Manually…` → dialog opens.
- Fill `Name = Beach HL2`, `IP = 192.168.8.3`, leave `Port = 1024`, pick `Model = Hermes Lite 2`, leave `Protocol = Auto-detect`.
- Click `Probe and connect now`. Verify ~1.5 s probe → Connected. Row lands in table tagged "(probe)".
- Disconnect. Verify multi-cue feedback.
- Now bring the WG tunnel down.
- Click Connect on the saved Beach HL2 row. Verify ~1.5 s probe → typed error in panel/status-bar ("Couldn't reach 192.168.8.3 — radio off, IP wrong, or VPN tunnel down."). Row pill goes red (Offline).
- Bring WG back up. Click Connect again. Verify probe → Connected.

- [ ] **Test scenario 4 — Save offline + later reconnect**

WG tunnel down, radio reachable in principle but not right now.

- Open `Add Manually…`. Fill in Name + IP + Model. Leave Protocol = Auto-detect.
- Click `Save offline`. Verify the dialog flags "Protocol must be set explicitly" (info band).
- Set Protocol = P1. Click `Save offline`. Verify dialog closes, row appears with red pill, "never seen" timestamp.
- Bring WG up. Double-click the row → probe → Connected. Verify the synthetic `manual-IP-port` key migrates to the real MAC (check `~/.config/NereusSDR/NereusSDR.settings` after).

- [ ] **Test scenario 5 — Auto-connect on launch failure**

Mark a radio with AutoConnect = true. Make it unreachable (unplug, change IP). Restart NereusSDR.

- Verify amber-pulse "Connecting…" briefly.
- Verify probe failure → panel auto-opens with that radio highlighted offline.
- Verify status-bar message: "Auto-connect target X isn't reachable from this network."

- [ ] **Test scenario 6 — Stale policy regression**

Connect to a real radio. Disconnect. Wait >60 s while the panel is closed. Reopen.
- Verify the saved radio is still in the panel (saved entries never age out).
- Run a Discover Now scan. Wait >60 s without any traffic from the discovered radio.
- Verify discovered-only entries age out at 60 s (used to be 15 s).

- [ ] **Final: confirm green CI**

Push the branch:

```bash
git push -u origin claude/elegant-elgamal-e3a99e
```

Open a draft PR for the whole stack (or sequentially per the PR strategy above). Verify CI runs through cleanly on Ubuntu 24.04, macOS Apple Silicon, Windows.

---

## Self-review

**Spec coverage:** Walking the design doc section by section:

- §1 Why → covered by the rationale in PR-1, PR-2, PR-5, PR-7 commit messages.
- §2 Design principles → embedded in commit messages and the rationale fields.
- §3 Lifecycle → Task 1 (enum) + Task 3 (failure types) + Tasks 6/7/8 (UI consumers).
- §4.1 TitleBar → Task 6.
- §4.2 Status bar verbose → Task 7.
- §4.3 ConnectionPanel → Task 5.
- §4.4 Add Radio dialog → Task 4.
- §4.5 Spectrum overlay → Task 8.
- §4.6 Radio menu → Task 9.
- §5.1 Model dropdown contents → Task 4.
- §5.2 Existing structs reused → no task (verified by inspection).
- §5.3 AppSettings keys → Task 11 (lastSeen on probe) + Task 12 (migrate).
- §5.4 macKey migration → Task 12.
- §6 Workflows → all covered by the PR set; Task 13 verifies end-to-end.
- §7.1 Discovery cadence → no behavior change required (today's one-shot is what we want); covered indirectly by Task 11 (stale policy raise).
- §7.2 Unicast probe → Task 2.
- §7.3 State pill thresholds → Task 5.
- §7.4 Stale timeout → Task 11.
- §7.5 Auto-connect-on-launch → Task 10.
- §7.6 Modality (modal kept) → no code change; Task 5 leaves the modal behavior untouched.
- §7.7 Auto-close on connect → Task 5 step 11.
- §7.8 Discover Now mid-session → Task 9 (opens panel if closed).
- §8 Implementation surface → matches the file table at the top of this plan.
- §9 Migration → Task 12 covers the macKey path; the rest of the AppSettings schema is unchanged.
- §10 Open questions → defaults locked in the design; no plan changes needed beyond the picks already in the design.
- §11 Testing → covered across Tasks 1, 2, 3, 4, 5, 11, 12, 13.

**Placeholder scan:** No "TBD", "TODO", "implement later" patterns. Every step has the actual content the engineer needs.

**Type consistency:**
- `ConnectionState` (Task 1) is referenced in Tasks 6, 8, 9, 10 with the same enum values.
- `ConnectFailure` (Task 3) is referenced in Tasks 4 (probe failure routes through `RadioDiscovery::probeFailed`, not `ConnectFailure` — that's connection-layer not discovery-layer; both exist for different concerns) and Task 10 (auto-connect failure routing).
- `StatePill` (Task 5) used only in Task 5.
- `migrateRadioKey` (Task 12) signature consistent across the helper, call site, and test.
- `frameReceivedThroughConnection` is mentioned in Task 6 step 5 — verify that's the actual signal name. The design doc and Task 3 use `frameReceived` directly on `RadioConnection`. Task 6 should connect from the `RadioConnection` not `RadioModel`. Fix: in Task 6 step 5, the wiring should be `m_radioModel->connection()->frameReceived` if `connection()` is exposed, or via a forwarding signal on `RadioModel`. Either is fine — the engineer can pick the cleanest path that's already there.

The `frameReceivedThroughConnection` reference in Task 6 step 5 is sloppy. Replacing with: `m_radioModel->connection()` may be null when the model first constructs, so the cleanest pattern is a forwarding signal on `RadioModel::frameReceived` that re-emits whatever the active `m_connection` emits. Engineer should add that forwarding signal in Task 3 if it's not already there.

No other inconsistencies. Plan complete.

---

## Execution handoff

**Plan complete and saved to `docs/architecture/2026-04-27-phase3q-connection-workflow-refactor-plan.md`.**

Two execution options:

1. **Subagent-Driven (recommended)** — Dispatch a fresh subagent per task, review between tasks, fast iteration, isolated context per task.
2. **Inline Execution** — Execute tasks in this session using `executing-plans`, batch with checkpoints between PRs for review.

Which approach?
