#!/usr/bin/env bash
#
# coturn-check.sh: the relay's configuration, in Docker, on Ubuntu 24.04's
# own coturn.
#
# A server container is set up by rendezvous/deploy/setup-server.sh (with
# --no-start, as the container has no systemd) and coturn is started the
# way the unit and its drop-in start it: as the turnserver account with
# CAP_NET_BIND_SERVICE only. A peer container on the same networks plays
# the client and the relayed peer. Two networks stand in for the internet:
# a public one (documentation ranges 192.0.2.0/24 and 2001:db8:77::/64) and
# a private one (10.99.0.0/24 and fd99::/64) that the relay must never
# reach, although it could.
#
# It checks:
#   - setup-server.sh (dry run, then real, then again: nothing changes),
#     the files it writes and their modes;
#   - coturn holds UDP 3478 and 443 on both addresses and no TCP port;
#   - STUN answers on both ports in both families, without credentials;
#   - turnutils_uclient: a valid allocation relays on both ports in both
#     families, an expired one is refused, a blocked peer never receives;
#   - every denied range, reachable ones included, in each written form
#     (turn_probe.py, for the error codes);
#   - no anonymous, wrongly signed or TCP relay allocation, no TCP listener;
#   - the per-user and total quotas;
#   - credentials minted by the running service verify with coturn;
#   - what coturn does when an allocation is refreshed after its credential
#     expired (the rendezvous document, section 8), with the real nonce
#     lifetime and with a short one;
#   - what coturn logs, and that it writes no log file.
#
# Usage: rendezvous/tests/coturn-check.sh      (needs Docker; about 2 minutes)

set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
readonly repo
readonly image="nereus-rv-coturn:24.04"
readonly tag="$$"
readonly pub="nereus-rv-pub-${tag}" priv="nereus-rv-priv-${tag}"
readonly server="nereus-rv-turn-${tag}" peer="nereus-rv-peer-${tag}"
readonly s4="192.0.2.10" s6="2001:db8:77::10"
readonly p4="192.0.2.20" p6="2001:db8:77::20"
readonly q4="10.99.0.20" q6="fd99::20"

passed=0
pass() { passed=$((passed + 1)); printf 'ok %d - %s\n' "$passed" "$*"; }
fail() { printf 'not ok - %s\n' "$*" >&2; exit 1; }

cleanup() {
    docker rm -f "$server" "$peer" >/dev/null 2>&1 || true
    docker network rm "$pub" "$priv" >/dev/null 2>&1 || true
}
trap cleanup EXIT

command -v docker >/dev/null 2>&1 || fail "docker is not installed"

echo "# building ${image} (ubuntu:24.04 with Ubuntu's coturn and Python packages)"
docker build -q -t "$image" - >/dev/null <<'EOF'
FROM ubuntu:24.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      coturn python3 python3-websockets python3-cryptography python3-pytest \
 && rm -rf /var/lib/apt/lists/*
EOF
echo "# coturn $(docker run --rm "$image" dpkg-query -W -f='${Version}' coturn), $(docker run --rm "$image" turnserver --version 2>/dev/null | head -1)"

docker network create --ipv6 --subnet 192.0.2.0/24 --subnet 2001:db8:77::/64 "$pub" >/dev/null
docker network create --ipv6 --subnet 10.99.0.0/24 --subnet fd99::/64 "$priv" >/dev/null
docker run -d --name "$server" --network "$pub" --ip "$s4" --ip6 "$s6" \
    -v "${repo}:/repo:ro" "$image" sleep infinity >/dev/null
docker network connect --ip 10.99.0.10 --ip6 fd99::10 "$priv" "$server"
docker run -d --name "$peer" --network "$pub" --ip "$p4" --ip6 "$p6" \
    -v "${repo}:/repo:ro" "$image" sleep infinity >/dev/null
docker network connect --ip "$q4" --ip6 "$q6" "$priv" "$peer"

sx() { docker exec -i "$server" bash -s -- "$@"; }
px() { docker exec -i "$peer" bash -s -- "$@"; }
probe() { docker exec "$peer" python3 /repo/rendezvous/tests/turn_probe.py "$@"; }
# Test-only settings for setup-server.sh: the documentation addresses stand
# in for public ones.
readonly setup_env="RV_PUBLIC_IPV4=${s4} RV_PUBLIC_IPV6=${s6} RV_ALLOW_DOCUMENTATION_ADDRESSES=1"

# --------------------------------------------------------------- setup
sx <<'EOS'
set -euo pipefail
# The deploy account the website's setup makes on the real server.
useradd -m -s /bin/bash nereusweb
# A syslog socket, as journald provides on the server, so coturn's log
# lines can be read.
cat > /usr/local/bin/syslog-capture <<'PY'
import os, socket, sys
try:
    os.unlink("/dev/log")
except FileNotFoundError:
    pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind("/dev/log")
os.chmod("/dev/log", 0o666)
with open("/run/syslog.txt", "ab", buffering=0) as out:
    while True:
        out.write(s.recv(65536).rstrip(b"\n") + b"\n")
PY
setsid python3 /usr/local/bin/syslog-capture </dev/null >/dev/null 2>&1 &
EOS

out="$(sx <<EOS 2>&1
set -euo pipefail
env ${setup_env} bash /repo/rendezvous/deploy/setup-server.sh --dry-run --no-start
EOS
)" || { printf '%s\n' "$out" >&2; fail "setup-server.sh --dry-run failed"; }
sx <<'EOS' || fail "the dry run changed something"
test ! -e /etc/nereus-rendezvous && test ! -e /etc/systemd/system/nereus-rendezvous.service
EOS
pass "setup-server.sh --dry-run passes and changes nothing"

out="$(sx <<EOS 2>&1
set -euo pipefail
env ${setup_env} bash /repo/rendezvous/deploy/setup-server.sh --no-start
EOS
)" || { printf '%s\n' "$out" >&2; fail "setup-server.sh failed"; }
printf '%s\n' "$out" | grep -E 'bps-capacity|made|installed|changed' | sed 's/^/# /'
pass "setup-server.sh --no-start"

out="$(sx <<EOS 2>&1
set -euo pipefail
env ${setup_env} bash /repo/rendezvous/deploy/setup-server.sh --no-start
EOS
)" || { printf '%s\n' "$out" >&2; fail "setup-server.sh (second run) failed"; }
if grep -q 'changed$' <<<"$out"; then
    printf '%s\n' "$out" >&2
    fail "a second run of setup-server.sh changed a file"
fi
pass "a second run changes nothing (idempotent)"

sx <<'EOS' || fail "files, owners or modes are not as expected"
set -euo pipefail
check() { [[ "$(stat -c '%U:%G %a' "$1")" == "$2" ]] || { echo "$1: $(stat -c '%U:%G %a' "$1"), want $2" >&2; exit 1; }; }
check /etc/nereus-rendezvous/turn-secret "root:root 600"
check /etc/turnserver.conf "root:turnserver 640"
check /etc/nereus-rendezvous/coturn/key.pem "root:turnserver 640"
check /etc/nereus-rendezvous/rendezvous.conf "root:root 644"
check /etc/systemd/system/nereus-rendezvous.service "root:root 644"
check /etc/systemd/system/coturn.service.d/nereus.conf "root:root 644"
check /opt/nereus-rendezvous "nereusweb:nereusweb 755"
[[ "$(wc -c < /etc/nereus-rendezvous/turn-secret)" -eq 65 ]]
grep -qx "static-auth-secret=$(cat /etc/nereus-rendezvous/turn-secret)" /etc/turnserver.conf
grep -qx 'turn_secret_file = /run/credentials/nereus-rendezvous.service/turn-secret' /etc/nereus-rendezvous/rendezvous.conf
EOS
pass "secret (root 600, 64 hex characters), coturn configuration (root:turnserver 640), service configuration, units and code directory"

# --------------------------------------------------------------- coturn
start_coturn() {
    # $1: configuration file. As the unit and drop-in start it: the
    # turnserver account, CAP_NET_BIND_SERVICE and nothing else.
    sx "$1" <<'EOS'
set -euo pipefail
cd /
setsid setpriv --reuid=turnserver --regid=turnserver --init-groups \
    --inh-caps=-all,+net_bind_service --ambient-caps=-all,+net_bind_service \
    --bounding-set=-all,+net_bind_service \
    turnserver -c "$1" --pidfile= </dev/null >/run/turnserver.stdout 2>&1 &
for _ in $(seq 50); do
    grep -q 'Total General servers' /run/syslog.txt 2>/dev/null && break
    sleep 0.1
done
sleep 0.5
EOS
}
stop_coturn() {
    sx <<'EOS'
for pid in $(ls /proc | grep -E '^[0-9]+$'); do
    [[ "$(cat /proc/$pid/comm 2>/dev/null)" == turnserver ]] && kill "$pid" 2>/dev/null
done
sleep 1
EOS
}

start_coturn /etc/turnserver.conf
sockets="$(sx <<'EOS'
python3 - <<'PY'
import os, socket
def addr(h):
    host, port = h.split(":")
    raw = bytes.fromhex(host)
    if len(raw) == 4:
        return socket.inet_ntop(socket.AF_INET, raw[::-1]), int(port, 16)
    return socket.inet_ntop(socket.AF_INET6, b"".join(raw[i:i + 4][::-1] for i in range(0, 16, 4))), int(port, 16)
# Docker's root may not read another account's file descriptors, so the
# sockets are found by owner: every socket of the turnserver account.
import pwd
uid = str(pwd.getpwnam("turnserver").pw_uid)
for pid in filter(str.isdigit, os.listdir("/proc")):
    try:
        if open("/proc/%s/comm" % pid).read().strip() != "turnserver":
            continue
        caps = [l for l in open("/proc/%s/status" % pid) if l.startswith(("Uid:", "CapEff:"))]
        print("process", " ".join(l.split()[1] for l in caps))
    except OSError:
        pass
seen = set()
for name in ("udp", "udp6", "tcp", "tcp6"):
    for line in open("/proc/net/" + name).read().splitlines()[1:]:
        f = line.split()
        if f[7] == uid and (name.startswith("udp") or f[3] == "0A"):
            host, port = addr(f[1])
            seen.add("%s %s %d" % (name.rstrip("6"), host, port))
for s in sorted(seen):
    print(s)
PY
EOS
)"
printf '%s\n' "$sockets" | sed 's/^/# /'
expected="$(printf 'udp %s 3478\nudp %s 443\nudp %s 3478\nudp %s 443\n' "$s6" "$s6" "$s4" "$s4" | sort)"
[[ "$(printf '%s\n' "$sockets" | grep -v '^process' | sort)" == "$expected" ]] \
    || { sx <<<'tail -20 /run/syslog.txt; cat /run/turnserver.stdout' >&2; fail "coturn's sockets are not exactly UDP 3478 and 443 on both addresses"; }
grep -q '^process 100 0000000000000400$' <<<"$sockets" \
    || fail "coturn does not run as turnserver with CAP_NET_BIND_SERVICE only"
pass "coturn (as turnserver, CAP_NET_BIND_SERVICE only) holds exactly UDP 3478 and 443 on ${s4} and ${s6}, and no TCP port"

for host in "$s4" "$s6"; do
    for port in 3478 443; do
        line="$(probe binding "$host" "$port")" || fail "no STUN answer on ${host} UDP ${port}"
        grep -q '"ok": true' <<<"$line" || fail "STUN on ${host} UDP ${port}: ${line}"
    done
done
pass "STUN binding without credentials answers on UDP 3478 and 443, IPv4 and IPv6"

# Relay peers: an echo on the public network, and echoes the relay must
# never reach (private network, and loopback on the server itself).
docker exec -d "$peer" turnutils_peer -L "$p4" -L "$p6" -L "$q4" -L "$q6" -p 3480
docker exec -d "$server" turnutils_peer -L 127.0.0.1 -L ::1 -p 3480
sleep 1

# Credentials in coturn's time-limited form, computed here, not by the
# service: "<expiry>:<id>" and base64(HMAC-SHA1(secret, username)).
docker cp "${server}:/etc/nereus-rendezvous/turn-secret" - | docker cp - "${peer}:/tmp/"
creds() {
    # $1: seconds until expiry (negative: already expired); $2: id.
    px "$1" "$2" <<'EOS'
python3 - "$1" "$2" <<'PY'
import base64, hashlib, hmac, sys, time
secret = open("/tmp/turn-secret", "rb").read().rstrip(b"\r\n")
user = "%d:%s" % (int(time.time()) + int(sys.argv[1]), sys.argv[2])
print(user, base64.b64encode(hmac.new(secret, user.encode(), hashlib.sha1).digest()).decode())
PY
EOS
}

# uclient: no RTCP pair (-c), 20 messages to the peer; its whole output.
uclient() {
    # $1 user $2 password $3 server $4 port $5 peer; prints uclient's summary.
    # uclient keeps its allocations until they expire, so each run uses an
    # id of its own (user-quota) and coturn is restarted before it
    # (total-quota).
    docker exec "$peer" turnutils_uclient -c -n 20 -u "$1" -w "$2" -e "$5" -r 3480 -p "$4" "$3" 2>&1 || true
}
for combo in "$s4 3478 $p4 uclientaaaaaaaaaaaaaaaaaaa" "$s4 443 $p4 uclientbbbbbbbbbbbbbbbbbbb" \
        "$s6 3478 $p6 uclientccccccccccccccccccc" "$s6 443 $p6 uclientddddddddddddddddddd"; do
    read -r host port target id <<<"$combo"
    stop_coturn
    start_coturn /etc/turnserver.conf
    read -r user password < <(creds 600 "$id")
    out="$(uclient "$user" "$password" "$host" "$port" "$target")"
    grep -q 'Total lost packets 0 (0.000000%)' <<<"$out" \
        || { printf '%s\n' "$out" >&2; fail "uclient via ${host} UDP ${port} to ${target} lost packets"; }
    grep -q 'Total transmit time' <<<"$out" || fail "uclient via ${host} UDP ${port} did not run"
done
pass "turnutils_uclient: valid allocations relay to the peer with no loss, UDP 3478 and 443, IPv4 and IPv6"

stop_coturn
start_coturn /etc/turnserver.conf
read -r user password < <(creds -60 uclientexpiredexpiredexpir)
out="$(uclient "$user" "$password" "$s4" 3478 "$p4")"
if ! grep -q 'Cannot complete Allocation' <<<"$out" || grep -q 'Total lost packets' <<<"$out"; then
    printf '%s\n' "$out" >&2
    fail "uclient allocated with an expired credential"
fi
# uclient does not print the error code; the probe does.
out="$(probe allocate "$s4" 3478 --username "$user" --password "$password" || true)"
grep -q '"error": 401' <<<"$out" || fail "an expired credential was not refused 401: ${out}"
pass "turnutils_uclient: an expired credential gets no allocation (Cannot complete Allocation; the probe sees 401)"

for target in "$q4" "$q6"; do
    stop_coturn
    start_coturn /etc/turnserver.conf
    read -r user password < <(creds 600 uclienteeeeeeeeeeeeeeeeeee)
    out="$(uclient "$user" "$password" "$s4" 3478 "$target")"
    if grep -q 'Total lost packets 0 (0.000000%)' <<<"$out"; then
        printf '%s\n' "$out" >&2
        fail "uclient reached ${target} through the relay"
    fi
done
pass "turnutils_uclient: nothing reaches the reachable private peers ${q4} and ${q6}"

stop_coturn
start_coturn /etc/turnserver.conf

# Every denied range, in each written form, with coturn's answer.
v4peers=(--peer "${p4}:3480" --peer "${q4}:3480" --peer 127.0.0.1:3480 --peer 0.0.0.0:3480
    --peer 172.16.0.1:3480 --peer 192.168.1.1:3480 --peer 100.64.0.1:3480 --peer 169.254.1.1:3480
    --peer 224.0.0.1:3480 --peer 255.255.255.255:3480)
v6peers=(--peer "[${p6}]:3480" --peer "[${q6}]:3480" --peer "[::1]:3480" --peer "[::]:3480"
    --peer "[fe80::1]:3480" --peer "[ff02::1]:3480" --peer "[::ffff:127.0.0.1]:3480"
    --peer "[::ffff:${q4}]:3480" --peer "[::ffff:${p4}]:3480" --peer "[64:ff9b::a63:14]:3480"
    --peer "[64:ff9b:1::a63:14]:3480")
peers4="$(probe allocate "$s4" 3478 --secret-file /tmp/turn-secret --id peersipv4peersipv4peersipv "${v4peers[@]}")"
peers6="$(probe allocate "$s6" 443 --secret-file /tmp/turn-secret --id peersipv6peersipv6peersipv "${v6peers[@]}")"
printf '%s\n%s\n' "$peers4" "$peers6" | sed 's/^/# /'
python3 - "$p4" "$p6" <<PY || fail "a denied peer was allowed, or the public peer was not"
import json, sys
lines = [json.loads(l) for l in """${peers4}
${peers6}""".splitlines()]
public = {"%s:3480" % sys.argv[1], "[%s]:3480" % sys.argv[2]}
peers = [l for l in lines if l["step"] == "peer"]
assert len(peers) == 21, len(peers)
for l in peers:
    if l["peer"] in public:
        assert l["permission"]["ok"] and l["relayed"], l
    else:
        assert not l["permission"]["ok"] and l["permission"]["error"] in (403, 443), l
PY
pass "denied peers: loopback, 0/8, RFC 1918, 100.64/10, link-local, multicast, broadcast, ::, unique-local, and IPv4-mapped and NAT64 forms all refused (403); the public peer relays in both families"

out="$(probe allocate "$s4" 3478 --anonymous || true)"
grep -q '"error": 401' <<<"$out" || fail "an anonymous allocation was not refused: ${out}"
out="$(probe allocate "$s4" 3478 --username "$(date +%s)0:someone" --password wrong || true)"
grep -q '"error": 401' <<<"$out" || fail "a wrongly signed allocation was not refused: ${out}"
read -r user password < <(creds 600 tcprelaytcprelaytcprelaytc)
out="$(probe allocate "$s4" 3478 --username "$user" --password "$password" --relay tcp || true)"
grep -q '"error": 442' <<<"$out" || fail "a TCP relay allocation was not refused: ${out}"
out="$(probe allocate "$s4" 3478 --username "$user" --password "$password" --transport tcp 2>&1 || true)"
grep -q 'ConnectionRefusedError' <<<"$out" || fail "TCP 3478 answered: ${out}"
pass "refused: anonymous (401), wrong password (401), TCP relay (442); TCP 3478 is closed"

# UDP 443 is coturn's DTLS-role port (turnserver.conf, "Ports"), with an
# empty cipher list: a DTLS client gets nowhere, plain TURN works (checked
# above).
read -r user password < <(creds 600 dtlsdtlsdtlsdtlsdtlsdtlsdt)
out="$(docker exec "$peer" timeout 20 turnutils_uclient -S -c -n 5 -u "$user" -w "$password" -e "$p4" -r 3480 -p 443 "$s4" 2>&1 || true)"
if grep -q 'Total lost packets' <<<"$out"; then
    printf '%s\n' "$out" >&2
    fail "a DTLS session was set up with coturn on UDP 443"
fi
pass "no DTLS session with coturn on UDP 443 (empty cipher list), while plain TURN there works"

stop_coturn
start_coturn /etc/turnserver.conf
quota="$(px "$s4" <<'EOS'
set -euo pipefail
cd /tmp
cat > hold.py <<'PY'
import sys, time
sys.path.insert(0, "/repo/rendezvous/tests")
import turn_probe as t
from argparse import Namespace
host, ids = sys.argv[1], sys.argv[2:]
clients = []
for sid in ids:
    c = t.Client(host, 3478)
    user, password, _ = t.credentials("/tmp/turn-secret", sid, 600)
    c.login(user, password)
    r = t.outcome(t.allocate(c, Namespace(relay="udp", family=None)))
    print(sid[:6], "ok" if r["ok"] else r["error"])
    clients.append(c)
PY
python3 hold.py "$1" useraaaaaaaaaaaaaaaaaaaaaa useraaaaaaaaaaaaaaaaaaaaaa useraaaaaaaaaaaaaaaaaaaaaa \
    userbbbbbbbbbbbbbbbbbbbbbb userbbbbbbbbbbbbbbbbbbbbbb usercccccccccccccccccccccc
EOS
)"
printf '%s\n' "$quota" | sed 's/^/# /'
[[ "$(printf '%s\n' "$quota" | tr '\n' ' ')" == "useraa ok useraa ok useraa 486 userbb ok userbb ok usercc 486 " ]] \
    || fail "quotas: expected ok ok 486 ok ok 486"
pass "quotas: a third allocation by one station id is refused (486, user-quota 2); a fifth on the server is refused (486, total-quota 4 from bps-capacity 347222 / max-bps 80000)"

# --------------------------------------------------- the service's credentials
stop_coturn
start_coturn /etc/turnserver.conf
sx <<'EOS'
set -euo pipefail
# LoadCredential=, by hand: systemd would copy the secret here for the
# service alone.
install -d -m 0755 /run/credentials/nereus-rendezvous.service
install -m 0400 -o nobody /etc/nereus-rendezvous/turn-secret /run/credentials/nereus-rendezvous.service/turn-secret
cd / && PYTHONPATH=/repo/rendezvous/server PYTHONDONTWRITEBYTECODE=1 \
    setsid runuser -u nobody -- python3 -m nereus_rendezvous --config /etc/nereus-rendezvous/rendezvous.conf \
    </dev/null >/run/rv.log 2>&1 &
for _ in $(seq 50); do grep -q 'listening on 2 addresses, relay on' /run/rv.log && exit 0; sleep 0.2; done
cat /run/rv.log >&2
exit 1
EOS
minted="$(sx <<'EOS'
cd /repo/rendezvous/tests && PYTHONPATH=/repo/rendezvous/server:/repo/rendezvous/tests PYTHONDONTWRITEBYTECODE=1 python3 - <<'PY'
import asyncio, json
from nereus_rendezvous import protocol
from helpers import register, recv_json, introduce_message
from runner import ws_connect
async def go():
    uri = "ws://127.0.0.1:8710/"
    st, _, sid = await register(uri, address="198.51.100.7")
    cl = await ws_connect(uri, "198.51.100.8")
    hello = await recv_json(cl)
    await cl.send(protocol.encode(introduce_message(sid, hello["nonce"])))
    intro = await recv_json(st)
    await st.send(protocol.encode({"type": "answer", "to": intro["from"], "answer": "v=0\r\n", "turn": True}))
    answer = await recv_json(cl)
    credentials = await recv_json(st)
    assert answer["turn"] == credentials["turn"]
    print(json.dumps(answer["turn"]))
asyncio.run(go())
PY
EOS
)"
user="$(printf '%s' "$minted" | python3 -c 'import json,sys; print(json.load(sys.stdin)["username"])')"
password="$(printf '%s' "$minted" | python3 -c 'import json,sys; print(json.load(sys.stdin)["password"])')"
urls="$(printf '%s' "$minted" | python3 -c 'import json,sys; print(" ".join(json.load(sys.stdin)["urls"]))')"
echo "# minted for a station: urls ${urls}"
out="$(probe allocate "$s4" 443 --username "$user" --password "$password" --peer "${p4}:3480")"
grep -q '"relayed": true' <<<"$out" || fail "coturn did not accept the service's credentials: ${out}"
out="$(probe allocate "$s6" 3478 --username "$user" --password "$password" --peer "[${p6}]:3480")"
grep -q '"relayed": true' <<<"$out" || fail "coturn did not accept the service's credentials over IPv6: ${out}"
pass "credentials minted by the running service (setup's configuration, secret via the credentials path) allocate and relay on coturn"

# --------------------------------------------------- refresh after expiry
expiry="$(probe expiry "$s4" 3478 --secret-file /tmp/turn-secret --id expiryexpiryexpiryexpiryex \
    --ttl 5 --wait 3 --wait 12 --peer "${p4}:3480")"
printf '%s\n' "$expiry" | sed 's/^/# real configuration (stale-nonce 600): /'
sx <<'EOS'
set -euo pipefail
# A test-only copy with a 15 s nonce lifetime and its own ports, to see what
# happens once the nonce the allocation was made with has gone stale.
sed -e 's/^stale-nonce=600$/stale-nonce=15/' -e 's/^listening-port=3478$/listening-port=5000/' \
    -e 's/^tls-listening-port=443$/tls-listening-port=5001/' -e 's/^min-port=61000$/min-port=60000/' \
    -e 's/^max-port=65535$/max-port=60500/' /etc/turnserver.conf > /run/stale.conf
chown root:turnserver /run/stale.conf && chmod 640 /run/stale.conf
EOS
start_coturn /run/stale.conf
stale="$(probe expiry "$s4" 5000 --secret-file /tmp/turn-secret --id staleexpirystaleexpirystal \
    --ttl 5 --wait 10 --wait 20 --wait 40 --peer "${p4}:3480")"
printf '%s\n' "$stale" | sed 's/^/# stale-nonce 15 (test only): /'
python3 - <<PY || fail "coturn's refresh-after-expiry behaviour is not the one recorded in the rendezvous document section 8"
import json
real = [json.loads(l) for l in """${expiry}""".splitlines()]
stale = [json.loads(l) for l in """${stale}""".splitlines()]
for lines in (real, stale):
    refreshes = [l for l in lines if l["step"] == "refresh"]
    assert all(l["ok"] for l in refreshes), refreshes
    assert any(l["credentialExpired"] for l in refreshes), refreshes
    assert all(l["permission"]["ok"] and l["relayed"] for l in lines if l["step"] == "peer-after-refresh"), lines
    late = [l for l in lines if l["step"] == "new-allocation-with-expired-credential"]
    assert late and late[0]["error"] == 401, late
assert any(l.get("firstAnswer") == 438 for l in stale if l["step"] == "refresh"), stale
PY
pass "refresh after expiry: coturn keeps refreshing an allocation (and granting permissions that relay) after its credential expired, also through a stale-nonce challenge (438, then accepted); only a new allocation with the expired credential is refused (401)"

# --------------------------------------------------------------- logs
logs="$(sx <<'EOS'
set -euo pipefail
test ! -e /var/log/turnserver
! ls /var/log/turn*.log /var/tmp/turn*.log /tmp/turn*.log /var/log/turn_*.log >/dev/null 2>&1
[[ ! -s /run/turnserver.stdout ]]
echo "lines $(grep -c . /run/syslog.txt)"
sed -E 's/^<[0-9]+>[A-Z][a-z]{2} [ 0-9]{2} [0-9:]{8} turnserver: [0-9]+: : //' /run/syslog.txt \
    | grep -E 'ERROR|WARNING|session|user|peer|realm' | sed -E 's/[0-9]{9,10}:[a-z0-9]{6}[a-z0-9]*/<expiry>:<station id>/g; s/[0-9a-f]{1,4}(:[0-9a-f]{0,4}){2,7}|([0-9]{1,3}\.){3}[0-9]{1,3}/<address>/g' \
    | sort | uniq -c | sort -rn
EOS
)" || fail "coturn wrote a log file or to standard output"
printf '%s\n' "$logs" | sed 's/^/# log: /'
pass "coturn logs to syslog only (no log file, nothing on standard output); the kinds of line it logs are listed above"

stop_coturn
echo "coturn-check: all ${passed} checks passed"
