#!/usr/bin/env bash
#
# deploy.sh: publish the rendezvous service's code to its server with rsync.
#
# Usage:
#   rendezvous/deploy.sh             publish the code
#   rendezvous/deploy.sh --dry-run   show what would change, change nothing
#
# Works from any directory: paths are resolved from this script's location.
#
# The destination is $NEREUS_RV_TARGET, by default
# nereus-web:/opt/nereus-rendezvous/ (nereus-web is the SSH alias from
# website/README.md; /opt/nereus-rendezvous is made for that account by
# rendezvous/deploy/setup-server.sh, and the service's unit runs the code
# from there). Point NEREUS_RV_TARGET at a local directory to try it
# without the server.
#
# It publishes rendezvous/server/nereus_rendezvous/ and the sample
# configuration, nothing else: no tests, no secret, no configuration of the
# server's own (setup-server.sh writes that). rsync runs with --delete, so
# the destination ends up with exactly those files. It never runs
# setup-server.sh and never restarts anything: the deploy account has no
# root. After a publish, restart the service as root (the script prints the
# command).
#
# As in website/deploy.sh, the rsync options are limited to ones that both
# GNU rsync 3.x and macOS's openrsync support, and the files are sent from a
# world-readable staging copy, so the service's account can read them.

set -euo pipefail

readonly default_target="nereus-web:/opt/nereus-rendezvous/"
readonly required=(__init__.py __main__.py clock.py config.py identity.py limits.py protocol.py service.py transport.py turn.py)

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly script_dir
readonly src_dir="${script_dir}/server"
readonly target="${NEREUS_RV_TARGET:-$default_target}"

die() { printf 'deploy.sh: error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: rendezvous/deploy.sh [--dry-run]

Publishes rendezvous/server/ (the service's code and sample configuration)
with rsync to $NEREUS_RV_TARGET (default: nereus-web:/opt/nereus-rendezvous/).

  --dry-run   show what would change on the target; change nothing
EOF
}

dry_run=0
if [[ $# -gt 0 ]]; then
    case "$1" in
        --dry-run) dry_run=1; shift ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
fi
if [[ $# -gt 0 ]]; then
    usage >&2
    exit 2
fi

command -v rsync >/dev/null 2>&1 || die "rsync not found"
command -v python3 >/dev/null 2>&1 || die "python3 not found"

# Safety checks: rsync runs with --delete, so a half-built source would
# leave the server with a service that cannot start.
[[ -d "${src_dir}/nereus_rendezvous" ]] || die "source directory not found: ${src_dir}/nereus_rendezvous"
for name in "${required[@]}"; do
    [[ -f "${src_dir}/nereus_rendezvous/${name}" ]] || die "missing ${src_dir}/nereus_rendezvous/${name}; refusing to deploy"
done
[[ -f "${src_dir}/rendezvous.conf.sample" ]] || die "missing ${src_dir}/rendezvous.conf.sample; refusing to deploy"
# Every module must at least parse (no bytecode is written).
python3 - "${src_dir}/nereus_rendezvous" <<'PY' || die "a module does not parse; refusing to deploy"
import ast, pathlib, sys
for path in sorted(pathlib.Path(sys.argv[1]).glob("*.py")):
    ast.parse(path.read_text(encoding="utf-8"), str(path))
PY

staging="$(mktemp -d "${TMPDIR:-/tmp}/nereus-rv-deploy.XXXXXX")"
trap 'rm -rf -- "$staging"' EXIT
mkdir -p "${staging}/nereus_rendezvous"
rsync -r -t --exclude='__pycache__' --exclude='*.pyc' --exclude='.DS_Store' \
    "${src_dir}/nereus_rendezvous/" "${staging}/nereus_rendezvous/"
cp -p "${src_dir}/rendezvous.conf.sample" "${staging}/"
chmod -R a+rX "$staging"
file_count="$(find "$staging" -type f | wc -l)"
file_count="${file_count//[[:space:]]/}"

rsync_args=(-r -l -t -z -v --delete)
if (( dry_run )); then
    rsync_args+=(--dry-run)
fi

echo "Source: ${src_dir}/ (${file_count} files)"
echo "Target: ${target}"
if (( dry_run )); then
    echo "Mode:   dry run, nothing is changed"
fi
echo

if ! rsync "${rsync_args[@]}" "${staging}/" "$target"; then
    die "rsync failed (for the default target, check the nereus-web entry in ~/.ssh/config, see website/README.md)"
fi

if (( dry_run )); then
    echo
    echo "Dry run only. Run rendezvous/deploy.sh without --dry-run to publish."
    exit 0
fi

echo
echo "Published. The service runs the new code once restarted, as root on the server:"
echo "  systemctl restart nereus-rendezvous"
echo "(Registered Cores reconnect on their own; a pairing in progress starts again.)"
