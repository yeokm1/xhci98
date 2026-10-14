# Linux's xHCI controller quirks

Design record for roadmap-hcd task 38.10: the survey of every quirk
Linux's xHCI host driver applies by controller identity, read against this
driver's code, and the quirks taken from it, each behind a registry value;
and one fix to the driver's endpoint bookkeeping the survey exposed
(section 5).

Revision 7, 2026-10-14, carries the owner's decision of that day on a
STALLed SET_INTERFACE at an interface with one alternate setting (section
5.3, step 4; section 9). Revision 6, the same day, answered review round 4
(section 11); revision 5,
the same day, answered round 3; revision 4,
2026-10-13, answered round 2; revision 3,
the same day, answered review round 1 with the
owner's rulings on it (section 9). Revision 0 (first committed as
`c06eda96`, rebased since) was the survey alone, with the decisions open;
revision 1 folded in the owner's decisions; revision 2 the owner's later
answers on value 2, the task's renumbering from 38.11 to 38.10 (commits
before it use 38.11) and the HCIVERSION floor. Sections 3, 4 and 6 are
built (task 38.10, 2026-10-14): section 13 says where each quirk acts and
where the code settles or departs from the text. Section 5's fix is a
change of its own, and section 13 does not describe it.

## 1. What is asked, and what is not

Task 38.10 asks for Linux's xHCI controller quirks in the driver, each
behind a registry gate for the controllers Linux names. Until now the
driver carries only quirks of its own: the routing quirk
`XhciIntelIrqRoute` (36.3), the port switchover `XhciIntelPortSwitch`
(design record 16) and the interval cap `XhciIntervalCap` (design record
17 section 4.7), with Linux's soft-retry exclusion list folded into record
17's soft retry and QEMU's stream-context quirk matched on QEMU's id alone
(record 17 section 4.12.4). `XHCIQUAL` already names Linux's quirks for
the controllers in its table, and Linux is the stated authority for that
table (`xhciqual/quirks.c`'s header; `legal-provenance.md`, the mirrors).

For every quirk flag Linux's xHCI host driver sets by vendor and device id
this record says what the controller does wrong, which controllers,
whether it reaches this driver's targets and code paths at all, whether
the driver already covers it another way, and what is taken, under which
value and default. Section 3 is the survey; section 4 takes each quirk
taken; section 5 is the endpoint-ring fix; section 6 is what is common to
the quirks; section 9 is the owner's decisions.

What it is not:

- **Not the code.** Sections 4 and 5 say where in this driver each change
  acts; the implementation follows review.
- **Not a copy.** The survey is facts with `file:line` citations at the
  pinned commit. No Linux code is copied into this record. The
  implementation may adapt Linux's code under the conditions of
  `legal-provenance.md`'s exception for Linux's xHCI quirks (section 6.6).
- **Not the platform glue.** Quirk bits that only Linux's platform (non-
  PCI) glue sets are listed and set aside (section 3.3); this driver binds
  PCI xHCI controllers only.

## 2. The source and how it was read

### 2.1 The mirror

`external/linux`, a flat copy of thirteen files of `drivers/usb/host` from
`torvalds/linux` at commit `c6859eed755df351a3978b33cb92365f9b3e8f06`
(2026-07-18), as `external/README.md`'s table pins it: `pci-quirks.c`,
`xhci-caps.h`, `xhci-ext-caps.c`, `xhci-ext-caps.h`, `xhci-hub.c`,
`xhci-mem.c`, `xhci-pci-renesas.c`, `xhci-pci.c`, `xhci-pci.h`,
`xhci-port.h`, `xhci-ring.c`, `xhci.c` and `xhci.h`. Every one of them
carries a `GPL-2.0` SPDX identifier on its line 1 (read 2026-10-13). Every
citation below is `file:line` in that copy at that commit.

What the mirror did not carry when this survey was written, and what
followed from it (since `0f33d190` the mirror carries
`include/linux/pci_ids.h` and `drivers/usb/core/message.c` at the pinned
commit, as section 10 proposed; the implementation's ids were checked
against that copy):

- `include/linux/pci_ids.h`. The vendor constants `xhci-pci.c` uses but
  does not define (`PCI_VENDOR_ID_NEC`, `_AMD`, `_ATI`, `_INTEL`,
  `_RENESAS`, `_VIA`, `_ASMEDIA`, `_TI`, `_BROADCOM`, `_CAVIUM`,
  `_ZHAOXIN`, `_CDNS`, `_SONY`) and two device constants
  (`PCI_DEVICE_ID_INTEL_PANTHERPOINT_XHCI`, `PCI_DEVICE_ID_CDNS_USBSSP`)
  are not in it. The tables mark those values **(h)**. Each was read in
  the reference copy `docs/references/pci_ids.h` (Linux master as fetched
  2026-10-08, SHA-256 `020fac00...4487c`, `docs/references/README.md`):
  NEC 1033, AMD 1022, ATI 1002, TI 104C, Sony 104D, VIA 1106, Broadcom
  14E4, Cavium 177D, Cadence 17CD and its USBSSP 0200, Renesas 1912,
  ASMedia 1B21, Zhaoxin 1D17, Intel 8086 and Panther Point 1E31. That copy
  is not the pinned commit; section 10 proposes adding the file to the
  mirror at it.
- `drivers/usb/core/message.c`, whose `usb_set_interface` section 5 cites
  for what Linux does when SET_INTERFACE fails or is STALLed. Not in the
  mirror when the survey was written; added at the pinned commit on
  2026-10-14 (`0f33d190`, `external/README.md`, copied flat as
  `external/linux/message.c`) and read there before the implementation
  (section 8).
- The platform glue (`xhci-plat.c`, `xhci-mtk.c`, `xhci-rcar.c`,
  `xhci-tegra.c`, `xhci-histb.c`, the DWC3 glue and the rest), and
  `xhci-pci-prom21.c`. What those set is listed by bit name only, from
  `xhci.h`, and set aside.
- The Raspberry Pi kernel's own VL805 quirks (issue 13 section 5), which
  are not mainline. Mainline's one VL805 case outside the quirk bits is in
  `pci-quirks.c` and is surveyed (section 3.3).

### 2.2 Where Linux sets a quirk

- The bit list: `xhci.h:1588-1649`, the `quirks` field of
  `struct xhci_hcd`, bits 0 to 50 (bit 36 reserved, `xhci.h:1635`).
- `xhci_pci_quirks()`, `xhci-pci.c:251-517`: every bit set by PCI vendor,
  device and (twice) revision id, and by HCIVERSION.
- `xhci_gen_setup()`, `xhci.c:5477-5492`: `XHCI_SPURIOUS_SUCCESS` on every
  controller above HCIVERSION 0.96, and `XHCI_LINK_TRB_QUIRK` on a 0.95
  controller when the `link_quirk` module parameter is set
  (`xhci.c:36-38`, default 0).
- `xhci_init()`, `xhci.c:567-571`: `XHCI_COMP_MODE_QUIRK` by DMI strings
  (`xhci.c:431-450`), not by any PCI id.
- Behaviour keyed on a PCI id outside the bit list, in `pci-quirks.c`: the
  forced BIOS handoff (`1190-1196`); the Intel port switchover
  (`1058-1147`, called at `1224-1225`, and from `xhci-pci.c:878-879` and
  `xhci.c:753-754`) with its Sony exemption (`1064-1069`) and its
  Intel-EHCI test (`1071-1081`); the early-handoff dispatcher's two
  exemptions, Netlogic vendor 184E (`1266-1270`) and the VL805 behind a
  `brcm,bcm2711-pcie` parent (`1272-1282`). Elsewhere: the Renesas firmware
  load (`xhci-pci-renesas.c`, matched by `xhci-pci.c:692-710`) and the
  Promontory 21 hand-off to a glue driver the mirror lacks
  (`xhci-pci.c:699-714`).

## 3. The survey

### 3.0 The scope rule: a platform with no CSM

**Owner, 2026-10-13:** a platform whose firmware has no Compatibility
Support Module (no legacy BIOS boot) cannot run any operating system this
driver supports, so a quirk for a controller found only on such platforms
is not applicable. **And** (owner, the same day, on review round 1): a
platform whose CSM is not established is **deferred pending platform
evidence**, not "not applicable". So each id below is one of: applicable
(a CSM read on a machine, or not a platform at all); not applicable (the
owner's ruling that its platforms have no CSM, or not x86); or deferred
(not established). A taken quirk keeps its deferred ids in its match,
since matching a controller that never runs costs nothing. Whether to take
a quirk all of whose ids are deferred is the owner's choice per quirk: Q7
and Q16 (AMD ids only) are taken (owner, 2026-10-13), Q17 is not.

| Ids | Platform | Standing | Evidence |
|---|---|---|---|
| 1B73:*, 1B6F:*, 1106:3432, 1106:3483, 1B21:*, 1912:*, 1033:*, 104C:8241, 17CD:0200 | add-in cards and secondary on-board controllers | applicable: the host's CSM | not a platform |
| 1002:7316 | the USB-C controller of an ATI Navi10 graphics card | applicable: the host's CSM | an add-in card |
| 8086:1E31, 8086:9CB1 | Intel 7-series PCH; 9-series PCH-LP | applicable, read | the project's B490 (1E31) and E7250 (9CB1) run the supported systems (`runs/run-35.md`, `runs/run-36.md`) |
| 8086:8C31, 9C31, 8CB1, 8D31 | Intel 8- and 9-series and C610 PCH | deferred, not read | the same PCH generations as the two read; no machine of these ids read here |
| 8086:9D2F | Sunrise Point-LP (100-series mobile) | applicable, read | the project's E460 runs them (`runs/run-35.md`) |
| 8086:A12F | Sunrise Point-H (100-series desktop) | deferred | no machine of the family read here |
| 8086:22B5 | Cherryview: Cherry Trail and Braswell SoCs | Cherry Trail: not applicable (owner's ruling); Braswell: deferred | Linux names the id `CHERRYVIEW_XHCI` (`xhci-pci.c:46`); whether both SoC lines carry it is not read here |
| 8086:5AA8 | Apollo Lake | deferred | no machine read here |
| 8086:19D0 | Denverton (server Atom) | deferred | no machine read here |
| 8086:A0ED, 51ED, 54ED | Tiger Lake PCH, Alder Lake PCH and Alder Lake-N | not applicable (owner's ruling) | Intel stated in 2017 a plan to require UEFI Class 3, which has no CSM, by 2020 (reported at the time, e.g. https://www.phoronix.com/news/Intel-Legacy-BIOS-EOL-2020); no machine of these lines read here |
| 1022:145C, 15E0, 15E1, 15E5, 43B9, 43BA, 43BB, 43BC and the other AMD ids | AMD desktop (AM4) platforms and their mobile parts | deferred | no AM4 board read here; the AM5 board the project probed for record 17 is a CSM model (PRO B650M-CT-CSM, `runs/run-35.md`), so a later AMD desktop socket can have one |
| 1D17:9202, 9203 | Zhaoxin | deferred | no machine read here |
| 1DB7:DC27, 14E4:9026, 177D:9026 | Phytium, Broadcom/Cavium SoCs | not applicable: not x86 | no supported system runs there |

The rule settles two quirks. Q6 names only Tiger Lake and Alder Lake ids
and is not applicable by the owner's ruling. Q17 names only 22B5, whose
Cherry Trail half is ruled out and whose Braswell half is deferred, so Q17
is deferred pending platform evidence and not taken. Q7 and Q16 name only
deferred AMD ids and are taken by the owner's choice; every other quirk
taken keeps at least one applicable id.

### 3.1 Taken

Ids are `vendor:device`, hexadecimal; **(h)** marks a value from
`pci_ids.h` (section 2.1). "Every" means vendor-wide. Each value is a
REG_DWORD in the controller's driver key (section 6.1); the Linux flag is
beside it.

| Id | Value (Linux flag) | Set at | Controllers | What the controller does wrong | Linux acts at | This driver until now | Default |
|---|---|---|---|---|---|---|---|
| Q1 | `XhciMsiAvoid` (`XHCI_BROKEN_MSI`) | `xhci-pci.c:256-284` | 1B73:1000 (FL1000/PDK), 1B73:1400, every revision | advertises MSI, raises none ("Fresco Logic confirms", `xhci-pci.c:275-278`) | `xhci-pci.c:149-154`: never tries MSI | Vista and 7 connect a message where Windows grants one (records 15 and 18; both INFs write `MSISupported` 1); the dead-line rule is the line's only (record 15) | 1, on |
| Q2 | `XhciStreamsOff` (`XHCI_BROKEN_STREAMS`, and Q20's id) | `xhci-pci.c:273-274`, `286-288`, `423`, `444-446`, `460`; `506-508` | 1B73:1000, 1B73:1009, 1B6F:7023, 1B6F:7052, 1106:3432 **(h)**, 1B21:1042 **(h)**; and 17CD:0200 **(h)** (owner, section 3.3's Q20) | streams "broken beyond repair" (`xhci.h:1616`); on 17CD:0200 a stream context Set TR Dequeue does not clear | `xhci-pci.c:664`, `xhci.c:3638-3643`: no streams | streams wherever HCCPARAMS1.MaxPSASize > 0 (`hcd_cfg.c` `hcdCfgStreamsOpen`; the transport decision of `xhci_xport.h`) | 1, on |
| Q3 | `XhciResumeReinit` (`XHCI_RESET_ON_RESUME`) | `xhci-pci.c:326-329`, `418-425`, `431-435`, `436-437`, `439-441`, `476-478`, `497-499` | 1022:15E5, 1B6F:7023, 1B6F:7052, 1912:0015, every 1106, 1DB7:DC27, 1B21:3042, 1D17:9202 **(h)** | state is not kept across a suspend | `xhci.c:1105-1108`: resume re-initializes | resume restores (CRS) when the suspend saved (FSC), else re-initializes (`xhci_init.c` `XhciResumeController`) | 1, on |
| Q4 | `XhciIntelShutdownToEhci` (`XHCI_SPURIOUS_REBOOT`) | `xhci-pci.c:350-370` | 8086:1E31 **(h)**, 8086:9C31, 8086:9CB1 | a powered-off machine powers back on (`xhci-pci.c:355-362`) | `xhci.c:753-754`: at shutdown both port-routing registers written 0 (`pci-quirks.c:1143-1147`) | at shutdown hands back only the connectors a route moved, and with `XhciIntelPortSwitch` 0, the default since `2.3.0.0`, writes nothing (`hcd_power.c`, `HcdPswRelease`; record 16 section 7a) | 1, on |
| Q5 | `XhciShutdownReset` (`XHCI_SPURIOUS_WAKEUP`) | `xhci-pci.c:365-370` | 8086:9C31, 8086:9CB1 | spurious wakes at shutdown ("with HSW", `xhci.c:770-772`, `xhci-pci.c:947`) | `xhci.c:774-776`: HCRST after the shutdown's halt; `xhci-pci.c:948-949`: the function put in D3hot | the shutdown's D3 halts and saves, no HCRST (`XhciSuspendController`) | 0, off |
| Q7 | `XhciSuspendHaltDelay` (`XHCI_SUSPEND_DELAY`) | `xhci-pci.c:315-320` | 1022:145C, 1022:15E0, 1022:15E1, 1022:43BB | needs a pause before Run/Stop is cleared at suspend | `xhci.c:985-986`: 1 to 1.5 ms before the halt | halts at once (`XhciSuspendController` -> `XhciQuiesceController`) | 1, on |
| Q8 | `XhciSuspendHaltTimeout` (`XHCI_SLOW_SUSPEND`) | `xhci-pci.c:265-272` | 1B73:1000 revision 04; matched here at every revision (section 6.3) | halts slowly at suspend ("extra slowly") | `xhci.c:1004-1007`: the suspend's halt bound times 10 (from `XHCI_MAX_HALT_USEC * 2`, `xhci.c:954`, `xhci-ext-caps.h:12`) | the halt bound is `XHCI_HALT_TIMEOUT_MS`, 32 ms (`xhci_init.c`), on every path | 1, on |
| Q9 | `XhciAsmediaFlowControl` (`XHCI_ASMEDIA_MODIFY_FLOWCONTROL`) | `xhci-pci.c:472-474` | 1B21:1142 **(h)** (ASM1042A) | needs a vendor flow-control setting after each reset | `xhci.c:219-220` (after every HCRST), `xhci.c:1275-1276` (at the end of every resume): `pci-quirks.c:593-634`, a vendor write through configuration offsets E0h, F8h and FCh | no such write | 1, on |
| Q10 | `XhciTiIntervalCap` (`XHCI_LIMIT_ENDPOINT_INTERVAL_7`) | `xhci-pci.c:480-481` | 104C:8241 **(h)** | periodic Interval 7 or above fails at High Speed and above | `xhci-mem.c:1452-1463`: interrupt and isochronous, High Speed and above, Interval 7 or more programmed as 6 | no cap on 104C (`xhci_tol.c` `XhciTolCapApplies` is vendor 1022 only) | 1, on (interrupt endpoints only) |
| Q11 | `XhciIntervalCap`, existing (`XHCI_LIMIT_ENDPOINT_INTERVAL_9`) | `xhci-pci.c:304-306` | 1002:7316 **(h)** (ATI Navi10) | periodic Interval 9 or above fails | `xhci-mem.c:1452-1458`: Interval 9 or more programmed as 8 | `XhciIntervalCap` covers vendor 1022 only | its existing 1 |
| Q12 | `XhciStuckLinkReset` (`XHCI_MISSING_CAS`) | `xhci-pci.c:390-396` | 8086:22B5, 8086:9D2F, 8086:A12F, 8086:5AA8, 8086:19D0 | a USB3 port stuck in Polling or Compliance after resume without setting CAS | `xhci-hub.c:1885-1896` with `xhci-hub.c:1820-1841`: at bus resume a port with CCS and CAS clear and PLS Polling or Compliance is warm-reset | a restore marks every root port changed (`hcd_dev.c`, `XhciRootHubInit`), and the thread's reading warm-resets one in Compliance (`xhci_link.c`, 29-A.2): that half is covered. A port in Polling is waited on (`XHCI_LINK_ACT_WAIT`), so one stuck there is never reset | 1, on (the Polling half) |
| Q13 | `XhciStreamRingPad` (`XHCI_TRB_OVERFETCH`) | `xhci-pci.c:448-451`, `497-504` | 1106:3483 **(h)** (VL805), 1D17:9202 **(h)**, 1D17:9203 **(h)** | prefetches past the end of a ring segment | `xhci-mem.c:2441-2447`: every segment allocated with a second segment's space after it | the controller common buffer has driver memory after every ring (record 04 section 4); a stream block ends on its last stream ring (`xhci_stream.c` `XhciStreamLayout`, `XhciStreamLayoutPlaced`; `hcd_dma.c` `HcdDmaStreamAlloc`) | 1, on |
| Q14a | `XhciControlLinkNoOp` (`XHCI_ETRON_HOST`, part) | `xhci-pci.c:418-425` | 1B6F:7023 (EJ168), 1B6F:7052 (EJ188) | a SuperSpeed control transfer whose Setup and Data stages a Link TRB separates | `xhci-ring.c:3772-3784`: a No Op TRB first so the stages stay together | a control TD may cross the Link TRB (`xhci_ring.c` `XhciRingEnqueueTdGroup`) | 1, on |
| Q14c | `XhciIsochSpuriousSuccess` (`XHCI_ETRON_HOST`, part) | `xhci-pci.c:418-425` | 1B6F:7023, 1B6F:7052 | on an isochronous ring, a Success event after a Transaction Error, Babble or Isoch Buffer Overrun for the same TRB | `xhci-ring.c:2593-2597`: the Success taken as spurious | not established (section 4.14) | 1, on |
| Q16 | `XhciAmdSuspendBitClear` (`XHCI_DISABLE_SPARSE`) | `xhci-pci.c:326-329` | 1022:15E5 | (none stated in the mirror) | `xhci-pci.c:802-809`, `837-838`: before each suspend, bit 17 of BAR offset C12Ch cleared | no such write | 1, on |

### 3.2 Covered already

| Linux flag (or behaviour) | Set at | Controllers | What Linux does | How this driver covers it |
|---|---|---|---|---|
| `XHCI_SPURIOUS_SUCCESS` | `xhci.c:5482-5487`; `xhci-pci.c:453-461` | every controller above 0.96; 1B21:1042 | `xhci-ring.c:2587-2592`: a Success after a Short Packet on the same TD ignored | on every controller: the TD engine takes the second event the specification allows after a short packet, and the spurious one on the TD's last TRB, as known and counted (`xhci_xfer.c`, the short-packet tail handling, `MidTdDeferralsTailedSpurious`) |
| `XHCI_INTEL_HOST`, its reset delay | `xhci-pci.c:345-349` | every 8086 **(h)** | `xhci.c:205-213`: 1 ms after HCRST before any register access | on every controller: `xhci_init.c` `xhciReset` stalls 1 ms after the HCRST write. Its other uses (LPM timeouts, `xhci.c:4906-4907`, `4970-4971`, `5054`; eUSB2 double-bandwidth isochronous Mult, `xhci-mem.c:1351-1354`) are features this driver does not use |
| `XHCI_AVOID_BEI` | `xhci-pci.c:345-349` | every 8086 **(h)** | `xhci-ring.c:4048-4049`: BEI cleared on every eighth isochronous TD | BEI is never set (`xhci_xfer.c`, the isochronous TD builder: every TD carries IOC) |
| `XHCI_NO_SOFT_RETRY` | `xhci-pci.c:418-425`, `488-491` | 1B6F:7023, 1B6F:7052, 1022:43B9, 1022:43BB | `xhci-ring.c:2529-2533`: no soft retry | the same four ids are excluded from 35-T.2's soft retry (`xhci_tol.c` `XhciTolNoSoftRetry`; record 17 section 4.2) |
| `XHCI_LIMIT_ENDPOINT_INTERVAL_9` on the AMD ids | `xhci-pci.c:293-302` | 1022:13ED, 13EE, 148C, 15D4, 15D5, 15E0, 15E1, 15E5 | `xhci-mem.c:1452-1458`: interrupt and isochronous Interval 9 or more programmed as 8 | `XhciIntervalCap` 1 caps every 1022 controller, wider than Linux's list, interrupt endpoints only: isochronous ones are not capped, by the owner's decision of 2026-10-06 (record 17 section 4.7). Q11 adds the one id it missed |
| `XHCI_EP_LIMIT_QUIRK` | `xhci-pci.c:350-353` | 8086:1E31 **(h)** | `xhci.c:2264-2282`, `4202-4214` and the release paths: at most 64 active endpoint contexts, EP0s included | **covered once section 5's fix lands, not before** (review round 1): today a failed Configure Endpoint or SET_INTERFACE leaves endpoints enabled whose pool rings were returned, so the pool does not bound the enabled contexts. With section 5 a pool ring is held until a proof shows the controller has relinquished that allocation (section 5.2), and 32 slots' EP0s (`XHCI_MAX_SLOTS`) plus 32 pooled rings (`XHCI_MAX_POOL_RINGS`) bound the enabled contexts at 64; no quirk of its own (owner, 2026-10-13) |
| `XHCI_SNPS_BROKEN_SUSPEND` | `xhci-pci.c:322-324` | 1022:15E0, 1022:15E1 | `xhci.c:1025-1046`: a save that never clears SSS accepted, and the resume re-initializes | a save that does not complete leaves no valid state (`xhci_init.c` `xhciSaveState`), and the resume then re-initializes - the same end |
| `XHCI_NO_64BIT_SUPPORT` | `xhci-pci.c:462-470` | 1B21:1142, 1242, 2142, 3242 | `xhci.c:5515-5516`: AC64 ignored, 32-bit DMA | every address is below 4 GB and every high dword written 0 (AGENTS.md, "No 64-bit DMA") |
| `XHCI_U2_DISABLE_WAKE` | `xhci-pci.c:334-339` | 1022:43B9, 43BA, 43BB, 43BC | `xhci-hub.c:1776-1781` with `pci-quirks.c:506-589`: no wake bits on a USB 2.0 port the firmware disabled | no port wake bit is ever set (`PORTSC` WCE, WDE and WOE are defined in `xhci.h` and written by nothing), and the controller never arms system wake (section 3.4) |
| `XHCI_ZHAOXIN_HOST`, its port protocol | `xhci-pci.c:493-495` | every 1D17 **(h)** | `xhci-mem.c:2060-2066`, `2113-2116`: a USB 3.1 Minor Revision taken as 3.0 unless a PSI names Gen 2 | speeds come from each port's PSIV and PSI table, never from the Minor Revision (`xhci_caps.c`, `xhciDefaultKilobits`). Its LPM uses (`xhci.c:4906`, `4970`, `5056`) are unused here |
| The Intel port switchover, with its Sony exemption and Intel-EHCI test | `pci-quirks.c:1058-1147`, `1224-1225`; `xhci-pci.c:878-879` | Intel xHCI with an Intel EHCI beside it, except subsystem 104D:90A8 **(h)** | connectors routed to xHCI at handoff and at each resume | `XhciIntelPortSwitch` (record 16), off by default by the owner's decision of 2026-10-09. Record 16 section 4 takes the Sony exemption (it reads the subsystem dword and skips that board, or any board whose subsystem cannot be read) and replaces the EHCI scan with a list of the PCH generations that carry an EHCI; Linux's shutdown write (Q4) has no Sony exemption, and Q4 follows it |

### 3.3 Not applicable, deferred, dropped or not taken

| Linux flag (or behaviour) | Set at | Why |
|---|---|---|
| Q6, `XHCI_RESET_TO_DEFAULT` | `xhci-pci.c:398-402` | 8086:A0ED, 51ED, 54ED (Tiger Lake PCH, Alder Lake PCH and N): HCRST at shutdown (`xhci.c:774-776`) and, at hibernate, a root port in U3 without wake disabled (`xhci-pci.c:890-936`). **Not applicable** by the owner's ruling that these platforms have no CSM (section 3.0) |
| Q17, `XHCI_SSIC_PORT_UNUSED` | `xhci-pci.c:382-384` | 8086:22B5: vendor registers at BAR offset 880Ch + 30h * i marked around D3, or the controller may not enter D3 (`xhci-pci.c:748-784`). **Deferred pending platform evidence**: the Cherry Trail half is ruled out, the Braswell half is not established (section 3.0) |
| Q15, the forced BIOS handoff | `pci-quirks.c:1190-1196` | 104C:8241 **(h)**, 1912:0014 **(h)**: OS Owned set and BIOS Owned cleared in one write. **Dropped** (owner's condition, 2026-10-13: taken only if xHCI 1.2c permits the OS to clear BIOS Owned). Table 7-4 (p.478) gives bit 16 the attribute RW, but assigns it to the BIOS: "The BIOS sets this bit to establish ownership of the xHC. System BIOS will set this bit to a '0' in response to a request for ownership of the xHC by system software". Section 4.22.1 (p.309-310) gives the OS one action - set OS Owned and wait for BIOS Owned to read 0, for no more than a second - and no step in which the OS clears BIOS Owned. So the specification's protocol does not provide for it, though the register would accept the write. (The handoff's comment in `xhciBiosHandoff` that bit 16 is "read-only from the OS side too" overstates the table's RW; a wording fix at implementation) |
| Q18, `XHCI_SW_BW_CHECKING` | `xhci-pci.c:354` | 8086:1E31 **(h)**: Linux keeps its own periodic bandwidth tables (`xhci.c:2996-3003` and the reservation code behind it) because the controller admits schedules it cannot serve. **Not taken** (owner): a task of its own if a report needs it. Known limitation, stated in the README: on 1E31 the driver relies on the controller's own admission, which Linux holds to be insufficient there, so an over-subscribed periodic schedule may lose data instead of being refused |
| Q19, `XHCI_AMD_PLL_FIX` | `xhci-pci.c:311-313` | every 1022 xHCI on an SB700 (revision 3Bh or below), SB800, Hudson-2 or Bolton chipset (`pci-quirks.c:153-194`, `209-287`): chipset power management held off while isochronous requests are queued (`pci-quirks.c:340-455`; `xhci-ring.c:832-837`, `4209-4212`), through I/O ports CD6h and CD7h and the northbridge's configuration space. **Not taken** (owner): a task of its own if a report needs it. It needs port-I/O imports, and `HAL.dll!WRITE_PORT_UCHAR` is the import kept out of the shipping flavours because defect 2b's cause is not established (AGENTS.md, "Build flavours"); it writes devices other than the xHCI function; and these FCH parts' HCIVERSION is not in the mirror |
| Q20, `XHCI_CDNS_SCTX_QUIRK` | `xhci-pci.c:506-508` | 17CD:0200 **(h)**: two reserved dwords of a stream context cleared after Set TR Dequeue (`xhci-ring.c:1490-1502`). **Not taken as such** (owner, on review round 1): the id is added to Q2's `XhciStreamsOff` list instead, so the driver never gives that controller a stream context to corrupt and writes no stream context |
| `XHCI_LINK_TRB_QUIRK` | `xhci.c:5489-5492` | HCIVERSION 0.95 only, and only with a module parameter that defaults to 0. This driver refuses a controller below 1.0 (`xhci_caps.c` `XhciCheckCapDword0`, `XHCI_HC_BAD_VERSION`); task 38.11 moves the floor to 0.96 (owner, 2026-10-13, design record 17 section 4.14), which still refuses 0.95 |
| `XHCI_AMD_0x96_HOST` | `xhci-pci.c:308-309` | HCIVERSION 0.96 only (its use, `xhci.h:1782-1786`: isochronous Link TRBs kept chained). **Not taken** (owner, 2026-10-13): with task 38.11 the 0.96 controllers start, driven as 1.0 ones but for the TD Size field, and the 0.96-era quirks are not taken there |
| `XHCI_NEC_HOST` | `xhci-pci.c:290-291` | every 1033 **(h)**: a vendor Get Firmware command at start whose answer is only logged (`xhci.c:652-663`, `xhci-ring.c:1683-1694`, `1935-1941`), and isochronous Link TRBs kept chained (`xhci.h:1779-1786`). The command is diagnostic. **The chaining is not taken** (owner, 2026-10-13, with task 38.11). Linux keys both on the vendor alone, whatever the version, though its comment names "0.96 hardware from AMD and NEC"; that every 1033 part is 0.96 is an assumption this survey does not establish, and the decision does not rest on it. Note (owner, 2026-10-14): Linux also rings the command doorbell right after Run on these hosts (`xhci.c:606-607`) and treats vendor event type 48 as a command completion (`xhci-ring.c:1935-1941`, `TRB_NEC_CMD_COMP` at `xhci.h:1170`); this driver issues no vendor command, and its first command after Run, the No Op self-test, rings doorbell 0 anyway, so neither is taken |
| `XHCI_SPURIOUS_SUCCESS` and `XHCI_BROKEN_STREAMS` on 1B21:1042 | `xhci-pci.c:453-461` | "reports 0.96" (`xhci-pci.c:455-458`): refused by today's floor; once task 38.11 lands it starts, and then the spurious Success is covered by the TD engine as on every controller (section 3.2) and the streams by Q2, whose list carries the id. `XhciStreamsOff` matters for it once both tasks land |
| `XHCI_RESET_EP_QUIRK`, `XHCI_TRUST_TX_LENGTH`, `XHCI_PLAT` | `xhci.h:1589`, `1607`, `1613` | deprecated; the first is set for 1B73:1000 revision 00 with a trace that says so (`xhci-pci.c:259-264`) and acts nowhere; the other two are set by no file in the mirror |
| `XHCI_LPM_SUPPORT` | `xhci-pci.c:345-349`, `448-451`, `493-495` | link power management: this driver programs no U1 or U2 timeout and no USB 2.0 LPM (no `PORTPMSC` write anywhere in `src`) |
| `XHCI_HW_LPM_DISABLE` | `xhci.h:1628` | set by no file in the mirror (its use, `xhci.c:4671-4672`, is USB 2.0 hardware LPM, which this driver does not use) |
| `XHCI_PME_STUCK_QUIRK` | `xhci-pci.c:371-381` | clears a PME latch so wake works (`xhci-pci.c:786-800`); the controller never arms wake here (section 3.4) |
| `XHCI_DEFAULT_PM_RUNTIME_ALLOW` | `xhci-pci.c:331-332`, `404-416`, `510-512` | Linux's runtime power policy (`xhci-pci.c:670-673`, `729-732`); no counterpart here |
| `XHCI_INTEL_USB_ROLE_SW` | `xhci-pci.c:385-389` | creates Linux's dual-role switch device (`xhci-ext-caps.c:95-100`); no counterpart here |
| `XHCI_BROKEN_D3COLD_S2I` | `xhci-pci.c:341-343` | suspend-to-idle only (`xhci-pci.c:824-829`), a Linux sleep state no target has |
| `XHCI_ZERO_64B_REGS` | `xhci-pci.c:427-435` | acts only behind a translating IOMMU (`xhci.c:257-264`); no target remaps this driver's DMA through one, as far as this survey read, and the driver writes every high dword 0 |
| `XHCI_RESET_PLL_ON_DISCONNECT` | `xhci-pci.c:483-486` | 14E4 **(h)** or 177D **(h)** device 9026: a PHY reset through vendor registers on a USB 2.0 disconnect (`xhci-ring.c:1975-1990`, `2134-2139`). Not x86 (section 3.0) |
| `XHCI_COMP_MODE_QUIRK` | `xhci.c:567-571` | keyed on DMI strings (HP Z420, Z620, Z820, Z1 Workstation, `xhci.c:425-450`), not on a PCI id, so outside the task's matching. Its symptom - a port in Compliance Mode with no change event - is partly covered: 29-A.2 warm-resets Compliance when a change shows it |
| `XHCI_MTK_HOST`, `XHCI_SKIP_PHY_INIT`, `XHCI_SUSPEND_RESUME_CLKS`, `XHCI_WRITE_64_HI_LO`, `XHCI_BROKEN_PORT_PED`, `XHCI_SG_TRB_CACHE_SIZE_QUIRK`, `XHCI_EP_CTX_BROKEN_DCS` | `xhci.h:1619`, `1636`, `1642`, `1646`, `1624`, `1638`, `1641` | set by no file in the mirror: the platform glue sets them, or (`XHCI_EP_CTX_BROKEN_DCS`) no mainline file the mirror carries. Platform glue is not a PCI controller this driver binds |
| The early-handoff exemptions | `pci-quirks.c:1266-1282` | Linux skips its early BIOS handoff for vendor 184E (a Netlogic MIPS SoC's internal controller) and for 1106:3483 when its parent is a `brcm,bcm2711-pcie` bridge (the Raspberry Pi 4, whose co-processor handles it). Neither platform is x86, and this driver's own handoff is unchanged for a VL805 on a PCIe card |
| The Renesas firmware load | `xhci-pci-renesas.c:579-626`; `xhci-pci.c:692-710` | a ROM-less uPD720201 or uPD720202 needs firmware the OS loads. That firmware is a third-party binary this project does not carry (AGENTS.md, "Never track a third-party document or binary"); a card with its ROM works without it. `XHCIQUAL` already reports the case (`QF_FW_UPLOAD`) |
| The Promontory 21 parts | `xhci-pci.c:699-714`, `xhci-pci.h:7-8` | 1022:43FC and 1022:43FD go to a glue driver the mirror lacks; nothing to survey (record 17 section 1 has the context) |

### 3.4 What the driver does that decides the groups

Read in `src` on branch `phase-38`, each by file and function:

- **Identity.** The PCI vendor and device dword (offset 0) is read once at
  each start into `XHCI_EXTENSION.PciVendorDevice` (`xhci_init.c`,
  `XhciInitController`); the revision (offset 08h) is not read. HCIVERSION
  is read and anything below 1.0 refused (`xhci_caps.c`,
  `XhciCheckCapDword0`); task 38.11 moves that floor to 0.96 behind a
  value of its own.
- **Settings.** Every value lives in the controller's driver key
  (`hcd_ctl.c`, `hcdReadDword`, `IoOpenDeviceRegistryKey` with
  `PLUGPLAY_REGKEY_DRIVER`), read at each start, a REG_DWORD of four bytes
  only. On NT 6 the requirements filter, which runs before the start, reads
  no registry: it uses `XhciInterruptMode` as `HcdMsiAddDevice` cached it
  at AddDevice or the last start (`hcd_msi86.c`). The id gates until now
  are `XhciIntelPortSwitch`, `XhciIntelIrqRoute` and `XhciIntervalCap`,
  each 0, 1 or 2 with 1 the listed controllers.
- **Reset.** A 1 ms stall follows every HCRST (`xhci_init.c`,
  `xhciReset`). The in-place recovery runs at DISPATCH_LEVEL
  (`hcd_ctl.c`, `hcdRecover` raises around `XhciRecoverController`); a
  configuration-space access needs PASSIVE_LEVEL (`hcd_svc.c`), which is
  why the Bus Master Enable restore is made after the raise, under the same
  power gate.
- **Suspend, resume, shutdown.** Every D3, the shutdown's included,
  quiesces, halts within 32 ms and saves with CSS where the controller has
  FSC (`xhci_init.c`, `XhciSuspendController`, `xhciSaveState`); a halt or
  quiesce that fails is counted (`SuspendFailures`) and the suspend goes
  on. A D0 restores with CRS when the save is valid, without an HCRST, and
  otherwise re-initializes, dropping every device (`XhciResumeController`).
  The shutdown's D3 adds 37.3's flush and `HcdPswRelease` (`hcd_power.c`),
  all before the D3 IRP is passed down. No HCRST at shutdown.
- **Wake.** The controller FDO never requests `IRP_MN_WAIT_WAKE`, the
  root-hub PDO refuses it (`hcd_rh.c`), and no `PORTSC` wake bit is set.
  System wake from USB is not offered; selective suspend's remote wake
  (record 14) is a device's, in S0.
- **Rings.** The Chain bit of the permanent Link TRB is rewritten at each
  crossing from the TD that crosses it, clear between TDs (`xhci_ring.c`,
  `XhciRingEnqueueTdGroup`), as xHCI 1.0 onward asks. Every non-EP0
  endpoint's ring comes from the pool (`hcd_cfg.c`, `hcdCfgPipeNew`,
  `XhciPoolAcquire`) and goes back in `hcdCfgPipeFree`.
- **Streams and UAS.** Streams are offered wherever MaxPSASize > 0
  (`hcd_cfg.c`, `hcdCfgStreamsOpen`); a UAS-only device at SuperSpeed on a
  controller with none has no transport and goes back to USB 2.0 (29-A.5;
  `xhci_xport.h`).
- **Interrupts.** 98 SE to XP take the line or poll; Vista and 7 take a
  message where Windows grants one (records 15 and 18), the pure
  `XhciPollDecide` (`xhci_poll.c`) choosing; the dead-line rule watches
  only a line.
- **Configuration space.** Writes above the standard header are the Intel
  switchover's alone (`implementation-invariants.md`, "PnP Resources").

## 4. The quirks taken

Each: what Linux does, where this driver acts, the change, its value and
default, its cost, and what the reports show. The behaviour named is
Linux's as read; the implementation checks each against xHCI 1.2c before
it is written (task 38.10). Values are 0 off, 1 Linux's ids and 2 every
controller where offered (section 6.1). Every quirk that writes a register
does so only on a controller whose window decodes (`HcInfoStatus` good, not
latched unreadable), in D0, and logs a skipped or failed action (section
6.4).

### 4.1 Q1, `XhciMsiAvoid` (Fresco Logic, `XHCI_BROKEN_MSI`)

Linux never asks for MSI on 1B73:1000 or 1B73:1400 and takes the line.
Here the question arises only on Vista and 7, where a message is
connected when Windows grants one. On a listed controller the message
would never arrive, and the dead-line watcher does not watch a message;
only 35-T.1's lost-interrupt backstop would drain the ring, each event at
least 100 ms and typically 300 to 400 ms late (record 17 section 4.1).

Two places change, because the first can only ask and the second decides:

- **The requirements filter asks for the line.** On a listed controller
  the filter keeps only the line configurations, as `XhciInterruptMode` 1
  and the no-helper x86 path already make it do (record 18 section 4.2).
  The filter runs before the start and reads no registry, so its inputs
  are cached where the mode is. `HcdMsiAddDevice` runs before
  `IoAttachDeviceToDeviceStack` (`hcd_entry.c`, `XhciAddDevice`), so the
  value is cached there with the mode, and the vendor and device dword
  (configuration offset 0) is read just after the attach, in the same
  AddDevice, through the PCI-PDO configuration service (`hcd_svc.c`, which
  sends to `hc->Pdo`). Each start refreshes both caches, as it does the
  mode's. An identity that cannot be read leaves the filter as it is (and
  is logged), and the start's decision below still protects the
  controller. A rebalance re-filters on the same cache, since the device
  does not change. Where the
  list has a shape the filter does not replace (record 18 section 4.2's
  unsupported shapes, `hcd_msi86.c`), it stays as given.
- **The start's delivery decision refuses the message.** `XhciPollDecide`
  gains the effective quirk as an input. Its identity cannot come from
  `PciVendorDevice`, which `XhciInitController` reads only after the
  delivery is settled and connected (`hcd_ctl.c`: `hcdSettleDelivery`, then
  `hcdConnect`, then `XhciInitController`); so the start reads
  configuration offset 0 itself, through the same service, before
  `hcdSettleDelivery`. A read that fails keeps the identity already
  established for this FDO (AddDevice's, or an earlier start's: the device
  does not change within an FDO's lifetime), logged as "identity kept". If
  no identity has ever been established, the start is conservative: at
  value 1 or 2 the message is avoided as if the controller were listed
  (line, else polling, under mode 0), logged as "identity unread"; a
  wrongly avoided message costs a line or polling on an unknown controller,
  where a wrongly taken one could cost a dead controller.
  `XhciInitController`'s own read later in the start is compared with the
  identity used and a disagreement logged. With the quirk effective:

  - **0, automatic (the default).** A message resource is never connected:
    the line where Windows granted one, and polling where it granted only a
    message or nothing, or once the line is proven dead - the usual
    fallback. A new reason code says the message was refused by the quirk.
  - **1, the line only.** Unchanged: the line, and a start that fails where
    no line is granted, as mode 1 does everywhere.
  - **2, polling.** Unchanged.
  - **3, a message only.** The user asked for a message by name, so the
    value is honoured and neither the filter nor the decision strips it; the
    log says the quirk was overridden (`quirk.msiavoid`, section 6.4). Mode 3
    exists to test a message on purpose; the quirk is not a reason to forbid
    that, and it does not weaken mode 0's protection.

98 SE to XP are unaffected (no message there). 2 applies the same on every
controller, with mode 0's fallback to polling - which is what separates it
from `XhciInterruptMode` 1. Default 1.

### 4.2 Q2, `XhciStreamsOff` (`XHCI_BROKEN_STREAMS`)

Linux offers no streams on six ids; 17CD:0200 joins them here (Q20, owner).
On a listed controller both consumers of MaxPSASize read it as 0: the
transport decision (UAS at SuperSpeed only where the controller streams;
`xhci_xport.h`) and `OPEN_STREAMS` (`hcdCfgStreamsOpen`, which already
refuses with `XHCI98_STREAMS_NO_CONTROLLER`), from one effective value
computed at the start so the two cannot disagree. A UAS device that also
offers Bulk-Only takes Bulk-Only; a UAS-only device at SuperSpeed goes back
to USB 2.0 by 29-A.5, where streamless UAS runs. 1B21:1042 and any other
0.96 part on the list start only once task 38.11 lands, and the value
matters for them from then. 2 makes every controller read as streamless (a
tester's way to take streams out of a report). Default 1. Cost: the pure
decision gains one input; host tests of both consumers.

### 4.3 Q3, `XhciResumeReinit` (`XHCI_RESET_ON_RESUME`)

Linux re-initializes these controllers at every resume. Here, on a listed
controller, the suspend does not save (no CSS, `SavedStateValid` left 0)
and the resume does not restore: it takes the existing re-initialization,
the path a failed restore already takes (`XhciResumeController`), which
drops every device and re-enumerates it. 2 applies it on every controller.
Default 1. Cost: two tests; the user-visible cost on a listed controller
is that a device in use across standby or hibernate is re-enumerated, as on
any controller without FSC today. The matching is vendor-wide for VIA
(section 6.2).

### 4.4 Q4, `XhciIntelShutdownToEhci` (`XHCI_SPURIOUS_REBOOT`)

Linux writes both routing registers (`USB3_PSSEN` D8h, `XUSB2PR` D0h) to 0
at every shutdown on 8086:1E31, 9C31 and 9CB1, so the connectors are
EHCI's when the machine powers off: on some 7-series desktop boards a
machine left with them on xHCI powers itself back on. Here the shutdown's
release writes back only what a route of this driver wrote, so with
`XhciIntelPortSwitch` 0, the default, connectors the firmware routed to
xHCI stayed there through the shutdown.

The change: at the shutdown's D3 (not at a sleep's), on a listed
controller, both registers are written 0 **whatever `XhciIntelPortSwitch`
says** (owner, 2026-10-13), through the existing config-space path
(`HcdSvcConfigSpace`) at PASSIVE_LEVEL, after the existing release and
before the D3 IRP goes down. Each write is read back; a failed write is
logged and the shutdown goes on. Linux's shutdown write has no Sony
exemption (its exemption is the enable's, `pci-quirks.c:1064-1069`), and
neither has this. Record 16 section 5's "0 ... makes no switchover write"
gains this exception, and `implementation-invariants.md` names the write.
0 or 1 only: the registers are Intel's and exist only on the PCH
generations record 16 lists, so a 2 is read as 1 and logged (section 6.1).
Default 1. The project's B490 (1E31) and E7250 (9CB1) are listed.

### 4.5 Q5, `XhciShutdownReset` (`XHCI_SPURIOUS_WAKEUP`), off by default

Linux resets the controller (HCRST) after the shutdown's halt and puts the
function in D3hot. Here, at the shutdown's D3 only, on a listed controller
when the value is 1 and on every controller at 2, an HCRST follows the
halt - **only on a halt proven**: xHCI 1.2c 5.4.1 (p.360) forbids setting
HCRST while HCHalted is 0, and the suspend path goes on after a failed
quiesce or halt (`XhciSuspendController`). The preconditions, each checked
and each failure logged and the reset skipped:

- a started lifetime with its BAR mapped (`ControllerStarted` and `BarVa`
  not NULL), checked under the power gate before the first register access,
  as 37.3's shutdown flush checks it (`hcd_power.c`: a stopped FDO keeps
  `HcInfoStatus` and its recorded D0 with its registers unmapped);
- the window decodes (`HcInfoStatus` good, not latched unreadable) and the
  controller is in D0;
- USBSTS reads other than all ones and with HCH 1, read after the halt
  (`XhciTolHaltProven`, the test the shutdown flush uses);
- the existing reset-readiness rules of `xhciReset` (no HCRST still
  pending, CNR handled), with its 1 ms stall and bounded wait.

Then the saved image is invalidated (`SavedStateValid` 0), since HCRST
destroys what CSS saved. All of it runs before the D3 IRP is passed down
(`hcd_power.c`). Whether the PCI bus driver of each target then sets D3hot
is the operating system's and is not read; the HCRST is the part this
driver owns. **Default 0, off** (owner, 2026-10-13): nothing the project
has read shows the symptom, and the E7250 (9CB1) runs without it; the value
is there for a machine that wakes by itself after shutdown. Cost: one
bounded HCRST at shutdown when on.

### 4.6 Q7, `XhciSuspendHaltDelay` (AMD, `XHCI_SUSPEND_DELAY`)

Linux sleeps 1 to 1.5 ms before clearing Run/Stop at suspend on 1022:145C,
15E0, 15E1 and 43BB. Here the suspend waits about 1.5 ms (a requested
delay at PASSIVE_LEVEL, not an exact one) before `XhciQuiesceController` on
a listed controller, in the power path. 2 applies the delay on every
controller. Default 1. Cost: about 1.5 ms per D3.

### 4.7 Q8, `XhciSuspendHaltTimeout` (Fresco Logic FL1000, `XHCI_SLOW_SUSPEND`)

Linux allows ten times its usual 64 ms for the suspend's halt on
1B73:1000 revision 04. Here every halt waits at most 32 ms
(`XHCI_HALT_TIMEOUT_MS`); a halt that times out at suspend is counted
(`SuspendFailures`) and leaves the controller unsaved and possibly running
into D3. The change: on a listed controller **the suspend's** halt waits up
to 640 ms; the start's, the recovery's and the stop's halts keep 32 ms.
The wait ends as soon as HCH reads 1, so a longer bound costs a healthy
controller nothing, which is why every revision of 1B73:1000 is matched
(section 6.3); that reasoning is this quirk's and is not a rule for later
quirks. 2 applies the longer bound on every controller. Default 1.

### 4.8 Q9, `XhciAsmediaFlowControl` (ASM1042A, `XHCI_ASMEDIA_MODIFY_FLOWCONTROL`)

Linux performs a vendor write after every HCRST and at the end of every
resume on 1B21:1142 (`pci-quirks.c:593-634`): wait for the write-busy bit
of configuration byte E0h to clear; write a command to F8h and an address
to FCh and set the write bit in E0h; wait again; write the data to F8h and
a pseudo-data dword to FCh and set the write bit again. That is six
configuration writes and two bounded waits (Linux allows 1000 x 50 us
each, giving up on a read of FFh or on the bound); the values are constants
there.

Here a configuration access needs PASSIVE_LEVEL, and the recovery's reset
runs at DISPATCH_LEVEL (section 3.4), so the write cannot follow the HCRST
inside it. It runs instead as a PASSIVE_LEVEL step, under the power gate,
before normal service resumes, after each of the paths that leave the
controller reset or restored:

- **the start**, after `XhciInitController` returns, before the
  enumeration begins;
- **the recovery**, in `hcdRecover` after the lowered IRQL and before
  `HcdPollGrant` (where the Bus Master Enable restore is already made);
- **the resume, both branches**: after `XhciResumeController` returns,
  whether it restored (no HCRST, as Linux's resume also writes it at its
  end, `xhci.c:1274-1276`) or re-initialized, before the power gate is
  left.

The controller is running by then on every path, as it is in Linux's
resume. A wait that gives up, or a read of FFh, skips the rest and is
logged; service goes on. It is a second kind of configuration write above
the standard header, beside the Intel switchover's, so
`implementation-invariants.md` changes with it. 0 or 1 only: the registers
are this vendor's, so a 2 is read as 1 and logged. Default 1.

### 4.9 Q10, `XhciTiIntervalCap` (TUSB73x0, `XHCI_LIMIT_ENDPOINT_INTERVAL_7`)

Linux programs a periodic endpoint's Interval as 6 where it would be 7 or
more, at High Speed and above, on 104C:8241. Here it takes
`XhciTolCapInterval`'s shape with another threshold and another id, for
**interrupt endpoints only** (owner, 2026-10-13), as record 17 decided for
the AMD cap: an isochronous endpoint's interval sets its data rate. Linux's
speed qualification is kept: Full- and Low-Speed endpoints are not
capped. The cap's ordering against fast polling (record 17 section 4.7)
carries over. 2 applies the cap on every controller. Default 1.

### 4.10 Q11, 1002:7316 under `XhciIntervalCap`

Linux caps Interval at 8 on 1002:7316, a controller of another vendor id
than `XhciIntervalCap`'s. `XhciIntervalCap` 1 covers this id as well (owner,
2026-10-13), with the AMD cap's threshold and endpoint types unchanged; no
new value; its 0 and 2 unchanged. Cost: one id in `XhciTolCapApplies`, and
record 17 section 4.7 says so.

### 4.11 Q12, `XhciStuckLinkReset` (`XHCI_MISSING_CAS`)

Linux, at each bus resume, warm-resets a USB3 root port whose CCS and CAS
are both clear while its link reads Polling or Compliance, because these
controllers can leave a port so after resume without the CAS that would
announce it. Here a resume that restored marks every root port changed
(`XhciRootHubInit`), so the thread reads each, and 29-A.2 warm-resets one
in Compliance whatever its CAS: that half is covered. A port in Polling is
waited on as a link still training (`xhci_link.c`, `XHCI_LINK_ACT_WAIT`),
and one stuck there raises no further change, so it is never reset. The
change (owner, 2026-10-13: the Polling half): on a listed controller, a
USB3 root port read with CCS and CAS clear and PLS Polling at that
post-resume reading is warm-reset within 29-A.2's existing budget and its
existing exclusions (a port held by 29-A.5 is not touched). The
re-initializing path starts from HCRST and needs nothing. 2 applies it on
every controller. Default 1. The project's E460 (9D2F) is listed.

### 4.12 Q13, `XhciStreamRingPad` (VIA VL805, Zhaoxin, `XHCI_TRB_OVERFETCH`)

Linux gives every ring segment a second segment's worth of space after it
on these. Here the controller common buffer already has driver memory
after every ring (record 04 section 4: the last region is the scratchpad
pages); the one place a ring ends at the end of an allocation is a stream
block, whose stream rings come last (`XhciStreamLayout`,
`XhciStreamLayoutPlaced`). The change: on a listed controller a stream
block is allocated one ring longer, in both layouts: the size computed
(`XhciStreamLayout`'s total, `XhciStreamBigBytes`), the alignment rules
unchanged, and the same size passed to the free (`HcdDmaStreamFree` with
the layout's total), with host vectors of both layouts padded and not. 2
pads on every controller. Default 1. The VL805 has a field user (issue
13) and does streams (MaxPSASize 4, issue 13 section 4).

### 4.13 Q14a, `XhciControlLinkNoOp` (Etron, `XHCI_ETRON_HOST`)

Linux, on the EJ168 and EJ188, puts a No Op TRB first when a SuperSpeed
control transfer's Setup TRB would be the last before the Link TRB, so
Setup and Data are not split by the link. Here the control TD is enqueued
as one group that may cross the link (`XhciRingEnqueueTdGroup`); the change
is a Transfer No Op at the ring's last usable position when the next TD is
a SuperSpeed control TD, counted in the capacity check before the group is
admitted (one more TRB). Small, in the pure ring core, host-testable. 2
applies it on every controller (it is generic and harmless on a compliant
one). Default 1.

Linux gives these parts three more bits: Q2's and Q3's lists include them,
and the soft-retry exclusion is covered (section 3.2). Its third
`XHCI_ETRON_HOST` behaviour, a Reset Device replaced by Disable Slot and a
new slot (`xhci.c:3974-3986`), is **left as it is** (owner, on review round
1): `RESET_PORT` keeps issuing Reset Device here. `XHCIQUAL` marks both
parts `QF_AVOID`, the project's own verdict.

### 4.14 Q14c, `XhciIsochSpuriousSuccess` (Etron, `XHCI_ETRON_HOST`)

Linux, on these parts, ignores a Success event on an isochronous ring that
follows a Transaction Error, Babble or Isoch Buffer Overrun for the same
TRB. Here what such an event meets is not established: if its TD was
retired by the error, the event finds no TD and is unattributed, and an
isochronous one cycles nothing (`XhciTolCycleReason`); but a request of
several packets may still be pending, so the event's pointer can fall
inside a request that is still live. The change, on a listed controller:
a Success whose TRB pointer names a TRB this endpoint's last event already
completed with one of those three codes is counted and dropped. The
properties host vectors prove, with the owning request both still pending
and already retired: a packet is never completed twice, a later packet or
request is never consumed by it, and no recovery or cycle follows. 2
applies it on every controller. Default 1.

### 4.15 Q16, `XhciAmdSuspendBitClear` (AMD Raven2, `XHCI_DISABLE_SPARSE`)

Linux clears bit 17 of BAR offset C12Ch before each suspend on 1022:15E5.
The mirror says nothing of what the bit does, and this record claims no
knowledge of it: the value is named for the action. Here it is one
read-modify-write in the D3 path on a listed controller, made only when:

- a started lifetime with its BAR mapped (`ControllerStarted` and `BarVa`
  not NULL), checked under the power gate before the access, as Q5 checks
  it (section 4.5): a stopped FDO keeps `IoSpaceLength`, `HcInfoStatus`
  and its recorded D0 after the BAR is unmapped (`hcd_power.c`); the write
  is made inside the D3 path's existing guarded suspend, before
  `XhciSuspendController`;
- the mapped window is at least C130h bytes (`IoSpaceLength`), so the
  dword lies inside the resource Windows granted;
- the controller is in D0 and its window decodes (`HcInfoStatus` good, not
  latched unreadable);
- the read is not all ones.

Otherwise it is skipped and logged. 0 or 1 only: an AMD vendor bit, so a 2
is read as 1 and logged. Default 1. Q3 also lists 15E5.

## 5. Endpoint rings held until the controller drops them

### 5.1 What review round 1 found

A pool ring goes back to the pool in `hcdCfgPipeFree`
(`XhciPoolRelease`), and the driver frees pipes at two points where the
controller may still hold their endpoints enabled:

- **A failed Configure Endpoint.** `hcdCfgSelectInterface` closes the
  interface's old pipes (`hcdCfgCloseMask`, which frees each) **before**
  the Configure Endpoint that drops their endpoints; when that command
  fails, the old endpoints stay enabled with no pipe (`dev->Stale`) and
  their rings are already back in the pool. The function select does the
  same (`hcdCfgCloseFunction`, then `hcdCfgReplace`).
- **A failed SET_INTERFACE after a successful Configure Endpoint.** The
  new endpoints are enabled on the controller but not on the device; their
  pipes are freed and their endpoints left for the next command to drop
  (`dev->Stale`), in `hcdCfgSelectInterface` and `hcdCfgSelectFunction`.

A freed ring can then back another endpoint on another slot while the
stale context still names it, and the enabled contexts are no longer
bounded by the pool. Stream blocks already have the right rule: one whose
endpoint is not yet Disabled is retired to the device
(`dev->StreamsRetired`) and freed only on a proof (`hcdCfgRetiredReclaim`,
`HcdCfgDeviceGone`).

### 5.2 The rule: an allocation is owned until a proof releases it

**A pool ring, and a stream block, return only once the controller is
proven done with them** (owner, 2026-10-13: fix it in 38.10, the Linux
way). Ownership is tracked per allocation, by the pipe object that holds
it, never inferred afterwards from the endpoint's current state - an
endpoint replaced by a Drop-and-Add Configure Endpoint is Running again,
on its new context, while the old allocation is free (xHCI 1.2c 4.6.6,
p.106-107). Each allocation is in one of three states:

- **New:** not yet exposed: no command naming it has been issued. It
  becomes owned the moment one is; a command that turns out refused or was
  never issued returns it to new, and it is freed at once.
- **Owned:** installed, or possibly installed (a command naming it issued
  and not yet answered, or answered unknown). Held, and its pipe's DCI
  charged.
- **Superseded:** a proof has released it: a successful Configure Endpoint
  that names it in a drop (alone or with an add), a successful Reset Device
  (every endpoint but EP0), a Disable Slot, an HCRST (start, recovery,
  re-initializing resume), or the slot gone - the proofs the stream blocks
  already use (`hcdCfgRetiredReclaim`, `HcdCfgDeviceGone`). A superseded
  allocation may be freed or, while its pipe object is kept for a rollback
  (section 5.3, step 4), held: not charged as a context, but still reserved
  in the pool (never reusable) until it is freed.

Ownership changes on a command's completion, whatever follows it on the
bus: a later control transfer's failure never reverses what a successful
command installed or released.

**The per-device cap during a transaction.** The pool refuses a device
already holding `XHCI_MAX_DEVICE_ENDPOINTS` (four) rings (`xhci_mem.c`,
`XhciPoolAcquire`; `xhci.h`), so holding the old rings while the new are
made would refuse ordinary switches - a device with four endpoints could
not make its first replacement. The cap is therefore taken against a
device's rings outside any transaction. A transaction's reservation is
fixed before it allocates anything: every allocation its command will
drop - the pipes it detaches **and** any allocation already retained as
stale from an earlier failure (`dev->Stale`, which the command's drop mask
already includes, `hcd_cfg.c` in `hcdCfgSelectInterface` and
`hcdCfgReplace`) - so stale rings left by one failure never block the
select that would release them. The reservation is bounded by the cap
again (a device holds at most twice the cap, only while a switch is in
flight). Two counts are kept apart, because `XhciPoolAcquire` today uses
one `held` count for both the cap and its first-ring fairness exemption
(`xhci_mem.c`): the **cap** count excludes the transaction's reservation;
the physical count - pool occupancy, free-ring accounting,
`dev->PoolRings`, and the fairness test - includes every retained
allocation, so a replacement never passes for a device's first ring. The
pool's own size, 32, still bounds everything the controller can hold
(section 5.5).

When a transaction ends without its command succeeding (the pool or the
fairness test refuses a replacement, or the command is refused or not
issued), its reservation settles back: the pipes it detached are
reattached (section 5.3), and allocations that were already stale stay
stale and retained, never republished.

A successful command's proof is applied to the allocations it named - the
detached old pipes and their stream blocks among them - before any
replacement is published, so `hcdCfgPipeFree`'s present test of the DCI's
current state (`hcd_cfg.c`, the `hcdCfgEpState` call) is not what decides
for a pipe the transaction detached.

A command has four outcomes, kept apart (the API already separates
`HCD_CFG_NOT_ISSUED` from an issued command, and `hcdCommand` returns 0
after a lost completion or a timeout and requests the recovery,
`hcd_enum.c`): **not issued**; **success**; **refused** (a completion with
an error code); and **unknown** (issued, no completion seen). Only a
refusal is the 4.6.6 case - "If the command is unsuccessful: ... The Output
Endpoint Contexts referenced by the command in the Device Context shall be
unchanged" (p.106); an unobserved completion proves nothing either way, so
after **unknown** every allocation the command named stays owned, every
affected pipe stays detached with submissions refused, and the recovery's
HCRST, which the timeout has already requested, supersedes them all.
Section 4.3.6's example (p.78-79) frees the affected rings before the
command; it is the sequence for a command that succeeds and does not
license freeing what the controller may still hold.

### 5.3 SELECT_INTERFACE, rewritten to it

1. **The old pipes are aborted resumably, then detached.** Each is paused
   and taken through the existing reusable abort, `hcdCfgAbortPaused`:
   every request cancelled, the endpoint Stopped, the ring emptied, Set TR
   Dequeue to the enqueue position, every stream likewise, and the
   cancellations settled - so a pipe resumed later restarts no TD whose
   request already completed. Pausing does not close admission
   (`hcd_io.c`, `HcdIoSubmit` accepts on a published, paused pipe), and the
   abort's wait covers only the records it marked, so a request can arrive
   between the abort and the detach. So each pipe is then unpublished
   atomically under the controller lock (`Closed`, the published pointer
   cleared, its streams' handles made not to resolve) and drained and waited
   out again - `HcdIoDrainPipe` then `HcdIoWaitPipe`, the order
   `hcdCfgCloseMask` uses, as the stream close already drains again after
   closing (`hcd_cfg.c`, its reusable abort and second drain) - but kept,
   with its ring, its stream block and its handle. An abort that fails ends
   as today (`hcdCfgFault`). A reattach later republishes the pipe, restores
   its streams' visibility, and releases only this transaction's pause.
2. **The new pipes are made**, rings from the pool (new). The URB's pipe
   entries are filled only at the end, on success (they receive candidate
   handles before the command today, `hcd_cfg.c` in
   `hcdCfgSelectInterface`; on any failure they are left zero). A pool that
   cannot supply them fails the request before any command: the new pipes
   are freed and the old reattached - republished and unpaused - nothing
   having changed (today's `EndpointRefusalsPool` count).
3. **The Configure Endpoint** drops the old endpoints (and any stale ones)
   and adds the new.
   - **Not issued or refused:** the controller changed nothing (4.6.6). The
     new pipes and their rings are freed, since the controller never saw
     them - as Linux's `xhci_reset_bandwidth` frees only the never-installed
     rings (`xhci.c:3176-3198`) - and the old pipes are reattached with
     their own handles, the interface at its old alternate setting. The
     request fails with the command's status.
   - **Unknown:** section 5.2's rule; the interface has no open pipe and
     the request fails.
   - **Success:** the old allocations are superseded (as Linux frees the
     dropped and replaced rings only after the command succeeds,
     `xhci_check_bandwidth`, `xhci.c:3135-3167`), and the new are owned.
     The old pipe objects are kept detached for step 4, holding their
     superseded rings uncharged; their stream blocks are freed now (a
     rollback re-adds an endpoint without streams, so a stream block is
     never reused). Then SET_INTERFACE goes to the device.
4. **SET_INTERFACE** has three outcomes, which the control path already
   separates (`hcd_enum.c`, `HcdThreadControlEx`: completed, stalled, or not
   established):
   - **Completed:** the old pipe objects and their rings are freed, the new
     pipes published and the URB's entries filled; success.
   - **Stalled, on an interface with one alternate setting** (owner,
     2026-10-14): the request is treated as a success, the way Linux treats
     it (`message.c:1655-1662`, its "manual set_interface"). A device whose
     interface "only supports a default setting" may STALL SET_INTERFACE
     (USB 2.0 9.4.10; USB 3.2 9.4.10, p.343), so the STALL refuses nothing:
     the setting asked for is the one setting there is, the one the device
     is at, and the new endpoints are the old setting's own. What the
     request would have done at the device - set "all of the status and
     configuration values" of the interface's endpoints to their defaults,
     "resetting the sequence numbers ... to zero" (USB 3.2 9.1.1.5, p.321,
     the section Linux's own comment cites at `message.c:1707`), and clear
     their Halt feature "even if the requested ... interface is the same as
     the current" one (9.4.5, p.338) - is done instead by
     CLEAR_FEATURE(ENDPOINT_HALT) to each new non-isochronous endpoint,
     which clears the halt and "always results in the data sequence being
     reinitialized to zero" (9.4.5, p.338; the toggle, USB 2.0 9.4.5), as Linux clears every endpoint of the setting
     after a manual set (`message.c:1690-1705`), with the mechanism
     `hcdCfgSetInterface` already uses for the function select. The host
     side needs nothing more: the contexts step 3 added are fresh, at DATA0
     or sequence 0, which is what Linux's own reset of the host toggles
     afterwards gives it (`usb_enable_interface` with its reset,
     `message.c:1707-1718`). If every CLEAR_FEATURE completes, the select
     ends as on **Completed**: the old pipe objects and their rings freed,
     the new pipes published, the URB's entries filled, success. If one
     fails - stalled or not established - the request takes the next
     branch, the STALL on an interface with other settings, from its
     rollback on: the device never left its setting, only its toggles are
     in doubt, and the rollback's own CLEAR_FEATUREs restart them for the
     old pipes; an old setting with streams open takes the "not
     established" branch from there, as below.

     Two differences from Linux, both narrower. Linux clears isochronous
     endpoints too (some devices are said to expect it, `message.c:1275-1278`);
     here they are skipped, as `hcdCfgSetInterface` skips them, because an
     isochronous endpoint keeps no toggle and the Halt feature "is required
     to be implemented for all interrupt and bulk endpoint types" only (USB
     3.2 9.4.5, p.338), so a device may refuse the clear there and fail a
     select that did nothing wrong. And Linux ignores what each clear
     returns and reports success whatever it is (`usb_clear_halt` leaves
     its own toggle alone on a failure, `message.c:1284-1286`); here a
     failed clear leaves the device's toggle out of step with the fresh
     context, so the select does not claim success over it (owner,
     2026-10-14).

     "One alternate setting" is read from the configuration the client
     selected (`dev->Selected`), as Linux reads `num_altsetting` from the
     interface's own: exactly one interface descriptor with that
     `bInterfaceNumber`, walked to `wTotalLength`. A descriptor walk that
     cannot finish counts as more than one, so a malformed configuration
     takes the STALL branch below rather than a success.
   - **Stalled, on an interface with more than one alternate setting:** the
     device refused the request and stays at its old
     setting, with its endpoints' data toggles or sequence numbers as they
     were. Only here is the old setting re-instated (owner, 2026-10-13, as
     Linux's `usb_set_interface` restores the old setting's endpoints when
     the request fails, `message.c:1663-1668`): a second Configure Endpoint drops the new endpoints and
     re-adds the old setting's on their own rings, each zeroed and given a
     fresh dequeue pointer with DCS 1 (zeroed TRBs carry cycle 0, xHCI
     4.3.6 footnote 10), the software ring state reset with it (enqueue,
     dequeue and cycle back to the base). The rollback's outcome decides
     ownership at once, before anything else is tried:
     - **Refused or not issued:** nothing changed; the new allocations stay
       owned and stale, the old pipe objects are freed (their rings were
       superseded at step 3), and the interface has no open pipe - today's
       outcome, with the rings held.
     - **Unknown:** both sets stay owned until the recovery's HCRST
       (section 5.2).
     - **Success:** the old allocations are owned again and the new
       superseded and freed. Fresh contexts start at DATA0 (sequence 0), so
       the device side is brought to match before anything reopens:
       CLEAR_FEATURE(ENDPOINT_HALT) to each re-added non-isochronous
       endpoint, which "always results in" the toggle (USB 2.0 9.4.5) or
       "the data sequence being reinitialized to zero" (USB 3.2 9.4.5,
       p.338) - the mechanism `hcdCfgSetInterface` already uses for its own
       STALL case; isochronous endpoints keep no toggle and are skipped. If
       every CLEAR_FEATURE completes, the old pipe objects are reattached
       with **their own handles**, so a client that keeps its old handles
       finds them valid; the request fails with the STALL status. If one
       fails, the old endpoints stay installed and owned but the interface
       is not reopened: the old pipe objects stay closed, their rings held
       and their DCIs marked stale, for the next successful Configure
       Endpoint or another proof to release.
   - **Not established** (no status stage seen, a transaction error, a
     timeout): the device may or may not have changed its setting, so
     nothing is re-instated on a guess. The new endpoints stay owned and
     stale, the old pipe objects are freed, and the interface has no open
     pipe until the client's next select or a reset settles it - today's
     outcome, with the rings held.

An old setting whose pipes had streams open is not re-instated even on a
STALL: its stream handles are a dependent lifetime a fresh, streamless
context cannot honour; it takes the "not established" branch for what it
holds. A STALL on
an interface with one alternate setting whose clears all complete needs no
re-instatement, so its streams do not matter there: the new pipes, which
have none, are published.

What the request answers is decided by how it ended, apart from what the
allocations became:

- **Success** after a completed SET_INTERFACE, or after a one-setting
  STALL whose clears all completed.
- **The command's own status** when the select ended before or at step
  3's Configure Endpoint - refused, not issued or unknown
  (`XhciPipeConfigureUsbdStatus`), or the pool's or the parameters'
  refusal before any command - as today.
- **The STALL status** (`USBD_STATUS_STALL_PID`, today's answer to a
  STALL) on every exit after the device STALLed SET_INTERFACE, whatever
  followed: a one-setting clear that failed, however it failed; the
  rollback refused, not issued or unknown; the rollback's own clears
  completing or failing; an old setting with streams open. The device's
  refusal is what the client is told; what became of the rings does not
  change it.
- **`INTERNAL_HC_ERROR`** when SET_INTERFACE was not established, as today.
- **`DEVICE_GONE`** in place of any failure above once the device is
  proven to have left its port, as every select's failure is answered
  today (`hcdCfgSelectAnswer`) - all but `BUFFER_TOO_SMALL`, the length
  probe, which that helper returns as it is.

### 5.4 The function select and every other path

`hcdCfgSelectFunction` sends SET_INTERFACE for several interfaces in turn,
recording each success (`dev->Alternate[n]`), so a failure can come after
some interfaces moved: restoring every old context would leave the moved
interfaces' device endpoints at the new setting. So the function select
re-instates nothing. Steps 1 to 3 apply as above (resumable abort, detach,
the owned and superseded states, the four command outcomes, the refused
command reattaching the old pipes); a failed SET_INTERFACE on any
interface leaves every new endpoint owned and stale, frees the old pipe
objects, and fails the request with no open pipe for the function -
today's outcome, with every ring held until its proof.

The same ownership binds every other path that frees a pipe: the select
configuration, the releases (`hcdCfgReleaseOne`), the deconfigure and the
hub's pipes (`hcdCfgPipeNew`, from `HcdCfgHubOpen`). The implementation
audits each caller of `hcdCfgPipeFree` and `hcdCfgCloseMask` for an
allocation returned before its proof.

Locking: publication, `Closed`, the pause counts and stream visibility
change under the controller lock, as `hcdCfgCloseMask` does today; the
waits (the abort's, the commands', the control transfers') run on the
thread outside it. A client's dispatch meanwhile finds a detached pipe
closed and is refused, as it is today between close and free.

### 5.5 The bound, and Panther Point

With section 5.2's rule every enabled non-EP0 endpoint context holds
exactly one charged pool ring until a proof supersedes it: hub status
pipes come from `hcdCfgPipeNew` like every other, and a stream endpoint is
one context holding its pipe's pool ring, its stream rings in its block
and not contexts. So the enabled contexts never exceed 32 EP0s (one per
enabled slot, `XHCI_MAX_SLOTS`) plus 32 pooled rings
(`XHCI_MAX_POOL_RINGS`): 64, Panther Point's limit in Linux
(`xhci-pci.c:350-353`). An empty pool refuses the endpoint, as today; the
controller is never asked for a 65th context. So `XHCI_EP_LIMIT_QUIRK`
needs no quirk of its own (owner, 2026-10-13). A host assertion proves the
invariant on the pure model: across select, every command outcome, every
SET_INTERFACE outcome, the rollback and every proof, the charged pool rings
plus the enabled slots never pass 64, and no ring is in the pool while it
is owned.

The cost: during an interface switch the old and the new rings are both
held until the switch settles, so pool exhaustion or the pool's fairness
admission (which keeps a ring free for each live device without one,
`XhciPoolAcquire`) can refuse the temporary replacement ring of a switch
that would have been served before - not the per-device cap, which a
transaction's reservation does not consume (section 5.2). That is the
bound working, and it is counted.

### 5.6 As built (2026-10-14)

Section 5 is built as written, with the owner's amendment of 2026-10-14
(section 5.3, step 4). Where each part lives:

- **The decisions are pure.** `src/xhci_own.c` / `xhci_own.h`:
  `XhciOwnSelectStep` is section 5.3's table - each phase (step 3's
  Configure Endpoint, SET_INTERFACE, the one-setting clears, the rollback,
  its clears) and each event (a command's four outcomes, SET_INTERFACE's
  three, the clears' two) to the actions taken on the old and new pipes
  and what is issued next; `XhciOwnDropStep` the drop-only command of a
  function's unconfigure and release; `XhciOwnCommandEvent` a completion
  code as an outcome; `XhciOwnSingleSetting` section 5.3's walk for one
  alternate setting. A step the phase does not wait for ends the select
  holding everything, as an unknown outcome does. Host suite `test_own`:
  the table cell by cell, then every path through a switch (540 at the
  time of writing: six starting shapes, every combination of the three
  flags, every event, each unknown outcome in both worlds - the
  controller applied it or did not) driven over a modelled controller and
  ring pool with `hcd_cfg.c`'s executor mirrored, checking after each step
  that no ring is free while a context may name it, that no two contexts
  name one ring, that every held ring has exactly one holder, that an
  enabled endpoint with no published pipe is marked stale, and that the
  enabled contexts never pass the rings held; and after each end that a
  later proof and the slot's going return every ring.
- **The reservation.** `XhciPoolAcquireReserved` (`xhci_mem.c`): the cap
  counts the rings held minus the switch's reservation, the fairness test
  every ring held; `XhciPoolAcquire` is it with no reservation. Host
  vectors: `test_membuf`, `test_pool_reserved`.
- **The executor.** `hcd_cfg.c`. A held allocation is a closed pipe on
  the device's `Retained` list (`HCD_PIPE.RetainNext`, `Pinned`), its ring
  still the device's in the pool and its DCI marked stale
  (`hcdCfgRetain`); a proof releases it (`hcdCfgRetainedReclaim`): a
  successful Configure Endpoint dropping its DCI (`hcdCfgCommand`, beside
  the stream blocks' reclaim), the deconfigure finding every endpoint
  Disabled or the slot not Configured, and RESET_PORT's Reset Device; the
  slot's going frees the pinned ones too (`HcdCfgDeviceGone`). A switch
  is `HCD_CFG_SWITCH`: `hcdCfgSwitchDetach` (step 1: the reusable abort,
  then unpublished, drained and waited out, kept),
  `hcdCfgSwitchReserved`, `hcdCfgSwitchConfigure` (step 3),
  `hcdCfgSwitchRollback`, `hcdCfgSwitchClears`, `hcdCfgSwitchApply` (the
  actions) and `hcdCfgSwitchRun` (the steps to the end). SELECT_INTERFACE
  (`hcdCfgSelectInterface`) and a function's select
  (`hcdCfgSelectFunction`, no rollback) run through them; a function's
  unconfigure and a removed function's release through `XhciOwnDropStep`.
- **The audit of section 5.4.** Every path that freed a pipe:
  `hcdCfgCloseMask` (and `hcdCfgCloseAll`) now runs only after a proof -
  the deconfigure's successful command or its all-Disabled reading, and
  the slot's going; the select configuration's refused command frees its
  new pipes (never installed) and an unknown one now pins them; the hub's
  status pipe likewise (`HcdCfgHubOpen`). No other caller of
  `hcdCfgPipeFree` returns a ring before its proof.

Taken at implementation, inside the record's rules:

- A function's unconfigure or release whose drop is refused leaves the
  old pipes held stale, not put back: the client asked for them closed,
  and that is today's outcome with the rings held.
- `hcdCfgSetInterface`, the function select's own one-setting fallback,
  reads one setting through `XhciOwnSingleSetting` on `dev->Selected` (the
  same bytes as the `dev->Config` it read before, for a split device), so
  a SELECT_INTERFACE and a function decide it by one rule, and its clears
  go through `hcdCfgDeviceClearHalt`, as the switch's do.
- The client's pipe entries are cleared before anything can fail and
  written only on success, so a failure leaves every entry naming no
  pipe.

What the matrix reads (`xhci_counters.h`, appended at word 233, after the
quirk words):
`PoolRingsInUse` (the pool rings held, copied at each poll from the
pool's own array), `RingsRetained`, `RingsPinned`, `RingsReleased`,
`SelectsReattached`, `SelectStallSuccesses`, `SelectRollbacks`,
`SelectRollbacksReopened`; `XHCISNAP` prints them as "endpoint rings". In
the log, every flavour: `cfg.ring.held` (slot, DCI, pinned, pool index),
`cfg.select.end` (slot, interface, the answer, the last event) for every
switch that did not end in a plain success, `cfg.select.rollback` (slot,
code). The qemu flavour's injection layer gained two faults for the legs
(`build-and-test.md`, `XhciQemuInject`): `18`, the next
SELECT_INTERFACE's SET_INTERFACE answered STALLed (`arg` 0) or not
established (1), never sent; `19`, the next switch's Configure Endpoint
answered refused (Resource Error) at step 3 (`arg` 0) or at the rollback
(1), or with no completion and the controller reset requested (2, 3).

## 6. What is common to every quirk taken

### 6.1 The registry values

**Owner, 2026-10-13:** one REG_DWORD per quirk, named for its effect in
the style of the values already there (`XhciIntelIrqRoute`,
`XhciIntelPortSwitch`, `XhciIntervalCap`, `XhciAvgTrbEsit`,
`XhciUsb2HubSpeedFallback`), with the Linux flag given beside each here and
in `XHCISNAP`; those five keep their names. The names, as amended on
review round 1 (owner):

| Value | Linux flag | Effect | Default | 2 offered |
|---|---|---|---|---|
| `XhciMsiAvoid` | `XHCI_BROKEN_MSI` | the line or polling, never a message (but at `XhciInterruptMode` 3) | 1 | yes |
| `XhciStreamsOff` | `XHCI_BROKEN_STREAMS` | the controller treated as having no streams | 1 | yes |
| `XhciResumeReinit` | `XHCI_RESET_ON_RESUME` | no save or restore; resume re-initializes | 1 | yes |
| `XhciIntelShutdownToEhci` | `XHCI_SPURIOUS_REBOOT` | connectors routed to EHCI at shutdown | 1 | no |
| `XhciShutdownReset` | `XHCI_SPURIOUS_WAKEUP` | HCRST after a proven halt at shutdown | 0 | yes |
| `XhciSuspendHaltDelay` | `XHCI_SUSPEND_DELAY` | about 1.5 ms before the suspend's halt | 1 | yes |
| `XhciSuspendHaltTimeout` | `XHCI_SLOW_SUSPEND` | the suspend's halt bound 640 ms | 1 | yes |
| `XhciAsmediaFlowControl` | `XHCI_ASMEDIA_MODIFY_FLOWCONTROL` | the vendor flow-control write after each reset and resume | 1 | no |
| `XhciTiIntervalCap` | `XHCI_LIMIT_ENDPOINT_INTERVAL_7` | High Speed and above, interrupt Interval 7 and above programmed as 6 | 1 | yes |
| `XhciStuckLinkReset` | `XHCI_MISSING_CAS` | a USB3 port stuck in Polling warm-reset after a restoring resume | 1 | yes |
| `XhciStreamRingPad` | `XHCI_TRB_OVERFETCH` | one ring of padding after a stream block | 1 | yes |
| `XhciControlLinkNoOp` | `XHCI_ETRON_HOST` (part) | a No Op so a SuperSpeed control TD's Setup and Data do not straddle the Link TRB | 1 | yes |
| `XhciIsochSpuriousSuccess` | `XHCI_ETRON_HOST` (part) | an isochronous Success after an error on the same TRB dropped | 1 | yes |
| `XhciAmdSuspendBitClear` | `XHCI_DISABLE_SPARSE` | bit 17 of BAR offset C12Ch cleared before suspend | 1 | no |
| `XhciIntervalCap` (existing) | `XHCI_LIMIT_ENDPOINT_INTERVAL_9` | 1002:7316 added to its value 1; no value of its own | 1 | its existing 2 |

Each is read at each start with `hcdReadDword` (and `XhciMsiAvoid` also at
AddDevice, section 4.1), in the house form of `XhciIntervalCap` and
`XhciIntelIrqRoute`:

| Value | Meaning |
|---|---|
| 0 | off: Linux's behaviour is not applied, on any controller |
| 1 | Linux's listed controllers |
| 2 | every controller, where the fix is generic xHCI behaviour (the "2 offered" column); for a vendor-register or vendor-specific quirk - `XhciIntelShutdownToEhci`, `XhciAsmediaFlowControl`, `XhciAmdSuspendBitClear` - a 2 is read as 1 and logged (owner, 2026-10-13) |
| absent, another type or any other number | the default: 1, except `XhciShutdownReset`, which is 0; the normalization is logged |

Absent is the default, so on by default for every value but one, unlike
`XhciIntelPortSwitch`: text-mode Setup's F6 install writes no values
(`txtsetup.oem`), and a quirk a listed controller needs there should apply
there. Both INFs write each value at its default with `0x00010003` under
task 34.1's rule, with a `VAL-*` row in the INF gate; the footprints,
`README.md` and the release notes name each with its Linux flag and its
controllers.

### 6.2 Matching

Each quirk's id list is a table in the pure core, matched against
`PciVendorDevice` (device << 16 | vendor), with a host test of the match
per quirk: every listed id matches, the neighbours and the other vendors'
same device numbers do not, and the vendor-wide row (Q3's every 1106)
matches every device of that vendor. No quirk taken here needs a
version match. (Linux keys four bits on HCIVERSION: `XHCI_LINK_TRB_QUIRK`
at 0.95 and `XHCI_AMD_0x96_HOST` at 0.96, not taken, section 3.3;
`XHCI_SPURIOUS_SUCCESS` above 0.96, covered on every controller, section
3.2; `XHCI_DEFAULT_PM_RUNTIME_ALLOW` at 1.20 and above, not applicable.)

### 6.3 Revision

No revision is matched (owner, 2026-10-13). Linux matches a revision
twice, both on 1B73:1000: revision 00 for a deprecated bit that does
nothing, and revision 04 for Q8, whose longer halt bound costs a healthy
controller nothing, so every revision is matched. The revision
(configuration byte 08h) is read at each start and logged (`hc.pci.rev`),
so a report shows it.

### 6.4 What `XHCISNAP` reports

Four things per value, kept apart because a user's dump has to tell them
apart: the value read (and its normalization: absent, another type, an
unknown number, a 2 read as 1); whether this controller's id matched; the
effective policy (on, off, or overridden, as Q1 at `XhciInterruptMode` 3);
and, for a quirk that acts, its outcome (done, skipped for a precondition
named, or failed). They are a log note per value at each start
(`quirk.<name>`, the four packed) and counter-block fields beside 35-T.8's
`CapMode` and `CapApplied` (`xhcisnap.c` prints those as the
`XhciIntervalCap` line), with counts where a quirk acts repeatedly: Q1's
refused messages, Q8's long halts, Q12's warm resets, Q13's padded blocks,
Q14a's No Ops, Q14c's dropped Successes, Q9's writes and give-ups, Q16's
skips. `XHCISNAP` prints a "Linux quirks" block naming each value, its
Linux flag and those four.

The shutdown's own actions (Q4's write, Q5's HCRST) are logged like the
rest, but the log ring lives in memory and the next boot cannot read it,
and a shutdown is not a PnP stop: its D3 path runs 37.3's flush of pending
requests and the switchover's release (`hcd_power.c`), not the stop's
`HcdLogFlush`. So after Q4 and Q5 have acted, and after the power gate is
left (the thread's pass takes the gate before it flushes, `hcd_ctl.c`, so
waiting for it inside the gate would deadlock), the D3 path takes a flush
ticket (a request counter it advances after its records are written),
wakes the controller thread (`HcdThreadWake`), and waits, bounded at
500 ms in all, for the thread to acknowledge that ticket, before the D3
IRP goes down. The thread captures the requested ticket **before** it
begins a flush, and after `HcdLogFlush` returns publishes the captured
ticket as acknowledged - so a flush already under way when the records
were written, which may have drained before them, never acknowledges
them. The sink stays the thread's, at PASSIVE_LEVEL, serialized as it is
now. A thread that has left its loop (`ThreadStop`) or does not reach its
flush within the bound is not waited for further; the attempt is counted as
unacknowledged, and nothing is claimed for it. An acknowledgement says the
request was serviced, not that records were emitted: `HcdLogFlush` returns
at once when the `XhciLogDebugView` sink is off (`hcd_log.c`), so the
thread acknowledges with the outcome - drained to the sink, or sink off -
and only the first is counted as a flushed shutdown record. Even a
flushed record reaches a user only through an external capture of that
shutdown - the `XhciLogDebugView` sink under a debug-output viewer or a
kernel debugger (`build-and-test.md`, "Getting a trace off a bare-metal
machine"); the release notes say so.

### 6.5 What `XHCIQUAL` reports

`xhciqual/quirks.c` keys rows on an exact vendor and device, with a
16-bit flags word of which bits 0 to 10 are spoken for. The quirks taken
need: a flag each, which widens the word to 32 bits; rows for ids it lacks
(104C:8241, 1002:7316, 1022:145C, 1B21:3042, 1DB7:DC27, 8086:22B5,
8086:A12F, 8086:5AA8, 8086:19D0, 1D17:9202, 1D17:9203, 17CD:0200, and Q18
and Q19's ids, named as not taken); a vendor-wide row form for VIA and
Zhaoxin, which it lacks; and each printed quirk naming the driver's
registry value and its Linux flag. It already reads the revision and
HCIVERSION, so it can say when a listed part is below the driver's floor
(1.0 today, 0.96 once task 38.11 lands).

### 6.6 Provenance

This record copies nothing: it cites Linux by file and line and states
what each quirk does in its own words, with the constants (offsets, bits,
ids) that are interface facts. The implementation may adapt Linux's code
for the quirks taken under `legal-provenance.md`'s exception for Linux's
xHCI quirks (owner, 2026-10-12, widened 2026-10-13), whose conditions are
the task's: only from a file whose SPDX identifier is GPL-2.0 (all
thirteen mirrored files are, section 2.1); every copyright, licence and
disclaimer notice of the source file kept beside the copy, and the copy
marked changed and dated; a row per copy in that section's table (file,
the pinned commit, lines, where it landed); the release's notices naming
the file and its copyright holders and offering the release's tag as its
source, checked at 38.final; and only the quirk and its handling taken.
Section 5's ring rule is this driver's own bookkeeping, written from the
specification; Linux is cited there for its order of operations, not
copied. Where a quirk is a register write or a test of an id, writing it
fresh from this record may be simpler than adapting, and then no row is
owed; the implementation says which, per quirk. As built, every quirk was
written fresh (section 13.6).

## 7. Legs

QEMU's controller (1B36:000D) is on no list, so a guest shows each value
read and "not this controller", value 2's reach where offered, and that
value 0 changes nothing; host tests carry the matching, the pure decisions
and section 5's invariant. Section 5's failure paths are driven on QEMU
where they can be staged - a SET_INTERFACE a device refuses (the
26-V.2 C-Media case is one), and a refused Configure Endpoint through the
qemu flavour's injection layer, which was taught both for it (section
5.6: faults `18` and `19`) - the leg for the fix: `usb-audio`'s
streaming interface switched between alternates 0 and 1 many times with
`PoolRingsInUse` back where it started after each; `18` with `arg` 0 on
that switch (two settings: the rollback, `SelectRollbacks` and
`SelectRollbacksReopened` up, the audio device still playing after its
class driver selects again); `19` with `arg` 0 (the old pipes put back,
`SelectsReattached` up) and 1 after an `18` (`RingsRetained` up, then
`RingsReleased` at the next select); `19` with `arg` 2 (the recovery,
`RingsPinned` up, every device back and `PoolRingsInUse` as before). Every value is documented with the controllers it was
read on, and for nearly all of them that is none: the project holds 9D2F
(E460, Q12), 1E31 (B490, Q4, and section 5's bound) and 9CB1 (E7250, Q4
and, at 1, Q5), and a field user holds a VL805 (Q3, Q13). Each such machine
read under the taken quirk is a leg; the others ship unread, and the
release notes say so.

## 8. Readings this record rests on

- The 0.96 rows: task 38.11's design is record 17 section 4.14 (on its own
  branch at the time of writing); this record assumes only what the task
  text says - the floor moves to 0.96 behind a default-on value and 0.96
  parts are driven as 1.0 but for the TD Size.
- `usb_set_interface`'s restore on a failed request and its "manual
  set_interface" on a STALL at an interface with one alternate setting
  (section 5.3, step 4): from `drivers/usb/core/message.c`, added to the
  mirror at the pinned commit on 2026-10-14 (`external/README.md`) and read
  there the same day, `message.c:1587-1725` and `usb_clear_halt` at
  `1267-1300`.

## 9. Decisions (owner, 2026-10-13)

From revision 0's open list, each with its recommendation then:

- **Form.** One REG_DWORD per quirk; 0 off, 1 Linux's ids, 2 every
  controller where sensible. (Recommended: the same.)
- **Naming.** Named for the effect in the style of the existing values,
  not `XhciQuirk<LinuxName>`; the Linux flag beside each in this record and
  in `XHCISNAP`; the existing five not renamed. (Recommended: Linux-derived
  names; not taken.) Amended on review round 1: `XhciIntelShutdownToEhci`,
  `XhciSuspendHaltDelay`, `XhciSuspendHaltTimeout`,
  `XhciAsmediaFlowControl`, the Etron controls split, and the AMD bit named
  without implying its meaning.
- **Revision.** No revision matching; the revision logged. (Recommended:
  the same.)
- **HCIVERSION floor.** Unchanged in this task. (Recommended: the same.)
  Later the same day: the floor moves to 0.96 in task 38.11, and the
  0.96-era quirks - NEC's isochronous chaining, the AMD 0x96 rules, the
  0.95 link-TRB quirk - are not taken (section 3.3).
- **CSM.** A platform without a CSM cannot run a supported system, so its
  quirks are not applicable; on review round 1, a platform whose CSM is not
  established is deferred pending platform evidence, not "not applicable"
  (section 3.0).
- **Value 2** (later the same day): offered only where the fix is generic
  xHCI behaviour - Q1, Q2, Q3, Q5, Q7, Q8, Q10, Q12, Q13, Q14a and Q14c;
  the vendor-register or vendor-specific ones - Q4, Q9, Q16 - take 0 or 1,
  a 2 read as 1 and logged; Q11 has no value of its own (section 6.1).
- **Numbering** (later the same day): the task is 38.10, renumbered from
  38.11.
- **Q1** taken, on, with the usual fallback to polling where
  `XhciInterruptMode` allows it (section 4.1).
- **Q2** taken, on, 2 offered. **Q3** taken, on, skipping save and
  restore. **Q4** taken, on, even at `XhciIntelPortSwitch` 0 (record 16's
  exception). **Q5** taken, **off** by default (recommended on). **Q6** not
  applicable (no CSM, by ruling). **Q7, Q8, Q16** taken, on. **Q9** taken,
  on. **Q10** taken, interrupt endpoints only, on. **Q11** folded into
  `XhciIntervalCap`. **Q12** taken, on, the Polling half. **Q13** taken,
  gated, on. **Q14** (a) and (c) taken, on; (b) - first a refused
  `RESET_PORT` - **left as it is** on review round 1 (no refusal, no slot
  rebuild). **Q15** taken only if xHCI 1.2c permits the OS to clear BIOS
  Owned - it does not, so dropped. **Q17** deferred pending platform
  evidence (round 1; it was "not applicable"). **Q18, Q19** not taken, each
  a task of its own if a report needs it. **Q20** not taken as such; its id
  added to `XhciStreamsOff` (round 1).
- **The endpoint rings** (round 1): fixed in 38.10 the Linux way (section
  5), so Panther Point's 64-endpoint limit is bounded by the pool with no
  quirk of its own. The owner's instruction to re-instate the old
  alternate setting after a failed SET_INTERFACE is carried out where it is
  known to be safe (review round 2): on a SET_INTERFACE the device STALLed,
  with the device's toggles restarted to match; not on an outcome that was
  not established, nor in the function select, where some interfaces may
  already have moved, nor for a setting with streams open (section 5.3,
  5.4). Those keep today's outcome with every ring held.

Later (owner, 2026-10-14):

- **A STALL on an interface with one alternate setting** is a success, as
  Linux's "manual set_interface" makes it: CLEAR_FEATURE(ENDPOINT_HALT) to
  each new non-isochronous endpoint, then the new pipes published as on a
  completed SET_INTERFACE; a CLEAR_FEATURE that fails takes the STALL
  branch, re-instating the old setting (section 5.3, step 4). A STALL on an
  interface with more than one keeps that branch unchanged. Linux's
  handling after its manual set was read and its intent kept - the device's
  toggles restarted to match the host's - with the two narrowings section
  5.3 states.

## 10. What changes in the documents

At implementation:

- `src/xhci98.inf` and `src/xhci98-amd64.inf`: a line per value; the INF
  gate's `VAL-*` table; the footprints.
- `README.md` and `docs/using/release-notes.md`: each value, its Linux
  flag, its controllers, its default, and which were read on hardware; Q18's
  known limitation; that Q4 and Q5's shutdown actions are visible only in
  an external capture.
- `docs/contributing/implementation-invariants.md`, "PnP Resources": Q4's
  shutdown write and Q9's vendor write as configuration writes above the
  header; and section 5's ring rule as an invariant of its own.
- Design record 13 (its endpoint and SELECT_INTERFACE sections) pointed at
  section 5 here; design record 15 (Q1's input to `XhciPollDecide` and the
  new reason); design record 16 section 5 (Q4's exception to value 0);
  design record 17 section 4.7 (Q11's id, and Q10 beside the cap); design
  record 18 section 4.2 (Q1's filter input and its AddDevice cache).
- `src/xhci_init.c`, `xhciBiosHandoff`'s comment on bit 16 (section 3.3,
  Q15).
- `xhciqual/quirks.c`, `qual.h` and `report.c`: section 6.5.
- `xhcisnap/xhcisnap.c` and `xhcisnap/README.md`: section 6.4.
- `legal-provenance.md` section on the mirrors: a row per copy, if any.
  As built there is no copy (section 13.6), so no row was added.
- `external/README.md`: `include/linux/pci_ids.h` and
  `drivers/usb/core/message.c` added to the Linux mirror at the pinned
  commit, so section 2.1's **(h)** values and section 5.3's citation rest
  on it (changes no pin).

## 11. Review round 1, and where each finding is answered

Round 1 ran on revision 1 (`aee03882` after rebasing).

| Finding | Answer |
|---|---|
| P1-1: the 64-endpoint "covered" claim false; rings returned while endpoints may be enabled | section 5 (owner: fixed in 38.10 the Linux way); section 3.2's row rewritten |
| P1-2: a refused `RESET_PORT` does not escalate | Q14's (b) left as it is (owner); section 4.13 |
| P2-3: Q9's hook at DISPATCH_LEVEL and missing the restore; its cost | section 4.8 |
| P2-4: Q1 needs early identity and a transport-decision input | section 4.1 |
| P2-5: the CSM exclusions | section 3.0 (owner: deferred pending platform evidence); Q17 in section 3.3 |
| P2-6: Q5 needs a proven halt | section 4.5 |
| P2-7: Q16's resource bound | section 4.15 |
| P2-8: the Sony exemption, the EHCI test, the 184E and bcm2711 exemptions | sections 2.2, 3.2 (switchover row), 3.3 (early-handoff row), 4.4 |
| P3-9: LPM flags apart; NEC applicability; Q14c's duplicate; the CSM "yes" row | section 3.3 (LPM rows, NEC row), section 4.14, section 3.0 (read and not-read rows) |
| P3-10: the diagnostic states; the shutdown result across a boot | section 6.4 |
| Names (Part B) | section 6.1 (owner took them) |
| Q20 (Part B) | section 3.3 (owner: the id joins `XhciStreamsOff`) |

Round 2 ran on revision 3 (`7c824cfc`).

| Finding | Answer |
|---|---|
| P1-1: an unknown Configure Endpoint outcome treated as a refusal | section 5.2 (four outcomes; unknown keeps every allocation owned until the recovery's HCRST) |
| P1-2: reattaching cancelled old pipes could restart abandoned TDs | section 5.3, step 1 (the reusable abort, `hcdCfgAbortPaused`, before detaching) |
| P1-3: the rollback did not establish device and controller agreement | section 5.3, step 4 (re-instated only on a STALL, with CLEAR_FEATURE(ENDPOINT_HALT) restarting the device's toggles; not on an outcome not established; not with streams open); section 5.4 (no rollback in the function select) |
| P2-4: the failed-select handle contract | section 5.3 (old pipe objects kept and reattached with their own handles; the URB's pipe entries filled only on success) |
| P2-5: "proven Disabled" is the wrong predicate for a replacement | section 5.2 (ownership per allocation - new, owned, superseded - applied to the named allocations before publication) |
| P2-6: Q1's identity read after delivery is settled; AddDevice before the attach | section 4.1 |
| P2-7: Q5's live-mapping guard | section 4.5 |
| P2-8: the shutdown's records not flushed | section 6.4 (a bounded thread flush after the gate is left) |
| P3-9: the CSM rule against Q7 and Q16 | section 3.0 |
| P3-10: the HCIVERSION-keyed bits | section 6.2 |

Round 3 ran on revision 4 (`c7b08912`); it agreed with the STALL-only
rollback, subject to its findings 1 and 2.

| Finding | Answer |
|---|---|
| P1-1: a CLEAR_FEATURE failure after a successful rollback freed installed rings | section 5.3, step 4 (ownership decided by the rollback's outcome at once; a later CLEAR_FEATURE failure leaves the old endpoints owned, closed and stale) and section 5.2 (ownership changes on a command's completion only) |
| P1-2: requests admitted between the abort and the detach | section 5.3, step 1 (unpublish, then `HcdIoDrainPipe` and `HcdIoWaitPipe`; reattach releases only the transaction's pause) |
| P2-3: the per-device cap of four refuses ordinary switches | section 5.2 (a transaction's reservation counted apart from the cap, still reserved in the pool) and section 5.5 |
| P2-4: a flush generation does not acknowledge the shutdown records | section 6.4 (a ticket the thread captures before it flushes) |
| P2-5: Q1 loses an established identity on a failed refresh | section 4.1 (the identity kept; none ever established is treated conservatively) |
| P3-6: "new" defined by completion; the USB 3.2 citation | section 5.2; section 5.3, step 4 |

Round 4 ran on revision 5 (`8046e1c7`) and found no P1.

| Finding | Answer |
|---|---|
| P2-1: stale allocations from an earlier failure not in the next transaction's reservation; one `held` count for cap and fairness | section 5.2 (the reservation is the command's full drop set, stale included, fixed before allocation; the cap count and the physical count kept apart; how a failed transaction settles) |
| P2-2: Q16 without the live-mapping guard | section 4.15 |
| P3-3: the cost is fairness refusal too | section 5.5 |
| P3-4: an acknowledged flush is not an emitted record | section 6.4 |
| P3-5: the survey row's old predicate | section 3.2 (`XHCI_EP_LIMIT_QUIRK` row) |

The owner's amendment of 2026-10-14 (revision 7, section 5.3's
one-setting STALL) was reviewed on its own before any code, in two
rounds; round 2 found no P1 or P2.

| Finding | Answer |
|---|---|
| Round 1 P2: the request's status on each exit after a failed clear not stated | section 5.3, "What the request answers" |
| Round 1 P3: the comment above `hcdCfgSelectInterface` still said every failure leaves no pipe | the comment rewritten for the switch as a whole |
| Round 1 P3: the same-setting sequence reset cited to the Halt sentence | USB 3.2 9.1.1.5 cited beside 9.4.5 |
| Round 2 P3: `BUFFER_TOO_SMALL` is not turned into `DEVICE_GONE` | section 5.3, the `DEVICE_GONE` row |
| Round 2 P3: the comment's streams exclusion read as covering a one-setting success | the comment limits it to the rollback |

## 12. Provenance of this record

Linux facts: read from source at the pinned commit, `file:line` above, and
the **(h)** constants from `docs/references/pci_ids.h`; no binary was read,
so no static or runtime tag applies and no `legal-provenance.md` row is
owed. xHCI facts: xHCI 1.2c as `docs/references/README.md` pins it, by
page. Driver facts: read from `src` on `phase-38`, by file and function.
Project machines and their ids: `docs/contributing/runs/run-35.md` (E460
9D2F, P14s Gen 1 02ED, B490 1E31, the AM5 CSM board) and `run-36.md` (E7250
9CB1); the VL805: issue 13. The standing column of section 3.0: the owner's
rulings and the readings named in it, nothing else.

## 13. As built (code, 2026-10-14)

Sections 3, 4 and 6, built for task 38.10. Section 5's endpoint-ring
ownership is a separate change and is not described here. Where the code
differs from, or settles, what the sections above say, the code is the
authority and this section says so.

### 13.1 Where it lives

- **The pure half**, `src\xhci_quirk.c` and `src\xhci_quirk.h`: the
  fourteen values in section 6.1's order (`XHCI_QK_MSI_AVOID` to
  `XHCI_QK_AMD_BIT`), each with its value name, Linux flag and log label;
  the defaults and whether a 2 is offered (`XhciQuirkDefault`,
  `XhciQuirkTwoOffered`); the normalization (`XhciQuirkMode`); section
  3.1's id lists and the match (`XhciQuirkMatches`, with VIA vendor-wide
  for `XhciResumeReinit` alone); the effective policy
  (`XhciQuirkEffective`, `XhciQuirkMatchAll`); the state a start fills
  (`XHCI_QUIRK_STATE`, the extension's `Quirk`, zeroed with it at each start
  and kept across a resume's or a recovery's reinitialization); the packed
  word (`XhciQuirkPacked`); Q1's decision (`XhciQuirkMsiAvoidDecide`); and
  Q2's effective HCCPARAMS1 (`XhciQuirkStreamsHcc1`). Host suite
  `test\test_quirk.c`; each executor's vectors are in `test_ring`,
  `test_iso`, `test_link`, `test_stream`, `test_tol` and `test_poll`.
- **The FDO's half**, `src\hcd_quirk.c`: the registry reads, the identity
  Q1 needs before the delivery is settled, and the quirks that act from the
  FDO's own paths (Q4, Q5, Q9, Q16, Q12's arming and the shutdown's log
  flush).

### 13.2 The values, the match and the revision

Fourteen REG_DWORDs in the controller's driver key, read through
`HcdReadValueDword` at each start (`HcdQuirkStart`, after
`XhciInitController`); `XhciMsiAvoid` is read earlier in the same start, in
`hcdSettleDelivery`, and on NT 6 also at AddDevice after the attach
(`HcdQuirkAddDevice`), for the requirements filter. Absent, another type,
or a number other than 0, 1 and 2 is the default: 1, except
`XhciShutdownReset`, which is 0. A 2 is every controller, except on the three
vendor-register quirks - `XhciIntelShutdownToEhci`,
`XhciAsmediaFlowControl`, `XhciAmdSuspendBitClear` - where it is read as 1.
Each normalization is recorded in the value's packed word. Both INFs write
each value at its default with `0x00010003`, and the INF gate's `VAL-*`
rows and the footprints carry them. `XhciIntervalCap` has no new value: its
1 now covers 1002:7316 (ATI Navi10) beside every 1022 controller
(`xhci_tol.c`, `XhciTolCapApplies`).

Matching is by the PCI vendor and device dword alone
(`PciVendorDevice`, the start's own read; Q1's identity, section 13.4).
The revision (configuration byte 08h) is read at each start and logged as
`hc.pci.rev` and in counter word 213, and is never matched (section 6.3).

### 13.3 Where each quirk acts

| Quirk | Value | Where (file, function) |
|---|---|---|
| Q1 | `XhciMsiAvoid` | `hcd_quirk.c` `HcdQuirkMsiAvoid`, called by `hcd_ctl.c` `hcdSettleDelivery` before `XhciPollDecideEx` (`xhci_poll.c`), which under mode 0 answers a granted message with polling and the new reason 19, `XHCI_POLL_R_MSI_AVOIDED`; the filter's side is `hcd_msi86.c` `HcdMsiFilter`, which asks for the line as mode 1 does when `HcdQuirkFilterAvoid` says so; the count is `HcdQuirkMsiRefused` |
| Q2 | `XhciStreamsOff` | `XhciQuirkStreamsHcc1` at both consumers of MaxPSASize: `hcd_pdo.c` `hcdXportFlags` (the transport decision) and `hcd_cfg.c` `hcdCfgStreamsOpen` |
| Q3 | `XhciResumeReinit` | `xhci_init.c` `XhciSuspendController`: no save, `SavedStateValid` 0, counted (`SavesSkipped`), so the resume takes the reinitialization |
| Q4 | `XhciIntelShutdownToEhci` | `hcd_quirk.c` `HcdQuirkShutdown`, from both shutdown paths of `hcd_power.c` (`HcdControllerPower` and `hcdDirectTransitionGated`) after `hcdShutFlush` and `HcdPswRelease`, under the power gate: `USB3_PSSEN` (D8h) and then `XUSB2PR` (D0h) written 0 through `HcdSvcConfigSpace` and each read back |
| Q5 | `XhciShutdownReset` | the same function, after Q4, through `xhci_init.c` `XhciShutdownReset` (the proven-halt test, then `xhciReset`, then `SavedStateValid` 0) |
| Q7 | `XhciSuspendHaltDelay` | `xhci_init.c` `XhciSuspendController`, before the quiesce |
| Q8 | `XhciSuspendHaltTimeout` | `xhci_init.c` `xhciQuiesceBounded`, given the long bound by `XhciSuspendController` alone; `XhciQuiesceController` (the start, the recovery, the stop) keeps 32 ms |
| Q9 | `XhciAsmediaFlowControl` | `hcd_quirk.c` `HcdQuirkAsmFlow`, called from `HcdQuirkStart` (the start, after `XhciInitController` and before the enumeration), from `hcd_ctl.c` `hcdRecover` (the IRQL lowered, before `HcdPollGrant`), and from `HcdQuirkResumed` at the end of every successful resume, either branch (`hcd_power.c` `hcdResume`) |
| Q10 | `XhciTiIntervalCap` | `xhci_tol.c` `XhciTolTiCapInterval`, applied in `hcd_cfg.c` `hcdCfgTolerance` after the AMD cap |
| Q11 | `XhciIntervalCap` | `xhci_tol.c` `XhciTolCapApplies` |
| Q12 | `XhciStuckLinkReset` | armed by `hcd_quirk.c` `HcdQuirkResumed`; decided in `xhci_link.c` `XhciLinkPortFeed` with `XhciLinkStuckPolling`; counted and logged in `hcd_enum.c` `hcdPortChanged` |
| Q13 | `XhciStreamRingPad` | `xhci_stream.c` `XhciStreamLayoutPad`, `XhciStreamBigBytesPad` and `XhciStreamLayoutPlacedPad`, from `hcd_cfg.c` `hcdCfgStreamsBuild`; the free takes the layout's `TotalBytes` |
| Q14a | `XhciControlLinkNoOp` | `xhci_ring.c` `XhciRingControlLinkPad`, through `xhci_xfer.c` `XhciXferSubmitControlEx` from `hcd_io.c` `HcdIoMapped` and `hcd_enum.c` `hcdThreadControlQuiet`; the input is `HcdQuirkLinkNoOp` |
| Q14c | `XhciIsochSpuriousSuccess` | `xhci_xfer.c` `XhciXferIsoEvent`, its scope set before each isochronous event in `hcd_dev.c` `hcdIsoEvent` |
| Q16 | `XhciAmdSuspendBitClear` | `hcd_quirk.c` `HcdQuirkSuspend`, from `hcd_power.c` `hcdSuspendSettled` before `XhciSuspendController` |

### 13.4 Where the code settles or departs from the text

- **Q1.** The start reads the identity through the configuration service
  before the delivery is settled, keeps an identity already established
  for the FDO when that read fails, and with none ever established avoids
  the message at value 1 (section 4.1). The requirements filter is the
  other way round: with no identity ever read it leaves the list as it is,
  and the start's decision protects the controller. A failed identity read
  at AddDevice is not logged there (the log does not exist yet); the start
  records "identity unread" or "identity kept" as Q1's reason in its
  packed word, and `quirk.msiavoid.idmismatch` when `XhciInitController`'s
  read differs from the identity Q1 used. A strip the filter made at the
  quirk's asking is kept on the FDO (`MsiAvoidFilterStrips`) and counted,
  with the outcome done, by the next start, since that start is then given
  the line and its own decision has nothing to refuse (review round 2).
- **Q7.** The pause is a 1.5 ms `KeStallExecutionProcessor` stall, bounded
  and exact, not the requested PASSIVE_LEVEL delay of about 1.5 ms that
  section 4.6 describes.
- **Q8.** The suspend's halt waits the usual 32 ms first; on a controller
  where the quirk applies, and with USBSTS not reading all ones, it then
  waits up to 608 ms more, 640 ms in all, ending as soon as HCH reads 1.
  `HaltsLong` counts the suspend halts that had not completed within the
  first 32 ms, whether or not they then completed; the extended wait's end
  is the outcome (done, or failed with HCH not set).
- **Q10.** `TiCapped` counts interrupt endpoints capped by it once the
  Configure Endpoint that opens them succeeds, beside the AMD cap's
  `CapIntervals` (review round 2): `hcdCfgTolerance` marks the endpoint
  (`Capped` bit 1) and `hcdCfgQuirkCapped` counts it.
- **Q14c.** The remembered event is the TRB pointer with its reserved low
  bits dropped, as the engine reads it, and is forgotten when that TRB is
  re-let to a new TD or the queue restarts, so a later request's Success at
  the same place is never taken for the old error's (review rounds 1 and
  2).
- **Q4.** The two registers are written independently; the first failure
  is the one recorded (review round 1).
- **Q12.** After a resume that restored (`Quirk.Restores`, counted by
  `XhciResumeController` on a restore), each USB3 root port's next reading
  is armed (`Link.StuckCheck`) and disarmed after that one reading,
  whatever it shows; a port held by 29-A.5 is disarmed without being
  touched (`hcdPortChanged`). A port read powered, not in reset, with CCS
  and CAS clear and PLS Polling is warm-reset through 29-A.2's
  `xhciLinkWarm`, within its budget, and logged as `quirk.stucklink.port`.
  A reinitializing resume arms nothing.
- **Q14a.** The No Op is placed only when the enqueue pointer rests on the
  last TRB before the Link TRB, and the room check counts it with the whole
  control group, so nothing is written unless both fit (the submission is
  then busy). At value 2 it still applies only to control transfers of a
  SuperSpeed or faster device: the speed condition is Linux's and is kept
  at every value.
- **Q4, Q5 and the shutdown's flush.** Both run in the shutdown's D3 only,
  after 37.3's flush and the switchover's release, on a started lifetime
  with its BAR mapped, in D0, on a window that decodes; a skip or a failure
  is the quirk's outcome, and the shutdown goes on. When either was on,
  the D3 path leaves the power gate and runs `HcdQuirkShutdownFlush`
  (section 6.4): it advances the ticket, wakes the thread and waits in 10 ms
  steps, bounded at 500 ms, for the thread to acknowledge it
  (`HcdQuirkFlushAck`, after the `HcdLogFlush` it began with the ticket
  captured); the thread acknowledges with whether that flush emitted any
  bytes to the sink (`Log.DebugViewBytes` moved: review round 1), so the
  outcome is counted as flushed, acknowledged with nothing emitted (the
  sink off, or nothing recorded), or not acknowledged.
- **Q9 and Q16** count every give-up and every skip (`AsmGiveUps`,
  `AmdBitSkips`); the packed word keeps the last outcome and its reason.

### 13.5 What `XHCISNAP` reads

At each start the log carries a note per value, `quirk.msiavoid`,
`quirk.streamsoff`, `quirk.resumereinit`, `quirk.shuttoehci`,
`quirk.shutreset`, `quirk.haltdelay`, `quirk.halttimeout`,
`quirk.asmflow`, `quirk.ticap`, `quirk.stucklink`, `quirk.ringpad`,
`quirk.linknoop`, `quirk.isochsuccess` and `quirk.amdbit`, each the packed
word of `xhci_quirk.h` (bits 1:0 the mode, 4:2 the normalization, bit 5
matched, bit 6 effective, bit 7 overridden, 11:8 the last outcome, 15:12
its reason, 31:16 the value as read, FFFFh when none was); and
`quirk.identity`, `hc.pci.rev` and, where they apply,
`quirk.msiavoid.idmismatch` and `quirk.stucklink.port`. Of the quirks that
act, Q4, Q5, Q9 and Q16 log their word again at each action (and Q12 its
port); Q1, Q3, Q7, Q8, Q10, Q13 and Q14 update their outcome and counts
only, which the counter block and `XHCISNAP` carry, so their start note
keeps the start's reading. The counter block gains words 199 to 232
(`src\xhci_counters.h`, after tasks 38.11 and 38.3; section 5's eight
follow at 233 to 240, 241 words in all):
the fourteen packed words (199 to 212), the revision (213), the identity
matched (214), and the counts (215 to 232). `XHCISNAP` prints them as a "Linux quirks" block and names
`XhciInterruptMode`'s reason 19, "automatic: a message granted, refused by
XhciMsiAvoid (Linux's XHCI_BROKEN_MSI), so polled" (`xhcisnap\README.md`). `XHCIQUAL`'s side is
section 6.5, with its host suite `xhciqual\test\test_quirks.c`.

### 13.6 Provenance

Every quirk is written fresh from this record: the ids, offsets, bits and
the ASMedia sequence's constants are interface facts, each cited at its
Linux line in sections 3 and 4 and in the code's comments, and no Linux
code is copied into `src`. `legal-provenance.md`'s exception for Linux's
xHCI quirks was not used: section 10's "a row per copy, if any" found no
copy, and that file gained no row. 38.final's check of the release
notices reads this section.

### 13.7 Read on hardware

None. Every value ships unread on the controllers it lists. The project
holds three listed controllers: the E460's 9D2F (Q12), the B490's 1E31 (Q4,
and Q18's limitation) and the E7250's 9CB1 (Q4; Q5 lists it but is off by
default); a field user holds a VL805 (Q3, Q13). QEMU's controller,
1B36:000D, is on no list, so a guest shows each value read and "not this
controller", value 2's reach, and that value 0 changes nothing (section 7).
