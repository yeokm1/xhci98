# Investigation: why 9x requests reach Stor98 as 63 KiB pieces (2026-10-19)

Static analysis only, plus the emulator data already taken (E0/E1, 2026-10-18). No driver
change. Binaries read: the QuickInstall 98 SE image's `IOS.VXD` (69,594 bytes, SHA-256
7adb5fe3...), `SCSIPORT.PDR` (23,674 bytes, af7610d4...), with Win98 DDK `DCB.H`, `IOR.H`,
`IOP.H`, `ISP.H`, `ILB.H`, `VRP.H`. Addresses are LE object 1 offsets (IOS, SCSIPORT).

## 1. The splitter is IOS's criteria routine, not the 17-element DCB field

IOS's criteria routine (ILB_int_io_criteria_rtn, IOS obj1 0x1592 / 0x15A0) is called from
IOS_SendCommand (0x030B) for every request. With `DCB_dmd_phys_sgd` (0x800) set in
`DCB_dmd_flags`, IOS builds the physical SGD list itself, and it fails the request with
`IORF_IO_TOO_BIG` (IOR_flags bit 27) when any of these hold:

| Check | Code | Limit |
|---|---|---|
| bytes > `DCB_max_xfer_len` | 0x15DB | miniport's MaximumTransferLength (Stor98 9x: 512 KiB) |
| bytes > 0x80000 | 0x16CC | **512 KiB, hard-coded**, phys-SGD path only |
| physical runs > min(17, `DCB_max_sg_elements`) | 0x16E7-0x16F2, 0x17DE | **17, hard-coded** (`mov al,11h`, then min with the DCB byte) |

A run is merged from page-contiguous frames and stops growing near 60 KiB (0x176C).
Misalignment against the word/dword demand bits (0x1631) sends the request to IOS's double
buffer instead (`IORF_DOUBLE_BUFFER`, 0x0317).

A request marked `IORF_IO_TOO_BIG` is cut by 0x1281 into pieces of
`(0xFFFF >> DCB_apparent_blk_shift) - 1` sectors: **126 sectors = 63 KiB** at 512-byte sectors.
The pieces go one after another on the same IOP (queue depth 1). Each piece is checked
again, and a piece that still fails is double-buffered (0x0F63-0x0F80).

This accounts for every measurement so far:
- raising `DCB_max_sg_elements` to 255 (E1) changed nothing, because IOS takes min(17, it);
- `NumberOfPhysicalBreaks` and `MaximumTransferLength` above 512 KiB changed nothing (hard 512 KiB);
- "misaligned" requests of 128 KiB and up split because their test buffers were physically
  fragmented (16-17 runs for each 63 KiB piece in X.txt/Y.txt), not because of the +512 offset;
- file I/O was whole on some boots and split on others (E0A against E0B): the physical
  fragmentation of the user buffer differs from boot to boot.

SweetLow's NVMEVSD 1.0.0.2 raises max_sg only when phys_sgd is *clear*. That fits: only on
the logical-SGD path (0x15F3) does IOS honour `DCB_max_sg_elements` as given.

None of this can be set from the miniport's ConfigInfo. SCSIPORT.PDR inserts its calldown
layer with phys_sgd demanded (DCB fill, obj2 0x18D9: flags 9 | 0x800 | 0x200).

## 2. What can lift it

IOS's ISP_INSERT_CALLDOWN (obj4 0x1268-0x12E2) sets `DCB_dmd_flags` to the flags of the
*topmost* calldown entry. A layer above SCSIPORT can therefore drop phys_sgd from the
demands. IOS then skips the 17-run and 512 KiB checks, and only `DCB_max_xfer_len`,
`DCB_max_sg_elements` (logical SGDs) and alignment remain. In exchange, that layer has to
supply the physical SGDs SCSIPORT needs: SCSIPORT's `ScsiPortGetPhysicalAddress`
(obj1 0x03A6) walks the (address, length) list at `IOR_sgd_lin_phys` by byte offset, with
no count limit. One way to do that is an IOS VSD that builds the list in an IOP expansion
area. A native port driver (.PDR) that replaces SCSIPORT would declare no phys_sgd and
translate pages itself.

Either way, the remaining limit is `DCB_max_xfer_len`, i.e. the controller/driver
transfer size. That matches SweetLow's "limited by controller supported size only", and,
because physical contiguity no longer matters, "stable during long OS functioning". This
is inferred from the code, **not measured**: no prototype was run.

File level: VFAT already issues unbuffered reads and writes of 128 KiB to 1 MiB to IOS whole
(E0B/E1A: 128, 256 and 512 KiB commands at the NVMe; 1 MiB split by IOS). So a lifted limit
would reach large sequential file I/O. VCACHE-built requests are capped by the FSD at
min(VRP_max_sgd, 17) 4 KiB blocks (VRP.H), about 68 KiB, so cached I/O gains nothing.

Hazard found on the way: for an IOR with a logical SG list, SCSIPORT sets
`Srb->DataBuffer` to the first fragment's linear address (obj1 0x07E2-0x0812). Stor98's
bounce path copies through `DataBuffer + offset` (engine.c 877, 1467), which is wrong when
fragments are not linearly contiguous. This is not proven reachable today; any layer that
passes multi-fragment requests whole must keep fragment boundaries page-aligned or split
them, as SweetLow's VSD does.

## 3. Queue depth

Pieces of one split request are serial. Win32 file I/O on 9x is synchronous (no overlapped
file I/O), so a single-threaded benchmark is queue depth 1 whatever the driver does; the
emulator has shown at most 2 outstanding. A queue depth above 1 needs several threads and
is unmeasured.

## 4. Open

- No candidate was built or run; the IOS/SCSIPORT reading above is the evidence.
- Whether layers above (VOLTRACK, DISKTSD) that were inserted before a new layer still carry
  phys_sgd in `DCB_dmd_flags` (an insertion-order question); check with a memory dump.
- Pass-through (IOR func 0x0F) and SMART paths would also need physical SGDs if phys_sgd
  is dropped.

## 5. The bounce hazard: fix and emulator check (2026-10-19)

Fix (branch stor98-spd, commit 334d8d28): on 9x the engine resolves byte offsets of the
SRB's data through the IOR's logical scatter/gather list, as SCSIPORT does
(`src/sg9x.h`, `EngSg9x`/`EngCallerVa`/`EngCallerCopy` in `engine.c`). This applies to both
bounce copies and to the PRP1 page-offset check. Linear buffers and NT are unchanged. Host
test: `tests/unit/sg9x_ut.c`, 15 checks pass. Every flavour builds, and the amd64 warning
count (5) is the same as before the change.

Emulator check (98 SE perf image, NVMe test disk F: on a fresh overlay, debug-port 9x image
with `STOR98_TEST_BOUNCE=1` (commit 88c3b3b0) so that every data child bounces, cold boot
between the phases). Workload: copy 501 files / 75.1 MiB from C:\WINDOWS\SYSTEM to F:\T1
through VCACHE, shut down, cold boot, copy F:\T1 to F:\T2. Compared on the host from the
disk images:

| Engine | Result |
|---|---|
| before the fix | page fault (exception 0E in a VxD, called from 0028:C004B857) during the first copy; reproduced on a second fresh run |
| with the fix | T1 and T2: 501 of 501 files byte-identical to their sources |

So the hazard is real on 9x as soon as a buffered (VCACHE) request is bounced. In normal
operation it is reached only when a child cannot be mapped, which is rare, but not never.

## 6. VSD prototype: not built

Writing the VSD prototype was stopped by a safety check on 2026-10-19 and was not
retried. Sections 1-4 remain static analysis.

## 7. The IOP pointer and list layout across SCSIPORT.PDR versions (2026-10-19)

The dword after the SRB is documented. The Win98 DDK's `block\inc\SCSIPORT.H` defines
`PORT_SRB { SCSI_REQUEST_BLOCK BaseSrb; PVOID SrbIopPointer; /* offset 64 */ ... }`, and the
IOP, IOR and DCB fields come from `IOP.H` (IOP_physical_dcb +0x04, IOP_srb +0x20, IOP_ior
+0x64; callback table depth 6), `IOR.H` (IOR_flags +0x08, IOR_buffer_ptr +0x1C) and `DCB.H`
(DCB_apparent_blk_shift +0x3C). Read from disassembly, not from the DDK: SCSIPORT sets
`Srb->DataBuffer` to the first fragment of a logical SG list, and resolves an offset by
walking that list with the block shift.

Every SCSIPORT.PDR available offline was checked (none downloaded). Sources: the 98 SE CABs
and guest images on this machine, and the ME guest images. No Windows 98 first edition copy
was available.

| Version | SHA-256 | Size, date | Where found | Result |
|---|---|---|---|---|
| 98 SE original (4.10.2222) | 8a0712513067edb7... | 23,650, 1999-04-23 | 98 SE CAB set; two clean 98 SE images | same code |
| 98 SE update (4.10.2223) | af7610d41970a589... | 23,674, 2001-01-09 | QuickInstall image (the perf VM) | same code |
| ME | bfac66edd87be878... | 26,957, 2000-06-08 | three ME images | same code, shifted addresses |

"Same code" means that each version has:
- `ScsiPortGetPhysicalAddress` reading `[Srb+0x40]` and then `[IOP+0xAC]`;
- the offset walk reading `[Srb+0x40]`, testing `[IOP+0x6C] & 2`, walking from
  `[IOP+0x80]` and shifting by `[[IOP+4]+0x3C]`;
- `DataBuffer = [[IOP+0x80]+4]` when `[IOP+0x6C] & 2`;
- the internal SRB build storing `IOP_srb = Srb` and `[Srb+0x40] = IOP`.

The 98 SE original and the update differ in object 1 only in data addresses.

What the code adds on top of that layout (`sg9x.h`, `Sg9xClassify`) is defensive and not
derived from these binaries. The IOP, DCB and list pointers must lie in the system arena,
the IOP and list must be dword-aligned, the IOP's `IOP_srb` must point back at the SRB, and
the first fragment must equal `DataBuffer`. Anything else is classified as unknown. The
request then fails cleanly (HARDWARE ERROR, ASC 44h) instead of being copied through a
guessed address. Byte-counted lists (`IORF_CHAR_COMMAND`) use shift 0.
