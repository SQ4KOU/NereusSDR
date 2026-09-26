# no-port-check: NereusSDR-original.
"""Outbound queues and message sizes: a peer that stops reading is closed
(1008) at its own byte cap or at the service's budget for every queue
together; the WebSocket options that bound what the service holds; the
sender's rule for the encoded size (section 2) at its worst case; and a
handler's failure logged by class name only."""

import asyncio
import json
import logging

import pytest
from cryptography.hazmat.primitives.asymmetric import ec

from nereus_rendezvous import identity, protocol, transport
from nereus_rendezvous.clock import ManualClock
from nereus_rendezvous.service import CLOSE_NOT_READING, Service
from helpers import introduce_message, live_service, recv_json, register
from runner import make_config, ws_connect


class FakeTransport:
    """A connection whose peer either reads everything at once or never
    reads at all (its send never returns)."""

    def __init__(self, reads: bool) -> None:
        self.reads = reads
        self.incoming: "asyncio.Queue" = asyncio.Queue()
        self.sent = []
        self.closed_with = None

    async def send(self, text: str) -> None:
        self.sent.append(text)
        if not self.reads:
            await asyncio.Event().wait()

    async def close(self, code: int) -> None:
        self.closed_with = code
        await self.incoming.put(None)

    def __aiter__(self):
        return self

    async def __anext__(self):
        item = await self.incoming.get()
        if item is None:
            raise StopAsyncIteration
        return item


async def _settle():
    for _ in range(20):
        await asyncio.sleep(0)


def _service(**limits):
    config = make_config({}, b"secret")
    for key, value in limits.items():
        setattr(config, key, value)
    return Service(config, ManualClock())


async def _start(service, fake, address):
    before = set(service.connections)
    task = asyncio.ensure_future(service.run_connection(fake, address))
    await _settle()
    (conn,) = set(service.connections) - before
    return conn, task


def test_a_peer_that_stops_reading_is_closed_at_its_byte_cap():
    async def go():
        service = _service(send_queue_bytes=1048576, mailbox_messages_per_side=64)
        station_io = FakeTransport(reads=True)
        station, _ = await _start(service, station_io, "192.0.2.1")
        key = ec.generate_private_key(ec.SECP256R1())
        spki = identity.spki_of(key.public_key())
        sid = identity.rendezvous_id(spki)
        await station_io.incoming.put(protocol.encode({"type": "register", "id": sid, "publicKey": identity.to_b64url(spki)}))
        await _settle()
        nonce = identity.from_b64url(json.loads(station_io.sent[-1])["nonce"])
        proof = identity.to_b64url(identity.sign_raw(key, identity.register_transcript(nonce)))
        await station_io.incoming.put(protocol.encode({"type": "prove", "signature": proof}))
        await station_io.incoming.put(protocol.encode({"type": "nameplate.claim"}))
        await _settle()
        number = json.loads(station_io.sent[-1])["nameplate"]

        client_io = FakeTransport(reads=False)
        client, client_task = await _start(service, client_io, "198.51.100.1")
        await client_io.incoming.put(protocol.encode({"type": "mailbox.open", "nameplate": number}))
        await _settle()
        body = "b" * 60000
        sent = 0
        while not client.closing and sent < 64:
            await station_io.incoming.put(protocol.encode({"type": "mailbox", "body": body}))
            await _settle()
            sent += 1
        # 60000-byte bodies against a 1 MiB cap: the 18th does not fit.
        assert client.closing and sent == 18, sent
        assert client_io.closed_with == CLOSE_NOT_READING
        assert client.queued_bytes == 0 and service.queued_bytes == 0
        # Nothing queued after the cap went out: the writer stopped at hello.
        assert len(client_io.sent) == 1 and json.loads(client_io.sent[0])["type"] == "hello"
        assert json.loads(station_io.sent[-1]) == {"type": "mailbox.closed", "code": "peerLeft"}
        await client_task
        assert client not in service.connections

    asyncio.run(go())


def test_the_budget_for_every_queue_together_closes_the_one_that_would_pass_it():
    async def go():
        service = _service(send_queue_bytes=1048576, send_budget_bytes=1500000)
        a_io, b_io = FakeTransport(reads=False), FakeTransport(reads=False)
        a, _ = await _start(service, a_io, "192.0.2.1")
        b, _ = await _start(service, b_io, "198.51.100.1")
        big = {"type": "mailbox", "body": "x" * 100000}
        for _ in range(8):
            a.send(big)
        assert not a.closing and service.queued_bytes > 800000
        for _ in range(8):
            b.send(big)
            if b.closing:
                break
        # a holds 8 messages; b is closed on the message that would take the
        # total past 1500000 bytes, though b is under its own cap.
        assert b.closing and not a.closing
        await _settle()
        # b's hello, stuck in its writer, is released when the writer stops.
        assert b.queued_bytes == 0 and service.queued_bytes == a.queued_bytes
        assert b_io.closed_with == CLOSE_NOT_READING

    asyncio.run(go())


def test_websocket_options_bound_what_is_held():
    config = make_config({}, b"secret")
    kwargs = transport.serve_kwargs(config)
    assert kwargs["max_size"] == protocol.MAX_MESSAGE_BYTES == 131072
    assert kwargs["max_queue"] == 1
    assert kwargs["write_limit"] == 32768


def test_the_encoded_size_rule_at_its_worst_case():
    """Section 2: a field's cap counts decoded UTF-8 bytes, and the whole
    encoded message must also fit in 131072 bytes. A legal 65536-byte body
    whose every character must be escaped cannot be sent; the largest one
    that fits is forwarded unchanged; and the longest introduction the
    service can build from a 131072-byte introduce is within
    MAX_SERVICE_MESSAGE_BYTES, which peers accept."""
    from websockets.exceptions import ConnectionClosed

    async def go():
        async with live_service({"connectionsPerAddress": 8}) as (service, uri):
            st, _, sid = await register(uri)
            await st.send(protocol.encode({"type": "nameplate.claim"}))
            number = (await recv_json(st))["nameplate"]

            # Every character a control character: 6 bytes each on the wire.
            worst = "\x01" * protocol.MAX_BODY_BYTES
            text = protocol.encode({"type": "mailbox", "body": worst})
            assert len(text.encode()) > protocol.MAX_MESSAGE_BYTES
            cl = await ws_connect(uri, "198.51.100.1")
            await recv_json(cl)
            await cl.send(protocol.encode({"type": "mailbox.open", "nameplate": number}))
            assert (await recv_json(cl))["type"] == "mailbox.opened"
            assert (await recv_json(st))["type"] == "mailbox.opened"
            await cl.send(text)
            with pytest.raises(ConnectionClosed) as info:
                await asyncio.wait_for(cl.recv(), 5)
            assert info.value.rcvd.code == 1009
            # websockets 10.4 (legacy) ends the connection only after its
            # close timeout (5 s) once it has failed it with 1009.
            assert (await recv_json(st, 15)) == {"type": "mailbox.closed", "code": "peerLeft"}

            # The largest all-escaped body that fits is forwarded unchanged.
            envelope = len(protocol.encode({"type": "mailbox", "body": ""}).encode())
            fits = "\x01" * ((protocol.MAX_MESSAGE_BYTES - envelope) // 6)
            cl2 = await ws_connect(uri, "198.51.100.2")
            await recv_json(cl2)
            await cl2.send(protocol.encode({"type": "mailbox.open", "nameplate": number}))
            await recv_json(cl2)
            await recv_json(st)
            await cl2.send(protocol.encode({"type": "mailbox", "body": fits}))
            raw = await asyncio.wait_for(st.recv(), 5)
            assert json.loads(raw)["body"] == fits
            assert len(raw.encode()) <= protocol.MAX_SERVICE_MESSAGE_BYTES

            # The longest introduce: an offer padded until the encoded
            # message is exactly 131072 bytes.
            cl3 = await ws_connect(uri, "198.51.100.3")
            hello = await recv_json(cl3)
            msg = introduce_message(sid, hello["nonce"], offer="\x01")
            short = len(protocol.encode(msg).encode())
            msg["offer"] = "\x01" * ((protocol.MAX_MESSAGE_BYTES - short) // 6 + 1)
            pad = protocol.MAX_MESSAGE_BYTES - len(protocol.encode(msg).encode())
            msg["offer"] += "v" * pad
            text = protocol.encode(msg)
            assert len(text.encode()) == protocol.MAX_MESSAGE_BYTES
            await cl3.send(text)
            raw = await asyncio.wait_for(st.recv(), 5)
            assert json.loads(raw)["type"] == "introduction"
            assert len(raw.encode()) <= protocol.MAX_SERVICE_MESSAGE_BYTES <= protocol.PEER_RECEIVE_BYTES

    asyncio.run(go())


def test_a_failing_handler_is_logged_by_class_name_only(caplog, monkeypatch):
    secret_words = "SECRET-CONTENT-OF-THE-EXCEPTION"

    def boom(self, conn, msg):
        raise ValueError(secret_words)

    monkeypatch.setattr(Service, "_client_mailbox_open", boom)

    async def go():
        async with live_service() as (service, uri):
            ws = await ws_connect(uri, "192.0.2.1")
            await recv_json(ws)
            await ws.send(protocol.encode({"type": "mailbox.open", "nameplate": 3}))
            from websockets.exceptions import ConnectionClosed

            with pytest.raises(ConnectionClosed):
                await asyncio.wait_for(ws.recv(), 5)

    with caplog.at_level(logging.DEBUG, logger="nereus_rendezvous"):
        asyncio.run(go())
    text = "\n".join(r.getMessage() for r in caplog.records)
    assert "ValueError" in text
    assert secret_words not in text
