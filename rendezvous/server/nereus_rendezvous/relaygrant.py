# no-port-check: NereusSDR-original.
"""Relay grants: the tokens that admit a leg to the WebSocket relay.

Rendezvous document section 12.2 is the authority. The rendezvous service
mints one grant per introduction a station accepts with the relay allowed,
one token for each end; the relay (nereus_relay) checks a token with the
same secret and nothing else, so the two processes share no state.

    payload = version (1 byte, 1) || leg (1 byte: 1 the Core, 2 the device)
              || session (16 random bytes) || expires (4 bytes, big-endian,
              Unix seconds)
    mac     = HMAC-SHA256(secret, "NereusSDR relay grant v1\\n" || payload)
    token   = base64url(payload || mac), no padding: 54 bytes, 72 characters

Only hashlib, hmac and base64 are used, so the relay imports this module
without the cryptography package.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import struct
from typing import NamedTuple, Optional

VERSION = 1
LEG_CORE = 1
LEG_DEVICE = 2
LEGS = (LEG_CORE, LEG_DEVICE)
SESSION_BYTES = 16
PAYLOAD_BYTES = 2 + SESSION_BYTES + 4
MAC_BYTES = 32
TOKEN_BYTES = PAYLOAD_BYTES + MAC_BYTES
TOKEN_CHARS = 72
PREFIX = b"NereusSDR relay grant v1\n"
EXPIRES_MAX = 4294967295

_ALPHABET = frozenset("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")


class Grant(NamedTuple):
    leg: int
    session: bytes
    expires: int


def to_b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def from_b64url(text: str) -> Optional[bytes]:
    """Strict base64url: no padding, only the alphabet, and the text must be
    exactly what its bytes encode to (so unused low bits are zero)."""
    if not isinstance(text, str) or not text or len(text) % 4 == 1:
        return None
    if any(c not in _ALPHABET for c in text):
        return None
    try:
        data = base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))
    except (ValueError, TypeError):
        return None
    if to_b64url(data) != text:
        return None
    return data


def payload_of(leg: int, session: bytes, expires: int) -> bytes:
    if leg not in LEGS or len(session) != SESSION_BYTES or not 0 <= expires <= EXPIRES_MAX:
        raise ValueError("not a relay grant")
    return bytes([VERSION, leg]) + bytes(session) + struct.pack(">I", expires)


def mac_of(secret: bytes, payload: bytes) -> bytes:
    return hmac.new(bytes(secret), PREFIX + payload, hashlib.sha256).digest()


def mint(secret: bytes, leg: int, session: bytes, expires: int) -> str:
    payload = payload_of(leg, session, expires)
    return to_b64url(payload + mac_of(secret, payload))


def verify(secret: bytes, token: str) -> Optional[Grant]:
    """The grant a token carries, or None when it is not one this secret
    minted: bad base64url, another length, another version or leg, or a MAC
    that does not verify (compared in constant time)."""
    if not isinstance(token, str) or len(token) != TOKEN_CHARS:
        return None
    raw = from_b64url(token)
    if raw is None or len(raw) != TOKEN_BYTES:
        return None
    payload, mac = raw[:PAYLOAD_BYTES], raw[PAYLOAD_BYTES:]
    if not hmac.compare_digest(mac, mac_of(secret, payload)):
        return None
    if payload[0] != VERSION or payload[1] not in LEGS:
        return None
    (expires,) = struct.unpack(">I", payload[2 + SESSION_BYTES :])
    return Grant(payload[1], payload[2 : 2 + SESSION_BYTES], expires)
