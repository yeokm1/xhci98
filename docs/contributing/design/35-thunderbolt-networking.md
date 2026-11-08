# Design Record 35 - Thunderbolt Networking: a Host-to-Host Link

Status: revision 17, 2026-11-08: the 98 SE and ME install of the
network adapter copies nothing and Setup places `xhcinet.sys` itself
(sections 8.2 and 9.5), the owner's decision of that day, reviewed and
revised.
Revision 16, 2026-11-08: the service comes up again after a
replug on an ASM4242 host router (section 5.1, record 22 section 18.9),
reviewed and revised.
Revision 15, 2026-11-08: no end-to-end flow control offered on
an ASM4242 host router (section 5.3), reviewed and revised.
Revision 14, 2026-11-08: Windows 11's inbox USB4 networking
read in the emulator (section 2.5) and the receive IPv4 fragmentation
it asked for (section 7.4), reviewed and revised.
Revision 13, 2026-11-01: the packaging and the installer as
built, steps S10 and S11 (section 9.5, with section 9.3 amended),
reviewed and revised.
Revision 12, 2026-11-01: the early UUID answer held until this
driver has cleared lane 0's Lock (section 3.11), reviewed and revised.
Revision 11, 2026-11-01: the rings' common buffers asked for in
whole pages, read on Windows 2000 and XP (section 6.5, "The rings"),
reviewed and revised.
Revision 10, 2026-10-31: the bench capture (section 2.7) weighed
against the code (section 3.11), reviewed and revised.
Revision 9, 2026-10-31: the driver half joined to the firmware
connection manager (section 6.5, "The two connection managers"), the
software connection manager's link lowered at this host's own Logout
(section 3.9), the network INF's models decoration (section 8.2),
reviewed and revised.
Revision 8, 2026-10-31, task 45-E.15: the owner's bench capture
of an Alpine Ridge firmware CM and a USB4 software CM, both Linux, read
(section 2.7, with pointers in sections 2.4, 3.4 and 10), reviewed and
revised. Revision 7, 2026-10-31, step S5: the service's driver half in
`xhcicm.sys` as built (section 6.5), reviewed and revised.
Revision 6, 2026-10-31, step S4: the firmware connection
manager's half as built (section 3.10), reviewed and revised.
Revision 5, 2026-10-31, step S3: the software connection
manager's half as built (section 3.9), reviewed and revised; an
emulator observation of Linux's end-to-end flow control (section 2.4
point 9), the emulator's firmware-CM model choice (section 10, E5) and
its SOF PDF on a short frame (section 5.3).
Revision 4, 2026-10-31, step S0: Linux's `thunderbolt-net` read
(section 2.4) and the record amended to it (sections 3.2, 4.1, 4.4, 7.2
to 7.4, receive segmentation). Revision 3, the owner's answers of the
same day in section 15; revision 2, before code, reviewed and revised (the
discovery and service, the paths and rings, the interface and the
network drivers, the INFs, installer, emulator and test plan, each in
turn), with the owner's `XhciCmNet` gate of 2026-10-31 (section 8.1).
Revision 1, 2026-10-31, before code. Written for
`roadmap-usb4.md` task 45.59 (the owner, 2026-10-31: reinstated whole in
release `3.4.0.0` after its withdrawal of 2026-10-30, when it was task
45.43), with the owner's decisions of 2026-10-31 on its installer box
(task 45.60), its packaging and its order folded in (sections 9 and 14).
Steps S1 to S4 are in `src\cm\` (`tbt_xdp.c`, ring 0's XDomain sink
in `tbt_ring.c`, `tbt_u4cm.c`'s inter-domain section and `tbt_icm.c`'s);
the rest is not yet written. Section 15 lists the owner's questions;
section 16 what is open and what is a risk; section 14 the steps a later
worker codes it in.

What it is: two computers joined by one Thunderbolt 3 or USB4 cable, each
seeing the other as an Ethernet adapter. The connection manager of each
computer (`xhcicm.sys`, design records 21 and 22) finds that the cable
leads to another host rather than to a device, exchanges identities and
property blocks with that host's connection manager over the inter-domain
discovery protocol, logs in to its Ethernet service, and builds a pair of
DMA paths between the two host interfaces (NHIs). A network driver of this
project's own, an NDIS 5 miniport on 98 SE to 7 and an NDIS 6 one on 8 to
10, carries Ethernet frames over those paths in the USB4NET format, which
is the format Linux's `thunderbolt-net` uses, so a Linux computer is a
peer, and, by Linux's own documentation, a Windows or macOS one.

Tags:

- **[ids]** the USB4 Inter-Domain Service Protocol, Version 2.0, Rev. 1.0,
  November 2025 ("IDS"), by section, table or figure.
- **[spec]** the USB4 Specification 2.0, November 2025 ("Spec"), and the
  USB4 Connection Manager Guide 2.0, November 2025 ("Guide"), as design
  record 22 cites them. All three are in `docs/references/usb4_202511/`
  (`docs/references/README.md`).
- **[linux]** Linux's `drivers/thunderbolt` at the pinned mirror commit
  (`external/README.md`: `c6859eed`), read for facts only under
  `AGENTS.md`, "Reference Implementations" (GPL-2.0-only, nothing adapted),
  cited by file and symbol; and Linux's `Documentation/admin-guide/
  thunderbolt.rst` as `usb4-emu` carries it (`docs/references/
  linux-thunderbolt/`, its `COMMIT.txt`). Unconfirmed until a capture of
  this project's own shows it.
- **[tbnet]** Linux's `drivers/net/thunderbolt` at the same pinned
  commit, fetched by step S0 (section 2.4), read for facts only, cited by
  file and symbol; nothing adapted. Unconfirmed until a capture of this
  project's own shows it, as every [linux] fact. Revision 2 marked these
  points `[tbnet-owed]`; revision 4 settles each.
- **[emu]** `usb4-emu`, read in place and never modified (`main` at
  `1bfa0bb`; its host-to-host task, written as `9c5e88a` and withdrawn in
  `90aad1c`, read with `git show`).
- **[code]** this tree's source at `9ca3cca1`.
- **[bench]** the owner's hardware captures of 2026-10-31 (task 45-E.15,
  section 2.7; `runs/run-45.md` section "45-E.15").
- **[inf]** an inference: a hypothesis the design is written to tolerate
  being wrong, and that a named leg or reading settles.

## 1. What it is for

### 1.1 The problem

A Windows 98 SE or 2000 machine has no way to move files to a modern one
except a network adapter or removable media, and many of the machines this
project serves have a Thunderbolt 3 or USB4 port and no wired Ethernet.
Both operating systems already run this project's connection manager on
that port (records 21 and 22). Thunderbolt networking turns the cable
itself into a point-to-point Ethernet link at the speed of the host
interface, with the operating system's own TCP/IP, file sharing and tools
above it.

### 1.2 Scope

In:

- The inter-domain discovery protocol in `xhcicm.sys`: finding that a
  port leads to another domain, the UUID exchange, the properties
  exchange in both directions, answering the peer's requests, the
  properties-changed notification, the link-state messages that bond the
  two lanes, and the protocol's termination (IDS chapters 2 to 4; Guide
  3.3 and 5.7) - under the USB4 software connection manager (record 22)
  and under the Thunderbolt 3 firmware connection manager's own
  inter-domain events and commands (record 21; IDS chapter 6).
- The USB4NET service: its `network` directory in the properties block,
  login, logout and their retries (IDS 5.1.1 to 5.1.4), the HopIDs, and
  the two DMA paths and two descriptor rings per peer (Guide 5.7; Spec
  chapter 12; under the firmware connection manager its approve and
  teardown commands).
- The network drivers: `xhcinet.sys`, an NDIS 5.0 miniport for 98 SE, ME,
  2000, XP, Vista and 7, x86 and x64; `xhcinet6.sys`, an NDIS 6.20 miniport
  for 8, 8.1 and 10, x86 and x64. The interface by which either binds to
  `xhcicm.sys`; the frame format, MTU, checksums and segmentation; link up
  and down on plug, unplug, a disable and an eject; power.
- The INF of the network drivers, every registry value both drivers read
  written by the install at its default, with the INF-gate and
  `tuning.md` rows (the owner's rule of 2026-10-30, record 21 section 8.1).
- The installer's box (task 45.60's page, the owner's decision of
  2026-10-31: unticked by default), the console and silent switches, and
  the packaging in `cm\tbnet\` of each family folder of task 45.44's
  layout (the owner, 2026-10-31).
- A host-to-host model in `usb4-emu`, as requirements for that repository
  (section 10), and the q35 legs against Linux's `thunderbolt-net`.
- The test plan, the security implications and what stays unsupported.

Out: section 13, each with where it goes.

### 1.3 How it fits records 21 and 22

Nothing changes in how either connection manager runs devices. The new
work hangs off three points each record already has: record 22's
depth-1 router read, which today marks a port whose router answers
ERR_ENUM or ERR_NUA UNSUPPORTED and leaves it alone ([code] `tbt_u4cm.c`
`tbtU4Router`, `TBT_U4_WHY_ENUM_ERROR`), and which is exactly the
Guide's signal that the port may lead to another domain (Guide 3.3); the
ICM client's event decoder, which today logs and ignores the two
inter-domain event codes ([code] `tbt_icm.h`, the out-of-scope list
naming host-to-host paths); and ring 0's frame dispatch, which today
knows control packets and ICM frames but not the inter-domain request
and response PDFs ([code] `tbt_regs.h`, `TBT_PDF_*`). The ring 0 owner
stays the connection-manager thread (record 21 section 4.4): every
discovery and login packet is built, sent and decoded there. What is new
is a second kind of ring, the data rings of section 5, which the thread
sets up and tears down but which the network driver's sends and the
receive service touch at DISPATCH_LEVEL under a lock of their own.

## 2. Facts

### 2.1 The inter-domain protocol [ids]

- **Packets.** An inter-domain packet is a control packet on HopID 0 with
  PDF 6 (request) or 7 (response) (IDS 2.1, Table 2-1; Spec 6.4.2.11 and
  6.4.2.12). After the route string (DW 1 and 2) comes DW 3 with the Data
  Size in bits 5:0 (the payload's DWs, CRC excluded) and a two-bit
  sequence number SN in bits 28:27 that a responder copies from the
  request; then a protocol UUID in DW 4 to 7 and, for the discovery
  protocol, a Packet Type in DW 8.
- **UUIDs in packets.** A UUID is written as four DWs with the bytes of
  each DW swapped (IDS 1.2): the discovery protocol's
  `B638D70E-42FF-40BB-97C2-90E2C0B2FF07` is DW 4 to 7 = `0x0ED738B6`,
  `0xBB40FF42`, `0xE290C297`, `0x07FFB2C0`. Read as a rule: DW n is the
  canonical form's bytes 4n to 4n+3 taken little-endian. The USB4NET
  protocol UUID `798F589E-3616-8A47-97C6-5664A920C8DD` is `0x9E588F79`,
  `0x478A1636`, `0x6456C697`, `0xDDC820A9` (IDS Table 5-3), which the rule
  reproduces.
- **Discovery packet types** (IDS Table 2-2, with Table 6-1): 1 the
  Thunderbolt 3 UUID Request, 2 UUID Response, 3 Properties Read Request,
  4 Properties Read Response, 5 Properties Changed Notification, 6
  Properties Changed Response, 7 Error Response, 12 UUID Request, 15 to 18
  the Link State Status and Change requests and responses, 19 Time Sync;
  the rest reserved. A UUID Response carries the sender's UUID (DW 9 to
  12) and, to a type-12 request, the sender's route string (DW 13 and 14);
  to a type-1 request it carries no route (IDS 6.1.3).
- **Error Response codes** (IDS Table 2-6): 1 Unknown Packet Type, 3 Not
  Supported, 4 Not Ready, every other value reserved. The table's text
  extraction interleaves its columns; this reading agrees with Linux's
  `enum tb_xdp_error` ([linux] `tb_msgs.h`), where 2 is Linux's own
  "unknown domain".
- **Timeouts.** A UUID Request, a Properties Read Request, a Properties
  Changed Notification, a Link State request and a Logout are each resent
  ten times one second apart before the step is abandoned (IDS 3.1, 3.2,
  3.3, 3.4.1, 5.1.2); a Login is resent for at least one minute, first
  often (about every 100 ms) and later about every second (IDS 5.1.1).
- **Discovery** (IDS 3.1 to 3.3, 3.7): on finding an inter-domain link
  the local CM sends a UUID Request; after the UUIDs, each side reads the
  other's properties block with Properties Read Requests at increasing
  DW offsets, every response but the last full-length, restarting from 0
  if the block's generation changes mid-read; a host whose block changes
  sends a Properties Changed Notification, and the peer reads again. A
  disconnect (signalled by a hot-plug event with UPG 1 on lane 0, the
  same event that ends a lane bonding, IDS 3.4.1, so section 3.8 confirms
  it by the port's state), the domain going to sleep, or a notification with a different source UUID ends every
  service with that peer and resets the protocol's state.
- **The properties block** (IDS chapter 4): a header DW `0x55584401`
  (version 1 under the fixed value `0x555844`), the root directory's
  length, then 4-DW entries (an 8-byte key, a type `v`, `d`, `t` or `D`,
  a size, a value or an absolute DW offset); directories carry a 4-DW UUID
  first. Required in the root: `vendorid` and `deviceid` immediates and
  `maxhopid` (the highest input HopID this host accepts on the link, 7
  bits) (IDS 4.3.4.2). Keys and text are packed four ASCII characters to
  a DW, the first in bits 31:24 (IDS Table 4-10's sample, `0x76656E64` =
  "vend"). IDS Table 4-10 is a complete 53-DW sample block; it is section
  11.1's first host vector.
- **USB4NET** (IDS 5.1): a host offers it with a `network` directory
  (`D` entry under key `network`) whose directory UUID is unique to it
  and which holds the immediates `prtcid` 1, `prtcvers` 1, `prtcrevs` and
  `prtcstns`. The sample's directory UUID DWs, `0xCA8961C6`, `0x9541CE1C`,
  `0x5949B8BD`, `0x4F5A5F2E`, are by IDS 1.2's rule the UUID
  `C66189CA-1CCE-4195-BDB8-49592E5F5A4F`. `prtcstns` (IDS Table 5-2,
  reading the interleaved columns as bit 0, bit 1, bit 2, bits 3 to 31):
  bit 0 end-to-end flow control supported; bit 1 the frame format with
  Frame ID supported, "shall be set to 1b"; bit 2 64 KB frames on
  transmit as well as receive; the rest reserved. IDS 5.1.6's example
  writes `0x00000003`.
- **Login and logout** (IDS 5.1.1, 5.1.2, 5.1.4): USB4NET packets are
  inter-domain packets carrying the USB4NET UUID in DW 4 to 7, the
  requester's and responder's UUIDs in DW 8 to 15, a type in DW 16 (0
  Login, 1 Login Response, 2 Logout, 3 Logout Response) and a request id
  in DW 17 that increments per request and is copied into its response. A
  Login carries the service revision 1 (DW 18) and the Service HopID (DW
  19): the HopID **this host transmits with**, which must not exceed the
  peer's `maxhopid`, or 15 when the peer's block has none (IDS 6.3.1.1.1,
  for a Thunderbolt 3 peer). A Login Response carries a status (0
  success; 1 to `0x7FFFFFFF` reserved and taken as failure; from
  `0x80000000` vendor failures), the responder's MAC address (DW 19 the
  most significant four bytes, DW 20 the rest) and its length, typically
  6. A responder that already has a login with another requester fails
  the new one. Each side logs in to the other: a CM may enable its
  inbound path once it has sent a successful Login Response, its
  outbound one once it has received one, and both by the time both have
  happened. A Logout is answered with a Logout Response; a CM tears its
  receive path and ring down after sending a successful Logout Response,
  its transmit path and ring after receiving one; frames not yet posted
  are failed back to the stack or dropped.
- **The frame** (IDS 5.1.5): each USB4 frame on the data ring is a 12-byte
  header in little-endian - Frame Size (32 bits: the bytes of Ethernet
  data in this frame), Frame Index (16: which frame of this packet, from
  0), Frame ID (16: incremented by one for each frame sent, wrapping at
  `0xFFFF`), Frame Count (32: the packet's number of frames) - followed by
  the Ethernet bytes in network order. A packet of up to 1518 bytes is one
  frame with Index 0 and Count 1; a larger one, up to 64 KB, is split
  into 4 KB frames, the last possibly shorter, each with its own header
  (IDS 5.1.5.2, Figure 5-7). Every CM "shall receive" large frames; one
  should not send them to a peer whose `prtcstns` bit 2 is 0. Tunnelled
  USB4NET packets use SOF PDF 1 and EOF PDF 2 (IDS 5.1.5).
- **The MAC address** (IDS 5.1.5.3): an algorithm derives a
  locally-administered unicast MAC from the host router's 64-bit UUID
  (Router Configuration Space), its router id and the USB4 port number.
  The PDF's pseudo-code interleaves two columns in text extraction; step
  S1 transcribes it from the PDF page (IDS page 39 and 40) into a pure
  function with a host vector computed by hand from that page.
- **Flow control** (IDS 5.1.5.4): per-link dedicated flow control on the
  inter-domain link; end-to-end flow control on both rings when both
  peers advertise it.
- **Lane bonding** (IDS 3.4.1, Appendix A): at Gen 2 or 3, if both ports
  can bond, the domain with the lower UUID (compared byte by byte in
  canonical form) asks the other to set its Target Link Width to x2
  (Link State Change Request), checks with a Link State Status Request,
  sets its own lanes and bonds; the higher one answers and waits for the
  hot-plug event with UPG 1. A failure disables lane 1 (Guide 7.5.2). At
  Gen 4 the link comes up bonded, symmetric or asymmetric, and a CM never
  starts an asymmetric transition there (IDS 3.4.2).
- **Time sync** (IDS 3.5): optional; a CM that omits the `timesync` entry
  has it disabled on the link by its peer.

### 2.2 The host interface for a data service [spec]

- **Rings.** A host interface has N >= 3 rings each way; ring n's
  transport packets carry HopID n at the host interface adapter (Spec
  chapter 12, Guide 5.7.1 and 5.7.2 notes). Ring 0 is raw-mode only and
  the connection manager's (Spec 12.1). Ring 1 is used for compliance
  testing "but can be used for other purposes" (Spec chapter 12). Total
  Paths is Host Interface Capabilities bits 10:0 (Spec Table 12-11).
- **Frame Mode** (Spec 12.1.3, 12.3.2.1, 12.4.2.1): a frame of 1 to 4096
  bytes, segmented into tunnelled packets by the hardware with the SOF
  PDF on the first, the EOF PDF on the last, and a 32-bit CRC over the
  frame, checked by the receiving host interface (the CRC Error bit of
  the receive descriptor; Data Length then not to be trusted). A receive
  descriptor's buffer holds one whole frame; Data Buffer Size is in the
  ring's size register, 0 meaning 4096 (Table 12-25); a frame larger than
  the buffer sets Buffer Overflow (Table 12-8). The receive ring's PDF
  Bit Masks register names which PDFs start and end a frame (Table
  12-27): EOF bitmask bit i for PDF i, SOF bitmask bit 16+i.
- **Ring Control** (Spec Tables 12-21 and 12-26, the interleaved columns
  read with Linux's `nhi_regs.h` `RING_FLAG_*`): bit 31 Ring Valid, 30
  Raw Mode, 29 No Snoop, 28 E2E Flow Control Enable; on a receive ring
  the Transmit E2E HopID in bits 18:12 (Linux masks 22:12,
  `REG_RX_OPTIONS_E2E_HOP_MASK`; with HopIDs below 128 the two agree). A
  transmit ring is enabled by one DW write of the whole register (Table
  12-21's note); a ring is disabled by writing every field to its
  default and then Base Address and Ring Size (Guide 5.7.3).
- **Descriptors** (Spec 12.3.1 and 12.4.1): 16 bytes; a transmit
  descriptor carries Data Length (11:0, 0 = 4096), EOF PDF (15:12), SOF
  PDF (19:16), Descriptor Done (21), Request Status (22), Interrupt
  Enable (23) and an Offset (31:24); a posted receive descriptor RS 1 and
  IE, the written-back one Data Length, the PDFs, CRC Error (20), DD (21)
  and Buffer Overflow (22). A ring keeps one slot empty between producer
  and consumer (Tables 12-19 and 12-24).
- **End-to-end flow control** (Spec 12.2): credits per receive descriptor
  posted, granted by the receiving host interface in Credit Grant packets
  every tE2ERate (1 to 1000 ms) and whenever descriptors are added, and
  resynchronised by Credit Sync packets every tE2ESync (10 to 20 s); both
  ends of a host-to-host path must use the same scheme. All of it is the
  host interface's work; the CM sets the E2E bit and the Transmit E2E
  HopID.
- **Interrupts** (Spec 12.5): a receive ring's Receive Data Buffer cause
  and its vacancy cause; INTx emulation optional on a USB4 host. Record 21
  section 4.5 already connects the line for ring 0's cause on NT behind
  `XhciCmRingInterrupt` (0 by default).
- **Path priority**: the Guide recommends priority 4 for host-to-host
  paths (Guide Table 6-1) and, on the segment from a lane adapter to the
  host interface adapter, EFC and ESE 0 (Guide 5.7 step 7).

### 2.3 What Linux's connection manager does [linux]

- **Detection under its software CM**: a device-router allocation that
  fails with an I/O error at the port's route is taken as another domain
  and an XDomain object is made at that route (`tb.c` `tb_scan_port`,
  `tb_scan_xdomain`), the downstream port unlocked first
  (`xdomain.c` `tb_xdomain_alloc`), the port marked inter-domain
  (`usb4.c` `usb4_port_configure_xdomain`, PORT_CS_19 bit 4
  `PORT_CS_19_PID` in `tb_regs.h`; `lc.c` `tb_lc_configure_xdomain` on a
  Thunderbolt 3 router).
- **Under the firmware CM**: the ICM announces the peer with event `0x06`
  and its loss with `0x07`, the remote and local UUIDs and both route
  strings in the announcement (`tb_msgs.h`
  `icm_fr_event_xdomain_connected`, `icm_tr_event_xdomain_connected`;
  `icm.c` `icm_fr_xdomain_connected`, `icm_tr_xdomain_connected`), so
  Linux skips its own UUID exchange there (`xdomain.c`
  `tb_xdomain_alloc`, `needs_uuid` false). Paths are approved with ICM
  command `0x10` carrying the transmit and receive HopIDs and rings
  (`icm_fr_pkg_approve_xdomain` keyed by link info on Falcon and Alpine
  Ridge; `icm_tr_pkg_approve_xdomain` keyed by route on Titan Ridge and
  Ice Lake), and torn down on Falcon and Alpine Ridge by NHI mailbox
  `0x10` or `0x11` (port A or B) written with data 1 then 2, and on Titan
  Ridge and Ice Lake by ICM command `0x11` in two stages (`icm.c`
  `icm_fr_disconnect_xdomain_paths`, `icm_tr_disconnect_xdomain_paths`;
  `nhi.h` `NHI_MAILBOX_DISCONNECT_PA`, `_PB`). Linux refuses a second
  tunnel to one peer under the firmware CM ("only one tunnel is supported
  by the firmware", `icm_fr_approve_xdomain_paths`). `usb4-emu`'s
  `CONTROLLER-CATALOG.md` records the same split per generation [emu].
- **The discovery state machine** (`xdomain.c` `tb_xdomain_state_work`):
  INIT, UUID, LINK_STATUS, LINK_STATE_CHANGE, LINK_STATUS2,
  BONDING_UUID_LOW or BONDING_UUID_HIGH, PROPERTIES, ENUMERATED, ERROR;
  ten retries a state (`XDOMAIN_RETRIES`), 1000 ms between them
  (`XDOMAIN_DEFAULT_TIMEOUT`), 100 ms before the first
  (`XDOMAIN_SHORT_TIMEOUT`), 10 s for the bonding to complete
  (`XDOMAIN_BONDING_TIMEOUT`); a failed bonding step skips to the
  properties. A remote UUID equal to the local one is a loop and is not
  bonded. A UUID Request received in the ERROR state restarts the
  handshake (`tb_xdp_handle_request`).
- **The properties**: Linux accepts a block of at most 500 DWs
  (`TB_XDP_PROPERTIES_MAX_LENGTH`), takes at most 45 DWs of block per
  response (`TB_XDP_PROPERTIES_MAX_DATA_LENGTH`, the 256-byte control
  packet less the response's header and CRC), nests directories at most
  8 deep (`property.c` `TB_PROPERTY_MAX_DEPTH`), refuses an entry whose
  offset and length leave the block, and requires `vendorid` and
  `deviceid` immediates, taking 15 when `maxhopid` is missing
  (`xdomain.c` `populate_properties`, `XDOMAIN_DEFAULT_MAX_HOPID`). It
  publishes `vendorid` `0x1d6b` with the text "Linux", `deviceid`
  `0x0004` with the host name as text, `devicerv` `0x80000100`, and
  `maxhopid` from the facing lane adapter's Max Input HopID
  (`tb_xdomain_init`, `update_property_block`, `tb_xdomain_alloc`), and
  starts its generation at a random number. It answers a Properties Read
  Request whose destination UUID is not its own with error 2
  (`tb_xdp_properties_response`).
- **HopIDs**: an XDomain's input and output HopIDs are allocated from 8
  (`TB_PATH_MIN_HOPID`) up to the local `maxhopid` and the remote one
  (`tb_xdomain_alloc_in_hopid`, `tb_xdomain_alloc_out_hopid`). Data rings
  are allocated from ring 1, or from ring 2 on Falcon Ridge, where a
  receive ring with E2E has its Transmit E2E HopID forced to 1 (`nhi.c`
  `nhi_alloc_hop`, `RING_E2E_RESERVED_HOPID`; `pci.c` sets `QUIRK_E2E` for
  the two Falcon Ridge NHIs).
- **The DMA tunnel under its software CM** (`tunnel.c`
  `tb_tunnel_alloc_dma`, `tb_dma_init_rx_path`, `tb_dma_init_tx_path`;
  `tb.c` `tb_approve_xdomain_paths`): a receive path from the lane
  adapter to the NHI adapter and a transmit path the other way, priority
  5 and weight 1 (`TB_DMA_PRIORITY`, `TB_DMA_WEIGHT`), 14 credits a hop
  by default bounded by the router's `baMaxHI` (`TB_DMA_CREDITS`,
  `usb4.c` `max_dma_credits`), at least 1; the receive path's first hop
  takes the lesser of the lane's usable credits and that bound (its
  comment: nothing else is tunnelled over an inter-domain link, so it
  "can use all the credits"); CL states are disabled while the tunnel is
  up. Linux's priority 5 differs from the Guide's recommended 4.
- **Ring start in frame mode** (`nhi.c` `tb_ring_start`): Data Buffer
  Size 0 (4096); the receive ring's PDF masks from the service's SOF and
  EOF masks; the ring enabled first and the E2E bits (and the receive
  ring's Transmit E2E HopID) written in a second write of the register.
- **Networking, as documented** (`thunderbolt.rst`, "Networking over
  Thunderbolt cable", in `usb4-emu`'s copy): "currently we only support
  Apple ThunderboltIP protocol"; "If the other host is running Windows or
  macOS ... the `thunderbolt-net` driver is loaded automatically"; between
  two Linux hosts the module is loaded by hand on one side, which "triggers
  module load on the other host automatically"; one interface per
  Thunderbolt port. The document calls the service "USB4NET".

### 2.4 Linux's `thunderbolt-net`, read (step S0) [tbnet]

Fetched on 2026-10-31 at the pinned commit (`c6859eed`) into
`external/linux/net-thunderbolt/` (`external/README.md`, its row and
fetch lines with each file's SHA-256), and read for facts only:
`drivers/net/thunderbolt/main.c` is SPDX `GPL-2.0`, so nothing of it is
adapted, quoted or paraphrased into `src\`; each fact below is cited by
symbol and tagged **[tbnet]**, and is Linux's behaviour, unconfirmed on
hardware like every [linux] fact. The eight points revision 2 left open,
answered:

1. **Login.** Both sides log in: the driver starts its login 1 s after
   its interface opens; each try waits up to 500 ms for an answer, and an
   unanswered one is followed by the next 4.5 s later, so tries are about
   5 s apart, 61 in all (the first and 60 more) (`start_login`,
   `tbnet_login_work`, `TBNET_LOGIN_DELAY`, `TBNET_LOGIN_TIMEOUT`,
   `TBNET_LOGIN_RETRIES`). **It sends its Login and Logout requests as
   PDF 7**, the response PDF, not 6 (`tbnet_login_request`,
   `tbnet_logout_request` pass `TB_CFG_PKG_XDOMAIN_RESP` as the
   request's type), and it takes USB4NET packets of either PDF itself
   (`domain.c` `tb_domain_event_cb` hands both to
   `tb_xdomain_handle_request`), so a USB4NET packet is classified by its
   type, never by its PDF (section 3.2). It answers a Login only while
   its interface is up, and then always with status 0 - it never sends a
   failure (`tbnet_handle_packet`, `tbnet_login_response`, whose status
   field stays zero). Every Login it answers overwrites the HopID it
   records for the peer's transmit path (`remote_transmit_path`), while a
   Login answered with its carrier already on builds nothing new
   (`tbnet_connected_work` returns at once): so a repeated Login with a
   different HopID would leave its paths as they are and its teardown
   bookkeeping wrong, and this driver never sends one (section 4.1). It
   accepts a Login only when the packet's first UUID field is the peer's
   and the second its own, and its route is the peer's
   (`tbnet_handle_packet`), and it matches its own requests' responses by
   route and protocol UUID alone (`tb_xdomain_request`, `xdomain.c`
   `tb_xdomain_match`): neither the type, the request id nor the UUID
   fields of a response are checked. In every packet it sends, requests
   and responses alike, the first UUID field is its own and the second the
   peer's (`tbnet_fill_header` with `local_uuid`, `remote_uuid`). Its
   Login Response copies the request's id (`tbnet_login_response`); its
   **Logout Response** is the type it calls `TBIP_STATUS` (3, IDS Table
   5-4's Logout Response) with status 0 and a **new** id from its own
   counter, not the request's (`tbnet_logout_response`), and its Logout is
   sent with SN 0 (`tbnet_logout_request`), resent on timeout ten times
   1 s apart (`TBNET_LOGOUT_RETRIES`, `TBNET_LOGOUT_TIMEOUT`). On the
   peer's Logout it answers and tears its service down without a Logout
   of its own (`tbnet_disconnect_work`, `tbnet_tear_down` with
   `send_logout` false); it sends one when its own interface stops, the
   host shuts down or sleeps (`tbnet_stop`, `tbnet_shutdown`,
   `tbnet_suspend`).
2. **Its `network` directory**: registered at module load for every
   XDomain (`tbnet_init`, `tb_register_property_dir`), `prtcid` 1,
   `prtcvers` 1, `prtcrevs` 1 and `prtcstns` bits 1 and 2 always with bit
   0 while end-to-end flow control is on (its `e2e` parameter, default
   on): `0x7` by default (`TBNET_E2E`, `TBNET_MATCH_FRAGS_ID`,
   `TBNET_64K_FRAMES`). It binds to a peer's service of key `network`
   with `prtcid` 1 (`tbnet_ids`, `TB_SERVICE("network", 1)`).
3. **Frame ID is per Ethernet packet**, not per USB4 frame: every frame of
   one packet carries the same Frame ID, and the counter advances once
   per packet, and only when the peer's `prtcstns` has bit 1; it starts
   at 0 and stays 0 towards a peer without bit 1 (`tbnet_start_xmit`,
   `frame_id`, and the comment on `struct
   thunderbolt_ip_frame_header`). Its
   receiver takes a packet's first frame with Index 0 and a Count from 1
   to `MAX_SKB_FRAGS` + 1, and each later frame only with the same Count,
   the Index one more and the same Frame ID; a frame whose Frame Size is
   0 or larger than the data it carries, or that holds no more than the
   header, is dropped, and so is a frame with CRC Error or Buffer
   Overflow (`tbnet_check_frame`). IDS Table 5-7's text and Figure 5-7
   read the other way (section 2.1); this record follows Linux, the peer
   the roadmap line names.
4. **The wire carries no FCS and no padding**: the frame's data is the
   socket buffer from its destination address on (`tbnet_start_xmit`),
   which holds no FCS and is not padded to 60 bytes, and the receiver
   hands the data as it came to `eth_type_trans` (`tbnet_poll`). IDS
   Figure 5-6's "Network CRC" is not sent by Linux.
5. **Checksums are complete on the wire**: it offers the stack checksum
   offload and segmentation offload and then computes the IPv4 header,
   TCP and UDP checksums in software over the frames before posting them
   (`tbnet_probe`'s features, `tbnet_xmit_csum_and_map`); its receiver
   leaves the stack to verify them (`tbnet_poll` sets no checksum state).
6. **MTU and large frames**: its MTU ranges from 68 to 65522
   (`TBNET_MAX_MTU` 64 KiB less the Ethernet header, `tbnet_probe`), and
   its default is the Ethernet one, 1500. **It sends TCP segments of up
   to 64 KiB whatever the peer's MTU**: it offers segmentation offload to
   its stack and puts each large segment on the wire whole, split only
   into 4084-byte USB4 frames, leaving the receiving host to re-segment
   it where that host's stack cannot take it whole (`tbnet_probe`, its
   features and the comment above them); nothing in its transmit path
   reads the peer's `prtcstns` bit 2. Its receiver refuses a packet longer than 64 KiB
   (`tbnet_check_frame`, `TBNET_MAX_MTU`). Section 7.4 designs the split
   this record's receiver therefore needs.
7. **Its rings**: 256 descriptors each way (`TBNET_RING_SIZE`), the
   transmit ring and HopID allocated as the first free ones (ring from 1,
   HopID from 8, `tbnet_open`), the receive ring's Transmit E2E HopID the
   transmit ring's, end-to-end flow control on both rings when its
   parameter is on and the peer's `prtcstns` has bit 0, the SOF and EOF
   PDFs 1 and 2 (`TBIP_PDF_FRAME_START`, `_END`), interrupt throttling
   128000 ns (`TBNET_THROTTLING`); the rings are started and the receive
   ring primed before the paths are enabled (`tbnet_connected_work`).
8. **Its MAC** is not IDS 5.1.5.3's: the first byte is the physical port
   shifted left by 4 with the locally-administered bit, the rest two
   hashes of the local UUID (`tbnet_generate_mac`); it puts its MAC in
   its Login Response and never reads the peer's.
9. **Observed, not read** ([emu] with [linux]: a leg of `usb4-emu`'s
   two-host model, step S8, 2026-10-31): Alpine Linux 3.24.2's kernel
   (6.18.52) `thunderbolt_net` logged "enabling E2E for RX ring 1 with TX
   HopID 1" and enabled end-to-end flow control on its receive ring
   alone, not on its transmit ring, though both blocks' `prtcstns` read
   `0x7`. That peer therefore sends without end-to-end credits while it
   grants credits to this side's transmit ring; this side's receive ring
   must take frames from a peer sending with no credits while its own
   E2E is enabled (section 5.3). On hardware, Linux 7.0 enables E2E on
   both rings, as point 7 reads (section 2.7 point 3) [bench]: that
   kernel's sender waits for this side's grants.

### 2.5 Windows' own Thunderbolt networking

What is public: Linux's documentation says a Windows host is a peer of
`thunderbolt-net` with no action on the Linux side (section 2.3), and
`usb4-emu`'s Windows 11 capture of the P14s lists Intel's
`tbtp2pndisdrv.inf` ("host-to-host networking") among the Thunderbolt
packages installed there [emu] (`ICM-PROTOCOL.md` section 10.1). No
specification or open-source driver describes that driver's wire
behaviour beyond what USB4NET defines; under the hardware support rule
(owner, 2026-10-29) nothing here is designed from guesses about it. A
Windows peer is therefore expected to interoperate only in so far as it
follows IDS chapter 5, and it is read on the owner's bench (section
11.4), never claimed before.

Read in `usb4-emu` (revision 14, 2026-11-08) [emu]: Windows 11 Pro
26100 on the `usb4-generic` host, its inbox USB4 connection manager,
cabled to 98 SE on the Alpine Ridge firmware model. With nothing
installed, Windows bound its own "USB4(TM) P2P Network Adapter"
(`usb4p2pnetadapter.inf`) to this driver's service
(`USB4\VEN_0000&DEV_00000001&PROT_USB4NET`), up about 11 s after the
plug, reporting 20 Gb/s and an MTU of 62000, with a link-local
address and the Public network profile (its firewall then refuses an
inbound ping). Ping and TCP both ways passed; IPv4 datagrams above 98
SE's MTU were lost until section 7.4's receive fragmentation, or until
Windows' MTU was set to 1500 by hand. Microsoft documents the adapter
as its implementation of USB4NET and says it also works with a
Thunderbolt 3 peer; this reading is the emulator's, the owner's bench
(section 11.4) still owed.

### 2.6 This tree at `9ca3cca1` [code]

- `tbt_u4cm.c` `tbtU4Router` marks a depth-1 port whose router read
  answers ERR_ENUM or ERR_NUA UNSUPPORTED (`TBT_U4_WHY_ENUM_ERROR`); the
  depth-2 child read does the same (`tbtU4KidRouter`). Record 22 section
  1.3 lists inter-domain links among its "Out" items.
- `tbt_icm.c` decodes events 3 and 4 per row and logs the others; record
  21 section 1.3 lists host-to-host links as out.
- `tbt_regs.h` defines PDFs 1 to 5, 8 and `0xA` to `0xC`; nothing sends or
  accepts 6 or 7.
- `cm_nhi.c` is the NHI's function driver; `cm_entry.c` `cmAddDevice`
  decides between the NHI, the bridge filter and the child filter; no
  role of `xhcicm.sys` enumerates a child PDO.
- `cm_pool.c` is the one file the import gate admits pool and common
  buffer imports in (record 21 section 4.2).
- `src\xhci_cmiso.h` is the precedent for an interface between this
  project's drivers: a GUID, a version and an `INTERFACE`-headed table
  asked for with `IRP_MN_QUERY_INTERFACE` (task 45.35).

### 2.7 Read on hardware: an Alpine Ridge firmware CM and a USB4 software CM, both Linux (task 45-E.15) [bench]

These are the owner's captures of 2026-10-31. A ThinkPad P14s Gen 1
(Alpine Ridge LP, NHI `8086:15bf`, firmware CM) was joined by one cable
to a 12th-generation Intel laptop (Alder Lake-P, Linux's software CM).
Both ran Linux `7.0.0-30-generic` with `thunderbolt_net`, the driver's
debug messages and its control-packet trace events on. There were two
sessions: plug, ping, unplug, replug, ping; and a cold start of the P14s
with the cable in. `runs/run-45.md` section "45-E.15" cites each point by
file and line. The facts that change this record:

1. **The P14s's firmware is Alpine Ridge's**, so the `icm_fr` row of
   section 3.10 applies. The connected event (14 DWs, link info 1 in DW
   1 bits 31:16, the remote then the local UUID, the local route, then
   the remote route), the disconnected event (6 DWs) and the approve (8
   DWs, `0x00010008` for ring 1 and HopID 8 both ways) read exactly as
   that table has them. The approve's response repeats every word after
   the header.
2. **The firmware is not a pass-through on the link**, which section
   3.4's open point and section 10's E5 assumed:
   - About 10 to 14 ms after link-up it sends, by itself, one UUID
     Request of type 12 (SN 0) on the lane-0 route. Before that, on
     each lane's route, it sends a PDF 6 packet with protocol UUID
     `819DF55E-4355-4A9E-8E65-6CC3FD4D68D7`, which no IDS table names
     (DW 8 `0x101`, DW 9 to 12 its domain UUID, DW 15 the lane's
     adapter). Linux has no handler for that packet and does not answer
     it. This driver ignores other protocol UUIDs (section 3.6), which
     fits.
   - It sends its host the connected event about 4 ms after the far
     side's UUID Response arrives, not before. In the captures it did
     not repeat the request within the 374 ms that Linux took to answer.
   - It answers the far side's UUID Request itself, within 4 ms. Its
     Source Route has the low half in DW 13, IDS Figure 2-3's order,
     where Linux's software CM puts the high half first. Its host never
     sees the request.
   - It answers a Link State Status Request itself with Error Response
     Not Supported. A software-CM peer therefore never bonds a link to
     it: Linux disables lane 1, and the link stays x1 at 20 Gb/s.
   - It passes its host's Properties and USB4NET frames to the link
     unchanged, and the far side's to its host, with the CM bit set and
     the route kept.
   - For every inter-domain frame its host sends, it gives the host one
     1-DW ICM response `0x01000008` (code 8, not one of Linux's ICM
     codes). Linux drops each one. The one-for-one count held for 29
     frames, including one sent after the unplug. This driver's ICM
     client takes them as strays (`tbt.icm.stray`, counted): that is
     harmless, but it is log noise, and the code is not the approve's.
   - Before its host's driver runs (a cold boot with the cable in), it
     serves a properties block of its own: Linux read
     `vendorid` `0x8086`, `deviceid` 1, with no `network` directory.
     At the host's driver ready it sends a new UUID Request, and its
     host then sends a Properties Changed Notification, after which the
     far side reads the host's own block.
   - At an unplug its host gets the disconnected event. Linux sends one
     Logout into the dead link and no ICM command. The mailbox teardown
     cannot be seen in a trace, so it is not read.
3. **Linux 7.0 enables end-to-end flow control on its transmit ring as
   well as its receive ring**, on both connection managers and every
   plug ("enabling E2E for TX ring 1", "enabling E2E for RX ring 1 with
   TX HopID 1"). This is section 2.4 point 7's reading of the source.
   Point 9 (receive ring only) is the emulator leg's 6.18.52 kernel. A
   current Linux sender therefore waits for this side's credit grants:
   section 5.3's E2E on this side's receive ring is required whenever
   both blocks advertise bit 0, not merely tolerated. This side's
   receive ring must still take frames from an older kernel's sender
   that uses no credits.
4. **Login timing on hardware** confirms section 2.4 point 1:
   - The first try goes out 1.02 to 1.06 s after the interface opens.
   - A try that gets no answer ends after about 504 ms, and the next
     starts about 5.12 s after the last.
   - Logins go on PDF 7 with SN 0 and Service HopID 8.
   - Linux answers a Login only while its interface is up. On the cold
     boot the interface opened 45.58 s after boot, and the 8 Logins
     before it got no answer.
   - The peer's Login does not bring Linux's next try forward.
   - On the three hot plugs, link-up to traffic took 7.7 to 8.1 s. Most
     of it is the first Login lost because the far side's service was
     not yet probed, plus the 5.12 s wait for the next try. The cold
     boot is another case: the firmware brought the link up while the
     P14s was still in firmware, and traffic started 104.7 s later,
     paced by that host's boot and its interface opening.
   Section 4.1's faster schedule therefore cannot shorten Linux's own
   half. It only makes sure this side's half is done by the time Linux's
   next try arrives.
5. Linux's software CM side confirms the other section 2.3 points
   (`tb_dma_init_*`): the paths have priority 5, weight 1 and 14
   credits; the receive path runs from lane HopID 8 to NHI HopID 1 and
   the transmit path back; rings are ring 1, 256 descriptors. The Linux
   properties block is 55 DWs (45 and 10 per read), with `maxhopid` 19
   on the Alder Lake host and 15 under the firmware CM.
6. **A risk under this driver's software CM**: the firmware's one
   early UUID Request arrives while record 22's port machine is still
   reading the new port. `tbt_u4cm.c` `tbtU4XdTake` drops an
   inter-domain packet for a port that is not yet `TBT_U4_XDOMAIN`, or
   UNSUPPORTED by the UUID step. Linux answers a UUID Request at domain
   level from the domain's UUID, whatever the port's state
   (`xdomain.c` `tb_xdp_handle_request`). If the firmware never asks
   again, this host's own UUID Request is answered by the firmware, but
   the far host may never be told about this one. This is open for
   task 45.59: answer a UUID Request on a lane-0 route whose link is up
   before the port is classified, or show on the bench that the firmware
   asks again (section 11.4).
7. Not read: frame sizes on the wire and how TCP segments are split (no
   packet capture). Both interfaces had MTU 1500 and `gso_max_size`
   65536, and both kernels logged "Driver has suspect GRO
   implementation" at the same moment. That is indirect evidence that
   segments larger than the MSS crossed the link whole, as section 2.4
   point 6 reads. Titan Ridge and Ice Lake firmware were not read.

Requirements this gives `usb4-emu`'s E5 model, for its maintainer
(section 10): on the Alpine Ridge profile,

- the firmware's own type-12 UUID Request at link-up, and its
  unknown-UUID packet per lane;
- the connected event held until that request is answered;
- the far side's UUID Request and Link State Status Request answered by
  the firmware (UUID Response with the route low half first; Not
  Supported), never passed to ring 0;
- no bonding offered to the far software CM;
- the 1-DW code-8 response to every inter-domain frame the host sends;
- the firmware's own properties block before driver ready, and a new
  UUID Request at driver ready;
- a Linux guest kernel of 7.0 or later in the two-host legs, so the
  transmit ring's E2E is exercised.

Only the last item is needed for the E2E path. The rest decide whether
this driver's software CM finds an Alpine Ridge peer at all (point 6).

## 3. Discovery, in the connection manager

### 3.1 This domain's UUID

The discovery protocol identifies each domain by a 128-bit UUID (IDS
2.3). Where it comes from:

- **Under the software CM**: the host router's 64-bit UUID (ROUTER_CS_7
  and 8, which `tbt_u4cm.c` already reads for a device router, record 22
  section 6.4 step 7) read once for the host router at bind, laid out as
  Linux and Intel's firmware lay it: DW 0 the low half, DW 1 the high
  half, DW 2 and 3 all ones ([linux] `switch.c` `tb_switch_set_uuid`,
  whose comment says the ICM does the same). Two machines therefore keep
  their UUIDs across reboots, which is what a peer's log and a later
  allow-list (section 12) need. A host router whose UUID reads all zero
  or all ones gets a random one per bind from the clock and the NHI's
  serial number [inf], logged.
- **Under the firmware CM**: the local UUID the inter-domain connected
  event carries (section 3.4); this driver never answers a discovery
  request before it has one, replying Not Ready instead (as Linux does
  without a root switch UUID, `tb_xdp_handle_request`).

### 3.2 The transport on ring 0

- `tbt_regs.h` gains `TBT_PDF_XDOMAIN_REQ` 6 and `TBT_PDF_XDOMAIN_RESP` 7.
  Ring 0's receive side already takes every PDF (the PDF masks are all
  ones and raw mode ignores them, record 21 section 4.1); the frame
  dispatch in `tbt_ring.c` hands PDF 6 and 7 frames to a new pure module,
  `tbt_xdp.c`, on both profiles.
- **Requests this driver sends** use the ring's existing request rule (one
  outstanding, record 22 section 5.1) with their own matching: a PDF 7
  frame matches the outstanding inter-domain request when its route
  string equals the request's with the CM bit (route string high bit 31)
  ignored and its protocol UUID the request's, as Linux matches ([linux]
  `xdomain.c` `tb_xdomain_match`, which checks neither SN nor type), and,
  beyond Linux, by the protocol's own fields: for a discovery request the
  SN (IDS Table 2-1: the target copies it) and the response type (2 for
  a UUID Request, 4 for a Properties Read Request, 6 for a Properties
  Changed Notification, 16 and 18 for the link-state requests) or an
  Error Response (7), and for a Properties Read Response its Offset
  equal to the request's (IDS Table 2-5; [linux]
  `tb_xdp_properties_request` refuses a mismatch), so a late answer to
  an earlier chunk is never assembled; for a USB4NET request the Request
  ID (IDS Table 5-3: SN is "not used" there, the Request ID matches
  responses), the response type (1 for a Login, 3 for a Logout), and the
  two UUID fields holding this domain's UUID and the peer's. IDS Table
  5-3 reads the fields as the transaction's roles, which a response
  keeps, and this driver's own responses follow it; `thunderbolt-net`
  puts the sender's UUID first in every packet, responses included, and
  sends its Logout Response with a new id of its own rather than the
  request's ([tbnet] section 2.4 point 1). So a response is accepted with
  the two UUIDs in either order, and a Logout Response is matched by its
  type, route, protocol UUID and UUID pair while a Logout is
  outstanding, its Request ID not checked; a Login Response keeps the
  Request ID check, which Linux's copy of the request's id meets. A
  control request
  and an inter-domain request are never outstanding at once; the thread
  serialises them. The bound is IDS's: one second a try, ten resends,
  each discovery resend with the next SN (Linux uses the retry count
  modulo 4, `tb_xdp_uuid_request`), each USB4NET resend as a new Request
  Packet with the next Request ID (IDS Table 5-3: "incremented per each
  Request Packet sent"); a response matches any Request ID this login or
  logout has used since it began, since a late answer to an earlier try
  is the peer's answer to the same request.
- **Requests the peer sends** - a discovery request on PDF 6, a USB4NET
  Login or Logout on PDF 6 or 7, taken by its type, since
  `thunderbolt-net` sends them on PDF 7 ([tbnet] section 2.4 point 1) -
  are decoded on the thread as they
  land - during a busy-poll too, as record 21 section 4.4 decodes events -
  and answered at once (section 3.6). A request is never answered from
  the drain of an interrupt wake (record 21 section 4.5.1): the wake
  queues it and the thread answers it in the same pass.
- **The route** of every packet to a depth-1 peer is the host's lane 0
  adapter number, as for enumerating a router there (Guide 3.3 step 4;
  [linux] `tb.c` `tb_scan_xdomain`, `tb_downstream_route`); under the
  firmware CM it is the local route of the connected event. Spec 6.4.2.11
  words the route as naming "the Inter-Domain Router"; the Guide's and
  Linux's reading, the remote router's TopologyID, is the one taken, and
  a leg against Linux settles it (section 11.3).
- **CRC and length**: each packet is a control packet, its CRC-32C
  appended and checked by `tbt_ring.c` as today; a frame whose Data Size
  disagrees with its length, or that is shorter than its type's
  structure, is dropped and counted (`cm.xdp.malformed`), as IDS chapter
  2's rules ask for a reserved or illegal field.

### 3.3 Finding the link under the software CM

In `tbtU4Router` (and nowhere at depth 2 in this release, section 13),
the ERR_ENUM or ERR_NUA branch, when `XhciCmNet` is not 0 (section 8.1),
goes on instead of stopping:

1. The port takes a new state, `TBT_U4_XDOMAIN` (record 22 section 7.2's
   table gains the row), with `Why` `TBT_U4_WHY_XDOMAIN`. Its lane 0 is
   already unlocked by step 2 of record 22 section 6.4.
2. A UUID Request (type 12) at the port's route (section 3.2). No answer
   after the tries: the port is left as it is today, UNSUPPORTED with
   `TBT_U4_WHY_ENUM_ERROR`, and the reason logged (`cm.xdp.uuid.none`); a
   later UUID Request from the peer restarts the protocol (IDS 3.1).
3. The answer's UUID equal to this domain's: a loop (Guide 3.3); the port
   becomes UNSUPPORTED with `TBT_U4_WHY_LOOP`, the TMU's Disable Time Sync
   set on it as the Guide says, logged, nothing else.
4. A different UUID: an inter-domain link. PORT_CS_19's "USB4 Port is
   Inter-Domain" bit is set on this port (Guide 3.3; [linux] bit 4,
   step S3 takes the bit from the Spec's PORT_CS_19 table), the peer
   record of section 3.5 is made, and the discovery of section 3.5 runs.

With `XhciCmNet` 0 the branch is today's: the port is UNSUPPORTED, no
UUID Request is sent, and a peer's inter-domain requests are not
answered (section 8.1, the owner's rule of 2026-10-31). Everything else about the port - its hot-plug handling, its
lock, its teardown on CLd - stays record 22 section 7's, with section
3.8's additions.

### 3.4 Finding the link under the firmware CM

The ICM rows (record 21 section 1.4.4) gain the two events and three
commands, each row's layout from `tb_msgs.h` [linux] and, where the
emulator models it, from `usb4-emu` [emu]:

| Row | Connected (`0x06`) | Disconnected (`0x07`) | Approve (`0x10`) | Teardown |
|---|---|---|---|---|
| FR, AR | `icm_fr_event_xdomain_connected`: link info, remote and local UUID, local and remote route | link info, remote UUID | `icm_fr_pkg_approve_xdomain`: link info, remote UUID, transmit path and ring, receive path and ring | NHI mailbox `0x10` (port A) or `0x11` (port B), data 1, then 10 to 50 us, then data 2 |
| TR, ICL | `icm_tr_event_xdomain_connected`: the same fields | route, remote UUID | `icm_tr_pkg_approve_xdomain`: route, remote UUID, the four numbers | ICM command `0x11` (`icm_tr_pkg_disconnect_xdomain`), stage 1, 10 to 50 us, stage 2 |

On a connected event, decoded and length-checked as every event is
(record 21 section 5.2), with `XhciCmNet` not 0: a peer record is made
with both UUIDs and the local route, keyed as Linux keys it (by remote
UUID; an existing record at the same link and depth, or its dual link,
replaced, [linux] `icm_fr_xdomain_connected`), and the discovery of
section 3.5 starts at its properties step, the UUIDs being known. A
disconnected event ends the peer (section 3.8). With `XhciCmNet` 0 the
events are logged as today, nothing is approved, and a peer's
inter-domain requests are not answered (section 8.1).

The firmware builds the paths itself on approve (section 5.2); the
discovery and login packets still go over ring 0 as PDF 6 and 7 frames
beside the ICM's own, as Linux sends them through the same control
channel in either mode [linux] (`domain.c` `tb_domain_event_cb`'s
`TB_CFG_PKG_XDOMAIN_REQ` case). Alpine Ridge firmware passes them
through unchanged, but answers UUID and Link State Status Requests
itself and adds packets of its own (section 2.7 point 2) [bench].

### 3.5 The discovery state machine

One peer record per inter-domain link (section 13: one at a time per
NHI), driven by the thread with section 5.3's clock of record 22. States
and their bounds, each try one second, ten resends, as IDS chapter 3:

| State | Sends | On success | On failure |
|---|---|---|---|
| UUID (software CM only) | UUID Request (type 12) | LINK (or PROPS where bonding is not possible) | the port as section 3.3 step 2 |
| LINK | Link State Status Request | the peer's Supported Link Width dual: BOND_LOW or BOND_HIGH by the UUID comparison; x1: PROPS, lane 1 disabled (IDS 3.4.1; [linux] `tb_xdomain_get_link_status`) | PROPS (bonding skipped), lane 1 disabled (Guide 7.5.2) |
| BOND_LOW | Link State Change Request (TLW 3), then Link State Status Requests until the peer's TLW reads 3; own lanes' Target Link Width set, Lane Bonding set, lane 0 back in CL0; the Negotiated Link Width x2 within record 22 section 5.2's 500 ms | PROPS | PROPS, lane 1 disabled |
| BOND_HIGH | nothing: waits for the peer's Change Request (section 3.6), sets both lanes' Target Link Width, waits for the UPG 1 hot-plug event or 3 s of Negotiated Link Width polling (IDS 3.4.1) | PROPS | PROPS, lane 1 disabled |
| PROPS | Properties Read Requests from offset 0 up, 45 DWs at most a response, the whole block at most 500 DWs (section 3.7), restart from 0 if the generation changes | READY, and the service step of section 4 | ERROR |
| READY | nothing; a Properties Changed Notification from the peer returns to PROPS, and a block read again without a USB4NET directory ends the service (section 3.8, IDS 3.3) | - | - |
| ERROR | nothing; a UUID Request from the peer restarts at UUID (or PROPS under the firmware CM) | - | - |

Bonding (LINK to BOND_HIGH) runs only under the software CM, only at Gen
2 or 3, only when both of this port's lane adapters report a dual
Supported Link Width and `XhciCmLaneBonding` is 1 (record 22 section 6.5,
default 1); otherwise the machine goes from UUID to PROPS and, at Gen 2
or 3, lane 1 is disabled. A Gen 4 link is used as it came up (record 22
section 6.2.3). Under the firmware CM the firmware manages the link and
the machine starts at PROPS. The UUID comparison is byte by byte over the
canonical form, the most significant byte first (IDS 3.4.1's example);
with IDS 1.2's DW rule that is DW 0's low byte first.

### 3.6 Answering the peer

With `XhciCmNet` 1, requests are answered whatever this driver's own
machine is doing, as IDS chapter 3 requires and as Linux answers ([linux]
`xdomain.c` `tb_xdp_handle_request`); with `XhciCmNet` 0 a PDF 6 frame is
counted (`cm.xdp.ignored`) and dropped unanswered, so the peer sees this
port as it does today, a host that never answers, and gives up after its
own retries (IDS 3.1):

- **UUID Request** (type 12 or 1): the UUID Response with this domain's
  UUID and, to type 12 only, the request's route as the source route (IDS
  2.3, 6.1.3). Before the domain UUID exists (section 3.1): Error Response
  Not Ready. A request received in ERROR restarts the machine.
- **Properties Read Request**: the block from the requested offset, at most
  45 DWs, with the block's size and generation. A destination UUID that is
  not this domain's, or an offset at or past the block's end, is an
  illegal field: the request is dropped and counted (IDS chapter 2's
  rule), since the spec defines no code for it (Linux answers its own
  code 2, `ERROR_UNKNOWN_DOMAIN`, reserved in IDS Table 2-6).
- **Properties Changed Notification**: the Properties Changed Response, and
  the machine returns to PROPS (IDS 3.3); a source UUID other than the
  peer's ends every service with the old peer and restarts (IDS 3.7).
- **Link State Status Request**: Status 0 with this port's Supported and
  Target Link Width (LANE_ADP_CS_0 and 1 of lane 0; IDS Table 2-7); in
  BOND_HIGH before the Target Link Width is set, Error Response Not Ready
  (IDS 3.4.1's Ver. 2 rule, which a Ver. 1 CM may also follow).
- **Link State Change Request**: in BOND_HIGH, Status 0 and the request's
  TLW applied to both lanes; otherwise Error Response Not Ready (IDS 3.6).
  Under the firmware CM, Not Supported.
- **USB4NET Login, Logout** (protocol UUID USB4NET): section 4.
- **Time Sync** and any other type: Error Response Unknown Packet Type
  (IDS 3.6); a packet with any other protocol UUID: ignored (IDS chapter
  5).

### 3.7 This host's properties block, and the parser

The block this driver publishes, built once per bind and again when
`XhciCmNet` or the service's settings change (with a Properties Changed
Notification to a peer in READY and the generation incremented):

| Entry | Type | Value |
|---|---|---|
| `vendorid` | `v` | section 15 Q4 (no USB-IF vendor id is this project's) |
| `vendorid` | `t` | "xHCI98" |
| `deviceid` | `v` | 1 |
| `deviceid` | `t` | "xhcicm" |
| `devicerv` | `v` | the package version's four parts, one byte each |
| `maxhopid` | `v` | the facing lane adapter's Max Input HopID (ADP_CS_5 bits 6:0, record 22 section 6.8) under the software CM; under the firmware CM 15 unless a control read of the host router's lane adapter at route 0 answers (record 21 section 5.1 step 9 already reads router space at route 0) |
| `network` | `D` | only while `XhciCmNet` is 1 and the network driver holds the service open (section 6.3's `Open`): directory UUID `C66189CA-1CCE-4195-BDB8-49592E5F5A4F` (section 2.1), `prtcid` 1, `prtcvers` 1, `prtcrevs` 1, `prtcstns` per section 5.3 (bit 1 always; bit 0 when E2E is used; bit 2 when the frame size `Open` passed is above 1514) |

No `timesync` entry (section 13). The generation starts from the clock at
each bind (IDS 2.5's recommendation of a fresh pseudo-random value) and
increments on every change. The block is 54 DWs (the header 2, seven root
entries 28, the two texts 4, the directory 20), so two responses of 45
and 9 DWs.

The parser (pure, `tbt_xdp.c`) takes a block of at most 500 DWs, as
Linux does; refuses a header other than `0x55584401` and a root length
past the block; reads each 4-DW entry, refusing one whose offset plus
length leaves the block (with no wrap: the sum is tested before it is
formed, C89 `unsigned long`), a `v` entry whose size is not 1, a
directory shorter than its 4-DW UUID, a text not NUL-terminated within
its length, and nesting deeper than 8; ignores keys it does not know and
vendor types (IDS 4.2); and stops at the first fault with the block
refused (`cm.xdp.props.bad`, the offending DW offset logged). From a
valid block it takes `vendorid`, `deviceid` (and their texts, for the log
and the query device), `maxhopid` (absent: section 4.3's HopID 15, IDS
6.3.1.1.1), and
the first `network` directory whose `prtcid` is 1 and `prtcvers` 1, with
its `prtcstns`. A block without one is a peer offering no USB4NET: the
peer stays in READY, logged, no login.

### 3.8 The end of a peer

A peer ends, and every service with it (section 4.2), on: the port's
unplug under the software CM, by record 22 section 7.1's evidence (lane
0 in CLd, a re-lock, ERR_CONN or ERR_LOCK); a hot-plug event with UPG 1
on lane 0 is a hint that queues that port check, as record 22 treats
every hot-plug packet, never an unplug by itself, because the same event
ends a lane bonding (IDS 3.4.1) and the check then finds the port in CL0
bonded (IDS 3.7's disconnect is the one the check confirms); the
disconnected event under the firmware CM; a Properties Changed
Notification from a different UUID; a block read again whose USB4NET
directory is gone (the service only; the peer stays READY, IDS 3.3); the NHI's stop, surprise removal,
removal or D3 (record 21 section 5.6 and record 22 section 11, before the
rings stop); and a start of the NHI with `XhciCmNet` 0 (section 8.1). On the unplug the
peer is gone, so its paths and rings are torn down without a logout; on
every other end a logout is sent first where the peer is still there
(section 4.2). Where only the service ends (the directory gone, the
network driver's `Close`), the peer stays READY and the port keeps its
Inter-Domain bit. Where a Properties Changed Notification brings a
different source UUID, the link is still there: the old peer's record
ends (its service without a logout, the old peer being gone) and the
discovery restarts at once on the same link, from UUID under the
software CM and from PROPS under the firmware CM (IDS 3.7's last
paragraph; section 3.6), the port keeping its Inter-Domain bit and its
`TBT_U4_XDOMAIN` state. Where the peer itself ends (every other case),
under the software CM the port then clears its Inter-Domain bit (Guide
3.3; [linux] `tb_port_unconfigure_xdomain`) and returns to record 22's
port machine, where the next plug starts again.

### 3.9 As built under the software CM (step S3)

`tbt_u4cm.c`'s section "Inter-domain links", reached by narrow hooks in
the port machine; `test_u4xd` drives it (section 11.1).

- **The port.** `tbtU4Router`'s ERR_ENUM or ERR_NUA branch leaves the
  port `TBT_U4_XDOMAIN` with `TBT_U4_WHY_XDOMAIN` when `XhciCmNet` is 1
  (`TBT_U4_CONFIG.Net`). The idle tick, after the port's check, starts a
  `TBT_XDP_PEER` for it, ticks it, and takes the port's state from the
  engine's: LOOP is UNSUPPORTED with `TBT_U4_WHY_LOOP` and Disable Time
  Sync set on lane 0 (Guide 3.3); ERROR before any UUID answer is
  UNSUPPORTED with `TBT_U4_WHY_ENUM_ERROR` as today, the record kept so
  that the peer's own request restarts it (section 3.3 step 2);
  anything else XDOMAIN. Its check ends the peer on record 22 section
  7.1's evidence (lane 0 in CLd, Lock set again) and then runs record
  22's teardown; a lane 0 hot plug event alone only asks for that check.
  One record per host port, kept outside the port records that the
  snapshots and the resume copy whole, and tagged with the port's
  connection generation: a record of an earlier connection (a teardown
  nested at a safe point, record 22 section 7.4, ends a connection
  without this section) is ended as gone before anything else touches
  it. A recovery (record 22 section 7.7) puts an XDOMAIN port back to
  EMPTY, so the ordinary check connects it afresh after the commit.
- **The packets.** Ring 0's XDomain sink (step S2) copies each packet
  into a queue of 8 that the thread empties after the read in progress
  (`TbtU4Poll`, `TbtU4RingWake`), each to the peer of the port whose
  lane 0 its depth-1 route names; a full queue drops the packet,
  counted (the peer resends). With `XhciCmNet` 0 the sink counts and
  drops every packet (`cm.xdp.ignored`, logged at the first).
- **One service per NHI** (section 13). While one port's service is up,
  every other peer's block carries no `network` directory and its
  logins are refused (the engine's `TbtXdpPeerSetNetWanted`); the
  directory is offered again when that service ends. A second service
  racing the first to its paths is refused there, and the engine logs
  out.
- **The link.** Bonding is offered to the engine when `XhciCmLaneBonding`
  is 1, the link is Gen 2 or 3 at x1 and both host lane adapters list
  x2; the peer's side is the Link State exchange's. Target Link Width and
  Lane Bonding are written on the host's lanes only, kept in the port's
  `LaneSaved` and `LaneWritten`, so record 22's host restore puts them
  back at the teardown. A Gen 2 or 3 link that does not bond has its
  lane 1 disabled once the peer's UUID is known (`Lane1Disabled`, put
  back the same way), not before, so a far end that never answers keeps
  today's state. That disable is record 22 section 6.5 step 6 on the
  host's side: what a failed bonding wrote put back and read back first,
  then, with both host lane adapters at their defaults (Target Link
  Width single, no Lane Bonding, no CL state enabled) and Disable Time
  Sync set on lane 0 (the link's TMU, which this driver never enables
  here, off), lane 1 in CL0 beside a lane 0 at x1 disabled and read back
  Disabled (a lane 1 already in CLd or Disabled, or a lane 0 at x2, is
  left as it is). A link that cannot be verified so is served but gets
  no path until a re-plug (`cm.xdp.lane1`). The engine polls the width
  while it waits for a bonding, so a lane 0 hot plug event is not handed
  to it.
- **PORT_CS_19's Inter-Domain bit** (bit 4, the Spec's PORT_CS_19
  table) is set once the peer's UUID is known and cleared, and read
  back, at the peer's end, the service waiting until the set is
  answered (tried again at each tick); a clear not read back stays owed
  to the port
  and is retried by the idle tick while no peer runs there (a new
  record on the port takes the obligation over).
- **The paths** of section 5.1, on ring 1: the receive entry first, at
  lane 0, then the transmit entry at the host interface adapter, each
  one read of two DWs and one write of both, priority 4, weight 1. The
  receive entry's credits are the lesser of what the lane has left
  (record 22 section 6.8's accounting) and the host router's `baMaxHI`,
  which the Buffer Allocation Request now keeps beside `baMaxPCIe` and
  `baMaxUSB3`, else 14. Before either is written, both HopIDs and the
  ring are checked free on their adapters, live or quarantined (record
  22 section 6.8's scan); one in use refuses the service
  (`cm.net.hop.busy`). Down: both entries are handed, with where they
  send, to a record of their own that outlives the service's; then the
  transmit entry, then the receive entry, each read first and
  invalidated through record 22's `tbtU4Invalidate` (Pending Packets
  waited for; an entry that never drains is quarantined) only while it
  still reads valid with that output. One that reads invalid, or valid
  with another output (left untouched, record 22 section 7.7's rule for
  what is not established as one's own), is owed no more; one that
  cannot be read stays owed to the idle tick, which takes it down at the
  top of the port's share, before any host restore or connection, and no
  new service starts on the port while any is owed. One that a
  quarantine record holds instead (its drain failed, or the access
  closed during the invalidation) is the quarantine's from then on, the
  record given this service's output identity where it has none, so its
  retry knows the entry as this driver's.
- **The link at a logout** (section 4.2). The network driver's link goes
  down at the first logout of either side, as under the firmware
  connection manager (section 3.10): a Logout from the peer takes the
  paths down inside the engine (`ServiceDown`, which lowers the link
  first); this host's own Logout (the network driver's `Close`, or the
  service withdrawn from a peer) takes them down only when it is answered
  or times out, so after each engine call (a tick, a packet, a change of
  the offer) the link is lowered as soon as the engine's service is no
  longer up, the paths, the rings and the NHI's one service held until
  `ServiceDown`. `test_u4xd`'s unanswered close holds it.
- **The driver's share** (step S5), `TBT_U4_NET`: `RingsUp` before the
  entries, `Link` up after them and down before they go (after which the
  driver accepts no send and drains its transmit ring, at most 500 ms),
  `RingsDown` after the entries are invalid. `TbtU4NetWanted` is the
  network driver's `Open` and `Close`; `TbtU4NetIdentity` gives the MAC
  of section 4.4 before any peer.
- **The stop and the suspend** end every peer with one Logout sent and
  not waited for (IDS 5.1.2 takes no answer as one), then its paths and
  rings down; the containment ends them with nothing written, the two
  entries left owed by identity to the idle tick. Any teardown of the
  port (record 22's, nested at a safe point included) ends its peer
  first, before the lanes are restored.
- **This domain's UUID**: DW 0 is ROUTER_CS_7 and DW 1 ROUTER_CS_8 (the
  Spec names them the UUID's high and low halves; Linux reads the two
  DWs as one little-endian 64-bit value whose low half is ROUTER_CS_7),
  DW 2 and 3 all ones; the MAC from the same two DWs, router id 0, port
  0. One that reads all zero or all ones is replaced by a value made from
  the clock and the ring's bus address, logged (`cm.xdp.uuid.random`).
- **Not on a Thunderbolt host router** (record 22 section 19): its link
  controller's inter-domain setting ([linux] `lc.c`
  `tb_lc_configure_xdomain`) is not designed here, so such a port stays
  UNSUPPORTED.

### 3.10 As built under the firmware CM (step S4)

`tbt_icm.c`'s section "Inter-domain links" and its row entries; the
engine is S1's (`tbt_xdp.c`, `FirmwareCm` TRUE: no UUID step, no
bonding), unchanged. `test_tbt_icm` drives it (section 11.1). The
driver reaches it through `TbtIcmSetNetConfig` (`XhciCmNet` and the
package version, set after `TbtIcmInit`), `TbtIcmSetNet` (the service's
three operations, `TBT_ICM_NET`, the same signatures as S3's
`TBT_U4_NET`, so one set serves both profiles), `TbtIcmNetWanted`,
`TbtIcmNetIdentity`, `TbtIcmSetNetSerial` and `TbtIcmPeer`; everything
else runs inside the client's existing entry points.

- **The rows.** Every row gains the two events, the approve and its
  teardown, in Linux's layouts ([linux] `tb_msgs.h`; `icm.c` `icm_probe`:
  Falcon and Alpine Ridge take the `icm_fr_*` set, Titan Ridge and Ice
  Lake the `icm_tr_*` set, Ice Lake included although it has no device
  approve), as DWs with the header in DW 0 (record 21 section 1.4):

  | Message | Falcon and Alpine Ridge | Titan Ridge and Ice Lake |
  |---|---|---|
  | connected `0x06`, 14 DWs | DW 1 bits 31:16 link info (link 2:0, depth 7:4; a link above 4 or a depth above 6 refused, as Linux); DW 2 to 5 the remote UUID; DW 6 to 9 the local UUID; DW 10 and 11 the local route high and low; DW 12 and 13 the remote route | the same words; the link info read and not used, the record keyed by the route alone (Linux passes link and depth 0) |
  | disconnected `0x07` | 6 DWs: DW 1 bits 31:16 link info, DW 2 to 5 the remote UUID | 7 DWs: DW 1 and 2 the route, DW 3 to 6 the remote UUID |
  | approve `0x10` | 8 DWs: DW 1 bits 31:16 the record's link and depth as link info; DW 2 to 5 the remote UUID; DW 6 the transmit HopID (15:0) and ring (31:16); DW 7 the receive HopID and ring | 9 DWs: DW 1 and 2 the route; DW 3 to 6 the remote UUID; DW 7 and 8 as DW 6 and 7 |
  | teardown | NHI mailbox `0x10` for links 1 and 2 (port A), `0x11` for 3 and 4 (port B) ([linux] `tb_phy_port_from_link`), data 1, 20 us, data 2, both sent whatever the first answers | command `0x11`, 8 DWs: DW 1 bits 7:0 the stage; DW 2 and 3 the route; DW 4 to 7 the remote UUID; stage 1, 20 us, stage 2, no stage 2 after a stage 1 that failed ([linux] `icm_tr_disconnect_xdomain_paths`) |

  A response is the request's echo: every word after the header must
  match, as a device approve's must (record 21 section 5.4), so stage
  1's late answer is never taken for stage 2's; a shorter one is
  logged whole and not taken. The approve waits
  `XhciCmApproveTimeoutMs`, the teardown `XhciCmCmdTimeoutMs`, each
  with the client's retries.
- **The events.** With `XhciCmNet` 0 both are counted and logged as
  ignored, as before this step. With 1 each is decoded where it lands, a
  command wait included. A connected event takes its turn among the
  device announcements, both kinds under one rule: acted on at once only
  where no wait and no engine runs and nothing older is queued, else
  queued in the devices' own queue, so a host and a device announced at
  one place are acted on in the order the firmware sent them; one lost
  to a full queue asks for the rediscovery a lost device announcement
  asks for (driver ready again, at which the firmware announces every
  link that is up [emu] (`XDOMAIN.md` section 7); real firmware [inf]).
  A disconnected event takes its turn the same way, so a connected event
  queued before it is acted on first, with everything that one replaces,
  and the packets held for a record's route go when the record ends; it
  also acts where it is decoded: its record is marked ended - its
  firmware paths taken as gone (no teardown is sent for them), a wait on
  its approve or teardown ended as a device disconnect ends that
  device's approve wait - by a flag of the record's that no queue can
  lose; the record ends at the next point where no wait and no engine
  runs.
- **The keying** ([linux] `icm_fr_xdomain_connected`,
  `icm_tr_xdomain_connected`, `icm_*_xdomain_disconnected`). Only a
  direct link gets a record (section 13): a local route of one level and,
  on Falcon and Alpine Ridge, a link info depth of 0, which names the
  router the far host hangs off (Linux finds that router by the event's
  link and depth; the emulator sends 0 for a direct link [emu]); any
  other is logged (`cm.xdp.deep`) and its packets get no record. A
  connected event whose remote UUID a record already has, at the same
  place (Falcon and Alpine Ridge: the same depth and physical port; Titan
  Ridge and Ice Lake: the same route), keeps that record, its route and
  link taken from the event (the engine's route too), the engine
  untouched; at another place, that record ends. Then a record at the
  event's link and depth or its dual link's (Falcon and Alpine Ridge) or
  at its route (Titan Ridge and Ice Lake) ends; a device in the table
  where the host now is (Titan Ridge and Ice Lake: the same route; Falcon
  and Alpine Ridge: depth 1 on the same physical port) leaves with its
  subtree, each with its TUNNEL_DOWN of why REPLACED, as Linux removes the
  switch there; and the new record starts. A device announced where a
  record's host is (the same route, or depth 1 on the same physical port)
  ends that record ([linux] `icm_fr_device_connected`,
  `__icm_tr_device_connected`; Linux looks the XDomain up by the device's
  own link and depth, which never names a direct link's depth 0, so the
  port is taken here instead). A disconnected event ends the record of
  its remote UUID (Falcon and Alpine Ridge) or route (Titan Ridge and Ice
  Lake). A rediscovery's driver ready marks every record and the
  reconciliation ends those not announced again within its window, as it
  drops devices ([linux] `icm_unplug_children` and
  `remove_unplugged_switch` mark and free XDomains the same way). Two
  records per NHI (`TBT_ICM_MAX_XD`, a two-port controller's two links);
  a third connection is refused, logged (`cm.xdp.full`). Each of these
  ends is the peer gone: no Logout and no teardown, since the firmware
  has said something at that place [inf].
- **The record.** The engine is given the event's local UUID as this
  domain's (section 3.1), its remote UUID, and its local route, the CM
  bit clear, as the peer's route (section 3.2), and starts at PROPS. The
  block: `vendorid` 0 "xHCI98", `deviceid` 1 "xhcicm", `devicerv` the
  package version, `maxhopid` 15 (the control read of section 3.7 is not
  made: which adapter faces the link is the firmware's), the `network`
  directory as the service offers it (below). The lane's Max Output
  HopID is taken as 15 and no HopID as in use, the firmware's own
  allocations being unreadable (section 4.3), so the transmit HopID is 8
  for a peer whose `maxhopid` is 8 or more and exactly 15 for one with
  none. The engine is ticked at each idle poll, after the frames and the
  events.
- **The packets.** `TbtIcmInit` installs ring 0's XDomain sink (step
  S2): each packet is copied into a queue of 8 that the thread empties
  after the events, each to the record whose route it carries (the CM
  bit cleared); a full queue drops it, counted. With no record at its
  route, a discovery request is answered Error Response Not Ready
  (section 3.1: this domain has no UUID for that link yet) and anything
  else is dropped and counted. With `XhciCmNet` 0 the sink counts and
  drops every packet (`cm.xdp.ignored`, logged at the first), so the
  peer sees no answer, as before this step.
- **The identity** (section 4.4): the MAC from the 64-bit serial the
  driver hands `TbtIcmSetNetSerial` (the NHI's PCI Express Device Serial
  Number, the driver's read), router id 0, port 0; without one a
  locally administered unicast address made from the clock and ring 0's
  bus address, logged (`cm.xdp.mac.random`). Taken once per instance;
  `TbtIcmNetIdentity` is FALSE with `XhciCmNet` 0 or before the bind.
- **The offer** (section 13: one service per NHI). While one record's
  service is up, every other record's block carries no `network`
  directory and its logins are refused, as under the software CM (S3).
  On Falcon Ridge the E2E bit is withdrawn from what this host offers:
  its quirk's Transmit E2E HopID 1 (section 4.3) is not one the service's
  ring operation is told, so neither side enables E2E there.
- **The service up**, called by the engine once both logins are done:
  refused while another record's service is up, without the service's
  operations, while an owed teardown (below) cannot be sent, or while
  the fence (below) holds. The ring
  is 1 (2 on Falcon Ridge, section 4.3); E2E when both blocks advertise
  it. In order: the service's `RingsUp` (port = the physical port + 1,
  that ring, E2E), the approve (section 5.2), then `Link` up with the
  peer's MAC. An approve answered with the error flag: the rings down,
  no service (`cm.net.approve.fail`), and the engine logs out. One that
  may still be answered (the fence, below): the firmware may have built
  the paths, so the teardown is sent, then the rings down, no service.
  One ended by a disconnect: the rings down.
- **The service down.** The network driver's link goes down at the
  first logout of either side (section 4.2). A Logout from the peer
  takes the paths down inside the engine's packet handling (its
  `ServiceDown`, which lowers the link first); this host's own Logout
  takes them down only when it is answered or times out, so after every
  engine call the link is lowered as soon as the engine's service is no
  longer up, the paths, the rings and the NHI's one service held until
  `ServiceDown`. That lowers the link where it is still up, sends the
  teardown where the approve was answered (or may still be) and no end
  of the link has been announced, then `RingsDown` (`lost` when the host
  interface is). A teardown not proven made - not answered, answered with
  the error flag (Linux takes the flag as a failure too, and nothing says
  the paths are gone), or that cannot be sent - is owed (one owed
  identity per NHI: the row's address and the remote UUID) and sent again
  before the next approve; until it is answered without the flag the new
  service is refused (`cm.net.teardown.owed`). An owed one is retired by
  the firmware's own evidence: a disconnect of its link, or another host
  or a device announced at its place, each of which takes its paths with
  it.
- **The fence.** An approve or teardown command any attempt of which
  may still be answered - an attempt that timed out, even when a retry
  was answered (which does not say which attempt it answered), or a
  request published whose wait ended with no answer - leaves an answer
  that would match a later command of the same words (the same UUID,
  place, HopIDs and ring), as record 21 section 5.4 treats a stale
  answer. So no approve is sent from then until the rings restart (a
  resume, a recovery, a rebind); the service is refused meanwhile
  (`cm.net.approve.fenced`). An approve left so is also taken as having
  perhaps built the paths: the teardown follows.
- **The lifecycle.** `TbtIcmSuspend` and `TbtIcmUnbind` end every record
  before the rings stop: one Logout sent and not waited for where the
  service is up (IDS 5.1.2 takes no answer as one), the teardown, the
  rings down, the record freed; after a resume the firmware's
  announcement starts a record afresh. `TbtIcmLost`, a failure after
  activation, and the entries of `TbtIcmResume` and `TbtIcmRecover` end
  every record as gone with nothing sent, the teardown left owed where
  the paths were approved. The packets held go with the records at each
  of these ends, and the queued connected events with the devices'
  queue, which every session boundary retires. A rediscovery's driver
  ready keeps the records; the re-announcement is keyed and reconciled as
  above.

### 3.11 The bench capture weighed (revision 10)

The facts are section 2.7's (task 45-E.15, the owner's bench: an Alpine
Ridge LP host under its firmware CM and a 12th-generation Intel host
under Linux's software CM, both on Linux 7.0; `runs/run-45.md`, "45-E.15
(the bench capture)", [bench]). What changed:

- **The firmware CM sends a UUID Request at a record's start.** The
  firmware answers the far end's UUID Request itself, so this host never
  sees it; a far software CM that gave up before this driver bound
  (Linux stops after about 20 s, "discovery failed", read on the
  emulator, run-45 "45.59 S5 (legs)") restarts only at a UUID Request.
  `TbtXdpPeerStart` in the firmware CM's mode sends one beside the
  discovery (`cm.xdp.uuid.nudge`), resent each second, at most ten more
  times, until a UUID Response comes; `test_tbt_xdp`'s
  `test_firmware_nudge`, its first request lost.
- **The software CM answers an early UUID Request.** An Alpine Ridge
  firmware at the far end sends one UUID Request about 10 to 14 ms after
  the link is up, with no retry seen, and tells its host of this one only
  once it is answered - before the port machine has read ERR_ENUM and
  classified the port, and perhaps with the hot plug event itself. With
  `XhciCmNet` 1 a UUID Request on a port's lane 0 that no record serves
  and that is not yet XDOMAIN is answered at once with this domain's
  UUID, whatever the port's state (`cm.xdp.uuid.early`); the
  classification then starts the discovery as before. An answer that
  cannot be sent at once (this domain's UUID unreadable, the ring
  refusing it) is owed to the next takes, at most ten more tries, until
  sent, a record started on the port since included (that request never
  reaches its engine). With this answer the link no longer depends
  on the Alpine Ridge firmware retrying its UUID Request, which section
  2.7 leaves unread. `test_u4xd`'s `test_early_uuid`, the request also
  queued with the plug before the first poll, and once with the first
  answer failing.
  Revision 12: the request arrives with the link, while the host
  router's lane 0 adapter is still Locked, and a host router refuses a
  control packet out through a Locked adapter (ERR_LOCK, Spec 8.2.2.1,
  ADP_CS_4); the ring takes the answer and the far end never sees it.
  Read on the emulator (run-45 "45.59 (legs, continued)", the Alpine
  Ridge leg with `xd-fw-uuid-retry-ms=0`): `cm.xdp.uuid.early` logged,
  the host router's ERR_LOCK at once, no UUID Response at the far
  firmware and no connected event at its host. The answer is therefore
  held, owed and no try counted, until this driver has cleared lane 0's
  Lock (`tbtU4Router`, the port's `Unlocked`) and lane 0's ADP_CS_4,
  read before each try, shows the Lock clear - the flag alone can
  describe a connection a quick re-plug has replaced; a Lock read set
  marks the answer as the next connection's - and goes at the next idle
  poll (retried there only: at a ring wake the Lock read's own
  completion would wake the ring again). It is dropped when lane 0 reads disconnected (the evidence
  read of any check), at the teardown of the connection it was taken on,
  and when the port's connection generation has moved on by more than
  one. At worst an answer held across a re-plug reaches the next
  connection: a UUID Response to no request of that peer's, which a
  receiver drops, or one carrying this domain's same UUID for the
  peer's own pending request. The host tests' fabric routes an
  inter-domain packet as any control packet now (a Locked adapter on
  the way refuses it, counted), and `test_early_uuid` takes the request
  at the ring wake that brings the hot plug event, before the port's
  check, as the emulator did; also behind a quick re-plug of a port this
  driver had unlocked, and with the cable out again before the port was
  connected.
- **Logins never give up.** Linux answers a Login only while its
  interface is up (43 s after a cold start on the bench, and only after
  `ip link set up` on the emulator), and itself retries every 5.12 s for
  ever. After a minute unanswered this host's Logins go on every 5 s
  (`TBT_NET_LOGIN_IDLE_MS`, `cm.net.login.none` logged once) for as long
  as the service is wanted, instead of stopping; a peer's Login is
  answered whenever it comes, as before. `test_tbt_xdp`'s
  `test_login_idle`. `TBT_NET_S_ABANDONED` now means only that no HopID
  is left to log in with.
- **The code-8 acknowledgement.** Alpine Ridge firmware answers every
  inter-domain frame its host sends with one one-DW ICM response,
  `0x01000008`. No command of this driver's has code 8, so none waits
  for it, and a command wait matches only its own code with the whole
  echo (an approve or teardown is never satisfied or disturbed by it);
  it is counted with the strays and logged once a start
  (`tbt.icm.xd-ack`) rather than once a frame. `test_tbt_icm`'s stray
  vector.

What needs no change:

- **E2E both ways.** Linux 7.0 enables E2E on its transmit ring as well
  as its receive ring (the emulator's Linux 6.18 only on receive, section
  2.4). With `XhciCmNetE2E` 1 and the peer's `prtcstns` bit 0, `RingsUp`
  enables E2E on this host's receive ring with the Transmit E2E HopID
  (section 5.3, step S5), and the host interface grants the credits from
  the descriptors posted (Spec 12.2.2.3): nothing in software waits for
  or grants a credit. With 0 the bit is withdrawn and Linux turns E2E off
  on both rings. Read on the emulator against Linux 7.0 sending with E2E
  (run-45 "45.59 S5 (legs)", the fifth leg): no loss up to 30000-byte
  packets, the credits granted by the host interface.
- The firmware's Error Response 3 to Link State Status (a far software
  CM then keeps the link single lane, 20 Gb/s), its own properties block
  before driver ready and its new UUID Request after it, event `0x07` at
  an unplug with one Logout from Linux into the dead link, the Falcon and
  Alpine Ridge layouts (section 3.10's row, matched), and Linux's runtime
  suspend 18 s after an unplug: this driver waits on none of them.

## 4. The USB4NET service

### 4.1 Who logs in

IDS 5.1.1 has each CM log in to the other. This driver does both halves:
as **requester**, while the network driver holds the service open
(section 6.3), it sends a Login once the peer is READY with a USB4NET
directory and its own block offers one; as **responder** it answers the
peer's Login. The service is up when this driver has both sent a
successful Login Response and received one (IDS 5.1.1's last paragraph),
which is when section 5 builds both paths and enables both rings, in one
step on the thread, and the network driver's link goes up (section 7.6).

Login: the next request id, revision 1, the Service HopID of section 4.3.
The resend schedule: every 100 ms for the first 2 s, then every second,
for at least a minute (IDS 5.1.1), then (revision 10, section 3.11) every
5 s for as long as the service is wanted, the minute logged once
(`cm.net.login.none`); a Login from the peer is answered whenever it
comes (IDS 5.1.1's second paragraph). Revision 9 and before abandoned
the login after the minute.

Login Response this driver sends: Status 0 and its MAC (section 4.4),
length 6, when the requester is the peer of this link and no other login
is up; otherwise the failure status `0x80000001` (vendor failure, IDS
Table 5-6), logged. A repeated Login from the same peer while the service
is up is answered with success again and changes nothing here. This
driver itself never sends a Login once its own has been answered, and
never one with another Service HopID while the service is up, since
`thunderbolt-net` would record the new HopID without rebuilding its
paths ([tbnet] section 2.4 point 1, whose tries come about 5 s apart).
That peer never sends a failure status, so a failure is this driver's
own case only.

A Login Response this driver receives with a failure: logged with its
status, the login retried at the slow rate until the minute ends.

### 4.2 Logout

Sent (requester side) at every end of section 3.8 but the unplug, and
when the network driver closes the service (section 6.3, a halt or a
disable). Resent once a second, ten times; no answer is taken as an
answer (IDS 5.1.2). On the response: the transmit path and ring torn
down. A Logout from the peer: Logout Response Status 0, then the receive
path and ring torn down, and this driver logs out in turn (its own
Logout, which tears its transmit half down), so the two halves never
outlive each other: the service is one unit here, as the firmware's one
tunnel per peer is ([linux] `icm_fr_approve_xdomain_paths`,
`icm_tr_approve_xdomain_paths`), and under the firmware CM the teardown
command of section 3.4 follows once both halves are down. The network
driver's link goes down at the first logout of either side. A peer that
logs out and later logs in again gets the service built afresh, both
logins redone (section 4.1), on rings and HopIDs chosen again.

### 4.3 HopIDs and rings

- **The transmit HopID** (the Service HopID in this driver's Login): the
  lowest HopID from 8 up that is at most the peer's `maxhopid` and this
  lane adapter's Max Output HopID (ADP_CS_5 bits 17:11) and not in use on
  that adapter (record 22 section 6.8's allocation rule). On an empty
  link that is 8, as Linux's allocation gives ([linux]
  `tb_xdomain_alloc_out_hopid`). A peer whose block has no `maxhopid`
  (a Thunderbolt 3 peer) gets exactly HopID 15, not the lowest free one
  (IDS 6.3.1.1.1, Table 6-2: the Router "shall use HopID=15"); 15 in use
  or above the lane's Max Output HopID: no login, logged
  (`cm.net.login.badhop`).
- **The receive HopID** is the peer's Service HopID from its Login. It
  must be from 8 up to this driver's published `maxhopid` and free as an
  input HopID on the lane adapter; one that is not gets a failure Login
  Response (`cm.net.login.badhop`).
- **The rings**: the lowest free ring pair from 1 up, both directions on
  the same ring number (Guide 5.7.2 note "in most cases"), at most Total
  Paths - 1; on the two Falcon Ridge NHIs from 2 up, with the receive
  ring's Transmit E2E HopID 1 ([linux] `nhi.c` `nhi_alloc_hop`, the
  quirk's comment: "to avoid losing Rx packets when RING_FLAG_E2E is
  set"). With one service per NHI (section 13) that is ring 1, or 2 on
  Falcon Ridge.
- Under the firmware CM the four numbers go into the approve (section 5.2)
  and the firmware owns the HopIDs on the lane adapter; this driver still
  takes the transmit HopID as above (from 8, bounded by the peer's
  `maxhopid`, or exactly 15 without one), since it cannot read the
  firmware's allocations.

### 4.4 The MAC address

The adapter's permanent address must exist before any peer, since the
network PDO and its miniport start without one (section 6.1), and must
not change with the port a later peer uses. It is therefore taken once
at the NHI's start, from what that start already reads, and kept for the
instance:

- under the software CM, IDS 5.1.5.3's derivation from the host
  router's 64-bit UUID (ROUTER_CS_7 and 8, section 3.1) and its router
  id, with the USB4 port number taken as 0 for every port, since the
  adapter belongs to the NHI, not to a port (section 6.1);
- under the firmware CM, where the host router's UUID is not read, the
  same derivation over the NHI's PCI Express Device Serial Number, which
  `usb4-emu`'s Windows 11 capture reads as the controller's 64-bit
  serial on Alpine Ridge [emu] (`ICM-PROTOCOL.md` section 10.1); a
  function without the capability takes the 64 bits from its vendor,
  device and subsystem ids and its bus, device and function, which are
  stable for one slot but may be the same on two machines of one model,
  so they are not used; such a function takes a random locally
  administered address at each start instead (from the tick count, the
  performance counter where it exists and the NHI's BAR address, mixed
  and logged), which a peer learns again by ARP after a restart [inf].
  The local UUID of a connected event is not used: it arrives only with
  a peer.

Both are locally administered and unicast by construction.
The peer's MAC from its Login Response is logged and handed to the
network driver for the log and the query device; the network stack
learns it by ARP as on any Ethernet. `thunderbolt-net` uses a
derivation of its own, not IDS 5.1.5.3's, and never reads the Login
Response's MAC ([tbnet] section 2.4 point 8); nothing depends on either
side's choice.

## 5. The DMA paths and rings

### 5.1 The paths under the software CM

Two path entries in the host router only (the inter-domain link is the
host's own port; nothing on the far side is this domain's), each one
read of its two DWs and one write, source adapter first (Guide 5.2.1;
record 22 section 6.8's discipline):

| Path | Ingress | Output | Attributes |
|---|---|---|---|
| transmit | host interface adapter, HopID = the transmit ring number | the port's lane 0 adapter, HopID = this driver's Service HopID | priority 4 (Guide Table 6-1; Linux uses 5, section 2.3), weight 1, EFC 1, ESE 0; the entry's credits, IFC and ISE kept as read, since a host interface adapter's path entry has the protocol adapter's layout, in which those fields are vendor-defined (Spec 8.2.3.3, Table 8-23 and its note; Guide 5.2.1; record 22 section 6.8 keeps them the same way at a PCIe adapter) |
| receive | the port's lane 0 adapter, HopID = the peer's Service HopID | host interface adapter, HopID = the receive ring number | priority 4, weight 1, IFC 1, EFC 0, ISE 0, ESE 0 (Guide 5.7 step 7's segment rule) |

**Credits** by record 22 section 6.8's accounting, for the receive
path's entry, the one at a lane adapter: the lesser of what the lane has
left after the control path and the flow-control-disabled pool, and the
host router's `baMaxHI` from its Buffer Allocation Request (record 22
section 6.7), else 14; at least 1 ([linux] `tb_dma_init_rx_path`,
`TB_DMA_CREDITS`, `TB_MIN_DMA_CREDITS`). Below 1: no service, logged
(`cm.net.credits`). The transmit entry's credits are the host interface
adapter's own, kept as read (above). Total Buffers is re-read after bonding (record 22
section 6.8).

**Order up**: both rings set up (section 5.3) with Ring Valid, then the
receive path, then the transmit path, then the link up to the network
driver. **Order down**: the link down to the network driver (no new
sends accepted), the transmit ring drained or 500 ms, the path entries'
Valid cleared with record 22 section 6.8's Pending Packets wait (500 ms,
then 2 us), then each ring disabled by Guide 5.7.3 and its memory kept
until the next service or the stop. On the ASM4242's host router the
Host Interface adapter's entry (the transmit entry) is drained once it
reads Valid 0: that adapter's Pending Packets never clear there, which
kept the entry quarantined and every later connection of the port
deferred (revision 16; record 22 section 18.9). CL states are never enabled by this
driver (record 22 section 1.3), so nothing is turned off for the service
as Linux does.

### 5.2 The paths under the firmware CM

The approve of section 3.4's table with the transmit HopID and ring and
the receive HopID and ring, after the login of section 4.1, its timeout
`XhciCmApproveTimeoutMs` (record 21 section 8) and its reply matched by
its echo as record 21 section 5.4 matches a device approve; an error flag
or no answer: no service, logged (`cm.net.approve.fail`), a logout sent.
The rings are set up and enabled before the approve. The teardown of
section 3.4's table at every end but the unplug, where the firmware has
already dropped the paths [inf] (it announces the disconnect), and the
rings disabled after it. One approve per peer, as the firmware allows
([linux] "only one tunnel is supported by the firmware").

### 5.3 The rings

Each service has one transmit and one receive ring of the ring number of
section 4.3, set up on the thread by Guide 5.7.1 and 5.7.2, each in one
common buffer:

- **Frame Mode** (Raw Mode 0), No Snoop 0.
- **Receive**: Data Buffer Size 0 (4096 bytes), every descriptor posted
  with RS 1 and IE 1 (IE is harmless while the interrupt is masked, and
  record 21 section 4.5's arm opens it); the PDF masks SOF bit 17 (PDF 1)
  and EOF bit 2 (PDF 2) (IDS 5.1.5; Spec Table 12-27). A completed
  descriptor is taken by its EOF PDF and its length, never by the SOF PDF
  it reports: `usb4-emu`'s model writes back SOF PDF 0 for a frame small
  enough for one tunnelled packet [emu] (the model's reading of Spec
  12.4.2.1, an inference of its own), so the receiver does not require
  SOF PDF 1 on a short frame.
- **Transmit**: each descriptor SOF PDF 1 and EOF PDF 2 for a frame in one
  descriptor (every USB4 frame here fits one 4 KiB buffer), RS 1, IE 0.
- **End-to-end flow control**: enabled on both rings when both blocks
  advertise `prtcstns` bit 0 (IDS 5.1.5.4), the receive ring's Transmit
  E2E HopID the transmit ring's number (Guide 5.7.2; on Falcon Ridge 1,
  section 4.3), written after Ring Valid as Linux writes it ([linux]
  `tb_ring_start`) - the Spec asks a transmit ring enabled in one write
  (Table 12-21's note), so the transmit ring takes all its bits in one
  write and the receive ring's E2E bits go in a second write, which the
  Spec does not forbid for a receive ring [inf]. This driver advertises
  bit 0 on every host but the ASM4242's (below); `XhciCmNetE2E` 0 (section 8.1) withdraws it. A peer may enable it on
  its receive ring alone (section 2.4 point 9, as observed of Linux):
  its frames then arrive with no credits used, which a receive ring with
  E2E enabled is taken to accept as any frame while it has a descriptor
  posted, the credits only pacing a sender that waits for them [inf]; a
  frame that finds no descriptor is that sender's loss, counted, and the
  S5 and S9 legs read both.
- **No E2E on an ASM4242** (revision 15, 2026-11-08): on the ASMedia
  ASM4242's host router (`ROUTER_CS_0` 174C:2428, the identity record 22
  section 18.8 keys on) the software connection manager withdraws
  `prtcstns` bit 0 from the block it serves, so neither side enables E2E
  (IDS 5.1.5.4), as the firmware CM does on Falcon Ridge (section 4.3).
  `usb4-emu` models the ASM4242's transmit-ring quirk from a public
  `thunderbolt-net` patch: a transmit ring other than ring 0 with E2E
  enabled never completes a descriptor. Read there [emu]: 98 SE on the
  Alpine Ridge model cabled to XP on the ASM4242 model, both blocks
  advertising 7, came up through login and both paths with credits
  granted both ways, yet the XP end's transmit ring never put a frame on
  the link and no ping was answered. The withdrawal is logged at each
  start of the service's rings (`cm.net.e2e.off`, the host router's id);
  `XhciCmNetE2E` keeps its meaning elsewhere.
- **Sizes**: `XhciCmNetRxFrames` and `XhciCmNetTxFrames` descriptors
  (section 8.1, 128 each by default, 32 to 1024: the floor leaves room
  for the 17 frames of the largest packet, section 7.2, beside the one
  empty slot), each with a 4096-byte buffer, so 512 KiB and 2 KiB of descriptors a ring by default; one
  slot of each ring is kept empty (Spec Tables 12-19 and 12-24).
- **Memory**: below 4 GB, cached, from `cm_pool.c`, as ring 0's (record
  21 section 4.2). The buffers are allocated as page-sized common buffers
  in groups rather than one contiguous block, so a fragmented 98 SE
  machine can still start a service; the descriptors' ring is one block.
  Allocated at the first service after a start and kept until the stop,
  so a re-plug allocates nothing; a failure is a service that does not
  start (`cm.net.nomem`) with the peer left READY.
- **The index registers** are written as whole DWs with 0 in the other
  half, as ring 0's are (record 21 section 4.2's open point, which the
  P14s bench settles for both).

### 5.4 Serving the rings: the lock, the poll and the interrupt

The data rings are not ring 0: the network driver sends at DISPATCH_LEVEL
and the receive service runs in a DPC. Each service has one spin lock
(`KeAcquireSpinLock`, the NHI's own, never held across a call into the
network driver) over both rings' indices and descriptors. The thread
takes it to set up or tear down a ring; the transmit call (section 6.3)
and the service DPC take it to post and reap.

The receive service runs in one DPC (`KeInitializeDpc`), queued by:

- **the poll timer**: a one-shot `KTIMER` with a KDPC of its own (not
  the receive service's), set for `XhciCmNetPollMs` (section 8.1, default
  1, which on every target is the next clock tick: 10 to 15.6 ms on NT,
  10 to 55 ms on 98 SE and ME, record 22 section 5.3) by the thread when
  a service comes up and thereafter only by its own DPC, at that DPC's
  end, while the service is up; so `KeSetTimer` is never called on a
  timer still pending and never cancels an earlier expiry implicitly.
  Its DPC does one thing, the kick below;
- **the ring interrupt**, where record 21 section 4.5 has it connected
  and armed (`XhciCmRingInterrupt` 1 on NT; never on 98 SE and ME until
  their route is read): the receive ring's Receive Data Buffer cause is
  opened beside ring 0's by the same arm. The existing service routine
  reads and acknowledges every status DW and passes no cause to its DPC
  ([code] `cm_rirqos.c`), and its classifier knows ring 0's receive bit
  alone (`cm_rirq.c` `CmRirqClassify`), so two changes: the classifier
  takes the data ring's receive bit as this device's too (the line may be
  shared, and an unclaimed interrupt of ours would be another device's
  to the system), and the DPC, on every claim while a service is up,
  kicks the receive service unconditionally, which decides from the
  descriptors' DD bits as the poll does. No cause bitmap crosses from the
  routine to the DPC; a spurious kick costs one empty pass. The status protocol, the storm count, the self-test and the
  fall back to polling are record 21 section 4.5.2 to 4.5.6's, unchanged;
  the poll timer stays armed as the fallback, as the idle poll does for
  ring 0.

**One consumer at a time.** The timer's DPC and the interrupt DPC's
`KeInsertQueueDpc` can run the receive service on two processors at once
(a periodic timer's DPC may be queued again while it runs), so the
service is serialised by an owner word of its own, not by the KDPC:
entering, it sets the word's "pending" bit interlocked; if the "owner"
bit was already set it returns, else it takes ownership and loops,
clearing "pending" before each pass and leaving only when a pass ends
with "pending" still clear, then releasing ownership and testing
"pending" once more. So exactly one processor delivers frames, in ring
order, and a reassembly (section 7.2) is never entered twice.

A pass, under the spin lock: reaps every receive descriptor with DD set
(each a USB4 frame: its length, CRC Error, Buffer Overflow), releases the
lock, hands the frames to the network driver's receive callback in ring
order, takes the lock again, reposts each descriptor (RS 1, IE, DD 0) and
advances the consumer index; it reaps the transmit ring's completed
descriptors too; at most one ring's worth a pass, the rest left to the
next. The callback copies (section 6.3), so a descriptor is never lent
out.

**The kick**, the one way the receive service is queued, from the timer
DPC and from the interrupt DPC: it raises the in-flight count, then
`KeInsertQueueDpc` on the service's KDPC, and lowers the count again
when that returns FALSE (already queued: the queued run will see the
frames, so two kicks coalesce into one counted run). **The in-flight
count** covers each object separately and is raised **before** each
queueing, never at a DPC's entry: the timer's share before each
`KeSetTimer`, lowered at the timer DPC's end or when `KeCancelTimer`
returns TRUE (removed before it fired); the service's share at the
kick, lowered as the service DPC's last act. So a DPC dequeued on
another processor but not yet running is already counted, and
`KeRemoveQueueDpc` is not used (it has no Windows 98 evidence, record
21 section 4.5.6, and is not needed).

**The rundown**, at every end of a service (every end of section 3.8,
the `Close` of section 6.3, a stop, a surprise removal): first the
producers stop - a "closing" flag set under the lock, which `TxReserve`,
the timer's re-arm, the interrupt DPC's kick and the service all test;
the receive cause masked through record 21 section 4.5's mask by
contract; `KeCancelTimer` - then the thread waits for the outstanding
reservation, if any, to be committed or aborted (both run at
DISPATCH_LEVEL with no wait inside, and a stale token's commit or abort
still releases it, section 6.3), so no sender is still copying into a
transmit buffer; then it waits on an event the last lowering of the
in-flight count signals; and only then are the rings disabled, reset or
reused. The KDPCs and the timer live in the NHI's device extension,
never freed by a service's end. The instructions a DPC runs after its
last lowering need the image itself: as record 21 section 4.5.6 pins
the image until the restart once an interrupt was connected, the first
arming of a network poll timer pins it the same way (an extra
reference on the driver's own object, taken then and never released),
whether or not an interrupt or any other publication succeeded, so a
removal that sees the count at zero cannot unload code a DPC is still
returning through.

**The memory** stays allocated across services (section 5.3) and is
freed only at the NHI's stop or removal, and then only when the rings
were proven stopped (Ring Valid read back clear on a live host
interface). Where the host interface was lost - a surprise removal, all
ones read back, record 21 section 5.6's "the host interface lost" - the
data rings' common buffers and their adapter are retained exactly as
ring 0's are (`cm_pool.c` and `CmDmaClose`'s rule), since no read can
show that the NHI stopped writing into them.

What the poll costs (section 15 Q6): with a 10 ms tick and 128 receive
descriptors, the receive side holds at most 128 frames (about 512 KiB)
a tick, a ceiling near 50 MB/s and a ping of up to one tick each way
[inf]; the interrupt removes both limits on NT. End-to-end flow control,
where on, makes a full receive ring hold the peer back instead of
dropping (Spec 12.2), which is why it is on by default.

## 6. The interface between `xhcicm.sys` and the network drivers

### 6.1 A child PDO of the NHI

An NDIS miniport is a function driver above a PDO. `xhcicm.sys` gains a
fourth role, the bus role on its NHI FDO: while `XhciCmNet` is 1 and the
NHI is started with a connection manager selected (record 21 section
1.4.2, record 22 section 3), the FDO reports one child in
`IRP_MN_QUERY_DEVICE_RELATIONS` (BusRelations), the **network PDO**, and
calls `IoInvalidateDeviceRelations` once after the start so the PnP
manager asks. One PDO per NHI, present whether or not a peer is plugged
(section 15 Q2): the adapter then behaves as a NIC whose cable is out
when no peer is logged in, which on 98 SE avoids a devnode created and
destroyed at every plug [inf]. An orderly stop of the NHI (a rebalance,
or on 98 SE and ME a Device Manager disable) ends the service and keeps
the PDO, which the FDO reports again at its restart, as `xhci98.sys`
keeps its children across a stop (`src\hcd_pnp.c`, the stop path); a
surprise removal or a removal of the NHI reports it no more, and it is
deleted once the PnP manager has removed it, the bus-driver rule. A
start with `XhciCmNet` 0 reports it no more either (section 8.1).

The PDO answers, and nothing else:

- `IRP_MN_QUERY_ID`: the ids of section 6.2;
  `IRP_MN_QUERY_DEVICE_TEXT`: "Thunderbolt networking"; on NT
  `IRP_MN_QUERY_BUS_INFORMATION` none.
- `IRP_MN_QUERY_CAPABILITIES`: `Removable` 0, `SurpriseRemovalOK` 1,
  `UniqueID` 0, `RawDeviceOK` 0, D-state mapping from the NHI's own.
- `IRP_MN_QUERY_RESOURCES`, `_REQUIREMENTS`: none.
- `IRP_MN_QUERY_INTERFACE` for the GUID of section 6.3: the table;
  every other GUID passed back unanswered.
- start, stop, query and cancel stop and remove, surprise removal and
  removal: their bookkeeping (a removed PDO is deleted only when the NHI
  no longer reports it, the bus-driver rule).
- power: `PoStartNextPowerIrp` and complete with success; the D3 and D0
  work is the network driver's call to close and open the service
  (section 7.7) and the NHI's own (section 3.8).

On 98 SE and ME this is the same WDM bus role under NTKERN that
`xhci98.sys` plays for its root hub there; the devnode is under
`Enum\XHCICM\TBNET` (section 6.2).

### 6.2 The ids

| Query | Value |
|---|---|
| `BusQueryDeviceID` | `XHCICM\TBNET` |
| `BusQueryHardwareIDs` | `XHCICM\TBNET` |
| `BusQueryCompatibleIDs` | none |
| `BusQueryInstanceID` | `0` (one per NHI; the parent's instance makes it unique) |

A private enumerator rather than an OS one because this driver is that
bus; GitHub issue 17's point about `XHCI98\` (record 13 section 10.13,
"there is no `XHCI98` bus") does not apply where the driver named does
enumerate the child. The id is the owner's (section 15 Q2). No stock INF
matches it by construction.

### 6.3 The interface

A new shared header, `src\xhci_cmnet.h`, in `xhci_cmiso.h`'s form: a GUID
of its own, `XHCI_CM_NET_VERSION` 1, and an `INTERFACE`-headed table the
network driver asks for with `IRP_MN_QUERY_INTERFACE` (size, version) at
its initialisation, sent to the device object below it (NDIS 5:
`NdisMGetDeviceProperty`'s `NextDeviceObject`; NDIS 6: the same call).
The precedent, `GUID_XHCI_CM_ISOLATION` (task 45.35), is asked by
`xhci98.sys` at a start; this one is asked on 98 SE as well, where a
query-interface IRP built by one WDM driver and answered by another is
unread [inf]; step S5's first 98 SE leg reads it, and if NTKERN does not
carry it, the same table is returned instead by an
`IRP_MJ_INTERNAL_DEVICE_CONTROL` with a private code, which 98 SE
carries for every USB class driver.

The table (every function callable at the IRQL named; `Context` the
PDO's):

| Function | IRQL | What it does |
|---|---|---|
| `Open(Context, Callbacks, ClientContext, Config)` | PASSIVE | registers the network driver's callbacks (below), its context and its configuration: the largest Ethernet frame it will send and accept (its MTU plus 14, section 7.3); fails if one is open. Open is the service's enable: only between `Open` and `Close` does the CM publish its `network` directory (built with `prtcstns` bit 2 set when the frame above is larger than 1514), log in and accept logins; `Open` adds the directory with a Properties Changed Notification to a peer in READY (section 3.7) |
| `Close(Context)` | PASSIVE | the service's disable: ends any service with a logout (section 4.2), withdraws the `network` directory with a Properties Changed Notification, refuses logins until the next `Open` (the vendor failure of section 4.1), runs section 5.4's rundown, waits until no callback is running or can start, and drops the callbacks; the CM keeps the rings' memory for the next `Open` |
| `QueryInfo(Context, Info)` | <= DISPATCH | this adapter's MAC (section 4.4), the link state, the peer's MAC and its `prtcstns`, the link's rate and width (record 22's query-device extensions), the service's maximum frame (section 7.3) |
| `TxReserve(Context, Count, Buffers, Token)` | <= DISPATCH | all or nothing: reserves `Count` consecutive transmit descriptors and returns their buffers' virtual addresses and a token (the service's generation and the first index), or reserves nothing and returns 0 when fewer are free, when another reservation is outstanding (one at a time: the caller serialises its sends, section 7.5), when the service is closing or when none is up |
| `TxCommit(Context, Token, Count, Lengths)` | <= DISPATCH | posts the reservation with those lengths (each at most 4096) and writes the producer index once; with a token of an earlier generation (the service ended and perhaps came back in between) it posts nothing but still releases the outstanding reservation that token names, which the rundown of section 5.4 is waiting for, and returns a refusal so the sender completes its frame as not sent |
| `TxAbort(Context, Token)` | <= DISPATCH | releases the reservation, of the current generation or an earlier one alike; nothing is posted |
| `Log(Context, Label, Value)` | <= DISPATCH | records a line in `xhcicm.sys`'s log ring (record 21 section 8), so the network driver needs no log of its own |

Callbacks the network driver gives `Open`:

| Callback | IRQL | When |
|---|---|---|
| `LinkChange(ClientContext, Up)` | PASSIVE (on the thread) | the service came up (both logins done, the paths built) or went down (section 4.2, 3.8) |
| `Receive(ClientContext, Frame, Length, Flags)` | DISPATCH (the service DPC) | one USB4 frame (header and data) from the receive ring, valid until the callback returns; `Flags` carries CRC Error and Buffer Overflow, and such a frame is counted and dropped by the network driver |
| `TxDone(ClientContext, Count)` | DISPATCH | descriptors reaped, so a sender waiting on a full ring may try again |

Copying is the rule both ways: the network driver writes frames straight
into the reserved transmit buffers and copies received frames out
before `Receive` returns. No network-stack buffer is ever handed to the
host interface, so no map register, no physical address of a stack
buffer and no lifetime of one crosses the interface, on 98 SE as on 10.
A 4 KiB copy per USB4 frame is the cost (section 7.4).

### 6.4 Lifetime and order

- `Open` succeeds only between the PDO's start and its stop or removal;
  `Close` is called by the network driver's halt before the PDO's
  removal reaches the CM (NDIS halts a miniport before its PDO is
  removed).
- A surprise removal of the NHI: the CM ends the service without a
  logout, raises `LinkChange` down and runs section 5.4's rundown (the
  reservation wait included, as at every end), and the memory follows
  section 5.4's rule for a lost host interface; the network driver's own
  surprise-removal notification (section 7.6) follows. A service's
  generation increments at every end, so a sender that reserved before
  the end and commits after it is refused (section 6.3), its reservation
  released.
- The interface's reference (`InterfaceReference`,
  `InterfaceDereference`) counts the network driver's hold; the PDO
  is not deleted while it is held, as record 21 section 3.2's rundown
  protects the filters' entries.

### 6.5 As built (step S5)

`cm_net.c` is the driver half, `cm_netr.c` the rings' pure half
(`test_cm_net`, section 11.1); the engine's calls are section 3.9's and
3.10's.

- **The PDO** is created at the NHI's first start that has `XhciCmNet`
  1 under either connection manager, and reported in BusRelations at
  every such start. Its removal while
  the NHI still reports it (a disable) keeps it; a removal once it is
  reported no more retires it from the NHI, so a later start that wants
  one makes a fresh PDO. It is deleted at the NHI's removal once the PnP
  manager has removed it, or at its own removal once orphaned or retired
  (2000 and later; on 98 SE and ME such a PDO stays allocated, as more
  IRPs may follow a removal there). Its answers are
  section 6.1's, built in non-paged pool as `xhci98.sys`'s root hub
  builds its own.
- **The interface.** `IRP_MN_QUERY_INTERFACE` for `GUID_XHCI_CM_NET`,
  and the same table from `IRP_MJ_INTERNAL_DEVICE_CONTROL` with the
  private code `CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA59, METHOD_NEITHER,
  FILE_ANY_ACCESS)` (0x00222967), `Parameters.Others.Argument1` the
  caller's table with `Header.Size` and `Header.Version` set: section
  6.3's 98 SE fallback, answered on every target. Each answer
  holds one reference. Every call reaches the NHI through the PDO under
  the PDO's lock, counted, so the NHI's removal waits out a call inside.
  The shared header names the code (`XHCI_CM_NET_IOCTL_INTERFACE`, the
  coordinator's decision of 2026-10-31, before the 98 SE leg read which
  of the two NTKERN carries), and both network drivers' shared glue
  (`src\net\net_cmif.c`) asks the query first and sends the code only
  where the query is not answered (a failure, or a success with no
  table), logging which way the table came (`net.cm.route`, 1 the query,
  2 the code) through the table's own `Log`.
- **Open and Close** set the wish under the lock and queue
  `CM_WORK_NET`; the thread gives it to the engine (`TbtU4NetWanted`,
  with `prtcstns` bit 1 always, bit 0 unless `XhciCmNetE2E` is 0, bit 2
  for a frame above 1514) and reads this domain's MAC
  (`TbtU4NetIdentity`, read afresh at each start), and both calls wait
  for that pass (at most 5 s); the client and its wish change together
  under the lock. A wish made while the NHI is stopped is applied at its
  next start, the client staying open across the stop. Close drops the
  client first (no callback or reservation starts after it, and a token
  of an earlier one is refused by a new generation), then waits, however
  long, for an outstanding reservation's commit or abort and for every
  callback running to end (logged after 1 s and 5 s); the engine's logout
  runs as section 4.2 has it and the rings come down when it ends.
  `QueryInfo` answers `XHCI_CM_NET_E_STATE` until this start's MAC is
  read. Both miniports ask for it at their initialisation for up to 5 s
  (`NetCmIfMacWait`, `net_cmif.c`) and fail the initialisation without
  one (`net.mac.none`), never registering an all-zero address; an
  adapter keeps that MAC until its halt, and a later start of the NHI
  that reads another is logged at link up (`net.mac.moved`).
- **The rings** are set up by `RingsUp` on ring 1 as section 5.3 says:
  the transmit ring's base and size, then its control in one write (Valid,
  E2E where used); the receive ring's base, size (Data Buffer Size 0),
  PDF masks `0x00020004`, its Producer Index read (a non-zero one is where
  the first frame lands), Valid, then Valid, E2E and the Transmit E2E
  HopID in bits 22:12 in a second write, then the Consumer Index one slot
  behind; the transmit indexes read back equal. The memory is allocated
  there at the first service after a start (each descriptor block and
  each 4 KiB buffer its own common buffer, each asked for in whole pages:
  Windows 2000 and XP can serve a common buffer of less than a page from
  pool, 8-byte aligned - a 2 KiB descriptor block at an address ending
  008h on 2000, runtime, read through this driver's log on 2026-11-01,
  run-45 "45.59 (legs, continued)"; on XP the rings' memory refused at
  one start and not at another, its addresses not logged, the same cause
  inferred - which the 16-byte check refuses, so before
  revision 11 a start that met such a block had no service) and freed at
  the stop, or kept
  with its adapter where a ring was not read back stopped, and forgotten
  then, so no later start frees or reuses it. A service whose earlier ring
  of this start was not read back stopped is refused (`cm.net.mem.busy`),
  and `RingsUp` disables the ring and reads it back before it writes any
  descriptor.
- **The poll** is the service's only clock in this step: the data ring's
  receive cause is never opened (section 5.4's interrupt share waits for
  a leg that measures it), so its descriptors' IE raises nothing. The
  one-shot timer is armed at Link up, re-armed by its own DPC while the
  rings are on and not closing, and its DPC kicks the service DPC; the
  owner word, the in-flight count (the rings' own share while they are
  on, the timer's while it is set, the service's while queued or
  running) and the rundown are section 5.4's. The image is pinned at the
  first Link up.
- **A pass** counts the receive ring's completed descriptors from Head
  under the lock, reaps the transmit ring, takes the client for the
  pass, releases the lock, hands each frame on in ring order (an EOF PDF
  other than 2 dropped and counted, the first logged with its length and
  flags, `cm.net.rx.pdf`; frames with no client or no link dropped and
  counted), calls `TxDone` with the count reaped, then reposts the
  descriptors and writes the Consumer Index under the lock. A peer that
  sends with no credits while this host's receive ring has E2E on (Linux,
  section 2.4 point 9) is served the same way: nothing in software waits
  for a credit.
- **Link down** stops sends (Up 0, a new generation), calls
  `LinkChange(FALSE)`, waits for the outstanding reservation to be
  committed or aborted (its sender may still be copying into the buffers,
  so it is never taken back; logged after 1 s, `cm.net.res.slow`), then for
  the transmit ring to drain (500 ms, `cm.net.tx.left`). **RingsDown**
  runs the rundown, then disables both rings and reads Ring Valid back
  (`cm.net.stop-unverified` and the memory kept where it does not read
  clear); with the host interface lost it writes nothing.
- **TxCommit** checks every length (12 to 4096) and the count against
  the token before posting; a bad one releases the reservation and
  answers `XHCI_CM_NET_E_PARAM`. `TxReserve` reaps the transmit ring
  first, so a sender does not wait a tick for descriptors already done.
- **Counters and the log**: the `cm.net.*` lines at the NHI's stop
  (opens, services, frames each way, drops by cause, passes, kicks,
  timers, the rundown's outcomes), and at each event `cm.net.open`,
  `cm.net.close`, `cm.net.wanted`, `cm.net.mac`, `cm.net.rings`,
  `cm.net.link.up` (port, the peer's `prtcstns`, Gb/s),
  `cm.net.peer.mac`, `cm.net.link.down`, `cm.net.rings.down`,
  `cm.net.pdo` and `cm.net.pnp`.
- **Imports**: `KeInitializeTimer`, `KeSetTimer`, `KeCancelTimer` and
  `ObfReferenceObject` join `xhcicm.sys`'s allowlists, each
  `xhci98.sys`'s own row with its Windows 98 evidence.
- **The two connection managers.** The three operations are given to
  the one the start activates, before its start (`CmNetBindOps`): to the
  software one by `TbtU4SetNet`, to the firmware one by `TbtIcmSetNet`
  after its `TbtIcmSetNetConfig`, `port` then being the peer record's
  slot + 1 (section 3.10). The thread's pass applies `Open` and `Close`
  and reads the MAC through the same profile (`TbtIcmNetWanted`,
  `TbtIcmNetIdentity` once the firmware connection manager is bound,
  before each idle poll and at each `CM_WORK_NET`). The NHI's serial for
  the firmware connection manager's MAC (section 3.10, "The identity") is
  its PCI Express Device Serial Number, read at that activation from the
  Device Serial Number Extended Capability (id 0003h) in the extended
  list from 100h, through the same configuration path as every read of
  the NHI's own; a list that cannot be read there (all ones, 0, or 100h
  reading as offset 0's DW, as a path that reaches only the first 256
  bytes may), or a serial of zero or all ones, gives none
  (`cm.net.dsn.none`) and the engine's own value. Under the firmware
  connection manager the link's rate and lanes are the firmware's and
  unread: `QueryInfo` reports both 0, unknown.

## 7. The network drivers

### 7.1 Two images, four builds

| Image | NDIS | Systems | Toolchain |
|---|---|---|---|
| `xhcinet.sys`, x86 | 5.0, WDM lower edge (`NDIS50_MINIPORT`, `NDIS_WDM`) | 98 SE, ME, 2000, XP, Vista, 7 | MSVC 6.0 and the Windows 2000 DDK, as `xhci98.sys` x86 |
| `xhcinet.sys`, x64 | 5.1 | XP x64, Server 2003 x64, Vista x64, 7 x64 | WDK 7.1 `WNET` amd64, as `xhcicm.sys` x64 |
| `xhcinet6.sys`, x86 | 6.20 | 8, 8.1, 10 (32-bit) | WDK 7.1 `WIN7` x86 |
| `xhcinet6.sys`, x64 | 6.20 | 8, 8.1, 10 (64-bit) | WDK 7.1 `WIN7` amd64 |

One INF per package chooses by its decorated sections (section 8.2).
Vista and 7 load NDIS 5.x miniports; 8 and later do not, which is why the
NDIS 6 image exists (the roadmap line). Both share a pure core,
`src\net\net_frame.c`, with no NDIS type in it: the USB4NET framing of
section 7.2, the reassembly, the counters, host-tested. The miniports
(`src\net\ndis5\`, `src\net\ndis6\`) are thin: the NDIS entry points, the
OIDs, the packet or net-buffer walking, the interface calls. Source rules
as for every driver here: C89, the arithmetic rules, ASCII and CRLF, the
IRQL of every function stated, no `DbgPrint` (the log goes through the
interface's `Log`).

### 7.2 The frame format

**Transmit**, one Ethernet frame from the stack (destination, source,
type and payload, as NDIS hands it, with no FCS):

1. Its length L: 14 to 1514 with the default MTU; up to the section 7.3
   maximum.
2. Frames needed: m = 1 when L fits one buffer (L + 12 <= 4096), else
   ceil(L / (4096 - 12)); each USB4 frame's data at most 4084 bytes, the
   last the rest. IDS 5.1.5.2 says "4KB USB4 Frames"; this reads it as
   the 4096-byte host-interface frame including its header, which is
   `thunderbolt-net`'s split, 4084 bytes of data a frame ([tbnet]
   `TBNET_MAX_PAYLOAD_SIZE`, `tbnet_probe`'s comment).
3. `TxReserve(m)`, all or nothing; refused, the frame stays at the head
   of the miniport's own send queue until `TxDone` (section 7.5). The
   ring's floor of 32 (section 5.3) holds the largest packet's 17 frames,
   so a refusal is always a ring that will drain.
4. Each buffer: Frame Size = this frame's data bytes, Frame Index = k
   (0 to m-1), Frame ID = the adapter's packet counter, **the same in all
   m frames of the packet**, Frame Count = m; all little-endian; then the
   data bytes copied from the stack's buffers. The counter starts at 0
   and advances by one (wrapping at `0xFFFF`) after each packet, and only
   while the peer's `prtcstns` has bit 1; with bit 1 clear it stays 0
   ([tbnet] section 2.4 point 3; IDS Table 5-7's text reads as per USB4
   frame, which Linux, the reference peer, does not follow).
5. `TxCommit(m)`, and the stack's frame is completed at once: its bytes
   are in the ring.

No FCS and no padding go on the wire: the frame is what NDIS hands the
miniport, as `thunderbolt-net` sends its socket buffer ([tbnet] section
2.4 point 4; IDS Figure 5-6 draws a "Network CRC" Linux does not send).

**Receive**, from the `Receive` callback, one USB4 frame at a time:

- Dropped and counted: CRC Error or Buffer Overflow set; a length of 12
  or less (a header and no data, as Linux drops it); a Frame Size of 0 or
  above the frame's length less 12; Frame Count 0 or above 17; Frame
  Index not below Count; a packet whose summed Frame Size passes 65550.
  That capacity is IDS 5.1.5.2's, a payload "of up to 64KB" (65536 bytes)
  and the 14-byte Ethernet header, no FCS being sent ([tbnet] point 4);
  17 frames of 4084 bytes hold it. `thunderbolt-net` itself sends at most
  64 KiB a packet ([tbnet] point 6).
- A frame with Count 1 and Index 0: one Ethernet frame of Frame Size
  bytes, indicated to the stack at once.
- Count above 1: reassembled in a per-adapter buffer of 65550 bytes
  (68 KiB allocated): Index 0 starts a packet (dropping any one in
  progress, counted); each next frame must carry the next Index, the
  same Count and the same Frame ID as the packet's first frame, as
  `thunderbolt-net`'s receiver checks ([tbnet] point 3); a gap or a
  mismatch drops the packet (counted); the frame with Index Count-1
  completes it.
- An assembled frame shorter than 14 bytes is dropped. One within the
  adapter's own maximum (its MTU plus 14) is indicated. **One longer** is
  the normal case against Linux, which sends TCP segments of up to 64 KiB
  whatever this side's MTU ([tbnet] point 6): it is split by section
  7.4's receive segmentation into frames within the maximum before any
  indication, since the stack was told that maximum (NDIS 5
  `OID_GEN_MAXIMUM_TOTAL_SIZE`, NDIS 6 `MtuSize`) and no indication may
  exceed it; a longer frame that is not TCP over IPv4 or IPv6 is dropped
  and counted. Short frames are not padded by Linux ([tbnet] point 4);
  the stack takes them as they are.

### 7.3 The MTU

`XhciNetMtu` (section 8.3), the IP payload, default 1500, from 1500 to
65522. The ceiling is a chosen transmit limit, not the spec's: IDS
5.1.5.2 lets a payload reach 64 KB, and 65522 keeps the whole frame,
header included, within 64 KiB, which is `thunderbolt-net`'s own maximum
([tbnet] point 6: 68 to 65522, its receiver refusing more than 64 KiB);
the receive side takes the spec's full size regardless (section 7.2). The adapter reports a maximum frame
of the MTU plus 14 (NDIS 5 `OID_GEN_MAXIMUM_FRAME_SIZE` is the MTU,
`OID_GEN_MAXIMUM_TOTAL_SIZE` the MTU plus 14; NDIS 6
`MtuSize` in its general attributes). Above 1500 the frames go out as
section 7.2's multi-frame packets, and only to a peer whose `prtcstns`
bit 2 is 1 (IDS 5.1.5.2's recommendation; `thunderbolt-net` sets it,
[tbnet] point 2): to a peer without it a frame longer than 1514 bytes is
dropped and counted, and the log says once per service that the peer
takes no large frames. On the IP layer the peer may send more than this
side's MTU: `thunderbolt-net` does (section 7.4).

### 7.4 Checksums, segmentation and other offloads

- **Checksum offload: none, both ways.** The host interface computes no
  IP, TCP or UDP checksum; a miniport that advertised the offload would
  compute it in software, which the stack already does, and the USB4NET
  header has no field to mark a checksum partial, so every frame on the
  wire carries complete checksums, as `thunderbolt-net` computes them in
  software before posting ([tbnet] point 5). On receive the frame CRC
  (Spec 12.3.2.1) covers the fabric from host interface to host
  interface, not either host's memory, so the stack keeps verifying, as
  Linux's receiver leaves its stack to verify.
- **Segmentation**: the fabric's segmentation (a frame into tunnelled
  packets) is the host interface's, below everything here; USB4NET's
  (a large Ethernet frame into 4 KB frames) is section 7.2's, in
  software. No large-send offload (NDIS 5 `TCP_TASK_TCP_LARGE_SEND`,
  NDIS 6 LSO): with the MTU raised and a peer taking large frames the
  stack already sends up to 64 KB a frame.
- **Receive segmentation** (S0's finding, [tbnet] point 6):
  `thunderbolt-net` hands its stack's TCP segmentation to the wire whole,
  up to 64 KiB a packet, and leaves a receiver with no large-receive
  path to re-segment it, and none of these systems' stacks offers a
  miniport one. So `net_frame.c` splits, in software and before
  indication, an assembled frame longer than the adapter's maximum when
  it is TCP over IPv4 (not a fragment) or over IPv6 (no extension
  header), in this order:
  1. **Checked first, nothing rewritten yet**: the IPv4 header checksum
     and the TCP checksum over the whole original segment (with the IPv4
     or IPv6 pseudo-header), and the lengths the headers state against
     the frame's; a frame that fails is dropped and counted, so a
     corrupted frame never comes out as valid-looking pieces the stack
     can no longer refuse (RFC 791 section 3.1, RFC 9293 section 3.1).
  2. **Not split**: a segment with SYN, RST or URG set is dropped and
     counted rather than split - SYN takes one sequence number only
     once, RST ends the connection, and the urgent pointer is relative to
     the original segment's sequence number - none of which a peer's
     segmentation offload produces in a large segment [inf].
  3. **The pieces**: each takes the Ethernet, IP and TCP headers of the
     original (TCP options copied to every piece) and up to MTU less the
     IP and TCP header lengths of the payload; the IPv4 total length,
     identification (incremented per piece) and header checksum, or the
     IPv6 payload length, and the TCP sequence number are rewritten per
     piece; FIN and PSH are kept on the last piece only and CWR on the
     first only (RFC 3168); and the TCP checksum is computed again per
     piece.
- **Receive IPv4 fragmentation** (revision 14, 2026-11-08): an IPv4
  frame longer than the maximum that the split above does not take -
  another protocol (UDP, ICMP), or a datagram that is already a
  fragment - is split into IPv4 fragments of at most the maximum before
  indication, as any node may fragment a datagram whose Don't Fragment
  flag is clear (RFC 791 sections 2.3 and 3.2), and the receiving stack
  reassembles it. Read in `usb4-emu` (section 2.5): Windows 11's
  inbox USB4 P2P adapter came up with an MTU of 62000 and sends what
  fits it whole, so a 3000-byte ping from it to 98 SE went unanswered
  until its MTU was set to 1500 by hand [emu]; Linux with a raised MTU
  does the same. Checked first, nothing rewritten: the header checksum,
  the total length against the frame, the options well formed, and a
  datagram that could exist (a fragment with More Fragments carries a
  multiple of 8 bytes; offset, data and header within 65535). Don't
  Fragment set: dropped and counted (no ICMP is sent; this driver
  originates no IP traffic). The first fragment carries the original
  header whole; every later one only the options whose copied flag is
  set, zero-padded to a multiple of 4 (RFC 791 section 3.1); each
  fragment's data is the largest multiple of 8 that fits, the last
  taking the rest; the offset is the original's plus the bytes before
  it, More Fragments is set on every fragment but the last, which keeps
  the original's, the identification is kept, and the header checksum is
  computed again. The data is copied unchanged, so no upper-layer
  checksum is touched. IPv6 is never fragmented on the way (RFC 8200
  section 4.5).
  Every other frame longer than the maximum (IPv6 other than the TCP
  split's, other EtherTypes, a malformed header) is dropped and counted.
  The rules are RFC 791, 9293, 3168 (CWR) and 8200's, written for this
  code from those RFCs, not from any implementation. The cost is one
  checksum pass over the payload, beside section 6.3's copy.
- **Receive side scaling, VLAN tags, priority, interrupt moderation,
  wake-on-LAN, header-data split**: none (section 13).
- **The cost**: one copy each way per USB4 frame (section 6.3).

### 7.5 What the miniports answer

- **Medium** 802.3; `OID_GEN_MEDIA_CONNECT_STATUS` (NDIS 5) and the
  NDIS 6 link state from section 7.6; the link speed from the CM's rate
  and width (`QueryInfo`), 10 Gbit/s when unknown.
- **Addresses**: `OID_802_3_PERMANENT_ADDRESS` and `_CURRENT_ADDRESS` the
  MAC of section 4.4; no locally administered override (the standard
  `NetworkAddress` keyword is not read, section 8.3).
- **Packet filter and multicast**: every filter accepted; a point-to-point
  link delivers every frame the peer sends, broadcast and multicast
  included, and the miniport passes all of them up (the stack filters by
  address itself above an adapter that accepts its filter); the
  multicast list (32 entries) is kept for the OID and not used.
- **Statistics**: frames and bytes each way, receive errors (the drops of
  section 7.2 by reason), transmit errors and the waits on a full ring.
- **Send**: both miniports are deserialized, as an NDIS 5 miniport with
  a WDM lower edge must be (`NDIS_ATTRIBUTE_DESERIALIZE`; NDIS 6
  miniports are deserialized by definition). NDIS 5
  `MiniportSendPackets` (at or below DISPATCH_LEVEL, on any processor)
  queues each packet on the miniport's own send queue under its own spin
  lock, marks it `NDIS_STATUS_PENDING`, and serves the queue: the head
  packet is framed into a reservation (section 7.2) and completed with
  `NdisMSendComplete` once committed; a refused reservation leaves it at
  the head until `TxDone` serves the queue again.
  `NdisMSendResourcesAvailable` is a serialized miniport's call and is
  not used. NDIS 6 `MiniportSendNetBufferLists` does the same with its
  net buffer lists and `NdisMSendNetBufferListsComplete`. One sender at a
  time serves the queue (the queue lock's owner), which is the
  one-reservation-at-a-time rule of section 6.3. With the link down, or
  the adapter paused (NDIS 6), every queued and new send completes with
  `NDIS_STATUS_MEDIA_DISCONNECTED` (NDIS 6 `NDIS_STATUS_PAUSED` while
  paused).
- **Receive**: copied from the callback into a packet (NDIS 5
  `NdisMIndicateReceivePacket` from the miniport's own pool, the packet
  returned through `MiniportReturnPacket`) or a net buffer list (NDIS 6,
  its own pool, `NdisMIndicateReceiveNetBufferLists`, returned through
  `MiniportReturnNetBufferLists`), at DISPATCH_LEVEL; with the pool empty
  the frame is dropped and counted.
- **NDIS 6 pause and restart**: `MiniportInitializeEx` leaves the adapter
  Paused, and it indicates nothing until `MiniportRestart` makes it
  Running; `MiniportPause` stops indications (frames from the `Receive`
  callback dropped and counted while not Running), completes every
  queued send with `NDIS_STATUS_PAUSED`, and completes (or returns
  `NDIS_STATUS_PENDING` and later `NdisMPauseComplete`) only once every
  indicated net buffer list has come back; `Open` stays in force across a
  pause, so a pause does not log out [inf: a pause is short and the peer
  would only see a stall].
- **Reset**: nothing below is reset (the CM's service and rings are
  untouched), but the send side is, by a gate and a sender count that
  cover completion, not only commit. Every sender (the queue owner of the
  Send item above) raises a sender count under the queue lock before it
  takes a packet off the queue, detaches the packet, releases the lock,
  frames and commits it, calls `NdisMSendComplete` with no spin lock held
  (as that call requires), and lowers the count, completing the reset if
  it was the last sender and a reset is waiting. NDIS 5 `MiniportReset`
  sets the reset gate under the lock (no sender starts while it is set),
  detaches the whole remaining queue, releases the lock, completes the
  detached packets with `NDIS_STATUS_RESET_IN_PROGRESS` outside it, and
  then either returns success at once when the sender count is zero or
  returns `NDIS_STATUS_PENDING` and lets the last sender's decrement call
  `NdisMResetComplete`, raised to DISPATCH_LEVEL for the call
  (`KeRaiseIrql`, then `KeLowerIrql`, no spin lock held), since a sender
  may run below DISPATCH_LEVEL and that call is a DISPATCH_LEVEL one; the
  gate clears with the completion. So no packet
  sent before the reset is completed after it, as a deserialized
  miniport's reset must. NDIS 6.20 has no such duty
  (`MiniportResetEx` completes at once; pending sends may survive a reset
  below NDIS 6.30), its queue drained only by a pause.
- **Check for hang**: never reports a hang.

### 7.6 Link up and down

| Event | What the user sees | Path |
|---|---|---|
| a peer plugged, both logins done | the adapter connected | `LinkChange` up; NDIS 5 `NdisMIndicateStatus(NDIS_STATUS_MEDIA_CONNECT)` and complete; NDIS 6 `NDIS_STATUS_LINK_STATE` with the state and speed |
| the cable pulled, the peer's sleep or shutdown | the cable unplugged | section 3.8, `LinkChange` down; `NDIS_STATUS_MEDIA_DISCONNECT`, or the NDIS 6 link state |
| the peer logs out (its adapter disabled, its driver stopped) | the cable unplugged | section 4.2; the same |
| this adapter disabled in Device Manager or Network Connections | the adapter disabled | the miniport's halt: `Close` (a logout, the `network` directory withdrawn); then, NDIS 5, the halt waits until every packet it indicated has come back through `MiniportReturnPacket` (an outstanding count, its buffers and pool kept until then), as `MiniportHalt` must, and NDIS 6 has already had its pause drain them (section 7.5); then the PDO stopped. The CM keeps answering discovery, so the peer sees a host offering no USB4NET |
| this adapter enabled again | connected once the logins redo | `Open`; the CM logs in again |
| the NHI disabled, stopped or removed (an update, the connection manager uninstalled) | the adapter removed | the NHI's PnP order: its children first, so the miniport halts and closes, then the NHI's own end of section 3.8 |
| a safe eject from `XHCITR` or `XHCIINST /eject` | not offered for a peer (section 15 Q9) | disabling the adapter is the eject; nothing of a peer's lives behind a PCI bridge |

A peer that unplugs and plugs again with the same UUID gets the same
adapter, so the operating system keeps its IP settings for it; a
different peer on the same port does too, since the adapter is the NHI's,
not the peer's.

### 7.7 Power

The miniports declare power management with no wake: NDIS 5 answers
`OID_PNP_CAPABILITIES` with every wake-up field `NdisDeviceStateUnspecified`;
NDIS 6.20 sets general attributes of revision 2
(`NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2`) with
`PowerManagementCapabilitiesEx` pointing at an `NDIS_PM_CAPABILITIES`
whose wake-up and protocol-offload capabilities are all off and whose
minimum device states are unspecified, the member NDIS 6.20 requires
there [inf: that 98 SE's NDIS treats this as a
power-managed miniport is read by the 98 SE standby leg]. On
`OID_PNP_SET_POWER` D3 (or NDIS 6's `OID_PNP_SET_POWER` with
`NDIS_STATUS_PENDING`): the request is pended and a work item
(`NdisScheduleWorkItem`; NDIS 6 `NdisQueueIoWorkItem`) calls `Close` at
PASSIVE_LEVEL, then completes it; the link goes down. On D0 the same
route calls `Open`, and the CM logs in again when the peer is there. The CM's
own D3 (the NHI's quiescence, record 21 section 5.6) ends the service
first anyway (IDS 5.1.2: a CM ends its USB4NET services before its
domain sleeps), so a system sleep reaches the peer as a logout, and a
wake as a new login. Hibernation and resume are the same. The miniports
keep no state across D3 but their counters.

### 7.8 98 SE and ME

- `xhcinet.sys` is an NDIS 5 WDM miniport loaded by `NDIS.VXD` through its
  NT-style loader, the model 98 SE uses for every NDIS 5 `.sys` miniport;
  its imports are `NDIS.SYS` and `NTOSKRNL.EXE` names, each resolved on
  98 SE only if `NDIS.VXD` or NTKERN exports it. The import gate gets a
  third profile, `-GateProfile net98`, whose allowlist
  (`xhcinet-imports.allow`) admits each `NDIS.SYS` import only with a
  static reading of 98 SE's and ME's `NDIS.VXD` export table
  (`tools\win98se-extracted`, `tools\winme-extracted`), a
  `legal-provenance.md` section 4 row each, and each `NTOSKRNL.EXE`
  import by the existing Windows 98 rule (`xhci98-imports.allow`'s
  evidence). Step S6 does the reading before the first link.
- The 98 SE and ME install is a net-class INF section (section 8.2):
  `Class=Net`, the `Ndi` keys the network control panel reads to bind
  TCP/IP and the client to the adapter, and `DevLoader=*ndis`. The exact
  key set is read from the Windows 98 DDK's network setup documentation
  and the 98 SE CD's own `NET*.INF` files (`tools\win98se-extracted`),
  facts only, in step S6, never copied.
- A first install on 98 SE asks for the TCP/IP binding and may ask for a
  restart and for the Windows CD (its own networking files) when TCP/IP
  was never installed; that is the operating system's, said in the
  release notes [inf].

### 7.9 8, 8.1 and 10

- `xhcinet6.sys` is an NDIS 6.20 miniport: `NdisMRegisterMiniportDriver`,
  `MiniportInitializeEx` with the registration and general attributes
  (`NdisMSetMiniportAttributes`), `*IfType` 6, `*MediaType` 0,
  `*PhysicalMediaType` 0 in the INF (values NDIS reads itself, exempt
  under the owner's rule as values Windows owns, section 8.3), the
  `MiniportPnpEventNotify` surprise-removal case ending sends at once.
- On 8 to 10 `xhcicm.sys` runs only where the user installed it over
  Windows' own driver for the NHI (record 23 section 8.5.1, record 21
  section 3.6); where Windows' or Intel's driver owns the NHI there is no
  `XHCICM\TBNET` and nothing here applies.
- The x64 image and its INF's catalog are test-signed by the packager
  (design record 19), as `xhcicm.sys`'s are.

## 8. The INFs and the registry values

### 8.1 `xhcicm.sys`'s new values

Read once at the NHI's start from `Services\USB` first and then the NHI's
driver key (`cm_reg.c` `cmQuery`, record 21 section 8), each written by
every path of both NHI INFs at its default with `0x00010003`
(`FLG_ADDREG_NOCLOBBER`, DWORD), each a `check-cm-inf.ps1` value-table
row and a `tuning.md` row in the connection manager's table:

| Value | Default | Range | Meaning |
|---|---|---|---|
| `XhciCmNet` | 0 | 0 to 1 | the gate of everything in this record (the owner, 2026-10-31): 1 runs the inter-domain discovery as section 3 says, publishes USB4NET, logs in, builds the DMA paths and reports the network PDO; 0 answers no inter-domain request, sends none, advertises no host-to-host service, builds no DMA path to another host and reports no network PDO: a peer host is treated as today (UNSUPPORTED under the software CM, the events logged and nothing approved under the firmware CM). The installer's networking box and `/tbnet` write 1 (section 9) |
| `XhciCmNetPollMs` | 1 | 1 to 100 | the receive poll's period in ms while a service is up, rounded up to the clock tick (section 5.4) |
| `XhciCmNetRxFrames` | 128 | 32 to 1024 | receive descriptors (4 KiB each) a service; the floor holds the largest packet (section 5.3) |
| `XhciCmNetTxFrames` | 128 | 32 to 1024 | transmit descriptors a service; the same floor |
| `XhciCmNetE2E` | 1 | 0 to 1 | advertise and use end-to-end flow control (section 5.3); 0 for a peer whose host interface mishandles it |

A value outside its range is refused and the default stands, logged
(`cm.reg.range`), as for every `XhciCm*` value.

**When `XhciCmNet` takes effect** (the owner's question of 2026-10-31).
Like every `XhciCm*` value it is read once, at the NHI's start (record 21
section 8), never live: a change made by the installer, `XHCIINST /set`
or a hand edit applies at the controller's next start (a restart, the
NHI disabled and enabled, an update of `xhcicm.sys`). A link that is up
when the value is set to 0 therefore stays up, its service running,
until that start; at that start the stop of the old instance ends the
service in section 3.8's order (a logout, the paths and rings torn down,
the network adapter's link down and the network PDO removed with the
NHI's other children), and the new instance, reading 0, answers no
request, so the peer sees the cable as a port that never answers. Read
live, the value would let a registry write tear DMA paths down outside
the PnP order section 6.4's teardown relies on; the next start is the
one point where all of that order already runs. Set from 0 to 1, the
same start brings the network PDO and the discovery up. `XhciCmLaneBonding`,
`XhciCmApproveTimeoutMs` and `XhciCmRingInterrupt` keep their meanings
and now also govern sections 3.5, 5.2 and 5.4.

### 8.2 The network driver's INF

`xhcinet.inf` in the x86 package and its 64-bit counterpart, named by task
45.44's convention, each one file with the decorated sections choosing per
system, as `xhcicm.inf` does (record 21 section 3.3; `AGENTS.md`, "The
INF and install media"):

| Section | Systems | Image | Notes |
|---|---|---|---|
| undecorated | 98 SE, ME | `xhcinet.sys` | net-class 9x keys (section 7.8), `DevLoader=*ndis`, the values of section 8.3 under `HKR`, the `Ndi\params` subkeys for the Advanced page; from revision 17 no file copied: Setup places the image (section 9.5) |
| `.NTx86` | 2000, XP, Vista, 7 | `xhcinet.sys` | `AddService=xhcinet`, `Characteristics` `NCF_PHYSICAL` and `NCF_HAS_UI`, the other keys NDIS reads for a WDM miniport (read from the 2000 DDK's network INF documentation in S6), the values, `Ndi\params` |
| `.NTx86.6.2` | 8, 8.1, 10 | `xhcinet6.sys` | `AddService=xhcinet` with the NDIS 6 image, `*IfType`, `*MediaType`, `*PhysicalMediaType`, the values, `Ndi\params` |
| `.NTamd64` | XP x64 to 7 x64 | `xhcinet.sys` (x64) | as `.NTx86` |
| `.NTamd64.6.2` | 8 to 10 x64 | `xhcinet6.sys` (x64) | as `.NTx86.6.2`; `CatalogFile=` (record 19 section 3a) |

The models line matches `XHCICM\TBNET`. The 32-bit file's `[Manufacturer]`
decoration is the one decision the INF gate holds for every file here
(`NTx86.6.2` beside the undecorated models, which Windows 98's engine must
parse past): step S6 reads it on 98 SE, ME, 2000 and XP before it is
taken, as task 22.5 read `NTx86.6.0` for `xhci98.inf` (`AGENTS.md`).
Revision 9: the line is `%Mfg%=NetModels,NTx86,NTx86.6.2`, with
`[NetModels.NTx86]` the undecorated section's model and install. Read on
Windows 7 x86 (2026-10-31, `runs/run-45.md`, "45.59 S5 (legs)"): with
`NetModels,NTx86.6.2` alone, `pnputil -a` staged into the driver store
only the files of the `.6.2` models' install (`xhcinet6.sys`, or nothing
while that section was empty), not the undecorated section's that 7
then installs, so the NDIS 5 install's copy failed (Error 2, Code 1);
the line with `NTx86` beside it staged both images, and with no
decoration at all `xhcinet.sys` alone; `Signature` (`$Windows NT$`),
the `Compatible` line and a `[SourceDisksFiles.x86]` changed nothing.
2000 reads no models decoration and takes the undecorated section as 98
SE and ME do. The three-field line was then read on 98 SE, ME, 2000 and
XP (`AGENTS.md`'s rule for `xhci98.inf`'s line; 2026-11-01, `runs/run-45.md`,
"45.59 (legs, continued)"): each engine matched the model and installed
`xhcinet.sys`.
A new gate script, `scripts\inf-gate\check-net-inf.ps1`, holds both files
(the paths, the image per path, the service, the value table per path,
the footprint, the 98 section's net keys, no undecorated section in the
64-bit file, `CatalogFile` on x64), with self-test mutations, and
`build-driver.cmd` runs it on every build as it runs the others.

### 8.3 The network driver's values

Read once at the miniport's initialisation from the adapter's driver key
(NDIS 5 `NdisOpenConfiguration`, `NdisReadConfiguration`; NDIS 6
`NdisOpenConfigurationEx`), each written by every path of both INFs at
its default with no-overwrite, a `check-net-inf.ps1` row and a
`tuning.md` row in a new "Thunderbolt networking" table:

| Value | Default | Range | Meaning |
|---|---|---|---|
| `XhciNetMtu` | 1500 | 1500 to 65522 | the MTU (section 7.3); in the adapter's Advanced page as "MTU" |

That is the one product value: the ring sizes, the poll and the flow
control are the connection manager's (section 8.1), because the CM owns
the rings. Not read, so not written: `NetworkAddress` (no override,
section 7.5) and every NDIS standardized keyword but the three NDIS 6
media keywords, which NDIS itself reads (exempt: values Windows owns).
Each value is an `Ndi\params` entry so the Advanced page lists it with its
range, on 98 SE as on 10. A user value survives an update (the flag).

### 8.4 Counters and the log

`xhcicm.sys`'s counter block gains a networking group, written only when
`XhciCmNet` is 1: peers found, discovery failures by state, logins sent,
received, failed, logouts, services up, frames and bytes each way (the
CM's ring view), receive drops by reason, transmit full-ring waits, and
the interrupt's receive causes. The log labels are `cm.xdp.*` (the
discovery) and `cm.net.*` (the service), plus the miniport's lines through
the interface's `Log` under `net.*`. The query device record (record 25
section 4.2's) gains a peer entry: remote UUID, vendor and device texts,
the state of section 3.5, the service up or not and the peer's MAC, for
`XHCITR` and `XHCIINST /devices` (section 15 Q9).

## 9. The installer and the packaging

### 9.1 The box (task 45.60's page; the owner, 2026-10-31)

Task 45.60 builds the installer's Thunderbolt and USB4 page, shown only
when the Thunderbolt 3 / USB4 box on the Components page is ticked;
`w45-adv` owns the page, so this section says only what networking needs
from it:

- **One box**, "Thunderbolt networking", **unticked by default** on every
  system, whatever is found, and never ticked but by the user or a
  switch (the owner); on an update its first state is the value in force
  (`XhciCmNet` 1 in a present NHI's key, or machine-wide), so a computer
  that has networking shows the box ticked. The box is hidden while
  `INST_TBNET_WIRED` (`xhciinst\inst_core.h`) is 0, with this section's
  rule (greyed and unticked when the package carries no network driver
  for the system) applying once step S11 wires it. The sentence under it, without testing
  status (record 26 section 5.3.1): that it makes a network connection to
  another computer over a Thunderbolt or USB4 cable, that the other
  computer then reaches this one as over any network (file sharing
  included), and that it can be turned off later. The page's texts are
  45.60's to fit.
- **What ticking does**, in the engine (record 23's, shared by the window
  and the console): `xhcinet.inf` (the family's) staged and installed as
  the other INFs are (NT: `SetupCopyOEMInf`, and
  `UpdateDriverForPlugAndPlayDevices` when `XHCICM\TBNET` is present; 98
  SE and ME: the INF in the local copy `DevicePath` names, record 23
  section 6.2), and `XhciCmNet` 1 written into each controller's own key
  (the driver key of every NHI `xhcicm.inf` is installed on, the owner's
  rule of 2026-10-31), after the INF's install so its no-overwrite 0 does
  not stand; where no NHI is present yet (the staged-only case of record
  26 section 5.5), into `Services\USB`, the machine-wide key the CM reads
  first, since no controller key exists to write, and there too whenever
  it already holds an `XhciCmNet` (the next item's rule). It takes effect at the
  controller's next start (section 8.1). Record 23 section 5.1 updates
  the NHI before it writes values (steps 8 and 9), so the restart an
  update brings comes too early, and a same-version run restarts nothing:
  a write of `XhciCmNet`, either way, follows record 23 section 8.4's
  rule for every value it writes: it sets the run's restart flag
  (record 23 section 5.9), so the run ends asking for a restart, with
  the exit code that goes with it, and the Summary says the network
  adapter appears, or goes, after that restart. Restarting the NHI from
  the engine instead is not designed: it would drop every tunnel the
  NHI carries mid-install.
- **Unticked on a new install**: nothing of networking staged, `XhciCmNet`
  left at the INF's 0.
- **The box is the setting.** `XhciCmNet` is written from the box alone,
  to one set of places whichever way it goes: every present NHI's key,
  and `Services\USB` whenever that key already holds an `XhciCmNet` (of
  any value, the install's or a user's) or no NHI is present. Ticking
  writes 1 there, unticking where the value in force is 1 writes 0 there,
  so a machine-wide value can never outrank the box (the CM reads
  `Services\USB` first, `cm_reg.c` `cmQuery`): staged-only `/tbnet`, then
  an NHI attached, `/notbnet` and `/tbnet` again ends with 1 in both
  places; `test_inst` carries that sequence and its mirror. A box left as
  it was writes nothing, as record 26
  section 5.5's changed-value rule has it. It is therefore not an
  Advanced row, and `/set XhciCmNet=` is refused with "use /tbnet or
  /notbnet", so no second control can contradict the box. Unticking
  removes nothing: `xhcinet` stays installed as every component's
  unticking is "not now" (record 26 section 5.3), and with the value at 0
  its PDO is no longer reported at the next start (section 8.1). Ticked
  on an update: the network driver updated with the others.
- **The uninstall** (record 23 section 9, record 26 section 9) removes
  `xhcinet` with the connection manager: the adapter's devnodes, the
  service, the staged INF, and `XhciCmNet` from `Services\USB` when its
  value is the one this installer wrote.
- **The Advanced list** (task 45.54): `XhciCmNetPollMs`,
  `XhciCmNetRxFrames`, `XhciCmNetTxFrames`, `XhciCmNetE2E` on the
  Thunderbolt tab with their ranges and "Asked when" (shown when the
  networking box is ticked); `XhciNetMtu` is the adapter's own and lives
  in its Advanced page, not the installer's.

### 9.2 The console and the silent install

- `/tbnet` preselects the box ticked (needs `/thunderbolt` or `/cmonly`,
  as `/stor98` does, record 23 section 7.1) and `/notbnet` unticked, each
  making the box changed so its value is written (as `/faster` and
  `/nofaster` do, record 26 section 10); refused
  together, with `/uninstall`, `/devices` and `/eject`, and `/tbnet` where
  the package carries no network driver for the system.
- The console asks "Install Thunderbolt networking?" after the
  Thunderbolt question and its children, Enter and `/yes` giving N, the
  owner's default; `/list` shows the question and its default.
- The silent install (`INSTALL /console /yes` with switches, record 26
  section 6.5.12's form) takes networking only with `/tbnet`.
- `inst_tbnet_decide`, a pure function in the installer engine,
  host-tested case by case (`test_inst`), as `inst_tb_decide` is.

### 9.3 The packaging (the owner, 2026-10-31)

In each family folder of task 45.44's layout the network driver goes in
`cm\tbnet\`: on `98-me\` `xhcinet.inf` and `xhcinet.sys` (x86); on
`nt-x86\` the INF, `xhcinet.sys` and `xhcinet6.sys` (x86); on `nt-x64\`
the 64-bit INF, both x64 images and the INF's `.cat`. Not a top-level
`tbnet\`. The 98 SE and ME hardware wizard finds the INF only through
`DevicePath`, whose budget is 280 characters on 98 SE and 259 on ME with
one entry per folder (task 45.44); `cm\tbnet\` adds one entry. If 45.44's
reading shows the budget cannot take it, on `98-me\` alone the two files
sit loose in `cm\` beside `xhcicm.inf` (the owner's fallback), where the
existing entry finds them. `make-package.ps1` stages them from the build
(never by hand), its `PKG-*` rules gain the files per folder, the x64
image is test-signed and catalogued with the others (record 19), and
`make-release.ps1`'s list and the download readme name the folder.
Revision 13: the installed copy stays flat (record 36 section 6.1, its
Q1 (a)), so `cm\tbnet\` adds no `DevicePath` entry: one entry, the
install directory, as before, and the fallback is not needed (section
9.5).

### 9.4 Order (the owner, 2026-10-31)

Every driver and connection-manager step (S0 to S7 of section 14) and
their emulator legs run before task 45.44, with the images copied into
the guest by hand and installed through Device Manager's "Have Disk" from
that folder, as the earlier connection-manager legs were. Only the
packaging (S10) waits for 45.44's merge, and the installer wiring (S11)
for 45.44's and 45.60's.

### 9.5 As built: the packaging and the installer (steps S10 and S11; revision 13)

Revision 13 (2026-11-01), after task 45.44's family layout (record 36)
and task 45.60's page (record 26 section 5.3.5) were merged. Section 9.3
said where the files go in the download; this section says how they get
there and what the installer does with them.

**The flat staging (S10).** `make-package.ps1` stages the network
driver at the root of the flat package, as it stages the connection
manager: `xhcinet.inf` (from `src\net\xhcinet.inf`, or
`src\net\xhcinet-amd64.inf` under the name `xhcinet.inf`, as each
amd64 INF is staged), `xhcinet.sys` and `xhcinet6.sys`, the images from
`src\net\ndis5\` and `src\net\ndis6\`'s build trees of the flavour
(`-NetInfPath`, `-NetDriverPath`, `-Net6DriverPath` override them; a
stand-in package that names `-DriverPath` carries the network driver
only when it names `-NetDriverPath` too, the `-CmDriverPath` rule). The
root, because the INF's `SourceDisksNames` finds both images beside
it and the installer's local copy is that flat package (record 36
section 6.1). Each image is held to the gates `build-driver.cmd` runs
on it: its flavour marker, and the import gate (x86 `xhcinet.sys` by
the `net98` profile over `xhcinet-imports.allow`, x86 `xhcinet6.sys`
by `net6` over `xhcinet6-imports.allow`; amd64 `xhcinet.sys` by the
default amd64 rules over `xhcinet-imports-amd64.allow` and
`xhcinet6.sys` by `net6` over `xhcinet6-imports-amd64.allow`); the INF
by `check-net-inf.ps1`. `build-driver.cmd`'s gated record (record 19,
`write-gated-manifest.ps1`) hashes and captures both images around
their import gates in every flavour and both architectures, so a
`-SignOnly` staging signs the gated bytes. On amd64 `-TestSignPfx` signs
both images with the other drivers, and `xhcinet.cat`, covering
`xhcinet.inf`, `xhcinet.sys` and `xhcinet6.sys`, is one more row of
`package-common.ps1`'s `$script:PackageCatalogs`, made and signed in the
flat staging with the others; an unsigned amd64 package carries no
catalog, as for every other INF (record 19 section 3a).

**The family table (S10).** `$script:FamilyLayout` places them, and the
"`cm\tbnet\` holds no file" rule of record 36 section 9 gives way to
the table:

| Flat name | `98-me\` | `nt-x86\` | `nt-x64\` |
|---|---|---|---|
| `xhcinet.inf` | `cm\tbnet\` | `cm\tbnet\` | `cm\tbnet\` (the amd64 INF) |
| `xhcinet.sys` | `cm\tbnet\` | `cm\tbnet\` | `cm\tbnet\` (x64) |
| `xhcinet6.sys` | - | `cm\tbnet\` | `cm\tbnet\` (x64) |
| `xhcinet.cat` | - | - | `cm\tbnet\` |

`98-me\` carries no `xhcinet6.sys`: 98 SE and ME install
`[Net.Dev]`, whose one image is `xhcinet.sys` (copied by
`[Net.CopyFiles9x]` until revision 17, placed by Setup since), and a
family holds only what its systems install (record 36 section 2). The INF still names `xhcinet6.sys` in
its `[SourceDisksFiles]`, for the `.NTx86.6.2` models; a name the
family carries nowhere is skipped by `PKG-FAMILY`'s family filter and
by the installer's package check, record 36 section 3.1's rule for
`xhcihlpr.sys` in `98-me\`. The legs read the wizard on 98 SE and ME
pointed at `98-me\cm\tbnet\` and installing from it. The installer's
source map (`instPkgFiles`) gains the same four rows, local names at
the copy's root. `make-release.ps1` publishes the images only as the
gated build's (an `Assert-CmPublished` of their own, x64 signed), lists
the folder in the download readme, and `INTEGRATE.TXT` says that
leaving `cm\tbnet\` out leaves Thunderbolt networking out, and leaving
`cm\` out leaves both the connection manager and networking out.

**`DevicePath`** (section 9.3 amended). The installed copy is flat
(record 36 section 6.1, its Q1 (a)): `xhcinet.inf` and `xhcinet.sys`
sit at the install directory's root beside `xhci98.inf`, and the
install directory is `DevicePath`'s one entry, as in `3.3.0.0`. So
`cm\tbnet\` costs `DevicePath` nothing; the owner's fallback (the
files loose in `98-me\cm\`) is not needed.

**The installer (S11).** `INST_TBNET_WIRED` is 1, and section 9.1's
rules apply whole, with the amendments below. The engine carries the
network driver as a sixth INF slot, `INST_INF_NET` (`xhcinet.inf`,
service `xhcinet`), appended after Stor98's so the slot numbers of an
interrupted run's records keep their meaning; the copy, the staging,
the store trap, the installs of present devices, the update and the
uninstall are the code every slot runs, with these places made the
network driver's own:

- *Carried* (`inst_tbnet_decide`'s `carried`): the family folder holds
  `cm\tbnet\xhcinet.inf`, a `DriverVer` of this release and of this
  architecture, and the images this system installs from it
  (`xhcinet.sys`; on NT `xhcinet6.sys` too). Part of it without the
  rest is a refusal, counted apart from the connection manager's own
  three files (`cmFiles` counts the CM and filter slots by name, no
  longer every slot from `INST_INF_CM` up).
- *In the run* (`net_set`, beside `s98_set`, called by `tb_apply` and
  by the box): only while the Thunderbolt support is in the run and the
  box's final state is 1; else the slot is empty and `package_files`
  leaves `xhcinet.*` out of the local copy, so no wizard and no store
  offers the driver unasked (the rule Stor98 follows, record 26
  section 6.3).
- *A version group of its own* (`INST_GROUP_NET`, service `xhcinet`),
  whose binary is the one this system loads: `System32\Drivers\
  xhcinet6.sys` on 8 to 10, `System32\Drivers\xhcinet.sys` on 2000 to 7
  (dirid 12), `SYSTEM\xhcinet.sys` on 98 SE and ME (dirid 11). A newer
  or unreadable one refuses the slot as every group's does; with the
  box unticked the group has no verdict (an installed adapter is not
  part of the run). On 98 SE and ME a devnode whose driver key names no
  `NTMPDriver` or `PortDriver` is read for its `DeviceVxDs` (the NDIS
  miniport NDIS.VXD loads, `xhcinet.sys`), so the adapter's service
  reads `xhcinet`, an unreadable value being unknown ownership as for
  the others.
- *Ticked:* NT stages the local copy's `xhcinet.inf` through
  `SetupCopyOEMInf`, so the `XHCICM\TBNET` device the connection
  manager reports after the restart finds it in the driver store, and a
  present one (an update over a ticked install) is installed as every
  slot's device is. On 98 SE and ME the INF and `xhcinet.sys` are in
  the local copy on `DevicePath`, where the hardware wizard finds them
  when the adapter appears, and a present adapter goes through the
  helper as every 98 slot's device does (the helper's "anything to do"
  test names the slot). From revision 17 Setup also places
  `SYSTEM\xhcinet.sys` itself, before the helper's jobs (below).
- *`XhciCmNet`*: after the installs and the table's values, the write
  `inst_tbnet_decide` decides: a `REG_DWORD` into each present NHI's
  driver key (the `xhcicm.inf` devices, read again after the installs)
  and, where it says so, `Services\USB`. A 1 is written only when the
  network driver is ready for the adapter: on NT staged by this run
  (the slot not skipped by the store trap, an unknown store, a newer
  version or a failed `SetupCopyOEMInf`), on 98 SE and ME in the local
  copy; else nothing is written and the run says why, so a restart
  never brings the adapter up over an older stored package. A 0 is
  always written where decided. The value in force it reads, as the
  connection manager reads it: `Services\USB` first (a `DWORD` 1 is 1,
  any other `DWORD` 0, the CM's refusal leaving its default; a value of
  another type or unreadable counts as 0, with `mwHas` set so the box's
  write replaces it); else 1 when any present NHI's key holds 1, 0 when
  one holds another value, -1 when none holds it. A write sets the
  run's restart flag, and the Summary and the console say that the
  network adapter appears, or goes, after the restart. `/list` writes
  nothing. `XhciCmNet` stays out of the values table, so `/set
  XhciCmNet=` is refused with "use /tbnet or /notbnet".
- *Unticked:* nothing of networking is copied or staged, and on a
  new install nothing is written (the INF's 0 stands); where the value
  in force is 1, 0 is written to the same places. An installed adapter
  stays installed (section 9.1).
- *The uninstall:* `xhcinet` is one of the package's services
  (`inst_own_service`), so the adapter's devnodes (removal rank 0,
  before the NHI) and the staged or copied INF (it carries
  `AddService=xhcinet`) go with the rest. Its service is deleted with
  both its images on NT (`xhcinet.sys` and `xhcinet6.sys` in
  `System32\Drivers`, each deleted only once the service is gone or
  absent, as every service's file is), and on 98 SE and ME
  `SYSTEM\xhcinet.sys` goes with the files of `SYSTEM32\DRIVERS` (from
  revision 17 with its placement record, and a queued replacement
  withdrawn first).
  `XhciCmNet` is deleted from `Services\USB` by name, beside the
  table's machine-wide values (this replaces section 9.1's "when its
  value is the one this installer wrote": the uninstall removes the
  package, and the value means nothing without it). An NHI's own
  values go with its devnode.
- *The console and the silent install* (section 9.2 amended): "Install
  Thunderbolt networking?" after the Thunderbolt question and its
  children; Enter and `/yes` give the box's first state, which is N on
  a new install and the value in force on an update, so an update keeps
  the earlier choice; `/list` shows it. A silent install turns
  networking on only with `/tbnet`, and a silent update keeps it as it
  is unless `/tbnet` or `/notbnet` says otherwise.
- *The window:* the box is the Thunderbolt and USB4 page's last row,
  shown, greyed and unticked where not carried, its first state the
  decision's; the page's Next and Back take it into the window's state,
  and the Summary lists "Thunderbolt networking: on" or "off" when it
  writes.

**Revision 17 (2026-11-08): no copy on 98 SE and ME.** Read on 98 SE
in the emulator (task 47.44's legs; observed only, no Microsoft binary
read): after Setup's restart the hardware wizard installed the adapter
from the INF's copy in `INF\OTHER`, resolved `[SourceDisksNames]`'s disk
1 under Windows' own source path (`C:\WIN98`) and asked for the "xhci98
xHCI Host Controller Disk" for any copy row of the 9x sections, whatever
was already at the destination (also with `xhcinet.sys` already in
`SYSTEM`, and with copy flag 16); `C:\XHCI98` typed answered it. With no
`CopyFiles` in `[Net.Dev]` or `[Net.Ndis]` (the section the
`Ndi\Install` keys name, which netdi runs) it did not ask. ME did not
ask either way. So, the owner's decision of 2026-11-08:

- `xhcinet.inf`'s `[Net.Dev]` and `[Net.Ndis]` copy nothing
  (`[Net.Ndis]` stays, empty, as the `Ndi\Install` keys' target), and
  `[Net.CopyFiles9x]` is gone; the NT sections and the 64-bit file are
  unchanged. The INF gate's NET9X-NOCOPY refuses a `CopyFiles`, an
  `Include` or a `Needs` in the 9x install section or a section its
  `Ndi\Install` keys name.
- On 98 SE and ME Setup places `xhcinet.sys` in `SYSTEM` whenever
  networking is in the run, through the placement of `xhciuas.sys` and
  `xhcicm.sys` (record 26 section 5.0, drafts 68, 69 and 75;
  `sys_9x`, the folder per file from `inst_place9x_dir`): before the
  helper's jobs; refused as its version group is (`INST_GROUP_NET`); a
  missing file copied at once; a file in place that is not the
  package's (the one an earlier release's INF copied, which NDIS.VXD may
  hold) replaced at the next start through `WININIT.INI`, with the
  restart asked for; the placement's SHA-256 recorded under
  `HKLM\Software\xhci98\Placed9x`. `XhciCmNet` 1 is written only once it
  is placed or queued ("its driver could not be copied to the SYSTEM
  folder" otherwise). The uninstall withdraws a queued replacement,
  deletes the file and its record (the record also when the file is
  already gone).
- What is given up: an install of the network adapter by hand on 98 SE
  and ME (Device Manager, Have Disk, `cm\tbnet\`) with no Setup run
  copies no driver, so the adapter cannot load until `xhcinet.sys` is
  copied to `SYSTEM`. The readme's networking section says so. Keeping
  that route too would have taken a second 9x INF with copy rows,
  ranked against this one by the setup engine; not taken.

**Not done.** The four connection-manager values of section 8.1 beside
`XhciCmNet` (`XhciCmNetPollMs`, `XhciCmNetRxFrames`,
`XhciCmNetTxFrames`, `XhciCmNetE2E`) are written at their defaults by
both NHI INFs (task 45.50's rule) and have `tuning.md` rows, but are not
Advanced rows of the Settings dialog: record 26 section 5.4.1 is the
owner's list of those rows ("no other", 2026-10-31), and section 9.1's
line naming them is an owner question (section 15, Q12). `XhciNetMtu`
is the adapter's own, in its Advanced page, as section 9.1 has it.

## 10. The host-to-host model in `usb4-emu`

`usb4-emu` models a host and devices behind it, and lists inter-domain
links among its non-goals ([emu] `docs/ARCHITECTURE.md` section 1; its
ICM answers `0x10` and `0x11` with the error flag, section 4.2). Its
withdrawn host-to-host task (`9c5e88a`) named the two shapes: two QEMU
instances linked by a socket, or a peer-host model. These are this
record's requirements for that repository, under its own `AGENTS.md`,
for its maintainer (section 15 Q7):

- **E1, the link.** An inter-domain link between the `usb4-host` of one
  QEMU instance and that of another, carried over a socket chardev
  (`-chardev socket,...` on each side), plugged and unplugged by a QMP
  command on either side, which raises the hot-plug event (UPG 0 or 1)
  on both hosts' lane adapters and moves their Adapter State, as a
  device plug does today.
- **E2, discovery under the software CM.** A control packet whose route
  reaches the inter-domain lane adapter is answered with ERR_ENUM (the far
  side enumerated) or ERR_NUA (not yet), as Guide 3.3 says; a PDF 6 or 7
  packet with that route crosses the link to the far host's ring 0, with
  its route rewritten as the far host would see it; PORT_CS_19's
  Inter-Domain bit stored; the Link State registers and Lane Bonding
  between the two lane adapters, or a documented "bonding not modelled"
  with lane 1 absent.
- **E3, the data paths.** Path entries the CM writes at the host interface
  adapter and the lane adapter honoured: a transmit ring's frames leave
  only through a valid transmit path to the lane adapter at its HopID,
  cross the link, enter the far host at that HopID and reach the receive
  ring its receive path names, in frame mode with SOF and EOF PDFs and
  the frame CRC; a wrong HopID, a missing path or a disabled ring drops
  the frame (counted in the trace), so a defect in this record's paths
  shows as no traffic, not as traffic.
- **E4, end-to-end flow control** (required: both peers advertise it by
  default, section 5.3, and IDS 5.1.5.4 then requires it on): Credit
  Grant and Sync packets per Spec 12.2, credits granted per posted
  receive descriptor, a transmit ring held at zero credits, the Transmit
  E2E HopID honoured, and Falcon Ridge's credit target where that model
  exists. The legs' default runs with it; one leg per profile runs with
  `XhciCmNetE2E` 0 on this side, where IDS 5.1.5.4 has both sides run
  without it.
- **E5, the firmware CM.** On the `tb3-alpine-ridge` and
  `tb3-titan-ridge` profiles, events `0x06` and `0x07` in each row's
  layout (section 3.4) when the link comes and goes; approve `0x10`
  building the two paths from the four numbers; mailbox `0x10` and `0x11`
  (AR) and command `0x11` in two stages (TR) tearing them down; PDF 6
  and 7 frames passed between ring 0 and the link. In the model the
  emulated ICM takes the far host's UUID and its route to the link from
  the two emulators' own link handshake, not from a UUID exchange of its
  own, and passes every PDF 6 and 7 between ring 0 and the link
  untouched. What real Alpine Ridge or Titan Ridge firmware sends here is
  not public. Alpine Ridge's, read on the bench, differs: section 2.7
  lists what the model needs [bench].
- **E6, the receive interrupt**: the receive ring's Receive Data Buffer
  cause raised as ring 0's is (record 21 section 4.5's emulator support).
- **E7, the harness**: a script that starts two q35 guests on one host,
  linked, each with its own QMP and serial ports in the caller's range,
  one of them the Linux guest of `docs/LINUX-TEST.md` with its stock
  `thunderbolt` and `thunderbolt-net`; and a qtest or fabric-harness case
  per requirement.
- **E8, the peer host** (optional, the second shape): a built-in far host
  that answers discovery and USB4NET login and echoes frames, so a single
  guest can be tested; useful for host-only CI, never instead of the
  Linux peer, which is the reference.

## 11. The test plan

### 11.1 Host vectors

- `test_tbt_xdp` (new): every discovery packet's encoding against IDS
  chapter 2's figures; the UUID DW rule against IDS 1.2's and Table 5-3's
  two examples; the properties parser over IDS Table 4-10's sample block
  word for word (each entry, the texts "Apple Inc." and "Macintosh", the
  network directory) and over IDS 5.1.6's; this driver's own block built
  and parsed back; the parser's refusals one by one (offset past the
  block, wrap, deep nesting, unterminated text, a bad header, an
  oversized block, a 45-DW split with the generation changing mid-read);
  the state machine of section 3.5 over a fake peer (every resend count
  and bound, the loop, the UUID comparison both ways, bonding failure to
  PROPS, ERROR and its restart); the responder's answers of section 3.6
  case by case; USB4NET login and logout both ways, crossing logins, a
  second requester refused, a bad Service HopID, the minute of resends.
- `test_u4xd` (new, step S3) over `test_u4cm`'s fake fabric
  (`u4_fakefab.c`): a device router standing for the far host's, every
  request to it answered ERR_ENUM, and a second engine of `tbt_xdp.c`'s
  as the far host's connection manager; both bonding roles and none, the
  Inter-Domain bit, the two path entries and their credits read back,
  the order up and down, an unplug at each stage before the service and
  during it, the stop's Logout, no answer and the peer's restart, a
  loop, the network driver's close and open, one service per NHI, rings
  refused, and `XhciCmNet` 0.
- `test_tbt_icm` (step S4): events `0x06` and `0x07` and the approve and
  teardown per row, synthesised from `tb_msgs.h`'s structures, as the
  other non-AR vectors are (record 21 section 5.2), and the AR mailbox
  pair (port A and B, data 1 and 2); a far host of `tbt_xdp.c`'s engine
  in the software CM's mode, the firmware passing its packets, to the
  service up (rings, approve, link, in that order) and down (the link at
  the close, the teardown, then the rings) on every row, Falcon Ridge's
  ring 2 without E2E; the unplug (no Logout, no teardown); the approve's
  error flag, no answer (the teardown, the fence) and a disconnect in its
  wait; Not Ready before a record; the keying (announced again, another
  host at the port, a device there, a link not direct, a host where a
  device was); the order of a host's and a device's announcement in one
  wait; Lost and the owed teardown, sent before the next approve and
  settled by a disconnect; the rediscovery's reconciliation; the MAC
  from the serial; and `XhciCmNet` 0.
- `test_tbt_ring`: PDF 6 and 7 in the dispatch, the matching rule with the
  CM bit, a stray response.
- The data rings (`test_cm_net`, new, against `tbt_fake.h`): frame-mode
  ring setup words, the PDF masks, E2E words and their order, the Falcon
  Ridge ring and HopID, descriptor posting and reaping, the empty slot,
  the poll and interrupt paths to one DPC.
- `test_net_frame` (new, `src\net\net_frame.c`): transmit framing at
  lengths 14, 60, 1514, 4084, 4085, 65536; the Frame ID wrap; reassembly
  in order, with a gap, a repeated Index 0, a wrong Count, an oversized
  sum, CRC Error and Buffer Overflow flags; the MAC derivation of IDS
  5.1.5.3 against the hand-computed vector.
- `test_inst`: `inst_tbnet_decide` and the switches' refusals.

### 11.2 Gates

The import gate's new profiles and allowlists (`xhcinet` x86 with the
98 SE and ME `NDIS.VXD` evidence, x64, `xhcinet6` x86 and x64 against the
8.1 and 10 export sets, record 11's amd64 rule), `check-net-inf.ps1`, the
CM INF gate's five new rows, the packager's file rules, `check-text.ps1`.

### 11.3 The legs (q35, the emulator of section 10)

Each leg: one guest of this project's package and one Linux guest with
stock `thunderbolt-net`, the link plugged after both are up; read the
Windows adapter's state, an IP each side (static, or the link-local
addresses each system gives itself), `ping` both ways, a TCP transfer of
at least 100 MB both ways (a file copy over SMB to Samba on the Linux
side where the system has a client, else FTP with the system's own
client), then unplug, replug, adapter disable and
enable, the NHI disabled and enabled, and on the systems that sleep a
standby and a resume. Read: the CM's log (`cm.xdp.*`, `cm.net.*`), the
Linux side's `dmesg` with `thunderbolt.dyndbg=+p`, the counters, and that
no frame was lost at the default poll on an idle link.

| System | Software CM (`usb4-generic`) | Firmware CM (`tb3-alpine-ridge`, AR row) | Firmware CM (`tb3-titan-ridge`, TR row) | Notes |
|---|---|---|---|---|
| 98 SE | yes | yes | - | the 9x net INF, the first-install TCP/IP binding, file sharing to Samba; polled only |
| ME | yes | - | - | as 98 SE |
| 2000 SP4 | yes | yes | yes | polled and, with `XhciCmRingInterrupt` 1, interrupt; the TR row's staged teardown |
| XP SP3 | yes | yes | - | as 2000; SMB to Samba |
| 7 x86 | yes | - | - | the NDIS 5 image under NDIS 6.x's wrapper |
| XP x64 | yes | - | - | the x64 NDIS 5 image on NT 5.2 |
| 7 x64 | yes | - | - | the x64 NDIS 5 image on NT 6.1 |
| 10 x86 (or 8.1 x86) | yes | - | - | the x86 NDIS 6 image |
| 10 x64 (or 8.1 x64) | yes | - | - | the x64 NDIS 6 image and its catalog |

| 98 SE to 2000 | one leg | - | - | two guests of this project's, no Linux: this driver against itself |
| 2000 | one leg | - | - | `XhciCmNet` 0: the port stays UNSUPPORTED, no request answered, Linux gives up its discovery, no DMA path |

Before task 45.44 the images are copied into each guest by hand (section
9.4). The x86 ones need no signature. The x64 ones must carry the
project's test signature, and the 8 to 10 x64 one its catalog too, and
`AGENTS.md` gives signing to the packager alone (design record 19): so
the x64 legs run on a **leg kit** the packager stages before 45.44 -
`make-package.ps1` taught to put the network driver into a development
package of today's layout, never published, signed and catalogued with
`-TestSignPfx` where the key is held (S8a, section 14) - and where no
such kit can be made on the leg host, the x64 legs are reported owed
until S10's package. A clone without the key stages the kit unsigned
and runs the x86 legs only. Whether the brief's in-guest throwaway
signature (task 43.36, `runs/run-43-24.md`) may stand in is section 15
Q11, not this record's to decide.

### 11.4 The bench (the owner's)

The P14s (Alpine Ridge, firmware CM) under 98 SE and 2000 against a
Linux laptop on Thunderbolt 3, and a USB4 host under the software CM
against Linux, and, where the owner has one, against a Windows 10 or 11
machine with Intel's or Microsoft's networking (section 2.5): what is
read there is what the release notes may claim. The bench also settles
the index-register write of section 5.3 and the firmware's passing of
PDF 6 and 7 (section 3.4).

## 12. Security

- **What a peer gets.** A DMA path of section 5 ends in this host's own
  host interface adapter: the peer's frames are written by the NHI only
  into the receive buffers this driver posted, each bounded by its Data
  Buffer Size (Buffer Overflow, not an overrun), and the peer reads
  nothing. Unlike a PCIe tunnel (record 21 section 5.4, where an
  approved device reaches all of memory on every target here), a peer
  host gets no address in this machine's memory. What it gets is a
  network interface: every service bound to that adapter (98 SE's File
  and Printer Sharing where installed, NT's Server service, any program
  listening on all addresses) becomes reachable from the peer, as from
  any Ethernet.
- **Who is accepted.** No user interface asks (record 21 section 5.4's
  reason): with `XhciCmNet` 1, any host that completes discovery and logs
  in gets the service. The consent is the installer's box, unticked by
  default (the owner), and `XhciCmNet` 0 by default in the INF. Section 15
  Q3 puts the alternatives.
- **What the code must withstand.** A hostile or broken peer sends
  malformed properties blocks (section 3.7's bounded parser), packets
  with wrong lengths and reserved fields (section 3.2), logins with
  HopIDs out of range (section 4.3), floods of requests (one answer each,
  no state grown per request; at most one peer record per port), and
  frames with wrong headers, counts and sizes (section 7.2's checks, the
  64 KiB reassembly buffer, no allocation per frame). None of these
  reaches anything but the CM's own state and the counters.
- **A device pretending to be a host.** A Thunderbolt or USB4 device that
  answers ERR_ENUM and the UUID exchange is treated as a peer host and
  gets exactly a peer's access: the rings, no PCIe tunnel. Under the
  firmware CM the firmware decides what is a host.
- **The release notes** say it in those terms: the networking box makes
  this computer reachable over the cable as over a network, a plugged
  host is accepted automatically, and turning it off is the box or
  `XhciCmNet` 0.

## 13. What stays unsupported

- More than one peer at a time per NHI, and more than one USB4NET service
  per peer: a second inter-domain link on the same NHI is discovered and
  answered, its login refused with the vendor failure while one is up
  (one ring pair; the firmware CM's one tunnel). A later revision may
  give each USB4 port its own PDO (section 15 Q2).
- A peer behind a device router (an inter-domain link at depth 2 or more,
  through a dock or a hub): the depth-2 child read keeps its ERR_ENUM
  branch as today (record 34's minimal chain does not reach other
  domains); under the firmware CM a connected event whose local route
  has more than one level, or on Falcon and Alpine Ridge whose link info
  depth is not 0 (the depth of the router the far host hangs off), is
  logged and gets no record (section 3.10).
- Time synchronisation across the link (IDS 3.5): no `timesync` entry, so
  every peer disables it.
- Initiating an asymmetric link, Gen 4 link changes, CL states on the
  link: as record 22 section 1.3.
- Any other inter-domain service (Linux's `thunderbolt-stream`, a DMA
  test, vendor directories): their directories parsed and ignored.
- Checksum, segmentation and every other offload (section 7.4); VLAN and
  priority tags passed as data only; wake-on-LAN; a locally administered
  MAC override.
- Windows 8 to 10 where Windows' or Intel's driver owns the NHI (section
  7.9); Apple's Thunderbolt hosts and Mac-to-Mac XDomain (record 22
  section 19.7).
- Bridging, Internet Connection Sharing and similar features above the
  adapter: the operating system's, untested here, not designed against.
- Any wire behaviour of Windows' own Thunderbolt networking beyond IDS
  chapter 5 (section 2.5).

## 14. The implementation steps

Sizes are lines of C (driver and tests apart) and working days for one
person who knows the code, in record 34 section 10.1's manner.

| Step | What | Size | Order |
|---|---|---|---|
| S0 | Fetch `drivers/net/thunderbolt` at the pinned commit into `external/linux/net-thunderbolt/`, `external/README.md`'s row and fetch lines; read section 2.4's eight points; amend this record's [tbnet-owed] places, reviewed | 0.5 to 1 day | first; before S1's USB4NET half and S6's framing |
| S1 | `tbt_xdp.c`/`.h`, pure: the packets, the UUID rule, the properties builder and parser, the discovery and service state machines, the responder, the HopID choice, the MAC derivation; `test_tbt_xdp` | 1,500 to 2,000 lines, 1,000 to 1,500 of tests; 5 to 7 days | after S0 for USB4NET |
| S2 | Ring 0: PDF 6 and 7 in `tbt_regs.h` and `tbt_ring.c`'s dispatch, the request matching, the thread's handling of a peer request during a busy-poll and an interrupt wake; `test_tbt_ring` rows | 300 to 400 lines; 2 days | with S1 |
| S3 | The software CM: `tbtU4Router`'s branch, `TBT_U4_XDOMAIN`, PORT_CS_19, lane bonding both roles, lane 1 disable, the two path entries and credits, the order up and down, the unplug; `test_u4cm` two-domain cases | 1,200 to 1,800 lines; 6 to 8 days | after S1, S2 |
| S4 | The firmware CM: events `0x06`, `0x07` per row, approve, the AR mailbox and TR staged teardown, the peer record; `test_tbt_icm` rows | 500 to 800 lines; 3 to 4 days | after S1, S2; with or after S3 |
| S5 | The data rings and the interface: `xhci_cmnet.h`, the network PDO (bus role), `QUERY_INTERFACE` (and its 98 SE reading, with the internal-IOCTL fallback if needed), ring allocation in `cm_pool.c`, the lock, the poll timer, the receive cause in the existing interrupt DPC, the five values in `cm_vals.c`/`cm_reg.c` and both NHI INFs, `check-cm-inf.ps1` rows, the import rows (`KeSetTimerEx`, `KeInitializeDpc` and the rest, with their Windows 98 evidence), counters, log, the query device's peer entry; `test_cm_net` | 1,500 to 2,000 lines; 7 to 9 days | after S3 or S4 |
| S6 | `xhcinet.sys`: `net_frame.c` and `test_net_frame`; the NDIS 5 miniport; the x86 (MSVC 6.0, 2000 DDK) and x64 (WDK 7.1) builds in `build-driver.cmd`; the `net98` import profile with the `NDIS.VXD` export reading and `legal-provenance.md` rows; `xhcinet.inf` (and x64), the 9x net keys read from the 98 DDK and CD, the `NTx86.6.2` decoration read on 98 SE, ME, 2000 and XP; `check-net-inf.ps1` | 2,000 to 2,500 lines; 8 to 10 days | after S5's interface header; S0 for framing |
| S7 | `xhcinet6.sys`: the NDIS 6.20 miniport over the same core, x86 and x64, its INF sections, the import allowlists against 8.1's and 10's exports, the x64 catalog path | 1,500 to 2,000 lines; 6 to 8 days | after S6's core |
| S8 | `usb4-emu`'s host-to-host model, E1 to E7 (E8 optional), in that repository | 1,500 to 2,500 lines there; 6 to 10 days | the owner's for that repository; S3's legs need E1 to E3, S4's E5 |
| S8a | The x64 leg kit (section 11.3): `make-package.ps1` stages the network driver into a never-published development package of today's layout, signed and catalogued by `-TestSignPfx` where the key is held; the packaging regression fixture gains the case | 100 to 200 lines of scripts; 1 day | before S9's x64 legs; subject to Q11 |
| S9 | The legs of section 11.3, images copied into the guests by hand | 6 to 8 days | after S6 to S8 (S8a for x64); before 45.44 |
| S10 | **After 45.44 is merged.** The packaging of section 9.3: `cm\tbnet\` (or loose in `cm\` on `98-me\`), `make-package.ps1`, `PKG-*` rules, signing and the catalog, `make-release.ps1`, the download readme | 300 to 500 lines of scripts; 2 to 3 days | after 45.44 |
| S11 | **After 45.44 and 45.60 are merged.** The installer of sections 9.1 and 9.2: the box on 45.60's page, `/tbnet` and `/notbnet`, the console question, `inst_tbnet_decide`, staging and installing `xhcinet.inf`, `XhciCmNet`'s write, update and uninstall, 45.54's Advanced rows; `test_inst`; the window's legs on 98 SE, XP and 7 | 600 to 900 lines; 3 to 5 days | after 45.44 and 45.60 |
| S12 | Documents: `tuning.md`'s rows (S5, S6), `source-files.md`, `legal-provenance.md` section 3 and 4 rows, `build-and-test.md`'s network leg, the release notes' text (handed to the coordinator) | 1 to 2 days | with each step |

Total, the rows summed: about 9,000 to 12,500 lines of driver and script
code and 2,500 to 3,500 of tests in this repository, 45 to 60 working
days, nine to twelve weeks for one person, plus `usb4-emu`'s share. The
least certain parts: the 98 SE NDIS 5 import surface and net INF (S6),
the query interface on 98 SE (S5), and the emulator's data path (S8).

## 15. The owner's questions

**Answered (owner, 2026-10-31).** Networking stays in `3.4.0.0`. Q3 (a):
any peer that logs in gets the service. Q11 (a): a packager-made x64 leg
kit, signed and catalogued with the project's test key; no worker signs:
when the kit is staged, the worker reports the exact `make-package.ps1`
command for the owner to run, and waits. Q1, Q2, Q4 to Q10 as
recommended: Q1 (a), the software CM first; Q2 (a), one network PDO per
NHI, `XHCICM\TBNET`; Q4 (a), `vendorid` 0 with the text "xHCI98"; Q5
(a), MTU 1500 by default, up to 65522; Q6 (c), the data path follows
`XhciCmRingInterrupt`, reconsidered after the q35 readings; Q7 (a), two
QEMU instances with a Linux peer, the built-in peer optional; Q8 (a),
lane bonding in both roles; Q9 (a), the tools list a peer read-only; Q10:
98 SE and ME in this work if S6's reading finds every `NDIS.SYS` import
the miniport needs among `NDIS.VXD`'s exports, else NT first. The
questions as asked follow.

- **Q1, which connection manager first.** (a) The software CM's half (S3)
  and its legs first, the firmware CM's (S4) after `usb4-emu`'s E5; (b)
  both together; (c) the firmware CM first, for the P14s. Recommended:
  (a): Linux on q35 already runs the software CM against `usb4-generic`
  ([emu] `LINUX-TEST.md`), so its legs need only E1 to E3, and the
  firmware CM's frames are synthesised from Linux's structures until E5
  exists.
- **Q2, the adapter model and its id.** (a) One network PDO per NHI,
  present while the CM runs with `XhciCmNet` 1, one peer at a time, id
  `XHCICM\TBNET`; (b) one per USB4 port (two adapters on a two-port
  host), each its own peer; (c) a PDO per peer, created at login and
  removed at the end. Recommended: (a): the fewest devnodes on 98 SE, the
  adapter's settings kept across plugs, and one ring pair; (b) is the
  later step if two peers at once are wanted. The id: `XHCICM\TBNET`
  recommended, or one under another enumerator of the owner's choosing.
- **Q3, the security default.** (a) With the box ticked, any peer that
  logs in gets the service (as Linux and, by its documentation, Windows
  and macOS peers behave); (b) (a) plus `XhciCmNetPeers`, a list of peer
  UUIDs the user fills from the log, empty meaning any; (c) only listed
  peers, so the box alone does nothing until a UUID is entered.
  Recommended: (a): the box is already the consent, unticked by default,
  a peer gets no memory access (section 12), and (b) can follow without
  changing (a)'s behaviour.
- **Q4, this host's `vendorid`.** The required `vendorid` is a USB-IF
  assigned number (IDS 4.3.4.2), and this project has none. (a) 0, with
  the vendor text "xHCI98"; (b) another number the owner holds or
  chooses. Recommended: (a): Linux requires the entry's presence and
  logs its value ([linux] `populate_properties`), nothing found here
  acts on it, and a number that is not this project's is worse than 0.
- **Q5, the MTU.** (a) 1500 by default, up to 65522 in the adapter's
  Advanced page; (b) a larger default (for example 9000) for speed.
  Recommended: (a): a peer whose MTU is 1500 then works with no setting,
  and a user who raises it on both ends gets the large frames.
- **Q6, the data path's interrupt.** (a) It follows
  `XhciCmRingInterrupt` (0 by default, record 21 section 4.5.10's Q2):
  polled at the tick unless the user sets 1 on NT; (b) networking turns
  the interrupt on for its ring on NT whatever that value says, the
  self-test still deciding; (c) (a) now, and the default reconsidered
  after the q35 readings of section 11.3. Recommended: (c): the owner
  kept the interrupt off until a hardware reading, and the legs measure
  what polling costs this service.
- **Q7, `usb4-emu`'s model.** (a) Two QEMU instances over a socket with a
  Linux guest as the far side (E1 to E7); (b) the peer-host model (E8)
  alone; (c) both. Recommended: (a), with E8 optional later: Linux's
  `thunderbolt-net` is the reference the roadmap line names, and only a
  real far side catches a difference between the two readings of the
  spec.
- **Q8, lane bonding on the link.** (a) Both roles, as IDS 3.4.1 and Linux
  do, behind `XhciCmLaneBonding`; (b) never: answer Not Supported and
  disable lane 1, single-lane only. Recommended: (a): a peer that bonds
  expects it, and the bonding code of record 22 section 6.5 is reused;
  (b) halves the link's rate.
- **Q9, the tools.** (a) `XHCITR` and `XHCIINST /devices` list a peer host
  read-only (section 8.4), with no eject; (b) not in this release.
  Recommended: (a): a peer is otherwise invisible but in the log.
- **Q10, scope on 98 SE and ME.** (a) All targets in this release; (b) NT
  first, 98 SE and ME after their NDIS 5 import reading and net INF leg,
  with a release-notes line. Recommended: (a) if S6's 98 SE import reading
  finds every needed `NDIS.SYS` export, else (b) for that release.
- **Q11, the x64 legs before 45.44** (section 11.3). (a) The packager's
  leg kit (S8a), signed and catalogued with the project's test key where
  it is held, the only route `AGENTS.md` and design record 19 name for a
  signature; (b) the brief's in-guest throwaway signature of task 43.36
  (`runs/run-43-24.md`), never published, which leaves the 8 to 10 x64
  catalog unmade; (c) no x64 leg until S10's package. Recommended: (a);
  (c) where the key is not on the leg host.

- **Q12, the networking values in the Settings dialog** (revision 13,
  section 9.5). (a) As record 26 section 5.4.1's list stands: the four
  connection-manager values written at their defaults by the INFs, in
  `tuning.md`, no Advanced row; (b) four Advanced rows on the
  Thunderbolt tab, shown while the networking box is ticked, as section
  9.1 first said. Recommended: (a) for `3.4.0.0`: the defaults are what
  the legs read, and the owner's list of 2026-10-31 named no other rows;
  (b) is a small later change if the bench asks for a tuning.

## 16. Open, and the risks

- **`thunderbolt-net`'s wire details** (section 2.4): until S0 reads them,
  the FCS, the Frame ID rule and the login's crossing behaviour are the
  spec's reading; a mismatch with Linux is the one failure that makes
  the link unusable against the reference.
- **The firmware CM's pass-through of PDF 6 and 7 on Alpine Ridge**
  (section 3.4): inferred from Linux's code path; read only on the P14s.
- **98 SE's NDIS 5 import surface and net INF** (section 7.8): unread; a
  missing export stops the miniport loading with no message.
- **`IRP_MN_QUERY_INTERFACE` between two WDM drivers on 98 SE** (section
  6.3): unread; the fallback is designed.
- **The firmware CM's cold start against Linux** (section 3.10; read on
  the emulator's Alpine Ridge model, `runs/run-45.md`, "45.59 S5
  (legs)"): Linux sends its UUID Requests from the moment the link is up
  and gives up after about 20 s; a far end that gave up before this
  driver bound was not restarted, the service waiting for a re-plug.
  Revision 10 sends a UUID Request at the record's start (section
  3.11, from task 45-E.15's capture: the firmware answers the far end's
  request itself); to be read again on a leg.
- **The index-register write** (section 5.3): as for ring 0, unverified on
  hardware.
- **Polling's throughput** (section 5.4): a ceiling of tens of MB/s at a
  10 ms tick [inf], measured by the legs.
- **The receive ring's two-write E2E enable** (section 5.3): Linux's
  order; the Spec's one-write note names the transmit ring only.
- **Falcon Ridge's E2E quirk** (section 4.3): Linux's alone, no capture.
- **The 4 KB frame's reading** (section 7.2): 4096 bytes with the header,
  by the descriptor's limit; S0 confirms.
- **The `usb4-emu` model** (section 10): its own repository's work, on
  which every leg waits.

## Appendix A. The binary readings behind `legal-provenance.md` section 4

Each item is a reading of a third-party binary, in the words of the
`docs/contributing/legal-provenance.md` section 4 row that indexes it by
method; that row keeps the fact, the method tag, the binary and a pointer
to the item here.
References inside an item ("as above", "the next row", "the same",
"section 2") are to that table and that file as the row stood there; read
them from the row that points here and its neighbours.

| Item | Fact | Method | Where recorded |
|---|---|---|---|
| A.1 | **The NDIS.SYS imports of `xhcinet.sys` x86 on 98 SE and ME (task 45.59 step S6, design record 35 section 7.8)**: each of its 24 `NDIS.SYS` pairs is a NUL-delimited name in `WINDOWS\SYSTEM\NDIS.VXD` of 98 SE (166,029 bytes, SHA-256 `71C52BC6AEDD76AED7B6E3D87B413689C3C711A1D896DA9FA36435BA1363FF94`, dated 1999-04-23) and of ME (157,397 bytes, `9E0F9ADA90BDC93985858D14A451AB706584FFC4E6F49BA69F48C00C7A332DB2`, 2000-06-08), the files of the q35 base images, whose LE resident and non-resident name tables hold only `NDIS` and `NDIS_DDB`; the reading discriminates (`NdisQueryBufferSafe` is a name of ME's file and not of 98 SE's; `NdisAllocateNetBufferList` of neither). Besides: each pair is declared in the Windows 2000 DDK's `BINARY_COMPATIBLE` `ndis.h` "so that we don't use functions that aren't available on Windows 9x" (the header's comment, line 27), and each is among the imports of `usb-wdm-free`'s `cdcnet.sys` (`src/net/cdcnet/imports-ndis5.txt` at `57ba809`), an NDIS 5.0 WDM miniport recorded loading and passing traffic on 98 SE and ME (`docs/testing/2026-10-22-cdcnet-9x.md` there). `xhcinet.sys`'s own load on 98 SE and ME was then read (2026-11-01, `runs/run-45.md` "45.59 (legs, continued)": loaded, the service up, ping both ways, in the emulator) | static: export names alone (a NUL-delimited name search with a script of this project, and the LE header's two name tables), the owner's rule of 2026-10-31, 14:40, which admits a Microsoft binary's export names for the import gate's baselines and nothing else; documentation (the DDK header); runtime (another driver's recorded load, QEMU only) | `scripts/import-gate/xhcinet-imports.allow` (each `NDIS.SYS` row: `bc-header`, `ndisvxd-name 98SE ME`, `w98-runtime cdcnet-9x`), `ndisvxd-baselines.expected` (the two files) and `net98-runtime.list`, enforced by `check-imports.ps1 -GateProfile net98` |
| A.2 | **The imports of `xhcinet6.sys` on 8, 8.1 and 10, x86 and x64 (task 45.59 step S7, design record 35 section 7.9)**: every `NTOSKRNL.EXE`, `HAL.DLL` and `NDIS.SYS` name it imports is in the export tables of the 8.1 and 10 `ntoskrnl.exe`, `hal.dll` and `ndis.sys` of its architecture, and on x86 also of Windows 7 SP1 x86's kernel and HAL; the 8.1 and 10 x86 `hal.dll` export `KfAcquireSpinLock` and `KfReleaseSpinLock` as forwarders to the kernel (`dumpbin /exports`: "forwarded to ntoskrnl.KfAcquireSpinLock"), which the kernel exports. The files: Windows 8.1 Update x86 and x64 (`ntoskrnl.exe` 6.3.9600.17415, `hal.dll` 6.3.9600.17196, `ndis.sys` 6.3.9600.17399) and Windows 10 x86 and x64 (`ntoskrnl.exe` 10.0.19041.2965, `hal.dll` 10.0.19041.2728, `ndis.sys` 10.0.19041.1387), each one's size and SHA-256 in its manifest, taken by the owner on 2026-10-31 with `7z e` out of the project's q35 base images, read-only, into the untracked `tools\win81-x86-extracted` and the three others. Only the export names were read (the owner, 2026-10-31: export-table reads for the gate's baselines, export names only); no code, no other header | static (`dumpbin /exports`, run by the gate at every build; nothing executed, nothing disassembled) | `scripts/import-gate/net6-win81-x86-baselines.expected` and its three siblings, `xhcinet6-imports.allow` and `xhcinet6-imports-amd64.allow`, enforced by `check-imports.ps1 -GateProfile net6` |

## Sources

- USB4 Inter-Domain Service Protocol, Version 2.0, Rev. 1.0 (November
  2025): sections 1.2, 2.1 to 2.12, 3.1 to 3.7, 4.1 to 4.3.9, 5.1 to
  5.1.6, 6.1 to 6.3.1.1.1, Appendix A; Tables 2-1 to 2-9, 4-1 to 4-10, 5-1
  to 5-8, 6-1, 6-2.
- USB4 Specification 2.0 (November 2025): 6.4.2.11, 6.4.2.12, chapter 12
  (12.1 to 12.7, Tables 12-1 to 12-38); USB4 Connection Manager Guide 2.0:
  1 (rings), 3.3, 5.2.1, 5.2.2, 5.7, 5.7.1 to 5.7.3, 6.1 Table 6-1, 7.1,
  7.5.2.
- Linux `drivers/thunderbolt` at `c6859eed`, facts only: `xdomain.c`
  (`tb_xdomain_state_work`, `tb_xdp_handle_request`, `tb_xdomain_match`,
  `tb_xdp_uuid_request`, `tb_xdp_properties_request`,
  `tb_xdp_properties_response`, `populate_properties`,
  `update_property_block`, `tb_xdomain_init`, `tb_xdomain_alloc`,
  `tb_xdomain_alloc_in_hopid`, `tb_xdomain_alloc_out_hopid`), `property.c`
  (`__tb_property_parse_dir`, `tb_property_entry_valid`,
  `TB_PROPERTY_MAX_DEPTH`), `tb_msgs.h` (the XDomain and ICM XDomain
  structures, `enum tb_xdp_error`, `TB_XDP_PROPERTIES_MAX_*`), `icm.c`
  (`icm_fr_approve_xdomain_paths`, `icm_fr_disconnect_xdomain_paths`,
  `icm_tr_approve_xdomain_paths`, `icm_tr_disconnect_xdomain_paths`,
  `icm_fr_xdomain_connected`, `icm_tr_xdomain_connected`), `tb.c`
  (`tb_scan_port`, `tb_scan_xdomain`, `tb_approve_xdomain_paths`),
  `tunnel.c` (`tb_tunnel_alloc_dma`, `tb_dma_init_rx_path`,
  `tb_dma_init_tx_path`, `TB_DMA_*`), `usb4.c`
  (`usb4_port_configure_xdomain`), `switch.c` (`tb_switch_set_uuid`),
  `nhi.c` (`tb_ring_start`, `nhi_alloc_hop`), `nhi_regs.h`, `nhi.h`,
  `pci.c` (`QUIRK_E2E`), `tb_regs.h` (`PORT_CS_19_PID`), `domain.c`.
- Linux `Documentation/admin-guide/thunderbolt.rst`, "Networking over
  Thunderbolt cable", as `usb4-emu` carries it.
- Linux `drivers/net/thunderbolt`: not read (section 2.4).
- `usb4-emu` at `1bfa0bb`: `docs/ARCHITECTURE.md` sections 1 and 4.2,
  `docs/ICM-PROTOCOL.md` section 10.1, `docs/CONTROLLER-CATALOG.md`,
  `docs/LINUX-TEST.md`; its withdrawn task `9c5e88a` (removed in
  `90aad1c`).
- This project: design records 13 (section 10.13), 19, 21 (sections 1.3,
  1.4, 3.2, 3.3, 4.1 to 4.5, 5.1 to 5.6, 8, 8.1), 22 (sections 1.3, 4.4,
  5.1 to 5.4, 6.3 to 6.8, 7.1 to 7.3, 11, 19.7), 23 (sections 6.2, 7.1,
  8.5, 9), 25 (section 4.2), 26 (sections 5.3, 5.5, 9, 10), 34 (section
  10.1); `roadmap-usb4.md` tasks 45.22, 45.44, 45.50, 45.54, 45.59, 45.60.
