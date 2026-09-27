# no-port-check: NereusSDR-original.
"""The relay's queues and what it reads of a frame (rendezvous document
section 12.5): a slow reader never grows the relay's memory and loses its
oldest datagrams first, and the relay reads a datagram's tag and length
and nothing more."""

import asyncio
import logging
import struct

from nereus_rendezvous.clock import ManualClock
from nereus_relay import transport as relay_transport
from nereus_relay.relay import TAG_JOIN, TAG_MEDIA, TAG_PEER, DropOldestQueue, Leg, Relay
from relay_helpers import WALL, grant_pair, join, live_relay, make_relay_config, recv, settle


def test_drop_oldest_queue_keeps_the_newest_within_both_caps():
    queue = DropOldestQueue(max_frames=8, max_bytes=10000)
    for n in range(100):
        queue.push(struct.pack(">I", n) + bytes(996))
        assert len(queue) <= 8 and queue.bytes <= 10000
    kept = []
    while len(queue):
        kept.append(struct.unpack(">I", queue.pop()[:4])[0])
    assert kept == list(range(92, 100))
    assert queue.dropped == 92 and queue.peak_bytes == 8000
    # The byte cap binds before the frame cap: 1500-byte frames, 6 fit.
    queue = DropOldestQueue(max_frames=64, max_bytes=9000)
    for n in range(20):
        queue.push(struct.pack(">I", n) + bytes(1496))
    kept = []
    while len(queue):
        kept.append(struct.unpack(">I", queue.pop()[:4])[0])
    assert kept == list(range(14, 20)) and queue.peak_bytes <= 9000


def test_a_slow_reader_never_grows_memory_and_loses_the_oldest_first():
    """The Core's leg stops reading while the device sends 3000 datagrams
    as fast as it can. The relay's queue for that leg never holds more than
    its cap, the library's write buffer stays at its limit, and when the
    reader comes back what it gets is in order, ends with the newest, and
    is exactly what was sent less what the queue dropped."""
    total = 3000

    async def go():
        async with live_relay(rate_bytes_per_second=10 ** 9, queue_frames=64, queue_bytes=24576) as (relay, clock, uri):
            core_token, device_token, _ = grant_pair()
            # max_queue 1: the reader's own library holds one message and
            # then stops reading its socket, as a stalled app would.
            core, _ = await join(uri, core_token, max_queue=1)
            device, _ = await join(uri, device_token)
            core_leg = next(leg for leg in relay.legs if leg.side == 1)
            ws_transport = core_leg.transport.ws.transport
            peak_buffer = 0
            for n in range(total):
                await device.send(bytes([TAG_MEDIA]) + struct.pack(">I", n) + bytes(995))
                if n % 50 == 0:
                    await asyncio.sleep(0)
                    peak_buffer = max(peak_buffer, ws_transport.get_write_buffer_size())
            await settle(relay, 2 + total)
            peak_buffer = max(peak_buffer, ws_transport.get_write_buffer_size())
            queue = core_leg.queue
            assert queue.peak_bytes <= 24576 and len(queue) <= 64
            assert queue.dropped > 0
            assert peak_buffer <= relay_transport.WRITE_LIMIT_BYTES + 2 * 1024
            session = next(iter(relay.sessions.values()))
            dropped = session.dropped_queue
            assert dropped == queue.dropped
            got = []
            while True:
                try:
                    frame = await asyncio.wait_for(core.recv(), 1.0)
                except asyncio.TimeoutError:
                    break
                if frame[0] == TAG_PEER:
                    continue
                got.append(struct.unpack(">I", frame[1:5])[0])
            assert got == sorted(got) and len(set(got)) == len(got)
            assert got[-1] == total - 1
            assert len(got) + dropped == total
            # The frames still queued when the reader came back were the
            # newest: the last run of what arrived is contiguous up to the
            # end and at least as long as what the queue held.
            run = 1
            while run < len(got) and got[-run - 1] == got[-run] - 1:
                run += 1
            assert run >= 16

    asyncio.run(go())


class _Opaque(bytes):
    """A datagram that records every way it is read beyond its first byte
    and its length."""

    touched: list = []

    def __getitem__(self, index):
        if index != 0:
            _Opaque.touched.append(("getitem", index))
        return bytes.__getitem__(self, index)

    def __iter__(self):
        _Opaque.touched.append(("iter",))
        return bytes.__iter__(self)

    def __contains__(self, item):
        _Opaque.touched.append(("contains",))
        return bytes.__contains__(self, item)

    def __eq__(self, other):
        _Opaque.touched.append(("eq",))
        return bytes.__eq__(self, other)

    __hash__ = bytes.__hash__

    def find(self, *args):
        _Opaque.touched.append(("find",))
        return bytes.find(self, *args)

    def decode(self, *args, **kwargs):
        _Opaque.touched.append(("decode",))
        return bytes.decode(self, *args, **kwargs)

    def startswith(self, *args):
        _Opaque.touched.append(("startswith",))
        return bytes.startswith(self, *args)


class _NullTransport:
    async def send(self, frame):
        await asyncio.sleep(3600)

    async def close(self, code):
        return

    def __aiter__(self):
        return self

    async def __anext__(self):
        raise StopAsyncIteration


MARKER = b"NEREUS-RELAY-PLAINTEXT-MARKER"
# A DTLS 1.2 application-data record header, then the marker, as a peer
# would carry it inside ciphertext; STUN and RTP first bytes too.
DATAGRAMS = [
    b"\x17\xfe\xfd\x00\x01\x00\x00\x00\x00\x00\x07\x00\x40" + MARKER + bytes(20),
    b"\x16\xfe\xfd\x00\x00" + MARKER,
    b"\x00\x01\x00\x08\x21\x12\xa4\x42" + MARKER,
    b"\x80\x6f\x00\x01" + MARKER,
]


def test_the_relay_reads_only_the_tag_and_the_length(caplog):
    """Requirement 6: frames that look like DTLS, STUN and RTP, each holding
    a known marker, go through the relay's frame handling; it reads the
    first byte and the length and nothing else, queues the very object it
    was given, and no log line holds the marker."""
    caplog.set_level(logging.DEBUG)

    async def go():
        clock = ManualClock(WALL)
        relay = Relay(make_relay_config(queue_frames=1000, queue_bytes=10 ** 6, rate_bytes_per_second=10 ** 9), clock)
        core = Leg(relay, _NullTransport(), "198.51.100.1")
        device = Leg(relay, _NullTransport(), "198.51.100.2")
        core_token, device_token, _ = grant_pair()
        for leg, token in ((core, core_token), (device, device_token)):
            relay.accept(leg)
            relay.on_frame(leg, bytes([TAG_JOIN]) + token.encode())
        assert core.joined and device.joined and core.session is device.session
        core.control.clear()
        device.control.clear()
        sent = []
        for tag in (1, 2, 0x7F):
            for datagram in DATAGRAMS:
                frame = _Opaque(bytes([tag]) + datagram)
                sent.append(frame)
                _Opaque.touched = []
                relay.on_frame(device, frame)
                assert _Opaque.touched == [], _Opaque.touched
        queued = []
        while len(core.queue):
            queued.append(core.queue.pop())
        assert len(queued) == len(sent)
        assert all(a is b for a, b in zip(queued, sent))
        relay.end_session(core.session, "idle")

    asyncio.run(go())
    for record in caplog.records:
        assert MARKER.decode() not in record.getMessage()


def test_marked_datagrams_cross_the_real_relay_byte_for_byte(caplog):
    caplog.set_level(logging.DEBUG)

    async def go():
        async with live_relay() as (relay, clock, uri):
            core_token, device_token, _ = grant_pair()
            core, _ = await join(uri, core_token)
            device, _ = await join(uri, device_token)
            assert await recv(core) == bytes([TAG_PEER, 1])
            for tag in (1, 2):
                for datagram in DATAGRAMS:
                    frame = bytes([tag]) + datagram
                    await device.send(frame)
                    assert await recv(core) == frame
                    await core.send(frame)
                    assert await recv(device) == frame

    asyncio.run(go())
    for record in caplog.records:
        if record.name.startswith("nereus"):
            assert MARKER.decode() not in record.getMessage()
