# Linux's xHCI controller quirks

Design record for roadmap-hcd task 38.10: the survey of every quirk
Linux's xHCI host driver applies by controller identity, read against this
driver's code, and the quirks taken from it, each behind a registry value.
Revision 1, 2026-10-13: the owner's decisions of the same day folded in
(section 7) - which quirks are taken, each default, the naming of the
values and a scope rule for platforms without a CSM (section 3.0).
Revision 0 (first committed as `c06eda96`, rebased since) was the survey
alone, with the decisions open. Revision 2, the same day, folds in three
further owner answers: where value 2 is offered (section 5.1), the task's
renumbering from 38.11 to 38.10 (commits before it use 38.11), and the
HCIVERSION floor, which moves to 0.96 in task 38.11 while the 0.96-era
quirks stay untaken (section 3.3). Review round 1 ran on revision 1; its
findings are not yet answered here. Nothing here is built yet; the record
is reviewed to convergence before any code is written.

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
taken or considered; section 5 is what is common to them; section 7 is the
owner's decisions.

What it is not:

- **Not the code.** Section 4 says where in this driver each quirk acts;
  the implementation follows review.
- **Not a copy.** The survey is facts with `file:line` citations at the
  pinned commit. No Linux code is copied into this record. The
  implementation may adapt Linux's code under the conditions of
  `legal-provenance.md`'s exception for Linux's xHCI quirks (section 5.6).
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

What the mirror does not carry, and what follows from it:

- `include/linux/pci_ids.h`. The vendor constants `xhci-pci.c` uses but
  does not define (`PCI_VENDOR_ID_NEC`, `_AMD`, `_ATI`, `_INTEL`,
  `_RENESAS`, `_VIA`, `_ASMEDIA`, `_TI`, `_BROADCOM`, `_CAVIUM`,
  `_ZHAOXIN`, `_CDNS`) and two device constants
  (`PCI_DEVICE_ID_INTEL_PANTHERPOINT_XHCI`, `PCI_DEVICE_ID_CDNS_USBSSP`)
  are not in it. The tables mark those values **(h)**. Each was read in
  the reference copy `docs/references/pci_ids.h` (Linux master as fetched
  2026-10-08, SHA-256 `020fac00...4487c`, `docs/references/README.md`):
  NEC 1033, AMD 1022, ATI 1002, TI 104C, VIA 1106, Broadcom 14E4, Cavium
  177D, Cadence 17CD and its USBSSP 0200, Renesas 1912, ASMedia 1B21,
  Zhaoxin 1D17, Intel 8086 and Panther Point 1E31. That copy is not the
  pinned commit; section 8 proposes adding the file to the mirror at it,
  so the values rest on the same tree as the rest.
- The platform glue (`xhci-plat.c`, `xhci-mtk.c`, `xhci-rcar.c`,
  `xhci-tegra.c`, `xhci-histb.c`, the DWC3 glue and the rest), and
  `xhci-pci-prom21.c`. What those set is listed by bit name only, from
  `xhci.h`, and set aside.
- The Raspberry Pi kernel's VL805 quirks, which issue 13 section 5 names
  (`XHCI_EP_CTX_BROKEN_DCS` is a mainline bit; the others are not
  mainline). Out of scope: not in the pinned tree.

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
- Behaviour keyed on PCI id outside the bit list: the forced BIOS
  handoff (`pci-quirks.c:1190-1196`), the Intel port switchover
  (`pci-quirks.c:1058-1147`, called at `pci-quirks.c:1224-1225`,
  `xhci-pci.c:878-879` and `xhci.c:753-754`), the Renesas firmware load
  (`xhci-pci-renesas.c`, matched by `xhci-pci.c:692-710`), and the
  Promontory 21 hand-off to a glue driver the mirror lacks
  (`xhci-pci.c:699-714`).

## 3. The survey

### 3.0 The scope rule: a platform with no CSM

**Owner, 2026-10-13:** a platform whose firmware has no Compatibility
Support Module (no legacy BIOS boot) cannot run any operating system this
driver supports, so a quirk for a controller found only on such platforms
is not applicable. The test is applied to every id below. A controller on a
PCIe card or a secondary on-board chip is not a platform: whether its host
has a CSM is the host's, so those ids stay applicable. Where the CSM of an
id's platforms is not established, the id stays in its quirk's match,
since matching a controller that never runs costs nothing; a quirk is
judged not applicable only where every id it names fails the test.

| Ids | Platform | CSM | Evidence |
|---|---|---|---|
| 1B73:*, 1B6F:*, 1106:3432, 1106:3483, 1B21:*, 1912:*, 1033:*, 104C:8241, 17CD:0200 | add-in cards and secondary on-board controllers | the host's | not a platform; stays applicable |
| 1002:7316 | the USB-C controller of an ATI Navi10 graphics card | the host's | an add-in card; stays applicable |
| 8086:1E31, 8C31, 9C31, 8CB1, 9CB1, 8D31 | Intel 7-, 8- and 9-series and C610 PCH | yes | the project's B490 (1E31) and E7250 (9CB1) run the supported systems (`runs/run-35.md`, `runs/run-36.md`) |
| 8086:9D2F | Sunrise Point-LP (100-series mobile) | yes | the project's E460 runs them (`runs/run-35.md`) |
| 8086:A12F | Sunrise Point-H (100-series desktop) | not established | no machine of the family read here |
| 8086:22B5 | Cherryview: Cherry Trail and Braswell SoCs | Cherry Trail: no (owner's ruling); Braswell: not established | Linux names the id `CHERRYVIEW_XHCI` (`xhci-pci.c:46`); whether both SoC lines share it is not read here |
| 8086:5AA8 | Apollo Lake | not established | no machine read here |
| 8086:19D0 | Denverton (server Atom) | not established | no machine read here |
| 8086:A0ED, 51ED, 54ED | Tiger Lake PCH, Alder Lake PCH and Alder Lake-N | no (owner's ruling) | Intel stated in 2017 a plan to require UEFI Class 3, which has no CSM, by 2020 (reported at the time, e.g. https://www.phoronix.com/news/Intel-Legacy-BIOS-EOL-2020); a year is not the test (`lessons.md`: a 2020 Comet Lake ThinkPad runs Windows 98 SE), and no machine of these lines is read here |
| 1022:145C, 15E0, 15E1, 15E5, 43B9, 43BA, 43BB, 43BC and the other AMD ids | AMD desktop (AM4) platforms and their mobile parts | not established for AM4; yes on one AM5 board | the AM5 board the project probed for record 17 is a CSM model (PRO B650M-CT-CSM, `runs/run-35.md`), so a later AMD desktop socket can have one; no AM4 board is read here |
| 1D17:9202, 9203 | Zhaoxin | not established | no machine read here |
| 1DB7:DC27, 14E4:9026, 177D:9026 | Phytium, Broadcom/Cavium SoCs | not x86 | no supported system runs there; listed so a contrary reading would move them |

The rule settles two quirks: Q6 (Tiger Lake and Alder Lake only) and Q17
(Cherry Trail, by the owner's ruling) are not applicable (section 3.3).
Every other quirk keeps at least one id that passes or is not established.

### 3.1 Taken

Ids are `vendor:device`, hexadecimal; **(h)** marks a value from
`pci_ids.h` (section 2.1). "Every" means vendor-wide. Each value is a
REG_DWORD in the controller's driver key (section 5.1); the Linux flag is
beside it.

| Id | Value (Linux flag) | Set at | Controllers | What the controller does wrong | Linux acts at | This driver until now | Default |
|---|---|---|---|---|---|---|---|
| Q1 | `XhciMsiAvoid` (`XHCI_BROKEN_MSI`) | `xhci-pci.c:256-284` | 1B73:1000 (FL1000/PDK), 1B73:1400, every revision | advertises MSI, raises none ("Fresco Logic confirms", `xhci-pci.c:275-278`) | `xhci-pci.c:149-154`: never tries MSI | Vista and 7 connect a message where Windows grants one (records 15 and 18; both INFs write `MSISupported` 1); the dead-line rule is the line's only (record 15) | 1, on |
| Q2 | `XhciStreamsOff` (`XHCI_BROKEN_STREAMS`) | `xhci-pci.c:273-274`, `286-288`, `423`, `444-446`, `460` | 1B73:1000, 1B73:1009, 1B6F:7023, 1B6F:7052, 1106:3432 **(h)**, 1B21:1042 **(h)** | streams "broken beyond repair" (`xhci.h:1616`) | `xhci-pci.c:664`, `xhci.c:3638-3643`: no streams | streams wherever HCCPARAMS1.MaxPSASize > 0 (`hcd_cfg.c` `hcdCfgStreamsOpen`; the transport decision of `xhci_xport.h`) | 1, on |
| Q3 | `XhciResumeReinit` (`XHCI_RESET_ON_RESUME`) | `xhci-pci.c:326-329`, `418-425`, `431-435`, `436-437`, `439-441`, `476-478`, `497-499` | 1022:15E5, 1B6F:7023, 1B6F:7052, 1912:0015, every 1106, 1DB7:DC27, 1B21:3042, 1D17:9202 **(h)** | state is not kept across a suspend | `xhci.c:1105-1108`: resume re-initializes | resume restores (CRS) when the suspend saved (FSC), else re-initializes (`xhci_init.c` `XhciResumeController`) | 1, on |
| Q4 | `XhciIntelShutdownRoute` (`XHCI_SPURIOUS_REBOOT`) | `xhci-pci.c:350-370` | 8086:1E31 **(h)**, 8086:9C31, 8086:9CB1 | a powered-off machine powers back on (`xhci-pci.c:355-362`) | `xhci.c:753-754`: at shutdown both port-routing registers written 0 (`pci-quirks.c:1143-1147`) | at shutdown hands back only the connectors a route moved, and with `XhciIntelPortSwitch` 0, the default since `2.3.0.0`, writes nothing (`hcd_power.c`, `HcdPswRelease`; record 16 section 7a) | 1, on |
| Q5 | `XhciShutdownReset` (`XHCI_SPURIOUS_WAKEUP`) | `xhci-pci.c:365-370` | 8086:9C31, 8086:9CB1 | spurious wakes at shutdown ("with HSW", `xhci.c:770-772`, `xhci-pci.c:947`) | `xhci.c:774-776`: HCRST after the shutdown's halt; `xhci-pci.c:948-949`: the function put in D3hot | the shutdown's D3 halts and saves, no HCRST (`XhciSuspendController`) | 0, off |
| Q7 | `XhciSuspendDelay` (`XHCI_SUSPEND_DELAY`) | `xhci-pci.c:315-320` | 1022:145C, 1022:15E0, 1022:15E1, 1022:43BB | needs a pause before Run/Stop is cleared at suspend | `xhci.c:985-986`: 1 to 1.5 ms before the halt | halts at once (`XhciSuspendController` -> `XhciQuiesceController`) | 1, on |
| Q8 | `XhciSlowHalt` (`XHCI_SLOW_SUSPEND`) | `xhci-pci.c:265-272` | 1B73:1000 revision 04; matched here at every revision (section 5.3) | halts slowly at suspend ("extra slowly") | `xhci.c:1004-1007`: the suspend's halt bound times 10 (from `XHCI_MAX_HALT_USEC * 2`, `xhci.c:954`, `xhci-ext-caps.h:12`) | the halt bound is `XHCI_HALT_TIMEOUT_MS`, 32 ms (`xhci_init.c`), on every path | 1, on |
| Q9 | `XhciAsmediaFlowCtl` (`XHCI_ASMEDIA_MODIFY_FLOWCONTROL`) | `xhci-pci.c:472-474` | 1B21:1142 **(h)** (ASM1042A) | needs a vendor flow-control setting after each reset | `xhci.c:219-220` (after every HCRST), `xhci.c:1275-1276` (resume): `pci-quirks.c:593-634`, a vendor write through configuration offsets E0h, F8h and FCh | no such write | 1, on |
| Q10 | `XhciTiIntervalCap` (`XHCI_LIMIT_ENDPOINT_INTERVAL_7`) | `xhci-pci.c:480-481` | 104C:8241 **(h)** | periodic Interval 7 or above fails at High Speed and above | `xhci-mem.c:1452-1463`: interrupt and isochronous, High Speed and above, Interval 7 or more programmed as 6 | no cap on 104C (`xhci_tol.c` `XhciTolCapApplies` is vendor 1022 only) | 1, on (interrupt endpoints only) |
| Q11 | `XhciIntervalCap`, existing (`XHCI_LIMIT_ENDPOINT_INTERVAL_9`) | `xhci-pci.c:304-306` | 1002:7316 **(h)** (ATI Navi10) | periodic Interval 9 or above fails | `xhci-mem.c:1452-1458`: Interval 9 or more programmed as 8 | `XhciIntervalCap` covers vendor 1022 only | its existing 1 |
| Q12 | `XhciStuckLinkReset` (`XHCI_MISSING_CAS`) | `xhci-pci.c:390-396` | 8086:22B5, 8086:9D2F, 8086:A12F, 8086:5AA8, 8086:19D0 | a USB3 port stuck in Polling or Compliance after resume without setting CAS | `xhci-hub.c:1885-1896` with `xhci-hub.c:1820-1841`: at bus resume a port with CCS and CAS clear and PLS Polling or Compliance is warm-reset | a restore marks every root port changed (`hcd_dev.c`, `XhciRootHubInit`), and the thread's reading warm-resets one in Compliance (`xhci_link.c`, 29-A.2): that half is covered. A port in Polling is waited on (`XHCI_LINK_ACT_WAIT`), so one stuck there is never reset | 1, on (the Polling half) |
| Q13 | `XhciStreamRingPad` (`XHCI_TRB_OVERFETCH`) | `xhci-pci.c:448-451`, `497-504` | 1106:3483 **(h)** (VL805), 1D17:9202 **(h)**, 1D17:9203 **(h)** | prefetches past the end of a ring segment | `xhci-mem.c:2441-2447`: every segment allocated with a second segment's space after it | the controller common buffer has driver memory after every ring (record 04 section 4); a stream block ends on its last stream ring (`xhci_stream.c` `XhciStreamLayout`, `XhciStreamLayoutPlaced`; `hcd_dma.c` `HcdDmaStreamAlloc`) | 1, on |
| Q14 | `XhciEtronWorkarounds` (`XHCI_ETRON_HOST`) | `xhci-pci.c:418-425` | 1B6F:7023 (EJ168), 1B6F:7052 (EJ188) | (a) a SuperSpeed control transfer whose Setup and Data stages a Link TRB separates; (b) a Reset Device command; (c) on an isochronous ring, a Success event after a Transaction Error, Babble or Isoch Buffer Overrun | (a) `xhci-ring.c:3772-3784`: a No Op TRB first so the stages stay together; (b) `xhci.c:3974-3986`: Disable Slot and a new slot instead; (c) `xhci-ring.c:2593-2597`: the Success taken as spurious | (a) a control TD may cross the Link TRB (`xhci_ring.c` `XhciRingEnqueueTdGroup`); (b) `RESET_PORT` issues Reset Device (`hcd_enum.c`, the `XhciTrbResetDevice` call); (c) an event with no TD is unattributed and an isochronous one cycles nothing (`xhci_tol.c` `XhciTolCycleReason`) - not confirmed against this case | 1, on: (a), (c), and `RESET_PORT` refused for (b) |
| Q16 | `XhciAmdSparseOff` (`XHCI_DISABLE_SPARSE`) | `xhci-pci.c:326-329` | 1022:15E5 | (none stated in the mirror) | `xhci-pci.c:802-809`, `837-838`: before each suspend, bit 17 of BAR offset C12Ch cleared | no such write | 1, on |

### 3.2 Covered already

| Linux flag (or behaviour) | Set at | Controllers | What Linux does | How this driver covers it |
|---|---|---|---|---|
| `XHCI_SPURIOUS_SUCCESS` | `xhci.c:5482-5487`; `xhci-pci.c:453-461` | every controller above 0.96; 1B21:1042 | `xhci-ring.c:2587-2592`: a Success after a Short Packet on the same TD ignored | on every controller: the TD engine takes the second event the specification allows after a short packet, and the spurious one on the TD's last TRB, as known and counted (`xhci_xfer.c`, the short-packet tail handling, `MidTdDeferralsTailedSpurious`) |
| `XHCI_INTEL_HOST`, its reset delay | `xhci-pci.c:345-349` | every 8086 **(h)** | `xhci.c:205-213`: 1 ms after HCRST before any register access | on every controller: `xhci_init.c` `xhciReset` stalls 1 ms after the HCRST write. Its other uses (LPM timeouts, `xhci.c:4906-4907`, `4970-4971`, `5054`; eUSB2 double-bandwidth isochronous Mult, `xhci-mem.c:1351-1354`) are features this driver does not use |
| `XHCI_AVOID_BEI` | `xhci-pci.c:345-349` | every 8086 **(h)** | `xhci-ring.c:4048-4049`: BEI cleared on every eighth isochronous TD | BEI is never set (`xhci_xfer.c`, the isochronous TD builder: every TD carries IOC) |
| `XHCI_NO_SOFT_RETRY` | `xhci-pci.c:418-425`, `488-491` | 1B6F:7023, 1B6F:7052, 1022:43B9, 1022:43BB | `xhci-ring.c:2529-2533`: no soft retry | the same four ids are excluded from 35-T.2's soft retry (`xhci_tol.c` `XhciTolNoSoftRetry`; record 17 section 4.2) |
| `XHCI_LIMIT_ENDPOINT_INTERVAL_9` on the AMD ids | `xhci-pci.c:293-302` | 1022:13ED, 13EE, 148C, 15D4, 15D5, 15E0, 15E1, 15E5 | `xhci-mem.c:1452-1458`: interrupt and isochronous Interval 9 or more programmed as 8 | `XhciIntervalCap` 1 caps every 1022 controller, wider than Linux's list, interrupt endpoints only: isochronous ones are not capped, by the owner's decision of 2026-10-06 (record 17 section 4.7). Q11 adds the one id it missed |
| `XHCI_EP_LIMIT_QUIRK` | `xhci-pci.c:350-353` | 8086:1E31 **(h)** | `xhci.c:2264-2282`, `4202-4214` and the release paths: at most 64 active endpoint contexts, EP0s included | the policy limits hold the driver at 64: 32 slots each with one EP0 (`XHCI_MAX_SLOTS`) and 32 pooled rings for every other endpoint (`XHCI_MAX_POOL_RINGS`, `xhci_mem.c` `XhciPoolAcquire`, called from `hcd_cfg.c`). A host assertion that no endpoint context is ever live without a pool ring is added at implementation, so the bound is proved rather than read |
| `XHCI_SNPS_BROKEN_SUSPEND` | `xhci-pci.c:322-324` | 1022:15E0, 1022:15E1 | `xhci.c:1025-1046`: a save that never clears SSS accepted, and the resume re-initializes | a save that does not complete leaves no valid state (`xhci_init.c` `xhciSaveState`), and the resume then re-initializes - the same end |
| `XHCI_NO_64BIT_SUPPORT` | `xhci-pci.c:462-470` | 1B21:1142, 1242, 2142, 3242 | `xhci.c:5515-5516`: AC64 ignored, 32-bit DMA | every address is below 4 GB and every high dword written 0 (AGENTS.md, "No 64-bit DMA") |
| `XHCI_U2_DISABLE_WAKE` | `xhci-pci.c:334-339` | 1022:43B9, 43BA, 43BB, 43BC | `xhci-hub.c:1776-1781` with `pci-quirks.c:506-589`: no wake bits on a USB 2.0 port the firmware disabled | no port wake bit is ever set (`PORTSC` WCE, WDE and WOE are defined in `xhci.h` and written by nothing), and the controller never arms system wake (section 3.4) |
| `XHCI_ZHAOXIN_HOST`, its port protocol | `xhci-pci.c:493-495` | every 1D17 **(h)** | `xhci-mem.c:2060-2066`, `2113-2116`: a USB 3.1 Minor Revision taken as 3.0 unless a PSI names Gen 2 | speeds come from each port's PSIV and PSI table, never from the Minor Revision (`xhci_caps.c`, `xhciDefaultKilobits`). Its LPM uses (`xhci.c:4906`, `4970`, `5056`) are unused here |
| The Intel port switchover | `pci-quirks.c:1058-1147`, `1224-1225`; `xhci-pci.c:878-879` | every 8086 **(h)** with the routing registers | connectors routed to xHCI at handoff and at each resume | `XhciIntelPortSwitch` (record 16), off by default by the owner's decision of 2026-10-09 |

### 3.3 Not applicable, dropped or not taken

| Linux flag (or behaviour) | Set at | Why |
|---|---|---|
| Q6, `XHCI_RESET_TO_DEFAULT` | `xhci-pci.c:398-402` | 8086:A0ED, 51ED, 54ED (Tiger Lake PCH, Alder Lake PCH and N): HCRST at shutdown (`xhci.c:774-776`) and, at hibernate, a root port in U3 without wake disabled (`xhci-pci.c:890-936`). Every id fails section 3.0's test (owner, 2026-10-13) |
| Q17, `XHCI_SSIC_PORT_UNUSED` | `xhci-pci.c:382-384` | 8086:22B5: vendor registers at BAR offset 880Ch + 30h * i marked around D3, or the controller may not enter D3 (`xhci-pci.c:748-784`). Not applicable: Cherry Trail has no CSM (owner, 2026-10-13); the Braswell question of section 3.0 is not pursued for a power-only quirk |
| Q15, the forced BIOS handoff | `pci-quirks.c:1190-1196` | 104C:8241 **(h)**, 1912:0014 **(h)**: OS Owned set and BIOS Owned cleared in one write. **Dropped** (owner's condition, 2026-10-13: taken only if xHCI 1.2c permits the OS to clear BIOS Owned). Table 7-4 (p.478) gives bit 16 the attribute RW, but describes it as the BIOS's: "The BIOS sets this bit to establish ownership of the xHC. System BIOS will set this bit to a '0' in response to a request for ownership of the xHC by system software". Section 4.22.1 (p.309-310) gives the OS one action - set OS Owned and wait for BIOS Owned to read 0, for no more than a second - and no step in which the OS clears BIOS Owned. So the specification does not provide for it. (The handoff's comment in `xhciBiosHandoff` that bit 16 is "read-only from the OS side too" overstates the table's RW; a wording fix for the implementation stage) |
| Q18, `XHCI_SW_BW_CHECKING` | `xhci-pci.c:354` | 8086:1E31 **(h)**: Linux keeps its own periodic bandwidth tables (`xhci.c:2996-3003` and the reservation code behind it) because the controller admits schedules it cannot serve. **Not taken** (owner): a task of its own if a report needs it |
| Q19, `XHCI_AMD_PLL_FIX` | `xhci-pci.c:311-313` | every 1022 xHCI on an SB700 (revision 3Bh or below), SB800, Hudson-2 or Bolton chipset (`pci-quirks.c:153-194`, `209-287`): chipset power management held off while isochronous requests are queued (`pci-quirks.c:340-455`; `xhci-ring.c:832-837`, `4209-4212`), through I/O ports CD6h and CD7h and the northbridge's configuration space. **Not taken** (owner): a task of its own if a report needs it. It needs port-I/O imports, and `HAL.dll!WRITE_PORT_UCHAR` is the import kept out of the shipping flavours because defect 2b's cause is not established (AGENTS.md, "Build flavours"); it writes devices other than the xHCI function; and whether these FCH parts pass the HCIVERSION floor is not in the mirror |
| Q20, `XHCI_CDNS_SCTX_QUIRK` | `xhci-pci.c:506-508` | 17CD:0200 **(h)**: two reserved dwords of a stream context cleared after Set TR Dequeue (`xhci-ring.c:1490-1502`). **Not taken** (owner): a task of its own if a report needs it |
| `XHCI_LINK_TRB_QUIRK` | `xhci.c:5489-5492` | HCIVERSION 0.95 only, and only with a module parameter that defaults to 0. This driver refuses a controller below 1.0 (`xhci_caps.c` `XhciCheckCapDword0`, `XHCI_HC_BAD_VERSION`); task 38.11 moves the floor to 0.96 (owner, 2026-10-13), which still refuses 0.95 |
| `XHCI_AMD_0x96_HOST` | `xhci-pci.c:308-309` | HCIVERSION 0.96 only (its use, `xhci.h:1782-1786`: isochronous Link TRBs kept chained). **Not taken** (owner, 2026-10-13): task 38.11 admits 0.96 controllers and drives them as 1.0 ones but for the TD Size field, and the 0.96-era quirks are not taken there |
| `XHCI_NEC_HOST` | `xhci-pci.c:290-291` | every 1033 **(h)**: a vendor Get Firmware command at start whose answer is only logged (`xhci.c:652-663`, `xhci-ring.c:1683-1694`, `1935-1941`), and isochronous Link TRBs kept chained for "0.96 hardware from AMD and NEC" (`xhci.h:1779-1786`). The command is diagnostic. **The chaining is not taken** (owner, 2026-10-13, with task 38.11, as for `XHCI_AMD_0x96_HOST`). Linux keys both on the vendor alone, whatever the version |
| `XHCI_SPURIOUS_SUCCESS` and `XHCI_BROKEN_STREAMS` on 1B21:1042 | `xhci-pci.c:453-461` | "reports 0.96" (`xhci-pci.c:455-458`): refused by today's floor, admitted by task 38.11's. Covered then as on every controller: the spurious Success by the TD engine (section 3.2), the streams by Q2, whose list carries the id |
| `XHCI_RESET_EP_QUIRK`, `XHCI_TRUST_TX_LENGTH`, `XHCI_PLAT` | `xhci.h:1589`, `1607`, `1613` | deprecated; the first is set for 1B73:1000 revision 00 with a trace that says so (`xhci-pci.c:259-264`) and acts nowhere; the other two are set by no file in the mirror |
| `XHCI_LPM_SUPPORT`, `XHCI_HW_LPM_DISABLE` | `xhci-pci.c:345-349`, `448-451`, `493-495` | link power management: this driver programs no U1 or U2 timeout and no USB 2.0 LPM (no `PORTPMSC` write anywhere in `src`) |
| `XHCI_PME_STUCK_QUIRK` | `xhci-pci.c:371-381` | clears a PME latch so wake works (`xhci-pci.c:786-800`); the controller never arms wake here (section 3.4) |
| `XHCI_DEFAULT_PM_RUNTIME_ALLOW` | `xhci-pci.c:331-332`, `404-416`, `510-512` | Linux's runtime power policy (`xhci-pci.c:670-673`, `729-732`); no counterpart here |
| `XHCI_INTEL_USB_ROLE_SW` | `xhci-pci.c:385-389` | creates Linux's dual-role switch device (`xhci-ext-caps.c:95-100`); no counterpart here |
| `XHCI_BROKEN_D3COLD_S2I` | `xhci-pci.c:341-343` | suspend-to-idle only (`xhci-pci.c:824-829`), a Linux sleep state no target has |
| `XHCI_ZERO_64B_REGS` | `xhci-pci.c:427-435` | acts only behind a translating IOMMU (`xhci.c:257-264`); no target remaps this driver's DMA through one, as far as this survey read, and the driver writes every high dword 0 |
| `XHCI_RESET_PLL_ON_DISCONNECT` | `xhci-pci.c:483-486` | 14E4 **(h)** or 177D **(h)** device 9026: a PHY reset through vendor registers on a USB 2.0 disconnect (`xhci-ring.c:1975-1990`, `2134-2139`). Not x86 (section 3.0) |
| `XHCI_COMP_MODE_QUIRK` | `xhci.c:567-571` | keyed on DMI strings (HP Z420, Z620, Z820, Z1 Workstation, `xhci.c:425-450`), not on a PCI id, so outside the task's matching. Its symptom - a port in Compliance Mode with no change event - is partly covered: 29-A.2 warm-resets Compliance when a change shows it |
| `XHCI_MTK_HOST`, `XHCI_SKIP_PHY_INIT`, `XHCI_SUSPEND_RESUME_CLKS`, `XHCI_WRITE_64_HI_LO`, `XHCI_BROKEN_PORT_PED`, `XHCI_SG_TRB_CACHE_SIZE_QUIRK`, `XHCI_EP_CTX_BROKEN_DCS` | `xhci.h:1619`, `1636`, `1642`, `1646`, `1624`, `1638`, `1641` | set by no file in the mirror: the platform glue sets them, or (`XHCI_EP_CTX_BROKEN_DCS`) no mainline file the mirror carries. Platform glue is not a PCI controller this driver binds |
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
  only. The id gates until now are `XhciIntelPortSwitch`,
  `XhciIntelIrqRoute` and `XhciIntervalCap`, each 0, 1 or 2 with 1 the
  listed controllers (`hcd_ctl.c` names; `hcdTolApplies` is where the cap
  and QEMU's stream-context quirk are settled after the id is read).
- **Reset.** A 1 ms stall follows every HCRST (`xhci_init.c`,
  `xhciReset`).
- **Suspend, resume, shutdown.** Every D3, the shutdown's included,
  quiesces, halts within 32 ms and saves with CSS where the controller has
  FSC (`xhci_init.c`, `XhciSuspendController`, `xhciSaveState`); a D0
  restores with CRS when the save is valid and otherwise re-initializes,
  dropping every device (`XhciResumeController`). The shutdown's D3 adds
  37.3's flush and `HcdPswRelease` (`hcd_power.c`). No HCRST at shutdown.
- **Wake.** The controller FDO never requests `IRP_MN_WAIT_WAKE`, the
  root-hub PDO refuses it (`hcd_rh.c`), and no `PORTSC` wake bit is set.
  System wake from USB is not offered; selective suspend's remote wake
  (record 14) is a device's, in S0.
- **Rings.** The Chain bit of the permanent Link TRB is rewritten at each
  crossing from the TD that crosses it, clear between TDs (`xhci_ring.c`,
  `XhciRingEnqueueTdGroup`), as xHCI 1.0 onward asks.
- **Streams and UAS.** Streams are offered wherever MaxPSASize > 0
  (`hcd_cfg.c`, `hcdCfgStreamsOpen`); a UAS-only device at SuperSpeed on a
  controller with none has no transport and goes back to USB 2.0 (29-A.5;
  `xhci_xport.h`).
- **Interrupts.** 98 SE to XP take the line or poll; Vista and 7 take a
  message where Windows grants one (records 15 and 18); the dead-line rule
  watches only a line.
- **Configuration space.** Writes above the standard header are the Intel
  switchover's alone (`implementation-invariants.md`, "PnP Resources").

## 4. The quirks taken

Each: what Linux does, where this driver acts, the change, its value and
default, its cost, and what the reports show. The behaviour named is
Linux's as read; the implementation checks each against xHCI 1.2c before
it is written (task 38.10). Values are 0 off, 1 Linux's ids and 2 every
controller where offered (section 5.1).

### 4.1 Q1, `XhciMsiAvoid` (Fresco Logic, `XHCI_BROKEN_MSI`)

Linux never asks for MSI on 1B73:1000 or 1B73:1400 and takes the line.
Here the question arises only on Vista and 7, where a message is
connected when Windows grants one. On a listed controller the message
would never arrive, and the dead-line watcher does not watch a message;
only 35-T.1's lost-interrupt backstop would drain the ring, each event at
least 100 ms and typically 300 to 400 ms late (record 17 section 4.1).

The change: on a listed controller the requirements filter of record 18
section 4.2 keeps only the line configurations, as `XhciInterruptMode` 1
and the no-helper x86 path already make it do. How it meets
`XhciInterruptMode` (record 15, as amended by 36.53):

- **0, automatic (the default).** The line where Windows offers one, and
  polling where there is none or once the line is proven dead - the usual
  fallback, unchanged. Not mode 1's refusal: a listed controller with no
  line is polled, not failed.
- **1, the line only.** Unchanged: the filter already keeps only the line;
  no line offered still fails the start, as mode 1 does everywhere.
- **2, polling.** Unchanged: no interrupt is connected.
- **3, a message only.** The user asked for a message by name, so the
  value is honoured and the quirk does not strip the message
  configurations; on a listed controller that start then runs on a
  message the controller does not raise, which the log says
  (`quirk.msiavoid` with "mode 3 overrides"). Mode 3 exists for testing a
  message on purpose; the quirk is not a reason to forbid that.

98 SE to XP are unaffected (no message there). 2 avoids a message on
every controller, with mode 0's fallback to polling - which is what
separates it from `XhciInterruptMode` 1, whose start fails where no line
is offered. Default 1.

### 4.2 Q2, `XhciStreamsOff` (`XHCI_BROKEN_STREAMS`)

Linux offers no streams on six ids. Here the controller's MaxPSASize is
read in two places, and both read it as 0 on a listed controller: the
transport decision (UAS at SuperSpeed only where the controller streams;
`xhci_xport.h`) and `OPEN_STREAMS` (`hcdCfgStreamsOpen`, which already
refuses with `XHCI98_STREAMS_NO_CONTROLLER`). A UAS device that also offers
Bulk-Only takes Bulk-Only; a UAS-only device at SuperSpeed goes back to USB
2.0 by 29-A.5, where streamless UAS runs. 2 makes every controller read as
streamless (a tester's way to take streams out of a report). Default 1.
Cost: the pure decision gains one input; host tests of both paths.

### 4.3 Q3, `XhciResumeReinit` (`XHCI_RESET_ON_RESUME`)

Linux re-initializes these controllers at every resume. Here, on a listed
controller, the suspend does not save (no CSS) and the resume does not
restore: it takes the existing re-initialization, the path a failed
restore already takes (`XhciResumeController`), which drops every device
and re-enumerates it. 2 applies it on every controller. Default 1. Cost:
two tests; the user-visible cost on a listed controller is that a device
in use across standby or hibernate is re-enumerated, as on any controller
without FSC today. The matching is vendor-wide for VIA (section 5.2).

### 4.4 Q4, `XhciIntelShutdownRoute` (`XHCI_SPURIOUS_REBOOT`)

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
(`HcdSvcConfigSpace`), after the existing release. Record 16 section 5's
"0 ... makes no switchover write" gains this exception, and
`implementation-invariants.md` names the write. 0 or 1 only: the
registers are Intel's and exist only on the PCH generations record 16
lists, so a 2 is read as 1 and logged (section 5.1). Default 1. The project's B490
(1E31) and E7250 (9CB1) are listed.

### 4.5 Q5, `XhciShutdownReset` (`XHCI_SPURIOUS_WAKEUP`), off by default

Linux resets the controller (HCRST) after the shutdown's halt and puts the
function in D3hot. Here the shutdown's D3 halts and saves; the change is
an HCRST after that halt, at the shutdown only, on a listed controller
when the value is 1, on every controller at 2. Whether the PCI bus driver
of each target then sets D3hot when the D3 IRP reaches it is the operating
system's and is not read; the HCRST is the part this driver owns. **Default
0, off** (owner, 2026-10-13): nothing in the project's reports shows the
symptom, and the E7250 (9CB1) runs without it; the value is there for a
machine that wakes by itself after shutdown. Cost: one bounded HCRST at
shutdown when on.

### 4.6 Q7, `XhciSuspendDelay` (AMD, `XHCI_SUSPEND_DELAY`)

Linux sleeps 1 to 1.5 ms before clearing Run/Stop at suspend on 1022:145C,
15E0, 15E1 and 43BB. Here the suspend waits 1.5 ms before
`XhciQuiesceController` on a listed controller, at PASSIVE_LEVEL in the
power path. 2 applies the delay on every controller. Default 1. Cost:
1.5 ms per D3.

### 4.7 Q8, `XhciSlowHalt` (Fresco Logic FL1000, `XHCI_SLOW_SUSPEND`)

Linux allows ten times its usual 64 ms for the suspend's halt on
1B73:1000 revision 04. Here every halt waits at most 32 ms
(`XHCI_HALT_TIMEOUT_MS`); a halt that times out at suspend is counted
(`SuspendFailures`) and leaves the controller unsaved and possibly running
into D3. The change: on a listed controller the suspend's halt waits up to
640 ms. The wait ends as soon as HCH reads 1, so a longer bound costs a
healthy controller nothing, which is why every revision of 1B73:1000 is
matched (section 5.3). 2 applies the longer bound on every controller.
Default 1.

### 4.8 Q9, `XhciAsmediaFlowCtl` (ASM1042A, `XHCI_ASMEDIA_MODIFY_FLOWCONTROL`)

Linux performs a vendor write after every HCRST and at every resume on
1B21:1142: wait for the write-busy bit of configuration byte E0h to clear,
write a command and an address to F8h and FCh, set the write bit in E0h,
wait again, write the data, set it again (`pci-quirks.c:593-634`; the
values are constants there). Here it follows `xhciReset` at each start,
recovery and resume, through the configuration-space path. It is a second
kind of configuration write above the standard header, beside the Intel
switchover's, so `implementation-invariants.md` changes with it. 0 or 1
only: the registers are this vendor's, so a 2 is read as 1 and logged.
Default 1. Cost: a bounded poll (Linux allows
1000 x 50 us) and four configuration writes per reset.

### 4.9 Q10, `XhciTiIntervalCap` (TUSB73x0, `XHCI_LIMIT_ENDPOINT_INTERVAL_7`)

Linux programs a periodic endpoint's Interval as 6 where it would be 7 or
more, at High Speed and above, on 104C:8241. Here it takes
`XhciTolCapInterval`'s shape with another threshold and another id, for
**interrupt endpoints only** (owner, 2026-10-13), as record 17 decided for
the AMD cap: an isochronous endpoint's interval sets its data rate. The
cap's ordering against fast polling (record 17 section 4.7) carries over.
2 applies the cap on every controller. Default 1.

### 4.10 Q11, 1002:7316 under `XhciIntervalCap`

Linux caps Interval at 8 on 1002:7316, a controller of another vendor id
than `XhciIntervalCap`'s. `XhciIntervalCap` 1 covers this id as well (owner,
2026-10-13); no new value; its 0 and 2 unchanged. Cost: one id in
`XhciTolCapApplies`, and record 17 section 4.7 says so.

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
post-resume reading is warm-reset within 29-A.2's existing budget (the
re-initializing path starts from HCRST and needs nothing). 2 applies it on
every controller. Default 1. The project's E460 (9D2F) is listed.

### 4.12 Q13, `XhciStreamRingPad` (VIA VL805, Zhaoxin, `XHCI_TRB_OVERFETCH`)

Linux gives every ring segment a second segment's worth of space after it
on these. Here the controller common buffer already has driver memory
after every ring (record 04 section 4: the last region is the scratchpad
pages); the one place a ring ends at the end of an allocation is a stream
block, whose stream rings come last (`XhciStreamLayout`,
`XhciStreamLayoutPlaced`). The change: on a listed controller a stream
block is allocated one ring longer. 2 pads on every controller. Default 1.
The VL805 has a field user (issue 13) and does streams (MaxPSASize 4,
issue 13 section 4).

### 4.13 Q14, `XhciEtronWorkarounds` (`XHCI_ETRON_HOST`)

Linux gives the EJ168 and EJ188 four bits: Q2's and Q3's lists include
them, the soft-retry exclusion is covered (section 3.2), and
`XHCI_ETRON_HOST` does three things. Taken (owner, 2026-10-13):

- (a) a SuperSpeed control transfer whose Setup TRB would be the last
  before the Link TRB gets a No Op TRB first, so Setup and Data are not
  split by the link. Here the control TD is enqueued as one group that may
  cross the link (`XhciRingEnqueueTdGroup`); the change is a Transfer No Op
  at the ring's last usable position when the next TD is a SuperSpeed
  control TD. Small, in the pure ring core, host-testable.
- (b) in place of Linux's Disable Slot and fresh slot, `RESET_PORT` is
  refused on a listed controller, so the class driver's recovery escalates
  to the device cycle of record 17 section 4.3 (a replug done by the bus,
  which recreates the PDO). A slot swap inside `RESET_PORT` would change
  the Slot ID the device's records, rings and handles are keyed by; the
  refusal reuses a path that exists. What status the refusal returns, and
  that each target's storage stack then escalates rather than gives up, is
  the implementation's to read per target.
- (c) on an isochronous ring, a Success event after a Transaction Error,
  Babble or Isoch Buffer Overrun on the same TD is ignored and counted.
  Here such an event finds no TD and is unattributed, and an isochronous
  one cycles nothing (`XhciTolCycleReason`); a host vector confirms that,
  or the case is added.

`XHCIQUAL` marks both parts `QF_AVOID`, the project's own verdict. 2
applies (a) and (c), which are generic, on every controller; (b) stays on
the listed ids at 2 as at 1 (owner, 2026-10-13: refusing `RESET_PORT` is
not offered everywhere). Default 1.

### 4.14 Q16, `XhciAmdSparseOff` (AMD Raven2, `XHCI_DISABLE_SPARSE`)

Linux clears bit 17 of BAR offset C12Ch before each suspend on 1022:15E5.
The mirror says nothing of what the bit does. Here it is one
read-modify-write in the D3 path on a listed controller. 0 or 1 only: an
AMD vendor bit, so a 2 is read as 1 and logged. Default 1.
Q3 also lists 15E5.

## 5. What is common to every quirk taken

### 5.1 The registry values

**Owner, 2026-10-13:** one REG_DWORD per quirk, named for its effect in
the style of the values already there (`XhciIntelIrqRoute`,
`XhciIntelPortSwitch`, `XhciIntervalCap`, `XhciAvgTrbEsit`,
`XhciUsb2HubSpeedFallback`), with the Linux flag given beside each here and
in `XHCISNAP`; those five keep their names. The names:

| Value | Linux flag | Effect | Default | 2 offered |
|---|---|---|---|---|
| `XhciMsiAvoid` | `XHCI_BROKEN_MSI` | the line or polling, never a message | 1 | yes |
| `XhciStreamsOff` | `XHCI_BROKEN_STREAMS` | the controller treated as having no streams | 1 | yes |
| `XhciResumeReinit` | `XHCI_RESET_ON_RESUME` | no save or restore; resume re-initializes | 1 | yes |
| `XhciIntelShutdownRoute` | `XHCI_SPURIOUS_REBOOT` | connectors routed to EHCI at shutdown | 1 | no |
| `XhciShutdownReset` | `XHCI_SPURIOUS_WAKEUP` | HCRST after the shutdown's halt | 0 | yes |
| `XhciSuspendDelay` | `XHCI_SUSPEND_DELAY` | 1.5 ms before the suspend's halt | 1 | yes |
| `XhciSlowHalt` | `XHCI_SLOW_SUSPEND` | the suspend's halt bound 640 ms | 1 | yes |
| `XhciAsmediaFlowCtl` | `XHCI_ASMEDIA_MODIFY_FLOWCONTROL` | the vendor flow-control write after each reset | 1 | no |
| `XhciTiIntervalCap` | `XHCI_LIMIT_ENDPOINT_INTERVAL_7` | interrupt Interval 7 and above programmed as 6 | 1 | yes |
| `XhciStuckLinkReset` | `XHCI_MISSING_CAS` | a USB3 port stuck in Polling warm-reset after resume | 1 | yes |
| `XhciStreamRingPad` | `XHCI_TRB_OVERFETCH` | one ring of padding after a stream block | 1 | yes |
| `XhciEtronWorkarounds` | `XHCI_ETRON_HOST` | (a), (b) and (c) of section 4.13 | 1 | (a) and (c) only |
| `XhciAmdSparseOff` | `XHCI_DISABLE_SPARSE` | bit 17 of C12Ch cleared before suspend | 1 | no |
| `XhciIntervalCap` (existing) | `XHCI_LIMIT_ENDPOINT_INTERVAL_9` | 1002:7316 added to its value 1; no value of its own | 1 | its existing 2 |

Each is read at each start with `hcdReadDword`, in the house form of
`XhciIntervalCap` and `XhciIntelIrqRoute`:

| Value | Meaning |
|---|---|
| 0 | off: Linux's behaviour is not applied, on any controller |
| 1 | Linux's listed controllers |
| 2 | every controller, where the fix is generic xHCI behaviour (the "2 offered" column); for a vendor-register or vendor-specific quirk - `XhciIntelShutdownRoute`, `XhciAsmediaFlowCtl`, `XhciAmdSparseOff`, and Q14's refused `RESET_PORT` - a 2 is read as 1 and logged (owner, 2026-10-13) |
| absent, another type or any other number | the default: 1, or 0 for `XhciShutdownReset` |

Absent is the default, so on by default for every value but one, unlike
`XhciIntelPortSwitch`: text-mode Setup's F6 install writes no values
(`txtsetup.oem`), and a quirk a listed controller needs there should apply
there. Both INFs write each value at its default with `0x00010003` under
task 34.1's rule, with a `VAL-*` row in the INF gate; the footprints,
`README.md` and the release notes name each with its Linux flag and its
controllers.

### 5.2 Matching

Each quirk's id list is a table in the pure core, matched against
`PciVendorDevice` (device << 16 | vendor), with a host test of the match
per quirk: every listed id matches, the neighbours and the other vendors'
same device numbers do not, and the vendor-wide row (Q3's every 1106)
matches every device of that vendor. HCIVERSION enters nowhere: of the
two Linux bits keyed on it, the 0.95 one stays below the floor and the
0.96 one is not taken (section 3.3).

### 5.3 Revision

No revision is matched (owner, 2026-10-13). Linux matches a revision
twice, both on 1B73:1000: revision 00 for a deprecated bit that does
nothing, and revision 04 for Q8, whose longer halt bound costs a healthy
controller nothing, so every revision is matched. The revision
(configuration byte 08h) is read at each start and logged
(`hc.pci.rev`), so a report shows it.

### 5.4 What `XHCISNAP` reports

At each start, per value: the value read and whether it applied on this
controller, as a log note (`quirk.<name>`, value << 8 | applied) and as
fields of the counter block, beside 35-T.8's `CapMode` and `CapApplied`
(`xhcisnap.c` prints those as the `XhciIntervalCap` line); and the counts
that say a quirk acted (Q8's long halts, Q12's warm resets, Q13's padded
blocks, Q14's refused `RESET_PORT`s and spurious Successes, Q4 and Q5's
shutdown writes, which a later start sees only through the log ring
flushed at shutdown). `XHCISNAP` prints a "Linux quirks" block naming each
value, its Linux flag, its setting, and "applied" or "not this
controller".

### 5.5 What `XHCIQUAL` reports

`xhciqual/quirks.c` keys rows on an exact vendor and device, with a
16-bit flags word of which bits 0 to 10 are spoken for. The quirks taken
need: a flag each, which widens the word to 32 bits; rows for ids it lacks
(104C:8241, 1002:7316, 1022:145C, 1B21:3042, 1DB7:DC27, 8086:22B5,
8086:A12F, 8086:5AA8, 8086:19D0, 1D17:9202, 1D17:9203, and Q18 to Q20's
ids, named as not taken); a vendor-wide row form for VIA and Zhaoxin,
which it lacks; and each printed quirk naming the driver's registry value
and its Linux flag. It already reads the revision and HCIVERSION, so it
can say when a listed part is below the driver's floor (1.0 today, 0.96
once task 38.11 lands).

### 5.6 Provenance

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
Where a quirk is a register write or a test of an id, writing it fresh
from this record's description may be simpler than adapting, and then no
row is owed; the implementation says which, per quirk.

## 6. Legs

QEMU's controller (1B36:000D) is on no list, so a guest shows each value
read and "not this controller", value 2's reach where offered, and that
value 0 changes nothing; host tests carry the matching and the pure
decisions. Every value is documented with the controllers it was read on,
and for nearly all of them that is none: the project holds 9D2F (E460,
Q12), 1E31 (B490, Q4) and 9CB1 (E7250, Q4 and, at 1, Q5), and a field user
holds a VL805 (Q3, Q13). Each such machine read under the taken quirk is a
leg; the others ship unread, and the release notes say so.

## 7. Decisions (owner, 2026-10-13)

Taken from revision 0's open list, each with its recommendation then:

- **Form.** One REG_DWORD per quirk; 0 off, 1 Linux's ids, 2 every
  controller where sensible. (Recommended: the same.)
- **Naming.** Named for the effect in the style of the existing values,
  not `XhciQuirk<LinuxName>`; the Linux flag beside each in this record and
  in `XHCISNAP`; the existing five not renamed. (Recommended: Linux-derived
  names; not taken.)
- **Revision.** No revision matching; the revision logged. (Recommended:
  the same.)
- **HCIVERSION floor.** Unchanged in this task. (Recommended: the same.)
  Later the same day (owner): the floor moves to 0.96 in task 38.11, and
  the 0.96-era quirks - NEC's isochronous chaining, the AMD 0x96 rules, the
  0.95 link-TRB quirk - are not taken (section 3.3).
- **Value 2** (owner, later the same day): offered only where the fix is
  generic xHCI behaviour - Q1, Q2, Q3, Q5, Q7, Q8, Q10, Q12, Q13 and Q14's
  (a) and (c); the vendor-register or vendor-specific ones - Q4, Q9, Q16
  and Q14's refused `RESET_PORT` - take 0 or 1, a 2 read as 1 and logged;
  Q11 has no value of its own (section 5.1).
- **Numbering** (owner, later the same day): the task is 38.10, renumbered
  from 38.11.
- **CSM.** New rule: a platform without a CSM cannot run a supported
  system, so its quirks are not applicable (section 3.0).
- **Q1** taken, on, with the usual fallback to polling where
  `XhciInterruptMode` allows it (section 4.1).
- **Q2** taken, on, 2 offered. **Q3** taken, on, skipping save and
  restore. **Q4** taken, on, even at `XhciIntelPortSwitch` 0 (record 16's
  exception). **Q5** taken, **off** by default (recommended on). **Q6** not
  applicable (no CSM). **Q7, Q8, Q16** taken, on. **Q9** taken, on.
  **Q10** taken, interrupt endpoints only, on. **Q11** folded into
  `XhciIntervalCap`. **Q12** taken, on, the Polling half. **Q13** taken,
  gated, on. **Q14** (a) and (c) on, and (b) as a refused `RESET_PORT` so
  recovery escalates. **Q15** taken only if xHCI 1.2c permits the OS to
  clear BIOS Owned - it does not, so dropped (section 3.3). **Q17** not
  applicable (no CSM). **Q18, Q19, Q20** not taken, each a task of its own
  if a report needs it.

## 8. What changes in the documents

At implementation:

- `src/xhci98.inf` and `src/xhci98-amd64.inf`: a line per value; the INF
  gate's `VAL-*` table; the footprints.
- `README.md` and `docs/using/release-notes.md`: each value, its Linux
  flag, its controllers, its default, and which were read on hardware.
- `docs/contributing/implementation-invariants.md`, "PnP Resources": Q4's
  shutdown write and Q9's vendor write as configuration writes above the
  header.
- Design record 16 section 5 (Q4's exception to value 0); design record 17
  section 4.7 (Q11's id, and Q10 beside the cap).
- Design record 15 (Q1's meeting with each `XhciInterruptMode` value).
- `src/xhci_init.c`, `xhciBiosHandoff`'s comment on bit 16 (section 3.3,
  Q15).
- `xhciqual/quirks.c`, `qual.h` and `report.c`: section 5.5.
- `xhcisnap/xhcisnap.c` and `xhcisnap/README.md`: section 5.4.
- `legal-provenance.md` section on the mirrors: a row per copy, if any.
- `external/README.md`: `include/linux/pci_ids.h` added to the Linux
  mirror at the pinned commit, so section 2.1's **(h)** values rest on it
  rather than on the reference copy of a later master (changes no pin).

## 9. Provenance of this record

Linux facts: read from source at the pinned commit, `file:line` above, and
the **(h)** constants from `docs/references/pci_ids.h`; no binary was read,
so no static or runtime tag applies and no `legal-provenance.md` row is
owed. xHCI facts: xHCI 1.2c as `docs/references/README.md` pins it, by
page. Driver facts: read from `src` on `phase-38` (at `0976a08a`, rebased
onto `62b413d6`, whose `src` changes are comments in `hcd_pdo.c` and
`xhci_xport.h`), by file and function. Project
machines and their ids: `docs/contributing/runs/run-35.md` (E460 9D2F,
P14s Gen 1 02ED, B490 1E31, the AM5 CSM board) and `run-36.md` (E7250
9CB1); the VL805: issue 13. The CSM column of section 3.0: the owner's
rulings and the readings named in it, nothing else.
