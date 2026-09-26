# NereusSDR rendezvous

The rendezvous lets NereusSDR (the desktop's remote window and the iPhone app)
reach a Core it cannot address directly, and carries pairing when the two are
not on one network. It has two parts, and a server running it has both:

- **The service** (`server/`): a small Python program that introduces clients
  to Cores over a WebSocket at `wss://rv.<your domain>/`, hands out relay
  credentials, and holds pairing mailboxes. It keeps everything in memory.
  Its wire is [the rendezvous document](../docs/architecture/2026-09-23-rendezvous-v1.md).
  Caddy sits in front of it for TLS.
- **The relay**: coturn, Ubuntu's own package, on UDP 3478 and UDP 443. It
  carries a session only when the two ends cannot reach each other directly.

NereusSDR uses `rv.nereussdr.com`, on a server of its own. Anyone can run
their own; this file is the whole recipe. Every default in the scripts is
nereussdr.com's, and each one is a setting.

- `server/`: the service. `rendezvous.conf.sample` lists every setting.
- `deploy/`: `setup-server.sh` (prepares a server; safe to run again),
  `Caddyfile` (Caddy's configuration for the service), `turnserver.conf`
  (coturn's configuration), `nereus-rendezvous.service` (the service's
  systemd unit), `coturn-override.conf` and `caddy-override.conf` (drop-ins
  for the packaged coturn and Caddy units), and the data-use report
  (`data-use.py`, `nereus-data-use.service` and `nereus-data-use.timer`;
  see "Data use").
- `deploy.sh`: publishes the service's code to the server.
- `conformance/`: the vectors the service, the Core and the app all run.
- `tests/`: the service's tests (`python3 -m pytest rendezvous/tests -q`) and
  the Docker checks (below).

## What runs where

| Program | Listens on | Reached from |
| --- | --- | --- |
| Caddy (TLS, the WebSocket's front) | TCP 80 and 443 | the internet |
| The service | TCP 8710 on 127.0.0.1 and ::1 | Caddy only |
| coturn (STUN and TURN) | UDP 3478 and 443, on the public IPv4 and IPv6 | the internet |
| coturn's relays | UDP 61000 to 65535, same addresses | the internet |

coturn uses no TCP port at all, and Caddy none of UDP: Caddy's HTTP/3 (which
would take UDP 443) is switched off. The relay range sits above Linux's
ephemeral ports (32768 to 60999), so a relay never collides with another
program's outgoing connection.

## Self-hosting

You need a server of its own for the rendezvous: Ubuntu 24.04 with a public
IPv4 and a public IPv6 address on its network interface (a typical VPS;
behind a provider's NAT, coturn would also need its `external-ip` setting,
which this recipe does not cover), a domain whose DNS you control, SSH to
the server as root, and `rsync` on your own computer (macOS has it). For up
to 2000 Cores, 2 GB of memory is recommended; 1 GB carries them in normal
use, and loses the service for a few seconds at a time if someone
deliberately fills its connections (the rendezvous document, section 9.1).
The commands below use `example.org`; use your own names.

### 1. DNS

| Type | Name | Value | Why |
| --- | --- | --- | --- |
| A | `rv.example.org` | the server's IPv4 | the service, over IPv4 |
| AAAA | `rv.example.org` | the server's IPv6 | the service, over IPv6 |
| A | `rv4.example.org` | the server's IPv4 | the relay, IPv4 only |
| AAAA | `rv6.example.org` | the server's IPv6 | the relay, IPv6 only |

`rv4` has no AAAA record and `rv6` no A record, on purpose. The relay needs
both address families (a client on an IPv6-only mobile network needs an
IPv6 relay, and one on an old IPv4-only network an IPv4 one), and the ICE
library in NereusSDR looks up one address for each relay name, preferring
IPv4. So each family gets a name of its own, and the service hands out
both, IPv4 first. The service's own name has both records.

Caddy obtains the certificate for `rv.example.org` from Let's Encrypt on its
own once the A and AAAA records point at the server and TCP 80 and 443 are
open, so set DNS up first. The relay names need no certificate.

### 2. Firewall

If the server has a firewall, open these, **SSH first**, so you do not lock
yourself out:

```sh
ufw allow OpenSSH
ufw allow 80/tcp
ufw allow 443/tcp
ufw allow 3478/udp
ufw allow 443/udp
ufw allow 61000:65535/udp
ufw enable
```

(With a cloud provider's firewall instead, open the same ports there, for
both IPv4 and IPv6.) Nothing else needs to be reachable: the service listens
on loopback only. `setup-server.sh` never changes the firewall.

### fail2ban for SSH

fail2ban for SSH is recommended on the server. It is optional: `setup-server.sh`
does not install it.

```sh
apt install fail2ban
```

Ubuntu's package enables the `sshd` jail with the systemd backend on install.

**The Ubuntu 24.04 trap:** sshd runs as `ssh.service`, but fail2ban 1.0.2's
`sshd` filter matches `_SYSTEMD_UNIT=sshd.service`, so the stock jail never
sees a failed login. The fix, observed on the live server on 2026-09-26:
create `/etc/fail2ban/jail.d/nereus-sshd.local` with exactly

```
# Ubuntu 24.04 runs sshd as ssh.service; the stock filter matches sshd.service and would see nothing.
[sshd]
enabled = true
journalmatch = _SYSTEMD_UNIT=ssh.service + _COMM=sshd
```

then `fail2ban-client reload`, and check with `fail2ban-client get sshd
journalmatch` and `fail2ban-client status sshd` (a failed login from another
machine shows as "Currently failed: 1"). Defaults are 5 failures, a 10 minute
ban. With password login off (key only), this mostly quiets the log and
slows scanners.

Do not point fail2ban at the rendezvous service or coturn. Cellular carriers
put many phones behind one shared IPv4 address, so a ban would cut off every
user on it; the service's log leaves out addresses on purpose, so there is
nothing to match; and the misuse that matters there (too many connections,
introductions or relay slots) is already limited by the service and coturn
per network, which "Limits" below describes.

### 3. Copy the files to the server

From a checkout of NereusSDR on your own computer:

<!-- check: copy -->
```sh
ssh root@rv.example.org rm -rf /root/rendezvous
scp -r rendezvous root@rv.example.org:/root/rendezvous
```

(The `rm` first: copying onto an earlier copy would put the new one inside
it, as `/root/rendezvous/rendezvous`, and leave the old files in use.)

### 4. Settings

On the server, as root, name your hosts, how many relays may run at once,
your server's monthly transfer allowance in GB (see "Limits" and "Data use"
below), and the SSH public key of the account `deploy.sh` will publish the
code as (`setup-server.sh` makes that account, `nereusrv`, with exactly
this key):

<!-- check: settings -->
```sh
export RV_HOST=rv.example.org
export RV_RELAY_HOST4=rv4.example.org
export RV_RELAY_HOST6=rv6.example.org
export RV_RELAY_SLOTS=64
export RV_TRANSFER_GB_PER_MONTH=1000
export RV_DEPLOY_KEY='ssh-ed25519 AAAA...your key... you@computer'
```

`setup-server.sh` finds the rest itself; set `RV_PUBLIC_IPV4` and
`RV_PUBLIC_IPV6` if it picks the wrong public addresses,
`RV_DATA_USE_INTERFACE` to count data use on another interface than the
default route's, and `RV_MEMORY_MB` to size the memory limits for another
amount than the server reports. Every value that belongs to one server is
one of these settings, so the same files move to another server unchanged.

### 5. Set up the server

As root, check first, then set up:

<!-- check: server -->
```sh
apt-get update && apt-get install -y python3   # already there on Ubuntu server images
bash /root/rendezvous/deploy/setup-server.sh --dry-run
bash /root/rendezvous/deploy/setup-server.sh
```

`setup-server.sh`:

- installs Caddy from Caddy's own apt repository, and coturn,
  `python3-websockets`, `python3-cryptography` and `rsync` from Ubuntu,
  without letting any package start a service; coturn stays disabled until
  its configuration is in place, also when the install fails;
- makes the deploy account with exactly the given key, the TURN secret
  (root only, in `/etc/nereus-rendezvous/turn-secret`), and writes
  `/etc/turnserver.conf`, `/etc/nereus-rendezvous/rendezvous.conf` and
  `/etc/caddy/Caddyfile` (the `Caddyfile` here with your `RV_HOST`, after
  `caddy validate`);
- installs the service's unit and the drop-ins for coturn and Caddy, with
  memory limits worked out from the server's memory (see "Memory"), and the
  data-use report;
- enables and starts Caddy, coturn and the report, and checks that coturn
  holds exactly UDP 3478 and 443 and no TCP port, and Caddy no UDP port.

Run again, it reloads Caddy after a Caddyfile change (open connections are
kept) and restarts a service only when one of its files changed (a coturn
restart drops every relay in use); the dry run says which it would do. It
refuses to go on while another program holds UDP 3478 or 443.

### 6. The service's code

From your own computer, publish the code as the deploy account. With an
entry for it in your `~/.ssh/config` (for example `Host nereus-rv`, with
`HostName rv.example.org`, `User nereusrv` and your key), `deploy.sh`'s
default target works as it is; without one, name the target:

<!-- check: deploy -->
```sh
NEREUS_RV_TARGET=nereusrv@rv.example.org:/opt/nereus-rendezvous/ rendezvous/deploy.sh --dry-run
NEREUS_RV_TARGET=nereusrv@rv.example.org:/opt/nereus-rendezvous/ rendezvous/deploy.sh
```

Then, as root on the server, start it (and after every later deploy):

<!-- check: service -->
```sh
systemctl restart nereus-rendezvous
```

### 7. Check it

From anywhere:

```sh
curl -sS https://rv.example.org/            # "This address is the NereusSDR connection service..." (426)
turnutils_stunclient -p 3478 rv4.example.org   # your address, over IPv4
turnutils_stunclient -p 443 rv6.example.org    # the same over IPv6
```

On the server: `systemctl status caddy coturn nereus-rendezvous`, and
`ss -lntup` shows coturn on UDP 3478 and 443 only, the service on
127.0.0.1:8710 and [::1]:8710, and Caddy on TCP 80 and 443.

Then point NereusSDR at `rv.example.org` in its remote access settings.

Every request to `rv.example.org` goes to the service, which upgrades a
WebSocket and answers anything else with `426`, a short text and `Upgrade:
websocket`. No matcher picks WebSockets out in Caddy, because Caddy compares
header values exactly and Apple's WebSocket client sends `Upgrade:
WebSocket`. Clients open the WebSocket over HTTP/1.1 (the rendezvous
document, section 2): Caddy offers no WebSockets over HTTP/2.

## Beside a website

The rendezvous can share a server with a website that Caddy already serves,
but it needs care, which is why nereussdr.com gives it a server of its own:

- **Caddy's memory.** Every connection to the service is also a connection
  Caddy holds. Measured, Caddy holds about 100 KiB for each idle Core, and
  far more for a peer that sends part of a large message and stops: a peer
  filling the service's connections can grow the Caddy that also serves the
  website by several hundred MiB (the rendezvous document, section 9.1),
  and nothing in the rendezvous's settings bounds that. Size the server for
  both, or keep the connection pools small (`max_stations` and
  `max_connections` in `rendezvous.conf`).
- **UDP 443.** coturn needs UDP 443 and 3478 to itself: switch Caddy's
  HTTP/3 off (`protocols h1 h2` in its global options block), and stop
  anything else on the host that uses UDP 443 or 3478 (`setup-server.sh`
  refuses to start coturn while one does).
- **Leave the website's Caddy to the website.** Run `setup-server.sh` with
  `RV_MANAGE_CADDY=no`: it then neither installs Caddy nor touches its
  configuration or unit. Add the `(rv_headers)` snippet and the site block
  from `deploy/Caddyfile` to the website's Caddyfile yourself, with your
  host name, validate it, and reload Caddy. Consider the same drop-in
  (`deploy/caddy-override.conf`: restart on failure, a lower OOM score, a
  GOMEMLIMIT) for the website's Caddy, since the website depends on it too.
- **The deploy account.** `setup-server.sh` makes its own (`nereusrv`),
  apart from any account the website deploys as.

## Limits

The relay is sized by slots: `RV_RELAY_SLOTS` (64 by default) is how many
relays (coturn allocations) run at once. From it:

- `total-quota` = the slots: **64**. One relayed session takes 2 when only
  one end needs the relay and 4 when both do (each end may relay in both
  address families), so 64 slots carry about 32 sessions relayed at one end
  or 16 relayed at both. One more is refused (and NereusSDR tries the next
  way to connect).
- `max-bps` = 80000 bytes a second (640 kbit/s) each way for each relay, a
  little above the largest session NereusSDR sends (four panadapters and the
  microphone, about 520 kbit/s).
- `bps-capacity` = slots x `max-bps` = 64 x 80000 = **5120000 bytes a
  second**. coturn reserves `max-bps` of it for every relay while it lives,
  so this keeps bandwidth from refusing a relay before the slots run out.
  With every slot full at full rate the relay sends at most 5.12 MB a second
  (about 41 Mbit/s), which would be about 13 TB in 30 days. That is the
  ceiling, not the expectation: most sessions go direct and cost the server
  nothing, and a relayed session is usually far below its cap.
- `user-quota` = 4 relays for each Core (both ends of one session use the
  Core's id, and each end may take one per address family), so one Core
  cannot take the whole relay.

Transfer is watched, not capped: see "Data use". To change the size, set
`RV_RELAY_SLOTS` and run `setup-server.sh` again (it restarts coturn, which
drops the relays in use).

The service's own limits (connections, rates, sizes) are in
`server/rendezvous.conf.sample`, with the reasons in the rendezvous
document, section 9: up to 2000 registered Cores and 256 other connections
at once.

## Memory

`setup-server.sh` works the memory limits out from the server's memory
(`RV_MEMORY_MB`, by default what the kernel reports), in one formula: 256
MiB is kept for the system and coturn, Caddy's GOMEMLIMIT is half of the
rest and the service's MemoryMax 35% of it, leaving 15% as headroom. On a
1 GB server that is 352 MiB for Caddy and 246 MiB for the service; on 2 GB,
855 and 598. With 2000 idle Cores and a full client pool the service holds
about 86 MiB and Caddy about 270 MiB, measured.

If memory runs short, the kernel ends the service first (it has the higher
OOM score), which also frees the connections Caddy holds for it; systemd
starts it again at once and the Cores register again. Caddy restarts on its
own after any failure. The rendezvous document, section 9.1, has the
measurements and the arithmetic at 1 GB and 2 GB.

## Data use

The relay's transfer is not capped. Instead, a small report watches it:
`nereus-data-use.timer` runs `nereus-data-use.service` every hour, which
reads how many bytes the server has sent on its network interface (the
kernel's counter for the interface of the default route, or
`RV_DATA_USE_INTERFACE`) and keeps the calendar month's total (UTC). Once a
day it writes one line to the journal, and a warning line when the month's
total has passed `RV_TRANSFER_GB_PER_MONTH` (1000 GB by default; set it to
your plan's allowance):

```sh
journalctl -u nereus-data-use --since today
```

It counts everything the server sends on that interface (the relay, the
service, updates), which is what a provider bills. What it cannot know: the
kernel's counter starts again at every boot, so the bytes sent between the
last hourly reading and a restart are lost (the line then says the total is
short); bytes sent before the report was first installed in a month are not
counted (the line says when it started counting); and the month is UTC's,
which may not be the provider's billing month. It changes nothing on the
server and needs no package beyond Python.


## Rotating the secret

The service and coturn share one secret. To replace it (for example if the
file may have been read by someone else):

```sh
bash /root/rendezvous/deploy/setup-server.sh --rotate-secret
```

It writes a new secret and restarts coturn and the service (`--dry-run
--rotate-secret` says so first). The restart drops the relays in use;
NereusSDR makes new ones with new credentials. Registered Cores reconnect by
themselves. The one backup it keeps of `/etc/turnserver.conf` holds the
previous secret, readable by root alone, until the next change.

## What the logs contain

Both log to the systemd journal only (`journalctl -u nereus-rendezvous`,
`journalctl -u coturn`); neither writes a log file.

- **The service** logs events (a Core registered or left, an introduction and
  how it ended, a pairing code's number claimed or released, a mailbox opened
  or closed, an error code) with at most the first six characters of an id.
  Never an address, a whole id, a label, the secret, a credential, an offer
  or answer, a candidate, a pairing message or a pairing code's number.
- **coturn** at its default level logs its start-up, and two kinds of line
  about clients: a refused credential, naming the username (which is an
  expiry time and a Core's whole id, never a device or a person), and a
  refused relay destination, naming the address a client asked to reach.
  It does not log successful relays or client addresses. coturn has no
  setting that shortens those two lines; the journal is readable by root
  and the `adm` group only.


## Updating

Pull NereusSDR, then `rendezvous/deploy.sh` and `systemctl restart
nereus-rendezvous` for the service. For configuration changes, copy the
tree again the same way as step 3 (remove the old copy first, or it nests),
with the same settings as step 4 (`RV_DEPLOY_KEY` may be left out once the
account exists):

```sh
ssh root@rv.example.org rm -rf /root/rendezvous
scp -r rendezvous root@rv.example.org:/root/rendezvous
ssh root@rv.example.org bash /root/rendezvous/deploy/setup-server.sh --dry-run
ssh root@rv.example.org bash /root/rendezvous/deploy/setup-server.sh
```

The dry run says whether Caddy would be reloaded or restarted and whether
coturn or the service would be restarted.

## The checks

These need Docker and run everything in `ubuntu:24.04` containers, nothing on
a real server:

- `rendezvous/tests/coturn-check.sh`: `setup-server.sh` in a container
  (with apt-get failing, without systemd, and with a stand-in systemctl so
  its start, reload, restart and port checks run), then coturn as its unit
  starts it: its ports, STUN, allocations on both ports in both families
  with `turnutils_uclient`, refused credentials, every blocked destination,
  the quotas, credentials minted by the running service, what happens to a
  relay whose credential expires, and its logs.
- `rendezvous/tests/caddy-check.sh`: `deploy/Caddyfile`: `caddy validate`,
  a WebSocket through Caddy to the service by host name with each client's
  own address, Apple's exact opening request, the plain answer, what an
  HTTP/2 client gets, and no UDP port.
- `rendezvous/tests/memory-check.sh`: the service and Caddy loaded with 2000
  registered Cores and 256 clients, then with unfinished messages and Cores
  that stop reading, measured (the numbers in section 9.1).
- `rendezvous/tests/readme-check.sh`: a fresh container set up by following
  this file (the blocks marked for it), then used as both the service and
  the relay.
