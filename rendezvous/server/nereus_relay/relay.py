# no-port-check: NereusSDR-original.
"""The WebSocket relay: datagrams between the two legs of one relay grant.

Rendezvous document section 12 is the authority. Each end (the Core and
the device) opens one WebSocket leg and joins it with the token of its
relay grant; the relay pairs the two legs of one grant's session and
forwards every data frame from one leg to the other. A data frame is one
ICE, DTLS, SCTP or SRTP datagram behind a one-byte stream tag. The relay
reads the tag and the length and nothing else: every reliable or secure
layer runs end to end, and what the relay carries is ciphertext.

Nothing here writes to disk. Everything lives in memory; a restart forgets
every session, and a leg whose grant has not yet expired simply joins
again.

Each leg has its own bounded send queue that drops its oldest frame when a
new one would not fit, and its own writer task, so a leg that reads slowly
loses its oldest datagrams (which SCTP sees as loss) instead of growing
memory or holding up the other leg.
"""

from __future__ import annotations

import asyncio
import collections
import datetime
import logging
from typing import Any, Deque, Dict, Optional, Set

from nereus_rendezvous import relaygrant
from nereus_rendezvous.clock import RealClock, TimerHandle
from nereus_rendezvous.limits import address_group

from .config import MAX_MESSAGE_BYTES, Config, check

log = logging.getLogger("nereus_relay")

# Section 12.3: the frame's first byte.
TAG_CONTROL = 0x01
TAG_MEDIA = 0x02
DATA_TAG_MAX = 0x7F
TAG_JOIN = 0x80
TAG_READY = 0x81
TAG_PEER = 0x82
TAG_END = 0x83
FRAME_VERSION = 1

CLOSE_NORMAL = 1000
CLOSE_GOING_AWAY = 1001
CLOSE_NOT_READING = 1008

# Section 12.4: why the relay ends a leg (the END frame's text).
END_CODES = (
    "protocolError",
    "timeout",
    "badToken",
    "expired",
    "ended",
    "full",
    "tooManyConnections",
    "replaced",
    "peerGone",
    "idle",
    "shuttingDown",
)

LEG_NAMES = {relaygrant.LEG_CORE: "core", relaygrant.LEG_DEVICE: "device"}


def short_session(session: bytes) -> str:
    """Sessions in logs: six characters of the id, never the whole."""
    return relaygrant.to_b64url(session)[:6]


def other_side(side: int) -> int:
    return relaygrant.LEG_DEVICE if side == relaygrant.LEG_CORE else relaygrant.LEG_CORE


class DropOldestQueue:
    """A bounded queue of frames. A push that would pass either cap drops
    the oldest frames until the new one fits, so what waits is always the
    newest, and never more than max_frames frames or max_bytes bytes."""

    def __init__(self, max_frames: int, max_bytes: int) -> None:
        self.max_frames = max_frames
        self.max_bytes = max_bytes
        self._frames: Deque[Any] = collections.deque()
        self.bytes = 0
        self.dropped = 0
        self.peak_bytes = 0

    def push(self, frame: Any) -> int:
        """Queue a frame; returns how many older frames were dropped."""
        size = len(frame)
        dropped = 0
        while self._frames and (len(self._frames) >= self.max_frames or self.bytes + size > self.max_bytes):
            old = self._frames.popleft()
            self.bytes -= len(old)
            dropped += 1
        self._frames.append(frame)
        self.bytes += size
        self.dropped += dropped
        if self.bytes > self.peak_bytes:
            self.peak_bytes = self.bytes
        return dropped

    def pop(self) -> Optional[Any]:
        if not self._frames:
            return None
        frame = self._frames.popleft()
        self.bytes -= len(frame)
        return frame

    def clear(self) -> None:
        self._frames.clear()
        self.bytes = 0

    def __len__(self) -> int:
        return len(self._frames)


class TokenBucket:
    """At most `rate` bytes a second on average, with bursts of up to one
    second's worth (section 12.5). A frame that does not fit is dropped,
    not delayed."""

    def __init__(self, rate: int, now_ms: int) -> None:
        self.rate = rate
        self.capacity = float(rate)
        self.tokens = float(rate)
        self.last_ms = now_ms

    def allow(self, size: int, now_ms: int) -> bool:
        elapsed = max(0, now_ms - self.last_ms)
        self.last_ms = now_ms
        self.tokens = min(self.capacity, self.tokens + elapsed * self.rate / 1000.0)
        if self.tokens < size:
            return False
        self.tokens -= size
        return True


class DataUse:
    """The relay's data-use count (section 12.6): bytes forwarded, by UTC
    day, with a journal line when a day ends, as the server's data-use
    report does for the whole host (rendezvous/deploy/data-use.py)."""

    def __init__(self, wall_seconds: int) -> None:
        self.started = wall_seconds
        self.day = self._day_of(wall_seconds)
        self.day_bytes = 0
        self.day_sessions = 0
        self.total_bytes = 0

    @staticmethod
    def _day_of(ts: int) -> str:
        return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).strftime("%Y-%m-%d")

    def add(self, size: int) -> None:
        self.day_bytes += size
        self.total_bytes += size

    def session(self) -> None:
        self.day_sessions += 1

    def line(self) -> str:
        return "relay data use: %.2f GB forwarded on %s (UTC) in %d sessions; %.2f GB since the relay started" % (
            self.day_bytes / 1e9,
            self.day,
            self.day_sessions,
            self.total_bytes / 1e9,
        )

    def roll(self, wall_seconds: int) -> Optional[str]:
        """The finished day's line once the UTC day has changed, else None."""
        today = self._day_of(wall_seconds)
        if today == self.day:
            return None
        line = self.line()
        self.day = today
        self.day_bytes = 0
        self.day_sessions = 0
        return line


class _Close:
    def __init__(self, code: int) -> None:
        self.code = code


class Session:
    def __init__(self, sid: bytes, expires: int, now_ms: int, rate: int) -> None:
        self.sid = sid
        self.expires = expires
        self.started_ms = now_ms
        # The rate cap each way, kept by the session rather than the
        # connection, so a leg that joins again does not start a fresh
        # bucket.
        self.buckets: Dict[int, TokenBucket] = {side: TokenBucket(rate, now_ms) for side in relaygrant.LEGS}
        self.legs: Dict[int, Optional["Leg"]] = {relaygrant.LEG_CORE: None, relaygrant.LEG_DEVICE: None}
        # A side that has not joined yet, or has left, has this long to come
        # (back); the idle timer runs only while both legs are present.
        self.away: Dict[int, Optional[TimerHandle]] = {relaygrant.LEG_CORE: None, relaygrant.LEG_DEVICE: None}
        self.idle: Optional[TimerHandle] = None
        self.forwarded_bytes = 0
        self.forwarded_frames = 0
        self.dropped_rate = 0
        self.dropped_queue = 0
        self.dropped_no_peer = 0
        self.ended = False


class Leg:
    """One WebSocket connection to the relay: pending until it joins, then
    one side of a session."""

    def __init__(self, relay: "Relay", transport: Any, address: str) -> None:
        self.relay = relay
        self.transport = transport
        self.group = address_group(address)
        self.session: Optional[Session] = None
        self.side = 0
        self.joined = False
        self.counted = False
        self.pending = False
        self.control: Deque[Any] = collections.deque()
        self.queue = DropOldestQueue(relay.config.queue_frames, relay.config.queue_bytes)
        self.wake = asyncio.Event()
        self.closing = False
        self.aborted = False
        self.writer: Optional["asyncio.Future[None]"] = None
        self.timer: Optional[TimerHandle] = None
        self.stall_timer: Optional[TimerHandle] = None

    def send_control(self, frame: bytes) -> None:
        if self.closing:
            return
        self.control.append(frame)
        self.wake.set()

    def send_data(self, frame: Any) -> int:
        """Queue a datagram for this leg; returns how many older ones were
        dropped to make room."""
        dropped = self.queue.push(frame)
        self.wake.set()
        return dropped

    def end(self, code: str, close_code: int = CLOSE_NORMAL) -> None:
        """Send END with its code, then close. Whatever data waits is
        dropped: it is no use to a leg that is going."""
        if self.closing:
            return
        self.control.append(bytes([TAG_END]) + code.encode("ascii"))
        self.closing = True
        self.queue.clear()
        self.control.append(_Close(close_code))
        self.wake.set()
        self.relay.detach(self)

    def cancel_timer(self) -> None:
        if self.timer is not None:
            self.timer.cancel()
            self.timer = None

    def stopped_reading(self) -> None:
        """One send has taken longer than send_stall_ms: the peer is not
        reading at all. The connection goes (1008, no END: it would not read
        one) and its place in the session waits for a rejoin."""
        if self.aborted:
            return
        self.aborted = True
        self.closing = True
        self.queue.clear()
        self.control.clear()
        if self.writer is not None:
            self.writer.cancel()
        asyncio.ensure_future(self._close_quietly(CLOSE_NOT_READING))
        self.relay.detach(self)

    async def _close_quietly(self, code: int) -> None:
        try:
            await self.transport.close(code)
        except Exception:  # noqa: BLE001 - the peer is gone either way
            return

    async def write_loop(self) -> None:
        try:
            while True:
                if self.control:
                    item = self.control.popleft()
                elif not self.closing and len(self.queue):
                    item = self.queue.pop()
                else:
                    self.wake.clear()
                    await self.wake.wait()
                    continue
                if isinstance(item, _Close):
                    await self.transport.close(item.code)
                    return
                self.stall_timer = self.relay.clock.call_later(self.relay.config.send_stall_ms, self.stopped_reading)
                await self.transport.send(item)
                self.stall_timer.cancel()
                self.stall_timer = None
        except Exception:  # noqa: BLE001 - the peer went away; the reader cleans up
            return
        finally:
            if self.stall_timer is not None:
                self.stall_timer.cancel()
                self.stall_timer = None
            self.queue.clear()


class Relay:
    def __init__(self, config: Config, clock: Any = None) -> None:
        check(config)
        if not config.relay_secret:
            raise ValueError("the relay needs its secret")
        self.config = config
        self.clock = clock or RealClock()
        self.legs: Set[Leg] = set()
        self.pending = 0
        self.per_group: Dict[str, int] = {}
        self.sessions: Dict[bytes, Session] = {}
        # Sessions that have ended, until their grant's expiry: a token of
        # an ended session never opens a new one (section 12.4).
        self.spent: Dict[bytes, int] = {}
        self.data_use = DataUse(self.clock.wall_seconds())
        self.frames_handled = 0
        self._day_timer: Optional[TimerHandle] = None

    # ----------------------------------------------------------- lifecycle

    def start_day_timer(self) -> None:
        """Checks once an hour whether the UTC day has changed, to log the
        day's data use."""

        def tick() -> None:
            line = self.data_use.roll(self.clock.wall_seconds())
            if line is not None:
                log.info("%s", line)
            self._day_timer = self.clock.call_later(3600000, tick)

        self._day_timer = self.clock.call_later(3600000, tick)

    async def run_connection(self, transport: Any, address: str) -> None:
        leg = Leg(self, transport, address)
        writer = asyncio.ensure_future(leg.write_loop())
        leg.writer = writer
        self.accept(leg)
        try:
            async for frame in transport:
                if not leg.closing:
                    try:
                        self.on_frame(leg, frame)
                    except Exception as exc:  # noqa: BLE001
                        # A bug, not the peer's doing. The class name only.
                        log.error("a frame handler failed (%s); closing its connection", type(exc).__name__)
                        self.frames_handled += 1
                        break
                self.frames_handled += 1
        except Exception as exc:  # noqa: BLE001 - any transport end is an end
            log.debug("a relay connection ended: %s", type(exc).__name__)
        finally:
            self.detach(leg)
            if leg.closing and not writer.done():
                await asyncio.wait({writer}, timeout=5)
            if not writer.done():
                writer.cancel()
                await asyncio.wait({writer})
            leg.queue.clear()

    def accept(self, leg: Leg) -> None:
        if self.pending >= self.config.max_pending:
            leg.end("full")
            return
        if self.per_group.get(leg.group, 0) >= self.config.connections_per_address:
            leg.end("tooManyConnections")
            return
        leg.counted = True
        leg.pending = True
        self.pending += 1
        self.per_group[leg.group] = self.per_group.get(leg.group, 0) + 1
        self.legs.add(leg)
        leg.timer = self.clock.call_later(self.config.join_timeout_ms, lambda: leg.end("timeout"))

    def shutdown(self) -> None:
        for leg in list(self.legs):
            leg.end("shuttingDown", close_code=CLOSE_GOING_AWAY)

    def detach(self, leg: Leg) -> None:
        """Forget a connection. A joined leg leaves its session's place
        open for a rejoin (section 12.4). Safe to call more than once."""
        leg.cancel_timer()
        if leg in self.legs:
            self.legs.discard(leg)
        if leg.pending:
            leg.pending = False
            self.pending -= 1
        if leg.counted:
            leg.counted = False
            left = self.per_group.get(leg.group, 1) - 1
            if left > 0:
                self.per_group[leg.group] = left
            else:
                self.per_group.pop(leg.group, None)
        session = leg.session
        leg.session = None
        if session is None or session.ended or session.legs.get(leg.side) is not leg:
            return
        session.legs[leg.side] = None
        log.info("relay session %s: %s leg left", short_session(session.sid), LEG_NAMES[leg.side])
        self._stop_idle(session)
        self._start_away(session, leg.side)
        other = session.legs[other_side(leg.side)]
        if other is not None:
            other.send_control(bytes([TAG_PEER, 0]))

    # ----------------------------------------------------------- frames

    def on_frame(self, leg: Leg, frame: Any) -> None:
        # Section 12.3: binary frames only; the tag is the first byte.
        if not isinstance(frame, (bytes, bytearray, memoryview)) or len(frame) == 0:
            leg.end("protocolError")
            return
        tag = frame[0]
        if not leg.joined:
            if tag != TAG_JOIN:
                leg.end("protocolError")
                return
            self._join(leg, bytes(frame[1:]))
            return
        if tag == 0 or tag > DATA_TAG_MAX or len(frame) < 2:
            leg.end("protocolError")
            return
        self._forward(leg, frame)

    def _forward(self, leg: Leg, frame: Any) -> None:
        """The whole of what the relay does with a datagram: it has read the
        tag (above) and takes the length; the frame goes on as it came."""
        session = leg.session
        if session is None:
            return
        size = len(frame)
        other = session.legs[other_side(leg.side)]
        if other is None or other.closing:
            session.dropped_no_peer += 1
            return
        if not session.buckets[leg.side].allow(size, self.clock.now_ms()):
            session.dropped_rate += 1
            return
        session.dropped_queue += other.send_data(frame)
        session.forwarded_bytes += size
        session.forwarded_frames += 1
        self.data_use.add(size)
        self._touch(session)

    # ----------------------------------------------------------- joining

    def _join(self, leg: Leg, token_bytes: bytes) -> None:
        try:
            token = token_bytes.decode("ascii")
        except UnicodeDecodeError:
            token = ""
        grant = relaygrant.verify(self.config.relay_secret or b"", token)
        if grant is None:
            leg.end("badToken")
            return
        now_wall = self.clock.wall_seconds()
        session = self.sessions.get(grant.session)
        if session is None:
            self._prune_spent(now_wall)
            if grant.session in self.spent:
                leg.end("ended")
                return
            if grant.expires < now_wall:
                leg.end("expired")
                return
            if len(self.sessions) >= self.config.slots:
                leg.end("full")
                return
            session = Session(grant.session, grant.expires, self.clock.now_ms(), self.config.rate_bytes_per_second)
            self.sessions[grant.session] = session
            self.data_use.session()
            log.info("relay session %s opened", short_session(session.sid))
            for side in relaygrant.LEGS:
                self._start_away(session, side)
        side = grant.leg
        previous = session.legs[side]
        if previous is not None and previous is not leg:
            # A leg that joins again while its older connection still looks
            # alive (a reset the relay has not seen yet) takes its place.
            previous.session = None
            previous.end("replaced")
            log.info("relay session %s: %s leg replaced", short_session(session.sid), LEG_NAMES[side])
        leg.cancel_timer()
        if leg.pending:
            leg.pending = False
            self.pending -= 1
        leg.joined = True
        leg.session = session
        leg.side = side
        session.legs[side] = leg
        timer = session.away[side]
        if timer is not None:
            timer.cancel()
            session.away[side] = None
        other = session.legs[other_side(side)]
        log.info("relay session %s: %s leg joined", short_session(session.sid), LEG_NAMES[side])
        leg.send_control(bytes([TAG_READY, FRAME_VERSION, 1 if other is not None else 0]))
        if other is not None:
            other.send_control(bytes([TAG_PEER, 1]))
            self._touch(session)

    # ----------------------------------------------------------- timers

    def _touch(self, session: Session) -> None:
        """Both legs present and something happened: the idle timer starts
        again."""
        self._stop_idle(session)
        if all(session.legs[s] is not None for s in relaygrant.LEGS):
            session.idle = self.clock.call_later(self.config.idle_timeout_ms, lambda: self.end_session(session, "idle"))

    def _stop_idle(self, session: Session) -> None:
        if session.idle is not None:
            session.idle.cancel()
            session.idle = None

    def _start_away(self, session: Session, side: int) -> None:
        if session.legs[side] is not None or session.away[side] is not None:
            return
        session.away[side] = self.clock.call_later(self.config.rejoin_ms, lambda: self.end_session(session, "peerGone"))

    def _prune_spent(self, now_wall: int) -> None:
        for sid in [s for s, exp in self.spent.items() if exp < now_wall]:
            del self.spent[sid]

    def end_session(self, session: Session, code: str) -> None:
        if session.ended:
            return
        session.ended = True
        self._stop_idle(session)
        for side in relaygrant.LEGS:
            timer = session.away[side]
            if timer is not None:
                timer.cancel()
                session.away[side] = None
        if self.sessions.get(session.sid) is session:
            del self.sessions[session.sid]
        self.spent[session.sid] = session.expires
        seconds = (self.clock.now_ms() - session.started_ms) // 1000
        log.info(
            "relay session %s ended (%s) after %d s: %d frames, %.2f MB forwarded; dropped %d over the rate, "
            "%d from full queues, %d with no peer",
            short_session(session.sid),
            code,
            seconds,
            session.forwarded_frames,
            session.forwarded_bytes / 1e6,
            session.dropped_rate,
            session.dropped_queue,
            session.dropped_no_peer,
        )
        for side in relaygrant.LEGS:
            leg = session.legs[side]
            session.legs[side] = None
            if leg is not None:
                leg.session = None
                leg.end(code)

