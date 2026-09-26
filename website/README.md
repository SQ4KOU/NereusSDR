# NereusSDR website

The public site at https://nereussdr.com/, served by Caddy from a DigitalOcean
droplet.

- `public/` is the site itself: static HTML, CSS, JS and images, no build step.
- `deploy/` is the server side: `Caddyfile` (the web server config, which
  also holds the `rv.nereussdr.com` site for the rendezvous service) and
  `setup-server.sh` (one-time, re-runnable server setup).
- `deploy.sh` publishes `public/` to the server with rsync.

## Preview locally

```sh
python3 -m http.server 8765 --directory website/public
```

Then open http://localhost:8765/. The preview does not send the headers Caddy
adds in production (including the Content-Security-Policy), so check the live
site after a deploy as well.

## One-time server setup

1. On the Mac, add the deploy alias to `~/.ssh/config`:

   ```
   Host nereus-web
       HostName <droplet IPv4>
       User nereusweb
       IdentityFile ~/.ssh/nereus_vps_ed25519
       IdentitiesOnly yes
   ```

   The scripts reach the server only through this alias, so the address never
   appears in the repository.

2. As root on the droplet (`root@<droplet>` is whatever root login already
   works), from the repository root on the Mac:

   ```sh
   scp website/deploy/Caddyfile website/deploy/setup-server.sh root@<droplet>:/root/
   ssh root@<droplet> "bash /root/setup-server.sh /root/Caddyfile '$(cat ~/.ssh/nereus_vps_ed25519.pub)'"
   ```

   The script installs Caddy from the official Caddy apt repository, creates the
   `nereusweb` deploy account with that public key, prepares
   `/var/www/nereussdr`, validates and installs the Caddyfile, and starts Caddy.
   It is safe to run again, for example after editing the Caddyfile. It does
   not touch the firewall, the SSH daemon or any other service on the droplet.

## DNS at Namecheap

| Type | Host | Value |
| ---- | ---- | ----- |
| A    | `@`   | `<droplet IPv4>` |
| AAAA | `@`   | `<droplet IPv6>` |
| A    | `www` | `<droplet IPv4>` |
| AAAA | `www` | `<droplet IPv6>` |
| A    | `rv`  | `<droplet IPv4>` |
| AAAA | `rv`  | `<droplet IPv6>` |
| A    | `rv4` | `<droplet IPv4>` (no AAAA) |
| AAAA | `rv6` | `<droplet IPv6>` (no A) |

`rv` is the rendezvous service; `rv4` and `rv6` are its relay's names, one
address family each (`rendezvous/README.md`, "DNS", says why).

If Namecheap's default parking records for `@` and `www` are still there,
delete them first. Caddy obtains the HTTPS certificates by itself once both
names resolve to the droplet, and keeps retrying until they do.

## Deploy

```sh
website/deploy.sh --dry-run   # list what would change
website/deploy.sh             # publish
```

rsync runs with `--delete`, so the server ends up with exactly what is in
`public/`, minus `.DS_Store`, `*.swp` and `.git*` files. The script refuses to
run when `public/index.html` is missing or `public/` holds fewer than 3 files.
Set `NEREUS_WEB_TARGET` to a local directory to try it without the server.

## The server's ports

The droplet also runs the NereusSDR rendezvous service and its relay
(`rendezvous/README.md`). Who holds which port:

| Port | Held by | Reached from |
| ---- | ------- | ------------ |
| TCP 80 | Caddy (HTTP, redirects to HTTPS, certificate challenges) | the internet |
| TCP 443 | Caddy: nereussdr.com, www.nereussdr.com and rv.nereussdr.com | the internet |
| TCP 8710 on 127.0.0.1 and ::1 | the rendezvous service, behind Caddy's rv.nereussdr.com site | Caddy only |
| UDP 3478 | coturn (STUN and TURN), public IPv4 and IPv6 | the internet |
| UDP 443 | coturn (STUN and TURN), public IPv4 and IPv6 | the internet |
| UDP 61000 to 65535 | coturn's relays | the internet |
| TCP 22 | SSH | the internet |

The R5 test server `netbench` (account `netbench`, its own systemd unit)
uses UDP 443, 3478, 50001 and 47001 and TCP 47000 while it runs. Its UDP
443 and 3478 are coturn's now, so it stays stopped while the relay runs;
`rendezvous/deploy/setup-server.sh` refuses to start coturn while anything
else holds either port. Its other ports are clear of everything above,
coturn's relay range included.

No firewall is active on the droplet; the scripts never change that.

## Why HTTP/3 is off

Caddy normally serves HTTP/3 as well, and HTTP/3 runs over QUIC on UDP port
443. On this droplet UDP 443 is the relay's (coturn, above), so the
Caddyfile limits Caddy to HTTP/1.1 and HTTP/2 (`protocols h1 h2`), which keeps
it on TCP 80 and TCP 443 only. Browsers fall back to HTTP/2 without any visible
difference. `setup-server.sh` fails if Caddy ever holds a UDP port, so HTTP/3
cannot come back unnoticed while that other service shares the host.
