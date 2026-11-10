# Design Record 28 - A Windows 98 SE or ME Function With No Interrupt

**Note, 2026-11-09 (tasks 48.31's follow-up and 48.36): section 14,
functions met while Windows installs, and an xHCI controller on
`xhci98.sys`: the window held while a bridge below the member, or a
function's own driver, is being installed (section 14.3); an xHCI
controller bound to `xhci98.sys` started by the memory-only form (the
owner's decision of 2026-11-09, section 14.2); a devnode refused by its
header kept off the entries (section 14.5); the time a blocked call held
the worker credited to the window and the attempts (section 14.6).**

**Note, 2026-10-30 (task 45.18): section 13, a function directly on the
tunnel port's bus, which the line stage did not reach.**

**Note, 2026-10-19 (task 42-A.15's emulator leg, `runs/run-42.md`): the
basic form is refuted for the case read; a redesign is owed.** On 98 SE,
an NVMe below a tunnel port with no routing entry sat at Code 29 ("the
BIOS for the device did not give it an IRQ"), yet `PCI.VXD`'s one basic
configuration of it (at `LCPRI_NORMAL`) held one memory descriptor and
no IRQ descriptor. Section 3 item 1's hypothesis - an IRQ requirement
that cannot be met is what leaves the device unstarted - does not hold
there: the configuration already is the one section 4 would add, so the
code refuses the device ("configuration") and changes nothing. The open
question the redesign starts from: does `Reprocess_DevNode` or
`Setup_DevNode(CM_SETUP_DEVNODE_READY)` clear a Code 29 that `PCI.VXD`
itself set on a memory-only configuration, and where (which event, on
what reading of the function or its route) does `PCI.VXD` set it?

**Note, 2026-10-25 (task 44.30): both answered, and the memory-only
form.** Section 12 has the readings and the design that follows from
them. [static] `PCI.VXD` itself disables such a function, once, while it
enumerates the parent bus: an interrupt pin, no routing-table link, a
line of 0 or `FFh` and its decode off give no IRQ descriptor and
`Disable_DevNode(dn, CM_DISABLE_HARDWARE)`, which sets problem 29;
nothing of `PCI.VXD` refuses a later configuration without an IRQ.
[obs, 98 SE in the emulator] `Reprocess_DevNode` alone cleared the
problem and the function started on its memory alone, Stor98 polling.
So for that case nothing is added: the attempt is the reprocess-only
one (section 12.2). Sections 3 to 5 stand for a configuration that
carries an IRQ descriptor, which no reading has met.

Status: draft 13, 2026-11-09 (tasks 48.31's follow-up and 48.36:
section 14, functions met while Windows installs - one hold of the
window for a bridge or a function's driver being installed, bounded at
30 minutes -, an xHCI controller on `xhci98.sys` admitted to the
memory-only form, the refused list and the credit of a blocked call;
section 1.3, section 4's budget paragraph and section 6's last
paragraph brought in line; section 14.8 has the section's own history);
draft 12, 2026-10-25 (task 44.30: section 12, the memory-only
form; the note above, the result table's configuration rows, N11, the
legs of section 8.2 item 1 and section 10 brought in line); draft 11,
2026-10-18, reviewed and revised (an unstarted device
with no problem kept ATTEMPTED, the disabled forced leftover's exception
carried into section 5.3 and N8, the re-creation leg split by what is
observed, the detached-call overlap marked as a hypothesis with a leg of
its own); draft 10, 2026-10-18, reviewed and revised (the changeable
admission conditions read again before the commit and before the
reprocess, the forced-list rule scoped to creation, the result table's
state scopes and the disappearance's precedence); draft 9, 2026-10-18,
reviewed and revised (the controller's
loss and the member's gates on every submission, an ACTIVE state with
its deadline for the creation and the reprocess-only paths, the forced
form's leftovers before and after its commit, the result table completed
with the transitions); draft 8, 2026-10-18, reviewed and revised (a
replacement
indistinguishable by the reads named as such, the transaction's budget
governing an active transaction and the window admission alone, the
committed match by kind and priority, the missing-handle check at the
next generation, the forced form's refusals as states before creation,
the VMM registry provenance row); draft 7, 2026-10-18, reviewed and
revised (the handle's place
read before every write and the residual stated whole, the journal's
limits against an unobserved re-creation, an eligible source
configuration required, the forced form's own membership check, the
attempts bound on creation alone, a progress budget for the serialised
calls); draft 6, 2026-10-18, reviewed and revised (no reconciliation, no
adoption and no shape reading: a configuration left by an earlier start
is never reused, a disabled one is ignored by its priority alone, the
transaction re-reads its own handle's place before each write, a bound on
the transactions per device, the forced form refused where any forced
configuration exists, the result table's rows scoped); draft 5,
2026-10-18, reviewed and revised (nothing this driver
adds is ever freed, so no identity of a configuration handle is relied
on; an alternative is recognised by its shape, not its priority; the
journal's end at a stop and late results need no owner; the forced
form's kind and priority journalled; a pending reconciliation; the
result table and the legs completed; the preference between two
configurations left unread); draft 4, 2026-10-18, reviewed and revised
(late mutating results
accounted, the journal's end at the stop and the adoption of a surviving
alternative, the configuration's continuity checked before any free, the
admission rules applied again at a reconciliation, one result-to-state
table with fresh reads for transient readings, the update-pending
exception as a decision, the endpoint-only negative leg, the mirror's
thunks as an inference); draft 3, 2026-10-18, reviewed and revised (the
transaction
moved onto the connection-manager thread's pass with the member's call
worker making each call, as record 21 section 7.2's 9x half has it; the
journal apart from the attempt state and reconciled across generations;
the committed configuration's ownership; pending observations apart
from terminal exclusions; the binding's two values; the forced form's
single-range rule; the legs' admission inputs); draft 2, 2026-10-18,
reviewed and revised (the candidate's
ownership established and failing closed, the journal of the
configurations this driver adds, the disabled-then-enabled transaction,
the descendant walk, the feature's own scheduling and per-device state,
the numbering's per-function interrupt outcomes retained, facts and
hypotheses told apart throughout, the existing priorities read, the
forced form's descriptors and placement, the Stor98 pairing on the pin
and line alone, the machine-wide setting); draft 1, 2026-10-18, before
code. Written for `roadmap-usb4.md` Phase 42 task 42-0.6 (planned as
`NOIRQ.1` until the owner opened the phase on 2026-10-18), for release
`3.1.0.0`; the code is task 42-A.15, the legs are 42-V.6. It follows
design record 21 section 6.1.1 (the bus numbers, windows and interrupt
lines a tunnel's bridges get on Windows 98 SE and ME before the
re-enumeration) and Stor98's polling mode (Stor98 `README.md`, "Polling
mode" and "Interrupt report"; its `docs/PLAN.md`, "Polling mode"), which
exists since Stor98 `0.4.0.0` for exactly the function this record
starts. Section 9 lists the decisions taken in writing it, each the
orchestrator's for the owner to confirm unless it names the owner;
section 10 what is open.

**The code, and its interim (2026-10-19, task 42-A.15).** The pure half
is `src\cm\cm_noirq.c` (`cm_noirq.h`, host suite `test\test_cm_noirq.c`):
the window, the per-device state, the journal, the rules, the result
table, the steps and budgets, the late results, the diagnostic and the
counters, as a state machine that hands out one operation at a time
(`CmNiNext`) and takes back its result by id (`CmNiTake`). Section 4
places each operation on the member's call worker behind interface 3's
R6f handoff word, in record 21 section 7.2's 9x execution model; those
workers are task 42-A.12's and do not exist yet. **Until they do, the 98
CONFIGMG worker (`cm_cfgwk.c`) makes each operation itself**, at once, at
each of its 200 ms ticks while a member has a window or an attempt in
progress (not the 50 ms pass cadence of section 4, "When"), with the
deadlines on its own clock. What that costs: a CONFIGMG call that blocks
(a wizard open) holds the domain's other 9x work for its duration, as
every call of that worker already does, and the attempt's budget runs
out meanwhile - FAILED "timeout", the late result accounted as section
4 says. What changes when 42-A.12 lands: the same machine driven by the
connection-manager thread's pass, each `CM_NI_OP` submitted through the
member's handoff word and each `CM_NI_RES` taken back by its id; the
machine needs no change, since it already holds one operation
outstanding per member and accounts a result that comes back after its
attempt ended. The gate the interim tests before each submission is the
worker's own view of record 21 section 5.5: the stop, the claim, and
`CONTROLLER_UNAVAILABLE`'s owed removal or no-scan re-enumeration
(`cmWkNiGate`); the host interface's liveness state itself is the
thread's and reaches the worker through that request. The forced form of
section 5.2 is not built: it is the leg's verdict (section 10). The
operations are each a short series of the services section 11 names,
made back to back (the place read, for one, is `Get_Device_ID`, then the
list and the priority).
The source's descriptors are read one resource type at a time (memory,
IRQ, I/O, DMA), then once with `ResType_All`, which must meet each of
them and no other type.

**What the emulator showed, 2026-10-19 (`runs/run-42.md`): the basic
form does not apply as section 3 expected.** On 98 SE, the NVMe behind a
tunnel port with no routing entry (the emulator's tunnel bridge at
00:1Ch), bound to Stor98 0.4.0.0 with the installer record present, sat
at Code 29 ("the BIOS for the device did not give it an IRQ"), and its
one basic configuration (at `LCPRI_NORMAL`) held **one memory descriptor
and no IRQ descriptor**, read twice across a re-plug. Section 3 item 1's
hypothesis - that `PCI.VXD` puts an IRQ requirement in the configuration
and that this requirement is what cannot be met - is refuted for this
case: the configuration is already the one section 4 would add, and the
problem is set without an allocation failure. The code refuses the device
("configuration": no IRQ descriptor) and adds nothing, so the feature is
inert there, and no function was started with no interrupt. A redesign is
owed before 42-V.6 can pass (section 10, the first two points; the owner's
or the designer's): whether `Reprocess_DevNode` or
`Setup_DevNode(READY)` clears a Code 29 set this way on a memory-only
configuration, and where `PCI.VXD` sets it.

The owner's rule binds every line of it (design record 23 section 6.1,
record 27 section 2): nothing undocumented. What "documented" means here
is section 2. Every statement below about what `PCI.VXD` or the
configuration manager does carries one of three tags: **[obs]**, read on
a machine or in the emulator and recorded in a run sheet; **[hdr]**,
stated by a DDK header's text; **[hyp]**, this record's inference from
the header's model or from an observation, which the legs of section 8
confirm or refute before the code relies on it.

## 1. What it is for

### 1.1 The problem

On Windows 98 SE and ME a PCI function behind a Thunderbolt or USB4
tunnel is enumerated by `PCI.VXD` under the configuration manager when
`xhcicm.sys`'s 98 worker asks for the tunnel port's re-enumeration
(record 21 section 6.1). `PCI.VXD` builds the function's resource
requirements from its configuration header - memory and I/O ranges for
its base address registers and, for a function whose Interrupt Pin
register is not 0, an interrupt [hyp: the header's model, section 3]. The
interrupt is the problem:

- **A pin with no routable interrupt.** `PCI.VXD` routes a function's
  interrupt pin to an IRQ through routing information kept per bus and
  device [hdr: `PCI.H`'s IRQ miniport structures, section 2]; a function
  on a bus the BIOS did not number at POST, or behind a bridge the tunnel
  brought up, is one that information need not cover. Read on the owner's
  ThinkPad P14s Gen 1 (record 21 section 6.1.1, `runs/run-41.md` series
  g, 2026-10-16) [obs]: with the bridges numbered and their windows in
  place, the NVMe appeared "at Code 29 for want of an IRQ"
  (`CM_PROB_HARDWARE_DISABLED`, `0x1D`), and started on IRQ 10 only once
  the numbering's interrupt stage had written the port's own Interrupt
  Line into its header. That stage writes a line only where the
  function's pin provably reaches the port's wire (the swizzle through the
  bridges ends on the port's pin); "any other function gets nothing, and
  keeps whatever PCI.VXD makes of it (Code 29 for want of an IRQ on the
  P14s)". [hyp] An NVMe function whose pin swizzles onto another wire,
  one behind a switch whose port numbering is not the standard one, or
  one below a port that has no Interrupt Line of its own, is expected to
  end the same way on 98 SE and on ME; the one reading is the P14s's,
  on 98 SE, and the two-system legs of section 8.2 are what generalise
  it.
- **No pin at all.** A function whose Interrupt Pin reads 0 signals only
  through MSI or MSI-X. Neither Windows 98 SE nor ME delivers message
  interrupts to a SCSI miniport (Stor98 `README.md`, "Polling mode": "a
  SCSI miniport gets no MSI on these systems"), and a function with no pin
  gives `PCI.VXD` nothing to route; whether `PCI.VXD` then starts it with
  its memory alone is [hyp], the ordinary case of a PCI function that
  uses no legacy interrupt, which no run of this project has read yet
  (section 5.4).

In both cases the driver below must run without an interrupt. Stor98
`0.4.0.0` does (its polling mode: "When Windows gives the controller no
usable interrupt, Stor98 runs without one: it polls the controller for
completed requests, often while requests are outstanding and seldom when
idle"), and it writes what it found into a user-readable registry value
(`StorInterrupt <bus>.<device>.<function>`: the pin, the line, MSI and
MSI-X presence, the port's interrupt, the outcome). What is missing is
the first half: on the P14s under 98 SE the function with a pin and no
route did not reach Stor98, because the configuration manager did not
start it, and [hyp] the same is expected of ME and of every such
function until section 8.2's legs read them.

### 1.2 Scope

- How an NVMe function whose interrupt pin cannot be routed is started
  with no interrupt on Windows 98 SE and ME: its logical configuration,
  the configuration-manager services that give it one, and what
  `PCI.VXD` then requires of it (sections 3 to 5).
- The pairing with Stor98's polling mode: what the started function
  presents, what Stor98 reads, and where the outcome is reported
  (section 6).
- The MSI-only function, which needs nothing of the connection manager if
  the hypothesis of section 5.4 holds, and the reading that says so.
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
  a function bound to a Stor98 that polls, established as section 4
  says, and fails closed where it cannot be established. Since task
  48.36 (the owner, 2026-11-09) one more class is in scope: an xHCI
  controller bound to `xhci98.sys`, which polls with no interrupt
  resource, by the memory-only form alone (section 14).
- The NT targets: `pci.sys` owns a function's resources there, and a
  function with no routable interrupt is the bus driver's business (the
  installed Stor98 polls where NT hands it no interrupt, its README).
- A function with an I/O base address register below the tunnel: record
  21 section 6.1.1 gives such a device no window ("outside what this
  does"), and this record does not start it either.
- A bridge: `PCI.VXD`'s to configure (record 21 section 6.1.1's numbering
  is as far as this driver goes), and it needs no interrupt to forward.

## 2. What is documented

Record 27 section 2 set the standard for the 16-bit setup engine: the
DDK header's own documentation blocks and the DDK's samples. For the
ring-0 configuration manager the same DDK (the Windows 98 Second Edition
DDK, `tools\win98ddk-extracted\`, `docs/references/README.md`) gives:

- **`INC_WIN98_CONFIGMG.H`**, the configuration manager's header, whose
  comments document the resource descriptors with worked examples - the
  memory descriptor and its range ("Example for memory Resource
  Description", `Mem_Des_s` and `Mem_Range_s`, lines 150 to 210: a
  descriptor header with a count of ranges, the allocated base and end
  written back into the header "when an allocation is made", and each
  range's alignment mask, byte count, minimum and maximum address and
  flags), the I/O descriptor, the IRQ descriptor (`IRQ_Des_s` with
  `fIRQD_Share` and `fIRQD_Level`, lines 335 to 360) - the
  logical-configuration kinds and their priorities (`BASIC_LOG_CONF`,
  `FILTERED_LOG_CONF`, `ALLOC_LOG_CONF`, `BOOT_LOG_CONF`,
  `FORCED_LOG_CONF`, lines 2212 to 2220; `LCPRI_FORCECONFIG` to
  `LCPRI_DISABLED`, lines 440 to 455; `PRIORITY_EQUAL_FIRST` and
  `PRIORITY_EQUAL_LAST`, lines 2234 and 2235), the service table
  (`Begin_Service_Table(CONFIGMG, VxD)`, line 532 on) with each service's
  C signature (`MAKE_CM_HEADER`, line 2800 on), the result codes (`CR_*`),
  the problem codes (`CM_PROB_*`) and the status flags (`DN_*`); and one
  block of prose on how the configuration manager configures a devnode
  (the `CONFIG_FILTER`, `CONFIG_START` and `CONFIG_STOP` events, line 1395
  on): "CM copies the requirement list (BASIC_LOG_CONF) onto the filtered
  requirement list (FILTER_LOG_CONF) so that they are originally the
  same", the enumerator's and the drivers' chance to patch the filtered
  list, and a start that looks "only at the allocated list
  (ALLOC_LOG_CONF)".
- **`INC_WIN98_CFGMGR32.H`**, the Win32 mirror of the same services
  (`CM_Add_Empty_Log_Conf`, `CM_Add_Res_Des`, `CM_Get_First_Log_Conf`,
  `CM_Get_Next_Log_Conf`, `CM_Free_Log_Conf`, `CM_Setup_DevNode`,
  `CM_Reenumerate_DevNode`, the same `LOG_CONF` kinds and `CM_SETUP_*`
  flags at lines 652 to 870), whose semantics Microsoft documents
  publicly in the Platform SDK's Configuration Manager reference - among
  them that a device instance may have several logical configurations of
  one kind, each with a priority, and that the configuration manager
  tries them in priority order. That 98 SE's and ME's `cfgmgr32.dll`
  carries these names is read (`legal-provenance.md` section 4, the 98
  SE and ME `cfgmgr32.dll` row: export names only); that each reaches
  the ring-0 service of the same name is [hyp], the natural reading of
  a 32-bit library over a ring-0 configuration manager, and nothing here
  rests on it beyond the two headers describing one model.
- **`INC_WIN98_PCI.H`**, `PCI.VXD`'s header: its four services
  (`_PCI_Get_Version`, `_PCI_Read_Config`, `_PCI_Write_Config`,
  `_PCI_Lock_Unlock`, line 56 on), the bus-number resource
  (`ResType_PCI_Bus_Number` = `PCI_DEVICE_ID`), and the IRQ miniport
  interface (`PCI_WITH_PCIMP`: `IRQINFO` per bus and device with a link
  and an IRQ map per pin, `IRQINFOHEADER` with the PIC's location and
  `wBestIRQs`, the `PCIMP_VALIDATE_SOURCE_PIRTABLE` and
  `PCIMP_VALIDATE_SOURCE_PCIBIOS` sources), which shows the shape of
  `PCI.VXD`'s routing knowledge: an entry per bus and device, from the
  BIOS's `$PIR` table or the PCI BIOS, through the platform's interrupt
  router. What `PCI.VXD` does for a device with no entry is not in the
  header [hyp, section 3].

What the DDK does not give: the Configuration Manager reference pages.
The DDK's help file (`HELP_WIN98DDK.CHM`, extracted with 7-Zip into a
scratch directory and searched on 2026-10-18, 1,548 pages) names no
`CONFIGMG_*` service, no logical-configuration kind and no
`Reprocess_DevNode`; like the device installer (record 27 section 2) the
configuration manager's prose reference is the Windows 95 DDK's, which is
not at hand. So the semantics this record relies on are the header's
comments, the Win32 mirror's names, flags and public documentation, and
the structural rule both state: a devnode may carry more than one basic
logical configuration, each with a priority, and the configuration
manager allocates the first it can satisfy in priority order. Where a
step rests on that rule, or on any reading of `PCI.VXD`'s behaviour,
rather than on a sentence of a header, it is tagged [hyp], and 42-V.6's
leg is what confirms it; a design that could not be read this way would
not be written.

Numeric values alone are taken from the headers, as record 21 section 6.1
takes them: the service numbers (their place in the table), the flag and
priority values, the resource types and the descriptor layouts, each cited
to its line; no macro body or comment is reproduced in `src\`, and
`legal-provenance.md` section 4's CONFIGMG row is extended by the code
task with the new numbers (section 11).

## 3. How `PCI.VXD` configures a function below the tunnel port

What is known, and what is inferred:

1. **Enumeration** [obs, record 21 section 6.1 and `runs/run-39.md`,
   `run-41.md`]: `Reenumerate_DevNode(bridge)` makes `PCI.VXD` read the
   port's secondary bus and create a devnode for each function that
   answers (`PCI\VEN_&DEV_&SUBSYS_&REV_` with the instance id in the
   `BUS_&DEV_&FUNC_` or the path form). [hyp] From the header's model
   (section 2) `PCI.VXD` builds the devnode's **basic logical
   configuration** from the function's header: memory requirements for
   its memory base address registers (expected as one `MEM_RANGE` per
   register, sized by writing all ones and reading back, with the range's
   alignment, length and limits; whether `PCI.VXD` writes one range per
   register, several alternatives, or a different count is unread, and
   section 4 copies whatever it finds), I/O requirements for I/O
   registers, and, where Interrupt Pin is not 0, an IRQ descriptor (its
   request mask presumably naming the IRQ the routing information gives
   that pin at that bus and device; its content for any function is
   unread). The leg captures the descriptors on both systems (section
   8.2) before any of this is promoted to a fact.
2. **Routing** [hdr, `PCI.H`]: `PCI.VXD`'s pin-to-IRQ knowledge is per bus
   and device (`IRQINFO`: `bBusNumber`, `bDevNumber`, and for each of the
   four pins a link and a map of the IRQs that link may take), from the
   BIOS's routing table or the PCI BIOS. [obs, record 21 section 6.1.1]
   On the P14s the `$PIR` table covers the standard swizzle for the ports
   the BIOS numbered, a bridge chain brought up after POST got no
   interrupt on its own, and writing the port's Interrupt Line into the
   NVMe's header made `PCI.VXD` start it on that IRQ. [hyp] The reading of that
   sequence is that `PCI.VXD` takes a line already in the register as it
   takes a BIOS-written one; the sequence alone is the fact. [hyp] Where the
   stage writes nothing, the register reads 0 or `FFh` and the routing
   table has no entry: the IRQ descriptor cannot be satisfied, which is
   the reading of the Code 29 the P14s showed before the line was written.
3. **Allocation** [hdr, the `CONFIG_*` prose]: the configuration manager
   copies the basic configuration to the filtered one, lets the branch
   above filter it (`CONFIG_FILTER`), allocates (`ALLOC_LOG_CONF`) and
   calls the enumerator's `CONFIG_START` handler, which looks "only at
   the allocated list". [hyp] `PCI.VXD`'s handler programs the base
   address registers from the allocation and the Interrupt Line from the
   allocated IRQ, and does neither for an element the allocation lacks.
   [obs, P14s] A configuration none of whose logical configurations can be
   allocated leaves the devnode with a problem: `CM_PROB_HARDWARE_DISABLED`
   (29) for the IRQ case, with an empty `Allocation` under
   `HKEY_DYN_DATA\Config Manager\Enum`; [hdr] `CM_PROB_NORMAL_CONFLICT`
   (12), `CM_PROB_PARTIAL_LOG_CONF` (16) and `CM_PROB_NO_VALID_LOG_CONF`
   (27) are the header's other codes for a configuration that cannot be
   met, and section 4 treats the four alike.
4. **A function with Interrupt Pin 0** [hyp]: gets no IRQ descriptor, is
   allocated its memory, and starts. Nothing here touches it; the leg
   reads it (section 5.4).
5. **Alternatives** [hdr and the public mirror]: a devnode carries a list
   of basic logical configurations, each with a priority
   (`Add_Empty_Log_Conf(plc, dn, Priority, ulFlags)` adds one of the kind
   `ulFlags` names, `PRIORITY_EQUAL_FIRST` or `PRIORITY_EQUAL_LAST`
   placing it among equals; `Get_First_Log_Conf` and `Get_Next_Log_Conf`
   walk them; `Get_Set_Log_Conf_Priority` reads or sets a priority), and
   the configuration manager allocates the best it can satisfy, lower
   numbers first: `LCPRI_FORCECONFIG` (0) and `LCPRI_BOOTCONFIG` (1),
   `LCPRI_DESIRED` (`0x2000`) and `LCPRI_NORMAL` (`0x3000`),
   `LCPRI_SUBOPTIMAL` (`0x5000`), then the `LCPRI_RESTART` (`0x8000`) to
   `LCPRI_IMPOSSIBLE` (`0xF000`) tiers for configurations that need more
   than a hot change, and `LCPRI_DISABLED` (`0xFFFF`). [hyp] A
   configuration at `LCPRI_DISABLED` is never allocated (its name; the
   leg reads it, section 5.1). This is the model ISA Plug and Play cards
   use (several configurations in the card's resource data), and the
   configuration manager's arbitrators serve it for every enumerator;
   what priority `PCI.VXD` gives its own configuration is unread and is
   read by section 4 step 2 before anything is added.

## 4. The design: an alternative configuration without the interrupt

**Where.** On Windows 98 SE and ME, in the execution model record 21
section 7.2's 9x half gives every 9x transaction of the connection
manager (task 42-0.7, "The transactions" and "The calls"): the no-IRQ
work is a **transaction**, per-member state owned and advanced by the
connection-manager thread's pass - its window, its per-device state, its
journal and its deadlines, every one read on the thread's clock and never
inside a CONFIGMG call - and every call it needs (a status read, a
registry read, a `PCI.VXD` read, each log-conf step, the reprocess) is
one operation handed to **the member's call worker** through interface
3's R6f handoff word (`IDLE -> SUBMITTED` by the thread with the
operation, its devnode and arguments and a new id; `SUBMITTED ->
RUNNING` by the worker; `RUNNING -> DONE` by the worker with the result;
`DONE -> IDLE` by the thread, which takes a result only for the id it
submitted), one call at a time per member. The thread never calls
CONFIGMG or `PCI.VXD` itself, for the reason record 21 section 6.1 gives;
a member whose worker is inside a blocked call (a wizard open) is "busy"
to this transaction as to every other, its window keeps running, and
another member's transaction is unaffected (R6e). An operation once submitted keeps its
in-flight record until its result arrives, whatever the transaction
does meanwhile (a window closed, the stop, a lost claim), because the
handoff word delivers exactly one result per id: a result that arrives
while its transaction is still inside its own budget ("When" below) is
the transaction's next step's input, whether or not the member's
admission window has closed meanwhile - the window admits devices, it
does not end transactions; a result that arrives after the transaction
ended (its budget spent, the stop, a lost claim) is taken by its id and
**accounted** while the member's state exists - a successful
`Add_Empty_Log_Conf` journals its handle as uncommitted, a successful
priority change marks the journalled handle committed - and never
**acted on** (no next step is submitted). After the NHI's stop has ended the member's state (the
detached worker finishes its one call and frees its block, `CmCfgwkStop`;
record 21 section 7.2 drops late results after detachment) a late
result has no owner and is dropped, and that is safe because **this
driver never frees a configuration** (N8): what a dropped late success
leaves behind is a disabled configuration nobody uses, which every later
reading ignores by its priority, or a complete committed alternative,
which the next start treats as any configuration it did not make
("Across generations and starts" below). A detached call that is still
outstanding when the next start's transaction reads the same devnode is
the one overlap left: the start's gate may read the old configuration
disabled and begin its own, and the old call may then commit the first -
two alternatives of the same content on one devnode, which [hyp] is
harmless in the basic form, since either satisfies the configuration
manager and neither is ever freed (the forced form cannot reach this:
its creation is refused while any forced configuration exists, section
5.2), and which section 8.2 item 13 reads - or read it committed and
refuse the device (the gate below), which a re-plug
ends. Both are counted where they can be seen. Nothing before the enumeration has
created the devnode a logical configuration belongs to, so the
transaction begins only when the member's re-enumeration result has come
back. Nothing of this runs on NT.

**When: the member's no-IRQ window, and the transaction's own budget.**
When the result of a member's `Reenumerate_DevNode` this driver asked
for comes back (a tunnel up, a resync, a retry of the call), the thread
opens the member's **no-IRQ window**, `CM_NOIRQ_WINDOW_MS` (10 s) on its
clock: the time in which a device must be **admitted** (its readings
final) for a transaction to begin. A transaction once begun runs on a
budget of its own, `CM_NOIRQ_TXN_MS` (30 s), past which it ends FAILED
("timeout") with its in-flight call still accounted when it returns. The
calls are serialised through the member's worker, so progress has to be
driven by completion, not by the idle cadence: the thread's pass takes a
call's result and submits the transaction's next call **in the same
pass** (`DONE -> IDLE -> SUBMITTED` on one handoff word), and while any
member of any domain has a no-IRQ transaction or window in progress the
pass runs at most 50 ms apart, the cadence record 21 section 7.2's 9x
half gives its pending acknowledgement, removal and statement states
(its `CmNvlinkIdleMs` rule gains this state; the code task 42-A.15
adds it beside 42-A.12's). So a call that returns at once costs one
pass: the gate's reads (one priority per configuration), the source's
descriptors (two or three calls each, at most `CM_WK_NOIRQ_MEM` plus
the IRQ one), the handle's place before each write (an enumeration and
a priority read each), the writes and the reprocess come to about a
hundred calls for the largest admitted source, some five seconds at
the 50 ms cadence, inside the budget; a call that blocks (a wizard
open) spends the budget as it spends every 9x transaction's, and the
transaction ends FAILED while its call stays the worker's (amended by
section 14.6: the time the worker was held is credited, up to
`CM_NOIRQ_HOLD_MS`, 30 minutes).
Devices of
one member advance one at a time (one worker, one call), those of other
members in parallel. The window is not conditioned on
`XhciCmSafeEject`, which governs the removable mark alone, and
independent of the marking window and its known set (`cmWkMark`), which
are not used for any of this. Each pass inside the window steps the
member's devices (below) once; a devnode whose problem appears only after
the wizard has bound its driver is therefore still seen. Only this
driver's own re-enumerations and a re-plug (a new devnode) open a window:
a Device Manager Refresh the user makes after the window closed is
`PCI.VXD`'s own pass, outside this driver's accounting (`cm_cfgwk.c`
treats it so today), and a function it leaves unstarted stays so until
the next tunnel change or re-plug; the viewer shows its problem code, and
the release notes say so.

The transaction keeps **per-device state** in the member's record, at
most `CM_NOIRQ_DEVICES` (8) devices a member, each keyed by the devnode
handle, the instance id's hash (`cmWkIdHash`) and the member's
publication generation (`PubGen`), with two parts kept apart:

- the **attempt state**: PENDING (seen; a rule's reading not final yet -
  not bound, no problem yet, a problem that says the driver is still
  being installed (`CM_PROB_NOT_CONFIGURED` 1, `CM_PROB_DEVLOADER_NOT_FOUND`
  8, `CM_PROB_REINSTALL` 18, `CM_PROB_FAILED_INSTALL` 28), a read that
  failed or answered Retry Status - re-read at each pass of the window),
  EXCLUDED (a terminal rule failed: the class, the header, an I/O
  register, another driver, a line set, a routable or ROUTED interrupt,
  a priority at or above the alternative's; with the rule noted),
  ACTIVE (admitted: a transaction's steps 1 to 4, or the reprocess-only
  attempt of "Across generations and starts" rule 1, running on the
  attempt's own deadline, `CM_NOIRQ_TXN_MS` from the admission; the
  window's end decides nothing for it), ATTEMPTED (step 4, or the
  reprocess, submitted; its outcome owed and read at every pass until
  the same deadline), DONE (started), FAILED (the step and `CONFIGRET`,
  or "timeout" at the deadline, or "controller" at a loss); one
  configuration transaction per devnode per generation; only a PENDING
  entry is ended by the window's close;
- the **journal**: for each devnode (handle and hash) the transactions
  this driver began for it in the member's life, at most
  `CM_NOIRQ_ATTEMPTS` (3), each with the handle of the configuration it
  added, its kind (`BASIC_LOG_CONF`, or `FORCED_LOG_CONF` for section
  5.2), the generation and whether it was committed (section 4, "The
  transaction"). The journal is this driver's own memory of what it did
  to a device while the member's state lives, never reset by a
  generation change; it identifies no configuration to anyone else and
  authorises no free, since nothing is ever freed (N8).

A handle whose hash no longer matches (CONFIGMG reuses handles, record 21
section 7.4) is a new device: its attempt state starts at PENDING and a
journal entry for the old hash is dropped unfreed (its devnode is gone).
**Across generations and starts.** A `LOG_CONF` is a `DWORD` handle
(CONFIGMG.H line 814) with no identity the header guarantees across a
devnode's removal and re-creation, this driver cannot tag a
configuration, and reading a configuration's descriptors to recognise
it would cost a handoff per descriptor on a list that may hold several.
So this record recognises **no configuration by anything**: what this
driver did to a devnode it knows from its journal while the member's
state lives, and nothing else is ever reused, adopted or compared. Three
rules follow, each a read before any write:

1. **A new publication generation** (another re-enumeration of the
   member) sets every attempt state back to PENDING and keeps the
   journal. A device with a journal entry is read again by the rules
   below as if new (the user may have disabled it, changed its driver or
   its interrupt circumstances since), with the result table's three
   outcomes; one still admitted and still unstarted whose last
   transaction was committed, and whose committed handle the gate's
   enumeration still finds as a **valid committed match** - on the list
   of the journalled kind, at the journalled committing priority
   (`LCPRI_SUBOPTIMAL` for a basic one, `LCPRI_FORCECONFIG` for a
   forced one) - gets one more `Reprocess_DevNode` under the new
   generation and no second configuration, whatever the attempts count
   (the reprocess has precedence over the bound below); one whose
   committed handle is **not found** there, or is found at another
   priority or on another list (something not this driver changed it,
   or the devnode was re-created and the handle reused), has a journal
   that describes a configuration that is no longer its own: the
   journal entry is marked gone, nothing is written to or through that
   handle again, the configuration found is treated as any other on
   the list (ignored at `LCPRI_DISABLED`, refusing the device at or
   above the threshold, rules 2 and 3), and the device is judged as
   new; one whose last transaction ended
   uncommitted, or gone, may begin another, up to `CM_NOIRQ_ATTEMPTS`
   **configurations created** in the member's life, after which no
   further one is created (EXCLUDED, "attempts") - the bound is on
   creation alone and never refuses an owed reprocess. A handle whose
   hash no longer matches is a new device with no journal. What the
   journal cannot see is a removal and re-creation that leaves handle,
   hash and generation all unchanged between two observations (an
   external Refresh between passes): the journal then stays attached to
   the old incarnation until a reading contradicts it (the committed
   handle not found, a rule now failing), and an EXCLUDED or FAILED
   state stands for the generation on the new one. This record promises
   no detection of that case and leaves it **unresolved** (section 10);
   the leg reads an observed disappearance only.
2. **A configuration at `LCPRI_DISABLED` is ignored**, whoever's it is
   and whatever it holds: never eligible [hyp, section 3 item 5], it is
   not counted against the threshold below, never read further, never
   written to. This covers this driver's own uncommitted leftovers -
   empty, partial or complete - after an interrupted transaction, a
   dropped late result or an earlier start, without knowing that they
   are its own. One exception, the forced list's: a disabled **forced**
   configuration still blocks the creation of a new forced one (below),
   since any forced configuration does.
3. **A configuration at or above `LCPRI_SUBOPTIMAL` that is not at
   `LCPRI_DISABLED` and is not the journal's own committed one** refuses
   the device (EXCLUDED, "configuration"), whatever it is: an
   enumerator's own alternative, a user's, or a committed alternative an
   earlier start of this driver left, which the journal no longer knows.
   The last case is the price of recognising nothing: a device left so
   stays at its problem code until its re-plug, which gives it a new
   devnode with a clean list, and the connection manager's stop and
   start with a tunnelled drive unstarted in between is a rare sequence
   (a Device Manager disable of the controller, an update of this
   driver). It is counted (`refused.left-over`), shown by the viewer as
   the problem code with "re-plug the device", and said in the release
   notes.

The journal ends with the member's state at the NHI's stop (the 98
worker's block is freed at the stop, `CmCfgwkStop`; the image stays
resident, record 21 section 7.2, but the state does not) and with a lost
claim; nothing is freed at either and nothing waits for CONFIGMG. For
the forced form of section 5.2 the rule is simpler still: the devnode's
forced configurations are enumerated (`Get_First_Log_Conf(&lc, dn,
FORCED_LOG_CONF)` and `Get_Next_Log_Conf`) and **any** forced
configuration present, this driver's earlier one (committed or
disabled) or a user's Device Manager setting, refuses the **creation**
of a new forced configuration for that devnode; the journal's own
committed forced one, found as a valid committed match, is reprocessed
once per generation as a basic one is (rule 1), and nothing else of the
forced form runs there.

**The walk.** Each pass of the window walks the member's **descendants**
(one `Get_Child` or `Get_Sibling` call per handoff), not its children
alone: `Get_Child` and `Get_Sibling` from the member's devnode,
descending into each child (the enclosure's switch is an upstream bridge,
a downstream bridge and then the NVMe, record 21 section 6.1.1), bounded
as the numbering is (six levels, `CM_WK_WALK_NODES` nodes), each
devnode's location resolved from its instance id in either form - the
`BUS_&DEV_&FUNC_` form by itself, the path form by the ancestry the walk
has in hand (the devnode's own devfn, then each parent bridge's up to the
member, then the member's path, the rule `cmWkIdNames` applies one level
down today, extended to the depth of the walk) - and a devnode whose id
resolves to no location is skipped and counted.

**The candidate.** A descendant in state PENDING is a candidate when
every one of these holds, each read through the member's call worker
with the services record 21 section 6.1 already uses and `PCI.VXD`'s
guarded read (`CmCfgmgPciRead`, record 21 section 6.1.1; the guard
revalidated where no numbering ran for the member in this generation);
a reading that is not final leaves the device PENDING for the next pass,
a terminal failure makes it EXCLUDED:

1. `XhciCmNoIrqStart` (section 7) is 1.
2. The devnode's status reads `DN_HAS_PROBLEM` with a problem of 29, 12,
   16 or 27 (`Get_DevNode_Status`), and not `DN_STARTED`. No problem yet,
   or one of the installation problems named above, is PENDING; a
   started devnode, or any other problem, is EXCLUDED.
3. Its configuration header at the resolved location: the complete class
   code `010802h` (NVM Express, the class Stor98's INFs bind,
   `PCI\CC_010802`), a type 0 header, Interrupt Pin not 0, Interrupt Line
   0 or `FFh`, and no I/O base address register.
4. **It is Stor98's.** Two values of the devnode's driver (software)
   key, each read with `Read_Registry_Value(dn, NULL, name, REG_SZ, ...,
   CM_REGISTRY_SOFTWARE)` (service 62; `CM_REGISTRY_SOFTWARE` `0x1`,
   CONFIGMG.H line 1227): `DevLoader` reading `*IOS` and `PortDriver`
   reading `stor98.mpd`, the pair Stor98's INF writes on 98 SE and ME
   (`stor98-x86.inf`: `HKR,,DevLoader,,*IOS`, `HKR,,PortDriver,,stor98.mpd`;
   the pair record 26 section 8 reads to recognise a Stor98 binding); and
   the Stor98 the installer records as installed (record 26 section 6.3's
   record, the `Version` value of `HKLM\Software\xhci98\Stor98`, read
   through the VMM registry services the Stor98 locator already uses,
   record 21 section 7.2, with `_RegQueryValueEx` added to them) is
   `0.4.0.0` or later, the first release with the polling mode. Both
   values absent (no driver bound yet, the wizard not run or cancelled)
   is PENDING for the window's length; another `DevLoader` or
   `PortDriver`, no installer record (a hand-installed Stor98) or an
   older recorded version is EXCLUDED: the design fails closed, and the
   release notes say that a hand-installed Stor98 is not started this
   way. What the record proves is that this installer last installed (or, on
   98 SE and ME since record 26 draft 43, staged with no owed update
   left unmade) a
   polling-capable release, not which `stor98.mpd` runs this devnode: an
   update of Stor98 on 98 SE and ME takes effect at the shutdown record
   26 section 6.3 asks for, and until it a devnode may still load the
   older image. No documented mechanism tells this driver which image a
   devnode loaded before it starts, so the rule accepts that one
   exception knowingly (decision N10, the owner's to confirm): admission
   on the recorded version, inside the window between a Stor98 update
   and the shutdown the installer's finish page asks for in the same
   run (record 26 section 5.9); what an older image does when started
   without an interrupt is **unmeasured** until section 8.2 item 7 reads
   it, and the release notes say that a drive plugged in after a Stor98
   update and before that shutdown may not start until the shutdown.
   The record is written only once an install or update of the bundled
   Stor98 has completed its copy, so a run that failed before that
   leaves the old value.
5. **Its interrupt is provably unroutable, by the numbering's record.**
   The numbering (record 21 section 6.1.1, `cmWkNumber`) now keeps its
   per-function interrupt outcomes in the member's record for the
   publication generation it ran in (today it keeps `XHCI_TBT_BRNUM`
   locally and publishes aggregate counts): for each function below the
   port with an Interrupt Pin, one of ROUTED_WRITTEN (the port's line
   written), ROUTED_ALREADY (a line already set, left), UNROUTABLE (the
   swizzled pin does not land on the port's pin), PORT_NO_LINE (the port
   itself has no Interrupt Line, `XHCI_TBT_BRIRQ_PORT_NONE`: nothing
   below it can be routed by this driver), UNKNOWN (a read failed, the
   function answered Retry Status, the write was not held, the stage was
   not run because the windows were refused, or the numbering is off).
   A candidate is a function recorded UNROUTABLE or PORT_NO_LINE. For a
   function with no record - no numbering ran for the member in this
   generation (the endpoint directly on the port's secondary bus, where
   step 2 of section 6.1.1 found no bridge, or `XhciCmBridgeNumbering` 0)
   - or recorded UNKNOWN for a cause that may have passed (a failed read,
   Retry Status, the stage not run because the windows were refused),
   the pass classifies afresh with reads alone: the same swizzle computed
   from the ancestry the walk has (the device numbers of the bridges
   between, the function's own device and pin) against the port's pin
   and line, read through the guarded route, and the function's own
   line; UNROUTABLE and PORT_NO_LINE are candidates, a line that reads
   set is EXCLUDED, a read that fails or answers Retry Status is PENDING
   and read again at the next pass. A write the stage made and could not
   hold is UNKNOWN for good within the generation (EXCLUDED: the
   function's line is in an unknown state): "not written" is not
   "unroutable", and only a fresh reading can say which.

**Result to state**, for every rule above (one table, the host vectors
cover each row):

| Reading | State |
|---|---|
| the switch 0 (rule 1) | EXCLUDED, "off" |
| no problem yet, or an installation problem (1, 8, 18, 28); a status read that failed (rule 2) | PENDING |
| started; any other problem (rule 2) | EXCLUDED, "not a configuration problem" |
| the header's class, header type, pin, line or an I/O register failing (rule 3) | EXCLUDED, "header" |
| a header read that failed or answered Retry Status (rule 3) | PENDING |
| `DevLoader` and `PortDriver` both absent, or exactly one of them present (the INF's writes in progress) (rule 4) | PENDING |
| another driver, no installer record, an older recorded version (rule 4) | EXCLUDED, "not Stor98's" |
| a registry read that failed (rule 4) | PENDING |
| UNROUTABLE or PORT_NO_LINE (rule 5) | candidate |
| ROUTED_WRITTEN, ROUTED_ALREADY, a line that reads set, a write not held; a fresh classification finding the swizzle on the port's pin with the port's line valid (routable, whether or not a line is set: the stage's business, not this record's) (rule 5) | EXCLUDED, "interrupt" |
| a read that failed or Retry Status in the fresh classification (rule 5) | PENDING |
| a PENDING entry, every rule passed, whose source configuration (the first eligible basic one, "The existing configurations") passes every descriptor check of that paragraph but the IRQ one - one to `CM_WK_NOIRQ_MEM` memory descriptors, each within the buffer, no other type - and holds no IRQ descriptor, its problem as admitted 29, and its devnode's reprocess mark of this generation unused (section 12, the memory-only form) | ACTIVE, the reprocess-only attempt, the mark taken; no configuration added |
| the same PENDING entry with its devnode's reprocess mark of this generation already taken (the devnode met as new again within the generation, section 12.2) | EXCLUDED, "attempts" |
| the source configuration without an IRQ descriptor at a problem other than 29, or with no memory descriptor; with another resource type, with more than `CM_WK_NOIRQ_MEM` memory descriptors or with a descriptor larger than the buffer; no basic configuration at all ("The existing configurations") | EXCLUDED, "configuration" |
| a basic configuration not at `LCPRI_DISABLED`, not the journal's valid committed match, at or above `LCPRI_SUBOPTIMAL` ("Across generations and starts", rule 3) | EXCLUDED, "configuration" (`refused.left-over` where it reads exactly `LCPRI_SUBOPTIMAL`) |
| a basic configuration at `LCPRI_DISABLED` (rule 2) | ignored |
| the journal's own committed alternative found as a valid committed match (its kind's list, its committing priority), the devnode unstarted (rule 1) | no new transaction; one `Reprocess_DevNode` per generation, whatever the attempts count |
| the journal's own committed alternative not found, or found at another priority or on another list (rule 1) | the entry marked gone; the configuration treated as any other; the device judged as new |
| no eligible source: nothing below `LCPRI_SUBOPTIMAL` that is not disabled and not the journal's own ("The existing configurations") | EXCLUDED, "configuration" |
| a configuration would be created and the journal already holds `CM_NOIRQ_ATTEMPTS` created for the devnode (rule 1) | EXCLUDED, "attempts" |
| any forced configuration present and a forced one would be created (the forced form alone, section 5.2) | EXCLUDED, "forced present" |
| a source descriptor with several ranges; a plan that does not fit or overflows; a conflict test answering `CR_SUCCESS` with a nonzero byte (the forced form alone, section 5.2) | EXCLUDED, "configuration", "placement", "conflict" |
| a conflict test refused by `CONFIGRET` (the forced form alone) | PENDING |
| the handle's place not as journalled before a write, the devnode present with the same hash ("The transaction" step 2) | FAILED, "handle" |
| the transaction's budget spent ("When") | FAILED, "timeout" |
| a log-conf, priority or descriptor read that failed ("The existing configurations") | PENDING |
| every rule passed and a transaction or a reprocess-only attempt begun ("The transaction"; rule 1) | ACTIVE, the deadline set |
| the controller lost, or the member's removal or exposure gate closed, before a submission ("The transaction") | FAILED, "controller"; the outstanding result accounted |
| a transaction step refused by `CONFIGRET` ("The transaction") | FAILED, the step and code |
| step 4, or the reprocess, submitted ("The transaction") | ATTEMPTED |
| the devnode started with no problem at an outcome read (step 5) | DONE |
| the deadline reached while ACTIVE or ATTEMPTED (step 5, "When") | FAILED, "timeout" |
| the devnode gone, or answering another hash, at any read of the transaction (steps 2 to 5), taking precedence over every other judgement | the entry forgotten, the journal entry retired |
| an outcome read in ATTEMPTED, before the deadline, that is not DONE: a problem still there, the devnode unstarted with no problem, or the read failing (step 5) | ATTEMPTED, read again at the next pass |
| a changeable admission condition (rule 2, rule 4) no longer met before the commit, the reprocess or the READY fallback (step 3) | FAILED, "admission"; an uncommitted configuration a disabled leftover, a committed one left eligible and not reprocessed |
| the window closed with the entry PENDING | EXCLUDED, "not read in time" |
| a descendant whose instance id resolves to no location ("The walk") | skipped and counted, no entry |
| a call's result after its transaction ended, the member's state alive ("Where") | accounted, not acted on |
| a call's result after the member's state ended ("Where") | dropped |

PENDING is re-read at every pass of the window and is EXCLUDED, "not
read in time", when the window closes; ACTIVE and ATTEMPTED run to
their deadline whatever the window does; EXCLUDED and FAILED stand for
the generation. A descendant left EXCLUDED or PENDING is left as
`PCI.VXD` made it; one left FAILED may already carry a disabled or a
committed alternative of this driver's, which stays as section 4 says.

**The existing configurations, read first.** Before anything is added,
the pass walks the devnode's basic configurations
(`Get_First_Log_Conf(&lc, dn, BASIC_LOG_CONF)`, then
`Get_Next_Log_Conf(&lc, lc, 0)`, service 27, until
`CR_NO_MORE_LOG_CONF`) and reads each one's priority
(`Get_Set_Log_Conf_Priority(&pri, lc, CM_GET_LOG_CONF_PRIORITY)`). It
goes on only when there is an **eligible source**: at least one
configuration that is not at `LCPRI_DISABLED` (ignored, rule 2 above),
is not the journal's own, and reads a priority below `LCPRI_SUBOPTIMAL`
- the first such on the list is the source whose descriptors are copied
below - and every other configuration not at `LCPRI_DISABLED` and not
the journal's own committed one reads below `LCPRI_SUBOPTIMAL` too, so
that the alternative never outranks or ties an original one and the
interrupt stays preferred wherever it can be routed (rule 3 above); the
journal's own committed one present means no second one is added (one
reprocess per generation is all it gets). A list of disabled
configurations alone, or with nothing below the threshold, has no
source and is refused (EXCLUDED, "configuration"): a disabled
configuration is never read further, whatever it holds. Otherwise the
attempt is refused likewise and nothing is added; a read that failed is
PENDING. The gate's reads are one `Get_Set_Log_Conf_Priority` per
configuration on the list and nothing else of them. The source
configuration's resource descriptors are then read: `Get_Next_Res_Des(&rd,
lc, ResType_All, &type, 0)` from the configuration, then from each
descriptor, until `CR_NO_MORE_RES_DES`; for each, `Get_Res_Des_Data_Size`
and `Get_Res_Des_Data` into the transaction's buffer. A descriptor of
`ResType_IRQ` is noted and not copied; `ResType_Mem` is copied as read
(the whole descriptor, header and ranges, whatever count the enumerator
wrote); any other type (`ResType_IO`, `ResType_DMA`, an unknown or
ignored type) refuses the attempt, since a function with such a
requirement is outside this record; a configuration with no IRQ
descriptor refuses it too (the function's problem is not the interrupt);
and more than `CM_WK_NOIRQ_MEM` (9) memory descriptors refuse it, the
bound being this implementation's buffer, not the header's (`CMCONFIG`'s
`MAX_MEM_REGISTERS` sizes a summary structure, not the descriptor list).
Every descriptor's size is checked against the buffer before it is read.

**The transaction.** So that no incomplete configuration is ever eligible,
the alternative is created disabled and enabled last [hyp:
`LCPRI_DISABLED` is never allocated, section 3 item 5]:

1. `Add_Empty_Log_Conf(&alt, dn, LCPRI_DISABLED, BASIC_LOG_CONF |
   PRIORITY_EQUAL_LAST)`; the handle is **journalled** at once in the
   member's per-device entry (the handle, the devnode, the instance id's
   hash, the generation, the kind and the committing priority), before
   any further call.
2. **Before every write to `alt`** - each `Add_Res_Des` of this step and
   the priority change of step 3 - the handle's place is read again:
   the devnode still answers with the same hash, and `alt` is still
   among the devnode's configurations **of the journalled kind**
   (`Get_First_Log_Conf(&lc, dn, kind)`, `Get_Next_Log_Conf`; the basic
   list here, the forced list for section 5.2) at `LCPRI_DISABLED`
   (`Get_Set_Log_Conf_Priority`); otherwise the transaction ends FAILED
   ("handle") with nothing more written. What this narrows, and what it
   does not: a devnode removed and re-created between two of this
   transaction's calls by something outside this driver (`PCI.VXD`'s own
   Refresh; this driver's own re-enumerations of the member queue behind
   the transaction, record 21 section 7.2) may reuse the devnode handle
   and, [hyp], the configuration handle; a reuse that happened before
   the read is caught where the reads can tell it (the handle gone, or
   found at another priority or on another list) and **not** where they
   cannot: a replacement that carries the same devnode handle and
   instance id and whose configuration has the same handle on the same
   list at `LCPRI_DISABLED` passes every check this record makes and
   is indistinguishable from this driver's own; a reuse that happens
   **between the read and the write that follows it** - for an
   enumerator's configuration or for anyone's - is not caught either,
   and in both cases the write lands on a configuration that is not
   this driver's. No documented service of the configuration
   manager serialises a read and a write of another caller's
   configuration, so this record cannot close that residual; it states
   it (section 10, N8), the gap being the one call's duration on a
   worker that holds no lock, and the leg's re-creation case reads only
   what can be observed. For each memory descriptor copied,
   `Add_Res_Des(&rd, alt, ResType_Mem, data, len, 0)` with the data
   exactly as read (the requirement, not an
   allocation: the arbitrator places the ranges as it would have for the
   enumerator's own configuration, and nothing here chooses an address).
3. **The changeable admission conditions read again**, before this step
   and before step 4 (and before the reprocess-only attempt of "Across
   generations and starts" rule 1): rule 2 (the devnode unstarted with a
   configuration problem - a device the user disabled meanwhile reads
   `CM_PROB_DISABLED`, 22, and is not one to make ready) and rule 4 (the
   binding still `*IOS` and `stor98.mpd`); a condition no longer met
   ends the attempt FAILED ("admission") with nothing more written: an
   uncommitted configuration stays a disabled leftover, a committed one
   stays committed and eligible but is not reprocessed by this attempt,
   and the next generation judges the device anew. The reads precede
   the call by the one call's duration, the same race as step 2's,
   stated in section 10. Then `Get_Set_Log_Conf_Priority(&pri, alt,
   CM_SET_LOG_CONF_PRIORITY)` with `LCPRI_SUBOPTIMAL`: the configuration
   becomes eligible only now, complete.
4. `Reprocess_DevNode(dn, CM_REPROCESS_DEVNODE_ASYNCHRONOUS)` (service 92,
   flag 0, CONFIGMG.H line 1287) [hyp: it re-runs the allocation for a
   devnode with a problem]; `Setup_DevNode(dn, CM_SETUP_DEVNODE_READY)`
   ("Reenable problem devinst", the mirror's comment at CFGMGR32.H line
   865) is the fallback the leg tries where the problem stays - made,
   like the reprocess, only after step 3's reads passed, so a device the
   user disabled is never made ready by this driver.
5. The entry is ATTEMPTED; at the pass's next tick, and at each pass
   after it, the devnode's status is read again: `DN_STARTED` with no
   problem is DONE (the Interrupt Line register read once more and
   logged, expected unchanged [hyp]); the same or another problem still
   there when the **transaction's budget** ends is FAILED with the
   outcome (the admission window's end decides nothing for an active
   transaction); any other successful read of the same devnode before
   the deadline - a problem still there, or the devnode unstarted with
   no problem (a start under way) - or a status read that failed, keeps
   the entry ATTEMPTED and is read again at the next pass; a devnode gone (its handle answering `CR_NO_SUCH_DEVNODE`
   or another hash) is forgotten, the journal entry retired with it.
   Nothing is retried within a publication generation. The
   disappearance's precedence, for every read of this transaction: a
   devnode gone or re-created with another hash is **forgotten**, before
   any other judgement; one present with the same hash whose
   configuration's place is not as journalled is FAILED ("handle").

Each step's `CONFIGRET` is logged. The transaction is never cancelled by
a newer post for the member (record 21 section 7.2, "The persistent
per-devnode transaction is not a call": a tunnel up, a resync or a
tunnel down posted meanwhile queues behind it); what ends it early is the
stop, the domain's claim lost, the devnode's disappearance, or **the
controller's loss and the member's gates**: the host interface's
liveness state no longer ACTIVE (record 21 section 5.5), the seam's
loss bracket open, or the member owing `CONTROLLER_UNAVAILABLE`'s
removal or its no-scan re-enumeration (`cm_cfgwk.c`'s `ReqGone` and
`NoScan`: the retained children are removed without reading a bridge the
loss may have reset, and nothing of this record may read configuration
space or reprocess a devnode under such a member) - record 21 section
7.2's rule that every 9x transaction observes the member's removal and
exposure gates. Each is tested on the thread before every submission,
admission included, and read from each result; a loss ends the attempt
FAILED ("controller") with its outstanding call's result accounted when
it returns, and the member's next window after the loss is resolved
judges the device anew.
Two kinds of leftover follow, and neither is ever freed (N8:
`Free_Log_Conf` is not called by this driver at all, so no identity of
a handle has to be trusted for a destructive call):

- **before step 3 (uncommitted)**: the configuration is at
  `LCPRI_DISABLED`, never eligible [hyp], so it is harmless; it stays
  with the devnode for the devnode's life, costs the configuration
  manager whatever it keeps for an empty or partial configuration
  (unmeasured; the leg reads a devnode carrying three), is ignored by
  every later reading by its priority alone and goes with the devnode's
  own removal. A transaction that ends before step 3 is FAILED for the
  generation; a later generation may begin another, up to
  `CM_NOIRQ_ATTEMPTS` (3) in the member's life, so a devnode carries at
  most three of this driver's configurations per start of the connection
  manager, counted (`leftovers`);
- **after step 3 (committed)**: the configuration is complete and
  eligible, which is what the transaction set out to leave; it is
  **kept** for the devnode's life. A stop or a lost claim after step 3
  leaves it in place (nothing is written back to `LCPRI_DISABLED`: a
  half-done rollback is worse than a complete alternative); a step 4
  that was not reached is made by the next generation's pass while the
  member's state lives ("Across generations and starts", rule 1).
  Section 5.3 and N8 say the same.

**What is never done.** No configuration is persisted (`Setup_DevNode`
with `CM_SETUP_WRITE_LOG_CONFS` is not called): the alternative is meant
for this boot, so a device present at POST on the next start, which the
BIOS routes, is configured by `PCI.VXD`'s own configuration with its
interrupt and nothing of this record applies; whether the configuration
manager persists a configuration on its own is [hyp] and the leg reads
the devnode's Enum key before and after (`LogConfig`, `ForcedConfig`,
`Allocation`, section 10). No forced configuration is written (section
5.2 is the fallback). No interrupt line is written. No configuration of
anyone's, this driver's included, is ever freed. No descriptor of the
enumerator's configuration is modified or freed. No devnode of another
class, another driver or no driver is touched, and no bridge.

**Why an alternative and not a change of the enumerator's requirement.**
`Modify_Res_Des` and `Free_Res_Des` on the enumerator's own basic
configuration would take the IRQ requirement away for good, in a
configuration `PCI.VXD` owns and may rebuild; a second configuration adds
to the list and takes nothing from it, and the priority order, read
before it is added, keeps the interrupt the preferred outcome. The filter
event (`CONFIG_FILTER`, which lets the devnodes above patch a child's
filtered list) belongs to the enumerator and the device drivers in the
branch, which this driver is not (record 21 section 6.1: nothing of this
driver sits on the bridge devnode), so it is not used.

## 5. What `PCI.VXD` requires, and the two forms

### 5.1 The arbitrated form (the design, section 4)

The hypotheses the design rests on, and what the leg reads for each:

- **That the configuration manager allocates a second basic
  configuration when the first cannot be met**, and that it prefers the
  lower priority number. The header's model and the public mirror's
  documentation say so in general; nothing says it for a devnode whose
  enumerator built the first configuration. Leg: the devnode starts after
  step 4, its `Allocation` holds the memory ranges and no IRQ; with the
  route present (section 8.2 item 3) the enumerator's configuration is
  the one allocated and no alternative exists, which shows the admission
  rule and not the preference between two configurations; the
  preference itself, two configurations on one devnode with the
  interrupt routable, has no fixture here (the route is made present
  only by a restart, which leaves no alternative) and stays **unread**
  (section 10), the threshold rule of N9 standing on the header's
  ordering alone.
- **That a configuration at `LCPRI_DISABLED` is never allocated**, so the
  transaction's incomplete state is never used. Leg: a configuration left
  disabled on purpose (the qemu flavour's test value stopping the
  transaction after step 2) is not allocated and the devnode keeps its
  problem.
- **That `PCI.VXD`'s start programs the base address registers from an
  allocation without an IRQ and writes no Interrupt Line.** The
  `CONFIG_START` prose says the handler looks at the allocated list; what
  it does with a missing element is inferred. Leg: the registers read
  programmed inside the port's window, the line unchanged.
- **That `Reprocess_DevNode` re-runs the allocation for a devnode with a
  problem.** The service's name and group; nothing more. Leg: the
  outcome after step 4, and `Setup_DevNode(READY)` where it does not.
- **That the services may be called from the member's call worker.**
  Record 21 section 6.1 established a worker thread as the one caller of
  CONFIGMG services (a passive thread of the system VM, blocking while
  the configuration manager is busy); the log-conf services are no
  different in kind from `Query_Remove_SubTree` (that `cfgmgr32.dll`'s
  Win32 mirror reaches the same services from a ring-3 thread is the
  hypothesis of section 2, not evidence here).
- **That `PCI.VXD`'s descriptors are what section 3 item 1 expects.**
  Whatever they are, section 4 copies them as read; the leg captures them
  on 98 SE and on ME for the record.

### 5.2 The forced form (the fallback)

If the leg shows that the configuration manager does not allocate the
alternative (the devnode keeps its problem after step 4 and after the
`Setup_DevNode` fallback), the second form is the forced configuration
the header provides for a user's Device Manager settings
(`FORCED_LOG_CONF`, `LCPRI_FORCECONFIG`), built by the same transaction
(created disabled, descriptors added, the kind's priority set last;
journalled with its kind, its handle's place read on the forced list
before each write; no forced configuration is **created** where the
devnode carries any forced configuration already, "Across generations
and starts", while the journal's own committed forced one is
reprocessed under a new generation as a basic one is) with fixed
ranges:

- **The descriptors.** One `MEM_DES` per memory descriptor captured from
  the enumerator's configuration, and only where that descriptor holds
  exactly one range (`MD_Count` 1): the header documents a descriptor's
  ranges as alternatives, which need not share a length, a bound or
  flags, and this form has no rule for choosing among them, so a
  descriptor with several ranges refuses the forced form (the arbitrated
  form copies it whole). The written descriptor: `MD_Count` 1, `MD_Type`
  `MType_Range`, `MD_Alloc_Base` and `MD_Alloc_End` 0 (a requirement; the
  configuration manager fills the allocated fields), `MD_Flags` as read,
  `MD_Reserved` 0, followed by one `MEM_RANGE` whose `MR_nBytes` and
  `MR_Align` are the captured range's, `MR_Min` the planned base and
  `MR_Max` the planned base plus `MR_nBytes` minus one (an interval that
  contains the whole register, inside the captured range's own `MR_Min`
  to `MR_Max`; a sum that overflows 32 bits refuses the plan), `MR_Flags`
  as read, `MR_Reserved` 0. Whether a forced configuration takes a requirement interval of
  one position or needs the allocated fields filled is [hyp] for the leg;
  the ring-0 layouts are `CONFIGMG.H`'s, never the modern mirror's.
- **The plan.** A pure placement (`tbt_brnum.c`, host-tested) over the
  captured requirements and the current allocations: the region record 21
  section 6.1.1 step 4 reserves for the base address registers of that
  bus (the aligned region at the bottom of the level), computed for the
  endpoint-only layout too; the other functions' current allocations on
  the same bus, read with `Get_Alloc_Log_Conf` (service 59) of each
  sibling devnode, and the bridges' windows below the member as the
  numbering journalled them, excluded; the planned ranges mutually
  disjoint and inside the port's window, largest alignment first. A plan
  that does not fit refuses the attempt.
- **The conflict test**, one per planned range, made **before the
  configuration is created** (the plan and its tests precede the forced
  form's step 1, so every refusal here leaves nothing behind):
  `Detect_Resource_Conflict(dn, ResType_Mem, data, len, &conflict, 0)`
  (service 112; the result byte is a `PFARCHAR`, not the mirror's
  `PBOOL`), and the form goes on only when the call returns `CR_SUCCESS`
  and the byte reads 0. The forced form's own refusals, each a state of
  the result table: a source descriptor with several ranges (EXCLUDED,
  "configuration"), a plan that does not fit or overflows (EXCLUDED,
  "placement"), a conflict test answering `CR_SUCCESS` with a nonzero
  byte (EXCLUDED, "conflict"), a conflict test refused by `CONFIGRET`
  (PENDING, re-read as a failed read), any forced configuration already
  on the devnode (EXCLUDED, "forced present"); none of them creates a
  configuration. A failure after step 1 and before step 3 is FAILED as
  in the basic form and leaves a **disabled forced leftover**, which
  then refuses every later forced creation on that devnode ("forced
  present") until a re-plug gives it a clean list - the forced form's
  recovery is the re-plug, and the viewer's line says so; a failure
  after step 3 (a `Reprocess_DevNode` refused or timed out) leaves a
  **committed** forced configuration at `LCPRI_FORCECONFIG`, eligible,
  which the next generation finds as a valid committed match and
  reprocesses once, as a basic one is. The test is a snapshot: a
  later change can still collide, which is one more reason this form is
  the fallback.

This form chooses addresses, which is why it is the fallback and not the
design: an address the arbitrator did not choose is one the next
`PCI.VXD` pass may disagree with. It is written here so that 42-A.15 has
it without a second design round; whether it is built is the leg's
verdict (section 10), and the owner's (section 9).

### 5.3 Undo and ownership

Section 4's transaction has no undo and needs none: an uncommitted
configuration is disabled and harmless, never eligible incomplete, and
stays ignored for allocation and reuse (a disabled **forced** leftover
also blocks any further forced creation on its devnode until a
re-plug, section 5.2); a committed one is complete and stays, reprocessed by the
next generation while the journal knows it and left alone, refusing the
device until a re-plug, after the journal is gone.
Nothing is ever freed, so no handle's identity is ever trusted for a
destructive call; the writes this driver makes to the handle it holds
are each preceded by a read of the handle's place, which catches a
reuse the reads can tell and neither a replacement indistinguishable by
them nor a reuse between the read and the write ("The transaction" step
2, section 10): the writes are to a handle this driver believes its own,
on evidence that is good and not conclusive. Nothing of the enumerator's is
changed by any step on purpose, so no other undo exists. A
devnode removed meanwhile (the tunnel down, the user's Device Manager)
takes its configurations with it, and its journal entry is dropped when
its hash no longer answers.

### 5.4 No pin: the MSI-only function

A function whose Interrupt Pin reads 0 is expected [hyp] to need nothing
from this driver on Windows 98 SE and ME: `PCI.VXD` builds no IRQ
descriptor and starts it with its memory (section 3 item 4), and Stor98
reads pin 0 and polls ("no interrupt pin" in its report). The design's
contribution is the reading: the pass counts a pin-0 NVMe function seen
below the member and its status, the diagnostic of section 7 records it,
and 42-V.6 reads one started and polling where the emulator can present
one (section 8.2 item 4). Until that leg is read the release notes say
the pinless case is unread, not that it works; if the emulator cannot
present one, the case stays [hyp] and is said so.

## 6. The pairing with Stor98's polling mode

Stor98 decides at its start, per controller (its README, "Polling mode";
`StorPollMode` 0, the default): it polls when "the controller has no
interrupt pin", when "Windows assigned it no interrupt", or when "its
interrupt line is not set"; otherwise it uses the interrupt. On Windows 98
SE and ME its decision takes **the Interrupt Pin and Interrupt Line of
the function's own configuration header and nothing else** (Stor98
`docs/PLAN.md`, "Polling mode": "The 9x decision still takes Pin and
Line, not the port's values"; the port's interrupt level and vector are
read into its report and decide nothing there). A function started by
section 4 keeps its header's pin and a line of 0 or `FFh`, so Stor98
polls it; the port's values for a function started with no IRQ element
(the 98 SE port put the IRQ into the level and left the vector 0 when an
IRQ was assigned, Stor98's measurement) are a measurement the leg records
beside the report, not a condition. Stor98 then polls at `StorPollBusyUs`
while busy (1 ms by default on 98 SE and ME) and `StorPollIdleMs` when
idle (1 s). Its report value `StorInterrupt <bus>.<device>.<function>`
under `Services\stor98` reads `pin N line 0` or `line 255`, the port's
values as handed, and `polling` with its reason, and is the evidence the
leg records beside this driver's diagnostic. Stor98's own README measured
its polling mode on 98 SE in the emulator (QEMU TCG, polling forced, 8
MiB written and read back at queue depth 1: 12 to 13 MB/s at 64 KiB
requests against 13 to 16 with the interrupt), figures it says show the
settings relative to each other and not what real hardware does; the
release notes say that a device started this way runs in Stor98's
polling mode and point at that section, promising no figure.

What Stor98 does not need from this driver: no new table field, no
statement, no value. The link-state channel of record 21 section 7.2 is
unchanged, and the `UP` entry for the function is published before the
enumeration as today, whatever the interrupt outcome. What Stor98's side
owes is tracked in its own plan (its `docs/PLAN.md`, "Polling mode"); the
joint reading is 42-V.6's.

A function of another class or driver, which the pass never touches
(an xHCI controller on `xhci98.sys` excepted since task 48.36, section
14), stays as `PCI.VXD` left it, and the viewer (XHCITR, record 25) shows its
problem code as it shows any other.

## 7. Registry value, diagnostic, log and counters

- **`XhciCmNoIrqStart`** (`REG_DWORD`): read as every `XhciCm*` value is
  read from `3.1.0.0` (task 42-A.17): the machine-wide key
  `HKLM\System\CurrentControlSet\Services\USB` first, and where absent
  there the NHI's driver key; once at start. Not written by the INFs (as
  `XhciCmBridgeNumbering` was not; record 21 section 8's table gains the
  row with the code); from task 44.46 (owner, 2026-10-26) the x86 INF's
  98 SE and ME install writes both at 1, only where missing. 1 (the default; absent in both places reads as 1)
  runs section 4 for the functions its rules admit; 0 turns it off,
  leaving such a function as it came up. Any other value is refused and 1
  stands. Windows 98 SE and ME only; the NT route never reads it. The
  installer never asks for it (record 23 section 8.1's never-asked set),
  and the window offers it under "Advanced..." as a 98 SE and ME row
  (record 26 section 5.5) where a user may change it, in which case the
  installer writes the machine-wide value as it writes every setting a
  user changes there (record 26 section 5.4).
- **The diagnostic** `XhciCmNoIrqDiag` (`REG_BINARY`, twelve dwords, in
  the NHI's hardware key beside `XhciCmBridgeDiag`, written by the thread
  from the transaction's state as the numbering's is from the worker's
  copy): the last attempt's member
  location, the function's location, its header's pin and line as read,
  the interrupt outcome the candidate rule used, the problem before, the
  step reached and its `CONFIGRET`, the status and problem after, the
  count of attempts, of candidates refused by rule (packed), and of pin-0
  NVMe functions seen with their started count; so a release build's
  outcome can be read from a registry export, as record 21 section
  6.1.1's can.
- **The log**: `cm.reenum.9x.noirq` per step (step in the high byte,
  `CONFIGRET` in the low word), `cm.reenum.9x.noirq.refused` with the
  rule, `cm.reenum.9x.noirq.after` with the status read at the next
  pass, and the devnode-handle records through `CmLogNoteAddress` as the
  other handle records of the 9x route go (level 4 alone);
  `cm.reenum.9x.noirq.hold` and `cm.reenum.9x.noirq.credit` since task
  48.36 (section 14.4).
- **Counters** (record 21 section 8's `cm.*` set, the 9x route's block):
  candidates, attempts, started, still unstarted, refused by each rule of
  section 4, left-over refusals, disabled leftovers, journal entries
  marked gone, timeouts, late results dropped; windows held and calls
  credited since task 48.36 (section 14.4).

## 8. Verification

### 8.1 Host vectors

The pure half - the candidate rules over a devnode's status, problem,
header, driver-key values and interrupt outcome; the descriptor copy
(every `ResType_Mem` descriptor copied byte for byte with its size
checked, the IRQ one dropped, any other type refusing); the priority
check; the swizzle test over an ancestry of bridges for the endpoint-only
and the chained layouts, shared with `tbt_brnum.c`; the per-device state
machine over generations and reused handles; the journal; the diagnostic's
layout; the forced form's plan - is DDK-free and runs in
`test\test_cm_cfgwk.c` or a suite of its own over a fake
configuration-manager tree, one case per row of the result-to-state
table and these besides: a function recorded UNROUTABLE (admitted),
PORT_NO_LINE (admitted), ROUTED_WRITTEN or ROUTED_ALREADY (excluded),
UNKNOWN by a write not held (excluded), UNKNOWN by a failed read or
Retry Status (classified afresh: admitted where the fresh reads say so,
PENDING while they fail), no record and a swizzle that lands on the
port's pin (excluded), no record and a swizzle elsewhere (admitted), pin
0 (not a candidate, counted), a line set (excluded), class `010801h` or
another class (excluded), `PortDriver` not `stor98.mpd` (excluded), no
driver bound and exactly one binding value (PENDING, re-read, EXCLUDED at
the window's end), no installer record or `0.3.x` recorded (excluded), an
I/O register (excluded), a configuration with no IRQ descriptor
(excluded), an original configuration at a priority below `0x5000`
(passes), at exactly `0x5000` and at `0x9000` (excluded, not of this
record's form), ten memory descriptors (excluded), a descriptor larger
than the buffer (excluded), a second attempt in the same publication
generation (none begun), a new generation (read again, one reprocess where the last transaction
was committed, another transaction where it was not),
a reused handle with another hash (a new device), an end of the
transaction after each of steps 1 and 2 (a disabled leftover, never
enabled, never freed, ignored by every later reading and counted) and
after step 3 (a complete configuration kept and reprocessed by the next
generation), a new generation while ATTEMPTED (no second one added), a
fourth transaction for one devnode (excluded, "attempts"), a devnode
that reads no driver then `stor98.mpd` then Code 29 within one window
(PENDING, PENDING, candidate), a late call result taken by id and
accounted and not acted on, a late result after the member's state ended
(dropped), the handle's place read before each write with the devnode gone or
answering another hash meanwhile (forgotten, the journal entry retired,
nothing written), with the devnode present under the same hash and its
configuration missing, at another priority or on the other kind's list
(FAILED "handle", nothing written), a basic
list of disabled configurations alone (excluded, no source read), a
committed handle not found at the next generation (marked gone, judged
as new), two abandoned transactions then a committed one stopped before
step 4 (the reprocess made, no "attempts" refusal) and three created
then a fourth wanted (refused), a forced transaction's handle read on
the forced list, the fake clock over the largest source with nine
descriptors and two devices on one member (inside the budget at the 50
ms cadence) and a call that blocks past it (FAILED "timeout", the late
result accounted), a result inside the budget after the window closed
(the next step submitted), the window closing with an entry ACTIVE and
with one ATTEMPTED (both running to their deadline) and with one PENDING
(excluded), a reprocess-only attempt given its own deadline and expiring
(FAILED "timeout"), a loss or a member gate closing before each kind of
submission (FAILED "controller", nothing submitted, the outstanding
result accounted), a forced transaction failing before and after its
commit (the disabled leftover refusing creation; the committed one
matched and reprocessed), the binding changed or the device disabled
between steps 2 and 3 and between steps 3 and 4 (FAILED "admission",
nothing more written, the leftover as the table says, never a READY of
a disabled device), a devnode gone during a transaction (forgotten, the
journal entry retired, no "handle" failure) against one re-created with
the same hash (FAILED "handle"), an outcome read failing in ATTEMPTED
(read again) and one still unstarted before the deadline (read again), a replacement configuration with the same
handle on the same list at `LCPRI_DISABLED` (passing every check: the
undetectable case, asserted undetected), a committed handle found at
`LCPRI_DISABLED` and at `0x9000` at the next generation (marked gone,
treated as any other, judged as new), a forced committed handle found at
`LCPRI_FORCECONFIG` (a valid match) and at another priority (gone), the
forced form's refusals before creation (each its state, nothing
created) and a conflict test refused by `CONFIGRET` (PENDING), a new start meeting a
committed configuration at `0x5000` with no journal (excluded,
`refused.left-over`) and a disabled one (ignored, a new transaction), a
devnode with one disabled and one committed configuration of this
driver's, any forced configuration present refusing a new forced
creation while a committed forced match is still reprocessed, a
device a rule now excludes after a committed transaction (its
alternative left untouched) and one whose reading is pending (no
reprocess spent, re-read), the forced form's refusal of a multi-range
descriptor, and the forced plan's disjointness and overflow checks.

### 8.2 The legs (42-V.6, "42-A.15 on 98 SE and ME with the IRQ withheld")

On the emulator, 98 SE and ME, with the bundled Stor98 `0.4.0.0` or later
installed by the installer (so the record of rule 4 exists), the
descriptors `PCI.VXD` built captured on each system (the qemu flavour
logs each copied descriptor) before anything else is judged:

1. **The IRQ withheld, the endpoint on the port's bus.** The admission
   produced by the production classifier from real inputs, never by a
   test override of its outcome: either the tunnel bridge's own Interrupt
   Line unset (the emulator's bridge with no routing entry, so the port
   has no line and the function is PORT_NO_LINE), or a function whose
   device number and pin swizzle onto a pin that is not the port's
   (UNROUTABLE), with the function's line unset; the matching swizzle
   beside it as the negative case (the classifier refuses it: EXCLUDED
   "interrupt" where the line reads set, or no candidate where the
   swizzle lands on the port's pin; no line write is expected here,
   since the numbering's stage runs only behind a bridge chain and its
   write is read in item 2). A tunnel up:
   before the fix the NVMe at Code 29 (or 12); with it, started,
   `Allocation` with the memory ranges and no IRQ, the Interrupt Line
   unchanged, Stor98's `StorInterrupt` reading `polling` with "no
   interrupt" or its line reason, the port's values recorded, a file
   written and compared on the drive, the removal and re-plug, and the
   diagnostic read from the registry export.
2. **Behind a bridge chain** (the emulator's q35 switch model behind a
   tunnel port, or the `usb4-generic` layout with a two-level chain,
   record 21 section 6.1.1's emulator readings), two cases: the IRQ
   withheld as in item 1 (the function's swizzle elsewhere, or the port
   with no line), the walk reaching the NVMe two levels down with the
   path form resolved and the same outcome as item 1; and the routed
   case - the port's line valid, the function's swizzle landing on the
   port's pin, its own line unset - where the numbering's interrupt stage
   writes the line (observed in the qemu flavour's trace and in the
   function's header), the enumerator's configuration is allocated with
   that IRQ, the function is never a candidate (ROUTED_WRITTEN) and no
   alternative is added.
3. **The route present.** The same device with the port's line routed:
   the enumerator's configuration allocated with its IRQ, the devnode
   never a candidate (no problem), **no alternative added** (the
   diagnostic's refusal count says why: not a candidate), Stor98 with the
   interrupt.
4. **Pin 0**, where the emulator can present an NVMe function without an
   interrupt pin: started by `PCI.VXD` alone, Stor98 polling with "no
   interrupt pin", the diagnostic counting it; otherwise the case stays
   unread and the release notes say so.
5. **A restart with the device present at POST**: `PCI.VXD`'s own
   configuration with the BIOS's routing, nothing of this record in the
   devnode's Enum key (`LogConfig`, `ForcedConfig`); and the Enum key read
   after item 1 as well, for a configuration the manager may have
   persisted on its own.
6. **The transaction's safety** (the basic form): the qemu flavour's
   test value stopping the transaction after step 1 and after step 2 (a
   disabled configuration left: the devnode keeps its problem, the next
   generation's transaction ignores it and adds its own, the device
   starts with a disabled and a committed configuration on its list, and
   a devnode carrying three of this driver's configurations is read for
   what the configuration manager keeps for them), a stop of the
   connection manager between steps (nothing freed, nothing waited for),
   a host-interface loss injected between creation and commit with the
   claim and the devnode still present (FAILED "controller", no further
   call, the result accounted, the device judged anew after the loss
   resolves), the device disabled in Device Manager between creation
   and commit (FAILED "admission", the disabled leftover, the device left
   disabled and never made ready), and a Device Manager Refresh during
   the window. Where the
   forced form is built: the same stops before its commit (a disabled
   forced leftover refusing further forced creation until a re-plug,
   which starts the device) and after it (the committed forced
   configuration reprocessed by the next generation).
7. **Fail closed**: a hand-installed Stor98 (no installer record), an
   installer record of `0.3.x`, a devnode not yet bound at the window's
   end (the wizard cancelled), and an update of the bundled Stor98 with
   its shutdown still pending (the record reads the new version while
   the old image may still load): what each does, the refusal counted
   where there is one.
8. **`XhciCmNoIrqStart` 0**, machine-wide and per driver key: the
   function left at its problem code; the machine-wide value winning over
   a per-controller 1.
9. **A Device Manager Refresh** after the window closed, with the
   function unstarted: nothing of this driver runs (the promise is
   narrowed to its own re-enumerations and re-plugs), the problem code
   shown by the viewer; then a re-plug starting it.
10. **ME** as a hypothesis the leg tests, as record 27 treats it: the
   same service table and the same `Get_Version` guard (record 21 section
   12); items 1 to 3 read there before the record says ME.
11. **Across a stop and start of the connection manager** (Device
   Manager's disable and enable of the NHI): after step 3 and before step
   4, then the start: the committed configuration refused as left over
   (`refused.left-over`), the device at its problem code with the
   viewer's "re-plug the device", and the re-plug starting it afresh;
   after step 1 or 2, then the start: the disabled leftover ignored, a new
   transaction made, the device started with one disabled and one
   committed configuration in its list. The preference between two
   eligible configurations with the interrupt routable is not read here
   (section 5.1): no fixture makes the route present without a restart,
   which leaves no alternative.
12. **Re-creation and late results** (the qemu flavour's trace): a unit
   removed and re-created by `PCI.VXD`'s own Refresh between a
   transaction's steps, recorded by what the read before the next write
   observes: the devnode gone or under another hash (the entry
   forgotten, the journal entry retired), or present under the same
   hash with its configuration's place changed (FAILED "handle"),
   nothing written to the new devnode's configurations either way; and
   after a committed transaction with
   the member re-enumerated once more (the next publication generation's
   gate not finding the committed handle as a valid match, the entry
   marked gone, the device judged as new: rule 1's check runs at a new
   generation, not inside the window); a re-creation that no reading
   observes is not claimed detected (section 10); a call returning after
   the admission window closed but inside the transaction's budget (the
   transaction going on to its next step) and one returning after the
   budget (journalled, not acted on); a call worker held in a wizard
   past the budget, its late `Add_Empty_Log_Conf` journalled and ignored,
   its late priority change marked committed and reprocessed by the next
   generation; the same with the NHI stopped meanwhile (the results
   dropped, the next start's gate reading what was left).
13. **The detached-call overlap** (the basic form; the qemu flavour's
   test value holding the call worker inside step 3's priority change
   across a stop and start of the connection manager): the new start's
   gate reading the old configuration disabled and making its own, the
   held call then committing the first, both configurations recorded
   from the devnode's list, and the allocation and the start that follow
   read - the reading the harmlessness of section 4 rests on.

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
| N1 | Where the function is started | a per-member transaction on the connection-manager thread's pass, in record 21 section 7.2's 9x execution model, each call made by the member's call worker through the R6f handoff word; its own window and per-device state after the member's re-enumeration result, independent of the removable mark and of `XhciCmSafeEject` | 4 |
| N2 | The form | a second basic logical configuration at `LCPRI_SUBOPTIMAL` with the enumerator's memory descriptors copied as read and no IRQ descriptor, created disabled and enabled last, allocated by the configuration manager; the forced form with fixed ranges kept as the fallback the leg decides | 4, 5 |
| N3 | Which functions | class code `010802h` alone, a type 0 header with no I/O register, a pin and a line reading 0 or `FFh`, a devnode with a configuration problem, bound to `stor98.mpd` with an installer record of Stor98 `0.4.0.0` or later, its interrupt recorded UNROUTABLE or PORT_NO_LINE; never a bridge, never another class or driver, never UNKNOWN; failing closed | 1.3, 4 |
| N4 | Persistence | none: no `CM_SETUP_WRITE_LOG_CONFS`, no forced configuration in the registry; a device present at POST is `PCI.VXD`'s | 4 |
| N5 | The switch | `XhciCmNoIrqStart`, default 1, 98 SE and ME only, read machine-wide first then per driver key (42-A.17), written at 1 where missing by the x86 INF's 98 SE and ME install (task 44.46; not INF-written before), never asked by the installer, editable under "Advanced..." and written machine-wide when changed | 7 |
| N6 | The MSI-only function | nothing done; a hypothesis until its leg is read, recorded in the diagnostic | 5.4 |
| N7 | Retries and budgets | one configuration transaction per devnode per publication generation of its member and at most `CM_NOIRQ_ATTEMPTS` (3) configurations created in the member's life, the bound never refusing an owed reprocess; a new generation reads the device again, reprocesses the journal's committed alternative once where it is still found, marks it gone where it is not, and may create another after an uncommitted or gone one; pending readings are re-read at every pass of the 10 s window, terminal exclusions once; a transaction runs on its own 30 s budget at the 50 ms cadence, completion-driven | 4 |
| N8 | What this driver adds, and never frees or recognises | no configuration of anyone's is ever freed (`Free_Log_Conf` is not called), and no configuration is ever recognised as this driver's except through its journal while the member's state lives; a configuration at `LCPRI_DISABLED` is ignored by its priority alone for allocation, reuse and the threshold, a disabled forced one still blocking any further forced creation until a re-plug; a committed alternative an earlier start left refuses the device until its re-plug; before each write to the handle it holds the transaction reads the handle's place again on the list of its kind, which catches what those reads can tell and not a replacement indistinguishable by them nor a reuse between the read and the write; a committed alternative is reused only as a valid committed match, by kind and committing priority | 4, 5.3 |
| N10 | The update-pending exception | admission on the installer's recorded version although, between a Stor98 update on 98 SE or ME and the shutdown the installer asks for, a devnode may still load the older image, no documented mechanism telling which image a devnode loaded; the older image's behaviour unmeasured until the leg, and the release notes saying a drive plugged in during that window may not start until the shutdown | 4 |
| N11 | The memory-only form (task 44.30) | where `PCI.VXD`'s own configuration holds the memory alone and the problem is the 29 it set at the devnode's creation, no configuration is added and the devnode is reprocessed once a generation, under every other rule of section 4 unchanged (the switch, the header, the binding and the installer record, the interrupt outcome, the gate over the existing configurations, the changeable conditions read again before the call, the one `READY` fallback, the budget); read on 98 SE in the emulator, the orchestrator's for the owner to confirm | 12 |
| N9 | The existing priorities | read before anything is added, one priority read per configuration; refused unless every basic configuration of the devnode not at `LCPRI_DISABLED` and not the journal's own committed one reads below `LCPRI_SUBOPTIMAL` | 4 |

## 10. Open, and the risks

- **Task 44.30** (section 12): `Setup_DevNode(READY)` alone not read
  (the reprocess sufficed); rule 4's installer record on an install that
  bound no Stor98 device, decided by the owner (section 12.3).
- Whether the configuration manager allocates a second basic
  configuration for a devnode whose enumerator built the first, whether
  `Reprocess_DevNode` is the call that makes it, and whether a
  configuration at `LCPRI_DISABLED` is never allocated (section 5.1): the
  leg's first readings; the forced form is the fallback.
- Whether `PCI.VXD`'s start handler programs a function from an
  allocation with no IRQ element without writing an Interrupt Line, and
  what descriptors `PCI.VXD` builds (section 3 item 1).
- Whether the configuration manager persists a logical configuration it
  was given at run time (`LogConfig` or `ForcedConfig` in the devnode's
  Enum key) without `CM_SETUP_WRITE_LOG_CONFS`; if it does, the transaction
  must delete it at the devnode's departure, and which service does that
  is then to be read (`Write_Registry_Value` with a zero length, or the
  VMM's own registry services the Stor98 locator already uses).
- Whether the emulator can present an NVMe function with Interrupt Pin 0
  (section 5.4) and withhold the routing for the tunnel bridge's bus
  (section 8.2 item 1); if it cannot, those cases stay unread and the
  record says so - no test value stands in for the classifier's outcome.
- Whether the forced form's requirement interval is what the
  configuration manager expects of a `FORCED_LOG_CONF`, or the allocated
  fields must be filled (section 5.2).
- Whether a configuration this driver added survives the connection
  manager's stop and start on the devnode (the left-over refusal's case),
  what the configuration manager keeps for a disabled empty or partial
  configuration left on a devnode for its life, and whether a
  configuration handle is ever reused for any other configuration of a
  re-created devnode between a transaction's read of the handle's place
  and the write that follows it (step 3's admission reads and its
  commit, the reprocess and the READY fallback included), and whether a
  re-created devnode can
  carry a replacement configuration with the same handle on the same
  list at `LCPRI_DISABLED`, which the reads cannot tell from this
  driver's own (the residuals of "The transaction" step 2, which no
  documented service closes).
- A removal and re-creation of a devnode that leaves its handle, its
  instance id and the member's publication generation all unchanged
  between two passes: undetected by design, the journal staying with the
  old incarnation until a reading contradicts it ("Across generations
  and starts", rule 1).
- The preference between two eligible configurations with the interrupt
  routable (section 5.1): unread, no fixture.
- A function directly on the port's secondary bus whose swizzle lands on
  the port's pin with its line unset: record 21 section 6.1.1's stage
  runs only behind a bridge chain, so nothing writes its line; the
  emulator's endpoint-only layout gets its routing from the BIOS, and
  whether such a case exists on hardware is unread. This record excludes
  it (routable), and does not start it.
- Whether an older `stor98.mpd` left until the update's shutdown is ever
  started this way on a real update (section 4 rule 4), and how it then
  behaves; the leg's item 7.
- Windows ME: the same services on the same guard, untested until the
  leg.
- A function whose pin the stage cannot route on real hardware: no device
  at hand presents one; the legs of section 8.2, where they pass, give
  the design an emulator-only standing until one does, and the release
  notes say so.
- Stor98's behaviour when it is started with no IRQ element on 98 SE and
  ME is its README's claim, read by Stor98's own legs on a forced polling
  mode; the joint leg here is its first reading with the interrupt
  actually absent at allocation, on both systems.

## 11. Files

Task 44.30 (section 12) is to change `src\cm\cm_noirq.c` and
`cm_noirq.h` (the form, its counter) and `test\test_cm_noirq.c` (its
vectors) alone.

| File | What |
|---|---|
| `src\cm\cm_cfgmg.h`, `cm_cfgmg.c` | the service numbers added with their table lines - `Add_Empty_Log_Conf` 24, `Get_First_Log_Conf` 26, `Get_Next_Log_Conf` 27, `Add_Res_Des` 28, `Get_Next_Res_Des` 31, `Get_Res_Des_Data_Size` 33, `Get_Res_Des_Data` 34, `Get_Alloc_Log_Conf` 59, `Read_Registry_Value` 62, `Reprocess_DevNode` 92, `Get_Set_Log_Conf_Priority` 106, `Detect_Resource_Conflict` 112 (lines 557, 559, 560, 561, 564, 566, 567, 592, 595, 628, 642 and 651 of the table that opens at line 532, counted as `legal-provenance.md`'s CONFIGMG row counts them); the flag and priority values (`BASIC_LOG_CONF` 0, `FORCED_LOG_CONF` 4, `PRIORITY_EQUAL_LAST` 0, `LCPRI_SUBOPTIMAL` `0x5000`, `LCPRI_DISABLED` `0xFFFF`, `LCPRI_FORCECONFIG` 0, `CM_GET_LOG_CONF_PRIORITY` 0, `CM_SET_LOG_CONF_PRIORITY` 1, `CM_REGISTRY_SOFTWARE` 1, `ResType_Mem` 1, `ResType_IRQ` 4, `ResType_All` 0, `MType_Range`, `CM_REPROCESS_DEVNODE_ASYNCHRONOUS` 0, `CR_NO_MORE_LOG_CONF` `0x0E`, `CR_NO_MORE_RES_DES` `0x0F`, `CM_PROB_NORMAL_CONFLICT` `0x0C`, `CM_PROB_PARTIAL_LOG_CONF` `0x10`, `CM_PROB_NO_VALID_LOG_CONF` `0x1B`, `CM_PROB_HARDWARE_DISABLED` `0x1D`); the thunks behind the `Get_Version` guard |
| `src\cm\cm_noirq.c`, `cm_noirq.h` (task 42-A.15) | the pure half: the state machine of operations above, every rule and table of section 4 |
| `src\cm\cm_cfgwk.c`, `cm_nvlinkos.c` (or the file 42-A.12 gives the 9x transactions) | the no-IRQ transaction on the thread's pass (the window, the per-device state and journal, the rules across generations, the steps as operations for the member's call worker with the handle's place read before each write, late results by id), the descendant walk and the candidate rules as operations, the outcome read, the diagnostic copy; the numbering's per-function interrupt outcomes kept per member and generation; the `_RegQueryValueEx` thunk beside the locator's (VMM service `0x151`, `VMM.H` line 2225, the signature at `VXDWRAPS.H` line 439) for the installer's `Version` record |
| `src\cm\tbt_brnum.c`, `tbt_brnum.h` | the per-function outcomes returned to the caller; the swizzle test over an ancestry; the fallback's placement plan, if built |
| `src\cm\cm_statb.h`, `cm_statos.c` | `XhciCmNoIrqDiag` |
| `src\cm\cm_vals.c` | `XhciCmNoIrqStart`, machine-wide first (42-A.17) |
| `test\test_cm_cfgwk.c` (or a new suite), `test\run-host-tests.cmd` | section 8.1 |
| `docs\contributing\legal-provenance.md` section 4 | the CONFIGMG row extended with the new numbers and lines; a new row for the VMM registry services the Stor98 locator uses (`_RegOpenKey` `0x148`, `_RegCloseKey` `0x149`, `_RegCreateKey` `0x14A`, `_RegDeleteKey` `0x14B`, `_RegSetValueEx` `0x152`, `_RegCreateDynKey` `0x176`, `VMM.H`'s table from line 2216 and `VXDWRAPS.H`'s signatures), which no row carries today, with `_RegQueryValueEx` `0x151` added to it, each a text read of the headers as the CONFIGMG row's; Stor98's INF lines `PCI\CC_010802`, `DevLoader` and `PortDriver` cited as the ownership evidence |
| design record 21 sections 6.1.1 and 8; design record 26 sections 5.4 and 5.5; `docs\using\release-notes.md`; `docs\using\tuning.md` | the pointer, the value's row and its machine-wide reading, the release-notes lines (Stor98's polling mode for a device started this way; a hand-installed Stor98 not started this way; the pinless case unread until its leg; the emulator-only standing) |

## 12. The memory-only form (task 44.30)

### 12.1 What was read

**Static** (`legal-provenance.md` section 4, the rows "`PCI.VXD`'s Code
29 on 98 SE and ME" and "CONFIGMG's `Disable_DevNode` and the problem it
clears"): Windows 98 SE's `PCI.VXD` 4.10.2222 and `CONFIGMG.VXD`, and
ME's `PCI.VXD` 4.90.3000 for the one path compared, read with an LE
parser and a disassembler; nothing executed.

1. **Where.** At the parent's `CONFIG_ENUMERATE`, `PCI.VXD` scans the
   bus, and a per-function callback builds each new devnode's basic
   configuration: the interrupt first, then the memory and I/O base
   address registers. For the interrupt it reads Interrupt Pin and
   Interrupt Line; a pin other than 0 is looked up in the routing table
   (the BIOS's `$PIR` or the registry's copy), by bus and device, walking
   up through PCI-to-PCI bridges with the standard swizzle. A link found
   gives an IRQ descriptor of the link's mask; none, with a line of 1 to
   15, an IRQ fixed to that line (record 21 section 6.1.1's written line
   is this case); none with a line of 0 or `FFh` gives **no IRQ
   descriptor**, and when the function's command register has neither
   I/O nor memory decode on and it is neither a display device nor a
   CardBus bridge, `Disable_DevNode(dn, CM_DISABLE_HARDWARE)`.
2. **What it sets.** CONFIGMG's `Disable_DevNode` with
   `CM_DISABLE_HARDWARE` sets problem 29 (`CM_PROB_HARDWARE_DISABLED`)
   and does not count a disable.
3. **Once.** The callback builds only a devnode it creates: a function
   whose devnode exists with a basic configuration is skipped, so a later
   re-enumeration of the parent (this driver's, or a Device Manager
   Refresh) does not disable it again.
4. **Nothing later refuses it.** `PCI.VXD`'s `CONFIG_FILTER` for a child
   returns the default; its `CONFIG_START` programs the allocated
   resources - the Interrupt Line only from an allocated IRQ - and fails
   only a failed power-state change.
5. **What clears it.** `Reprocess_DevNode` and `Setup_DevNode(READY)`
   both reach CONFIGMG's routine that clears a devnode's problem whatever
   its code (problem 22 set again only where the disable count is not
   zero) and re-runs its configuration; the paths followed are the ones
   these calls take, not every path of CONFIGMG.
6. **The machine-wide case.** The routing table is dropped for the whole
   machine when a bus-0 function with a pin has no entry in it (its
   validation at load) - the reading behind `runs/run-43.md` 43.20's kit
   finding, inferred from the rule read, not observed.

**Runtime** (`runs/run-44.md`, "44.30"): on 98 SE in the emulator, the
NVMe of an enclosure directly below a tunnel port at 00:1Ch, where the
BIOS's table has no entry, bound to Stor98 with the installer record
present: problem 29, one basic configuration at `LCPRI_NORMAL` holding
one memory descriptor and no IRQ (as 42-A.15 read). With a reading build
that took the reprocess-only attempt for it, `Reprocess_DevNode`
answered `CR_SUCCESS`, the next status reads showed no problem and a
later one `DN_STARTED`; Stor98 recorded `pin 1 line 0 msi no msix no port
0/0 polling (interrupt line not set)`; the volume listed and a 16 MiB
file written to it compared equal. `Setup_DevNode(READY)` was not
reached.

### 12.2 The form

A function every candidate rule of section 4 admits (rules 1 to 5 as
they stand), whose problem as admitted is 29, and whose source
configuration ("The existing configurations", the gate unchanged) holds
**memory descriptors only** - one or more of `ResType_Mem`, no IRQ, I/O
or DMA, the every-type read meeting exactly those - is the
**memory-only form**. Nothing is added to it: the attempt is section
4's reprocess-only attempt ("Across generations and starts", rule 1),
run as that one is:

1. ACTIVE on its own deadline (`CM_NOIRQ_TXN_MS`), the admission inside
   the window as for any attempt.
2. The changeable admission conditions read again (step 3 of "The
   transaction"): rule 2 (unstarted, a configuration problem; a device
   the user disabled meanwhile reads 22 and is not reprocessed) and rule
   4 (the binding still `*IOS` and `stor98.mpd`); one no longer met is
   FAILED "admission".
3. `Reprocess_DevNode(dn, CM_REPROCESS_DEVNODE_ASYNCHRONOUS)`;
   ATTEMPTED.
4. The outcome read at each pass (step 5): `DN_STARTED` with no problem
   is DONE, the Interrupt Line read and logged (read 0 in the leg);
   the problem still there after `CM_NI_READY_AFTER_MS`, the one
   `Setup_DevNode(READY)` fallback after the same reads; the deadline
   FAILED "timeout".

Why this and not a configuration: the configuration `PCI.VXD` built is
already the one section 4 would add (memory, no IRQ), and what stands
between it and an allocation is the problem `PCI.VXD` set once, at the
devnode's creation, which nothing sets again (12.1 items 3 and 4).
Problems 12, 16 and 27 with such a configuration are not this case and
stay EXCLUDED "configuration".

**Bounds.** One such attempt per devnode per publication generation, by
the journal's reprocess mark (a journal entry for the devnode that
holds no configuration, so `CM_NOIRQ_ATTEMPTS`, which counts
configurations created, is untouched). The mark is tested only for a
PENDING entry that has passed every rule and every descriptor check:
an entry ACTIVE or ATTEMPTED runs to its deadline, and DONE, EXCLUDED
and FAILED stand for the generation, as section 4 has them; a PENDING
entry of the form whose devnode's mark this generation already took (the
devnode met as new again within it) is EXCLUDED "attempts". The next
generation (a re-plug is a new devnode; this driver's next
re-enumeration of the member) judges the device anew. Counted: a
candidate, a reprocess, and `MemOnly`.

**What it writes.** The reprocess, and the fallback where it is
reached; no configuration added, changed or freed, no line written,
nothing persisted. N8's rules have nothing to hold here.

**What it overrides, and why that is safe.** `PCI.VXD`'s disable says
the BIOS gave the function no interrupt (12.1 item 1's condition). The
start is made only for a function bound to a Stor98 the installer
recorded at `0.4.0.0` or later, which polls when it finds no line (rule
4, unchanged), so its driver needs nothing that was withheld; the line
stays as it was ([static] `CONFIG_START` writes it only from an
allocated IRQ; [obs] 0). The user's own disable is never undone: it
reads 22 before the call (step 2), and a disable count above zero makes
CONFIGMG set 22 again rather than start the device [static].

**Across a restart and a Refresh.** A function present at POST is
`PCI.VXD`'s afresh, with whatever routing the BIOS gives; nothing of this
form persists. A Refresh after the start re-enumerates the parent; where
it keeps the existing devnode and its basic configuration, `PCI.VXD`
skips it (12.1 item 3) and the function stays started [static,
inferred; no leg has read a Refresh after the start]. A Refresh that
removes and re-creates the devnode gives a new one that `PCI.VXD` judges
afresh - at problem 29 again only where 12.1 item 1's conditions hold
again, its decode off among them [inferred, unread]; such a devnode is
seen by this driver only inside a window, the current one if it is
still open, otherwise the next (section 4, "When"), with the limits on
telling a re-created devnode from the old one that section 4 states.

**ME.** Its `PCI.VXD` takes the same path [static, compared]; the leg
reads it (section 8.2 item 10).

### 12.3 Rule 4's installer record, as installs leave it

On the 98 SE image read here, installed by the `3.1.0.0` package with
no drive attached, `HKLM\Software\xhci98\Stor98` held `PreExisting` and
no `Version`: record 26 section 6.3 writes the version in a run that
installed or updated the bundled Stor98 on a device or its file
(`s98_record_version`, `xhciinst\inst_eng.c`), and that run bound none.
A drive first plugged in after such an install is then refused "not
Stor98's" by rule 4 until a later run of the installer records the
version, so the form started no such drive. This record does not change
rule 4, which N10 rests on. **The owner's decision of 2026-10-25**
(`roadmap-usb4.md`, decisions table, "44.30"): the installer records
the version whenever it stages the bundled Stor98 on 98 SE or ME, drive
present or not (record 26 draft 43, section 6.3, "The record"), so a
drive first plugged in after such an install meets rule 4. Rule 4's
`Version` then says the bundled release was installed or staged; a run
that owed the file update of an older installed `stor98.mpd` and did not
make it writes no value from the copy (record 26 draft 43), so N10's
exception stays the one it names - an update made and its shutdown
pending. The legs of
section 12.4 imported the record by hand, as 42-A.15's did; the leg of
the installer change installs with no drive attached and plugs the
drive afterwards, with nothing imported.

### 12.4 Host vectors and legs

Host vectors (`test\test_cm_noirq.c`, `test_memonly` and `test_gate`):
the form at problem 29 reprocessed with nothing added, placed or
written, then DONE with the line read; problems 12, 16 and 27 with the
same configuration, and an IRQ descriptor with no memory one, refused
"configuration"; the `READY` fallback and the deadline with nothing
added; the same generation not reprocessed twice, a new generation
reprocessed once more with the attempts bound untouched; the user's
disable and another driver met before the call (FAILED "admission"); the
window closed before the form is reached; two memory descriptors; the
switch 0 and no installer record.

Legs, on 98 SE and ME in the emulator with the IRQ withheld (the tunnel
bridge at 00:1Ch): problem 29 before; the form taken, started on its
memory alone, Stor98 polling, a file written and compared; an unplug
and re-plug (a new devnode) started again; `XhciCmNoIrqStart` 0 leaving
it at problem 29; and the routed control (the tunnel bridge at 00:05),
where the function starts on its IRQ and is never a candidate.

## 13. A function directly on the tunnel port's bus (task 45.18)

### 13.1 What was read

On 2026-10-30 (`runs/run-45.md`, "45.18", the 98 SE subsection) task
45.18's legs on the q35 98 SE base used the emulator's software
connection manager host (`-Usb4 adl`). Its tunnel ports are QEMU's
PCI Express root ports 00:05.0 and 00:06.0, numbered by the firmware at
boot (00:05.0: secondary bus 1, subordinate 9, Interrupt Line 10, pin
A). A plain NVMe router was plugged at the desktop. Its controller (QEMU's
`1B36:0010`) sits directly on the port's secondary bus, at 01:00.0, pin
A, Interrupt Line 0 (`info pci`), with no bridge between. What followed:

- the worker's numbering found no bridge below the port
  (`cm.brnum.9x.outcome=00002800`: 00:05.0, `XHCI_TBT_BRNUM_NONE`), so it
  ran neither the window stage nor the line stage (section 4 rule 5's
  "stage"), and the function's line stayed 0;
- rule 5's fresh classification found the function's pin arriving on the
  port's own pin with the port's line valid: routable, "the stage's
  business, not this record's", and excluded it
  (`cm.reenum.9x.noirq.refused=01000205`: 01:00.0, EXCLUDED, why 5,
  "interrupt");
- `PCI.VXD` gave it no IRQ, and Device Manager showed Stor98 at Code 29
  ("the BIOS for the device did not give it an IRQ"). Section 12.1 item
  1 is the path: no routing entry found for the function, a line of 0,
  so no IRQ descriptor and problem 29. Why the routing lookup found no
  entry on q35 was not read; the line stage does not depend on it, since
  a line of 1 to 15 is taken as a fixed IRQ whatever the lookup finds
  (record 21 section 6.1.1 read the same cure on the P14s).

So the function fell between the two mechanisms: the stage that would
have given it the port's line runs only on a chain it numbered or
reopened, and rule 5 hands a routable function to that stage. Every real
enclosure read so far puts a switch between the port and the drive
(record 31 section 4.3), so the stage ran there; only a drive directly
on the port's bus, the emulator's plain enclosure, meets the gap.

### 13.2 The change

`XhciTbtNumberBridges`, when its survey of the port's secondary bus
finds no bridge (`XHCI_TBT_BRNUM_NONE`) and `XHCI_TBT_BRNUM_F_IRQ` is
asked, runs the line stage on that bus as it does after a numbering:
the port's own Interrupt Line and Pin read, and each function on the
secondary bus whose pin arrives on the port's pin (device d, pin p, as
pin (p - 1 + d) mod 4 + 1, with no bridge between) gets the port's line
where its own is 0 or FFh, journalled, read back, and undone if the
operation goes stale. Nothing else changes:

- the outcome stays `XHCI_TBT_BRNUM_NONE` (no bridge numbered), and only
  `IrqOutcome`, the counts and the per-function record are filled;
- no window stage (no bridge needs a window), and the bus numbers are
  not touched;
- a function on another pin, a line already set, a port with no line of
  its own, a read that fails: as in a numbered chain (section 4 rule 5,
  `XHCI_TBT_BRFN_*`);
- a survey that finds no function at all writes nothing.

The worker hands this record to rule 5 as it hands a numbered chain's
(`CmNiIrqFromBrnum` takes `XHCI_TBT_BRNUM_NONE` with an interrupt
outcome), so a function given the line is `ROUTED_WRITTEN`, excluded as
"interrupt" because it has one, and `PCI.VXD` routes it from the line,
as on the P14s. The worker logs the stage's outcome and counts for a
`NONE` survey as it does for a numbering.

On hardware this reaches only a function directly on a tunnel port's
bus, which no enclosure read so far has; where one does, the rule is the
one the stage already applies behind a switch.

What it does not change, the limits the stage already has: the line is
written before the re-enumeration that lets `PCI.VXD` build the
function's devnode, and a devnode built before it (a Device Manager
Refresh between the tunnel's arrival and the numbering) keeps its Code 29
(section 12.1 item 3), where section 12's memory-only form then applies;
the undo checks the function's pin, not its whole identity; and a survey
that found no function writes nothing, the worker's retry for an empty
bus taking the next look.

### 13.3 Host vectors and legs

`test_tbt_brnum.c`: the emulator's layout (a root port with line 10,
pin A, the NVMe at device 0, pin A, line 0, no bridge): `NONE`,
`IrqOutcome` DONE, the line 10 written and recorded `WRITTEN`, the
line the one write (no bus number, BAR, command or window); a second
function at device 1 on pin A arriving on pin B, left and recorded
`OTHER`; a line already set kept; the port with no line,
`PORT_NONE`; `F_IRQ` not asked, nothing written; an empty bus, no
stage; the go callback refusing before any write and after one, every
line put back; a line write not held, `ACCESS` and `NOT_HELD`.
`test_cm_noirq.c`: a `NONE` record with DONE, with ACCESS and with
PORT_NONE handed to rule 5 as a numbered chain's; stale or no stage, no
record. The legs are task 45.18's
98 SE and ME runs on q35: the function started on IRQ 10, Stor98
running, before their gate-G3 readings.

## 14. Functions met while Windows installs, and an xHCI controller on `xhci98.sys` (tasks 48.31's follow-up and 48.36)

Two readings of 2026-11-09 met the same gap from two sides, and this
section answers both with one design: a function that appears, or is
bound, only after a New Hardware wizard has run (a chained unit's first
plug, a dock's first plug), and a dock's second and third USB
controllers, which no rule of this record admitted. Section 14.8 says
how the section came to be.

### 14.1 What was read

**A chained unit's first plug** (task 48.31's follow-up, `runs/run-48.md`,
"48.31 follow-up"). On the q35 98 SE base, a fresh overlay with the
release package and Stor98 installed and the emulator's dock
(`usb4-device`, `downstream-ports=1`) at power-on, the first chained
ASMedia ASM2464 (`device-model=asm2464`) plugged behind the dock's chain
port 02:01.0:

- the worker numbered the chain port's range (record 34 section 3.3.7
  item 4, `cm.chain.9x.number=00020801`), re-enumerated it
  (`cm.chain.9x.reenumerate=00020800`) and opened the member's no-IRQ
  window;
- `PCI.VXD` made devnodes for the ASM2464's two bridges (04:00.0, 05:00.0)
  with no driver (problem 28), and the window's rule 3 excluded both
  ("class", `cm.reenum.9x.noirq.refused=04000203` and `05000203`); no
  devnode existed yet for the NVMe at 06:00.0, which a bridge's
  children get only once the bridge has its driver;
- Windows' Add New Hardware wizard for "PCI standard PCI-to-PCI bridge"
  ran (on 98 SE it ended with "To finish setting up your new hardware,
  you must restart your computer", answered No), then a wizard bound
  Stor98 to the NVMe's devnode, minutes after the re-enumeration;
- by then the window (10 s) had closed: the NVMe sat at problem 29, its
  memory base address register unassigned (`info pci`: no BAR), Stor98
  bound and not started, with no `cm.reenum.9x.noirq` line for it
  (a CFGMGR32 walk: `st=08000620 pr=29`);
- the chained unit unplugged and plugged again (the bridges' driver
  installed now): the window admitted the NVMe within its 10 s, the
  memory-only form of section 12 started it (`cm.reenum.9x.noirq.after=
  06000500`), Stor98 polling ("StorInterrupt 6.0.0 = pin 1 line 0 ...
  polling (interrupt line not set)").

ME read the same at the first plug (no restart asked there), and one
more thing: during the window the new bridges read **no** installation
problem - only once the wizard was up did they read `DN_HAS_PROBLEM`
with problem 1 (`st=18086ECE pr=1`) - and their hardware keys held
`HardwareID`, `CompatibleIDs`, `DeviceDesc` and `HWRevision` and no
`Driver`, against the dock's installed bridge's `"Driver"=
"System\\0057"`. So section 4's "a devnode whose problem appears only
after the wizard has bound its driver is therefore still seen" holds
only for a devnode that exists while the window is open; a function
below a bridge Windows has not installed yet appears after it.

**A dock's USB controllers** (task 48.36). The owner's capture of
2026-11-09 (a P14s Gen 1 under 98 SE with `3.4.2.0`, a CalDigit TS3
Plus on its tunnel port 04:01.0, line 10, pin A): the dock's switch
(06:00.0 upstream, 07:00.0 to 07:04.0 downstream) carries a Fresco Logic
FL1100 on port 0 (08:00.0), a second FL1100 on port 1 (09:00.0), an
ASMedia ASM1142 on port 2 (0A:00.0) and an Intel I210 on port 3
(0B:00.0), each function on pin A. Seen from the tunnel port, port d's
pin A arrives as pin (d mod 4) + 1 (the swizzle of record 21 section
6.1.1 step 5 through 07:0d.0 and 06:00.0): port 0's on the port's own
pin, ports 1 to 3's on pins B, C and D. Step 5 gave line 10 to 08:00.0
alone, as its rule says; 09:00.0 kept line `FFh`, 0A:00.0 and 0B:00.0
line 0, and `PCI.VXD` disabled the three at their first enumeration
(section 12.1 item 1: problem 29, decode off). This record's pass met
each and refused it on its class (`cm.reenum.9x.noirq.refused`
`09000203`, `0A000203`, `0B000203`: EXCLUDED, why 3, "header"). So the
dock's card reader (on 09:00.0's root hub) and its own USB ports (on
0A:00.0's) did not work on 98 SE, where on Windows 11 every function
started. The emulator's model of the dock (`usb4-emu`'s `usb4-device
dock-model=ts3plus`, `runs/run-48.md`, "48.36") reproduces the same
pins, lines and problems on 98 SE by construction, and on it two more
things were read: the member's eight entries ran out in device order
(section 14.5), and every plug opened the New Hardware wizard for the
I210, which has no 98 SE driver, during which the configuration
manager's calls blocked (section 14.6).

### 14.2 An xHCI controller on `xhci98.sys` (the owner's decision of 2026-11-09)

Section 1.3's "any function that is not Stor98's" is narrowed by one
class: an xHCI controller (class code `0C0330h`) below a tunnel port,
bound to `xhci98.sys`, is started by section 12's memory-only form as an
NVMe drive on Stor98 is. `xhci98.sys` polls when it is given no
interrupt resource (design record 15 section 4: `XhciInterruptMode` 0,
the default, with no interrupt resource is "poll, automatic: no
interrupt resource"), so the function needs nothing that was withheld.
No line is written or invented, so section 1.3's second rule stands; the
I210, and every other class, stays EXCLUDED "header" at problem 29.

The rules of section 4, with what changes for the new class:

1. Rule 1 (`XhciCmNoIrqStart` 1): unchanged; the switch covers both
   classes.
2. Rule 2 (problem 29, 12, 16 or 27, unstarted): unchanged.
3. Rule 3: the class code `010802h` **or `0C0330h`** (the class
   `xhci98.inf` binds as `PCI\CC_0C0330`), a type 0 header, a pin other
   than 0, a line of 0 or `FFh`, no I/O base address register. The entry
   records which class it read (its **kind**: NVMe or xHCI), and rule 4
   and the form follow the kind.
4. Rule 4, for the xHCI kind: two values of the devnode's driver key,
   read as for Stor98: `DevLoader` reading `*NTKERN` and `NTMPDriver`
   reading `xhci98.sys`, the pair `xhci98.inf`'s 98 SE and ME sections
   write (`HKR,,DevLoader,,*NTKERN`, `HKR,,NTMPDriver,,xhci98.sys`).
   Both absent, or one alone, is PENDING (the driver not installed yet,
   section 14.3); another pair (a `DevLoader` other than `*NTKERN`, an
   `NTMPDriver` other than `xhci98.sys`) is EXCLUDED "not its driver"
   (the reason that read "not Stor98's", the same code). **No version is
   read** for this kind, and the pair names the driver, not its release:
   every `xhci98.sys` from `2.3.0.0` polls with no interrupt resource
   (record 15, roadmap-hcd Phase 36), and `xhcicm.sys` has shipped only
   from `3.0.0.0`, in the same package as `xhci98.sys`, installed and
   updated with it by the same run, so the `xhci98.sys` beside this
   `xhcicm.sys` is expected to poll. That is accepted knowingly as an
   exception, as N10 accepts Stor98's: an older `xhci98.sys` left by a
   hand copy or a run that failed between the two files. What such a
   file does is bounded by record 15 section 4: before Phase 36 an
   interrupt descriptor was required and its start was refused without
   one (the refusal mode 1 keeps), so the function then shows that start
   failure instead of problem 29 - no interrupt is invented and no
   driver waits for one. The NVMe kind keeps its pair and its installer
   record unchanged. A Stor98 pair on an xHCI function, or `xhci98.sys`'s
   on an NVMe one, is "not its driver".
5. Rule 5: unchanged (UNROUTABLE or PORT_NO_LINE, by the numbering's
   record or a fresh reading).

**The form.** The xHCI kind is admitted to the memory-only form alone
(section 12.2): problem 29 as admitted, the source configuration memory
descriptors only, one reprocess a devnode a generation by the journal's
reprocess mark, the changeable conditions (rules 2 and 4, with the
kind's pair) read again before the reprocess and before the `READY`
fallback. A source configuration with an IRQ descriptor, or a problem
other than 29, is EXCLUDED "configuration" for this kind: section 4's
added configuration has been read on no machine for any class, and
nothing is created for a function of a class this record did not first
read. So the journal's committed alternative (rule 1 of "Across
generations and starts") never arises for the kind.

**What `xhci98.sys` then does.** Its start finds a memory window and no
interrupt resource; under mode 0 it polls (record 15 section 4) and logs
the reason; record 15 section 4.4's held-line scan finds no line and
does nothing. The function's Interrupt Line stays as it was ([static]
`CONFIG_START` writes it only from an allocated IRQ, section 12.1 item
4). Under `XhciInterruptMode` 1 or 3, which only a user sets,
`xhci98.sys` refuses the start (record 15's table, "none: the start
fails"), and the function shows that failure instead of problem 29: the
user's setting decides, as it does for a controller with an interrupt.
Mode 2 polls.

**What it overrides, and why that is safe.** As section 12.2 says for
Stor98: `PCI.VXD`'s disable records that no interrupt was found, and the
start is made only for a function whose driver polls without one; the
user's own disable (problem 22) is never undone. An xHCI function on a
port whose pin arrives on the tunnel port's own keeps the line record 21
section 6.1.1 step 5 writes and is left untouched: EXCLUDED "header"
when its line reads set (rule 3), as a routed NVMe function is, or
EXCLUDED "interrupt" where its line still reads 0 or `FFh` and rule 5
finds it routable (the stage's business, section 4).

**Pin 0.** An xHCI function with no pin is counted with section 5.4's
pin-0 functions (seen, and started where it starts by itself), as an
NVMe one is; nothing is done to it.

### 14.3 The window held while a driver is being installed

The member's window (section 4, `CM_NOIRQ_WINDOW_MS`, 10 s) is **held**
while a pass finds Windows installing a driver that can still bring a
function this record would start. Two readings hold it, and only these:

1. **A bridge being installed** (the walk's). Each pass's walk (section
   4, "The walk"; `cmWkNiWalk`) reads, for each descendant it places
   (its location resolved), until one holds: its class code (`PCI.VXD`'s
   configuration read of offset 08h); for a PCI-to-PCI bridge (base
   class 06h, subclass 04h) alone, its status (`Get_DevNode_Status`)
   and, where that says no installation, its hardware key's `Driver`
   value (`Read_Registry_Value`, `CM_REGISTRY_HARDWARE`). A bridge whose
   status reads `DN_HAS_PROBLEM` with one of the installation problems
   section 4 already names for PENDING (`CM_PROB_NOT_CONFIGURED` 1,
   `CM_PROB_DEVLOADER_NOT_FOUND` 8, `CM_PROB_REINSTALL` 18,
   `CM_PROB_FAILED_INSTALL` 28; `CmNiInstalling`), or whose hardware key
   has no `Driver` value (`CR_NO_SUCH_VALUE`: Windows has installed no
   driver for it yet, ME's reading in section 14.1;
   `CmNiWalkInstalling`), holds, whatever its entry (a bridge is
   EXCLUDED by its header and on the refused list of section 14.5; the
   walk reads it all the same). So does a bridge that is installed but
   has a function behind it and no child devnode yet: its
   `Get_Child` answers `CR_NO_SUCH_DEVNODE` while a function the window
   waits for answers on its secondary bus (devices 0 to 31, function 0,
   read through `PCI.VXD`) - Windows has not yet enumerated what the
   installation brought. Read on 98 SE (section 14.1, the joined
   build's first leg): there a bridge's wizard ends with its restart
   question, and its children are enumerated only once that is
   answered No; a user who
   takes longer than 10 s over it left a gap in which no bridge read as
   being installed, and the window closed before the dock's controllers
   appeared. And so does a function waited for that answers on the
   member's or a walked bridge's secondary bus (devices 0 to 31,
   function 0) at a device where the walk placed no devnode
   (`CmNiUnenumerated`): read on 98 SE with
   the dock model (section 14.7, "as read", 2026-11-10), 98 SE creates a
   new bridge's children one at a time - after the dock's upstream port
   was installed only its first downstream port appeared, and the second
   only once the wizard of the first's USB controller had finished - so
   with that first child in place no bridge read as childless, and the
   window closed during the controller's wizard.
   The functions waited for (`CmNiAwaited`) are a bridge, which brings
   more devnodes, and the two classes this record starts (section
   14.2), or one whose class is not read; a function of another class
   is not: read on 98 SE with the dock model, the I210's wizard,
   cancelled, left no devnode below its port, which would otherwise
   hold every first plug's window to the bound. That reading is made by
   a complete walk alone, whose locations were all kept
   (`CM_WK_NI_MET`, 64): a devnode the walk could not read or place
   might be the one at that device. So that a started USB controller's
   root hub (`USB\XHCI98_ROOT_HUB`, no PCI location) does not leave
   every such walk incomplete, the walk goes below a bridge alone, or a
   devnode whose class it could not read: only a bridge has PCI
   devnodes below it. The walk's own bounds are kept (six levels,
   `CM_WK_WALK_NODES`).
2. **An entry waiting for its driver** (the entry's own). An entry
   PENDING whose last reading was rule 2's with an installation problem
   (the same four), or rule 4's with the kind's pair absent or one value
   alone (its own wizard still running: section 14.1's NVMe, whose
   devnode `PCI.VXD` created at problem 29 before Stor98's wizard wrote
   its binding, or a dock's xHCI controller before `xhci98.inf`'s did).
   The mark (`CM_NI_DEV.WaitDrv`) clears at the entry's next reading of
   either rule that says otherwise, and at any state other than
   PENDING; it is not cleared by a pass that has not yet read the entry
   again.

Why a bridge, and not any descendant (the first form of this section
read every descendant): only a bridge's installation makes new devnodes
appear below the member, and a function this record would start is
held by its own entry (item 2). A function of another class that never
gets a driver - the TS3 Plus's I210, whose wizard is cancelled at every
plug on 98 SE - would otherwise hold every window of that dock to the
bound. A read that fails (the class, the status, the location) holds
nothing; a problem 29 alone, with the binding in place or another
driver's, holds nothing; an entry PENDING for another reading (a read
that failed, `Stor98`'s installer record not read, a fresh
classification to repeat) holds nothing.

**The window's clock.** The window has an **opening** (`CmNiOpenWindow`;
moved forward only by the credit of section 14.6) and, once held, a
**last hold**: the last pass that found item 1 or item 2. It closes
`CM_NOIRQ_WINDOW_MS` after the later of the two, and never later than
`CM_NOIRQ_HOLD_MS` (30 minutes) after its opening: one deadline, used by
its close and by every admission check that reads the window (the
gate's and the descriptors' "not in time", the memory-only form's). A
pass at or past `CM_NOIRQ_HOLD_MS` from the opening holds nothing. So a
binding that appears at 40 s leaves the entry 10 s, from the last pass
that still saw it waiting, to pass its remaining rules and its
descriptors, as at a window's opening; a function whose devnode appears
once the wizard has bound its parent bridge's driver is walked,
admitted and started as any other; an entry that never gets a driver,
or a bridge left at problem 28 for good, holds the window to 30 minutes
and no longer, and every entry still PENDING then is EXCLUDED "not read
in time", as at any close. A pass that comes late (a call that blocked)
holds by the entry's last reading before anything closes on it. The
deadline is reset with the window: a new generation and a reopening
within the same generation (a retry of the call) both start it afresh
from the new opening, with no hold yet; the entries themselves follow
section 4's generation rules unchanged (an entry ACTIVE or ATTEMPTED
keeps its attempt and its own deadline). An entry admitted before the
close keeps its own transaction budget (`CM_NOIRQ_TXN_MS`).

Why 30 minutes (the first form of this section, and the joined build,
had 10): read on 98 SE in the emulator with the dock model (section
14.8), a dock's first plug asks about ten wizards in a row - six
bridges, each with its restart question, three USB controllers and the
network function - and they took 13 minutes at a deliberate pace; the
dock's second USB controller, the ninth devnode installed, appeared
after a 10-minute bound and stayed at problem 29. While held the window
costs one walk a tick and holds nothing else back, so the bound is set
for the slowest first plug a user is expected to answer, not for the
cost.

Nothing else changes: the rules, the result table, the transaction and
its budget (a transaction already FAILED in the generation is not
revived), the journal, the switch (`XhciCmNoIrqStart` 0 opens no
window, so nothing walks and nothing is held) and the NT route. While
held the worker keeps the cadence it has for a window in progress - the
interim's `CM_WK_TICK_MS` (200 ms) between passes, not section 4's
planned 50 ms - so a held window costs one walk a pass, some 9,000 walks
at most; the walk's added reads are one configuration read a placed
descendant and two calls a bridge, until one holds. Nothing else waits
on a held window (this driver's re-enumerations queue only behind an
entry ACTIVE or ATTEMPTED, section 4). Both kinds are held alike: an
NVMe drive's first plug meets the same wizards as a dock's controllers.

[inf] What it rests on: that the NVMe's devnode, once it appears, reads
an installation problem or problem 29 inside the held window, and that
the memory-only form starts it there as at a later plug (the reading of
section 14.1's second plug). The legs read it.

### 14.4 The diagnostic, the log and the counters

`XhciCmNoIrqDiag` word 4 gains the kind in bits 31:24 (0 NVMe, 1 xHCI);
XHCITR prints it. The refusal and attempt records are unchanged (the
function's location says which device; its class is in the PCI section
of the same report). Two lines are added, a pair:

- `cm.reenum.9x.noirq.hold`, once a window, at its first hold: bits
  31:24 say which reading held it - 1 a bridge the walk met, with its
  depth below the member in 15:8 and its problem in 7:0 (`FFh` for no
  `Driver` value, `FEh` for a function behind it with no child devnode
  yet); 2 an entry waiting for its driver, with the
  function's location (bus 15:8, device and function 7:0).
- `cm.reenum.9x.noirq.credit`, at each call whose held time is credited
  (section 14.6): the call's measured duration in milliseconds, held at
  `FFFFFFFFh`.

Two more pairs say why a window ended (added 2026-11-10, after the 98 SE
first plug of section 14.7 left neither the close nor the walk's progress
in the log):

- `cm.reenum.9x.noirq.close`, at the close: why in 31:24 (1 at 10 s from
  the opening with no hold, 2 at 10 s from the last hold, 3 at
  `CM_NOIRQ_HOLD_MS`), the seconds from the opening in 23:12 and from the
  last hold (the opening when none) in 11:0, each held at `FFFh`; then
  `cm.reenum.9x.noirq.close.hold`, the last hold's source as in the
  hold's line, 0 for none.
- `cm.reenum.9x.noirq.walk`, at each walk whose end, count or hold
  differs from the last logged in the window: its end in 31:24 (0
  complete; 1 the member's devnode not found, 2 its path not read, 3 the
  gate closed, 4 its route not read, 5 its `Get_Child` failed, 6 the
  stack full, 7 a `Get_Sibling` failed, 8 cut short by the node bound,
  the worker or the gate, 9 a `Get_Device_ID` failed, 10 a devnode not
  placed, 11 a `Get_Child` failed, 12 the gate closed at the tick so no
  walk), the devnodes met in 23:16 (held at `FFh`) and the hold's 15:0;
  then a `cm.reenum.9x.noirq.walk.node` for each devnode met (the first
  32): its location in 31:16 (`FFFFh` not placed, its depth in 7:0), a
  bridge in 15, its `Get_Child` answer in 14:8 (`7Fh` not asked: a
  function, or below the depth bound) and, for a bridge read before the walk's first hold,
  its problem in 7:0 (`FFh` no `Driver` value).

The counters are shared by the two kinds; two are added: `Held`
(windows held) and `Credited` (calls credited).

### 14.5 Room for a dock's functions

The walk meets every PCI devnode below the member, bridges included,
and a member keeps `CM_NOIRQ_DEVICES` (8) entries. The TS3 Plus brings
ten (its upstream port, five downstream ports and four functions), and
an entry refused by its header kept its slot for as long as the walk
met it, so the room ran out in device order: on 98 SE with the dock
model (`runs/run-48.md`, "48.36"), the second FL1100 was taken and
started while the ASM1142 and the I210 were never met (`NoRoom`).
So an entry refused by its header (rule 3: the class, the header type,
a pin above 4, a line that reads set, an I/O register) and holding no
journal leaves the entries: its devnode handle and its instance id's
hash go into the member's **refused list** (`CM_NOIRQ_SKIPS`, 32), and
a devnode the walk meets on that list (the same handle and hash) is
passed over with no reading by the entries (the walk's own reading of
section 14.3 item 1 still reads a bridge on the list). The entry is
freed not at the refusal but at the next pass, or by the walk when it
needs the room, so the refusal is logged and counted as before. An
entry with a journal (a device an attempt was made for, whose line
reads set since) keeps its entry and its journal; a function with
Interrupt Pin 0 (section 5.4, "no pin", counted after its status read)
keeps its entry too. A handle met with another hash is a new device:
its old line is taken off the list and the device is judged afresh.
Each line is marked by the walks that meet it, and a walk that ended
complete (`CmNiWalkDone`) takes off every line it did not meet, as it
forgets an entry it did not meet, so a devnode that left and came back
with the same handle is judged afresh; an incomplete walk takes off
nothing. The list is emptied at a new generation (`CmNiOpenWindow` with
another generation), so every devnode below the member is judged once
more after each of this driver's re-enumerations; a reopening within
the same generation keeps it. A line goes into a free line first; when
the list is full the lines are overwritten in turn (a cyclic cursor,
close to the oldest first), so a devnode evicted from a full list is
read and refused once more: a refusal by its header is counted and
logged once a generation while its line stays on the list. Nothing is
written for such a devnode, so nothing is lost with its entry.

### 14.6 A call that blocked

Read on 98 SE with the TS3 Plus model (section 14.1): while the I210's
wizard is open the configuration manager's calls block. The worker's
reprocess of the second FL1100 was held about 80 s, until the wizard was
cancelled: the attempt then ended FAILED "timeout" although the
controller had started, and the window had closed before the ASM1142
was met. Section 4's rule ("a call that blocks spends the budget") is
amended for this: **time the worker spent blocked does not count against
the window or an attempt.** What is measured is the worker's own time
away from the member, never the time a pass waits for its tick: an
operation's, from the pass that handed it out (`CmNiNext`) to its result
(`CmNiTake`), on the clock the machine is given; the walk's, which the
worker times and hands over (`CmNiCredit`); and the worker's other work
between two of the member's ticks (the marking, the tunnel ids, a chain
operation, another member's calls, all on the same thread and as able
to block on a wizard): the time from the end of the member's last tick,
or from its window's opening when its ticks were not running, to the
start of the next, less the tick's own wait (`CM_WK_TICK_MS`), handed
over the same way at the start of the tick before anything else. A
duration above `CM_NI_BLOCK_MS` (2 s; a call that does
not block returns in milliseconds, a walk in well under a second) is
**credited**: the window's opening and its last hold (section 14.3) move
forward by it, never by more than has passed since the opening (a last
hold made while the call was outstanding stays where it is, and none is
left before the moved opening), and so do the deadline and the attempted
time of each entry ACTIVE or ATTEMPTED. The credit is bounded by the
same `CM_NOIRQ_HOLD_MS` (30 minutes) a window and as much an attempt,
past which a held call counts as before and the deadlines run out; so a
window lives at most an hour of the machine's time, 30 minutes of them
its own. A new window (`CmNiOpenWindow`) starts with no credit, and a new
attempt with none of its own. An attempt whose devnode started while its
call was held is then read DONE at its next outcome read, and a function
whose devnode appeared while the wizard was open is met by the next
walk, inside the credited window.

**What the credit rests on: no pass while a call is outstanding.** The
interim ("The code, and its interim" above) makes each operation on the
worker's own thread, at once, and the walk too: while a call blocks no
pass runs, so no deadline is judged and the window is not closed on the
time the call takes. The first thing the machine sees after a held call
is that call's end, and the credit is applied there before anything is
judged on time: `CmNiTake` credits before it reads the attempt's
deadline or the window, and the worker hands the walk's time, and the
time its other work took since the member's last tick, over
(`CmNiCredit`) before `CmNiPass`. When task 42-A.12's call workers land
and the connection-manager thread's passes run while an operation is
outstanding, that order must be kept: a pass does not end the attempt
whose operation is outstanding, nor close its member's window, on time
alone until the result is taken and its time credited (the stop, the
claim's loss and the gate are judged as before). That is owed with
42-A.12; the host vectors run the interim's order.

### 14.7 Host vectors and legs

`test\test_cm_noirq.c`, the hold (section 14.3): `CmNiInstalling` for
problems 1, 8, 18 and 28 with `DN_HAS_PROBLEM`, none for 29, 0 or a
problem without the flag; `CmNiWalkInstalling` for ME's bridge during
its wizard, no `Driver` value, a started devnode with one, both reads
failing, a value too long for the room (present); `CmNiIsBridge` for a
bridge (`060400h`, `060401h`) and none for another class or a failed
read; a window held at 9 s stays open past 10 s from its opening and
closes 10 s after the last hold; a hold at `CM_NOIRQ_HOLD_MS` - 1 does
not keep it open past `CM_NOIRQ_HOLD_MS` (the exact boundary); a new
opening restarts the bound; a hold with the window closed does nothing;
an entry PENDING for its binding holds the window and is admitted once
bound, the binding appearing after 10 s and the admission spanning
further passes; an entry that never gets a driver, closed at the bound;
one at problem 29 with no installation reading, or with its status or
binding read failing, is EXCLUDED "not read in time" at 10 s; the
deadline reset by a new generation and by a reopening; the hold's log
once a window with its source. The refused list (section 14.5): ten
devnodes below one member, nine of them refused by their headers, the
function reached; a refused devnode not read again in the generation,
read again at a new generation, a reused handle judged afresh, a line
not met by a complete walk taken off and judged afresh when it comes
back, kept after an incomplete walk, an entry with a journal kept, a
full list overwritten. The credit (section 14.6): the reprocess held 80
s, then DONE; the window's deadline moved by the credit; a credit past
the bound, timeout as before; a call held 2 s not credited; a walk's
held time handed over and credited; a new window with no credit,
nothing before its opening credited; the worker's time between two
ticks credited as a call's (the window opened, the marking blocked 80
s, two controllers then met and admitted, an attempt surviving); the
vectors of a result returned
past its budget now held past the bound; the credit's log. The xHCI
kind (section 14.2): an xHCI function at problem 29, memory only, bound
`*NTKERN` and `xhci98.sys`, unroutable: the reprocess-only attempt with
no installer record present, DONE with the line read and the kind in
the diagnostic; the same with Stor98's pair, and an NVMe function with
`xhci98.sys`'s, EXCLUDED "not its driver"; an xHCI function whose
configuration holds an IRQ descriptor, EXCLUDED "configuration" with
nothing created; an xHCI function on the port's pin with its line set,
EXCLUDED "header", and with its line 0, EXCLUDED "interrupt"; class
`020000h`, EXCLUDED "header"; the binding read again before the
reprocess, changed to another driver, FAILED "admission". The combined
cases: a chained NVMe behind two bridges still being installed (the
walk's hold) whose devnode appears after 10 s and waits for Stor98's
binding (the entry's hold), started; a second xHCI controller of the
same member waiting for `xhci98.inf`'s binding while the first is
attempted, both started; a member whose only unfinished devnode is a
function of another class with no driver (the I210), closed at 10 s.
The one-at-a-time enumeration (section 14.3 item 1): a device answering
on a secondary bus with no devnode placed found, a placed function of
the device counting for it and the same device on another bus not, a
bus not read or locations not kept whole saying nothing;
`CmNiAwaited` for a bridge, an NVMe drive and an xHCI controller and
none for a network function, an EHCI controller or a failed read; the
dock's first plug as read, held through 04:00's install and then its
unenumerated ports and closed 10 s after the last hold; the close's two
lines unheld, after a hold and at the bound.

Legs, in the emulator: 98 SE and ME with the TS3 Plus model behind the
P14s's host (`launch-q35.ps1 -Usb4 tb3ar`, the dock hot-plugged): the
dock's first plug with its wizards answered, and a re-plug; both FL1100s
and the ASM1142 started (the second FL1100 and the ASM1142 polling, line
0 or `FFh`), a stick on each controller's ports with a file compared;
the dock ejected through XHCITR, and removed by surprise;
`XhciCmNoIrqStart` 0 leaving them at problem 29. 98 SE and ME, fresh
overlays, the first chained ASM2464 plugged behind the emulator's dock:
the wizards answered (98 SE's restart answered No), the NVMe started on
Stor98 with no second plug, polling, a file written and read back.

As read (2026-11-10, `runs/run-48.md`, "48.36, the two joined"): ME,
the dock's first plug, both polled controllers started with no second
plug, a stick on each controller compared, the eject, the re-plug and
the surprise removal pass; 98 SE, the first chained ASM2464 started
with no second plug (each restart question left 25 s) and a file
compared; 98 SE, the dock's re-plug starts both polled controllers, and
the eject and the surprise removal pass. Open: 98 SE, the dock's very
first plug - after its ten wizards the second FL1100 and the ASM1142
stay at problem 29 until the next plug; the walk met the dock's first
three devnodes and none after, though both controllers' devnodes were
there and bound, and the window's close was not logged (the release
log has no line for it). Not explained; the user documents say so.

Explained and fixed (2026-11-10, task 48.36 continued, `runs/run-48.md`,
"48.36 (continued) The dock's first plug on 98 SE"): with the close and
walk lines of section 14.4, a fresh 98 SE overlay read the walk complete
each time and the window closing 10 s after the last hold, 258 s from its
opening (`cm.reenum.9x.noirq.close=0210200A`, last hold `010002FE`),
during the first FL1100's wizard: below the dock's upstream port 98 SE
had created only the first downstream port's devnode, and created the
next only after that wizard (section 14.3 item 1). With a device on a
walked secondary bus with no devnode yet holding, the dock's first plug
on a fresh overlay, every wizard answered (each restart question left
15 to 20 s, the I210's open 50 s and cancelled), started both polled
controllers (`cm.reenum.9x.noirq.after=07000500`, `08000500`, the
reprocess held 478 s by the wizards and credited), and the window closed
10 s after its last hold (`close=021DC00A`, last hold the second FL1100's
entry); a stick on each of the three controllers enumerated at
SuperSpeed.

### 14.8 How this section came to be

- 2026-11-09, task 48.31's follow-up (commit 8edfd362): the window held
  while the walk met any descendant at an installation problem, or an
  entry was PENDING for one or for its binding; its start moved to the
  pass, bounded at 10 minutes from its opening; `cm.reenum.9x.noirq.hold`
  and `Held`.
- 2026-11-09, the same (59a28f59): the walk also reads a descendant's
  `Driver` value, after ME's reading (section 14.1).
- 2026-11-09, task 48.36, on a branch of its own (7621fd7c to 8d7bc126):
  the xHCI kind; a deadline moved by an entry waiting for its driver,
  bounded at 300 s; the refused list; the credit of a blocked call,
  bounded at 300 s.
- 2026-11-09, the two joined (task 48.36's merge): one hold of two
  readings (section 14.3), the walk's restricted to bridges so that a
  function with no driver for this system holds nothing; one bound,
  `CM_NOIRQ_HOLD_MS` (10 minutes then), for the hold and the credit alike;
  one clock (the opening and the last hold, the credit moving both); the
  pair of log lines and the two counters of section 14.4.
- 2026-11-10, after the joined build's first 98 SE leg: a bridge with a
  function behind it and no child devnode yet holds too (section 14.3
  item 1); and `CM_NOIRQ_HOLD_MS` raised to 30 minutes, after a dock's
  first plug on 98 SE outlasted 10 (section 14.3, "Why 30 minutes").
- 2026-11-10, task 48.36 continued: the close and walk lines of section
  14.4 read the dock's first plug on 98 SE, and a device on a walked
  secondary bus with no devnode yet holds (section 14.3 item 1).

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
| A.1 | **`PCI.VXD`'s Code 29 on 98 SE and ME (task 44.30)**: at its enumerator's `CONFIG_ENUMERATE` (handler `obj4:0x2763`, registered by `Register_Enumerator` at `obj3:0x60b2`), the bus scan `obj3:0x5905` calls the per-function callback `obj3:0x27e4`, which builds a new devnode's basic configuration (`obj3:0x1b22`: the IRQ step `obj3:0x14ac`, then memory `obj3:0x163f`, then I/O `obj3:0x16a5`) and skips a function whose devnode already exists with a basic configuration (`obj3:0x2743`-`0x2757`); the IRQ step reads pin and line (`obj3:0x1a7c`-`0x1ada`: needed when the pin is not 0, the assignment forced to 0 for a line above 15, `FFh` included), looks the function up in the routing table walking up through bridges with the swizzle `(dev + pin) mod 4` (`obj3:0x3a76`, `0x3741`, `0x363d`, `0x367c`), adds an IRQ descriptor for a link found or a line of 1 to 15, and otherwise, for a function whose command register has neither decode bit set, not a CardBus bridge and not a display device, calls `Disable_DevNode(dn, 2)` (the sequence at `obj3:0x1614`, `push 2` at `obj3:0x1619`, through the `VxDJmp CONFIGMG 64` thunk `obj4:0x3008`); its child `CONFIG_FILTER` returns `CR_DEFAULT` (`obj4:0x2b78`) and its child `CONFIG_START` (`obj4:0x2b3b`) fails only a power-state change, writing 3Ch only from an allocated IRQ (`obj4:0x23d9`); the routing table is dropped when a bus-0 function with a pin has no entry (`obj3:0x48ed`, its callback `obj3:0x482f`). Windows 98 SE `pci.vxd` 4.10.0.2222, 65,895 bytes, SHA-256 `a9268f51f5a486817f5484686398215b3600c8eb3608773195ea05ecfacb303d`, from `win98\BASE6.CAB` of `Win98SE.iso` (SHA-256 `C048C31E...F7F584`); ME `pci.vxd` 4.90.0.3000, 70,539 bytes, SHA-256 `5fcb833b90693617fc8003e3d84e48e7b24058cd579096a43e28effeae61cb58`, from `win9x\BASE2.CAB` of the owner's Windows ME OEM CD, its `Disable_DevNode(.., 2)` at `obj4:0x4f4`, the path compared, not every byte | static (7-Zip extraction of the one file into a scratch directory, a Python LE parser applying the internal fixups and capstone 5 over the code objects, `int 20h` with its dword read as the VxD call; object-relative addresses; nothing executed; a subagent's read, 2026-10-25, its key sites re-read by the worker; not tracked). The disable's condition as the trigger of 98 SE's Code 29 in the emulator is consistent with the runtime reading of `runs/run-44.md` 44.30, which reads the outcome, not the instruction | design record 28 section 12.1; `src/cm/cm_noirq.c` (`niMemOnly`) |
| A.2 | **CONFIGMG's `Disable_DevNode` with `CM_DISABLE_HARDWARE` sets problem 29, and `Reprocess_DevNode` and `Setup_DevNode(READY)` clear a problem whatever its code (task 44.30)**: `_CONFIGMG_Disable_DevNode` at `obj7:0xabe7`, `test al,2` then `push 1Dh` / `call obj7:0x7746` at `obj7:0xac14` (the routine sets `DN_HAS_PROBLEM` and the problem, the disable count untouched); `Reprocess_DevNode` (`obj7:0xb265`) and `Setup_DevNode`'s `READY` walk (`obj7:0xa687`, callback `obj7:0x1c81`) reach `obj7:0x1c35` and `obj7:0x7285`, which clears the problem and re-runs the devnode's configuration, setting problem 22 again only where the disable count is not zero. Windows 98 SE `configmg.vxd`, 125,057 bytes, SHA-256 `568f800c9633f1ebaf48204fe2c5e6b5cffaee25f00191b9c29e762a0e154162`, from `WIN98_53.CAB` of the same ISO | static (as the row above; the paths these calls take were followed, not every path of the file; a subagent's read, 2026-10-25). The reprocess's effect on a devnode `PCI.VXD` disabled is also runtime: `runs/run-44.md` 44.30, through this driver's own log on 98 SE in the emulator | design record 28 section 12.1 |

## Sources

- Windows 98 SE's `PCI.VXD` 4.10.2222 and `CONFIGMG.VXD`, and ME's
  `PCI.VXD` 4.90.3000, from the owner's install media, read statically
  on 2026-10-25 (`legal-provenance.md` section 4); section 12.

- The Windows 98 Second Edition DDK's `INC_WIN98_CONFIGMG.H`,
  `INC_WIN98_CFGMGR32.H` and `INC_WIN98_PCI.H` (`tools\win98ddk-extracted\incs\`;
  `docs/references/README.md`, "Windows 98 DDK"), read 2026-10-18; its
  `HELP_WIN98DDK.CHM`, searched the same day.
- Microsoft's public Configuration Manager reference for the Win32 mirror
  (`CM_Add_Empty_Log_Conf`, `CM_Add_Res_Des`, `MEM_DES`, `MEM_RANGE`),
  for the general model of logical configurations and priorities; the
  ring-0 layouts are the DDK header's.
- Design record 21 sections 6.1, 6.1.1, 7.2, 7.4 and 8; `src\cm\cm_cfgwk.c`,
  `cm_cfgmg.h`, `tbt_brnum.c`, `tbt_brnum.h`.
- Stor98 `README.md` ("Settings", "Polling mode", "Interrupt report"),
  `docs/PLAN.md` ("Polling mode", the 9x decision on Pin and Line) and
  `inf/stor98-x86.inf` (`PCI\CC_010802`, `DevLoader`, `PortDriver`), read
  2026-10-18 at Stor98's `audit-2026-10-18` branch head `4253b58`.
- `runs/run-41.md`, series g (the P14s readings of 2026-10-16).
