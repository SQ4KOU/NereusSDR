# no-port-check: NereusSDR-original.
"""Address grouping, the rate-limit window, configuration, the proxy
address rule, nameplate allocation and listening in both address
families."""

import asyncio
import socket

import pytest

from nereus_rendezvous import config as cfg
from nereus_rendezvous import transport
from nereus_rendezvous.clock import ManualClock
from nereus_rendezvous.limits import WindowLimiter, address_group
from nereus_rendezvous.service import Service
from helpers import recv_json
from runner import ws_connect


@pytest.mark.parametrize(
    "address,group",
    [
        ("192.0.2.1", "192.0.2.1"),
        ("::ffff:192.0.2.1", "192.0.2.1"),
        ("2001:db8:1:2:3:4:5:6", "2001:db8:1:2::/64"),
        ("2001:db8:1:2::", "2001:db8:1:2::/64"),
        ("fe80::1%en0", "fe80::/64"),
        ("not an address", "not an address"),
    ],
)
def test_address_group(address, group):
    assert address_group(address) == group


def test_window_limiter():
    limiter = WindowLimiter(2, 60000)
    assert limiter.check("a", 0) is None
    limiter.record("a", 0)
    limiter.record("a", 10)
    assert limiter.check("a", 1000) == 59000
    assert limiter.check("b", 1000) is None
    assert limiter.check("a", 60000) is None
    limiter.sweep(120000)
    assert len(limiter) == 0


def test_config_file(tmp_path):
    secret = tmp_path / "secret"
    secret.write_bytes(b"s3cret\r\n")
    path = tmp_path / "r.conf"
    path.write_text(
        "[rendezvous]\nlisten = 127.0.0.1:9000 [::1]:9001\nturn_urls = turn:a turn:b\nturn_secret_file = %s\n"
        "[limits]\nintroductions_per_address_per_minute = 5\n" % secret
    )
    config = cfg.load(str(path))
    assert config.listen == [("127.0.0.1", 9000), ("::1", 9001)]
    assert config.turn_urls == ["turn:a", "turn:b"]
    assert config.turn_secret == b"s3cret"
    assert config.introductions_per_address_per_minute == 5
    assert config.introductions_per_station_per_minute == 60


@pytest.mark.parametrize(
    "text",
    [
        "[rendezvous]\nunknown = 1\n",
        "[other]\nx = 1\n",
        "[limits]\ncandidates_per_side = many\n",
        "[limits]\ncandidates_per_side = -1\n",
        "[rendezvous]\nlisten = nowhere\n",
    ],
)
def test_config_refused(tmp_path, text):
    path = tmp_path / "r.conf"
    path.write_text(text)
    with pytest.raises(cfg.ConfigError):
        cfg.load(str(path))


def test_empty_secret_refused(tmp_path):
    secret = tmp_path / "secret"
    secret.write_text("\n")
    path = tmp_path / "r.conf"
    path.write_text("[rendezvous]\nturn_secret_file = %s\n" % secret)
    with pytest.raises(cfg.ConfigError):
        cfg.load(str(path))


def test_defaults():
    config = cfg.Config()
    assert config.listen == [("127.0.0.1", 8710), ("::1", 8710)]
    assert config.turn_ttl_seconds == 86400
    assert (config.introductions_per_address_per_minute, config.introductions_per_station_per_minute) == (30, 60)
    assert config.mailbox_opens_per_address_per_minute == 10
    assert config.candidates_per_side == 64
    assert config.introduction_lifetime_ms == 120000
    assert all("rv4.nereussdr.com" in u or "rv6.nereussdr.com" in u for u in config.turn_urls + config.stun_urls)
    assert {u.split(":")[2].split("?")[0] for u in config.turn_urls} == {"3478", "443"}


class _Headers:
    def __init__(self, values):
        self._values = values

    def get_all(self, name):
        return self._values if name.lower() == "x-forwarded-for" else []


class _Ws:
    def __init__(self, peer, forwarded):
        self.remote_address = (peer, 1234)
        self.request_headers = _Headers(forwarded)


@pytest.mark.parametrize(
    "peer,forwarded,want",
    [
        ("127.0.0.1", ["198.51.100.1"], "198.51.100.1"),
        ("::1", ["10.0.0.1, 2001:db8::9"], "2001:db8::9"),
        ("127.0.0.1", ["a, b", "198.51.100.2"], "198.51.100.2"),
        ("127.0.0.1", [], "127.0.0.1"),
        ("127.0.0.1", ["garbage"], "127.0.0.1"),
        ("203.0.113.5", ["198.51.100.1"], "203.0.113.5"),
    ],
)
def test_client_address(peer, forwarded, want):
    assert transport.client_address(_Ws(peer, forwarded), ["127.0.0.1", "::1"]) == want


def test_nameplates_lowest_free_and_capped():
    config = cfg.Config()
    service = Service(config, ManualClock())
    assert [service._allocate_nameplate() for _ in range(3)] == [1, 2, 3]
    service._next_nameplate = 999999
    assert service._allocate_nameplate() == 999999
    assert service._allocate_nameplate() is None


def _has_ipv6():
    try:
        with socket.socket(socket.AF_INET6) as s:
            s.bind(("::1", 0))
        return True
    except OSError:
        return False


def test_listens_on_both_families():
    if not _has_ipv6():
        pytest.skip("this machine has no IPv6 loopback")

    async def go():
        config = cfg.Config()
        config.ping_interval_seconds = 0
        service = Service(config, ManualClock())
        v4 = await transport.start(service, "127.0.0.1", 0)
        port = v4.sockets[0].getsockname()[1]
        v6 = await transport.start(service, "::1", port)
        try:
            for uri in (f"ws://127.0.0.1:{port}/", f"ws://[::1]:{port}/"):
                ws = await ws_connect(uri, "192.0.2.1")
                assert (await recv_json(ws))["type"] == "hello"
                await ws.close()
        finally:
            await transport.stop([v4, v6], service, grace_s=0.05, timeout_s=5)

    asyncio.run(go())


def test_stop_tells_every_connection():
    from websockets.exceptions import ConnectionClosed

    async def go():
        config = cfg.Config()
        config.ping_interval_seconds = 0
        service = Service(config, ManualClock())
        server = await transport.start(service, "127.0.0.1", 0)
        port = server.sockets[0].getsockname()[1]
        ws = await ws_connect(f"ws://127.0.0.1:{port}/", "192.0.2.1")
        await recv_json(ws)
        await transport.stop([server], service, grace_s=0.05, timeout_s=5)
        answer = await recv_json(ws)
        assert answer["code"] == "shuttingDown" and answer["retryAfterMs"] == 5000
        with pytest.raises(ConnectionClosed) as info:
            await asyncio.wait_for(ws.recv(), 5)
        assert info.value.rcvd.code == 1001

    asyncio.run(go())


def test_sample_configuration_is_the_defaults():
    from pathlib import Path

    sample = Path(__file__).resolve().parent.parent / "server" / "rendezvous.conf.sample"
    loaded = cfg.load(str(sample))
    defaults = cfg.Config()
    assert loaded == defaults


def test_missing_secret_file_is_a_configuration_error(tmp_path):
    path = tmp_path / "r.conf"
    path.write_text("[rendezvous]\nturn_secret_file = %s\n" % (tmp_path / "absent"))
    with pytest.raises(cfg.ConfigError):
        cfg.load(str(path))
