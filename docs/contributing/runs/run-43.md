# Run 43 - Release `3.2.0.0`

Phase 43 of `roadmap-usb4.md`, for release `3.2.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

## 43.46 read first: a VSD's gain on the 98 SE emulator (2026-10-21)

The read-first step of task 43.46, before any design: the gain of an IOS
vendor-supplied driver (VSD) that drops `DCB_dmd_phys_sgd` above
`SCSIPORT.PDR` on Stor98's disk, measured rather than inferred. Owner,
2026-10-21: the VSD lives in this repository (GPL-2.0), not in Stor98;
nothing of it moves into `src/stor98/`. What the measurement asks of the
design is in "Open for the design" below; nothing else is decided here.

Background: Stor98 `docs/INVESTIGATION-9X-REQUEST-SIZE.md` (static reading
of `IOS.VXD` and `SCSIPORT.PDR`, 2026-10-19). On the `phys_sgd` path that
`SCSIPORT.PDR` demands, IOS's criteria routine fails a request over 512 KiB
or of more than 17 physical runs and cuts it into 63 KiB pieces sent one
after another; IOS takes the demand bits from the topmost calldown entry.
SweetLow's `NVMEVSD.VXD` was not used as the measurement: its reading of
2026-10-18 (Stor98 `docs/gates/G9.md` and its PLAN 0.4) found that it
matches only a DCB whose port name is `nvme2k` and keeps `phys_sgd`
demanded, so it does not lift these checks; no fact was taken from it here.

### The prototype

`S98VSDP.VXD` (6,343 bytes), a static IOS VSD written for this reading in
MASM from the Windows 98 SE DDK's headers, with the DDK's `HELPVSD` sample
read for IOS's calling conventions (the request routine's and callback's
stack frame, how a layer calls the next calldown entry and the callback
above it). Emulator only: never shipped, not in the tree; its source, build
script and image are kept on development host A in the reading's VM folder.
It:

- registers with IOS at `Sys_Dynamic_Device_Init` (`IOS_Register`, load
  group `DRP_VSD_9`);
- at `AEP_CONFIG_DCB` on a physical DCB whose port name is `nvme2k` or
  `stor98`, inserts a calldown entry (`ISP_INSERT_CALLDOWN`) whose demand
  bits are the DCB's with `DCB_dmd_phys_sgd` cleared, with an IOP
  expansion area of 1,616 bytes (room for 200 physical runs);
- for read, write, verify and write-verify requests not already carrying
  `IORF_PHYS_SGDS`, builds the physical (address, length) list that
  `SCSIPORT.PDR` reads at `IOR_sgd_lin_phys` - one `_CopyPageTable` call
  per page of the request's linear buffer or logical scatter/gather list,
  adjacent pages merged into one run - points `IOR_sgd_lin_phys` and
  `IOR_num_sgds` at it, sets `IORF_PHYS_SGDS`, pushes a callback that puts
  the three back, and calls the entry below; everything else passes down
  untouched;
- writes what it sees to port `0E9h` (QEMU's debug console): each DCB at
  configuration, the calldown chain after its insertion and at the first
  request, and a count line every 4,096 built lists.

Toolchain, all from the extracted Windows 98 SE DDK
(`tools\win98ddk-extracted\`, `docs/references/README.md`, "Windows 98
DDK"): `ML.EXE` 6.11d (`all\BINS_DDK\BIN_WIN98_ML.EXE`), `LINK.EXE`
5.12.8181 with `/VXD` (`all\BINS_DDK\BIN_LINK.EXE` and its
`BIN_MSPDB50.DLL`), and the `.INC` headers of `incs\` (`INC_WIN98_*`) and
`all\BLCK_DDK\` (`SRC_BLOCK_INC_*`) copied under their plain names. The
workload tool `PERF2.EXE` was built with MSVC 6.0 from `tools\MSVC600`.

### How a VxD is built here today

Nothing in the tree builds a VxD. Stor98's `stor98.mpd` is a PE image from
the Windows 2000 DDK and MSVC 6.0 (`src\stor98\scripts\build.cmd`, target
`9x`, with `SCSI.H` and `SRB.H` from `tools\w98ddk\inc`); `XHCIDI.EXE` is a
16-bit Open Watcom 2.0 program; the drivers are WDM. The 98 SE DDK's
assembler and linker above produce an LE VxD with no other tool, and
`xhciinst\di\stage-ddk.ps1` is the tree's existing pattern for staging DDK
members under their plain names against the SHA-256s
`docs/references/README.md` records. Open Watcom (at `C:\WATCOM` on
development host A) also links LE images; it was not tried.

### The vehicle

One 98 SE guest image, in two copies run side by side: the QuickInstall
image of Stor98's performance readings (an overlay of
`E:\stor98-vm\perf\98\base.qcow2`; updated IOS set, `IOS.VXD` 69,594 bytes,
`SCSIPORT.PDR` 23,674 bytes, no `NVMEVSD.VXD` in `IOSUBSYS`, checked in
the guest), `usb4-emu`'s QEMU under TCG, one vCPU, 256 MB, a 1 GiB NVMe
disk (F:) behind the emulated USB4 tunnel. Stor98 is the 9x free build of
this branch's `src/stor98/` (`stor98.mpd`, 52,140 bytes), copied over the
image's devnode's port driver file `NVME2K.MPD`, so the DCB's port name is
`nvme2k` (which the prototype also matches). Arm A: Stor98 alone. Arm V:
Stor98 and the prototype. Each arm: a fresh overlay, install, cold boot,
tunnel up, then

1. `PERF2 file F:\P.BIN 256 0`: for each request size 64, 128, 256, 512
   and 1,024 KiB, the 256 MiB file written sequentially with
   `FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH` and a pattern, read
   back unbuffered (both timed), then verified in an untimed pass;
2. the same with the buffer at +512 bytes;
3. a 300 MiB patterned file written through VCACHE; cold boot; tunnel up;
   `CopyFile` of it to a new file on F: (timed), `FlushFileBuffers` on the
   copy, and the copy verified.

The request sizes are QEMU's `pci_nvme_read` and `pci_nvme_write` traces
(the NVMe commands Stor98 issued), grouped per phase by the tool's
debug-port marks. Two pairs were run (A1 with V1, then A2 with V2 on the
swapped slots).

### Readings

**The calldown chain.** The prototype's `AEP_CONFIG_DCB` for Stor98's DCB
arrives before DISKTSD's and before the load group 0x10 layer's, with only
`SCSIPORT.PDR` (load group 0x14, demand bits `0x00000E09`) and load group
0x1F on the DCB. After its insertion the DCB's `DCB_dmd_flags` is
`0x00000609`; the two layers inserted after it copy its bits (load group 7,
DISKTSD, `0x00040600`; load group 0x10, `0x00040608`), so at the first
request the DCB demands `0x00040600` and `phys_sgd` is gone from every
entry above `SCSIPORT.PDR`. The prototype's fallback, clearing `phys_sgd`
from entries above it inserted earlier, was never needed (all four boots of
arm V). `DCB_max_xfer_len` is `0x80000` (Stor98's 9x maximum) and
`DCB_max_sg_elements` 17.

**Throughput**, MB/s (MiB moved / s), 256 MiB per phase; QD 1 throughout
(at most one NVMe write and two reads outstanding):

| Op | Buffer | KiB | A1 | V1 | A2 | V2 |
|---|---|---|---|---|---|---|
| write | +0 | 64 | 27.4 | 25.1 | 36.3 | 34.2 |
| read | +0 | 64 | 40.0 | 36.3 | 46.4 | 49.8 |
| write | +0 | 128 | 55.7 | 45.0 | 57.8 | 53.8 |
| read | +0 | 128 | 56.1 | 54.5 | 60.4 | 56.7 |
| write | +0 | 256 | 65.5 | 58.2 | 66.1 | 61.7 |
| read | +0 | 256 | 67.5 | 65.5 | 78.6 | 72.1 |
| write | +0 | 512 | 84.8 | 72.0 | 84.2 | 78.4 |
| read | +0 | 512 | 91.1 | 62.8 | 87.8 | 78.3 |
| write | +0 | 1,024 | 40.1 | 40.6 | 49.7 | 48.7 |
| read | +0 | 1,024 | 45.8 | 43.9 | 50.5 | 48.6 |
| write | +512 | 64 | 29.9 | 28.2 | 38.3 | 35.4 |
| read | +512 | 64 | 35.5 | 29.8 | 45.4 | 43.3 |
| write | +512 | 128 | 25.5 | 45.7 | 35.7 | 54.8 |
| read | +512 | 128 | 29.2 | 51.0 | 38.8 | 62.9 |
| write | +512 | 256 | 33.3 | 61.4 | 38.8 | 77.5 |
| read | +512 | 256 | 37.8 | 57.1 | 40.0 | 75.4 |
| write | +512 | 512 | 35.1 | 72.4 | 41.0 | 77.8 |
| read | +512 | 512 | 35.5 | 76.1 | 41.7 | 78.8 |
| write | +512 | 1,024 | 48.9 | 39.1 | 50.5 | 47.9 |
| read | +512 | 1,024 | 48.0 | 42.0 | 51.0 | 45.4 |
| copy (VCACHE) | - | 300 MiB | 4.7 | 4.3 | 4.7 | 4.6 |

These are TCG numbers on a loaded host: a ratio between arms of one pair
means something, an absolute value nothing about real hardware.

**Request sizes at the NVMe** (KiB: count), the same in both pairs:

| Phase | Arm A | Arm V |
|---|---|---|
| +0, 128 / 256 / 512 KiB | whole (128: 2,048; 256: 1,024; 512: 512) | whole, the same |
| +512, 128 KiB | 63: 4,096 and 2: 2,048 | 128: 2,048 |
| +512, 256 KiB | 63: 4,096 and 4: 1,024 | 256: 1,024 |
| +512, 512 KiB | 63: 4,096 and 8: 512 | 512: 512 |
| either, 1,024 KiB | 63: 4,096 and 16: 256 | the same |
| copy | reads 64 KiB (4,800), writes 32 and 64 KiB | the same |

The +0 and +512 passes differ in more than the offset: the prototype's own
count shows the +0 pass's buffers at no more than 8 physical runs per
request, and the +512 pass's at up to 65 runs for 256 KiB and 129 for
512 KiB, a run a page (the buffer's physical layout changed between the
passes; 256 MB with VCACHE pages it). Arm A therefore got its requests
whole while the buffer was physically near-contiguous and cut to 63 KiB
pieces once it was not, which is the boot-to-boot variation Stor98's E0/E1
readings saw; arm V got them whole either way.

**Data.** Every verify pass of all four arms compared clean (0 mismatched
dwords, 0 failed calls), the four copies included; the prototype never ran
out of room for a list (overflow 0).

### What it shows

- Dropping `phys_sgd` above `SCSIPORT.PDR` makes the 17-run limit go away:
  unbuffered requests of 128 to 512 KiB reach Stor98 whole however
  fragmented the caller's buffer is, and on a fragmented buffer that is
  1.6 to 2.2 times arm A's rate at 128-512 KiB in these guests (arm A
  35-41 MB/s against arm V 72-79 MB/s at 512 KiB).
- It gains nothing where the buffer was already near-contiguous, and arm V
  was up to 31% slower there (512 KiB read in pair 1; at most 11% in
  pair 2): the cost of building the list one `_CopyPageTable` call a page,
  under TCG.
- It does not lift the 512 KiB cut: a request over `DCB_max_xfer_len`
  (1,024 KiB here) is still marked too big and cut into 63 KiB pieces by
  IOS, so 1 MiB requests are as slow with it as without.
- Copies through VCACHE are unchanged (64 KiB reads, 32 and 64 KiB writes,
  the same time), as the roadmap already says: the gain is unbuffered
  sequential I/O only.

### Open for the design

1. Requests over `DCB_max_xfer_len`: let IOS keep cutting them to 63 KiB,
   have the VSD cut them itself into `DCB_max_xfer_len` pieces, or raise
   Stor98's 9x maximum (512 KiB today) towards the controller's MDTS.
   Measured 2026-10-22: "Requests over 512 KiB" below.
2. The list's cost: one `_CopyPageTable` call for a run of pages instead
   of one a page, and whether to build a list at all when the request is
   already within 17 runs (a request IOS would pass whole anyway).
3. Page locking: the prototype, like IOS's own builder, reads the page
   tables and locks nothing, relying on the requester having locked the
   buffer; to be read for every requester the design admits.
4. How it claims only Stor98's disks: the port name (`stor98`, or
   `nvme2k` where a devnode still names that file, as this image does),
   or the DDB, or the INQUIRY data.
5. The pass-through (`IOR_func` `0x0F`) and SMART requests, which the
   prototype passes untouched and which `SCSIPORT.PDR` may need a list for
   once `phys_sgd` is dropped; and `IORF_DOUBLE_BUFFER` requests.
6. The insertion order seen here (the VSD's `AEP_CONFIG_DCB` before
   DISKTSD's, so the later layers copy its bits) on the gold 98 SE IOS and
   on ME's, and with Stor98 present at boot rather than arriving behind the
   tunnel after it.
7. Surprise removal and the tunnel's link-down with a large request in
   flight, `AEP_UNCONFIG_DCB`, and the install and removal in `IOSUBSYS`
   on 98 SE and ME.
8. The toolchain: the DDK's MASM and linker as here (staged like
   `xhciinst\di\stage-ddk.ps1`), MSVC 6.0 with the DDK's `VXDWRAPS`, or
   Open Watcom; and what is taken from the DDK's headers into tracked
   source (`docs/references/README.md` records that none of the DDK's
   declarations or sample code is reproduced today).
   2026-10-22: Open Watcom with the DDK's LINK 5.12 builds a VSD that
   loads, under the rules in "The C port" below.

### The C port (2026-10-22)

Owner, 2026-10-22: the VSD is written in C as far as C can do it, with
assembly only where C cannot, each such line saying why; Open Watcom first,
and MSVC 6.0 with the DDK's `VXDWRAPS` and `LINK /VXD` if Watcom still fails
once the images are compared byte for byte.

`S98VSDC.VXD` is `S98VSDP`'s C port: the same behaviour, counters and
prints, from the 98 SE DDK's C headers (`all\BLCK_DDK` `SRC_BLOCK_INC_*.H`
for the IOP, DCB and DDB, `incs` `INC_WIN98_*.H` for the IOR, ISP, ILB, AEP,
DRP, SGD and IOS), compiled by Open Watcom 2.0 `wcc386` with `-zp1` (the
headers mirror the packed `.INC` layouts; 42 offsets compared equal to
MASM's) and linked by the DDK's `LINK.EXE` 5.12 `/VXD`, which reads
Watcom's OMF objects. Its only assembly is three lines in the control
procedure (call the C handler, `neg eax` to turn its result into the carry
flag VMM reads, `ret`); the two VMM and IOS services it calls
(`_CopyPageTable`, `IOS_Register`) are `#pragma aux` byte sequences, `int
20h` and the service's dword. Like the prototype it stays on development
host A, outside the tree.

**Why no Watcom-built VSD loaded at first.** `wlink`'s own `format windows
vxd` image was never loaded by 98 SE; through `LINK /VXD`, a minimal VxD
loaded, but the full VSD either did not load or stopped 98 SE at "While
initializing device IOS: Windows protection error", and none of its code
ever ran. Page size, pages without fixups, object size, where the DRP sat
and the `int 20h` bytes were each ruled out. The byte comparison of two
minimal images, one that loaded (W3) and one that did not (W5, the same
plus an `IOS_Register` call), found the cause in the linker, not the
loader: in W5 the control procedure's `call ctrl_c` was linked to the
address of another static function (the AER, which writes through its
argument, so the first control message wrote into VMM's stack), and the
DRP's ILB pointer to `ctrl_c`. `LINK` 5.12 mislinks two shapes of OMF
record Watcom writes:

- a static symbol's records (`LEXTDEF` `B4h`, `LPUBDEF` `B7h`): a
  reference can be resolved to a different static symbol of the object
  (W3 loaded only because its order happened to come out right);
- an `EXTDEF` naming a symbol the same object already defined in an
  earlier `PUBDEF`: `LNK2005` "already defined". Watcom writes its public
  definitions in batches as it goes, so a reference to a symbol whose batch
  is already written takes this shape.

A reference written before its definition links correctly. The build
therefore has no file-scope `static`, keeps the data in an object of its
own, and moves every `PUBDEF` of an object to just before its `MODEND`
(a record may sit anywhere after the segment and group definitions it
names, and each keeps its own checksum); two checks fail the build if
either shape is left, or if any call the linker resolved in the image
lands anywhere but the function the compiler's fixup names (the check
flags W5). Also fixed in the copied headers: `AEP.H` declares three
pointer types as `} AEP_x; *PAEP_x;`, which defines an implicit-`int`
global in every object (`} AEP_x, *PAEP_x;`).

With that, the Watcom-built VSD loads and registers on 98 SE
(`IOS_Register` result 1, `DRP_REMAIN_RESIDENT`) and configures Stor98's
DCB as the MASM prototype did; MSVC 6.0 was not needed.

**The C port against Stor98 alone**, run back to back on one guest
(2026-10-22, 14:18 to 14:44; host at 88 to 91 % CPU with 8 to 11 QEMU
guests of other tasks running, so the absolute values say even less than
the earlier pairs'): MB/s, 256 MiB per phase.

| Op | Buffer | KiB | A (Stor98 alone) | C (with the C port) |
|---|---|---|---|---|
| write | +0 | 64 | 14.6 | 11.7 |
| read | +0 | 64 | 19.4 | 15.8 |
| write | +0 | 256 | 39.3 | 31.1 |
| read | +0 | 256 | 43.2 | 30.7 |
| write | +0 | 512 | 48.1 | 42.3 |
| read | +0 | 512 | 54.3 | 46.2 |
| write | +512 | 128 | 14.8 | 23.3 |
| read | +512 | 128 | 14.8 | 25.5 |
| write | +512 | 256 | 18.6 | 30.2 |
| read | +512 | 256 | 19.5 | 31.4 |
| write | +512 | 512 | 24.6 | 39.2 |
| read | +512 | 512 | 26.4 | 40.0 |
| write | either | 1,024 | 31.0 / 32.5 | 28.1 / 28.0 |

The same shape as the MASM prototype's pairs: 1.5 to 1.7 times arm A's
rate on the fragmented buffer at 128 to 512 KiB, 12 to 29 % slower on the
near-contiguous one, nothing gained at 1 MiB. Every verify compared clean,
no list overflowed.

### Requests over 512 KiB (2026-10-22)

The owner's direction of 2026-10-21 ("read more first"): measure, before
the design chooses, a VSD that cuts a large request itself, Stor98's 9x
maximum raised, and both. Four arms, the C port in each, Stor98 the 9x
free build of `p43-46` except where raised:

- **SC**: Stor98 as built (`STOR98_MAX_TRANSFER` 128 pages, 512 KiB) and
  the VSD as above (the baseline: IOS still cuts a request over 512 KiB to
  63 KiB pieces);
- **SS** (option a): Stor98 as built, the VSD built to advertise
  `DCB_max_xfer_len` 2 MiB to IOS and cut a larger request into pieces of
  Stor98's own 512 KiB, one after another, from its callback;
- **BG** (option b): a test build of Stor98 with `STOR98_MAX_TRANSFER`
  512 pages (2 MiB; not committed), the VSD with room for a 2 MiB list at a
  run a page (520 entries; the 200 of the arms above overflowed, and those
  requests failed, in the first try of this arm);
- **BS** (both): the 2 MiB Stor98 and the cutting VSD (which then cuts
  nothing: Stor98's maximum is the 2 MiB it advertises).

`PERF2 file` with 512, 1,024 and 2,048 KiB requests, aligned and at +512,
256 MiB per phase. Round 3 ran the four arms back to back on one guest
(13:32 to 14:18, host 83 to 97 % CPU); round 2 two at a time on two guests
(SS with BG, then SC with BS), heavier load still.

| Op | Buffer | KiB | SC | SS | BG | BS | SC r2 | SS r2 | BG r2 | BS r2 |
|---|---|---|---|---|---|---|---|---|---|---|
| write | +0 | 512 | 38.5 | 33.9 | 35.3 | 37.0 | 29.1 | 34.3 | 34.8 | 25.7 |
| read | +0 | 512 | 58.2 | 46.3 | 52.4 | 52.3 | 40.8 | 48.8 | 49.5 | 36.9 |
| write | +0 | 1,024 | 33.1 | 61.3 | 66.0 | 60.0 | 26.7 | 59.9 | 60.3 | 48.5 |
| read | +0 | 1,024 | 32.9 | 65.0 | 69.2 | 66.0 | 27.2 | 62.0 | 63.8 | 51.7 |
| write | +0 | 2,048 | 38.3 | 70.5 | 80.4 | 86.1 | 27.8 | 75.3 | 79.9 | 67.8 |
| read | +0 | 2,048 | 37.4 | 73.4 | 80.6 | 87.7 | 30.6 | 73.1 | 82.8 | 76.0 |
| write | +512 | 512 | 40.2 | 41.5 | 44.4 | 43.7 | 37.3 | 41.2 | 41.1 | 34.9 |
| read | +512 | 512 | 47.6 | 45.3 | 53.6 | 62.8 | 38.6 | 47.5 | 47.2 | 51.9 |
| write | +512 | 1,024 | 28.2 | 57.7 | 62.5 | 69.0 | 25.1 | 55.2 | 55.8 | 64.2 |
| read | +512 | 1,024 | 28.9 | 62.3 | 66.6 | 70.4 | 25.2 | 58.4 | 60.2 | 74.7 |
| write | +512 | 2,048 | 33.3 | 74.4 | 78.6 | 83.6 | 23.7 | 71.8 | 71.7 | 79.4 |
| read | +512 | 2,048 | 34.8 | 75.9 | 84.3 | 92.1 | 25.7 | 72.2 | 78.8 | 82.0 |

**At the NVMe** (QEMU's `pci_nvme_read`/`write` traces, every phase of
both rounds): SC sends a 1 MiB request as 63 KiB commands (4,096) and one
16 KiB command per request, a 2 MiB one as 63 KiB commands and one of
32 KiB; SS, BG and BS send 512 KiB commands only, 512 per 256 MiB phase,
at every size. The emulator's controller limits a command to 512 KiB
(MDTS), so with the 2 MiB Stor98 the cut to 512 KiB moves from IOS or the
VSD into Stor98's own children; the commands reaching the disk are the
same in the three arms.

**Data.** Every verify of both rounds compared clean, the +512 buffers at
2 MiB included (up to 513 physical runs a request in BG and BS, with
`IOR_num_sgds`, a byte, held at 255: `SCSIPORT.PDR` evidently walks the
list by length, not by that count; not read in its code).

**What the cutting VSD needs** (found on the way, round 1). The first SS
build changed only the IOR's start sector, count and list for each piece,
and the data came back wrong (each 1 MiB request wrote its first half
twice, at the request's own start). The request arrives with
`IORF_SRB_VALID` set: the SCSI'izer, load group `0x0B`, above this VSD's
`0x11`, has already built the SRB `SCSIPORT.PDR` executes, at `IOP_srb`,
for the whole request (function 0, a 10-byte `WRITE(10)`/`READ(10)` CDB,
`DataTransferLength` 1 MiB, `DataBuffer` the linear buffer), and
`SCSIPORT.PDR` reads its CDB, not the IOR. So each piece also rewrites the
CDB's block address and count, the SRB's length, buffer and sort key, and
clears its two status bytes, and the last restores them; a request whose
SRB is not a 10-byte read, write, verify or write-and-verify is refused
rather than cut (none was seen). With that, round 2 and 3's SS compared
clean.

### What it adds for the design

- Requests over 512 KiB: either way of letting them through (a VSD that
  cuts them, or Stor98's 9x maximum raised) takes 1 and 2 MiB unbuffered
  requests from 28-38 MB/s to 58-92 MB/s in round 3; at 512 KiB
  nothing changes. The two are within the run-to-run spread of each other
  here (BG at or above SS in most rows); both together equal the raised
  maximum alone, the VSD then cutting nothing. On this emulator the disk
  sees the same 512 KiB commands either way, so the difference on real
  hardware would come from a controller whose MDTS is over 512 KiB and
  from one IOS round trip a piece against Stor98's children.
- The cutting VSD has to own the SRB the SCSI'izer built, a structure of
  `SCSIPORT.PDR`'s path rather than of IOS's IOR, and restore it; raising
  Stor98's maximum touches Stor98 alone (its PRP-list and NLB checks hold
  at 512 pages) but changes a value Stor98's own `AGENTS.md` and records
  govern, so it is Stor98's change to make.
- The list's room: 200 runs suffice up to 512 KiB at a run a page; 2 MiB
  needs 513 runs and a list of over 4 KiB in each IOP's expansion area.

Harness notes for these runs: under this host load the scripted DOS box
lost the keyboard focus to the tunnel drive's Explorer window, so two
first tries ran without the VSD installed and one leg's later commands
were typed into Explorer; the legs that count proved their DOS box with a
mark in the debug log, checked that the VSD had loaded, and dropped the
VCACHE copy phase (unchanged in every earlier arm). Kept on development
host A in the reading's VM folder (`vm\res-x3*.txt`, `a4`, `c6`, and
`hostload.txt` for the load samples).

Legs owed by this reading: ME; 98 SE with the gold IOS set; a boot with the
disk present at boot; real hardware (the owner's).

Binary-derived facts used here: `legal-provenance.md` section 4, the row
"Task 43.46 read-first".

### Static readings before the code (2026-10-22)

Owed by design record 30 (sections 2.3, 4.3 and 4.7) before its code.
Static only (capstone over the LE images with their fixups applied,
objects mapped at `object * 0x100000`; nothing executed), the QuickInstall
image's `IOS.VXD`, `SCSIPORT.PDR`, `DISKTSD.VXD` and `DISKVSD.VXD`
(copies kept on development host A), and the gold 98 SE CAB members for the
DRPs. Addresses are mapped addresses (object 1 at `0x100000`).

**Which file is which layer**, from each file's DRP (`XXXXXXXX`, then
`DRP_LGN`): `DISKTSD.VXD` "Fixed Disk TSD" bit 7; `DISKVSD.VXD` "SCSI Disk
VSD" bit 16 (`0x10`); `APIX.VXD` "SCSI API XLATOR" bit 11 (`0x0B`);
`SCSIPORT.PDR` bit 21; `VOLTRACK.VXD` bit 5. So the chain the prototype
printed on Stor98's DCB (7, `0x10`, `0x11`, `0x14`, `0x1F`) is DISKTSD,
DISKVSD (the disk SCSI'izer, which builds the SRB), this layer,
`SCSIPORT.PDR`'s miniport entry and a last entry; the "SCSI'izer, load
group `0x0B`" of "Requests over 512 KiB" above is DISKVSD at `0x10`.

**Stores at `IOR_buffer_ptr`** (`[reg + 0x80]` from an IOP): DISKTSD
`0x30161F`, `0x30165E` (a request it builds, `IOR_flags` written whole at
`0x301603`), `0x301EC8` (after `ISP_CREATE_IOP` at `0x301E79`); DISKVSD
`0x10063A`, `0x100884`-`0x1008E8` (its own SCSI requests, the buffer from
`_HeapAllocate` or its own structure), `0x1009E7` (after `ISP_CREATE_IOP`
at `0x10099C`, `IOR_flags` `0x8F09`, a one-entry list slot at
`IOR_sgd_lin_phys`, then the criteria routine through the ILB at
`0x1009F4`); VOLTRACK `0x400778` (after `ISP_CREATE_IOP`). None in a path
passing a request from above. A sweep of `[reg + disp]` stores only: an
IOR addressed through another base is not covered.

**IOS's double buffer.** `IOS_SendCommand` is IOS service 4 (the
service table at `0x1039CC`: `0x100B20`). At `0x100BD3` it tests
`IOR_flags` for `0x08000040` (`IORF_IO_TOO_BIG | IORF_DOUBLE_BUFFER`) and
goes to `0x100D0F`, which allocates pages (`0x100DD3`) before the first
calldown is called at `0x100BED`. The fields it then rewrites are not
read.

**`SCSIPORT.PDR`'s own requests.** `0x100C34`-`0x100D3E` builds a request
of its own: `ISP_CREATE_IOP` through the ILB (`0x100C61`), `IOR_sgd_lin_phys`
set to a slot in the IOP (`0x100C7A`), `IOR_flags` `0x8F09`, the buffer and
count at `0x100CAA`/`0x100CB0`, the criteria routine called (`0x100CBA`, the
only call of it in the image, `IORF_DOUBLE_BUFFER` set on a non-zero
result at `0x100CC7`), then the SRB inside the IOP at `+0xBC`
(`0x100CDA`), `SrbIopPointer` (`0x100CE3`) and `DataTransferLength` from
`IOR_xfer_count` (`0x100CEE`). Its callers and the route the request takes
were not found (no direct call; capstone's linear sweep loses sync around
it). No construction of an SRB for an incoming IOR that has none was
found.

What it changes in record 30: its section 2.3 (and the chain's names in
2.2 and 4.7); a new open item, `SCSIPORT.PDR`'s own requests after the
DCB is configured, release-blocking until read.

## 43.13 the idle poll period, 50 against 500 ms (2026-10-22)

The table `runs/run-38.md` "38.3" left as T8 (deferred by the owner on
2026-10-14, the default staying 50; read then on 2000, XP, XP x64 and
Vista only), completed on 98 SE, ME, 7 x86 and 7 x64 under mode 2.
Runtime, development host A, in three sessions on 2026-10-22, the last
ending at 11:05.

**Verdict: done, the default stays 50** (owner, 2026-10-22). On none of
the four systems did 500 give a gain that could be measured: the idle
processor load, the idle poll rate, plug-to-working and the item-5
counts are the same within noise, with the guest clock checked at about
1.0 in every window. The one counter that moves with the value is
`TmrObsReleasedCount` in the idle window on 7 (about 260 a minute at 50,
about 2018 at 500). `src/xhci98.inf` (both `XhciPollIdleMs` rows) and
`src/xhci98-amd64.inf` already write 50; nothing changes in the tree.

### The vehicle

- QEMU under TCG, headless, no display client. 98 SE and ME 1 vCPU each,
  7 x86 and 7 x64 2 vCPUs each; other agents' guests ran on the host at the
  same time (1 to 16 vCPUs in all), so the idle load is compared only
  within one boot or between boots close in time, never across sessions.
- The guests, each on an overlay of its own over a read-only copy: 98 SE
  with NUSB 3.3 and `3.1.0.0` over a pristine 98 SE image, stock ME with
  `3.1.0.0`,
  7 x86 and 7 x64 from Phase 38's matrix (the `2.4.0.0` dry cut's `f`
  overlays). The kit, its scripts and every snapshot are in
  `E:\xhci98-vm\p43-13` (not in the tree; `RUN-NOTES.md` there).
- The driver: the gated `release` binaries of `dd46948c` (`XHCI98.SYS`
  `0546d544...`, `XHCIHLPR.SYS` `0ae5f606...`, x64 `XHCI98.SYS`
  `5f578d8a...`, the hashes of `out\gated-*-release.txt`) copied over an
  earlier install, not a fresh install: the INF and the registry are the
  earlier install's (`3.1.0.0` on 98 SE and ME). The 98 SE copy was
  checked by its SHA-256 read back from the disk; the NT copies were not
  hashed again (the x64 file read 345,600 bytes, the date of the build).
- The values, under the controller's driver key (`Class\USB\0000` on 9x,
  `Class\{36FC9E60-...}\0000` on 7): `XhciInterruptMode` 2,
  `XhciPollFineTimer` 0, `XhciLogVerbosity` 2, `XhciPollIdleMs` per row.
  On 9x a cold boot after each change; on 7 the value written and the
  controller restarted with `pnpctl`. Every leg's first snapshot read
  `IntrMode` 2, `IntrDelivery` 2 and `PollIdleMsRead` equal to the row's
  value.
- Attached through every leg: a mouse at root port 1, a stick at root
  port 2 (the snapshots' copy target), a USB 2.0 hub at root port 4.

### What each column is

- **Idle load**: the QEMU process's host processor time over 60 s, as a
  percentage of one host processor, the guest at its desktop with nothing
  done (on 9x a DOS prompt open, on 7 an elevated `cmd`); four minutes, or
  as noted.
- **Plug**: plug-to-working of a second mouse, at root port 3 and at the
  hub's port 1 (4.1): `device_add`, then a pointer move of 4 pixels each
  way and a screen capture for every frame; the time to the first of two
  consecutive frames changed only inside a 64x64 box round the pointer.
  Only re-plugs of a known devnode count: a first plug installs a driver
  (98 SE's Add New Hardware Wizard for every new HID devnode, ME's silent
  install of 24 to 26 s, 7's driver install of up to 3.6 s). On ME the
  plugs were made with the DOS prompt closed and 3 s between plugs: with
  no gap a plug picks up the previous one's queued moves (times under
  0.1 s, which cannot be).
- **Item 5**: a snapshot, 60 s of mouse movement with two 4 MB copies to
  the stick at +10 and +32 s, a snapshot; the counters' deltas per host
  minute (`PollRuns`, `PollRunsWithEvents`, promotions). Kicks were 0 in
  every leg.
- **Idle window**: a snapshot, 60 s with no input, a snapshot; `PollRuns`
  and peeks per minute. Events were 0 on 9x in every window.
- **Clock**: the snapshot's guest "local time" against the host time at
  which the snapshot was typed, over each window.

### Results (`dd46948c`, mode 2, `XhciPollFineTimer` 0)

| System, value | Idle load (%) | Plug, root port 3 (s) | Plug, hub 4.1 (s) | Item 5 per minute: runs, with events, promotions | Idle window per minute: runs, peeks | Clock |
|---|---|---|---|---|---|---|
| 98 SE, 50 | 20.7, 10.2, 14.9, 20.5 (boot c); 24.3, 24.1, 24.5, 24.3 (boot f) | 0.22, 0.21, 0.20 | 0.25, 0.25, 0.22 | 14916, 894, 865 | 5851, 703 | 0.99 to 1.02; 1.000 (f) |
| 98 SE, 500 | 20.3, 20.7, 20.9, 21.8 (boot d) | 0.19, 0.21, 0.17 | 0.21, 0.46, 0.20 | 14656, 888, 861 | 5491, 700 | 0.99 to 1.02 |
| ME, 50 | 13.2, 13.7, 14.7, 13.0 (boot g) | 0.50, 0.45, 0.45, 0.33 | 0.49, 0.43, 0.51, 0.50 | 13481, 881, 857 | 6048, 706 | 0.99 to 1.000 |
| ME, 500 | 13.7, 13.9, 14.3, 15.0 (boot f) | 0.52, 0.48, 0.53, 0.52 | 0.50, 0.46, 0.33, 0.53 | 14723, 881, 864 | 6105, 710 | 0.98 to 1.005 |
| 7 x86, 50 | 12.9 (settling after the boot), 4.8, 5.4, 5.3 | 0.21, 0.24, 0.22, 0.23 | 0.24, 0.25, 0.24, 0.25 | 3841, 992, 901 | 3882, 1032 | 0.996 to 1.004 |
| 7 x86, 500 | 6.0, 4.8, 4.9, 5.5 | 0.24, 0.24, 0.21, 0.25 | 0.24, 0.26, 0.26, 0.26 | 3851, 987, 893 | 3856, 1027 | 0.996 to 1.004 |
| 7 x64, 50 | 4.9, 4.4, 3.6, 4.9 | 0.22, 0.22, 0.20, 0.21 | 0.23, 0.26, 0.24, 0.25 | 3851, 996, 898 | 3817, 1018 | 0.996 to 1.004 |
| 7 x64, 500 | 4.6, 4.3, 4.3, 4.3 | 0.21, 0.22, 0.22, 0.21 | 0.25, 0.32, 0.25, 0.24 | 3851, 988, 894 | 3851, 1023 | 0.996 to 1.004 |

The 98 SE pair is boots c (50) and d (500), close in time, with boot f's
50 idle window and load taken later under a heavier host (the load of
24 % is the host's, not the value's: boot c read 10 to 21 % at the same
value). ME's pair is boots g (50) and f (500), back to back under the
same host load. ME's first 50 boot read the same (load 17.5 to 22.9 %,
plugs 0.42 to 0.59 s, item 5 14331 runs, idle window 5952 runs and 707
peeks). On 7 each guest read 50, 500 and 50 again in one boot (the
values switched by `pnpctl`); the 50 rows are the second pass, whose
plugs met known devnodes (the first pass installed the drivers; its load
was 15.2, 4.6, 12.5, 3.5 % on x64, bursts after the boot).

**The timer's holds and releases.** With `XhciPollFineTimer` 0 there is
nothing to hold: `TmrHolds` and `TmrReleases` were absent from every 7
snapshot, and 9x has no timer helper (absent by design). The phase
observation's released-state count (`TmrObsReleasedCount`, design record
15 section 7.2.9: consecutive poll runs at the same short arm, the timer
released) is the one counter that moved:

| | Idle window per minute, 50 | 500 | Item 5 per minute, 50 | 500 |
|---|---|---|---|---|
| 7 x86 | 262 | 2018 | 3004 | 3451 |
| 7 x64 | 259 | 2018 | 3003 | 3442 |

Its meaning was not read further. A likely reading, not proven: the idle
period is also the window within which a drained event still counts as
recent (`XhciPollRecent` in `hcd_poll.c`), so at 500 the poller stays at
its short arm for longer after each mouse or hub event; the poll rate
does not change, since every arm waits for the clock tick (the 7 guests
poll about 64 times a second at both values, the 15.6 ms tick;
`PollIntervalMinMs` 15 at both). `PollIntervalMaxMs` read 62 at 50 and
15 at 500 on 7 x86, the 500 reading from the controller's restart on.

**Reading.** The poller is not paced by `XhciPollIdleMs` with a mouse, a
stick and a hub attached: 9x polls about 95 to 100 times a second
(about 11.7 peeks a second) and 7 at its tick at both values, so the idle
load cannot fall. The earlier partial reading of 2026-10-14 (`run-38.md`
"38.3", T8, `results.md` section 5) differs on Vista alone, which 43.13
does not cover: the idle load halved at 500 on Vista x86 (12.7 to 5.4 %)
and about 30 % lower on Vista x64, with no difference on XP, XP x64 and
2000.

### Not run

USB 2.0 remote wake on 98 SE and ME (owner, 2026-10-22: not needed
before the tick, since the default is unchanged). The test client
(`pmclient-mouse`) has never been installed on 9x. Remote wake was read
at both values on the x86 NT guests of 2026-10-14 (`run-38.md` "38.3",
T8); 7 x86's own wake legs (`PMI`, `PMW`, `PMS`) were not taken either,
for the same reason, and x64 has no test client.

### The ME guest's clock and the ME hang

- **The slow clock, 08:00.** ME's first boot at 500 (boot d, 07:59 to
  08:09, with the host nearly idle) read the guest clock at 0.645 of host
  time during item 5 and 0.317 during the idle window; its load (3.6 to
  7.7 %), its lower counts and its plugs of 0.56 to 0.80 s followed the
  slow guest (per guest second the idle poll rate was the same, about 88).
  Retaken at 500 (boot f) and at 50 (boot g) back to back, with the clock
  read in every window: 0.98 to 1.005, and the rows above. The slow clock
  never came back. Its cause was not read; it is most likely the hang
  below in a partial form (the system VM blocked for stretches), not
  proven.
- **The hang.** On ME a run of mouse plugs and unplugs with no gap between
  them hung the guest: the tray clock stopped, typed text never echoed,
  and every instruction-pointer sample sat in the VMM's idle loop
  (`c00138c0`, `sti`/`nop`/`nop`/`cli`, nothing to schedule) with an
  occasional V86 sample; no recovery in 3 minutes, the guest quit from
  QMP. Seen at 500 (boot e, 08:54, its first plug series) and reproduced
  at 50 (boot g, 09:59:46, after 24 good plugs, at the hub's port 1), so
  it is not the idle period's. Not tried on 98 SE. It is task 43.49; the
  register samples are in the kit (`legs\me\e\hang-regs.txt`,
  `legs\me\g\hang-regs.txt`).

### What it decides

`XhciPollIdleMs` stays 50 by default on every target (owner,
2026-10-22): 500 gave no gain that could be measured on any system read,
and only `TmrObsReleasedCount` differed. The INFs are unchanged.

## 43.14 read first: whether a VID/PID INF binds a refused interface (2026-10-22)

The reading task 43.15 waited on (was NOX.1's reading; design record 13
section 10.7.1, whose plan of 2026-10-13 it follows except where said).
A storage interface with no transport the bus can run - a UAS-only device
at SuperSpeed on a controller that does not stream - is refused: its one
hardware id is `USB\XHCI98_NOXPORT&VID_v&PID_p&REV_r`, and it has no
compatible id, but until 43.15 its device id kept the `USB\VID_v&PID_p`
form, which task 38.2 found in the 98 SE devnode's `HardwareID` value.
The question: does an INF whose only line is that VID/PID form bind it?
Runtime, development host A.

### The vehicle

- QEMU 11.1.0 under TCG, headless; each guest 1 vCPU (`pentium3,-apic`),
  256 MB, `-action reboot=shutdown`. The controller
  `qemu-xhci,p2=1,p3=4,streams=off`: port 3 is a SuperSpeed port with no
  USB 2.0 companion, and the controller reports no streams.
- The device: `usb-uas` with a `scsi-hd` (a 128 MiB FAT16 image),
  hot-plugged on port 3 after the desktop, `attached` set, each plug with a
  serial never seen before (`NOXW98V1`, ...). VID/PID `46F4:0003`, REV
  `0000`. A `usb-tablet` on port 1 for the pointer.
- The driver: the published `3.1.0.0` x86 package
  (`releases\3.1.0.0`), installed from its INF through the controller's
  Found New Hardware wizard. Every release since `2.0.0.0` answers a refused
  interface's ids the same way, so no build of this branch was needed.
- Three guests, each on its own overlay:
  - **98 SE**: installed fresh from the 98 SE CD on 2026-10-20 for task
    43.5 (no NUSB, nothing added), then `3.1.0.0`.
  - **98 SE + NUSB 3.3**: the same, then NUSB 3.3 English (`nusb33e.exe`,
    its agreement accepted in the guest, owner) and a restart.
  - **ME**: stock Windows ME (`vm\t37me\winme-stock-base.qcow2`), then
    `3.1.0.0`.
- The probes, this project's own INFs written for the reading, never in the
  tree: `Class=USB`, an install section that only writes one value
  (`NoxProbe`) under the driver key, and `.NT` sections with
  `AddService=,2`. Probe V's one line is `USB\VID_46F4&PID_0003`; probe
  C's is `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000`.
- "Binds": the probe's line is chosen for the devnode - its `DeviceDesc`,
  `Mfg`, `Driver` key and Device Manager name come from the probe.

### Results

| Guest | First plug, probe V offered at the wizard | Second plug, V already installed, index deleted |
|---|---|---|
| 98 SE | Wizard for "USB Attached SCSI HBA"; at `D:\PV` it named "NOX probe V device id", `D:\PV\PROBEV.INF`, and installed it. Device Manager: "NOX probe V device id" under Universal Serial Bus controllers | No wizard, no prompt: bound to probe V |
| 98 SE + NUSB 3.3 | The same | The same; "Updating hardware information database" shown at the plug, then bound |
| ME | The same (ME's wizard: "Specify the location", the folder typed) | The same as NUSB's |

Every one of the six devnodes read the same (`regedit /e` of
`HKLM\Enum\USB\VID_46F4&PID_0003` and of `...\Services\Class\USB`, taken
off the disk after a clean shut down), for example 98 SE's second:

```
[HKEY_LOCAL_MACHINE\Enum\USB\VID_46F4&PID_0003\NOXW98V2]
"HardwareID"="USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\VID_46F4&PID_0003"
"DeviceDesc"="NOX probe V device id"
"Class"="USB"
"Driver"="USB\\0003"
"Mfg"="NOX probe"

[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\0003]
"NoxProbe"="V"
"InfPath"="NOXPRO~1.INF"
"InfSection"="Probe.Dev"
"MatchingDeviceId"="USB\\VID_46F4&PID_0003"
```

`MatchingDeviceId` is the device id, the second entry of the `HardwareID`
value; the PDO answers only the first as a hardware id. So Windows 98 SE,
with and without NUSB 3.3, and ME match INF lines against the device id the
configuration manager adds to that value, and an INF whose only line is a
refused device's `USB\VID_v&PID_p` binds it - silently, with no wizard,
once that INF is installed: the wizard had copied the probe into
`INF\OTHER` (`NOXPRO~1.INF`, "NOX probePROBEV.INF"), and with
`DRVIDX.BIN` and `DRVDATA.BIN` deleted before the second plug, the index
was rebuilt and the new device bound from it.

For the second plug the guests were shut down and relaunched cold (98 SE
excepted, which took it in the same session).

### Not read

- Probe C, the control: not offered. V bound, which answers the question C
  was there to qualify (C's line is the id the PDO answers as its hardware
  id).
- Windows 2000 SP4 and NT's matching in general (documented as hardware and
  compatible ids only): not read. Route 2 of the plan (a serial-number
  device's devnode, installed in its working form, reloading its driver in
  the refused form): not read. Neither can bind a refused device after
  43.15, whose device id is project-owned and differs from the working
  form's.
- The NUSB 3.3 `USBSTOR.INF`'s own VID/PID lines: none names `46F4:0003`,
  so they were not exercised; a listed device would match the same way.

### What it decides

By record 13 section 10.7.1's decision 1, the refused PDO's device id
moves to `USB\XHCI98_NOXPORT&VID_v&PID_p[&MI_nn]` on every target: task
43.15.

## 43.16 the legs: a refused interface under the new device id, 98 SE, ME and 2000 (2026-10-22)

NOX.1's legs (design record 13 section 10.7.1) for task 43.15's change,
on the three guests 43.14 read, each continued from the disk 43.14 left:
probe V (`USB\VID_46F4&PID_0003`) installed in `INF\OTHER` and bound to
the two refused devnodes 43.14 made; and on Windows 2000 (below).
Runtime, development host A.

### The vehicle

- As 43.14's: QEMU 11.1.0 under TCG, headless, 1 vCPU, 256 MB,
  `qemu-xhci,p2=1,p3=4,streams=off`, a `usb-uas` with a `scsi-hd` (a fresh
  overlay of the 128 MiB FAT16 image) hot-plugged on port 3 after the
  desktop, `attached` set, a serial never seen before (`NOXW98N1`,
  `NOXN98N1`, `NOXMEN1`).
- The driver: this branch's x86 release `xhci98.sys` (the gated build of
  `66840a9e`, 300,271 bytes, SHA-256
  `81E163EC145B5A99996E5E2F0456F6083A6A4247AFB0B3E786A4C309FF4E005D`),
  copied over `3.1.0.0`'s in `C:\WINDOWS\SYSTEM32\DRIVERS` from an MS-DOS
  Prompt; the INF and the rest of `3.1.0.0` unchanged. The guest shut
  down, the copy read back off the disk with the same SHA-256, then a cold
  boot.
- Guests: 98 SE, 98 SE + NUSB 3.3, ME, as in 43.14.

### Results

| Guest | The plug | The wizard pointed at `D:\PV` (probe V) | The new devnode |
|---|---|---|---|
| 98 SE | Add New Hardware Wizard for "USB Attached SCSI HBA" | "Windows was unable to locate a driver for this device"; Finish: "Unknown Device", no driver installed | `Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW98N1` |
| 98 SE + NUSB 3.3 | The same | The same | `...\NOXN98N1` |
| ME | The same (ME's wizard) | "Windows was unable to locate the software for this device"; Finish | `...\NOXMEN1` |

Every new devnode read the same (`regedit /e` of `HKLM\Enum\USB` and of
`...\Services\Class\USB`, taken off the disk after a clean shut down), for
example 98 SE's:

```
[HKEY_LOCAL_MACHINE\Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW98N1]
"HardwareID"="USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\XHCI98_NOXPORT&VID_46F4&PID_0003"
"DeviceDesc"="USB Attached SCSI HBA"
"Class"="Unknown"
"ConfigFlags"=hex:40,00,00,00
```

No `Driver` value and no new `Class\USB` key: probe V, already installed
and offered again at the wizard, did not bind, on any of the three. The
device id is the second `HardwareID` entry, as 43.14 read for the old form.
The old devnodes, `Enum\USB\VID_46F4&PID_0003\<serial>` (43.14's two per
guest), were left as they were - the same `HardwareID`, `Driver`
(`USB\0002`, `USB\0003`) and probe V's `Class\USB` keys as 43.14 read -
by decision 4; the device did not return to them.

### Windows 2000

The plan's NT target, on a fresh overlay of the project's clean Windows
2000 image (`vm\t26\win2k-clean.qcow2`), the same controller and
device:

1. `3.1.0.0` installed through the controller's Found New Hardware
   wizard from the package's INF, then a restart.
2. A refused `usb-uas` plugged under `3.1.0.0` (serial `NOXW2KO1`), and
   the wizard pointed at probe V's folder: "Windows was unable to locate a
   driver for this device"; "Skip driver installation" chosen.
3. The gated `xhci98.sys` of `66840a9e` copied over `3.1.0.0`'s in
   `C:\WINNT\system32\drivers`, the guest shut down, the copy read back off
   the disk with the same SHA-256, a cold boot.
4. The same device plugged again (the same serial): a new wizard, probe V
   offered again, "unable to locate a driver", skipped.

`regedit /e` of `HKLM\SYSTEM\CurrentControlSet\Enum\USB` and of the USB
class key, off the disk after a clean shut down:

- Step 2's devnode, `Enum\USB\VID_46F4&PID_0003\NOXW2KO1`, its
  `HardwareID` the one id the PDO answered,
  `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000`: 2000 does not add the
  device id to that value as 98 SE and ME do, and probe V's
  `USB\VID_46F4&PID_0003` line did not bind the old form either.
- Step 4's devnode, `Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW2KO1`,
  its `HardwareID` the two NOXPORT ids, no `Driver`; the old devnode left
  beside it, not present (no `Control` key), by decision 4.
- No USB class key past the controller's and the root hub's.

### Not read

- A refused function of a composite device (`&MI_nn`): host vectors only.

## 43.45 - The `amd` host model: 7, 2000 and 98 SE

**Verdict: done** (2026-10-22, ticked on the owner's approval). Every leg
passes on 7, 2000 and 98 SE except the resume, which passes on 7 only.

- The 2000 resume is owed. The ACPI-on 2000 image did not finish: GUI-mode
  Setup looped once the product key was in (below).
- The 98 SE resume is not readable anywhere. On QEMU, the connection
  manager's half passes, and then Windows 98 SE's own post-standby fault
  (36.28 part 2) stops the guest from reading the disk. The owner's real
  machine cannot put 98 SE in Stand by either. So the release notes list
  Stand by with a USB4 or Thunderbolt disk on 98 SE as untested ground.

The owner approved the tick on 2026-10-22 with these two legs recorded
this way. Two findings in XHCITR,
neither in the connection manager: its eject box on 7 waits too short a
time when XHCITR is not elevated, and it warns "controller stopped
answering" across a hibernate. Both are described below; the code was not
changed.

**What `amd` is.** usb4-emu's `minimal` router with AMD's router identity:
vendor `0438`, device `0208` to `020B` by `host-domain`. The host interface
is still the emulator's `1B36:00F4`. So these legs read the connection
manager's vendor checks and quirk table against an AMD router identity.
They do not read AMD hardware, and they do not read the INF's match on
`1022:1668` and `1022:1669`.

**Build and package.** Branch `p43-45` at `1a85f36e`. The gated x86 `all`
build passed (`test_u4cm` 1154 checks). Release `xhcicm.sys` `E827BD54...`,
`xhci98.sys` `15E22D38...`, `stor98.sys` `571AE627...`; the `qemu`-flavour
`xhcicm.sys` `C191766A...`, which writes its log to the debug console. A
TEST cut of the x86 release package (`-TestLabel "p43-45 amd legs"`,
version 3.1.0.0) was installed with `INSTALL /console /yes /thunderbolt
/stor98` on the NT guests and with `XHCIINST` on 98 SE.

**The host and the emulator.** Development host A, QEMU TCG, 2 vCPUs per
guest on 7, 1 on 98 SE and 2000. The emulator was a copy of `vm\usb4\bin-2a893ea`.
Topology: `usb4-host` at 00:04 (`profile=usb4-generic`, `host-model=amd`,
`host-domain` 0 or 3), the SHPC tunnel bridge `tb0` at 00:05 (USB4 port 1),
and `enc0`, the ASM2464 device router (`device-model=asm2464`) with an NVMe
namespace behind it. Kit `E:\xhci98-vm\p43-45` (`RUN-NOTES.md`, shots,
debug-console logs and `usb4_*` traces per leg).

**The data check.** `CHK.BAT` on each leg: `fc /b` of the unit's
`TESTDATA.BIN` against the host's copy, then a 1 MiB `WRITE.BIN` copied to
the unit as `Wn.BIN` and compared back. Every comparison below said "no
differences encountered".

### Windows 7 x86, `host-domain` 0 (`qemu` flavour)

| Step | Reading |
|---|---|
| Identity | Debug console `u4.host.id=04380208`, `u4.quirk=02080002` (the `quirk_clx_disable` row for vendor `0438` matched), `cm.profile=5`, `cm.select.outcome=4` |
| Plug | `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.nvlink.up=01000101`. F: (`ORICO`) mounted under Stor98; W1 compared clean. `INSTALL /devices`: "ASMedia 246x, USB4 (version 1.0), port 1, depth 1, approved, router 174C:2463, PCIe up" |
| XHCITR | "ASMedia 246x (USB4 v1.0), approved"; Generation USB4 v1.0; Link USB4 Gen 3 x2. The `qemu` flavour leaves XHCITR's driver-log pane empty (its log goes to the debug console), so the host id was read on domain 3's release flavour |
| Eject (XHCITR, twice) | The device stopped, `u4.eject=00000100`, trace "PCIe tunnel down reason 1 (admin-down)". The tree afterwards: "PCIe tunnel: closed after an eject". XHCITR's box said "The Thunderbolt driver has not confirmed that the PCIe tunnel was closed" both times (finding 1) |
| Unplug and replug | Generation 2, F: back, W2 compared clean |
| Surprise unplug and replug | `u4.linkdown=01000209`, trace reason 3 (surprise); replug: W2 intact, W3 compared clean |
| Resume | 7 x86 offers hibernate only here. `shutdown /h` with `enc0` attached: `cm.suspend=5`, `u4.suspend.obligations=1`. Relaunched with `enc0` at power-on. The restore showed a black text screen for some minutes under TCG, then the desktop. Debug console: `cm.resume=1`, the host id and quirk read again, `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.reenum.resumed=1`. F: back with W1 to W3; W4 compared clean; Stor98 running; `INSTALL /devices` "approved, PCIe up". XHCITR showed the balloon "Thunderbolt/USB4 warning: The Thunderbolt/USB4 controller stopped answering" after the logon (finding 2) |
| After the resume | Eject from `XHCIINST /console /eject 1` in an elevated prompt: "Windows has stopped Stor98 Storage Controller for NVMe SSDs. The PCIe tunnel was closed." `cm.reenum.confirm` 2.2 s and `u4.eject` 2.4 s after the answer. Replug: W5 compared clean. A second XHCITR eject: the same box as above, `cm.reenum.confirm` 13.8 s after OK |
| Shutdown | `cm.shutdown=2`; `cm.reenum.ups=5`, `downs=5`, `ejects=4`, `confirms=4`, `confirms.refused=0`, `poll-timeouts=0`, `abandoned=0` |

### Windows 7 x86, `host-domain` 3 (release flavour)

The release `xhcicm.sys` copied over the `qemu` one, `enc0` attached at
power-on.

| Step | Reading |
|---|---|
| Identity | XHCITR's driver-log pane (xhcicm): `u4.host.id=0438020B`, `u4.quirk=020B0002`, `cm.select.outcome=4`, `tbt.hw.software-cm=1` |
| Unit | XHCITR "ASMedia 246x (USB4 v1.0), approved", "PCIe tunnel: up - QEMU NVMe Ctrl", the disk drive below it; W6 compared clean |
| Surprise unplug and replug | Trace reason 3, then "PCIe tunnel up", insert generation 2; W7 compared clean. XHCITR's counters: `u4.connects` 2, `u4.pcie.built` 2, `u4.pcie.torn-down` 1 |

### Windows 2000 (`win2k-xonly`, `acpi=off`), `host-domain` 0 (`qemu` flavour)

| Step | Reading |
|---|---|
| Install | `INSTALL` exit 3, restart. The plug raised Found New Hardware for "Stor98 Storage Controller for NVMe SSDs" (unsigned: Yes) |
| Identity | `u4.host.id=04380208`, `u4.quirk=02080002` |
| Plug | `u4.router.up=174C2463`, `u4.pcie.up=00010809`; F: mounted, W1 compared clean |
| Eject (XHCITR) | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (F:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel was closed." `cm.reenum.confirm` 2.0 s and `u4.eject` 2.2 s after OK. The tree: "PCIe tunnel: closed after an eject" |
| Unplug and replug | F: back, W2 compared clean |
| Surprise unplug and replug | `u4.linkdown=0100020B`; replug: W3 compared clean |
| Resume | **Owed.** This image runs `acpi=off` and cannot sleep. The owner's decision of 2026-10-22 was to build an ACPI-on 2000 image for this leg, and to accept the leg as owed if that image had problems. It did. The unattended install (`win2ksp4.ISO`, `-machine pc`, ACPI on, `E:\xhci98-vm\p43-45\base\win2k-acpi.qcow2`) ran text mode and stopped in GUI mode at "Your Product Key". The owner then supplied the key, which is kept in the kit folder only. Setup was resumed from the disk and the key typed over QMP. Setup accepted it and installed the networking components, then went back to "Installing Devices". It ran that stage again and stopped once more with "Unattended Setup is unable to continue because a Setup parameter ... is missing or invalid": the same sequence of screens as before the key, so one clear loop. This is the known loop of 2000's GUI-mode Setup under QEMU 11.1 with ACPI on under TCG, control installs included. The install was stopped there and the leg recorded as owed, per the owner's decision |

### Windows 98 SE, `host-domain` 0 (`qemu` flavour)

The `w98-300` overlay with the tunnel bridge installed from `MACHINE2.INF`
and the NHI's own wizard cancelled; `XHCIINST` installed the package, then
a restart.

| Step | Reading |
|---|---|
| Identity | `u4.host.id=04380208`, `u4.quirk=02080002`, `cm.select.outcome=4`, `cm.reenum.9x.version=0000040B` |
| Plug | `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.reenum.9x.reenumerate=00002800`, `cm.nvlink.up=01000101`; F: (`ORICO`) in My Computer; W1 compared clean |
| Eject (XHCITR) | "Windows has stopped QEMU NVMe Ctrl (F:). Until you unplug it, Device Manager's Refresh, or the Thunderbolt driver looking at the port again, can start it again." Unplug: `u4.linkdown=0100010B` |
| Replug | F: back, W2 compared clean. `cm.reenum.9x.noirq.refused=01080203`: the no-IRQ remedy excluded the NVMe function at 01:01.0 for its header (it had an interrupt line), so the remedy was not needed |
| Surprise unplug and replug | `u4.linkdown=0100020B`, `cm.reenum.9x.query-remove=0`, `cm.reenum.9x.remove=0`; replug: W3 compared clean |
| Resume (Stand by) | `cm.suspend=4`, `u4.suspend.obligations=1`; QEMU `suspended`; `system_wakeup`. Connection manager: `cm.resume=1`, the host id and quirk read again, `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.reenum.resumed=1`, `cm.brnum.9x.held=00002801` (the bridge still had its child, so it was not numbered again), `cm.reenum.9x.reenumerate=00002800`, and seven `cm.reenum.9x.removable=0`. The trace shows the tunnel up again (insert generation 4). Then the first new program (an MS-DOS prompt from Run) stopped with "fatal exception 0E at 0187:BFF9DFFF", again on every key. This is run-36's open 36.28 part 2 defect: KERNEL32 at `BFF9DFFF` after any 98 SE standby on QEMU, with or without a USB controller in the guest |
| Resume, again | A fresh boot, `enc0` hot-plugged at the desktop, F:\ and XHCITR opened before Stand by so that no new program would be needed after the wake. After `system_wakeup` the connection manager read the same as above. Windows then raised Add New Hardware for "PCI standard PCI-to-PCI bridge" and for "Intel 82441FX Pentium(R) Pro Processor to PCI bridge" (the emulated host bridge), and closed the F:\ windows. Closing the first wizard brought up the
second, and the guest was quit there. Not attributed: Windows enumerated the system board's own bridge as new after the wake. Not read further |
| Side reading | A guest reset with `enc0` still attached, so the NVMe was present at POST: a Stand by there never resumed. QEMU went back to `running`, the screen stayed on the Shut Down dialog, and no `cm.resume` came in 4 minutes. That matches run-36's 36.28 part 2 finding that a disk attached at power-on stops a 98 SE resume. Not this task's scenario |

`cm.nvlink.up.no-bus=00000028` after both 98 SE wakes: the bridge's
secondary bus was not readable when the link came up, so no new Stor98
statement was published. On 7 the same moment logged
`cm.nvlink.up.no-channel=00000028`, and the unit worked there.

### Finding 1: XHCITR's eject box on 7, "has not confirmed that the PCIe tunnel was closed"

The cause is not TCG timing alone, and it is not a bridge mismatch. The
cause is that XHCITR does not confirm the eject when it is not elevated.

- `ej_run` (`xhcitr/xhcitr.c`) confirms an eject to the connection manager
  itself (`ej_mark_confirm`, record 21 section 7.4, decision M16) only when
  `ej_is_admin()` is true, and on Vista and 7 that means an elevated token.
  XHCITR was started from the Start menu, so it was not elevated. Its box
  carried no "The eject was confirmed to the Thunderbolt driver" line.
- Without a confirmation the connection manager holds the tunnel. The
  confirmation then comes from the mark service (`xhcimark`), which writes
  `XhciCmEjectConfirm` when its next pass finds the unit held for eject
  (problem 47). Its cadence is 1 s, or 10 s while no NHI answers
  (`mk_poll_ms`), plus a device walk that is slow under TCG.
  `cm.reenum.confirm` was logged 12 to 18 s after OK on 7, and `u4.eject`
  0.2 s after each confirmation.
- `ej_watch_tunnel` gives up 10 s after the eject returned. So it answers
  before the mark service's confirmation arrives.
- Elevated tools and administrators confirm themselves and pass. Elevated
  `XHCIINST` on 7: confirm 2.2 s, "The PCIe tunnel was closed." XHCITR on
  2000 as an administrator: confirm 2.0 s, the same line. The bridge match
  (`CM_ST_E_BRIDGE == q->PortBdf`) is the one these passing cases use, and
  the confirmations name bridge `0x28` in every case.

The fix, for a code task (not made here): when `ej_run` did not confirm
the eject itself (not elevated, or `ej_mark_confirm` returned nothing),
`ej_watch_tunnel` should not use the 10 s bound that assumes a
self-confirmation. Two ways:

- Wait for the mark service's confirmation: about its 10 s idle cadence,
  plus one device walk, plus a margin, so 30 s.
- Or say what is happening: "The Thunderbolt driver closes the PCIe tunnel
  when the eject is confirmed (the xHCI98 mark service does this in the
  background; running XHCITR as administrator confirms it at once)."

The tree already shows the result later ("PCIe tunnel: closed after an
eject"). `XHCIINST`'s watch has the same 10 s shape and would read the same
way unelevated, but it runs elevated.

### Finding 2: XHCITR's "controller stopped answering" balloon after a hibernate

`tr_diff` (`xhcitr/tr_core.c`) raises `TR_EV_STOPPED` when an NHI's
liveness goes from `CM_LIVE_ACTIVE` to any other state. A sleep disarms the
liveness gate (`CmLiveDisarm` sets `CM_LIVE_OFF`, "not monitored"). XHCITR
then sees ACTIVE followed by OFF across the hibernate and warns, although
nothing failed: `cm.resume=1`, the unit came back, and the data compared
clean.

The fix, for a code task: leave `CM_LIVE_OFF` out of the transitions that
raise `TR_EV_STOPPED`. Only a real loss should raise it: suspect, absent,
configuration lost, recovery failed, contained. A `test_tr.c` case should
cover ACTIVE to OFF to ACTIVE.

### What stays owed

- The 2000 resume, on an ACPI-on 2000 guest. One needs either an emulator
  where 2000's GUI-mode Setup does not loop with ACPI on, or real hardware.
  The partial image and its recipe are kept in the kit
  (`E:\xhci98-vm\p43-45\RUN-NOTES.md`).
- The 98 SE resume is not readable on any vehicle held: QEMU (36.28
  part 2), and the owner's machine, which cannot put 98 SE in Stand by.
  It is listed as untested ground.
- The two XHCITR fixes above.
