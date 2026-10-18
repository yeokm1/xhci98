# Design Record 28 - A Windows 98 SE or ME Function With No Interrupt

Status: draft 11, 2026-10-18, reviewed and revised (an unstarted device
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
  says, and fails closed where it cannot be established.
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
transaction ends FAILED while its call stays the worker's. Devices of
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
   way. What the record proves is that this installer last installed a
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
| the source configuration (the first eligible basic one, "The existing configurations") without an IRQ descriptor, with another resource type, with more than `CM_WK_NOIRQ_MEM` memory descriptors or with a descriptor larger than the buffer; no basic configuration at all ("The existing configurations") | EXCLUDED, "configuration" |
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

A function of another class or driver, which the pass never touches,
stays as `PCI.VXD` left it, and the viewer (XHCITR, record 25) shows its
problem code as it shows any other.

## 7. Registry value, diagnostic, log and counters

- **`XhciCmNoIrqStart`** (`REG_DWORD`): read as every `XhciCm*` value is
  read from `3.1.0.0` (task 42-A.17): the machine-wide key
  `HKLM\System\CurrentControlSet\Services\USB` first, and where absent
  there the NHI's driver key; once at start. Not written by the INFs (as
  `XhciCmBridgeNumbering` is not; record 21 section 8's table gains the
  row with the code). 1 (the default; absent in both places reads as 1)
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
  other handle records of the 9x route go (level 4 alone).
- **Counters** (record 21 section 8's `cm.*` set, the 9x route's block):
  candidates, attempts, started, still unstarted, refused by each rule of
  section 4, left-over refusals, disabled leftovers, journal entries
  marked gone, timeouts, late results dropped.

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
| N5 | The switch | `XhciCmNoIrqStart`, default 1, 98 SE and ME only, read machine-wide first then per driver key (42-A.17), not INF-written, never asked by the installer, editable under "Advanced..." and written machine-wide when changed | 7 |
| N6 | The MSI-only function | nothing done; a hypothesis until its leg is read, recorded in the diagnostic | 5.4 |
| N7 | Retries and budgets | one configuration transaction per devnode per publication generation of its member and at most `CM_NOIRQ_ATTEMPTS` (3) configurations created in the member's life, the bound never refusing an owed reprocess; a new generation reads the device again, reprocesses the journal's committed alternative once where it is still found, marks it gone where it is not, and may create another after an uncommitted or gone one; pending readings are re-read at every pass of the 10 s window, terminal exclusions once; a transaction runs on its own 30 s budget at the 50 ms cadence, completion-driven | 4 |
| N8 | What this driver adds, and never frees or recognises | no configuration of anyone's is ever freed (`Free_Log_Conf` is not called), and no configuration is ever recognised as this driver's except through its journal while the member's state lives; a configuration at `LCPRI_DISABLED` is ignored by its priority alone for allocation, reuse and the threshold, a disabled forced one still blocking any further forced creation until a re-plug; a committed alternative an earlier start left refuses the device until its re-plug; before each write to the handle it holds the transaction reads the handle's place again on the list of its kind, which catches what those reads can tell and not a replacement indistinguishable by them nor a reuse between the read and the write; a committed alternative is reused only as a valid committed match, by kind and committing priority | 4, 5.3 |
| N10 | The update-pending exception | admission on the installer's recorded version although, between a Stor98 update on 98 SE or ME and the shutdown the installer asks for, a devnode may still load the older image, no documented mechanism telling which image a devnode loaded; the older image's behaviour unmeasured until the leg, and the release notes saying a drive plugged in during that window may not start until the shutdown | 4 |
| N9 | The existing priorities | read before anything is added, one priority read per configuration; refused unless every basic configuration of the devnode not at `LCPRI_DISABLED` and not the journal's own committed one reads below `LCPRI_SUBOPTIMAL` | 4 |

## 10. Open, and the risks

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

## Sources

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
