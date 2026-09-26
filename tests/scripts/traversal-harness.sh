#!/usr/bin/env bash
# no-port-check: NereusSDR-original.
# =================================================================
# tests/scripts/traversal-harness.sh  (NereusSDR)
# =================================================================
#
# iPhone app plan Task 27 (R-IOS-16): reaching a Core through the remote
# access service across real NATs, on one Linux machine, with nothing
# leaving it. Linux network namespaces and nftables stand up a small
# internet:
#
#   rvsrv   the service's host: the rendezvous (rendezvous/server) behind a
#           TLS terminator, coturn for STUN and TURN, and DNS (a plain
#           resolver, and DNS64 for the IPv6-only client)
#   inet    the router between everyone's public addresses
#   nats    the Core's NAT router          sta   the Core (station role)
#   natc    the client's NAT router        cli   the client
#   plat    a NAT64 router (IPv6 inside)   cli6  an IPv6-only client with a
#                                                CLAT (464XLAT)
#
# and runs tests/tools/nereus_rendezvous_peer in `sta` and in `cli`/`cli6`
# for each scenario, checking how the connection was made:
#
#   eim-nat            both behind endpoint-independent NAT: direct
#   random-nat-both    port-randomising NAT at both ends: relayed
#   udp-direct-blocked UDP between the two ends' public addresses dropped:
#                      relayed
#   udp-blocked        all UDP from the client dropped but DNS: no
#                      connection, reported plainly and in time (the TCP
#                      floor is plan Task 29)
#   mtu-1100           datagrams over 1100 bytes dropped: connected, a
#                      60000-byte message echoed in 1000-byte datagrams
#   ipv6-only-nat64    an IPv6-only client behind NAT64 and DNS64 with a
#                      CLAT, the Core on IPv4: connected
#   netem-loss         2 % loss and 40 ms delay on both uplinks: connected
#
# Every secret (the TURN secret, the TLS key, both ends' keys) is made at
# run time in a temporary directory and removed with it. It never touches
# the live server: every name is under harness.test and every address is a
# documentation or private one, inside namespaces.
#
# Needs root, and: ip (iproute2), nft, tc, python3 with websockets and
# cryptography, turnserver (coturn), socat, openssl, unbound, tayga.
# Registered as the ctest `traversal_harness` (label `traversal`) when
# configured with -DNEREUS_TRAVERSAL_TESTS=ON; the CI job of that name runs
# it.
#
# Usage: traversal-harness.sh --peer PATH --source DIR [--only SCENARIO]
#
# =================================================================
# Modification history (NereusSDR):
#   2026-09-26: original implementation for NereusSDR by J.J. Boyd
#               (KG4VCF), with AI-assisted implementation via Anthropic
#               Claude Code.
# =================================================================

set -euo pipefail

PEER=""
SOURCE=""
ONLY=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --peer) PEER="$2"; shift 2 ;;
        --source) SOURCE="$2"; shift 2 ;;
        --only) ONLY="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$PEER" || -z "$SOURCE" ]]; then
    echo "usage: $0 --peer PATH --source DIR [--only SCENARIO]" >&2
    exit 2
fi
if [[ "$(uname -s)" != "Linux" ]]; then
    echo "the traversal harness runs on Linux only" >&2
    exit 2
fi
if [[ "$(id -u)" != "0" ]]; then
    echo "the traversal harness needs root (network namespaces)" >&2
    exit 2
fi
for tool in ip nft tc python3 turnserver socat openssl unbound tayga; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "missing: $tool" >&2
        exit 2
    fi
done
python3 -c "import websockets, cryptography" 2>/dev/null || {
    echo "missing: python3 websockets and cryptography" >&2
    exit 2
}

WORK="$(mktemp -d /tmp/nereus-traversal.XXXXXX)"
chmod 700 "$WORK"
NAMESPACES=(rvsrv inet nats natc sta cli plat cli6)
PIDS=()

cleanup() {
    set +e
    for pid in "${PIDS[@]:-}"; do
        [[ -n "$pid" ]] && kill "$pid" 2>/dev/null
    done
    sleep 0.5
    for pid in "${PIDS[@]:-}"; do
        [[ -n "$pid" ]] && kill -9 "$pid" 2>/dev/null
    done
    for ns in "${NAMESPACES[@]}"; do
        ip netns del "h-$ns" 2>/dev/null
        rm -rf "/etc/netns/h-$ns"
    done
    rm -rf "$WORK"
}
trap cleanup EXIT

say() { echo "[traversal] $*"; }
in_ns() { local ns="$1"; shift; ip netns exec "h-$ns" "$@"; }

# ── The small internet ────────────────────────────────────────────────

for ns in "${NAMESPACES[@]}"; do
    ip netns add "h-$ns"
    in_ns "$ns" ip link set lo up
    in_ns "$ns" sysctl -qw net.ipv6.conf.all.disable_ipv6=0
    in_ns "$ns" sysctl -qw net.ipv6.conf.all.accept_dad=0
    in_ns "$ns" sysctl -qw net.ipv6.conf.default.accept_dad=0
done

# link A B IFA IFB: a veth pair between two namespaces.
link() {
    ip link add "$3" netns "h-$1" type veth peer name "$4" netns "h-$2"
    in_ns "$1" ip link set "$3" up
    in_ns "$2" ip link set "$4" up
}

# Public side, IPv4 198.51.100.0/24 in /30s and IPv6 2001:db8::/32.
link rvsrv inet srv0 inet-srv
in_ns rvsrv ip addr add 198.51.100.2/30 dev srv0
in_ns rvsrv ip -6 addr add 2001:db8:1::2/64 dev srv0 nodad
in_ns rvsrv ip -6 addr add 2001:db8:1::53/64 dev srv0 nodad
in_ns inet ip addr add 198.51.100.1/30 dev inet-srv
in_ns inet ip -6 addr add 2001:db8:1::1/64 dev inet-srv nodad
in_ns rvsrv ip route add default via 198.51.100.1
in_ns rvsrv ip -6 route add default via 2001:db8:1::1

link natc inet wan inet-natc
in_ns natc ip addr add 198.51.100.6/30 dev wan
in_ns inet ip addr add 198.51.100.5/30 dev inet-natc
in_ns natc ip route add default via 198.51.100.5

link nats inet wan inet-nats
in_ns nats ip addr add 198.51.100.10/30 dev wan
in_ns inet ip addr add 198.51.100.9/30 dev inet-nats
in_ns nats ip route add default via 198.51.100.9

link plat inet wan inet-plat
in_ns plat ip addr add 198.51.100.14/30 dev wan
in_ns inet ip addr add 198.51.100.13/30 dev inet-plat
in_ns plat ip route add default via 198.51.100.13
in_ns plat ip -6 addr add 2001:db8:4::2/64 dev wan nodad
in_ns inet ip -6 addr add 2001:db8:4::1/64 dev inet-plat nodad
in_ns plat ip -6 route add 2001:db8:1::/64 via 2001:db8:4::1

# Private sides.
link cli natc eth0 lan
in_ns cli ip addr add 10.1.0.2/24 dev eth0
in_ns natc ip addr add 10.1.0.1/24 dev lan
in_ns cli ip route add default via 10.1.0.1

link sta nats eth0 lan
in_ns sta ip addr add 10.2.0.2/24 dev eth0
in_ns nats ip addr add 10.2.0.1/24 dev lan
in_ns sta ip route add default via 10.2.0.1

# The IPv6-only client: IPv6 on its link, IPv4 only through its CLAT.
link cli6 plat eth0 lan
in_ns cli6 ip -6 addr add 2001:db8:5::2/64 dev eth0 nodad
in_ns plat ip -6 addr add 2001:db8:5::1/64 dev lan nodad
in_ns cli6 ip -6 route add default via 2001:db8:5::1
in_ns inet ip -6 route add 2001:db8:5::/64 via 2001:db8:4::2
in_ns rvsrv ip -6 route add 2001:db8:5::/64 via 2001:db8:1::1

for ns in inet natc nats plat; do
    in_ns "$ns" sysctl -qw net.ipv4.ip_forward=1
    in_ns "$ns" sysctl -qw net.ipv6.conf.all.forwarding=1
done

# The NAT routers' own firewall: replies only, as a home router.
nat_router() {
    local ns="$1" mode="$2"
    in_ns "$ns" nft -f - <<EOF
flush ruleset
table ip nat {
    chain post {
        type nat hook postrouting priority 100;
        oifname "wan" masquerade $mode
    }
}
table inet filter {
    chain forward {
        type filter hook forward priority 0; policy drop;
        ct state established,related accept
        iifname "lan" accept
    }
}
EOF
}

# ── DNS: names under harness.test ────────────────────────────────────

# rv (the service), rv4 (IPv4 only) and rv6 (IPv6 only), as on the NereusSDR
# server; the service's own name is IPv4 only here so the IPv6-only client
# reaches it through NAT64.
for ns in sta cli; do
    mkdir -p "/etc/netns/h-$ns"
    cat > "/etc/netns/h-$ns/hosts" <<EOF
127.0.0.1 localhost
198.51.100.2 rv.harness.test
198.51.100.2 rv4.harness.test
2001:db8:1::2 rv6.harness.test
EOF
    echo "nameserver 198.51.100.2" > "/etc/netns/h-$ns/resolv.conf"
done
mkdir -p /etc/netns/h-cli6
printf '::1 localhost\n' > /etc/netns/h-cli6/hosts
echo "nameserver 2001:db8:1::53" > /etc/netns/h-cli6/resolv.conf

cat > "$WORK/unbound.conf" <<EOF
server:
    interface: 198.51.100.2
    interface: 2001:db8:1::53
    access-control: 0.0.0.0/0 allow
    access-control: ::/0 allow
    do-daemonize: no
    username: ""
    chroot: ""
    directory: "$WORK"
    pidfile: ""
    use-syslog: no
    logfile: ""
    module-config: "dns64 iterator"
    dns64-prefix: 64:ff9b::/96
    local-zone: "harness.test." static
    local-data: "rv.harness.test. A 198.51.100.2"
    local-data: "rv4.harness.test. A 198.51.100.2"
    local-data: "rv6.harness.test. AAAA 2001:db8:1::2"
EOF
in_ns rvsrv unbound -d -c "$WORK/unbound.conf" >"$WORK/unbound.log" 2>&1 &
PIDS+=($!)

# ── NAT64 (the carrier's PLAT) and the client's CLAT ─────────────────

# plat: tayga maps 64:ff9b::/96 to IPv4 through its own pool, which
# nftables then masquerades onto its public address.
mkdir -p "$WORK/tayga-plat"
cat > "$WORK/tayga-plat.conf" <<EOF
tun-device nat64
ipv4-addr 192.168.255.1
ipv6-addr 2001:db8:4::64
prefix 64:ff9b::/96
wkpf-strict no
dynamic-pool 192.168.255.0/24
data-dir $WORK/tayga-plat
EOF
in_ns plat tayga -c "$WORK/tayga-plat.conf" --mktun
in_ns plat ip link set nat64 up
in_ns plat ip route add 192.168.255.0/24 dev nat64
in_ns plat ip -6 route add 64:ff9b::/96 dev nat64
in_ns plat tayga -c "$WORK/tayga-plat.conf" -d >"$WORK/tayga-plat.log" 2>&1 &
PIDS+=($!)
in_ns plat nft -f - <<EOF
flush ruleset
table ip nat {
    chain post {
        type nat hook postrouting priority 100;
        oifname "wan" masquerade
    }
}
EOF

# cli6: a CLAT (464XLAT), tayga mapping the host's 192.0.0.2 (RFC 7335) to
# an IPv6 address of its own prefix, routed to it through plat, so IPv4
# sockets work through NAT64 as on a phone.
cat > "$WORK/tayga-clat.conf" <<EOF
tun-device clat
ipv4-addr 192.0.0.1
ipv6-addr 2001:db8:6::1
prefix 64:ff9b::/96
wkpf-strict no
map 192.0.0.2 2001:db8:6::464
EOF
in_ns cli6 sysctl -qw net.ipv6.conf.all.forwarding=1
in_ns cli6 tayga -c "$WORK/tayga-clat.conf" --mktun
in_ns cli6 ip link set clat up
in_ns cli6 ip addr add 192.0.0.2/32 dev clat
in_ns cli6 ip route add default dev clat mtu 1260
in_ns cli6 ip -6 route add 2001:db8:6::/64 dev clat
in_ns plat ip -6 route add 2001:db8:6::/64 via 2001:db8:5::2
in_ns cli6 tayga -c "$WORK/tayga-clat.conf" -d >"$WORK/tayga-clat.log" 2>&1 &
PIDS+=($!)

# ── The service: coturn, the rendezvous behind TLS ──────────────────

python3 -c 'import secrets; print(secrets.token_hex(24))' > "$WORK/turn-secret"
chmod 600 "$WORK/turn-secret"
in_ns rvsrv turnserver -n --no-cli --no-tls --no-dtls --fingerprint \
    --listening-ip=198.51.100.2 --listening-ip=2001:db8:1::2 --listening-port=3478 \
    --relay-ip=198.51.100.2 --relay-ip=2001:db8:1::2 \
    --realm=harness.test --use-auth-secret \
    --static-auth-secret="$(cat "$WORK/turn-secret")" \
    --user-quota=4 --total-quota=64 \
    --allowed-peer-ip=198.51.100.0-198.51.100.255 \
    --allowed-peer-ip=2001:db8::-2001:db8:ffff:ffff:ffff:ffff:ffff:ffff \
    --log-file=stdout >"$WORK/coturn.log" 2>&1 &
PIDS+=($!)

cat > "$WORK/rendezvous.conf" <<EOF
[rendezvous]
listen = 127.0.0.1:8710
stun_urls = stun:rv4.harness.test:3478 stun:rv6.harness.test:3478
turn_urls = turn:rv4.harness.test:3478?transport=udp turn:rv6.harness.test:3478?transport=udp
turn_secret_file = $WORK/turn-secret
log_level = info
EOF
in_ns rvsrv env PYTHONPATH="$SOURCE/rendezvous/server" \
    python3 -m nereus_rendezvous --config "$WORK/rendezvous.conf" >"$WORK/rendezvous.log" 2>&1 &
PIDS+=($!)

# A test certificate authority and a certificate for rv.harness.test, made
# now, trusted only by the two peers (--ca).
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 1 \
    -subj "/CN=NereusSDR traversal harness CA" \
    -keyout "$WORK/ca.key" -out "$WORK/ca.pem" >/dev/null 2>&1
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
    -subj "/CN=rv.harness.test" -addext "subjectAltName=DNS:rv.harness.test" \
    -keyout "$WORK/rv.key" -out "$WORK/rv.csr" >/dev/null 2>&1
printf 'subjectAltName=DNS:rv.harness.test\n' > "$WORK/rv.ext"
openssl x509 -req -in "$WORK/rv.csr" -CA "$WORK/ca.pem" -CAkey "$WORK/ca.key" \
    -CAcreateserial -days 1 -extfile "$WORK/rv.ext" -out "$WORK/rv.pem" >/dev/null 2>&1
cat "$WORK/rv.pem" "$WORK/rv.key" > "$WORK/rv-bundle.pem"
chmod 600 "$WORK"/*.key "$WORK/rv-bundle.pem"
for listen in "OPENSSL-LISTEN:443,bind=198.51.100.2,reuseaddr,fork" \
              "OPENSSL-LISTEN:443,bind=[2001:db8:1::2],pf=ip6,reuseaddr,fork"; do
    in_ns rvsrv socat "$listen,cert=$WORK/rv-bundle.pem,verify=0" TCP:127.0.0.1:8710 \
        >>"$WORK/socat.log" 2>&1 &
    PIDS+=($!)
done
sleep 2

SERVER="wss://rv.harness.test/"

# ── The two ends ─────────────────────────────────────────────────────

CLIENT_KEY="$("$PEER" key --dir "$WORK/client-key")"

start_station() {
    local relay="$1"
    rm -f "$WORK/station-id"
    in_ns sta "$PEER" station --dir "$WORK/station-key" --server "$SERVER" \
        --paired "$CLIENT_KEY" --relay "$relay" --id-file "$WORK/station-id" \
        --ca "$WORK/ca.pem" >"$WORK/station.log" 2>&1 &
    STATION_PID=$!
    PIDS+=("$STATION_PID")
    for _ in $(seq 1 100); do
        [[ -s "$WORK/station-id" ]] && return 0
        sleep 0.2
    done
    say "the Core did not register"
    cat "$WORK/station.log" "$WORK/rendezvous.log" >&2 || true
    return 1
}

stop_station() {
    kill "$STATION_PID" 2>/dev/null || true
    wait "$STATION_PID" 2>/dev/null || true
}

# run_client NS: prints the client's JSON result line.
run_client() {
    local ns="$1"
    in_ns "$ns" "$PEER" client --dir "$WORK/client-key" --server "$SERVER" \
        --station-id "$(cat "$WORK/station-id")" --timeout-ms 90000 \
        --ca "$WORK/ca.pem" 2>>"$WORK/client.log" | tail -n 1 || true
}

field() { python3 -c "import json,sys; print(json.loads(sys.argv[1]).get(sys.argv[2]))" "$1" "$2"; }

FAILED=0
check() {
    local name="$1" result="$2" want_echo="$3" want_relay="$4"
    local echoed relayed
    echoed="$(field "$result" echoed 2>/dev/null || echo None)"
    relayed="$(field "$result" relayed 2>/dev/null || echo None)"
    if [[ "$echoed" != "$want_echo" ]]; then
        say "FAIL $name: echoed=$echoed, expected $want_echo: $result"
        FAILED=1
        return
    fi
    if [[ "$want_relay" != "any" && "$relayed" != "$want_relay" ]]; then
        say "FAIL $name: relayed=$relayed, expected $want_relay: $result"
        FAILED=1
        return
    fi
    say "PASS $name: $result"
}

reset_rules() {
    nat_router natc ""
    nat_router nats ""
    in_ns inet nft flush ruleset
    for ns in natc nats; do
        in_ns "$ns" tc qdisc del dev wan root 2>/dev/null || true
    done
}

scenario() {
    [[ -z "$ONLY" || "$ONLY" == "$1" ]]
}

# eim-nat: Linux masquerade keeps each flow's source port where it can, so
# the mapping is endpoint independent: hole punching works.
if scenario eim-nat; then
    reset_rules
    start_station allow
    check eim-nat "$(run_client cli)" True False
    stop_station
fi

# random-nat-both: every mapping gets a random port at both ends, so the
# server-reflexive ports mean nothing to the far end: the relay carries it.
if scenario random-nat-both; then
    reset_rules
    nat_router natc "fully-random"
    nat_router nats "fully-random"
    start_station allow
    check random-nat-both "$(run_client cli)" True True
    stop_station
fi

# udp-direct-blocked: no UDP between the two NATs' public addresses; UDP to
# the service's host still flows.
if scenario udp-direct-blocked; then
    reset_rules
    in_ns inet nft -f - <<EOF
table inet filter {
    chain forward {
        type filter hook forward priority 0;
        ip saddr 198.51.100.6 ip daddr 198.51.100.10 meta l4proto udp drop
        ip saddr 198.51.100.10 ip daddr 198.51.100.6 meta l4proto udp drop
    }
}
EOF
    start_station allow
    check udp-direct-blocked "$(run_client cli)" True True
    stop_station
fi

# udp-blocked: the client's network passes no UDP but DNS. TURN over UDP
# cannot work; the TCP floor is plan Task 29. The client must say so in
# time, not hang.
if scenario udp-blocked; then
    reset_rules
    in_ns natc nft insert rule inet filter forward iifname "lan" meta l4proto udp udp dport != 53 drop
    start_station allow
    started=$(date +%s)
    result="$(run_client cli)"
    elapsed=$(( $(date +%s) - started ))
    check udp-blocked "$result" False any
    if (( elapsed > 120 )); then
        say "FAIL udp-blocked: the client took ${elapsed}s to give up"
        FAILED=1
    fi
    stop_station
fi

# mtu-1100: datagrams over 1100 bytes dropped on the way (the carrier
# behaviour the pairing design section 9.3 describes); the 60000-byte
# message crosses in 1000-byte datagrams.
if scenario mtu-1100; then
    reset_rules
    in_ns inet nft -f - <<EOF
table inet filter {
    chain forward {
        type filter hook forward priority 0;
        meta l4proto udp meta length > 1100 drop
    }
}
EOF
    start_station allow
    check mtu-1100 "$(run_client cli)" True any
    stop_station
fi

# ipv6-only-nat64: the client has only IPv6 (with DNS64, NAT64 and a CLAT),
# the Core only IPv4 behind NAT.
if scenario ipv6-only-nat64; then
    reset_rules
    start_station allow
    check ipv6-only-nat64 "$(run_client cli6)" True any
    stop_station
fi

# netem-loss: 2 % loss and 40 ms each way on both uplinks.
if scenario netem-loss; then
    reset_rules
    for ns in natc nats; do
        in_ns "$ns" tc qdisc add dev wan root netem loss 2% delay 40ms
    done
    start_station allow
    check netem-loss "$(run_client cli)" True any
    stop_station
fi

if (( FAILED )); then
    say "logs:"
    for log in rendezvous coturn station client; do
        echo "── $log ──" >&2
        tail -n 40 "$WORK/$log.log" >&2 2>/dev/null || true
    done
    exit 1
fi
say "all scenarios passed"
