# Issue 28 - Thunderbolt networking between 98 SE and Windows 11: the link up at 20 Gb/s and no traffic, then lost after a re-plug at the Windows end

Status: **fixed on branch `w48-net`, not yet merged** (roadmap-usb4
Phase 48, the owner's bench of 2026-11-09). All three causes are fixed:
Windows 11's Login Response taken whatever its Request ID (`27b06dbd`,
design record 35 revision 20), and after a re-plug at the Windows end the
firmware's paths torn down at the disconnect, as Linux does, so the
firmware announces the peer again (`c9b30c64`, revision 23, section 3.17;
read on the bench in round 7). The changes of `20755bf6`, `38144519` and
`ba9bac2d` (revisions 20 to 22) stay as fallbacks, and an unannounced peer
now stops short of a link that carries nothing (revision 23, section
3.16).

**Scope.** Thunderbolt networking (`xhcinet.sys` above `xhcicm.sys`'s
network device, design record 35) on a Thunderbolt 3 host whose firmware
runs the connection manager (Intel Alpine Ridge, `8086:15BF`, the ICM),
cabled straight to a Windows 11 computer whose own connection manager and
inbox "USB4(TM) P2P Network Adapter" (`USB4P2PNetAdapter.sys`) answer it.
Read on the owner's ThinkPad P14s Gen 1 under Windows 98 SE with
`3.4.2.0`, and the owner's Framework 13 (12th generation Intel) under
Windows 11. Other Windows 11 peers, the software connection manager's
hosts and the other xHCI98 targets are not read against Windows 11 on real
hardware.

## 1. What was seen

The owner connected the two computers with a Thunderbolt cable. Windows
11's Advanced network settings showed the USB4 P2P network adapter up at
20 Gb/s, but its sent and received bytes stayed at zero, Wireshark on the
adapter showed no frame at all, and a ping failed both ways. On 98 SE
`winipcfg` gave the xHCI98 Thunderbolt Networking adapter a 169.254
address.

The same pairing had passed in the emulator for `3.4.2.0` (run-47, 47.44:
Windows 11 Pro 26100 on `usb4-generic`, cabled to 98 SE on the P14s
model): discovery, login, ping and TCP both ways. This was its first
reading on real hardware.

## 2. Why 20 Gb/s and not 40

The link is a Thunderbolt 3 link of two lanes. Two computers bond them
only if both connection managers agree (USB4 Inter-Domain Specification
section 3.4.1). The Alpine Ridge firmware answers the far side's Link
State Status Request itself with Error Response Not Supported, so a
software connection manager on the far side never bonds and disables lane
1 (design record 35 section 3.11, read from the P14s captures with Linux).
Windows 11's 20 Gb/s matches that. It is the firmware's, not this
driver's, and it is not the fault here.

## 3. The first reading, and why it could not settle the cause

XHCITR's report from 98 SE (`3.4.2.0`) showed:

- discovery done both ways: Windows 11's properties block read
  (`cm.xdp.peer`; vendor `045Eh`, device `083Fh`), and the adapter on
  Windows proves Windows read this domain's;
- this driver's Logins never answered (`cm.net.login.none` after a minute,
  then one every 5 s), each acknowledged by the firmware with its one-DW
  code-8 answer (logged as `tbt.ring.rx-crc=0000000C`, the ICM response's
  PDF; record 35 section 3.11);
- `cm.ring.xdomain` 144: about 130 inter-domain frames from Windows beyond
  discovery, every one dropped by the inter-domain engine with nothing
  logged.

A release build logged none of the engine's drop paths or counters, so no
log level could say which drop it was. A bench-only release-flavour
`xhcicm.sys` with that logging was built (`a168a650`, record 35 revision
18, section 3.12), with batch files for both computers so the owner ran
three plug orders with almost no typing: the cable in after 98 SE was up,
the cable in while 98 SE started, and a pull and re-plug at the Windows
end.

## 4. Cause 1: Windows 11 does not echo the Login's Request ID

The diagnostic run's drops were all of one kind (`cm.xdp.drop=01077301`:
a USB4NET Login Response) with `cm.xdp.drop.id` running 08h, 09h ... 17h,
while this driver's Login Request IDs start at 1. Windows 11 answered
every Login with a Login Response, but numbered the response from its own
counter (it had sent eight Logins of its own, each answered) instead of
copying the request's ID, which the Inter-Domain Specification asks of a
responder. This driver matched a Login Response by its ID and dropped
every one, so its half of the login never finished and no path was built,
while Windows' half was done: hence the adapter up with nothing to send
on. Linux's `thunderbolt-net` copies the ID in its own responses and
checks none in the responses it receives (facts only), which is why the
emulator's Linux and xHCI98 peers never showed it; the emulator's Windows
11 guest happened to answer with IDs in range in 47.44.

The second plug order passed by chance: Windows' number fell inside the
range this driver accepted.

**Fix (`27b06dbd`, record 35 revision 20, section 4.5).** A Login
Response is matched the way a Logout Response already was: by type,
route, protocol UUID and the two domains' UUIDs while this driver's Login
is outstanding. A foreign ID is counted and logged once a login
(`cm.net.login.id`). Read on the bench (round 2): the first and second
plug orders ping 4/4 both ways at about 15 ms, the Windows adapter's
counters moving.

## 5. Cause 2: no new announcement after a re-plug at the Windows end

In the third plug order the firmware reported the disconnection and this
driver ended the peer's record cleanly. After the re-plug, requests from
Windows arrived on the same route, but the firmware sent no XDomain
connected event again, so no record matched them (`cm.icm.xd.nopeer`);
Windows gave up and removed its adapter.

**First change (`27b06dbd`):** a request from a direct link with no record
asks the firmware for another driver ready, which elsewhere makes it
announce again (at most once in 10 s, six times a route). Round 2: the
asks went out, the firmware answered with its ready flags and nothing
else.

**Second change (`38144519`, revision 21, section 3.14):** on the reading
that the eleven requests were Windows' UUID Request and its ten resends
(IDS 3.1), a UUID Request with no record is answered with this domain's
UUID, cached from the firmware's last connected event, as Linux answers
it at domain level. Round 3: no UUID Request came at all.

## 6. Cause 3: Windows asks for the properties of a peer it remembers

Round 3's log names the packets (`cm.icm.xd.nopeer.type`): Properties
Read Requests (type 3), sequence numbers 0 to 3 with their resends, and
no UUID Request (`cm.icm.xd.nopeer.uuids` 0). Windows 11 keeps the peer's
UUID across the re-plug and goes straight to reading its properties,
while the Alpine Ridge firmware, which announced the peer at the first
connection, does not announce it again. Nothing in this driver can answer
a properties request, or build paths through the firmware, for a peer it
has no record of. Under study on `w48-net`: answering such a request from
this domain's properties block, a record built from what the request
carries, and what the firmware needs to set the paths for a peer it never
announced.

**Round 3's change (`ba9bac2d`, revision 22, section 3.15)** answered
such a request with a record of its own, logged in both ways and sent the
approve, which the firmware answered without its error flag; rounds 4 to
6 found no frame crossing either way. So the peer was found and the paths
were not built.

**The owner's Linux run on the P14s** (the same firmware, the same
Framework) recovered after a re-plug at either end, the firmware
announcing the peer afresh. Linux's `thunderbolt-net` tears the
firmware's paths down at the end of a logged-in peer, the disconnect
included (`tbnet_tear_down`, `tb_xdomain_disable_paths`,
`icm_fr_disconnect_xdomain_paths`: the NHI mailbox, data 1 then 2; facts
only). This driver sent nothing there, taking the firmware's
disconnected event as the paths gone.

**Fix (`c9b30c64`, revision 23, section 3.17).** The paths of an approved
record are torn down at every end, the firmware's own included
(`cm.net.teardown.gone`), with no Logout into the dead link. Read on the
bench (round 7, plug order 3 with a 10 s gap): the teardown answered,
a fresh XDomain connected event, both logins, the paths, and ping 4/4
from the Framework after the re-plug. Should the firmware still not
announce a peer, section 3.16 keeps the link down and the xHCI98 USB
Tree says "found, not connected", instead of a link up with no traffic.

## 7. Rules kept

- **A peer that passed in the emulator is not a peer read.** The emulator
  runs real Windows and Linux guests, but a host firmware model and one
  guest's numbering are not the real machine's; Windows 11 against a real
  firmware connection manager differed in two places the emulator could
  not show.
- **Never match a response by the peer's echo of a field the
  specification asks it to echo** when the route, the protocol and the
  two UUIDs already identify the exchange; a peer's deviation should cost
  a log line, not the link.
- **Every drop on a protocol path is counted and the first few logged** in
  the release build (record 35 section 3.12), so a user's report can name
  the cause without a special build.
- **A bench reading is scripted.** The owner ran three rounds of three
  plug orders through batch files and one PowerShell script, which also
  found a wrong driver path in the kit (`SYSTEM` instead of
  `SYSTEM32\DRIVERS`) on its first run.

## Sources

- Design record 35 (`docs/contributing/design/35-thunderbolt-networking.md`)
  sections 3.11 to 3.17 and 4.5, revisions 18 to 23; on branch `w48-net`
  until merged.
- Commits `a168a650`, `27b06dbd`, `20755bf6`, `38144519`, `ba9bac2d`
  and `c9b30c64` on `w48-net`.
- `docs/contributing/runs/run-47.md`, "47.44 Thunderbolt networking
  interop"; `docs/contributing/runs/run-48.md`, the owner's bench of
  2026-11-09 (when the branch's run section is folded in).
- The owner's XHCITR reports from 98 SE and the Windows 11 collections of
  each round (rounds 1 to 3), held on the maintainer's disk (git-ignored).
- USB4 Inter-Domain Specification, sections 3.1 and 3.4.1, and 5.1.1 (the
  Login exchange); Microsoft Learn, "USB4 inter-domain connections".
- Linux `drivers/net/thunderbolt` and `drivers/thunderbolt/xdomain.c`,
  facts only.
