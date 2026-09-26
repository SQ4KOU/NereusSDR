#!/usr/bin/env bash
#
# caddy-check.sh: the website's Caddyfile with the rendezvous site, in
# Docker.
#
# In ubuntu:24.04 with Caddy from Caddy's official apt repository (the
# same source website/deploy/setup-server.sh installs from):
#
#   1. caddy validate passes on the real website/deploy/Caddyfile.
#   2. Caddy serves the website's pages with the same status, headers and
#      bodies with the rendezvous site as without it (the part of the
#      Caddyfile before the rendezvous site, run on its own).
#   3. A WebSocket to rv.nereussdr.com upgrades and reaches the service by
#      host name; a WebSocket to the website's names does not. Written byte
#      for byte, both the usual request (Upgrade: websocket, Host without a
#      port) and Apple's Network.framework's (Upgrade: WebSocket, Host with
#      :443) upgrade.
#   4. The service sees each client's own address, whatever X-Forwarded-For
#      the client sends: with two connections allowed per address, a third
#      from one client is refused while another client, over IPv4 or IPv6,
#      is still let in.
#   5. A plain request to rv gets the service's short plain answer (426)
#      and the headers, "Upgrade: websocket" included over HTTP/1.1.
#   6. What an HTTP/2 client gets: a plain GET, and whether Caddy offers
#      WebSockets over HTTP/2 (RFC 8441) and what one gets.
#   7. Caddy holds no UDP port (HTTP/3 stays off).
#
# Certificates come from Caddy's own local authority, switched on for the
# test only (local_certs in a copy of the Caddyfile); the real file is
# validated unchanged. Nothing leaves the containers but the apt downloads
# when the image is first built.
#
# Usage: rendezvous/tests/caddy-check.sh      (needs Docker)

set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
readonly repo
readonly image="nereus-rv-caddy:24.04"
readonly tag="$$"
readonly net="nereus-rv-caddy-${tag}"
readonly server="nereus-rv-caddy-server-${tag}"
readonly client_a="nereus-rv-caddy-a-${tag}"
readonly client_b="nereus-rv-caddy-b-${tag}"
readonly client_c="nereus-rv-caddy-c-${tag}"
readonly v4_net="198.51.100.0/24" v6_net="2001:db8:c0::/64"
readonly server_v4="198.51.100.10" server_v6="2001:db8:c0::10"
readonly marker="# The NereusSDR rendezvous service"

passed=0
pass() { passed=$((passed + 1)); printf 'ok %d - %s\n' "$passed" "$*"; }
fail() { printf 'not ok - %s\n' "$*" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/nereus-caddy-check.XXXXXX")"
cleanup() {
    docker rm -f "$server" "$client_a" "$client_b" "$client_c" >/dev/null 2>&1 || true
    docker network rm "$net" >/dev/null 2>&1 || true
    rm -rf -- "$work"
}
trap cleanup EXIT

command -v docker >/dev/null 2>&1 || fail "docker is not installed"

echo "# building ${image} (ubuntu:24.04, Caddy from Caddy's apt repository)"
docker build -q -t "$image" - >/dev/null <<'EOF'
FROM ubuntu:24.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      debian-keyring debian-archive-keyring apt-transport-https curl gnupg ca-certificates \
 && curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/gpg.key \
      | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg \
 && curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt \
      -o /etc/apt/sources.list.d/caddy-stable.list \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      caddy python3 python3-websockets python3-cryptography \
 && rm -rf /var/lib/apt/lists/*
EOF
echo "# $(docker run --rm "$image" caddy version)"

docker network create --ipv6 --subnet "$v4_net" --subnet "$v6_net" "$net" >/dev/null
docker run -d --name "$server" --network "$net" --ip "$server_v4" --ip6 "$server_v6" \
    -v "${repo}:/repo:ro" "$image" sleep infinity >/dev/null

# 1. The real file, unchanged.
out="$(docker exec "$server" caddy validate --config /repo/website/deploy/Caddyfile --adapter caddyfile 2>&1)" \
    || { printf '%s\n' "$out" >&2; fail "caddy validate refused website/deploy/Caddyfile"; }
grep -q 'Valid configuration' <<<"$out" || { printf '%s\n' "$out" >&2; fail "no 'Valid configuration' from caddy validate"; }
pass "caddy validate website/deploy/Caddyfile: Valid configuration"

# The test copies: the whole file, and the website part alone (everything
# before the rendezvous site), each with local certificates switched on in
# the global block.
docker exec -i "$server" bash -s -- "$marker" <<'EOS'
set -euo pipefail
marker="$1"
grep -q "^${marker}" /repo/website/deploy/Caddyfile
local_certs() {
    # After the global block's opening line (the first line that is only "{").
    awk 'BEGIN { done = 0 } { print } !done && $0 == "{" { print "\tlocal_certs"; print "\tskip_install_trust"; done = 1 }'
}
local_certs < /repo/website/deploy/Caddyfile > /tmp/full.Caddyfile
awk -v m="$marker" 'index($0, m) == 1 { exit } { print }' /repo/website/deploy/Caddyfile | local_certs > /tmp/website.Caddyfile
caddy validate --config /tmp/full.Caddyfile --adapter caddyfile >/dev/null 2>&1
caddy validate --config /tmp/website.Caddyfile --adapter caddyfile >/dev/null 2>&1
mkdir -p /var/www/nereussdr
cp -R /repo/website/public/. /var/www/nereussdr/
EOS
pass "test copies with local certificates validate (website alone, and whole)"

# The service, as the unprivileged nobody, on loopback as in production,
# with two connections allowed per address for step 4.
docker exec -i "$server" bash -s <<'EOS'
set -euo pipefail
mkdir -p /run/rv && chmod 755 /run/rv
python3 -c 'import secrets; print(secrets.token_hex(32))' > /run/rv/secret
chown nobody /run/rv/secret && chmod 600 /run/rv/secret
cat > /run/rv/rv.conf <<'CONF'
[rendezvous]
listen = 127.0.0.1:8710 [::1]:8710
turn_secret_file = /run/rv/secret
[limits]
connections_per_address = 2
CONF
cd / && PYTHONPATH=/repo/rendezvous/server PYTHONDONTWRITEBYTECODE=1 \
    setsid runuser -u nobody -- python3 -m nereus_rendezvous --config /run/rv/rv.conf \
    > /run/rv/log 2>&1 < /dev/null &
for _ in $(seq 50); do
    grep -q "listening on 2 addresses" /run/rv/log 2>/dev/null && exit 0
    sleep 0.2
done
cat /run/rv/log >&2
exit 1
EOS
pass "the service listens on 127.0.0.1:8710 and [::1]:8710"

caddy_run() {
    # $1: start or reload; $2: the config.
    docker exec "$server" bash -c "caddy $1 --config $2 --adapter caddyfile >/tmp/caddy-$1.log 2>&1" \
        || { docker exec "$server" cat "/tmp/caddy-$1.log" >&2; fail "caddy $1 $2"; }
    for _ in $(seq 50); do
        if docker exec "$server" bash -c "exec 3<>/dev/tcp/127.0.0.1/443" 2>/dev/null; then
            return
        fi
        sleep 0.2
    done
    fail "caddy does not listen on 443"
}

hosts=(--add-host "nereussdr.com:${server_v4}" --add-host "www.nereussdr.com:${server_v4}" --add-host "rv.nereussdr.com:${server_v4}")
docker run -d --name "$client_a" --network "$net" --ip 198.51.100.30 "${hosts[@]}" \
    -v "${repo}:/repo:ro" "$image" sleep infinity >/dev/null
docker run -d --name "$client_b" --network "$net" --ip 198.51.100.31 "${hosts[@]}" \
    -v "${repo}:/repo:ro" "$image" sleep infinity >/dev/null
docker run -d --name "$client_c" --network "$net" --ip6 2001:db8:c0::32 \
    --add-host "rv.nereussdr.com:${server_v6}" \
    -v "${repo}:/repo:ro" "$image" sleep infinity >/dev/null

# Each website request's status line, headers (all but Date) and a hash of
# its body, with redirects not followed.
snapshot() {
    docker exec -i "$client_a" bash -s <<'EOS'
set -euo pipefail
urls=(
    http://nereussdr.com/
    https://nereussdr.com/
    https://nereussdr.com/index.html
    https://nereussdr.com/404
    https://nereussdr.com/assets/css/site.css
    https://nereussdr.com/assets/js/site.js
    https://nereussdr.com/assets/img/nereus-64.png
    https://nereussdr.com/robots.txt
    https://www.nereussdr.com/
    "https://www.nereussdr.com/some/page?x=1"
)
for url in "${urls[@]}"; do
    echo "== ${url}"
    curl -sS --cacert /tmp/ca.crt -o /tmp/body -D /tmp/head "$url"
    tr -d '\r' < /tmp/head | grep -v -i '^date:' | { read -r status; echo "$status"; sort; }
    echo "body $(sha256sum < /tmp/body | cut -c1-16)"
done
EOS
}

caddy_run start /tmp/website.Caddyfile
docker exec "$server" cat /root/.local/share/caddy/pki/authorities/local/root.crt > "${work}/ca.crt"
for c in "$client_a" "$client_b" "$client_c"; do
    docker cp "${work}/ca.crt" "${c}:/tmp/ca.crt"
done
snapshot > "${work}/website-alone.txt"
caddy_run reload /tmp/full.Caddyfile
snapshot > "${work}/website-with-rv.txt"
grep -q '^HTTP/2 200' "${work}/website-alone.txt" || fail "the website did not answer 200 over HTTP/2"
if ! diff -u "${work}/website-alone.txt" "${work}/website-with-rv.txt" >&2; then
    fail "the website's answers differ once the rendezvous site is added (diff above)"
fi
pass "website requests: same status, headers and bodies with and without the rv site ($(grep -c '^== ' "${work}/website-alone.txt") compared)"

# 3 and 4. WebSockets by host name, through Caddy, from three clients.
docker exec "$client_a" python3 /repo/rendezvous/tests/caddy_probe.py open wss://rv.nereussdr.com/ \
    --cacert /tmp/ca.crt --count 3 --hold 6 > "${work}/a.txt" &
probe_a=$!
sleep 3
docker exec "$client_b" python3 /repo/rendezvous/tests/caddy_probe.py open wss://rv.nereussdr.com/ \
    --cacert /tmp/ca.crt --count 1 > "${work}/b.txt"
docker exec "$client_c" python3 /repo/rendezvous/tests/caddy_probe.py open wss://rv.nereussdr.com/ \
    --cacert /tmp/ca.crt --count 1 > "${work}/c.txt"
wait "$probe_a"
cat "${work}/a.txt" "${work}/b.txt" "${work}/c.txt" | sed 's/^/# /'
python3 - "${work}/a.txt" "${work}/b.txt" "${work}/c.txt" <<'PY' || fail "the WebSocket results above are not the expected ones"
import json, sys
a, b, c = ([json.loads(l) for l in open(p)] for p in sys.argv[1:4])
assert [x["first"] for x in a[:2]] == ["hello", "hello"], a
assert a[2]["upgraded"] and a[2]["first"] == "error" and a[2]["code"] == "tooManyConnections", a
assert b[0]["upgraded"] and b[0]["first"] == "hello", b
assert c[0]["upgraded"] and c[0]["first"] == "hello", c
PY
pass "wss://rv.nereussdr.com/ upgrades and reaches the service (hello)"
pass "the service counts each client by its own address: a third connection from one client refused (tooManyConnections) whatever X-Forwarded-For it sent, while another IPv4 client and an IPv6 client were let in"

code="$(docker exec "$client_a" curl -sS --cacert /tmp/ca.crt -o /dev/null -w '%{http_code}' \
    -H 'Connection: Upgrade' -H 'Upgrade: websocket' -H 'Sec-WebSocket-Version: 13' \
    -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' --http1.1 https://nereussdr.com/)"
[[ "$code" != "101" ]] || fail "a WebSocket to nereussdr.com was upgraded"
pass "a WebSocket request to nereussdr.com is not upgraded (answered ${code})"

# 3, byte for byte: the usual request, and Apple's.
raws="$(for pair in "websocket rv.nereussdr.com" "WebSocket rv.nereussdr.com:443"; do
    read -r upgrade host_header <<<"$pair"
    docker exec "$client_a" python3 /repo/rendezvous/tests/caddy_probe.py raw rv.nereussdr.com \
        --cacert /tmp/ca.crt --upgrade "$upgrade" --host-header "$host_header"
done)"
printf '%s\n' "$raws" | sed 's/^/# /'
python3 - <<PY || fail "a WebSocket request written byte for byte did not upgrade"
import json
lines = [json.loads(l) for l in """${raws}""".splitlines()]
assert len(lines) == 2, lines
for l in lines:
    assert l["status"].startswith("HTTP/1.1 101") and l["hello"], l
PY
pass "byte for byte over HTTP/1.1, 'Upgrade: websocket' with 'Host: rv.nereussdr.com' and Apple's 'Upgrade: WebSocket' with 'Host: rv.nereussdr.com:443' both upgrade and get hello"

# 5. A plain request to rv, over HTTP/1.1 and over HTTP/2.
for version in http1.1 http2; do
    docker exec "$client_a" curl -sS "--${version}" --cacert /tmp/ca.crt -o /tmp/rv-body -D /tmp/rv-head https://rv.nereussdr.com/
    docker exec "$client_a" cat /tmp/rv-head /tmp/rv-body | tr -d '\r' > "${work}/rv-${version}.txt"
    sed "s/^/# ${version}: /" "${work}/rv-${version}.txt"
    grep -Eq '^HTTP/(1.1|2) 426' "${work}/rv-${version}.txt" || fail "a plain ${version} request to rv did not get 426"
    for h in 'strict-transport-security: max-age=31536000' 'x-content-type-options: nosniff' \
             'referrer-policy: no-referrer' 'x-frame-options: DENY' 'cache-control: no-store' \
             "content-security-policy: default-src 'none'; frame-ancestors 'none'" \
             'content-type: text/plain; charset=utf-8'; do
        grep -qixF "$h" "${work}/rv-${version}.txt" || fail "rv's ${version} answer lacks: ${h}"
    done
    if grep -Eqi '^(server|via):' "${work}/rv-${version}.txt"; then
        fail "rv's ${version} answer names the server or the proxy"
    fi
    grep -q 'NereusSDR connection service' "${work}/rv-${version}.txt" || fail "rv's ${version} answer lacks its text"
done
grep -qixF 'upgrade: websocket' "${work}/rv-http1.1.txt" || fail "rv's HTTP/1.1 426 lacks Upgrade: websocket"
h2_upgrade="absent"
if grep -qi '^upgrade:' "${work}/rv-http2.txt"; then
    h2_upgrade="present"
fi
pass "a plain request to rv gets the service's 426, its short text and the security headers, no Server or Via header; over HTTP/1.1 with Upgrade: websocket (over HTTP/2 the Upgrade header is ${h2_upgrade}: HTTP/2 has no Upgrade)"

# 6. What an HTTP/2 client gets.
h2="$(docker exec "$client_a" python3 /repo/rendezvous/tests/caddy_probe.py h2 rv.nereussdr.com --cacert /tmp/ca.crt)"
printf '%s\n' "$h2" | sed 's/^/# h2: /'
h2_summary="$(python3 - "$h2" <<'PY'
import json, sys
r = json.loads(sys.argv[1])
assert r["alpn"] == "h2", r
assert r["get"]["status"] == 426, r
assert r["serverSettings"], r  # the SETTINGS of the server were read
if not r["enableConnectProtocol"]:
    print("Caddy does not offer WebSockets over HTTP/2 (no SETTINGS_ENABLE_CONNECT_PROTOCOL), so an RFC 8441 client opens its WebSocket over HTTP/1.1")
elif r["connect"]["hello"]:
    print("Caddy offers WebSockets over HTTP/2 and an extended CONNECT reaches the service (hello)")
else:
    print("Caddy offers WebSockets over HTTP/2 but an extended CONNECT gets status %s, reset %s, and no hello" % (
        r["connect"]["status"], r["connect"]["reset"]))
PY
)" || fail "the HTTP/2 probe: ${h2}"
pass "HTTP/2: a plain GET gets 426; ${h2_summary}"

# 7. HTTP/3 stays off.
udp="$(docker exec -i "$server" python3 - <<'PY'
import os
inodes = set()
for pid in filter(str.isdigit, os.listdir("/proc")):
    try:
        if open("/proc/%s/comm" % pid).read().strip() != "caddy":
            continue
        for fd in os.listdir("/proc/%s/fd" % pid):
            link = os.readlink("/proc/%s/fd/%s" % (pid, fd))
            if link.startswith("socket:["):
                inodes.add(link[8:-1])
    except OSError:
        continue
for name in ("udp", "udp6"):
    for line in open("/proc/net/" + name).read().splitlines()[1:]:
        if line.split()[9] in inodes:
            print(name, line.split()[1])
PY
)"
[[ -z "$udp" ]] || fail "caddy holds a UDP socket: ${udp}"
pass "caddy holds no UDP socket, so HTTP/3 is off"

echo "caddy-check: all ${passed} checks passed"
