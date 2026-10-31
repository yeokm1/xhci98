# Design Record 35 - Thunderbolt Networking: a Host-to-Host Link

Status: revision 1, 2026-10-31, before code. Written for
`roadmap-usb4.md` task 45.59 (the owner, 2026-10-31: reinstated whole in
release `3.4.0.0` after its withdrawal of 2026-10-30, when it was task
45.43), with the owner's decisions of 2026-10-31 on its installer box
(task 45.60), its packaging and its order folded in (sections 9 and 14).
Nothing in `src\` implements it. Section 15 lists the owner's questions;
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
- **[tbnet-owed]** a fact that only Linux's `drivers/net/thunderbolt`
  settles. That driver is **not on hand** (section 2.4); every such point
  is listed in section 2.4 and closed by step S0 before the code that
  depends on it.
- **[emu]** `usb4-emu`, read in place and never modified (`main` at
  `1bfa0bb`; its host-to-host task, written as `9c5e88a` and withdrawn in
  `90aad1c`, read with `git show`).
- **[code]** this tree's source at `9ca3cca1`.
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
  disconnect (a hot-plug event with UPG 1 on lane 0), the domain going to
  sleep, or a notification with a different source UUID ends every
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
  takes all the lane's usable credits, since nothing else is tunnelled
  over an inter-domain link; CL states are disabled while the tunnel is
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

### 2.4 Linux's `thunderbolt-net`: not on hand, and what is needed from it [tbnet-owed]

The pinned mirror (`external/README.md`) holds `drivers/thunderbolt` and
`include/linux/thunderbolt.h`; it does not hold `drivers/net/thunderbolt/`
(`main.c`, `trace.c`, `trace.h`, `Kconfig`, `Makefile`), and `usb4-emu`'s
reference copy holds the same set. Nothing of that driver is quoted or
paraphrased here. Step S0 fetches it at the same pinned commit
(`c6859eed`) into `external/linux/net-thunderbolt/` with a row in
`external/README.md`'s table and its fetch script (the same sparse
checkout of `torvalds/linux`, `git sparse-checkout set
drivers/net/thunderbolt`), read for facts only as `drivers/thunderbolt`
is, and amends this record where the reading differs. What the reading
must settle, each a point the design below leaves configurable or
tolerant until then:

1. **Login role**: whether it logs in from both sides at once, as IDS
   5.1.1 reads, and how it handles a crossing login, a repeated one, and
   a Login Response with a failure status.
2. **Its `network` directory**: its `prtcstns`, `prtcvers` and `prtcrevs`
   values, and whether it publishes the directory at load or only on a
   peer (the documentation's "load on one host triggers the other").
3. **Frame ID**: whether it increments per USB4 frame, as IDS Table 5-7's
   text and Figure 5-7 read (section 7.2), or per Ethernet packet; and
   what its receiver checks among Index, ID and Count.
4. **The Ethernet frame on the wire**: whether the 4-byte FCS and the
   padding to 60 bytes that IDS Figure 5-6 draws are sent, and whether
   its receiver strips four bytes. The receive side below tolerates
   either for IP and ARP (section 7.2); the transmit side cannot.
5. **Checksums**: whether it sends complete IP, TCP and UDP checksums
   (the frame header has no field to say otherwise, so a peer cannot know
   a checksum was left partial [inf]) and whether its receiver trusts the
   frame CRC in their place.
6. **MTU**: its default and its maximum (IDS 5.1.5.2's 64 KB less the
   14-byte Ethernet header is 65522 [inf]).
7. **Its rings**: sizes, end-to-end flow control on or off, and the
   HopIDs and rings it asks for.
8. **Its MAC address**: whether it uses IDS 5.1.5.3's algorithm, and
   whether it reads the Login Response's MAC.

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
  ignored, as Linux matches ([linux] `xdomain.c` `tb_xdomain_match`),
  its protocol UUID is the request's, and its SN is the request's. A
  control request and an inter-domain request are never outstanding at
  once; the thread serialises them. The bound is IDS's: one second a try,
  ten resends, each with the next SN (Linux uses the retry count modulo 4,
  `tb_xdp_uuid_request`).
- **Requests the peer sends** (PDF 6) are decoded on the thread as they
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

With `XhciCmNet` 0 the branch is today's: the port is UNSUPPORTED and the
peer's requests are answered as section 3.6 says for a host offering no
service. Everything else about the port - its hot-plug handling, its
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
events are logged as today and nothing is approved; the peer's
Properties Read Requests are still answered (section 3.6).

The firmware builds the paths itself on approve (section 5.2); the
discovery and login packets still go over ring 0 as PDF 6 and 7 frames
beside the ICM's own, as Linux sends them through the same control
channel in either mode [linux] (`domain.c` `tb_domain_handle_rx`'s
`TB_CFG_PKG_XDOMAIN_REQ` case). Whether Alpine Ridge firmware passes them
through unchanged is unread on hardware [inf]; the P14s bench reads it
(section 11.4).

### 3.5 The discovery state machine

One peer record per inter-domain link (section 13: one at a time per
NHI), driven by the thread with section 5.3's clock of record 22. States
and their bounds, each try one second, ten resends, as IDS chapter 3:

| State | Sends | On success | On failure |
|---|---|---|---|
| UUID (software CM only) | UUID Request (type 12) | LINK (or PROPS where bonding is not possible) | the port as section 3.3 step 2 |
| LINK | Link State Status Request | BOND_LOW or BOND_HIGH by the UUID comparison | PROPS (bonding skipped), lane 1 disabled (Guide 7.5.2) |
| BOND_LOW | Link State Change Request (TLW 3), then Link State Status Requests until the peer's TLW reads 3; own lanes' Target Link Width set, Lane Bonding set, lane 0 back in CL0; the Negotiated Link Width x2 within record 22 section 5.2's 500 ms | PROPS | PROPS, lane 1 disabled |
| BOND_HIGH | nothing: waits for the peer's Change Request (section 3.6), sets both lanes' Target Link Width, waits for the UPG 1 hot-plug event or 3 s of Negotiated Link Width polling (IDS 3.4.1) | PROPS | PROPS, lane 1 disabled |
| PROPS | Properties Read Requests from offset 0 up, 45 DWs at most a response, the whole block at most 500 DWs (section 3.7), restart from 0 if the generation changes | READY, and the service step of section 4 | ERROR |
| READY | nothing; a Properties Changed Notification from the peer returns to PROPS | - | - |
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

Requests are answered whatever this driver's own machine is doing, as
IDS chapter 3 requires and as Linux answers ([linux] `xdomain.c`
`tb_xdp_handle_request`):

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
| `network` | `D` | only while `XhciCmNet` is 1: directory UUID `C66189CA-1CCE-4195-BDB8-49592E5F5A4F` (section 2.1), `prtcid` 1, `prtcvers` 1, `prtcrevs` 1, `prtcstns` per section 5.3 (bit 1 always; bit 0 when E2E is used; bit 2 when `XhciNetMtu` is above 1500) |

No `timesync` entry (section 13). The generation starts from the clock at
each bind (IDS 2.5's recommendation of a fresh pseudo-random value) and
increments on every change. The block is about 40 DWs, one response.

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
and the query device), `maxhopid` (15 when absent, IDS 6.3.1.1.1), and
the first `network` directory whose `prtcid` is 1 and `prtcvers` 1, with
its `prtcstns`. A block without one is a peer offering no USB4NET: the
peer stays in READY, logged, no login.

### 3.8 The end of a peer

A peer ends, and every service with it (section 4.4), on: the port's
unplug under the software CM (CLd, a re-lock, ERR_CONN or ERR_LOCK, or
the UPG 1 hot-plug event on lane 0, record 22 section 7.1, IDS 3.7);
the disconnected event under the firmware CM; a Properties Changed
Notification from a different UUID; the NHI's stop, surprise removal,
removal or D3 (record 21 section 5.6 and record 22 section 11, before the
rings stop); and `XhciCmNet` going to 0 at a restart. On the unplug the
peer is gone, so its paths and rings are torn down without a logout; on
every other end a logout is sent first where the peer is still there
(section 4.4). Under the software CM the port then clears its
Inter-Domain bit (Guide 3.3; [linux] `tb_port_unconfigure_xdomain`) and
returns to record 22's port machine, where the next plug starts again.

## 4. The USB4NET service

### 4.1 Who logs in

IDS 5.1.1 has each CM log in to the other. This driver does both halves:
as **requester** it sends a Login once the peer is READY with a USB4NET
directory and its own block offers one; as **responder** it answers the
peer's Login. The service is up when this driver has both sent a
successful Login Response and received one (IDS 5.1.1's last paragraph),
which is when section 5 builds both paths and enables both rings, in one
step on the thread, and the network driver's link goes up (section 7.6).

Login: the next request id, revision 1, the Service HopID of section 4.3.
The resend schedule: every 100 ms for the first 2 s, then every second,
for at least a minute (IDS 5.1.1), then the login is abandoned, logged
(`cm.net.login.none`), and the peer stays READY; a Login from the peer
later restarts it (IDS 5.1.1's second paragraph).

Login Response this driver sends: Status 0 and its MAC (section 4.4),
length 6, when the requester is the peer of this link and no other login
is up; otherwise the failure status `0x80000001` (vendor failure, IDS
Table 5-6), logged. A repeated Login from the same peer while the service
is up is answered with success again and changes nothing [inf]; whether a
peer resends after success is [tbnet-owed] point 1.

A Login Response this driver receives with a failure: logged with its
status, the login retried at the slow rate until the minute ends.

### 4.2 Logout

Sent (requester side) at every end of section 3.8 but the unplug, and
when the network driver closes the service (section 6.3, a halt or a
disable). Resent once a second, ten times; no answer is taken as an
answer (IDS 5.1.2). On the response: the transmit path and ring torn
down. A Logout from the peer: Logout Response Status 0, then the receive
path and ring torn down; the network driver's link goes down at the
first of the two. A peer that logs out and logs in again gets the
service built afresh.

### 4.3 HopIDs and rings

- **The transmit HopID** (the Service HopID in this driver's Login): the
  lowest HopID from 8 up that is at most the peer's `maxhopid` and this
  lane adapter's Max Output HopID (ADP_CS_5 bits 17:11) and not in use on
  that adapter (record 22 section 6.8's allocation rule). On an empty
  link that is 8, as Linux's allocation gives ([linux]
  `tb_xdomain_alloc_out_hopid`).
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
  takes the transmit HopID as above from 8, bounded by the peer's
  `maxhopid`, since it cannot read the firmware's allocations.

### 4.4 The MAC address

The adapter's permanent address is IDS 5.1.5.3's derivation from the
host router's UUID, router id and port number, under the software CM;
under the firmware CM, where the host router's 64-bit UUID is not read,
from the low 64 bits of the local UUID of the connected event, which
Linux and Intel's firmware build from the same 64-bit value (section
3.1) [inf]. Both are locally administered and unicast by construction.
The peer's MAC from its Login Response is logged and handed to the
network driver for the log and the query device; the network stack
learns it by ARP as on any Ethernet. Whether `thunderbolt-net` uses the
same derivation is [tbnet-owed] point 8; nothing depends on it.

## 5. The DMA paths and rings

### 5.1 The paths under the software CM

Two path entries in the host router only (the inter-domain link is the
host's own port; nothing on the far side is this domain's), each one
read of its two DWs and one write, source adapter first (Guide 5.2.1;
record 22 section 6.8's discipline):

| Path | Ingress | Output | Attributes |
|---|---|---|---|
| transmit | host interface adapter, HopID = the transmit ring number | the port's lane 0 adapter, HopID = this driver's Service HopID | priority 4 (Guide Table 6-1; Linux uses 5, section 2.3), weight 1, IFC 1, EFC 1, ISE 0, ESE 0; credits as below |
| receive | the port's lane 0 adapter, HopID = the peer's Service HopID | host interface adapter, HopID = the receive ring number | priority 4, weight 1, IFC 1, EFC 0, ISE 0, ESE 0 (Guide 5.7 step 7's segment rule) |

**Credits** by record 22 section 6.8's accounting, the inter-domain lane
carrying nothing but this service: the receive path's entry takes what
the lane has left after the control path and the flow-control-disabled
pool, as Linux takes "all the credits" there ([linux]
`tb_dma_init_rx_path`); the transmit path's entry at the host interface
adapter takes the host router's `baMaxHI` from its Buffer Allocation
Request (record 22 section 6.7), else 14, at least 1 ([linux]
`TB_DMA_CREDITS`, `TB_MIN_DMA_CREDITS`). Below 1: no service, logged
(`cm.net.credits`). Total Buffers is re-read after bonding (record 22
section 6.8).

**Order up**: both rings set up (section 5.3) with Ring Valid, then the
receive path, then the transmit path, then the link up to the network
driver. **Order down**: the link down to the network driver (no new
sends accepted), the transmit ring drained or 500 ms, the path entries'
Valid cleared with record 22 section 6.8's Pending Packets wait (500 ms,
then 2 us), then each ring disabled by Guide 5.7.3 and its memory kept
until the next service or the stop. CL states are never enabled by this
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
  and EOF bit 2 (PDF 2) (IDS 5.1.5; Spec Table 12-27).
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
  bit 0 on every host; `XhciCmNetE2E` 0 (section 8.1) withdraws it.
- **Sizes**: `XhciCmNetRxFrames` and `XhciCmNetTxFrames` descriptors
  (section 8.1, 128 each by default, 16 to 1024), each with a 4096-byte
  buffer, so 512 KiB and 2 KiB of descriptors a ring by default; one
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

- **the poll timer**: a `KTIMER` with that DPC, periodic at
  `XhciCmNetPollMs` (section 8.1, default 1, which on every target is the
  next clock tick: 10 to 15.6 ms on NT, 10 to 55 ms on 98 SE and ME,
  record 22 section 5.3), armed while a service is up and cancelled at
  its end;
- **the ring interrupt**, where record 21 section 4.5 has it connected
  and armed (`XhciCmRingInterrupt` 1 on NT; never on 98 SE and ME until
  their route is read): the receive ring's Receive Data Buffer cause is
  opened beside ring 0's by the same arm, and the existing service
  routine's DPC queues the receive DPC when that bit is among the causes
  it read. The status protocol, the storm count, the self-test and the
  fall back to polling are record 21 section 4.5.2 to 4.5.6's, unchanged;
  the poll timer stays armed as the fallback, as the idle poll does for
  ring 0.

The DPC, under the lock: reaps every receive descriptor with DD set
(each a USB4 frame: its length, CRC Error, Buffer Overflow), releases the
lock, hands the frames to the network driver's receive callback in ring
order, takes the lock again, reposts each descriptor (RS 1, IE, DD 0) and
advances the consumer index; it reaps the transmit ring's completed
descriptors too; at most one ring's worth a run, the rest left to the
next. The callback copies (section 6.3), so a descriptor is never lent
out.

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
destroyed at every plug [inf]. The PDO goes at the NHI's stop, surprise
removal and removal, as the root hub goes with `xhci98.sys`'s controller.

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
| `Open(Context, Callbacks, ClientContext)` | PASSIVE | registers the network driver's callbacks (below) and its context; fails if one is open; the CM then may raise `LinkChange` at once if a service is up |
| `Close(Context)` | PASSIVE | ends any service with a logout (section 4.2), waits until no callback is running or can start, drops the callbacks |
| `QueryInfo(Context, Info)` | <= DISPATCH | this adapter's MAC (section 4.4), the link state, the peer's MAC and its `prtcstns`, the link's rate and width (record 22's query-device extensions), the service's maximum frame (section 7.3) |
| `TxReserve(Context, Count, Buffers)` | <= DISPATCH | reserves up to `Count` transmit descriptors and returns their buffers' virtual addresses, or fewer when the ring is fuller; 0 when no service is up |
| `TxCommit(Context, Count, Lengths)` | <= DISPATCH | posts the reserved descriptors with those lengths (each at most 4096) and writes the producer index once |
| `TxAbort(Context)` | <= DISPATCH | releases a reservation not committed |
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
  logout, raises `LinkChange` down, refuses further `TxReserve`, waits for
  a running DPC and callback to finish, and only then frees the rings;
  the network driver's own surprise-removal notification (section 7.6)
  follows.
- The interface's reference (`InterfaceReference`,
  `InterfaceDereference`) counts the network driver's hold; the PDO
  is not deleted while it is held, as record 21 section 3.2's rundown
  protects the filters' entries.

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
   last the rest. IDS 5.1.5.2 says "4KB USB4 Frames", which this reads as
   the 4096-byte host-interface frame including its header [inf]; the
   alternative, 4096 bytes of data a frame, cannot fit one descriptor.
3. `TxReserve(m)`; fewer than m: the frame waits (NDIS 6 queues it,
   NDIS 5 returns it as resources; section 7.5).
4. Each buffer: Frame Size = this frame's data bytes, Frame Index = k
   (0 to m-1), Frame ID = the adapter's transmit counter, incremented
   after every USB4 frame and wrapping at `0xFFFF` (IDS Table 5-7's text;
   [tbnet-owed] point 3), Frame Count = m; all little-endian; then the
   data bytes copied from the stack's buffers.
5. `TxCommit(m)`, and the stack's frame is completed at once: its bytes
   are in the ring.

Whether the FCS and the 60-byte padding IDS Figure 5-6 draws go on the
wire is [tbnet-owed] point 4; until S0 settles it the transmit side
sends neither (what NDIS hands a miniport) behind a compile-time
switch the step resolves, never a registry value.

**Receive**, from the `Receive` callback, one USB4 frame at a time:

- Dropped and counted: CRC Error or Buffer Overflow set; a length below
  12; a Frame Size of 0 or above the frame's length less 12; Frame Count
  0 or above 17 (65536 bytes at 4084 a frame, IDS 5.1.5.2's 64 KB); Frame
  Index not below Count; a packet whose summed Frame Size passes 65536.
- A frame with Count 1 and Index 0: one Ethernet frame of Frame Size
  bytes, indicated to the stack at once.
- Count above 1: reassembled in a per-adapter 64 KiB buffer: Index 0
  starts a packet (dropping any one in progress, counted); each next
  frame must carry the next Index and the same Count, and the Frame ID
  one more than the last (modulo `0x10000`) where [tbnet-owed] point 3
  confirms that rule, else only Index and Count are checked; a gap drops
  the packet (counted); the frame with Index Count-1 completes it.
- An indicated frame shorter than 14 bytes is dropped. One longer than
  the payload its IP or ARP header names reaches the stack as padding,
  which IP and ARP ignore, so a peer that sends the FCS or pads is
  received correctly either way [inf, by the protocols' own rules].

### 7.3 The MTU

`XhciNetMtu` (section 8.3), the IP payload, default 1500, from 1500 to
65522 (IDS 5.1.5.2's 64 KB less the Ethernet header; [tbnet-owed] point 6
checks Linux's maximum against it). The adapter reports a maximum frame
of the MTU plus 14 (NDIS 5 `OID_GEN_MAXIMUM_FRAME_SIZE` is the MTU,
`OID_GEN_MAXIMUM_TOTAL_SIZE` the MTU plus 14; NDIS 6
`MtuSize` in its general attributes). Above 1500 the frames go out as
section 7.2's multi-frame packets, and only to a peer whose `prtcstns`
bit 2 is 1 (IDS 5.1.5.2's recommendation): to a peer without it a frame
longer than 1514 bytes is dropped and counted, and the log says once per
service that the peer takes no large frames. Both ends must agree on the
MTU at the IP layer, as on any Ethernet; that is the user's (section
15 Q5).

### 7.4 Checksums, segmentation and other offloads

- **Checksum offload: none, both ways.** The host interface computes no
  IP, TCP or UDP checksum; a miniport that advertised the offload would
  compute it in software, which the stack already does, and the USB4NET
  header has no field to mark a checksum partial, so every frame on the
  wire must carry complete checksums [inf; tbnet-owed point 5]. On
  receive the frame CRC (Spec 12.3.2.1) covers the fabric from host
  interface to host interface, not either host's memory, so the stack
  keeps verifying.
- **Segmentation**: the fabric's segmentation (a frame into tunnelled
  packets) is the host interface's, below everything here; USB4NET's
  (a large Ethernet frame into 4 KB frames) is section 7.2's, in
  software. No large-send offload (NDIS 5 `TCP_TASK_TCP_LARGE_SEND`,
  NDIS 6 LSO): with the MTU raised and a peer taking large frames the
  stack already sends up to 64 KB a frame.
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
- **Send**: NDIS 5 `MiniportSendPackets` at DISPATCH_LEVEL; a packet that
  does not get its descriptors returns `NDIS_STATUS_RESOURCES` so NDIS
  queues it and sends it again after `NdisMSendResourcesAvailable`, which
  the `TxDone` callback raises; NDIS 6 `MiniportSendNetBufferLists` keeps
  its own queue, served by `TxDone`. With the link down every send
  completes with `NDIS_STATUS_MEDIA_DISCONNECTED` (NDIS 5) or
  `NDIS_STATUS_MEDIA_DISCONNECTED` in the list (NDIS 6).
- **Receive**: copied from the callback into a packet (NDIS 5
  `NdisMIndicateReceivePacket` from the miniport's own pool, the packet
  returned through `MiniportReturnPacket`) or a net buffer list (NDIS 6,
  its own pool, `NdisMIndicateReceiveNetBufferLists`, returned through
  `MiniportReturnNetBufferLists`), at DISPATCH_LEVEL; with the pool empty
  the frame is dropped and counted.
- **Reset**: nothing to reset below; `MiniportReset` completes at once.
- **Check for hang**: never reports a hang.

### 7.6 Link up and down

| Event | What the user sees | Path |
|---|---|---|
| a peer plugged, both logins done | the adapter connected | `LinkChange` up; NDIS 5 `NdisMIndicateStatus(NDIS_STATUS_MEDIA_CONNECT)` and complete; NDIS 6 `NDIS_STATUS_LINK_STATE` with the state and speed |
| the cable pulled, the peer's sleep or shutdown | the cable unplugged | section 3.8, `LinkChange` down; `NDIS_STATUS_MEDIA_DISCONNECT`, or the NDIS 6 link state |
| the peer logs out (its adapter disabled, its driver stopped) | the cable unplugged | section 4.2; the same |
| this adapter disabled in Device Manager or Network Connections | the adapter disabled | the miniport's halt: `Close` (a logout first), then the PDO stopped; the CM keeps answering discovery, so the peer sees a host with no logged-in service |
| this adapter enabled again | connected once the logins redo | `Open`; the CM logs in again |
| the NHI disabled, stopped or removed (an update, the connection manager uninstalled) | the adapter removed | the NHI's PnP order: its children first, so the miniport halts and closes, then the NHI's own end of section 3.8 |
| a safe eject from `XHCITR` or `XHCIINST /eject` | not offered for a peer (section 15 Q9) | disabling the adapter is the eject; nothing of a peer's lives behind a PCI bridge |

A peer that unplugs and plugs again with the same UUID gets the same
adapter, so the operating system keeps its IP settings for it; a
different peer on the same port does too, since the adapter is the NHI's,
not the peer's.

### 7.7 Power

The miniports declare power management with no wake (NDIS 5
`OID_PNP_CAPABILITIES` with every wake-up field unspecified; NDIS 6 the
same in its attributes) [inf: that 98 SE's NDIS treats this as a
power-managed miniport is read by the 98 SE standby leg]. On
`OID_PNP_SET_POWER` D3 (or NDIS 6's set power): `Close`, the link down.
On D0: `Open`, and the CM logs in again when the peer is there. The CM's
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
| `XhciCmNet` | 0 | 0 to 1 | 1 runs the inter-domain discovery as section 3 says, publishes USB4NET, logs in and reports the network PDO; 0 leaves an inter-domain port as today (UNSUPPORTED under the software CM, the events logged under the firmware CM) while still answering a peer's discovery requests for a host with no service. The installer's networking box writes 1 (section 9) |
| `XhciCmNetPollMs` | 1 | 1 to 100 | the receive poll's period in ms while a service is up, rounded up to the clock tick (section 5.4) |
| `XhciCmNetRxFrames` | 128 | 16 to 1024 | receive descriptors (4 KiB each) a service |
| `XhciCmNetTxFrames` | 128 | 16 to 1024 | transmit descriptors a service |
| `XhciCmNetE2E` | 1 | 0 to 1 | advertise and use end-to-end flow control (section 5.3); 0 for a peer whose host interface mishandles it |

A value outside its range is refused and the default stands, logged
(`cm.reg.range`), as for every `XhciCm*` value. `XhciCmLaneBonding`,
`XhciCmApproveTimeoutMs` and `XhciCmRingInterrupt` keep their meanings
and now also govern sections 3.5, 5.2 and 5.4.

### 8.2 The network driver's INF

`xhcinet.inf` in the x86 package and its 64-bit counterpart, named by task
45.44's convention, each one file with the decorated sections choosing per
system, as `xhcicm.inf` does (record 21 section 3.3; `AGENTS.md`, "The
INF and install media"):

| Section | Systems | Image | Notes |
|---|---|---|---|
| undecorated | 98 SE, ME | `xhcinet.sys` | net-class 9x keys (section 7.8), `DevLoader=*ndis`, the values of section 8.3 under `HKR`, the `Ndi\params` subkeys for the Advanced page |
| `.NTx86` | 2000, XP, Vista, 7 | `xhcinet.sys` | `AddService=xhcinet`, `Characteristics` `NCF_PHYSICAL` and `NCF_HAS_UI`, the other keys NDIS reads for a WDM miniport (read from the 2000 DDK's network INF documentation in S6), the values, `Ndi\params` |
| `.NTx86.6.2` | 8, 8.1, 10 | `xhcinet6.sys` | `AddService=xhcinet` with the NDIS 6 image, `*IfType`, `*MediaType`, `*PhysicalMediaType`, the values, `Ndi\params` |
| `.NTamd64` | XP x64 to 7 x64 | `xhcinet.sys` (x64) | as `.NTx86` |
| `.NTamd64.6.2` | 8 to 10 x64 | `xhcinet6.sys` (x64) | as `.NTx86.6.2`; `CatalogFile=` (record 19 section 3a) |

The models line matches `XHCICM\TBNET`. The 32-bit file's `[Manufacturer]`
decoration is the one decision the INF gate holds for every file here
(`NTx86.6.2` beside the undecorated models, which Windows 98's engine must
parse past): step S6 reads it on 98 SE, ME, 2000 and XP before it is
taken, as task 22.5 read `NTx86.6.0` for `xhci98.inf` (`AGENTS.md`).
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
  switch (the owner); greyed and unticked when the package carries no
  network driver for the system. The sentence under it, without testing
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
  section 6.2), and `XhciCmNet` 1 written machine-wide in `Services\USB`
  (record 26 section 5.5's changed-value rule), so the CM reports the
  network PDO at its next start; the Summary says the network adapter
  appears once the connection manager restarts or the computer does.
- **Unticked on a new install**: nothing of networking staged, `XhciCmNet`
  left at the INF's 0.
- **Unticked on an update over an install that has it** (`xhcinet`
  installed, or `XhciCmNet` 1): kept as it is, as every component's
  unticking is "not now" (record 26 section 5.3); the Advanced list's
  `XhciCmNet` row (45.54's) is how a user turns it off. Ticked on such an
  update: the network driver updated with the others.
- **The uninstall** (record 23 section 9, record 26 section 9) removes
  `xhcinet` with the connection manager: the adapter's devnodes, the
  service, the staged INF, and `XhciCmNet` from `Services\USB` when its
  value is the one this installer wrote.
- **The Advanced list** (task 45.54): `XhciCmNet`, `XhciCmNetPollMs`,
  `XhciCmNetRxFrames`, `XhciCmNetTxFrames`, `XhciCmNetE2E` on the
  Thunderbolt tab with their ranges and "Asked when" (shown when the
  networking box is ticked); `XhciNetMtu` is the adapter's own and lives
  in its Advanced page, not the installer's.

### 9.2 The console and the silent install

- `/tbnet` preselects the box ticked (needs `/thunderbolt` or `/cmonly`,
  as `/stor98` does, record 23 section 7.1); `/notbnet` unticked; refused
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

### 9.4 Order (the owner, 2026-10-31)

Every driver and connection-manager step (S0 to S7 of section 14) and
their emulator legs run before task 45.44, with the images copied into
the guest by hand and installed through Device Manager's "Have Disk" from
that folder, as the earlier connection-manager legs were. Only the
packaging (S10) waits for 45.44's merge, and the installer wiring (S11)
for 45.44's and 45.60's.

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
- **E4, end-to-end flow control**, or a documented absence: credits
  granted per posted receive descriptor and a transmit ring held at zero
  credits; without it, a full receive ring drops, which the legs then
  read as loss.
- **E5, the firmware CM.** On the `tb3-alpine-ridge` and
  `tb3-titan-ridge` profiles, events `0x06` and `0x07` in each row's
  layout (section 3.4) when the link comes and goes; approve `0x10`
  building the two paths from the four numbers; mailbox `0x10` and `0x11`
  (AR) and command `0x11` in two stages (TR) tearing them down; PDF 6
  and 7 frames passed between ring 0 and the link.
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
- `test_u4cm` and the fake fabric (`u4_fakefab.c`, which already holds two
  domains): two hosts joined port to port, ERR_ENUM at the far router,
  the Inter-Domain bit, the two path entries and their credits read back,
  the order up and down, an unplug mid-login, mid-service and mid-teardown.
- `test_tbt_icm`: events `0x06` and `0x07` and the approve and teardown
  per row, synthesised from `tb_msgs.h`'s structures, as the other non-AR
  vectors are (record 21 section 5.2), and the AR mailbox pair.
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

| System | Software CM (`usb4-generic`) | Firmware CM (`tb3-alpine-ridge`) | Notes |
|---|---|---|---|
| 98 SE | yes | yes | the 9x net INF, the first-install TCP/IP binding, file sharing to Samba; polled only |
| ME | yes | - | as 98 SE |
| 2000 SP4 | yes | yes | polled and, with `XhciCmRingInterrupt` 1, interrupt |
| XP SP3 | yes | yes | as 2000; SMB to Samba |
| 7 x86 | yes | - | the NDIS 5 image under NDIS 6.x's wrapper |
| XP x64 or 7 x64 | yes | - | the x64 NDIS 5 image, test-signed in the guest (the brief's MakeCert route) |
| 8.1 or 10 x64 | yes | - | the NDIS 6 image, catalog, test-signing |
| 98 SE to 2000 | one leg | - | two guests of this project's, no Linux: this driver against itself |
| 2000 | one leg | - | `XhciCmNet` 0: the port stays UNSUPPORTED and Linux sees a host with no service |

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
  domains); under the firmware CM a connected event at depth above 1 is
  logged and not approved.
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
| S9 | The legs of section 11.3, images copied into the guests by hand | 6 to 8 days | after S6 to S8; before 45.44 |
| S10 | **After 45.44 is merged.** The packaging of section 9.3: `cm\tbnet\` (or loose in `cm\` on `98-me\`), `make-package.ps1`, `PKG-*` rules, signing and the catalog, `make-release.ps1`, the download readme | 300 to 500 lines of scripts; 2 to 3 days | after 45.44 |
| S11 | **After 45.44 and 45.60 are merged.** The installer of sections 9.1 and 9.2: the box on 45.60's page, `/tbnet` and `/notbnet`, the console question, `inst_tbnet_decide`, staging and installing `xhcinet.inf`, `XhciCmNet`'s write, update and uninstall, 45.54's Advanced rows; `test_inst`; the window's legs on 98 SE, XP and 7 | 600 to 900 lines; 3 to 5 days | after 45.44 and 45.60 |
| S12 | Documents: `tuning.md`'s rows (S5, S6), `source-files.md`, `legal-provenance.md` section 3 and 4 rows, `build-and-test.md`'s network leg, the release notes' text (handed to the coordinator) | 1 to 2 days | with each step |

Total, the rows summed: about 9,000 to 12,500 lines of driver and script
code and 2,500 to 3,500 of tests in this repository, 45 to 60 working
days, nine to twelve weeks for one person, plus `usb4-emu`'s share. The
least certain parts: the 98 SE NDIS 5 import surface and net INF (S6),
the query interface on 98 SE (S5), and the emulator's data path (S8).

## 15. The owner's questions

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
