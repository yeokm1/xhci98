# Design Record 28 - A Windows 98 SE or ME Function With No Interrupt

Status: draft 1, 2026-10-18, before code. Written for `roadmap-usb4.md`
Phase 42 task 42-0.6 (planned as `NOIRQ.1` until the owner opened the
phase on 2026-10-18), for release `3.1.0.0`; the code is task 42-A.15,
the legs are 42-V.6. It follows design record 21 section 6.1.1 (the bus
numbers, windows and interrupt lines a tunnel's bridges get on Windows
98 SE and ME before the re-enumeration) and Stor98's polling mode
(Stor98 `README.md`, "Polling mode" and "Interrupt report"; its
`docs/PLAN.md`, "Polling mode"), which exists since Stor98 `0.4.0.0` for
exactly the function this record starts. Section 9 lists the decisions
taken in writing it, each the orchestrator's for the owner to confirm
unless it names the owner; section 10 what is open.

The owner's rule binds every line of it (design record 23 section 6.1,
record 27 section 2): nothing undocumented. What "documented" means here
is section 2.

## 1. What it is for

### 1.1 The problem

On Windows 98 SE and ME a PCI function behind a Thunderbolt or USB4
tunnel is enumerated by `PCI.VXD` under the configuration manager when
`xhcicm.sys`'s 98 worker asks for the tunnel port's re-enumeration
(record 21 section 6.1). `PCI.VXD` builds the function's resource
requirements from its configuration header: a memory or I/O range per
base address register, and, for a function whose Interrupt Pin register
is not 0, an interrupt. The interrupt is the problem:

- **A pin with no routable interrupt.** `PCI.VXD` routes a function's
  interrupt pin to an IRQ through the platform's routing information for
  the function's bus and device; a function on a bus the BIOS numbered
  after the fact, or behind a bridge the tunnel brought up, has no entry,
  and the function does not start. Read on the owner's ThinkPad P14s Gen
  1 (record 21 section 6.1.1, 2026-10-16): with the bridges numbered and
  their windows in place, the NVMe appeared "at Code 29 for want of an
  IRQ" (`CM_PROB_HARDWARE_DISABLED`, `0x1D`), and started on IRQ 10 only
  once the numbering's interrupt stage had written the port's own
  Interrupt Line into its header. That stage writes a line only where the
  function's pin provably reaches the port's wire (the swizzle through
  the bridges ends on the port's pin); "any other function gets nothing,
  and keeps whatever PCI.VXD makes of it (Code 29 for want of an IRQ on
  the P14s)". An NVMe function whose pin swizzles onto another wire, or
  one behind a switch whose port numbering is not the standard one, is
  such a function.
- **No pin at all.** A function whose Interrupt Pin reads 0 signals only
  through MSI or MSI-X. Neither Windows 98 SE nor ME delivers message
  interrupts to a SCSI miniport (Stor98 `README.md`, "Polling mode": "a
  SCSI miniport gets no MSI on these systems"), and `PCI.VXD` builds no
  interrupt requirement for it, so the function starts with its memory
  alone and its driver has no interrupt to use.

In both cases the driver below must run without an interrupt. Stor98
`0.4.0.0` does (its polling mode: "When Windows gives the controller no
usable interrupt, Stor98 runs without one: it polls the controller for
completed requests, often while requests are outstanding and seldom when
idle"), and it writes what it found into a user-readable registry value
(`StorInterrupt <bus>.<device>.<function>`: the pin, the line, MSI and
MSI-X presence, the port's interrupt, the outcome). What is missing is
the first half: on Windows 98 SE and ME the function with a pin and no
route never reaches Stor98, because the configuration manager does not
start it.

### 1.2 Scope

- How a function whose interrupt pin cannot be routed is started with no
  interrupt on Windows 98 SE and ME: its logical configuration, the
  configuration-manager services that give it one, and what `PCI.VXD`
  then requires of it (sections 3 to 5).
- The pairing with Stor98's polling mode: what the started function
  presents, what Stor98 reads, and where the outcome is reported
  (section 6).
- The MSI-only function, which needs nothing of the connection manager,
  and the reading that says so (section 5.4).
- Host vectors and the legs (section 8).

### 1.3 Not in scope

- An interrupt of the connection manager's own (`xhcicm.sys` polls,
  record 21 decision D3) or of the HCD's: this is about the device
  behind the tunnel.
- Routing an interrupt that the platform does not route: a line written
  for a wire the function is not on would share another device's IRQ and
  give Stor98 an interrupt it never receives (its polling mode would not
  engage, since the line reads set), so no line is ever invented. Record
  21 section 6.1.1 step 5's rule stands: a line is written only where the
  route is provable.
- Any function that is not Stor98's: a driver that does not poll would
  wait for an interrupt that never comes. Section 4's rule limits this to
  the NVMe class, the class Stor98 binds (the owner's choice, section 9).
- The NT targets: `pci.sys` owns a function's resources there, and a
  function with no routable interrupt is the bus driver's business (the
  installed Stor98 polls where NT hands it no interrupt, its README).
- A function with an I/O base address register below the tunnel: record
  21 section 6.1.1 gives such a device no window ("outside what this
  does"), and this record does not start it either.

## 2. What is documented

Record 27 section 2 set the standard for the 16-bit setup engine: the
DDK header's own documentation blocks and the DDK's samples. For the
ring-0 configuration manager the same DDK (the Windows 98 Second Edition
DDK, `tools\win98ddk-extracted\`, `docs/references/README.md`) gives:

- **`INC_WIN98_CONFIGMG.H`**, the configuration manager's header, whose
  comments document the resource descriptors with worked examples - the
  memory descriptor and its range ("Example for memory Resource
  Description", `Mem_Des_s` and `Mem_Range_s`), the I/O descriptor, the
  IRQ descriptor (`IRQ_Des_s` with `fIRQD_Share` and `fIRQD_Level`) - the
  logical-configuration kinds and their priorities (`BASIC_LOG_CONF`,
  `FILTERED_LOG_CONF`, `ALLOC_LOG_CONF`, `BOOT_LOG_CONF`,
  `FORCED_LOG_CONF`; `LCPRI_FORCECONFIG` to `LCPRI_DISABLED`), the
  service table (`Begin_Service_Table(CONFIGMG, VxD)`, line 532 on) with
  each service's C signature (`MAKE_CM_HEADER`, line 2800 on), the result
  codes (`CR_*`), the problem codes (`CM_PROB_*`) and the status flags
  (`DN_*`); and one block of prose on how the configuration manager
  configures a devnode (the `CONFIG_FILTER`, `CONFIG_START` and
  `CONFIG_STOP` events, line 1395 on): "CM copies the requirement list
  (BASIC_LOG_CONF) onto the filtered requirement list (FILTER_LOG_CONF)
  so that they are originally the same", the enumerator's and the
  drivers' chance to patch the filtered list, and a start that looks
  "only at the allocated list (ALLOC_LOG_CONF)".
- **`INC_WIN98_CFGMGR32.H`**, the Win32 mirror of the same services
  (`CM_Add_Empty_Log_Conf`, `CM_Add_Res_Des`, `CM_Get_First_Log_Conf`,
  `CM_Free_Log_Conf`, `CM_Setup_DevNode`, `CM_Reenumerate_DevNode`, the
  same `LOG_CONF` kinds and `CM_SETUP_*` flags at lines 652 to 870),
  whose semantics Microsoft documents publicly in the Platform SDK's
  Configuration Manager reference; on Windows 98 SE and ME `cfgmgr32.dll`
  thunks them to the ring-0 services of the first header.
- **`INC_WIN98_PCI.H`**, `PCI.VXD`'s header: its four services
  (`_PCI_Get_Version`, `_PCI_Read_Config`, `_PCI_Write_Config`,
  `_PCI_Lock_Unlock`), the bus-number resource (`ResType_PCI_Bus_Number`
  = `PCI_DEVICE_ID`), and the IRQ miniport interface (`PCI_WITH_PCIMP`:
  `IRQINFO` per bus and device with a link and an IRQ map per pin,
  `IRQINFOHEADER` with the PIC's location and `wBestIRQs`, the
  `PCIMP_VALIDATE_SOURCE_PIRTABLE` and `PCIMP_VALIDATE_SOURCE_PCIBIOS`
  sources), which is how `PCI.VXD` routes a pin: by a per-device table
  entry, from the BIOS's `$PIR` table or the PCI BIOS, through the
  platform's interrupt router.

What the DDK does not give: the Configuration Manager reference pages.
The DDK's help file (`HELP_WIN98DDK.CHM`, extracted with 7-Zip into a
scratch directory and searched on 2026-10-18, 1,548 pages) names no
`CONFIGMG_*` service, no logical-configuration kind and no
`Reprocess_DevNode`; like the device installer (record 27 section 2) the
configuration manager's prose reference is the Windows 95 DDK's, which is
not at hand. So the semantics this record relies on are the header's
comments, the Win32 mirror's names and flags (whose Platform SDK
documentation is public), and the structural rule the header states: a
devnode may carry more than one basic logical configuration, each with a
priority, and the configuration manager allocates the first it can
satisfy in priority order (`LCPRI_*`; `PRIORITY_EQUAL_FIRST` and
`PRIORITY_EQUAL_LAST` order two of equal priority). Where a step rests on
that rule rather than on a sentence of the header, section 5 says so,
and 42-V.6's leg is what confirms it; a design that could not be read
this way would not be written.

Numeric values alone are taken from the headers, as record 21 section 6.1
takes them: the service numbers (their place in the table), the flag and
priority values, the resource types and the descriptor layouts, each cited
to its line; no macro body or comment is reproduced in `src\`, and
`legal-provenance.md` section 4's CONFIGMG row is extended by the code
task with the new numbers (section 11).

## 3. How `PCI.VXD` configures a function below the tunnel port

What is known, from the headers, from record 21 section 6.1.1's readings
and from the P14s:

1. **Enumeration.** `Reenumerate_DevNode(bridge)` makes `PCI.VXD` read the
   port's secondary bus; for each function that answers it creates a
   devnode (`PCI\VEN_&DEV_&SUBSYS_&REV_` with the instance id in the
   `BUS_&DEV_&FUNC_` or the path form, record 21 section 6.1) and builds
   its **basic logical configuration** from the header: one memory
   resource descriptor (`MEM_DES` with one `MEM_RANGE`) per memory base
   address register, sized by writing all ones and reading back, with the
   range's alignment, length, minimum and maximum, `fMD_32` where the
   register decodes 32 bits and `fMD_Pref` where it is prefetchable; an
   I/O descriptor per I/O register; and, where Interrupt Pin is not 0, an
   IRQ descriptor (`IRQ_DES`) whose request mask names the one IRQ the
   routing information gives that pin at that bus and device, shareable
   and level-triggered (`fIRQD_Share | fIRQD_Level`). The descriptor's
   content for a function with no routing entry is not read; what is read
   is the outcome: the devnode does not start.
2. **Routing.** `PCI.VXD`'s pin-to-IRQ knowledge is per bus and device
   (`PCI.H`'s `IRQINFO`: `bBusNumber`, `bDevNumber`, and for each of the
   four pins a link and a map of the IRQs that link may take), from the
   BIOS's routing table or the PCI BIOS. A bridge the BIOS did not see at
   POST has no entry for the bus behind it (record 21 section 6.1.1: the
   P14s's `$PIR` covers the standard swizzle only for the ports the BIOS
   numbered). The numbering's interrupt stage fills the gap where it can:
   the function's Interrupt Line register gets the port's line where the
   swizzled pin lands on the port's pin, and `PCI.VXD` then accepts the
   line as it accepts a BIOS-written one (the P14s started the NVMe on
   IRQ 10 that way). Where the stage writes nothing, the register reads 0
   or `FFh` and the routing table has no entry: the IRQ descriptor cannot
   be satisfied.
3. **Allocation.** The configuration manager copies the basic
   configuration to the filtered one, lets the branch above filter it
   (`CONFIG_FILTER`), and allocates (`ALLOC_LOG_CONF`); `PCI.VXD`'s
   `CONFIG_START` handler then programs the base address registers from
   the allocation and the Interrupt Line from the allocated IRQ. A
   configuration none of whose logical configurations can be allocated
   leaves the devnode with a problem: `CM_PROB_HARDWARE_DISABLED` (29) on
   the P14s for the IRQ case, with an empty `Allocation` under
   `HKEY_DYN_DATA\Config Manager\Enum`; `CM_PROB_NORMAL_CONFLICT` (12),
   `CM_PROB_PARTIAL_LOG_CONF` (16) and `CM_PROB_NO_VALID_LOG_CONF` (27)
   are the header's other codes for a configuration that cannot be met,
   and section 4 treats the four alike.
4. **A function with Interrupt Pin 0** gets no IRQ descriptor, is
   allocated its memory, and starts (the ordinary case of a PCI function
   that uses no legacy interrupt). Nothing here touches it.
5. **Alternatives.** The header's model is that a devnode carries a list
   of basic logical configurations, each with a priority
   (`Add_Empty_Log_Conf(plc, dn, Priority, ulFlags)` adds one of the kind
   `ulFlags` names, `PRIORITY_EQUAL_FIRST` or `PRIORITY_EQUAL_LAST`
   placing it among equals), and that the configuration manager
   allocates the best it can satisfy: `LCPRI_FORCECONFIG` (0) and
   `LCPRI_BOOTCONFIG` first, `LCPRI_DESIRED` (`0x2000`) and `LCPRI_NORMAL`
   (`0x3000`) next, `LCPRI_SUBOPTIMAL` (`0x5000`) after those, and the
   `LCPRI_RESTART` to `LCPRI_DISABLED` tiers for configurations that need
   more than a hot change (CONFIGMG.H lines 440 to 455). This is the model
   ISA Plug and Play cards use (several configurations in the card's own
   resource data), and the configuration manager's arbitrators serve it
   for every enumerator.

## 4. The design: an alternative configuration without the interrupt

**Where.** In `xhcicm.sys`'s Windows 98 SE and ME worker (`cm_cfgwk.c`,
record 21 sections 6.1 and 7.4), after a member's `Reenumerate_DevNode`,
inside the same window in which the worker walks the member's new
children to mark them removable (`cmWkMark`, the marking window of
`CM_WK_MARK_MS`, 10 s): every CONFIGMG call is the worker's, never the
connection-manager thread's, for the reason record 21 section 6.1 gives.
Nothing of this runs on NT, and nothing before the enumeration has
created the devnode: a logical configuration belongs to a devnode.

**When, for a child.** Each new child of an active member (not in the
worker's known set) is a candidate when every one of these holds, read
through the services record 21 section 6.1 already uses and `PCI.VXD`'s
read (`CmCfgmgPciRead`, record 21 section 6.1.1):

1. `XhciCmNoIrqStart` (section 7) is 1.
2. The devnode's status reads `DN_HAS_PROBLEM` with a problem of 29, 12,
   16 or 27 (`Get_DevNode_Status`), and not `DN_STARTED`.
3. Its configuration header, read through `PCI.VXD` at the location its
   instance id names (the `BUS_&DEV_&FUNC_` form, or the path form
   resolved as `cmWkRemoveChild` resolves it): class `01h` subclass `08h`
   (NVMe, the class Stor98 binds and the one with a polling mode), a type
   0 header, Interrupt Pin not 0, Interrupt Line 0 or `FFh`, and no I/O
   base address register.
4. The numbering's interrupt stage recorded no line written for that
   function (record 21 section 6.1.1 step 5; the worker's last
   `XHCI_TBT_BRNUM` for the member, when one ran), or no numbering ran
   for the member (the endpoint directly on the port's secondary bus,
   the emulator's layout, where the stage's rule - the port's own pin -
   is applied to the function by the same swizzle test and found false).
5. No attempt has been made for this devnode handle at this publication
   generation (`PubGen`, the worker's per-member count of its own
   re-enumerations), so a devnode that stays unstarted is tried once per
   enumeration, and a re-plug's new devnode is tried afresh.

A child that fails any test is left as `PCI.VXD` made it, as today.

**What.** For a candidate, in order, each step's `CONFIGRET` logged and
the attempt abandoned at the first failure (nothing added is left
behind: a logical configuration this driver added and did not finish is
freed with `Free_Log_Conf`):

1. `Get_First_Log_Conf(&lc, dn, BASIC_LOG_CONF)`: the enumerator's basic
   configuration. `CR_NO_MORE_LOG_CONF` means `PCI.VXD` built none, and
   nothing is done.
2. Its resource descriptors are read: `Get_Next_Res_Des(&rd, lc,
   ResType_All, &type, 0)` from the configuration, then from each
   descriptor, until `CR_NO_MORE_RES_DES`; for each, `Get_Res_Des_Data_Size`
   and `Get_Res_Des_Data` into a buffer of the worker's (at most nine
   memory ranges, `MAX_MEM_REGISTERS`, the header's own bound). A
   descriptor of `ResType_IRQ` is noted and not copied; `ResType_Mem` is
   copied as read; any other type (`ResType_IO`, `ResType_DMA`, an
   unknown or ignored type) ends the attempt, since a function with such
   a requirement is outside this record. A basic configuration with no
   IRQ descriptor at all also ends it: the function's problem is not the
   interrupt.
3. `Add_Empty_Log_Conf(&alt, dn, LCPRI_SUBOPTIMAL, BASIC_LOG_CONF |
   PRIORITY_EQUAL_LAST)`: a second basic configuration, below the
   enumerator's in priority so that the configuration manager prefers
   the interrupt whenever it can route it (a function whose route appears
   later, after a restart with the device present at POST, is configured
   with its interrupt, not without), and `LCPRI_SUBOPTIMAL` rather than a
   restart tier so that it is allocated now.
4. For each memory descriptor copied, `Add_Res_Des(&rd, alt, ResType_Mem,
   data, len, 0)` with the data exactly as read from the enumerator's
   descriptor (alignment, length, minimum, maximum and flags): the
   requirement, not an allocation, so the arbitrator places the ranges
   inside the port's window as it would have for the enumerator's own
   configuration, and nothing here chooses an address. The forced form of
   section 5.2 is not used.
5. `Reprocess_DevNode(dn, CM_REPROCESS_DEVNODE_ASYNCHRONOUS)` (service 92,
   the "4.0 OPK2" group; flag 0, CONFIGMG.H line 1287): the configuration
   manager re-runs the devnode's configuration, which is the point at
   which it is expected to choose the alternative; `Setup_DevNode(dn,
   CM_SETUP_DEVNODE_READY)` ("Reenable problem devinst", the mirror's
   comment at CFGMGR32.H line 865) is the fallback the leg tries where
   `Reprocess_DevNode` leaves the problem in place.
6. At the worker's next pass over the member's children (the marking
   window's), the devnode's status is read again: `DN_STARTED` with no
   problem is the success; the Interrupt Line register is read once more
   and logged (it is expected unchanged: `PCI.VXD` programs a line only
   from an allocated IRQ), and the outcome - started, still the same
   problem, another problem, or a devnode gone - is logged and written to
   the diagnostic (section 7). Nothing is retried within a publication
   generation; the next re-enumeration of the member tries again under
   rule 5 above if the devnode is still there and still unstarted.

**What is never done.** No configuration is persisted (`Setup_DevNode`
with `CM_SETUP_WRITE_LOG_CONFS` is not called): the alternative lives in
the devnode for this boot, so a device present at POST on the next start,
which the BIOS routes, is configured by `PCI.VXD`'s own configuration with
its interrupt and nothing of this record applies; the leg reads the
devnode's Enum key before and after (`LogConfig`, `ForcedConfig`,
`Allocation`) to confirm the configuration manager wrote nothing of the
alternative on its own (section 10). No forced configuration is written
(section 5.2). No interrupt line is written. No descriptor of the
enumerator's configuration is modified or freed. No devnode of another
class is touched, and no bridge (a bridge is `PCI.VXD`'s to configure, and
the numbering of record 21 section 6.1.1 is as far as this driver goes
below a port).

**Why an alternative and not a change of the enumerator's requirement.**
`Modify_Res_Des` and `Free_Res_Des` on the enumerator's own basic
configuration would take the IRQ requirement away for good, in a
configuration `PCI.VXD` owns and may rebuild; a second configuration adds
to the list and takes nothing from it, and the priority order makes the
interrupt the preferred outcome whenever it is available. The filter
event (`CONFIG_FILTER`, which lets the devnodes above patch a child's
filtered list) belongs to the enumerator and the device drivers in the
branch, which this driver is not (record 21 section 6.1: nothing of this
driver sits on the bridge devnode), so it is not used.

## 5. What `PCI.VXD` requires, and the two forms

### 5.1 The arbitrated form (the design, section 4)

What the design rests on, and what the leg reads for each:

- **That the configuration manager allocates a second basic
  configuration when the first cannot be met.** The header's model
  (section 3 item 5) and the Plug and Play design it serves say so; no
  sentence of the header says it in those words for a devnode whose
  enumerator built the first configuration. Leg: the devnode starts after
  step 5, its `Allocation` holds the memory ranges and no IRQ.
- **That `PCI.VXD`'s start programs the base address registers from an
  allocation without an IRQ and writes no Interrupt Line.** The
  `CONFIG_START` prose ("look only at the allocated list") and the pin-0
  case, which `PCI.VXD` starts without an IRQ every day, say the handler
  copes with no IRQ element. Leg: the registers read programmed inside
  the port's window, the line unchanged.
- **That `Reprocess_DevNode` re-runs the allocation for a devnode with a
  problem.** The service's name and group; nothing more. Leg: the
  outcome after step 5, and `Setup_DevNode(READY)` where it does not.
- **That the services may be called from the worker thread.** Record 21
  section 6.1 established the worker as the one caller of CONFIGMG
  services (a passive thread of the system VM, blocking while the
  configuration manager is busy); the log-conf services are no different
  in kind from `Query_Remove_SubTree`, and `cfgmgr32.dll`'s Win32 mirror
  calls the same services from a ring-3 thread.

### 5.2 The forced form (the fallback)

If the leg shows that the configuration manager does not allocate the
alternative (the devnode keeps its problem after step 5 and after the
`Setup_DevNode` fallback), the second form is the forced configuration
the header provides for a user's Device Manager settings
(`FORCED_LOG_CONF`, `LCPRI_FORCECONFIG`): `Add_Empty_Log_Conf(&f, dn,
LCPRI_FORCECONFIG, FORCED_LOG_CONF)` and, for each memory requirement, a
descriptor with a fixed range (`MR_Min` = `MR_Max` = the base, `MR_nBytes`
the size, the flags as read) taken from the region record 21 section
6.1.1 step 4 reserves for the base address registers of that bus (the
aligned region at the bottom of the level, the windows packed above it),
computed by the same pure code (`tbt_brnum.c`) for the endpoint-only
layout too, each range tested free with `Detect_Resource_Conflict`
(service 112) before it is added. This form chooses addresses, which is
why it is the fallback and not the design: an address the arbitrator did
not choose is one the next `PCI.VXD` pass may disagree with. It is written
here so that 42-A.15 has it without a second design round; whether it is
built is the leg's verdict (section 10), and the owner's (section 9).

### 5.3 Undo

An attempt that fails after step 3 frees its own configuration
(`Free_Log_Conf(alt, 0)`); nothing of the enumerator's is changed by any
step, so no other undo exists. A devnode removed meanwhile (the tunnel
down, the user's Device Manager) takes its configurations with it. A
worker whose request went stale (a newer post for the member, the stop)
between two steps finishes the step it is in and makes no further call,
as `cmWkGo` rules every CONFIGMG unit today; a configuration left added
but not reprocessed is harmless (a second basic configuration the manager
may or may not use) and is freed by the next attempt's step 1 finding it
(a basic configuration this driver added is recognised by its priority,
`LCPRI_SUBOPTIMAL`, which `PCI.VXD` does not use; read through
`Get_Set_Log_Conf_Priority`, service 106).

### 5.4 No pin: the MSI-only function

A function whose Interrupt Pin reads 0 needs nothing from this driver on
Windows 98 SE and ME: `PCI.VXD` builds no IRQ descriptor and starts it
with its memory (section 3 item 4). Stor98 reads pin 0 and polls ("no
interrupt pin" in its report). The design's contribution is the reading
alone: the numbering's interrupt stage already counts functions with a
pin (`IrqFunctions`) and leaves a pin-0 function untouched; the diagnostic
of section 7 records a pin-0 NVMe function seen below the member, and
42-V.6 reads one started and polling where the emulator can present one
(section 8). If the emulator cannot, the case rests on `PCI.VXD`'s
ordinary handling of a pinless function and is said so in the release
notes.

## 6. The pairing with Stor98's polling mode

Stor98 decides at its start, per controller (its README, "Polling mode";
`StorPollMode` 0, the default): it polls when "the controller has no
interrupt pin", when "Windows assigned it no interrupt", or when "its
interrupt line is not set"; otherwise it uses the interrupt. On Windows
98 SE and ME the port hands a miniport the interrupt the configuration
manager allocated (`ConfigInfo`'s bus interrupt level), so a function
started by section 4 - allocated its memory and no IRQ, its line register
0 or `FFh` - meets two of the three conditions, and Stor98 polls at
`StorPollBusyUs` while busy (1 ms by default on 98 SE and ME) and
`StorPollIdleMs` when idle (1 s). Its report value
`StorInterrupt <bus>.<device>.<function>` under `Services\stor98` then
reads `pin N line 255 ... port 0/0 polling (...)` with its reason, which
is the evidence the leg records beside this driver's diagnostic. The
throughput Stor98 measured in polling mode on 98 SE (its README's table:
12 to 13 MB/s at 64 KiB requests against 13 to 16 with the interrupt) is
what a user of such a function gets, and the release notes say that a
device started this way runs in Stor98's polling mode.

What Stor98 does not need from this driver: no new table field, no
statement, no value. The link-state channel of record 21 section 7.2 is
unchanged, and the `UP` entry for the function is published before the
enumeration as today, whatever the interrupt outcome. What Stor98's side
owes is tracked in its own plan (its `docs/PLAN.md`, "Polling mode"); the
joint reading is 42-V.6's.

A function of another class started by nobody stays as `PCI.VXD` left it,
and the viewer (XHCITR, record 25) shows its problem code as it shows any
other.

## 7. Registry value, diagnostic, log and counters

- **`XhciCmNoIrqStart`** (`REG_DWORD`, the NHI's driver key, read once at
  start; not written by the INFs, as `XhciCmBridgeNumbering` is not;
  record 21 section 8's table gains the row with the code): 1 (the
  default; absent reads as 1) runs section 4 for the functions its rules
  admit; 0 turns it off, leaving such a function as it came up. Any other
  value is refused and 1 stands. Windows 98 SE and ME only; the NT route
  never reads it. The installer neither asks for it nor writes it (record
  23 section 8.1's never-asked set, behind the window's "Advanced..." in
  record 26 section 5.5 as a 98 SE and ME row).
- **The diagnostic** `XhciCmNoIrqDiag` (`REG_BINARY`, eight dwords, in the
  NHI's hardware key beside `XhciCmBridgeDiag`, written by the thread from
  the worker's copy as that one is): the last attempt's member location,
  the function's location, its header's pin and line as read, the problem
  before, the step reached and its `CONFIGRET`, the status and problem
  after, and the count of attempts and of pin-0 NVMe functions seen; so a
  release build's outcome can be read from a registry export, as record
  21 section 6.1.1's can.
- **The log**: `cm.reenum.9x.noirq` per step (step in the high byte,
  `CONFIGRET` in the low word), `cm.reenum.9x.noirq.after` with the status
  read at the next pass, and the devnode-handle records through
  `CmLogNoteAddress` as the worker's other handle records go (level 4
  alone).
- **Counters** (record 21 section 8's `cm.*` set, the worker's block):
  candidates, attempts, started, still unstarted, refused by each rule of
  section 4, freed after a failure.

## 8. Verification

### 8.1 Host vectors

The pure half - the candidate rules over a devnode's status, problem and
header; the descriptor copy (every `ResType_Mem` descriptor copied byte
for byte, the IRQ one dropped, any other type refusing); the swizzle test
for the endpoint-only layout, shared with `tbt_brnum.c`; the diagnostic's
layout - is DDK-free and runs in `test\test_cm_cfgwk.c` or a suite of its
own over a fake configuration-manager table: a function with a pin and no
line (admitted), pin 0 (not a candidate, counted), a line set (not a
candidate), a non-NVMe class (refused), an I/O register (refused), a
configuration with no IRQ descriptor (refused), nine memory ranges (the
bound), a second attempt in the same publication generation (refused),
and the undo after a failed `Add_Res_Des`.

### 8.2 The legs (42-V.6, "42-A.15 on 98 SE and ME with the IRQ withheld")

On the emulator, 98 SE and ME, with Stor98 `0.4.0.0` or later bundled:

1. **The IRQ withheld.** The port's own line withheld from the function
   (the emulator's routing for the tunnel bridge's bus absent, or a test
   value of the qemu flavour that makes the interrupt stage write
   nothing), a tunnel up: before the fix the NVMe at Code 29 (or 12); with
   it, started, `Allocation` with the memory ranges and no IRQ, the
   Interrupt Line unchanged, Stor98's `StorInterrupt` reading `polling`
   with "no interrupt" as its reason, a file written and compared on the
   drive, the removal and re-plug, and the diagnostic read from the
   registry export.
2. **The route present.** The same device with the port's line routed:
   the enumerator's configuration allocated with its IRQ, the alternative
   added and not used (the diagnostic says "not a candidate": no
   problem), Stor98 with the interrupt - so the alternative never
   displaces a routable interrupt.
3. **A restart with the device present at POST**: `PCI.VXD`'s own
   configuration with the BIOS's routing, nothing of this record in the
   devnode's Enum key.
4. **Pin 0** where the emulator can present an NVMe function without an
   interrupt pin: started by `PCI.VXD` alone, Stor98 polling with "no
   interrupt pin", the diagnostic counting it.
5. **`XhciCmNoIrqStart` 0**: the function left at its problem code.
6. **ME** as a hypothesis the leg tests, as record 27 treats it: the same
   service table and the same `Get_Version` guard (record 21 section
   12); the leg reads item 1 there before the record says ME.

On the owner's ThinkPad P14s Gen 1 (42-V.6's hardware half, owed where
the bench allows): the ORICO ASM2464 enclosure's NVMe, whose pin the
numbering's stage already routes (record 21 section 6.1.1); a function
this record would start there needs a device whose pin does not land on
the port's pin, which no device at hand presents, so the hardware
reading is "nothing to do, the stage's line stands" unless such a device
turns up.

## 9. Decisions taken in writing this (the orchestrator's, owner to confirm)

| Id | Decision | Taken | Sections |
|---|---|---|---|
| N1 | Where the function is started | in `xhcicm.sys`'s 98 worker, after the member's re-enumeration, within the marking window; never on the connection-manager thread | 4 |
| N2 | The form | a second basic logical configuration at `LCPRI_SUBOPTIMAL` with the enumerator's memory requirements and no IRQ descriptor, allocated by the configuration manager; the forced form with fixed ranges kept as the fallback the leg decides | 4, 5 |
| N3 | Which functions | NVMe (class `01h` subclass `08h`) alone, a type 0 header with no I/O register, a pin with no provable route and a line reading 0 or `FFh`, a devnode with a configuration problem; never a bridge, never another class, never a function with a line | 1.3, 4 |
| N4 | Persistence | none: no `CM_SETUP_WRITE_LOG_CONFS`, no forced configuration in the registry; a device present at POST is `PCI.VXD`'s | 4 |
| N5 | The switch | `XhciCmNoIrqStart`, default 1, 98 SE and ME only, not INF-written, never asked by the installer | 7 |
| N6 | The MSI-only function | nothing done; the reading recorded in the diagnostic and read by the leg | 5.4 |
| N7 | Retries | one attempt per devnode per publication generation of its member; the next re-enumeration may try again | 4 |

## 10. Open, and the risks

- Whether the configuration manager allocates a second basic
  configuration for a devnode whose enumerator built the first, and
  whether `Reprocess_DevNode` is the call that makes it (section 5.1):
  the leg's first reading; the forced form is the fallback.
- Whether `PCI.VXD`'s start handler programs a function from an
  allocation with no IRQ element without writing an Interrupt Line
  (section 5.1).
- Whether the configuration manager persists a logical configuration it
  was given at run time (`LogConfig` or `ForcedConfig` in the devnode's
  Enum key) without `CM_SETUP_WRITE_LOG_CONFS`; if it does, the worker
  must delete it at the devnode's departure, and which service does that
  is then to be read (`Write_Registry_Value` with a zero length, or the
  VMM's own registry services the Stor98 locator already uses).
- Whether the emulator can present an NVMe function with Interrupt Pin 0
  (section 5.4) and withhold the routing for the tunnel bridge's bus
  (section 8.2 item 1); otherwise the qemu flavour's test value stands in.
- Windows ME: the same services on the same guard, untested until the
  leg.
- A function whose pin the stage cannot route on real hardware: no device
  at hand presents one; the design is proven on the emulator alone until
  one does, and the release notes say so.
- Stor98's behaviour when the port hands it no interrupt on 98 SE and ME
  is its README's claim, read by Stor98's own legs on a forced polling
  mode; the joint leg here is its first reading with the interrupt
  actually absent at allocation.

## 11. Files

| File | What |
|---|---|
| `src\cm\cm_cfgmg.h`, `cm_cfgmg.c` | the service numbers added with their table lines - `Add_Empty_Log_Conf` 24, `Free_Log_Conf` 25, `Get_First_Log_Conf` 26, `Add_Res_Des` 28, `Get_Next_Res_Des` 31, `Get_Res_Des_Data_Size` 33, `Get_Res_Des_Data` 34, `Reprocess_DevNode` 92, `Get_Set_Log_Conf_Priority` 106, `Detect_Resource_Conflict` 112 (lines 557, 558, 559, 561, 564, 566, 567, 628, 642 and 651 of the table that opens at line 532, counted as `legal-provenance.md`'s CONFIGMG row counts them); the flag and priority values (`BASIC_LOG_CONF` 0, `FORCED_LOG_CONF` 4, `PRIORITY_EQUAL_LAST` 0, `LCPRI_SUBOPTIMAL` `0x5000`, `LCPRI_FORCECONFIG` 0, `ResType_Mem` 1, `ResType_IRQ` 4, `ResType_All` 0, `CM_REPROCESS_DEVNODE_ASYNCHRONOUS` 0, `CR_NO_MORE_LOG_CONF` `0x0E`, `CR_NO_MORE_RES_DES` `0x0F`, `CM_PROB_NORMAL_CONFLICT` `0x0C`, `CM_PROB_PARTIAL_LOG_CONF` `0x10`, `CM_PROB_NO_VALID_LOG_CONF` `0x1B`, `CM_PROB_HARDWARE_DISABLED` `0x1D`); the thunks behind the `Get_Version` guard |
| `src\cm\cm_cfgwk.c` | the candidate walk, the attempt, the outcome read, the diagnostic copy |
| `src\cm\tbt_brnum.c`, `tbt_brnum.h` | the swizzle test for the endpoint-only layout; the fallback's region computation, if built |
| `src\cm\cm_statb.h`, `cm_statos.c` | `XhciCmNoIrqDiag` |
| `src\cm\cm_vals.c` | `XhciCmNoIrqStart` |
| `test\test_cm_cfgwk.c` (or a new suite), `test\run-host-tests.cmd` | section 8.1 |
| `docs\contributing\legal-provenance.md` section 4 | the CONFIGMG row extended with the new numbers and lines |
| design record 21 sections 6.1.1 and 8; `docs\using\release-notes.md`; `docs\using\tuning.md` | the pointer, the value's row, the release-notes line (Stor98's polling mode for a device started this way; the emulator-only standing) |

## Sources

- The Windows 98 Second Edition DDK's `INC_WIN98_CONFIGMG.H`,
  `INC_WIN98_CFGMGR32.H` and `INC_WIN98_PCI.H` (`tools\win98ddk-extracted\incs\`;
  `docs/references/README.md`, "Windows 98 DDK"), read 2026-10-18; its
  `HELP_WIN98DDK.CHM`, searched the same day.
- Design record 21 sections 6.1, 6.1.1, 7.4 and 8; `src\cm\cm_cfgwk.c`,
  `cm_cfgmg.h`, `tbt_brnum.c`.
- Stor98 `README.md` ("Settings", "Polling mode", "Interrupt report") and
  `docs/PLAN.md` ("Polling mode"), read 2026-10-18 at Stor98's
  `audit-2026-10-18` branch head `4253b58`.
- `runs/run-41.md`, series g (the P14s readings of 2026-10-16).
