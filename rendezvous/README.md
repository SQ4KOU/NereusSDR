# NereusSDR rendezvous

The rendezvous lets NereusSDR (the desktop's remote window and the iPhone app)
reach a Core it cannot address directly, and carries pairing when the two are
not on one network. It has two parts, and a server running it has both:

- **The service** (`server/`): a small Python program that introduces clients
  to Cores over a WebSocket at `wss://rv.<your domain>/`, hands out relay
  credentials, and holds pairing mailboxes. It keeps everything in memory.
  Its wire is [the rendezvous document](../docs/architecture/2026-09-23-rendezvous-v1.md).
- **The relay**: coturn, Ubuntu's own package, on UDP 3478 and UDP 443. It
  carries a session only when the two ends cannot reach each other directly.

NereusSDR uses `rv.nereussdr.com`, which runs beside the website on the same
server. Anyone can run their own; this file is the whole recipe. Every
default in the scripts is nereussdr.com's, and each one is a setting.

- `server/`: the service. `rendezvous.conf.sample` lists every setting.
- `deploy/`: `setup-server.sh` (prepares a server; safe to run again),
  `turnserver.conf` (coturn's configuration), `nereus-rendezvous.service`
  (the service's systemd unit), `coturn-override.conf` (a drop-in for
  Ubuntu's coturn unit), and the data-use report (`data-use.py`,
  `nereus-data-use.service` and `nereus-data-use.timer`; see "Data use").
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

You need an Ubuntu 24.04 server with a public IPv4 and a public IPv6 address
on its network interface (a typical VPS; behind a provider's NAT, coturn
would also need its `external-ip` setting, which this recipe does not
cover), a domain whose DNS you control, SSH to the server as root, and
`rsync` on both ends (Ubuntu's server images and macOS have it). The commands below use `example.org`; use
your own names.

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
both. The service's own name has both records.

Caddy obtains the certificate for `rv.example.org` from Let's Encrypt on its
own once the A and AAAA records point at the server and TCP 80 and 443 are
open. The relay names need no certificate.

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
on loopback only.

### 3. Copy the files to the server

From a checkout of NereusSDR on your own computer:

<!-- check: copy -->
```sh
ssh root@rv.example.org rm -rf /root/rendezvous
scp -r rendezvous root@rv.example.org:/root/rendezvous
```

(The `rm` first: copying onto an earlier copy would put the new one inside
it, as `/root/rendezvous/rendezvous`, and leave the old files in use.)

### 4. The deploy account

`deploy.sh` publishes the code as an ordinary account, never as root. On
nereussdr.com that is the website's `nereusweb`. On a server of your own, as
root, make one and give it your SSH public key:

<!-- check: server -->
```sh
useradd --create-home --shell /bin/bash nereusweb
install -d -m 700 -o nereusweb -g nereusweb /home/nereusweb/.ssh
echo 'ssh-ed25519 AAAA...your key... you@computer' > /home/nereusweb/.ssh/authorized_keys
chown nereusweb:nereusweb /home/nereusweb/.ssh/authorized_keys
chmod 600 /home/nereusweb/.ssh/authorized_keys
```

### 5. Settings

On the server, as root, name your hosts, how many relays may run at once,
and your server's monthly transfer allowance in GB (see "Limits" and "Data
use" below):

<!-- check: settings -->
```sh
export RV_HOST=rv.example.org
export RV_RELAY_HOST4=rv4.example.org
export RV_RELAY_HOST6=rv6.example.org
export RV_RELAY_SLOTS=64
export RV_TRANSFER_GB_PER_MONTH=1000
```

`setup-server.sh` finds the server's public addresses itself; set
`RV_PUBLIC_IPV4` and `RV_PUBLIC_IPV6` if it picks the wrong ones. It counts
data use on the interface of the default route; set `RV_DATA_USE_INTERFACE`
to count another. Every value that belongs to one server is one of these
settings, so the same files move to another server unchanged.

### 6. Caddy

Caddy terminates TLS for the service. Install it from Caddy's own apt
repository (the commands from Caddy's install page):

<!-- check: server -->
```sh
apt-get update
apt-get install -y debian-keyring debian-archive-keyring apt-transport-https curl gnupg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' -o /etc/apt/sources.list.d/caddy-stable.list
chmod o+r /usr/share/keyrings/caddy-stable-archive-keyring.gpg /etc/apt/sources.list.d/caddy-stable.list
apt-get update
apt-get install -y caddy
```

Then give it the rendezvous site. On a server with nothing else on it, this
is the whole Caddyfile:

<!-- check: server -->
```sh
cat > /etc/caddy/Caddyfile <<EOF
{
	# HTTP/3 off: it would take UDP 443, which is the relay's.
	servers {
		protocols h1 h2
	}
}

${RV_HOST} {
	header {
		Strict-Transport-Security "max-age=31536000"
		X-Content-Type-Options "nosniff"
		Referrer-Policy "no-referrer"
		X-Frame-Options "DENY"
		Content-Security-Policy "default-src 'none'; frame-ancestors 'none'"
		-Server
		-Via
	}

	reverse_proxy 127.0.0.1:8710 [::1]:8710 {
		lb_policy first
		header_up X-Forwarded-For {remote_host}
		stream_close_delay 5m
	}

	@http1 protocol http/1.1
	header @http1 ?Upgrade websocket
}
EOF
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile
```

<!-- check: service -->
```sh
systemctl reload caddy
```

Every request goes to the service, which upgrades a WebSocket and answers
anything else with `426`, a short text and `Upgrade: websocket` (the last
line puts that header back on HTTP/1.1 answers, since a proxy drops it). No
matcher picks WebSockets out in Caddy, because Caddy compares header values
exactly and Apple's WebSocket client sends `Upgrade: WebSocket`. Clients open
the WebSocket over HTTP/1.1 (the rendezvous document, section 2): Caddy
offers no WebSockets over HTTP/2.

**Beside a website.** If the server already serves a website with Caddy, do
not replace its Caddyfile: add the `${RV_HOST} { ... }` block to it, and make
sure its global options block has `protocols h1 h2` as above (Caddy's HTTP/3
would otherwise take UDP 443 from the relay). That is how nereussdr.com runs:
see `website/deploy/Caddyfile`, whose `rv.nereussdr.com` site is this block
with an error page. The website's own blocks do not change. The
`header_up X-Forwarded-For {remote_host}` line matters: the service takes the
client's address from the last entry of that header, and believes it only
from a loopback peer.

### 7. The relay and the service

As root, check first, then set up:

<!-- check: server -->
```sh
apt-get install -y python3   # already there on Ubuntu's server images
bash /root/rendezvous/deploy/setup-server.sh --dry-run
bash /root/rendezvous/deploy/setup-server.sh
```

`setup-server.sh` installs coturn, `python3-websockets` and
`python3-cryptography` from Ubuntu (without letting coturn start with its
stock configuration, and with coturn disabled until its configuration is in
place), makes the TURN secret (root only, in
`/etc/nereus-rendezvous/turn-secret`), writes `/etc/turnserver.conf` and
`/etc/nereus-rendezvous/rendezvous.conf`, installs the service's unit,
coturn's drop-in and the data-use report, enables all three and starts
coturn. Run again, it restarts coturn or the service only when one of its
files changed (a coturn restart drops every relay in use), and the dry run
says which it would restart. It refuses to go on while another program holds
UDP 3478 or 443 (Caddy's TCP 443 is no obstacle). It never touches the
firewall, Caddy, or any account but the deploy account's code directory,
`/opt/nereus-rendezvous`.

### 8. The service's code

From your own computer, publish the code to the deploy account:

<!-- check: deploy -->
```sh
NEREUS_RV_TARGET=nereusweb@rv.example.org:/opt/nereus-rendezvous/ rendezvous/deploy.sh --dry-run
NEREUS_RV_TARGET=nereusweb@rv.example.org:/opt/nereus-rendezvous/ rendezvous/deploy.sh
```

Then, as root on the server, start it (and after every later deploy):

<!-- check: service -->
```sh
systemctl restart nereus-rendezvous
```

### 9. Check it

From anywhere:

```sh
curl -sS https://rv.example.org/            # "This address is the NereusSDR connection service..." (426)
turnutils_stunclient -p 3478 rv4.example.org   # your address, over IPv4
turnutils_stunclient -p 443 rv6.example.org    # the same over IPv6
```

On the server: `systemctl status coturn nereus-rendezvous caddy`, and
`ss -lntup` shows coturn on UDP 3478 and 443 only, the service on
127.0.0.1:8710 and [::1]:8710, and Caddy on TCP 80 and 443.

Then point NereusSDR at `rv.example.org` in its remote access settings.

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
document, section 9: up to 2000 registered Cores and 1024 other connections
at once. Its unit caps its memory at 256 MiB (it holds about 106 MiB with
both pools full of idle connections, measured) and allows it 8192 open
files. On a server of 1 GB, Caddy holds about 100 KiB for each connection
it forwards, measured, so 2000 Cores and a full client pool take about
370 MiB of Caddy's memory as well: section 9.1 has the whole budget.

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

It counts everything the server sends on that interface (the website, the
relay, updates), which is what a provider bills. What it cannot know: the
kernel's counter starts again at every boot, so the bytes sent between the
last hourly reading and a restart are lost (the line then says the total is
short); bytes sent before the report was first installed in a month are not
counted (the line says when it started counting); and the month is UTC's,
which may not be the provider's billing month. It changes nothing on the
server and needs no package beyond Python.

## Rotating the secret

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
tree again the same way as step 3 (remove the old copy first, or it nests):

```sh
ssh root@rv.example.org rm -rf /root/rendezvous
scp -r rendezvous root@rv.example.org:/root/rendezvous
ssh root@rv.example.org bash /root/rendezvous/deploy/setup-server.sh --dry-run
ssh root@rv.example.org bash /root/rendezvous/deploy/setup-server.sh
```

The dry run says whether coturn or the service would be restarted.

## The checks

These need Docker and run everything in `ubuntu:24.04` containers, nothing on
a real server:

- `rendezvous/tests/coturn-check.sh`: `setup-server.sh` in a container
  (with apt-get failing, without systemd, and with a stand-in systemctl so
  its start, restart and port checks run), then coturn as its unit starts
  it: its ports, STUN, allocations on both ports in both families with
  `turnutils_uclient`, refused credentials, every blocked destination, the
  quotas, credentials minted by the running service, what happens to a relay
  whose credential expires, and its logs.
- `rendezvous/tests/caddy-check.sh`: `website/deploy/Caddyfile` with the
  `rv` site: `caddy validate`, the website unchanged by it, a WebSocket
  through Caddy to the service by host name with each client's own address,
  Apple's exact opening request, the plain answer, and what an HTTP/2 client
  gets.
- `rendezvous/tests/memory-check.sh`: the service and Caddy loaded with 2000
  registered Cores and 1024 clients, then with unfinished messages and Cores
  that stop reading, measured (the numbers in section 9.1).
- `rendezvous/tests/readme-check.sh`: a fresh container set up by following
  this file (the blocks marked for it), then used as both the service and
  the relay.
