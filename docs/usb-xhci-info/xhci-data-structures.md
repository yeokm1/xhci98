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

Sections 10 and 11 (SuperSpeed and SuperSpeedPlus, Phase 29's task 29-0; the
USB 3.2 hub class, Phase 30's task 30-0) were drafted without the PDF and
then verified row by row against xHCI 1.2c and against the USB 3.2
specification, revision 1.1, which section 10's provenance paragraph
identifies; their USB 3.2 citations are `USB 3.2 p.N`. Since Phase 29 the driver
manages USB3 protocol ports too, so the remarks in sections 3 and 7 that every
managed port is a USB2 protocol port describe the driver before that phase.

Conventions:

- `DW0..DW3` are the four little-endian 32-bit words of a 16-byte TRB or a context row.
- Bit ranges are `high:low`, inclusive.
- "RW1C" = write 1 to clear; writing 0 has no effect.
- "RsvdZ" = reserved, software must write 0. "RsvdP" = reserved, software must preserve (read-modify-write).
- Every "Hi" DWORD of a 64-bit pointer field is written as 0, but it must still be written. That holds in the amd64 build too, and it is a measurement rather than an assumption: NT 5.2's `usbport.sys` creates its DMA adapter 32-bit on amd64 as well (`Dma32BitAddresses = 1`, `DmaWidth = Width32Bits`; design record 11, M5), so no address it hands this driver is above 4 GB, and the scatter-gather high-DWORD check refuses one that is.

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
  - QEMU's model has no HCCPARAMS2 case at all and its capability reads default to 0, so FSC reads 0 there. That one is measured. The fleet is measured in part. `xhciqual` reads and prints HCCPARAMS2 and decodes FSC (`xhciqual/xhcicap.c`, `xhciqual/report.c`), and the E460 logs of 2026-08-22 carry it: `HCCPARAMS2 00000000`, `fsc=0` in the `FACT` line, corroborated at stage E0 of `runs/run-13e.md`. What predates the change is the two 2026-07-25 sets, the earlier E460 one and the P14s one, which establish HCIVERSION and CAPLENGTH and nothing about their FSC bit. Closing that half needs a fresh bare-metal run on the P14s, not a code change. Do not infer one controller's answer from another's.

## 3. Operational Registers (BAR0 + CAPLENGTH, spec 5.4)

| Offset | Register | Bits used by this driver |
|---|---|---|
| +0x00 | USBCMD | R/S `0`, HCRST `1`, INTE `2`, HSEE `3`, RsvdP `6:4`, LHCRST `7`, CSS `8`, CRS `9`, EWE `10`, EU3S `11`, then the 1.1/1.2 additions CME `13`, ETE `14`, TSC_EN `15` and VTIOE `16` - defined bits this driver never enables and always writes as zero, which is why `XHCI_USBCMD_DEFINED_MASK` in `src/xhci.h` covers `16:0` and not just `11:0` |
| +0x04 | USBSTS | HCH `0` (RO), HSE `2` (RW1C), EINT `3` (RW1C), PCD `4` (RW1C), SSS `8`, RSS `9`, SRE `10` (RW1C), CNR `11` (RO), HCE `12` (RO) |
| +0x08 | PAGESIZE | Bit n set => page size 2^(n+12). Bit 0 = 4 KB (the normal case) |
| +0x14 | DNCTRL | Notification Enable N0-N15 `15:0`, RsvdP `31:16` (Table 5-23 p.366). Write 0x0002 (enable FUNCTION_WAKE only, spec 5.4.4 Table 5-23 note; Function Wake is 4.13.2) or 0 |
| +0x18 | CRCR (64-bit) | RCS `0`, CS `1` (RW1S), CA `2` (RW1S), CRR `3` (RO), RsvdP `5:4`, Command Ring Pointer `63:6` (Table 5-24 p.367-368) |
| +0x30 | DCBAAP (64-bit) | Pointer `63:6`, low 6 bits RsvdZ - write 0, do not preserve (Table 5-25 p.369) |
| +0x38 | CONFIG | MaxSlotsEn `7:0`, U3E `8`, CIE `9`, SOC `10` (RW, new in revision 1.2c; RsvdP in 1.2), RsvdP `31:11` (Table 5-26 p.370). This driver never sets SOC and its read-modify-write carries whatever it read, so the bit's promotion changed nothing - and the write behaviour is the same either way, which is why `xhci-programming.md`'s step 6 can call `31:10` RsvdP without any consequence following from the difference |
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
LWS + the RsvdZ bits (`2` and `29:28`, which `XHCI_PORTSC_UNSAFE_MASK`
includes), then OR in the one bit being changed. After PP 0->1, wait 20 ms
before touching the port (spec 5.4.8 note).

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

#### ISR/DPC rules

- ISR: read USBSTS; if EINT = 0 the interrupt is not ours. Clear EINT (write 1), then clear IMAN.IP (write IMAN with IP = 1). Both are RW1C. EINT is a summary of IP 0->1 transitions, not the INTx line source; IMAN.IP holds INTx asserted. Acknowledge this pair once and defer Event Ring work. Do not copy a generic PCI ISR's status-drain loop into the xHCI path.
  - This driver's ISR writes IE as 0, not as 1. That costs no delivery: the xHC sets EHB when it sets IP and cannot set IP again while EHB is set, so no interrupt can be generated in the window the ISR opens whatever IE holds. What it buys is that the ISR, which runs at DIRQL and cannot take the miniport's DISPATCH-level controller lock, moves IE in the same direction every masking path does, so it can never re-publish an enable a concurrent mask has just cleared. IE is re-raised only by the DPC's re-arm, under the lock and only while usbport still wants interrupts. See `docs/contributing/implementation-invariants.md`, "Interrupt Ordering", and `src/xhci_evt.c` (`XhciIsr`).
- DPC: after draining events, write ERDP = (current dequeue physical address) | EHB(bit 3) to clear Event Handler Busy. EHB gating is architectural, not controller-specific: the xHC sets EHB = 1 whenever it sets IP, and IP shall not be set again while EHB = 1 (spec 4.17.2 interrupt-assertion conditions, 4.17.5 IP rules, 5.5.2.3.3 EHB field). A drain in progress therefore cannot be re-interrupted by its own interrupter, and forgetting the final EHB = 1 write silences the interrupter permanently.
- During a long drain, also write ERDP periodically (e.g. every 32 events), not only at the end: the xHC detects a full event ring from the software-advertised dequeue pointer, so a stale ERDP during an event burst causes Event Ring Full (completion code 21) even though the DPC is consuming events. EHB is RW1C, so put 0 in bit 3 on these intermediate writes. That preserves EHB = 1, keeping interrupts suppressed mid-drain; writing 1 would clear it. Only the final write after the ring is empty carries bit 3 = 1.
- With PCI pin-based interrupts (this driver's only mode), the INTx line stays asserted while IMAN.IP = 1 (spec 5.5.2.1). It is a level-triggered line, so clearing IP in the ISR is mandatory or the machine hangs in an interrupt storm.
- IMOD: IMODI 15:0 in 250 ns units, IMODC 31:16 a counter, both RW, reset IMODI 4000 = 1 ms (Table 5-39, p.392). Since roadmap task 23.4 every start writes IMODI from the `XhciImodInterval250ns` registry value at the end of the event-ring programming, before R/S: 10 to 4000 as given, anything else replaced by 4000; the INFs write 500 (125 us), which read at nearly twice the Bulk-Only throughput of 4000 on bare metal - 500 was not on task 23.3's ladder, and the reading is task 23.5's silent pass, which fills 23.3's missing rung against 23.3's own 4000 control. Until then the start never wrote it and ran at the reset value. The other write is the Save/Restore resume path, which writes back the value read at the save (the restore list below requires IMOD to be written before CRS). It wrote 0 there until the 2026-09-05 audit's F10, silently removing the moderation the isochronous builder's IOC-per-TD policy relies on after every successful restore. QEMU stores IMOD and reads it back but never consults it, and resets it to 0 rather than 4000 (task 23.3).

## 5. Doorbell Registers (BAR0 + DBOFF, spec 5.6)

Each doorbell is one 32-bit register: `DB Target 7:0`, RsvdZ `15:8`, `DB Stream ID 31:16`.

| Register | Write value | Meaning |
|---|---|---|
| DB[0] | 0 | Host controller doorbell: run the command ring |
| DB[SlotID] | DCI (1..31) | Ring endpoint DCI of that slot; stream bits 0 on an endpoint without streams. On an endpoint with streams open, `DCI + (StreamID << 16)`, one doorbell per stream: see "Streams" in section 8 |

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
- DW1 (offset +4, USBLEGCTLSTS, spec 7.1.2, Table 7-5): the SMI enables are five bits, not the whole low half - bit 0 = USB SMI Enable, bit 4 = SMI on Host System Error Enable, bit 13 = SMI on OS Ownership Enable, bit 14 = SMI on PCI Command Enable, bit 15 = SMI on BAR Enable (mask `0x0000E011`). Bits `3:1`, `12:5` and `19:17` are RsvdP and must be written back as read (mask `0x000E1FEE`); bit 16 (SMI on Event Interrupt) and bit 20 (SMI on Host System Error) are read-only status; `28:21` are RsvdZ; `31:29` are RW1C status (bit 29 = SMI on OS Ownership Change, 30 = SMI on PCI Command, 31 = SMI on BAR). The handoff's write used a blanket `0xFFFF` enable mask and zeroed the RsvdP fields until the 2026-09-05 audit's F13.

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

#### TD composition and Link placement

Spec 4.11.7, p.212; Link TRB notes p.208; CH field description p.464,
transcribed because a TD that spans the wrap-back Link TRB has to obey all of
it:

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
| Set TR Dequeue Ptr | 16 | Endpoint ID (DCI) `20:16`, Slot ID `31:24`; DW2: Stream ID `31:16` = 0 on an endpoint without streams | New dequeue ptr, with SCT `3:1` = 0 and DCS `0` = the ring's current dequeue cycle - see below. On an endpoint with streams open, Stream ID names the stream and SCT is 1 (Primary TR): see "Streams" in section 8 |

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
that found the event ring empty (`XhciXferDrainSettled`, reached from
`XhciEventDpc` only when that pass observed the ring empty - one level down,
through `XhciSlotDrainSettled`, which is what the DPC actually calls). At that instant
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
| 7 | Configuration Value `7:0`, Interface Number `15:8`, Alternate Setting `23:16`. These are valid only when HCCPARAMS2.CIC = 1 **and** CONFIG.CIE is set; otherwise RsvdZ - leave 0. (CIC is Configuration Information Capability, HCCPARAMS2 bit 5, listed in Appendix H.1.8 as one of the bits that became required at xHCI 1.1. It is NOT HCCPARAMS1.CFC, which is Contiguous Frame ID and is described in section 5.) This driver never writes DW7 and never sets CONFIG.CIE, so it leaves the whole doubleword zero |

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
| 0 | 14:10 | MaxPStreams | 0 (no streams); with streams open, 1..15 and the array is 2^(MaxPStreams+1) entries - see "Streams" below |
| 0 | 15 | LSA | 0; 1 with streams open (Linear Stream Array: primary streams only) |
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

### Streams (spec 4.12, 5.3.6, 5.6, 6.2.3, 6.2.4, 6.4.2.1, 6.4.3.9) - task 31-0's static notes

Written for roadmap task 31-A.1, primary streams in the bus (`src/xhci_stream.h`,
`src/hcd_cfg.c`; the private request a class driver opens them with is
`src/xhci98_streams.h`). **Unlike the rest of this file, the rows below were
not transcribed from the local PDF, which was not at hand when they were
written**; they are the reading Linux's `xhci-mem.c` and `xhci.c` (interface
documentation, `external/README.md`) agree with, and every row marked "to
verify" is the first thing a reader with the PDF checks. Nothing here has been
observed on a controller: QEMU's model streams, but this tree enumerates no
SuperSpeed device until Phase 29's work lands, so the path has run nowhere.

Only primary streams with a Linear Stream Array are used: no Secondary Stream
Arrays (HCCPARAMS1.NSS is not consulted), a Stream ID is an index into the one
Primary Stream Context Array, and stream rings are one segment each.

| Item | Rule | Spec | Status |
|---|---|---|---|
| Controller support | HCCPARAMS1 MaxPSASize `15:12`: the largest Primary Stream Context Array is 2^(MaxPSASize+1) entries; "a value of '0' indicates that Streams are not supported" | 5.3.6 | to verify (wording); the field position is section 2's row |
| Device support | SuperSpeed Endpoint Companion (`bDescriptorType` 0x30, 6 bytes) immediately after the bulk endpoint descriptor; `bmAttributes 4:0` MaxStreams, 0 none, 1..16 meaning 2^MaxStreams streams, above 16 reserved | USB 3.2 9.6.7, Table 9-27 | to verify against the USB 3.2 text |
| Stream ID 0 | Reserved: its Stream Context is all zero and no doorbell names it, so an array serving N streams has at least N + 1 entries | 4.12.2 | to verify (section number) |
| Endpoint Context, streams | MaxPStreams `DW0 14:10` > 0 and LSA `DW0 15` = 1: the TR Dequeue Pointer (`DW2/3`) is the Primary Stream Context Array's address, DCS 0; with LSA = 1 MaxPStreams is 1..15 and the array 2^(MaxPStreams+1) entries; MaxPStreams must not exceed MaxPSASize | 6.2.3, Table 6-8, Table 6-10 | to verify: that DCS is 0 rather than ignored |
| Array alignment | 16-byte aligned; this driver also keeps the whole array (at most 32 x 16 = 512 bytes) inside one page | Table 6-1 | to verify: Table 6-1's boundary column for the Stream Context Array (the row is absent from section 1 above) |
| Stream Context (16 bytes) | `DW0` bit 0 DCS, bits `3:1` SCT, bits `31:4` TR Dequeue Pointer low; `DW1` pointer high (0 here); `DW2 23:0` Stopped EDTLA (written 0); `DW3` reserved | 6.2.4.1 | to verify (Stopped EDTLA width) |
| SCT values | 0 Secondary Transfer Ring, 1 Primary Transfer Ring, 2..7 a Secondary Stream Array of 8..256 entries; with LSA = 1 every used entry is 1 | Table 6-13 | to verify |
| Normal TRB | Carries no Stream ID: the stream is the ring the TD is placed on | 6.4.1.1 | as transcribed in "Transfer TRBs" |
| Doorbell | `DB Stream ID 31:16` names the stream whose ring has work; a stream endpoint is rung once per stream that has a TD to run, and a stopped stream endpoint restarts only for the streams rung | 5.6, 4.12 | to verify: whether the xHC re-arms every stream with a non-empty ring itself after a Stop Endpoint (this driver assumes it does not, and rings each) |
| Transfer Event | Carries no Stream ID: the TRB Pointer (`DW0/1`) names the TRB, so the stream is the ring holding that address (`XhciStreamFind`). An event with ED = 1 carries an Event Data value instead and names no ring; a zero pointer is the Prime Pipe STALL row below | 6.4.2.1 | the event layout is the "Event TRBs" table above |
| Set TR Dequeue Pointer | `DW0` new pointer with SCT `3:1` and DCS `0`; `DW2 31:16` Stream ID - 0 on an endpoint without streams, the stream otherwise; legal only with the endpoint Stopped (or Error); one command per stream | 6.4.3.9, 4.6.10 | to verify: that SCT is required (Linux writes 1 for a primary ring) |
| Reset Endpoint, Stop Endpoint | The endpoint's, not a stream's: every stream stops together, and each stream's partial progress is saved in its Stream Context and kept across Reset Endpoint. So after a halt or an Error only the stream whose own event failed gets a Set TR Dequeue (past its failed TD); every other stream resumes from its saved progress when rung, since a Set TR Dequeue to its oldest unretired TD would replay a partly moved TD. An Error endpoint with no failed stream known has every stream's requests terminated (completed with an error, rings set empty), never replayed | 4.6.8, 4.6.9, 4.12 | to verify against the PDF (Codex review of 31-A.1, round 1, finding 2, cites 4.12 and 4.6.8 for the saved progress) |
| Prime Pipe STALL | A device that stalls a Prime Pipe transaction halts the endpoint with a STALL Transfer Event whose TRB Pointer and length are 0: no TD and no stream is named. This driver takes it as the endpoint's: Reset Endpoint, CLEAR_FEATURE(ENDPOINT_HALT), every stream's requests completed as stalled, no Set TR Dequeue for a stream that did not fail. A nonzero pointer no stream ring holds is counted unclaimed and assigned to nothing | 4.12 | to verify (as cited by the Codex review, round 1, finding 3) |
| Installing and removing streams | One Configure Endpoint that drops and adds the DCI (4.6.6 forbids an Add on an endpoint that is not Disabled without its Drop), the Endpoint Context carrying the array; removing them is the same command with MaxPStreams 0 and the endpoint's own ring | 4.6.6 | as transcribed in "Which command may set which Slot Context field" for the flags; Linux does the same (`xhci_alloc_streams`, `xhci_free_streams`) |
| Completion codes | 10 Invalid Stream Type, 34 Invalid Stream ID (Table 6-90 above): a doorbell or context naming a stream the array does not hold | 6.4.5 | as transcribed |

What the driver chooses inside those rules, and which are policy rather than
specification (tested as policy by `test\test_stream.c`): at most 32 array
entries (31 streams) per endpoint; never fewer than 4 entries; the grant is
the smallest of the request, 2^MaxStreams and the array size less one; one
page-aligned common buffer per stream endpoint holding the array at offset 0
and one 64-TRB ring per stream at a multiple of its own size, so no ring
crosses a page or a 64 KB boundary; and a fresh Configure Endpoint (with a
CLEAR_FEATURE(ENDPOINT_HALT) to the device, as RESET_PIPE does) whenever streams
are installed or removed.

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

---

## 10. SuperSpeed and SuperSpeedPlus (roadmap-hcd.md task 29-0)

**Provenance.** This section was drafted on 2026-10-04 without the PDF open
and was then verified row by row on 2026-10-04 (branch `p29-spec`) against
the local xHCI 1.2c PDF and its text dump (`docs/references/`, every `p.N` the
page the copy prints) and against the USB 3.2 specification, revision 1.1
(June 2022, `docs/references/README.md`, "USB 3.2 revision 1.1"; every
`USB 3.2 p.N` is the page that copy prints, which is the PDF page index minus
31). Figure 4-27 was read from the rendered page, not the dump, because the
dump drops its arrows. Each row now says what it was verified against;
"verified" means the value or rule was found printed on the page cited, and
anything the specification does not say is labelled as this driver's policy
or as an inference. Codex's first-round reading (round 1 of the Phase 29
review) is superseded by this transcription where they differ. Where this
section and the PDF disagree, the PDF wins.

### 10.1 Supported Protocol Capability, USB3 groups and PSI DWORDs (xHCI 7.2)

The capability's layout is section 6's table. What a USB3 group adds:

| Field | Value | Verified |
|---|---|---|
| Major Revision `31:24` of DW0 | 03h | xHCI 1.2c Table 7-6, p.480; Table 7-11, p.483 |
| Minor Revision `23:16` of DW0 | 00h USB 3.0, 10h USB 3.1, 20h USB 3.2 (BCD) | xHCI 1.2c Table 7-6, p.480; Table 7-11, p.483 |
| PSIC `31:28` of DW2 (offset 08h) | 0 to 15; nonzero means every speed is a PSI DWORD and **no** default applies | xHCI 1.2c Table 7-8, p.481 |

Protocol Speed ID DWORD (xHCI 1.2c 7.2.1, Table 7-10, p.482; all rows
verified there):

| Bits | Field | Meaning |
|---|---|---|
| 3:0 | PSIV | the value PORTSC Port Speed reports for a device at this rate; 0 is reserved and never defined by a PSI |
| 5:4 | PSIE | exponent of PSIM: 0 bit/s, 1 Kb/s, 2 Mb/s, 3 Gb/s |
| 7:6 | PLT | 0 symmetric, 1 reserved, 2 asymmetric Rx, 3 asymmetric Tx; an asymmetric pair is an Rx DWORD immediately followed by a Tx DWORD with the same PSIV |
| 8 | PFD | 1 full-duplex (dual-simplex), 0 half-duplex |
| 13:9 | - | RsvdP |
| 15:14 | LP | Link Protocol, defined when the capability's Major Revision is 03h: 0 SuperSpeed, 1 SuperSpeedPlus, 2-3 reserved; 0 on a Major Revision 02h group |
| 31:16 | PSIM | mantissa |

A symmetric PSI makes PORTLI's RLC and TLC equal; an asymmetric pair gives
the Rx and Tx rates in its two DWORDs and the lane counts in RLC and TLC
(xHCI 1.2c 7.2.1 note, p.483).

Default Protocol Speed IDs (xHCI 1.2c 7.2.2.1.1, Table 7-13, p.485), presented
only when PSIC = 0 (footnote 120, p.485):

| PSIV | Meaning | Table 7-13's PSIM (PSIE 3 for USB3) | Defined for a group of |
|---|---|---|---|
| 1 | Full-speed | 12 Mb/s | USB 2.0 |
| 2 | Low-speed | 1500 Kb/s | USB 2.0 |
| 3 | High-speed | 480 Mb/s | USB 2.0 |
| 4 | SuperSpeed Gen 1 x1 | 5 Gb/s | USB 3.0, 3.1 and 3.2 |
| 5 | SuperSpeedPlus Gen 2 x1 | 10 Gb/s | USB 3.1 and 3.2 |
| 6 | SuperSpeedPlus Gen 1 x2 | 10 Gb/s | USB 3.2 only |
| 7 | SuperSpeedPlus Gen 2 x2 | 20 Gb/s | USB 3.2 only |

The last column is 7.2.2.1.2 (p.485-486): a PSIC = 0 group of Minor Revision
20h implies 4 to 7, one of 10h implies "the default SuperSpeed and
SuperSpeedPlus bit rates" (4 and 5; Table 7-13 marks 5 as USB 3.1 and 6 and 7
as USB 3.2), one of 00h implies 4 only, and no other protocol or revision may
leave PSIC at 0. Table 7-13 prints two columns that disagree for 6 and 7: its
"Bit Rate" column says 5 Gb/s and 10 Gb/s (the lane rate) while its PSIM
column says 10 and 20 (the aggregate). This driver takes the PSIM column,
which is what a PSI DWORD would carry. Table 7-13 gives no LP value; that 5 to
7 are SuperSpeedPlus is its "Definition" column.

The code decodes 5 to 7 on any USB3 group (`xhci_caps.c`,
`xhciDefaultKilobits`, keyed on Major Revision only) and never on a USB2
group, where a PSIC = 0 controller such as `qemu-xhci` reports them; it does
not narrow 5 to 3.1 and 3.2 groups or 6 and 7 to 3.2 groups as 7.2.2.1.2 does.
A conforming controller never reports an ID its group does not define, so
this is leniency, not a misreading; `qemu-xhci` models no rate above 5 Gb/s,
so these three defaults are untested ground.

A rate alone does not name a mode: Gen 2 x1 and Gen 1 x2 are both 10 Gb/s.
The negotiated lane counts are in PORTLI (10.2). The code treats any
SuperSpeed-class rate above 5 Gb/s as SuperSpeedPlus whatever LP says, and LP
= 1 as SuperSpeedPlus whatever the rate (`XhciPortRate`); 1.2c does not say
which LP a Gen 1 x2 PSI carries, so that rule is the driver's.

### 10.2 PORTSC, PORTLI and HCCPARAMS2 for a USB3 protocol port (xHCI 5.4.8, 5.4.10, 5.3.9)

PORTSC's bit table is section 3's; these are the USB3 meanings of its
SuperSpeed-only fields (xHCI 1.2c Table 5-27, p.371-379).

| Bit | Field | USB3 meaning | Verified |
|---|---|---|---|
| 0 | CCS | Asserted when the port goes from Polling to Enabled, that is once the link has trained to U0 (footnote 81). Figure 4-27's signal states (PP, CCS, PED, PR) put CCS = 1 only in Enabled (U0, U1, U2, U3, Resume or Recovery) and Reset, and **CCS = 0 in Error (SS.Inactive)**, Disabled, Polling, Compliance, Loopback and Disconnected; a state's signal values are forced on entry. The code does not rely on CCS in SS.Inactive | Table 5-27 CCS row and footnote 81, p.371; Figure 4-27, p.279; 4.19.1 notation, p.273; 4.19.8, p.303 |
| 1 | PED | Set by the xHC when link training succeeds; software disables with a write of 1 (and PR 0, footnote 82); **a Disabled port leaves Disabled only by a PLS = 5 (RxDetect) write**, to Disconnected, after which a new training enables it | Table 5-27 PED row, p.372 |
| 8:5 | PLS | 10.3's encodings | Table 5-27, p.374 |
| 18 | PEC | **Never set on a USB3 protocol port** | Table 5-27 PEC row, p.377; 4.19.2, p.292 |
| 19 | WRC | RW1CS: set when warm reset processing completes, including a hot reset the xHC turned into a warm one; not set if the warm reset was cut short by software clearing PP or PED | Table 5-27, p.377; 4.19.5, p.299; 4.19.5.1, p.300 |
| 23 | CEC | RW1CS: the port failed to configure its link partner (Port Capability/Configuration LMP exchange), for example two downstream-only ports cabled together; the port goes to Error | Table 5-27, p.378; 4.19.1.2.4.2, p.282 |
| 24 | CAS | RO: far-end terminations detected in Disconnected that the port could not take to Enabled, asserted only under the D3 conditions of 4.19.8 and never while the LTSSM is in Error, U3 or Disabled. **Software clears it by writing WPR = 1**, or the xHC clears it when CCS goes to 1. A CAS transition sets CSC, except one caused by software writing WPR | Table 5-27 CAS row, p.379, and CSC row, p.376; 4.19.8, p.303 |
| 31 | WPR | RW1S, reads 0: starts a warm reset and sets PR; PR, PRC and WRC then report its progress. **Completion is not success**: a reset that fails sets PLS to RxDetect, clears PR and CCS, sets PRC (and WRC for a warm one) and Port Speed 0, so the driver requires CCS, PED and U0 as well (`XhciLinkResetDone`) | Table 5-27 WPR row, p.379; 4.19.5 unsuccessful-reset list, p.299; 4.19.5.1, p.300 |

PR on a USB3 port starts a hot reset (Table 5-27 PR row, p.373). PR or WPR
moves the port to Reset from every state except Disconnected, Powered-off and
Disabled (4.19.1.2.5, p.284), so from every Enabled substate (4.19.1.2.9,
p.286) and also from Error, Polling and Loopback; Compliance's own text names
only WPR and HCRST (4.19.1.2.7, p.285). The hot-to-warm conversion is the
xHC's own and narrow: when the hot-reset TS1/TS2 handshake fails a warm reset
is tried automatically (4.19.5.1 footnote 66, p.300, quoting USB 3.2
10.3.1.6), and the only sign of it is WRC set at the reset's end (4.19.5,
p.299; 4.19.1.2.5 note, p.284-285). Choosing WPR whenever the link is not in
U0 - U1, U2, U3, SS.Inactive, Compliance Mode, or trained with PED clear - is
**this driver's policy** (`xhci_link.c`), not that rule. The counter
`superspeed: hot resets converted to warm` counts the xHC's conversions, from
WRC observed after a PR the driver wrote; `superspeed: warm resets` counts the
WPR writes.

PORTLI, PORTSC + 8 (xHCI 1.2c 5.4.10.1, Table 5-31, p.385; all rows verified
there):

| Bits | Field | Notes |
|---|---|---|
| 15:0 | Link Error Count | RW; reset by HCRST, by PR going 1 to 0, or by software writing 0 |
| 19:16 | RLC | Rx Lane Count, zero-based (0 to 15 is 1 to 16 lanes); valid only while CCS = 1; 0 for a simplex sublink |
| 23:20 | TLC | Tx Lane Count, likewise |
| 31:24 | - | RsvdP |

HCCPARAMS2 (xHCI 1.2c 5.3.9, Table 5-16, p.355-357; verified there): U3C 0,
CMC 1, FSC 2, CTC 3, **LEC 4** (Large ESIT Payload Capability: ESIT payloads
above 48K bytes supported), CIC 5, ETC 6, ETC_TSC 7, GSC 8, VTC 9, 10
reserved, DIC 11, E2V2C 12, 31:13 reserved. LEC decides 10.6's Max ESIT
Payload Hi and Mult (`XHCI_HCCPARAMS2_LEC`, bit 4: correct).

### 10.3 PLS encodings (xHCI 5.4.8, Table 5-27)

All rows verified against xHCI 1.2c Table 5-27, PLS row, p.374. Every write
needs LWS = 1 in the same write.

| PLS | Read meaning | A write of it |
|---|---|---|
| 0 | U0 | the link goes to U0 from any U state: a USB3 resume from U3 is this one write (USB2 writes 15, then 0) |
| 1 | U1 | ignored (footnote 85: U1 is entered through PORTPMSC's U1 Timeout, not this field) |
| 2 | U2 | USB2 protocol ports only (L1 entry) |
| 3 | U3 | the link goes from U0 to U3: suspend |
| 4 | Disabled (SS.Disabled, footnote 86) | **ignored**: a port is disabled with PED = 1, never with this write |
| 5 | RxDetect (footnote 87) | USB3 ports only: from Disabled (PP = 1) the link goes to RxDetect and the port to Disconnected; ignored in any other state |
| 6 | Inactive (SS.Inactive, footnote 88) | ignored |
| 7 | Polling | ignored |
| 8 | Recovery | ignored |
| 9 | Hot Reset | ignored |
| 10 | Compliance Mode | USB3 ports only: sets the port's internal CTE flag, which **enables** the Polling-to-Compliance transition; 4.19.1.2.4.1 (p.281-282) honours it only in the Disconnected state. Not an immediate transition. The driver never writes it |
| 11 | Test Mode: on a USB3 port, **the Loopback link state** (footnote 89) | ignored |
| 12-14 | reserved | ignored |
| 15 | Resume | USB2 ports only |

`src/xhci.h` names them `XHCI_PLS_*`; every value there matches.

### 10.4 The USB3 root port state machine (xHCI 4.19.1.2, Figure 4-27)

Verified against xHCI 1.2c Figure 4-27 (p.279, read from the rendered page),
the state texts 4.19.1.2.1 to 4.19.1.2.9 (p.279-286) and 4.19.5 (p.299-300).
Each state's signal values are (PP, CCS, PED, PR), forced on entry (4.19.1,
p.273): Powered-off 0,0,0,0; Disconnected, Polling, Error, Compliance,
Loopback and **Disabled 1,0,0,0**; Reset 1,1,0,1; Enabled 1,1,1,0.

| From | Trigger | To | Flags | Verified |
|---|---|---|---|---|
| any | PP written 0, or over-current (OCA = 1) | Powered-off | CCS, PR, PED cleared | 4.19.1.2.2, p.280 |
| Powered-off | PP written 1, or HCRST | Disconnected | - | 4.19.1.2.2, p.280 |
| Disconnected | Connect Detect (far-end terminations, footnote 58) | Polling | - | 4.19.1.2.3, p.280 |
| Polling | training and port configuration succeed (Downstream Config Successful) | Enabled | CCS, PED (footnote 81; PED row); CSC follows from the CCS change. **No software reset needed**, unlike USB2's Disabled -> Reset -> Enabled | 4.19.1.2.4.2, p.282; Table 5-27, p.371-372; 4.19.3, p.295 |
| Polling | training fails on a Link Timeout or a Disconnect Detect | Disconnected | no CSC: CCS is 0 in both states | 4.19.1.2.4.1, p.281 (footnote 60: LTSSM Rx.Detect maps to Disconnected) |
| Polling | port configuration fails (Config Error) | Error | CEC | 4.19.1.2.4.2, p.282 |
| Polling | first LFPS timeout with CTE = 1 | Compliance | - | 4.19.1.2.4.1, p.281-282 |
| Enabled (any substate), Error, Polling, Loopback | PR or WPR written 1 (Compliance: WPR) | Reset | PED cleared | 4.19.1.2.5, p.284; 4.19.1.2.9, p.286; 4.19.1.2.7, p.285 |
| Reset | success | Enabled | PRC; WRC if warm or converted to warm | 4.19.5, p.299; 4.19.5.1, p.300 |
| Reset | failure | Disconnected | PRC (WRC as above), CCS cleared, Port Speed 0 | 4.19.5, p.299; 4.19.1.2.5, p.284 |
| Enabled | link error (SS.Inactive) | Error | PLC. **PEC is never set on a USB3 port**. The Error signal state has CCS = 0, so CSC follows by Table 5-27's CSC definition; that is an inference, as no text names CSC for this transition | 4.19.1.2.6, p.285; 4.19.1.2.9, p.286; Table 5-27 PLC and PEC rows, p.377-378 |
| any but Powered-off | PED written 1 | Disabled (PLS Disabled; the link in SS.Disabled, receiver terminations withdrawn, USB 3.2 7.5.1.1.1, USB 3.2 p.162) | **no PEC** (never on USB3). Disabled's signal state has CCS = 0, so writing PED on a connected port drops CCS; Table 5-27 then implies CSC, but no text names it | 4.19.1.2.1, p.279; Table 5-27 PEC row, p.377 |
| any but Powered-off and Disabled | Disconnect Detect (footnote 59) | Disconnected | CSC where CCS was 1; PR and PED cleared | 4.19.1.2.3, p.280; 4.19.1, p.273 |
| Disabled | PLS = RxDetect written with LWS, or HCRST | Disconnected | - | 4.19.1.2.1, p.280; Table 5-27 PLS row, p.374 |
| Disabled | WPR or PR written 1 | none: Reset is not entered from Disabled | - | 4.19.1.2.5, p.284 |
| Disabled | PP written 0, or over-current | Powered-off | - | 4.19.1.2.1, p.280 |

**Whether the Disabled state raises CSC on a physical disconnect: it does
not.** What Figure 4-27 (p.279) draws: Disabled sits outside the large
bubble, its signal state is 1,0,0,0 (CCS already 0), and its only exits are
`HCRST=1 | Wr(PLS=RxDetect & LWS=1)` to Disconnected and `Wr(PP=0) | OCA=1`
to Powered-off, plus a dashed optional Debug Capability arrow; the
`Disconnect Detect` arrow into Disconnected leaves the bubble, not Disabled.
What the text says: "A Disconnect Detect in the any state, except Powered-off
or Disabled shall transition the port to the Disconnected state" (4.19.1.2.3,
p.280). CSC is set only by a change of CCS or CAS (Table 5-27 CSC row, p.376;
4.19.2, p.292), CCS is 0 throughout Disabled, and CAS is not asserted while
the LTSSM is in Disabled (4.19.8, p.303). A Disabled port has also withdrawn
the receiver terminations it would detect a disconnect with (USB 3.2 7.5.1.1.1,
USB 3.2 p.162). So nothing in a Disabled port reports the device leaving, and
29-A.5's pessimistic orphan rule is the specified behaviour, not a guess: an
orphan port, or a paired port whose companion never connected, holds until
the controller's next start.

### 10.5 The Slot Context for a SuperSpeed device (xHCI 6.2.2)

- Speed (DW0 23:20) is the port's PSIV, raw, as for every speed (section 8).
  1.2c says the field "is not applicable to USB3 Gen X" and refers to PORTSC
  Port Speed for its values (Table 6-4, p.408; verified); the driver writes
  it regardless.
- Route String is 0 on a root port; behind a SuperSpeed hub it is the
  topology graph's, as for any hub (section 11.8). The root port is not part
  of the Route String (USB 3.2 Figure 10-5, USB 3.2 p.377; verified).
- No TT: a SuperSpeed device has no transaction translator, and TTT and MTT
  are a High-speed hub's and its LS/FS children's (Tables 6-4 and 6-6). **But
  Parent Hub Slot ID and Parent Port Number are not TT-only**: Table 6-6
  (p.409-410; verified) also requires them for an SS or SSP device "connected
  through a higher rank hub" - "a Gen1 x1 connected behind a Gen1 x2 hub, or
  Gen1 x2 device connected behind Gen2 x2 hub" - where footnote 110 makes a
  higher rank hub one whose downstream port isolates the signalling between
  its upstream and downstream ports, ranks as USB 3.2 Polling.PortMatch
  orders them: Gen 2x2, Gen 2x1, Gen 1x2, Gen 1x1 (USB 3.2 7.5.4.5, USB 3.2
  p.176). Both stay 0 for a device on a root port, a High-Speed device, and
  the highest-rank SS/SSP device the xHC supports. **Implemented, host vectors
  only** (no SuperSpeedPlus hub is held; section 11.8):
  - every SS device's own link rank is kept in its record. On a root port it
    comes at Address Device from the PSI rate (XhciPortRate, the aggregate)
    and PORTLI's RLC, zero-based: the lane rate is the aggregate over RLC + 1
    (`XhciSsRootRank`). Behind a SuperSpeed hub it comes from the hub's
    extended port status, read at the port's reset when the hub qualifies
    (11.6); a hub with no SuperSpeedPlus capability has Gen 1x1 links
    (`XhciSsHubChildRank`);
  - `HcdHubPlace` fills Parent Hub Slot ID and Parent Port Number with the
    hub's slot and port when the hub's own upstream link outranks the
    device's (`XhciSsHubParentOf`, `XhciSsParentNeeded`), and with the
    hub's own pair when the device ranks the same as the hub, so a device
    behind a chain of equal-rank hubs names the boundary hub above them, as
    an LS/FS device behind an FS hub names the HS hub's TT. Kept apart from
    the TT fields, which CLEAR_TT_BUFFER is sent through;
  - a slot named as a parent is disabled after its children: the abandoned
    sweep goes deepest tier first and stops at a failed Disable Slot, as
    every other teardown goes deepest first;
  - a RESET_PORT whose link retrains to another rank fails, so the device
    is enumerated afresh rather than readdressed with a stale pair;
  - when either rank is unknown - PORTLI unreadable, or an SSP hub whose
    extended status was not read - both stay 0. That is a best-effort
    fallback, not a rule the specification gives: it invents no boundary
    and is right on every path whose links rank alike, which every Gen 1x1
    hub's do, but it still leaves both 0 where an unobserved boundary
    exists, an unresolved case;
  - a USB 2.0 device behind a USB 3 hub's USB 2.0 half is unchanged: its TT
    fields are the topology graph's.
- Max Exit Latency 0: "a Max Exit Latency value of '0' indicates to the xHC
  that no links in the path to the device are being power managed" (4.23.5.2,
  p.326; verified). That holds here because the driver enables neither U1 nor
  U2: a root port whose PORTPMSC U1 and U2 Timeouts are both 0, their default,
  rejects every U1 and U2 request and initiates none (4.19.4.1, p.297;
  verified), and a hub downstream port's timeout of 0 disables its timer and
  is the default after a power-on or upstream-port reset (USB 3.2 10.2.2, USB
  3.2 p.381; verified). With 0 the xHC may also skip PING TPs for the slot's
  periodic endpoints (4.23.5.2 note, p.326).

### 10.6 The Endpoint Context for a SuperSpeed endpoint (xHCI 6.2.3, 4.14.2)

| Field | SuperSpeed value | Verified |
|---|---|---|
| Max Packet Size (DW1 31:16) | wMaxPacketSize 10:0. Control 512; bulk 1024; interrupt 1 to 1024 and isochronous **0** to 1024, and 1024 when bMaxBurst > 0 | xHCI 1.2c 6.2.3.5, p.419; xHCI 4.3 step 7, p.72 (EP0 512); USB 3.2 Table 9-26, USB 3.2 p.365 |
| Max Burst Size (DW1 15:8) | the SS Endpoint Companion's bMaxBurst, 0-15 (zero-based); 0 for EP0, which has no companion | xHCI Table 6-9, p.415; 6.2.3.4, p.418-419; USB 3.2 9.6.7 and Table 9-28, USB 3.2 p.367-368 |
| Mult (DW0 9:8) | LEC = 0: SS isochronous only, the companion's bmAttributes 1:0, valid 0-2 (zero-based bursts per interval); 0 for every other type. **LEC = 1: RsvdZ (written 0)**, and the xHC computes ROUNDUP(Max ESIT Payload / Max Packet Size / (Max Burst Size + 1)) - 1 | xHCI Table 6-8, p.414 (both halves) |
| Interval (DW0 23:16) | interrupt and isochronous: bInterval - 1, bInterval 1-16, giving 0-15; bulk and control: not used by the xHC (the driver writes 0) | xHCI Table 6-12 ("SSP, SS or HS Interrupt or Isoch" row), p.420; 6.2.3.6, p.419 |
| Max ESIT Payload Lo (DW4 31:16) | periodic: wBytesPerInterval of the SS companion, or, when the companion's SSP ISO Companion bit is 1, the SSP isochronous companion's dwBytesPerInterval 15:0 | xHCI Table 6-11, p.416; 4.14.2, p.236-237 |
| Max ESIT Payload Hi (DW0 31:24) | bits 23:16 of the payload when LEC = 1; RsvdZ when LEC = 0 | xHCI Table 6-8, p.414 |
| MaxPStreams, LSA | 0 until Phase 31 (streams) | xHCI Table 6-8, p.414 |

The largest payload the legacy fields describe is Max Packet Size x (Max
Burst + 1) x (Mult + 1) = 1024 x 16 x 3 = 49,152 bytes, which is 1.2c's 48K
limit without LEC; with LEC it is 16 MB - 1 (6.2.3.8, p.420; verified). A
device sends the SuperSpeedPlus Isochronous Endpoint Companion for every
isochronous endpoint that needs more than 48K bytes per service interval,
only when operating above Gen 1 (USB 3.2 9.6.8, USB 3.2 p.369).

What this driver carries is narrower than either, and that is the driver's
limit, not the specification's: an isochronous URB packet is one interval's
payload, and the transfer path maps it as at most two page-bounded pieces of
one 4 KiB page (`XhciPipeIsoFragments`, `hcd_io.c`). So any SuperSpeed or
SuperSpeedPlus isochronous endpoint whose interval payload exceeds 4096 bytes
is refused at SELECT_CONFIGURATION or SELECT_INTERFACE
(`XHCI_PIPE_ESIT_REFUSED`, counted as `superspeed: endpoints refused - ESIT`),
never admitted and never truncated (Codex review of Phase 29, round 1,
finding 1). It also refuses one whose interval needs more than four bursts
of its packet size and Max Burst (4096 bytes in 512-byte packets at burst 0
is eight), because TBC is two bits without ETC (4.11.2.3, p.197; round 2,
finding 2). Lifting the limit means multi-page isochronous packets - several
TRBs per packet - in `hcd_io.c` and `xhci_xfer.c`.

A zero-bandwidth isochronous endpoint - Max Packet Size 0, which USB 3.2
Table 9-26 allows for an isochronous endpoint with bMaxBurst 0 and for no
other type - is accepted (owner's ruling, 2026-10-04; `xhci_pipe.c`,
`XhciPipeZeroBandwidth`), so the SELECT_CONFIGURATION or SELECT_INTERFACE
naming one succeeds. Its Endpoint Context is configured as any other, with
Max Packet Size 0 (6.2.3.5's literal rule) and Max ESIT Payload 0, which by
4.14.2's bandwidth formula (p.237) reserves nothing; the pipe handle is
valid, and an isochronous URB on it is refused with
USBD_STATUS_INVALID_PARAMETER before any TRB is built (`hcd_io.c`,
`hcdIsoAdmit`), so no TD Size or TBC arithmetic divides by its size. A zero
size with a nonzero bMaxBurst, a nonzero wBytesPerInterval or an SSP
isochronous companion is malformed, and so is size 0 on a bulk or interrupt
endpoint. Whether a controller accepts an Endpoint Context with Max Packet
Size 0 is unobserved.

TD Size (xHCI 4.11.2.4, p.198; verified) has no burst term: it counts packets
of Max Packet Size remaining in the TD (TD Packet Count = ROUNDUP(TD Transfer
Size / Max Packet Size), saturating at 31, 0 in the last TRB), so at
SuperSpeed it divides by 1024 (bulk) or 512 (EP0), and the formula in section
7 stands unchanged. Max Burst enters only the isochronous TBC =
ROUNDUP(TDPC / (Max Burst Size + 1)) - 1 and TLBPC (4.11.2.3, p.197;
verified), which section 7 already transcribes. That answers the roadmap's
"TD Size against burst": there is no relation to transcribe.

Table 6-9's fields for EP0 at SuperSpeed: Max Burst 0, Mult 0, Max Packet
Size 512 (xHCI 4.3 step 7, p.72; USB 3.2 Table 9-11, USB 3.2 p.350, where
bMaxPacketSize0 09h is "the only valid value ... when operating at Gen X
speed").

### 10.7 The USB 3.2 descriptors (USB 3.2 9.4, 9.6.1, 9.6.2, 9.6.7, 9.6.8)

Descriptor types (USB 3.2 Table 9-6, USB 3.2 p.332; verified): BOS 0Fh,
DEVICE CAPABILITY 10h, SUPERSPEED_USB_ENDPOINT_COMPANION 30h,
SUPERSPEEDPLUS_ISOCHRONOUS_ENDPOINT_COMPANION 31h.

Device descriptor (9.6.1, Table 9-11, USB 3.2 p.349-350; verified):
bMaxPacketSize0 is an exponent, and 09h (512) is the only valid value at Gen X
speed. bcdUSB is the specification release the device complies with, in BCD
(USB 3.2 p.348-349); the specification sets no "0300h or above" rule in that
row, and the hub examples of 10.15.1 show 0310h.

BOS descriptor, type 0Fh (9.6.2, Table 9-12, USB 3.2 p.350; verified):

| Offset | Field |
|---|---|
| 0 | bLength = 5 |
| 1 | bDescriptorType = 0Fh |
| 2 | wTotalLength (this descriptor and all its device capabilities) |
| 4 | bNumDeviceCaps |

Device Capability header, type 10h (Table 9-13, USB 3.2 p.351; verified):
bLength, bDescriptorType, bDevCapabilityType. Capability type codes (Table
9-14, USB 3.2 p.351; verified): 02h USB 2.0 Extension, 03h SuperSpeed USB,
04h Container ID, 0Ah SuperSpeedPlus (also 01h, 05h-09h, 0Bh-11h for others;
00h and 12h-FFh reserved).

SuperSpeed USB Device Capability (9.6.2.2, Table 9-16, USB 3.2 p.354-355;
verified), 10 bytes, required of every Enhanced SuperSpeed device:

| Offset | Field |
|---|---|
| 3 | bmAttributes (bit 1 LTM capable) |
| 4 | wSpeedsSupported (bit 0 LS, 1 FS, 2 HS, 3 Gen 1) |
| 6 | bFunctionalitySupport (lowest speed with full function) |
| 7 | bU1DevExitLat (us; 00h-0Ah) |
| 8 | wU2DevExitLat (us; 0000h-07FFh) |

Container ID (9.6.2.3, Table 9-17, USB 3.2 p.356; verified): 20 bytes, the
UUID at offset 4. It "shall be implemented by all USB hubs", and a device
that provides it in one operating mode provides it in every mode, so both
halves of a USB 3 hub report it; that they report the same UUID is what its
purpose (one instance across all modes) implies rather than a sentence the
section prints.

SuperSpeedPlus Device Capability (9.6.2.5, Table 9-19, USB 3.2 p.357-358;
verified), 12 + 4 x (SSAC + 1) bytes, required of every SuperSpeedPlus
device:

| Offset | Field |
|---|---|
| 3 | bReserved |
| 4 | bmAttributes (4 bytes): SSAC 4:0 (attribute count - 1), SSIC 8:5 (sublink speed ID count - 1) |
| 8 | wFunctionalitySupport: SSID 3:0 (minimum lane speed), 7:4 reserved, min Rx lanes 11:8, min Tx lanes 15:12 |
| 10 | wReserved |
| 12 | bmSublinkSpeedAttr[0..SSAC]: SSID 3:0, LSE 5:4 (0 b/s, 1 Kb/s, 2 Mb/s, 3 Gb/s), ST 7:6, LP 15:14 (0 SuperSpeed, 1 SuperSpeedPlus), LSM 31:16 |

ST is two independent bits, not xHCI's PLT encoding: bit 6 is 0 symmetric
or 1 asymmetric, bit 7 is 0 receive or 1 transmit; attributes come in Rx/Tx
pairs with the same SSID. `XHCI_SSHUB_SSA_ST_TX` (2, bit 7) is right.

SuperSpeed Endpoint Companion, type 30h, 6 bytes (9.6.7, Table 9-28, USB 3.2
p.367-369; verified). Returned only at Gen X speed; every endpoint but the
Default Control Pipe has one, and it "shall immediately follow" its endpoint
descriptor:

| Offset | Field |
|---|---|
| 2 | bMaxBurst, 0-15 (0 for control) |
| 3 | bmAttributes: bulk 4:0 MaxStreams (0-16, 2^MaxStreams streams); control and interrupt reserved; isochronous 1:0 Mult (0-2, and 0 when bMaxBurst is 0), bit 7 SSP ISO Companion |
| 4 | wBytesPerInterval: periodic only; reserved 0 for control and bulk |

When SSP ISO Companion is 1, a SuperSpeedPlus Isochronous Endpoint Companion
"shall immediately follow", the Mult field "shall be ignored", and
wBytesPerInterval "shall be set to one". Table 9-28 states the actual Mult as
dwBytesPerInterval / bMaxBurst / wMaxPacketSize rounded up; the xHC's own
derivation under LEC (10.6) and this driver's no-LEC derivation
(`xhciPipeSuperSpeed`) both use (Max Burst Size + 1), which is the
zero-based count Table 9-28's own bMaxBurst row defines.

SuperSpeedPlus Isochronous Endpoint Companion, type 31h, 8 bytes (9.6.8,
Table 9-29, USB 3.2 p.369-370; verified): wReserved at 2, dwBytesPerInterval
at 4. Returned only above Gen 1 speed, for each isochronous endpoint that
needs more than 48K bytes per service interval.

Configuration descriptor, bMaxPower at offset 8 (9.6.3, Table 9-23, USB 3.2
p.360-361; verified 2026-10-05, roadmap-hcd task 34.2): "Expressed in 2 mA
units when the device is operating in high-speed mode and in 8 mA units when
operating at Gen X speed (i.e., 50 = 100 mA when operating at highspeed and
50 = 400 mA when operating at Gen X speed)". So 0FFh is 510 mA below
SuperSpeed and 2040 mA at it; a device reports one value per speed it runs
at, so the unit follows the speed the device is running at now.

### 10.8 Link states and the fallen-back upstream port (USB 3.2 7.5, 7.5.1, 10.18.1)

USB 3.2 link states (7.5, USB 3.2 p.159; verified): U0, U1, U2, U3, Rx.Detect,
Polling, Recovery, Hot Reset, Loopback, Compliance Mode, eSS.Inactive and
eSS.Disabled - twelve. eSS.Disabled removes the receiver terminations and
disables LFPS and SuperSpeed signalling; a downstream port enters it when
directed and leaves it to Rx.Detect when directed (7.5.1, 7.5.1.1, USB 3.2
p.161-162; verified).

The SS.Disabled rule for a peripheral's upstream port that has fallen back
(7.5.1.2, USB 3.2 p.162-163; verified, and the text matches the "UFP Exit
Condition Clarification" ECN, which revision 1.1 has absorbed):

- A peripheral's upstream port reaches eSS.Disabled when Rx.Detect has
  counted eight far-end termination detections without finding one (7.5.3.6,
  USB 3.2 p.167; "to transition to USB 2.0 after 80 ms"), or when Polling or
  port configuration times out (7.5.4.3 to 7.5.4.9, 7.5.6.2 and 8.4.6).
- Its eSS.Disabled has two substates. On entry to eSS.Disabled.Default a
  tDisabledCount counter is incremented; the counter is reset only by invalid
  VBUS or a successful port configuration exchange.
- From eSS.Disabled.Default the port returns to Rx.Detect when VBUS becomes
  valid, **when a USB 2.0 bus reset is detected and tDisabledCount is below
  3**, or when directed. At tDisabledCount = 3 it goes to eSS.Disabled.Error.
- eSS.Disabled.Error stays put across USB 2.0 bus resets and leaves only on a
  power-on reset (to Rx.Detect) or, for a self-powered device, invalid VBUS
  (to eSS.Disabled.Default).
- A hub's upstream port has no counter: it leaves eSS.Disabled only when VBUS
  becomes valid or when directed (7.5.1.1.2, USB 3.2 p.162).

And on the USB 2.0 side (10.18.1, USB 3.2 p.456, with Table 10-19's
parameters, USB 3.2 p.460; verified): a peripheral connects on USB 2.0 only
after its SuperSpeed port has reached USPORT.Powered-off with VBUS present;
on a USB 2.0 bus reset it must enter USPORT.Powered-On within
tCheckSuperSpeedOnReset (1 ms); and if its SuperSpeed port then reaches
USPORT.Training it must disconnect from USB 2.0 within tUSB2SwitchDisconnect
(1 ms). That is the ping-pong 29-A.5's hold prevents: every bus reset the
companion port issues sends the device back to look for SuperSpeed
terminations, and a restored SuperSpeed port would take it off USB 2.0. The
specification bounds it from the device's side at three such attempts
without a successful configuration exchange, but a re-armed SuperSpeed port
that trains resets the count, so the hold is still needed.

### 10.9 Not answered by this section

- Which P14s Gen 1 connectors reach which controller (the roadmap's 29-0
  question): whether its USB-C 3.1 Gen 2 ports are the chipset controller's
  (`8086:02ED`) ports 13-18 or the Thunderbolt controller's own xHCI, and
  whether that one is present to Windows 98 SE. Needs the machine, or the
  vendor's documentation, not a specification.

## 11. The USB 3.2 hub class (roadmap-hcd.md task 30-0)

**Provenance.** Drafted on 2026-10-04 from the drafting agent's knowledge of
the USB 3.2 specification with Linux's `include/uapi/linux/usb/ch11.h` and
`drivers/usb/core/hub.c` (`external/`, interface documentation only) as a
cross-check, then verified row by row on 2026-10-04 against the USB 3.2
specification, revision 1.1 (June 2022), chapter 10, "Hub, Host Downstream
Port, and Device Upstream Port Specification"; every `USB 3.2 p.N` is the page
that copy prints (section 10's provenance paragraph). The section numbers the
draft gave (10.15.x, 10.16.x, Tables 10-5 to 10-18) are those of revision 1.1
and were found as given. Where this section and the specification disagree,
the specification wins and `src\xhci_sshub.h`, which cites this section, is
fixed with it.

A USB 3 hub is two hubs in one enclosure: a USB 2.0 hub on its D+/D- pair
(section 10 of design record 13; Phase 27) and an Enhanced SuperSpeed hub on
its SuperSpeed pairs, each with its own upstream port, device address,
descriptors and status-change endpoint ("a USB hub is the logical combination
of two hubs", USB 3.2 10.1, USB 3.2 p.373; verified). This section is the
SuperSpeed hub's class. Where a value is the same as USB 2.0's it is said so
and design record 13 section 10.1 is the citation.

### 11.1 Identification and the descriptor (USB 3.2 10.15.1, 10.15.2.1)

- Device descriptor: bDeviceClass 09h, bDeviceSubClass 0, **bDeviceProtocol
  3** and the interface's bInterfaceProtocol 0 (10.15.1, USB 3.2 p.432;
  verified); bMaxPacketSize0 09h (512). The example descriptor set carries
  bcdUSB 0310h; the extended port status (11.6) is keyed to the
  SuperSpeedPlus capability, not to bcdUSB (Table 10-12's rule, 11.6).
- One configuration, one interface (class 09h), one endpoint: the
  status-change endpoint, interrupt IN, wMaxPacketSize 2, bInterval 8
  ("maximum allowable interval"), followed by its SuperSpeed Endpoint
  Companion with bMaxBurst 0, bmAttributes 0, wBytesPerInterval 2 (10.15.1's
  example set, USB 3.2 p.436; verified as printed there). The two-byte size
  follows from 10.13.4's "the Hub and Port Status Change Bitmap size is two
  bytes" (USB 3.2 p.428).
- The BOS descriptor carries a SuperSpeed USB Device Capability, a Container
  ID (mandatory for a hub, 9.6.2.3, USB 3.2 p.356; this driver does not read
  it - 30-A.1 pairs nothing), and on a SuperSpeedPlus hub a SuperSpeedPlus
  capability (section 10.7) whose sublink speed attributes 11.6 indexes
  (10.15.1's example, USB 3.2 p.435).

Enhanced SuperSpeed hub descriptor, **type 2Ah**, read with GET_DESCRIPTOR,
bmRequestType A0h, wValue 2A00h, wLength 12 (10.15.2.1, Table 10-5, USB 3.2
p.437-438; every row verified there):

| Offset | Field | Size | Notes |
|---|---|---|---|
| 0 | bDescLength | 1 | 12 |
| 1 | bDescriptorType | 1 | 2Ah |
| 2 | bNbrPorts | 1 | at most 15 (also nMaxHubPorts, Table 10-19, USB 3.2 p.460), so a Route String nibble names each exactly (xHCI Table 6-4 footnote 106's "above 14 is written 15" is about High- and Full-speed hubs, xHCI p.408) |
| 3 | wHubCharacteristics | 2 | D1:D0 power switching (00 ganged, 01 individual, 1X reserved), D2 compound device, D4:D3 over-current protection (00 global, 01 per port, 1X none); **D15:D5 reserved**, so USB 2.0's TT think time (6:5) and port indicators (7) do not exist here |
| 5 | bPwrOn2PwrGood | 1 | 2 ms units; 0 if the hub has no power switching |
| 6 | bHubContrCurrent | 1 | in units of aCurrentUnit, **4 mA** (Table 10-19, USB 3.2 p.460), for operation on both USB 2.0 and SuperSpeed; the USB 2.0 hub descriptor uses its own encoding |
| 7 | bHubHdrDecLat | 1 | hub packet header decode latency, 0.1 us units: 00h ("much less than 0.1 us") to 0Ah (1.0 us), 0Bh-FFh reserved. Informs U1/U2 exit-latency budgets, which this driver does not use |
| 8 | wHubDelay | 2 | the hub's maximum forwarding delay in ns, either direction, at most tHubDelay. Same remark |
| 10 | DeviceRemovable | 2 | bit n = port n non-removable (1); bit 0 reserved. A fixed two bytes, unlike USB 2.0's variable bitmap; no PortPwrCtrlMask follows |

`src\xhci_sshub.c` (`XhciSsHubParseDescriptor`) refuses a reply shorter than
12, a bLength below 12 or past what arrived, a type other than 2Ah, no ports,
and more than 15 ports. The bus manages ports 1-14 of it (every hub object has
14 port objects) - a policy, not a specification limit. A fifteenth port is
cleared of PORT_POWER at bring-up and any change it reports is read and
cleared, so it never keeps the status-change endpoint completing; a USB 2.0
hub's ports above 14 are treated the same (Codex review of 034a119, finding
3).

### 11.2 The class requests (USB 3.2 10.16.2, Tables 10-7 and 10-8)

All rows verified against USB 3.2 Tables 10-7 and 10-8 (USB 3.2 p.440) and
the request sections 10.16.2.1 to 10.16.2.10 (USB 3.2 p.441-455).

| Request | bmRequestType | bRequest | wValue | wIndex | wLength | Notes |
|---|---|---|---|---|---|---|
| CLEAR_FEATURE (hub) | 20h | 1 | C_HUB_LOCAL_POWER 0, C_HUB_OVER_CURRENT 1 | 0 | 0 | as USB 2.0 |
| CLEAR_FEATURE (port) | 23h | 1 | 11.4's selectors | port in 7:0 | 0 | |
| GET_DESCRIPTOR | A0h | 6 | 2A00h | 0 | 12 | 11.1 |
| GET_STATUS (hub) | A0h | 0 | 0 | 0 | 4 | wHubStatus: bit 0 local power source lost, bit 1 over-current; wHubChange: bit 0 C_HUB_LOCAL_POWER, bit 1 C_HUB_OVER_CURRENT (Tables 10-10 and 10-11, USB 3.2 p.443-444) |
| GET_STATUS (port) | A3h | 0 | Port Status Type in 7:0: 00h PORT_STATUS, 01h PD_STATUS (**deprecated, shall not be used**), 02h EXT_PORT_STATUS, 03h-FFh reserved | port | 4 for PORT_STATUS, 8 for the other two | 10.16.2.6 and Table 10-12, USB 3.2 p.444-445. Table 10-7's row prints wValue "Zero" and wLength "Four", which predates the type field; 10.16.2.6 and Table 10-12 govern |
| GET_PORT_ERR_COUNT | A3h | 13 | 0 | port | 2 | link errors since the last reset; not used (10.16.2.5, USB 3.2 p.444) |
| SET_DESCRIPTOR | 20h | 7 | | 0 | | optional; not used |
| SET_FEATURE (hub) | 20h | 3 | | 0 | 0 | not used |
| SET_FEATURE (port) | 23h | 3 | 11.4's selectors | port in 7:0; 15:8 the U1/U2 timeout, link state or remote wake mask, 0 otherwise | 0 | 10.16.2.10, USB 3.2 p.452 |
| SET_HUB_DEPTH | 20h | **12** | the hub depth (11.3) | 0 | 0 | 10.16.2.9, USB 3.2 p.451-452 |
| bRequest 2, 4-5 and 8-11 | | | | | | reserved (8-11 were USB 2.0's TT requests); a SuperSpeed hub has no transaction translator |

### 11.3 SET_HUB_DEPTH (USB 3.2 10.16.2.9)

A SuperSpeed hub routes a downstream packet by the Route String in its header
(the same five 4-bit nibbles the xHC's Slot Context carries, xHCI 4.3.3 and
`xhci-data-structures.md` section 8, "Route String tier order"). "The Hub
Depth left shifted by two is the offset into the Route String that identifies
the lsb of the Route String Port Field for the hub" (10.16.2.9, USB 3.2
p.451; verified), and a hub on a root port has depth 0 (Figure 10-5, USB 3.2
p.377: depths 0 to 4 for five levels, the root port not in the string;
verified). So wValue is **the number of hubs between it and the root port**,
0 for a hub on a root port, 4 for the fifth; a wValue above 4 is a Request
Error (USB 3.2 p.452). Until the hub is configured and its depth set it
ignores the Route String and takes every packet as its own (10.1.3.1, USB 3.2
p.376; verified), so the request must come before any downstream port is
used. **Corrected**: the draft said a hub answers a Request Error to
SET_HUB_DEPTH before SET_CONFIGURATION; the specification says "If the hub is
not configured, the hub's response to this request is undefined" (USB 3.2
p.452). `hcd_sshub.c` sends it after SET_CONFIGURATION and before
GET_DESCRIPTOR, with the topology graph's tier (0 on a root port; `hcd.h`
and `xhci_topo.c` define Tier so), which is the order the specification
requires, and does not serve a hub that refuses it.

### 11.4 Port status, change bits and features at SuperSpeed (USB 3.2 10.16.2.6, Tables 10-9, 10-13, 10-14)

wPortStatus, first word of a GET_STATUS(port) answer (Table 10-13 and
10.16.2.6.1, USB 3.2 p.445-448; every row verified there):

| Bit(s) | Field | USB 2.0's bit there | Notes |
|---|---|---|---|
| 0 | PORT_CONNECTION | same | set when the port is in DSPORT.Enabled; **in DSPORT.Resetting and DSPORT.Error (eSS.Inactive) it keeps the value of the state before** |
| 1 | PORT_ENABLE | same | 1 in DSPORT.Enabled, 0 otherwise. **There is no ClearPortFeature(PORT_ENABLE) at SuperSpeed** ("cannot be used by USB system software to disable a port"); a port is taken down with SetPortFeature(PORT_LINK_STATE, eSS.Disabled) (USB 3.2 p.447, p.454) |
| 2 | reserved | PORT_SUSPEND | suspend is the link state U3 |
| 3 | PORT_OVER_CURRENT | same | |
| 4 | PORT_RESET | same | 1 in DSPORT.Resetting, hot or warm |
| 8:5 | PORT_LINK_STATE | PORT_POWER (8); 7:5 reserved | 11.5 |
| 9 | PORT_POWER | low-speed | **power moves from bit 8 to bit 9** |
| 12:10 | PORT_SPEED | high-speed (10), test (11), indicator (12) | valid with PORT_ENABLE: 0 = Enhanced SuperSpeed, 1-7 reserved. The actual rate is in the extended status (11.6) |
| 15:13 | reserved | | |

wPortChange, second word (Table 10-14 and 10.16.2.6.2, USB 3.2 p.448-450;
every row verified there):

| Bit | Change | Cleared by selector | USB 2.0's bit there |
|---|---|---|---|
| 0 | C_PORT_CONNECTION | 16 | same |
| 1 | reserved | - | C_PORT_ENABLE: **none at SuperSpeed** |
| 2 | reserved | - | C_PORT_SUSPEND: none (a U3 exit is C_PORT_LINK_STATE) |
| 3 | C_PORT_OVER_CURRENT | 19 | same |
| 4 | C_PORT_RESET | 20 | set on DSPORT.Resetting -> DSPORT.Enabled for **any** type of reset, hot or warm. **Only on success**: a reset that fails ends in DSPORT.Disconnected (10.3.1.2, 10.3.1.6, USB 3.2 p.386-389) and raises neither reset change |
| 5 | C_BH_PORT_RESET | 29 | set on DSPORT.Resetting -> DSPORT.Enabled for a warm reset only |
| 6 | C_PORT_LINK_STATE | 25 | set when the link completes U3 -> U0 because of SetPortFeature(PORT_LINK_STATE), or completes a transition to Loopback, to Compliance, or to eSS.Inactive with Rx terminations present; **not** for a U3 -> U0 caused by the device's remote wake |
| 7 | C_PORT_CONFIG_ERROR | 26 | the link partner could not be configured (for example two downstream-only ports); **the port then goes to DSPORT.Error**, whose link is in eSS.Inactive (10.3.1.4, USB 3.2 p.387) |

The driver's decision checks the link state first: a link in SS.Inactive or
Compliance Mode takes the warm-reset recovery whatever else changed. Since a
configuration error always leaves the port in DSPORT.Error with the link in
eSS.Inactive, a config error is always recovered by that warm reset in the
specified case; the other branch (config error with the link in any other
state: the device dropped, the port down until its next connect change)
covers only a hub that does not follow 10.3.1.4.

Port feature selectors (Table 10-9, USB 3.2 p.441; every value verified
there; those USB 2.0 shares keep USB 2.0's numbers):

| Selector | Value | Set / Clear | Notes |
|---|---|---|---|
| PORT_CONNECTION | 0 | - | status only; Set and Clear are no-ops |
| PORT_OVER_CURRENT | 3 | - | status only |
| PORT_RESET | 4 | Set | 11.5; completes with C_PORT_RESET |
| PORT_LINK_STATE | 5 | Set | wIndex 15:8 = 0 (U0), 1 (U1), 2 (U2), 3 (U3), 4 (eSS.Disabled), 5 (Rx.Detect) or 10 (enable Compliance Mode for the next attach); any other value is a Request Error (10.16.2.10, USB 3.2 p.453-454) |
| PORT_POWER | 8 | Set, Clear | as USB 2.0 |
| C_PORT_CONNECTION | 16 | Clear | |
| C_PORT_OVER_CURRENT | 19 | Clear | |
| C_PORT_RESET | 20 | Clear | |
| (reserved) | 21 | - | used by USB 2.0 |
| PORT_U1_TIMEOUT | 23 | Set | wIndex 15:8 = timeout: 00h zero (the default; a zero timeout disables the timer, 10.2.2, USB 3.2 p.381), 01h-7Fh in us, 80h-FEh reserved, FFh infinite (Table 10-16) |
| PORT_U2_TIMEOUT | 24 | Set | likewise for U2, in 256 us steps to FEh = 65.024 ms, FFh infinite (Table 10-17) |
| C_PORT_LINK_STATE | 25 | Clear | |
| C_PORT_CONFIG_ERROR | 26 | Clear | |
| PORT_REMOTE_WAKE_MASK | 27 | Set | wIndex 15:8 = the mask, 11.7 |
| BH_PORT_RESET | 28 | Set | warm reset; completes with C_BH_PORT_RESET |
| C_BH_PORT_RESET | 29 | Clear | |
| FORCE_LINKPM_ACCEPT | 30 | Set, Clear | compliance testing; not used |

The status-change endpoint's report is USB 2.0's shape: bit 0 the hub, bit n
port n, two bytes (10.13.4, USB 3.2 p.427-428; verified).

`src\xhci_sshub.h`'s values for every status bit, change bit, link state,
feature selector, request code and remote wake bit match the tables above.

### 11.5 Link states and resets (USB 3.2 7.4.2, 10.16.2.6.1, 10.3.1)

PORT_LINK_STATE values (Table 10-13, USB 3.2 p.446; verified; the wPortStatus
column is the value shifted to bits 8:5):

| Value | State | wPortStatus with it | How it is left |
|---|---|---|---|
| 0 | U0 | 0000h | |
| 1 | U1 | 0020h | |
| 2 | U2 | 0040h | |
| 3 | U3 | 0060h | SetPortFeature(PORT_LINK_STATE, U0), then C_PORT_LINK_STATE |
| 4 | eSS.Disabled | 0080h | SetPortFeature(PORT_LINK_STATE, Rx.Detect), valid only in DSPORT.Disabled, to DSPORT.Disconnected; also the hub's own upstream-port reset (10.3.1.2, USB 3.2 p.387). A reset is ignored there |
| 5 | Rx.Detect | 00A0h | a far-end receiver detected, then Polling |
| 6 | eSS.Inactive | 00C0h | a warm reset (PORT_RESET or BH_PORT_RESET both send one from DSPORT.Error), or a detected disconnect (7.5.2, USB 3.2 p.163-164) |
| 7 | Polling | 00E0h | training ends in U0, or falls to Rx.Detect / eSS.Inactive |
| 8 | Recovery | 0100h | |
| 9 | Hot Reset | 0120h | |
| 0Ah | Compliance Mode | 0140h | a warm reset (the only reset that leaves it), or SetPortFeature(PORT_LINK_STATE, eSS.Disabled) (7.5.5.2, USB 3.2 p.188) |
| 0Bh | Loopback | 0160h | a successful Loopback exit (to Rx.Detect) or a warm reset |
| 0Ch-0Fh | reserved | | |

What a downstream port does with each reset request (7.4.2, USB 3.2
p.158-159, and 10.3.1.6, USB 3.2 p.388-389; verified):

- **PORT_RESET** sends a warm reset if the link is in U3, Loopback,
  Compliance Mode or eSS.Inactive (and from DSPORT.Error always), and a hot
  reset from U0, Polling or Recovery; from U1 or U2 it exits to Recovery and
  sends a hot reset. A hot reset whose TS1/TS2 handshake times out falls back
  to Rx.Detect and a warm reset automatically, the port staying in
  DSPORT.Resetting until the warm reset completes; one that fails on an LFPS
  timeout in U1 or U2 leaves the link in eSS.Inactive.
- **BH_PORT_RESET** sends a warm reset from every link state but eSS.Disabled.
- Both are ignored in DSPORT.Powered-off, Powered-off-reset,
  Powered-off-detect, Disabled and Disconnected.
- A warm reset whose link stays in Rx.Detect for tTimeForResetError (100 to
  200 ms, Table 10-19, USB 3.2 p.460) has failed, and the port goes to
  DSPORT.Disconnected; a Polling timeout during a reset does the same.
- After a successful reset the device may be accessed immediately: USB 3.2
  has no reset recovery interval (9.2.6.2, USB 3.2 p.327). The driver's 10 ms
  (`HCD_SSHUB_RESET_RECOVERY_MS`) is USB 2.0's TRSTRCY kept as margin, a
  policy and not a requirement.

`XhciSsHubResetKind` encodes the driver's choice: hot from U0, U1, U2 or
Recovery with the port connected; warm from SS.Inactive or Compliance Mode
(whether or not the port reads connected) and from any other state with a
connection; none with nothing connected and the link in Rx.Detect,
eSS.Disabled or Polling. It differs from what the hub would choose for
PORT_RESET in one case, a connected port stuck in Polling, where the driver
sends BH_PORT_RESET and the hub would have tried a hot reset first; that is
allowed (BH_PORT_RESET is valid there) and is the driver's policy. Either
change bit is accepted as the end of either reset (`XhciSsHubResetProgress`),
and a port that reads disconnected is taken as a failed reset, which is how
the specified failure (no reset change, DSPORT.Disconnected) shows. A warm
reset retrains the link, which the hub may also report as C_PORT_CONNECTION
and C_PORT_LINK_STATE; the driver clears both after one, since the device did
not leave. After a reset, success is PORT_ENABLE with the link in U0. Warm
resets are bounded per port by `xhci_link.h`'s `XHCI_LINK_MAX_WARM_RESETS`
(three, this driver's number), after which the port is given up and the
device - which falls back to its USB 2.0 path when its SuperSpeed link does
not train (10.8) - appears on the hub's USB 2.0 half.

### 11.6 The extended port status of a SuperSpeedPlus hub (USB 3.2 10.16.2.6.3, Table 10-15)

GET_STATUS(port) with wValue = 2 (EXT_PORT_STATUS) answers 8 bytes:
wPortStatus, wPortChange, and **dwExtPortStatus** (Table 10-15, USB 3.2 p.450;
every row verified there):

| Bits | Field | Notes |
|---|---|---|
| 3:0 | Rx Sublink Speed ID | the SSID of the hub's own SuperSpeedPlus capability attribute the receive sublink runs at; valid with PORT_ENABLE |
| 7:4 | Tx Sublink Speed ID | likewise, transmit |
| 11:8 | Rx Lane Count | **zero-based**: 0 = one lane |
| 15:12 | Tx Lane Count | likewise |
| 31:16 | reserved | (Table 10-15 misprints this row as "12-31") |

It is a Request Error, which the hub signals with a STALL, to ask for
EXT_PORT_STATUS of a hub that defines no SuperSpeedPlus USB Capability
descriptor (USB 3.2 p.445; verified). The condition is the capability, not
bcdUSB; `XhciSsHubHasExtStatus` asks only when both bcdUSB >= 0310h and the
capability are present, which never sends the request to a hub that would
refuse it and skips only a hub with the capability and an older bcdUSB. The
lane rate is that attribute's LSM x 10^(3 x LSE) b/s (LP 1 says
SuperSpeedPlus), and "the speed of a port is determined by multiplying the
Sublink Speed ... by the Lane Count" (10.16.2.6.3, USB 3.2 p.450; verified) -
the SSID indexes the **hub's** BOS attributes, not the device's or the xHC's
PSI table. `XhciSsHubDownstream` reads the Rx sublink (an asymmetric link's
Rx attribute; a Tx-only attribute is not a receive rate). PORT_SPEED in
wPortStatus (11.4) says 0 for every rate, which is why the extended status
exists.

**The xHC is told the rate through the device's Slot Context Speed**, a PSIV
of the root port's protocol (section 10.5 and xHCI 6.2.2): the bus looks for
the PSIV whose PSI DWORD names SuperSpeedPlus at the aggregate rate - a PSI
DWORD's rate is the aggregate (xHCI Table 7-13's PSIM column, p.485; section
10.1) and a BOS sublink attribute's the lane rate (USB 3.2 p.450) - and on a
protocol with no PSI table uses the default IDs: 5 for 10 Gb/s on one lane
(Gen 2x1), 6 for two lanes (Gen 1x2), 7 for 20 Gb/s (Gen 2x2). xHCI 7.2.2.1.2
defines 6 and 7 only for a USB 3.2 group and 5 only for a USB 3.1 or 3.2 group
(section 10.1); the bus does not narrow its choice by the group's revision.
Only those are matches. Failing them, the PSIV named at the lane rate, else
SuperSpeed's, is given as a guess and counted (`superspeed hubs: rates
without an ID`), and after Address Device the speed the controller wrote into
the output Slot Context is taken as authoritative when it names a
SuperSpeed-class rate (`XhciSsHubAdoptSpeed`). That is what xHCI 4.19.9
(p.304-305; verified) specifies: a SuperSpeedPlus device sends a Sublink
Speed Device Notification right after SET_ADDRESS, and an SSP-capable xHC
that receives one during Address Device "shall ignore the value of the Input
Slot Context:Speed field and update the Output Slot Context:Speed field"
before completing the command - which is also why 1.2c calls the input field
"not applicable to USB3 Gen X" (Table 6-4, p.408).

### 11.7 Remote wake masks (USB 3.2 10.16.2.10, Table 10-18)

SetPortFeature(PORT_REMOTE_WAKE_MASK) with wIndex 15:8 = the conditions on
this downstream port that may make the hub signal remote wake upstream:
bit 0 Conn_RWEnable (connect), bit 1 Disconn_RWEnable (disconnect), bit 2
OC_RWEnable (over-current), bits 7:3 reserved; each takes effect only if
Function Remote Wake is also enabled, and an event masked off is still
reported as a change after the resume. After power-on or a hub reset the
mask is zero, every source disabled (Table 10-18 and the paragraph after it,
USB 3.2 p.454-455; verified). This driver suspends no hub (selective suspend
is not on the roadmap, design record 13 section 10.2), so it sets no mask;
the values are transcribed so a later phase starts from them.

### 11.8 What the driver does with this (task 30-A.1)

- A SuperSpeed hub on a SuperSpeed port is brought up like a USB 2.0 one
  (design record 13 section 10.3) with three differences: SET_HUB_DEPTH after
  SET_CONFIGURATION, the 2Ah descriptor, and a Slot Context marked Hub = 1 and
  Number of Ports with **no TT fields** (TTT and MTT 0; xHCI Table 6-6
  conditions both on a High-Speed hub), its status-change endpoint opened with
  its companion at SuperSpeed.
- A device behind it has the Route String and root port the topology graph
  gives (design record 02), Speed its PSIV (11.6), and Parent Hub Slot ID
  and Port Number 0 - except a lower-rank device behind a hub whose own
  upstream link outranks it, which gets the hub's Slot ID and port as xHCI
  Table 6-6 requires, and a device that ranks the same as its hub, which
  inherits the hub's own pair, the boundary being further up (section 10.5;
  implemented, host vectors only).
- Its two halves are two hubs of the bus. Nothing passes between them; a
  counter (`superspeed hubs: halves paired`) records that a SuperSpeed hub
  and a USB 2.0 hub of the same vendor sit at the same tier and route on a
  companion-paired root port.

### 11.9 Not answered by this section

- Whether PORT_CONNECTION reads 1 in Compliance Mode: 10.16.2.6.1 names only
  DSPORT.Resetting and DSPORT.Error as keeping their prior value (11.4); the
  driver warm-resets on the link state alone, so it does not depend on it.
- The bench unit's own answers (`05E3:0610` and `05E3:0612`,
  `test-equipment.md`): its descriptor bytes, bcdUSB, whether it carries a
  SuperSpeedPlus capability, and what it reports on a hot-plug - 30-E.1's
  reading, the first execution of this whole path.
