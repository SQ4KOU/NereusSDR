# no-port-check: NereusSDR-original.
"""The service stores nothing about stations on disk, and its log holds no
secret, password, SDP, candidate, body, nonce, signature, key, full id or
address.

The service runs as its own process, as systemd runs it, with its working
directory, HOME and TMPDIR in an empty sandbox; after a full session of
every kind the sandbox is still empty and the configuration directory is
unchanged. A source scan backs this up: the only file the code opens is
read-only, in config.py.
"""

import ast
import asyncio
import os
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest
from cryptography.hazmat.primitives.asymmetric import ec

from nereus_rendezvous import identity, protocol
from runner import ws_connect

SERVER = Path(__file__).resolve().parent.parent / "server"


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _ipv6_loopback() -> bool:
    try:
        with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
            s.bind(("::1", 0))
        return True
    except OSError:
        return False


def _tree(root: Path):
    return sorted((str(p.relative_to(root)), p.stat().st_size, p.stat().st_mtime_ns) for p in root.rglob("*"))


async def _recv(ws):
    import json

    return json.loads(await asyncio.wait_for(ws.recv(), 5))


async def _full_session(uri: str, secret_values: list) -> None:
    key = ec.generate_private_key(ec.SECP256R1())
    spki = identity.spki_of(key.public_key())
    sid = identity.rendezvous_id(spki)
    secret_values += [sid, identity.to_b64url(spki)]
    st = await ws_connect(uri, "198.51.100.77")
    hello = await _recv(st)
    secret_values.append(hello["nonce"])
    await st.send(protocol.encode({"type": "register", "id": sid, "publicKey": identity.to_b64url(spki)}))
    challenge = await _recv(st)
    secret_values.append(challenge["nonce"])
    proof = identity.to_b64url(identity.sign_raw(key, identity.register_transcript(identity.from_b64url(challenge["nonce"]))))
    secret_values.append(proof)
    await st.send(protocol.encode({"type": "prove", "signature": proof}))
    assert (await _recv(st))["type"] == "registered"

    device = ec.generate_private_key(ec.SECP256R1())
    cl = await ws_connect(uri, "2001:db8:77::5")
    chello = await _recv(cl)
    secret_values.append(chello["nonce"])
    dsig = identity.to_b64url(identity.sign_raw(device, identity.introduce_transcript(sid, identity.from_b64url(chello["nonce"]))))
    did = identity.to_b64url(identity.fingerprint(identity.spki_of(device.public_key())))
    offer = "v=0\r\na=ice-ufrag:SECRETOFFERUFRAG\r\n"
    secret_values += [dsig, did, "SECRETOFFERUFRAG"]
    await cl.send(protocol.encode({"type": "introduce", "id": sid, "device": did, "deviceSignature": dsig, "offer": offer}))
    intro = await _recv(st)
    secret_values.append(intro["from"])
    await st.send(protocol.encode({"type": "answer", "to": intro["from"], "answer": "v=0\r\na=ice-ufrag:SECRETANSWERUFRAG\r\n", "turn": True}))
    secret_values.append("SECRETANSWERUFRAG")
    answer = await _recv(cl)
    assert answer["turn"] is not None
    secret_values += [answer["turn"]["password"], answer["turn"]["username"]]
    assert (await _recv(st))["type"] == "credentials"
    cand = "candidate:1 1 UDP 1 203.0.113.250 4444 typ host"
    secret_values += ["203.0.113.250"]
    await st.send(protocol.encode({"type": "candidate", "to": intro["from"], "candidate": cand}))
    assert (await _recv(cl))["candidate"] == cand
    await cl.close()
    assert (await _recv(st))["type"] == "introduction.end"

    await st.send(protocol.encode({"type": "nameplate.claim"}))
    number = (await _recv(st))["nameplate"]
    mb = await ws_connect(uri, "203.0.113.99")
    await _recv(mb)
    await mb.send(protocol.encode({"type": "mailbox.open", "nameplate": number}))
    assert (await _recv(mb))["type"] == "mailbox.opened"
    assert (await _recv(st))["type"] == "mailbox.opened"
    body = '{"type":"pair.spake","step":1,"data":"SECRETMAILBOXBODY"}'
    secret_values.append("SECRETMAILBOXBODY")
    await mb.send(protocol.encode({"type": "mailbox", "body": body}))
    assert (await _recv(st))["body"] == body
    await mb.close()
    assert (await _recv(st))["type"] == "mailbox.closed"
    await st.close()


@pytest.mark.parametrize("family", ["ipv4", "ipv6"])
def test_the_running_service_writes_nothing_and_logs_no_secret(tmp_path, family):
    if family == "ipv6" and not _ipv6_loopback():
        pytest.skip("this machine has no IPv6 loopback")
    sandbox = tmp_path / "sandbox"
    (sandbox / "home").mkdir(parents=True)
    (sandbox / "tmp").mkdir()
    etc = tmp_path / "etc"
    etc.mkdir()
    secret = os.urandom(24).hex()
    (etc / "turn-secret").write_text(secret + "\n")
    port = _free_port()
    listen = f"127.0.0.1:{port}" + (f" [::1]:{port}" if _ipv6_loopback() else "")
    (etc / "rendezvous.conf").write_text(
        "[rendezvous]\n"
        f"listen = {listen}\n"
        f"turn_secret_file = {etc / 'turn-secret'}\n"
        "log_level = debug\n"
    )
    before_etc = _tree(etc)
    env = dict(os.environ)
    env.update(
        HOME=str(sandbox / "home"),
        TMPDIR=str(sandbox / "tmp"),
        # A new HOME hides a per-user site-packages directory; keep the
        # parent's module path so the same websockets and cryptography load.
        PYTHONPATH=os.pathsep.join([str(SERVER)] + [p for p in sys.path if p and Path(p).is_dir()]),
        PYTHONDONTWRITEBYTECODE="1",
    )
    proc = subprocess.Popen(
        [sys.executable, "-m", "nereus_rendezvous", "--config", str(etc / "rendezvous.conf")],
        cwd=str(sandbox),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    secret_values = [secret]
    try:
        host = "127.0.0.1" if family == "ipv4" else "[::1]"
        uri = f"ws://{host}:{port}/"
        deadline = time.time() + 10
        while True:
            try:
                with socket.create_connection((host.strip("[]"), port), timeout=0.2):
                    break
            except OSError:
                if time.time() > deadline or proc.poll() is not None:
                    proc.kill()
                    out, err = proc.communicate()
                    raise AssertionError("the service did not start: " + (out + err).decode(errors="replace"))
                time.sleep(0.05)
        asyncio.run(_full_session(uri, secret_values))
    finally:
        proc.send_signal(signal.SIGTERM)
        out, err = proc.communicate(timeout=10)
    assert proc.returncode == 0, err.decode(errors="replace")
    assert [p for p in sandbox.rglob("*") if not p.is_dir()] == []
    assert sorted(p.name for p in sandbox.iterdir()) == ["home", "tmp"]
    assert list((sandbox / "home").iterdir()) == [] and list((sandbox / "tmp").iterdir()) == []
    assert _tree(etc) == before_etc
    log = (out + err).decode("utf-8", errors="replace")
    assert "registered" in log and "introduction" in log and "mailbox" in log
    for value in secret_values:
        assert value not in log, f"the log holds a value it must not: {value[:12]}"
    for address in ("198.51.100.77", "2001:db8:77", "203.0.113.99", "127.0.0.1", "::1"):
        assert address not in log
    # Ids appear only as their first six characters.
    for token in re.findall(r"[a-z2-7]{7,}", log):
        assert token not in "".join(secret_values)


FORBIDDEN_IMPORTS = {"sqlite3", "shelve", "pickle", "dbm", "tempfile", "shutil"}
FORBIDDEN_ATTRS = {"write_text", "write_bytes", "makedirs", "mkdir", "FileHandler", "RotatingFileHandler", "dump"}


def test_the_source_opens_no_file_for_writing():
    for path in sorted((SERVER / "nereus_rendezvous").glob("*.py")):
        tree = ast.parse(path.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                assert not {a.name.split(".")[0] for a in node.names} & FORBIDDEN_IMPORTS, path.name
            if isinstance(node, ast.ImportFrom) and node.module:
                assert node.module.split(".")[0] not in FORBIDDEN_IMPORTS, path.name
            if isinstance(node, ast.Attribute):
                assert node.attr not in FORBIDDEN_ATTRS, f"{path.name}: {node.attr}"
            if isinstance(node, ast.Call) and isinstance(node.func, (ast.Name, ast.Attribute)):
                name = node.func.id if isinstance(node.func, ast.Name) else node.func.attr
                if name == "open":
                    assert path.name == "config.py", f"{path.name} opens a file"
                    mode = node.args[1].value if len(node.args) > 1 else "r"
                    assert mode in ("r", "rb"), f"{path.name} opens a file with mode {mode}"
