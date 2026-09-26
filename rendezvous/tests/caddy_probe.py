# no-port-check: NereusSDR-original.
"""The client side of rendezvous/tests/caddy-check.sh: WebSockets through
Caddy to the rendezvous service, by host name, over TLS checked against the
local certificate authority Caddy made for the test.

Not a pytest module (no test_ prefix); the check runs it in its client
containers, on Ubuntu's python3 and python3-websockets.

  open URL --cacert F --count N [--hold S]
      Open N WebSockets one after another, each claiming a different
      X-Forwarded-For of its own (which Caddy must replace), print the first
      message each receives, then keep the open ones for S seconds.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import ssl
import sys

try:  # websockets 13 and later
    from websockets.asyncio.client import connect as _connect

    _HEADERS = "additional_headers"
except ImportError:  # Ubuntu 24.04's 10.4
    from websockets.legacy.client import connect as _connect  # type: ignore[no-redef]

    _HEADERS = "extra_headers"


async def run(args) -> int:
    context = ssl.create_default_context(cafile=args.cacert)
    held = []
    for n in range(args.count):
        claimed = "203.0.113.%d" % (n + 1)
        try:
            ws = await _connect(args.url, ssl=context, **{_HEADERS: {"X-Forwarded-For": claimed}})
        except Exception as exc:  # noqa: BLE001 - report and go on
            print(json.dumps({"n": n, "upgraded": False, "error": type(exc).__name__}), flush=True)
            continue
        first = json.loads(await asyncio.wait_for(ws.recv(), 5))
        print(json.dumps({"n": n, "upgraded": True, "first": first.get("type"), "code": first.get("code")}), flush=True)
        held.append(ws)
    await asyncio.sleep(args.hold)
    for ws in held:
        await ws.close()
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="caddy_probe")
    sub = parser.add_subparsers(dest="command", required=True)
    o = sub.add_parser("open")
    o.add_argument("url")
    o.add_argument("--cacert", required=True)
    o.add_argument("--count", type=int, default=1)
    o.add_argument("--hold", type=float, default=0.0)
    args = parser.parse_args(argv)
    return asyncio.run(run(args))


if __name__ == "__main__":
    sys.exit(main())
