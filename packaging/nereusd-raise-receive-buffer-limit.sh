#!/bin/sh
# raise-receive-buffer-limit <settings file> [<limit file>]
#
# Applies the Core's network receive buffer limit at once, so an install does
# not wait for the next system start, and says in plain words what it did.
# It only ever raises: a limit already at or above the one in the settings
# file is left as it is.
#
# <settings file> is the sysctl.d file the Core ships
# (/usr/lib/sysctl.d/10-nereusd.conf, from
# packaging/nereusd-receive-buffer.conf). The number is read from it, so one
# file states it. <limit file> is where the kernel keeps the limit in force,
# /proc/sys/net/core/rmem_max unless given; the tests give a temporary file.
#
# It gives way to the system's own settings, as the settings file tells an
# administrator to keep them: when a file of the same name in /etc/sysctl.d
# or /run/sysctl.d replaces the Core's (a link to /dev/null masks it), or when
# a file read after it sets the same limit, the limit in force is left as it
# is. Those checks run when the settings file sits at its installed place,
# <root>/usr/lib/sysctl.d/10-nereusd.conf; root is / on a system and a
# temporary directory in the tests.
#
# Run as root by the Core's installers: the .deb postinst and the Rock and Pi
# stage installers. It exits 0 whenever the settings file is sound, raised or
# not. A limit that cannot be changed now must not fail an install: the
# settings file still applies it at the next system start.
set -eu

conf=${1:?settings file required}
limit_file=${2:-/proc/sys/net/core/rmem_max}
key=net.core.rmem_max

wanted=$(sed -n 's/^[[:space:]]*net\.core\.rmem_max[[:space:]]*=[[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/\1/p' "$conf" | tail -n 1)
if [ -z "$wanted" ]; then
    echo "$conf sets no $key; nothing done" >&2
    exit 2
fi

# The file that sets the limit at the next system start, read as
# systemd-sysctl reads them there (sysctl.d(5)): each name from the first
# directory that has it, every name in byte order, the last setting of a key
# winning. systemd-sysctl does not read /etc/sysctl.conf itself; it counts
# only through a sysctl.d file that links to it, as Debian's
# /etc/sysctl.d/99-sysctl.conf did up to bookworm (trixie has none).
# Prints "replaces <file>" when a file of the Core's name replaces it, or
# "sets <file>" for the file whose setting wins, or nothing for none.
# Names are taken one per line and never as patterns, so a name with a space
# or a wildcard is read as it is. It runs in a subshell, $(...), so the IFS
# and set -f it changes end with it.
sets_limit() {
    grep -Eq '^[[:space:]]*-?net[./]core[./]rmem_max[[:space:]]*=' "$1" 2>/dev/null
}
winning_setting() {
    root=$1
    IFS='
'
    dirs='/etc/sysctl.d
/run/sysctl.d
/usr/local/lib/sysctl.d
/usr/lib/sysctl.d
/lib/sysctl.d'
    names=$(for dir in $dirs; do
                for file in "$root$dir"/*.conf; do
                    if [ -e "$file" ] || [ -L "$file" ]; then
                        echo "${file##*/}"
                    fi
                done
            done | LC_ALL=C sort -u)
    winner=
    set -f
    for name in $names; do
        found=
        for dir in $dirs; do
            if [ -e "$root$dir/$name" ] || [ -L "$root$dir/$name" ]; then
                found=$root$dir/$name
                break
            fi
        done
        if [ "$name" = 10-nereusd.conf ] && [ "$found" != "$root$installed" ]; then
            echo "replaces $found"
            return
        fi
        if sets_limit "$found"; then
            winner=$found
        fi
    done
    if [ -n "$winner" ]; then
        echo "sets $winner"
    fi
}

current=$(cat "$limit_file" 2>/dev/null) || current=
case "$current" in
    ''|*[!0-9]*)
        echo "Network receive buffer limit ($key): the limit in force could not be read. $conf sets it to $wanted bytes at the next system start."
        exit 0
        ;;
esac

installed=/usr/lib/sysctl.d/10-nereusd.conf
case "$conf" in
    *"$installed")
        setting=$(winning_setting "${conf%"$installed"}")
        case "$setting" in
            "replaces "*)
                echo "Network receive buffer limit ($key): ${setting#replaces } replaces the Core's $installed, so the system keeps its own limit. Left unchanged at $current bytes."
                exit 0
                ;;
            "sets $conf") ;;
            "sets "*)
                echo "Network receive buffer limit ($key): ${setting#sets } sets this limit and is read after the Core's $installed, so the system keeps its own limit. Left unchanged at $current bytes."
                exit 0
                ;;
        esac
        ;;
esac

if [ "$current" -ge "$wanted" ]; then
    echo "Network receive buffer limit ($key) is $current bytes, already at least the $wanted the Core asks for. Left unchanged."
    exit 0
fi

if { echo "$wanted" > "$limit_file"; } 2>/dev/null; then
    echo "Raised the network receive buffer limit ($key) from $current to $wanted bytes, the size the Core asks for on the socket its radio sends to, so the system can hold the radio's packets while the Core is briefly held up. $conf keeps it at every system start; removing that file undoes it from the next start."
else
    echo "Network receive buffer limit ($key): it could not be raised from $current to $wanted bytes now. $conf sets it at the next system start."
fi
exit 0
