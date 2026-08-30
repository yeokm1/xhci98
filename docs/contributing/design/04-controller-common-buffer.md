# Controller Common-Buffer Layout - Design

Design doc 04. Settles the Option A DMA-memory model (Phase 3 task 2, consumed
by Phase 4 task 2 and Phase 6). It was written before any controller code, so
that the one number Phase 3 can never take back, the resource size committed in
`DriverEntry`, is the product of an argument rather than a first guess.

Implemented by `src/xhci.h` (constants and region map), `src/xhci_mem.c`
(computation and checks), and `test/test_membuf.c` (864 checks, run by
`test\run-host-tests.cmd`).

## 1. The constraint

`USBPORT_REGISTRATION_PACKET.MiniPortResourcesSize` is filled in `DriverEntry`
and copied into `usbport.sys`'s private interface record at registration. It is
per-driver, not per-device, and it is read again at every controller start
from usbport's copy. The miniport cannot revise it after seeing the hardware:
`StartController` is handed a common buffer that was already allocated at that
size (`docs/usb-xhci-info/usbport-miniport-abi.md` section 5).

Every xHCI quantity that would naturally size that buffer is
controller-specific and only readable inside `StartController`:

| Quantity | Register | Fleet value (measured) |
|---|---|---|
| Max Device Slots | HCSPARAMS1 `7:0` | 64 |
| Max Scratchpad Buffers | HCSPARAMS2 `31:27`/`25:21` | 34 |
| Context size (CSZ) | HCCPARAMS1 bit 2 | 0 -> 32 bytes |
| xHC page size | PAGESIZE | `0x00000001` -> 4096 |

So the layout has to be a worst-case reservation with an explicit refusal
path, not a computation. That is the whole of this design.

Fleet values are from the Phase 0 qualifier's bare-metal runs
(`xhciqual/results/e460-2026-07-25/PROBE.LOG` and
`xhciqual/results/p14s-gen1-2026-07-25/PROBE.LOG`): ThinkPad E460
(`8086:9D2F`, Skylake) and ThinkPad P14s Gen 1 (`8086:02ED`, Comet Lake) report
identical values four PCH generations apart.

## 2. What usbport provides, read out of both shipping binaries

`docs/contributing/roadmap.md` Phase 3 task 2 requires the DMA path to be
confirmed in the shipping binaries, not inferred from ReactOS. Done with
`dumpbin /disasm` on NUSB `5.00.2195.5652` and Windows 2000 SP4
`5.00.2195.6681`; the extracted routines are kept as
`tools/{nusb,win2ksp4}-extracted/usbport-commonbuffer-disasm.txt`. The findings
are identical in both builds (the two differ only in FDO-extension field
offsets).

The DMA adapter is constrained to 32 bits. `USBPORT_StartDevice` zeroes a
40-byte `DEVICE_DESCRIPTION` local and fills exactly six fields before
`IoGetDmaAdapter`:

| Offset | Field | Value |
|---|---|---|
| 0x00 | `Version` | 0 (`DEVICE_DESCRIPTION_VERSION`) |
| 0x04 | `Master` | 1 |
| 0x05 | `ScatterGather` | 1 |
| 0x08 | `Dma32BitAddresses` | 1 |
| 0x0B | `Dma64BitAddresses` | never written, stays 0 |
| 0x14 | `InterfaceType` | 5 (`PCIBus`) |
| 0x18 | `DmaWidth` | 2 (`Width32Bits`) |
| 0x1C | `DmaSpeed` | 0 (`Compatible`) |
| 0x20 | `MaximumLength` | `0xFFFFFFFF` |

That discharges the primary requirement: every buffer this adapter returns,
and every scatter/gather element it maps, is below 4 GB. It also matches
ReactOS `usbport/pnp.c:555-562` field for field, which raises confidence in the
rest of the ReactOS transcription.

usbport truncates the physical address itself. `USBPORT_AllocateCommonBuffer`
(NUSB VA `0002E300`, SP4 VA `0002EBDA`) takes the `PHYSICAL_ADDRESS` back from
`AllocateCommonBuffer` and uses only `LowPart` to form the address it
publishes; `HighPart` is stored in its private header solely so
`FreeCommonBuffer` can be called later. Combined with the adapter constraint
above, the miniport may treat `Resources->StartPA` as a plain `ULONG` with an
implicit high DWORD of zero, which is what the "No 64-bit DMA" rule in
`AGENTS.md` ("Language and arithmetic") assumes.

StartVA and StartPA are both page-aligned. Both are masked with
`~(PAGE_SIZE - 1)` (`mov ecx,0FFFFF000h; and ebx,ecx; and edx,ecx`) before
being published. Section 4 rests on this: because the virtual and physical
bases share the same page alignment, a single table of byte offsets satisfies
the spec's alignment and no-cross-boundary rules in both address spaces at
once. `XhciCheckResourceBase()` verifies it at start rather than trusting it.
A merely 64-byte-aligned `StartPA` would satisfy every xHCI alignment rule and
still break the device-context page rule.

The allocation costs `ROUND_TO_PAGES(size + 48)`. usbport appends its own
48-byte header to the miniport's request, rounds the total up to a page, and
places the header at the end so the miniport's block starts at the page
boundary. usbport corroborates this itself: its second, private common-buffer
request in the same function is `0xFD0` = 4048 bytes, one page once the
48-byte header is added.

usbport zeroes the block (`rep stos` over `BufferLength + LengthPadded`)
before `StartController` is called, so the miniport need not zero what it does
not write. It is still zeroed explicitly on the re-init paths, where the buffer
is not freshly allocated.

`MiniPortFlags` bit 0x100 (`NO_DMA`) would disable all of this. If it is set,
`StartDevice` skips `IoGetDmaAdapter` and overwrites its own copy of
`MiniPortResourcesSize` with zero: no adapter, no common buffer, no
diagnostic. `xhci98.sys` uses `MiniPortFlags = 0x95`, which does not include
it. This is recorded so a future flag edit cannot quietly remove the buffer.

With `HeaderBufferSize = 0`, usbport supplies a per-endpoint common buffer for
no endpoint at all. This was measured on a target rather than inferred (task
7b-A.1.0): the probe's `XHCI_PROBE_EP_BUFFER` shape bit (`0x800`) was never
set across an entire run and `NO_BUFFER` always was, so `xhciDevBufferUsable`
returns 1 on its `BufferLength == 0` first line every time. The refusal path
for an unusable endpoint buffer is unreachable while this driver asks for `0`.

## 3. The policy choices

### 3.1 `MaxSlotsEn`: 32, by clamping

`CONFIG.MaxSlotsEn` is written to the controller and is genuinely ours to
choose; using fewer slots than the hardware offers is legal and normal. Each
enabled slot costs a persistent Device Context (2048 B worst case) plus a
persistent EP0 transfer ring (1024 B, section 3.4), 3 KB per slot, so enabling
all 64 offered slots would cost 96 KB for capacity nobody will use.

Chosen: `min(HCSPARAMS1.MaxSlots, 32)`. A slot is consumed per device, hubs
included, so 32 covers a root hub's worth of ports plus several tiers of
external hubs, well past what a Win98-era machine will drive and past the 12
managed USB2 ports the fleet controllers expose. Hardware offering fewer is
used as-is; hardware offering more is clamped, never refused.

### 3.2 Scratchpad: cap 64, refuse above

Scratchpad demand is the opposite kind of number. The controller dictates it,
and a controller whose scratchpad array is short of `MaxScratchpadBufs` entries
simply will not run. The only choice is where to stop.

The spec maximum is 1023 buffers = 4 MB of pages, which is not allocatable as
one contiguous block on either target. The measured fleet value is 34 on both
Intel generations.

Chosen: 64. The smallest power of two above the only measured value, with 88%
headroom, at a cost of 256 KB. Above 64, `StartController` fails with
`MP_STATUS_NO_RESOURCES`. It never builds a partial array, which would be a
controller that silently DMAs into memory the driver does not own. The required
count is readable whatever the flavour: it is written into the log ring as
`hc.scratchpad` at capability decode, before the carve, and `XHCISNAP` reads
it back off the machine. Phase 0's qualifier already reports `scratch=N` in
its fact sheet, so a machine can be screened before it is ever booted with the
driver.

### 3.3 xHC page size: select 4096 when supported

`PAGESIZE` is a capability bitmap: bit n advertises support for
`2^(n+12)` bytes. Every reservation below selects 4096, and the fleet reports
`0x00000001`. A controller is accepted whenever bit 0 is set, including a
multi-bit value such as `0x00000003`; a controller without bit 0 is refused
with its own diagnostic rather than mis-served. `XhciComputeLayout()` takes the
raw bitmap so the register cannot be mistaken for a decoded byte count.

### 3.4 What must be persistent, and why it cannot live in an endpoint buffer

usbport gives each endpoint its own common buffer, sized by the miniport's
`QueryEndpointRequirements.HeaderBufferSize`. That buffer's lifetime is not
the endpoint's lifetime in any sense the miniport can rely on: usbport frees
it on a reopen as well as at delete, and in neither case does the miniport get
a callback at the free or any way to hold it off. So it is the wrong home for
anything the slot owns, and (section 3.6) for transfer rings as well.

usbport destroys and re-creates the endpoint buffer during enumeration.
`USBPORT_ReopenPipe` [endpoint.c:1194-1306] sets the endpoint state to REMOVE,
waits 2 ms, and then:

1. `RtlZeroMemory(Endpoint + 1, MiniPortEndpointSize)`: the miniport's
   endpoint extension is wiped, so nothing cached there survives either;
2. `USBPORT_FreeCommonBuffer(Endpoint->HeaderBuffer)`: the old buffer goes
   back to the DMA pool;
3. `QueryEndpointRequirements` again, then `USBPORT_AllocateCommonBuffer` for a
   new buffer. The binaries show a free followed by a fresh allocation
   request; they do not guarantee the allocator hands back a different
   address, so the rule is "it may move", and nothing may cache it. Re-read
   `BufferVA`/`BufferPA`/`BufferLength` from the properties at every open.

usbport does this to EP0 in the middle of every device's enumeration
[device.c:1362-1368]: EP0 is opened at address 0, SET_ADDRESS is submitted as a
plain control transfer, then EP0 is closed and reopened at the new address.

Both shipping builds were disassembled for this (batch 6-0). The reopen
contains no `CloseEndpoint` call at all, and neither build invokes that slot
anywhere. Nor does the endpoint delete path, which frees the endpoint common
buffer and `ExFreePool`s the endpoint with no callback at the reclamation. The
miniport does get an earlier `SetEndpointState(REMOVE)` in both cases, so the
precise statement is "no notice at the free and no way to hold it off", not
"told nothing". Anything the xHC still points at when the free happens is a
live DMA pointer into freed pool. Evidence:
`docs/usb-xhci-info/usbport-miniport-abi.md` section 4.

The slot must survive the reopen, so:

- Device Contexts are per-slot and persistent. `DCBAA[SlotID]` points at one
  for the slot's whole life and the xHC writes into it asynchronously. Putting
  it in an endpoint buffer would hand the hardware a pointer to memory usbport
  had already freed.
- EP0 transfer rings are per-slot and persistent. The EP0 ring's physical
  address is baked into the Endpoint Context's TR Dequeue Pointer. If the ring
  lived in the endpoint buffer, every reopen would need a Stop Endpoint plus
  Set TR Dequeue Pointer command sequence just to re-point it, because Evaluate
  Context cannot change that field. Making the ring persistent removes the
  whole sequence: reopen becomes an Evaluate Context for the corrected MPS0.
- Non-EP0 transfer rings were at first left in the endpoint buffer, on the
  assumption that their close/reopen came with a Configure Endpoint that
  re-establishes the dequeue pointer. That assumed a `CloseEndpoint` callback
  to hang the command off, and there is none. Worse, a Configure Endpoint after
  the fact cannot help, because the hazard is the window in which the xHC's
  Endpoint Context still holds a TR Dequeue Pointer into a buffer usbport has
  already returned to the pool. Section 3.6 is where they live now.

A corollary for Phase 6, recorded here because this is where the evidence
sits: since the endpoint extension is zeroed on reopen, the usbport-address ->
Slot ID map cannot live there. It belongs in the miniport device extension and
must be keyed by port/topology, since the reopened EP0 arrives with an address
the miniport has never seen.

Batch 6-B confirmed the EP0 rule from the other direction, so here it is as
a positive statement. The reopen path does not merely avoid needing a Set TR
Dequeue Pointer; it is forbidden from reinitialising the ring at all. The xHC's
Endpoint Context still holds a TR Dequeue Pointer into it across the reopen,
and the Evaluate Context that carries the corrected MPS0 evaluates Max Packet
Size and Max Exit Latency only (spec 6.2.3.2), so a ring restarted at its base
would leave the software and hardware pointers on different laps of the same
memory with nothing to reconcile them.

`src/xhci_slot.c` therefore leaves
`Ep0Ring` untouched on a reopen, and `test_init`'s MPS-correction vector
asserts the enqueue position, the dequeue pointer and the cycle state are all
unchanged, after first putting real traffic on the ring, because a fresh ring
and an untouched one are indistinguishable at index 0. A mutation that added
`XhciRingInit` to the reopen path failed zero checks until that traffic was
there.

The same batch read the request side of this section back into the code.
Because a per-endpoint buffer is freed and reallocated on every reopen and
EP0's ring is here instead, `QueryEndpointRequirements` answers
`HeaderBufferSize = 0`: this driver asks usbport for no per-endpoint common
buffer at all.

### 3.5 Input Contexts: one, shared, not per-slot

Input Contexts are consumed by commands (Address Device, Configure Endpoint,
Evaluate Context) and are dead once the command retires. Phase 4's command
engine allows one outstanding command, so one Input Context suffices; a
per-slot array would reserve 66 KB for a structure that is only ever in use one
at a time.

The ownership rule that makes this safe: the single Input Context belongs to
the outstanding command and is not reused until that command is retired,
either completed via a Command Completion Event or with the command ring
confirmed stopped after a CA abort (Phase 4 task 7). It is not released on
timeout alone, because a controller that has not acknowledged the abort may
still be reading it. Phase 4 must not weaken this without re-sizing the region.

### 3.6 Where a non-EP0 transfer ring lives

Resolved in batch 7a-A: non-EP0 transfer rings live in the controller common
buffer, carved from a fixed pool rather than one per possible endpoint.

Three facts from batch 6-0 rule out the endpoint buffer:

1. The miniport's only notice before an endpoint buffer is freed is
   `SetEndpointState(REMOVE)`, which runs at DISPATCH_LEVEL under a usbport spin
   lock and therefore cannot wait for a Stop Endpoint command to complete.
2. `USBPORT_ReopenPipe` frees 2 ms after that notice. The delete path frees on a
   later reclaim sweep with no guaranteed interval and no gate the miniport
   participates in.
3. There is no `CloseEndpoint` callback, in either build, at all.

So a ring in the endpoint buffer can be handed back to the DMA pool while the
xHC's Endpoint Context still points at it. An earlier draft framed the choice
as a price comparison and wanted the delete-sweep interval measured in the
Phase 7a VM runs before deciding. That framing was wrong. The endpoint-buffer
option is not cheaper but riskier; it is unsound at any price. The failure
mode is the xHC holding a TR Dequeue Pointer into pool memory usbport has
already freed, which is a DMA write into whatever was allocated there next. No
measured interval makes that safe, because the sweep's schedule is not a
contract. It is an observation of one build on one machine on one day, and
nothing in either binary promises it again. A measurement could only ever say
how often we would lose.

The interval measurement is therefore not part of this decision. It is still
worth taking in the 7a-V runs as a characterisation of usbport's behaviour,
and task 7a-B.1 keeps it for that reason, but it gates nothing and must not be
cited as the justification for a placement.

#### The pool

The alternative, one ring per possible endpoint, does not fit. The
architectural maximum is 32 slots x 30 non-default endpoints = 960 rings,
which at the EP0 stride of 1024 bytes is 983,040 bytes, larger than the entire
buffer, for a machine that will realistically open a handful. Section 2
already carries declared-policy limits with an explicit refusal above them
(`XHCI_MAX_SLOTS`, `XHCI_MAX_SCRATCHPAD`), and this is the same shape:

- 32 rings, 1024-byte stride, 32,768 bytes: one new region, sized to equal the
  EP0 region.
- A per-device cap of 4 open non-default endpoints, which bounds how fast one
  device can consume the pool.
- Exhaustion refuses (`MP_STATUS_NO_RESOURCES` from `OpenEndpoint`) and is
  counted. usbport turns that into a pipe-open failure that usbhub degrades on,
  which is the same contract the slot limit already relies on.

The pool size alone does not guarantee every slot a first endpoint. With a
per-device cap of 4, eight devices can hold all 32 rings, and the ninth is
refused its first endpoint. Sizing cannot fix it either: the worst arbitrary
distribution with 32 slots at cap 4 needs 31x4 + 1 = 125 rings, four times the
whole region.

Starving a first endpoint is a much worse failure than refusing a fifth (a
keyboard that cannot open its interrupt endpoint is a dead device), so the
pool buys what protection it can with an admission rule rather than with
bytes:

- A device's first pool ring is granted whenever any ring is free.
- A second or later ring is granted only if doing so would leave at least one
  free ring for every device that is live now and holds none
  (`free - 1 >= ringless`).

What that delivers: no later endpoint ever displaces a device
that is already live, because at the moment a second-or-later ring is granted,
every live ringless device still has one available. What it does not deliver
is a ring for a device created afterwards. Devices appear asynchronously,
`ringless` grows when they do, and no rule applied at acquire time can reserve
against a device that does not exist yet.

That gap cannot be closed at this pool size, and the arithmetic is worth
writing down so nobody tries. To protect every slot that could ever become
live, a later-ring grant would need `free - 1 >= XHCI_MAX_SLOTS - D`, where `D`
is the number of devices already holding a ring. Since `free = POOL - InUse`
and `InUse >= D`, with `POOL == XHCI_MAX_SLOTS` that reduces to
`D >= InUse + 1`, which is impossible, because a device holding at least one
ring contributes at least one to `InUse`. With a pool equal to the slot count,
"every future device gets a first ring" and "any device gets a second" are
mutually exclusive. Buying the stronger property means growing the region, and
125 rings is what the arbitrary worst case costs. The cap remains as a
fairness bound, not as the guarantee's mechanism.

Both ends of the rule are wired (task 7a-A.1): `XhciSlotOpenEndpoint` acquires
a ring for every non-default endpoint it accepts, `XhciInitController` calls
`XhciPoolInit` beside `XhciSlotInit`, and the teardown path returns the rings.
A constant with nothing reading it is not a policy, and neither is a function
with no production caller; both were true of the first draft.

`ringless` is counted by the device layer, not by the allocator, because
"could still ask for an endpoint" is a lifecycle question the pool cannot see.
The set it counts is the set the endpoint open will serve; both ask
`xhciDevMayOpenEndpoint`. That shared predicate matters: a review found the two
disagreeing about a disowned record, which reserved a ring nothing could ever
spend. Counting a record the open would refuse is an availability bug;
refusing a record the count reserved for is a starvation one.

#### When a pool entry may be handed back

Two lifecycle rules came out of the integration. They are here rather than in
the code alone because the failure they prevent, the xHC holding a TR Dequeue
Pointer into memory another endpoint now owns, is the one this whole section
chose the controller block to avoid, and rediscovering it from a corrupted
guest would be expensive.

- A ring is not released at `SetEndpointState(REMOVE)`. REMOVE precedes both
  an endpoint delete and a `USBPORT_ReopenPipe`, is indistinguishable between
  them (batch 6-0), and nothing has stopped the endpoint; proving quiescence
  needs the Stop Endpoint machine task 7a-B.1 owns. So the record and its ring
  survive a REMOVE, and a reopen at the same DCI rebinds to the very same ring,
  which is also what keeps the xHC's dequeue pointer meaningful. The cost is
  one pool entry per opened-and-abandoned endpoint until the device goes,
  bounded by the per-device cap.
- A ring goes back to the free list only where the slot has been shown to have
  gone: a completed Disable Slot whose completion code proves it (Success or
  Slot Not Enabled; Table 6-90 makes Incompatible Device Error returnable by
  any command and names Disable Slot as its recovery, so other codes prove
  nothing), a halted controller, or a record that never held a Slot ID.

  Everywhere else the whole record is abandoned rather than released: it stays
  GONE with its Slot ID, its DCBAA entry, its rings and its queued transfers
  exactly as the hardware may still be reading them, and is counted in
  `DevicesAbandoned`, which is what `XhciSlotInvalidateAll` already does when
  it cannot prove the controller let go.

  `XhciPoolInit` reclaims the rings at
  the next start, and it only runs after HCRST, where "all of the Operational
  and Runtime Registers shall be at their default values" (4.23.1, p.312). The
  per-slot EP0 rings need no equivalent: they are indexed by Slot ID, and a
  Slot ID abandoned with its record is never handed out again.

  A draft between the two released the record and marked only the rings with
  a sentinel "owned by nobody" value. That is the wrong cut: the same missing
  evidence that says a ring may not be recycled says the record's transfers
  may not be completed, and completing them hands their mapped pages back.
  Withholding the ring while releasing the pages protects the smaller half of
  the same hazard.

  Neither arrangement closes that hazard on its own, and this section must not
  be read as claiming one does. usbport reclaims a deleted device's transfers
  independently of the miniport, so withholding a completion cannot keep a
  mapping alive indefinitely; what ends it is Stop Endpoint, which is task
  7a-B.1's. The four hazards that batch owns are listed under it in
  `docs/contributing/roadmap.md`. What the rule here does buy is that this
  driver never actively hands memory back early, and never hands a ring to a
  second endpoint.

#### Stride and size

The 1024-byte stride is inherited rather than re-argued: section 4's EP0 note
already establishes that a 1024-byte object at a 1024-aligned offset cannot
cross a 64 KB boundary, and keeping the two regions the same shape means one
rule covers both. 64 TRBs gives a capacity of 62 (`TRBs - 2`), which covers a
page-granular bulk transfer at the `0x10000` cap in Phase 8 with room to spare.
How deep usbport queues an interrupt endpoint is not derived anywhere, so 62
is chosen as "the same as EP0, which has been sufficient" and not as a bound
anything has established. If a ring-full refusal ever shows up in the 7a-V
counters, that is the number to revisit and the derivation to do.

`MiniPortResourcesSize` therefore becomes 409,600 bytes (400 KiB), up from
376,832: a 2.25x multiple of the shipping EHCI miniport's 182,272 rather than
2.06x. Section 5's lever list still applies; levers 1 and 2 together now reach
256 KiB rather than 224 KiB.

What this costs elsewhere: it does not remove the need for task 7a-B.1's
asynchronous Stop Endpoint machine. A ring that outlives its endpoint is safe
from reuse-after-free, but the xHC must still be off it before the ring is
handed to the next endpoint that allocates from the pool. The difference is
that this is now a race the miniport owns both ends of, since it chooses when
to recycle a pool entry, instead of one usbport can lose on its own schedule.
That is the whole gain.

## 4. The layout

All eight regions start on an xHC page boundary at a page-aligned base, so
each object's offset carries its alignment and its no-cross-boundary property
into physical space unchanged (section 2).

| Offset | Size | Contents |
|---|---|---|
| `0x00000` | 4096 | DCBAA at `+0x000` (2048 reserved, 264 used at 32 slots); Scratchpad Buffer Array at `+0x800` (2048 reserved, 512 used at 64 buffers) |
| `0x01000` | 4096 | Command Ring at `+0x000` (64 TRBs = 1024 B); ERST at `+0x400` (64 reserved, one 16-byte entry) |
| `0x02000` | 4096 | Event Ring segment 0 (256 TRBs = 4096 B) |
| `0x03000` | 4096 | Input Context (2112 B worst case at CSZ = 1) |
| `0x04000` | 65536 | 32 Device Contexts, stride 2048 |
| `0x14000` | 32768 | 32 EP0 transfer rings, stride 1024 (64 TRBs) |
| `0x1C000` | 32768 | 32 pooled non-EP0 transfer rings, stride 1024 (64 TRBs) |
| `0x24000` | 262144 | 64 scratchpad pages, stride 4096 |
| `0x64000` | | total = `MiniPortResourcesSize` = 409,600 B (400 KiB) |

The pooled-ring region is a declared policy limit in the sense section 2
means: 32 rings with a per-device cap of 4, an explicit refusal above them,
and no way to grow the number at runtime because `MiniPortResourcesSize` is
committed in `DriverEntry` before a register can be read. Section 3.6 says why
the pool is sized to match the EP0 region rather than the architectural
maximum of 960.

Why each rule holds (`docs/usb-xhci-info/xhci-data-structures.md` section 1, Table 6-1):

- DCBAA: 64 B aligned at a page boundary; 264 B cannot leave the page; well
  under the 2048 B maximum.
- Scratchpad Buffer Array: 64 B aligned at `+0x800`; 512 B ends at `+0xA00`,
  inside the page.
- Command Ring: 64 B aligned at a page boundary; 1024 B, so 64 KB-safe.
- ERST: 64 B aligned; no boundary rule applies.
- Event Ring: 64 B aligned at a page boundary; 4096 B ends exactly at the
  page, so 64 KB-safe.
- Input Context: 64 B aligned at a page boundary; 2112 B stays inside.
- Device Contexts: stride 2048 from a page-aligned base, so each starts at
  page offset 0 or 2048 and 2048 B never crosses the page.
- EP0 rings: stride 1024 from a 1024-aligned base; a 1024 B object at a
  1024-aligned offset cannot cross 64 KB. The compile-time policy keeps this
  stride at or below one page and uses a power-of-two TRB count: usbport
  guarantees only 4096-byte base alignment, so a larger ring could cross a
  64 KB physical boundary even when its relative offset looks safe. Supporting
  one would require a 64 KB-aligned region or a check using `StartPA + offset`.
- Scratchpad pages: page-aligned by construction.

`XhciComputeLayout()` re-derives and re-checks all of this at every
`StartController`, and `test/test_membuf.c` restates the Table 6-1 rules by
hand and applies them to every slot and every scratchpad page across the CSZ =
0, CSZ = 1, minimal, and QEMU-shaped cases.

The CSZ slack is intended. At CSZ = 0 a Device Context is 1024 B and an
Input Context 1056 B, so roughly half of those two regions goes unused on the
measured fleet. Reserving the CSZ = 1 worst case is the price of committing the
size before the register can be read; a runtime-variable stride would save
32 KB and buy a class of bug that only appears on 64-byte-context silicon.

## 5. What Win98 and Win2000 are asked to allocate

`MiniPortResourcesSize` = 409,600 bytes. Applying usbport's own rule from
section 2:

```
ROUND_TO_PAGES(409600 + 48) = 413,696 bytes = 101 pages = 404 KB
```

physically contiguous, below 4 GB, from `AllocateCommonBuffer` on a
`Width32Bits`/`Dma32BitAddresses` PCI adapter, identically on both targets,
since the allocation code is instruction-for-instruction the same in the two
builds.

Is that feasible? The precedent is the shipping `usbehci.sys` on these exact
stacks, whose `MiniPortResourcesSize` is `0x2C800` = 182,272 bytes
(`docs/usb-xhci-info/usbport-miniport-interface.md`, "Ground truth from the
shipping miniports"). This design asks for 2.25x that, of the same allocator,
on the same code path, at the same point in device start, which is early boot,
when physical memory is least fragmented. That is a real increase, and the
thing that settles it is an observation on a target: the allocation either
succeeds at start-device time or `StartController` is never reached.

The observation that exists is at the old size. Phase 3 tasks 8 and 9 watched
usbport grant 376,832 bytes on both targets, which is evidence about the
allocator at that size and not a licence for a larger one. The 32 KB increase
is small against the fragmentation risk the original figure already carried,
but it has not been observed on a target; the 7a-V runs owe that observation
(section 7, open item 1). The allocation is the first thing that fails, and it
fails before `StartController`.

The odd 101st page is the 48-byte header's rounding. Sizing the request to
`k * 4096 - 48` would reclaim it, at the cost of coupling the layout to a
usbport internal that is not part of the miniport ABI. 4 KB out of 404 KB is
not worth that coupling, so the request stays a clean page multiple.

If the allocation fails on either target, these are the levers in order,
cheapest first. Each is a one-line constant change that the host tests
re-verify:

1. `XHCI_MAX_SCRATCHPAD` 64 -> 40 (saves 96 KB; still 6 buffers above the
   measured fleet value, and the refusal path already exists for the rest).
2. `XHCI_MAX_SLOTS` 32 -> 16 (saves 48 KB; still comfortably above a realistic
   device count). This lever has a second effect: section 3.6 sizes the pool
   to `XHCI_MAX_SLOTS`, so halving the slots allows halving
   `XHCI_MAX_POOL_RINGS` with it for a further 16 KB. Leaving the pool at 32
   costs the bytes and breaks nothing; the two constants are not required to
   match, only `POOL >= SLOTS`.
2a. `XHCI_MAX_POOL_RINGS` 32 -> 16 on its own (saves 16 KB). This is the one
   lever that changes behaviour rather than only capacity. There was never an
   "every slot gets one" guarantee to trade away (section 3.6 shows it is
   unreachable at `POOL == SLOTS` in the first place); what halving the pool
   does is make the admission rule bind sooner, so a device is refused its
   first endpoint after four devices at the cap rather than eight. Cheaper in
   bytes than lever 2 and worse in behaviour, so it is not lever 1.
3. Move the scratchpad pages out of the fixed buffer entirely, allocating them
   individually. This is the only lever that changes the architecture rather
   than a number: it reintroduces private driver allocation, which `AGENTS.md`
   avoids under Option A. So it is last, and it is the point at which the
   Option B question would be reopened.

Levers 1 and 2 together bring `MiniPortResourcesSize` to 256 KiB (262,144
bytes) with the pool left at 32, or 240 KiB (245,760 bytes) if the pool is
halved alongside the slots as lever 2 suggests: about 1.44x and 1.35x the
shipping EHCI miniport's 182,272-byte request respectively. Without the pool
region the same two levers would reach 224 KiB (229,376 bytes), or 1.26x. None
of these is below EHCI's.

Reaching a smaller-than-EHCI request while keeping the fixed-buffer design
would need tighter limits, and the pool has put that out of reach for any
setting that keeps it. Without the pool, exactly 34 scratchpads and 8 slots
produce 176 KiB (180,224 bytes), 2,048 below EHCI's, though usbport rounds
both to the same 180 KiB allocation after its header. The same limits with a
pool of 8 come to 188,416 bytes, above EHCI's. Going under now means dropping
the pool region, which is lever 3's kind of change rather than a number: it
puts non-EP0 rings back in endpoint memory, which section 3.6 rejected on
soundness and not on price. These comparisons establish reduction options, not
feasibility; Phase 3 tasks 8 and 9 observed the allocation at the pre-pool
size on both target stacks, and the 7a-V runs owe the observation at this one.

## 6. Cacheability

Ring memory must be physically contiguous and DMA-accessible. It is not
non-cacheable, and under Option A that is not the miniport's decision: usbport
calls `AllocateCommonBuffer(..., CacheEnabled = TRUE)` (`push 1` in both
binaries), so the block is mapped cached.

This is correct for x86: PCI DMA is cache-coherent there, and cached common
buffers are what every NT-era HCD miniport has always used. The consequence for
this driver is that no ordering can be assumed from an uncached mapping.
Ordering rules stand on their own:

- Every TRB field is written before the TRB's Cycle Bit is published, as an
  explicit final step in the encode layer.
- All hardware-visible structures are accessed through `volatile` so MSVC 6.0
  cannot sink or reorder the stores.
- Register writes go through the DDK's `WRITE_REGISTER_*` accessors, never
  through a plain pointer store.

## 7. Open items

1. The allocation itself. Phase 3 tasks 8/9 asked both targets for 372 KB and
   both granted it, so that item is discharged at that size. Batch 7a-A raised
   the request to 404 KB (section 3.6), and no target has been asked for that
   yet; the 7a-V runs owe the observation. It is the first thing that fails
   and it fails before `StartController`, so a silent success is not what to
   look for. Record the granted size on both.
2. `Resources->StartVA`/`StartPA` at runtime. `XhciCheckResourceBase()`
   asserts page alignment on both. The Phase 3 spike logs the raw
   `USBPORT_RESOURCES` block anyway (interface doc section 9, open item 4);
   this design gives that log a specific thing to confirm.
3. `QueryEndpointRequirements.HeaderBufferSize` for non-EP0 endpoints is a
   Phase 6 number, not fixed here. It is bounded by the same 64 KB
   transfer-ring segment rule and does not come out of this buffer. Batch 6-B
   answered `0` (section 3.4), so `OpenEndpoint`'s 64 KB no-cross check on the
   supplied buffer is written but reachable only from a build that requests one.
4. Scratchpad on the untested fleet half. The B650M (AMD) machine never went
   through Phase 0 and has since left the project, so this gap is permanent
   rather than pending. If a controller reports more than 64 scratchpad
   buffers, this design refuses it loudly, which is the intended behaviour,
   but the cap would then deserve revisiting.
