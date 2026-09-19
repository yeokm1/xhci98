# Phase 22 Record - Vista and Windows 7 on both architectures, and release `1.1.0.0`

The detail behind `docs/contributing/roadmap.md`, "Phase 22 - Vista and
Windows 7, and Release `1.1.0.0`". The roadmap entry carries the goal, the
status, the task table, what is still owed and the checkpoint; this file
carries what each task did and what each reading said, moved here out of the
roadmap on 2026-09-17 while the phase was still open. Where the two disagree
about a clause, the roadmap wins.

**Most of this was written while the phase was open**, and it is kept as it
was written rather than rewritten in the past tense. A sentence that says a
box "stays open", or that something "is owed", describes the day it was
written; the roadmap's task table says how each task closed. Tasks 22.8,
22.9 and 22.10 were open when this file was made: their sections below are
the full wording each was written with, and the roadmap's "Owed" list is
where they are ticked.

**On `out\...` and `vm\...` paths in this file.** They say where a reading was
taken and what the file was called, on the host that ran it; they are not
files a clone has.

## The phase narrative, as the roadmap carried it

Goal: whether the binary this project already ships installs, loads and works
on 32-bit Windows Vista and 32-bit Windows 7 - settled from the shipping
`usbport.sys` first and guests second - and, if it does, its standing stated
in every document that names the targets; **and the tree cut as `1.1.0.0`**,
which is the first release to carry a 64-bit package.

**The cut was added to this phase on 2026-09-09 on the owner's instruction,
and it is not a consequence of the Vista and Windows 7 work.** What it
publishes is Phase 21's: an amd64 `xhci98.sys`, the second INF, and a
publisher whose `-Arch` defaults to both architectures, so an ordinary cut
now writes four directories - `release-x86`, `debug-x86`, `release-x64`,
`debug-x64`. Phase 21 bumped `src\xhci_version.h` to `1.1.0.0` and stopped
there; the number has never been spent, and until it is uploaded it stays
free (`releases/README.md`). It lands in Phase 22 rather than in 21 because
21 closes on task 21.8's guests, which may end in a negative result, and a
cut should not wait on a leg whose answer changes nothing about what is being
published.

Two things follow from putting it here and both are ordering constraints
rather than extra work. **The cut waits on task 22.5 being settled, not on a
yes from the Vista and Windows 7 guests.** If 22.5 turns out to be a driver
change it is a change to the shipping 32-bit binary, it goes into this
release, and all four existing 32-bit install legs are re-validated behind it;
if 22.5 stays empty, as everything read so far says it will, the 32-bit
binary in this release is `1.0.2.0`'s code at a new version. Either way the
cut is last. And **the asset gains five install legs** no previous cut had:
the amd64 package on the XP x64 guest, and - on the owner's instruction of
2026-09-16 - the amd64 package on the Vista x64 and Windows 7 x64 guests,
carrying task 21.8's `release`-flavour clause, and the x86 package on the
Vista x86 and Windows 7 x86 guests. The x86 half's four existing legs stay.

**What the x64 half of this release may be said to be is exactly what tasks
21.5 and 22.5 observed and no more**: three guests in virtual machines, never
real hardware, with Vista x64 and Windows 7 x64 loading the driver only on an
F8 boot, against the x86 half's four install legs. `AGENTS.md` states it and
the release notes state it for the user; do not let the act of publishing
inflate it.

Status: open, 2026-09-08, on the owner's instruction, with the `1.1.0.0` cut
added 2026-09-09. Tasks 22.1 and 22.2
are complete as of 2026-09-09, taken in one pass with 21.7 off the same
media: the six measurements on 6.0 and 6.1 x86 all pass, `USBPORT_GetHciMn`
returns the value this driver already accepts, and every import the shipping
binary names resolves in the right module on both systems. So **nothing read
statically argues against the existing 32-bit binary on Vista or Windows 7,
and task 22.5 has no work from either.** What is left of the Vista and
Windows 7 question is entirely the guest half - 22.3 and 22.4 - which is
where the two hard parts below live and where the unsigned-driver assumption
is confirmed or refuted. Left beside it, and independent of how that comes
out, is the `1.1.0.0` cut in 22.8 to 22.10.

**As of 2026-09-10, 22.3 and 22.4 are both closed.** Both guests are
installed, shut down and snapshotted (`vista-clean-install`,
`win7-clean-install`), and the static reading is taken off each. **It comes
back the same on both, and it is the answer that makes the rest of this half
cheap: everything the install path needs is already on disk.** All four
Microsoft USB files are in `Windows\System32\drivers`, the driver store stages
them again with their INF, and `usbui.dll` is there too - so the Code 39 that
XP and Windows 2000 suffer does not arise on either system, and the INF's
`LayoutFile` route is not needed on either.

Two things the pair taught that outlive it. **The two guests do not share an
accelerator** - Vista wants `tcg` because WHPX wedges its Setup after the first
reboot, Windows 7 runs under `whpx,kernel-irqchip=off` - so task 22.4's
"probe per host AND per guest" rule paid for itself for the first time
*within* a single pair. And **an accelerator may not be written down until an
install has completed under it**: the first probe stopped at the Setup
language page, recorded WHPX for both, and was wrong about one of them.

What the guest half still owes is the checkpoint itself: install the driver on
each guest and record what the unsigned-driver prompt actually did.
`build-and-test.md`, "Windows Vista and Windows 7 target VMs", is the recipe
and the readings.

**As of 2026-09-11 the premise of this phase is overtaken, and 22.5 is its
main work rather than an empty box.** Task 21.8's guests and the static
reading behind them (design record 11 sections 6.1 to 6.4) found that no
Version 200 miniport can run on Vista or Windows 7 in either architecture:
registration takes a fourth argument, the resource bits moved, and the
interrupt DPC is taken from a Version 300 slot past the end of what a Version
200 packet copies - and on x86 the registration is callee-cleaned, so there is
no inert-extra-argument escape. The owner decided the same day that the driver
is to run on Vista and Windows 7 on both architectures, with 64-bit users told
about signature enforcement in the release notes (design record 11 section 12,
decision 12). So 22.5 is a change to the shipping 32-bit binary and to the
amd64 one, this release carries it, and every install leg is re-validated
behind it - which is the case the paragraph above 22.5 always provided for.

Why a phase, and why it is a different one from 21: **this asks nothing of
the toolchain.** Phase 21 needs a second DDK, a second import library and an
arch fork before a compiler is even reached; this phase's subject is the
`xhci98.sys` that exists, unchanged, on the `.NTx86` half of the INF that
exists. If the answer is yes, it costs guests and readings and no build at
all. That is a different shape of work from 21 and a different set of ways to
fail, which is why it is not a task inside it.

Two things make it cheaper than 21's 64-bit leg:

- **32-bit Vista and Windows 7 do not enforce kernel-mode code signing.**
  That enforcement is x64-only. An unsigned build can load; PnP still warns
  at install time that the publisher cannot be verified, which is a prompt
  and not a refusal. **Confirm this on the guest rather than taking it from
  here** - it is the single assumption that would make the phase pointless if
  wrong, and it is cheap to check.
- **Both still ship `usbport.sys`.** Vista and 7 carry the USB 1.1/2.0 stack
  natively, so Option A - be a miniport under the OS's own usbport - is still
  on the table. It is Windows 8 that replaces it for xHCI, and Windows 8 is
  not in scope here.

Two made it harder, both written down as consequences of Phase 21's reading.
The second is settled as of 2026-09-09; the first is not, and is now the whole
of the phase's risk:

- **They stage driver packages into the driver store**, which the INF's
  `LayoutFile` route was not written for. That route is Phase 19's fix - the
  NT install path pulling `usbport.sys`, `usbd.sys` and `usbhub.sys` from the
  operating system's own cache, because an xHCI-only NT install has none of
  them on disk. Whether that mechanism survives into 6.0 and 6.1 is a
  reading, not a deduction. One half of it looks easier than it did: both
  install images carry all four Microsoft USB files in `System32\drivers`
  outright, so every `COPYFLG_NO_OVERWRITE` copy should skip and never need a
  source. What that does not answer is the driver store's own validation of
  the package's file list, which is the part no static reading reaches.
- **`USBPORT_GetHciMn` has changed across lineages before - settled
  2026-09-09, and it did not change here.** The worry was real: the 5.0 -> 5.1
  step kept the registration packet byte-identical and *still* changed the
  value, and a fourth constant on 6.0 or 6.1 would have been a fourth arm on
  the refusal in `DriverEntry` (`src/xhci_dispatch.c`, the `USBPORT_GetHciMn`
  test that traces `unknown usbport lineage - refusing to register`) - a code
  change to the **shipping
  32-bit binary**, with all four existing install legs to re-validate behind
  it. Both 6.0 and 6.1 return `0x10000001`, which that refusal already
  accepts. Task 22.1 has the reading.

Tier, if the phase closes yes: supported in virtual machines, stated the way
Windows ME and 32-bit XP are. **No checkpoint tax.** No phase waits on a
Vista or Windows 7 observation, "observed on both" does not include them, and
the standing rule holds unchanged - accommodate them where the change is
small and low-risk, never at a primary target's expense.

Tasks. A box is ticked only on the evidence its line names, and a static
reading may not tick a box whose line names a guest.

## 22.1 - the static ABI pass on 6.0 and 6.1 x86

**22.1 - the static ABI pass on 6.0 and 6.1 x86.** Taken 2026-09-09 in
one pass with 21.7, extraction and disassembly only. **All six pass on
both**, and the transcription is in
`usb-xhci-info/usbport-miniport-abi.md`, "The 6.0 and 6.1 lineages".
Method `static` throughout. Nothing here implies a driver change, so
task 22.5 has no work from this task.

- Vista x86 - `usbport.sys` and `usbehci.sys` extracted from
  `sources\install.wim` image 1 (`Windows Vista Business`), hashed,
  version-stamped 6.0.6002.18005, and the six read
- Windows 7 x86 - the same, 6.1.7601.17514
- **M2 recorded: `USBPORT_GetHciMn` returns `0x10000001` on both** -
  the XP-lineage value `DriverEntry`'s `USBPORT_GetHciMn` refusal
  (`src/xhci_dispatch.c`) already accepts. No
  fourth constant, and no code change to the shipping binary
- and the differences the pass did find, none of which reaches this
  driver as it is built today: a fourth export (`DllInitialize`) that
  shifts all three ordinals but is bound by name and so is inert; two
  packet version tiers above `0x13C`, whose extra service-pointer stores
  sit behind the version tests so a `Version = 200` packet is never
  written past `0x120`; a `USBPORT_RESOURCES` that keeps every field
  this driver reads at its NT 5.x offset while growing a tail past
  `0x34`, which means its `sizeof` is not a fact about 6.0 or 6.1; the
  usbport-internal wrapper moving the packet to `interface+0x1C`, which
  no miniport reads; and the one to remember - **Windows 7's usbport has
  a second `IoGetDmaAdapter` call that passes `Dma64BitAddresses = 1`**,
  reached only by a miniport declaring `Version >= 310` *and* filling
  packet slot `0x1F8`. This driver declares 200 and fills neither, so
  usbport copies its 32-bit adapter into the 64-bit slot and there is
  one 32-bit adapter as on every earlier lineage. Raising the declared
  version some day is the one change that would move physical addresses
  above 4 GB, and `src/xhci_xfer.c:542`'s high-DWORD check is what
  stands between that and corruption

## 22.2 - the imports

**22.2 - the imports.** Read 2026-09-09 from the two systems'
`ntoskrnl.exe` and `hal.dll`, extracted in the same pass. **All ten
module/symbol pairs the shipping binary can name resolve, in the right
module, on both**: the five `ntoskrnl.exe` rows (`DbgPrint`,
`InterlockedIncrement`, `KeInitializeSpinLock`, `READ_REGISTER_ULONG`,
`WRITE_REGISTER_ULONG`), the four `HAL.dll` rows (`KeStallExecutionProcessor`,
`KeGetCurrentIrql`, `KfAcquireSpinLock`, `KfReleaseSpinLock`) and, though
it is never published, `HAL.dll!WRITE_PORT_UCHAR` for the `qemu` flavour.
Every one is a real code export with an RVA rather than a forwarder. The
`USBPORT.SYS` pair is covered by 22.1's M1. `scripts\import-gate\` still
has no 6.0/6.1 baseline behind it and this reading does not add one; it
answers the question ahead of a guest, which is the point of taking it.

## 22.3 - the install path, read statically off a clean guest

**22.3 - the install path, read statically off a clean guest.** The two
cheap readings 21.5 took for XP x64, on each of these. **Both taken
2026-09-10 off `vista-clean-install` and `win7-clean-install`, and both
come back the same way on both guests: everything the install path needs
is already on disk.** So the Code 39 that XP and Windows 2000 suffer
does not arise on either system, and the INF's `LayoutFile` route is not
needed on either - which settles the question this task was opened to
ask:

- whether an xHCI-only Vista/7 install has `usbport.sys`, `usbhub.sys`,
  `usbd.sys` or `usbehci.sys` on disk at all. **A strong prior, but not
  this box:** all four are in `Windows\System32\drivers` inside the
  `install.wim` of both systems in both architectures (read 2026-09-09
  with 22.1), and Vista and later apply the whole image rather than
  copying drivers on demand the way XP Setup does. That is a reading of
  the install media, not of an installed system, so the box stays open
  until a guest is looked at. **Asked and answered on 2026-09-10 - why
  not just read the WIM?** Because the gap between what Setup carries
  and what Setup leaves is where the defect this box hunts actually
  lived: on the XP lineage those same four files were on the media the
  whole time, in `Driver Cache\i386`, and an xHCI-only install still had
  none of them on disk, which is the Code 39 that `1.0.1.0`'s INF fix
  answers. The WIM says what an install would apply, not what it has.
  **VISTA READ 2026-09-10 OFF `vista-clean-install`, AND THE ANSWER IS
  YES - ALL FOUR ARE ON DISK.** `Windows\System32\drivers` holds
  `usbport.sys` (226,304), `usbhub.sys` (196,096), `usbehci.sys`
  (39,936) and `usbd.sys` (5,888), plus `usbuhci.sys`, `usbohci.sys`,
  `usbccgp.sys` and `hidusb.sys`. And it is a **stronger** reading than
  the box asks for: this guest was installed with **no USB host
  controller at all**, not merely an xHCI-only one, so if the files
  survive that they survive the xHCI case a fortiori. So the Code 39
  that XP and 2000 suffer **does not arise on Vista**, and every
  `COPYFLG_NO_OVERWRITE` copy should skip with no source needed.
  **`usbui.dll` is on disk as well** (`Windows\System32\usbui.dll`,
  83,456 bytes, 6.0.6001.18000, with its `en-US` MUI and a WinSxS
  component behind it) - the fifth file the install path needs, and the
  one release `1.0.2.0` had to add to the package for the four 9x and NT
  sources. **WINDOWS 7 READ THE SAME DAY AND AGREES ON EVERY COUNT**:
  `usbport.sys` (284,672), `usbhub.sys` (258,560), `usbehci.sys`
  (42,496), `usbd.sys` (5,888), the same four companions, and
  `usbui.dll` at 80,896 / 6.1.7600.16385. **Listing trap**: Windows 7
  Setup makes the 100 MB System Reserved partition, so `7z l` on the
  image **stops at the MBR** with three volumes instead of recursing -
  no error, just 32 lines that read like an empty disk. Extract the
  volume (`7z e vm\win7.img 1.ntfs`) and list that
- where the operating system keeps them if it does not, and whether the
  `LayoutFile` route can still reach them from a driver-store install.
  **This half cannot be read off media at any price**:
  `System32\DriverStore\FileRepository` is made by the install, so a WIM
  listing cannot be asked the question at all. **VISTA READ 2026-09-10,
  and the store carries the whole payload, not just the INFs.** Three
  generations of `usbport.inf_*` are staged - `_4d107f9d` (RTM,
  2006-11-02), `_dab84ba6` (SP1, 2008-01-21) and `_2c537348` (SP2,
  2009-04-11) - and each holds `usbport.sys`, `usbhub.sys`, `usbd.sys`,
  `usbehci.sys`, `usbuhci.sys` and `usbohci.sys` beside its `.inf`,
  `.PNF`, `hccoin.dll` and `hcrstco.dll`. So the operating system's own
  copies are on disk in a place Setup can source from, and **the
  `LayoutFile` route is not needed here at all** - the question it
  exists to answer, a file that is not on the machine, does not arise
  when the file is already at its destination. **Windows 7 agrees**:
  `usbport.inf_x86_neutral_f9abf85fd00186bd` stages `usbport.sys`,
  `usbhub.sys`, `usbd.sys` and `usbehci.sys`. Note the store's naming
  differs between the two - Vista's `<inf>_<hash>` against Windows 7's
  `<inf>_<arch>_<lang>_<hash>` - so anything matching those directories
  by pattern must know which system it is reading

## 22.4 - the guests

**22.4 - the guests.** One Vista x86 and one Windows 7 x86 QEMU guest,
each with a committed generator and a launcher-gate row, the way
`setup-qemu-winxp.ps1` and `setup-qemu-winxp64.ps1` were done.
**Probe the accelerator per host AND per guest** - Phase 21 paid for
that rule twice, in opposite directions, and neither reading
generalises. **All of it done 2026-09-10**, both guests installed,
shut down and snapshotted. The accelerator rule paid for itself a third
time and for the first time *within* a pair: **these two guests do not
share one.** What this task does NOT cover, and what the phase's
checkpoint still owes, is installing the driver on either guest and
recording what the unsigned-driver prompt did.

- the generators, and they are **one recipe written once**:
  `scripts\qemu-nt6-common.ps1` holds the machine and
  `scripts\setup-qemu-vista.ps1` / `setup-qemu-win7.ps1` are thin
  callers. That is the one structural departure from the six generators
  before them and the 2026-09-07 audit's H28 is the reason - five copies
  of one resolver had drifted apart unnoticed, and two guests born the
  same day out of one recipe are the pair that would drift next
- their launcher-gate rows, plus two refusals the gate now asserts:
  `-cpu pentium3` (the 32-bit XP recipe's own value, one line away in
  the same directory) **predates the NX bit and Windows 7 Setup refuses
  such a processor**, so the shared body throws rather than writing a
  launcher that installs nothing; and the install and run launchers must
  agree on the accelerator, since the HAL is fixed at install time.
  209 checks, 9 monitor ports, none shared
- **the accelerator probed on each, 2026-09-10, host `minis-w11p-ykm` -
  and the first probe was WRONG, which is the useful part.** It ran each
  guest to its "Install Windows" language page (Vista under four
  minutes, Windows 7 about two) and recorded WHPX for both. Installing
  Vista the same day under `whpx,kernel-irqchip=off` ran the whole first
  phase and then **wedged on the boot after it**: `EIP` confined to two
  addresses, interrupts enabled at `CPL=0`, half a core burning, and the
  disk idle for **twenty-two minutes** while the boot marquee kept
  animating. Relaunched on the same half-installed image under
  `-accel tcg`, Setup **resumed** and ran to the desktop. So **Vista is
  `tcg`**, as the XP x64 guest is - and **Windows 7 is not**: installed
  the same day under `whpx,kernel-irqchip=off` it ran the whole way
  through, first reboot included, to a finished desktop, so its value is
  confirmed the same way Vista's is. **The pair disagrees**, which is
  the point: two guests one WDM revision apart, from one recipe on one
  host in one afternoon, do not share an accelerator, and neither answer
  could have been inherited from the other. Three rules came out of it,
  all now in
  `build-and-test.md`: reaching the first prompt probes nothing but
  WinPE, so **an accelerator may not be written down until an install
  has completed under it**; a pinned `EIP` is read differently at a
  prompt (screendump) than at a boot screen (disk idle time, since the
  marquee animates either way); and **plain `-accel whpx` cannot
  initialise on this host at all** (it wants nested virtualisation), so
  `kernel-irqchip=off` is the only WHPX there is here and TCG is the
  only alternative when it wedges
- **four vCPUs on both, and `thread=multi` when the accelerator is
  TCG.** Not a property of these guests but a consequence of the line
  above: TCG is the fallback, and QEMU emulates x86-on-x86 with
  multi-threaded TCG, so vCPUs become host threads. Measured on the
  Vista guest - all four threads busy and roughly even - which also
  says Setup chose the multiprocessor HAL. `thread=multi` is never
  handed to WHPX, which refuses the whole `-accel` argument rather than
  ignoring an option it does not know, and the gate asserts both that
  and the install/run agreement on `-smp`
- the disks (`vm\vista.img`, `vm\win7.img`, 32 GB each) and a smoke test
  of the generated install launcher itself, which the gate cannot do:
  `qemu-vista-install.cmd` boots the DVD and reaches "Windows is loading
  files"
- **the two Setup runs, both taken 2026-09-10.** The owner drove Setup
  at the console - the standing decision of 2026-09-03, taken for the
  32-bit XP guest and unchanged here - and both guests are shut down and
  snapshotted, `vista-clean-install` and `win7-clean-install`. Vista is
  Business (the edition the media's image 1 and tasks 22.1/22.2 name),
  confirmed on its own logon screen. **`vm\vista.img`'s provenance is
  not what a clean run of the recipe would produce and the record says
  so**: its first phase ran under WHPX up to the wedge, the rest under
  single-vCPU TCG after the relaunch resumed it, and it has been booted
  since under `tcg,thread=multi` with four vCPUs, which it uses - so
  Setup chose the multiprocessor HAL. If anything ever turns on that
  provenance, reinstall rather than argue from this line
- **a monitor-port collision caught before it was generated, and the
  gate widened so the next one is not.** These two guests were drafted
  onto **55563 and 55564**, which task 21.8 reserved in writing for the
  Vista x64 and Windows 7 x64 guests on 2026-09-09. The launcher gate
  compares generated launchers against each other, so it is blind to a
  port reserved in prose and would have stayed silent until 21.8's
  generators were written - months later, with both records believed in
  between. The 32-bit pair took **55565 and 55566**, 21.8's reservation
  stood untouched, and the gate asserted the reservation itself.
  **Released 2026-09-10, the way it was meant to be**: task 21.8's
  generators now claim 55563 and 55564, so the assertion that nothing
  takes them is gone - keeping it would assert that those guests must
  not exist - and in its place the gate asserts that they still do, so a
  guest that gave one up cannot leave it reserved for nothing. The
  ordinary no-two-guests-share-a-port scan now covers all four

## 22.5 - Version 300 on NT 6.x, both architectures

**22.5 - Version 300 on NT 6.x, both architectures.** The owner's
decision of 2026-09-11 (design record 11 section 12, decision 12). The
driver presents `Version = 300` to an NT 6.x `usbport.sys` and `200` to
everything else, from the `IoIsWdmVersionAvailable(6, 0)` answer the
amd64 build already takes and the x86 build will now take too; its
packet grows to the 300 tier (`0x1E0` x86, `0x368` amd64); its interrupt
DPC returns the `ULONG` NT 6.x reads; and x86 registration passes the
fourth argument through a cast the same way amd64 does. **Read first,
then design, then build, then guests, in that order, and no constant
moves before its reading is written down.** Four NT 6.x divergences were
each invisible until the one before it was fixed (task 21.8), and that
task's method lesson stands: disassemble the *callers*, not just the
structures.

- **the static reading of the 300 tier**, `static`, off the four NT 6.x
  `usbport.sys` and `usbehci.sys` already in `tools\`, transcribed into
  `usbport-miniport-abi.md` with its provenance rows. **Done 2026-09-11,
  the same day, with Microsoft's public symbol files loaded** - every
  usbport-to-miniport call on NT 6.x is a named `usbport!MPf_<Callback>`
  wrapper, which turned the reading into an afternoon (`lessons.md`).
  `usbport-miniport-abi.md`, "The Version 300 tier, slot by slot", and
  design record 11 section 6.5 are the record; the tier is twelve
  `ULONG`s and 29 pointers, zero is safe for every one of them, and the
  one slot that must be filled is `InterruptDpcEx`:
  - every test of the interface `Version` field (`interface+0x10` x86,
    `+0x20` amd64) in all four binaries, so that what `300` turns on is
    enumerated rather than assumed. The `>= 0x12C` copy size and its two
    service pointers are known, the `>= 0x136` 64-bit DMA adapter gate is
    known and must stay closed; anything else is the finding. **Found:
    every `>= 300` test outside registration is a wrapper gate that
    NULL-checks the slot behind it, plus one in
    `USBPORT_AllocateControllerCommonBuffers` that reads packet `0x13C`
    as a count of extra common buffers, zero meaning none. Three
    wrappers on 200-tier slots - `CloseEndpoint`, `RebalanceEndpoint`,
    `TakePortControl` - test `>= 300` before calling at all, so those
    three become reachable on NT 6.x for the first time**
  - every read of a packet slot in `[0x13C, 0x1E0)` x86 and
    `[0x250, 0x368)` amd64: which slots usbport calls, from where, under
    which lock, NULL-checked or not, with what arguments and return - a
    table with a verdict per slot, must-fill, may-zero, or written by
    usbport. The slots Microsoft's own `usbehci.sys` fills (x86
    `0x16C`-`0x180`, `0x1CC`, `0x1D8`; amd64 `0x280`-`0x2A8`, `0x340`,
    `0x358`) are identified by reading what usbehci puts there. **Found:
    29 named slots, all NULL-checked; two OUT services at `0x1B0`/`0x1B4`
    (`0x308`/`0x310`); six slots with no reader at all; the amd64 map is
    `0x280 + (X - 0x16C) * 2`, confirmed at every site. One hard rule
    from the tier above: the 310 slot at `0x1E0`/`0x368` is called
    without a NULL check behind `MiniPortFlags & 0x4000` - never set it**
  - the `InterruptDpc` return value: what bits 0 and 1 mean, read from
    usbport's consumer and from usbehci's producer both, and whether NT
    6.x still reads `packet+0x4C` / `+0x70` for anything at all. **Found:
    `ULONG (extension, BOOLEAN enableInterrupts)` under usbport's DPC
    lock; bits 0 and 1 are read identically and either invalidates the
    root-hub interrupt endpoint; usbehci returns 2 for a port change, 1
    for a transfer interrupt with a pending count, 0 otherwise, and its
    own `0x4C`-slot function is a thunk onto the same routine. NT 6.x
    never reads `0x4C` / `0x70`**
  - the NT 5.x control: that NUSB, SP4, XP SP3 and XP x64 `usbport.sys`
    test `Version` nowhere but at `>= 100` and `>= 200`, so a grown
    packet under `200` is inert there by reading and not by hope.
    **Found: exactly that, on all four, every compare in `.text` listed
    in `legal-provenance.md` section 4**
- **the design, written into design record 11 before code**: the
  300-tier packet declaration with measured sizes and offsets on both
  architectures, the two-version registration, the x86 four-argument
  call through a cast of the one import (the import library binds by
  plain name, so no second stub is needed and `XHCI_CHECK_STACK_DELTA`
  is the net under the `qemu` flavour), the `ULONG` DPC serving both
  slots, and `XhciResourcesRequired` settled on x86 from the same answer.
  **Decided by the owner 2026-09-11 as recommended: `300` on NT 6.x
  only, `200` everywhere else** - it keeps every NT 5.x and 9x system
  wire-identical to what it has been observed with, at the price of the
  runtime branch decision 10 already chose. Design record 11 section
  6.5 is the design
- **the build**: `src\xhci_usbport.h`, `src\xhci_dispatch.c`,
  `test\test_packet.c` and host tests carrying both arms; the x86
  allowlist row for `ntoskrnl.exe!IoIsWdmVersionAvailable` with its
  Windows 98 evidence - `w2k-export` in both SP4 kernels,
  `win98-precedent` in NUSB's own `USBPORT.SYS`, which imports it, and
  `ntkern-name`; every gate green on all three flavours of both
  architectures. **Done 2026-09-11.** The tier is declared as read
  (`sizeof` `0x1E0` / `0x368`, six new asserts), `xhciInterruptDpc`
  returns the `ULONG` and serves both slots, DriverEntry's branch runs
  on both architectures and the built x86 `qemu` binary shows the
  three-`push` and four-`push` arms into one IAT slot, the
  post-registration verifier holds usbport to the tier it was offered,
  and the gate re-derived all three kinds of Windows 98 evidence for the
  new row itself. Host tests 12,610 + 232 + 2,027 (both architectures),
  import gate 13 pairs x86 / 8 amd64, INF self-tests 452, packager 254,
  launchers 330. No INF touched. Design record 11 section 6.5, "Built"
- **the guests, in this order, `qemu` flavour first, and the readings
  named before each boot**: Vista x64, where the wall is known (`dpc
  count` climbing with `isr count`, No Op `CC_SUCCESS`, then the clauses
  21.5 took - HID, mass storage, composite audio, disable / enable /
  remove / rescan); Windows 7 x64, never yet booted with the driver;
  Vista x86 and Windows 7 x86 off `vista-clean-install` and
  `win7-clean-install`, which also takes this phase's unsigned-prompt
  reading; then XP x64 again, for the `200` arm on amd64; then the four
  32-bit legs - 98, ME, 2000, XP32 - because the shipping binary changed
  - **Windows 7 x86: taken 2026-09-12 over four runs (`p225win7x86r2`
    to `r5`), and it found a defect that does NOT block this cut.**
    Install, first plug, disable and recovery-by-restart all pass; a
    **disable/enable cycle intermittently comes back without the audio
    device**, at a rate that is not a measurement (3 in 5, then 1 in
    11, with one cycle recovering on its own minutes later - only the
    observer's waiting time separates "arrested" from "slow").
    Localised, from usbport's own log ring and a static read of
    `usbport.sys`, to a **write-after-queue lost wakeup in**
    **`USBPORT_Xdpc_iSignal`**: it calls `KeInsertQueueDpc` before
    storing the queued state and takes no lock, so a DPC that fires
    first reads the stale idle state, drops the work, and leaves the
    done list marked queued for ever. This driver completed the
    transfer that is waited on (252 bytes) and usbport accepted the
    completion; the loss is entirely inside usbport, on a path guarded
    by a lock not exposed to miniports. **Written up as
    [issue 7](../../issues/07-win7-x86-enable-arrest-usbport-done-dpc.md),
    which states plainly what is NOT established.** When this box was
    written the single-vCPU run was unrun and this was a reading of a
    binary rather than a result; **that run was taken on 2026-09-12
    on the x64 guest and the prediction held** (the Windows 7 x64
    sub-box below). **The x86 guest has still not been re-run
    single-processor**, and the two objects usbport holds are still
    unnamed on either target. **Owner's ruling,
    2026-09-12: the leg stops being the critical path and the cut goes
    ahead**, with 22.6 disclosing it as a limitation of `1.1.0.0`
    rather than the cut waiting on it
  - **Vista x64 and Windows 7 x64 RE-TAKEN against the current binary,
    2026-09-12** (`p225vistax64re1`, `p225win7x64re1`), because
    `8a46d1b` and `789848c` landed after the 2026-09-11 legs and those
    were therefore recorded against a superseded binary. Each image was
    reverted to its clean-install snapshot first. **Install, the three
    devices and the Device Manager disable PASS on both**, with
    `read-v300.ps1 -Expect nt6` reading ALL PASS on every load -
    `presented 0000012C`, `nt6 services written 2`, `isr == dpc`, No Op
    `CC_SUCCESS`, no `ABI-SUSPECT`, no `ResetController` - and all
    three devices bound with `DescIsoEntries 2`. **Vista x64's enable
    also passes, so the 2026-09-11 result holds on the new binary**
  - **Windows 7 x64: the ENABLE ARRESTS, and it is issue 7's defect on
    the other architecture.** One install on a freshly reverted image;
    the enable came back with **2 slots, 1 device, `DescIsoEntries 0`**
    and held for a **threshold fixed at ten minutes before the run**
    (twenty identical samples). usbport's ring was **100% its idle
    poll with zero work records**, `Tmt2` naming **exactly two objects
    per tick for ever**, while this driver showed submitted ==
    completed and every refusal counter zero. **So the defect is not
    x86-only and not a property of the 32-bit binary** - a separate
    amd64 build, a separate toolchain, a different `usbport.sys`.
    **Then `-smp 1` was run on the same guest within the hour and the
    arrest VANISHED across five consecutive disable/enable cycles**,
    all six loads at `SlotsEnabled 3` / `DescIsoEntries 2` with no
    non-zero refusal counter at all - **which is issue 7 section 6's
    own experiment, and it held.** Evidence in `vm\ring-win7x64-re1\`.
    What was still owed when this box was written - the x86 guest
    re-run single-processor, and the identity of the two objects - is
    **answered by the sub-boxes below**, which is why this box is now
    ticked: the two objects are named in the next one (two usbport
    transfer records on the FDO's all-transfers list), and the
    single-processor run is the `-smp 1` result recorded in the
    paragraph above. *(The box stayed unticked over eight ticked
    children with its own owed list answered underneath it - the
    2026-09-16 audit's E8.)*
    - **2026-09-13, host-side: the two objects named, the cause
      read, and the fix landed in source - live re-run owed.** The
      objects are two usbport transfer records (`TrxC`) on the FDO's
      all-transfers list, which `Tmt2` walks - a count of outstanding
      transfers, one of them the mouse's standing read, not of blocked
      threads. The cause: NT 6.x `USBPORTSVC_CompleteTransfer` takes no
      lock and assumes the miniport calls it from a callback usbport
      made under its EpList lock (`PollEndpoint`, as usbehci does);
      this driver delivered from anywhere, and the r5 completion came
      from `RH_GetPortStatus`, which usbport calls at PASSIVE with no
      lock, so the done DPC ran on the same CPU before its state store.
      Fixed: delivery always at DISPATCH under a private lock, and on
      the 300 tier only from `PollEndpoint` / `AbortTransfer` /
      `SetEndpointState`, with forced lifecycle drains and a 1 s poll
      fallback. Host tests 12,675 green with three new vectors and two
      new nets; both architectures built through every gate. Issue 7
      section 7 is the record
    - **2026-09-13, the fix RE-RUN on all four NT 6.x guests, and
      every clause passes on each** (`fix7win7x64`, `fix7vistax64`,
      `fix7win7x86`, `fix7vistax86`): each off a fresh revert, one
      install, `-smp 4`, the three devices, five watched
      disable/enable cycles against a ten-minute threshold fixed
      before each click, remove and rescan. Every enable reached 3
      slots / 3 reopened / iso 2 within a minute; every remove's
      teardown finished within one 30 s sample; every rescan tree was
      clean. `completions delivered forced` and `completion fallback
      polls` read 0 on every load - from guest memory on both x86
      guests, and exactly from the log on amd64, where the other two
      completion counters are lower bounds at the print cap.
      **Deviation, stated rather than smoothed over: the first
      Windows 7 x86 disable was refused by Windows** (a restart prompt
      twice, nothing reached the driver) and was applied by a restart;
      what vetoed it was not read. Every later disable on that guest,
      and every disable on the other three, applied live. Issue 7
      section 7.5 is the record. **Owed: the NT 5.x legs** (XP x64 for
      the `200` arm on amd64, then 98, ME, 2000, XP32), where the tier
      must read `completions delivered only under usbport's lock` 0
    - **2026-09-13, the first NT 5.x leg: Windows XP x64, the `200`
      arm on amd64** (`fix7xp64`). Off a fresh revert, one install,
      the three devices, five watched disable/enable cycles against a
      ten-minute threshold, remove and rescan. Seven loads, one build
      stamp, `read-v300.ps1 -Expect nt5` ALL PASS on every one; all
      five disables applied live with no restart prompt; every reload
      3 slots / 3 reopened / iso 2. **The tier read the 0 it must** -
      `completions delivered only under usbport's lock` 0, and
      `delivered under usbport's lock`, `held for PollEndpoint`,
      `forced` and `fallback polls` 0 with it, exact rather than
      capped. **But this guest ran on ONE vCPU** (its launcher carries
      no `-smp`, where section 7.5's four ran `-smp 4`), and `-smp 1`
      is the rung under which the UNFIXED binary also passed five
      cycles - so this is a 200-tier compatibility result and **not**
      an SMP one. Deviation: the remove took two 30 s samples where
      all four NT 6.x guests took one, with forward progress in
      between. Issue 7 section 7.6 is the record.
      **Owed: 98, ME, 2000, XP32**
    - **2026-09-13, XP x64 raised to `-smp 4` and it BUGCHECKED on the
      second live disable/enable cycle** (`fix7xp64smp4`). Raising it
      needed **no reinstall and no HAL switch** - the claim in 7.6 that
      it would was wrong and is corrected there; XP x64 has one HAL and
      has reported `ACPI Multiprocessor x64-based PC` since install, so
      `-smp 4` plus one restart was the whole change, and the dump reads
      `MP (4 procs)`. `D1 DRIVER_IRQL_NOT_LESS_OR_EQUAL` at
      **`USBPORT+0x1d1a7`**, reading `fffffadf00000000` at IRQL 2 while
      walking a usbport-private list under usbport's own lock - an
      address whose **low dword is zero**, which is a 64-bit pointer
      written 32 bits wide. *(This said "usbport's high 32 bits", and
      issue 8 section 3 withdrew that: `fffffadf` is the high half of
      every non-paged pool pointer on this guest, not something
      particular to usbport. The finding is the zero low half. The
      2026-09-16 audit's E8.)* **It did not
      reproduce**: one cold enable and five live cycles clean
      afterwards, so it is timing-dependent, not deterministic (0 in 7
      uniprocessor loads, 1 in 2 SMP cycles, 0 in 6 more). **Whose write
      it is has NOT been established** - usbport faulted, which does not
      mean usbport wrote it - and whether the 7.3 fix introduced it is
      unknown, task 21.5 having passed this sequence on one vCPU only.
      Owed: Driver Verifier special pool (catches a true out-of-bounds
      write) **and** an amd64 layout audit (catches a wrong-offset write
      *inside* a usbport allocation, which Verifier cannot see). Issue 7
      section 7.7 is the record. **The SMP leg is UNFINISHED, not
      passed**. *2026-09-14: the writer was usbport's own store,
      reached through this driver's delivery without the endpoint lock
      (issue 8, fixed in `20af60b`), and the leg was retaken on that
      build and PASSED - settled read, five live cycles, remove,
      rescan, 0 bugchecks, 0 damaged heads (issue 7 section 7.8). The
      Verifier and layout-audit items above are superseded by issue 8's
      caught write.*
    - **2026-09-14/15, XP 32-bit raised to `-smp 4` (`-cpu core2duo`,
      `-accel tcg,thread=multi`): two findings, then PASSED under WHPX
      on 2026-09-15.** First,
      bugcheck `FC` on the first device attach, twice, with the mouse
      alone - **not SMP**: XP SP3's `MP_CloseEndpoint` pushes three
      arguments and the 2026-09-12 two-parameter callee cleaned eight
      bytes, so usbport's epilogue returned into its own stack; XP x86
      had not run since 2026-09-07. Fixed with one callee per tier
      (`PHCI_CLOSE_ENDPOINT`), Windows 2000, NUSB and SweetLow read as
      never calling the slot. Second, on the fixed build, after a
      settled read ALL PASS (3 slots / iso 2, `only under usbport's
      lock` 0) the guest **livelocked before its first disable** on
      issue 8's mechanism, which the x86 tier does not carry the fix
      for. Issue 7 section 7.9 is the record. *The x86 tier decision
      was taken the same night: every 32-bit usbport was read to share
      the unlocked mover (issue 8 section 4d) and the `_WIN64` guard was
      lifted, so the 32-bit binary now delivers per endpoint on its 200
      tier.* *2026-09-15: on that build (`B410BA07`) the leg PASSED
      under `-accel whpx,kernel-irqchip=off` (r5, host `fw-w11p-ykm`) -
      settled read, five live cycles, remove, rescan, `forced` 0 and
      `fallback polls` 0 on every load; one run. Two TCG runs of the
      same build stalled in PnP (r4 on the third disable, r6 on a
      device install) and are not driver evidence by the owner's
      decision that multi-core 32-bit XP under TCG is not
      representative; this guest's SMP legs run under WHPX.*
    - **the remaining legs on the per-endpoint build, single-core under
      TCG** (owner, 2026-09-15; no four-vCPU and no two-processor leg -
      98 and ME are uniprocessor by construction): Windows 98 under
      NUSB, ME under SweetLow's stack, Windows 2000 SP4. Install, the
      three devices, disable/enable, remove and rescan, with `per
      endpoint only` 1 and `forced` / `fallback polls` 0. Regression
      readings of the mode, not tests of issue 8's race. *Taken
      2026-09-15, all three PASSED on what each can take (issue 7
      section 7.10): ME the full leg; Windows 98 without controller
      stops, which NUSB cannot survive, device unplug/replug instead;
      Windows 2000 with the USB audio device unplugged before each
      disable.*
    - **Windows 2000 refuses a live controller disable while a USB
      audio device is attached** (2026-09-15, issue 7 section 7.10):
      twice a restart prompt, the driver seeing only the mouse
      endpoint's stop and aborts and no `StopController`; with audio
      unplugged every disable applied live. What holds the stack was not
      read, and whether it is the OS audio stack or something this
      driver answers is not established. (22.10 leg 9 later read the same
      refusal on Windows 7 x86: a `PNP_VetoOutstandingOpen` veto by the
      audio function, gone with Windows Audio stopped.) No earlier Windows 2000 run had
      audio attached for a disable. *2026-09-16: reproduced on the
      build before `413581c` (per-endpoint delivery compiled out, both
      mode flags 0) once USB Audio Device was bound, with this driver
      seeing nothing of the refused disable - so not the per-endpoint
      change. Audio alone reproduces it (per-endpoint build), with this
      driver's counters flat and nothing outstanding at the refusal, so
      the holder is above the miniport - the audio stack holding the
      device open is inferred, not read. Closed 2026-09-16 on the
      owner's decision: recorded as a Windows 2000 known limitation in
      `docs/using/release-notes.md`, the holder left unnamed*
    - **`scripts\vm-matrix\prepare-image.ps1 -Xfer` stages a stale
      package**: it reads `out\pkg-qemu`, while `make-package.ps1` has
      written `out\pkg-qemu-<arch>` since the x64 split, so a prep boot
      is handed whatever the old directory last held (found
      2026-09-15, when it held a 1.0.2.0-era build; the single-core
      legs were run from `vm\xferxp` instead). *Fixed 2026-09-16:
      `lib\fresh.ps1` `Get-QemuPackageDir` / `Get-QemuPackageProblem`
      name `out\pkg-qemu-<arch>` for both `prepare-image.ps1` and
      `run-matrix.ps1`'s post-release header, and refuse (rather than
      use) an untagged `out\pkg-qemu` or a binary without its INF;
      `selftest.ps1` holds 11 checks for it, 7 of which fail against
      the old path, and reads 252 checks, all passed. No guest run*
  - **Vista x64: REMOVE/RESCAN does not complete, twice, and that
    clause had never been run on this guest.** Device Manager Uninstall
    of the devnode (package left in the store) wedged: attempt 1 sent
    **nothing to the driver at all in ~21 measured minutes**, its ring
    one `biCF` tag on two objects at ~37 records/s; attempt 2, after a
    reset and a fresh load, **reached the driver at 4m01s**
    (`AbortTransfer`, endpoint stop/dequeue/restart) and then stopped
    dead, its ring 100% idle poll with `Tmt2` on **eleven** objects.
    **Not the same phase as issue 7's enable arrest and NOT tested
    under `-smp 1`**, so do not fold the two together. Note also that
    the 2026-09-11 Vista x64 record covered install, devices, disable
    and enable only - **this clause is newly run, not newly broken.**
    Evidence in `vm\ring-vistax64-re1\`. **2026-09-13: on the fixed
    binary (`fix7vistax64`) the remove's teardown finished within the
    first 30 s sample and the rescan reinstalled clean.** One remove
    is not proof that the wedge shared issue 7's cause, and the fix
    was not written against it, so this box stays open until the
    owner decides what that one remove licenses (22.6).
    **Owner's ruling, 2026-09-16: re-run it first.** One pass licenses
    nothing on its own; more remove/rescan cycles on Vista x64, on the
    current binary off a fresh revert, are taken before the box is
    ticked or a word goes into the release notes, and that is a guest
    session rather than host work.
    **RE-RUN 2026-09-16 (`vm\vistax64-rr\`): five remove/rescan cycles,
    all five pass.** A fresh copy of `vista-x64-clean-install`, `-smp 4`
    under TCG, one install of the qemu package at `3fb63c5` - whose INF
    is the committed `src\xhci98-amd64.inf`, so this is also the first
    install of the shipping NT 6.x path - the three devices, then each
    remove and each rescan held to a ten-minute threshold fixed before
    the run, with the watcher armed before every click. Every teardown
    reached `quiesce: halted` within the first 30 s sample; every rescan
    reloaded the driver at 3 slots / 3 reopened / iso 2 and read
    `read-v300.ps1 -Expect nt6` ALL PASS with `forced` and `fallback
    polls` 0 - four within one sample, the fourth after four samples with
    the log flat until then, where the click time was not recorded, so
    operator and PnP latency are not separable. Device Manager's tree was
    clean after the last. **Six clean removes in six since issue 7's
    fix**, against two wedges before it; that still does not establish
    the wedge shared issue 7's cause
- **the INF for the 6.x installs, an owner's decision on each file.**
  Vista's file queue aborts on the `LayoutFile` copies (task 21.8), and
  the shape that installs is a `.6.0`-decorated models section naming
  its own install section (`Xhci.Dev6`, a staged copy only so far). On
  amd64 that is a line in `src\xhci98-amd64.inf` that no 9x engine
  reads. **On x86 it is `%Mfg%=XhciModels,NTx86.6.0` in the one line
  Windows 98's 16-bit engine parses**, the measurement decision 2
  declined to make - so measure it on the Windows 98 guest during that
  leg's re-validation before the shipping x86 INF is touched, and record
  the answer either way.
  **Owner's decisions, 2026-09-16 - one per file, and they differ**
  (design record 11 section 12, decision 13):
  - **`src\xhci98-amd64.inf`: adopted as staged.**
    `%Mfg%=XhciModels,NTamd64,NTamd64.6.0`, `[XhciModels.NTamd64.6.0]`
    naming `[Xhci.Dev6.NTamd64]`, which copies `xhci98.sys` alone and
    writes the same values and service as the NT 5.2 path. The gate
    learned the path the same day, and learning it found a gap: it read
    **only the first models section** of a `[Manufacturer]` line, so the
    staged copy had passed as `models: 1` with its whole NT 6.x path
    never checked. Now each model is checked against the install path its
    own models section selects, `PATH-MFGDEC` requires both fields and
    refuses a third, and `OS-ONNT6` refuses any of the four OS-supplied
    files on the NT 6.x path. Eleven new self-test cases, each run
    against the previous gate and seen to fail there, plus a `models: 2`
    assertion and the hardware ID of the new models line in the
    two-INF comparison; `expected-footprint-amd64.txt` regenerated with
    the NT 6.x device install. **What it has not had is a guest:** the
    XP x64 engine has never read the two-field line (every XP x64 leg
    installed through the one-field one), and that it ignores the `6.0`
    field is documented behaviour rather than this project's reading -
    task 22.10's fifth leg is what reads it. The NT 6.x legs of
    2026-09-13 ran the same sections from a staged copy, so they are
    the evidence for the shape and not for this file
  - **`src\xhci98.inf`: measured on Windows 98 first**, as this box
    always said. Install `%Mfg%=XhciModels,NTx86.6.0` with its
    `[XhciModels.NTx86.6.0]` and `[Xhci.Dev6.NTx86]` sections (the
    staged `vm\xfervista\xhci98.inf` is that file) on the Windows 98
    guest under NUSB, and on Windows ME, which shares the 16-bit engine;
    record what each engine does with the line; only then change the
    shipping file, lift `PATH-MFGDEC`'s x86 refusal for exactly that
    field, and re-take the NT 5.x x86 legs behind it. **One consequence
    to carry with it:** `xhcisnap.c`'s `ourSections` recognises a driver
    key by `InfSection`, matched exactly against `Xhci.Dev` and
    `Xhci.Dev.NTx86`, and fails closed on anything else - what an NT 6.x
    install records there is unread, and if it is `Xhci.Dev6` the tool
    will not recognise the key until a row is added.
    **The 16-bit engine half was READ 2026-09-16, and it takes the
    line** (`vm\inf60-w98\`, `vm\inf60-me\`). The candidate was
    `src\xhci98.inf` at `3fb63c5` plus exactly the three staged hunks
    (byte-identical to `vm\xfervista\xhci98.inf`), and the x86 gate
    refused it on `PATH-MFGDEC` alone, no `W98-*` rule. **Windows 98 SE
    under NUSB**, off a fresh copy of `post-nusb` that never had this
    driver: Found New Hardware, location `d:\` only, and the engine
    offered "USB 2.0 eXtensible Host Controller (xhci98)" from
    `D:\XHCI98.INF` - a name only `[XhciModels]` carries - installed it
    with the documented Windows 98 CD prompt, and on the cold boot the
    driver loaded (`built Sep 16 2026 18:39:20`, `read-v300.ps1 -Expect
    nt5` ALL PASS) and a hot-plugged mouse enumerated, so `[Xhci.Dev]`'s
    loader values were written. **Windows ME**, off
    `winme-clean-install` (stock stack, never had this driver): the same
    offer from the same file, installed with no prompt, and Device
    Manager shows the controller with Manufacturer "xHCI98 Project" at
    Code 2 - the documented stock-stack outcome, so a reading of the
    engine and not of a load. **What is left before the shipping file
    changes is the NT half**: Windows 2000 SP4's and 32-bit XP's setupapi
    read the same line, and whether each falls back to the undecorated
    `[XhciModels]` when `NTx86.6.0` does not match is unmeasured here.
    **Read the same evening (`vm\inf60-w2k\`, `vm\inf60-xp\`), and it
    does**, off `win2k-xonly-clean-install` and `winxp-clean-install`,
    neither of which had had this driver, with the same candidate: each
    installed "USB 2.0 eXtensible Host Controller (xhci98)", loaded it
    (`read-v300.ps1 -Expect nt5` ALL PASS) and enumerated a hot-plugged
    mouse, and each `setupapi.log`, read offline from the image, names
    `Section: Xhci.Dev` and the install section `[Xhci.Dev.NTx86]`, with
    `Xhci.Dev6` nowhere. `usbport.sys`, `usbd.sys`, `usbhub.sys` and
    `usbui.dll` were on disk after each, so the NT 5.x path's
    `LayoutFile` copies ran. **Deviation:** Windows 2000 asked for a
    restart after the install (declined; the driver was already
    running), which the earlier Have Disk route on that image family did
    not, and `setupapi.log` holds no reboot entry after the install - the
    cause is not established. **So `src\xhci98.inf` took the line the
    same evening**: its non-comment content is exactly the measured
    candidate's; `PATH-MFGDEC` now requires `NTx86.6.0` on the 32-bit
    file and refuses any other field; the x86 profile gained the NT 6.x
    path with `OS-ONNT6`; eight new self-test cases, each seen to fail
    against the previous gate, plus `models: 2` and the two NT 6.x models
    lines compared whole; `expected-footprint.txt` regenerated. What was
    re-taken on the four NT 5.x and 9x targets is the INSTALL and a load
    with one device, not the full device legs - those are task 22.10's
    install legs from the asset, which read this file on every target.
    The `xhcisnap` row above stays owed: nothing has read what an NT 6.x
    install records as `InfSection`
- **`xhcisnap` on an NT 6.x install.** `ourSections` in
  `xhcisnap\xhcisnap.c` recognises a driver key by its `InfSection`
  value, matched exactly against `Xhci.Dev` and `Xhci.Dev.NTx86`, and
  fails closed on anything else. Both INFs now install NT 6.x through
  `Xhci.Dev6` (the INF box above), and what that install records in the
  key is unread - Windows 2000 was measured recording the undecorated
  name rather than the section it ran, so neither `Xhci.Dev6` nor
  `Xhci.Dev6.NTx86` may be assumed. Read the value off a Vista or
  Windows 7 x86 guest after an install from the tree, add the row that
  matches it, and rebuild the tool. Until then the tool does not
  recognise the key on those systems. Owed since 2026-09-16. **Read
  2026-09-17 (`vm\p225-vista-infsection\`): `Xhci.Dev6`, undecorated,
  as Windows 2000 records its section.** Off a fresh copy of
  `vista-clean-install`, installed through Device Manager from the
  committed `src\xhci98.inf` and the `qemu` x86 build: the driver key,
  read offline from the SYSTEM hive, holds `InfSection = Xhci.Dev6` and
  `InfSectionExt = .NTx86`, and `setupapi.dev.log` names `InstallSec -
  Xhci.Dev6` and `ActualSec - Xhci.Dev6.NTx86`. `ourSections` gained
  exactly that row and the tool rebuilt, self-test green. On the same
  install, elevated, the tool built from `7ea7115` SKIPPED the key as
  "identified by a value NAME alone" and wrote nothing, and the new one
  matched it (`InfSection = Xhci.Dev6`) and set it. What ran is the key
  match and the registry write; the `\\.\HCD0` dump route has not been
  run on Vista or Windows 7
- **Windows 2000's restart prompt after the NT 6.x-line install.**
  On 2026-09-16 (`vm\inf60-w2k\`), installing the candidate through
  the Found New Hardware wizard on `win2k-xonly-clean-install` ended
  in "You must restart your computer", declined with the driver
  already loaded and ALL PASS. The earlier Have Disk install on the same
  image family asked for no restart, and that run's `setupapi.log`
  holds no "Device required reboot" entry after the install, so the
  cause is not in the log and is not established - whether it is the
  wizard route, the root hub's `usb.inf` install, or the widened line.
  One install off the same snapshot through Have Disk with the shipping
  INF separates the first from the other two. A restart prompt is not a
  failure, but a user will see it and the release notes say nothing
  about one. **Read 2026-09-17 (`vm\p225-w2k-havedisk\`): it is the
  route.** Off a fresh copy of `win2k-xonly-clean-install`, the wizard
  cancelled, then Device Manager -> *Update Driver* -> *Have Disk* ->
  `E:\` with the committed `src\xhci98.inf` and the `qemu` x86 build of
  13:13:02: **no restart prompt**, the driver loaded (interface `0xC8`,
  registration 0, init complete) and Device Manager showed the
  controller and the USB 2.0 Root Hub with no bang. Its `setupapi.log`,
  read offline, is line for line the 2026-09-16 wizard install's -
  `Section: Xhci.Dev`, `[Xhci.Dev.NTx86]`, the one `xhci98.sys` copy,
  the root hub's copy-only then full install from `usb.inf` - and
  neither holds a reboot entry; the one difference is the caller,
  `rundll32.exe newdev.dll,DevInstall` there against `mmc.exe
  devmgmt.msc` here. So the widened line and the root hub are ruled
  out; the prompt belongs to the Found New Hardware wizard, which also
  raised it on both of 2026-09-17's issue 5 installs
  (`out\post-release\issue5-ssflag\README.md`, the `w2k` rows) and was
  declined there too with the driver running. Why that route asks is
  not established. The release notes' documented Windows 2000 route is
  the Have Disk one, which does not ask; they now also say that the
  wizard's prompt can be answered No
- **The right-click Install on NT 6.x.** Neither INF has a
  right-click section of its own for Vista or Windows 7:
  `[DefaultInstall.NTx86]` and `[DefaultInstall.NTamd64]` are what an
  NT 6.x engine would run, and both carry the `LayoutFile` copies of
  `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll` - the copies
  whose unresolvable source aborts Vista's file queue on the device
  route (task 21.8). What the right-click route does on 6.x is unread,
  and the INF gate's `OS-ONNT6` covers device install paths only. Read
  it once on a Vista guest; if it aborts, either document the
  right-click route as unsupported on 6.x or find a decoration that
  reaches a 6.x-only right-click section - which the lesson that a
  version decoration selects the models section only says is not
  `[DefaultInstall.NTx86.6.0]`. **Read 2026-09-17 on Vista x86
  (`vm\p225-vista-rightclick\`), and it fails; the owner's decision the
  same evening is to document the route as unsupported on NT 6.x.** Off
  a fresh copy of `vista-clean-install`, right-click *Install* on the
  committed `src\xhci98.inf`: UAC for "INF Default Install", then
  "Files Needed - The file 'usbport.sys' on (Unknown) is needed".
  Cancelled, the route ends with no message at all. `setupapi.app.log`,
  read offline: `InfDefaultInstall.exe`, five queued copies, the four
  `LayoutFile` ones each warned `Missing
  SourceDisksFiles/SourceDisksNames information from INF`, then
  `SPFILENOTIFY_NEEDMEDIA: returned FILEOP_ABORT` and `Install failed,
  attempting to restore original files`. **Left behind anyway:
  `System32\drivers\xhci98.sys`**, absent from the clean base, with no
  `xhci98` service and the controller unbound, since a right-click
  section touches no device. The release notes, both INFs' comments and
  `build-and-test.md` now say so. Not read: `[DefaultInstall.NTamd64]`
  on Vista x64 or Windows 7 x64, which carries the same four copies and
  is documented as untried rather than as failing, and Windows 7 x86
- **the record**: task 21.6's tier wording for Vista and Windows 7 on
  both architectures - VM-supported, no checkpoint tax, and for x64 the
  signing paragraph beside the tier rather than in a footnote -
  `AGENTS.md`, `build-and-test.md`, `win98-wdm.md`, the release notes,
  and the `legal-provenance.md` section 4 rows; task 21.8's last box
  closes with it. **Written 2026-09-16** on the owner's decisions of
  that day: both architectures at once, nothing said about Vista x64's
  remove and rescan, and nothing about issue 7. Two gaps are stated
  rather than closed: the 32-bit pair ran only the `qemu` build from a
  staged copy of the NT 6.x sections (true until 2026-09-17, when Vista
  x86 installed the `qemu` build through the committed `src\xhci98.inf`
  for 22.5's `InfSection` reading; Windows 7 x86 still has not, and
  neither has run the `release` flavour), and the 32-bit install prompt
  was never written down (`build-and-test.md`, step 4 of the 32-bit
  section). Both are task 22.10's eighth and ninth install legs, added
  the same day

## 22.6 - the record

**22.6 - the record.** The tier stated where Windows ME and 32-bit XP
are stated, in `AGENTS.md`, `build-and-test.md`, `win98-wdm.md` and the
release notes, with the provenance rows beside it. Task 21.6 is the
worked example and its lesson transfers: check first whether the wording
the other tiers carry is true of this one before reusing it. **Done
2026-09-16, as task 22.5's record box** - one change for both
architectures. The lesson did transfer: XP x64's "on the `qemu` build
and then the `release` flavour" is true of neither pair here, and its
"installed with no prompt" is recorded for neither, so the wording
reuses neither

- **the Windows 7 disclosure - BOTH ARCHITECTURES - and it is not
  optional.** The `1.1.0.0` cut goes ahead over a known, open defect
  (the guest boxes in 22.5, and
  [issue 7](../../issues/07-win7-x86-enable-arrest-usbport-done-dpc.md)),
  so the release notes must say what a user meets - a disable/enable
  cycle can come back with a device missing, a restart recovers it and
  a rescan does not - **without** stating as settled what is only a
  reading of `usbport.sys`.
  **Owner's decision, 2026-09-16: the release notes say nothing about
  issue 7** - no known limitation and no fixed-in note. The fix held on
  all four NT 6.x guests and on every NT 5.x leg (22.5), and no
  published release ever claimed Vista or Windows 7, so there is no
  user who met the defect to be told it is gone. Issue 7 stays the
  record. That settles the three boxes below that were about the
  wording; the Vista x64 remove/rescan box is a different clause and
  stays open behind its re-run, which is why this one does too
  - **The scope this box was written with is WRONG and must be widened
    before it is drafted.** It said Windows 7 x86; on 2026-09-12 the
    same arrest, with the same ring signature, reproduced on **Windows
    7 x64** - a different binary from a different toolchain against a
    different `usbport.sys`. The disclosure covers **both**, and it
    may not imply the 32-bit binary is the thing at fault
  - **What the single-vCPU run does and does not license.** That run
    was taken 2026-09-12 and the prediction held five times over, so
    the notes **may** say the fault has needed more than one processor
    wherever it has been looked at - naming that as one target, one
    architecture, five cycles, and not a proof of absence. **The tier
    wording still may not lean on "the defect is not ours"**: the two
    objects usbport holds are unnamed on both targets and issue 7
    still declines that claim
  - **Decide what to say, if anything, about Vista x64's remove and
    rescan**, which did not complete on 2026-09-12 (22.5). It is a
    different clause from the enable arrest, was never run on that
    guest before, and was not tested single-processor - so it is
    either a second disclosed limitation or an explicitly untested
    clause, and saying nothing at all is the one option that is not
    honest. **Owner, 2026-09-16: re-run it first** (22.5's box); the
    wording waits on that reading. *Taken the same evening: five
    cycles, all pass (22.5). The wording is the owner's; the reading
    supports saying nothing about it.* **Owner, 2026-09-16: nothing is
    said.** The tier wording treats remove and rescan on Vista x64 as
    the passing clause it has been six times in six since issue 7's
    fix
  - **The premise of this box changed on 2026-09-13**: the defect's
    cause was read and is fixed in source (22.5's last sub-box,
    issue 7 section 7), and the fault was this driver's completion
    context rather than usbport's alone. If the re-run holds on all
    four NT 6.x guests the disclosure becomes a note that `1.1.0.0`
    carries the fix and what it was; if it does not, the boxes above
    stand as written. Draft nothing here until the re-run is read.
    **The re-run was read on 2026-09-13 and held on all four**
    (22.5, issue 7 section 7.5), so the condition for the note is
    met on NT 6.x. What the note says, and whether it waits for the
    NT 5.x legs, is the owner's decision and is not yet taken.
    *Taken 2026-09-16: no note at all* (the parent box)

## 22.7 - a charset gate on tracked source

**22.7 - a charset gate on tracked source.** All four boxes done on
2026-09-12; the parent stayed unticked until the 2026-09-16 audit's E8
found it, which is the shape that makes a task list unreadable - a
reader scanning the parents sees work owed that is not. Added after the cut tasks
were written and numbered 22.10 at first; renumbered here so the phase
reads in order, which also puts the number where the work sits, because
**it runs BEFORE the cut**: `1.1.0.0` is taken with it in place and
exercised rather than with it pending. The cut's
own checkpoint clause is "every gate and self-test green on both
architectures", and a gate added afterwards would never have been run
against a release. It is small and additive, and if it is dropped the
cut is unaffected. **There is no byte-level check on `src\` anywhere in
the build.** The one line-ending guard, `xhciqual\test\check-bat-eol.ps1`,
scans `*.BAT` only, for a reason that does not generalise (MS-DOS 7.1
`COMMAND.COM` parses batch lines on CR, so an LF-only `.BAT` dies and
skips its `:logerr` branches), and every other gate reads `src\` through
a compiler or parser that accepts any byte inside a comment or a string
literal.

- **the check**, shaped like `check-bat-eol.ps1` and run from
  `build-driver.cmd` with its own `errorlevel` label: **control bytes
  below 0x20 other than TAB, CR and LF, and a UTF-8 BOM at the head of a
  source file.** **Done 2026-09-12**: `scripts\check-source-charset.ps1`,
  wired in after the batch-file line-ending check with its own
  `:charsetfail` label. It scans 158 files and reports them clean.
  **One correction to the measurement this box was written from.** The
  control-byte rule does flag **zero** files across all five trees, as
  stated. The BOM rule does not - **five tracked files carry a UTF-8
  BOM**: `scripts\inf-gate\check-inf.ps1`,
  `scripts\inf-gate\test-inf-checks.ps1`,
  `scripts\package\test-package.ps1`,
  `scripts\vm-matrix\offsets.labels.txt` and `xhcisnap\README.md`. None
  is read by the 1998-era toolchain, and **on a `.ps1` a BOM is not a
  defect but the fix**: Windows PowerShell 5.1 reads a BOM-less script as
  the system ANSI codepage, so the BOM is what makes a non-ASCII script
  read correctly. The BOM rule is therefore scoped to the kinds MSVC 6.0,
  `rc.exe`, `build.exe`, `COMMAND.COM` and Win98 setup read - `.c`, `.h`,
  `.asm`, `.rc`, `.def`, `.inf`, `.bat`, `.cmd`, `makefile` and `sources`
  - while the control-byte rule keeps the full scope. Scoped that way
  both rules flag zero and the gate stayed additive, as intended
- **the check's own failure path is exercised**, added because a gate
  that has only ever passed reports a pass it did not establish - which
  is the whole of what this task is about. Seven in-memory self-tests run
  on **every** invocation (BEL caught, NUL caught, BOM caught where
  forbidden, BOM allowed where not, TAB/CR/LF clean, a UTF-8 section sign
  caught as two bytes, and a permitted BOM not hiding a non-ASCII byte
  after it) and abort the gate if the detector is broken.
  An on-disk run confirmed all three behaviours against real files,
  reproducing the original BEL-inside-"address" shape in a `src\*.c` and
  confirming a BOM on a `.ps1` still passes
- **the non-ASCII half, TAKEN by the owner 2026-09-12.** A rule on bytes
  `>= 0x80` failed the tree on exactly one character: a UTF-8 section
  sign (`0xC2 0xA7`) in an `src\xhci.h` comment reading "Design record
  08 (section)13.2's dated amendment". **Line 7788 of the same file
  already wrote "section 13.2" in plain ASCII**, 32 lines from it, so
  the tree was inconsistent with itself and the rewrite cost one
  character and a re-wrap of the paragraph. `src\xhci.h` is now pure
  ASCII, and the gate carries the rule at full scope. Its argument was
  weighed on its own and is different from the control-character one:
  the targets are Windows 98 and MSVC 6.0 with C89, and a non-ASCII byte
  in a **string literal** reaches the debugcon channel and a Windows 98
  console, where the encoding is not UTF-8. A **permitted** BOM (on the
  kinds the BOM rule does not cover) is not counted against this rule -
  the gate's own self-test caught that double-report the moment the rule
  was added, which is what the self-tests are for
- **which trees it covers** - `src\` is the one that ships; `test\`,
  `scripts\`, `xhcisnap\` and `xhciqual\` were clean on 2026-09-12 too
  under the rules as scoped above, so including them costs nothing today
  but adds places a future edit can trip the gate. **All five are
  covered.** The walk is a filesystem walk, not `git ls-files` - no other
  gate in this build shells out to git and this one does not start - so
  four git-ignored paths that a walk finds and git would not are skipped
  by name, each for a stated reason: `src\obj*` (build output),
  `scripts\local\` (per-operator bench tooling), `xhciqual\test\hdd\` and
  `...\win98hdd\` (generated guest disks), and
  `scripts\vm-matrix\matrix.config.psd1` (the per-host copy of a tracked
  sample). An extension allowlist does the rest of the work, so no
  binary is ever opened

**Why this is worth a task rather than a habit.** On 2026-09-12 a
diagnostic comment written into `src\xhci_slot.c` through a
**double-quoted** PowerShell here-string put a literal **BEL (0x07)**
into the source - the backtick is PowerShell's escape character and
`` `a `` is its alert escape - and `build-driver.cmd all` then compiled
all three x86 flavours and **passed every gate with that byte present**.
It was caught only because the text rendered as "ddress" in `git diff`.
Writing source through a PowerShell string layer is the documented
method in this repository, because the Bash tool mangles quotes and eats
a backslash level, so every source edit passes through an escape layer
that can inject a byte silently. It landed in a comment this time; in a
string literal it would have reached the shipping binary, and the
driver's strings go out the channel the project reads its evidence from.
`docs\contributing\lessons.md`, "A comment written through a PowerShell
here-string put a BEL into `src\`, and every gate passed", is the record.

## The `1.1.0.0` cut

The `1.1.0.0` cut. These three run last, after 22.5 is settled either way,
and they are the phase's other half rather than a coda to the first.

## 22.8 - what a cut needs that no gate supplies

**22.8 - what a cut needs that no gate supplies.** Each of these is
hand-written or hand-bumped, and the first two are refusals rather than
omissions:

- the `releases\history.md` entry for `1.1.0.0`, dated to agree with
  the INFs' `DriverVer` - `make-release.ps1` checks for it **before**
  anything is built and refuses without it. What the entry says is
  addressed to a user, because the download's `readme.txt` embeds it
  verbatim; how the release was assembled does not belong in it
  (`releases/README.md`)
- the release date in `src\xhci_version.h` and the `DriverVer` line in
  **both** INFs set to the day of the cut. The INF gate checks the two
  files against the header, and `check-inf.ps1 -Arch` runs over each,
  so a stale date in the amd64 file fails the build rather than
  shipping
- `docs/using/release-notes.md`'s opening line, which states the
  version the file describes. **Nothing reads it and no gate catches
  it**, and it has sat stale across cuts before
  (`build-and-test.md`, "Versioning the driver"). The x64 statements
  task 21.6 wrote into that file say "from `1.1.0.0`"; at the cut they
  stop being a forward reference and should read as current
- `README.md`'s **Install** section, which describes the `1.0.2.0`
  download by name - two directories, `release\` and `debug\`. The
  moment `1.1.0.0` is uploaded that download has four with different
  names, and nothing reads this either. The 2026-09-16 audit added the
  naming note that makes the section honest until then; at the cut it is
  the section itself that changes
- `.github/ISSUE_TEMPLATE/bug_report.yml` and `hardware_report.yml`,
  whose operating-system lists stop at "Windows XP, 32-bit". A reporter
  on the tier this release publishes for has no row to pick, and no gate
  reads a form
- **the version scheme, which no longer has a gap. Settled by the owner
  2026-09-10** and written into `build-and-test.md`, "Versioning the
  driver": first field a change really major enough to warrant it (and
  the `0` -> `1` step to a final release, which is the only time it has
  moved), **second a major change - a new architecture, or a new
  function**, third a patch or bug fix in the driver's code, fourth a
  release that changes only the install media or the documents. Read
  the second and third as a pair rather than by size of diff. This box
  existed because the paragraph documented only the third and fourth
  while `1.1.0.0` moved the second; all four published numbers agree
  with the rule as now written

**Done 2026-09-18**, the day of the cut. The date `09/18/2026` in
`src\xhci_version.h` and both INFs' `DriverVer`; the `1.1.0.0` entry in
`releases\history.md` (the new tiers, the four directories, the registry
value replaced by the miniport flag, issue 8's delivery rule on the 32-bit
tier, the two audits' driver and tool fixes, written for the installer);
the release notes' opening line and their three "from `1.1.0.0`" forward
references; `README.md`'s Install section rewritten for the four
directories, with the NUSB and F8 prerequisites as a step; both issue
forms' operating-system lists gaining Vista and Windows 7 in both
architectures and their example number `1.1.0.0`; and the "has not been
cut" sentences in `docs/issues/README.md` and `legal-provenance.md`
section 5.

## 22.9 - the primary targets unchanged

**22.9 - the primary targets unchanged**, the way task 19.8 and Phase
20 did it: `run-matrix.ps1 -PostRelease` on freshly re-taken 2a and 2b
clones, against the Phase 20 reports. Both primary targets are 32-bit
and neither is touched by anything in this release, so a difference
here is a finding about the release rather than about the phase.
Reports under `docs\contributing\runs\run-22-post-release\` (written by 22.9;
the directory does not exist until then). **Since 2026-09-18 it follows the
cut** (the owner's decision: cut first), and it covers XP x64 and Windows 7
x86 as well (`xp64-fresh`, `win7-fresh`, design record 09 section 2.6); the
harness learned both targets the same day.

*Preparation, not yet started (2026-09-18).* The procedure is
`scripts\vm-matrix\README.md`, "The post-release run". Build first -
`build-driver.cmd qemu` and `qemu -amd64`, `make-package.ps1 -Flavor qemu`
with and without `-Arch amd64`, `gen-offsets.ps1` with and without
`-Arch amd64` - then for each of `2a-fresh`, `2b-fresh`, `xp64-fresh` and
`win7-fresh`: `prepare-image.ps1 -Target <id> -Clone -FreshCopy`; `-Boot
-Xfer` and the install in the guest; `-Status`, and on Windows 98 `-Attach`
per device class; a clean shutdown (`quit` at the monitor on the NT guests);
`-Stamp`. Then `run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1
-PostRelease`. The state it starts from: `vm\fresh-2a.img` and
`fresh-2b.img` are stamped `1.0.2.0`, so both need re-preparing, and
`fresh-2b.img` was re-cloned, booted once and left half-prepared - run
`-Clone -FreshCopy` on it again. Not yet measured on the two new targets:
the liveness probe (PIT IRQ0 on an MP HAL), the 600 s boot and ready
deadlines under TCG, and whether the keep-alive mouse binds before the ready
poll; and their `ExpectNoDriver` rows for uas, serial and braille are
guesses the first run corrects. Its audio rows give 22.12 (d).

*The first run, 2026-09-18 night to 2026-09-19, and it is open: one
finding, and two readings that change earlier records.* Taken on the cut's
tree (HEAD `8706468`), the `qemu` build `926846ec74bbd4fd` (164,016 B) and
its amd64 twin, every gate green, the offset tables unchanged (`SIZEOF`
92304 x86, 95544 amd64). QEMU 11.1.0 (`v11.1.0-12130-ge470268ff4`) under
TCG unless a line says otherwise. The four reports are in
`run-22-post-release/`; the evidence is in `out\post-release\1.1.0.0*\`.

- **Preparation.** All four images re-cloned and stamped
  `base-1.1.0.0-qemu`, each stamp its file's only snapshot; the owner drove
  every in-guest install and wizard. Two corrections to how it was first
  done, both mine. Windows 98 was first taught by `-Preload` across root
  ports 2-8, but the matrix attaches every row to **root port 2** and Windows
  98 keys a devnode by port, so a first run raised the audio wizard mid-row
  and was stopped (partial output parked, not a reading); the image was
  re-taught one class at a time with `-Attach <class> -AtPort 2` - wizards
  for the High-Speed mouse, storage, uas, serial, braille, ccid, u2f and
  audio, the rest bound silently, audio attached last and left attached
  through the shutdown - and re-stamped. `-Preload` beyond port 8 is refused
  by QEMU ("usb port 9 (bus xhci.0) not found") while the script still
  counts the device as attached. And a tablet preload boot hung QEMU exactly
  as `prepare-image.ps1`'s account of the tablet hangs describes; the matrix
  excludes both tablet rows on Windows 98 for that reason, so it was never
  needed. The NT guests sit at their login screen during a run; that costs
  nothing (ready after 31 s on all three).
- **`2b-fresh` (Windows 2000): PASS**, 17 rows, 6 NODRIVER expected, 0
  against, 1:47:44. Against Phase 20's report the body differs only by the
  three "ExpectNoDriver entry did not apply" notes, whose entries the
  `1.0.2.0` release commit removed as that report asked, and the storage
  row's transfer identity count.
- **`xp64-fresh` (XP x64): FAIL on one row, `usb-net/fs` NODRIVER** on both
  legs, every fault and refusal counter zero; there is no `ExpectNoDriver`
  entry for it, and XP x64 has no in-box driver for QEMU's RNDIS device.
  Everything else as expected (uas, serial, braille NODRIVER as guessed),
  both hub rows including the churn PASS; 1:47:06.
- **`2a-fresh` (Windows 98 SE): FAIL on one row, the `usb-audio/fs`
  replug**, the second arrival never addressed - the 20.7 signature (connect
  change raised, no port reset asked for), every fault counter zero; 1:23:04,
  run alone. `lessons.md`'s entry for this row said it fails only with a
  second guest beside it. **Here it failed alone, and the emulator version
  is a variable**: the audio group by itself, same image, stamp, binary and
  host, alternating - QEMU 11.1.0 replug FAIL in four of five runs (the
  fifth, the owner's requested re-run, passed), QEMU 11.0.0
  (`v11.0.0-12122-ga4bb4b10c9`) PASS both legs in three of three. It skews
  the row; it does not decide it. `lessons.md` carries the new reading.
- **`win7-fresh` (Windows 7 x86): FAIL**, 17 rows, 3 NODRIVER expected, 1
  not reached, 4 against, 1:32:00. HID, storage, bot, ccid, u2f and the plain
  hub rows PASS; uas, serial, braille NODRIVER as guessed; `usb-net/fs`
  NODRIVER as on XP x64. Two rows need the paragraphs below.
- **`usb-audio/fs` on Windows 7 reads NODRIVER, and the device is bound.**
  Not the login (the row's own screenshot shows a logged-in desktop), not QEMU (the same
  under 11.0.0), not install time (the same with the settle raised from 35
  to 300 s through an untracked matrix copy). Booted read-only by hand with
  the device attached, Device Manager shows "USB Composite Device" and
  "Audio Device" under Sound, video and game controllers with no problem
  code. QEMU's `usb-audio` has endpoints only in its streaming interface's
  alternate setting 1, so "endpoints opened" moves only when something
  streams; an attempt to play a system sound moved no isochronous counter,
  and whether it reached the device is not established. The played-stream
  readings below settle it: no target from XP on submits a single
  isochronous transfer while playing, and XP x64 passes this row only
  because its audio stack opens an endpoint on arrival.
- **`usb-hub/churn` on Windows 7: the guest bugchecks - the finding.** The
  row read ERROR "not-executing" on the run and again on a `-Group hub`
  re-run; each time the group's debug console holds four `DriverEntry`
  lines, three of them with no `StopController` or teardown before them,
  where every other group on every target holds one: the guest was
  restarting, and the harness went on reading the first boot's extension
  address (`transfers completed` read `0x7D8306EB`, then
  `0x8201A401` -> 0). A third run through an untracked copy of the runner
  with `-action reboot=shutdown` held the guest on the stop screen:
  **`STOP 0x0000007E (0xC0000005, 0x8EAEED30, 0x8A6D749C, 0x8A6D7080)`,
  `USBPORT.SYS` base `8EAD0000`, DateStamp `4ce79c15`** (the
  `tools/win7-x86-extracted` file). The exception record is a read of
  `0x00000A04`; the faulting instruction, read statically with the public
  PDB, is `usbport!Allocate_time_for_endpoint+0x15d`, `mov eax,[ecx+0A00h]`
  with `ecx = [esi+0Ch] = 4`, and the stack walked from the context record
  is usbport's alone, `USB2LIB_AllocUsb2BusTime`,
  `USBPORT_AllocateBandwidthUSB20`, `MPx_AllocateBandwidth`,
  `USBPORT_OpenEndpoint`, `USBPORT_InternalOpenInterface`,
  `USBPORT_SelectConfiguration`, `USBPORT_ProcessURB`, down to
  `USBPORT_Dispatch` - no `xhci98.sys` frame. The trigger is the churn's
  first steps: QEMU's `usb-hub` (a Full-Speed hub) on root port 2 and a
  `usb-mouse` behind it at `2.1`; the mouse's slot was addressed and its
  configuration is what faults, in usbport's USB 2.0 bus-time budgeter. That
  is **issue 6 section 6.1's residual topology** (section 5 when this was written) - a hub with no transaction
  translator on a root port, with a Full-Speed device behind it - which
  batch 7b-V0 measured harmless on Windows 98 and 2000 and which the same
  churn passed on 2000 and XP x64 in this run. **Why the pointer is 4**
  (static, the public PDB, read after the owner chose to measure more):
  `USBPORT_AllocateBandwidthUSB20` hands the budgeter the endpoint's
  transaction translator as `[ep+1Ch] ? [[ep+1Ch]+38h] : 0` (`0x23b26`-`0x23b3f`)
  - it sees a missing TT and passes NULL on; `USB2LIB_AllocUsb2BusTime`
  takes the schedule pointer as `TT + 4` for a Full or Low Speed endpoint
  and `bus + 414h` for High Speed, and `Set_endpoint` stores it at `+0Ch`;
  so a NULL TT is exactly the `4` read at the fault, and the object's other
  words are the mouse's endpoint (maximum packet 4, period 8). The mouse has
  no TT because the driver reports every root-port device as High Speed
  (issue 6's fix), which makes the Full-Speed hub look High Speed to usbport,
  while that hub's own descriptor offers no TT and the mouse's speed comes
  from the real hub. What this predicts and has not been measured: any Full
  or Low Speed periodic endpoint behind a USB 1.1 hub faults on this usbport,
  and a bulk-only device there may not reach the budgeter; a Full-Speed
  device directly on a root port never needs a TT, which is why every HID row
  passed. Vista, and Windows 7 and Vista x64, were not tried. Evidence `out\post-release\1.1.0.0-win7-hub-diag\`: the
  stop screen, the registers, the whole guest memory as ELF
  (`win7-bsod-mem.elf`, 2.16 GB), both logs.
- **22.12 (d) cannot be read off these rows.** It needs isochronous
  traffic, and the matrix's audio row plays nothing by design (its
  expectations are inert for that reason); every report's isochronous
  counters are zero. It needs a played stream, as batch 9-V had.

What the owner has to decide, and nothing was changed for: whether the
Windows 7 finding amends `1.1.0.0` under Phase 15's rule (a release-notes
limitation for a USB 1.1 hub on Vista and Windows 7, a driver change, or
both); the `usb-net/fs` `ExpectNoDriver` entries for `xp64-fresh` and
`win7-fresh`; how the Windows 7 audio row should be judged; and the QEMU
version the harness pins.

*The owner's decisions of 2026-09-19, and what they measured.* Measure the
hub finding on the other NT 6.x targets before deciding the release; check
the net row on each OS before adding entries; take a played-stream audio
reading, widened to every target; and re-run the Windows 98 audio group once
more (the fifth 11.1.0 run above). All of it on the `1.1.0.0` qemu build.
The Vista and Windows 7 x64 guests, and 32-bit XP, were installed on fresh
overlays of their clean snapshots (`vm\t2210\<guest>-t229.qcow2`,
`pnputil -i -a` from an elevated prompt on NT 6.x, the wizard on XP; the x64
pair booted with signature enforcement disabled from F8), launched by
`out\post-release\task22-10\t229.ps1` with a QEMU `wav` audio backend as the
oracle and `-action reboot=shutdown` so a stop screen stays up; the stamped
fresh images were booted with `-snapshot`. Evidence
`out\post-release\1.1.0.0-t229-<guest>\` and `1.1.0.0-{win7,xp64,2b}-checks\`.

- **The hub bugcheck is on every NT 6.x target.** A QEMU `usb-hub` on root
  port 2 and a `usb-mouse` behind it at `2.1`, nothing else: Vista x86
  `STOP 0x7E (0xC0000005, 0x8E1A9E9C, ...)`, `usbport!Allocate_time_for_endpoint+0x162`,
  a read of `0xA04`; Windows 7 x64 `STOP 0x7E (0xFFFFFFFFC0000005,
  0xFFFFF88002D96B4C, ...)`, `+0x19c`, a read of `0xC08`; Vista x64 `STOP
  0x7E (0xFFFFFFFFC0000005, 0xFFFFFA6002A72398, ...)`, `+0x194`, a read of
  `0xC08`. On x64 the field is a pointer further in (`mov rdi,[rcx+10h]`,
  `mov rax,[rdi+0C00h]`), so the NULL translator plus 8 is `0xC08` where
  x86 has 4 and `0xA04`: the same mechanism on all four usbport builds.
  Each guest had been installed a minute earlier and bound nothing else.
- **Played audio reaches the device on Windows 2000 and on no later
  target.** The Sound panel's Test (or Sound Recorder, or a `bgsound`
  page) played to the USB speakers, the only playback device and the
  default on every guest, while the driver's counters were read over the
  monitor:

  | Target | Playing, per the guest | Iso submits | `played.wav` |
  |---|---|---|---|
  | Windows 98 SE | the startup sound | 1 (10 packets, all answered), then `USBAUDIO(01) + 00002ED4` fatal exception 00 | 0 B |
  | Windows 2000 | Sounds and Multimedia | 376 (3,760 packets, all answered) | 659,456 B |
  | XP SP3 x86 | Sound Recorder, 1.93 s of 1.93 s | 0 | 0 B |
  | XP x64 | Sounds tab | 0 | 0 B |
  | Vista x86 / x64 | Test showing Stop | 0 | 0 B |
  | Windows 7 x86 / x64 | Test showing Stop | 0 | 0 B |

  Where every counter was dumped (Windows 7 x86, XP x64, Vista x86) nothing
  was refused - every open usbport asked for was accepted and no refusal
  counter moved; on Vista x86 and Windows 7 x64 "endpoints opened" rose by
  one as playback began, so the pipe opened and no transfer was ever
  submitted down it. 32-bit XP runs the same binary
  through the same NT 5.x registration as Windows 2000, so the line falls at
  the OS's own stack from XP on; why is not read. Windows 98 is batch 9-V's
  recorded reading, the OS's own `USBAUDIO.VXD` dividing by zero after one
  URB. The records before this only ever said "bound" for audio on XP and
  later, so this is a first measurement rather than a regression. Every
  matrix audio row judges arrival, not playback, which is why XP x64's
  passes. 22.12 (d) wants isochronous counters moving and so can only be
  read on Windows 2000 as things stand; Windows 2000's reading above had
  `UnmatchedEventsTotal` 0 and no split packets.
- **The net row, checked on the OS.** On XP x64 and Windows 7 x86 the
  device sits under Other devices as "RNDIS/QEMU USB Network Device", Code
  28, hardware IDs `USB\VID_0525&PID_A4A2&REV_0000` and
  `USB\VID_0525&PID_A4A2`: no in-box driver. `matrix.psd1` now carries
  measured `ExpectNoDriver` entries for both (self-test 298 checks, every
  one passing; `-PostRelease -ValidateOnly` 0 problems). The two reports
  in `run-22-post-release/` were taken before them and still count the row
  against.
- **Why nothing plays from XP on: the High-Speed root-port report.** On the
  owner's instruction to look if it was quick. XP SP3's
  `usbport!USBPORT_IsochTransfer` takes a separate branch when the
  endpoint's device is High Speed (`cmp dword ptr [eax+110h],2`,
  `0x243EF`): up to 0x400 packets instead of 0xFF, counted as microframes
  (`packets x period >> 3`, `0x244EE`-`0x244F4`) - and every device on a
  root port is reported High Speed (issue 6), so a Full-Speed audio stream
  of one packet per frame is scheduled as a High-Speed one (static, the
  public PDB). The runtime test: the same 32-bit XP guest, the same audio
  device put behind QEMU's Full-Speed `usb-hub` at `2.1`, where the hub
  reports its true speed - Sound Recorder played `tada.wav` and the driver
  saw 196 isochronous submits, 1,960 packets, all answered, `played.wav`
  344,064 B, where on the root port it saw none. How XP's High-Speed branch
  then loses the stream is not read. Not fixable quickly: the report is
  what keeps Windows 98 and 2000 from bugchecking. QEMU has no High-Speed
  hub (`usb-hub` is its only hub, has no speed option and enumerates at
  12 Mb/s), so the USB 2.0 hub workaround the release notes name for Vista
  and 7 is unmeasured.
- **Both are known limitations in `1.1.0.0`, by the owner's decision of
  2026-09-19**, which re-cut it with `-Force` a sixth time: the release notes
  and the readme's section 7 carry the USB 1.1 hub crash on Vista and 7 and
  the silent Full-Speed audio on a root port from XP on. The four
  `xhci98.sys` restaged from `src\objfre` / `src\objchk` are byte-identical
  to the ones every leg installed (`E97FA781...`, `A3B5521A...`,
  `98A5A32A...`, `8BE118B2...`); only `readme.txt` changed. Asset
  `out\xhci98-1.1.0.0.zip` 397,113 B, 17 files, each SHA-256 identical to
  `releases\1.1.0.0\`. The overlay guests and their clean copies
  (`vm\t2210\`) were deleted on the owner's instruction.
- **Two launch traps.** A guest booted without the harness's default
  network card has its xHCI controller at another PCI slot, which XP x64
  treats as new hardware and asks to install again (Windows 7 re-binds
  silently); and a Windows 98 boot with a USB device on the command line
  parks in the BIOS unless the machine is `pc,smm=off`, as `lessons.md`
  already records for the prep boots.
- **Windows 98 audio on real hardware, the owner's hand-test (2026-09-19).**
  The ThinkPad E460, Windows 98 SE under NUSB 3.3, the `1.1.0.0` package's
  `release-x86` flavour: a
  Sound Blaster Play! 2 (`041E:323D`, Full Speed, composite, UAC 1.0)
  plugged directly into a root port played, and the owner heard the audio.
  It is the reading the played-stream table above could not take on Windows
  98 - the VM's own `USBAUDIO.VXD` faults after one URB - and it repeats
  batch 13-E's Finding X on the released binary: a Full-Speed audio device
  on a root port, reported High Speed, plays on Windows 98 where it plays
  nothing from XP on (issue 6 sections 1 and 7). No counters were read; the
  oracle is the owner's ear. Not run behind a hub this time.

## 22.10 - the cut itself, and the install route read from the asset

**22.10 - the cut itself, and the install route read from the asset.**
`build-driver.cmd all` and `build-driver.cmd all -amd64` after the
header change, both tools rebuilt, every gate green, then
`make-release.ps1` with its default `-Arch`. It writes
`releases\1.1.0.0\` with **four** flavour directories and
`out\xhci98-1.1.0.0.zip`.

- the four x86 install legs from the unzipped asset, as every cut since
  `1.0.1.0` has taken them - Windows 98 SE (on both stacks, NUSB and
  SweetLow), Windows ME, Windows 2000 SP4 and 32-bit Windows XP.
  **These legs are also the device-level re-validation of the x86 INF
  change** (22.5's INF box, `d165773`): on 2026-09-16 the widened
  `%Mfg%=XhciModels,NTx86.6.0` line was read on each of the four with an
  install and a one-device load only (ME on its stock stack, engine
  only). So each leg takes the full clauses - the three devices, and
  disable / enable / remove / rescan wherever that target can take them
  - and on the NT pair reads `setupapi.log` for `Section: Xhci.Dev` and
  `[Xhci.Dev.NTx86]`, as the 2026-09-16 readings did
- **the fifth leg, which is new: the amd64 package on the XP x64
  guest**, installed from the asset's `RELEASE-X64\` directory rather
  than from `src\objfre\amd64`. Task 21.5 already read the `release`
  flavour on that guest and the recipe for reading a flavour that
  writes no port-`0xE9` trace is in its entry; what this leg adds is
  that the bytes came out of the published download. **It is also the
  first XP x64 install through the two-field line**
  `%Mfg%=XhciModels,NTamd64,NTamd64.6.0` (22.5's INF box, `3fb63c5`):
  every earlier XP x64 leg installed through `NTamd64` alone, and that
  NT 5.2 ignores the `6.0` field is documented behaviour, not a
  reading. So read `setupapi.log` off the image as well, for the
  `XhciModels.NTamd64` models section and `[Xhci.Dev.NTamd64]` - not
  `Xhci.Dev6` - and for the four OS-supplied files on disk after it
- **the sixth and seventh legs: the amd64 package on the Vista x64 and
  Windows 7 x64 guests**, from the asset's `RELEASE-X64\` directory,
  each off its clean-install snapshot under F8. Added 2026-09-16 on the
  owner's instruction, and it closes the `release`-flavour half of task
  21.8's clauses box: every NT 6.x x64 reading so far was taken on the
  `qemu` build. Take 21.5's clauses - registered and started, the three
  devices, disable / enable / remove / rescan - read the way 21.5 read a
  flavour that writes no port-`0xE9` trace. On Windows 7 x64 it is also
  the first install through the committed `src\xhci98-amd64.inf` (22.5's
  INF box); read `setupapi.log` for `XhciModels.NTamd64.6.0` and
  `[Xhci.Dev6.NTamd64]` on both
- **the eighth and ninth legs: the x86 package on the Vista x86 and
  Windows 7 x86 guests**, from the asset's `RELEASE-X86\` directory,
  each off `vista-clean-install` / `win7-clean-install`. Added
  2026-09-16 on the owner's instruction, because the NT 6.x tier's
  32-bit half had no leg in the cut at all: every reading on those two
  guests was taken on the `qemu` build from a staged copy of the NT 6.x
  sections, so **this is their first `release` flavour and, for Windows
  7 x86, the first install through the committed `src\xhci98.inf`**
  (22.5's INF box, `d165773`; Vista x86 installed the `qemu` build
  through the committed INF on 2026-09-17 for 22.5's `InfSection`
  reading, so for it only the flavour is new). Take the same clauses as
  the sixth and seventh legs,
  read the same way, and `setupapi.log` for `XhciModels.NTx86.6.0` and
  `[Xhci.Dev6.NTx86]`. **Write down what the unsigned-driver prompt
  says and which choice took it** - no leg has recorded it on either
  guest (`build-and-test.md`, step 4 of "Windows Vista and Windows 7
  target VMs"), and the release notes' install step waits on it
- the asset's file list checked against what the packager staged, and
  no Microsoft file in it. `PKG-MSFILE` refuses one by name; the rule
  that actually closes it is the packager publishing nothing it did not
  itself stage (`AGENTS.md`, `legal-provenance.md` section 5)
- a re-cut, if one is needed, under the same number with `-Force` while
  nothing has been uploaded - recorded here and not in the `history.md`
  entry, for the embedding reason `releases/README.md` gives

**The cut, 2026-09-18.** `build-driver.cmd all` and `all -amd64` at the
22.8 header, every gate green; `XHCIQUAL.EXE` and `XHCISNAP.EXE` rebuilt;
`make-release.ps1` with its default `-Arch` at 14:50 wrote
`releases\1.1.0.0\` (`release-x86`, `debug-x86`, `release-x64`,
`debug-x64`, the two tools, `LICENSE`, `readme.txt`) and the upload set,
with no Microsoft file. Re-cut twice the same afternoon with `-Force`, nothing
having been uploaded, for the owner's changes to the download readme: the log
request asks for `FULL.LOG` first and `PROBE.LOG` only if the full run did
not finish; the "what the version number means" section became "ISSUE
REPORTING"; and Windows Server 2003 x64 is no longer named in the readme or
the history entry it embeds (it remains the same OS as XP x64, and the other
documents still say so). Asset `out\xhci98-1.1.0.0.zip` then 396,894 B; after
the ProductName re-cut below, **396,812 B**.
The packager's closing amd64 warning, which still called Vista x64 and
Windows 7 x64 outside the tier, was corrected in the same change; it is
console output and reaches no published file.

**Leg 5, XP x64, 2026-09-18 evening: passed every clause, no finding.** The
guest was `vm\winxp64.img` @ `winxp64-clean-install-smp4` (multiprocessor HAL),
copied read-only with `qemu-img convert -l` and booted through a throw-away
overlay: `-smp 4`, `-accel tcg,thread=multi`, `qemu-xhci,p3=0`, the 22.11 XP
x64 machine otherwise. The transfer drive was the unzipped
`out\xhci98-1.1.0.0.zip`'s `release-x64\` directory: `xhci98.sys` SHA-256
`AD73345A...`, 97,280 B, identical to `releases\1.1.0.0\release-x64` and
`src\objfre\amd64`, and its INF identical to `src\xhci98-amd64.inf`.

- the install: Found New Hardware Wizard, "Install from a list or specific
  location", the transfer drive (`E:` on this guest; `D:` is the CD). **The
  prompt was XP's Windows Logo one** - "has not passed Windows Logo testing
  to verify its compatibility with this version of Windows", taken with
  Continue Anyway; `setupapi.log` logs it as `#W366 ... (Policy=Warn, user
  said ok)`. No prompt for a CD, no restart asked for
- **the two-field line on NT 5.2**: `setupapi.log` records `Found
  "PCI\CC_0C0330" in ... Section name: "Xhci.Dev"` and installs
  `[Xhci.Dev.NTAMD64]`; `Xhci.Dev6` appears nowhere. Only
  `[XhciModels.NTamd64]` lists `Xhci.Dev` (`[XhciModels.NTamd64.6.0]` lists
  `Xhci.Dev6`), so the engine took the `NTamd64` field and skipped
  `NTamd64.6.0`. XP's default log level names the install section, not the
  models section, so that is the reading's form
- the four OS-supplied files on disk after it, read off the flattened
  overlay with 7-Zip: `usbport.sys` (`6FC83F49...`, 212,480 B) and
  `usbhub.sys` (`92B1744E...`, 102,400 B), the hashes "Windows XP x64
  target VM" records, `usbd.sys` (7,552 B) in `System32\Drivers` and
  `usbui.dll` (123,392 B) in `System32`; the same listing of the clean copy
  finds none of the five, and the installed `xhci98.sys` is the asset's
  `AD73345A...`
- registered and started: Device Manager shows **USB 2.0 eXtensible Host
  Controller (xhci98)** and **USB Root Hub**, no bang
- the three devices, hot-plugged over the monitor: **USB Human Interface
  Device**, **HID-compliant mouse**, **USB Mass Storage Device**, **USB
  Composite Device**, **USB Audio Device** - the five names task 21.5 read
- disable (children torn down, no restart prompt), enable (all five back),
  uninstall (the USB class gone, no restart prompt), rescan (the server-side
  install refused as unsigned, `#E358`, then the wizard reinstalled it from
  `oem0.inf` behind the same Logo prompt; all five back, no bang)
- the flavour, read 21.5's way: the port-`0xE9` log stayed at **0 bytes**
  for the whole session, and QEMU's trace shows `slot_enable` 9,
  `slot_configure` 12 - three full enumerations of three devices (install,
  enable, rescan)

Harness `out\post-release\task22-10\` (git-ignored: `xp64.cmd`, the unzipped
asset, screenshots, the disk extract); disks `vm\t2210\`; logs
`vm\t2210-xp64-l5-*`. 22.12 (a) was read on the same clean copy afterwards,
from the `qemu` build, because `release` cannot show it (22.12 below).
Legs 7, 9, 6 and 8 are below, and after them the four x86 legs, taken
last.

Whether the cut needed a re-cut was checked the same evening, because the
published binaries (14:33) predate the last `src\` commit (`26162ae`, 14:45).
That commit changed only `XHCI_DRIVERVER_DATE` and the two INFs' `DriverVer`.
The date string appears in none of the four published `.sys` files - only
the INF gate reads it - and all four published INFs, `readme.txt` and the
`history.md` entry say 2026-09-18. No re-cut for that.

**Re-cut for the ProductName, and leg 5 retaken, 2026-09-18 evening.** The
binaries' version resource still read "xhci98 - USB 2.0 over xHCI for Windows
98 SE and Windows 2000", short of the supported set since Windows ME and 32-bit
XP. The owner chose a name that lists no system: "xhci98 - USB 2.0 host
controller driver for xHCI" (`src\xhci98.rc`, `1a8970e`).
`build-driver.cmd all` and `all -amd64`, every gate green, then
`make-release.ps1 -Force`, nothing uploaded (`b4d3404`): the four `xhci98.sys`
files and their size and SHA-256 lines in `readme.txt` changed, nothing else.

*Why only leg 5 was retaken, and 22.12 (a) and (c) were not re-read.* Each
new binary was compared section by section with the one read before it -
the four published ones, 22.12 (c)'s `qemu` x86 build (`22344b2c...`) and
22.12 (a)'s `qemu` amd64 build (`150C9F5B...`). Every section but `.rsrc` has
the same layout, and `.data`, `INIT`, `.pdata` and `.reloc` are
byte-identical. `.text` differs only in build metadata, which lives there
because `.rdata` is merged into it: the debug directory's timestamp and
debug-data file offset (x86), the timestamp and the 16-byte PDB GUID
(amd64), and on the two `qemu` builds the `__TIME__` digits of the "built"
line. No instruction byte moved, so (c)'s breakpoint site `0x843D` and both
readings carry over as measured.

*Leg 5 retaken* on a fresh overlay of the same clean copy, from the re-cut
asset's `release-x64\` (`98A5A32A...`, 96,768 B), by the same route. Same
Logo prompt, `Section name: "Xhci.Dev"` four times and `Xhci.Dev6` never, the
four OS-supplied files with the same hashes, the installed `xhci98.sys`
`98A5A32A...` with the new ProductName, the same five device entries,
disable / enable / uninstall / rescan all clean, the port-`0xE9` log 0 bytes,
and QEMU's trace `slot_enable` 9, `slot_address` 20, `slot_configure` 12 - the
first run's counts exactly. Logs `vm\t2210-xp64-l5b-*`.

**Leg 7, Windows 7 x64, 2026-09-18 evening: passed every clause, no finding.**
The guest was `vm\win7-x64.img` @ `win7-x64-clean-install`, copied read-only
with `qemu-img convert -l` and booted through a throw-away overlay: `-smp 4`,
`-accel tcg,thread=multi`, `qemu-xhci,p3=0`, the 22.11 Windows 7 x64 machine
otherwise. The transfer drive was the unzipped asset's `release-x64\`
(`xhci98.sys` `98A5A32A...`, 96,768 B, identical to `releases\1.1.0.0\release-x64`;
its INF identical too). The boot took F8 -> Disable Driver Signature
Enforcement. **`sendkey f8` over the monitor does reach the Advanced Boot
Options menu** if it is sent every 200 ms from the moment QEMU starts;
"Vista x64 and Windows 7 x64 target VMs" had recorded it as ignored, which is
true of a single key sent too late.

- the install: Device Manager -> *Universal Serial Bus (USB) Controller*
  under *Other devices* -> *Update Driver Software* -> *Browse my computer
  for driver software* -> `E:\` (`D:` is the DVD). **The prompt is a Windows
  Security dialog headed "Windows can't verify the publisher of this driver
  software"**, with two choices, "Don't install this driver software" (which
  has the focus) and "Install this driver software anyway"; its details say
  the software "does not have a valid digital signature that verifies who
  published it". **Install this driver software anyway took it**:
  `setupapi.dev.log` records `Driver package does not contain a catalog
  file, but user wants to install anyway.`, publishes `oem2.inf`, and the
  wizard ends "Windows has successfully updated your driver software". No CD
  asked for, no restart asked for
- **a second dialog follows on x64, and it is not a refusal**: a Program
  Compatibility Assistant box, "Windows requires a digitally signed driver",
  naming the driver, its service, the publisher and
  `C:\Windows\System32\...\xhci98.sys`. It appeared on this boot with
  enforcement disabled, after the root hub had already installed; Close
  dismisses it and the driver runs
- a first attempt on the same boot failed, and it was the operator's: the
  answer to the Windows Security dialog was taken as "Don't install" (a
  monitor pointer click that missed), which the log records as `...and user
  does not want to install driver package` and the wizard reports as "A file
  could not be verified because it does not have an associated catalog signed
  via Authenticode(tm)" (`0xE000023F`). The retake above chose "Install
  anyway" and went through. That wizard message is therefore what a user who
  declines the dialog sees, not a property of the package
- **the committed INF on Windows 7 x64**: the driver node is
  `xhci98.inf:XhciModels.NTamd64.6.0:Xhci.Dev6:1.1.0.0:pci\cc_0c0330` and
  the install runs `[Xhci.Dev6.NTAMD64]` and `[Xhci.Dev6.NTAMD64.Services]`,
  copying `xhci98.sys` alone to `System32\drivers`. The four OS-supplied files
  were already on disk before the install (`usbport.sys` 325,120 B,
  `usbhub.sys` 343,040 B, `usbd.sys` 7,936 B, `usbui.dll` 101,376 B)
- registered and started: **USB 2.0 eXtensible Host Controller (xhci98)**
  and **USB Root Hub**, no bang
- the three devices, hot-plugged over the monitor: **USB Input Device**
  (Human Interface Devices), **USB Mass Storage Device**, **USB Composite
  Device** and **Audio Device** (Sound, video and game controllers), no bang;
  mouse and storage at 480 Mb/s, audio at 12 Mb/s
- disable (every child gone, no restart prompt), enable (all back), uninstall
  with the driver software kept (the USB class gone, no restart prompt),
  rescan (reinstalled from the driver store in two seconds through the same
  `Xhci.Dev6.NTAMD64`, **with no prompt**; all back, no bang). The device
  sequence was driven by the owner at the console
- the flavour, read 21.5's way: the port-`0xE9` log stayed at **0 bytes**,
  and QEMU's trace shows `slot_enable` 9, `slot_address` 18,
  `slot_configure` 10 - three enumerations of three devices (install,
  enable, rescan)

Harness `out\post-release\task22-10\` (`win764.cmd`, monitor 57131; the
guest's logs came off on a FAT12 floppy image, `mkfloppy.py` and
`getlog.cmd`, because the VVFAT transfer drive is read-only); disks
`vm\t2210\`; logs `vm\t2210-win764-l7-*`.

**Leg 9, Windows 7 x86, 2026-09-18 night: passed every clause, no finding.**
The guest was `vm\win7.img` @ `win7-clean-install`, copied read-only with
`qemu-img convert -l` and booted through a throw-away overlay: `-smp 4`,
`-accel tcg,thread=multi`, `qemu-xhci,p3=0`, the 22.11 Windows 7 x86 machine
otherwise (`ssflag-win7.cmd`'s line, not the WHPX one the guest was installed
under). The transfer drive was the unzipped asset's `release-x86\`
(`xhci98.sys` `E97FA781...`, 85,579 B, and its INF, both identical to
`releases\1.1.0.0\release-x86`). No F8: 32-bit Windows 7 does not enforce
kernel-mode signing. The clean image's first boot asked to restart for its own
device installs (CPU, disk, IDE channel - `setupapi.dev.log` records their
query-removes vetoed); that restart was taken before anything was installed.

- the install: Device Manager -> *Universal Serial Bus (USB) Controller*
  under *Other devices* -> *Update Driver Software* -> *Browse my computer
  for driver software* -> `E:\`. **The prompt is the same Windows Security
  dialog as on x64**, headed "Windows can't verify the publisher of this
  driver software", "Don't install this driver software" with the focus,
  and "Install this driver software anyway" ("Only install driver software
  obtained from your manufacturer's website or disc. Unsigned software from
  other sources may harm your computer or steal information."). **Install
  this driver software anyway took it**: `Driver package does not contain a
  catalog file, but user wants to install anyway.`, `oem2.inf` published,
  and the wizard ends "Windows has successfully updated your driver
  software", the tray "USB Root Hub - Device driver software installed
  successfully". **No second box on x86** - no Program Compatibility
  Assistant - and no CD or restart asked for
- **the committed INF on Windows 7 x86**: the driver node is
  `xhci98.inf:XhciModels.NTx86.6.0:Xhci.Dev6:1.1.0.0:pci\cc_0c0330`, the
  install runs `[Xhci.Dev6.NTx86]` and `[Xhci.Dev6.NTx86.Services]`, and
  `Xhci.Dev` without the `6` appears nowhere; `xhci98.sys` alone is copied to
  `System32\drivers`. The four OS-supplied files were already on disk
  (`usbport.sys` 284,672 B, `usbhub.sys` 258,560 B, `usbd.sys` 5,888 B,
  `usbui.dll` 80,896 B, all dated 2009-2010)
- registered and started: **USB 2.0 eXtensible Host Controller (xhci98)**
  and **USB Root Hub**, no bang
- the three devices, hot-plugged over the monitor: **USB Input Device**
  (Human Interface Devices), **USB Mass Storage Device** (and its volume
  under *Portable Devices*), **USB Composite Device** and **Audio Device**
  (Sound, video and game controllers), no bang; mouse and storage at
  480 Mb/s, audio at 12 Mb/s
- disable (every child gone, no restart prompt) and enable (all back, no
  bang)
- **uninstall asked for a restart the first time, and the cause was read:
  it is Windows' audio service, not this driver.** "System Settings Change:
  To finish removing your hardware, you must restart your computer", answered
  No. `setupapi.dev.log`'s uninstall section removes the HID mouse and the
  mass-storage device live (`Query-and-Remove succeeded`), then records
  `Query-removal was vetoed by USB\VID_46F4&PID_0002&MI_00\... (veto type 5:
  PNP_VetoOutstandingOpen)` - the audio function, held open by a user-mode
  handle - for it, the composite parent, the root hub and the controller,
  each `Setting needs reboot`. QEMU's trace shows nothing reaching the
  controller: no slot teardown, the ports still polled. **The control:**
  `net stop audiosrv` (Windows Audio) from an elevated prompt, then the same
  uninstall - every `Query-and-Remove succeeded`, the USB class gone, **no
  restart prompt**, the trace going quiet. The audio function's veto sits
  above usbport, where this driver is not consulted; a user with a USB audio
  device in use would see the same on Microsoft's own host controller
  drivers. The disable before it applied live with the same device attached,
  so the handle is not always open - the service picks the endpoint up at
  some point after arrival
- rescan (Windows Audio started again first): reinstalled from the driver
  store through the same `XhciModels.NTx86.6.0` / `[Xhci.Dev6.NTx86]`,
  `Signer - Not digitally signed`, **with no prompt**; all back, no bang.
  The device sequence was driven by the owner at the console
- the flavour, read 21.5's way: the port-`0xE9` log stayed at **0 bytes**,
  and QEMU's trace shows `slot_enable` 9, `slot_address` 19,
  `slot_configure` 13 - three enumerations of three devices (install, enable,
  rescan), the mouse unplugged and replugged once in between because QEMU
  routes the host pointer to the newest mouse, which the uninstall had left
  without a driver

**This reading answers two refusals recorded as unexplained.** This guest's
first disable on 2026-09-13 (issue 7 section 7.5, "Event Viewer was not read
before the restart") and Windows 2000's refused disables with the USB audio
device attached (2026-09-15, issue 7 section 7.10) both had the USB audio
device plugged in and nothing reaching the driver. Neither was read at the
time, so this is the likely cause for both, not a reading of either.

Harness `out\post-release\task22-10\` (`win7.cmd`, monitor 57132; logs off
the guest by `getlog9.cmd` on a floppy image); disks `vm\t2210\`; logs
`vm\t2210-win7-l9-*`.

**Leg 6, Vista x64, 2026-09-18 night: passed every clause, no finding.**
The guest was `vm\vista-x64.img` @ `vista-x64-clean-install`, copied read-only
with `qemu-img convert -l` and booted through a throw-away overlay: `-smp 4`,
`-accel tcg,thread=multi`, `qemu-xhci,p3=0`, the 22.11 Vista x64 machine
otherwise, no CD. The transfer drive was the unzipped asset's `release-x64\`
(`xhci98.sys` `98A5A32A...`, 96,768 B, the same bytes as leg 7).

*Reaching F8 on Vista x64.* Leg 7's route does not work here: `sendkey f8`
every 200 ms from QEMU's start for 40 s went unnoticed and the guest booted to
the logon screen, and after a `system_reset` the Windows Error Recovery screen
that follows ignores F8. What worked, on the overlay only, was making the boot
manager show its menu: `bcdedit /set {bootmgr} displaybootmenu yes` and
`bcdedit /timeout 30` from an elevated prompt, a clean ACPI power-off, then
`system_reset` and `cont`. The Windows Boot Manager menu waits, F8 there opens
Advanced Boot Options, and nine `down` from *Safe Mode* reach *Disable Driver
Signature Enforcement*. It changes the boot UI and nothing else.

- the install: Device Manager -> *Universal Serial Bus (USB) Controller*
  under *Other devices* -> *Update Driver Software* -> *Browse* -> `E:\`
  (no CD on this machine; the Found New Hardware wizard was dismissed with
  "Ask me again later"). **The prompt is the Windows Security dialog of
  Windows 7**: "Windows can't verify the publisher of this driver software",
  "Don't install this driver software" with the focus, "Install this driver
  software anyway" with the same text. It took it: `The Driver Package does
  not contain a catalog file, but user wants to install anyway.`, `oem3.inf`
  published, "Windows has successfully updated your driver software", the
  tray "USB Root Hub - Device driver software installed successfully".
  **Then the Program Compatibility Assistant box of Windows 7 x64**,
  "Windows requires a digitally signed driver" ("The driver is unavailable
  and the program that uses this driver might not work correctly"), naming
  Driver *xHCI USB 2.0 Host Controller Miniport Driver (xhci98)*, the
  service, Publisher *Yeo Kheng Meng* and `C:\Windows\System32\...\xhci98.sys`.
  As on Windows 7 x64 it is wrong about the driver: the root hub had already
  installed, and every clause below ran on that boot
- **the committed INF on Vista x64**: `xhci98.inf:XhciModels.NTamd64.6.0:Xhci.Dev6:1.1.0.0:pci\cc_0c0330`,
  `[Xhci.Dev6.NTAMD64]` and `[Xhci.Dev6.NTAMD64.Services]`. **One thing only
  Vista logs**: its driver-store import tries to stage every file any
  `CopyFiles` section names, and logs `CopyFile from ...\Package\usbport.sys
  ... failed 2` for `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll`,
  the four the XP x64 path takes from the OS media. It is not an error to the
  install: the import registers `xhci98.inf_b354c29c`, whose store directory
  holds `xhci98.inf`, `xhci98.PNF` and `xhci98.sys` alone, and the device
  installs. Windows 7 (legs 7 and 9) logs no such attempt. The four files were
  already on disk (`usbport.sys` 259,584 B, `usbhub.sys` 273,920 B,
  `usbd.sys` 7,680 B, `usbui.dll` 104,960 B)
- registered and started: **USB 2.0 eXtensible Host Controller (xhci98)**
  and **USB Root Hub**, no bang
- the three devices, hot-plugged over the monitor: **USB Human Interface
  Device** (Human Interface Devices), **USB Mass Storage Device** (its
  volume under *Portable Devices*), **USB Composite Device** and **Audio
  Device** (Sound, video and game controllers); mouse and storage at
  480 Mb/s, audio at 12 Mb/s. Audio Device sat under *Other devices* with a
  bang for under a minute while Vista installed it, then moved, no bang
- disable (every child gone, no restart prompt), enable (all back),
  uninstall with the driver software kept (the USB class gone, **no restart
  prompt** - eight `Query-and-Remove succeeded`, no veto; the audio device's
  open handle of leg 9 did not occur here), rescan (reinstalled from the
  store through `Xhci.Dev6.NTAMD64` with no prompt; all back, no bang). The
  device sequence was driven by the owner at the console
- the flavour, read 21.5's way: the port-`0xE9` log stayed at **0 bytes**,
  and QEMU's trace shows `slot_enable` 9, `slot_address` 18,
  `slot_configure` 11

22.12 (a) was read on a fresh overlay of the same clean copy straight after
(22.12 below). Harness `out\post-release\task22-10\` (`vista64.cmd`, monitor
57133; `f8spam.ps1` and `waitbootmgr.ps1`; logs off by `getlog6.cmd`); disks
`vm\t2210\`; logs `vm\t2210-vista64-l6-*`.

**Leg 8, Vista x86, 2026-09-18 night: passed every clause, no finding.**
The guest was `vm\vista.img` @ `vista-clean-install`, copied read-only with
`qemu-img convert -l` and booted through a throw-away overlay: `-smp 4`,
`-accel tcg,thread=multi`, `qemu-xhci,p3=0`, 22.12 (b)'s Vista x86 machine
otherwise, the Vista DVD in the drive. The transfer drive was the unzipped
asset's `release-x86\` (`xhci98.sys` `E97FA781...`, 85,579 B, identical to
`releases\1.1.0.0\release-x86`; its INF identical to `src\xhci98.inf`). **It
is Vista x86's first `release` flavour**; the committed INF it had installed
before, on the `qemu` build (22.5).

- the install: the Found New Hardware wizard dismissed with "Ask me again
  later", then Device Manager -> *Universal Serial Bus (USB) Controller*
  under *Other devices* -> *Update Driver Software* -> *Browse* -> `E:\`.
  **The prompt is the same Windows Security dialog as the other three NT
  6.x legs**: "Windows can't verify the publisher of this driver software",
  "Don't install this driver software" with the focus, "Install this driver
  software anyway" below it. It took it: `The Driver Package does not
  contain a catalog file, but user wants to install anyway.`, `oem3.inf`
  published, "Windows has successfully updated your driver software". **No
  second box**: the Program Compatibility Assistant box is x64's alone, as
  leg 9 found on Windows 7 x86
- **the committed INF on Vista x86**: `DriverNodeName=xhci98.inf:XhciModels.NTx86.6.0:Xhci.Dev6:1.1.0.0:pci\cc_0c0330`,
  `ModelsSec - XhciModels.NTx86.6.0`, `ActualSec - Xhci.Dev6.NTx86`,
  `[Xhci.Dev6.NTx86]` and `[Xhci.Dev6.NTx86.Services]` each exiting `0`,
  `Signer - Not digitally signed`. The store directory `xhci98.inf_2915841d`
  holds `xhci98.inf`, `xhci98.PNF` and `xhci98.sys` alone, and **this log
  has none of Vista x64's `CopyFile ... failed 2` lines**: no `failed` at all
  from the install on. The four OS files are Vista's own (`usbport.sys`
  226,304 B and `usbhub.sys` 196,096 B, the builds `legal-provenance.md`
  section 4 reads; `usbd.sys` 5,888 B; `usbui.dll` 83,456 B)
- registered and started: **USB 2.0 eXtensible Host Controller (xhci98)**
  and **USB Root Hub**, no bang
- the three devices, hot-plugged over the monitor: **USB Human Interface
  Device** and **HID-compliant mouse**, **USB Mass Storage Device** (its
  volume under *Portable Devices*), **USB Composite Device** and **Audio
  Device**; "Your devices are ready to use", no bang
- disable (every child gone, no restart prompt), enable (all back),
  uninstall with the driver software kept (the USB class gone, **no restart
  prompt**, eight `Query-and-Remove succeeded`, no veto), rescan
  (reinstalled from the store through `Xhci.Dev6.NTx86` with no prompt; all
  back, no bang). The pair was taken twice: the owner's first uninstall ran
  straight into a rescan by a mis-click, so a second uninstall was read on
  its own before the rescan; both logged the eight `Query-and-Remove
  succeeded` and no veto. The mouse was unplugged across the second pair,
  for the pointer reason leg 9 gives. The device sequence was driven by the
  owner at the console
- the flavour, read 21.5's way: the port-`0xE9` log stayed at **0 bytes**,
  and QEMU's trace shows `slot_enable` 13 (the three devices at install,
  enable and both rescans, the mouse's replug once) and, per controller
  reset, QEMU's own sweep of all 64 slots (`slot_disable` 385 = six resets
  and the mouse's unplug; leg 9's 513 is the same pattern)

22.12 (b) was then re-read on the same boot (22.12 below). Harness
`out\post-release\task22-10\` (`vista.cmd`, monitor 57134; logs off by
`getlog8.cmd`); disks `vm\t2210\`; logs `vm\t2210-vista-l8-*`.

**The four x86 legs, 2026-09-18 night: all passed, no finding.** Each off
a read-only clean copy in `vm\t2210\` made with `qemu-img convert -l`, through a
throw-away overlay, the transfer drive being the unzipped asset's
`release-x86\` (`xhci98.sys` SHA-256 `E97FA781...`, 85,579 B, identical to
`releases\1.1.0.0\release-x86` and `src\objfre\i386`; its INF identical to
`src\xhci98.inf`). The machines are the 2026-09-16 INF readings' (`vm\inf60-*`)
with USB audio added. The owner drove the GUI at the console; the harness
booted, hot-plugged the mouse, the stick and the audio device over the monitor,
and read the screen and QEMU's trace. On every leg the port-`0xE9` log stayed at
**0 bytes**, as the `release` flavour should.

- **Windows 98 SE under NUSB 3.3** (`win98.img @ post-nusb`): the Add New
  Hardware wizard, "Specify a location" `D:\`, found **USB 2.0 eXtensible Host
  Controller (xhci98)**, asked for the Windows 98 SE CD for `usbd.sys`
  (answered from `E:\WIN98`) and a restart, taken as a shutdown and a cold
  launch because this guest wedges on a warm one. Controller and **USB 2.0
  Root Hub** clean; the mouse (its `hidclass.sys` from the CD), **USB Mass
  Storage Device** and the audio device (**USB Composite Device**, **USB Audio
  Device**) bound. Disable, enable, remove and rescan **not taken**: NUSB's
  `usbport.sys` crashes on any controller stop, the release notes' first known
  limitation. `slot_enable` 3
- **Windows 98 SE under SweetLow's stack**: the same clean copy with one more
  overlay, on which NUSB's own uninstall string
  (`_USB2UN.INF,UNINSTALL`) and SweetLow's `USB2.INF` were run and the guest
  shut down (`dir` showing his `usbport.sys` 134,912 B, `usbehci.sys`,
  `usbhub20.sys`, `usbccgp.sys`). That overlay was flattened into
  `vm\sweetlow-2a.img` (snapshot `sweetlow-stack-nodriver`, no driver), which
  the owner keeps as a permanent image, replacing the one pruned on
  2026-09-06. The install ran as under NUSB, CD prompt included; the three
  devices bound, the audio one under **Composite Device** (his `usbccgp`);
  **disable** applied live (the red X on the controller and every child, no
  crash, no restart prompt), **enable** brought all back, **Remove** cleared
  the USB class, and **Refresh** reinstalled it through the wizard and the
  CD, all back. `slot_enable` 9, `slot_configure` 9: three full enumerations
- **Windows ME under SweetLow's stack** (`winme.img @ winme-clean-install`,
  with his `USB2.INF` installed on an intermediate overlay; ME keeps its own
  18,288-byte `usbccgp.sys`): Advanced, Removable Media unticked, `D:\`;
  **no CD prompt**, then the restart as a cold launch. The three devices
  bound (the audio device through ME's `WDMA_USB.INF`, under **Composite
  Device**); disable (Device Manager redrew by itself), enable, remove and
  refresh all clean, the refresh running the audio device's wizard again.
  `slot_enable` 9, `slot_configure` 9
- **Windows 2000 SP4** (`win2k-xonly.img @ win2k-xonly-clean-install`):
  Found New Hardware wizard, `E:\`, no media prompt, then the same "You must
  restart" box 2026-09-16 recorded, answered No; the driver was already
  running. `setupapi.log`, read off the flattened overlay: **`Found
  PCI\CC_0C0330 in E:\xhci98.inf ... Section: Xhci.Dev`**, `Decorated section
  name: Xhci.Dev.NTx86`, `Installing section Xhci.Dev.NTx86`; `Xhci.Dev6`
  nowhere, and no `Device required reboot` after the install. `oem0.inf` is
  the asset's INF byte for byte, the installed `xhci98.sys` the asset's, and
  SP4's `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll` on disk. The
  three devices bound silently. Disable, enable, uninstall and rescan were
  taken **with the audio device unplugged**, which is the release notes'
  documented way round Windows 2000's restart prompt on a disable with USB
  audio attached, and it was replugged after each; all four applied live
  with no restart prompt, the rescan reinstalling from `oem0.inf`.
  `slot_enable` 9
- **32-bit Windows XP SP3** (`winxp.img @ winxp-clean-install`): Update
  Driver from `E:\`, behind **XP's Windows Logo prompt**, taken with Continue
  Anyway (`#E366 ... (Policy=Warn, user said ok)`); nothing else asked for,
  no restart. `setupapi.log`: **`#I022 Found "PCI\CC_0C0330" in
  e:\xhci98.inf`**, **`#I023 Actual install section: [Xhci.Dev.NTx86]`**,
  `#I063 ... from section [Xhci.Dev]`; `Xhci.Dev6` nowhere. `oem0.inf` and
  `xhci98.sys` are the asset's; SP3's `usbport.sys`, `usbhub.sys`,
  `usbui.dll` and XP's 4,736-byte `usbd.sys` on disk. The three devices
  bound; nine "Unknown device" entries stood under Other devices while the
  audio device's classes installed and were gone once it finished. Disable,
  enable, uninstall and rescan with all three attached, no restart prompt;
  the rescan's server-side install refused as unsigned (`#E358`) and the
  wizard reinstalled from `oem0.inf`, as leg 5 read on XP x64. `slot_enable` 9

Every trace also shows two `unimplemented cap read (0x1c)` per controller
start, as on every NT 6.x leg - a capability register QEMU does not
implement. Harness `out\post-release\task22-10\` (`win98.cmd` 57135,
`winme.cmd` 57136, `win2k.cmd` 57137, `winxp.cmd` 57138; `type.ps1 -Qwerty`
for the 9x guests; the NT pair's `setupapi.log` in `l4log\` and `l10log\`);
logs `vm\t2210-win98-l1b-*`, `-l2b-*`, `vm\t2210-winme-l3b-*`,
`vm\t2210-win2k-l4-*`, `vm\t2210-winxp-l10-*`.

**The asset's file list, 2026-09-18 night.** `out\xhci98-1.1.0.0.zip`
(396,812 B, SHA-256 `45FF2726...`) holds 17 files, and `releases\1.1.0.0\`,
which the packager staged, holds the same 17; every pair is SHA-256
identical, nothing on either side alone. No Microsoft file: the four
`xhci98.sys` carry this project's version resource, and the only
"Microsoft" in any binary is the statically linked C runtime's "Microsoft
Visual C++ Runtime Library" error text inside `XHCISNAP.EXE`, part of that
tool as compiled, not a file. **That closes 22.10: nine install legs, no
finding, no re-cut.**

**The order the rest of the phase is taken in** (2026-09-18). Two rules shape
it. Risk first: a finding re-cuts the release, and every leg already taken on
the changed binary is taken again - so a reading that can amend the release
and owes no leg goes before the legs (22.12 (b), then (c), both done), then
the legs never read, and the four legacy x86 legs, read at every cut since
`1.0.1.0`, last. And one guest at a time: each guest is booted once and
everything owed on it is taken in that session.

1. **Done 2026-09-18, no finding.** XP x64 (leg 5), with 22.12 (a). First of the legs because it carries the
   one INF shape no guest has parsed, the two-field line on an NT 5.2
   engine; a finding there is an INF change, which leaves the `.sys` files
   but retakes every leg, since every leg installs through the INF. The NT
   6.x path of the same file already ran from a staged copy on 2026-09-13.
2. **Done 2026-09-18, no finding.** Windows 7 x64 (leg 7).
3. **Done 2026-09-18, no finding.** Windows 7 x86 (leg 9).
4. **Done 2026-09-18, no finding, and 22.12 (a) with it.** Vista x64 (leg 6), with 22.12 (a): the `release` flavour writes no trace,
   so revert to the clean snapshot, install the `qemu` package, and read
   `usbport services written=16` off the port-`0xE9` log. `release` cannot
   show (a)'s line at all (settled on XP x64, 22.12 (a) below), so this
   route is the only one.
5. **Done 2026-09-18, no finding, and 22.12 (b) re-read on `release` with it.**
   Vista x86 (leg 8), re-reading 22.12 (b) on the `release` flavour, or by
   the same `qemu` route if `release` cannot show it.
6. **Done 2026-09-18, no finding.** The four legacy x86 legs, then the asset's file list against what the
   packager staged. That closes 22.10.
7. 22.9, whose audio rows give 22.12 (d). (d) carries the same
   amend-the-release risk as (c) but cannot move earlier: it is read off
   22.9's rows, and 22.9's images are prepared from the cut's tree.
8. The acceptance test by hand (`docs/using/release-acceptance-test.md`),
   on a fresh VM and on a physical machine; then the owner uploads.

On a finding: fix it, re-cut with `-Force`, and retake from leg 5 every leg
whose binary or INF changed. **A `.sys` change also re-reads 22.12 (c)**, ahead
of the retaken legs, by its own recipe: rebuild the `qemu` flavour, take the
new image stamp and the breakpoint site's RVA from `dumpbin /disasm`, and run
the same five incidents (the harness is `vm\t2212c\`, git-ignored).

*Driving the NT guests over the monitor.* XP (both), Vista and Windows 7 use
US Dvorak, and `sendkey` names QWERTY positions: Win+R is `meta_l-o`, Alt+C is
physical `i`; log in as `test` / `test`. XP and Vista accelerate the pointer,
so relative moves go in 3-5 unit steps (a ratio of about 0.74-0.8).
`mouse_move` goes to the newest QEMU mouse, so a USB mouse under test is never
moved while it is suspended - drive by keyboard instead. UAC and the
unsigned-driver prompts take a click on Vista and 7, or the accelerator key.
An editor tab left open on `releases\1.1.0.0\readme.txt` across a re-cut
shows the old file, because the cut replaces the directory.

## 22.11 - issue 5's mechanism replaced, and read on every target

**22.11 - issue 5's mechanism replaced, and read on every target.**
The owner's instruction of 2026-09-16: stop writing the machine-wide
`Services\USB\DisableSelectiveSuspend`, and confirm the replacement
against each OS's own `usbport.sys`. **This is a prerequisite of 22.10,
not a successor** - the cut must not be taken until the runtime legs
below pass, because the mechanism it publishes is untested at run time
on every target. The code, INF, gate and document half is done
(2026-09-17): the driver declares `USB_MINIPORT_FLAGS_DISABLE_SS`
(0x20), both INFs lost `[Xhci.AddReg.Global]`, and the gate's `SUSP-*`
rules inverted from requiring the value to refusing either registry
spelling anywhere. `docs/issues/05-idle-suspend-and-disableselectivesuspend.md`
section 5.4 is the reasoning and every address.

- the mechanism chosen against all nine usbport builds, statically -
  bit 0x20 tested once per build, in the start routine, after the
  registry reads, forcing the selective-suspend-disabled state and
  nothing else; and the per-controller registry alternative refused on
  a Vista x86 runtime reading, the Balanced plan rewriting
  `HcDisableSelectiveSuspend` to 0 and suspending the controller at once
- the code, both INFs, the INF gate and its self-tests, the two
  footprints, the packager's readme template, and the documents
- **the runtime legs, one per target, owner at the console.** Each:
  a clean disk with the new package, an empty bus, five minutes watched
  for `cb SuspendController`, `USBCMD`/`USBSTS` off the monitor, a
  hot-plugged `usb-kbd`, then an unplug and 90 s. Plus a CONTROL leg on
  the previous build with no value, on each target the OS is known to
  idle (98 under both stacks, ME, XP, XP x64, the Vista boot pair,
  Windows 7 x86 and x64) - without the control a pass says nothing.
  Windows 2000's control needs an explicit `DisableSelectiveSuspend = 0`
  or it cannot idle at all. On Vista, flip the Balanced plan's USB
  selective suspend to Enabled during the leg: that is what broke the
  registry route and the flag must survive it. **Windows 7 x86 is the
  one that matters most** - it idles 9 s after the start.
  `out\post-release\issue5-ssflag\` holds the prepared harness (ten
  launchers on monitors 57110-57119, `prepare.ps1`, the per-OS plan).
  **All 10 done, 2026-09-17, all PASS with a valid control**: Windows
  98 SE + NUSB, Windows 98 SE + SweetLow's stack, Windows ME +
  SweetLow's stack, Windows 2000 SP4, Windows XP x86, Windows XP x64,
  Vista x86 and Vista x64 (both on the plan flip), Windows 7 x86,
  Windows 7 x64. ME, XP x64, Vista x64 and Windows 7 x64 were the
  first readings of those systems without the value at all. Virtual
  machines only; 22.10 is no longer waiting on this task.
  The readings are written up in issue 5 section 5.5 (per-run detail for
  Vista x64 and the two SweetLow-stack targets included) and their provenance row in
  `legal-provenance.md` section 4
- the last open static item, closed 2026-09-17: the callers of
  `USBPORTBUSIF_UsbdQueryControllerType`, the one route by which the raw
  `MiniPortFlags` word leaves usbport. There are none - no NT 6.x
  `usbhub.sys` calls that slot (`interface+0x2C` x86, `+0x58` amd64),
  against 4 and 5 calls per x86 build and 3 and 4 per x64 build on the
  two adjacent slots swept as controls. Bounded to the OS's own
  drivers; a third-party holder of the USBDI interface is not something
  a reading here can enumerate

## 22.12 - the guest readings the 2026-09-17 audit fixes owe

**22.12 - the guest readings the 2026-09-17 audit fixes owe.** Written
2026-09-18, open. The audit of 2026-09-17 (root `handoff.md`, deleted once
answered; the memory `repo-audit-2026-09-17` and the commit messages from
`188cb32` on branch `audit-2026-09-17` are the record) fixed the driver in
four places whose effect only a guest shows. The roadmap's owed box names
the four; this section takes their readings as they come. Ordered ahead of
22.10 on 2026-09-18: (b) gates the cut as 22.11 did, because it changes
root-hub suspend/resume on a path no target has measured; (a) is read off
22.10's own legs, and (d) off 22.9's matrix, which since 2026-09-18 follows
the cut; (c) and (d) may follow it.

- [x] (a) amd64 service block: `usbport services written=16` on XP x64 and
      Vista x64 from the corrected verifier (`xhciVerifyPacketAfterRegistration`
      walks `PVOID`s; design 11 sections 6.3 and 6.5 carry the qualification
      that every earlier amd64 reading was eight). **XP x64 and Vista x64
      done 2026-09-18.**

      *`release` cannot show it.* The count and the `ABI-SUSPECT` lines are
      `XHCI_DBG_VALUE` / `XHCI_DBG_TEXT`, which compile only into `qemu`, and
      the verifier runs in `DriverEntry`, before any extension exists, so its
      result reaches no counter `xhcisnap` could read. Both halves are
      therefore read on `qemu`.

      *XP x64.* `build-driver.cmd qemu -amd64` on the cut's tree (every gate
      green; `built Sep 18 2026 19:11:53`, SHA-256 `150C9F5B...`), staged by
      `make-package.ps1 -Flavor qemu -Arch amd64`, installed on a fresh
      overlay of leg 5's clean copy by the same wizard route. The log:
      `interface version presented=000000C8`, `nt6 services written=0`,
      **`usbport services written=00000010`**, and the 16-pointer dump with
      every slot a full 64-bit pointer (high half `FFFFFADF`) - the first
      three equal, one routine in three slots. No `ABI-SUSPECT` line anywhere
      in the log; registered, No Op self-test completion code 1. Log
      `vm\t2210-xp64-a-debugcon.log`

      *Vista x64, 2026-09-18 night.* The same `qemu -amd64` build on the
      cut's tree (`built Sep 18 2026 21:42:23`, SHA-256 `D6D22BD9...`,
      278,528 B; every gate green; `src\` unchanged since the re-cut),
      staged by `make-package.ps1 -Flavor qemu -Arch amd64`, installed on a
      fresh overlay of leg 6's clean copy by leg 6's route (the boot-manager
      F8, the Windows Security prompt, the Program Compatibility Assistant
      box). The log: `interface version presented=0000012C`, `nt6 services
      written=00000002`, **`usbport services written=00000010`**, and the
      16-pointer dump with every slot a full 64-bit pointer (high half
      `FFFFFA60`) - the first two equal. No `ABI-SUSPECT` line anywhere in
      the log; registered, No Op self-test completion code 1. Log
      `vm\t2210-vista64-a-debugcon.log`. **(a) is done.**
- [x] (b) port suspend and resume, **taken on Vista x86 rather than XP,
      2026-09-18, and passed after one driver fix.** Expected: a device
      usbhub selectively suspends and resumes with no status query between
      the two operations, `C_PORT_SUSPEND` reported once, `RhPortsResumed`
      +1, `RhResumesAbandoned` unchanged, and no second `U0|LWS` write
      (`xhciRhFoldReading`, `src/xhci_rh.c`).

      *Why not XP.* With `USB_MINIPORT_FLAGS_DISABLE_SS` (task 22.11) no
      selective suspend can reach a root-hub port on XP. XP SP3's
      `usbhub.sys` sends SET_FEATURE(PORT_SUSPEND) only from a child's D1,
      D2 or D3, and calls a child's idle callback only from its own idle
      callback, which runs only if usbport accepts the root hub's idle
      request - and the flag makes XP's usbport refuse it (issue 5 section
      5.4). Measured on an XP SP3 guest the same morning: the QEMU mouse
      with HID selective suspend enabled (`SelectiveSuspendEnabled`, which
      XP's `hidclass.sys` reads) raised the Power Management tab and never
      suspended; a Device Manager disable goes out as
      CLEAR_FEATURE(PORT_ENABLE), and Stand By was unavailable on the guest.
      Windows 7's hub parks a child's idle request while the root hub's
      selective-suspend state is the one the flag sets; Vista's does not
      look at it. `legal-provenance.md` section 4 has the three static
      readings. The owner chose Vista on 2026-09-18.

      *First reading, `73fd505` (`qemu`, built 10:11:24), a Vista x86 guest
      from `vista-clean-install`.* The QEMU mouse on hub port 1, with
      `SelectiveSuspendEnabled` set, was idled at once: SET_FEATURE
      (DEVICE_REMOTE_WAKEUP) to the device, then `RH_SetFeaturePortSuspend`
      on port 1 and QEMU's `port_link pls 3`. `RhPortStatusQueries` held at
      328 while it stayed suspended. Unticking "Allow the computer to turn
      off this device" (keyboard only - any pointer movement makes the
      mouse wake itself) sent `RH_ClearFeaturePortSuspend`: the driver wrote
      `PLS = 15` with LWS and **QEMU did not act on it** - no `port_link` in
      its trace, and the timer read PORTSC `0x00000E63`, still U3. The timer
      abandons a port that is not in Resume (a gate the 2026-09-17 audit
      added; before it only a disconnect abandoned), so no U0 write went out
      and no `C_PORT_SUSPEND` was ever derived: `RhResumesAbandoned` 1. Sixty
      seconds later Vista bugchecked **0xFE (8, 6, 1)** - `UsbhSyncResumePort`
      waits 60 000 ms for the port's suspend change and traps rather than
      hang a power IRP.

      *The fix* (owner's decision, 2026-09-18): `xhciRhResumeOwesU0` owes
      the terminating U0 write to a port in U3 as well as Resume, on both
      the timer and the age-retire paths, as Linux's `xhci-hub.c` ends a
      host-initiated USB 2.0 resume with an unconditional U0 write. A port
      found in U0, disabled or resetting is still abandoned. Host vector
      `test_root_hub_resume_write_ignored`, with a model knob for a
      controller that ignores the Resume write; reverting the helper to
      Resume-only fails nine of its checks.

      *Second reading, the fix (`qemu`, built 12:20:51), the same guest.*
      Suspend within seconds of the plug; `RhPortStatusQueries` 133 at
      12:35:35 and at 12:36:55. The untick at 12:39:04:
      `RH_ClearFeaturePortSuspend`, "T(DRSMDN) elapsed, driving hub port to
      U0", the PLC event latching `C_PORT_SUSPEND` (`0x4`), one status query
      (133 to 134), `RH_ClearFeaturePortSuspendChange` once, then
      CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP) to the mouse. QEMU's trace: the
      suspend write (`0x00010e61`, `pls 3`), the ignored Resume write
      (`0x00010fe1`), **one** `U0|LWS` write (`0x00010e01`, `pls 0`, PLC),
      then a PLC acknowledgement with no LWS. At 12:41:05, past the 60 s
      window: `RhPortsSuspended` 1, `RhPortsResumed` 1, `RhResumesCompleted`
      1, `RhResumesAbandoned` 0, `RhAgeRetires` 0, the mouse still
      addressed, no bugcheck. Harness `out\post-release\task22-12b\`
      (git-ignored); logs `vm\t2212-vista-i1-*` and `vm\t2212-vista-i2-*`.

      *Third reading, the `release` flavour from the asset (22.10 leg 8,
      2026-09-18 night), a fresh overlay of the same clean install.* The
      flavour writes no port-`0xE9` trace and no counter is reachable
      without it, so this reading is QEMU's trace, the guest and the clock.
      `SelectiveSuspendEnabled` = `01` set on the mouse's device key
      (`USB\VID_0627&PID_0001\89126-0000:00:03.0-1\Device Parameters`), the
      mouse replugged on hub port 1: the suspend write `0x00010e61`, `pls 3`.
      Then, before the untick, **the device-initiated resume** - the owner's
      pointer crossed the QEMU window and the mouse woke itself, three times:
      each time QEMU raised PLC with the port in Resume, the driver's PLC
      acknowledgement (`0x00400fe1`), **one** `U0|LWS` write (`0x00010e01`,
      `pls 0`, PLC), its acknowledgement (`0x00400e01`), and the mouse idled
      back into U3 with a fresh `0x00010e61`. The untick, keyboard only:
      the Resume write (`0x00010fe1`) with no `port_link`, **one** `U0|LWS`
      write (`0x00010e01`, `pls 0`, PLC), the acknowledgement
      (`0x00400e01`) - the second reading's sequence write for write - and
      no port-1 write after it. Ninety seconds on, past the 60 s window: no
      bugcheck, the mouse still addressed (`info usb`: `Device 0.3, Port 1,
      480 Mb/s`), *HID-compliant mouse* in Device Manager, and the mouse
      moving the guest pointer.

      Not established: the counters on either resume under `release`
      (`RhPortsResumed`, `RhResumesAbandoned`), which this flavour gives no
      route to here - the second reading is where they were read, on
      `qemu`; and a conforming xHC, which enters Resume on the write,
      exercises the U0 write from Resume as before - these readings cover
      the U3 arm. The device-initiated resume, unread at the second
      reading, is now read, on `release` and by QEMU's trace alone.
- [x] (c) recovery under a reset in progress, **taken 2026-09-18 on the
      Windows 2000 SMP guest under TCG, and passed with no finding.**
      Expected: with CNR or HCRST held, `RecoveryLastStep =
      XHCI_INIT_STEP_RESET`, zero operational-register writes, and the next
      poll's retry completing through one HCRST (`XhciRecoverController`,
      `xhciReset`, `XhciMaskInterrupts`).

      *No QEMU controller can hold either bit.* Measured the same afternoon
      on a bare QEMU 11.1 `qemu-xhci` (TCG, no guest) written through the
      gdbstub: a USBCMD write with HCRST runs the reset synchronously and
      USBCMD reads `0` straight after, USBSTS reads `0x1` with CNR never
      set, and a write of CNR to USBSTS is dropped. So the owner chose
      (2026-09-18) to take the clause in two halves: Phase 20's HCE route
      unchanged, which reads the ordinary path through the audit's new
      check and its 1 ms post-HCRST stall; and the refusal by a **doctored
      register read** - a gdbstub breakpoint in `xhci98.sys` at the
      instruction after `XhciRecoverController`'s USBSTS and USBCMD reads
      (image RVA `0x843D`, `cmp edi,0FFFFFFFFh`, with USBSTS in EDI and
      USBCMD in EAX), ORing CNR (`0x800`) or HCRST (`0x2`) into the value
      read, on one attempt. It is not a controller in reset: what it shows
      is what the driver does with such a reading.

      *The build and the guest.* `build-driver.cmd qemu` on the cut's tree
      (`c5df75b`; `src\` unchanged since `26162ae`), built 18:19:15,
      image stamp `6AAD1025`, SHA-256 `22344b2c...` - copied over
      `xhci98.sys` in a throw-away overlay of `vm\win2k-smp.img` (ACPI
      Multiprocessor HAL, `-smp 2`), restarted from the Start menu, and run
      under `-accel tcg` because WHPX does not run on this host; the image
      has booted under TCG before ("Phase 2d" in `build-and-test.md`), and
      nothing here asks it to be the race detector. A QEMU USB mouse on hub
      port 1. HCE by Phase 20's method: interrupter 0's `ERSTBA` (BAR0
      `0xFEBF0000` + `0x1030`) written to `0xFFF00000`, then its high half.

      *Five incidents, read by counters, the port-`0xE9` log and QEMU's
      `usb_xhci_oper_*` trace:*

      | Incident | Injected at the site | Attempts / completions / refusals after | Refused attempt | Retry |
      |---|---|---|---|---|
      | 1 | nothing | 1 / 1 / 0 | - | step 22, one `=== RESET ===` |
      | 2 | nothing (a script fault; the attempt ran unmodified) | 2 / 2 / 0 | - | step 22, one reset |
      | 3 | CNR and HCRST, one attempt | 4 / 3 / 1 | step 9, status `0x1800`, USBCMD `0x3` | step 22, one reset |
      | 4 | CNR alone | 6 / 4 / 2 | step 9, status `0x1800`, USBCMD `0x1` | step 22, one reset |
      | 5 | HCRST alone | 8 / 5 / 3 | step 9, status `0x1000`, USBCMD `0x3` | step 22, one reset |

      On every refused attempt the driver logged "the previous reset is
      still running - refusing before touching a register" with both
      readings, `RecoveryFailuresConsecutive` went to 1 and back to 0 on the
      retry, and the trace shows the refused attempt's USBSTS-then-USBCMD
      read pair followed by **no operational-register write** before the
      retry's own pair; the only writes ahead of it, the latch's INTE and
      IMAN mask, are in incidents 1 and 2 too. Each retry is the ordinary
      sequence: the quiesce's USBCMD write, the halt, the CNR wait's USBSTS
      reads, one `USBCMD = 0x2`, one reset, the run. After the five:
      `FatalStatusDetected` 5, `ResetControllerCalls` 5, `RecoveryAttempts`
      8, `RecoveryCompletions` 5, `RecoveryFailures` 3,
      `RecoveryDeliveriesLost` and `RecoveryCallbacksLate` 0,
      `DevicesAddressed` 6 (the mouse re-addressed each time), the mouse
      still moving the pointer, no bugcheck. The breakpoint is reported
      again on continuing from it, so incident 3's second stop was the same
      execution (EDI still `0x1800`, which QEMU cannot return); incidents 4
      and 5 absorbed that re-report with a no-op. Harness and logs in
      `vm\t2212c\` (git-ignored): `gdbinj.py`, `smp-c.cmd`,
      `debugcon-tcg1.log`, `trace-{h1,h2,cnr,hcrst}.log`.

      Not established: a controller that really holds CNR or HCRST, which
      no VM here can supply; and the 1 ms post-HCRST stall has no
      observable effect on QEMU, whose reset is synchronous - these
      readings show only that the stall broke nothing.
- [ ] (d) isochronous counters on a QEMU audio row: `IsoTailEventsTotal`
      moving with split packets, `UnmatchedEventsTotal` staying at zero,
      `OrphanedGroups` at zero; a group swept by the next group's tail on a
      normal short read is the audit's B10 (QEMU reporting one event per
      TD) and would want a settle like `XhciXferDrainSettled` for an
      all-answered group

## The checkpoint, as the roadmap carried it

Checkpoint, the first half. Every clause, on **each** of the two guests, or
the phase is not closed:

- [ ] the existing package installed on an xHCI-only machine, and what the
      unsigned-driver prompt actually did recorded
- [ ] the driver registered and started, and its No Op self-test passed
- [ ] the root-hub callbacks answered
- [ ] a HID mouse, a mass-storage device and a composite audio device bound
- [ ] the Device Manager disable, enable, remove and rescan sequence survived

Not a checkpoint: a static pass standing in for a guest, or one of the two
guests standing in for the other. No primary target's checkpoint waits on any
of this, and none of it may cost a primary target anything.

**A negative closes this half.** If neither guest will load the driver, or
the driver store will not take the package, that is a complete result: the
tier is not claimed, 22.6 records why, and the cut below goes ahead
regardless, because nothing in it depends on the answer.

Checkpoint, the second half - the cut. All of it, or the phase is not closed:

- [ ] every gate and self-test green on both architectures, and
      `make-release.ps1` exit 0 with `releases\1.1.0.0\` holding four flavour
      directories
- [ ] ~~the post-release matrix on both primary targets no worse than Phase
      20's reports~~ - moved after the cut by the owner on 2026-09-18; it is
      task 22.9 and no longer part of this half
- [ ] the install route read from the published asset on all nine legs -
      the four x86 ones every cut since `1.0.1.0` has taken, the amd64 ones
      on the XP x64, Vista x64 and Windows 7 x64 guests, and the x86 ones on
      the Vista x86 and Windows 7 x86 guests
- [ ] the asset holding exactly what the packager staged - `xhci98.sys` and
      its INF in each of the four flavour directories, the two tools, the
      readmes and the licence texts - and no Microsoft file under any name
- [ ] the prose that no gate reaches bumped: `history.md`'s entry, the
      release notes' opening line, and the release date in the header and
      both INFs

Not a checkpoint: a `qemu`-flavour reading standing in for the published
`release` binary, on either architecture - Phase 21 paid for that rule on the
x64 side and it is why the `release` leg exists at all. Not a checkpoint
either: the acceptance test from the download, or the upload. Both are the
owner's, and the end of this file says so.

Records: `usb-xhci-info/usbport-miniport-abi.md`; `build-and-test.md`;
`usb-xhci-info/win98-wdm.md`; `lessons.md`; `releases/README.md` and
`releases/history.md`; `docs/using/release-acceptance-test.md`;
`docs/contributing/runs/run-22-post-release/` (written by 22.9).
