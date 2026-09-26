# no-port-check: NereusSDR-original.
"""Configuration: one INI file read at start, and the TURN secret from a
file of its own.

The secret file is separate so systemd can hand it over with
LoadCredential= and it never sits in the configuration or the repository.
The service never writes either file.
"""

from __future__ import annotations

import configparser
import dataclasses
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


class ConfigError(Exception):
    pass


@dataclass
class Config:
    # [rendezvous]
    listen: List[Tuple[str, int]] = field(default_factory=lambda: [("127.0.0.1", 8710), ("::1", 8710)])
    trusted_proxies: List[str] = field(default_factory=lambda: ["127.0.0.1", "::1"])
    stun_urls: List[str] = field(
        default_factory=lambda: [
            "stun:rv6.nereussdr.com:3478",
            "stun:rv4.nereussdr.com:3478",
        ]
    )
    turn_urls: List[str] = field(
        default_factory=lambda: [
            "turn:rv6.nereussdr.com:3478?transport=udp",
            "turn:rv6.nereussdr.com:443?transport=udp",
            "turn:rv4.nereussdr.com:3478?transport=udp",
            "turn:rv4.nereussdr.com:443?transport=udp",
        ]
    )
    turn_secret_file: str = ""
    turn_ttl_seconds: int = 86400
    log_level: str = "info"
    # [limits]
    introductions_per_address_per_minute: int = 30
    introductions_per_station_per_minute: int = 60
    mailbox_opens_per_address_per_minute: int = 10
    candidates_per_side: int = 64
    mailbox_messages_per_side: int = 32
    connections_per_address: int = 16
    stations_per_address: int = 4
    max_connections: int = 512
    max_stations: int = 512
    handshake_timeout_ms: int = 10000
    idle_timeout_ms: int = 30000
    introduction_lifetime_ms: int = 120000
    mailbox_lifetime_ms: int = 300000
    ping_interval_seconds: int = 20
    ping_timeout_seconds: int = 20
    send_queue_messages: int = 256
    send_queue_bytes: int = 1048576
    send_budget_bytes: int = 33554432
    # Not from the file: the secret's bytes, read from turn_secret_file.
    turn_secret: Optional[bytes] = field(default=None, repr=False)


_SECTIONS = {
    "rendezvous": [
        "listen",
        "trusted_proxies",
        "stun_urls",
        "turn_urls",
        "turn_secret_file",
        "turn_ttl_seconds",
        "log_level",
    ],
    "limits": [
        "introductions_per_address_per_minute",
        "introductions_per_station_per_minute",
        "mailbox_opens_per_address_per_minute",
        "candidates_per_side",
        "mailbox_messages_per_side",
        "connections_per_address",
        "stations_per_address",
        "max_connections",
        "max_stations",
        "handshake_timeout_ms",
        "idle_timeout_ms",
        "introduction_lifetime_ms",
        "mailbox_lifetime_ms",
        "ping_interval_seconds",
        "ping_timeout_seconds",
        "send_queue_messages",
        "send_queue_bytes",
        "send_budget_bytes",
    ],
}


def parse_listen(text: str) -> List[Tuple[str, int]]:
    """`127.0.0.1:8710 [::1]:8710`: space-separated host:port, IPv6 in
    brackets."""
    out: List[Tuple[str, int]] = []
    for item in text.split():
        if item.startswith("["):
            host, sep, rest = item[1:].partition("]:")
            if not sep:
                raise ConfigError(f"listen: bad address {item!r}")
            port_text = rest
        else:
            host, sep, port_text = item.rpartition(":")
            if not sep:
                raise ConfigError(f"listen: bad address {item!r}")
        try:
            port = int(port_text)
        except ValueError as exc:
            raise ConfigError(f"listen: bad port in {item!r}") from exc
        if not 0 <= port <= 65535:
            raise ConfigError(f"listen: bad port in {item!r}")
        out.append((host, port))
    if not out:
        raise ConfigError("listen: no address")
    return out


def read_secret(path: str) -> bytes:
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        # The error names the path, never the contents.
        raise ConfigError(f"cannot read the TURN secret file {path}") from exc
    secret = data.rstrip(b"\r\n")
    if not secret:
        raise ConfigError("the TURN secret file is empty")
    return secret


def load(path: Optional[str]) -> Config:
    config = Config()
    if path:
        parser = configparser.ConfigParser(interpolation=None)
        try:
            with open(path, "r", encoding="utf-8") as handle:
                parser.read_file(handle)
        except (OSError, configparser.Error) as exc:
            raise ConfigError(f"cannot read {path}: {exc}") from exc
        for section in parser.sections():
            if section not in _SECTIONS:
                raise ConfigError(f"unknown section [{section}]")
            for key, value in parser.items(section):
                if key not in _SECTIONS[section]:
                    raise ConfigError(f"unknown key {key} in [{section}]")
                apply(config, key, value)
    if config.turn_secret_file:
        config.turn_secret = read_secret(config.turn_secret_file)
    check(config)
    return config


# Zero turns the WebSocket ping off; every other number must be at least 1,
# because 0 would make the service refuse everything, or nothing.
_MAY_BE_ZERO = ("ping_interval_seconds", "ping_timeout_seconds")


def check(config: Config) -> None:
    """URLs must fit the wire (rendezvous document section 5.2), and every
    limit, timeout and cap must leave the service usable."""
    for key in _SECTIONS["limits"] + ["turn_ttl_seconds"]:
        value = getattr(config, key)
        if not isinstance(value, int) or isinstance(value, bool):
            raise ConfigError(f"{key}: not a whole number")
        if value < 0 or (value == 0 and key not in _MAY_BE_ZERO):
            raise ConfigError(f"{key}: must be at least 1" if key not in _MAY_BE_ZERO else f"{key}: negative")
    if config.send_budget_bytes < config.send_queue_bytes:
        raise ConfigError("send_budget_bytes: smaller than send_queue_bytes")
    for name in ("stun_urls", "turn_urls"):
        urls = getattr(config, name)
        if len(urls) > 8:
            raise ConfigError(f"{name}: at most 8")
        for url in urls:
            if not 1 <= len(url.encode("utf-8")) <= 512:
                raise ConfigError(f"{name}: a URL longer than 512 bytes")
    if config.log_level.upper() not in ("DEBUG", "INFO", "WARNING", "ERROR"):
        raise ConfigError("log_level: debug, info, warning or error")


def apply(config: Config, key: str, value: str) -> None:
    fields = {f.name: f for f in dataclasses.fields(Config)}
    if key == "listen":
        config.listen = parse_listen(value)
    elif key in ("trusted_proxies", "stun_urls", "turn_urls"):
        setattr(config, key, value.split())
    elif key in ("turn_secret_file", "log_level"):
        setattr(config, key, value.strip())
    elif key in fields:
        try:
            number = int(value)
        except ValueError as exc:
            raise ConfigError(f"{key}: not a whole number") from exc
        if number < 0:
            raise ConfigError(f"{key}: negative")
        setattr(config, key, number)
    else:
        raise ConfigError(f"unknown key {key}")
