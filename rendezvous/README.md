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
  Ubuntu's coturn unit).
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
scp -r rendezvous root@rv.example.org:/root/rendezvous
```

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

On the server, as root, name your hosts and your server's monthly transfer
allowance in GB (it sizes the relay; see "Limits" below):

<!-- check: settings -->
```sh
export RV_HOST=rv.example.org
export RV_RELAY_HOST4=rv4.example.org
export RV_RELAY_HOST6=rv6.example.org
export RV_TRANSFER_GB_PER_MONTH=1000
```

`setup-server.sh` finds the server's public addresses itself; set
`RV_PUBLIC_IPV4` and `RV_PUBLIC_IPV6` if it picks the wrong ones.

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
	}

	@websocket {
		header Connection *Upgrade*
		header Upgrade websocket
	}
	handle @websocket {
		reverse_proxy 127.0.0.1:8710 [::1]:8710 {
			lb_policy first
			header_up X-Forwarded-For {remote_host}
			stream_close_delay 5m
		}
	}

	handle {
		header Content-Type "text/plain; charset=utf-8"
		respond "NereusSDR connection service" 426
	}
}
EOF
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile
```

<!-- check: service -->
```sh
systemctl reload caddy
```

**Beside a website.** If the server already serves a website with Caddy, do
not replace its Caddyfile: add the `${RV_HOST} { ... }` block to it, and make
sure its global options block has `protocols h1 h2` as above (Caddy's HTTP/3
would otherwise take UDP 443 from the relay). That is how nereussdr.com runs:
see `website/deploy/Caddyfile`, whose `rv.nereussdr.com` site is this block
with a few more headers. The website's own blocks do not change. The
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
stock configuration), makes the TURN secret (root only, in
`/etc/nereus-rendezvous/turn-secret`), writes `/etc/turnserver.conf` and
`/etc/nereus-rendezvous/rendezvous.conf`, installs the service's unit and
coturn's drop-in, and starts coturn. It refuses to go on while another
program holds UDP 3478 or 443. It never touches the firewall, Caddy, or any
account but the deploy account's code directory, `/opt/nereus-rendezvous`.

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
curl -sS https://rv.example.org/            # "NereusSDR connection service" (426)
turnutils_stunclient -p 3478 rv4.example.org   # your address, over IPv4
turnutils_stunclient -p 443 rv6.example.org    # the same over IPv6
```

On the server: `systemctl status coturn nereus-rendezvous caddy`, and
`ss -lntup` shows coturn on UDP 3478 and 443 only, the service on
127.0.0.1:8710 and [::1]:8710, and Caddy on TCP 80 and 443.

Then point NereusSDR at `rv.example.org` in its remote access settings.

## Limits

`RV_TRANSFER_GB_PER_MONTH` is the one number to set: the monthly transfer
your server's plan includes, in GB (10^9 bytes). From it:

- `bps-capacity` (coturn's cap on the bytes a second it sends, all relays
  together) = allowance x 90% / the seconds in 30 days. At that rate for a
  whole month the relay uses 90% of the allowance and no more, leaving the
  rest for the website, the service and the host. For 1000 GB:
  1000 x 10^9 x 0.9 / 2592000 = **347222 bytes a second** (about 2.8 Mbit/s).
- `max-bps` = 80000 bytes a second (640 kbit/s) for each relay, a little
  above the largest session NereusSDR sends (four panadapters and the
  microphone, about 520 kbit/s). coturn reserves that much of `bps-capacity`
  for each relay while it lives.
- `total-quota` = `bps-capacity` / `max-bps`, rounded down: how many relays
  run at once. For 1000 GB: **4**. A fifth is refused (and NereusSDR tries
  the next way to connect).
- `user-quota` = 2 relays for each Core (both ends of one session use the
  Core's id), so one Core cannot take the whole relay.

The relay is the last way NereusSDR connects: most sessions go direct and
cost the server nothing. A plan with more transfer carries more relayed
sessions; set the number and run `setup-server.sh` again.

The service's own limits (connections, rates, sizes) are in
`server/rendezvous.conf.sample`, with the reasons in the rendezvous
document, section 9. Its unit caps its memory at 320 MiB, just above the
worst case those limits allow.

## Rotating the secret

The service and coturn share one secret. To replace it (for example if the
file may have been read by someone else):

```sh
bash /root/rendezvous/deploy/setup-server.sh --rotate-secret
```

It writes a new secret and restarts coturn and the service. Relays already
running carry on (coturn checks a credential when a relay is made); new
ones use the new secret. Registered Cores reconnect by themselves.

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
nereus-rendezvous` for the service; copy `rendezvous/deploy/` again and run
`setup-server.sh` for configuration changes.

## The checks

These need Docker and run everything in `ubuntu:24.04` containers, nothing on
a real server:

- `rendezvous/tests/coturn-check.sh`: `setup-server.sh` in a container, then
  coturn as its unit starts it: its ports, STUN, allocations on both ports
  in both families with `turnutils_uclient`, refused credentials, every
  blocked destination, the quotas, credentials minted by the running
  service, what happens to a relay whose credential expires, and its logs.
- `rendezvous/tests/caddy-check.sh`: `website/deploy/Caddyfile` with the
  `rv` site: `caddy validate`, the website unchanged by it, and a WebSocket
  through Caddy to the service by host name with each client's own address.
- `rendezvous/tests/readme-check.sh`: a fresh container set up by following
  this file (the blocks marked for it), then used as both the service and
  the relay.
