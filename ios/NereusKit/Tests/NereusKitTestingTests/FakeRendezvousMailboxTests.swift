// NereusSDR for iOS: the fake Core pairs by code through a stand-in for the remote access service's mailbox
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation
import LinkSessionTestSupport
@testable import NereusKitTesting
import NereusLink
import Testing

/// The app's pairing client, through ``FakeStation/rendezvousTransportFactory``:
/// the mailbox on the code's number carries the whole code exchange to the
/// fake's Core side, with no `hello` either way, and gives back the Core's
/// key from its sealed box and no address; a number the fake does not show
/// is refused in the service's words. Codes are the fake's, made at run time.
@Suite("FakeStation, pairing through the service's mailbox")
struct FakeRendezvousMailboxTests {
    /// A clock that never moves, for the pairing clients: the connect
    /// deadline is not what these tests check, and both sides hash the code
    /// with Argon2id (64 MiB), which on a busy Mac can outlast the 30 second
    /// wall-clock deadline and end a pairing that would have finished.
    private static var noDeadline: ManualLinkClock { ManualLinkClock() }

    private func client(_ station: FakeStation) throws -> PairingClient {
        let device = try DeviceIdentity.load(store: InMemoryKeyStore())
        return try PairingClient(identity: device, name: "Test iPhone", kind: .phone, clock: Self.noDeadline,
                                 transportFactory: { _, _ in
                                     Issue.record("a pairing through the mailbox dialled the Core straight")
                                     return FailingTransport()
                                 },
                                 rendezvousTransportFactory: station.rendezvousTransportFactory)
    }

    @Test func aCodePairsThroughTheMailbox() async throws {
        let station = try FakeStation(requiresPairing: true)
        let code = station.pairingCode
        let nameplate = try #require(PairingClient.nameplate(ofNormalised: code))
        let paired = try await client(station).pair(code: code, via: .rendezvous(server: RendezvousServer.defaults,
                                                                               nameplate: nameplate))
        #expect(paired.identityKey == station.identity.publicKey)
        #expect(paired.endpoints.isEmpty)
        #expect(station.pairedDeviceKeys.count == 1)
        #expect(station.pairingConnectionCount == 1)
    }

    /// The number is one no Core shows and no other pairing in the process
    /// holds: a number derived from the fake's code could be the one
    /// ``aCodePairsThroughTheMailbox()`` holds while both run, and the
    /// client's one-pairing-per-number gate would refuse it as `alreadyPairing`.
    @Test func aNumberTheCoreDoesNotShowIsRefusedInTheServicesWords() async throws {
        let station = try FakeStation(requiresPairing: true)
        let parts = station.pairingCode.split(separator: "-")
        let number = MailboxNameplates.unshown()
        let other = "\(number)-\(parts[1])-\(parts[2])"
        await #expect(throws: PairingError.refused(
            reason: "No Core is showing that pairing code right now. Check the code and try again.",
            retryAfter: .zero)) {
            _ = try await client(station).pair(code: other, via: .rendezvous(server: RendezvousServer.defaults,
                                                                           nameplate: number))
        }
        #expect(station.pairedDeviceKeys.isEmpty)
        #expect(station.pairingConnectionCount == 0)
    }

    @Test func mailboxOpenedWaitsUntilTheCoreCanReceiveItsFirstBody() async throws {
        let station = try FakeStation(requiresPairing: true)
        let opening = OpeningGate()
        let events = MailboxEvents()
        let mailbox = FakeRendezvousMailbox(station: station, beforePairingOpen: { await opening.hold() })
        _ = try await mailbox.open { event in
            events.record(event)
        }
        let nameplate = try #require(PairingClient.nameplate(ofNormalised: station.pairingCode))

        #expect(mailbox.send(RendezvousMessage.mailboxOpen(nameplate: nameplate).encoded))
        #expect(await opening.waitUntilHeld())
        mailbox.ping()
        #expect(await events.waitUntilPong())
        #expect(!events.mailboxOpened)

        opening.release()
        #expect(await events.waitUntilMailboxOpened())
        #expect(mailbox.send(RendezvousMessage.mailbox(body: "{\"type\":\"pair.start\"}").encoded))
        mailbox.close()
    }

    /// A connection that never opens.
    private struct FailingTransport: LinkTransport {
        func open(onEvent: @escaping @Sendable (LinkTransportEvent) async -> Void) async throws -> Data {
            throw LinkTransportError.failed("not here")
        }

        @discardableResult func send(_ text: String) -> Bool { false }
        func ping() {}
        func close() {}
    }

    private final class OpeningGate: @unchecked Sendable {
        private let lock = NSLock()
        private var held = false
        private var released = false
        private var waiter: CheckedContinuation<Void, Never>?

        func hold() async {
            await withCheckedContinuation { continuation in
                let resumeNow = lock.withLock { () -> Bool in
                    held = true
                    if released {
                        return true
                    }
                    waiter = continuation
                    return false
                }
                if resumeNow {
                    continuation.resume()
                }
            }
        }

        func waitUntilHeld() async -> Bool {
            await FakeRendezvousMailboxTests.waitUntil { self.lock.withLock { self.held } }
        }

        func release() {
            let waiting = lock.withLock { () -> CheckedContinuation<Void, Never>? in
                released = true
                defer { waiter = nil }
                return waiter
            }
            waiting?.resume()
        }
    }

    private final class MailboxEvents: @unchecked Sendable {
        private let lock = NSLock()
        private var opened = false
        private var pong = false

        var mailboxOpened: Bool {
            lock.withLock { opened }
        }

        func record(_ event: LinkTransportEvent) {
            switch event {
            case .pong:
                lock.withLock { pong = true }
            case .text(let text):
                guard let message = try? RendezvousMessage.decode(text, direction: .toClient),
                      case .mailboxOpened = message else {
                    return
                }
                lock.withLock { opened = true }
            case .closed:
                break
            }
        }

        func waitUntilMailboxOpened(within timeout: Duration = .seconds(2)) async -> Bool {
            await FakeRendezvousMailboxTests.waitUntil(within: timeout) { self.lock.withLock { self.opened } }
        }

        func waitUntilPong(within timeout: Duration = .seconds(2)) async -> Bool {
            await FakeRendezvousMailboxTests.waitUntil(within: timeout) { self.lock.withLock { self.pong } }
        }
    }

    private static func waitUntil(within timeout: Duration = .seconds(2),
                                  _ condition: @escaping @Sendable () -> Bool) async -> Bool {
        let clock = ContinuousClock()
        let deadline = clock.now.advanced(by: timeout)
        while clock.now < deadline {
            if condition() {
                return true
            }
            try? await Task.sleep(for: .milliseconds(5))
        }
        return condition()
    }
}
