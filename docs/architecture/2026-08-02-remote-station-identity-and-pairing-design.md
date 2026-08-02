# Remote Station Identity, Pairing and Rendezvous: Design

**Status:** Design, pending approval. Not yet planned or implemented.
**Date:** 2026-08-02
**Author:** J.J. Boyd (KG4VCF), with AI-assisted drafting via Anthropic Claude Code

> **Parent document:**
> [2026-07-28-remote-daemon-architecture-design.md](2026-07-28-remote-daemon-architecture-design.md).
> That document covers the daemon, the split point, state synchronisation, the
> wire, codecs, and the transport ladder. It resolves identity in a single
> sentence at section 7.1: "the token distribution mechanism must be specified
> before R2". **This document is that specification**, and it is larger than one
> sentence implies. It also corrects the parent's carrier-NAT assumption at
> section 10.2 (see section 9 below).

---

## 1. Goal

Let an operator connect a NereusSDR client to their own `nereusd` station from
anywhere, with no account to create, no service that can lock them out, and a
setup process short enough that a first-time user does not give up.

Three requirements, in priority order:

1. **Not centralised.** No component of the normal operating path may depend on
   infrastructure the operator does not control. This restates the parent
   document's section 10.1 governing principle and extends it to identity.
2. **Easy.** A fresh Pi on the operator's own bench should be usable in one
   click. Nothing that expires while the operator walks to another room.
3. **Reasonably secure.** Proportionate to a device that keys a transmitter and
   is reachable from the internet. Not proportionate to a bank.

**Attribution:** everything specified here is NereusSDR-original. There is no
Thetis, AetherSDR, freedv-gui or WDSP equivalent to port from, so no upstream
header and no `THETIS-PROVENANCE.md` row applies. This is consistent with parent
section 11, which already classifies the session layer, state mirror and
transport as NereusSDR-original. Third-party libraries named in section 10.1 are
dependencies to be licence-reviewed, not ports.

**Non-goal:** guest sessions and multi-operator access. The parent document
defers these (section 16) while requiring that every connection carry an
identity and a role from day one. Nothing here blocks that later work; adding a
guest role becomes new permission checks, not a redesign.

---

## 2. Decisions

| Decision | Choice |
| --- | --- |
| What identity is | An asymmetric key pair, generated on first run, held by the machine. Not an account, not a name |
| Human-readable name | Callsign plus a free-form suffix, e.g. `KG4VCF/shack`. A local label only |
| Where the label lives | Client-side and on the daemon. **Never sent to the rendezvous** |
| Accounts, usernames, passwords | None. Explicitly considered and rejected: they solve none of the hard problems and centralise the one thing the operator cares about |
| Station visibility | Private. The rendezvous cannot enumerate stations or confirm whether a given callsign exists |
| Pairing on the LAN | One click, no code, no fingerprint comparison |
| Pairing remotely | Short human-speakable code, protected so a hostile rendezvous cannot use or learn it |
| Pairing by hand | Copy the key over SSH. Always available, never required |
| Pairing window | Open while the station is unclaimed, with **no timer**. Closes permanently on first pair |
| Reopening the window | From the daemon console, or from an already-paired device |
| Paired devices | A list, each with its own key, name and last-seen time |
| Revocation | Immediate, from any paired device or the console. Drops a live session |
| Lockout recovery | A console command resets the station to unclaimed. Physical access always wins |
| Concurrent sessions | One, unchanged from parent section 7.1. A new connection preempts |
| Rendezvous roles | Introduction, reflexive address, relay. One binary, one host |
| Rendezvous implementation | Off-the-shelf `coturn` for STUN and TURN. We write only the small signalling service |
| Self-hosting | Supported and documented, for **both** introduction and relay |
| Server list | Ordered, client-side. The operator's own server first, the default behind it |
| Relay posture | Last resort, switchable off, short-lived credentials, with background upgrade to direct |
| Manual address entry | First class. Hostname, IPv4, or bracketed IPv6, with optional port |
| Station discovery on the LAN | New, multicast on both IP stacks, daemon announces and client listens |
| Radio discovery | Unchanged. Same `RadioDiscovery` broadcast, now running on the daemon host |
| Scrambled-name rotation | Deferred. Fixed for v1; self-hosting is the answer for operators who need more |

---

## 3. Identity

### 3.1 The key is the station

`nereusd` generates a key pair on first run and never releases the private half.
That key is the station's identity. The client generates its own key pair for
the same reason, so a device is identified by what it holds rather than by what
it claims.

This is the model SSH, WireGuard and Syncthing arrived at independently. It is
chosen here for one specific property: there is no third party who can grant,
revoke, or withhold an operator's access to their own radio.

### 3.2 The cost, stated plainly

The private key on the daemon **is** the station. Losing it, most realistically
by an SD card failure, means every paired device must pair again. There is no
account to recover from, by design.

Mitigations, both required:

- The daemon writes its key to a single file with a documented path, so backing
  it up is one copy operation.
- First-run output and the Setup page both prompt for that backup. A user who
  loses a card and was never told is a support failure, not a user error.

### 3.3 Naming

The label is `<callsign>/<suffix>`, for example `KG4VCF/shack`. The convention
is deliberately the one hams already read on sight from packet radio and APRS,
where a suffix distinguishes stations under one call.

Rules:

- Callsign portion defaults to the callsign already stored in `AppSettings` for
  spotting and reporting. **Setup asks the operator for nothing.**
- Suffix is free-form, maximum 32 characters, from `[A-Za-z0-9_-]`.
- Comparison is case-insensitive, so `KG4VCF/Shack` and `kg4vcf/shack` cannot
  both exist and be mistaken for each other. Display preserves what was typed.
- Empty suffix is legal and displays as the bare callsign.

**The label is not an identifier.** It is not checked, not unique, and never
transmitted to the rendezvous. Two operators may both label a station
`KG4VCF/shack` with no consequence whatsoever, because nothing matches on it.
This dissolves the callsign-squatting problem rather than policing it.

---

## 4. Pairing

### 4.1 One ceremony

Fingerprint comparison is deliberately **not** used. It is a ritual users click
through without reading, so it costs friction and buys close to nothing. Where a
check is needed, the short code provides it.

### 4.2 On the local network: one click

1. The daemon announces itself (section 6).
2. The client lists it under "Stations on this network", marked unclaimed.
3. The operator clicks Pair. Keys are exchanged. Done.

**Accepted risk, recorded so it is a choice and not an oversight.** Another
device on the same network could claim a brand-new station before the operator
does. It applies only to a station that has never been paired, and it requires
presence on the operator's own network. A configuration key forces the code path
always, for operators on shared or untrusted networks. The default is one click,
because the alternative taxes every user forever against a case most never meet.

### 4.3 Remotely: a short code

The daemon displays a code such as `7-anvil-harbor` on its console, in its log,
and on a local status page. The operator types it into the client from anywhere.

Requirements:

- **Safe against a hostile rendezvous.** The code must be usable for
  authenticated key exchange without the server learning it or being able to
  impersonate either end. This is a password-authenticated key exchange, the
  mechanism Magic Wormhole uses for the same purpose.
- Single use. Consumed on success, burned on a wrong guess.
- Attempts rate-limited, so a short code cannot be ground down.
- Speakable over the phone. Digit plus two words, not hexadecimal.
- Displayed continuously while unclaimed, so there is nothing to miss.

This adds a cryptographic dependency (section 10).

### 4.4 By hand

The operator copies the daemon's public key over SSH. No rendezvous, no code, no
discovery. It exists so the operator is never without an option, and it is not
part of the documented normal flow.

### 4.5 The pairing window

A daemon accepts pairing only while its pairing window is open.

- **Open by default while unclaimed, with no timer.** An unpaired station holds
  nothing worth taking, and the code still gates the remote path. A timer here
  would produce the exact failure this design is trying to avoid: setup silently
  expiring while the operator fetches a laptop.
- **Closes permanently on the first successful pair.**
- **Reopens** from the daemon console, or from an already-paired device.

That last route is what makes adding a second device pleasant: the operator adds
a phone from an already-paired laptop and never approaches the radio.

---

## 5. The rendezvous

### 5.1 Three jobs, one host

Per parent section 10.3: introduction, reflexive address reporting, and relay.
`coturn` provides STUN and TURN, is packaged in every distribution, and is what
the rest of the industry uses. **We write only the signalling service**, and the
existing Qt WebSocket infrastructure from the TCI work is the natural base.

Correcting an impression left earlier in design discussion: the rendezvous is
not a large new product. The novel surface is identity binding and operations,
not traversal.

### 5.2 What it can and cannot see

The daemon registers under a **scrambled name derived from its public key**, not
from its label. A client holding the daemon's key derives the same value and
asks where it is. The server matches opaque strings.

| The rendezvous sees | The rendezvous cannot see |
| --- | --- |
| IP addresses of both ends | Callsign or label |
| That an opaque identifier is online | Which operator or station it belongs to |
| Traffic volume and timing when relaying | Any relayed content, which is encrypted end to end |

It cannot enumerate stations, and it cannot answer "is KG4VCF on the air".

**Rotation is deferred.** Rotating the scrambled name would defeat long-term
correlation by an observer, at the cost of clock agreement, drift handling, and
boundary lookups. For v1 the value is fixed. The stronger answer for an operator
who needs more is self-hosting, which is supported.

### 5.3 Not a single point of failure

Three mechanisms, all required:

- **Cached address first.** The client tries where the station was last seen
  before contacting anyone. Normal reconnects never touch the rendezvous.
- **Ordered server list.** The operator's own server first, the default behind
  it. One list covers introduction and relay.
- **Established sessions are unaffected by an outage**, because the rendezvous
  was never in the media path. Only a first-time connection from a new network
  with no cached address is blocked.

### 5.4 Relay

**Relay carries every packet.** Audio, spectrum, meters, control, microphone,
in both directions. The content is opaque to it, but the bytes all pass through.

Sizing, derived from parent section 10.3:

| Session shape | Relay egress | At 3 hours a day |
| --- | --- | --- |
| One pan, receive only | ~145 kbit/s | ~6 GB per month |
| Four pans plus microphone | ~480 kbit/s | ~19 GB per month |

A VPS including 1 to 2 TB of egress therefore carries on the order of a hundred
regular users. Idle registered stations cost only their check-in.

Policy:

- **Last resort.** Direct is attempted first.
- **Bounded wait, then background upgrade.** If direct has not come up within
  **3 seconds by default, configurable**, the session falls back to relay so the
  operator is not staring at a spinner, and the client keeps hunting for a direct
  path and migrates to it silently when one appears. The status bar shows the
  current path. The default is a starting point to be tuned against measurement,
  alongside the keepalive interval in section 9.1.
- **Switchable off** for operators who want direct or nothing.
- **Short-lived credentials**, minted at connect time for an already-paired
  pair. A relay with static credentials is an open relay.
- **Must work over TCP 443 with TLS**, not UDP only. See section 9.

### 5.5 Self-hosting

Both introduction and relay must be self-hostable, packaged and documented for
someone who is not the author. This is not a nicety: for a station behind a
carrier that forbids direct connections, running a small server on a cheap VPS
is the only path that is both reliable and independent, and it also removes that
operator from the default server's bandwidth bill.

Deliverables: a package or container image, a sample configuration, and
documentation covering DNS, certificates, and firewall rules.

---

## 6. Discovery on the local network

Two discoveries, independent of each other.

**Radio discovery is unchanged.** The existing `RadioDiscovery` UDP broadcast on
port 1024 keeps its behaviour and its code. The only change is that it now runs
on the daemon host, because that is what is attached to the radio. Parent
section 10.6 already records this as unaffected.

**Station discovery is new.** The daemon announces itself, the client listens.
Used both for one-click pairing and, permanently afterwards, for connecting on
the LAN without an address.

- **Multicast, not broadcast**, because it must work over IPv6, which parent
  section 10.6 requires from the start.
- Mirrors the existing radio-discovery pattern rather than adopting mDNS. That
  keeps the shape familiar and adds no Avahi or Bonjour dependency and no
  per-platform service. The cost is no free `.local` name, which is acceptable
  because discovery hands the client an address directly.
- **One way.** The daemon announces, the client only listens. A client never
  advertises itself on a network it does not own.

The connect screen presents three groups, and the operator is never asked to
understand the difference:

- **Radios on this network:** unchanged local direct mode
- **Stations on this network:** daemons found on the LAN
- **Your stations:** every paired station, reachable from anywhere

---

## 7. Devices and revocation

Each paired device holds its own key. The daemon keeps a list.

| Field | Note |
| --- | --- |
| Public key | The identity |
| Name | Defaults to the machine name, so nothing is typed |
| Paired at | |
| Last seen | |

- **Revocation is immediate**, from any paired device or the console. A revoked
  device that is connected at that moment is dropped mid-session.
- **Multiple devices may be paired; one may be connected.** Parent section 7.1's
  preemption rule is unchanged, and it is what lets an operator reconnect from a
  phone after a link drop.
- **Recovery from total loss:** a console command returns the station to
  unclaimed. There is no account, so physical access is the recovery path.

---

## 8. Manual address entry

First class, never required, never contacting any server.

Accepted forms, everywhere an address is taken:

- Hostname, `shack.example.net`
- IPv4 literal
- IPv6 literal in brackets, `[2001:db8::1]`
- Optional `:port`, with a documented default

Behaviour:

- Hostnames resolve with the IPv6-preferring staggered approach in parent
  section 10.6.
- **Identity still applies.** A mistyped address that reaches someone else's
  machine fails to authenticate rather than connecting. This is what makes
  manual entry safe to expose.
- Covers the port-forwarded case, the routable-IPv6 case, the LAN case, and an
  operator-run VPN. NereusSDR does not require a VPN and does not obstruct one.

**Manual NAT to NAT, with no server at all.** The daemon surfaces its own
reflexive address, the operator tells each end about the other, and both punch
outward. Documented honestly: fiddly, requires both ends within roughly the same
half-minute, and fails against endpoint-dependent mapping. It exists so the
operator is never without options. Self-hosting a rendezvous is the better
answer to the same problem.

---

## 9. Carrier NAT, and a correction to the parent design

**Parent section 10.2 is wrong on a point that matters.** It argues that most
carrier NAT is endpoint-independent "because symmetric behaviour breaks games
and VoIP", and concludes that hole punching usually succeeds. Field experience
and published documentation both contradict this.

**Field evidence.** The maintainer attempted Tailscale over T-Mobile to reach a
FlexRadio and could not obtain a usable connection. The cause was the carrier's
CGNAT posture, not bandwidth.

**Published evidence.**

- T-Mobile Home Internet blocks unsolicited inbound traffic **on IPv6 as well as
  IPv4**, and its gateway exposes no firewall or port-forwarding controls. The
  parent document's tier 1 assumption, that IPv6 is a clean path needing no
  traversal machinery, does not hold there.
- Its IPv4 path is double NAT, carrier CGNAT plus gateway NAT, producing strict
  NAT in gaming terms with no port forwarding available.
- Mobile carriers commonly use endpoint-dependent mapping, which is the
  behaviour that defeats hole punching.
- Tailscale's own documentation describes mobile CGNAT as very restrictive, with
  short port timeouts and symmetric mapping, and notes that two devices on
  different cellular networks will often be stuck on relay. Their relay runs
  over HTTPS on TCP 443 specifically because UDP is filtered.
- The parent's supporting argument is self-refuting on inspection: gaming **is**
  broken on these connections, and the carrier shipped it anyway.

### 9.1 What this changes

1. **Relay is a first-class path, not a rare tier.** For a station behind such a
   carrier it is the only path. It must be built and sized accordingly.
2. **Relay must run over TCP 443 with TLS.** A UDP-only relay fails on exactly
   the networks that need one.
3. **Keepalive cadence is load-bearing.** Carrier NAT mappings expire far faster
   than home-router mappings. A heartbeat tuned for a normal network will lose
   the path silently. The interval must be derived from measurement, not
   assumed, and it interacts with the parent's section 12.1 TX watchdog deadline.
4. **Cached-address reconnect degrades for these stations.** A rotating carrier
   prefix means the cached address is usually stale, so those operators depend
   on the rendezvous far more than the parent assumes.
5. **Reachability diagnostics must state the truth bluntly.** "Your carrier does
   not permit direct connections, you will be relayed" is correct and useful.
   Sending that operator to configure a port forward is worse than useless.
   Parent section 10.7 already identifies this population as the target case.
6. **Direction asymmetry must be documented for operators.** A client on a
   restrictive carrier reaching a station on ordinary internet is the easy case,
   because the client dials outward. A station behind a restrictive carrier is
   the hard case. **The station's connection matters far more than the
   client's**, and that guidance belongs in user documentation.

### 9.2 Open measurement

Whether a T-Mobile IPv6 pinhole opens for a **simultaneous** outbound from both
ends is unknown. "Unsolicited inbound is blocked" does not by itself mean
"blocked after we have sent outbound". If it opens, IPv6 hole punching may
succeed on a carrier where port forwarding never can.

A STUN-based NAT classification from a T-Mobile connection answers this in well
under an hour, and the answer directly sizes required relay capacity. **This
should be run before the transport phase is planned.**

---

## 10. What this costs

### 10.1 New dependencies, both gated on licence review

| Dependency | For | Licence question |
| --- | --- | --- |
| ICE / DTLS stack, `libdatachannel` the leading candidate | Traversal and data channels | MPL-2.0 plus libjuice LGPL against GPLv2-or-later. Already parent section 17 item 1 |
| Password-authenticated key exchange library | The section 4.3 pairing code | Unresolved, and new with this document |
| `coturn` | STUN and TURN | Server-side only, not linked into our binaries |

**Both licence reviews must complete before any of this is planned.** A failure
on either roughly doubles the corresponding piece of work: the ICE fallback is a
hand-rolled implementation over Qt's `QDtls`, and a PAKE fallback means either
writing one, which is unwise, or dropping section 4.3 to LAN and manual pairing
only.

### 10.2 New components

- Signalling service, small, over existing Qt WebSocket infrastructure
- Packaging and documentation for self-hosting introduction and relay
- Station discovery, multicast on both stacks
- Pairing state machine, code display, and window management
- Paired-device store, plus a Setup page listing devices with revoke
- Key generation, storage, and backup prompting
- Certificate handling, already parent R2

### 10.3 Honest scope note

The parent document allots one sentence to this. It is more than a sentence of
work, and it should appear in the phase plan as its own unit rather than as a
footnote inside R2. That is a consequence of the three requirements in section
1, not scope creep.

---

## 11. End-to-end operator workflow

**First run.** Flash a card, connect the Pi to the same network as the radio,
power it. The daemon discovers the radio exactly as the client does today,
connects, generates its key, names itself from the stored callsign, and waits.
The operator has performed one action.

**Claiming.** Open the client on the same network. The station appears under
"Stations on this network", marked unclaimed. Click Pair. The client offers to
copy station settings up to the daemon so bands, modes, profiles, and PA
calibration travel with it. The pairing window closes.

**At home.** Open the client, click the station under "Your stations". The
client notices it is on the same LAN and connects directly. The local direct
radio connection is released first, with a visible explanation, because the two
must never hold the radio at once (parent section 4.4).

**Away.** Identical interaction. The client tries the cached address, then the
rendezvous, and connects directly if it can. The current path is shown in the
status bar.

**Adding a phone.** On the paired laptop, "Add a Device". A code appears. Type
it on the phone. The radio is not involved.

**A station out of reach.** SSH in once, read the code from the log, and pair
from home with "Add a station by code".

**Losing a laptop.** From the phone: Setup, Devices, Revoke. A live session is
dropped immediately.

**Link failure while transmitting.** The daemon unkeys immediately on its own,
per parent section 12.1. The radio keeps receiving and station state survives.

**Rendezvous outage.** Running sessions are unaffected. New connections use the
cached address or a typed address.

**Card failure.** The key was the station. New card, pair again. A backed-up key
file avoids this, and the client prompts for that backup.

**Everything lost.** A console command returns the station to unclaimed.

---

## 12. Testing

**Unit.** Label parsing including case folding, length, and character rules.
Pairing state machine across window open, closed, reopened, and first-pair
transitions. Code generation, single use, and burn-on-wrong-guess. Device store
add, list, revoke, and revoke-while-connected. Address parsing for hostname,
IPv4, bracketed IPv6, and optional port.

**Security.** Pairing to a station that has already been claimed is refused.
An unpaired key cannot connect. A revoked key cannot reconnect. A wrong code
consumes an attempt. A manual address to a machine with a different key fails to
authenticate rather than connecting.

**Integration.** Client and daemon in one process, full pair, connect, revoke,
reconnect cycle.

**Loopback.** Separate processes on one machine, exercising discovery, pairing,
and both address paths.

**Network bench**, extending the matrix the parent names at section 14:

- Pair on the LAN, one click
- Pair by code across the internet
- Pair by manual key copy
- Connect on the LAN by discovery
- Connect away by cached address
- Connect away by rendezvous
- Connect by typed hostname, typed IPv4, and typed bracketed IPv6
- Direct connection established, verified as not relayed
- Relay fallback, verified as relayed
- **Background upgrade from relay to direct mid-session**
- Self-hosted rendezvous, both introduction and relay
- Rendezvous outage with a session running, and with a new connection
- Revocation dropping a live session
- Station behind a restrictive carrier, both directions
- **STUN classification of a T-Mobile connection** (section 9.2)

---

## 13. Open items

1. **PAKE library selection and licence review** (section 10.1). Gates
   section 4.3.
2. **`libdatachannel` licence review**, inherited from parent section 17 item 1.
   Gates the transport phase.
3. **T-Mobile NAT classification** (section 9.2). Sizes relay capacity and
   decides whether IPv6 hole punching is viable on restrictive carriers.
4. **Keepalive interval** (section 9.1 item 3), to be measured rather than
   assumed, and reconciled with the parent's TX watchdog deadline.
5. **Where the settings copy in section 11 draws its boundary.** It depends on
   parent section 6.3, which the parent itself records as not fully settled.
6. **Default rendezvous hostname, certificate strategy, and operational
   ownership.** Not a code question, but it blocks packaging.
