#!/usr/bin/env bash
#
# setup-server.sh: prepare a server for the NereusSDR rendezvous service and
# its relay (coturn).
#
# Run it as root on the server itself, from a copy of rendezvous/deploy/
# (it reads its sibling files). It is idempotent: every step looks at the
# current state first and skips work that is already done, so it is safe to
# run again, for example after changing turnserver.conf.
#
# Usage:
#   bash setup-server.sh [--dry-run] [--no-start] [--rotate-secret]
#
#   --dry-run        check everything and print what would change; change
#                    nothing
#   --no-start       install and configure, but start, stop or enable no
#                    service (the default on a host not booted with systemd,
#                    such as the containers of the checks in rendezvous/tests/)
#   --rotate-secret  make a new TURN secret (see rendezvous/README.md,
#                    "Rotating the secret")
#
# Settings, as environment variables (defaults are nereussdr.com's):
#   RV_HOST                  the service's host name       rv.nereussdr.com
#   RV_RELAY_HOST4           the IPv4-only relay name      rv4.nereussdr.com
#   RV_RELAY_HOST6           the IPv6-only relay name      rv6.nereussdr.com
#   RV_PUBLIC_IPV4           the server's public IPv4      found on the host
#   RV_PUBLIC_IPV6           the server's public IPv6      found on the host
#   RV_TRANSFER_GB_PER_MONTH the transfer allowance, GB    1000
#   RV_DEPLOY_USER           the account deploy.sh uses    nereusweb
#
# Typical run, from the repository root on the maintainer's Mac (see
# rendezvous/README.md):
#   scp -r rendezvous/deploy root@<server>:/root/rendezvous-deploy
#   ssh root@<server> bash /root/rendezvous-deploy/setup-server.sh --dry-run
#   ssh root@<server> bash /root/rendezvous-deploy/setup-server.sh
#
# Steps:
#   1. Check the host, the inputs and the settings; refuse to go on while
#      any other process holds UDP 3478 or UDP 443 (the relay's ports).
#   2. Install coturn, python3-websockets and python3-cryptography from
#      Ubuntu, without letting the package start coturn on its own.
#   3. Make the TURN secret (root only) once, and coturn's DTLS key pair.
#   4. Write /etc/turnserver.conf: turnserver.conf from this directory plus
#      the lines only this server knows (secret, addresses, sizing).
#   5. Write /etc/nereus-rendezvous/rendezvous.conf.
#   6. Install the service's unit and coturn's drop-in, and make
#      /opt/nereus-rendezvous for deploy.sh.
#   7. Start (or restart) coturn and the service, and check exactly which
#      ports each one holds.
#   8. Print a summary.
#
# It never changes the firewall, Caddy or its configuration, the website,
# the SSH daemon, or any account but the ones named here (it creates none).
# TLS for the service belongs to Caddy (website/deploy/Caddyfile).

set -euo pipefail
umask 022

readonly ETC_DIR="/etc/nereus-rendezvous"
readonly SECRET_FILE="${ETC_DIR}/turn-secret"
readonly COTURN_DIR="${ETC_DIR}/coturn"
readonly DTLS_CERT="${COTURN_DIR}/cert.pem"
readonly DTLS_KEY="${COTURN_DIR}/key.pem"
readonly SERVICE_CONF="${ETC_DIR}/rendezvous.conf"
readonly TURNSERVER_CONF="/etc/turnserver.conf"
readonly CODE_DIR="/opt/nereus-rendezvous"
readonly UNIT_NAME="nereus-rendezvous.service"
readonly UNIT_DEST="/etc/systemd/system/${UNIT_NAME}"
readonly DROPIN_DEST="/etc/systemd/system/coturn.service.d/nereus.conf"
readonly CREDENTIAL_PATH="/run/credentials/${UNIT_NAME}/turn-secret"
readonly SERVICE_PORT=8710
readonly RELAY_PORTS=(3478 443)
readonly PACKAGES=(coturn python3 python3-websockets python3-cryptography)

# The transfer allowance the relay is sized from, in GB (10^9 bytes) a
# month: the one number to change for a server with a different allowance.
readonly DEFAULT_TRANSFER_GB_PER_MONTH=1000
# The relay's share of it; the rest is left for the website, the
# rendezvous service's signalling and the host itself.
readonly RELAY_SHARE_PERCENT=90
# A month, for the sizing: 30 days.
readonly SECONDS_PER_MONTH=2592000

# Keys only this script writes into /etc/turnserver.conf. turnserver.conf
# in the repository must not set any of them.
readonly MANAGED_KEYS=(static-auth-secret listening-ip relay-ip external-ip cert pkey bps-capacity total-quota realm)

export DEBIAN_FRONTEND=noninteractive
# needrestart (part of Ubuntu server) can restart running services after
# apt installs packages. List mode only reports, so no other service on
# this host (Caddy, netbench) is restarted by this script.
export NEEDRESTART_MODE=l

log()  { printf '\n==> %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }
die()  { printf 'setup-server.sh: error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: bash setup-server.sh [--dry-run] [--no-start] [--rotate-secret]

Run as root on the server, from a copy of rendezvous/deploy/. Safe to run
again. Settings come from RV_* environment variables; see the script's
header and rendezvous/README.md.
EOF
}

apt_get() {
    apt-get -o DPkg::Lock::Timeout=300 "$@"
}

is_installed() {
    local status
    # shellcheck disable=SC2016  # ${Status} is a dpkg-query field, not a shell variable
    status="$(dpkg-query -W -f='${Status}' "$1" 2>/dev/null || true)"
    [[ "$status" == "install ok installed" ]]
}

dry_run=0
no_start=0
rotate=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) dry_run=1 ;;
        --no-start) no_start=1 ;;
        --rotate-secret) rotate=1 ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly script_dir
readonly repo_turnserver_conf="${script_dir}/turnserver.conf"
readonly repo_unit="${script_dir}/${UNIT_NAME}"
readonly repo_dropin="${script_dir}/coturn-override.conf"

rv_host="${RV_HOST:-rv.nereussdr.com}"
relay_host4="${RV_RELAY_HOST4:-rv4.nereussdr.com}"
relay_host6="${RV_RELAY_HOST6:-rv6.nereussdr.com}"
transfer_gb="${RV_TRANSFER_GB_PER_MONTH:-$DEFAULT_TRANSFER_GB_PER_MONTH}"
deploy_user="${RV_DEPLOY_USER:-nereusweb}"

# Root's temporary files, removed on exit.
tmp_paths=()
cleanup() {
    local p
    for p in ${tmp_paths[@]+"${tmp_paths[@]}"}; do
        rm -rf -- "$p"
    done
}
trap cleanup EXIT

# Lists every socket bound to one of the given ports (every port when none
# is given), one line each:
# "<proto> <port> <local address> <pid> <program>", for UDP and TCP in both
# families, from /proc (the same on a server and in a container, without
# ss). Run as root it sees every process.
socket_holders() {
    python3 - "$@" <<'PY'
import os, socket, struct, sys
ports = {int(p) for p in sys.argv[1:]}
every = not ports
def addr(hexaddr):
    host, port = hexaddr.split(":")
    raw = bytes.fromhex(host)
    if len(raw) == 4:
        text = socket.inet_ntop(socket.AF_INET, raw[::-1])
    else:
        text = socket.inet_ntop(socket.AF_INET6, b"".join(raw[i:i + 4][::-1] for i in range(0, 16, 4)))
    return text, int(port, 16)
owners = {}
for pid in filter(str.isdigit, os.listdir("/proc")):
    try:
        comm = open("/proc/%s/comm" % pid).read().strip()
        for fd in os.listdir("/proc/%s/fd" % pid):
            try:
                link = os.readlink("/proc/%s/fd/%s" % (pid, fd))
            except OSError:
                continue
            if link.startswith("socket:["):
                owners.setdefault(link[8:-1], (pid, comm))
    except OSError:
        continue
for proto in ("udp", "udp6", "tcp", "tcp6"):
    try:
        lines = open("/proc/net/%s" % proto).read().splitlines()[1:]
    except OSError:
        continue
    for line in lines:
        fields = line.split()
        host, port = addr(fields[1])
        state, inode = fields[3], fields[9]
        # TCP sockets count only when listening (state 0A).
        if proto.startswith("tcp") and state != "0A":
            continue
        if every or port in ports:
            pid, comm = owners.get(inode, ("?", "?"))
            print(proto.rstrip("6"), port, host, pid, comm)
PY
}

# The source address this host would use to reach the internet in one
# family, found without sending anything (a UDP connect sends no packet).
detect_address() {
    python3 - "$1" <<'PY'
import socket, sys
family, target = (socket.AF_INET, "192.0.2.1") if sys.argv[1] == "4" else (socket.AF_INET6, "2001:db8::1")
try:
    s = socket.socket(family, socket.SOCK_DGRAM)
    s.connect((target, 9))
    print(s.getsockname()[0])
except OSError:
    pass
PY
}

# True when $1 is a public unicast address of family $2 (4 or 6).
is_public_address() {
    python3 - "$1" "$2" <<'PY'
import ipaddress, sys
try:
    a = ipaddress.ip_address(sys.argv[1])
except ValueError:
    sys.exit(1)
ok = a.version == int(sys.argv[2]) and not (a.is_private or a.is_loopback or a.is_link_local or a.is_multicast or a.is_reserved or a.is_unspecified)
# The Docker checks use documentation ranges as stand-ins for public ones.
if not ok and "RV_ALLOW_DOCUMENTATION_ADDRESSES" in __import__("os").environ:
    docs = ("192.0.2.0/24", "198.51.100.0/24", "203.0.113.0/24", "2001:db8::/32")
    ok = a.version == int(sys.argv[2]) and any(a in ipaddress.ip_network(n) for n in docs)
sys.exit(0 if ok else 1)
PY
}

# Installs $1 as $2 with mode $3 and owner $4 when it differs, keeping a
# timestamped copy of what was there. Prints "same" or "changed". In a dry
# run it only says what it would do.
install_file() {
    local src="$1" dest="$2" mode="$3" owner="$4"
    if [[ -L "$dest" ]]; then
        die "${dest} is a symlink; refusing to follow it as root"
    fi
    if [[ -f "$dest" ]] && cmp -s "$src" "$dest"; then
        if ! (( dry_run )); then
            chmod "$mode" "$dest"
            chown "$owner" "$dest"
        fi
        echo same
        return
    fi
    if (( dry_run )); then
        echo changed
        return
    fi
    if [[ -e "$dest" ]]; then
        cp -p -- "$dest" "${dest}.bak-$(date +%Y%m%d-%H%M%S)"
    fi
    local staged
    staged="$(mktemp "$(dirname "$dest")/.$(basename "$dest").XXXXXX")"
    cp -- "$src" "$staged"
    chmod "$mode" "$staged"
    chown "$owner" "$staged"
    mv -f -- "$staged" "$dest"
    echo changed
}

# ---------------------------------------------------------------------------
log "1/8 Checking the host, the inputs and the settings"

if [[ "$(id -u)" -ne 0 ]]; then
    die "run this as root"
fi
# shellcheck source=/dev/null  # /etc/os-release exists only on the server
os_id="$(. /etc/os-release 2>/dev/null && printf '%s' "${ID:-}")" || true
if [[ "$os_id" != "ubuntu" ]]; then
    die "this script supports Ubuntu only (found: ${os_id:-unknown})"
fi
command -v python3 >/dev/null 2>&1 || die "python3 is not installed (it is on every Ubuntu server image)"
for f in "$repo_turnserver_conf" "$repo_unit" "$repo_dropin"; do
    [[ -f "$f" && -r "$f" ]] || die "missing ${f}; run this from a whole copy of rendezvous/deploy/"
done
(( dry_run )) && note "dry run: nothing is changed"
if ! (( no_start )) && [[ ! -d /run/systemd/system ]]; then
    # Not booted with systemd (a container): nothing can be started here.
    note "systemd is not running on this host: services are installed but not started (as with --no-start)"
    no_start=1
fi

for key in "${MANAGED_KEYS[@]}"; do
    if grep -Eq "^[[:space:]]*-{0,2}${key}([[:space:]]*=|[[:space:]]*$)" "$repo_turnserver_conf"; then
        die "${repo_turnserver_conf} sets ${key}, which only this script may write"
    fi
done
for name in "$rv_host" "$relay_host4" "$relay_host6"; do
    [[ "$name" =~ ^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$ ]] || die "not a host name: ${name}"
done
[[ "$transfer_gb" =~ ^[1-9][0-9]*$ ]] || die "RV_TRANSFER_GB_PER_MONTH must be a whole number of GB (got ${transfer_gb})"
id -u "$deploy_user" >/dev/null 2>&1 \
    || die "the deploy account ${deploy_user} does not exist (website/deploy/setup-server.sh makes nereusweb; see rendezvous/README.md)"

public4="${RV_PUBLIC_IPV4:-$(detect_address 4)}"
public6="${RV_PUBLIC_IPV6:-$(detect_address 6)}"
[[ -n "$public4" ]] || die "no public IPv4 address found; set RV_PUBLIC_IPV4"
is_public_address "$public4" 4 || die "${public4} is not a public IPv4 address; set RV_PUBLIC_IPV4"
[[ -n "$public6" ]] || die "no public IPv6 address found; set RV_PUBLIC_IPV6 (the relay needs both families)"
is_public_address "$public6" 6 || die "${public6} is not a public IPv6 address; set RV_PUBLIC_IPV6"
note "public addresses: IPv4 ${public4}, IPv6 ${public6} (they stay on this server)"

# Sizing (rendezvous/README.md, "Limits"): coturn caps the bytes a second it
# sends in total at bps-capacity, so a month at that rate can never pass the
# relay's share of the allowance. Each allocation reserves max-bps of it,
# so bps-capacity / max-bps allocations run at once.
max_bps="$(sed -n 's/^max-bps=\([0-9][0-9]*\)$/\1/p' "$repo_turnserver_conf")"
[[ -n "$max_bps" ]] || die "${repo_turnserver_conf} has no max-bps line"
bps_capacity=$(( transfer_gb * 1000000000 * RELAY_SHARE_PERCENT / 100 / SECONDS_PER_MONTH ))
total_quota=$(( bps_capacity / max_bps ))
(( total_quota >= 1 )) || die "the allowance is too small for even one allocation at max-bps ${max_bps}"
note "allowance ${transfer_gb} GB a month, ${RELAY_SHARE_PERCENT}% for the relay: bps-capacity ${bps_capacity} bytes/s, total-quota ${total_quota} allocations of ${max_bps} bytes/s"

# The relay's ports must be free, or already coturn's.
holders="$(socket_holders "${RELAY_PORTS[@]}" | awk '$5 != "turnserver"' || true)"
if [[ -n "$holders" ]]; then
    printf '%s\n' "$holders" >&2
    if (( no_start )); then
        note "another process holds a relay port (above); coturn is not started (--no-start)"
    else
        die "another process holds UDP 3478 or 443 (above); stop it first (on nereussdr.com: the netbench service, with JJ's agreement). Nothing was changed."
    fi
else
    note "UDP 3478 and 443 are free or held by coturn"
fi

# ---------------------------------------------------------------------------
log "2/8 Packages: ${PACKAGES[*]}"

missing=()
for pkg in "${PACKAGES[@]}"; do
    is_installed "$pkg" || missing+=("$pkg")
done
if [[ ${#missing[@]} -eq 0 ]]; then
    note "already installed"
elif (( dry_run )); then
    note "would install: ${missing[*]}"
else
    note "installing: ${missing[*]}"
    # coturn's package starts coturn with its stock configuration, which
    # allows anyone to relay. A policy-rc.d that answers 101 keeps any
    # service from being started during this install; it is removed after,
    # and one that was already there is left alone.
    policy="/usr/sbin/policy-rc.d"
    added_policy=0
    if [[ ! -e "$policy" ]]; then
        printf '#!/bin/sh\nexit 101\n' > "$policy"
        chmod 755 "$policy"
        added_policy=1
    fi
    apt_get update
    install_status=0
    apt_get install -y --no-install-recommends \
        -o Dpkg::Options::=--force-confdef \
        -o Dpkg::Options::=--force-confold \
        "${missing[@]}" || install_status=$?
    if (( added_policy )); then
        rm -f -- "$policy"
    fi
    (( install_status == 0 )) || die "apt-get install failed"
    if ! (( no_start )) && systemctl is-active --quiet coturn; then
        # Only if something started it anyway: never leave the stock
        # configuration serving.
        systemctl stop coturn
    fi
fi
if ! (( dry_run )); then
    command -v turnserver >/dev/null 2>&1 || die "turnserver is not on PATH after installation"
    id -u turnserver >/dev/null 2>&1 || die "the turnserver account is missing after installation"
fi

# ---------------------------------------------------------------------------
log "3/8 TURN secret and coturn's DTLS key pair"

if (( dry_run )); then
    if [[ -s "$SECRET_FILE" ]]; then note "secret present"; else note "would make ${SECRET_FILE}"; fi
    if [[ -s "$DTLS_CERT" && -s "$DTLS_KEY" ]]; then
        note "key pair present"
    else
        note "would make ${DTLS_CERT} and ${DTLS_KEY}"
    fi
else
    install -d -m 0755 -o root -g root "$ETC_DIR"
    install -d -m 0750 -o root -g turnserver "$COTURN_DIR"
    if [[ -s "$SECRET_FILE" && "$rotate" -eq 0 ]]; then
        note "secret present; left as it is"
    else
        # 32 random bytes as 64 hex characters: printable, so coturn's
        # configuration and the service read the same bytes (rendezvous
        # document section 8).
        staged="$(mktemp "${ETC_DIR}/.turn-secret.XXXXXX")"
        tmp_paths+=("$staged")
        chmod 600 "$staged"
        python3 -c 'import secrets; print(secrets.token_hex(32))' > "$staged"
        mv -f -- "$staged" "$SECRET_FILE"
        note "made a new secret in ${SECRET_FILE} (root only)"
    fi
    chown root:root "$SECRET_FILE"
    chmod 600 "$SECRET_FILE"
    if [[ -s "$DTLS_CERT" && -s "$DTLS_KEY" ]]; then
        note "key pair present; left as it is"
    else
        # Self-signed, for coturn's second UDP port only (turnserver.conf,
        # "Ports"): its cipher list is empty, so no DTLS session is ever made
        # and nothing ever checks the certificate.
        python3 - "$DTLS_CERT" "$DTLS_KEY" "$rv_host" <<'PY'
import datetime, os, sys
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
cert_path, key_path, host = sys.argv[1:4]
key = ec.generate_private_key(ec.SECP256R1())
name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)])
now = datetime.datetime.now(datetime.timezone.utc)
cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
        .public_key(key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=3650))
        .sign(key, hashes.SHA256()))
old = os.umask(0o027)
with open(key_path + ".new", "wb") as f:
    f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
with open(cert_path + ".new", "wb") as f:
    f.write(cert.public_bytes(serialization.Encoding.PEM))
os.umask(old)
os.replace(key_path + ".new", key_path)
os.replace(cert_path + ".new", cert_path)
PY
        note "made ${DTLS_CERT} and ${DTLS_KEY}"
    fi
    chown root:turnserver "$DTLS_CERT" "$DTLS_KEY"
    chmod 640 "$DTLS_CERT" "$DTLS_KEY"
fi

# ---------------------------------------------------------------------------
log "4/8 ${TURNSERVER_CONF}"

work_dir="$(mktemp -d)"
tmp_paths+=("$work_dir")
chmod 700 "$work_dir"
secret_line="static-auth-secret=(made in step 3)"
if [[ -s "$SECRET_FILE" ]]; then
    secret_line="static-auth-secret=$(cat "$SECRET_FILE")"
fi
{
    cat "$repo_turnserver_conf"
    printf '\n# ---------------------------------------------------------------- This server\n'
    printf '# Written by rendezvous/deploy/setup-server.sh; never copy these lines\n'
    printf '# into the repository.\n'
    printf 'realm=%s\n' "$rv_host"
    printf 'listening-ip=%s\n' "$public4" "$public6"
    printf 'relay-ip=%s\n' "$public4" "$public6"
    printf 'cert=%s\n' "$DTLS_CERT"
    printf 'pkey=%s\n' "$DTLS_KEY"
    printf '# %s GB a month, %s%% of it for the relay, over %s s.\n' "$transfer_gb" "$RELAY_SHARE_PERCENT" "$SECONDS_PER_MONTH"
    printf 'bps-capacity=%s\n' "$bps_capacity"
    printf 'total-quota=%s\n' "$total_quota"
    printf '%s\n' "$secret_line"
} > "${work_dir}/turnserver.conf"
if (( dry_run )); then
    note "${TURNSERVER_CONF} would be: $(install_file "${work_dir}/turnserver.conf" "$TURNSERVER_CONF" 640 root:turnserver)"
else
    note "${TURNSERVER_CONF}: $(install_file "${work_dir}/turnserver.conf" "$TURNSERVER_CONF" 640 root:turnserver)"
fi

# ---------------------------------------------------------------------------
log "5/8 ${SERVICE_CONF}"

{
    printf '# The NereusSDR rendezvous service on this server. Written by\n'
    printf '# rendezvous/deploy/setup-server.sh; every key not here has the\n'
    printf "# default in rendezvous/server/rendezvous.conf.sample.\n\n"
    printf '[rendezvous]\n'
    printf 'listen = 127.0.0.1:%s [::1]:%s\n' "$SERVICE_PORT" "$SERVICE_PORT"
    printf 'trusted_proxies = 127.0.0.1 ::1\n'
    printf 'stun_urls = stun:%s:3478 stun:%s:3478\n' "$relay_host6" "$relay_host4"
    printf 'turn_urls ='
    for name in "$relay_host6" "$relay_host4"; do
        for port in "${RELAY_PORTS[@]}"; do
            printf ' turn:%s:%s?transport=udp' "$name" "$port"
        done
    done
    printf '\n'
    printf '# systemd hands the secret over here (LoadCredential=).\n'
    printf 'turn_secret_file = %s\n' "$CREDENTIAL_PATH"
} > "${work_dir}/rendezvous.conf"
if [[ -f "${CODE_DIR}/nereus_rendezvous/config.py" ]]; then
    # Check it with the service's own reader, the secret taken from where
    # it is now rather than where systemd will put it.
    sed "s|^turn_secret_file = .*|turn_secret_file = ${SECRET_FILE}|" "${work_dir}/rendezvous.conf" > "${work_dir}/check.conf"
    if [[ -s "$SECRET_FILE" ]] && ! PYTHONPATH="$CODE_DIR" PYTHONDONTWRITEBYTECODE=1 python3 -c \
            'import sys; from nereus_rendezvous import config; config.load(sys.argv[1])' "${work_dir}/check.conf"; then
        die "the service refuses the configuration above; ${SERVICE_CONF} was left unchanged"
    fi
fi
if (( dry_run )); then
    note "${SERVICE_CONF} would be: $(install_file "${work_dir}/rendezvous.conf" "$SERVICE_CONF" 644 root:root)"
else
    note "${SERVICE_CONF}: $(install_file "${work_dir}/rendezvous.conf" "$SERVICE_CONF" 644 root:root)"
fi

# ---------------------------------------------------------------------------
log "6/8 Units and ${CODE_DIR}"

if (( dry_run )); then
    note "unit would be: $(install_file "$repo_unit" "$UNIT_DEST" 644 root:root)"
    note "coturn drop-in would be: $(install_file "$repo_dropin" "$DROPIN_DEST" 644 root:root)"
    if [[ -d "$CODE_DIR" ]]; then note "${CODE_DIR} present"; else note "would make ${CODE_DIR} for ${deploy_user}"; fi
else
    note "unit: $(install_file "$repo_unit" "$UNIT_DEST" 644 root:root)"
    install -d -m 0755 -o root -g root "$(dirname "$DROPIN_DEST")"
    note "coturn drop-in: $(install_file "$repo_dropin" "$DROPIN_DEST" 644 root:root)"
    if [[ -L "$CODE_DIR" ]]; then
        die "${CODE_DIR} is a symlink; refusing to follow it as root"
    fi
    install -d -m 0755 "$CODE_DIR"
    chown -h "${deploy_user}:${deploy_user}" "$CODE_DIR"
    note "${CODE_DIR} belongs to ${deploy_user} (deploy.sh writes it)"
fi

# ---------------------------------------------------------------------------
log "7/8 Services"

if (( dry_run )); then
    note "dry run: no service is started"
elif (( no_start )); then
    note "--no-start: no service is started, stopped or enabled"
else
    systemctl daemon-reload
    systemctl enable coturn >/dev/null 2>&1
    systemctl restart coturn || die "coturn did not start; see: journalctl -u coturn -n 50 --no-pager"
    sleep 1
    systemctl is-active --quiet coturn || die "coturn is not active; see: journalctl -u coturn -n 50 --no-pager"
    note "coturn is active"
    systemctl enable "$UNIT_NAME" >/dev/null 2>&1
    if [[ -f "${CODE_DIR}/nereus_rendezvous/__main__.py" ]]; then
        systemctl restart "$UNIT_NAME" || die "the service did not start; see: journalctl -u ${UNIT_NAME} -n 50 --no-pager"
        sleep 1
        systemctl is-active --quiet "$UNIT_NAME" || die "the service is not active; see: journalctl -u ${UNIT_NAME} -n 50 --no-pager"
        note "${UNIT_NAME} is active"
    else
        note "${CODE_DIR} holds no code yet: run rendezvous/deploy.sh, then: systemctl restart ${UNIT_NAME}"
    fi
fi

if ! (( dry_run || no_start )); then
    # coturn: UDP 3478 and 443 on the two public addresses, and no TCP port.
    # The service: TCP 8710 on loopback only.
    held="$(socket_holders 3478 443 "$SERVICE_PORT")"
    note "sockets on UDP 3478, UDP 443 and TCP ${SERVICE_PORT}:"
    printf '%s\n' "$held" | sed 's/^/      /'
    if [[ -n "$(socket_holders | awk '$1 == "tcp" && $5 == "turnserver"')" ]]; then
        die "coturn holds a TCP port; it must use UDP only"
    fi
    for port in "${RELAY_PORTS[@]}"; do
        for addr in "$public4" "$public6"; do
            [[ -n "$(awk -v p="$port" -v a="$addr" '$1 == "udp" && $2 == p && $3 == a && $5 == "turnserver"' <<<"$held")" ]] \
                || die "coturn does not hold UDP ${port} on ${addr}"
        done
    done
    if [[ -n "$(awk -v p="$SERVICE_PORT" '$1 == "tcp" && $2 == p && $3 != "127.0.0.1" && $3 != "::1"' <<<"$held")" ]]; then
        die "something listens on TCP ${SERVICE_PORT} beyond loopback"
    fi
    note "coturn holds UDP 3478 and 443 on both addresses and no TCP port"
fi

# ---------------------------------------------------------------------------
log "8/8 Summary"

if ! (( dry_run )); then
    note "coturn:        $(dpkg-query -W -f='${Version}' coturn 2>/dev/null || echo unknown)"
    note "websockets:    $(dpkg-query -W -f='${Version}' python3-websockets 2>/dev/null || echo unknown)"
fi
note "service name:  ${rv_host} (Caddy forwards it to 127.0.0.1:${SERVICE_PORT} and [::1]:${SERVICE_PORT})"
note "relay names:   ${relay_host4} (A ${public4}), ${relay_host6} (AAAA ${public6})"
note "relay ports:   UDP 3478 and 443; relays on UDP 61000 to 65535"
note "secret:        ${SECRET_FILE} (root only; never printed)"
note ""
note "Caddy's site for ${rv_host} comes from website/deploy/Caddyfile, installed by"
note "website/deploy/setup-server.sh; this script leaves Caddy alone."
