# no-port-check: NereusSDR-original.
"""A live relay on loopback, legs that join it, and grants minted with its
secret, for the relay's tests (rendezvous document section 12)."""

from __future__ import annotations

import asyncio
import os
from contextlib import asynccontextmanager
from typing import Any, AsyncIterator, Dict, Optional, Tuple

from nereus_rendezvous import relaygrant
from nereus_rendezvous.clock import ManualClock
from nereus_relay import transport as relay_transport
from nereus_relay.config import Config
from nereus_relay.relay import TAG_END, TAG_JOIN, TAG_PEER, TAG_READY, Relay

try:  # websockets 13 and later
    from websockets.asyncio.client import connect as _ws_connect

    _HEADERS_KW = "additional_headers"
except ImportError:  # websockets 10.x: the legacy client
    from websockets.legacy.client import connect as _ws_connect  # type: ignore[no-redef]

    _HEADERS_KW = "extra_headers"

from websockets.exceptions import ConnectionClosed

WALL = 1800000000
SECRET = b"relay-test-secret-made-for-these-tests"


def make_relay_config(**overrides: Any) -> Config:
    config = Config()
    config.relay_secret = SECRET
    config.ping_interval_seconds = 0
    config.ping_timeout_seconds = 0
    for key, value in overrides.items():
        setattr(config, key, value)
    return config


@asynccontextmanager
async def live_relay(**overrides: Any) -> AsyncIterator[Tuple[Relay, ManualClock, str]]:
    clock = ManualClock(WALL)
    relay = Relay(make_relay_config(**overrides), clock)
    server = await relay_transport.start(relay, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    try:
        yield relay, clock, f"ws://127.0.0.1:{port}/v1/relay"
    finally:
        await relay_transport.stop([server], relay, grace_s=0.05, timeout_s=5)


def grant_pair(expires: int = WALL + 120, session: Optional[bytes] = None) -> Tuple[str, str, bytes]:
    """(core token, device token, session) of one grant."""
    session = session or os.urandom(relaygrant.SESSION_BYTES)
    return (
        relaygrant.mint(SECRET, relaygrant.LEG_CORE, session, expires),
        relaygrant.mint(SECRET, relaygrant.LEG_DEVICE, session, expires),
        session,
    )


async def connect(uri: str, address: str = "192.0.2.1", max_queue: Optional[int] = None) -> Any:
    kwargs: Dict[str, Any] = {
        _HEADERS_KW: {"X-Forwarded-For": address},
        "max_size": None,
        "ping_interval": None,
        "compression": None,
    }
    if max_queue is not None:
        kwargs["max_queue"] = max_queue
    return await _ws_connect(uri, **kwargs)


async def recv(ws: Any, timeout: float = 5.0) -> Any:
    return await asyncio.wait_for(ws.recv(), timeout)


async def join(uri: str, token: str, address: str = "192.0.2.1", **kwargs: Any) -> Tuple[Any, bytes]:
    """Open a leg, join it, and return it with the READY frame."""
    ws = await connect(uri, address, **kwargs)
    await ws.send(bytes([TAG_JOIN]) + token.encode("ascii"))
    ready = await recv(ws)
    assert isinstance(ready, bytes) and ready[0] == TAG_READY, ready
    return ws, ready


async def expect_end(ws: Any, code: str, close_code: int = 1000, timeout: float = 5.0) -> None:
    """The relay sends END with this code, then closes with close_code."""
    frame = await recv(ws, timeout)
    while isinstance(frame, bytes) and frame[0] == TAG_PEER:
        frame = await recv(ws, timeout)
    assert isinstance(frame, bytes) and frame[0] == TAG_END, frame
    assert frame[1:].decode("ascii") == code, frame
    await expect_close(ws, close_code, timeout)


async def expect_close(ws: Any, close_code: int, timeout: float = 5.0) -> None:
    try:
        frame = await recv(ws, timeout)
    except ConnectionClosed as exc:
        got = exc.rcvd.code if exc.rcvd is not None else None
        assert got == close_code, got
        return
    raise AssertionError(f"received {frame!r} instead of the close")


async def expect_nothing(ws: Any, timeout: float = 0.2) -> None:
    try:
        frame = await recv(ws, timeout)
    except asyncio.TimeoutError:
        return
    raise AssertionError(f"received {frame!r}")


async def settle(relay: Relay, handled: int, timeout: float = 5.0) -> None:
    """Wait until the relay has read `handled` frames in all."""
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while relay.frames_handled < handled:
        if loop.time() > deadline:
            raise AssertionError(f"the relay read {relay.frames_handled} frames, not {handled}")
        await asyncio.sleep(0.001)
    for _ in range(5):
        await asyncio.sleep(0)
