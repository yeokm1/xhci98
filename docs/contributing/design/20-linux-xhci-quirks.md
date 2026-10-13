# Linux's xHCI controller quirks: the survey

Design record for roadmap-hcd task 38.11, its first stage: the survey of
every quirk Linux's xHCI host driver applies by controller identity, read
against this driver's code, with a proposed gate and default for each.
Revision 0, 2026-10-13, a draft written before the owner's decisions and
before any review. Nothing here is built: the owner decides which quirks
are taken and each default (section 7), and only then is the record
reviewed, and only after that is any code written (owner, 2026-10-13:
survey only at this stage).

## 1. What is asked, and what is not

Task 38.11 asks for Linux's xHCI controller quirks in the driver, each
behind a registry gate for the controllers Linux names. Today the driver
carries only quirks of its own: the routing quirk `XhciIntelIrqRoute`
(36.3), the port switchover `XhciIntelPortSwitch` (design record 16) and
the interval cap `XhciIntervalCap` (design record 17 section 4.7), with
Linux's soft-retry exclusion list folded into record 17's soft retry and
QEMU's stream-context quirk matched on QEMU's id alone (record 17 section
4.12.4). `XHCIQUAL` already names Linux's quirks for the controllers in
its table, and Linux is the stated authority for that table
(`xhciqual/quirks.c`'s header; `legal-provenance.md`, the mirrors).

This record is the survey the task opens with. For every quirk flag
Linux's xHCI host driver sets by vendor and device id it says what the
controller does wrong, which controllers, whether it reaches this
driver's targets and code paths at all, whether the driver already
covers it another way, and the gate and default proposed. Section 3
groups the results; section 4 takes each candidate in turn; section 5 is
what is common to all of them; section 7 is what the owner decides.

What it is not:

- **Not an implementation.** No code, no INF line and no host test is
  proposed in final form; section 4 says where in this driver each
  candidate would act, so the owner can weigh its cost.
- **Not a copy.** The survey is facts with `file:line` citations at the
  pinned commit. No Linux code is copied into this record. The
  implementation stage may adapt Linux's code under the conditions of
  `legal-provenance.md`'s exception for Linux's xHCI quirks (section 5.6
  here); nothing in this record relies on that exception.
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
  are not in it. The tables below give those values marked **(h)**: the
  ones `xhciqual/quirks.c` already carries (1033, 1022, 1912, 1B21, 1106,
  8086, 8086:1E31) rest on that file's 2026-09-17 audit, which its header
  says was not re-read from the mirror; the others (1002, 104C, 14E4,
  177D, 1D17, 17CD, 17CD:0200) are read from no source in this tree and
  are to be confirmed against `pci_ids.h` at the same commit before any is
  matched (section 8 proposes adding that file to the mirror).
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

Three groups. **Candidates** apply to a target and a code path of this
driver and are not covered by anything it does today; section 4 takes
each. **Covered** apply, and the driver already does the equivalent, by
design or by a broader rule. **Not applicable** cannot arise here, with the
reason.

Ids are `vendor:device`, hexadecimal; **(h)** marks a value from
`pci_ids.h` (section 2.1). "Every" means vendor-wide.

### 3.1 Candidates: applies, not covered

| Id | Linux flag (or behaviour) | Set at | Controllers | What the controller does wrong | Linux acts at | This driver today | Proposed gate, default |
|---|---|---|---|---|---|---|---|
| Q1 | `XHCI_BROKEN_MSI` | `xhci-pci.c:256-284` | 1B73:1000 (FL1000/PDK), 1B73:1400, every revision | advertises MSI, raises none ("Fresco Logic confirms", `xhci-pci.c:275-278`) | `xhci-pci.c:149-154`: never tries MSI | Vista and 7 connect a message where Windows grants one (records 15 and 18; both INFs write `MSISupported` 1); the dead-line rule is the line's only (record 15) | `XhciQuirkBrokenMsi`, 1 |
| Q2 | `XHCI_BROKEN_STREAMS` | `xhci-pci.c:273-274`, `286-288`, `423`, `444-446`, `460` | 1B73:1000, 1B73:1009, 1B6F:7023, 1B6F:7052, 1106:3432 **(h)**, 1B21:1042 **(h)** | streams "broken beyond repair" (`xhci.h:1616`) | `xhci-pci.c:664`, `xhci.c:3638-3643`: no streams | streams wherever HCCPARAMS1.MaxPSASize > 0 (`hcd_cfg.c` `hcdCfgStreamsOpen`; the transport decision of `xhci_xport.h`) | `XhciQuirkBrokenStreams`, 1 |
| Q3 | `XHCI_RESET_ON_RESUME` | `xhci-pci.c:326-329`, `418-425`, `431-435`, `436-437`, `439-441`, `476-478`, `497-499` | 1022:15E5, 1B6F:7023, 1B6F:7052, 1912:0015, every 1106, 1DB7:DC27, 1B21:3042, 1D17:9202 **(h)** | state is not kept across a suspend | `xhci.c:1105-1108`: resume re-initializes | resume restores (CRS) when the suspend saved (FSC), else re-initializes (`xhci_init.c` `XhciResumeController`) | `XhciQuirkResetOnResume`, 1 |
| Q4 | `XHCI_SPURIOUS_REBOOT` | `xhci-pci.c:350-370` | 8086:1E31 **(h)**, 8086:9C31, 8086:9CB1 | a powered-off machine powers back on (`xhci-pci.c:355-362`) | `xhci.c:753-754`: at shutdown both port-routing registers written 0 (`pci-quirks.c:1143-1147`) | at shutdown hands back only the connectors a route moved, and with `XhciIntelPortSwitch` 0, the default since `2.3.0.0`, writes nothing (`hcd_power.c`, `HcdPswRelease`; record 16 section 7a) | `XhciQuirkSpuriousReboot`, 1 |
| Q5 | `XHCI_SPURIOUS_WAKEUP` | `xhci-pci.c:365-370` | 8086:9C31, 8086:9CB1 | spurious wakes at shutdown ("with HSW", `xhci.c:770-772`, `xhci-pci.c:947`) | `xhci.c:774-776`: HCRST after the shutdown's halt; `xhci-pci.c:948-949`: the function put in D3hot | the shutdown's D3 halts and saves, no HCRST (`XhciSuspendController`) | `XhciQuirkSpuriousWakeup`, 1 |
| Q6 | `XHCI_RESET_TO_DEFAULT` | `xhci-pci.c:398-402` | 8086:A0ED, 8086:51ED, 8086:54ED | firmware boot delay when ports are left in U3 at shutdown (`xhci-pci.c:898-905`) | `xhci.c:774-776` (HCRST at shutdown); `xhci-pci.c:890-936` (at hibernate, a port in U3 without wake disabled) | as Q5; ports reach U3 only under selective suspend (`XhciSelectiveSuspend`, default 0, record 14) | `XhciQuirkResetToDefault`, 1 |
| Q7 | `XHCI_SUSPEND_DELAY` | `xhci-pci.c:315-320` | 1022:145C, 1022:15E0, 1022:15E1, 1022:43BB | needs a pause before Run/Stop is cleared at suspend | `xhci.c:985-986`: 1 to 1.5 ms before the halt | halts at once (`XhciSuspendController` -> `XhciQuiesceController`) | `XhciQuirkSuspendDelay`, 1 |
| Q8 | `XHCI_SLOW_SUSPEND` | `xhci-pci.c:265-272` | 1B73:1000 revision 04 | halts slowly at suspend ("extra slowly") | `xhci.c:1004-1007`: the suspend's halt bound times 10 (from `XHCI_MAX_HALT_USEC * 2`, `xhci.c:954`, `xhci-ext-caps.h:12`) | the halt bound is `XHCI_HALT_TIMEOUT_MS`, 32 ms (`xhci_init.c`), on every path | `XhciQuirkSlowSuspend`, 1 |
| Q9 | `XHCI_ASMEDIA_MODIFY_FLOWCONTROL` | `xhci-pci.c:472-474` | 1B21:1142 **(h)** (ASM1042A) | needs a vendor flow-control setting after each reset | `xhci.c:219-220` (after every HCRST), `xhci.c:1275-1276` (resume): `pci-quirks.c:593-634`, a vendor write through configuration offsets E0h, F8h and FCh | no such write | `XhciQuirkAsmediaFlowControl`, 1 |
| Q10 | `XHCI_LIMIT_ENDPOINT_INTERVAL_7` | `xhci-pci.c:480-481` | 104C:8241 **(h)** | periodic Interval 7 or above fails at High Speed and above | `xhci-mem.c:1452-1463`: interrupt and isochronous, High Speed and above, Interval 7 or more programmed as 6 | no cap on 104C (`xhci_tol.c` `XhciTolCapApplies` is vendor 1022 only) | `XhciQuirkInterval7`, 1 |
| Q11 | `XHCI_LIMIT_ENDPOINT_INTERVAL_9` on one more id | `xhci-pci.c:304-306` | 1002:7316 **(h)** (ATI Navi10) | periodic Interval 9 or above fails | `xhci-mem.c:1452-1458`: Interval 9 or more programmed as 8 | `XhciIntervalCap` covers vendor 1022 only; 1002 is another vendor | widen `XhciIntervalCap` 1 to this id; no new value |
| Q12 | `XHCI_MISSING_CAS` | `xhci-pci.c:390-396` | 8086:22B5, 8086:9D2F, 8086:A12F, 8086:5AA8, 8086:19D0 | a USB3 port stuck in Polling or Compliance after resume without setting CAS | `xhci-hub.c:1885-1896` with `xhci-hub.c:1820-1841`: at bus resume a port with CCS and CAS clear and PLS Polling or Compliance is warm-reset | a restore marks every root port changed (`hcd_dev.c`, `XhciRootHubInit`), and the thread's reading warm-resets one in Compliance (`xhci_link.c`, 29-A.2): that half is covered. A port in Polling is waited on (`XHCI_LINK_ACT_WAIT`), so one stuck there is never reset | `XhciQuirkMissingCas`, 1 |
| Q13 | `XHCI_TRB_OVERFETCH` | `xhci-pci.c:448-451`, `497-504` | 1106:3483 **(h)** (VL805), 1D17:9202 **(h)**, 1D17:9203 **(h)** | prefetches past the end of a ring segment | `xhci-mem.c:2441-2447`: every segment allocated with a second segment's space after it | the controller common buffer has driver memory after every ring (record 04 section 4); a stream block ends on its last stream ring (`xhci_stream.c` `XhciStreamLayout`, `XhciStreamLayoutPlaced`; `hcd_dma.c` `HcdDmaStreamAlloc`) | `XhciQuirkTrbOverfetch`, 1 |
| Q14 | `XHCI_ETRON_HOST` | `xhci-pci.c:418-425` | 1B6F:7023 (EJ168), 1B6F:7052 (EJ188) | (a) a SuperSpeed control transfer whose Setup and Data stages a Link TRB separates; (b) a Reset Device command; (c) on an isochronous ring, a Success event after a Transaction Error, Babble or Isoch Buffer Overrun | (a) `xhci-ring.c:3772-3784`: a No Op TRB first so the stages stay together; (b) `xhci.c:3974-3986`: Disable Slot and a new slot instead; (c) `xhci-ring.c:2593-2597`: the Success taken as spurious | (a) a control TD may cross the Link TRB (`xhci_ring.c` `XhciRingEnqueueTdGroup`); (b) `RESET_PORT` issues Reset Device (`hcd_enum.c`, the `XhciTrbResetDevice` call); (c) an event with no TD is unattributed and an isochronous one cycles nothing (`xhci_tol.c` `XhciTolCycleReason`) - not confirmed against this case | `XhciQuirkEtron`, 1 |
| Q15 | the forced BIOS handoff | `pci-quirks.c:1190-1196` | 104C:8241 **(h)**, 1912:0014 **(h)** | "Auto handoff never worked for these devices" | OS Owned set and BIOS Owned cleared in one write, before the wait | waits up to `XHCI_HANDOFF_TIMEOUT_MS` (1000 ms) for BIOS Owned to clear, then proceeds with it set, SMIs disabled either way (`xhci_init.c` `xhciBiosHandoff`) | `XhciQuirkForceHandoff`, 1 |
| Q16 | `XHCI_DISABLE_SPARSE` | `xhci-pci.c:326-329` | 1022:15E5 | (none stated in the mirror) | `xhci-pci.c:802-809`, `837-838`: before each suspend, bit 17 of BAR offset C12Ch cleared | no such write | `XhciQuirkDisableSparse`, 1 |
| Q17 | `XHCI_SSIC_PORT_UNUSED` | `xhci-pci.c:382-384` | 8086:22B5 | does not enter D3 unless its SSIC ports are marked unused (`xhci-pci.c:748-754`) | `xhci-pci.c:755-784`, `834-835`, `845-846`, `881-882`: vendor registers at BAR offset 880Ch + 30h * i, around D3 | no such write | `XhciQuirkSsicPortUnused`, not taken (section 4.17) |
| Q18 | `XHCI_SW_BW_CHECKING` | `xhci-pci.c:354` | 8086:1E31 **(h)** | does no periodic bandwidth check of its own | `xhci.c:2996-3003` and Linux's bandwidth tables: software admission | the controller's own admission (Configure Endpoint's Bandwidth Error) | `XhciQuirkSwBwChecking`, not taken (section 4.18) |
| Q19 | `XHCI_AMD_PLL_FIX` | `xhci-pci.c:311-313` | every 1022 xHCI on an SB700 (revision 3Bh or below), SB800, Hudson-2 or Bolton chipset, told by the SMBus device's id and revision (`pci-quirks.c:153-194`, `209-287`) | isochronous streams stutter when the A-link powers down (`pci-quirks.c:330-339`) | `xhci-ring.c:4209-4212`, `xhci-ring.c:832-837`: chipset power management held off while isochronous requests are queued (`pci-quirks.c:340-455`) | nothing | `XhciQuirkAmdPllFix`, not taken (section 4.19) |
| Q20 | `XHCI_CDNS_SCTX_QUIRK` | `xhci-pci.c:506-508` | 17CD:0200 **(h)** (Cadence USBSSP) | keeps state in a stream context's reserved fields, which Set TR Dequeue does not clear, and skips the ring (`xhci-ring.c:1490-1498`) | `xhci-ring.c:1499-1502`: the two reserved dwords cleared | nothing | `XhciQuirkCdnsSctx`, not taken (section 4.20) |

### 3.2 Covered already

| Linux flag (or behaviour) | Set at | Controllers | What Linux does | How this driver covers it |
|---|---|---|---|---|
| `XHCI_SPURIOUS_SUCCESS` | `xhci.c:5482-5487`; `xhci-pci.c:453-461` | every controller above 0.96; 1B21:1042 | `xhci-ring.c:2587-2592`: a Success after a Short Packet on the same TD ignored | on every controller: the TD engine takes the second event the specification allows after a short packet, and the spurious one on the TD's last TRB, as known and counted (`xhci_xfer.c`, the short-packet tail handling, `MidTdDeferralsTailedSpurious`) |
| `XHCI_INTEL_HOST`, its reset delay | `xhci-pci.c:345-349` | every 8086 **(h)** | `xhci.c:205-213`: 1 ms after HCRST before any register access | on every controller: `xhci_init.c` `xhciReset` stalls 1 ms after the HCRST write. Its other uses (LPM timeouts, `xhci.c:4906-4907`, `4970-4971`, `5054`; eUSB2 double-bandwidth isochronous Mult, `xhci-mem.c:1351-1354`) are features this driver does not use |
| `XHCI_AVOID_BEI` | `xhci-pci.c:345-349` | every 8086 **(h)** | `xhci-ring.c:4048-4049`: BEI cleared on every eighth isochronous TD | BEI is never set (`xhci_xfer.c`, the isochronous TD builder: every TD carries IOC) |
| `XHCI_NO_SOFT_RETRY` | `xhci-pci.c:418-425`, `488-491` | 1B6F:7023, 1B6F:7052, 1022:43B9, 1022:43BB | `xhci-ring.c:2529-2533`: no soft retry | the same four ids are excluded from 35-T.2's soft retry (`xhci_tol.c` `XhciTolNoSoftRetry`; record 17 section 4.2) |
| `XHCI_LIMIT_ENDPOINT_INTERVAL_9` on the AMD ids | `xhci-pci.c:293-302` | 1022:13ED, 13EE, 148C, 15D4, 15D5, 15E0, 15E1, 15E5 | `xhci-mem.c:1452-1458`: interrupt and isochronous Interval 9 or more programmed as 8 | `XhciIntervalCap` 1 caps every 1022 controller, wider than Linux's list, interrupt endpoints only: isochronous ones are not capped, by the owner's decision of 2026-10-06 (record 17 section 4.7). Q11 is the one id it misses |
| `XHCI_EP_LIMIT_QUIRK` | `xhci-pci.c:350-353` | 8086:1E31 **(h)** | `xhci.c:2264-2282`, `4202-4214` and the release paths: at most 64 active endpoint contexts, EP0s included | the policy limits hold the driver at 64: 32 slots each with one EP0 (`XHCI_MAX_SLOTS`) and 32 pooled rings for every other endpoint (`XHCI_MAX_POOL_RINGS`, `xhci_mem.c` `XhciPoolAcquire`, called from `hcd_cfg.c`). A host assertion that no endpoint context is ever live without a pool ring is proposed at implementation, so the bound is proved rather than read |
| `XHCI_SNPS_BROKEN_SUSPEND` | `xhci-pci.c:322-324` | 1022:15E0, 1022:15E1 | `xhci.c:1025-1046`: a save that never clears SSS accepted, and the resume re-initializes | a save that does not complete leaves no valid state (`xhci_init.c` `xhciSaveState`), and the resume then re-initializes - the same end |
| `XHCI_NO_64BIT_SUPPORT` | `xhci-pci.c:462-470` | 1B21:1142, 1242, 2142, 3242 | `xhci.c:5515-5516`: AC64 ignored, 32-bit DMA | every address is below 4 GB and every high dword written 0 (AGENTS.md, "No 64-bit DMA") |
| `XHCI_U2_DISABLE_WAKE` | `xhci-pci.c:334-339` | 1022:43B9, 43BA, 43BB, 43BC | `xhci-hub.c:1776-1781` with `pci-quirks.c:506-589`: no wake bits on a USB 2.0 port the firmware disabled | no port wake bit is ever set (`PORTSC` WCE, WDE and WOE are defined in `xhci.h` and written by nothing), and the controller never arms system wake (section 3.4) |
| `XHCI_ZHAOXIN_HOST`, its port protocol | `xhci-pci.c:493-495` | every 1D17 **(h)** | `xhci-mem.c:2060-2066`, `2113-2116`: a USB 3.1 Minor Revision taken as 3.0 unless a PSI names Gen 2 | speeds come from each port's PSIV and PSI table, never from the Minor Revision (`xhci_caps.c`, `xhciDefaultKilobits`). Its LPM uses (`xhci.c:4906`, `4970`, `5056`) are unused here |
| The Intel port switchover | `pci-quirks.c:1058-1147`, `1224-1225`; `xhci-pci.c:878-879` | every 8086 **(h)** with the routing registers | connectors routed to xHCI at handoff and at each resume | `XhciIntelPortSwitch` (record 16), off by default by the owner's decision of 2026-10-09 |

### 3.3 Not applicable

| Linux flag (or behaviour) | Set at | Why it cannot arise here |
|---|---|---|
| `XHCI_LINK_TRB_QUIRK` | `xhci.c:5489-5492` | HCIVERSION 0.95 only, and only with a module parameter that defaults to 0. This driver refuses a controller below 1.0 (`xhci_caps.c` `XhciCheckCapDword0`, `XHCI_HC_BAD_VERSION`) |
| `XHCI_AMD_0x96_HOST` | `xhci-pci.c:308-309` | HCIVERSION 0.96 only (its use, `xhci.h:1782-1786`: isochronous Link TRBs kept chained); refused by the same floor |
| `XHCI_NEC_HOST` | `xhci-pci.c:290-291` | every 1033 **(h)**: a vendor Get Firmware command at start whose answer is only logged (`xhci.c:652-663`, `xhci-ring.c:1683-1694`, `1935-1941`), and isochronous Link TRBs kept chained for "0.96 hardware from AMD and NEC" (`xhci.h:1779-1786`). The command is diagnostic; the chaining is for a 0.96 part, which the floor refuses. The uPD720200's HCIVERSION is not in the mirror; `XHCIQUAL` prints it, and a 1.0 reading would move the chaining to the candidates |
| `XHCI_SPURIOUS_SUCCESS` and `XHCI_BROKEN_STREAMS` on 1B21:1042 | `xhci-pci.c:453-461` | "reports 0.96" (`xhci-pci.c:455-458`): refused by the floor. Q2 keeps the id in its list at no cost |
| `XHCI_RESET_EP_QUIRK`, `XHCI_TRUST_TX_LENGTH`, `XHCI_PLAT` | `xhci.h:1589`, `1607`, `1613` | deprecated; the first is set for 1B73:1000 revision 00 with a trace that says so (`xhci-pci.c:259-264`) and acts nowhere; the other two are set by no file in the mirror |
| `XHCI_LPM_SUPPORT`, `XHCI_HW_LPM_DISABLE` | `xhci-pci.c:345-349`, `448-451`, `493-495` | link power management: this driver programs no U1 or U2 timeout and no USB 2.0 LPM (no `PORTPMSC` write anywhere in `src`) |
| `XHCI_PME_STUCK_QUIRK` | `xhci-pci.c:371-381` | clears a PME latch so wake works (`xhci-pci.c:786-800`); the controller never arms wake here (section 3.4) |
| `XHCI_DEFAULT_PM_RUNTIME_ALLOW` | `xhci-pci.c:331-332`, `404-416`, `510-512` | Linux's runtime power policy (`xhci-pci.c:670-673`, `729-732`); no counterpart here |
| `XHCI_INTEL_USB_ROLE_SW` | `xhci-pci.c:385-389` | creates Linux's dual-role switch device (`xhci-ext-caps.c:95-100`); no counterpart here |
| `XHCI_BROKEN_D3COLD_S2I` | `xhci-pci.c:341-343` | suspend-to-idle only (`xhci-pci.c:824-829`), a Linux sleep state no target has |
| `XHCI_ZERO_64B_REGS` | `xhci-pci.c:427-435` | acts only behind a translating IOMMU (`xhci.c:257-264`); no target remaps this driver's DMA through one, as far as this survey read, and the driver writes every high dword 0 |
| `XHCI_RESET_PLL_ON_DISCONNECT` | `xhci-pci.c:483-486` | 14E4 **(h)** or 177D **(h)** device 9026: a PHY reset through vendor registers on a USB 2.0 disconnect (`xhci-ring.c:1975-1990`, `2134-2139`). The vendor pair is Broadcom and Cavium; the mirror names no platform, and no target has been read on one. Listed so a reading on an x86 machine would move it |
| `XHCI_COMP_MODE_QUIRK` | `xhci.c:567-571` | keyed on DMI strings (HP Z420, Z620, Z820, Z1 Workstation, `xhci.c:425-450`), not on a PCI id, so outside the task's matching. Its symptom - a port in Compliance Mode with no change event - is partly covered: 29-A.2 warm-resets Compliance when a change shows it |
| `XHCI_MTK_HOST`, `XHCI_SKIP_PHY_INIT`, `XHCI_SUSPEND_RESUME_CLKS`, `XHCI_WRITE_64_HI_LO`, `XHCI_BROKEN_PORT_PED`, `XHCI_SG_TRB_CACHE_SIZE_QUIRK`, `XHCI_EP_CTX_BROKEN_DCS` | `xhci.h:1619`, `1636`, `1642`, `1646`, `1624`, `1638`, `1641` | set by no file in the mirror: the platform glue sets them, or (`XHCI_EP_CTX_BROKEN_DCS`) no mainline file the mirror carries. Platform glue is not a PCI controller this driver binds |
| The Renesas firmware load | `xhci-pci-renesas.c:579-626`; `xhci-pci.c:692-710` | a ROM-less uPD720201 or uPD720202 needs firmware the OS loads. That firmware is a third-party binary this project does not carry (AGENTS.md, "Never track a third-party document or binary"); a card with its ROM works without it. `XHCIQUAL` already reports the case (`QF_FW_UPLOAD`) |
| The Promontory 21 parts | `xhci-pci.c:699-714`, `xhci-pci.h:7-8` | 1022:43FC and 1022:43FD go to a glue driver the mirror lacks; nothing to survey (record 17 section 1 has the context) |

### 3.4 What the driver does that decides the groups

Read in `src` on branch `phase-38` at `0976a08a`, each by file and function:

- **Identity.** The PCI vendor and device dword (offset 0) is read once at
  each start into `XHCI_EXTENSION.PciVendorDevice` (`xhci_init.c`,
  `XhciInitController`); the revision (offset 08h) is not read. HCIVERSION
  is read and anything below 1.0 refused (`xhci_caps.c`,
  `XhciCheckCapDword0`).
- **Settings.** Every value lives in the controller's driver key
  (`hcd_ctl.c`, `hcdReadDword`, `IoOpenDeviceRegistryKey` with
  `PLUGPLAY_REGKEY_DRIVER`), read at each start, a REG_DWORD of four bytes
  only. Today's id gates are `XhciIntelPortSwitch`, `XhciIntelIrqRoute`
  and `XhciIntervalCap`, each 0, 1 or 2 with 1 the listed controllers
  (`hcd_ctl.c` names; `hcdTolApplies` is where the cap and QEMU's
  stream-context quirk are settled after the id is read).
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

## 4. The candidates

Each: what Linux does, where this driver would act, the change, its gate
and default, its cost, and what the reports show. The behaviour named is
Linux's as read; the implementation checks each against xHCI 1.2c before
it is written (task 38.11).

### 4.1 Q1, broken MSI (Fresco Logic)

Linux never asks for MSI on 1B73:1000 or 1B73:1400 and takes the line.
Here the question arises only on Vista and 7, where a message is
connected when Windows grants one. On a listed controller the message
would never arrive, and the dead-line watcher does not watch a message;
only 35-T.1's lost-interrupt backstop would drain the ring, each event at
least 100 ms and typically 300 to 400 ms late (record 17 section 4.1).

The change: on a listed controller the requirements filter of record 18
section 4.2 keeps only the line configurations, as `XhciInterruptMode` 1
and the no-helper x86 path already make it do, and mode 0 then takes the
line, or polls where there is none - not mode 1's refusal. Gate
`XhciQuirkBrokenMsi`: 0 off, 1 (default) the listed ids. No 2: forcing the
line everywhere is `XhciInterruptMode` 1's job. Cost: one id test in the
filter. 98 SE to XP unchanged.

### 4.2 Q2, broken streams

Linux offers no streams on six ids. Here the controller's MaxPSASize is
read in two places, and both would read it as 0 on a listed controller:
the transport decision (UAS at SuperSpeed only where the controller
streams; `xhci_xport.h`) and `OPEN_STREAMS` (`hcdCfgStreamsOpen`, which
already refuses with `XHCI98_STREAMS_NO_CONTROLLER`). A UAS device that
also offers Bulk-Only takes Bulk-Only; a UAS-only device at SuperSpeed goes
back to USB 2.0 by 29-A.5, where streamless UAS runs. Gate
`XhciQuirkBrokenStreams`: 0 off, 1 (default) the listed ids, 2 every
controller (a tester's way to take streams out of a report). Cost: the
pure decision gains one input; host tests of both paths.

### 4.3 Q3, reset on resume

Linux re-initializes these controllers at every resume. Here the resume
restores when the suspend saved; on a listed controller it would skip
the restore and take the existing re-initialization, the path a failed
restore already takes (`XhciResumeController`), which drops every device
and re-enumerates it. The save itself can be skipped too, so CSS is never
asked of a controller whose restore is not believed. Gate
`XhciQuirkResetOnResume`: 0 off, 1 (default) the listed ids (VIA vendor-
wide, section 5.2), 2 every controller. Cost: one test before the restore;
the user-visible cost on a listed controller is that a device in use
across standby or hibernate is re-enumerated, as on any controller
without FSC today.

### 4.4 Q4, the spurious reboot (Panther Point, Lynx Point-LP, Wildcat Point-LP)

Linux writes both routing registers (`USB3_PSSEN` D8h, `XUSB2PR` D0h) to 0
at every shutdown on 8086:1E31, 9C31 and 9CB1, so the connectors are
EHCI's when the machine powers off: on some 7-series desktop boards a
machine left with them on xHCI powers itself back on. Here the shutdown's
release writes back only what a route of this driver wrote, so with
`XhciIntelPortSwitch` 0, the default, connectors the firmware routed to
xHCI stay there through the shutdown.

The change: at the shutdown's D3 (not at a sleep's), on a listed
controller, both registers are written 0 whatever the switch, through the
existing config-space path (`HcdSvcConfigSpace`). Gate
`XhciQuirkSpuriousReboot`: 0 off, 1 (default) the listed ids; no 2, since
the registers exist only on the PCH generations record 16 lists. Cost: the
write already exists; record 16 section 5's sentence "0 ... makes no
switchover write" gains an exception (decision D8). The project's B490
(1E31) and E7250 (9CB1) are listed.

### 4.5 Q5, the spurious wakeup (Lynx Point-LP, Wildcat Point-LP)

Linux resets the controller (HCRST) after the shutdown's halt and puts the
function in D3hot. Here the shutdown's D3 halts and saves; the change is
an HCRST after that halt on a listed controller, at the shutdown only.
Whether the PCI bus driver of each target then sets D3hot when the D3
IRP reaches it is the operating system's and is not read; the HCRST is
the part this driver owns. Gate `XhciQuirkSpuriousWakeup`: 0 off, 1
(default) the listed ids, 2 every controller. Cost: one bounded HCRST at
shutdown; the save that preceded it is then stale, which does not matter
at a shutdown. The E7250 (9CB1) is listed.

### 4.6 Q6, reset to default (Tiger Lake PCH, Alder Lake PCH and N)

Linux does Q5's HCRST at shutdown on these, and at hibernate disables a
root port left in U3 with no wake armed, because firmware then boots
slowly. Here only selective suspend puts a port in U3 (off by default),
and wake is never armed. Proposed: Q5's HCRST, and at a hibernate's D3 a
root port reading U3 is disabled (PED written 1) before the halt. Gate
`XhciQuirkResetToDefault`: 0 off, 1 (default) the listed ids. Cost: small;
the port part is reached only with `XhciSelectiveSuspend` 1.

### 4.7 Q7, the suspend delay (AMD)

Linux sleeps 1 to 1.5 ms before clearing Run/Stop at suspend on 1022:145C,
15E0, 15E1 and 43BB. Here the stop would wait 1.5 ms before
`XhciQuiesceController` on a listed controller, at PASSIVE_LEVEL in the
power path. Gate `XhciQuirkSuspendDelay`: 0 off, 1 (default) the listed
ids. Cost: 1.5 ms per D3.

### 4.8 Q8, the slow suspend (Fresco Logic FL1000)

Linux allows ten times its usual 64 ms for the suspend's halt on
1B73:1000 revision 04. Here every halt waits at most 32 ms
(`XHCI_HALT_TIMEOUT_MS`); a halt that times out at suspend is counted
(`SuspendFailures`) and leaves the controller unsaved and possibly running
into D3. The change: on a listed controller the suspend's halt waits up to
640 ms. The wait ends as soon as HCH reads 1, so a longer bound costs a
healthy controller nothing, which is why section 5.3 proposes matching
every revision of 1B73:1000 rather than reading the revision for this one
row. Gate `XhciQuirkSlowSuspend`: 0 off, 1 (default) the listed id, 2
every controller.

### 4.9 Q9, the ASMedia flow control (ASM1042A)

Linux performs a vendor write after every HCRST and at every resume on
1B21:1142: wait for the write-busy bit of configuration byte E0h to clear,
write a command and an address to F8h and FCh, set the write bit in E0h,
wait again, write the data, set it again (`pci-quirks.c:593-634`; the
values are constants there). Here it would follow `xhciReset` at each
start, recovery and resume, through the configuration-space path. It is
a second kind of configuration write above the standard header, beside the
Intel switchover's, so `implementation-invariants.md` changes with it.
Gate `XhciQuirkAsmediaFlowControl`: 0 off, 1 (default) the listed id; no 2,
since the registers are this vendor's. Cost: a bounded poll (Linux allows
1000 x 50 us) and four config writes per reset.

### 4.10 Q10, the TI interval limit (TUSB73x0)

Linux programs a periodic endpoint's Interval as 6 where it would be 7 or
more, at High Speed and above, on 104C:8241. Here that is
`XhciTolCapInterval`'s shape with another threshold and another id; the
cap's ordering against fast polling (record 17 section 4.7) carries over.
Record 17 caps interrupt endpoints only and leaves isochronous ones, whose
interval sets their data rate; Linux caps both here. Decision D13 asks
which. Gate `XhciQuirkInterval7`: 0 off, 1 (default) the listed id.

### 4.11 Q11, one more interval-9 id (ATI Navi10)

Linux caps Interval at 8 on 1002:7316, a controller of another vendor id
than `XhciIntervalCap`'s. Proposed: `XhciIntervalCap` 1 covers this id as
well, no new value; its 0 and 2 unchanged. Cost: one id in
`XhciTolCapApplies`.

### 4.12 Q12, the missing CAS (Intel Atom, Sunrise Point, Apollo Lake, Denverton)

Linux, at each bus resume, warm-resets a USB3 root port whose CCS and CAS
are both clear while its link reads Polling or Compliance, because these
controllers can leave a port so after resume without the CAS that would
announce it. Here a resume that restored marks every root port changed
(`XhciRootHubInit`), so the thread reads each, and 29-A.2 warm-resets one
in Compliance whatever its CAS: that half is covered. A port in Polling is
waited on as a link still training (`xhci_link.c`, `XHCI_LINK_ACT_WAIT`),
and one stuck there raises no further change, so it is never reset.
Proposed: on a listed controller, a USB3 root port read with CCS and CAS
clear and PLS Polling at that post-resume reading is warm-reset within
29-A.2's existing budget (the re-initializing path starts from HCRST and
needs nothing). Gate
`XhciQuirkMissingCas`: 0 off, 1 (default) the listed ids, 2 every
controller. The project's E460 (9D2F) is listed.

### 4.13 Q13, TRB overfetch (VIA VL805, Zhaoxin)

Linux gives every ring segment a second segment's worth of space after it
on these. Here the controller common buffer already has driver memory
after every ring (record 04 section 4: the last region is the scratchpad
pages); the one place a ring ends at the end of an allocation is a stream
block, whose stream rings come last (`XhciStreamLayout`,
`XhciStreamLayoutPlaced`). Proposed: on a listed controller a stream block
is allocated one ring longer. Gate `XhciQuirkTrbOverfetch`: 0 off, 1
(default) the listed ids, 2 every controller. The VL805 has a field user
(issue 13) and does streams (MaxPSASize 4, issue 13 section 4).

### 4.14 Q14, Etron

Linux gives the EJ168 and EJ188 four bits: Q2's and Q3's lists include
them, the soft-retry exclusion is covered (section 3.2), and
`XHCI_ETRON_HOST` does three things:

- (a) a SuperSpeed control transfer whose Setup TRB would be the last
  before the Link TRB gets a No Op TRB first, so Setup and Data are not
  split by the link. Here the control TD is enqueued as one group that may
  cross the link (`XhciRingEnqueueTdGroup`); the change is a Transfer No Op
  at the ring's last usable position when the next TD is a SuperSpeed
  control TD. Small, in the pure ring core, host-testable.
- (b) Reset Device replaced by Disable Slot and a fresh slot. Here
  `RESET_PORT` (the class drivers' recovery) issues Reset Device and then
  re-addresses into the same records (`hcd_enum.c`). A new slot changes
  the Slot ID the device's records, its rings and its PDO's handles are
  keyed by; that is not small. Alternatives: refuse `RESET_PORT` on Etron
  so the class driver's recovery escalates to the device cycle of record 17
  section 4.3 (a replug done by the bus, which recreates the PDO), or do
  the slot swap properly.
- (c) on an isochronous ring, a Success event after a Transaction Error,
  Babble or Isoch Buffer Overrun on the same TD is ignored. Here such an
  event finds no TD and is unattributed, and an isochronous one cycles
  nothing (`XhciTolCycleReason`); that it is then only counted is to be
  confirmed at implementation with a host vector, not assumed.

`XHCIQUAL` marks both parts `QF_AVOID`, the project's own verdict on them.
Gate `XhciQuirkEtron`: 0 off, 1 (default) the listed ids. Decision D14
asks which of (a) to (c), and for (b) which form.

### 4.15 Q15, the forced handoff (TI TUSB73x0, Renesas uPD720201)

Linux writes OS Owned set and BIOS Owned clear in one write on 104C:8241
and 1912:0014, before its usual wait. Here the handoff sets OS Owned and
waits up to 1000 ms for BIOS Owned to clear, then goes on with it set and
the SMIs disabled (`xhciBiosHandoff`, which also says BIOS Owned is "read-
only from the OS side"). The xHCI 1.2c reading of that bit's access is to
be checked before this is written; if the OS may write it, the change is
the one write on a listed controller. Gate `XhciQuirkForceHandoff`: 0 off,
1 (default) the listed ids. What it buys: up to a second per start and a
log free of `handoff.bios.held` on these parts; SMIs are already off.

### 4.16 Q16, the sparse control (AMD Raven2)

Linux clears bit 17 of BAR offset C12Ch before each suspend on 1022:15E5.
The mirror says nothing of what the bit does. Here it would be one
read-modify-write in the D3 path on a listed controller. Gate
`XhciQuirkDisableSparse`: 0 off, 1 (default) the listed id. Q3 also lists
15E5.

### 4.17 Q17, SSIC ports (Cherry Trail): recommended not taken

The controller may not reach D3 unless two vendor registers mark its SSIC
ports unused first. The cost of not doing it is power in a sleep state
on one Atom tablet platform, against vendor-register writes on every D3
and D0. Recommended: report only.

### 4.18 Q18, software bandwidth checking (Panther Point): recommended not taken

Linux keeps its own periodic bandwidth tables for 8086:1E31 because the
controller admits schedules it cannot serve. Taking it means
transcribing Linux's bandwidth model (`xhci.c:2996-3003` and the
reservation code behind it), a design of its own. Without it an
over-subscribed periodic schedule on a Panther Point machine can lose
data silently. Recommended: not in this task; report the id, and if a
report shows it, open a task of its own.

### 4.19 Q19, the AMD PLL fix: recommended not taken

The fix finds the chipset by the SMBus device's id and revision, and while
isochronous requests are queued writes power-management registers of the
southbridge through I/O ports CD6h and CD7h and an index pair, and of the
northbridge through its configuration space (`pci-quirks.c:340-455`). It
needs port-I/O imports, and `HAL.dll!WRITE_PORT_UCHAR` is the one import
that separates the `qemu` flavour from the shipping ones, kept out of them
because defect 2b's cause is not established (AGENTS.md, "Build
flavours"). It writes devices that are not the xHCI function. And whether
these FCH xHCI parts report HCIVERSION 1.0, which this driver needs, is not
in the mirror. Recommended: not taken; `XHCIQUAL` to name the chipset case.

### 4.20 Q20, Cadence stream context: recommended not taken

17CD:0200 is Cadence's USBSSP PCI part; no project target has been read on
one and no report names it. The fix is two dword writes in the Set TR
Dequeue completion of a stream, beside QEMU's stream-context quirk.
Recommended: not taken; cheap to add if a report names it.

## 5. What is common to every quirk taken

### 5.1 The registry values

Proposed: one REG_DWORD per Linux behaviour taken, named
`XhciQuirk<Name>` after the Linux flag, in the controller's driver key,
read at each start with today's `hcdReadDword`, and following the house
form of `XhciIntervalCap` and `XhciIntelIrqRoute`:

| Value | Meaning |
|---|---|
| 0 | off: Linux's behaviour is not applied, on any controller |
| 1, absent, another type or any other number | Linux's listed controllers (the default) |
| 2 | every controller, where section 4 offers it; elsewhere read as 1 |

Absent means 1, unlike `XhciIntelPortSwitch`: text-mode Setup's F6 install
writes no values (`txtsetup.oem`), and a quirk a listed controller needs
there should apply there. Both INFs write each value at its default with
`0x00010003` under task 34.1's rule, with a `VAL-*` row in the INF gate;
the footprints, `README.md` and the release notes name each with its
controllers.

The alternatives are in decision D1.

### 5.2 Matching

Each quirk's id list is a table in the pure core, matched against
`PciVendorDevice` (device << 16 | vendor), with a host test of the match
per quirk: every listed id matches, the neighbours and the other vendors'
same device numbers do not, and the vendor-wide rows (Q3's every 1106)
match every device of that vendor. HCIVERSION enters nowhere: the two
Linux bits keyed on it are below the floor (section 3.3). The revision is
proposed read (configuration byte 08h) at each start and logged, and
matched by no quirk (section 4.8; decision D3).

### 5.3 Revision matching

Linux matches a revision twice, both on 1B73:1000: revision 00 for a
deprecated bit that does nothing, and revision 04 for Q8. Q8's longer
halt bound costs a healthy controller nothing, so matching every revision
of 1B73:1000 serves revision 04 and needs no revision match in the
driver. The revision is still worth reading and logging, so a report
shows it.

### 5.4 What `XHCISNAP` reports

At each start, per quirk: the value read and whether it applied on this
controller, as a log note (`quirk.<name>`, value << 8 | applied) and as
fields of the counter block, beside 35-T.8's `CapMode` and `CapApplied`
(`xhcisnap.c` prints those as the `XhciIntervalCap` line); the counts that
say a quirk acted (Q8's long halts, Q12's warm resets, Q13's padded
blocks, Q5 and Q6's shutdown resets, which a later start cannot see
unless kept in the log ring flushed at shutdown). `XHCISNAP` prints a
"Linux quirks" block naming each value, its setting, and "applied" or
"not this controller".

### 5.5 What `XHCIQUAL` reports

`xhciqual/quirks.c` keys rows on an exact vendor and device, with a
16-bit flags word of which bits 0 to 10 are spoken for. The quirks taken
need: a flag each, which widens the word to 32 bits; rows for ids it lacks
(104C:8241, 1002:7316, 1022:145C, 1B21:3042, 1DB7:DC27, 8086:22B5,
8086:5AA8, 8086:19D0, 8086:A0ED, 8086:51ED, 8086:54ED, 1D17:9202,
1D17:9203, and each not-taken quirk's ids it should name); a vendor-wide
row form for VIA and Zhaoxin, which it lacks; and each printed quirk
naming the driver's registry value, so a user reading the qualifier's
report knows which switch it is. It already reads the revision and
HCIVERSION, so it can say when a listed part is below the driver's floor.

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
decisions. Every quirk's registry value is documented with the
controllers it was read on, and for nearly all of them that is none: the
project holds 9D2F (E460, Q12), 1E31 (B490, Q4), 9CB1 (E7250, Q4 and Q5)
and a field user holds a VL805 (Q3, Q13). Each such machine read under the
taken quirk is a leg; the others ship unread, and the release notes say
so.

## 7. Open for the owner

Each is a fork with a recommendation. D1 to D4 are cross-cutting; D5 on
are one per candidate (take it? default for the listed ids?).

**D1. One value per quirk, or one value for all?** (a) one REG_DWORD per
quirk, `XhciQuirk<Name>`, 0/1/2 as section 5.1; (b) one bitmask value,
`XhciLinuxQuirks`, a bit per quirk set to turn it off, plus a second for
"every controller"; (c) one master value 0/1 for all of them plus (a).
(a) is the house form, each switch documented beside its controllers and
gated by the INF gate; (b) is one INF line and one gate row but a hex
mask a user must compose; (c) adds an escape like `XhciTolerance`, at the
price of a second switch interacting with each. **Recommended: (a), no
master.**

**D2. The name.** `XhciQuirk<LinuxFlagInCamelCase>` (traceable to Linux:
`XhciQuirkBrokenStreams`, `XhciQuirkResetOnResume`), or names that say the
effect (`XhciNoStreamsListed`, `XhciResetOnResumeListed`). **Recommended:
the Linux-derived names**, since the README and `XHCIQUAL` will cite Linux
for each.

**D3. Revision matching.** (a) none: Q8 matches every revision of
1B73:1000, and the revision is read and logged at each start; (b) match
revision 04 as Linux does, which adds the revision to the matching input.
**Recommended: (a).**

**D4. The HCIVERSION floor.** The quirks for 0.95 and 0.96 controllers
(`XHCI_LINK_TRB_QUIRK`, `XHCI_AMD_0x96_HOST`, NEC's chaining) stay set aside
while `XhciCheckCapDword0` refuses those parts. **Recommended: set aside;
lowering the floor is not this task.**

**D5 (Q1) Broken MSI, 1B73:1000 and 1400.** Take; default 1.
**Recommended: take, 1.**

**D6 (Q2) Broken streams.** Take; default 1; offer 2. **Recommended:
take, 1.**

**D7 (Q3) Reset on resume.** Take; default 1; offer 2; skip the save too.
**Recommended: take, 1.**

**D8 (Q4) Spurious reboot.** Take; default 1. And: does the shutdown write
happen when `XhciIntelPortSwitch` is 0? (a) yes, as Linux, and record 16
section 5's "makes no switchover write" gains the exception; (b) only when
the switch is nonzero, which is today's behaviour for connectors this
driver routed and leaves firmware-routed ones on xHCI. **Recommended: take,
1, (a).**

**D9 (Q5) Spurious wakeup.** Take the HCRST at shutdown; default 1.
**Recommended: take, 1.**

**D10 (Q6) Reset to default.** (a) the HCRST only; (b) and the hibernate
port disable. **Recommended: take (a), 1; (b) only if selective suspend
is turned on by default later.**

**D11 (Q7, Q8, Q16) The AMD suspend delay, the Fresco slow suspend, the
Raven2 sparse control.** Take each; default 1. Each is a few lines in the
D3 path. **Recommended: take Q7 and Q8 at 1; Q16 at 1 as well, though the
mirror does not say what the bit does.**

**D12 (Q9, Q15) The ASMedia flow control and the forced handoff.** Take
each; default 1. Q9 adds a configuration write above the header, which
`implementation-invariants.md` records. Q15 waits on a spec check of BIOS
Owned's access. **Recommended: take Q9 at 1; take Q15 at 1 if the
specification lets the OS write the bit, else drop it.**

**D13 (Q10, Q11) The interval limits.** Take Q10 (104C:8241, Interval 7
and above to 6) and Q11 (1002:7316 under `XhciIntervalCap`). And for Q10:
(a) interrupt endpoints only, as record 17 decided for the AMD cap; (b)
interrupt and isochronous, as Linux. **Recommended: take both; Q10 (a).**

**D14 (Q14) Etron.** Take (a) the No Op before a split control TD, (c) the
spurious isochronous Success (confirmed or added); and for (b): (i) a real
Disable Slot and new slot inside `RESET_PORT`; (ii) refuse `RESET_PORT` on
Etron, so recovery escalates to the device cycle; (iii) leave Reset Device.
Default 1. **Recommended: take (a) and (c) at 1, (b)(ii)**, given
`XHCIQUAL`'s verdict that these parts are best avoided.

**D15 (Q12) Missing CAS.** Take; default 1; offer 2. **Recommended:
take, 1.**

**D16 (Q13) TRB overfetch.** Take, padding a stream block on a listed
controller; default 1; offer 2. Or pad every stream block on every
controller with no value at all, since it costs one ring of memory per
stream endpoint. **Recommended: take, gated, 1** (the task asks for a gate
per quirk).

**D17 (Q17 to Q20) Not taken.** SSIC ports, software bandwidth checking,
the AMD PLL fix and the Cadence stream context: report them in `XHCIQUAL`
and the README, take none. **Recommended: as stated; Q18 and Q19 each a
task of their own if a report needs one.**

## 8. What changes in the documents

At implementation, after the owner's decisions:

- `src/xhci98.inf` and `src/xhci98-amd64.inf`: a line per value; the INF
  gate's `VAL-*` table; the footprints.
- `README.md` and `docs/using/release-notes.md`: each value, its
  controllers, its default, and which were read on hardware.
- `docs/contributing/implementation-invariants.md`, "PnP Resources": Q4's
  shutdown write and Q9's vendor write as configuration writes above the
  header.
- Design record 16 section 5, if D8 (a); design record 17 section 4.7,
  for Q11 (and Q10 if folded there).
- `xhciqual/quirks.c`, `qual.h` and `report.c`: section 5.5.
- `xhcisnap/xhcisnap.c` and `xhcisnap/README.md`: section 5.4.
- `legal-provenance.md` section on the mirrors: a row per copy, if any.
- `external/README.md`: `include/linux/pci_ids.h` added to the Linux
  mirror at the same commit, so section 2.1's **(h)** values are read
  rather than carried (proposed now; changes no pin).

## 9. Provenance of this record

Linux facts: read from source at the pinned commit, `file:line` above; no
binary was read, so no static or runtime tag applies and no
`legal-provenance.md` row is owed. Driver facts: read from `src` at
`0976a08a`, by file and function. Project machines and their ids:
`docs/contributing/runs/run-35.md` (E460 9D2F, P14s Gen 1 02ED, B490
1E31) and `run-36.md` (E7250 9CB1); the VL805: issue 13.
