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

Legs owed by this reading: ME; 98 SE with the gold IOS set; a boot with the
disk present at boot; real hardware (the owner's).

Binary-derived facts used here: `legal-provenance.md` section 4, the row
"Task 43.46 read-first".
