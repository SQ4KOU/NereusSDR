# no-port-check: NereusSDR-original.
"""The relay's WebSocket glue, on the rendezvous service's (which isolates
the two websockets APIs, 10.4 on Ubuntu 24.04 and 13 and later): the same
listening sockets, kernel buffers, client address and 426 answer, with the
relay's own message size and queue, and TCP_NODELAY on every leg.
"""

from __future__ import annotations

import logging
import socket
from typing import Any, List

from nereus_rendezvous import transport as rv_transport

from .config import MAX_MESSAGE_BYTES
from .relay import Relay

# The relay reads each message as it arrives; a few waiting are enough, and
# with max_size they bound what a peer can make it hold on the way in.
MAX_QUEUE = 16
# The high-water mark of the library's write buffer. The relay's own
# drop-oldest queue sits in front of it (section 12.5), so this is kept to
# a few messages: bytes past the queue cannot be dropped any more.
WRITE_LIMIT_BYTES = 8192


class LegTransport:
    """What the relay needs from a connection."""

    def __init__(self, ws: Any) -> None:
        self.ws = ws

    async def send(self, frame: Any) -> None:
        await self.ws.send(bytes(frame))

    async def close(self, code: int) -> None:
        await self.ws.close(code, "")

    def __aiter__(self) -> Any:
        return self.ws.__aiter__()


def set_nodelay(ws: Any) -> bool:
    """TCP_NODELAY on the leg's socket, so each datagram goes out at once
    rather than waiting to be coalesced (section 12.5). asyncio sets it on
    its TCP transports already; it is set again here so the rule never
    depends on that. Returns whether it is on."""
    transport = getattr(ws, "transport", None)
    sock = transport.get_extra_info("socket") if transport is not None else None
    if sock is None:
        return False
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return bool(sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY))
    except OSError:
        return False


def serve_kwargs(relay: Relay) -> dict:
    config = relay.config
    kwargs = dict(
        max_size=MAX_MESSAGE_BYTES,
        max_queue=MAX_QUEUE,
        write_limit=WRITE_LIMIT_BYTES,
        ping_interval=config.ping_interval_seconds or None,
        ping_timeout=config.ping_timeout_seconds or None,
        # A closing leg holds its place per address until it has closed
        # (section 12.5), so a peer that never answers the close is dropped
        # after 2 s.
        close_timeout=2,
        compression=None,
        server_header=None,
        process_request=rv_transport._process_request,
    )
    if rv_transport.NEW_API:
        kwargs["open_timeout"] = config.join_timeout_ms / 1000.0
    return kwargs


async def start(relay: Relay, host: str, port: int) -> Any:
    async def handler(ws: Any) -> None:
        rv_transport.set_buffers(ws, relay.config.socket_buffer_bytes)
        set_nodelay(ws)
        address = rv_transport.client_address(ws, relay.config.trusted_proxies)
        await relay.run_connection(LegTransport(ws), address)

    sock = rv_transport.listening_socket(host, port, relay.config.socket_buffer_bytes)
    return await rv_transport._serve(
        handler,
        sock=sock,
        logger=logging.getLogger("websockets.quiet"),
        **serve_kwargs(relay),
    )


async def stop(servers: List[Any], relay: Relay, grace_s: float = 0.5, timeout_s: float = 10.0) -> None:
    """Stop listening, end every leg with shuttingDown, and wait for them to
    close (the order the rendezvous service's stop explains)."""
    await rv_transport.stop(servers, relay, grace_s=grace_s, timeout_s=timeout_s)
