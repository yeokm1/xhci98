# xHCI Data Structures and Register Reference (bit-exact)

This is the bit-level companion to `docs/usb-xhci-info/xhci-programming.md`.
That doc explains sequences and concepts; this one gives the exact bit
positions, type codes, and layouts needed to write `src/xhci.h` without
re-deriving them from the 600-page spec PDF. Every table cites its spec section
so it can be re-verified against `docs/references/xHCI__Rev1.2c.pdf`
(revision 1.2c; every `p.N` below is the page that copy prints).

All tables in this file were transcribed from that local spec PDF (register
figures and field tables checked page by page), not from memory. If this file
and the PDF ever disagree, the PDF wins: fix this file.

Conventions:

- `DW0..DW3` are the four little-endian 32-bit words of a 16-byte TRB or a context row.
- Bit ranges are `high:low`, inclusive.
- "RW1C" = write 1 to clear; writing 0 has no effect.
- "RsvdZ" = reserved, software must write 0. "RsvdP" = reserved, software must preserve (read-modify-write).
- The driver runs 32-bit only: every "Hi" DWORD of a 64-bit pointer field is written as 0, but it must still be written.

---

## 1. Structure Size / Alignment / Boundary Requirements (spec Table 6-1)

`HalAllocateCommonBuffer` guarantees page alignment only for allocations of a
page or more. The simplest safe policy: allocate whole pages and carve them,
keeping the table below satisfied. "Boundary" means the structure must not
span a boundary of that size.

| Structure | Max size | Alignment | Must not cross | Spec |
|---|---|---|---|---|
| DCBAA (Device Context Base Address Array) | 2048 B | 64 B | PAGESIZE | 6.1 |
| Device Context | 2048 B | 64 B | PAGESIZE | 6.2.1 |
| Input Context (incl. Input Control Context) | - | 64 B | PAGESIZE | 6.2.5 |
| Slot Context / Endpoint Context (individually) | 64 B | 32 B | PAGESIZE | 6.2.2/6.2.3 |
| Transfer Ring segment | 64 KB | 16 B | 64 KB | 4.9.2 |
| Command Ring segment | 64 KB | 64 B | 64 KB | 4.9.3 |
| Event Ring segment | 64 KB | 64 B | 64 KB | 4.9.4 |
| Event Ring Segment Table (ERST) | 512 KB | 64 B | none | 6.5 |
| Scratchpad Buffer Array | `MaxScratchpadBuffers * 8` bytes (max 8184 B) | 64 B | PAGESIZE | 6.6 |
| Scratchpad Buffer pages | PAGESIZE | PAGESIZE | PAGESIZE | 4.20 |

Additional rules:

- No structure <= 64 KB may span a 64 KB boundary; none <= PAGESIZE may span a PAGESIZE boundary (spec 6, intro).
- The TR Dequeue Pointer written into an Endpoint Context must be 16-byte aligned (Table 6-10).
- Input Context pointers in command TRBs must be 16-byte aligned (Table 6-61).
- All structures are little-endian; software must preserve fields marked RsvdO/RO on writes.

## 2. Capability Registers (BAR0 + 0, spec 5.3)

| Offset | Size | Register | Field layout |
|---|---|---|---|
| 0x00 | 1 | CAPLENGTH | Byte offset from BAR0 to Operational registers |
| 0x02 | 2 | HCIVERSION | BCD: 0x0100 = 1.0, 0x0110 = 1.1, 0x0120 = 1.2 |
| 0x04 | 4 | HCSPARAMS1 | MaxSlots `7:0`, MaxIntrs `18:8`, MaxPorts `31:24` |
| 0x08 | 4 | HCSPARAMS2 | IST `3:0`, ERST Max `7:4` (as 2^n entries), Max Scratchpad Bufs Hi `25:21`, SPR `26`, Max Scratchpad Bufs Lo `31:27` |
| 0x0C | 4 | HCSPARAMS3 | U1 Device Exit Latency `7:0`, U2 `31:16` (SS only - ignore) |
| 0x10 | 4 | HCCPARAMS1 | AC64 `0`, BNC `1`, CSZ `2`, PPC `3`, PIND `4`, LHRC `5`, LTC `6`, NSS `7`, PAE `8`, SPC `9`, SEC `10`, CFC `11`, MaxPSASize `15:12`, xECP `31:16` |
| 0x14 | 4 | DBOFF | Doorbell array offset from BAR0, bits `31:2` (low 2 bits RsvdZ - mask them) |
| 0x18 | 4 | RTSOFF | Runtime registers offset from BAR0, bits `31:5` (low 5 bits RsvdZ - mask them) |
| 0x1C | 4 | HCCPARAMS2 | U3C `0`, CMC `1`, FSC `2`, and higher bits this driver does not use (spec 5.3.9, Table 5-16 p.355-356; only bits 0-2 are transcribed here). Revision 1.2c defined two more, DIC `11` and E2V2C `12` (both RO, eUSB2 features); nothing here reads them |

Notes:

- Max Scratchpad Buffers = `(Hi << 5) | Lo`; the `25:21` field is the high 5 bits. The maximum encoded count is 1023, so the scratchpad buffer array can be larger than one page. Getting this backwards or allocating only 31 entries makes the controller silently refuse to run when the count is nonzero.
- xECP is a DWORD offset: extended capabilities start at `BAR0 + (xECP << 2)`.
- CSZ: 0 = 32-byte contexts, 1 = 64-byte contexts. Read once, store as `ContextSize` (32/64), use for every context stride.
- FSC (Force Save Context Capability, HCCPARAMS2 bit 2): "When this bit is '1', the Save State operation shall save any cached Slot, Endpoint, Stream or other Context information to memory" (5.3.9). It changes which endpoints must be stopped before a save; see "Controller Save/Restore State" in section 3. FSC = 0 is the conservative direction, and that much is spec-backed: p.313 requires Stop Endpoint on Idle Running endpoints as well as Busy ones when FSC = 0.
  - HCCPARAMS2 is not a 1.1-only register. "Force Save Context Capability support (i.e. FSC = '1') shall be mandatory for all xHCI 1.1 and xHCI 1.2 compliant xHCs" (4.23.2, p.313) states when the capability becomes required, not when the register appears.

    Appendix H.1 settles the other half: it lists the capabilities "that were optional for xHCI 1.0 implementations [and] are now required in xHCI 1.1 implementations", and H.1.6 is FSC (p.593), as are U3C (H.1.4), CTC (H.1.7) and CIC (H.1.8), three more bits of this same register. So a 1.0 controller may legitimately advertise FSC, and a driver that forces the bit to 0 on an HCIVERSION test is discarding a discovery bit. `src/xhci_caps.c` gates the read on reach instead (CAPLENGTH and the mapped window must both extend to 0x20), which is checkable rather than inferred.
  - What remains an inference: the PDF does not say what a controller predating the register returns at that address. With the reach gate the exposure is a controller whose CAPLENGTH covers 0x20 and which implements nothing there. The convention that such a read is 0 is a backward-compatibility assumption, not spec text; do not restate it as a requirement. An all-ones read is refused separately, since bit 2 of it is a 1.
  - QEMU's model has no HCCPARAMS2 case at all and its capability reads default to 0, so FSC reads 0 there. That one is measured. The fleet controllers are not measured: `xhciqual` reads and prints HCCPARAMS2 and decodes FSC (`xhciqual/xhcicap.c`, `xhciqual/report.c`), but `xhciqual/results/` predates that change, so what it establishes for the fleet is HCIVERSION and CAPLENGTH and nothing about their FSC bit. Closing the gap needs a fresh bare-metal run, not a code change. Do not infer one controller's answer from another's.

## 3. Operational Registers (BAR0 + CAPLENGTH, spec 5.4)

| Offset | Register | Bits used by this driver |
|---|---|---|
| +0x00 | USBCMD | R/S `0`, HCRST `1`, INTE `2`, HSEE `3`, RsvdP `6:4`, LHCRST `7`, CSS `8`, CRS `9`, EWE `10`, EU3S `11` |
| +0x04 | USBSTS | HCH `0` (RO), HSE `2` (RW1C), EINT `3` (RW1C), PCD `4` (RW1C), SSS `8`, RSS `9`, SRE `10` (RW1C), CNR `11` (RO), HCE `12` (RO) |
| +0x08 | PAGESIZE | Bit n set => page size 2^(n+12). Bit 0 = 4 KB (the normal case) |
| +0x14 | DNCTRL | Notification Enable N0-N15 `15:0`, RsvdP `31:16` (Table 5-23 p.366). Write 0x0002 (enable FUNCTION_WAKE only, spec 5.4.4 Table 5-23 note; Function Wake is 4.13.2) or 0 |
| +0x18 | CRCR (64-bit) | RCS `0`, CS `1` (RW1S), CA `2` (RW1S), CRR `3` (RO), RsvdP `5:4`, Command Ring Pointer `63:6` (Table 5-24 p.367-368) |
| +0x30 | DCBAAP (64-bit) | Pointer `63:6`, low 6 bits RsvdZ - write 0, do not preserve (Table 5-25 p.369) |
| +0x38 | CONFIG | MaxSlotsEn `7:0`, U3E `8`, CIE `9`, SOC `10` (RW, new in revision 1.2c; RsvdP in 1.2), RsvdP `31:11` (Table 5-26 p.370). This driver never sets SOC and its read-modify-write carries whatever it read, so the bit's promotion changed nothing |
| +0x400 + 0x10*(n-1) | PORTSC for port n (1-based) | See below |

USBSTS is RW1C: to clear EINT write a value with bit 3 set. **Never
read-modify-write USBSTS with the read value ORed in**; that clears every
change bit that happened to be set. Clear EINT before clearing the
interrupter's IP bit (spec Table 5-21 note: clearing IP first then EINT can
lose an interrupt).

CRCR reads back as 0 for the pointer bits (spec 5.4.5: pointer reads are
undefined/0); keep a software copy. Write CRCR only when CRR = 0.

RsvdP in this table is a requirement. "RsvdP Reserved and Preserved: Reserved
for future RW implementations. Software shall preserve the value read for
writes to bits" (5.1.1, p.338), so every write of DNCTRL, CRCR, CONFIG,
USBCMD, ERSTSZ, ERSTBA and IMAN has to be a read-modify-write that carries the
reserved field back. Two traps this project has hit:

- A composed write is as bad as a literal one. `CONFIG = MaxSlotsEn` and
  `CRCR = base | RCS` look like they only set what they name; they clear 31:10
  and 5:4 respectively.
- A reset image is not a licence. "HCRST has just run, so the reserved field
  is zero" is not sound: a controller that implements something reserved sets
  it at reset like any other field.

RsvdZ runs the other way: DCBAAP's low six bits and the ERST entry's DW3 are
RsvdZ, where writing zero is what the specification asks for and preserving a
read would be wrong.

Two registers have no reserved field at all, so a plain write to them is
correct rather than an omission: ERDP (DESI `2:0`, EHB `3`, pointer `63:4`,
Table 5-42 p.394) and IMOD (IMODI `15:0`, IMODC `31:16`, both RW, Table 5-39
p.392).

### PORTSC (spec 5.4.8, Table 5-27) - one register per port, ports are 1-based

| Bits | Name | Type | Meaning |
|---|---|---|---|
| 0 | CCS | RO | Current Connect Status |
| 1 | PED | RW1C | Port Enabled. Writing 1 *disables* the port |
| 2 | TM | RO | Tunneled Mode (revision 1.2b; RsvdZ in 1.2). USB3 protocol ports only; "RsvdZ for a USB2 protocol port" (p.372), which is every port this driver manages, so it is still written as 0 |
| 3 | OCA | RO | Over-current Active |
| 4 | PR | RW1S | Port Reset - write 1 to start reset; reads 1 while resetting |
| 8:5 | PLS | RW | Port Link State (write only with LWS = 1). USB2 in-use values: 0 = U0, 3 = U3/suspend, 15 = Resume |
| 9 | PP | RW | Port Power (only meaningful if HCCPARAMS1.PPC = 1) |
| 13:10 | Port Speed | RO | Protocol Speed ID; default IDs: 1 = FS, 2 = LS, 3 = HS, 4 = SS |
| 15:14 | PIC | RW | Port Indicator Control (leave 0) |
| 16 | LWS | RW | Link State Write Strobe - set to make a PLS write take effect |
| 17 | CSC | RW1C | Connect Status Change |
| 18 | PEC | RW1C | Port Enabled Change |
| 19 | WRC | RW1C | Warm Reset Change (SS only) |
| 20 | OCC | RW1C | Over-current Change |
| 21 | PRC | RW1C | Port Reset Change |
| 22 | PLC | RW1C | Port Link State Change |
| 23 | CEC | RW1C | Port Config Error Change (SS only) |
| 24 | CAS | RO | Cold Attach Status (SS only) |
| 25 | WCE | RW | Wake on Connect Enable |
| 26 | WDE | RW | Wake on Disconnect Enable |
| 27 | WOE | RW | Wake on Over-current Enable |
| 30 | DR | RO | Device Removable |
| 31 | WPR | RW1S | Warm Port Reset (SS only) |

Safe-write rule (also in `docs/usb-xhci-info/xhci-programming.md`): build the
value from the read, clear PED + PR + WPR + all RW1C change bits (`17:23`) +
LWS, then OR in the one bit being changed. After PP 0->1, wait 20 ms before
touching the port (spec 5.4.8 note).

Both reset strobes are cleared, not just PR. WPR is bit 31 and RW1S: "when
software writes a `1` to this bit, the Warm Reset sequence as defined in the
USB3 Specification is initiated and the PR flag is set to `1`" (p.379).
`src/xhci.h`'s `XHCI_PORTSC_UNSAFE_MASK` clears it.

WPR is belt-and-braces here rather than the same hazard as PR. The same page
says "this flag shall always return `0` when read", so a read never carries a
set WPR and an ordinary read-modify-write cannot replay one, unlike PR, which
does read back as 1 while a reset runs. And this driver manages USB2 protocol
ports only, where p.379 says the bit "shall be RsvdZ" outright. So the mask is
protecting against a composed write (a value built from something other than
a fresh read), not against replaying a bit the hardware handed back. Keep it
cleared; do not write a poll of WPR, and do not reason that PR's read-back
behaviour applies to it.

A software-initiated power-off produces no change bits at all, and the same
goes for a disable. Every change bit in the table above carries the exclusion
in its own row (Table 5-27, p.376-378), transcribed here because a design rule
rests on it:

| Bit | Rule as written in the spec |
|---|---|
| CSC `17` | "shall not be set if the CCS transition was due to software setting PP to `0`, or the CAS transition was due to software setting WPR to `1`" |
| PEC `18` | "shall not be set if the PED transition was due to software setting PP to `0`" |
| WRC `19` | "shall not be set to `1` if the Warm Reset processing was forced to terminate due to software clearing PP or PED to '0'" |
| PRC `21` | "shall not be set to `1` if the reset processing was forced to terminate due to software clearing PP or PED to '0'" |
| PLC `22` | "shall not be set if the PLS transition was due to software setting PP to `0`" |

Two consequences for the driver. Clearing PP or writing PED = 1 terminates a
reset in progress: PRC's wording says so directly, and PR "is `0` if PP is
`0`" (p.372). And that termination is silent: no change bit, therefore no Port
Status Change Event, therefore nothing will ever observe it. Software that
ends a reset this way is the only thing that can report `C_PORT_RESET` to the
hub class, and must do so after issuing the write rather than before
(`docs/contributing/implementation-invariants.md`, "Root Hub Reporting").

PLC is set by an enumerated list of transitions, not by "the link state
changed". The PLC row (p.378) reads "this flag is set to `1` due to the
following PLS transitions" and then names them; for a USB2 protocol port the
list is:

| Transition | Condition |
|---|---|
| U3 -> Resume | Wakeup signalling from a device |
| Resume -> U0 | Device Resume complete |
| U3 -> U0 | Software Resume complete |
| U2 -> U0 | L1 Resume complete |
| U0 -> U0 | L1 Entry Reject |
| Any State -> U3 | U3 Entry complete, and only if U3E = `1` |

(The remaining rows, Resume -> Recovery -> U0, U3 -> Recovery -> U0 and Any
state -> Inactive, are USB3-only.) Disabling a port is not on that list, so a
port driven out of Resume by a `PED` write sets no PLC. Do not reason from the
row's closing exclusion instead ("shall not be set if the PLS transition was
due to software setting PP to `0`", therefore anything else does set it): the
exclusion narrows the list, it does not define it.

A reset is only defined from the Disabled state, i.e. on a port with a device
on it. The USB2 Root Hub Port state machine (4.19.1.1, p.274-275) is explicit
about which state each transition leaves from:

| From | Trigger | To | Flags set |
|---|---|---|---|
| Powered-off | write PP = 1 | Disconnected | - |
| any state | write PP = 0, or over-current | Powered-off | - |
| Disconnected | device connect detect (CCS = 1) | Disabled | CSC |
| Disabled | write PR = 1 | Reset | - |
| Reset | "the Reset operation completes (PR = '0')" | Enabled | PED and PRC |
| Disabled or Enabled | disconnect detect (CCS = 0) | Disconnected | CSC, and "if PR or PED flags are set to '1', they shall be cleared to '0'" |
| Enabled | write PED = 1, or a Port_Error | Disabled | PEC, and only if it was a Port_Error |

Two consequences:

- A `PR` write to a port in the Disconnected state has no transition at all.
  The port never enters Reset, so it never "automatically advance[s] to the
  Enabled state, setting PED and PRC to `1`". There is no PED, no PRC, and
  therefore no Port Status Change Event. A driver that resets an empty port
  and waits for PRC waits for ever, which is what its own age-based retire
  exists for. (4.19.1.1.4 also says "Software shall ignore the value of the
  Port Link State (PLS) field while in the Reset state".)
- PED and PRC arrive together or not at all, from the same transition. A model
  that sets PRC while leaving PED clear is describing a controller that has no
  state for the port to be in.

Also from the PED row (p.372): PED and PR are mutually exclusive. "Note that
when software writes this bit to a `1`, it shall also write a `0` to the PR
bit", and writing both as `1` is undefined (footnote 82). The neutral-write
rule above already satisfies this, since it clears both before OR-ing in the
one being changed.

### Controller Save/Restore State (CSS/CRS, spec 4.23.2 and 5.4.1/5.4.2)

Transcribed from the local spec PDF. Page numbers are the PDF's printed page
numbers.

The four bits, quoted from the register tables (5.4.1 p.361, 5.4.2 p.364):

| Bit | Register | Type | Contract |
|---|---|---|---|
| CSS `8` | USBCMD | RW | "When written by software with `1` and HCHalted (HCH) = `1`, then the xHC shall save any internal state ... and if FSC = '1' any cached Slot, Endpoint, Stream, or other Context information. When written by software with `1` and HCHalted (HCH) = `0`, or written with `0`, no Save State operation shall be performed. This flag always returns `0` when read." Undefined behaviour if started while `RSS = 1` |
| CRS `9` | USBCMD | RW | "When set to `1`, and HCHalted (HCH) = `1`, then the xHC shall perform a Restore State operation ... When set to `1` and Run/Stop (R/S) = `1` or HCHalted (HCH) = `0`, or when cleared to `0`, no Restore State operation shall be performed. This flag always returns `0` when read." Undefined behaviour if started while `SSS = 1` |
| SSS `8` | USBSTS | RO | Set to `1` when CSS is written `1`, "remain 1 while the xHC saves its internal state ... When the Save State operation is complete, this bit shall be cleared to `0`" |
| RSS `9` | USBSTS | RO | The same, for CRS |
| SRE `10` | USBSTS | RW1C | "If an error occurs during a Save or Restore operation this bit shall be set to `1`. This bit shall be cleared to `0` when a Save or Restore operation is initiated or when written with `1`" |

Consequences that are easy to get wrong: CSS and CRS read back as 0, so
neither may be polled for completion and neither survives a read-modify-write
of USBCMD. The completion signal is SSS/RSS in USBSTS, and SRE is RW1C like
the rest of USBSTS's change bits (the "never RMW USBSTS with the read value
ORed in" rule above applies to it).

The ordered procedure (4.23.2 p.313-314), abbreviated to what this driver
does (no streams, one interrupter, no VTIO):

Save: (1) Stop Endpoint on every Busy endpoint in the Running state, and, if
FSC = 0, on every Idle endpoint in the Running state as well; the command is
what makes the xHC write back the TR Dequeue Pointer and DCS. (2) Ensure the
command ring is Stopped (`CRCR.CRR = 0`) or idle and all its Command
Completion Events have been received. (3) `R/S = 0`. (4) Read and save USBCMD,
DNCTRL, DCBAAP, CONFIG, ERSTSZ, ERSTBA, ERDP, IMAN, IMOD in that order. (5)
Set CSS and wait for SSS to become 0. (6) If Max Scratchpad Buffers > 0 and
SPR = 1, save an image of the scratchpad buffers. (7) Save an image of the
DCBAA, contexts and everything they reference. (8) Remove Core Well power.

Restore: (1) power; (2) restore the DCBAA/context/ring images to the same
physical addresses; (3) restore the scratchpad image if one was saved; (4)
write DNCTRL, DCBAAP, CONFIG, ERSTSZ, ERSTBA, ERDP, IMAN, IMOD in that order
and before CRS, because "The Restore operation overwrites internal default
values asserted by a xHC reset" (p.314); (5) set CRS and wait for RSS to
become 0; (6) reinitialize the command ring so its Cycle bits agree with the
RCS to be written; (7) write CRCR with that address and RCS (the write itself
restarts the ring); (8) `R/S = 1`; (9) walk the topology and initialize PORTSC
(and PORTPMSC/PORTLI) per port; (10) re-ring the doorbell of each previously
Running endpoint.

Four normative statements that decide design questions in this driver:

- "The state of a Root Hub port is not covered by a Save or Restore
  operation" (p.315). Restore therefore does not recreate a saved PORTSC
  state, and step 9 above is not optional. In this driver the
  successful-restore path re-seeds the root-hub shadow from the live port
  registers and skips the pass that drives U3 ports to U0, preserving a
  suspend that usbhub requested. Consequently `RhPortsDriventoU0` does not
  increment on that path because the pass did not run, not because Restore
  State defaulted the ports; `RhU3PassSkippedAfterRestore` records the
  distinction. On the fallback path, HCRST defaults the port registers before
  the pass, so a zero fresh increment remains the prediction for that separate
  reason. The counter is cumulative; neither statement promises that its
  lifetime total is zero.
- "After a Save or Restore State operation completes, the Save/Restore Error
  (SRE) flag ... should be checked to ensure that the operation completed
  successfully" (p.314), and "If the saved state is corrupted, the SRE flag
  ... shall be set to `1`, the Restore operation terminated, and the RSS flag
  cleared to `0`" (p.315). A cleared RSS is therefore not evidence of
  success; a terminated restore clears it too. SRE is the only success test.
- Putting the xHC into Run mode between a Save and a Restore is itself an
  error the controller must report: it "shall be reported by the assertion of
  SRE upon the completion of the Restore State operation", though "only if
  the Aux Power well has been maintained", and unaffected by an intervening
  HCRST (p.315).
- "The internal state of the xHC shall be valid until it enters the D3cold
  state ... If prior to setting the xHC into the D3cold state, software
  decides to restart the xHC, then a Restore State operation is not required"
  (p.314). This is the same sentence the halt/resume argument leans on, with
  its Save/Restore half attached.

What QEMU 11.0.0 does with all of this, read out of `hw/usb/hcd-xhci.c` at
the exact commit the installed binary reports (`a4bb4b10c9`;
`qemu-system-x86_64 --version` prints `v11.0.0-12122-ga4bb4b10c9`): a USBCMD
write with CSS does `xhci->usbsts &= ~USBSTS_SRE;` and nothing else, and a
write with CRS does `xhci->usbsts |= USBSTS_SRE;` and nothing else.
`USBSTS_SSS` and `USBSTS_RSS` are never assigned anywhere in the file, and
neither branch checks HCH or R/S. So in the target VMs:

- the SSS/RSS polls both complete on their first read, because the bits are
  never set;
- every Restore State attempt sets SRE, i.e. QEMU always reports a failed
  restore;
- HCSPARAMS2 is the constant `0x0000000F`, so Max Scratchpad Buffers = 0 and
  SPR = 0, and HCCPARAMS2 is absent so FSC = 0.

That is more useful than a silent no-op: the error path is the one that
executes in both target VMs on every resume, so the slot-invalidation fallback
is VM-observable and only the success path is bare-metal-only.

## 4. Runtime Registers (BAR0 + RTSOFF, spec 5.5)

| Offset | Register | Fields |
|---|---|---|
| +0x00 | MFINDEX | Microframe index `13:0` (125 us units) |
| +0x20 + 32*n | IR[n].IMAN | IP `0` (RW1C), IE `1`, RsvdP `31:2` |
| +0x24 + 32*n | IR[n].IMOD | IMODI `15:0` (interval, 250 ns units; default 4000 = 1 ms), IMODC `31:16`. No reserved field (Table 5-39 p.392) |
| +0x28 + 32*n | IR[n].ERSTSZ | Number of ERST entries `15:0` (write 1), RsvdP `31:16` (Table 5-40 p.393) |
| +0x30 + 32*n | IR[n].ERSTBA (64-bit) | RsvdP `5:0`, ERST base `63:6` (Table 5-41 p.394) |
| +0x38 + 32*n | IR[n].ERDP (64-bit) | DESI `2:0`, EHB `3` (RW1C), dequeue pointer `63:4`. No reserved field (Table 5-42 p.394) |

ISR/DPC rules:

- ISR: read USBSTS; if EINT = 0 the interrupt is not ours. Clear EINT (write 1), then clear IMAN.IP (write IMAN with IP = 1). Both are RW1C. EINT is a summary of IP 0->1 transitions, not the INTx line source; IMAN.IP holds INTx asserted. Acknowledge this pair once and defer Event Ring work. Do not copy a generic PCI ISR's status-drain loop into the xHCI path.
  - This driver's ISR writes IE as 0, not as 1. That costs no delivery: the xHC sets EHB when it sets IP and cannot set IP again while EHB is set, so no interrupt can be generated in the window the ISR opens whatever IE holds. What it buys is that the ISR, which runs at DIRQL and cannot take the miniport's DISPATCH-level controller lock, moves IE in the same direction every masking path does, so it can never re-publish an enable a concurrent mask has just cleared. IE is re-raised only by the DPC's re-arm, under the lock and only while usbport still wants interrupts. See `docs/contributing/implementation-invariants.md`, "Interrupt Ordering", and `src/xhci_evt.c` (`XhciIsr`).
- DPC: after draining events, write ERDP = (current dequeue physical address) | EHB(bit 3) to clear Event Handler Busy. EHB gating is architectural, not controller-specific: the xHC sets EHB = 1 whenever it sets IP, and IP shall not be set again while EHB = 1 (spec 4.17.2 interrupt-assertion conditions, 4.17.5 IP rules, 5.5.2.3.3 EHB field). A drain in progress therefore cannot be re-interrupted by its own interrupter, and forgetting the final EHB = 1 write silences the interrupter permanently.
- During a long drain, also write ERDP periodically (e.g. every 32 events), not only at the end: the xHC detects a full event ring from the software-advertised dequeue pointer, so a stale ERDP during an event burst causes Event Ring Full (completion code 21) even though the DPC is consuming events. EHB is RW1C, so put 0 in bit 3 on these intermediate writes. That preserves EHB = 1, keeping interrupts suppressed mid-drain; writing 1 would clear it. Only the final write after the ring is empty carries bit 3 = 1.
- With PCI pin-based interrupts (this driver's only mode), the INTx line stays asserted while IMAN.IP = 1 (spec 5.5.2.1). It is a level-triggered line, so clearing IP in the ISR is mandatory or the machine hangs in an interrupt storm.
- IMOD: leave default (4000 = 1 ms moderation) initially; lower it later only if HID latency is an issue.

## 5. Doorbell Registers (BAR0 + DBOFF, spec 5.6)

Each doorbell is one 32-bit register: `DB Target 7:0`, RsvdZ `15:8`, `DB Stream ID 31:16`.

| Register | Write value | Meaning |
|---|---|---|
| DB[0] | 0 | Host controller doorbell: run the command ring |
| DB[SlotID] | DCI (1..31) | Ring endpoint DCI of that slot; stream bits stay 0 (no streams) |

Doorbell writes may be posted; a read of any xHCI register flushes them
(rarely needed, only when ordering a doorbell against something else).

## 6. Extended Capabilities (BAR0 + (HCCPARAMS1.xECP << 2), spec 7)

Each capability header DWORD: `Capability ID 7:0`, `Next Capability Pointer
15:8` (in DWORDs, relative to this capability; 0 = end of list).

| ID | Capability | Use here |
|---|---|---|
| 1 | USB Legacy Support | BIOS handoff - see below |
| 2 | Supported Protocol | Port topology classification - see below |
| 10 | Debug Capability | Skip; note ports it claims |
| 18 | USB3 Tunneling Support | Skip (revision 1.2b, Table 7-2 p.477; USB4 tunneling, not reachable from a USB 2.0 port). The walk matches the IDs it wants and steps over the rest, so this one was never special-cased |
| 192-255 | Vendor Defined | Skip; log the ID. (The Renesas uPD720201/202 firmware upload runs via PCI config space, not an extended capability - Linux `xhci-pci-renesas.c`) |

### USB Legacy Support (USBLEGSUP, spec 7.1.1)

- DW0 (the capability header DWORD itself): ID `7:0` = 1, Next `15:8`, HC BIOS Owned Semaphore `16`, HC OS Owned Semaphore `24`.
- DW1 (offset +4, USBLEGCTLSTS, spec 7.1.2): SMI enable bits in `15:0` (bit 0 = USB SMI Enable, bit 4 = SMI on Host System Error, bit 13 = SMI on OS Ownership Enable, bit 14 = SMI on PCI Command, bit 15 = SMI on BAR), status/RW1C bits in `31:16` (bit 29 = SMI on OS Ownership Change, 30 = SMI on PCI Command, 31 = SMI on BAR).

Handoff: set DW0 bit 24; poll until bit 16 clears (~1 s timeout, proceed with a
warning on timeout); then write DW1 clearing all enable bits in `15:0` and
writing 1s to the RW1C status bits `31:29`. Full procedure in
`docs/usb-xhci-info/xhci-programming.md`, "BIOS handoff - required on all controllers".

### Supported Protocol (spec 7.2)

| Offset | Fields |
|---|---|
| +0x00 | ID `7:0` = 2, Next `15:8`, Minor Revision (BCD) `23:16`, Major Revision (BCD) `31:24` (0x02 = USB 2, 0x03 = USB 3) |
| +0x04 | Name String = 0x20425355 ("USB ") |
| +0x08 | Compatible Port Offset `7:0` (1-based first port), Compatible Port Count `15:8`, protocol-defined flags `27:16`, PSIC `31:28` |
| +0x0C | Protocol Slot Type `4:0` - value to put in the Enable Slot command's Slot Type field |
| +0x10.. | PSIC x Protocol Speed ID DWORDs (only present if PSIC > 0; if PSIC = 0 the default speed IDs apply: 1 = FS, 2 = LS, 3 = HS, 4 = SS) |

## 7. TRBs (spec 6.4)

All TRBs are 16 bytes: DW0, DW1 (usually a 64-bit parameter), DW2 (status),
DW3 (control). DW3 always has Cycle `0` and TRB Type `15:10`.

### TRB Type codes (spec Table 6-91)

| ID | TRB | Ring | | ID | TRB | Ring |
|---|---|---|---|---|---|---|
| 1 | Normal | Transfer | | 14 | Reset Endpoint Cmd | Command |
| 2 | Setup Stage | Transfer | | 15 | Stop Endpoint Cmd | Command |
| 3 | Data Stage | Transfer | | 16 | Set TR Dequeue Ptr Cmd | Command |
| 4 | Status Stage | Transfer | | 17 | Reset Device Cmd | Command |
| 5 | Isoch | Transfer | | 23 | No Op Cmd | Command |
| 6 | Link | Transfer + Command | | 32 | Transfer Event | Event |
| 7 | Event Data | Transfer | | 33 | Command Completion Event | Event |
| 8 | No-Op (transfer) | Transfer | | 34 | Port Status Change Event | Event |
| 9 | Enable Slot Cmd | Command | | 35 | Bandwidth Request Event | Event |
| 10 | Disable Slot Cmd | Command | | 36 | Doorbell Event | Event |
| 11 | Address Device Cmd | Command | | 37 | Host Controller Event | Event |
| 12 | Configure Endpoint Cmd | Command | | 38 | Device Notification Event | Event |
| 13 | Evaluate Context Cmd | Command | | 39 | MFINDEX Wrap Event | Event |

### Transfer TRBs

64 KB boundary rule (spec 6.4.1 note): a data buffer referenced by any
Transfer TRB (Normal, Data Stage, Isoch) **must not span a 64 KB physical
boundary**. "If a physical data buffer spans a 64KB boundary, software shall
chain multiple TRBs to describe the buffer." Keeping the length <= 64 KB is
not sufficient: an SG entry from usbport can start anywhere, so split every
buffer fragment at 64 KB boundaries when encoding TRBs. Violations are silent
data corruption on some controllers.

Normal (type 1, spec 6.4.1.1), bulk/interrupt data:

| DW | Fields |
|---|---|
| 0/1 | Data Buffer Pointer Lo/Hi (physical) |
| 2 | TRB Transfer Length `16:0` (max 64 KB), TD Size `21:17`, Interrupter Target `31:22` |
| 3 | C `0`, ENT `1`, ISP `2`, NS `3`, CH `4`, IOC `5`, IDT `6`, BEI `9`, Type `15:10` |

- TD Size (spec 4.11.2.4): number of packets remaining in the TD after this TRB, capped at 31; must be 0 on the last TRB of a TD. Single-TRB transfers: 0. The formula is transcribed below.
- ISP (Interrupt on Short Packet): set it on IN TRBs so short packets generate a Transfer Event with code 13.
- CH chains TRBs into one TD; IOC set on the last TRB only.

#### TD Size formula (spec 4.11.2.4, p.198)

Transcribed because it is a packet count computed from a byte running total,
and because getting it wrong is invisible: the xHC treats it as a scheduling
hint, so a wrong value costs performance or nothing at all until it costs
correctness on some controller.

```
TD Packet Count       = ROUNDUP( TD Transfer Size / Max Packet Size )
Packets Transferred(n)= ROUNDDOWN( TRB Transfer Length Sum(n) / Max Packet Size )
TD Size(n)            = IF ( TD Packet Count - Packets Transferred(n) > 31 )
                          THEN 31 ELSE TD Packet Count - Packets Transferred(n)
TD Size(x)            = 0                     -- x = the last TRB of the TD
```

`TRB Transfer Length Sum(n)` is inclusive of TRB n. "The value of the TD Size in
the last Transfer TRB of a TD (TD Size (x)) shall be cleared to '0' to explicitly
indicate that it is the last Transfer TRB of the TD. Since the TD Size field is
only 5 bits, its value shall be forced to 31 if the number of packets to be
scheduled is greater than 31."

So the field needs the endpoint's Max Packet Size. That is the reason
`XhciXferBuildControl` takes EP0's MPS0 and refuses anything but 8/16/32/64
(USB2 9.6.1 `bMaxPacketSize0`).

### Control transfers are two or three TDs (spec 4.11.2.2, 6.4.1.2)

Transcribed in full because the obvious reading (one TD with a Setup TRB, some
Data TRBs and a Status TRB chained together) is wrong, and wrong in a way that
produces a plausible-looking ring:

> "Control transfers require two or three TDs to define them: a Setup Stage TD
> followed by an Status Stage TD, if a data stage is required for the transfer an
> optional Data Stage TD will reside between the Setup Stage and Status Stage
> TDs." (p.430)

The rules a control endpoint's Transfer Ring obeys (p.192):

- "Each Setup Stage TD shall contain a single Setup Stage TRB."
- "A Data Stage TD shall consist of a Data Stage TRB chained to zero or more
  Normal TRBs, or Event Data TRBs."
- "A Status Stage TD shall contain of a single Status Stage TRB, optionally
  chained to an Event Data TRB."
- "All Control transfers require a Setup Stage TD followed by a Status Stage TD."
  A transfer with no data stage is generated by there being no Data Stage TD
  between them, and "No more than one Data Stage TD may be defined between a pair
  of Setup and Status Stage TDs."
- "A Setup Stage TRB shall contain immediate data (IDT flag = '1'), its Parameter
  fields shall contain the 8-byte USB SETUP Data ... and its Length field shall
  be set to '8'."
- "System software is responsible for ensuring that the total data length defined
  by a Data Stage TD ... is equal to wLength. Note that communicating with some
  non-compliant devices may require violating this rule. The transfer lengths
  managed by the xHC depend strictly on the TRB Length fields." (p.193)

Two consequences this driver is built on. Because each stage is its own TD,
the three must still be published as one store, otherwise the xHC can begin a
control transfer whose Status Stage TRB does not exist yet. That is what
`XhciRingEnqueueTdGroup` exists for. And because "the IOC flag should only be
set in the Status Stage TRB of a Control transfer" (p.430), a successful
control transfer produces exactly one Transfer Event, naming the group's last
TRB; the retire that event triggers jumps the dequeue pointer past all three
TDs, because `XhciRingRetireTd` moves to just past the matched tail rather
than walking one TD.

Direction is derived from the SETUP bytes, not from the caller's flags
(p.192): "System software is responsible for ensuring that the Direction (DIR)
flag of the Data Stage and Status Stage TRBs are consistent with the USB SETUP
Data defined bmRequestType:Data Transfer Direction (DTD) flag and wLength field."
Table 4-7 (p.193), transcribed:

| bmRequestType DTD | wLength | Setup TRT | Data Stage TRB DIR | Status Stage TRB DIR |
|---|---|---|---|---|
| Host-to-device | 0 | 0 (No Data Stage) | no Data Stage TD | IN |
| Host-to-device | >0 | 2 (OUT Data Stage) | OUT | IN |
| Device-to-host | 0 | 0 (No Data Stage) | no Data Stage TD | IN |
| Device-to-host | >0 | 3 (IN Data Stage) | IN | OUT |

"The Direction (DIR) flag in the Data Stage TRB defines the transfer direction
for all TRBs in the Data Stage TD", so the chained Normal TRBs after it have
no direction of their own to set, and a Normal TRB "depends on the direction
defined by the Endpoint Context that it is associated with, or the preceding
Data Stage TRB" (p.191).

Setup Stage (type 2, spec 6.4.1.2.1):

| DW | Fields |
|---|---|
| 0 | `bmRequestType 7:0`, `bRequest 15:8`, `wValue 31:16` (immediate data, not a pointer) |
| 1 | `wIndex 15:0`, `wLength 31:16` |
| 2 | TRB Transfer Length `16:0` = always 8, `21:17` RsvdZ (no TD Size), Interrupter Target `31:22` |
| 3 | C `0`, `4:1` RsvdZ, IOC `5`, IDT `6` = always 1, `9:7` RsvdZ, Type `15:10` = 2, TRT `17:16`: 0 = no data stage, 1 reserved, 2 = OUT data stage, 3 = IN data stage, `31:18` RsvdZ |

Data Stage (type 3, spec 6.4.1.2.2): same as Normal plus DIR `16` in DW3 (1 = IN). Length in DW2 `16:0`, "Valid values are 1 to 64K"; TD Size `21:17`. "The Chain bit is always '0' in the last TRB of a Data Stage TD."

Status Stage (type 4, spec 6.4.1.2.3): DW0/1 RsvdZ; DW2 bits `21:0` RsvdZ, Interrupter Target `31:22`; DW3: C `0`, ENT `1`, `3:2` RsvdZ, CH `4`, IOC `5` (set; this is where the control transfer completes), `9:6` RsvdZ, Type `15:10` = 4, DIR `16`, `31:17` RsvdZ. Status direction is opposite the data stage; with no data stage, Status is IN (DIR = 1). "A Transfer Event generated by this TRB shall reflect the status state response from the USB device", and it "shall report a Success, Stall Error, or other error Completion Code" (p.193).

Isoch (type 5, spec 6.4.1.3, p.435-437), transcribed field for field because
the builder needs all of them. DW0/1 is the Data Buffer Pointer, the same as a
Normal TRB's.

| DW | Bits | Field | Notes |
|---|---|---|---|
| 2 | 16:0 | TRB Transfer Length | "Valid values are 0 to 64K" (Table 6-33) |
| 2 | 21:17 | TD Size / TBC | TD Size when ETE = 0, which is this driver's case; the Transfer Burst Count when ETE = 1 |
| 2 | 31:22 | Interrupter Target | |
| 3 | 0 | C | |
| 3 | 1 | ENT | |
| 3 | 2 | ISP | |
| 3 | 3 | NS | never set |
| 3 | 4 | CH | "An Isoch Transfer Descriptor is defined as an Isoch TRB followed by zero or more Normal TRBs ... The Chain bit is always '0' in the last TRB of an Isoch TD" (Table 6-34) |
| 3 | 5 | IOC | |
| 3 | 6 | IDT | never set - "shall not be set ... on IN endpoints", and this driver's buffers are always pointers |
| 3 | 8:7 | TBC | when ETE = 0, "number of bursts - 1 that shall be required to move this Isoch TD" |
| 3 | 9 | BEI | **never set it** (Intel quirk, `docs/usb-xhci-info/xhci-programming.md` "Never set BEI") |
| 3 | 15:10 | TRB Type = 5 | |
| 3 | 19:16 | TLBPC | "the number of packets -1 that shall be in the last burst of this Isoch TD" |
| 3 | 30:20 | Frame ID | 11 bits, matched against MFINDEX bits `13:3`; "ignored by the xHC if the Start Isoch ASAP flag is set" |
| 3 | 31 | SIA | Start Isoch ASAP. 1 = ignore Frame ID and schedule as soon as possible |

Note that TBC appears at two different offsets and which one is live depends
on ETE, a capability this driver does not enable: with ETE = 0 the DW2 field
is TD Size and the DW3 `8:7` field is TBC. Writing the DW2 encoding would put
a burst count into the TD Size field of every Isoch TRB.

Link (type 6, spec 6.4.4.1): DW0/1 = next segment pointer (16-byte aligned); DW3: C `0`, TC `1` (Toggle Cycle; set on the wrap-back link of a single-segment ring), CH `4`, Type `15:10` = 6.

TD composition and Link placement (spec 4.11.7, p.212; Link TRB notes p.208;
CH field description p.464), transcribed because a TD that spans the
wrap-back Link TRB has to obey all of it:

- "The TRB Chain flag is used [to] identify the TRBs of a TD, where the Chain
  flag is set in all the TRBs of a TD except the last." A Link TRB inside a TD
  is one of those TRBs, so its CH must be 1; a Link TRB between TDs must have
  CH = 0. On a single-segment ring the one Link TRB is permanent and reused
  every lap, so this is rewritten at each crossing, not set once.
- p.208: "If the Chain bit (CH) of the previous TRB is `1`, then the multi-TRB TD
  that it defines spans segments and shall continue with the first TRB of the
  next segment." And: "As software advances its Enqueue Pointer and advances over
  a Link TRB, the Cycle (C) bit shall be updated with the value of the PCS flag."
- "Software shall not define a Link TRB as the first TRB of a multi-TRB TD" nor
  "as the last TRB of a multi-TRB TD", and "shall not define consecutive Link
  TRBs within a TD". "Undefined xHC behavior may occur if the requirements
  defined in this section are not met."
- "In a Command Ring the Link TRB Chain bit (CH) is ignored by the xHC"
  (p.208), so one implementation serves both ring kinds.
- A Link TRB alone is a legal TD ("A Link TD is a TD that consists of just one
  Link TRB"), which is what an ordinary between-TD crossing produces.

`src/xhci_ring.c` (`XhciRingEnqueueTd`) implements these; `test/test_ring.c`
pins the CH-set and CH-cleared crossings.

### Isochronous scheduling (spec 4.11.2.3, 4.11.2.5, 4.14.2.1, 4.10.3)

Everything here is about when an Isoch TD runs, which is the half of
isochronous transfer that has no analogue in the control, interrupt or bulk
paths; those place TRBs and the controller runs them when it gets to them.

One TD per ESIT, and the TD is the unit. "An Isoch TD defines an isochronous
data transfer that will occur during a single Interval" and "the xHC shall
consume one Isoch TD each Interval on an Isoch Transfer Ring" (p.196). "If
an Isoch Endpoint Context is Active, the xHC shall process one Isoch TD from
its Transfer Ring each ESIT" (4.14.2.1 p.238). So a usbport isochronous
request of N packets is N Isoch TDs, not one TD of N TRBs. The packet boundary
is a scheduling boundary, and a TD is what the schedule consumes.

Two consequences of that unit. IOC lands on every TD's last TRB rather than
once per request, because usbport wants a length and a status per packet and
only an event supplies one. So an isochronous stream costs one interrupt per
service interval, 1,000/s on a Full-Speed stream. BEI, which would keep the
event and suppress the interrupt, is not available to this driver: Linux sets
`XHCI_AVOID_BEI` on every Intel controller (`xhci-programming.md`, "Never set
BEI").

Low Speed is refused outright at endpoint open, because USB 2.0 gives Low
Speed no isochronous transfers at all. The Interval this driver
programs is 0 on High Speed and 3 on Full Speed when it has to assume one
(Table 6-12's isochronous rows: FS Isoch is 3-18 where FS/LS Interrupt stops
at 10), and the descriptor-derived value otherwise
(`usbport-miniport-abi.md`, "Periodic scheduling").

TD Transfer Size may not exceed Max ESIT Payload. "Software shall not define a
TD Transfer Size for a TD of an Isoch endpoint that exceeds the Max ESIT
Payload" (4.14.2.1 p.238). Exceeding it is not merely rejected: "a Bandwidth
Overrun Error shall be generated for the offending TRB and the xHC shall
advance its Dequeue Pointer to the next Isoch TD boundary or the Enqueue
Pointer ... whichever is encountered first. Note that the pipe remains Active
after this error, the xHC simply truncates the transfer".

TBC and TLBPC are software's to compute (4.11.2.3 p.197), from the Transfer
Descriptor Packet Count (TDPC, 4.14.1 p.234: "the number of packets required
to move all the data defined by a TD. Note that a partial or a zero-length
packet increments this count by 1"):

```
TDPC  = ROUNDUP ( TD Transfer Size / Max Packet Size )      (>= 1, see below)
TBC   = ROUNDUP ( TDPC / ( Max Burst Size + 1 ) ) - 1
residue = TDPC MODULUS ( Max Burst Size + 1 )
TLBPC = IF ( residue == 0 ) THEN Max Burst Size ELSE residue - 1
```

A zero-length Isoch TD still moves one packet. The xHC "shall transmit a
zero-length DP to the USB bus regardless bus speed, consuming the Isoch TD for
the Service Interval" (4.14.2.1 p.239), so its TDPC is 1, not 0, which is
what the "a partial or a zero-length packet increments this count by 1"
sentence means. TDPC = 0 would encode TBC = -1.

Frame ID, and the window it must fall in (4.11.2.5 p.199). The field is
11 bits, "calculated as the modulus of 2048, i.e. the size of the Frame Index
portion of the MFINDEX register", and it is matched against MFINDEX bits `13:3`:

```
Start Frame ID = ( MFINDEX frame index + IST + 1 ) MOD 2048    (IST rounded up to frames)
End   Frame ID = ( MFINDEX frame index + 895   ) MOD 2048
```

Software "shall not" schedule above the End Frame ID and "should not" below
the Start Frame ID. The two bounds are distances from the current frame, not
magnitudes (at current = 2040 the frames 2041..2047 and 0..7 are all in the
near future and half carry the smaller number). `XhciXferFrameIdUsable`
(`src/xhci_xfer.c`) takes that distance in the full 32-bit frame domain and
only then compares the result against the two bounds, not modulo 2048. So a
usbport stamp a lap or more stale lands near 2^32 and is refused, where
reducing both numbers first would have made it look 48 frames ahead and named
a frame already passed. Only the Frame ID written into the TRB is the low 11
bits, because that is the field's width.

IST is HCSPARAMS2 `3:0`: bit 3 selects the unit. Clear means `IST[2:0]`
microframes, set means `IST[2:0]` frames (5.3.4, and 4.14.2.1.4 p.243),
and "software shall always add a value of one microframe to the value read" to
cover the read latency.

CFC decides whether Frame IDs may be used at all after the first TD.
Contiguous Frame ID Capability is HCCPARAMS1 bit 11 (Table 5-13), mandatory
for xHCI 1.1 and 1.2. The two cases are not symmetric:

- CFC = 1: "software should set the Frame IDs (i.e. SIA = '0') in all Isoch
  TDs", and the xHC matches every one of them, which "ensures
  resynchronization of Isoch TDs even if some are dropped due to Missed
  Service Errors or Stopping the endpoint".
- CFC = 0: "software may set the Frame ID (i.e. SIA = '0') only in the first
  Isoch TD of an Isoch data flow, and shall set SIA = '1' in all subsequent
  TDs of the data flow". Setting Frame IDs throughout on such a controller is
  not a missed optimisation, it is outside the contract: the xHC "may ...
  ignore the Frame ID fields in subsequent Isoch TDs until the data flow is
  terminated".

Setting SIA = 1 everywhere is legal on both (the Frame ID is permitted, never
required), and it is what a driver whose frame numbering cannot be proved
congruent to MFINDEX has to do. See
`docs/usb-xhci-info/usbport-miniport-abi.md` on `Get32BitFrameNumber` and the
isoch parameter block.

Ring Underrun, Ring Overrun, Missed Service (4.10.3.1 p.185-187, 4.10.3.2
p.187, 4.14.2.1 p.238). All three use the Transfer Event TRB format, and none
of them halts the endpoint:

| Condition | Code | When | What software owes |
|---|---|---|---|
| Ring Underrun | 14 | an OUT isoch endpoint's ring is empty at the ESIT | ring the doorbell once there is work: "Ringing the doorbell of a periodic endpoint that has encountered a Ring Overrun or Ring Underrun condition shall place it back on the periodic schedule" |
| Ring Overrun | 15 | an IN isoch endpoint's ring is empty at the ESIT | the same |
| Missed Service Error | 23 | the xHC could not meet the deadline for a TD | nothing: "the data associated with the TD in error shall be lost, however for the next ESIT the xHC shall advance to the next Isoch TD and attempt to execute it" |

Two properties of the first two matter to the event reader and are easy to get
wrong. The endpoint stays Running ("the endpoint shall remain in the Running
state, and be removed from the Pipe Schedule"), so this is not a recovery case
and must not reach a Reset Endpoint or a Set TR Dequeue Pointer.

The TRB Pointer is not a TRB of any TD: "the TRB referenced by the Dequeue Pointer is
not valid. Ring Underrun and Ring Overrun Transfer Events shall clear the TRB
Transfer Length field to '0', and set the TRB Pointer field to the address of the
invalid TRB (i.e. the value of the Dequeue Pointer where the ... condition was
detected)", with the note that "Pre-1.1 xHC implementations clear the TRB Pointer
field ... to '0'". So the pointer is either zero or a live ring address that
belongs to no outstanding transfer, and an event reader that resolves it as a
completion will attribute an underrun to whatever TD happens to sit there.

Only one is generated per empty stretch: "A Ring Underrun or Ring Overrun Event
is only generated the first Interval that an empty Transfer Ring is detected"
(p.196).

A late doorbell produces both, in order (p.187): "A late doorbell ring may
result in the generation of two Events; a Ring Overrun or Ring Underrun
condition, being followed immediately by a Missed Service Error." Counting the
pair as two independent faults over-reports; it is one late submission.

### Command TRBs (spec 6.4.3)

Common shape: DW0/1 = Input Context Pointer (physical, 16-byte aligned) where
applicable, DW2 = 0, DW3 carries C, Type, and:

| Command | Type | DW3 extras | DW0/1 |
|---|---|---|---|
| No Op | 23 | - | 0 |
| Enable Slot | 9 | Slot Type `20:16` (from Supported Protocol cap, offset 0x0C) | 0 |
| Disable Slot | 10 | Slot ID `31:24` | 0 |
| Address Device | 11 | BSR `9`, Slot ID `31:24` | Input Context ptr |
| Configure Endpoint | 12 | DC `9` (deconfigure), Slot ID `31:24` | Input Context ptr |
| Evaluate Context | 13 | Slot ID `31:24` | Input Context ptr |
| Reset Device | 17 | Slot ID `31:24` | 0 |
| Reset Endpoint | 14 | TSP `9`, Endpoint ID (DCI) `20:16`, Slot ID `31:24` | 0 |
| Stop Endpoint | 15 | Endpoint ID (DCI) `20:16`, SP `23` (suspend), Slot ID `31:24` | 0 |
| Set TR Dequeue Ptr | 16 | Endpoint ID (DCI) `20:16`, Slot ID `31:24`; DW2: Stream ID `31:16` = 0 | New dequeue ptr, with SCT `3:1` = 0 and DCS `0` = the ring's current dequeue cycle - see below |

#### DCS is not a constant

Bit 0 of that pointer field is the Dequeue Cycle State, and it "identifies
the value of the xHC Consumer Cycle State (CCS) flag for the TRB referenced by
the TR Dequeue Pointer" (Table 6-67, p.455; 4.6.10 p.127 states the same
requirement in prose). It is a property of which lap the dequeue pointer is
on, not of the driver.

A ring that has wrapped an odd number of times is sitting on TRBs written with
the opposite cycle to the one it started with, so a hard-coded DCS is wrong
half the time, and wrong silently. Too low, the controller reads a live TD as
unproduced and stops at it; too high, it reads stale TRBs from the previous
lap as work and executes them. Neither raises an error.

Take the value from `XhciRingDequeueCycle()` and OR it into the address from
`XhciRingDequeuePA()`. Nothing else may compute it: those two functions are
what keep the software and hardware dequeue pointers from diverging.

### Event TRBs (spec 6.4.2)

Transfer Event (type 32):

| DW | Fields |
|---|---|
| 0/1 | TRB Pointer - physical address of the transfer TRB that completed (or Event Data value if ED = 1) |
| 2 | TRB Transfer Length `23:0` = residual bytes NOT transferred, Completion Code `31:24` |
| 3 | C `0`, ED `2`, Type `15:10` = 32, Endpoint ID (DCI) `20:16`, Slot ID `31:24` |

Actual bytes transferred is not "requested length minus residual" in general.
That form is correct only for a single-TRB TD. Table 6-39's own note (p.441):

> "For multi-TRB TDs, if ED = `0`, the TRB Transfer Length only reflects the
> number of bytes transferred for the buffer associated with the Transfer TRB
> pointed to by the Transfer Event, not the total bytes transferred for the
> TD."

The arithmetic that is right is spelled out at 4.10.1.1.2 (p.175):

> "If Event Data TRBs are not used, then the total number of received bytes for a
> Short Packet TD is the sum of the TRB Transfer Length fields in all Transfer
> TRBs up to and including the one that generated the Short Packet Event, minus
> the residue value of the TRB Transfer Length field in the Short Packet Event."

Table 6-38 gives an error event the same shape ("the difference between the
expected transfer size and the number of bytes successfully received"), so the
one formula covers both. `XhciRingSumTrbLengths` is that sum; never subtract a
residual from a transfer's total length.

What the field is (Table 6-38): for an OUT, "the value of the Length field of
the Transfer TRB, minus the data bytes that were successfully transmitted. A
successful OUT transfer shall return a Length of '0'"; for an IN, the same
against the TRB's declared size. It stops being a residual in two cases: "If
the Event Data flag is '1' or the Condition Code is Stopped - Short Packet, then
this field shall be set to the value of the Event Data Transfer Length
Accumulator (EDTLA)". Neither of those may have the residual arithmetic applied
to it, because the EDTLA is a running total of the bytes the TD has moved
rather than a count of the ones it has not.

It is still a real byte count. "The xHC maintains an internal 24-bit Event Data
Transfer Length Accumulator (EDTLA) for each endpoint", cleared "immediately
prior to executing the first Transfer TRB of a TD or when a Set TR Dequeue
Pointer Command is executed" and added to as each Transfer TRB completes
(4.11.5.2, p.209-210). None of that depends on software placing an Event Data
TRB; those TRBs report the accumulator, they do not create it. So a Stopped -
Short Packet event's length field is directly usable as "bytes transferred so
far", which is how `XhciXferQueueStopped` reads it. What nothing may do is
subtract it from a sum.

Always compute the length from the event rather than assuming the full request
completed, including when the completion code is Success. Several controller
families return code 1 for a transfer that delivered fewer bytes, with the
length field still correct (the spurious-success quirk in
`docs/usb-xhci-info/xhci-programming.md`). The rule is about any measurement,
whatever the code: an event that reported a byte count fixes the reported
length, and only a transfer where no event measured anything may have a
terminal Success read as "the whole request completed". That covers a Success
with a nonzero residual, and equally a Success with a zero residual on a
non-final TRB, which measures the bytes moved so far and is indistinguishable,
when it arrives, from a controller that stopped there.

#### What the TRB Pointer actually points at

Transcribed from Tables 6-37 and 6-39, spec 4.11.3.1 p.202 and 4.11.5.2 p.210,
because "the TRB that completed" is not the same thing as "the TD that
completed", and matching the two by equality rejects legitimate completions.

- Table 6-37: "the 64-bit address of the TRB that generated this event or 64
  bits of Event Data if the ED flag is `1`". With ED = 0 it is 16-byte aligned
  ("bits 0 through 3 of the address are `0`").
- ED (DW3 bit 2) is set only for an event generated by an Event Data TRB,
  which exists only if software placed one. This driver places none, so ED = 1
  is unexpected input, not a case to decode.
- Events are generated on IOC, on a short transfer where ISP is set, and on
  any error (4.11.3.1). The last two can name a TRB in the middle of a
  multi-TRB TD.
- "Several transfer related errors may be detected that cannot be attributed to
  a specific TRB, e.g. Ring Overrun, Ring Underrun ... In these cases, the xHC
  shall set the TRB Pointer to `0` and software shall treat it as invalid."
- One TD can raise several events: "while advancing to the end [of] the current
  TD after generating this event, each Transfer TRB encountered with its IOC
  flag set to `1` shall generate a Transfer Event", carrying the original
  event's Condition Code and length.
- An event is not TD completion unless it names the TD's last TRB (4.11.7
  p.214): "Software shall not interpret an error Event as indicating that the TD
  that it is associated with is `complete` (i.e. ownership of all the TRBs of
  the TD have been relinquished by the xHC), unless the TRB Pointer field of the
  error Transfer Event references the last TRB of the TD." The same page
  explains why a successful intermediate event is no better: software "may
  periodically set IOC flags in TRBs of a large TD so that it may update its
  Dequeue Pointer and reuse the TRBs that have been consumed by the xHC ...
  Unless an error is encountered, all the intermediate events shall report
  Success." Such an event says the controller passed that TRB, not that it
  finished the TD.
- "If any event generated by a TD reports an error, then that Completion Code
  overrides any Successful Completion Codes that other TRBs associated with the
  TD may have asserted, whether they come before or after the error Event."
- Codes 24-25 hand the command ring back to software; codes 26-28 do the same
  for a transfer ring. Stop Endpoint "transfer[s] ownership of all the TDs on
  the associated Transfer Ring to software" (4.11.4.8), which then places the
  dequeue pointer with Set TR Dequeue Pointer. A Command Ring Stopped event's
  pointer is a dequeue position, not a completed command. Neither
  completion-code family is valid for the other ring type.
- Missed Service Error (23) is not a recovery case. p.172: "If a Missed
  Service Error occurs on an intermediate TRB of a TD of an Isoch endpoint the
  xHC shall advance to the first TRB of the next TD or the Enqueue Pointer (i.e.
  Cycle bit transition), whichever is encountered first, when continuing
  execution on the Transfer Ring", the same sentence as the Short Packet rule.
  p.200-201: after one, "the xHC is required to advance through a Transfer Ring
  until it is `resynchronized` or the ring is exhausted", generating a Missed
  Service Error per skipped TD, and it "shall not drop Events associated with
  TRBs as it attempts to resynchronize".
- A halting error is the case where the hardware does stop: "the xHC shall
  stop on the TRB in error, the endpoint shall be halted, and software shall use
  a Set TR Dequeue Pointer Command to advance the Transfer Ring to the next TD"
  (p.172). The position software may then choose is constrained: "the xHC
  shall assume that the modified Dequeue Pointer references the first TRB of a
  TD". The halt itself is unconditional on position ("all Transfer Ring error
  conditions force the state of the associated endpoint to Halted and require
  system software intervention to recover", p.176), so an error on a TD's last
  TRB both completes the TD and halts the endpoint.
- TRB Error is the stated exception to that sentence, and it is not the isoch
  one. 4.8.3 p.149: "A TRB Error condition should cause a Running Endpoint to
  transition to the Error state. A Set TR Dequeue Pointer Command shall be used
  to transition the endpoint to the Stopped state."
  - Error and Halted are two of the five separately encoded EP States
    (Endpoint Context DW0 bits 2:0, below), and the recovery differs with
    them: a halt takes a Reset Endpoint first, an Error takes the Set TR
    Dequeue Pointer alone, because a Reset Endpoint "may only be issued to
    endpoints in the Halted state" (4.6.8 p.118).
  - The rule holds for every endpoint type, isoch included: what the next
    bullet exempts an isoch pipe from is halting, and 4.8.3's sentence is
    qualified by no type at all.
  - Same section, the other hard edge of that state field: "Software shall
    not write to the Doorbell register with the DB Target field value set to
    an endpoint that is in the Disabled state" (p.150).
- Isoch endpoints are the exception to the halting sentence, on the same page.
  "An isoch end point never halts because there is no handshake to report a
  halt condition ... an isoch pipe is not halted in an error case. If an error
  is detected, the xHC shall continue to process the data associated with the
  next ESIT of the transfer" (p.177); 4.10.2.8 p.184 repeats it for Data
  Transaction errors, and p.188 shows a USB Transaction Error on an Isoch IN
  leaving a pipe that "does not stall, but advances to the next Isoch TD in
  preparation for the next Interval".
  - So no error completion code asks for recovery on an isoch ring except
    TRB Error, per the bullet above: that one does not halt any endpoint
    type, so this exemption never covered it.
  - The stopped family (26-28) is a separate matter and reaches isoch rings
    like any other; that is software stopping the ring rather than the pipe
    halting itself, and software chooses the resume position afterwards.
- On a short packet mid-TD "the xHC shall advance to the first TRB of the next
  TD or the Enqueue Pointer ... whichever is encountered first" (p.210), so the
  rest of that TD is not executed. That is the controller advancing, which is
  not the same as software being free to reclaim: "software shall not interpret
  a Short Packet Event as indicating that the TD that it is associated with is
  `complete`, unless the TRB Pointer field of the Transfer Event references the
  last TRB of the TD" (p.175). Wait for the tail event.

  On a conforming controller the tail event does arrive, which is what makes
  that a terminating rule rather than a hang. The mechanism is the bullet above
  about one TD raising several events: "while advancing to the end [of] the
  current TD after generating this event, each Transfer TRB encountered with
  its IOC flag set to `1` shall generate a Transfer Event", carrying the
  original event's Condition Code and length (p.202). This driver sets IOC on
  the last TRB of every Normal TD and on a control transfer's Status Stage TRB,
  so there is always such a TRB to encounter.

  This matters for the length arithmetic: the tail event repeats the same residual against a different
  TRB, so a driver that recomputed from it would report the sum of the whole TD
  less that residual instead of the bytes that actually moved. The first
  measurement fixes the length (the general rule above, and
  `XHCI_XFER_FLAG_LENGTH_FIXED`).

  This driver departs from the "wait for the tail event" half of that rule.
  The next section records why, and what the departure looks like.

#### The withheld second Short Packet Event

4.10.1.1.2 p.175 states the requirement twice over. Software "shall not
interpret a Short Packet Event as indicating that the TD ... is `complete`,
unless the TRB Pointer field ... references the last TRB of the TD", and the
controller owes the event that makes waiting terminate: "If the Short Packet
occurred while processing a Transfer TRB with only an ISP flag set, then two
events shall be generated for the transfer; one for the Transfer TRB that the
Short Packet occurred on, and a second for the last TRB with the IOC flag set."

QEMU's xHC emits one. Batch 8-V.2 measured it with a passed-through ASIX
AX88772 on Windows 98: the only Transfer Event bulk IN ever produced was
`s 0x0d000476` (Short Packet, residual 1142 against a 1504-byte first TRB, so a
multi-TRB TD whose short packet landed on TRB 1), and no second event followed.
The TD was never retired, the receive never completed up to usbport, the vendor
driver never posted another one, and the endpoint was dead after a single
362-byte frame. That is a non-conformance in the controller, not in this
driver: the spec sentence above is unambiguous and was read from the PDF.

So this driver ends a whole-data TD on the short packet rather than waiting for
a tail that may never come. Two facts make that safe rather than merely
pragmatic, and both hold on a conforming controller as well:

- The second event carries no information the first did not. p.175 requires its
  length to "be set to the same value that was reported by the initial Short
  Packet Event", and the first measurement is already latched.
- The xHC has finished the TD before the first event is visible: on a short
  packet it "shall advance to the first TRB of the next TD or the Enqueue
  Pointer" (p.210). No TRB reclaimed is one the controller is still executing.

When the departure is taken is the second half of the rule. Retiring on the
short event itself would free the TD's TRBs while the promised tail may still
be sitting unread in the event ring. A Transfer Event names a TRB address, not
a generation, so once those TRBs are re-let the tail is indistinguishable from
the new TD's own event and completes it with the wrong length: a truncated bulk
IN reported as success. So the short event only defers. The transfer stays
queued and keeps its TRBs, and the retire happens at the end of a drain pass
that found the event ring empty (`XhciXferDrainSettled`, called from
`XhciEventDpc` only when that pass observed the ring empty). At that instant
every tail the controller had written has been consumed and matched, so a TD
still short is one whose tail was not sent.

The gate is the observation, not which exit the drain loop took. The loop tests
its bound before it dequeues, so a pass whose last consumed event is the
`XHCI_DPC_MAX_EVENTS`-th leaves without having asked the ring anything, and if
that event was also the controller's last, the ring is empty. Gating on the
exit would skip the settle there, and with the ring empty IPE never re-asserts
(4.17.5 p.270), so no later pass is guaranteed and the transfer would be
stranded for the life of the endpoint. `XhciEventDpc` therefore takes an
explicit `XhciEventRingPending` peek after a bounded exit and counts the
disagreement as `DrainBoundEmptyHits`.

Three consequences:

- A conforming controller never reaches the departure at all. Its tail is
  already queued behind the short event, is consumed in the same drain, lands on
  the transfer that is still queued, and ends it through the ordinary positional
  rule. `MidTdShortRetires` stays 0 there and `MidTdDeferralsTailed` carries
  the count, which is what turns a number both kinds of controller produce into
  a reading.
- A later TD's event answers a deferral too, and more strongly. The drain is
  FIFO, so a tail for an earlier TD would be ahead of any later TD's event; a
  deferred transfer swept by a later retire is one whose tail was never sent,
  and it is completed as the successful short transfer it is rather than failed
  as a dropped event.
- One window is not closed: the xHC may write the tail just after a pass has
  read the ring empty. Closing it needs event identity (Event Data TRBs, which
  this driver places none of), so it is bounded rather than removed.
  `MidTdShortTails` measures that residue.

Two limits are part of the rule:

- It applies only to a TD that is entirely data (a Normal TD: bulk or
  interrupt). A control transfer's Data Stage keeps the spec rule unchanged: its
  Status Stage TD still has to run, its TRB carries the group's only IOC (p.430),
  and the xHC "shall advance to the Status Stage TD" after a short packet
  (p.433), so the event that ends the transfer really is still coming.
- The decision does not belong to the ring layer, which cannot tell those two
  shapes apart. `XhciRingClassifyEvent`'s `CanRetire` stays positional; the
  departure has its own entry point, `XhciRingRetireAdvancedTd`, which accepts
  Short Packet on an endpoint ring and nothing else. Relaxing `CanRetire`
  instead is caught by the control-transfer vectors in `test_ring.c`.

Linux has always done this: `process_bulk_intr_td`'s `case COMP_SHORT_PACKET:`
falls through to `finish_td` whether or not the event named `td->end_trb`. In
the trust order of `docs/contributing/failure-diagnosis.md` that is #2
outranking #3, this file's reading of the spec.

Reading the counters. `XHCI_EXTENSION.MidTdShortRetiresTotal` counts how often
the departure fires. The conformance verdict is `MidTdDeferralsTailedTotal`
against `MidTdDeferralsTotal`:

    tailed == deferrals          -> conforming: every promised tail arrived
    retired > 0 with *both* the   -> one event only, which is QEMU's xHC
    tailed counters 0

`MidTdDeferralsTailedSpuriousTotal` is a third reading between those two: a
second event did arrive on the TD's last TRB but carried Success where p.175
requires Short Packet, a case Linux carries explicit handling for. It answers
the verdict's question with yes while failing the conformance test, so it is
counted apart rather than folded into either neighbour; folding it into `Lost`
would make a two-event controller indistinguishable from a teardown.

`MidTdDeferralsLostTotal` is neither verdict: a teardown, an abort, or a settle
whose retire the ring refused ended the observation. The two refusal counters
(`MidTdRefusedRetires`, `MidTdRefusedRetiresUnarmable`) are related but are not
a decomposition of it. They also count event-time divergences, which never
armed a deferral and so move them without moving `Lost`, and subtracting can
underflow. `MidTdDeferPending` is the fifth term; the five partition the
deferrals, and `XhciSlotDrainSettled` checks that identity at the one instant
it can be checked, setting the sticky `MidTdDeferAccountingBroken` if it does
not hold.

The unclosed window has its own measure: `MidTdShortTailsTotal` against
`MidTdShortRetiresTotal`. That is a reading at all only while the sticky
`MidTdVerdictVoided` is 0 and the derived `still outstanding` term is 0. The
gate is the sticky flag and not the two totals `MidTdTailsDroppedTotal` and
`MidTdTailsCensoredTotal`, because both totals are wrapping `ULONG`s and "== 0"
stops being proof after 2^32 of them (`src/xhci.h` says so where they are
declared).

`src/xhci_dispatch.c`'s print site requires `outstanding` 0 as the
other half, since a verdict taken while records are live in a queue reads a
tail that has not arrived yet as one the controller withheld. The two totals
still say what was dropped and why. A conforming controller answers in-band and
records no tail at all, so censoring at zero on such a machine is the check
that the deferral is working.

From the bulk IN measurement (runtime, both targets): the records retired by
the settle satisfy a four-state identity, `retired == tails + dropped +
censored + outstanding`, which closes to zero after the teardown fold on both
targets. `MidTdTailsDroppedTotal` is folded by `xhciDevFoldTailsDropped` at
both of its sites so the two cannot drift; a run in which the identity does not
close is a run whose dropped term has a mover the fold is not bracketing.

`DrainBoundHits` = 0 is structurally unreachable in this vehicle rather than a
negative result: the bounded exit needs `XHCI_DPC_MAX_EVENTS` =
`XHCI_EVENT_RING_TRBS * 4` = 1,024 events consumed inside one DPC pass, and no
device class this project can attach posts a thousand completions between
interrupts (the whole 2b leg produced 2,821 interrupts and 4,222 completions),
so `DrainBoundEmptyHits` stays theoretical.

The consequences for the driver are collected in
`docs/contributing/implementation-invariants.md`, "Completion Matching";
`XhciRingTdBounds`/`XhciRingRetireTd` implement them.

#### Other event types

Command Completion Event (type 33): DW0/1 = physical address of the
completed command TRB (match against your command ring); DW2: Command
Completion Parameter `23:0`, Completion Code `31:24`; DW3: C `0`, Type
`15:10` = 33, VF ID `23:16`, Slot ID `31:24` (this is where Enable Slot
returns the new Slot ID).

Port Status Change Event (type 34): DW0: Port ID `31:24` (1-based root
port); DW2: Completion Code `31:24`; DW3: C `0`, Type = 34. On receipt, read
that port's PORTSC, update the shadow, clear the change bits.

Doorbell Event (type 36): not expected in the normal path for this driver.
Log it if it appears; it usually means software rang a doorbell the controller
could not consume in the current state.

Host Controller Event (type 37): Completion Code in DW2 `31:24` reports
controller-level errors (e.g. Event Ring Full = 21). Log loudly.

### Completion Codes (spec 6.4.5, Table 6-90)

The full currently assigned range from Table 6-90 is retained here because the
event family is part of each code's contract. A numeric value from the wrong
family must not be allowed to change ownership of an unrelated ring.

| Code | Name | | Code | Name |
|---|---|---|---|---|
| 0 | Invalid | | 18 | Bandwidth Overrun (isoch) |
| 1 | Success | | 19 | Context State Error |
| 2 | Data Buffer Error | | 20 | No Ping Response |
| 3 | Babble Detected | | 21 | Event Ring Full (Host Controller Event) |
| 4 | USB Transaction Error | | 22 | Incompatible Device |
| 5 | TRB Error | | 23 | Missed Service (isoch) |
| 6 | Stall Error | | 24 | Command Ring Stopped (command only) |
| 7 | Resource Error | | 25 | Command Aborted (command only) |
| 8 | Bandwidth Error | | 26 | Stopped (transfer only) |
| 9 | No Slots Available | | 27 | Stopped - Length Invalid (transfer only) |
| 10 | Invalid Stream Type | | 28 | Stopped - Short Packet (transfer only) |
| 11 | Slot Not Enabled | | 29 | Max Exit Latency Too Large |
| 12 | Endpoint Not Enabled | | 30 | Reserved |
| 13 | Short Packet (success + residual) | | 31 | Isoch Buffer Overrun (isoch) |
| 14 | Ring Underrun (isoch; pointer invalid) | | 32 | Event Lost |
| 15 | Ring Overrun (isoch; pointer invalid) | | 33 | Undefined Error |
| 16 | VF Event Ring Full (Force Event command) | | 34 | Invalid Stream ID |
| 17 | Parameter Error | | 35 | Secondary Bandwidth Error |
|  |  | | 36 | Split Transaction Error |
| 192-223 | Vendor Defined Error; unknown means Undefined Error | | 224-255 | Vendor Defined Info; unknown means Success |

USBD_STATUS mapping for the common ones is in `docs/usb-xhci-info/xhci-programming.md`
"Completion Status Mapping". Codes 26-28 arrive after a Stop Endpoint command;
they identify where a transfer ring stopped, not an error. Codes 24-25 are
the separate command-ring stop/abort family.

## 8. Contexts (spec 6.2)

Byte offset of context index i = `i * ContextSize` (32 or 64 from
HCCPARAMS1.CSZ). Only the first 32 bytes carry defined fields either way; with
CSZ = 1 the upper 32 bytes are reserved. Zero the whole Input Context when it
is first allocated: "system software shall set all reserved register fields
to '0' when initially allocating the data structure" (5.1.1 note, p.338), and
4.5.2 p.84 says of the Input Slot Context that "all fields ... (including the
Reserved fields) shall be initialized to '0'". Output/Device Contexts are
initialized to zero once and then owned by the hardware; treat them as
read-only, RsvdO preserved.

Input Contexts are not RsvdZ throughout. Only the Input Control Context's own
padding is RsvdZ (p.424). The Slot and Endpoint Contexts inside an Input
Context are the same structures as the ones in a Device Context ("Slot Context
or Endpoint Contexts contained in an Input Context are also referred to as
`Input` Slot or Endpoint Contexts", p.51), so they carry the same RsvdO areas:
Slot bytes `10h-1Fh`, Endpoint bytes `14h-1Fh`, and with CSZ = 1 bytes 32-63 of
each (p.411, p.416). RsvdO is "reserved for exclusive xHC use, e.g. temporary
xHC workspace ... software shall not write this space" (p.338).

What the spec does not settle is re-use, and that is carried here as an open
question. p.51 says "after a command is complete, software may reuse or free
the Input Context data structure", so no conforming controller can depend on
that workspace surviving a completed command, which argues that re-zeroing a
reused block is harmless. But the spec never says whether reusing one fixed
allocation counts as initially allocating it (the p.338 note, zero every
reserved field) or as continued use of the same structure (the RsvdO
definition on the same page, do not write that space). The driver
currently zeroes the whole block on every reuse. Linux takes the conservative
side: zero once at allocation, then clear only defined fields. Do not change
this either way without settling that question.

### Device Context (spec 6.2.1) - OUTPUT, hardware-written

Index 0 = Slot Context, index i = Endpoint Context for DCI i (1..31).
DCBAA[SlotID] points here. 64-byte aligned.

### Input Context (spec 6.2.5) - INPUT to commands

Index 0 = Input Control Context, index 1 = Slot Context, index i+1 =
Endpoint Context for DCI i. (Everything is shifted by one relative to the
Device Context.)

Input Control Context (spec 6.2.5.1):

| DW | Fields |
|---|---|
| 0 | Drop Context flags D2-D31 (bits `31:2`; bits 0-1 RsvdZ) - contexts to disable |
| 1 | Add Context flags A0-A31 (bit i = context DCI i) - contexts to evaluate/enable |
| 7 | Configuration Value `7:0`, Interface Number `15:8`, Alternate Setting `23:16` (only if HCCPARAMS1.CFC = 1; otherwise RsvdZ - leave 0) |

Usage: Address Device sets A0 + A1 (slot + EP0). Configure Endpoint sets A0
plus one A-bit per endpoint being added and D-bits for endpoints being
dropped. A0 is mandatory on it ("A0 shall be set to '1'", 4.6.6 p.104), and A0
alone, with no endpoint flag, is a valid command that changes only the Slot
Context (p.106: an endpoint with neither flag is one the xHC does nothing to).
Evaluate Context sets only the A-bits of contexts being changed (e.g. A1 to
update EP0 Max Packet Size).

Which A-bit is set is not the same question as which fields the command then
looks at; see "Which command may set which Slot Context field" below, where an
Evaluate Context flagging the Slot Context turns out to consider two fields and
ignore the rest.

### Slot Context (spec 6.2.2, Table 6-4..6-7)

| DW | Bits | Field | Notes |
|---|---|---|---|
| 0 | 19:0 | Route String | 0 for root-port devices; 4 bits per hub tier below the root |
| 0 | 23:20 | Speed | Same encoding as PORTSC Port Speed (1 = FS, 2 = LS, 3 = HS). Revision 1.2 called it deprecated and reserved; 1.2c says only "not applicable to USB3 Gen X" (Table 6-4 p.408). Every device here is USB 2.0 and 1.0/1.1-era controllers require it - always set it |
| 0 | 25 | MTT | Multi-TT: 1 if device is (or hangs off) a multi-TT hub interface. "Interface" means the currently enabled alternate setting, not the hardware's capability (Table 6-4): a multi-TT-capable hub running its single-TT alternate is MTT = 0, so this follows SET_INTERFACE and is not decided once at enumeration. Table 6-4 states that qualifier only in its hub clause; its child clause ("a Low-/Full-speed device or Full-speed hub ... connected ... through a parent High-speed hub that supports Multiple TTs") says merely "supports". This driver applies the enabled-interface reading to both, because a hub running its single-TT alternate really does route every downstream port through one translator, and because Linux does the same (`tt->multi` is set when the alternate setting is selected). This is the driver's reading, not a transcription. The two causes are independent and OR together: a Full-Speed hub behind a multi-TT High-Speed hub carries MTT for the child reason and would carry it for the hub reason if it were High-Speed |
| 0 | 26 | Hub | 1 if this device is a hub |
| 0 | 31:27 | Context Entries | Index of the last valid Endpoint Context (= highest DCI in use; 1 during Address Device) |
| 1 | 15:0 | Max Exit Latency | 0 for our use |
| 1 | 23:16 | Root Hub Port Number | 1-based root port the device path starts at |
| 1 | 31:24 | Number of Ports | Only if Hub = 1: the hub's port count |
| 2 | 7:0 | Parent (TT) Hub Slot ID | Only for FS/LS device below a HS hub: slot ID of that hub. `0` if the device is on a root-hub port or is itself High-Speed (Table 6-6) |
| 2 | 15:8 | Parent (TT) Port Number | Port on that hub the device is behind; same two `0` conditions as the field above (Table 6-6) |
| 2 | 17:16 | TTT | TT Think Time (from hub descriptor `wHubCharacteristics` bits 6:5). This field belongs to the hub's own Slot Context, not its children's. Table 6-6 p.409: set "if this is a High-speed hub (Hub = '1' and Speed = High-Speed)"; "if this device is not a High-speed hub (Hub = '0' or Speed != High-speed), then this field shall be '0'". So an FS/LS device behind a TT has TTT = 0; what it inherits from the hub is MTT (DW0 bit 25), not TTT |
| 2 | 31:22 | Interrupter Target | 0 (single interrupter) |
| 3 | 7:0 | USB Device Address | OUTPUT only - the address the xHC assigned; read after Address Device |
| 3 | 31:27 | Slot State | OUTPUT only: 0 Disabled/Enabled, 1 Default, 2 Addressed, 3 Configured |

### Endpoint Context (spec 6.2.3, Tables 6-8..6-11)

| DW | Bits | Field | Notes |
|---|---|---|---|
| 0 | 2:0 | EP State | OUTPUT: 0 Disabled, 1 Running, 2 Halted, 3 Stopped, 4 Error. Input: write 0 |
| 0 | 9:8 | Mult | 0 for all USB2 endpoints |
| 0 | 14:10 | MaxPStreams | 0 (no streams) |
| 0 | 15 | LSA | 0 |
| 0 | 23:16 | Interval | Period = 2^Interval * 125 us. See conversion table below |
| 1 | 2:1 | CErr | Error count; use 3 for control/bulk/interrupt, must be 0 for isoch |
| 1 | 5:3 | EP Type | 0 invalid, 1 Isoch OUT, 2 Bulk OUT, 3 Interrupt OUT, 4 Control, 5 Isoch IN, 6 Bulk IN, 7 Interrupt IN |
| 1 | 15:8 | Max Burst Size | Not SuperSpeed-only: "For all Low-/Full-Speed endpoints this field shall be cleared to '0'. For High-Speed control and bulk endpoints this field shall be cleared to '0'. For High-Speed isochronous and interrupt endpoints this field shall be set to the number of additional transaction opportunities per microframe, i.e. the value defined in bits 12:11 of the USB2 Endpoint Descriptor wMaxPacketSize field" (6.2.3.4 p.418). usbport hands that count over as `TransactionPerMicroframe`, so the field is the count minus one |
| 1 | 31:16 | Max Packet Size | From endpoint descriptor `wMaxPacketSize` bits 10:0 |
| 2 | 0 | DCS | Dequeue Cycle State = 1 for a fresh ring |
| 2/3 | 63:4 | TR Dequeue Pointer | Physical address of the endpoint's transfer ring (16-byte aligned) |
| 4 | 15:0 | Average TRB Length | Must be > 0. Use 8 for EP0/control; otherwise a typical transfer size estimate (e.g. Max Packet Size) |
| 4 | 31:16 | Max ESIT Payload Lo | Periodic only: Max Packet Size * (Max Burst + 1); 0 for control/bulk |

What Address Device considers a valid Slot Context (spec 6.2.2.1 p.411),
transcribed because the TT half is easy to get backwards: Route String valid,
Speed identifying the device, Context Entries = 1, Root Hub Port Number between
1 and MaxPorts, Interrupter Target valid, and "if the device is LS/FS and
connected through a HS hub, then the Parent Hub Slot ID field references a
Device Slot that is assigned to the HS hub, the MTT field indicates whether the
HS hub supports Multi-TTs, and the Parent Port Number field indicates the
correct Parent Port Number on the HS hub, else these fields are cleared to '0',
... and all other fields are cleared to '0'".

Note what is not in that list: TTT. A child behind a TT carries Parent Hub Slot
ID, Parent Port Number and MTT; TTT is set on the hub's own slot and is 0 on
the child's (Table 6-6, quoted in the DW2 row above).

#### Which command may set which Slot Context field

The Slot Context is one structure, but the three commands that take one do not
consider the same fields, and the differences are stated only in these three
sub-sections of the spec.

| Command | What the Input Slot Context must carry, and what the xHC uses | Source |
|---|---|---|
| Address Device | Route String, Speed, Context Entries = 1, Root Hub Port Number, the TT triple (Parent Hub Slot ID / Parent Port Number / MTT) for an LS/FS device behind an HS hub, Interrupter Target, "and all other fields are cleared to '0'". Every field of the Output Slot Context is overwritten. | 6.2.2.1 p.411-412 |
| Configure Endpoint | Context Entries for the target configuration, the Hub field, Number of Ports when Hub = 1 (else 0), and TTT + MTT when Hub = 1 and Speed = High-Speed. The Output Hub / Number of Ports / TTT / MTT are initialized from them. | 6.2.2.2 p.412 |
| Evaluate Context | Interrupter Target and Max Exit Latency, and "Only these fields shall be evaluated when the xHC receives an Evaluate Context Command that flags the Slot Context"; "Only the Output Interrupter Target and Max Exit Latency fields are updated". | 6.2.2.3 p.412 |

Three consequences this driver depends on:

- A hub's `Hub` / `Number of Ports` / `TTT` / `MTT` can only be programmed by a
  Configure Endpoint. An Evaluate Context naming the Slot Context ignores them
  silently: it does not fail, it evaluates the two fields it is defined for.
- An Address Device un-marks a hub, because its own validity list requires Hub
  cleared to 0 and "all fields of the Output Slot Context are overwritten by
  the xHC" (6.2.2.1 p.412). Any re-enumeration of a hub therefore has to be
  followed by a fresh marking.
- A Configure Endpoint issued for an endpoint carries the hub fields too, so
  one built without them will clear a marking rather than leave it alone. A0
  is mandatory on that command ("A0 shall be set to '1'", 4.6.6 p.104), so there
  is no way to issue one that leaves the Slot Context untouched.

A Configure Endpoint with A0 and no endpoint flags at all is the command that
marking uses, and it is well-defined rather than a trick: 4.6.6 p.106 says that
for each endpoint "If the Drop Context flag is '0' and the Add Context flag is
'0', the xHC shall: Do nothing. The respective Input Endpoint Context is
ignored by the xHC." The Stopped-or-idle precondition on p.104 applies to an
endpoint "if its Drop Context flag is set", so such a command needs nothing
quiesced and may be issued with the device's pipes running.

#### Which Slot State each command requires

The Slot State is the Output Slot Context field at DW3 `31:27` above (0
Disabled/Enabled, 1 Default, 2 Addressed, 3 Configured). Every row below is the
spec's own pseudo-code for the command, and the `else` branch of each is the
same sentence: `Completion Code = Context State Error`.

| Command | Slot States it may be issued against | Source |
|---|---|---|
| Address Device, BSR = 1 | Enabled only. "If the slot is in the Enabled state: ... Set the Slot State in the Output Slot Context to Default ... else // The slot is not in the Enabled state: Completion Code = Context State Error." | 4.6.5 p.101 |
| Address Device, BSR = 0 | Enabled or Default. "If the slot is in the Enabled or Default state: ... Set the Slot State in the Output Slot Context to Addressed ... else // The slot is not in the Enabled or Default state: Completion Code = Context State Error." | 4.6.5 p.101 |
| Reset Device | Addressed or Configured. "If the Device Slot is in the Addressed or Configured state: ... Set the Slot State field of Slot Context to the Default state ... else // The Device Slot was not in the Addressed or Configured state: Completion Code = Context State Error." | 4.6.11 p.130 |
| Configure Endpoint | Addressed or Configured. | 6.2.2.2 p.412 |
| Evaluate Context | Default, Addressed or Configured. | 4.6.7 p.113 |
| Reset Endpoint | Default, Addressed or Configured (and the endpoint itself Halted). | 4.6.8 p.115 |
| Stop Endpoint | Default, Addressed or Configured. | 4.6.9 p.119 |
| Set TR Dequeue Pointer | Default, Addressed or Configured (and the endpoint Stopped or Error). | 4.6.10 p.126 |

Both halves of the Address Device row matter, and they are not the same list:

- BSR = 1 is legal from `Enabled` and nothing else. Not Default, not Addressed,
  not Configured. So a re-enumeration that keeps its slot cannot simply
  re-issue the BSR form to get back to Default; on a conforming xHC that
  answers Context State Error. (QEMU does not enforce the check, so a VM run
  will not catch a wrong list.)
- Reset Device is the command that closes that gap, and its own list is the
  complement: Addressed or Configured. It "sets the Slot State field to the
  Default state and the USB Device Address field to '0'" and "disables all
  endpoints of the slot except for the Default Control Endpoint by setting the
  Endpoint Context EP State field to Disabled in all enabled Endpoint Contexts"
  (4.6.11 p.129); the executed command also sets "the Context Entries field
  of Slot Context to '1'" (p.130). For EP0 the xHC shall "terminate any USB
  activity, abort any pending events not already posted to an Event Ring, and
  transition the endpoint to the Running state".
- So the three cases partition cleanly, and a driver re-entering enumeration on a
  kept slot has to branch on the Output Slot State it reads:

  | Output Slot State | What is owed to reach Default with EP0 Running |
  |---|---|
  | 0 (Disabled/Enabled) | Address Device with BSR = 1 |
  | 1 (Default) | nothing - the slot is already there |
  | 2 (Addressed) or 3 (Configured) | Reset Device |

- Software owes the endpoint bookkeeping either way. "Software is responsible
  for recovering any memory data structures (Stream Context Arrays, Transfer
  Rings, etc.) owned by disabled Endpoint Contexts the slot when the Reset
  Device Command is issued" (4.6.11 p.130). The same is true after an Address
  Device, which rewrites Context Entries to 1; a driver that leaves its own
  endpoint records "configured" then rings doorbells for DCIs the Slot Context
  no longer claims.
- One caution the spec states outright: "Undefined behavior may occur if this
  command is executed and the device associated with it is not successfully
  reset. E.g. if the USB device is not in the Default state, then a subsequent
  Address Device Command shall fail" (4.6.11 p.129). Reset Device informs the
  xHC that the port reset already happened; it does not perform one.

#### Route String tier order

This is the one field in this document whose layout the local spec PDF does
not define. Table 6-4 gives the field's position, DW0 `19:0`, and then defers
its format: "The format of the Route String is defined in section 8.9 the USB3
specification." That document is not in `docs/references/`, so the nibble
order below is not transcribed from a specification this repository holds.

What the local PDF does settle, quoted rather than inferred:

- The root port is not part of it. Section 4.3.3 footnote 8: "Note that the
  Route String does not include the Root Hub Port Number... e.g. To access a
  device attached directly to a Root Hub port, the Route String shall equal
  '0', and the Root Hub Port Number shall indicate the specific Root Hub port
  to use."
- A wide hub clamps rather than wraps. Table 6-4 footnote 106: "If HS or FS
  hub in the path supports more than 14 ports the associated Route String Port
  field shall be set to 15."

The nibble order was taken from two independent implementations mirrored under
`external/`, which agree:

| Source | Construction |
|---|---|
| FreeBSD `xhci.c` `xhci_configure_device` | walks device -> root, `route \|= port << (4 * (depth - 1))` where the parent hub's `depth` is 1 for a hub on a root port |
| Haiku `xhci.cpp` `ConfigureDevice` | walks device -> root, `route = route << 4; route \|= port`, so the last port OR-ed (the root-most) lands in bits 3:0 |

So, in this driver's vocabulary (`src/xhci_topo.h`, where tier 0 is a hub
attached to a root port):

- a device on a root port has Route String 0;
- a device attached to a tier-`T` hub contributes its port number at bits
  `4T+3 : 4T`, so the hub nearest the root hub owns the low nibble and each
  further tier moves up one;
- five nibbles fit, so a path deeper than five hub tiers has no representable
  route and must be refused, not truncated: a truncated route names a
  different, real device.

Those two implementations are no longer what this rests on. The batch 7b-A
runs had the controller decode this driver's own route strings and resolve
each slot to the device physically there, on both usbport builds:
`usb_xhci_slot_address slotid 3, port 2.1` on 2a, and on 2b, under Driver
Verifier with Force IRQL checking, every slot including `slotid 5 -> 2.2.1`,
two tiers deep. A second 2b boot rebuilt the graph identically
(`docs/contributing/design/02-hub-topology-route-string.md` section 4). An xHC
that walks the route to the right physical port outranks any pair of
implementations agreeing, so the table above is kept as the record of where
the order came from, not as the evidence that it is right.

Still true, and the reason this section exists at all: the order is not
transcribed from a specification this repository holds. If a controller ever
disagrees, the USB3 specification is the tie-break to obtain rather than a
quirk to work around.

DCI math (spec 4.5.1): EP0 = DCI 1; endpoint n OUT = DCI 2n; endpoint n
IN = DCI 2n+1. Example: EP1 IN (0x81) = DCI 3; EP2 OUT (0x02) = DCI 4.

Interval conversion (spec 6.2.3.6, Table 6-12) from the USB endpoint
descriptor `bInterval`:

| Endpoint | bInterval means | Endpoint Context Interval | Valid range |
|---|---|---|---|
| HS interrupt/isoch | period = 2^(bInterval-1) microframes | `bInterval - 1` | 0-15 |
| FS isoch | period = 2^(bInterval-1) ms | `bInterval + 2` | 3-18 |
| FS/LS interrupt | period = bInterval ms (1-255) | `floor(log2(bInterval)) + 3` (round *down* to a power of 2) | 3-10 |
| HS bulk/control | max NAK rate | 0 | - |

EP0 Max Packet Size by speed (spec 4.3): LS = 8, HS = 64. FS = 8/16/32/64,
unknowable before the descriptor is read. Assume one; then, when usbport reads
the device descriptor and the real `bMaxPacketSize0` differs, issue Evaluate
Context with A1 set and the corrected Max Packet Size in the EP0 context.

The FS assumption this driver makes is 64, not spec 4.3's 8. Bench run 13-E
found the 8 failing on real silicon. The field bounds what the controller will
accept, and the two directions are not symmetric: declared larger than actual
costs a short packet, declared smaller is babble on the first packet that
exceeds it. Spec 4.3's advice assumes software fetches 8 descriptor bytes
first.

Linux's usbcore does; usbport does not. It issues
`GET_DESCRIPTOR(DEVICE)` with `wLength` = 0x40 at address 0, before SET_ADDRESS
and before any correction can land, and on real xHCI silicon that made every
Full-Speed device with `bMaxPacketSize0` != 8 fail to enumerate at all. Linux
assumes 64 for a Full-Speed control endpoint for the same reason and corrects
afterwards. See `docs/contributing/runs/run-13e.md`, "Session record - bench
session 1".

### Event Ring Segment Table entry (spec 6.5)

16 bytes: DW0/1 = segment base (64-byte aligned), DW2 = segment size in TRBs
`15:0` (16-4096; use 256), DW3 = RsvdZ.

## 9. C89 skeleton guidance for `src/xhci.h`

Rules that keep MSVC 6.0 and the hardware honest:

- No C bitfields for hardware structures. Bitfield layout is
  implementation-defined; use ULONG words + shift/mask macros exclusively.
- A TRB is four ULONGs; use helper macros for fields:

```c
typedef struct _XHCI_TRB {
    ULONG Param0;   /* DW0 - parameter lo / immediate data       */
    ULONG Param1;   /* DW1 - parameter hi (always 0 for us)      */
    ULONG Status;   /* DW2 - length / interrupter / completion   */
    ULONG Control;  /* DW3 - cycle, type, per-type flags         */
} XHCI_TRB, *PXHCI_TRB;      /* sizeof == 16 - ASSERT this        */

#define TRB_CYCLE               0x00000001UL
#define TRB_TYPE(t)             (((ULONG)(t) & 0x3F) << 10)
#define TRB_GET_TYPE(dw3)       (((dw3) >> 10) & 0x3F)
#define TRB_IOC                 (1UL << 5)
#define TRB_IDT                 (1UL << 6)
#define TRB_CH                  (1UL << 4)
#define TRB_ISP                 (1UL << 2)
#define TRB_LINK_TC             (1UL << 1)
#define TRB_DIR_IN              (1UL << 16)
#define TRB_BSR                 (1UL << 9)
#define TRB_SLOT_ID(s)          (((ULONG)(s) & 0xFF) << 24)
#define TRB_GET_SLOT_ID(dw3)    (((dw3) >> 24) & 0xFF)
#define TRB_EP_ID(dci)          (((ULONG)(dci) & 0x1F) << 16)
#define TRB_GET_EP_ID(dw3)      (((dw3) >> 16) & 0x1F)
#define TRB_GET_COMPLETION(dw2) (((dw2) >> 24) & 0xFF)
#define TRB_GET_RESIDUAL(dw2)   ((dw2) & 0x00FFFFFFUL)
```

- Contexts must not be fixed-size structs (CSZ varies). Address them as ULONG
  arrays via the stride:

```c
#define XHCI_CTX(base, index, ctxSize) \
    ((PULONG)((PUCHAR)(base) + (index) * (ctxSize)))
```

- 64-bit MMIO registers (CRCR, DCBAAP, ERSTBA, ERDP): two 32-bit writes,
  low DWORD first, then high (high = 0). The spec permits 32-bit access;
  do not attempt 64-bit stores on a 32-bit OS.
- All ring/TRB memory writes must hit memory before the doorbell: use
  `WRITE_REGISTER_ULONG` for the doorbell (it serializes on x86) and write the
  first TRB's cycle bit last (already documented in `docs/usb-xhci-info/xhci-programming.md`).
- Compile-time layout checks C89-style:
  `typedef char ASSERT_TRB_SIZE[(sizeof(XHCI_TRB) == 16) ? 1 : -1];`

Cross-references: initialization order in `docs/usb-xhci-info/xhci-programming.md`; rules
that must survive refactoring in `docs/contributing/implementation-invariants.md`; per-chip
deviations this driver acts on in `docs/usb-xhci-info/xhci-programming.md`.
