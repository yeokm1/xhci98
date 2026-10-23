# Run 43 - Release `3.2.0.0`

Phase 43 of `roadmap-usb4.md`, for release `3.2.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

## 43.11 the installer's settings pages on 98 SE, XP and 7 (2026-10-22)

43.10's layout (design record 26 section 5.4, "The layout") read on the
screen: 98 SE at 640x480, XP and 7 at 96 DPI, 7 at 120 DPI. Runtime,
development host A, QEMU, one guest at a time, 16:54 to 19:11.

**Verdict: passes on every point, on all four.** Every drop-down of every
settings page opened and closed shows its longest entry whole; the note
shows its whole text with no scroll at all; the USB settings page, the
one with four asked rows, is two parts, "(page 1 of 2)" and "(page 2 of
2)", and Back and Next move between them and to the pages either side;
a value set on a part or a page is still set after Back and Next, and
the Summary lists it.

### The vehicle

- The build: the gated build of `3.2.0.0`'s HEAD `2038250e`, on branch
  `p43-11` (`build-driver.cmd all` and `all -amd64` both passed; the
  installer's window self-test 584 checks and `test_wizlay` 199, both
  with no failure). Staged with `make-release.ps1 -Arch x86 -TestLabel
  -NoDriverPack -SkipQualtool` into a scratch directory; `XHCIWIZ.EXE`
  `16ab9fc5...`, the gated build's own file. The package still reads
  `3.1.0.0` (the version is not yet bumped).
- The guests, each on an overlay of its own over another kit's image
  (kit `E:\xhci98-vm\p43-11`, not in the tree; every screen is in its
  `shots\`):
  - 98 SE: p43-12's pristine 98 SE (no display driver: 640x480, 16
    colours), 1 vCPU, 256 MB; then p43-13's `w98-hcd310`, the same
    install with `3.1.0.0`'s drivers on the controller (see below).
  - XP SP3 32-bit: 1024x768, `LogPixels` 0x60 (96 DPI, read from the
    registry), 2 vCPUs, 512 MB.
  - 7 SP1 32-bit: Phase 38's matrix `f` overlay (`2.4.0.0` installed,
    `XhciInterruptMode` 2 on the controller from 43.13's legs), 800x600,
    2 vCPUs, 1536 MB; read at 96 DPI, then with `LogPixels` 120 written
    under `HKCU\Control Panel\Desktop` and Windows restarted (the fonts
    and the window larger on the screen).
- Every guest: a `qemu-xhci` with no device on it, so no Thunderbolt
  controller. The package was on an IDE VVFAT drive; `SETUP.EXE` was run
  inside the guest (Start, Run), elevated through UAC on 7. Keys by QMP
  `send-key`, screens by QMP `screendump`; no display client.
- Each run: on Components, Thunderbolt 3 / USB4 support and then Stor98
  ticked (the staged-only case of section 5.5, so the Thunderbolt and
  Stor98 pages are shown); on each settings page every enabled drop-down
  focused and opened (Alt+Down) and closed, a value changed with the
  arrow keys; Back and Next across the parts and pages; the Summary
  read; Back from the Summary to the USB page's first part; the wizard
  cancelled, so nothing was installed.

### Results

The pages, the same on all four but for the one 98 SE row:

| Page | Rows | Parts |
|---|---|---|
| USB settings | `XhciIntelPortSwitch` (greyed, "(Intel 7-9 series, C610 only)"), `XhciInterruptMode`; then `XhciSilentInstall` (greyed on XP and 7, "(Windows 98 SE and ME only)"; asked on 98 SE), `XhciForceBulkOnly` | two, "(page 1 of 2)" and "(page 2 of 2)" |
| Thunderbolt settings | `XhciCmMode`, `XhciCmApprove`, `XhciCmSafeEject`, each "now: not set (default 1)" | one, no count |
| Stor98's settings | `writethrough`, `pollmode` (`StorWriteThrough`, `StorPollMode` on 98 SE) | one, no count |

- **Drop-downs.** Every closed box spans the column and its choice reads
  whole; every open list is as wide as the closed box and every entry
  reads whole. The longest: "3  the driver's own USB4 connection
  manager, forced", "cacheoff - turns the drive's write cache off (fua if
  it refuses)", "not set (the built-in default is auto (here: off))",
  and on 7 "3  a message-signalled interrupt only". The "now" text sits
  on the label's line at the right, whole, wrapping inside its column
  where long (Stor98's "not set (the built-in default is auto (here:
  off))" in two lines). The greyed rows' boxes show their value whole.
- **The note.** The longest note, `XhciIntelPortSwitch`'s, is nine lines
  at 96 DPI (seven at 120) in a box of about thirteen; every note read
  whole with no scroll. Focusing a drop-down shows its row's note; a new
  part shows its first row's.
- **Parts, Back and Next.** Next on part 1 shows part 2 on the same
  page; Back on part 2 shows part 1; Next on part 2 goes to the
  Thunderbolt page; Back from the Thunderbolt page shows the USB page's
  part 2 (entered by Back, its last part). "Advanced..." from part 2
  (USB, 7 at 96 DPI and 98 SE) returned to part 2.

**Values kept.** Set, then read after Next and Back across parts and
pages, and after Back from the Summary:

| System | USB part 1 | USB part 2 | Thunderbolt | Stor98 | Summary |
|---|---|---|---|---|---|
| 98 SE (update over `3.1.0.0`) | `XhciInterruptMode` 0 to 1 | `XhciSilentInstall` 0 to 1 | `XhciCmApprove` 0 | `StorPollMode` 1 | lists `XhciInterruptMode = 1`, `XhciSilentInstall = 1` (the rest under a scroll) |
| XP, 96 DPI (fresh) | 0 to 1 | `XhciForceBulkOnly` 0 to 1 | 0 | `pollmode` 0 | `XhciInterruptMode = 1`, `XhciForceBulkOnly = 1`, `XhciCmApprove = 0` |
| 7, 96 DPI (update over `2.4.0.0`) | 2 to 3 | 0 to 1 | 0 | `pollmode` 1 | `= 3`, `= 1`, `= 0` |
| 7, 120 DPI | 2 to 3 | 0 to 1 | 0 | `pollmode` 0 | `= 3`, `= 1`, `= 0` |

Each value was in its box on every return.

- **The Advanced dialogs** (43.10's; read here once each, not a 43.11
  point): the USB one on 7 at 96 DPI and on 98 SE, `XhciFastPollFsLs`'s
  list whole, the dialog inside 640x480; the Thunderbolt one on 7 at 120
  DPI, `XhciCmLogVerbosity`'s list whole.

### Seen on the way, not 43.11's points

- **98 SE with no controller listed has no USB settings page.** On the
  pristine 98 SE, its Add New Hardware wizard for the controller
  cancelled, "What was found" says no device is present and the wizard
  goes from Components to the Thunderbolt page: a value with no
  controller stays off the pages (section 5.4). The 98 SE leg was
  therefore read on the same install with `3.1.0.0` on the controller,
  as an update.
- **7 at 120 DPI on 800x600**: the window (about 635 by 575) is taller
  than the work area; its frame below the buttons sits under the
  taskbar, the buttons whole. A screen size, not the pages' layout.
- **Components**: with the keyboard, ticking Thunderbolt and then Tab,
  Space on Stor98 left it unticked on all four; a second Space ticked
  it. Not read further (the Components page is not 43.10's).
- **`XhciCmLogVerbosity`'s value 4** is listed as a bare "4" in the
  Thunderbolt Advanced dialog (the table carries four meanings, for 0 to
  3), and the note reads "4: allowed."; its sentence is in the note's
  NOTE. A text, not the layout.
- **The cut's text checks refuse HEAD**: `make-release.ps1` stopped on
  two readme lines past 78 columns, `xhciinst\README.TXT` line 313 ("...
  it never changes it.", from the 43.34 merge) and `xhcitr\README.TXT`
  line 90 ("... On Windows 2000 without administrator", from 43.52). The
  test package was staged with both lines rewrapped in the working tree
  for that run only; neither change is committed here.

### Owed

Nothing for 43.11. The three findings above are for the owner; the two
readme lines must be rewrapped before the `3.2.0.0` cut.

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
`IOR_xfer_count` (`0x100CEE`). The builder's function starts at `0x100BDF`;
its two callers are `0x100707` (a `REQUEST SENSE`, CDB `03h`, its 0x40-byte
buffer in the IOP, completion routine `0x100D42`) and `0x2019ED` (object 2,
no buffer, IOR function `0x0F`). Stor98 answers `REQUEST SENSE` in software
(`ScsiCompletesInSoftware`, no DMA), so that request's unfilled list slot
is never walked. No construction of an SRB for an incoming IOR that has none was
found.

The builder is the same instruction sequence, with two callers and one
`REQUEST SENSE` CDB set-up, in the gold 98 SE (`0x100BDF`; callers
`0x100707`, `0x201B8D`) and ME (`0x100C7F`; `0x1007A7`, `0x301B25`)
`SCSIPORT.PDR`. The files, sizes and hashes are `legal-provenance.md`
section 4's row "Task 43.46 design: the layers above Stor98's disk and
`SCSIPORT.PDR`'s own requests".

What it changes in record 30: its section 2.3 (and the chain's names in
2.2 and 4.7); a new open item, `SCSIPORT.PDR`'s own requests after the
DCB is configured, release-blocking until read.

### STOR98.VXD's smoke leg (2026-10-22)

The tree's layer (`src/vsd`, the `qemu` flavour's image with the test-only
`VSD_TEST_NVME2K` match, since the read-first image names Stor98's file
`NVME2K.MPD`) and this branch's 2 MiB Stor98 (`stor98.mpd` of the gated
build, copied over `NVME2K.MPD`) on the read-first vehicle (98 SE,
QuickInstall IOS, one vCPU, 256 MB, the NVMe behind the emulated tunnel),
one guest. The kit's `leg4.sh`: install, cold boot, tunnel up, then `PERF2
file F:\P.BIN 64 <off> 1024,2048` at +0, +512 and +0 on a fragmented
buffer (32 MiB touched and every other page given back first), each verified.
Host at 85 to 95 % CPU.

**What the first tries found.** The layer loaded (`IOS_Register` 1) and
claimed the DCB (its entry at load group `0x11` with demand bits `0x609`,
`SCSIPORT.PDR`'s `0xE09` below), but refused every request: the SRB's
`DataBuffer` at this layer is not set yet - 0 on DISKVSD's own requests
(`IOR_func` `0x0C`, flags `0x8F01`, an 8-byte buffer in DCB space) and the
previous request's value in the reused SRB on reads (`0x00`, flags `0x8D01`)
- while `DataTransferLength` was right. `SCSIPORT.PDR` sets `DataBuffer`
below from the IOR. The first-byte check was removed (`5694d1a9`; record 30
section 4.3); the length coverage stays. A try with Stor98's devnode
pointed at `stor98.mpd` by a registry edit (`PERF2 setport`) brought no
disk at all and was not pursued; the legs with the installer read that
route.

**Result (`sm8`)**: no refusal, every verify "mismatched dwords 0":

| Buffer | KiB | write MB/s | read MB/s |
|---|---|---|---|
| +0 | 1,024 | 34.6 | 67.0 |
| +0 | 2,048 | 79.7 | 91.4 |
| +512 | 1,024 | 52.9 | 65.5 |
| +512 | 2,048 | 82.2 | 105.9 |
| +0, fragmented | 1,024 | 53.2 | 82.9 |
| +0, fragmented | 2,048 | 93.9 | 96.9 |

At the NVMe (QEMU's traces): 512 KiB commands only, up to four outstanding
at 2 MiB - each 2 MiB request reached Stor98 whole and was split at the
emulator's MDTS. (The Stor98-alone baseline of "Requests over 512 KiB" was
28 to 38 MB/s at 1 and 2 MiB.)

### Code 10: the file name (2026-10-22)

**The bench.** The first bench package (`p43-46-bench` `e4633c12`, way B
by hand: the 2 MiB `stor98.mpd` and the layer as `STOR98.VXD`) on the
owner's ThinkPad P14s, 98 SE 4.10.2222 A, broke the drive. After a cold
start, an enclosure plugged in after Windows had started got no drive
letter, and Device Manager showed Code 10 on Stor98's controller ("Stor
NVMe"). XHCITR still showed the tunnel's PCI configuration correctly.
With `STOR98.VXD` deleted and the 2 MiB `stor98.mpd` kept, the drive
came back after a cold start and a plug-in (the owner).

**In the emulator.** One guest at a time on the read-first vehicle (98 SE,
the QuickInstall IOS, 256 MB, one vCPU, the tunnel brought up after the
desktop), with the bench's exact `stor98.mpd` copied as `STOR98.MPD` and
the devnode's `PortDriver` set to `stor98.mpd` (`PERF2 setport`). The kit's
`rp2.sh`, Stor98's devnode state as the connection manager logs it 3 s
after the enumeration, then `PERF2 file F:\Q.BIN 8 0 64,2048`:

| Run | In IOSUBSYS beside `STOR98.MPD` | Devnode problem | F: | 2 MiB write / read MB/s |
|---|---|---|---|---|
| `sp1` | the bench's layer as `STOR98.VXD` | 10 (Code 10) | `open` fails (error 3) | - |
| `sp0` | no layer | 0 | verify 0 | 46.2 / 43.2 |
| `sp2` | the same bytes as `S98VSD.VXD` | 0 | verify 0 | 83.3 / 87.9 |

The layer logged nothing for that disk in `sp1`: Stor98's start failed
before any DCB was made. The cause is the file's base name. Windows loads
an IOSUBSYS driver by it, and with a `STOR98` present, `stor98.mpd` does
not start. The exact mechanism in `IOS.VXD` or `VXDLDR.VXD` is not read
[hyp]. Nothing in the layer's code took part, and nothing in the 2 MiB
Stor98 did. `sp2`'s 2 MiB rates, twice `sp0`'s, point to the release
layer claiming port `stor98`; the release image writes no log, so this
is inferred.

Why the earlier legs passed: every one ran on the read-first image,
whose Stor98 file is `NVME2K.MPD`, with the `qemu` build's `NVME2K`
match. A run before the smoke leg (`sm2`) set `PortDriver` to
`stor98.mpd` with the layer as `STOR98.VXD`. It showed the same devnode
problem 10 ("no disk at all", above) and was set aside rather than read.

**The fix** (owner, 2026-10-22): the file is `S98VSD.VXD`, its device
name, everywhere. The installer removes this project's `STOR98.VXD` on
every 98 SE and ME run. `make-package.ps1` refuses two IOSUBSYS drivers
with one base name, and `test_inst` holds Stor98's IOSUBSYS files
against the layer's name (record 30, sections 3.1 and 7.3).

**After the rename, in the emulator** (`cl.sh`, the same vehicle and
`stor98.mpd` setup). `cq1` ran the `qemu` image of `7bff66a2` (no
`NVME2K` match) as `S98VSD.VXD`: devnode problem 0, and the layer logged
`claimed dcb/dmd ... 00000609`, its entry at load group `0x11` above
`SCSIPORT.PDR`'s `0x14`. `PERF2 file F:\P.BIN 64 <off> 1024,2048` at +0,
+512 and +0 fragmented then verified clean, with no refusal line; at
+0, 2 MiB wrote at 117.0 and read at 126.4 MB/s. `cn1` ran the
release image of `7bff66a2`, which differs from the first bench's image
only in its 10-byte stamp: devnode problem 0, and the 2 MiB phases at +0,
+512 and fragmented verified clean.

### The first real-hardware reading (2026-10-22)

The owner's bench. The machine is a ThinkPad P14s running 98 SE
4.10.2222 A, with 32 GB under R. Loew's PATCHMEM (its patched `VMM.VXD`
and `VCACHE.VXD`). `IOS.VXD` is 69,594 bytes of 2001-01-09.
`SCSIPORT.PDR` is the gold one (23,650 bytes, 1999-04-23).
`DISKTSD.VXD` is 18,663 bytes of 2007-10-27, the size of ME's. The
drive is an NVMe drive behind the USB4 tunnel, FAT32 as d:.

Both arms ran the first bench package's 2 MiB `stor98.mpd`. "Layer" is
that package's layer image renamed `S98VSD.VXD`, with `STOR98.VXD`
deleted; "no layer" is the same `stor98.mpd` alone. Each arm was
measured with `PERF2 file d:\P.BIN 256 <off> 128,...,2048` (unbuffered,
write-through). Every verify was 0 and every bad count 0. In MB/s at +0:

| Request | write, no layer | write, layer | read, no layer | read, layer |
|---|---|---|---|---|
| 128 KiB | 104.0 | 220.1 | 142.4 | 350.6 |
| 256 KiB | 190.1 | 533.3 | 170.5 | 541.2 |
| 512 KiB | 210.8 | 682.6 | 187.8 | 785.2 |
| 1 MiB | 222.4 | 800.0 | 200.0 | 996.1 |
| 2 MiB | 229.3 | 876.7 | 209.1 | 1,122.8 |

At +512 the rates were within 5 % of these (2 MiB: 836.6 write, 1,075.6
read).

These are not cache effects. The tool is the same in both arms, and its
file is opened with `FILE_FLAG_NO_BUFFERING` (and
`FILE_FLAG_WRITE_THROUGH` for the writes). Each read compares data the
write put there. 1,122.8 MB/s is below the 1,396 MB/s Windows 10 read on
the same drive at Q1T1 (roadmap 43.46).

The layer claiming port `stor98` is inferred from the rates; the
release image writes no log. XHCIPERF's Stor98 page (43.50's counters)
would show the request sizes reaching Stor98.

This run counts as the layer's pass on real hardware (owner, 2026-10-22);
no confirming run with the second bench package is needed. The run's file
was the first package's image renamed (SHA-256
`1b04c554fb4524d53cf91c715075ae9c3a5d5d5cf51771662e77c5addcb60e47`). The
image built from `7bff66a2` and shipped differs from it only in the 10-byte
stamp, `S98VSD.VXD` where it read `STOR98.VXD` (SHA-256
`85c2ac4b7488ca745e53d0293441a83389c9341189e6cfa08d5af7fda583b9f3`).

Why 128 KiB gains as well: on the physical-list path
`SCSIPORT.PDR` sets the DCB's `DCB_max_sg_elements` to
`min(NumberOfPhysicalBreaks, 17)`. This is read in the gold build
[static], object 2 offset `0x1A1D`. IOS therefore cuts any buffer of
more than 17 physical runs, whatever its length. On a 32 GB machine a
128 KiB buffer's 32 pages are seldom that contiguous. In the 256 MB
emulator they mostly are, which is why the emulator gained less.

No layer against 3.1.0.0's Stor98 (the owner's before and after files,
same machine): writes were 2 to 3 % slower from 256 KiB up, and 15 %
slower at 128 KiB; reads were within 1.5 %. The write deficit is about
30 ms per 256 MiB phase at every size from 256 KiB, whatever the request
count. A per-request cost such as 43.50's counter hook (`EngPfSrbIn`, a
few loads per SRB) would grow with the count and would show in the
reads, which are equal. The more likely cause is the drive's or the
file's state between the runs: the 128 KiB write is the phase that
follows the file's creation. A fair second run deletes `P.BIN`, cold
starts, runs `PERF2` twice per boot and keeps the second run, and
alternates the arms.

The cost table (Stor98 alone against Stor98 with the layer, near-contiguous
and fragmented, back to back in the emulator; record 30 section 9.2 item 2)
is dropped for 3.2.0.0 (owner, 2026-10-22). A cost or speed task opens
after 3.2.0.0 only if Windows 7's Q1T1 reading on the same drive is much
faster.

### The legs of section 9.2 (2026-10-23)

These are record 30's section 9.2 legs, run overnight one guest at a time
(TCG, one vCPU, 256 MB). There were three vehicles:

- **98 SE gold.** The kit's `i98`: an overlay of the t26 gold 98 SE with
  xHCI98 3.0.0.0. It uses the generic USB4 profile (`usb4-host
  profile=usb4-generic`, bridge 00:05) with no TB3 xHCI and no internal
  NVMe. With p42-s98tun's layout (`usb4-tb3-xhci` at 00:07, an internal
  NVMe at 00:09) the hot-plugged NVMe got Code 29 ("did not give it an
  IRQ"), as in `run-40.md`; without the internal NVMe it mounted.
- **98 SE QuickInstall.** The read-first vehicle: the 2 MiB `stor98.mpd`
  as `STOR98.MPD`, with `PortDriver` set to `stor98.mpd`.
- **ME.** The p43-13 ME image with xHCI98 3.1.0.0, the same generic
  profile.

The package was this branch's test cut ("43.46 legs, S98VSD.VXD") at
`8f99acde`; its `S98VSD.VXD` is `85C2AC4B...`. The `qemu` image is the same
code with its debug-port lines. Every verify was 0 unless said otherwise.

**The installer, 98 SE gold, console route** (`INSTALL.BAT /console`,
read back from `XHCIINST.LOG`, `IOSUBSYS`, `WININIT.INI` and `regedit
/e` of the record). A cold start and a plug after the desktop came between
the install and the first I/O.

| Run | Switches | Table (the log) | After |
|---|---|---|---|
| install over 3.0.0.0 | `/yes /thunderbolt` | 16, 17, 10, 13, 0 | `S98VSD.VXD` placed; `File` = `85C2AC4B...` |
| update, same bytes | `/yes /thunderbolt` | 0 | "as it is" |
| untick | `/yes /thunderbolt /nos98vsd` | 15, 14, 0 | file and key gone |
| retick | `/yes /thunderbolt /s98vsd` | 16, 17, 10, 13, 0 | placed again |
| old name, a test cut's queued update (`STOR98.VXD` = the first bench's bytes, `S98VSD.TMP` the same, `Pending` its hash, `WININIT.INI` `STOR98.VXD=S98VSD.TMP`) | `/yes /thunderbolt` | 12, 13, 0 | `STOR98.VXD` deleted ("this package's layer under its old name"), the line taken back (`[rename]` left empty), the `.TMP` and `Pending` deleted, `File` kept; after a restart no `STOR98.VXD`, and the drive verified 0 |
| old name alone (the bench's way B) | `/yes /thunderbolt` | 0 | `STOR98.VXD` deleted, identified by its DDB and stamp |
| uninstall | `/uninstall /yes` | - | `S98VSD.VXD`, `STOR98.MPD` and the key gone; no `WININIT.INI` |

On ME the install read 16, 17, 10, 13, 0 as on 98 SE. ME's first
shutdown after that install hung in VMM (EIP `C005255A`, unchanged over a
minute) and was stopped from QMP. The layer was not loaded in that boot,
since IOS loads it at the next start. Every later ME shutdown, with the
layer loaded, was clean.

**Data and insertion.**

| Vehicle | Arrival | Image | Phases | Result |
|---|---|---|---|---|
| 98 SE gold | plugged after the desktop | release, installed | 1 and 2 MiB at +0, +512, fragmented, then +0 again | verify 0; 2 MiB read 125 to 166 MB/s |
| ME | plugged after the desktop | release, installed | 1 and 2 MiB at +0, +512, fragmented | verify 0; 2 MiB read 185 to 213 MB/s |
| ME | plugged after the desktop | `qemu` | the same, then 128 KiB, 1 and 2 MiB again in the same boot | claimed `0x609` at `0x11`; most runs 257 (1 MiB +512), 431 (2 MiB +512), 512 (2 MiB fragmented), all as whole requests; no refusal |
| QuickInstall | present at boot (the tunnel up before Windows) | `qemu` | 1 and 2 MiB at +0, +512, fragmented | claimed at boot; most runs 512 at 2 MiB |

**Pass-through and SMART** (QuickInstall, `qemu` image): `t9x aspi`
returned INQUIRY "QEMU NVMe Ctrl", VPD 80h "USB4EMU01", READ CAPACITY
2,097,151 x 512, LOG SENSE 2Fh, ATA PASS-THROUGH IDENTIFY (serial, firmware,
model) and SMART READ DATA, and READ/WRITE(10) of 1 to 255 sectors at
offsets 0, 4, 512, 2052 and 4092, with 0 failures. `t9x abs F 60000` and
`t9x file` also had 0 failures.

**Surprise removal** (ME, release image): the enclosure was pulled twice
during 2 MiB phases. PERF2 reported errors ("bad 32", `open` failed 31)
and nothing hung. After a replug, 1 and 2 MiB verified 0. (The "bad 1"
lines before the second pull were the volume filling up, 128 MiB of files
on 126.7 MiB free, not the layer.)

**Memory pressure** (QuickInstall, `qemu` image): `PERF2 file ... eat=230`
commits 230 MiB and touches it in a thread beside the phases, more than
the 256 MB guest has. 1 and 2 MiB at +0 (4 to 32 MB/s while paging) and
on a fragmented buffer verified 0. A first form, a 300 MiB eater in its
own window, thrashed until no DOS box could be opened, and was stopped.

**Fault injection** (QuickInstall, the `qemu` flavour with
`VSD_TEST_DEFS=-dVSD_TEST_REFUSE=37` and kind 1 or 2, never packaged): every
37th request with data was refused before any IOR field changed, 121
refusals in each run. PERF2 at 1 and 2 MiB, 64 and 128 KiB had bad counts
of 0 and verified 0. The refused requests were retried above the layer,
with no hang and no visible second completion.

**`SCSIPORT.PDR`'s own sense fetch** (Stor98's test build
`STOR98_TEST_NOAUTOSENSE=7` with `STOR98_DBGPORT`, never packaged): every
seventh READ(10) completes CHECK CONDITION, its sense kept and no autosense.
Stage `0x17E` logs it with the READ's `DataBuffer`, and `0x17F` logs each
REQUEST SENSE with its own:

| SCSIPORT.PDR | Injected | REQUEST SENSE | The REQUEST SENSE's buffer | PERF2 64 KiB, 1 and 2 MiB |
|---|---|---|---|---|
| QuickInstall (23,674 bytes, 2001-01-09) | 330 | 330 | `C26B96B8`, `C26B6BF0` (VxD memory; the READs' were `C6xxxxxx`) | verify 0 |
| gold (23,650 bytes, 1999-04-23), copied over it | 323 | 323 | `C26B96B8`, `C26B6BF0` | verify 0 |
| ME's | 320 | 320 | `C2E2E518`, `C2E2BA40` | verify 0 |

That is the port's own REQUEST SENSE, answered from Stor98's kept entry,
and the read retried. `ScsiPortGetPhysicalAddress` calls were not logged,
so record 30's "no translation for it" is not read. The i98 kit needed
`-device usb4-debug-port` for Stor98's debug-port lines; without it the
test build injected without logging.

After review the test build was revised (`b5008f40`): the injected
failure ends earlier kept sense and finishes through `EngStartIoTail`.
The QuickInstall row was run again with it: 330 injected, 330 REQUEST
SENSE, and 64 KiB, 1 and 2 MiB with `t9x file` all verified 0.

**43.50's free-memory finding.** These readings were each a new boot's
first runs, apart from the fragmented phases and the second runs noted
above. The P14s "no layer" arm ran after an XHCIPERF run in the same boot.
Record 30 section 9.2 carries the caveat.

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

## 43.18 - The 9x Link-Down Statement's Gate-G3 Readings on 98 SE and ME
Task 43.18 of `roadmap-usb4.md`, the third of the owner's answers of
2026-10-22 ("What 43.18 left" in the decisions table): the gate-G3
readings of the 9x link-down statement, one per system, on a trial build
in emulator guests, each system that passes admitted. Read on
2026-10-22. (A file of its own because `run-43.md` lives on another
branch; the two are joined at the merge. Record 21 section 7.2 part 2
names `run-42.md` for these readings; this sheet holds them instead.)

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

### Verdict

**Both systems pass; both are admitted** (`cm_eject.c`,
`CM_EJ_QUALIFIED_98SE` and `CM_EJ_QUALIFIED_ME` set to 1). On 98 SE under
the ICM client and on ME under the USB4 profile, at every point read, the
statement (the entry marked `DOWN_QUIESCED`) came after the emulator's
stage-2 reset and quiesce of the endpoint, the acknowledgement wait ended
after the statement, and the vanished child's `CM_Query_Remove_SubTree`
came after the wait's end. Every wait but one ended acknowledged, in 41
to 220 ms; the one that did not is described under ME, run C2: a
system-modal error screen Windows put up stopped every thread, the
connection manager's included, and the answer that came after it counted
as late. The ordering held there too.

### The vehicle

- Development host A, QEMU under TCG, headless, one vCPU per guest
  (`pentium3,-apic`), 256 MB; `usb4-emu` at `660f085` (the emulator's
  SHA-256 prefix `9dfdcf9b`), with `-device usb4-debug-port` for the
  stage codes and `-trace usb4_tp_*`, `usb4_dbg_*`, `usb4_h_*`.
- **98 SE**: the 3.0.0.0 image, the Alpine Ridge topology of the ICM
  client (`profile=tb3-alpine-ridge`, the TB3 PCI bridges, an SHPC tunnel
  bridge), updated with the published `3.1.0.0` package,
  `INSTALL /console /yes /thunderbolt /stor98`, the enclosure plugged
  (Windows' files the install asked for skipped or pointed at
  `C:\XHCI98`), then restarted.
- **ME**: the 3.1.0.0 image with the Thunderbolt driver and Stor98 of
  the joint legs, the generic USB4 topology (`profile=usb4-generic`, two
  tunnel ports), an ASM2464 USB4 enclosure.
- The NVMe drive: a 128 MiB FAT image, enclosure `enc0` hot-plugged after
  the desktop on both; a 16 MiB file of random data copied onto it as
  `E:\BIG.BIN`. Stor98: the bundled 3.1.0.0 build on both.
- **The trial build** (never committed): the `qemu` flavour of
  `xhcicm.sys` from a `git archive` of `p43-18` at `e620f8bf`, with both
  systems admitted and three stage codes written to the debug port (code
  at `5A0h`, argument at `5A4h`): `0x1C0` an entry marked
  `DOWN_QUIESCED` (argument generation, bus, slot), `0x1C1` an entry's
  acknowledgement wait ended (argument `CM_NVLINK_AW_*` << 8 | entry: 1
  acknowledged, 5 late), `0x1C2` the CONFIGMG worker's
  `CM_Query_Remove_SubTree` of the vanished child (argument the devnode).
  SHA-256 prefix `7901ed4d`, copied over the installed `xhcicm.sys`, then
  a cold restart.
- **The stage-2 event**: `usb4_tp_cutoff` ("stage 2 complete"), the
  emulator's endpoint reset and quiesce at the tunnel down. Times below
  are virtual time from it. The unplug is `device_del enc0`; a held read
  is the harness's `x-faults` rule `at=fetch op=read ... act=hold`, the
  unplug issued over QMP as soon as `x-harness-state` shows the read held
  (`E:\xhci98-vm\p43-18g3\scripts\holdunplug.py`); the re-plug leg unplugs
  as soon as `cm.nvlink.up=` reaches the debug console
  (`plugunplug.py`). The load is `copy /b E:\BIG.BIN NUL` in an MS-DOS
  Prompt, after a re-plug so that the file is not in the cache.
- The host was shared with other guests and builds (CPU near 97 % by
  midday): the times are the guests' virtual time and what they show is
  the order; their sizes are noisy.

### Results

| System, leg | 0x1C0 statement | 0x1C1 wait end | 0x1C2 query-remove | Driver's lines |
|---|---|---|---|---|
| 98 SE, idle unplug | +68 ms | +171 ms, acknowledged | +178 ms | `cm.nvlink.icm-down=000B0400`, `cm.nvlink.await=0000006E` (110 ms), query and removal 0 |
| 98 SE, unplug with the file read from the cache (no read reached the drive) | +201 ms | +257 ms, acknowledged | +267 ms | `await=00000037` (55 ms) |
| 98 SE, the 20th read held at its fetch, unplugged 3 ms after | +116 ms | +165 ms, acknowledged | +172 ms | `await=00000029` (41 ms); the query vetoed (`0x17`, the copy holds the file open) |
| 98 SE, every read held (`repeat=on`), unplugged 1.5 s after the first | +63 ms | +128 ms, acknowledged | +134 ms | `await=00000037` (55 ms); query vetoed |
| 98 SE, re-plug, unplugged 3 ms after `cm.nvlink.up` (1.34 s after the plug) | +219 ms | +372 ms, acknowledged | +378 ms | `await=00000097` (151 ms); query and removal 0 |
| ME, idle unplug | +243 ms | +458 ms, acknowledged | +463 ms | `cm.nvlink.u4-down=000B0000`, `await=000000DC` (220 ms), query and removal 0 |
| ME, the 20th read held at its fetch, unplugged 2 ms after | +271 ms | +323 ms, acknowledged | +329 ms | `await=00000036` (54 ms); query vetoed |
| ME, every read held, unplugged 1.5 s after the first | +107 ms | +216 ms, acknowledged | +222 ms | `await=0000006E` (110 ms); query vetoed |
| ME, run C2: every read held, two MS-DOS Prompts reading, unplugged 6.3 s after the first | +229 ms | +72 311 ms, late | +72 317 ms | `await=000118AE`; a system-modal "Disk Write Error" screen up from the unplug until a key was pressed (below) |
| ME, re-plug, unplugged 2 ms after `cm.nvlink.up` (3.31 s after the plug) | +222 ms | +275 ms, acknowledged | +280 ms | `await=00000037` (55 ms); query and removal 0 |

Every leg: `cm.nvlink.down` once, `cm.nvlink.retired` after the wait,
then the query; the re-plug that followed published `UP` again under a
new generation. In each leg with a held read the copy failed with
Windows' own "General failure reading drive E" or "The volume that was
removed had open files" and Windows ran on.

**The queue depth.** The held reads never went past one: an MS-DOS copy
reads one request at a time, and a second MS-DOS Prompt reading another
file on the same drive (run C2) did not add a second request at the
drive while the first was held. "A saturated queue", as record 21 section
7.2 part 2 asks for it, is not reachable this way on 9x; the most it
reached is a held request with more behind it in Windows. **Owner's
decision, 2026-10-22:** the one-request reading meets the "saturated
queue" point on 98 SE and ME.

**ME, run C2.** After the unplug Windows put up its system-modal "Disk
Write Error" screen for drive E (what it was writing was not read). While it was up, nothing ran: the statement was made
before it (+229 ms) but the 50 ms pass that ends the wait did not run
until the key was pressed, 72 s later, and then found Stor98's answer
past the 3 s bound (`CM_NVLINK_AW_LATE`). The removal still came after
the wait's end, and the ordering held. A system-modal screen stops this
driver's wait on 9x as it stops Windows; that is the systems' own
behaviour, not something the driver can shorten.

**Not a leg: the harness's surprise rule on the USB4 profile.** A rule
`act=surprise` takes the tunnel down without unplugging the enclosure's
router. On ME the USB4 connection manager saw nothing (its down comes
from the router's hot-unplug); when the enclosure was removed 2.7 minutes
later it found the function already gone and withheld the statement
(`cm.nvlink.withheld=03000002`), as the condition says. Every unplug
above is a `device_del` of the enclosure.

### What it changes

Both systems admitted in `cm_eject.c`; a host vector checks it
(`test_cm_eject`), and `test_ej9x` checks that the tools' refusal for a
drive with no Stor98 channel tells the user to shut Windows down before
unplugging it. The statement's other conditions are unchanged: the
emulator's bridge, one member under the ICM client, the known
correspondence under the USB4 profile, so on hardware no statement is
made. The admission also opens the two eject paths that needed it on 9x
(the failed branch's isolation, the USB4 profile's clean-removal teardown
with its `DOWN_QUIESCED`); those are read by 43.19's legs.

The guests' logs: `E:\xhci98-vm\p43-18g3\keep` (`trace-*.log`,
`debugcon-*.log`).

### 43.19, first legs: `XHCIINST /eject` on 98 SE and ME (2026-10-22)

After the admission's commit (`fef563af`), the same guests with that
commit's gated `qemu` build of `xhcicm.sys` (from the `scripts\build-driver.cmd all`
run that passed, SHA-256 prefix `e3697476`) and its `XHCIINST.EXE` copied
in (`XHCITR.EXE` was not: the tray had it open on 98 SE), a cold restart,
the enclosure plugged, then `XHCIINST /eject 1` and `Y`, then the unplug.
Not 43.19's whole: XHCITR's Safely Remove, the two-port legs and 42-A.14
are not read.

| System | What the tool said | The driver's lines |
|---|---|---|
| 98 SE, ICM client | "Asked the Thunderbolt driver to stop the device. Windows has stopped Stor98 Storage Controller for NVMe SSDs. ... You can now unplug the device.", exit 0 | `cm.eject.accepted`, the query (`cm.eject.call=41..`), the request (`42..`, `cm.eject.request=00000100`: Stor98's `RemoveAck`), the removal (`43..`), `cm.eject.result=080291E0`: step 8 removed, outcome 2 clean; no teardown (decision V8) |
| 98 SE, the unplug after it | - | the statement made on the tunnel down, and its wait ran to the 3 s bound (`cm.nvlink.await.end=00000400`, timeout, `await=00000BE8`): the drive's Stor98 had already been removed by the eject, so nothing could answer. Only the member's re-enumeration waited; there was no vanished child to remove |
| ME, USB4 profile | the same words, exit 0 | the same steps, then the teardown the admission opens: `u4.eject=00000100`, the tunnel taken down (`admin-down` in the emulator's trace, its stage-2 event), `cm.eject.teardown=00002801`, `cm.nvlink.down=00000001` and `cm.eject.mark=00000001` (the entry's `DOWN_QUIESCED`), the entry retired; `cm.eject.result=0802BA4A`, clean. The unplug after it logged nothing (the tunnel was already down), and a re-plug brought the drive back with a new generation (`cm.nvlink.up=02000101`) |

**Finding (98 SE, for 43.19):** after a clean eject under the ICM client,
the unplug's statement waits the full 3 s for an acknowledgement no
Stor98 can give, because the entry still reads `UP` after the eject
removed the drive's Stor98 (under the USB4 profile the teardown retires
the entry first). It delays only the member's re-enumeration, by the 3 s
acknowledgement deadline when Windows schedules the driver's 50 ms pass
on time; the deadline bounds what counts as an answer, not when the wait
is next looked at, so a stalled 9x scheduler can hold it far longer (as
run C2's 72 s shows).
**Fixed the same day on the owner's answer** (below, "The 3 s wait after
a clean eject").

**ME between the two sessions.** The ME guest, paused over QMP for about
an hour while the builds ran and then resumed, was found with a black
text-mode screen and the processor in the VMM, not answering keys; it
was quit and cold-started. Whether this is the hang task 43.49 reads (not
the same trigger: no plug cycles were running) is not read here.

### The 3 s wait after a clean eject (2026-10-22)

The owner's answer to the 98 SE finding above: fix it in 43.18. A
removal that ends clean with an entry and no teardown to follow (the
firmware connection manager, decision V8) now takes the entry from `UP`
to `IN_USE` alone after the removal (`CM_EJ_P_RETIRE`,
`CmNvlinkRetireEjected`): no `DOWN_QUIESCED`, no wait, the generation
kept. ME under the USB4 profile does not share the cause (its teardown
retires the entry first); NT does not either (Windows ejects there, and
the driver is not told that Stor98 left), and neither is changed. The
same change makes the eject's own statements take the whole
qualification: the member must read as the emulator's bridge, not only
the systems be admitted (`CmEjStatementQualified`, logged as
`cm.eject.emulator`).

**The retake, 98 SE under the ICM client.** The same guest and overlay as
the first leg, with the uncommitted tree's gated `qemu` build of
`xhcicm.sys` (`scripts\build-driver.cmd all`, x86, passed; SHA-256 prefix
`a9e38b90`) copied over the installed one and a cold restart; the host
loaded by other agents' guests and an amd64 gated build. The enclosure
plugged (`cm.nvlink.up=01000401`), then `XHCIINST /eject 1` and `Y`:
the same words, exit 0. The driver's lines: `cm.eject.emulator=00020802`
(member 02:08, read as the emulator's bridge), the query, the request and
the removal as before, `cm.eject.retire=00000001` (entry 0 retired), then
`cm.eject.result=08025107` (removed, clean). The unplug: the tunnel down,
the statement decided (`cm.nvlink.9x.decided=00000200`), and then no
`cm.nvlink.down` and no wait: the member's re-enumeration ran at once
(`cm.reenum.held.run`), where the first leg logged
`cm.nvlink.await.end=00000400` and `await=00000BE8`. A re-plug brought the
drive back with a new generation (`cm.nvlink.up=02000401`) and `dir E:\`
listed its files. Logs: `E:\xhci98-vm\p43-18g3\keep\debugcon-w98-r2.log`,
`trace-w98-r2.log`.

### 43.19, the remaining legs: XHCITR, two ports, 42-A.14 (2026-10-22)

**Verdict: pass.** Every leg 43.19's entry names that the first legs
above left: XHCITR's Safely Remove clean with Stor98's `RemoveAck` on
both systems, the two-port legs under the USB4 profile with known
correspondence (98 SE and ME) and under the ICM client with two members
(98 SE), and 42-A.14 with the TB3 enclosure and a bridge chain on both.
Three findings, none a failed leg, are at the end.

**The vehicle.** As the G3 readings above (the same emulator, SHA-256
prefix `9dfdcf9b`, one vCPU, 256 MB, TCG, the bundled Stor98), on branch
`p43-19` at `6776fd69` (`3.2.0.0` at `a48e8688` and roadmap text): its
gated `scripts\build-driver.cmd qemu` build (passed) gave `xhcicm.sys`
(SHA-256 prefix `ae8700b2`) and `STOR98.MPD` (`8dd65321`), copied with
that tree's `XHCITR.EXE` (`0dc4e522`) and `XHCIINST.EXE` (`9abd298b`)
over the installed ones (the tray's XHCITR closed from its menu first),
then a cold restart. `xhci98.sys` stayed the installed one. Overlays over
copies of the G3 guests (`E:\xhci98-vm\p43-19\img`); a hot-added chain
is `device_add usb4-asm-pci-usp` at the tunnel bridge's slot 3, a
`pci-bridge-seat` below it and an NVMe below that (the recipe of
`E:\xhci98-vm\p42-9x\RECIPE.txt` (c2)), with `bus-reserve=8` and
`mem-reserve=32M` on the tunnel bridge; the two-port unplugs are one QMP
session's `device_del`s a set time apart (`scripts\twoport.py`). Times
are the drivers' own readings; the host was shared with other agents'
guests and builds, so their sizes are noisy.

**XHCITR's Safely Remove.** The enclosure's drive selected, Action >
Safely Remove: the box named the drive, the devices that stop with it
and "Close any program that uses them first.", and after OK said
"Windows has stopped QEMU NVMe Ctrl (E:). Until you unplug it, Device
Manager's Refresh, or the Thunderbolt driver looking at the port again,
can start it again." and "You can now unplug the device." (43.8's words).

| System, leg | The driver's lines | After it |
|---|---|---|
| 98 SE, ICM client (Alpine Ridge), one enclosure | `cm.eject.accepted`, `cm.eject.emulator=00020802`, the query (`41..`), the request (`42..`, `cm.eject.request=00000100`: `RemoveAck`), the removal (`43..`), `cm.eject.retire=00000001`, `cm.eject.result=080290E5` (removed, clean) | the unplug: `cm.nvlink.9x.decided=00000200`, no `cm.nvlink.down`, no wait, `cm.reenum.held.run` at once; the re-plug `UP` (`cm.nvlink.up=02000401`), E: listed |
| ME, USB4 profile (ASM2464), port 1 | the same steps, then `cm.eject.call=44..`, `u4.eject=00000100`, `cm.eject.teardown=00002801`, `cm.nvlink.down=00000001`, `cm.eject.mark=00000001`, `cm.nvlink.retired=00002801`, `cm.eject.result=0802A40D` | the unplug logged nothing (the tunnel was down); the re-plug `UP` (`02000101`) |
| ME, USB4 profile, port 2's drive with port 1's up | member `0030`'s steps alone (`cm.eject.request=00000201`, `u4.eject=00000200`, `cm.eject.teardown=00003001`, `cm.eject.mark=00000101`), `cm.eject.result=08024385` | port 1's drive F: still listed |
| 98 SE, USB4 profile, port 2's drive with port 1 empty | as ME's (`cm.eject.request=00000101`, `cm.eject.teardown=00003001`), `cm.eject.result=0802DE08` | - |
| 98 SE, ICM client with two members (Titan Ridge 4C), port 1's drive | member `0208`'s steps, `cm.eject.retire=00000001`, `cm.eject.result=0802E07E`; no teardown (decision V8) | port 2's drive F: still listed; port 1's unplug `cm.nvlink.withheld=02010002` (two members), no wait; the re-plug `UP` |

**The two-port legs, USB4 profile.** Both enclosures plugged (ME: one
`UP` each, `cm.nvlink.up=..000101` and `..000A01`, port 2 behind bus
`0Ah` beside port 1's reserve; 98 SE `..000101` and `..000201`), both
drives listed, then:

| Leg | ME | 98 SE |
|---|---|---|
| port 1 then port 2 unplugged 1 s apart | each member's own statement (`cm.nvlink.down=00000002`, `00010001`), its own hold (`cm.nvlink.up.held=00000028`, `00000030`) and its own end: port 1 acknowledged after 219 ms (`cm.nvlink.await=000000DB`), port 2 after 55 ms; while port 1 was held, port 2's member's re-enumeration ran (`cm.brnum.9x.held=00003001`, then `cm.reenum.9x.reenumerate=00003000`) | port 1 110 ms, port 2 96 ms, each held and released by its own end |
| both unplugged 20 ms apart (both statements in one pass) | both marked before either ended (`down=00000003`, `00010002`); port 2 acknowledged first (96 ms) and its held work ran (`cm.reenum.held.run=03040030`) while port 1 still waited, which ended at 151 ms | both marked in one pass; port 1 55 ms, port 2 110 ms |
| port 1 unplugged and port 2 plugged in the same instant | port 1's statement (`down=00000004`), port 2's tunnel up (`u4.pcie.up=00020809`) during the wait, port 1 acknowledged at 110 ms, then port 2's `UP` published (`02000A01`) and E: listed | the same: port 1 55 ms, port 2's `UP` (`02000201`) after it |

Every statement was followed by the vanished child's query and removal
(`cm.reenum.9x.query-remove=00000000`, `remove=00000000`) after the wait's
end; the re-plugs brought the drives back.

**The two-port legs, ICM client with two members** (98 SE,
`profile=tb3-titan-ridge,ports=2`, a TB3 tunnel bridge per port, a TB3
enclosure on each, E: and F:): port 2 unplugged, `cm.nvlink.icm-down=00020400`
(two members) and `cm.nvlink.withheld=02000001`, `02010001`
(`CM_NVLINK_SEVERAL`: no statement, as the condition says), the vanished
child queried and removed, F: back after the re-plug; port 1's eject and
unplug as in the table above. XHCITR shows each drive under its port and
the two enclosures beside them (the device's port is not known under the
ICM, record 21 section 6.3).

**42-A.14 with the TB3 enclosure and a bridge chain.** The chain added
beside the enclosure, the enclosure unplugged and plugged again so that
`xhcicm.sys` numbered the chain (98 SE `cm.brnum.9x.port-buses=00040402`;
ME `00090100`, the chain's buses 02-03, its windows and IRQ written,
`cm.brnum.9x.irq=00010A01`), Windows' wizards for the two bridges, then
Stor98's for the chain's NVMe from `C:\XHCI98\STOR98.INF`: the chain's
drive started (F:, its files listed), which 3.1.0.0's 42-A.14 leg could
not reach. The Thunderbolt controller's PCI page in XHCITR:

| System | Functions below the port | The count |
|---|---|---|
| 98 SE, ICM client, tunnel port 02:01.0 (buses 04-06) | the enclosure's NVMe 04:01.0 (1B36:0010, class 010802, pin INTA, line 10, its 64-bit BAR, PCI Express v2 and power management), the switch's upstream port 04:03.0 (1B21:2421, buses 05-06, window 10100000-102FFFFF), its downstream port 05:01.0 (1B36:000A, bus 06), the chain's NVMe 06:01.0 (line 10) | "102 configuration reads, 0 refused." |
| ME, USB4 profile, the TB3 enclosure on tunnel port 00:05.0 (buses 01-09) | 01:01.0 the enclosure's NVMe, 01:03.0 the upstream port (buses 02-03, window FE600000-FE7FFFFF), 02:01.0 the downstream port (bus 03), 03:01.0 the chain's NVMe (line 10); tunnel port 00:06.0, empty: "Not read on this system." (finding 2) | "135 configuration reads, 0 refused." |

**Findings.**

1. *Not a defect.* With the chain on the tunnel bridge beside the
   enclosure (the emulator puts it at the bridge's slot 3, not behind the
   enclosure's router), the enclosure's unplug made no statement on
   either system: `cm.nvlink.9x.decided=04000100`,
   `cm.nvlink.withheld=04000002` (98 SE), `04000006` (ME), verdict
   `CM_NVLINK_NOT_DOWN`, because the bridge still has a function present
   and its reading does not reach `DOWN`. The withheld statement is the
   safe outcome; the vanished child was removed.
2. *XHCITR's 9x PCI page, an empty tunnel port.* "Functions below this
   port: Not read on this system." (`tr_pci.c`, `below == 0` and not NT),
   the 3.0.0.0 text for a driver that does not read below the port; the
   driver since 42-A.14 does read it, and an empty port gives the same
   answer as no walk. `test_trpci` checks the text for the old driver's
   answer. Not changed here.
3. *XHCITR's eject box under the ICM with two members* shows 43.8's
   reminder ("Before you eject it, make sure none of its other devices
   ...") with the console's hard line break after "card", so the box
   wraps the line raggedly (`inst_eject_remind_text`). Not changed here.

**Not chased.** An ME overlay of the predecessor's session (the 43.18
trial binaries, a chain's devnodes installed) stopped at its next boot
with "Error: 0E : 0197 : BFF62170" and then a grey screen; it was quit
and the ME legs above were taken on a fresh overlay from the same image.

The guests' logs: `E:\xhci98-vm\p43-19\keep` (`debugcon-w98-c2.log`
XHCITR and 42-A.14 on 98 SE, `debugcon-me-c2.log` ME's XHCITR, two-port
and 42-A.14, `debugcon-w98-t3.log` the ICM with two members,
`debugcon-w98-u3.log` 98 SE's USB4 two-port; `trace-*.log` beside each);
the pages in `E:\xhci98-vm\p43-19\shots`.

## 43.21 - the unexpected-removal notice, leg T.11 (2026-10-22)
Roadmap 43.21; design record 25 section 7.6 (revision 16) and leg T.11 in
its section 11.

**Verdict: done** (2026-10-22, ticked on the owner's approval). Every leg
that can be read on the emulator passes on 2000, XP, XP x64, Vista x86,
7 x86, 7 x64 and 98 SE. Two parts are recorded as not readable here, as
43.45's resumes were:

- A chain's first device unplugged. The emulator's chained routers exist
  only under the software connection manager's `usb4-generic` profile on
  q35; on the `pc` machine every NT guest uses, one tunnel-bridge level is
  reachable, so a unit behind a chained router never appears on the PCI
  side (usb4-emu `docs/XHCI98-HANDOFF.md`, "Chained PCIe tunnel"). It is
  listed for the owner's session on real hardware.
- Sleep on 2000 and XP: those images have no standby or hibernate.

One defect was found and fixed during the legs: on XP the warning balloon
never showed, because the unit's "disconnected" balloon of the same poll
replaced it at once. Commit `b03d4422` puts a due warning and the other
events of that poll in one balloon under the warning title. Every leg
after that fix ran with its XHCITR; the 2000 legs before it were retaken
for the main case.

### Build

Branch `p43-21`. Gated `build-driver all` passed for x86 and amd64 at
`b03d4422` (x86 `qemu` `xhcicm.sys` `9FE8E826...`, release `B3C59BCE...`;
amd64 `qemu` `5A766C66...`, release `77CF75FF...`; `test_tr` host vectors
pass). The x86 guests ran the `qemu`-flavour `xhcicm.sys` of `1b27ce1d`
(`B463877F...`); `b03d4422` changed no driver source, only XHCITR, its
tests and documents. XHCITR was the gated `b03d4422` build (`5B85735C...`)
on every NT leg but the first 2000 boot and the first XP boot, which found
the defect. The 98 SE guest ran `1b27ce1d`'s XHCITR: `b03d4422` changes only
a poll with a warning, which 98 SE never has.

### The vehicle

Development host A, QEMU TCG under heavy load from other guests (CPU near
97 %). The usb4-emu build copied from task 43.44's kit (`bin`, and `bin-icl`
for Ice Lake). Topology as task 43.44's: `usb4-host` with a firmware
connection manager (Titan Ridge, Alpine Ridge on 98 SE, Ice Lake on 7 x86),
the tunnel bridge, and `enc0`, the ASM2464 device router with an NVMe
namespace behind it, plugged with `device_add` and pulled with
`device_del`. NVMe through nvme98 on NT. The mark service (`XHCIMARK`,
`XHCIMK64` on x64) registered on each NT guest. Guests: 2000 (`acpi=off`,
1 vCPU), XP (1), XP x64, Vista x86, 7 x86, 7 x64 (2 each), 98 SE (1). Kit
`E:\xhci98-vm\p43-21` (`RUN-NOTES.md`, screenshots, debug-console logs).

The count is read in two places: the driver's debug-console line
`cm.reenum.unexpected=<member>` and the NHI's details in XHCITR,
"Unexpected removals: N since this start; the last on tunnel port ...".

### Results

| Case | 2000 | XP | XP x64 | Vista | 7 x86 | 7 x64 |
|---|---|---|---|---|---|---|
| Unit present at boot and never marked: unplug | "disconnected" only, not counted | same | same | same | same | same |
| Marked unit, unplug with no eject | warning, counted | warning, counted | warning, counted | warning, counted (a poll late, below) | warning, counted | warning, counted |
| Eject from XHCITR, then unplug | "disconnected" only | same | same | same | same | same |
| Driver update of the unit (nvme98 forced) | nothing | nothing | nothing | nothing | nothing | nothing |
| Disable and enable of the unit | nothing | nothing | nothing | nothing | nothing | nothing |
| Option off, unplug with no eject | no warning, counted | same | same | same | same | same |
| Option on again, unplug | warning | - | warning | - | - | - |
| Sleep and resume with the unit attached | no standby (`acpi=off`) | no S1-S4 on this image | hibernate: nothing, count unchanged | hibernate: nothing | hibernate: nothing | hibernate: nothing |

"Warning" means one balloon titled "Thunderbolt/USB4 warning" reading "A
Thunderbolt or USB4 device was removed unexpectedly." with "ASMedia 246x was
disconnected." on the next line. On 2000 the first boot, before
`b03d4422`, showed the warning and then the "disconnected" balloon; the
retake with `b03d4422` showed the single combined balloon.

On Vista the departure fell one poll before the count: "disconnected"
first, the warning alone a poll later. Section 7.6 item 4 accepts this.

A unit present at boot is counted only once it is marked: the mark
service marks it at its first plug while the service runs, and removes the
mark when the unit leaves. Section 7.6 item 1 and the release notes say so.

On 98 SE (Alpine Ridge): the tray menu and the View menu have no "Warn
when a device is removed unexpectedly" item; the NHI's details read
"Unexpected removals: not watched on this system"; an unplug gives the
tooltip "ASMedia 246x was disconnected." and nothing else.

### Not read

- A chain's first device unplugged: not readable on the emulator (above).
- Sleep on 2000 and XP: those images have no standby or hibernate.

### Findings outside 43.21

- After a hibernate and resume on 7 x86, 7 x64 and XP x64, XHCITR showed
  "The Thunderbolt/USB4 controller stopped answering." This is the
  existing finding from 43.45: the liveness gate's off state across a
  sleep. The count and the unit were unchanged.
- On 7 x86, after a resume, an eject by a standard user with an AutoPlay
  Explorer window open on the unit left the replugged device not started
  until a restart. It did not recur after a cold boot.
- On Vista and 7 x64, a guest asked to shut down at the end of its legs
  stayed at "Shutting down..." and was stopped through QEMU.

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

## 43.23 Stor98 on Windows 8 and 8.1, x86 and x64 (2026-10-22)

Branch `p43-23`. The owner's instruction of 2026-10-20 ("stor98 can be
adjusted to work on win 8 also since it's in this repo now"), in place of
42-A.21's "the bundled Stor98 not offered on 8 and 8.1". Static readings
from the owner's install media and runtime readings in QEMU on development
host A; Stor98's own record of the runtime legs is its `docs/TESTING.md`,
"Windows 8 and 8.1", and its `docs/PLAN.md` item 2.2.

**Verdict: open** (Codex round 1, 2026-10-22). The x86 half is read; the
x64 half cannot be until the x64 packages carry a test-signed catalog,
which branch `p43-36s` is adding (owner's decision). Waiting on it:
Stor98 x64's install and load on 8 x64, 8.1 x64 and 10 x64, and so a
removal under Stor98 on 8.1 at all (8.1 was read on x64 only, under its own
`stornvme`). The owner may instead defer them past `3.2.0.0`.

### The installer row

43.43 (merged into `3.2.0.0` at `22ec1372`) already took 43.23's row, as
its entry says: Stor98's box shown and tickable on 8 and 8.1, unticked by
default, its case 3b warning, the bundled Stor98 placed on tunnel drives
only, and on 8.1 a tunnel drive whose service is `stornvme` installed over
with `INSTALLFLAG_FORCE` and recorded under `Software\xhci98\Stor98\Pending`
(record 26 section 5.3.1, case 3b). 43.23 adds no behaviour there. It moves
the `stornvme` test, written three times in `inst_eng.c` (the plan's
"no driver" reading, `call_flags` and the pending mark), into one pure
function, `inst_s98_over_inbox(is8, service)` in `inst_core.c`, and holds
it with host vectors in `test_inst.c`: on 8 and 8.1 `stornvme` in any case
is installed over; on 2000 to 7 it is another driver and left; Stor98
itself, a vendor's NVMe service (`secnvme`, `iaStorAC`), a longer name
(`stornvme2`), an empty service and no service are never installed over.
`call_flags` and the pending mark now test the system as the plan always
did (they had tested the service name alone, which on 2000 to 7 can reach
neither, since the plan leaves such a drive). `test_inst`: 1805 checks,
0 failures, with the Windows 10 vectors ("Windows 10" below) after the merge of `3.2.0.0`
at `2038250e`; the gated `all` builds, x86 and amd64, passed.

### Static: what SCSIport and the class driver on NT 6.2 and 6.3 are

Taken with `7z e` from `sources\install.wim` of
`en_windows_8_pro_vl_x86_dvd_917830.iso`, `Windows 8 x64.iso` (image 1),
`en_windows_8.1_with_update_x64_dvd_6051480.iso` (image 1) and, for the
comparison, `en_windows_7_professional_with_sp1_vl_build_x64_dvd_u_677791.iso`,
into a scratch directory outside the tree; read with WDK 7.1's
`link -dump -exports` / `-imports` and a strings sweep by a PowerShell
script kept outside the tree; nothing executed.

| Build | File | Version | Bytes | SHA-256 |
|---|---|---|---|---|
| 7 x64 | `scsiport.sys` | 6.1.7601.17514 | 171,392 | `8bbda37be8f842719a3324ca5120a9327fbc799d0d8596aecc97c538d2b28ba3` |
| 7 x64 | `Classpnp.sys` | 6.1.7601.17514 | 179,072 | `f0f1c47cc164b8bf4b99cc4e9c0d34d603051cd01ed369acb9a781ec9cca56aa` |
| 8 x86 | `scsiport.sys` | 6.2.9200.16384 | 146,672 | `a677380bb7cde97f7f74adc74e793565be3e71a2e1637f48bec9eef6ff7e2dec` |
| 8 x86 | `Classpnp.sys` | 6.2.9200.16384 | 299,760 | `966adacfef0037de184aa514e9136ebf085330106504c2dc023feb9066510297` |
| 8 x86 | `ntoskrnl.exe` | 6.2.9200.16384 | 5,563,120 | `fce2e5990281c3564a4f9aa684999e1c79f7531cacdd391adbe7eee323ad265f` |
| 8 x86 | `hal.dll` | 6.2.9200.16384 | 319,216 | `ce46da146250c6b0cde8fa14cdc5c85cdc19bcf64ceb9c383ecc732c12cfdd99` |
| 8 x64 | `scsiport.sys` | 6.2.9200.16384 | 172,784 | `d6515d9a5bb252b3fc77b8f20cd6becf12731c342949502057bde27d7a2111c2` |
| 8 x64 | `Classpnp.sys` | 6.2.9200.16384 | 336,624 | `0f95c7b3fd6b5485c5ede2dd50579cde8d9866cf35661a359e0d046a0ec7fe65` |
| 8 x64 | `ntoskrnl.exe` | 6.2.9200.16384 | 6,969,584 | `309d04e864809f20f23c1fbc77cd10de0d581b078b132281cdb6a1b7bdeb475a` |
| 8 x64 | `hal.dll` | 6.2.9200.16384 | 395,504 | `6b711a6c86c1564978ca65e360416918caf8991ddf9d041683588d59ff6bff7d` |
| 8.1 x64 | `scsiport.sys` | 6.3.9600.16384 | 170,848 | `b5ac8890b2d584d070f2a95118eb15b8b256b793b812d678c544f784699d901f` |
| 8.1 x64 | `Classpnp.sys` | 6.3.9600.17396 | 337,728 | `35ca4cc06d29dc068081976aceb5d5bec32d4663b7d93cb21599da3f92ef47a4` |
| 8.1 x64 | `ntoskrnl.exe` | 6.3.9600.17415 | 7,474,496 | `2676bd81b52e8e35d149d8753eb0c438f04c3bbbc11348f9f34758dbee26c843` |
| 8.1 x64 | `hal.dll` | 6.3.9600.17196 | 423,768 | `898637660689dc5cf91e3536109eb439a21f6f65bfd22e6f857fb75605fc22a8` |
| 8.1 x64 | `stornvme.sys` | 6.3.9600.16421 | 57,176 | `2ddea1568a85ad64fce5d10d348304fcd9be6e96c2313353ef70a2933306d188` |
| 8.1 x64 | `stornvme.inf` | - | 8,666 | `12257b98c43b178825dec26a6b54bcc2c0d060790aeeb15cdef29f2d6c6a2124` |

- **Stor98's imports are all exported.** Every NT line of
  `scripts\import-gate\stor98-imports.allow` (x86 against 8 x86, amd64
  against 8 x64 and 8.1 x64): the 19 x86 names (11 `SCSIPORT.SYS`, 8
  `NTOSKRNL.EXE`) and the 18 amd64 names (9 and 9) are all present in
  those builds' export tables. 8 x86's `scsiport.sys` exports 47 names, 8 x64's and 8.1 x64's 50.
- **SCSIport on 6.2 and 6.3 against 7's, by its interface only.** 7 x64, 8 x64 and 8.1 x64 export the
  same 50 names. Their imports differ by five names in all: 8 adds
  `KeInitializeSpinLock`, `KeLowerIrql`, `KfRaiseIrql` and
  `MmAllocateContiguousNodeMemory` and drops
  `MmAllocateContiguousMemorySpecifyCache` (the uncached extension now
  comes from the node-aware allocator); 8.1 imports what 8 does. The
  registry values the port reads are the same names on 7 x64 and 8 x64
  (`PnpInterface`, `MaximumSGList`, `NumberOfRequests`,
  `DisableTaggedQueuing`, `TimeoutValue` and the rest); 8 adds the
  `RtlQueryRegistryValuesEx` lookup, 8.1 a read of
  `Control\MiniNT` (Windows PE). That is all the static read covers:
  exports, imports and registry value names. The miniport contract itself
  was **not** compared with 7's: `ScsiPortInitialize`'s checks of
  `HW_INITIALIZATION_DATA` (size, fields, the PnP path), the
  `PORT_CONFIGURATION_INFORMATION` it fills, how it calls `HwFindAdapter`,
  `HwInitialize`, `HwStartIo`, `HwInterrupt`, `HwResetBus` and
  `HwAdapterControl`, and which PnP and power SRBs and adapter-control
  types it sends. What stands in for that read is the runtime one: on 8
  x86 (and 10 x86) SCSIport took Stor98's `HW_INITIALIZATION_DATA` and the
  INF's `PnpInterface\5`, found and started both adapters, ran I/O and
  the removal path. Unread, statically and at runtime: the x64 SCSIport
  (behind the catalog), a PnP stop and restart (Device Manager's disable
  and enable), a hibernate or sleep and resume, and
  `ScsiStopAdapter` / `ScsiRestartAdapter` on 6.2 and later.
- **The class driver.** `Classpnp.sys` exports the same 60 names on 7 x64
  and 8 x64; 8.1 adds `ClassModeSelect` and `ClassModeSenseEx`. Its retry
  policy was not read statically; the runtime legs read it.
- **8.1's own NVMe driver.** `stornvme.inf` (`DriverVer=06/21/2006,
  6.3.9600.16421`, `Provider=%MSFT%`) binds `PCI\CC_010802` in its
  `NVME.NTamd64` and `NVME.NTx86` models, as a boot-start Storport
  miniport with `ExcludeFromSelect = *`; Stor98's INFs match the same
  compatible id, so Windows' ranking (a signed in-box INF over an unsigned
  one) gives every NVMe controller to `stornvme`, which the 8.1 guest
  showed at first boot. A tunnel drive the installer forces onto Stor98
  keeps its devnode, and with it Stor98, for as long as Windows keeps that
  devnode; a drive of another model (other PCI ids) gets a new devnode and
  `stornvme`, until the installer runs again (Q2 below).

### Runtime: the vehicle

usb4-emu's QEMU `bin-2a893ea` (SHA-256 `6a1dfcc8...bd66`), xhci98's
pristine images `w8x86`, `w8x64` and `w81x64` (`E:\xhci98-vm\win8-10`)
through overlays, `-machine pc`, TCG, `-cpu qemu64,+3dnowprefetch`,
2 vCPUs, 2048 MB (the coordinator's resource rule of the day), one guest
at a time from 13:20; kit `E:\xhci98-vm\p43-23`, QMP 48640-48642. Each
guest: a plain QEMU `nvme` on the root bus (a blank 1 GiB disk, serial
`S98W8INT1`), and a `usb4-tunnel-bridge` (SHPC, `x-force-tunnel=on`,
`nvme-drive=`, serial `S98W8TUN1`) with the FAT test image
`vm\u4-asm\base\nvme-fat.qcow2` behind it; a `qemu-xhci` with a
`usb-tablet` for the pointer. No connection manager in the guest (8 and
8.1's connection-manager path is 43.22's): the tunnel up at power-on,
down with `qom-set /machine/peripheral/tb0 tunnel surprise`. **Windows 8
does not take the bridge's SHPC**: the emulator's trace shows the
surprise removal signalled and never acknowledged, and the controller's
devnode stayed started; so each removal below is a link loss with no
notice from Windows, Stor98's all-ones detection and its gone deadline
alone. The install: `UpdateDriverForPlugAndPlayDevices(NULL,
"PCI\CC_010802", <inf>, INSTALLFLAG_FORCE)` from a PowerShell script in
the guest, the installer's own call, its "Windows can't verify the
publisher" prompt answered "Install this driver software anyway". The
guest's scripts wrote `wmic`, `sc`, `reg` and `wevtutil` output to COM1;
the debug-port builds' stage lines went to `isa-debugcon` with
`usb4-debug-port`. Host load: the coordinator's note of the day (CPU
97 %), so timings are to the second.

### Runtime: results

| Leg | Driver | Result |
|---|---|---|
| 8 x86, before | - | Both controllers "PCI Device", problem 28, no service: Windows 8 has no NVMe driver |
| 8 x86, install | this branch's free x86 `stor98.sys` (`2fd7005b...29bfd`) and `stor98-x86.inf` | `ok=True` after the prompt; both controllers "Stor98 Storage Controller for NVMe SSDs", service `stor98` running, demand-start, `SCSI Miniport`, `Parameters\PnpInterface\5` = 1; both disks present (serials `S98W8INT1`, `S98W8TUN1`), the tunnel volume F:; `StorInterrupt` "pin 1 line 10 msi no msix yes port 10/10 interrupt" and "pin 1 line 5 ... interrupt" |
| 8 x86, unplug during writes | the same | a 4 MB copy looped onto F:: the 181st failed with "The system cannot find the drive specified"; disk event 153 ("... was retried") three times for each of the four lazy writes; the disk and volume gone, the controller started |
| 8 x86, with stage codes | the `STOR98_DBGPORT=1` free x86 build (`5c6de2f3...d245`) | gone entered with no command issued; each WRITE fast-failed `SRB_STATUS_NO_DEVICE` (0x08) at 0, 1.3 and 4.0 s (the same four, classpnp's retries, event 153 each time); the gone deadline at 5.0 s, `BusChangeDetected`, the next INQUIRY failed `NO_DEVICE`, the disk removed; the copy failed at about 5.4 s |
| 8 x86, `SRB_STATUS_SELECTION_TIMEOUT` | a test build with Stor98's two terminal sites set to 0x0A, never committed (`71fcde8f...05f1`), installed over the first by a copy of the INF with its `DriverVer` raised in the guest | the same shape: 0x0A fast-fails from 0.4 s, 38 event 153 entries, fastfat event 141 and the "Windows - Delayed Write Failed" popup (event 26) at about 4.7 s, the deadline and the disk's removal at 5.0 s, the copy failed at about 5.6 s |
| 8 x64, install | `releases\3.1.0.0\release-x64\stor98.sys` and `stor98.inf` (test-signed; Stor98's source unchanged since `v3.1.0.0`) | **refused**: `0xE000022F` (`ERROR_NO_CATALOG_FOR_OEM_INF`); `setupapi.dev.log`: "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode", "Failed to import driver package into Driver Store", a NULL driver installed. `bcdedit`: `testsigning Yes` |
| 8.1 x64, before | - | `stornvme` on both controllers ("Standard NVM Express Controller"), the tunnel volume F: |
| 8.1 x64, forced install over `stornvme` | the same `3.1.0.0` x64 pair | the same `0xE000022F` and the same `setupapi` lines |
| 8.1 x64, unplug during writes under `stornvme` | Windows' own | the copy in flight blocked; `stornvme` events 129 ("Reset to device ... was issued") and 11 ("controller error") every 35 s, the disk never removed; with the tunnel brought back after 4.6 min the copy completed with success 23 s later |

### Windows 10 (after 43.34)

43.34 (branch `p43-34`, finished, not yet merged here) makes Windows 10
below build 22000 the same kind of system as 8 and 8.1 (`INST_OS_CMONLY`),
and with it Stor98's row: its case 3b warning already reads "Windows 8,
8.1 or 10". `inst_s98_over_inbox` is called with that kind, so a tunnel
drive under 10's own `stornvme` is installed over, forced, as on 8.1, with
no change here beyond its comments. Host vectors in `test_inst.c` hold it
through `inst_os_kind`: on 10 1507 and 22H2 Stor98 is asked with Enter
giving N, `/yes` gives N, `/stor98` installs it unasked, and a drive under
`stornvme` is installed over while a vendor's driver is left; 11 (build
22000) is not that kind. Read on 2026-10-22 after the 8 and 8.1
legs, on xhci98's pristine `w10x86` and `w10x64` images (Windows 10 Pro
22H2, 19045.2965), the same vehicle and guest scripts, QMP 48643 and 48644,
one guest at a time.

Static, from the same two images' `Windows` trees, read with `7z e`
through the powered-off qcow2 (the images were installed from the owner's
media; nothing executed); the export and import comparison against 8's by
WDK 7.1's `link -dump`:

| Build | File | Version | Bytes | SHA-256 |
|---|---|---|---|---|
| 10 x86 | `scsiport.sys` | 10.0.19041.1288 | 145,232 | `fcefde990f6ca548a1f6f1f606bb2d5e9a2c01d33fc8ebf78e043c78f26d4484` |
| 10 x86 | `Classpnp.sys` | 10.0.19041.2673 | 385,072 | `8e5c4bef0939f4db8cce846dea5235f0c2cd1a91c5cda6777971e58d44240ea4` |
| 10 x86 | `ntoskrnl.exe` | 10.0.19041.2965 | 7,204,688 | `f57145f91a3b6527afd0b386151c4c769b52f18c00ceb2cc7a5d405b931a2827` |
| 10 x86 | `stornvme.sys` | 10.0.19041.2075 | 126,816 | `87ec028b500bd24b4c4cc61c53f7562a3628a4333f2ab69feb2dce169b318e6d` |
| 10 x86 | `stornvme.inf` | - | 18,144 | `9ec0a307119914983e32429ee73bd7bc2cb08883822270a80ed590ee340b174f` |
| 10 x64 | `scsiport.sys` | 10.0.19041.1288 | 188,232 | `2a72c9c5cb02e03ced6a6f52000d92485bf2453374cc47e32541b5def1144b2f` |
| 10 x64 | `Classpnp.sys` | 10.0.19041.2673 | 452,656 | `5f3b2144c8b118a2666abcfc4ee30d4900fee48719ddfbb2b9c6ae8e9a5c4de9` |
| 10 x64 | `ntoskrnl.exe` | 10.0.19041.2965 | 10,865,536 | `eace65a06bab6b67461ed29783cde9367c3ddd2a31b2594c59d22d77460d4f4b` |
| 10 x64 | `stornvme.inf` | - | 18,152 | `2729d9ef07aecd2036f55ea51009ccc26f766537859dd1ea6c6b34d6bb5cfab6` |

- Every NT import of Stor98 (the 19 x86 and 18 amd64 lines of
  `stor98-imports.allow`) is exported by 10's `scsiport.sys` and
  `ntoskrnl.exe`. `scsiport.sys` exports exactly 8's names (47 x86, 50
  x64); its imports add `IoGetCurrentProcess`, `IoGetRequestorProcess`,
  `KeFlushIoBuffers`, `KeStackAttachProcess` and `KeUnstackDetachProcess`
  and drop `NlsMbCodePageTag` (and `_allmul` on x86). `Classpnp.sys` adds
  `ClassModeSenseTranslate` and `DllInitialize` to 8.1's names.
- `stornvme.inf` (`DriverVer=06/21/2006,10.0.19041.2075`) binds
  `PCI\CC_010802` with `ExcludeFromSelect = *`, as 8.1's does.

| Leg | Driver | Result |
|---|---|---|
| 10 x86, before | - | `stornvme` on both controllers, the tunnel volume F: |
| 10 x86, forced install over `stornvme` | the free x86 of the 8 legs (`2fd7005b...29bfd`) | the unsigned-driver prompt, "Install this driver software anyway"; `ok=True`; both controllers Stor98, `stor98` running, `stornvme` stopped, both disks and F: present, `PnpInterface\5` = 1 |
| 10 x86, unplug during writes | the same | the 56th copy failed about 5.9 s after the `qom-set` ("The system cannot find the path specified"); fastfat event 50 with the "Delayed Write Failed" popup (event 26) at about 2.4, 3.8 and 5.1 s, 37 disk event 51 (paging-operation errors) at 5.1 s, disk event 157 ("Disk 2 has been surprise removed") at 5.4 s; no event 153; the controller started; the SHPC removal again never acknowledged (`awaiting_ack=1`); the disk did not come back when the tunnel did |
| 10 x64, install | `releases\3.1.0.0\release-x64` | **refused**: `0xE000022F`, `setupapi.dev.log` "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode", "Failed to import driver package into Driver Store. Error = 0xE000022F" |

Decided: Stor98's x86 NT path runs on Windows 10 22H2 as it is, over 10's
own `stornvme` by the installer's forced call, and its docs and INF
comments now say 2000 to 10; the x64 package's refusal is the same as on
8 and 8.1 (Q1 covers 10 x64 too). Owed on 10: PLAN 2.2's terminal-status
question (only `NO_DEVICE` ran, with no stage-code build, and 10 logged no
retry event, so its retry count is unread); 10 builds other than 22H2;
the connection manager's legs on 10 (43.37).

### What it decides

- Stor98's NT path runs on Windows 8 (32-bit) as it is, for start, I/O
  and a removal: no source change (the contract comparison and the PnP
  and power paths are unread, above).
  Its INFs need no new decoration; their comments, Stor98's `AGENTS.md`,
  `README.md`, `docs/TESTING.md`, `docs/PLAN.md` 2.2 and the package
  readme now say 2000 to 8.1.
- PLAN 2.2's question for 8: neither `NO_DEVICE` nor `SELECTION_TIMEOUT`
  ends classpnp's retries there (2000 and XP x64: both did). What ends
  them is the disk's removal after `BusChangeDetected`, and Stor98's
  fast-fail makes each retry cost nothing. `NO_DEVICE` stays the default.
- **The x64 package cannot be installed on 8 x64 or 8.1 x64**, Stor98's
  nor, by the same rule, any of this package's x64 INFs: none names a
  `CatalogFile` (record 19: "no catalog"). That is 43.22's to settle for
  `xhcicm`, and an owner decision (Q1).

### Not read

- Windows 8.1 x86: no medium (owner, 2026-10-22: skipped for `3.2.0.0`,
  untested ground, covered by 8 x86 and 8.1 x64; the release notes'
  untested row).
- Stor98 x64 running on 8 and 8.1 (refused at the driver store), and so
  the kernel's acceptance of its SHA-1 test signature there, and a
  removal under Stor98 on 8.1: waiting on `p43-36s`'s catalog.
- SCSIport's miniport contract on 6.2 and 6.3 compared with 7's, and the
  PnP stop, restart and power paths (above).
- A removal Windows is told of (pci.sys removing the controller), and
  xhci98's link-down statement on 8 and 8.1: 43.24, with the installer's
  own run there.

### Questions for the owner

1. **A catalog for the x64 package.** Windows 8 x64, 8.1 x64 and 10 x64
   refuse every catalog-less package in test-signing mode, so Stor98, and by the
   same rule `xhcicm.inf` and the USB drivers' x64 INFs, cannot install
   there as the package stands. Recommendation: the packager makes one
   catalog per x64 INF (WDK 7.1's `inf2cat` or `makecat`) and signs it with
   the same test key at the same step, each x64 INF gaining
   `CatalogFile=`; record 19 amended; read on 7 x64 again (a catalog the
   test certificate signs must not change what 7 x64 does) and on 8 x64.
   Until then the release notes say the x64 drivers do not install on 8,
   8.1 and 10.
2. **A tunnel drive first plugged after the install, on 8.1 and 10.** It gets
   `stornvme` (Windows' ranking), not Stor98, until the installer runs
   again; on 2000 to 7 the staged `stor98.inf` takes it by itself.
   Recommendation: accept it and say so in the release notes and the
   installer's warning text, rather than raise Stor98's rank.

## 43.49 - Windows ME Stops After Back-to-Back Mouse Plugs and Unplugs
Roadmap-usb4 task 43.49, opened by the owner on 2026-10-22 from 43.13's ME
legs. Development host A, QEMU under TCG, one virtual CPU, 256 MB, headless,
`qemu-xhci` (`p2=4,p3=4`); the host was heavily loaded by other agents'
guests throughout (the coordinator measured 97 % CPU at about 13:45), so no
timing below is a performance reading. Branch `p43-49` off `3.2.0.0`
(`77384e93`); the driver source under test is that of `dd46948c`, the build
43.13 found the stop on (the only `src\` difference between the two is
`src\silent-on.bat`).

### Method

- Driver: the `qemu` flavour of `d35829cf` (gated run, 364,084 bytes), and
  for runs `d2` and `c2` ungated rebuilds of the same flavour (c2 with one
  experimental change, below), copied over the installed `xhci98.sys` on
  overlays of the 43.13 bases (ME + `3.1.0.0`; 98 SE + `3.1.0.0` + NUSB 3.3).
  Registry as 43.13's: `XhciInterruptMode` 2, `XhciPollIdleMs` 50,
  `XhciPollFineTimer` 0, `XhciLogVerbosity` 2; cold boot after the change.
- The trace: QEMU `isa-debugcon` at port `0xE9`, the `qemu` flavour's mirror.
- The series: a mouse `m1` at root port 1 and a hub `h1` at root port 4
  (ME: and a stick at port 2), an MS-DOS Prompt open; then `usb-mouse`
  hot-plugs alternating root port 3 and hub port 4.1, each removed as soon
  as the pointer seemed to move (43.13's `plug.py`, no gap between cycles).
  A cycle that timed out was taken as a stop only when the tray clock did
  not change across a further 75 s.
- At a stop: `info registers`, and the guest's physical memory saved over
  QMP (`pmemsave`) and read offline: driver and device objects found by
  their type and size words, the HID stacks' extensions read through the
  guest's page tables (kernel space, CR3 from the register dump).
- Static reads: ME's `HIDUSB.SYS` (14,256 bytes, SHA-256 `6648b28c...2b91`),
  ME's `HIDCLASS.SYS` (25,584 bytes, `07b5d374...59cb`) and 98 SE's
  `HIDUSB.SYS` (9,296 bytes, `307cd8d8...0196`), copied off the base images
  and disassembled; addresses below are image VAs at base `0x10000`.

### Readings, Windows ME

| Run | Build | Cycles before the stop | Last PnP the PDOs saw |
|---|---|---|---|
| b | gated `qemu` | 17 | a hub-port PDO's START refused (device gone), then STOP, then the hub's relations without it, then nothing |
| c2 | `qemu` + START of a gone PDO answered success (experiment) | 4 | START succeeded, then STOP at once, then the relations without it, then nothing |
| d2 | ungated `qemu`, unmodified | 10 | START succeeded with the device still present, then STOP, then the relations without it, then nothing |

All three: the tray clock frozen at the series' last minute, no recovery, the
processor in the VMM's idle loop (`c00138c1`/`c00138c2` most samples), as 43.13
saw. The controller thread was still alive (a mouse removed afterwards was
seen: its transfers cancelled, the counters emitted), but no PnP IRP ever
reached any of the driver's devices again - neither the root hub nor the
hub asked for relations after later plugs and removals.

**Where it stops (runtime, memory read, runs c2 and d2).** The PDO the
relations had just omitted had a `hidusb.sys` FDO above it whose
minidriver extension read state 5 (removing), pending-request count 0,
and a waiter on its removal event; `hidclass.sys`'s state for the same FDO
read 6 (stopped). Every other HID stack in memory read state 2 (started).
So the REMOVE that followed the relations answer was blocked inside
`hidusb.sys`, before it was passed down to this driver, and the
configuration manager, which runs one device at a time in the system VM,
waited on it for good.

**Why (static, ME's `HIDUSB.SYS`).** `hidusb.sys` counts its requests from
0, and its removal event (a notification event, not signalled) is set when
the count goes below 0 (`0x10b00`). Its START (`0x11df9`): state 1, the
START passed down and waited for; on failure below, or of its own
descriptor and configuration requests after (`0x105b0`), state 6. Its STOP
calls the stop routine (state 3, the count decremented, the event waited
for, `0x12090`) **only when the state is 2** (`0x11e2d`), and sets state 4
after the STOP returns from below (`0x11fa6`). Its REMOVE (`0x12200`):
state 5, the count decremented only when the previous state was neither 3
nor 4, then the event waited for, then the REMOVE passed down. So a START
that failed (state 6), then a STOP (skipped, then state 4), then a REMOVE
(no decrement) waits on an event nothing will ever set. Windows ME sends
exactly that STOP after a failed START (runs b, c2, d2). This is a defect of ME's `hidusb.sys`, reached
whenever a USB HID device's start fails and its device then goes; it does
not depend on how the start failed:

- b: this driver refused the START of a PDO whose device had left
  (`hcd_pdo.c`, `hcdPdoStarted`).
- c2: with that START answered success instead (an experiment, not kept),
  `hidusb.sys`'s own descriptor requests to the departed device failed, and
  the same STOP followed.
- d2: the device left while `hidusb.sys`'s start requests were under way.

So the refusal is not the cause, and answering such a START success does
not cure it. The 2026-10-04 ME reading in `hcd_pdo.c`'s lifecycle note ("a
device pulled while its install was under way was started (refused),
stopped and left ... the shell wedged") reads as the same stop: the REMOVE
that "never came" was held in `hidusb.sys`.

**98 SE's `hidusb.sys` (static) has no such gate**: its STOP calls the stop
routine unconditionally (`0x1179b`), so the count reaches -1 and a later
REMOVE finds the event set. Its REMOVE logic is otherwise the same. And 98
SE's configuration manager, read at runtime, answers a failed START with
REMOVE, not STOP (98 SE run b: START refused, then REMOVE, and the series
went on).

### Readings, Windows 98 SE

98 SE did not stop. Two runs (a2: about 45 cycles; b: 60 cycles, with QEMU's
own `usb_xhci_*` trace) showed a different failure instead:

- The bus, not the system, failed: in a2 the controller was reset three
  times inside ten minutes, the fourth request was refused by task 35-T.6's
  window (`ctrl.window.refused`; the snapshot's verdict "LATCHED FAILED:
  the recovery window refused a fourth recovery"), and no device was seen
  after that. Windows stayed responsive throughout.
- Each reset followed a request to a device that had just been removed
  (a2: the SET_CONFIGURATION of a select; b: the first EP0 request after
  Address Device behind the hub) that never completed within
  5 s, which the driver treats as the controller's failure (`hcdCommand`'s
  `HCD_COMMAND_WAIT_MS` for a command; in b QEMU's trace shows no command
  after the Address Device, so it was the EP0 request's own wait).
- QEMU's trace (b) shows why it never completed: QEMU's `xhci_kick_epctx`
  returns without completing anything when the slot's device has been
  detached (`hcd-xhci.c`, `xhci_slot_ok`, "we must NOT continue"), so a
  transfer queued after the detach gets no event at all. Real controllers
  end such a transfer with an error, so this path is in part an emulator
  artefact; the driver's answer to it, a whole-controller reset, then spends
  the recovery window.

### The control: Windows ME on its own USB stack (2026-10-22)

The stock ME image (no `xhci98.sys`), a QEMU `piix3-usb-uhci` controller,
ME's own USB stack with the same 4.90 `HIDUSB.SYS`, one virtual CPU,
256 MB, the host still saturated. The same series: a hub at port 2, an
MS-DOS Prompt open, `usb-mouse` hot-plugs alternating port 1 and hub port
2.1, each removed as soon as the pointer moved, no gap. A `usb-mouse` was
first checked to move the pointer under ME's own stack.

- Series a: 80 cycles (13:41 to 13:47), no timeout. Series b: 80 cycles
  (13:53 to 14:01), plug-to-pointer 0.78 to 26.57 s, mean 5.57 s, no
  timeout. The tray clock advanced after each (75 s and 65 s checks).
- So ME's own stack did not stop in 160 cycles, where `xhci98.sys` stopped
  ME within 4 to 17. The defect is in ME's `hidusb.sys`, and any stack can
  reach it in principle (a START that fails, a STOP, then the REMOVE), but
  this series did not reach it on ME's own stack. Under `xhci98.sys`, ME's
  configuration manager reached a device's START many seconds after its
  PDO was reported, when at all (run b: 4 STARTs in 17 cycles, the rest
  removed before their START), so far more starts meet a device already
  gone. Why the configuration manager is that much slower here was not
  read; the log shows no request of its waiting on this driver.

Note, 2026-10-22 (task 43.54, `runs/run-43.md`): this conclusion does
not hold. With each mouse pulled a fixed 1.0 s after its plug, this driver
had the device addressed and started sooner than ME's own stack (medians
0.16 and 0.30 s against 0.58 and 0.66 s), and ME's own stack stopped the
same way, at cycles 58 and 8, with the same `hidusb.sys` state in memory.
The 160 clean cycles above came from the series' pointer detector, which
held most devices on ME's own stack past their START and, under this
driver, often fired before any START could come.

### Candidate fixes (none made)

No fix is clear and small, so none was committed. In the order the owner
may want to weigh them:

1. **ME: let `hidusb.sys`'s start succeed for a device that leaves before
   its stack has started** - answer the device and configuration
   descriptors from the PDO's own copies and a SELECT_CONFIGURATION with
   pipe handles that hold every later request until the REMOVE (the hold
   and park of design record 13 section 10.5), on Windows 9x only. Then
   STOP and REMOVE find state 2 and take the counted path. It changes the
   PDO lifecycle and the select path (record 13 sections 5.2 and 10.5); it
   needs its own design note and review.
2. **ME: never report missing a PDO whose START failed and was then
   stopped**, so ME never sends the REMOVE that blocks. That leaves a dead
   devnode until restart, needs the same device's return mapped onto it (an
   instance id may not be presented twice), and only moves the stop to the
   parent's removal (a hub pulled, a controller disabled). Not recommended.
3. **Record it as a Windows ME defect** in the release notes (rapid replug
   of a HID device on ME can stop the system; `hidusb.sys` 4.90) and keep
   the driver as it is.
4. **98 SE (and every target): a request to a departed device that never
   completes need not reset the controller** - before requesting the reset,
   check whether the slot's device has left (its root port or hub port
   reads disconnected, or a departure is pending) and complete the request
   DEVICE_GONE instead, as `hcdCfgFault` already does for a failed command
   on a departing device. Its trigger here is QEMU's; whether metal can
   reach it is unread.

Recommendation, revised after the control: 3 now, worded from the control
(the defect is ME's, but this driver reaches it where ME's own stack did
not in 160 cycles); 1 as a later task, now weighed higher; 4 as its own
task, read on metal before it is changed.

Owner, 2026-10-22: 3 taken (the release notes' "Known limitations"), 4
opened as task 43.53 with 1 noted there as the later option for ME, and
43.49 closed on this reading.

### Owed

- Why ME's configuration manager starts this driver's PDOs so much later
  than its own stack's (not read; 43.53's ME line). Read by 43.54
  (2026-10-22): it does not; see the note above.

## 43.52 two XHCITR defects from 43.45's legs: the code (2026-10-22)

43.45's legs on 7 x86 (2026-10-22, branch `p43-45`, its run sheet
`run-43.md`, its two XHCITR sections) found two XHCITR defects. This
section records their code on branch `xhcitr-fixes`. No virtual machine
was run for it; the legs were owed at the code review (below), and
"43.52 the legs", after this section, records them.

### The unelevated eject's tunnel line

Read from the code. Without administrator rights `ej_run`
(`xhcitr\xhcitr.c`) does not confirm its own eject (`ej_mark_confirm`
only when `ej_is_admin()`); the mark service does, at a pass that finds
the unit held for eject (problem 47), and `mk_confirm`
(`xhcimark\xhcimark.c`) writes `XhciCmEjectConfirm` under the tools'
eject mutex, `Global\XhciinstTbAsk`, waiting 1 s for it. XHCITR held
that mutex through its 10 s `ej_watch_tunnel` and the assessment after
it, so the service's confirmation could only follow the tool's answer:
43.45 read `cm.reenum.confirm` 12 to 18 s after OK. That also explains
why waiting longer alone would not have helped.

The change: `tr_eject_watch_kind` (`xhcitr\tr_core.c`) decides the
watch. An administrator's run, and the not-removable route, keep the
10 s watch and its line. A run without administrator rights on XP and
later, the service not read as stopped or absent, releases the mutex
before the watch and waits 10 s plus twice the service's idle period
(`mk_poll_ms(0)`, 10 s; `test_tr` checks the two agree), 30 s, a budget
rather than a bound the service promises; the watch ends at the
installer's quit event, as 30 s outlasts the installer's 15 s wait for
the tool to end. A tunnel still up then reads "The PCIe tunnel has not
been seen closed yet. It closes if the xHCI98 mark service confirms the
eject to the Thunderbolt driver, which the service does in the
background; an eject made as an administrator is confirmed by this tool
itself." Without administrator rights on 2000 (problem 24 is no
confirmation), or with the service stopped or absent: 10 s, then "The
PCIe tunnel stays up until the device is unplugged, because the eject
was not confirmed to the Thunderbolt driver; an eject made as an
administrator is confirmed by this tool itself." `XHCIINST` always runs
as an administrator and is unchanged.

### The balloon after a sleep

`tr_diff` raised "The Thunderbolt/USB4 controller stopped answering"
whenever an NHI's liveness left ACTIVE; `CmLiveDisarm` sets OFF ("not
monitored") at every sleep and stop. It is now raised only for ACTIVE
to a loss state (`tr_live_lost`: suspect, absent, configuration lost,
recovering, recovery failed, contained, preparing), or for the record's
stage going from active to failed, which a resume that fails leaves
with the liveness OFF (`cm_power.c`); once an NHI either way. OFF,
revoked and a value the tool does not know raise nothing. `test_tr`:
ACTIVE to OFF to ACTIVE raises nothing; ACTIVE to each loss state, and
the failed stage, raise it once.

### Builds and review

Host suites pass (`test_tr` 1002 checks). The gated builds, x86 `all`
and amd64 `all`, pass at `38fc1f45`. Reviewed and revised once; the
second reading found nothing. Commits: `650f5fc6` (the eject),
`cded3b9b` (the balloon), `38fc1f45` (the revision).

### Owed at the code review

The legs, on 7 x86 in the emulator under the USB4 profile with the
ASM2464 router, as 43.45 ran them (run 2026-10-23: "43.52 the legs",
below):

- an eject from XHCITR started without administrator rights: the tunnel
  read closed within the watch (`cm.reenum.confirm` shortly after OK),
  or the new line; and the same eject as an administrator, unchanged;
- a sleep or hibernate and resume with XHCITR in the tray: no balloon;
- where the vehicle allows, a resume that fails: the balloon.

## 43.52 the legs: XHCITR's eject without administrator rights, and the resume balloon (2026-10-23)

Task 43.52 of `roadmap-usb4.md`, read on 2026-10-23. The roadmap entry
carries the task; this section carries what each leg did and what it
read.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

### 43.52 - the legs on 7 x86 in the emulator

**Verdict: done** (2026-10-23, ticked on the owner's approval; its own
remaining legs pass, and leg b's defect is task 43.70). The eject from XHCITR
without administrator rights closes the tunnel once the mark service
confirms it. With the service stopped, the box says the tunnel stays up
until the unplug. After a hibernate and resume there is no "stopped
answering" balloon. One leg asked for alongside them did not pass: an
elevated `XHCIINST /eject` left the PCIe tunnel up on this q35 machine
three times out of three (below). The 43.52 change does not touch that
path (an administrator's run is unchanged by design), so the leg reads a
separate defect, taken up as task 43.70.

**Build and package.** Branch `3.2.0.0` at `062e02d7`, built in a
worktree of its own. The gated x86 `all` build passed ("BUILD + GATES
PASSED (debug release qemu)"). The package is a TEST cut of the release
flavour (`make-release.ps1 -Arch x86 -Flavor release -TestLabel "43.52
legs" -SkipQualtool -NoDriverPack`), stamped 3.1.0.0 as the tree is.
`XHCITR.EXE` SHA-256 `0ADA6353A1AEE895...`, the same in the guest
(`certutil -hashfile`).

**The machine.** usb4-emu at QEMU `46d96fb` (link retrain), TCG, 2 vCPUs,
1536 MB, q35. The host interface is the emulator's `1B36:00F4` on
`usb4-generic` with `host-model=amd host-domain=3`, its PCIe tunnel on
the q35 `usb4-tunnel-port` (native PCIe hot-plug, `link-retrain` on). The
enclosure is the ASM2464 model with a FAT volume (`E:`). Windows 7 SP1
x86: a fresh overlay of `vm\win7.img` with `msahci` set to start at boot
(the image was installed on IDE), then `INSTALL /console /yes
/thunderbolt /stor98`. XHCITR runs from the logon, without
administrator rights. Each data check copies a 1 MiB file to `E:` and
compares it (`fc /b`). Kit `E:\usb4-emu-vm\lr-w7`.

| Leg | Result |
|---|---|
| a. XHCITR's eject from the tray, without administrator rights | **Pass.** Windows stops the device and turns the slot off. The tunnel closes (`admin-down` in the emulator's trace) 6 s after OK. The driver log shows `cm.reenum.confirm` then `cm.reenum.eject` and `u4.pcie.torn-down 1`. The balloon: "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The PCIe tunnel was closed." The confirmation came inside the watch, so the "not seen closed yet" line was not shown. |
| b. Elevated `XHCIINST /eject 1` | **Fails on this machine** (not a 43.52 path). Windows stops the device, and the tool writes its confirmation ("XhciCmEjectConfirm (serial N) written"). But it ends "The Thunderbolt driver has not confirmed that the PCIe tunnel was closed", and no teardown follows, even after a minute or more. Three runs: after a replug the driver log shows `cm.reenum.confirm.refused 3` (no entry of that serial); after a second replug, and again as the first eject after a restart, it shows `cm.reenum.confirm` and then `cm.reenum.eject.stale`. Read from the code (`CmReenumService`): the hand-off refuses an eject whose bridge publication or generation changed after the confirmation, or whose bridge is not ready. On q35, Windows' PCIe hot-plug driver turns the slot off as part of the eject, and the emulator then removes the endpoint. That changes the bridge's population just after the tool's immediate confirmation. The service's later confirmation in leg a lands after it. Inferred, not traced in the driver. The same kit on 2026-10-22, with the 43.45 test cut, read one elevated eject that closed. |
| c. XHCITR's eject without administrator rights, `xhcimark` stopped | **Pass.** Windows stops the device; the tunnel stays up (no `admin-down`). The box: "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The PCIe tunnel stays up until the device is unplugged, because the eject was not confirmed to the Thunderbolt driver; an eject made as an administrator is confirmed by this tool itself." |
| d. Sleep and resume with XHCITR in the tray | **Pass.** 7 x86 offers no standby here (`powercfg /a`: hibernate only; the display driver), so the leg used hibernate (`shutdown /h`), resumed with the enclosure present. After logon: no "stopped answering" balloon in 16 screenshots over 64 s, and XHCITR and `xhcimark` still running. |

**Two side readings from leg d, not 43.52's.** The volume did not come
back after the resume. Windows turned the slot off, so the emulator
removed the endpoint, and a replug brought `E:` back. A file written and
compared just before `shutdown /h` (`W5.BIN`) was not on the volume
afterwards. Earlier files were intact, and a new write compared clean.
Neither was investigated further.

**Owed elsewhere.** Leg b's defect (the elevated eject's confirmation
against the slot power-off under PCIe native hot-plug) is task 43.70;
the hibernate readings above are task 43.71.

## 43.32 the Windows 10 virtual machines (2026-10-22)

Two Windows 10 22H2 guests, x86 and x64, recorded in `build-and-test.md`,
"Windows 10 target VMs (roadmap-usb4 43.32)". Development host A; the
host loaded throughout (other agents' guests and builds; CPU near 97 % at
13:45).

**Verdict: the two TCG machines are set up; the OVMF guest with Secure
Boot on is not built, so 43.32 stays open.**

### What was done

- **The images** (2026-10-22, 11:13 to 12:40, the Windows 8, 8.1 and 10
  image session): `w10x64-pristine.qcow2` and `w10x86-pristine.qcow2`,
  10.0.19045.2965, Pro, from the owner's `Win10_22H2_English_x64v1.iso`
  and `Win10_22H2_English_x32v1.iso` by an unattended install - WinPE and
  the image copy under WHPX (owner: WHPX for the base-image installs
  only), specialize, OOBE and the first logon under TCG with the legs'
  `-machine pc -cpu qemu64,+3dnowprefetch`. `SkipMachineOOBE` and
  `SkipUserOOBE` were needed: without them 10's user OOBE stalled.
  Test-signing on, autologon, hibernation and Windows Update off, no
  network; a verify boot of each reached the desktop in under 7 minutes.
- **The prepared starting points** (12:55 to 13:38): each image booted
  once on an overlay that became `prep.qcow2` (now read-only): the
  scheduled task `xrun` (run level highest, as `xhci`) that calls
  `<transfer drive>\auto\go.cmd`, and a first reading of each guest. The
  task's logon trigger did not fire at the autologon on either guest; a
  leg starts it with `schtasks /run /tn xrun` from Win+R, which runs it
  elevated with no consent prompt.
- **The machine**: the usb4-emu QEMU (`vm\usb4\bin-2a893ea`), TCG
  `thread=multi`, `-smp 2`, 2048 MB (the host's limit for 8 to 10 guests
  that day), SeaBIOS, `qemu-xhci,p2=8,p3=4` with the tablet, two
  `usb-storage` (a USB 3 port and a USB 2.0-only port), `usb-uas`,
  `usb-audio`, `usb-hub` with `usb-kbd`, and the emulator's Thunderbolt 3
  NHI with its xHCI and tunnel bridge; QMP 48660 (x86) and 48661 (x64).

### The speed under TCG (read first, as the entry asks)

Power-on to the autologon desktop about 4.5 minutes at `-smp 2` on both
guests; a forced driver install 7 s (x64) to under 3 minutes with its
prompt (x86); `msinfo32 /report` about 10 minutes, the longest step of a
reading. **Usable for reading and install legs on TCG**; no WHPX was
needed for any leg, so the owner's choice between a slower leg and WHPX
does not arise for these. Timings are from a loaded host and are noisy.

### Not done

- **The OVMF guest with Secure Boot on** (for 43.36's refusal case).
  The usb4-emu build carries `edk2-x86_64-secure-code.fd` (needs `q35`
  with SMM) but no variable store with Microsoft's keys enrolled, and no
  `EnrollDefaultKeys` application; the keys would have to be enrolled by
  hand in the firmware's Secure Boot configuration (custom mode: a
  platform key of the project's own, Microsoft's KEK and Windows
  Production PCA 2011 certificates from Microsoft's download pages), and
  10 x64 installed fresh under UEFI, since the MBR images do not boot
  under OVMF. Not attempted in this task; an owner question in the
  report.

## 43.33 Windows 10, read first: the guest legs (2026-10-22)

The measured half of 43.33; the static reading and the conclusions are
design record 13 section 6.14 ((a) and (b)), record 21 section 3.6 ((c))
and record 23 section 5.12 ((d) and (e)). Runtime, development host A,
on 43.32's two guests, one at a time, host loaded (other agents' guests
and builds). Nothing of this project's was built: the package is the
published `3.1.0.0` (`releases\3.1.0.0`, `release-x64` and the x86
release directory), copied to the transfer drive's `pkg\`.

### The vehicle

- The guests of 43.32 (`build-and-test.md`, "Windows 10 target VMs"):
  10.0.19045.2965 Pro, legacy BIOS ("BIOS Mode Legacy", "Secure Boot
  State Unsupported", "Kernel DMA Protection Off", "Virtualization-based
  security Not enabled", `msinfo32`), test-signing on, `RtlGetVersion`
  10.0.19045, platform 2, product type 1.
- A leg overlay per guest (`w10x64-x98`, `w10x86-x98`) over the prepared
  starting point, the full device set (`tab,ss3,ss2,uas,aud,hub,nhi`),
  `-smp 2`, 2048 MB.
- The leg's script `go.cmd` (started by `schtasks /run /tn xrun`): a
  reading under Windows' own stack (`ms`); the installer's NT route by
  hand, `UpdateDriverForPlugAndPlayDevicesW` with `INSTALLFLAG_FORCE` on
  the `qemu-xhci` controller's first hardware id with the package's
  `xhci98.inf` (`force.ps1`); a reading (`x98a`); a restart and a
  reading. Each reading (`read2.ps1`): `pnputil /enum-devices /connected
  /drivers` and `/enum-drivers`, the Enum, Class, service and USB keys,
  the setup logs, the System, Kernel-PnP, USB and Code Integrity event
  logs, `bcdedit`, `msinfo32 /report`, the USB scheduled tasks, written
  to the output disk and read on the host with 7-Zip after power-off.

### x64 (13:38 to 14:33)

- `ms`: the devices of record 13 section 6.14.3's table: `qemu-xhci` and
  the emulator's xHCI under `usbxhci.inf` (`00802005`), root hubs and
  the USB 2.0 hub under `usbhub3.inf`, HID under `input.inf`, both
  `usb-storage` under `usbstor.inf`, `usb-audio` composite with
  `wdma_usb.inf` on `MI_00`, **`usb-uas` Code 10 under Windows' own
  `uaspstor.inf`**, the NHI `8086:15BF` Code 28. Explorer's details pane
  (This PC, a drive selected): F: "Connected to USB 3.0", G: no line.
- The forced install, 14:10: **refused**, `0xE000022F`, in 7 s, no
  prompt; the setup log: "Driver package does not contain a catalog
  file, and Code Integrity is in Test Signing mode", "Failed to import
  driver package into Driver Store". The controller stayed on
  `usbxhci.sys`; the guest was powered off with no restart leg.

### x86 (14:34 to 16:12)

- `ms` (14:47 to 15:05): the same table as x64's, `usb-uas` Code 10 and
  the NHI Code 28 included. (A second instance of the leg's script,
  started twice by the previous operator, took a second reading under
  Windows' own stack from 15:05 to 15:09, filed as `x98b`; it is a
  duplicate of `ms`, not a post-restart reading.)
- The forced install, 15:16 to 15:22: "Windows can't verify the
  publisher of this driver software" (Windows Security), "Install this
  driver software anyway" clicked; `UpdateDriverForPlugAndPlayDevicesW`
  returned TRUE, reboot required, 349 s with the prompt. The setup log:
  "Driver package does not contain a catalog file, but user wants to
  install anyway", "Signer Score = 0x80000000 (Unsigned)", the package
  imported as `oem2.inf`, `xhci98.sys` and `xhcihlpr.sys` copied, the
  services `xhci98` and `xhcihlpr` added, `DelService=xhcimsi`, and
  "Device required reboot: 'Reboot' in section: [Xhci.Dev6.NTx86]".
- `x98a` (15:24 to 15:29, before the restart): the controller's devnode
  "xHCI98 USB 3.x eXtensible Host Controller", `oem2.inf`, started, with
  the children of the stack it had before still listed under
  `usbhub3.inf`, `usb.inf` and `uaspstor.inf`: the restart had not yet
  happened, so nothing of `xhci98.sys` ran yet. Explorer's details pane
  at 15:41 showed F: "Connected to USB 3.0"; taken in that state, it is
  not counted for either driver.
- Restart 15:43; `x98c` (15:54 to 16:00), **`xhci98.sys` running on
  Windows 10 for the first time in this project**:

| Device | Bound | State |
|---|---|---|
| `qemu-xhci` `1B36:000D` | `oem2.inf` (`xhci98.inf`), "xHCI98 USB 3.x eXtensible Host Controller"; `usbxhci.inf` still listed as "Best Ranked" (`00802005`) beside it | started |
| its root hub | `USB\XHCI98_ROOT_HUB`, `oem2.inf` (`80FF0000`) | started |
| `usb-hub` port 8 | `USB\XHCI98_HUB&VID_0409&PID_55AA`, `oem2.inf` (`80FF0002`) | started |
| `usb-kbd` behind it, `usb-tablet` port 5 | `input.inf` (`USB\Class_03&SubClass_01`, `USB\Class_03`) | started; the pointer and keyboard worked |
| `usb-storage` `P4333SS3` (USB 3 port), `P4333SS2` (USB 2.0 port) | `usbstor.inf`, disks `disk.inf`, volumes F: and G: | started |
| `usb-audio` port 7 | `USB\XHCI98_COMPOSITE&VID_46F4&PID_0002`, `oem2.inf`; `MI_00` `wdma_usb.inf` | started |
| `usb-uas` port 2 | `uaspstor.inf` | Code 10, as under Windows' own stack |
| the emulator's xHCI `8086:15C1` | `usbxhci.inf` (untouched: the force names only `1B36:000D`) | started |
| the NHI `8086:15BF` | none | Code 28 |

  Code Integrity event 3001: "an unsigned kernel module ...
  `xhci98.sys` is loaded into the system"; System events 20003 for the
  `xhci98` service on the root hub, composite and hub. The NHI's ids,
  read from its Enum key: `HardwareID` `VEN&DEV&SUBSYS&REV`,
  `VEN&DEV&SUBSYS`, `VEN&DEV&CC_088000`, `VEN&DEV&CC_0880`;
  `CompatibleIDs` `VEN&DEV&REV`, **`VEN&DEV`**, `VEN&CC_088000`,
  `VEN&CC_0880`, `VEN`, `CC_088000`, `CC_0880` - so on 10 the bare
  `PCI\VEN_8086&DEV_15BF` that `xhcicm.inf` names is a compatible id,
  and Intel's `&CC_0880` form a hardware id (record 21 section 3.6).
- A second boot (16:03 to 16:12) for Explorer's details pane under
  `xhci98.sys`: This PC, each drive selected - F: "USB Drive", **"Connected
  to USB 3.0"**; G: "USB Drive", no line; as under Windows' own stack on
  x64. Shut down 16:12; every guest of this kit is off (pids 65764,
  41336, 25916 in the kit's `pids.txt`).

### Not read

- A restart leg on x64: the package cannot be staged there without a
  catalog (record 13 section 6.14.5).
- Device Manager's pages under `xhci98.sys`, a hot-plug, Sleep, an
  update and the uninstall back to `usbxhci.sys`: 43.37's.
- Intel's Thunderbolt driver in a guest: no image of it here; its INFs
  were read statically (record 21 section 3.6).

## 43.54 - Does Windows ME Start This Driver's Devices Late?
Roadmap-usb4 task 43.54, opened by the owner on 2026-10-22 ("start ME
investigation") from task 43.49's control (`runs/run-43.md` on branch
`p43-49`): ME's own USB stack had run 160 cycles of 43.49's plug and
unplug series without stopping. 43.49 put that down to ME's configuration
manager starting `xhci98.sys`'s device PDOs late, so that more starts ran
into a device already gone. This run measured plug-to-START on both
stacks. Development host A, QEMU under TCG, one virtual CPU, 256 MB,
headless; the host was loaded by other agents' guests throughout, so the
absolute times below are slow, and each comparison was run close in time
to the other.

### Verdict

**ME does not start this driver's devices late.** In a series that pulls
every mouse a fixed time after its plug, `xhci98.sys` had the device
addressed and the class driver's START selecting its configuration in
about half the time ME's own stack took (medians 0.16 and 0.30 s, against
0.58 and 0.66 s). **ME's own stack stopped too** in that series, at
cycles 58 and 8, with the same `hidusb.sys` state read from the guest's
memory as 43.49's stop. 43.49's 160 clean cycles came from how that
series decided when to pull, not from either stack. Nothing on this
driver's side needs a fix for this task. The release notes' known
limitation needs rewording (below).

### Method

- Guests: ME + `3.1.0.0` with the `qemu` flavour of `2f46080a` copied over
  `xhci98.sys` (`qemu-xhci`, `p2=4,p3=4`; a mouse at root port 1, a stick
  at 2, a hub at 4, an MS-DOS Prompt open), as 43.49. Registry as 43.49:
  `XhciInterruptMode` 2, `XhciPollIdleMs` 50, `XhciPollFineTimer` 0,
  `XhciLogVerbosity` 2. Two binaries: the gated build, and an ungated
  rebuild of the same source with trace lines added (each PnP IRP's PDO,
  each URB but interrupt transfers, each request held for a departed
  device and its cancel, each deferred refusal). Not kept.
- The control: stock ME (no `xhci98.sys`) on a QEMU `piix3-usb-uhci`,
  ME's own USB stack, a hub at port 2 and an MS-DOS Prompt open, as 43.49's
  control. Run c on a fresh overlay of the stock image.
- The timings: QEMU's own trace (`-trace usb_set_addr`, `usb_set_config`,
  host-stamped). `usb_set_addr` is the stack addressing the new device;
  the first `usb_set_config` after the plug is `hidusb.sys`'s START
  selecting the configuration. The same two events on both stacks. On
  `xhci98.sys` also the driver's debugcon trace, host-stamped as read.
- **The fixed series**: `usb-mouse` hot-plugged alternately at a root port
  and at a hub port (`xhci98.sys`: 3 and 4.1; UHCI: 1 and 2.1), each pulled
  exactly 1.0 s after its plug (`device_del`), the next plugged as soon as
  QEMU had dropped it; no pointer detector. After four cycles in a row
  without a START, the tray clock was checked across 75 s, and a frozen
  clock taken as a stop. At a stop the guest's memory was saved and read
  with 43.49's parser (`devs.py`).
- **43.49's series**, for the comparison: the same plugs, each pulled when
  a screen detector saw the pointer move (`plug.py`'s detector), no gap.
- **Spaced plugs**: the same plugs 6 s apart, each held until 3 s after
  its START.

### Readings

#### The fixed series (pull at 1.0 s)

| Stack | Run | Cycles | Addressed by 1.0 s (median) | START by 1.0 s (median) | Stop |
|---|---|---|---|---|---|
| `xhci98.sys`, gated `qemu` | b2 | 13 | 10 (0.17 s) | 3 (0.21 s) | at cycle 13 |
| `xhci98.sys`, `qemu` + trace lines | c | 60 | 53 (0.16 s) | 42 (0.30 s) | none in 60 |
| ME's own (UHCI) | b | 58 | 21 (0.58 s) | 18 (0.66 s) | at cycle 58 |
| ME's own (UHCI), fresh overlay | c | 8 | 3 (0.66 s) | 2 (0.99 s) | at cycle 8 |

Each of the three stops: the tray clock still across 75 s and 90 s,
typed text never echoed, the processor in the VMM's idle loop
(`c00138c1`/`c00138c2`, as 43.49's), and in the saved memory a
`hidusb.sys` minidriver extension in state 5 with its pending count 0 and
a waiter on its remove event - 43.49's signature: a STOP after a failed
START, then a REMOVE waiting for ever (b2: `c15aac3c`; UHCI b: `c145d4ec`;
UHCI c: `c153d56c`). On ME's own stack the START that preceded run b's
stop came 0.95 s after the plug, 0.05 s before the pull.

#### Spaced plugs

| Stack | START after the plug, once the device was installed |
|---|---|
| `xhci98.sys` (gated, run a, cycles 5 to 12) | 0.21 to 0.32 s |
| ME's own (UHCI, run a, cycles 6 to 12) | 0.42 to 0.50 s |

The first plugs on either stack took 4 to 14 s (ME installing the
device); the rows leave those out.

#### 43.49's series

On `xhci98.sys` (trace build, 40 cycles, no stop; the gated build's run
of the same series stopped answering at cycle 16, its clock check not
read), 13 of 40 devices were pulled before their
START, 9 of them 0.01 to 0.24 s after the plug - before this driver had
reset the port, which it does 0.16 to 0.25 s after the plug. The detector
saw a screen change that was not the new mouse's pointer, so these pulls
were the series', not the stack's. 43.49's UHCI control, run with the
same detector, had a plug-to-pointer of 0.78 to 26.57 s (mean 5.57 s): it
held most devices well past their START, and a START that has finished
before the pull does not take `hidusb.sys`'s failing path. That, not a
late START under `xhci98.sys`, is why that control did not stop in 160
cycles.

#### Where the configuration manager waits

The trace build shows ME sometimes saying nothing for 4 to 7 s after a
device leaves - no PnP request to any of this driver's devices - and then
asking the gone device's relations, cancelling its two held interrupt
reads, and sending its REMOVE (in 43.49's series 12 of 44 such answers
came more than 2 s after a plug; in the fixed series 2 of 48). The driver
had already reported the change; nothing of ME's waited on it. ME's own
stack cannot be traced at that level here, but it addressed only 21 of 58
devices within the second, against 53 of 60, which an idle configuration
manager does not explain either.

One effect of those waits is this driver's own: a port's next device is
enumerated only once ME has removed the previous device's PDO there, so
during such a wait the next device at that port is not addressed (all 7
of the 60 fixed-series cycles that were not addressed came 1.6 to 4.5 s
into such a wait). It lengthens no START that ME would otherwise have sent.

### Candidate changes

None to this driver: the START is not late, and the stop reaches ME's own
stack in the same series. 43.49's candidate 1 (let `hidusb.sys`'s start
succeed for a device that leaves first, on 9x) remains the only way to
make ME stop less often than its own stack does. It is no longer
justified by this driver reaching the defect more readily, so it weighs
less than 43.49 put it.

### The release notes

The known limitation 43.49 added (`docs/using/release-notes.md`, on branch
`p43-49`, not yet on `3.2.0.0`) says ME's own stack ran 160 cycles
without stopping and that this driver "reaches it far more readily".
These readings contradict both statements. Proposed replacement for its
last three sentences, to apply when `p43-49` is merged (owner, 2026-10-22:
accepted; applied after the merge, worded as below with the defect named as
any stack's and this driver's starts no later than ME's own stack's):

> It was seen in a virtual machine after 4 to 17 back-to-back plug and
> unplug cycles; ME's own USB stack, on a UHCI controller in the same
> virtual machine, stopped the same way after 8 and 58 cycles of a
> series that pulled each mouse one second after its plug. Plugs a few
> seconds apart, each left until the device worked, did not reach it.

### Owner, 2026-10-22

All three accepted: 43.54 closed with no driver change, the release notes'
known limitation reworded from these readings, and 43.53's ME option (43.49's
candidate 1) lowered to a low-priority later idea. `runs/run-43.md`'s 43.49
section carries a dated note pointing here.

### Owed

- Nothing for this driver. What ME's configuration manager does during
  its 4 to 7 s waits was not read (it happens on its own stack as well,
  and is ME's).

## 43.22 Windows 8 and 8.1, a target of the connection manager: what NT 6.2 and 6.3 need (2026-10-22)

Branch `p43-22`, from `3.2.0.0` at `46e7d2af`. The roadmap entry lists what
an install on NT 6.2 and 6.3 needs that 42-A.21 did not read; each item
below says what was read and how. The install, update and uninstall legs,
the emulator's tunnel legs and the NVMe legs are 43.24's; this task read
the install alone, one per system, with the published `3.1.0.0` package
(the installer as 42-A.21 coded it; `release-x64\xhcicm.sys` SHA-256
`b643972abf8f940c2fd5f07ecb1b55871a8a5e5f78433a210f718e74f2444de0`, an
embedded test signature, security directory 1,496 bytes; `release-x86`'s
`d7202c39e01450e051b65496ad66d7e94e6a537c8a601729ae0971a4465f602b`,
unsigned).

### The vehicle

Development host A, QEMU, headless, TCG: the usb4-emu QEMU
(`vm\usb4\bin-2a893ea`, SHA-256 `6a1dfcc8...bd66`, the hash every kit
checks), `-machine pc` (SeaBIOS: no Secure Boot), `-cpu
qemu64,+3dnowprefetch`, smp 2, a `qemu-xhci` with a tablet, and the
emulator's `tb3-alpine-ridge` NHI (`8086:15BF`) with its tunnel bridge
(`1B36:0001`) and xHCI - the `p43-33` kit's topology. Overlays of the
read-only pristine images of the `win8-10` set: Windows 8 Pro x86
(6.2.9200), Windows 8 Pro x64 (6.2.9200) and Windows 8.1 Pro x64
(6.3.9600, with Update); test-signing mode on in each, as built. **No
Windows 8.1 x86 image exists** (no medium; owner, 2026-10-22: 8.1 32-bit
is skipped for `3.2.0.0`, recorded as untested ground, covered by 8 x86
and 8.1 x64). A guest script (`read.ps1`, kit `E:\xhci98-vm\p43-22`) read
`SystemStartOptions`, `sc qc`, the PCI devices' problem codes and
drivers, the `UpperFilters`, and `setupapi.dev.log` onto a second disk,
read after power-off with 7-Zip; the guest ran `INSTALL /console /cmonly
/yes`, elevated, from a VVFAT drive. The host was saturated (another
agent's builds and a dozen guests): nothing here is a timing. Kit ports
48620 to 48622.

### Results

| System | `INSTALL /console /cmonly /yes` in test-signing mode | On a start with signature enforcement disabled | After |
|---|---|---|---|
| 8 x86 | **installs**: two "Windows can't verify the publisher" prompts (`xhcicm.inf`, `xhcicmf.inf`), answered; `Cm.Dev6.NTx86` and `CmF.Dev6.NTx86`; NHI on `xhcicm`, started; bridge filtered, started; `xhcimark` running; exit 3 | - | `xhcicm` `BOOT_START` ("Updated service 'xhcicm' to be boot start"); with test-signing off and on again, each start reached the desktop with the NHI and the bridge at problem 0 and `xhcicm` running |
| 8 x64 | **refused**: "xhcicm.inf could not be staged (error 0xE000022F)"; `setupapi.dev.log`: "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode" / "The third-party INF does not contain digital signature information"; "No changes were made", exit 2 | installs as on 8 x86 (the two prompts; "Driver package does not contain a catalog file, but user wants to install anyway"); `Cm.Dev6.NTamd64`, `CmF.Dev6.NTamd64`; NHI and bridge started, exit 3 | `xhcicm` `BOOT_START`; the next start, test-signing mode off, stopped in "Attempting repairs" (Automatic Repair) and stayed there through two launches, over an hour |
| 8.1 x64 | **refused**, the same error and log lines; the local copy made, exit 2 | installs as on 8 x64, exit 3 | `xhcicm` `BOOT_START`; a plain start in test-signing mode (`SystemStartOptions` `TESTSIGNING NOEXECUTE=OPTIN`) reached the desktop with the NHI on `xhcicm`, started, and the bridge filtered: the test-signed SHA-1 binary loads in test-signing mode |

`SystemStartOptions` on a start with enforcement disabled read
`TESTSIGNING NOEXECUTE=OPTIN DISABLE_INTEGRITY_CHECKS` (8 x64, 8.1 x64);
in test-signing mode `TESTSIGNING NOEXECUTE=OPTIN`; with it off
`NOEXECUTE=OPTIN`. `HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot`
holds `AvailableUpdates` alone and `Confirm-SecureBootUEFI` answers "Cmdlet
not supported on this platform" on these SeaBIOS guests. The installer
named the systems "Windows 8" and "Windows 8.1" (`RtlGetVersion`), offered
the connection manager alone and left Stor98 out, as 42-A.21 coded; its
x64 note said "Test-signing mode is on: the test-signed x64 connection
manager loads." and the staging then failed. The console tool waits for
Enter at its end even with `/yes` and its output redirected; a script feeds
it `< NUL`.

### What the reading settles, item by item

- **The INFs' decorations**: right as they are. 8 and 8.1 take
  `NTx86.6.0` / `NTamd64.6.0` and the `Dev6` install sections, as the host
  vectors of `inst_models_section` say; `Signature="$CHICAGO$"` and
  `$Windows NT$` both accepted. No INF change.
- **The x64 test signature with Secure Boot off and test-signing on**: the
  binary loads (8.1 x64, a plain start; 8 x64 in the start with
  enforcement disabled), but the package does not install in test-signing
  mode: Windows 8 and 8.1 x64 refuse an INF with no catalog there. Record
  19's premise that a catalog governs only the prompt holds for Vista and
  7 and not for 8 and 8.1.
- **Boot start**: Windows 8 and 8.1 make `xhcicm` boot start through the
  bridge filter (32-bit too). On x64 that turns test-signing mode from
  "needed for the driver to load" into "needed for Windows to start". Read
  on 8 x64 only, with a hung Automatic Repair; the cause is inferred from
  the boot-start change and the test signature, not read from a boot log.
  64-bit 8.1 and 32-bit 8 read `BOOT_START` too.
- **32-bit 8 and 8.1 with Secure Boot on**: Microsoft's "Driver Signing
  Policy" page ("Signing requirements by version"; `ms.date` 2024-08-19,
  read 2026-10-22) lists "64-bit, 32-bit" for "Windows 8, Windows 8.1,
  Windows 10, versions 1507, 1511 with Secure Boot on", against "64-bit
  only, no signature required for 32-bit" for "Windows Vista, Windows 7;
  Windows 8+ with Secure Boot off". It does: so the note of record 23
  section 5.10 covers the x86 package there (owner, 2026-10-20). With
  Secure Boot off (SeaBIOS) 32-bit 8 loaded the unsigned driver with
  test-signing mode on and off. Secure Boot on is unread: no guest here
  has it.
- **The installer's 8 and 8.1 path as 42-A.21 coded it**: version, name,
  the connection manager alone, Stor98 left out: as designed. Wrong: on
  8 and 8.1 x64 it went on in test-signing mode into a staging Windows
  refuses, said nothing of why, and its note promised the driver would
  load; and it would install outside test-signing mode with a note, which
  on 8 and 8.1 x64 leaves a machine that does not start. Fixed, commit
  `6c9e49f1` and the commit that closes this task (record 23 section
  5.10, revision 30; numbered 30 because `p43-34` and `p43-27` take 27
  and `integ` and `p43-42` take 28 and 29): `inst_cm_boot_gate` refuses the connection manager on 8 and 8.1 x64 unless the package is
  test-signed and test-signing mode is confirmed, and warns when it goes
  on; `inst_stage_error_text` explains `0xE000022F` there and names the
  start without enforcement; on 32-bit 8 and 8.1 `inst_sig_note_kind`
  prints the Secure Boot note, and a Code 52 there is reported as that
  refusal. Host vectors in `test_inst.c`. The three decisions key on the
  kind `inst_os_kind` gives (`INST_OS_CMONLY`), not on 6.2 and 6.3, and
  their texts say "this Windows", so Windows 10 below build 22000, which
  `p43-34` (task 43.34) admits to that kind, takes them on the merge;
  whether 10 makes `xhcicm` boot start and refuses the catalog-less
  package in test-signing mode is 43.34's to read. The note that told 8
  and 8.1 x64 users that test-signing mode needs Secure Boot off, after
  the test-signing note, is gone: the gate's texts say it on those
  systems, and the branch is dead code otherwise (`p43-34` edits the same
  lines for 10: on the merge, take the removal).
- **8.1 with Intel's own Thunderbolt software**: not read in a guest (no
  image of it at hand; 43.24's leg, "where an image of it is at hand").
  Read from the code: a controller another driver holds is foreign and
  left (decision D3), so the NHI keeps Intel's driver; the filter still
  goes onto the bridges, where it does nothing without this driver on the
  NHI (record 21 section 3.2) - but on 8.1 x64 it makes `xhcicm` boot
  start all the same, so the gate applies. Intel's release notes for its
  Thunderbolt software 16.3.61.275 (as reposted on a vendor forum, not
  read on Intel's site) list Windows 7 32- and 64-bit, 8.1 64-bit and 10
  64-bit, Falcon Ridge and Alpine Ridge, and call it the last package for
  7 and 8.1; no 8 and no 8.1 32-bit.
- **README and the release notes**: moved from "never run" to what was
  read here (install alone), the legs still 43.24's; 8.1 32-bit in
  "Untested ground".

### Builds

| Build | Result |
|---|---|
| `xhciinst\build.cmd`, `6c9e49f1` | passed |
| host suites (`test\run-host-tests.cmd`), `6c9e49f1` | 0 failures (`test_inst` 1790 checks) |
| host suites, the closing commit (the gates keyed on the kind, Secure Boot's state) | 0 failures (`test_inst` 1798 checks) |
| host suites after merging `3.2.0.0` at `a48e8688` (Windows 10 below build 22000 now `INST_OS_CMONLY`; vectors added for 10 and 11 through every gate) | 0 failures (`test_inst` 1821 checks) |
| `xhciinst\build.cmd`, `build-driver.cmd all`, `all -amd64` after that merge | passed (`BUILD + GATES PASSED (debug release qemu)` for x86 and amd64) |
| `xhciinst\build.cmd`, `build-driver.cmd all`, `all -amd64`, `544cfcd4` | passed (`BUILD + GATES PASSED (debug release qemu)` for x86 and amd64; a first run failed the self-test's "every screen within 79 columns" on an 80-column line of the x86 Secure Boot note, rewrapped before the commit) |

### Not read

8.1 32-bit (no medium). Secure Boot on, either architecture (no UEFI
guest). The way back from Automatic Repair on 8 x64 by a start with
enforcement disabled: the guest stayed at "Attempting repairs" through
two launches (13:51 to 15:04 on the second), never reaching the menu, and
was closed; the warning's way back rests on Microsoft's documented
Startup Settings, not on a reading. Whether 7 x64 also makes `xhcicm`
boot start (the same filter; 42-A.21's 7 leg was 32-bit, and no x64 leg
has read `sc qc xhcicm`): owed, and if it does, 7 x64 has the same hazard
and the gate should cover it. Intel's own Thunderbolt software on 8.1 (no image). The update and
uninstall on 8 and 8.1, the
tunnels, eject and NVMe (43.24). The new installer in a guest: its
refusal, warning and notes rest on the host vectors and the self-test's
screens (a package with an x64 half needs the signing key, which this
session did not have).

### Secure Boot's state (owner, 2026-10-22)

The owner accepted `p43-34`'s design (record 26 section 5.3.2): the
installer reads `UEFISecureBootEnabled` (on, off, not known) and shows it
in the signature note on 8, 8.1 and 10, never changing it; with Secure
Boot on, test-signing mode never counts as confirmed and the user is told
to turn Secure Boot off first. Coded here (record 23 section 5.10,
revision 30): `inst_sb_state`, `inst_sb_applies`, `inst_sb_text`,
`inst_ts_confirmed`, keyed on the connection-manager-alone kind so 10
takes them; on 32-bit 8 and 8.1 a known state replaces the Secure Boot
note by its case. Host vectors for the three states on each system and
through the gate. The value is read on no guest here (SeaBIOS only).

The owner's answers the same day: Intel's own Thunderbolt driver on a
controller is another vendor's device and is left alone, on 8.1 and 10
alike (as the code does: the NHI is foreign and left); the per-system
"untested" warning is 43.37's.

### Owner questions

1. A signed catalog for the x64 package's INFs (record 19 decided against
   one): with a test-signed catalog, 8 and 8.1 x64 would install in
   test-signing mode with no start without enforcement. Recommendation:
   yes for `xhcicm.inf` and `xhcicmf.inf` at least, signed with the same
   test key the packaging scripts sign the x64 binaries with (no script
   here makes a catalog yet); Vista and 7 accept a catalog-less package
   now, so they lose nothing. It does not cure item 2 of record 23's
   revision 30: the boot driver still needs test-signing mode. Owner,
   2026-10-22: yes, a catalog for every x64 INF (43.36 below; record 23
   revision 33).
2. Whether 7 x64 also makes `xhcicm` boot start through the filter: if it
   does, 7 x64 on starts with enforcement disabled (F8 at every start) is
   the same hazard, and the gate should cover it. Recommendation: read it
   in 43.24's or 43.31's 7 x64 leg (`sc qc xhcicm` after the install) and
   widen the gate to Vista and 7 x64 if so. Owner, 2026-10-22: read in
   43.24's or 43.31's 7 x64 leg.
3. On a controller another driver holds (Intel's software, left alone
   as the owner decided), the filter still goes onto its bridges, which
   run Windows' own `pci`, where it does nothing, yet on 8 and 8.1 x64 it
   makes the system depend on test-signing mode. Recommendation: leave
   the filter out too where no NHI in the run is this driver's (a small
   installer change, host-testable), as a later task. Owner, 2026-10-22:
   a new task, 43.58, not this one.

## 43.36 the x64 catalog, read on Windows 10 and 8.1 x64 (2026-10-22)

The owner's decision of 2026-10-22 on 43.33's refusal (record 13 section
6.14.5): every x64 INF names a catalog of its own, made and signed by
`make-package.ps1` (design record 19 section 3a; branch `p43-36s`).
Development host A, QEMU under TCG, two virtual processors, 2048 MB,
headless, one guest at a time; the host loaded by other agents' guests
and builds throughout. Kit `E:\xhci98-vm\p43-36s` (ports 48800 to 48802),
guest scripts `xfer\auto\go.cmd` and `leg.ps1`.

**Verdict: with the catalogs, Windows 10 22H2 x64 and Windows 8.1 x64
in test-signing mode take every one of the five x64 INFs into the driver
store, after "Windows can't verify the publisher", and `xhci98.sys` and
`xhcicm.sys` run after the restart; the same package with its catalogs
removed is refused with `0xE000022F`, no prompt.**

### The package

The `release` amd64 package of this branch (gated build of `caa79889`,
the drivers' sources as on `3.2.0.0` at `2038250e`), staged by
`make-package.ps1 -Arch amd64` without the key, so the binaries were
unsigned; the five catalogs made by the packager's own
`New-PackageCatalogFile` (MakeCat, the CDF the signed path writes) and
left unsigned (`E:\xhci98-scratch\p43-36s\stage-guest.ps1`). Each read
back by `Read-CatalogFile` with its two members of the right types:
`xhci98.cat` (`xhci98.inf`, `xhci98.sys`), `xhciuas.cat`, `xhcicm.cat`,
`xhcicmf.cat` (`xhcicmf.inf`, `xhcicm.sys`) and `stor98.cat`.

**The project's key was not used**: its password is the owner's and no
agent holds it. In each guest a throwaway code-signing certificate was
made in the guest's own store (`CN=p43-36s guest throwaway (not
trusted)`, SHA-1, 2048 bits: `New-SelfSignedCertificate` on 10, WDK 7.1's
`MakeCert` on 8.1, which has no `-Type`), and the four `.sys` files and
the five `.cat` files were signed with it there (`Set-AuthenticodeSignature`
on 10, WDK 7.1's `SignTool /fd sha1` on 8.1). Test-signing mode accepts a
signature by any certificate, and the project's is trusted nowhere
either, so the reading stands for it by inference; the leg with the
owner-signed package is owed (below). Nothing was signed or installed on
the host.

### The legs (measured)

| | Windows 10 22H2 x64 (19045) | Windows 8.1 x64 (9600) |
|---|---|---|
| Starting point | `p43-33`'s prepared `w10x64` (copied), test-signing on | the pristine image plus the runner task, test-signing on (`bcdedit`: `testsigning Yes`) |
| Control: catalogs removed, `UpdateDriverForPlugAndPlayDevicesW` forced on `PCI\VEN_1B36&DEV_000D` | FALSE, `0xE000022F`, no prompt; "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode" | FALSE, `0xE000022F`, no prompt |
| `pnputil` of the five INFs with their catalogs | each after "Windows can't verify the publisher of this driver software", "Install this driver software anyway": added as `oem2.inf` to `oem6.inf`, each "Installed catalog '<name>.cat' as 'oem<n>.cat'" | the same, `oem1.inf` to `oem5.inf` |
| What the store says of each catalog | "Driver package catalog is valid", then `0x800B0109` (a root not trusted), "Driver package catalog file certificate does not belong to Trusted Root Certificates, but user wants to install anyway", "Signer Score = 0x80000000 (Unsigned)" | the same `0x800B0109` and Signer Score |
| Forced install, `xhci98.inf` on the controller | TRUE, no restart asked | TRUE |
| Forced install, `xhcicm.inf` on the NHI (`PCI\VEN_8086&DEV_15BF`, the emulator's Alpine Ridge) | TRUE | TRUE |
| After the restart | `xhci98` and `xhcicm` running; the controller, root hub and hub as `xHCI98` devnodes, HID mouse and keyboard, both Bulk-Only sticks started; the NHI "xHCI98 Thunderbolt 3 Controller" started | the same |

Not a reading of this package: QEMU's `usb-uas` at Code 10 under
`uaspstor` on both, as under Windows' own driver (record 13 section
6.14.3), so `xhciuas.sys` and `stor98.sys` were imported into the store
and never bound (no device of theirs runs here). `Win32_PnPSignedDriver`
reports the installed drivers `IsSigned False`, the analogue of Vista
and 7's "Not digitally signed" for a signer no root trusts.

Harness notes: on 10 the runner task started at logon by itself, on 8.1
it did not, and the 8.1 leg's first run, from a Run box with
Ctrl+Shift+Enter, which elevates only from 10 on, was not elevated
(every step "Access is denied"; set aside as `w81x64-cat-noelev`); the
second ran from Win+X, A. On both the first click on the publisher
prompt only activates it. The Shift key sent to wake 8.1's display
raised Sticky Keys. The output disk was not formatted on the first 8.1
run (the readings were read off the system disk).

### Owed

- The same legs with the owner-signed package (`make-package.ps1
  -TestSignPfx`, the project key): by inference identical, since the
  store reports only an untrusted root for either signer.
- Windows 8 x64 (43.22 read the refusal there; the catalog leg is not
  run); XP x64, Vista x64 and 7 x64 with a catalog'd package (the INFs
  gained one line; by inference unchanged: an untrusted signer prompts
  as an unsigned package did).
- Stor98's `DmaRemappingCompatible`: no machine here has Kernel DMA
  Protection on; the value is static (Stor98 `docs/PLAN.md`).

## 43.61 the tool readmes held to the cut's text rule on every gated build (2026-10-22)

Branch `p43-61`, from `3.2.0.0` at
`b7e58ca1`.

**Why.** `make-release.ps1`'s `Write-GeneratedText` refuses a generated
plain-text file with a line past 78 columns or a character that is not
printable ASCII (TAB allowed; `Write-AsciiFile` would turn it into `?`).
Nothing met that rule before a cut: 43.11's test cut on 2026-10-22 stopped
on one line in `xhciinst/README.TXT` (left by the 43.34 merge) and one in
`xhcitr/README.TXT` (43.52's), each having passed every gate. The two lines
are rewrapped on branch `readme-wrap`, `e547cd30`.

**What changed.** One rule, read by the cut and by the gated build:

- `scripts/package/package-common.ps1`: `Get-PlainTextFaults` (the rule and
  the cut's own messages, `$script:PlainTextColumns` = 78),
  `$script:ToolReadmeDirs` and `$script:ToolReadmeName` (the four tools
  whose `README.TXT` the cut publishes: `xhcicln`, `xhcimark`, `xhciinst`,
  `xhcitr`), `Read-ToolReadmeLines` (how the cut reads one), and, from Codex
  round 1, `Get-HistoryEmbedLines`, `Format-Wrapped` and
  `ConvertFrom-MarkdownBlocks` (how the cut renders `releases/history.md`
  into the top-level `readme.txt`), moved out of `make-release.ps1`.
- `scripts/package/make-release.ps1`: `Write-GeneratedText` calls
  `Get-PlainTextFaults`; the default tool directories come from the table;
  the four readmes are read with `Read-ToolReadmeLines`; the history with
  `Get-HistoryEmbedLines`. Behaviour and messages unchanged.
- `scripts/check-readme-text.ps1` (new): `-SelfTest` (the rule's refusals,
  the reader, the history rendering, in memory), then the four tracked
  readmes and the rendered history. Exit 1 on any fault.
- `scripts/build-driver.cmd`: `=== tool readme text ===` after the source
  charset gate, self-test first, `:readmetextfail`.
- `docs/contributing/build-and-test.md`: the script's row.

Not covered, by design: the footer the cut appends to each tool readme and
the rest of the top-level readme are `make-release.ps1`'s own template
text, met at a cut.

**Proof.** On `p43-61` at `96c44b47` (from `b7e58ca1`), the gated
`build-driver.cmd release` passed every gate before this one, then stopped
at it:

```
=== tool readme text ===
readme text self-test: OK
FAIL: xhciinst\README.TXT has 1 line(s) past 78 columns, the first being:
installer says whether it could confirm test-signing mode; it never changes it.
FAIL: xhcitr\README.TXT has 1 line(s) past 78 columns, the first being:
closed and closes if the mark service confirms the eject. On Windows 2000 ...
2 fault(s) in the readmes; make-release.ps1 would refuse the cut.

ERROR: a tool readme or the release history the release cut publishes has
...
```

The same two faults from `check-readme-text.ps1` alone (exit 1). With
`e547cd30` cherry-picked onto `96c44b47` (a scratch worktree, `d3451b2d`):
`-SelfTest` OK, then `readme text: OK (4 tool readmes and the release
history, 78 columns, printable ASCII)`, exit 0. The full gated build was not
rerun with it: the gate's success path falls through to the gates that the
failing run had already passed before it.

Batch 6 (`integ6b`, `25e7dd6a`, which carries `e547cd30`): the same rule
over its four readmes and its `releases/history.md`, read in place, finds
0 faults, and `p43-61` merges into `25e7dd6a` without conflict (`git
merge-tree`); batch 6 does not touch the functions moved to
`package-common.ps1`. So `p43-61` merges after or with `readme-wrap`, never
before it, or the gated build fails on the two lines.

Codex: round 1 found the embedded release history unchecked (fixed in
`96c44b47`); round 2 clean.

## 43.60 isochronous transfers and the Logitech Brio 100 (2026-10-22)

Task 43.60 (`roadmap-usb4.md`) has three parts, from the `usb-wdm-free` legs of
2026-10-22 (`usb-wdm-free` `docs/testing/2026-10-22-98se-legs.md`):

1. plugging the Brio 100 freezes Windows 98 SE under `xhci98.sys`, and does
   not with 98 SE's `USBAUDIO.SYS` renamed away;
2. on Windows 2000 under `xhci98.sys`, `uvc98` reads an almost all-empty
   isochronous stream, and SELECT_INTERFACE to alternate 0 at the stop
   fails `C0000004`;
3. `uvc98` at 320x240 on 98 SE: every frame carries the camera's error bit.

Parts 1, 2 and 3 are all decided below. None of them is this driver's, and
nothing in the tree changes.

### 43.60 part 1 - the freeze at plug-in is 98 SE's audio-load wedge (2026-10-22)

Runtime, development host A, QEMU, one vCPU, on a loaded host (13 guests
and two gated builds, CPU about 97 %; nothing here is a timing reading).

**Verdict: not this driver's.** The Brio 100's freeze is the Windows 98 SE
audio-load wedge that 36.41 measured (`runs/run-36.md`, "36.41") and the
release notes already carry ("Known limitations", "Windows 98 SE: a USB
audio device can wedge the machine as it loads"). It reproduced with
QEMU's emulated Full-Speed `usb-audio` on 98 SE's own UHCI stack, with
`xhci98.sys` not loaded at all, with the same Close Program list
("Msgsrv32 [Not responding]") and the same VMM addresses. In every wedge
read here, no isochronous transfer had been given to the driver.

#### The vehicle

- **QEMU**: `usb-wdm-free`'s pinned `qemu-system-orphan.exe` (11.1.0 with
  the ISO tracing patches, `E:\usb-wdm-free\host-qemu\run-orphan\`), TCG,
  `-machine pc,smm=off -cpu pentium3 -smp 1 -m 256`, `-display none`,
  `-audiodev none`. The Brio 100 (`046D:094C`) passed through with
  `usb-host` exactly as `usb-wdm-free`'s xhci-legs rig does.
- **Guest**: Windows 98 SE under NUSB 3.3 (`vm\t26\win98-gold.qcow2`,
  read-only, overlays only), its `AUTOEXEC.BAT` copying the transfer
  drive's `XHCI98.SYS` over the installed driver at each boot.
- **Driver**: the `qemu` x86 flavour of the `isoch-ring` branch (43.38 to
  43.40's ring; the tree of `ed1b4eca`), 367,256 bytes, SHA-256
  `480d5b749412eabb5b0103a8bf6adfecbadbff66e71ccfbdc69253b10294aba9`.
- **Three bases**, each installed once from a fresh overlay and then kept
  read-only:
  - `x2`: `qemu-xhci` (`p2=4,p3=4`), the Brio 100 installed, its
    microphone function on 98 SE's own `usbaudio.sys`;
  - `qp`, over `x2`: QEMU's `usb-audio` at `xhci.0` port 2 installed
    ("USB Audio Device", 98 SE's own driver);
  - `up`, over `qp`: no `qemu-xhci`; a `piix3-usb-uhci` driven by
    Windows 98 SE's own `uhcd.sys`, the `usb-audio` at `uhci.0` port 1
    installed (`xhci98.sys` is on the disk but has no controller to load
    on).
- **One repetition** (kit `E:\xhci98-vm\p43-60\rep.sh`): a fresh overlay
  of the base, a cold boot, 110 s to the desktop, the device hot-added from
  the monitor, then read at attach plus 60, 130 and 200 s: a screenshot,
  Ctrl+Esc, a screenshot, Esc. **A wedge** is the Start menu not opening at
  any read (the two screenshots of each read byte-identical in size, the
  frame unchanged through 200 s). Then eight `info registers` samples, and
  Ctrl+Alt+Del for the Close Program list.

#### The readings

| Device | Bus | Wedged | Repetitions |
|---|---|---|---|
| Brio 100 (`usb-host`, High Speed) | `xhci98.sys` on `qemu-xhci` | 3 of 5 | `xr1`, `xr3`, `xr4` |
| QEMU `usb-audio` (Full Speed, UAC 1.0) | `xhci98.sys` on `qemu-xhci` | 1 of 5 | `qr2` |
| QEMU `usb-audio` (Full Speed, UAC 1.0) | 98 SE's own `uhcd.sys`, no `xhci98.sys` | 2 of 5 | `ur3`, `ur4` |

A sixth UHCI repetition was not run (the session stopped at 20:57).
The counts are small; with 36.41's (UHCI 14 of 30, `xhci98.sys` 27 of 50)
they say the rate on 98 SE's own stack is not lower than under this driver.

- **The same state on all three rows.** Every wedge's Close Program list
  reads Explorer, Systray, "Msgsrv32 [Not responding]" (`shots\xr1-cad.png`,
  `qr2-cad.png`, `ur3-cad.png`); every live one reads Explorer and Systray
  only (`qr1-cad.png`). The register samples of every wedge are in the
  VMM (`C000A881`, `C000A882`, `C00036xx`, `C005340E`, `C0051551`), the
  addresses 36.41 and 36.44 read, on the UHCI control as well; live idle
  guests sample mostly `FF0270ED`.
- **The driver's view, wedged against live** (`ev\xr1-debug.txt` against
  `ev\xr2-debug.txt`, the `qemu` flavour's debug port): the same sequence
  in both - the microphone function's SELECT_INTERFACE to interface 3
  alternate 0 (endpoint mask 0), its first URB, QUERY_CAPABILITIES
  answered, transfers submitted 6 and completed 6, `iso packets answered`
  and `iso pages built` 0. The live run goes on to QUERY_DEVICE_RELATIONS;
  the wedged run asks the driver nothing more. That is where 36.41's
  wedge stopped too (`runs/run-36.md`).
- **No isochronous transfer before the wedge.** The microphone's streaming
  interface is at alternate 0 and no isochronous packet was ever on a ring
  when the guest stopped asking. Whatever the wedge is, it is not in how
  this driver schedules isochronous transfers.

#### The NUSB comparison: not makeable for the microphone

Under NUSB 3.3's EHCI (an overlay of a copy of `usb-wdm-free`'s
`net9x\base\w98-nusb`, `-cpu pentium3,-apic`; `n98a`, and the uac2 agent's
reading): no freeze, but not like for like. NUSB does not split the
Brio's composite, which uses interface association descriptors (class
`EF`); there is one devnode, `USB\VID_046D&PID_094C\...`, with the
compatible ID `CLASS_0E`, the microphone never gets a devnode, and
`usbaudio.sys` never loads. NUSB cannot decide this question.

#### The Brio 100's microphone

From `uvc98`'s captured configuration descriptor
(`usb-wdm-free` `src\video\uvc98\testdata\brio100\CONFIG.BIN`): the
microphone is a second association (interfaces 2 and 3, class `01`), its
streaming interface 3 with alternates 1 to 3, isochronous IN endpoint
`0x86`, 36, 68 and 100 bytes, one transaction, `bInterval` 4. At High
Speed that is one packet every eight microframes: one per millisecond, the
same rate as a Full-Speed device's one packet per frame.

#### What the owner asked: a UAC 1.0 route

The owner asked whether this needs our own UAC 1.0 driver, and named three
routes, none to be built yet. What the readings say about each:

1. **A compatibility shim in `xhci98.sys`**, mapping `usbaudio.sys`'s
   Full-Speed 1 ms requests onto a High Speed endpoint's microframes or
   `bInterval`. **Not supported by the evidence.** The wedge comes before
   any isochronous request reaches the driver, it happens on 98 SE's own
   UHCI stack with a Full-Speed device, and the Brio's microphone needs no
   mapping anyway (`bInterval` 4 at High Speed is already one packet a
   millisecond). Rough size: a few hundred lines in the 98 SE isochronous
   submit path and its host vectors. Risk: high. It would change what a
   client's request means in an engine every system shares, and it would
   not touch the freeze.
2. **UAC 1.0 inside `usb-wdm-free`'s `uac2`**, so 98 SE's `usbaudio.sys`
   never loads. **Not supported yet.** The wedge's wait is in MSGSRV32,
   the process that loads the multimedia drivers for a newly arrived sound
   device (36.41's reading; that this is what MSGSRV32 does is not read
   here). A PortCls driver arrives through the same path, so a new class
   driver might wedge in the same place. No reading says it would not:
   `uac2` on 98 SE has one start, on the X4 simulator, with no count of
   plug repetitions. `uac2` on 98 SE does not yet expose a wave device
   either (`waveOut devices=0`, open in `usb-wdm-free`), so this route
   would rest on unfinished ground. Rough size: large. It needs the UAC 1.0
   descriptor model (class-specific AC header, terminals and feature units
   as 1.0 lays them out, Type I formats with discrete rate tables), the
   sampling-frequency control on the endpoint, Full-Speed packet sizing at
   44.1 kHz, 1.0's synch endpoints, and an INF that outranks
   `WDMA_USB.INF`; then the 98 SE legs. Risk: high, for an unproven gain.
3. **A known issue.** **Supported, and already published**: the release
   notes' "Known limitations" entry above covers it ("a USB audio device
   can wedge the machine as it loads ... not this driver's doing", with
   the UHCI control). The Brio's microphone is one more audio device of
   that entry. Rough size: one sentence naming a camera's microphone as an
   audio device, in the release notes and `releases/history.md` at
   43.final. Risk: low. The cost is that the Brio's microphone stays
   unreliable to plug on 98 SE in a virtual machine. Renaming
   `USBAUDIO.SYS`, `uvc98`'s workaround, gives up every USB audio device.

**Recommendation: route 3, with one reading still owed: the Brio 100 on
real hardware under 98 SE.** Every count of this wedge, in 36.41, 36.44
and here, was taken under QEMU TCG. On the E460 bench a Full-Speed audio
device played under 98 SE's own driver at a root port and behind a hub,
and nothing records a wedge there (`runs/run-13e.md`, Finding X; that
session did not count plug repetitions). If the Brio wedges on metal too,
the next step before route 2 is to show that a PortCls driver from
`usb-wdm-free` arrives on 98 SE without MSGSRV32 stopping, over counted
repetitions. Route 1 is ruled out by these readings whatever the metal
shows.

**The owner's decision (2026-10-22): route 3.** The freeze stays a known
issue. At 43.final, one sentence in the release notes' entry names a
camera's microphone among the affected audio devices. The owner will try
the Brio 100 under 98 SE on real hardware. UAC 1.0 support in `uac2` is
not started, and no shim is built in `xhci98.sys`.

#### Evidence

Kit `E:\xhci98-vm\p43-60\` (not tracked): `rep.sh`, `launch.ps1`,
`pids.txt`; `ev\<rep>-rep.txt` (reads, EIP samples, `info usb`),
`ev\<rep>-debug.txt` (the driver's debug port), `ev\<rep>-qtrace.log`;
`shots\<rep>-a<t>*.png` and `shots\<rep>-cad.png`. Bases `ovl\x2.qcow2`,
`ovl\qp.qcow2`, `ovl\up.qcow2` (read-only).

### 43.60 parts 2 and 3 - the empty stream, `C0000004` and the 320x240 error bit (2026-10-22)

Runtime, development host A, QEMU, one vCPU, on a loaded host (as part 1).

**Verdict, part 2: not this driver's.** The almost all-empty stream on
Windows 2000 comes from the QEMU build. The `usb-wdm-free` leg ran on the
stock Scoop QEMU. On `usb-wdm-free`'s patched QEMU (the ISO accounting
patches, `run-orphan`), the same driver on the same guest captured three
complete frames at 160x120, twice. Back on the stock QEMU, the same
guest reproduced the reported failure exactly. `C0000004` is
`USBD_STATUS_STALL_PID`, not an NTSTATUS. `uvc98`'s `UVU_SELECT_USBD`
prints the URB's USBD status (`src\video\uvc98\uvcusb.c`, `SelectAlt`).
It is QEMU `usb-host`'s answer when the host's
`libusb_set_interface_alt_setting` fails. Each such failure printed
`libusb_set_interface_alt_setting: -1 [IO]` on QEMU's stderr, one for one
with `C0000004`: two in the `usb-wdm-free` leg's two runs, and here once
in each run that logged it, never in a run that did not. The driver
passed the stall up faithfully.

**Verdict, part 3: not this driver's, and not 98 SE's.** 320x240 fails
the same way on Windows 2000 as on 98 SE under this driver, and the
Endpoint Context is right. The limit is how fast QEMU's emulated xHCI
consumes High Speed isochronous TDs: about 2.2 to 3.1 a millisecond
against the camera's 8. QEMU's `usb-host` resubmits a host transfer only
once the guest has consumed it, so the host's stream to the camera stops
and restarts about 60 times a second. The camera flags the payload after
each gap with its error bit. At 320x240 every frame has a gap in it; at
160x120 a few frames fit between gaps.

#### The vehicle

- **QEMU**: `usb-wdm-free`'s patched `qemu-system-orphan.exe` (as part 1),
  and for run `k3` the stock Scoop QEMU 11.1.0 (`qemu-system-i386.exe`),
  which is what the `usb-wdm-free` 2000 leg ran. `qemu-xhci,p2=8,p3=0` as
  in that leg. The Brio 100 passed through with `usb-host`. Trace events
  in kit `cam.events`: the `usb_host_iso_*` events of the patched build,
  `usb_host_set_interface`, and the xHCI endpoint, slot and
  `usb_xhci_queue_event` events.
- **Driver**: the same `qemu` x86 flavour as part 1 (SHA-256
  `480d5b74...aba9`), on both guests.
- **Guests**: copies of `usb-wdm-free`'s own leg overlays, where `uvc98`
  was installed and had run (`E:\usb-wdm-free\xhci-legs\m2k1.qcow2` and
  `m98a.qcow2`, copied into the kit, never written). In each, this build
  replaced the installed 3.1.0.0 `xhci98.sys`: on 2000 by `SWQ.BAT`
  (`fc /b` clean), on 98 SE by a copy (367,256 bytes read back). Then the
  guest was shut down cleanly and kept read-only (`ovl\s2k.qcow2`,
  `ovl\s98.qcow2`). 98 SE's `USBAUDIO.SYS` stays renamed in `m98a`, as
  `usb-wdm-free` left it (part 1's wedge). Part 1's `x2` base turned out
  never to have loaded `uvcm2.sys` (no `uvc98` line in any of its debug
  logs; M1CAP: "exact synthetic filter absent", run `e1`), so it was not
  used.
- **One run** (`cam.sh`): a fresh overlay, a cold boot, the Brio
  hot-added, then `M1CAP <hardware ID> <size>`, the report written to a
  floppy (`fd\<run>\M1CAP.TXT`). With the guest stopped about 45 s into the
  capture, the Brio's device context was read from guest memory
  (`ctx.sh`: DCBAA from the trace's register writes, then HMP `xp`). The
  rate below is from `rate.py`: Transfer Events for the video endpoint
  (DCI 3) against the host's `usb_host_iso_callback` real-time
  timestamps.

#### The readings

| Run | Guest | QEMU | Size | M1CAP | Guest TDs per ms of host time | Host stream restarts | Alternate-0 select at stop |
|---|---|---|---|---|---|---|---|
| `k1` | 2000 | patched | 160x120 | **PASS**, 3 frames | 2.36 | 75 in 1.39 s | `C0000004`, one `libusb ... -1 [IO]` |
| `k4` | 2000 | patched | 160x120 | **PASS**, 3 frames | 2.22 | 73 in 1.65 s | 0, no libusb error |
| `k3` | 2000 | stock | 160x120 | FAIL, error 1784 | not traced (stock has no ISO events) | - | `C0000004`, one `libusb ... -1 [IO]` |
| `k2` | 2000 | patched | 320x240 | FAIL, error 1784 | 2.90 | 289 in 5.09 s | `C0000004`, one `libusb ... -1 [IO]` |
| `e2` | 98 SE | patched | 320x240 | FAIL, error 1784 | 3.07 | 333 in 6.14 s | 0, no libusb error |

- **`k3` is the reported failure.** 9,528 packets, 9,317 of them empty
  (`USB_PACKETS 2538`, `USB_EMPTY 2465`, hex), against the `usb-wdm-free`
  leg's 9,672 and 9,464. 11,727 of 11,981 TDs completed short. Same
  guest and driver as `k1` and `k4`; only the QEMU build differs. The
  `usb-wdm-free` EHCI record (`docs/testing/2026-10-22-uvc-m2.md`, "Stock
  vs patched QEMU") found the same on Windows' own EHCI: no frames on the
  stock build, frames on the patched one.
- **The Endpoint Context is right**, read from the controller's copy while
  streaming (DCI 3, `ev\k1-cam.txt`, `k2-cam.txt`, `e2-cam.txt`). EP Type
  5 (Isoch IN), Interval 0 (every microframe: `bInterval` 1 at High Speed,
  "2^(bInterval-1)" microframes, 6.2.3.6), Max Burst 0 and Mult 0 (one
  transaction, Table 6-8), and Max Packet Size and Max ESIT Payload 192 at
  alternate 1 and 640 at alternate 4 (`0x00C00028`/`0x00C00C00`,
  `0x02800028`/`0x02800C00`). 98 SE and 2000 program the same values.
  `usb-wdm-free`'s 98 SE record read alternate 4 for 320x240, not a
  high-bandwidth alternate, and so do these runs (`USB_ALT 4`), so
  Mult/Max Burst/ESIT for a 3x1020 endpoint are not in play.
- **The ring was not the limit.** Every TD completed as Success or Short
  Packet, except 5 to 10 Ring Overrun events per run (the ring empty when
  the controller came to it) and the stop's one Stopped. No Missed Service
  and no Transaction Error. `uvc98` counted 2 to 4 resubmission gaps in its
  own frame numbering (`USB_GAPS`). TDs were waiting on the ring; the
  controller took them at about 3 a millisecond.
- **Why the controller takes them that slowly** (QEMU's source, `qemu-11.1.0\hw\usb\hcd-xhci.c`,
  `xhci_calc_iso_kick` and `xhci_check_intr_iso_kick`, read not run).
  On a controller with CFC 0 (QEMU's), this driver sends every TD after a
  request's first with SIA, as the spec asks (the TRBs themselves were not
  traced here). QEMU schedules each such TD at the previous TD's
  microframe plus one interval and arms a timer for it. So it services one
  TD per timer expiry, never more than one per microframe, and never
  catches up after a late timer. The rate is about the same at 192 and at
  640 bytes (2.2 to 2.4 against 2.9 to 3.1), which fits a per-TD pace
  rather than a byte limit. Whether the pace comes from the host's timer
  resolution or from TCG's speed was not separated.
- **Why that becomes the error bit** (the patched `host-libusb.c`,
  `usb_host_iso_data_in` and the callback, read not run). `usb-host` takes
  32-packet host transfers, and puts one back on the bus only after the
  guest has copied all of it out. With the guest at about 3 packets a
  millisecond, every host transfer completes before the guest frees the
  next. The ring drains (`usb_host_iso_stop` is traced when nothing is in
  flight) and restarts: 289 times in 5.09 s in `k2`. The camera's payload
  headers carry the error bit after those gaps (`USB_ERRPKTS` 972 in `k2`,
  954 in `e2`, hex `3CC` and `3BA`), and every 320x240 frame was dropped
  (`USB_DROPPED` 108 and 111, `USB_FRAMES` 0). At 160x120 there were
  error-bit packets too (203 and 261) and 11 to 13 dropped frames, but
  three frames came through whole.
- **The EHCI pass, for comparison** (`usb-wdm-free`'s
  `E:\usb-wdm-free\wave2\w2km2-iso.log`, the 2000 EHCI 320x240 run on the
  patched QEMU): 504 host callbacks over 3.25 s with 21 restarts, against
  289 in 5.09 s here. QEMU's EHCI model works frame by frame, eight
  microframes at a time, and its host stream gaps are rare enough for
  frames to come through. That is the vehicle difference between the
  EHCI pass and the xHCI failure.

#### What is owed

- A High Speed isochronous camera stream on real xHCI hardware under
  98 SE and 2000. It is already in the release notes' "Untested ground"
  (43.40: "a High Speed isochronous stream, a camera"). No VM leg on
  `qemu-xhci` can stand in for it above the rate read here.
- For `usb-wdm-free` (not this repository's work): `uvc98` legs above
  160x120 belong on QEMU's EHCI, or on a QEMU whose xHCI model catches up
  on late SIA TDs; and `uvc98`'s alternate-0 select on a `usb-host`
  passthrough can fail through the host's libusb, which its stop path
  already survives (`UVU_ALT0 C0000001`, then unplug clean).
- No change to this driver. A driver workaround for QEMU's pace was
  considered and rejected: explicit Frame IDs on every TD, which QEMU would
  service in batches of eight. The spec asks a controller with CFC 0 for
  SIA on every TD after the first (`xhci.h`, the Frame ID policy of 36.57
  and 38.11), and the change would serve the vehicle, not a real
  controller.

#### Evidence

Kit `E:\xhci98-vm\p43-60\` (not tracked): `cam.sh`, `ctx.sh`, `rate.py`,
`cam.events`; `ev\<run>-cam.txt` (timeline and the device context),
`ev\<run>-debug.txt` (the driver's and `uvc98`'s debug port),
`ev\<run>-qtrace.log`, `ev\<run>.err` (QEMU stderr, the libusb errors);
`fd\<run>\M1CAP.TXT` and the frames; `shots\<run>-*.png`. Bases
`ovl\s2k.qcow2` and `ovl\s98.qcow2` (read-only) over the copies
`ovl\m2kcopy.qcow2` and `ovl\m98copy.qcow2`.

## 43.62 "Unplug or Eject Hardware" on 98 SE and ME (2026-10-22)

Roadmap-usb4 task 43.62, an investigation (owner, 2026-10-22): does the
"Unplug or Eject Hardware" tray icon and dialog of Windows 98 SE and ME list,
and stop, a USB stick under the bundled USBSTOR, a UAS disk, and an NVMe drive
behind a USB4 or Thunderbolt tunnel under Stor98, with `xhci98.sys`? Nothing
in the tree changes here; the owner decides on any change (the end of this
sheet).

### Verdict

| System, applet | Device | Icon | Listed | Stop, idle | Stop, a file open |
|---|---|---|---|---|---|
| 98 SE as shipped (no `HOTPLUG.DLL`, `SYSTRAY.EXE` 4.10.2222) | any | no | - (there is no dialog) | - | - |
| 98 SE, NUSB 3.3's 4.10.2224 (KB 242975), throwaway overlay | USB stick (USBSTOR) | yes | yes, "USB Disk" | "Safe To Remove Hardware", then unplugged: no dialog | refused: "Problem Stopping Device ... cannot be stopped right now"; the stick kept working |
| the same | UAS disk | yes | yes, "USB Disk" | "Safe To Remove Hardware", then unplugged: no dialog | not read (the stick's reading is the same code path) |
| the same | NVMe behind a TB3 tunnel, Stor98 | no | no, also with "Display device components" | - | - |
| ME (its own `HOTPLUG.DLL`, `SYSTRAY.EXE` 4.90.3000) | USB stick | yes | yes, "USB Disk" | "Safe To Remove Hardware", then unplugged: no dialog, the icon gone | refused, the same message; the stick kept working |
| the same | UAS disk | yes | yes, "USB Disk" | "Safe To Remove Hardware", then unplugged: no dialog | not read |
| the same | NVMe behind a USB4 tunnel, Stor98 | no | no, also with "Display device components" | - | - |

An unplug without a stop: ME showed "Unsafe Removal of Device"; 98 SE with
2224 showed no dialog in the 12 s read. A re-plug after either was listed
again.

**Why 98 SE as shipped has no icon.** 98 SE's CD and its installed image
carry no `HOTPLUG.DLL` (7-Zip listings of every CAB of `Win98SE.iso` and of
the pristine image); its `SYSTRAY.EXE` 4.10.2222 holds only the PC Card
strings ("Stop %1", `mspcic.dll,EjectSocket`) and no `hotplug.dll` or
`HotPlugEjectDevice` string. The applet came to 98 SE with hotfix KB 242975
(`HOTPLUG.DLL` and `SYSTRAY.EXE` 4.10.2224, both in NUSB 3.3; NUSB's
`_NUSB.INF` copies them and lists them under 242975). The owner's "98 SE's own
are 4.10.2222" holds for `SYSTRAY.EXE` alone. So with this release, which
copies only NUSB's seven storage files, a 98 SE user has no "Unplug or Eject
Hardware" for any device; XHCITR's Safely Remove and `INSTALL /eject` are the
route there, as the release notes say.

**Why the NVMe is not listed (static, from disassembly; Codex below).** On 9x
the applet's listing does not come from the device's capabilities. Both 9x
`HOTPLUG.DLL`s (2224 and ME's) offer a devnode only when a predicate holds
(2224 `0x411AD2`, ME `0x7BE91CBC`): its problem is not 0x18, and either a
present device interface of class `{40EF6CF0-2DB6-11D3-9F78-00C04F6D5135}`
belongs to the devnode's parent (`SetupDiGetClassDevs(..., DIGCF_PRESENT |
DIGCF_DEVICEINTERFACE)`, `SetupDiEnumDeviceInterfaces`, then `CM_Get_Parent`
of the candidate compared with the interface's `DevInst`), or the devnode is
in the table `\\.\PCCARD` returns (PC Card sockets). The tree walk (ME near
`0x7BE930F6`) shows no node for which the predicate is false while its
filtering flag is set (who sets it is not read). That interface
is registered by NUSB's (and ME's) `USBNTMAP.SYS` 4.90.3000 on the PDO it
filters (`0x10452`: `IoRegisterDeviceInterface` with that GUID, then
`IoSetDeviceInterfaceState` TRUE; FALSE on the other path). `USBNTMAP.SYS` is
the upper filter of the USB mass-storage device's devnode (NUSB's and ME's
`usbstor.inf`) and of the UAS device's (`src/uas/xhciuas.inf`,
`[Uas.HW.AddReg]`), so the child unit, "USB Disk", is offered: the stick and
the UAS disk are listed through NUSB's own filter, not through anything
`xhci98.sys` answers. `xhci98.sys`'s PDO answers `Removable` TRUE and
`SurpriseRemovalOK` FALSE (`src/hcd_pdo.c`), and on 98 SE and ME `xhcicm.sys`
adds `DN_REMOVABLE` to each new devnode below a tunnel (`src/cm/cm_cfgwk.c`;
read set in `runs/run-39.md`); neither is read by the 9x predicate, which is
why the NVMe is not listed though `DN_REMOVABLE` is set. Stor98 on 98 SE and
ME is an IOS SCSI miniport (`stor98.mpd`, `DevLoader *IOS`,
`src/stor98/inf/stor98-x86.inf`) on a devnode 9x's PCI bus enumerates: no
WDM device object of this repository sits on the NVMe controller's devnode
or on the tunnel's bridges, so nothing here can call
`IoRegisterDeviceInterface` for them. Nothing in the removability code
changed since `v3.1.0.0` (`git diff v3.1.0.0 b7e58ca1` over
`src/hcd_pdo.c`, `src/uas/`, `src/cm/cm_filt.c`: no `Removable`,
`SurpriseRemoval` or `caps->` line), so the 3.1.0.0-based tunnel images below
read the same as 3.2.0.0.

### The vehicle

- Development host A, QEMU under TCG, headless, 1 vCPU and 256 MB per 9x
  guest, one guest at a time; the host loaded throughout (other agents'
  guests and gated builds). No timing is read here.
- Kit `E:\xhci98-vm\p43-62` (QMP 49140 98 SE, 49142 ME; `u4\` 49144 98 SE,
  49146 ME), scripts adapted from `p43-14` and `p43-18g3`. Every disk an
  overlay; no base written.
- USB legs: `qemu-xhci` (p2=4, p3=4), `usb-storage` and `usb-uas`
  over `usbfat128.qcow2` overlays. 98 SE: `p43-5\base\w98-dp.qcow2`
  (pristine 98 SE with the 43.4 cut of `3.2.0.0` installed, its USB storage
  row ticked, i.e. NUSB 3.3's seven `W98SE` files), overlay `w98\a.qcow2`
  for the stock reading, then `w98\b24.qcow2` over it with 2224's two files
  copied in from `D:\N24` (extracted by 7-Zip from `tools\nusb33e.exe`;
  `SYSTRAY.EXE` replaced through `WININIT.INI`) and a cold restart. ME:
  `p43-14\base\me-hcd310.qcow2` with `xhci98.sys` of the `3.2.0.0` package
  `e61538ae` staged over it (`WININIT.INI`), the UAS INF of that package
  given to the first-plug wizard.
- Tunnel legs: the `usb4-emu` emulator of `p43-18g3` (sha256 `9dfdcf9b...`),
  NVMe `nvme-fat.qcow2` overlay in an enclosure hot-plugged after the
  desktop: 98 SE on `p43-18g3\w98\inst.qcow2` (the Alpine Ridge ICM
  topology; `3.1.0.0` with Thunderbolt and Stor98, a `p43-18` `xhcicm.sys`;
  NUSB 3.3 installed whole, so `HOTPLUG.DLL` 71,680 bytes and `SYSTRAY.EXE`
  31,232 bytes, the 2224 files, already there), overlay `u4\w98\b24.qcow2`;
  ME on `p43-18g3\me\g3.qcow2` (`usb4-generic`, the ASM2464 model),
  overlay `u4\me\m1.qcow2`. Both mounted the drive (`ORICO (E:)`) within a
  minute of the plug.
- The applet was opened with `rundll32 shell32.dll,Control_RunDLL
  hotplug.dll`; a file was held open with `type con > F:\HOLD.TXT` in an
  MS-DOS Prompt.
- The ME USB leg was retaken on a fresh overlay (`me\b.qcow2`, stick
  `me\stick2.qcow2`) after the first session's guest was stopped mid-leg by
  a host interruption; the first session's ME UAS readings stand (shots
  `mea-*`).

Screenshots: `E:\xhci98-vm\p43-62\shots\` (98 SE stock `w98a-*`, 98 SE 2224
`w98b-*`, ME `mea-*` and `meb-*`) and `E:\xhci98-vm\p43-62\u4\shots\`
(`uw-*` 98 SE, `um-*` ME). The ones each row rests on:

| Reading | Shots |
|---|---|
| 98 SE stock: stick and UAS mounted, no icon; the applet command opens nothing | `w98a-after-uas`, `w98a-run-typed`, `w98a-run-hotplug` |
| 98 SE 2224: icon on plug, both listed | `w98b-boot-noplug`, `w98b-stick`, `w98b-applet-both` |
| 98 SE 2224: UAS stop, unplug | `w98b-uas-stop1` to `-stop3`, `w98b-uas-unplugged` |
| 98 SE 2224: stick with a file open | `w98b-hold`, `w98b-hold-stop1`, `-stop2`, `w98b-hold-after` |
| 98 SE 2224: stick stop, unplug, re-plug, unplug unstopped | `w98b-st-stop2`, `w98b-st-unplugged`, `w98b-replug`, `w98b-surprise` |
| ME: icon, stick listed; UAS stop | `meb-stick`, `mea-applet-stick`, `mea-uas-stop1`, `mea-uas-stop2`, `mea-uas-unplugged` |
| ME: stick with a file open | `meb-hold`, `meb-hold-stop1`, `meb-hold-stop2`, `meb-hold-after` |
| ME: stick stop, unplug, re-plug, unplug unstopped | `meb-st-stop2`, `meb-st-unplugged`, `meb-replug`, `meb-surprise` |
| tunnel NVMe, 98 SE 2224 and ME: mounted, not listed | `uw-files`, `uw-plug`, `uw-applet`, `uw-applet-comp`; `um-plug2`, `um-applet`, `um-applet-comp` |

The binaries read (outside the tree, `E:\xhci98-scratch\p43-62`, nothing
executed; capstone listings `me-hotplug-lin.txt`, `hotplug2224-lin.txt`,
`usbntmap-lin.txt`):

| File | Version | Size | SHA-256 |
|---|---|---|---|
| 98 SE `SYSTRAY.EXE` | 4.10.0.2222 | 32,768 | `5f475b7100287a5a497303935d86a73927ee177970c7d15622bcb0eb6fbc9088` |
| NUSB 3.3 `HOTPLUG.DLL` | 4.10.0.2224 | 71,680 | `3b655411b48f5a33ff0f64658cb361244bd8d753a201e702dcd1fb4f60e7d4af` |
| NUSB 3.3 `SYSTRAY.EXE` | 4.10.0.2224 | 31,232 | `45c94ee53ad6697f328b7d57fb57c24a20d6b12c873220e62905b8ef79c8357d` |
| ME `HOTPLUG.DLL` | no version resource | 81,920 | `3ecc9094b27faf6aee2b9809ada8c1b5b21822cec2b7b86925a55aa559cd6b5d` |
| ME `SYSTRAY.EXE` | 4.90.0.3000 | 36,864 | `6aa2f6eb85bcc5e9846dc64ecff90006d7cd30bffa57b9bc2f3083ced9250694` |
| NUSB 3.3 `USBNTMAP.SYS` | 4.90.0.3000 | 7,136 | `d1595cdbfdc22925c4e47d1720f117fe6c9beda36417ba41a1fd7398878cc6e4` |

### Options for the owner (none taken)

1. **No change (recommended).** The stick and the UAS disk are already
   listed and stop cleanly wherever the applet exists (ME; 98 SE with
   2224), refusing correctly with a file open. On 98 SE as this release
   installs it there is no applet, and on both systems a tunnelled NVMe is
   never listed; XHCITR's Safely Remove and `INSTALL /eject` cover both, and
   only they take the tunnel down (`xhcicm.sys`'s eject, `src/cm/cm_eject.c`).
2. **Ship 2224's `HOTPLUG.DLL` and `SYSTRAY.EXE` on 98 SE** (the NUSB 3.3
   copy). Small in the installer (two more files in the `NUSB33\W98SE`
   component and `SYSTRAY.EXE` replaced in use, through `WININIT.INI`), but
   they are Microsoft hotfix files that replace a system file: a
   redistribution question first (43.1's decision,
   `legal-provenance.md` section 5, names the seven storage files only), and an uninstall must put
   2222 back. It gives USB storage the icon; it does nothing for the
   tunnel.
3. **List the tunnelled NVMe.** It would need a device interface of class
   `40EF6CF0` on the parent of the node to be offered - the NVMe
   controller's devnode for its disk, or the tunnel's downstream bridge for
   the controller. No component of this repository owns a WDM device object
   on either devnode on 9x (Stor98 is an IOS miniport, the bridges are 9x's
   PCI bus's), so it means a new ring-0 WDM component on that devnode, or
   writing the interface's `DeviceClasses` registry entries by hand, an
   undocumented 9x path. Large, with a high risk, and even then the
   applet's Stop is `CM_Query_Remove_SubTree` / `CM_Remove_SubTree` of that
   node: it would not take the tunnel down, unlike `INSTALL /eject`, and a
   Stop of the disk node alone would not establish the DMA isolation
   Stor98's rules require before a pull. Not recommended.

### Codex

Round 1 (`task-mvjltrl7-3lf32i`, session `01a163e6`, read only, against the
three disassembly listings and the tree): every claim confirmed, with these
qualifications, taken into the text above where they change it.

- The predicate rejects a devnode only when the status query succeeds and
  reports problem 0x18; a failed query goes on to the interface test. The
  tree walk's skip applies only while its filtering flag is set (ME `+0x45`,
  2224 `+0x41`; who sets it is not in the listing) and is bypassed for a
  node below a listed one with "Display device components" ticked;
  children are walked either way.
- `USBNTMAP.SYS` as the stick's upper filter: confirmed from NUSB 3.3's
  `USBSTOR.INF` (line 349, `HKR,,upperfilters,0,"USBNTMAP.SYS"`), read after
  the round.
- Stop: ME calls `CM_Query_Remove_SubTree` and `CM_Remove_SubTree` on the
  selected node (`0x7BE93656` to `0x7BE936D1`), also querying its parent
  for some enumerators; 2224 queries the node and its parent
  (`0x41347F` to `0x4134BB`). Neither reaches `xhcicm.sys`'s eject.
- Option 3 is weaker than stated: a Stop of the NVMe's disk node alone need
  not stop the controller, drain its requests or establish DMA isolation,
  which Stor98's ground rules require before buffers are released
  (`src/stor98/AGENTS.md`); so even a listed NVMe could not be called safe
  to pull from that Stop. Hand-written `DeviceClasses` entries are not
  decidable from these sources (registration contract and lifetime on 9x
  unread).
- The only device-interface registrations in `src/` are `hcd_door.c`'s
  (USB, controller and hub objects) and `cm_qryos.c`'s own class on the NHI
  PDO, never enabled; none covers a bridge or an NVMe function.
- Docs: nothing claims 98 SE's own applet lists devices (`README.md` says
  "No such dialog"; the release notes give the applet to NUSB), but the
  release notes' "lists nothing, as on 98 SE" (the ME tunnel item) hides
  that stock 98 SE has no applet, and the `XhciCmSafeEject` row ("offers
  devices behind a tunnel for safe removal") reads as if 98 SE's and ME's
  applet offered them. A wording question for the owner; not changed here.

### The owner's decision and the NUSB 3.3 leg (2026-10-22, evening)

Owner, 2026-10-22: keep carrying NUSB's seven storage files and tell 98 SE
users that the "Unplug or Eject Hardware" icon comes from installing NUSB
3.3 itself; the release notes' 9x removal wording qualified as proposed
(`123e443e`), and `README.md`, the release notes' storage section and
`tuning.md` saying so (the commit after this sheet's). Asked the same day:
SweetLow's package (`tools\usb20_win9x.zip`, every one of its 27 entries)
carries no `HOTPLUG.DLL`, `SYSTRAY.EXE`, `USBNTMAP.SYS` or storage INF and
names no install order, so it brings neither the applet nor the interface.

**The leg: PASS.** 98 SE, one guest, from the pristine image
(`p43-14\base\w98se-pristine.qcow2`, overlay `w98\n1.qcow2`):

1. NUSB 3.3 by its own setup: `NUSB33E.EXE` (`tools\nusb33e.exe`, SHA-256
   `F1F30A800CF6013EB6F18DB775D88E777DDC3924622E6560EDD2DCD7B53698D3`)
   run from the share, its
   licence accepted, its restart taken. Afterwards `HOTPLUG.DLL` 71,680
   bytes, `SYSTRAY.EXE` 31,232 bytes (the 2224 pair) and `USBNTMAP.SYS`
   7,136 bytes were in place (`n-files`).
2. The xHCI98 installer, `SETUP.EXE`, from a test package assembled for the
   leg: `SETUP.EXE`, `XHCIINST.EXE` and `XHCIWIZ.EXE` built at `b7e58ca1`
   (`xhciinst\build.cmd x86`; they call themselves 3.1.0.0, the version
   not being bumped before the cut), `INSTALL.BAT` of `b7e58ca1`, the
   drivers and the other tools of the `3.2.0.0` test package `e61538ae`
   (the drivers' removability code and every INF but Stor98's unchanged
   since), and the seven files in `NUSB33\W98SE\` as `d64db54b` lays them
   out. The Components page left "USB storage drivers for Windows 98 SE
   (NUSB 3.3's)" unticked, and the Summary gave the 43.51 note: "Found
   installed: USBSTOR.SYS, USBNTMAP.SYS, USBAUTH.SYS, USBU2A.SYS,
   USBMPHLP.PDR, USBSTOR.INF, USBNTMAP.INF. They are left as they are. NUSB
   is installed on this computer (found its Uninstall\USB20U entry). It
   already provides the USB storage drivers, so you can leave this
   unticked." (`n-setup3`, `n-setup4`). Installed, restarted, the
   controller's wizard walked (`hcdwiz-n-*`), restarted.
3. A stick (the 43.42 wizard, then `F:`) and a UAS disk (its first-plug
   wizard, then `G:`): the tray icon appeared with the stick; both listed
   as "USB Disk" (`n-applet`). UAS: Stop, "Safe To Remove Hardware",
   unplugged with no dialog (`n-uas-stop1`, `n-uas-stop2`,
   `n-uas-unplugged`). The stick with `F:\HOLD.TXT` held open: "Problem
   Stopping Device ... cannot be stopped right now", the stick still
   working after (`n-hold-stop2`, `n-hold-after`); then Stop, "Safe To
   Remove Hardware", unplugged with no dialog and the icon gone
   (`n-st-stop2`, `n-st-unplugged`).

This is also 43.51's owed 98 SE reading with NUSB installed: the row
unticked and its note, on a real NUSB setup. Shots in
`E:\xhci98-vm\p43-62\shots\` (`n-*`, `hcdwiz-n-*`); the package in
`E:\xhci98-vm\p43-62\pkg32`. Guest closed.

## 43.65 Windows ME's first shutdown after xhcicm.sys was started (2026-10-23)

Found by 43.46's legs (2026-10-23, its ME leg): the first shutdown right
after the installer had started `xhcicm.sys`, before any restart, hung
in VMM (EIP `c005255a`, steady); later shutdowns were clean.

**Vehicle.** Development host A, QEMU from `usb4-emu` (`bin-2a893ea`),
`pc`, TCG, `pentium3,-apic`, one vCPU, 256 MB; the emulator's
`usb4-generic` USB4 host at 00:04 and its tunnel bridge at 00:05, no
device on the fabric, `qemu-xhci` with a tablet; ME 4.90.3000, the
`me-hcd310` image (copied, an overlay per run), with the ME CD. Kit
`E:\xhci98-vm\p43-6x` (git-ignored).

**Readings.**

| Run | How `xhcicm.sys` was started | Flavour | First shutdown |
|---|---|---|---|
| me1 b | `INSTALL /console /yes` (the Thunderbolt component left out: `xhcicm.sys` not started) | release | clean, about 5 s |
| me1 c | `INSTALL /console /yes /thunderbolt` on the same disk ("xhcicm 3.1.0.0 started") | release (43.46's package) | **hung**, EIP `c005255a` |
| me2 | the Add New Hardware wizard at first boot, pointed at the `qemu` package | `qemu` | **hung**, EIP `c005255a` |
| me3, me4 | the same | `qemu` with a mark at each shutdown step | clean |
| me5 | the same | the same | **hung**, EIP `c005255a` |

Each hang: EIP `c005255a` in VMM (code there reads the PIT counter,
`xor eax,eax; cli; out 43h,al; in al,40h ...`, called again and again),
`EFL` 3046 (interrupts off), no `HLT`; the stack a short VMM one, every
thread blocked. `xhcicm.sys`'s debug-console output stopped at the
start's last lines (its thread emits the log ring after each pass). The
log ring was read out of the guest's memory instead (QMP `memsave`, page
by page over the kernel's linear range; the ring's text found by its
record names): on me2 it ended `cm.shutdown=00000002`, the reenumeration
counters, `u4.stop`; on me5, with a mark after each step of the
shutdown quiescence and of the D3 work, `u4.stop`, `dbg.q1.unbound`,
`dbg.q2.drained`, `dbg.q3.busmaster`, `dbg.q4.released`,
`dbg.q5.d3set` - the mark before `PoCallDriver` of the D3 to the bus -
and no `dbg.q6.d3down` after it. The emulator's trace agrees: the host
interface's rings were stopped (`TX_RING_CTRL`, `RX_RING_CTRL` written
0) as the last thing the driver did. So the D3 `IRP_MN_SET_POWER` that
the connection-manager thread passes down after its quiescence never
came back from the bus driver below. Why the bus blocks there was not
read (NTKERN's and CONFIGMG's; no static reading taken); `xhci98.sys`
passes its D3 down from its own dispatch routine and its shutdown is
clean in every run.

**The change** (`2d65409a`, design record 21 section 5.6): on Windows
98 SE and ME a system shutdown takes the direct route, the quiescence on
the thread and then the system IRP completed, with no D3 requested
(`cm_pwrr.c`, host suite `test_cm_pwrr`).

**After the change** (the gated `qemu` build of `c91940ec`, the same
vehicle, the wizard route of me2 to me5, each on a fresh overlay): five
first shutdowns, all clean, QEMU gone about 5 s after OK; four of the
five logs carry `cm.power.direct=00000004` (the direct route to D3's
quiescence) and `cm.shutdown=00000002`, which the thread now gets to emit
before the machine powers off; the fifth stopped after `cm.start` and
shut down cleanly all the same. A sixth run, whose wizard keystrokes
fell out of step on the loaded host and never started the driver, is not
counted. Before the change three of five such shutdowns hung.

## 43.66 XHCITR's "can run faster" balloon the shell did not show (2026-10-23)

43.28's finding 1: on 7, a stick's first plug gave no balloon of ours
while Windows' own "Your device is ready to use" balloon showed.

**Vehicle.** Development host A, QEMU 11.1.0 (scoop), TCG, `pc`,
`qemu64`, two vCPUs, 1536 MB, the clock at 2026-09-20 as 43.28 ran it;
Windows 7 SP1 x86 (`vm\t28pre\win7-base`, an overlay); `qemu-xhci`
`p2=4,p3=2`, a tablet on QEMU port 1, QEMU's Full Speed `usb-hub` on
port 4 and VVFAT sticks behind it, each with a serial of its own so
that Windows meets it for the first time. The package of `p43-66` at
`401d49fb`, `INSTALL /console /yes /nopause` elevated, one restart.
Kit `E:\xhci98-vm\p43-6x` (git-ignored). The balloons read from
screenshots of the notification area every 4 to 10 s.

**Readings.**

1. `401d49fb` (the first watch: a re-send after a timeout or hide, the
   no-answer re-send only once the shell had answered): first plug
   with the user idle. Windows' "Installing" and then "Your device is
   ready to use" held the area for about seven minutes, since 7 keeps
   a balloon while nobody uses the machine; once the pointer moved it
   went, and no balloon of ours followed in the next two minutes. The
   icon was red in the overflow. 43.28's case, reproduced.
2. The same build with a log of every watch event, the user active: the
   balloon was sent, `NIN_BALLOONSHOW` came 9.7 s later, and
   `NIN_BALLOONTIMEOUT` 8.5 s after that when Windows' replaced it: shown,
   so not sent again.
3. The same, a new stick with the user idle for 150 s: no message at all
   for our balloon while Windows' stayed - no show, no timeout - so the
   no-answer re-send went at 60 s (the session had heard a show in 2);
   once the pointer moved Windows' went and ours was shown, once, then
   timed out. A queued balloon on 7 is answered by silence, which the
   first build only acted on after an earlier answer: reading 1's first
   balloon of the session had none.
4. `bcdbe4b4` (Vista and later count as answering from the start; a
   re-send first takes the queued balloon away): a new tray process, its
   first balloon, a new stick, the user idle for 150 s, then active:
   ours shown once after Windows' went, no second balloon in the next
   two minutes. **Passes.**

Design record 25 section 7.7 item 9.

## 43.68 XHCITR's balloon click on Windows 2000 (2026-10-23)

43.28's finding 3: on 2000 a click on the "can run faster" balloon closed
it and opened no list window; the icon menu's first line did.

**Vehicle.** Development host A, QEMU 11.1.0 (scoop, stock), TCG,
`pc,acpi=off`, `pentium3,-apic`, one vCPU, 512 MB; Windows 2000 SP4
(`vm\t26\win2k-clean`, an overlay); `qemu-xhci` `p2=4,p3=2`, the tablet
on port 1, the Full Speed hub on port 4 and new sticks behind it; the
`p43-66` package at `401d49fb` installed with `INSTALL /console /yes
/nopause` (controller and root hub started, no restart).

**Readings.**

1. A build of `bcdbe4b4` logging every `WM_TRAYICON` message: the
   balloon shown, clicked (the click found by the balloon's colour in a
   screenshot): it closed, no window, and **no message at all** reached
   the tool - no `NIN_BALLOONSHOW` (`WM_USER` + 2) when it appeared, no
   `NIN_BALLOONUSERCLICK` (+ 5) for the click - while a right click on
   the icon arrived as `0x204`, `0x205` and `WM_CONTEXTMENU` (`0x7B`),
   so `NIM_SETVERSION` 3 had taken. 2000's shell does not report
   balloons; the click cannot be seen.
2. `046a8996`: the balloon reads "... Click the icon for what to try.";
   the icon's left click opens the menu, "1 USB device can run
   faster..." first, which opened the list window "A USB device can run
   faster" with the stick's entry. **Passes.**

Design record 25 section 7.7 item 6.

## 43.53 - A Request to a Departed Device That Never Completes

Roadmap-usb4 task 43.53, opened by the owner on 2026-10-22 from 43.49's
Windows 98 SE legs (above): a thread control transfer queued to a mouse
just pulled never completed under QEMU, its 5 s deadline requested the
controller reset, and the fourth reset inside ten minutes latched the bus
failed. Development host A, 2026-10-22 and 23. Design record 13 section
10.5.1 is the design; this section is the run.

### The change

At a thread transfer's deadline (`hcdThreadControlQuiet`), before the
reset: EP0 is stopped, the completion taken if it came first, the
device's path read (`HcdHubPathPresent`), and `XhciTolLateStopped`,
`XhciTolLateVerdict` and `XhciTolLateRetired` (`src\xhci_tol.c`) decide.
A device that has left has its record retired from EP0 (Set TR Dequeue)
and, published, its location cycled; a device present, a hub, or a
record that cannot be retired keeps the reset. Host vectors: `test_tol`'s
`test_late` (3397 checks before the retirement step, 3406 after, 0
failures). Commits on branch `p43-53`: `27b641c3` (the design),
`be9111b0` (the code), `a0f516da`, `f3e42494`, `cdfc8afd` and `0a2188f5`
(reviewed and revised: a trace name; a CREATE_PDO that succeeds after a
late read; the retirement, since a cancel's restart of EP0's survivors
could otherwise run the stopped TD again; comments).

### Method

- Guest: 98 SE + `3.1.0.0` + NUSB 3.3, a standalone copy of 43.49's own
  98 SE overlay (its HID devnodes for the serials `P4349C3` and
  `P4349C41` already installed), a fresh overlay per run; `qemu-xhci`
  `p2=4,p3=4`, TCG, one vCPU, 256 MB, headless. `XhciInterruptMode` 2,
  `XhciPollIdleMs` 50 (43.49's `M2I50`). A mouse at root port 1 and a
  hub at root port 4. 43.49's stick at root port 2 was left out: its
  serial raised the Add New Hardware wizard on this copy, which hid the
  pointer from the detector, so it was removed (run b) or never plugged
  (runs c2, c3).
- 43.49's series unchanged (`churn.ps1`): a `usb-mouse` hot-plugged
  alternately at root port 3 and hub port 4.1, pulled as soon as the
  pointer moved, no gap; a 60 s timeout followed by a 75 s tray-clock
  check.
- Binaries: the `qemu` flavour of `be9111b0` (run b) and of `0a2188f5`
  (runs c2 and c3, one boot), the driver's debugcon log read after each.

### Readings (runtime, QEMU)

| Run | Binary | Cycles | Late transfers to a departed device | Controller resets requested |
|---|---|---|---|---|
| b | `be9111b0` | 60 | 3 (a select's SET_CONFIGURATION) | 0 |
| c2 + c3 | `0a2188f5` | 69 + 60 | 8 (a select's SET_CONFIGURATION: 6 behind the hub, 2 at root port 3) | 0 |

- Each late transfer read in the log as 43.49's a2 did - the select, then
  `select configuration failed, USBD status=80000800` - with no
  `controller reset requested` before it (43.49's a2 had one before each,
  and `ctrl.window.refused` after the third); the select was answered
  DEVICE_GONE (`thread URB done, function/status=00007000`) and the
  location cycled (`hcd: cycling port`). No first-EP0-request-after-
  Address-Device case (43.49's run b) came up in these runs, so that path
  of the change was not read at run time.
- 189 cycles in all, the bus serving plugs at the end of each run, no
  controller reset, the recovery window untouched.
- Pointer timeouts: 1 in run b (the wizard), 9 in runs c2 and c3. Seven
  had `hub port never stable` on hub port 4.1 (the debounce, section
  10.2; 43.49's own logs have it too, 5 times in a2 and b); the others a
  mouse at root port 3 enumerated and its PDO created with no START seen
  within 60 s, as 43.49's a2 cycle 7 shows before any reset. Each was
  followed by a tray-clock check that read the guest alive, except c2's
  last: there `frozen.py` read FROZEN, the screen was a blank text-mode
  frame, and one key brought the desktop back with the clock right - the
  guest's screen blanking after the two idle minutes, not a stop. The
  series then went on (c3) on the same boot.

### Verdict

On QEMU, 43.49's 98 SE series no longer resets the controller: every late
select to a departed device ended DEVICE_GONE with its location cycled,
and none of the 189 cycles spent the recovery window. What a real
controller does with such a transfer is still unread.

### Owed

- The bench reading the roadmap line asks for first: on real hardware,
  a device pulled mid-enumeration (and mid-select) at a root port and
  behind a hub - whether metal ever leaves the transfer without an event.
- The first-EP0-request-after-Address-Device case at run time.
## 43.47 - The Vanished-Child Transaction's Legs on 98 SE and ME

Task 43.47 of `roadmap-usb4.md`: the functions a tunnel down took away,
removed as transactions of 43.18's kind on the member's call worker,
the veto retried every 2 s, the member held until every one is retired
(record 21 section 7.2, "The vanished-child transaction"). Read on
2026-10-23.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

### Verdict

**Every leg passes, on 98 SE and on ME.** With a file held open on the
drive at the unplug, the vanished child's `CM_Query_Remove_SubTree` was
refused (`CR_REMOVE_VETOED`, 17h) and asked again every 2 s, never
forced; the drive plugged back in meanwhile was neither exposed (no
`UP`) nor enumerated; once the file was let go the next query was
accepted, the removal followed, the record retired, and only then did
the held exposure and re-enumeration run: the returning drive came up
under a new generation and its files listed. With two tunnel ports the
other port's plug, unplug, statement and removal ran while the first
port was held.

### The vehicle

- Development host A, QEMU under TCG, headless, one vCPU per guest
  (`pentium3,-apic`), 256 MB, one guest at a time; `usb4-emu` at
  `660f085` (the emulator binary 43.18 and 43.19 used, SHA-256 prefix
  `9dfdcf9b`). The host was shared with other agents' guests and builds.
- **98 SE**: 43.18's 98 SE image (3.1.0.0 installed, the Alpine Ridge
  topology of the ICM client, one SHPC tunnel bridge at 00:06 below the
  TB3 switch, member 02:08), and the same image under the generic USB4
  topology (`-Usb4`, two tunnel ports 00:05 and 00:06).
- **ME**: 43.18's ME image, the generic USB4 topology (two tunnel ports,
  members 00:05 and 00:06), an ASM2464 USB4 enclosure.
- The drive: the 128 MiB FAT image of 43.18 (`E:`, `BIG.BIN` 16 MiB,
  `TESTDATA.BIN`), enclosure `enc0` on port 1 (`enc1` on port 2 for the
  two-port leg). Stor98: the bundled 3.1.0.0 build as installed.
- `xhcicm.sys`: the `qemu` flavour, copied over the installed one by
  `D:\N\UPD.BAT`, then a cold restart. The legs below are of the build
  of `ff50ddb3` (this task's last commit of code; `scripts\build-driver.cmd all`, x86
  and amd64, passed; the `qemu` image's SHA-256 prefix `92dcbfd0`),
  but where a row says otherwise: the ME rows marked *earlier* are of
  `4a0d143a`'s build, two review rounds before, whose changes since are
  races the legs do not reach (the stop's ordering, an adoption's
  revalidation, a usable route, a fallback with no free record).
- **The holder.** A copy from the drive with its 20th read held by the
  harness's `x-faults` rule `at=fetch op=read nth=20 act=hold` on `tb0`,
  and the enclosure unplugged while the read is held: (L1) Explorer's
  copy of `E:\BIG.BIN` to the desktop, which after the unplug shows
  "Cannot copy BIG: ..." and keeps the file open until OK is pressed;
  (L2) an MS-DOS Prompt's `copy /b E:\BIG.BIN NUL`, which shows "General
  failure reading drive E / Abort, Retry, Fail?" and keeps the file open
  until it is aborted and the prompt exited - the DOS box of Stor98's
  Q-9x reading. The rule is cleared after the unplug.
- The driver's lines are the `qemu` flavour's log on the debug console:
  `cm.vanish.sweep` (the sweep entered), `cm.vanish.reading` (bridge
  31:16, complete 8000h, records made 7:0), `cm.eject.call=42..` and
  `43..` (a record's query and removal submitted), `cm.vanish.cr`
  (bridge 31:16, the record's phase 15:8, the call's `CONFIGRET`),
  `cm.vanish.retired`, `cm.nvlink.up.held` (an exposure held behind the
  gate), `cm.reenum.held.run`, `cm.reenum.9x.reenumerate`,
  `cm.nvlink.up`.

### Results

| System, profile, leg | At the unplug | While vetoed | Re-plug meanwhile | Released |
|---|---|---|---|---|
| 98 SE, ICM, L1 (Explorer's copy) | statement, ack 206 ms, sweep: one record | query refused 17h, 15 times in 30 s (every 2 s) | `cm.nvlink.up.held`, no `UP`, no re-enumeration | OK pressed: query accepted, removal, `cm.vanish.retired`, held run, `cm.nvlink.up=02000401`, re-enumeration, `E:` listed |
| 98 SE, ICM, L2 (DOS box) | the same, ack 55 ms | 17h, 13 then 15 times | held, as L1 | Abort, `exit`: accepted on the third query after, removed, `UP` at the new generation, `E:` back |
| 98 SE, USB4, L1 | both members swept: 00:06 had nothing (its held work ran at once), 00:05 one record | 17h, 15 times | held for 00:05; 00:06's re-enumeration ran | as on the ICM, `UP=02000101` |
| 98 SE, USB4, L2 with the two-port leg | as L1 | 17h throughout | port 2's drive plugged: its `UP=02000201` and re-enumeration ran while port 1 stayed held; unplugged: its own statement, sweep, record, query accepted and removal ran, and port 1's retries went on | Abort, `exit`: removed, `UP`, `E:` back |
| ME, USB4, L1, *earlier* | as on 98 SE under USB4 | 17h, 15 times | held; the other member's re-enumeration ran | OK: removed, `UP=02000101`, `E:` back |
| ME, USB4, L2 with the two-port leg, *earlier* and again on the last build | as above | 17h throughout | port 2 as on 98 SE (its `UP=01000201`, then its removal) | removed, `UP=02000101`, `E:` back |
| ME, USB4, no holder (the last build; the copy did not start) | the query accepted at once, removal, retired | - | the re-plug enumerated and exposed at once | - |

Every removal of a vanished child after its accepted query ended in the
identity read-back before `Remove_SubTree` answering
`CR_NO_SUCH_DEVNODE` (0Dh): the devnode was gone (or its handle named
another id) by then, so the record retired as a verified-gone one and no
`Remove_SubTree` was called; in every leg the returning drive then came
up as a fresh device and listed its files. Which of the two it was is
not read.

The guests' logs: `E:\xhci98-vm\p43-47\keep` (`debugcon-*.log`,
`trace-*.log`, `leg-*.txt`).

### Findings

- **A wizard holds the query, and the member waits.** On 98 SE under
  the USB4 profile, the first plug of the drive on the new topology
  opened Windows' Add New Hardware wizard for Stor98; the unplug's
  query was submitted and did not return while the wizard was open (the
  configuration manager busy, record 21 section 6.1's reading), the
  member stayed held, the other member's work went on, and the query,
  the removal and the re-plug's `UP` followed as soon as the wizard was
  finished.
- **What holds the volume on 9x.** Only a copy whose read had failed
  vetoed the query: Explorer's copy with its error box up, and the DOS
  box's copy at its Abort, Retry, Fail prompt (and, after Abort, until
  the prompt was exited). A file kept open by `copy con E:\HOLD.TXT` in
  an MS-DOS Prompt, and an Explorer window open on `E:\`, did not: the
  query was accepted at once.
- **A system-modal screen stops everything**, as 43.18's ME run C2
  found: Windows' "Disk Write Error" screen came up after some unplugs
  with a held read (98 SE and ME), and nothing ran - the driver's
  retries included - until a key was pressed; then the retries went on.
- **One stall, on a trial build, not reproduced.** On 98 SE under the
  ICM client, with the first build of the code (uncommitted then,
  SHA-256 prefix `6ad15224`), after the DOS box's copy was aborted and an
  Explorer window on `E:` was closed, one query never returned and the
  connection-manager thread logged nothing more (a later unplug was not
  logged); the guest was quit. The same sequence, the Explorer window
  included, ran through when tried again on that build, and no later
  leg stalled. The cause is not read; a system-modal screen up at that
  moment, which the screenshots taken may have missed, is the
  likeliest, and is not established.
- **ME at boot.** One fresh ME overlay faulted at boot with
  `0E : 0197 : BFF62170` before anything of this task was copied in
  (43.19 saw the same on an older overlay); the next fresh overlay
  booted and was used.

### Not read

- A removal that keeps failing past 30 s (the blocked bit, O6): no
  holder made `Remove_SubTree` fail here (every removal ended verified
  gone); the host vectors carry it.
- The NHI stopped mid-retry and the record adopted by the next start,
  the takeovers by `CONTROLLER_UNAVAILABLE` and by the removal-only
  request, and the parked claims: host vectors only.
- The ICM client with two members (Titan Ridge) on 98 SE.
- A deeper queue, and real hardware.

## 43.20 - GEN1.3's Virtual-Machine Legs on 98 SE and ME (2026-10-22/23)

Roadmap 43.20: "the virtual-machine part of GEN1.3: ME; a resume with two
enclosures attached; volumes over 128 MiB; two drives behind one
enclosure's or dock's switch under load; an internal NVMe under Stor98
beside a tunnelled one under load". GEN1.2 (42-V.7, `E:\xhci98-vm\m31\v7\RESULT.md`)
read two enclosures, one per USB4 port, on 98 SE only, with 128 MiB volumes
and no resume. This sheet is for the coordinator to fold into `runs/run-43.md`.

**Verdict: open.** Every leg passes on ME. On 98 SE every leg passes except
the resume, which cannot be read on this vehicle (below). No driver defect
was found, and nothing in the tree changes except this sheet and the roadmap
line. Three things were found on the way. None is shown to be the driver's:

- **The kit's own first layout was wrong.** With an internal NVMe at `00:08`,
  both enclosures' NVMes sat at Code 29 with no interrupt (42-A.15's
  no-route case). The chain's NVMe, whose interrupt line `xhcicm.sys` writes,
  started. With the internal NVMe moved to a function of a listed slot
  (`00:03.1`), every NVMe starts. `00:08` lies outside SeaBIOS's static
  `$PIR` table, which lists only bus-0 devices 1-6. That this is the cause is
  an inference from the three layout controls in "The `$PIR` layout" below.
- **ME hangs on waking after a cancelled background MS-DOS job.** Three wakes
  hung, and each came after a `start /m` copy loop had been stopped with
  Ctrl+C. A stock ME guest with none of this package's files hangs the same
  way after the same steps, so the hang is not attributed to the package.
  The resume leg's readings come from wakes without that step.
- **98 SE cannot be read across a Stand by here.** After the wake, 98 SE
  keeps finding "new" PCI-to-PCI bridges, Stor98 controllers and unknown
  "PCI Card" devices, and the tunnelled volumes do not come back. QEMU, read
  after the wizards had started, showed both tunnel bridges with secondary
  and subordinate bus 0 and their windows closed. The likely reading, not
  shown: the bridges lost their numbers in S3 and 98 SE walked them in that
  state. ME logs `cm.nvlink.up.no-bus` at each resume too (as in 42-A.25),
  yet its volumes return; what numbers ME's bus again was not identified.
  Some 98 SE wakes here also showed display or MS-DOS Prompt failures,
  one of them on a package-installed control with no tunnel device
  attached (as N3 N6). So a real check is needed (below).

GEN1.3's real-hardware part (two-port hardware) stays with GEN1.3 and the
owner's bench, as before.

| Leg | ME | 98 SE |
|---|---|---|
| Fresh install with Thunderbolt and Stor98, cold start, all five NVMe volumes present, no problem code | P (exit 4 and the wizards, as N3 meu2) | P (exit 3) |
| GEN1.2 on ME: a long run on port 1's enclosure while port 2's is unplugged for 20 s and replugged, x3 | P | (GEN1.2, 42-V.7) |
| Volumes over 128 MiB: an 8 MiB file written past 4 GiB (6 GiB volume) and past 2 GiB (3 GiB volume) read back; 100.7 MB passes written past 4 GiB and compared | P | P |
| Two drives behind one switch (two downstream ports of one switch, each with its NVMe), both under copy-and-compare at once | P (2 passes each) | P (4 passes each) |
| Internal NVMe under Stor98 beside a tunnelled one, both under copy-and-compare at once | P (2 passes each) | P (4 passes each) |
| Resume with two enclosures attached (and the switch's two drives and the internal NVMe): files written before Stand by compare clean after the wake, new writes compare | P (one wake with every drive written before and compared after; other wakes checked to varying extents, as listed in ME item 6; see the hang note) | not readable here ("98 SE resume") |
| N3 F1 retaken: port 2's enclosure unplugged, Stand by, wake, port 2 plugged | P: port 1's volume stays; only port 2's entry published | - |

Mismatches: none. The one `MISMATCH` mark in `me2x\debugcon-d.log` (`HIF1`)
was an operator error: the test named `F:`, which was the CD there, so `fc`
found no file (the internal volume was `E:`; `HIE1-OK` follows).

### Build

Branch `p43-20` at `b7e58ca1` (the `3.2.0.0` release branch's head, no
source change: the development build for `3.2.0.0`, still stamped
`3.1.0.0`, as the installer log and `XHCITR` show). Gated
`build-driver` x86, all flavours, passed (`out\gated-x86-release.txt`); the
release package staged by `make-package.ps1 -Flavor release`
(`E:\xhci98-vm\p43-20\pkg-rel`), checked equal to the guests' `xfer\PKG\release-x86`:
`xhci98.sys` `1A4328F4...`, `xhcicm.sys` `B02C0C71...`, `stor98.mpd`
`6A46D54F...` (the gated manifest's values). `INSTALL.BAT`, `XHCIINST.EXE`,
`XHCIWIZ.EXE`, `XHCIDI.EXE` and `XHCITR.EXE` from the same build. Hashes of every
transferred file: `E:\xhci98-vm\p43-20\sums-pkg.txt`, `sums-xfer.txt`.

### The vehicle

Development host A, QEMU TCG, host under heavy load from other agents'
guests (CPU about 76-97 %); no timing is read here. usb4-emu `660f085`
(`qemu-system-x86_64.exe` SHA-256 `9dfdcf9b...`, checked by the launcher),
i440FX, `pentium3,-apic`, 1 vCPU, 256 MB, headless, screendumps and keys over
QMP. Kit `E:\xhci98-vm\p43-20` (`scripts\launch.ps1`, `pids.txt`, `shots\`,
`logs\`). Topology (`launch.ps1 -Chain -Chain2 -Internal -Enc0AtBoot -Enc1AtBoot`):

- `usb4-host profile=usb4-generic` NHI `00:04`, tunnel bridges `tb0` `00:05`
  (port 1; `bus-reserve=8,mem-reserve=32M`) and `tb1` `00:06` (port 2);
- port 1: `enc0`, the ASM2464 device router, with its NVMe (6 GiB FAT32,
  `HI.BIN` written by mtools after 4 GiB of filler, so its clusters lie past
  4 GiB), and behind the same tunnel the switch stand-in: `usb4-asm-pci-usp`
  (1B21:2421) with two downstream ports (`pci-bridge-seat` at its 0 and 1),
  each with an NVMe (`CHAIN`, `CHAIN2`, 512 MiB) - two drives behind one
  switch, as a dock's;
- port 2: `enc1`, a plain `usb4-device` with its NVMe (1 GiB);
- internal: an NVMe on bus 0 at `00:03.1` (3 GiB, `HI.BIN` past 2 GiB;
  `00:03.2` on 98 SE from its cold start on, see 98 SE item 2),
  `qemu-xhci` at `00:03.0` made multifunction for it.

Data: `D:\SET\S1..S3.BIN` (3 x 32 MiB random, 100.7 MB a pass), `D:\SM\B4.BIN`
(4 MiB), `D:\HI.BIN` (8 MiB); batches from 42-V.7 (`LA.BAT`, `LP.BAT` = four
`LA` passes, `SB.BAT`, `FC1.BAT`) plus `HI.BAT`. Every compare is `fc /b`
and writes a `t9x mark <tag>-OK` or `-MISMATCH` line to the debug console.
"At once" = two `start /m D:\LP.BAT` windows running together.

Guests: ME = an overlay of `vm\t37me\winme-stock-base.qcow2` (`me2`, the
install), then overlays of it for the legs (`me2x`, `me2c`, `me3`). 98 SE =
an overlay of `vm\t26\win98-clean.qcow2` (`w98b`). The predecessor's ME
overlay (`me`, the 3.1.0.0 image `me-310u4` with binaries copied by hand) was
set aside and not used for any reading.

### The `$PIR` layout (kit finding, not the driver)

First cold start with the internal NVMe at `00:08` (`me2` boot b): only the
chain's volume came up. `XHCITR /report`: both enclosures' NVMes "Stor98
Storage Controller for NVMe SSDs [not started - problem code 29]",
"Resources: none allocated", asking for 16 KiB of memory and no IRQ; the
chain's NVMe, whose interrupt line `xhcicm.sys` writes when it numbers the
chain (`cm.brnum.9x.irq=00010A01`), started. Three boots of the same disk:

| Boot (`me2x`) | Internal NVMe | Enclosures' NVMes |
|---|---|---|
| a | none | both started (IRQ 10), F: and G: |
| b | `00:08` | both Code 29, no resources |
| c | `00:03.1` | both started, plus the internal volume |

SeaBIOS's `$PIR` (read from guest memory at `F55A0`) has six entries, bus 0
devices 1-6. A bus-0 device outside it is consistent with `PCI.VXD` giving
up IRQ routing for functions added after POST, which is 42-A.15's case
(record 28's note); the readings above show the effect, not `PCI.VXD`'s
reason. The ME install (`me2` boot a) had the internal NVMe at `00:08`;
every ME leg after it uses `00:03.1`, every 98 SE leg after its install
`00:03.2`. Nothing in the driver changes:
interrupt routing for a function that appears after POST with no route is
NOIRQ.1's question and stays there.

### ME

1. Install (`me2` boot a): first boot's wizards finished with no driver;
   `INSTALL /console /yes /thunderbolt /stor98` from `D:\PKG`: exit 4 and
   the Steps text, while Windows' own wizards bound Stor98 on the tunnel
   NVMes from `C:\XHCI98` / `INF\OTHER` (as N3 N4 and meu2). Log
   `logs\me2\inst1-log.txt`: the internal NVMe "is internal ... the installer
   leaves it as it is (Stor98 goes on tunnel drives only)". The shutdown
   after the install did not power off (black screen for 6 min, the processor
   looping at `C005255A` with interrupts off); stopped by hand, ScanDisk at
   the next boot (compare N3 N8; one occurrence, unattributed).
2. Cold start, full topology (`me2x` boot d): Windows' wizard found Stor98 for
   the second downstream port's NVMe ("Select Other Driver": the
   `INF\OTHER` copy and `C:\XHCI98\STOR98.INF`, the first taken) and for the
   internal NVMe at its new address. Volumes: E: INTNL (internal), G: ENC0,
   H: CHAIN, I: ENC1, J: CHAIN2. `XHCITR /report` (`logs\me2x\d1-rep.txt`):
   no problem code; `cm.brnum.9x.win-need=00300000` with three bridge windows
   (`FE70FE60`, `FE70FE70`, `FE60FE60`) inside port 1's `FC800000-FE7FFFFF`.
3. Over 128 MiB: `HIG1-OK` (6 GiB volume, past 4 GiB), `HIE1-OK` (3 GiB,
   past 2 GiB).
4. Switch pair at once (H: and J:): `CH1`, `CH2`, `CJ1`, `CJ2` all S1-S3 OK
   (12 compares, 4 x 100.7 MB written). Stopped after pass 2 (about 20 min a
   pass under this load).
5. Internal beside tunnelled at once (E: and G:): `IE1`, `IE2`, `TG1`, `TG2`
   all OK (12). G:'s writes land past 4 GiB.
6. Resume (`me2c` boot e, full topology): five drives written (`P1x-OK`),
   Stand by (QEMU "paused (suspended)"), `system_wakeup`: all five volumes
   back; the files written before compare clean (`Q1E/G/H/I/J-OK`), `HIG2-OK`,
   `HIE2-OK`, new writes `R1x-OK`. Other wakes that came back, each read
   less fully:

   | Wake | Attached across the sleep | Read after it |
   |---|---|---|
   | `me2c` e, first | full topology | the desktop, an Explorer window per volume, all five `vol` labels |
   | `me2c` e, third (item 7) | full, ENC1 absent | files compared, see item 7 |
   | `me3` a, first (an MS-DOS Prompt left open after `H1G-OK`) | full | the desktop and the open prompt only |
   | `me2c` a | NHI and bridges only | the desktop |
   | `me2c` b | the two enclosures | the desktop, Explorer windows for both volumes |
   | `me2c` c | the enclosures and the internal NVMe | the desktop, Explorer windows for the three volumes |
   | `me2c` d | the enclosures and the switch's two drives | the desktop, Explorer windows for the volumes |

   `me2c` boot e's report counts `cm.suspends 3`, `cm.resumes 3` (the first
   wake, this one and item 7's).
7. N3 F1 retaken (`me2c` boot e): ENC1 unplugged awake, Stand by, wake, ENC1
   plugged: port 1's volume stays (G: ENC0), ENC1 returns (as J:); every file
   written before compares clean (`F1G/J/H/I/E-OK`), new writes `F2G/J-OK`.
   The log publishes port 2's own entry only, `cm.nvlink.up=02000A01`, after
   the resume's `no-bus` entries; the cross-port republish F1 read
   (`02000101` on port 2's action) is gone.
8. GEN1.2 on ME (`me2c` boot e): a long run on G: (`UG1`, `UG2` OK) while
   ENC1 was unplugged for 20 s and replugged three times, with a 4 MiB
   compare after each replug (`UB1-3-OK`); the tray said "QEMU USB4 NVMe
   Enclosure was disconnected".

Hangs on waking (not attributed). Three wakes did not come back:
`me2x` boot d, `me2c` boot e's fourth wake, `me3` boot a's second wake.
Each followed a `start /m D:\LP.BAT` window that had been stopped with
Ctrl+C and closed. Display frozen; the processor in V86 code
(`0070:042B`/`0000:0000`, then `FFFF:3536`) or in the VMM (`C00138C1`); in
the usb4 trace both tunnels stay down with `awaiting_ack=1` (the NHI is
never touched again). Control: the stock ME image with no xhci98 file, the
same NHI and bridges with no driver, the same loop on C: stopped with Ctrl+C,
then Stand by: hung the same way (processor at `016F:BFF83847`, display
frozen). A stock wake with an MS-DOS Prompt merely open came back. Traces:
`logs\me2x\trace-d-resume-hang.log`, `logs\me2c\trace-e-resume-hang.log`,
`logs\trace-me3-a-resume-hang.log`.

### 98 SE

1. Install (`w98b` boot a): bridge wizards (`MACHINE2.INF`), USB and internal
   NVMe finished with no driver; `INSTALL /console /yes /thunderbolt /stor98`:
   the CD prompt for `usbd.sys` (`E:\WIN98`), then, as the tunnel came up,
   the chain's bridge wizards (each asking for a restart, declined) and
   Windows' Stor98 wizards for the four tunnel NVMes (the first asked for
   the "Stor98 Storage Controller Disk", `C:\XHCI98` typed; as 42-V.7 O1);
   exit 3. Log `logs\w98b\inst1-log.txt`.
2. Cold start (boot b, the internal NVMe moved to `00:03.2` so that it is a
   new device): Windows' wizard installed Stor98 on it from `C:\XHCI98`, and
   the USB HID wizard took the CD. Volumes: F: INTNL, G: CHAIN, H: CHAIN2,
   I: ENC0, J: ENC1.
3. Over 128 MiB: `WHI1-OK` (past 4 GiB), `WHF1-OK` (past 2 GiB).
4. Switch pair at once (G: and H:): four passes each, 24 compares OK.
5. Internal beside tunnelled at once (F: and I:): four passes each, 24
   compares OK. I:'s writes land past 4 GiB.

Marks: `w98b\debugcon-b.log`: 48 load-compare `-OK` marks, 55 `-OK`
marks in all, no `MISMATCH`.

### 98 SE resume

Not readable on this vehicle. Read in order:

| Guest, boot | Attached | Before Stand by | After `system_wakeup` |
|---|---|---|---|
| `w98b` b | full topology | MS-DOS Prompts used for the legs | display frozen; a `T9X` typed into Run never ran; both tunnels stay down |
| `w98b` c | full | fresh boot, no MS-DOS Prompt | the same |
| `w98k` a (`w98b`'s disk) | NHI and bridges only | - | fatal exception 0E, `018F:BFF9DFFF`, after the Run probe |
| `w98ctl` a, `w98ctl2` a (stock 98 SE, no package; `2a893ea` emulator for the second) | NHI and bridges only | - | `w98ctl2`: a live desktop before any key; the Run probe then gave fatal exception 0E, `0167:BFF9DFFF` (both). So the probe crashes 98 SE after a wake, and the `w98k` reading says nothing |
| `w98b` d | full | fresh boot | display back after Ctrl+Esc; both tunnels re-inserted (gen 2); then New Hardware wizards one after another: four PCI-to-PCI bridges (each asking for a restart), two Stor98 controllers (each asking for a restart, one with an empty warning box), unknown "PCI Card" devices; then "There is not enough memory available to display the program" for an MS-DOS Prompt |
| `w98b` e | enclosures, internal | fresh boot | an unknown "PCI Card" wizard; in a full-screen MS-DOS Prompt `F:` was the CD and `G:` to `J:` read "Invalid drive specification": no tunnelled volume |
| `w98b` f | none (the package installed) | fresh boot | a live desktop; a windowed MS-DOS Prompt "not enough memory to display", black after full screen and back |
| `w98b` g, h (`2a893ea`) | two enclosures | fresh boot | a "PCI Card" wizard. `w98b`'s disk still carried the cancelled wizards of boot d, so this is not a clean reading |
| `w98c` c (a fresh install with only the two enclosures; `XPF`, `XPG`, `XHF` written and compared in the boot before) | two enclosures | fresh boot, no MS-DOS Prompt | both tunnels re-inserted; then a PCI-to-PCI bridge wizard, another, a third. QEMU `info pci` (`logs\w98c\info-pci-after-wake.txt`): `tb0` and `tb1` "secondary bus 0, subordinate bus 0", memory windows closed; before the sleep `XHCITR` read `tb0` "primary 00, Bus 01-01" |

What was read: `info pci`, taken after the third bridge wizard of `w98c`
boot c, shows `tb0` and `tb1` at secondary and subordinate bus 0 with their
windows closed. Before the sleep, `XHCITR` showed them numbered. The rest is
a hypothesis, not shown: the bridges lose their numbers and windows in S3,
98 SE walks them before anything numbers them again, and a bridge whose
secondary bus is 0 shows bus 0 again below it. That would fit the wizards
for "new" bridges and for devices with no driver, but when the numbers
changed and what 98 SE walked were not read. ME also logs
`cm.nvlink.up.no-bus` at each resume (42-A.25), yet its volumes return; what
numbers its bus again was not identified.

Source reading: `xhcicm.sys` numbers only bridges below a tunnel port, never
the port itself. `XhciTbtNumberBridges` (`src/cm/tbt_brnum.c`, at the
`XHCI_TBT_CFG_BUSES` read) refuses a port whose secondary bus is not above
its own bus (outcome `XHCI_TBT_BRNUM_PORT`), and `cm_noirq.c` has no
numbering step. Numbering the tunnel port on a 9x resume would be new
design, not a fix within 43.20. Some wakes here also broke 98 SE's display
or a windowed MS-DOS Prompt, `w98b` boot f with no tunnel device among
them (N3 N6). So the data part of the leg could not be read on this
vehicle.

Traces: `logs\w98b\trace-b-resume-hang.log`, `trace-c-resume-hang.log`,
`trace-d-resume-storm.log`, `trace-e-resume-encs.log`,
`trace-g-resume-encs.log`; `logs\w98c\trace-c-wake.log`.

### Left for the owner's bench (GEN1.3)

- **Real check needed (a diagnostic, not 43.20's leg): 98 SE Stand by with
  a Thunderbolt/USB4 NVMe enclosure attached.** It tells whether the
  emulator's unnumbered bridges also happen on real hardware. 43.20's
  98 SE resume with two enclosures stays owed either way: it needs a
  two-port host (next item), unless the owner substitutes or defers it. On
  the P14s Gen 1 (one port) under 98 SE with this package and the bundled Stor98: write a file
  to the enclosure's volume, restart, then without opening an MS-DOS Prompt
  choose Start > Shut Down > Stand by, wake the machine, and wait a minute.
  Look for: no New Hardware wizard for a "PCI standard PCI-to-PCI bridge",
  a "Stor98 Storage Controller" or a "PCI Card"; the volume under the same
  letter; the file comparing clean (`fc /b` in a full-screen MS-DOS Prompt);
  `XHCITR /report` showing the tunnel port with its bus numbers (the
  "Buses" line) as before the sleep. If wizards do appear, record them and
  compare the tunnel port's bus numbers before and after the sleep;
  matching symptoms alone do not establish the emulator's cause.
- Two real NVMe enclosures on a two-port USB4/Thunderbolt host, 98 SE and
  ME: a long copy-and-compare on one while the other is unplugged and
  replugged, and a Stand by and wake with both attached (the P14s has one
  port).
- Two NVMe drives behind one real dock's switch at once, and an internal
  NVMe under Stor98 beside a tunnelled one, on a machine whose BIOS routes the
  tunnel port's interrupts (the emulator's needs the layout above).
- ME's hang on waking after a cancelled background MS-DOS job: whether a real
  ME machine does it too (the stock control here reproduces the hang
  without the package; the cause remains unidentified).

## 43.28 - the "can run faster" notice, the legs (2026-10-23)

Roadmap-usb4 task 43.28: XHCITR's "can run faster" notice (43.27, design
record 25 section 7.6; the driver's BOS read at High and Full Speed,
record 13 section 8.4.1) read in guests. Runtime, development host A,
2026-10-22 23:00 to 2026-10-23 03:30, on branch `p43-28` (= `p43-27` at
`71e6c270`).

**Verdict: ticked (2026-10-23), the legs moved to other checks excepted.**
The first session (below, "Readings") read the notice's High Speed case
on six systems; the second ("A QEMU whose stick declares 0x0210") read the
SuperSpeed case on all six systems with a QEMU built for it, and read the
legs the first left. What the line asks and is not read here was moved by
the coordinator's decisions of 2026-10-23: a stick behind a High Speed hub
and a SuperSpeedPlus device at 5 Gbit/s to the owner's checks on real
hardware (QEMU models neither), the two-controller 8 or 8.1 guest to
43.31's bench, and finding 1 (7's lost first balloon) to task 43.66.
Finding 3 (2000's balloon click) is open.

### What QEMU can and cannot carry (read from its source)

QEMU 11.1.0's device sources (`hw/usb/`, tag `v11.1.0`, read 2026-10-23
from an unpacked release tree on this host; `external\qemu` holds only
`hcd-xhci.c`):

- `dev-storage.c` and `dev-uas.c` give `usb-storage` and `usb-uas` a Full
  and a High Speed device descriptor with `bcdUSB` 0x0200 and a SuperSpeed
  one with 0x0300. `desc.c` picks the descriptor by the speed the device
  attached at, so at High or Full Speed the device declares 0x0200. No
  device in `hw/usb/` declares 0x0210 at any speed (the others: 0x0100,
  0x0110, 0x0200; `canokey` 0). `desc.c` raises a `bcdUSB` below 0x0200 to
  0x0200 when `msos-desc` is on, never further.
- `desc.c` answers GET_DESCRIPTOR(BOS) at any speed (a USB 2.0 Extension
  when the device has a High Speed descriptor, a SuperSpeed capability
  when it has a SuperSpeed one), but record 13 section 8.4.1's gate asks
  only a device declaring 0x0210 or more, so `xhci98.sys` never reads it
  below SuperSpeed.
- `dev-hub.c`'s `usb-hub` is the only hub QEMU models: `bcdUSB` 0x0110,
  Low and Full Speed ports only. There is no High Speed hub.
- `hcd-xhci.c`: a root port that has both a USB 2.0 and a USB 3 half puts
  a SuperSpeed-capable device on the USB 3 half; a root port with a USB
  2.0 half only (`p2` greater than `p3`) puts it at High Speed.

So in QEMU: **a SuperSpeed stick on a USB 2.0 root port runs at High
Speed declaring 0x0200 and is never counted** (no BOS read, no capable
flag; read below on every system), **no stick can be put behind a High
Speed hub**, and no device runs at SuperSpeedPlus. The USB 2.0
root-port legs need a device that declares 0x0210 at High Speed: a real USB
3 stick, or a QEMU built with `dev-storage.c`'s High Speed `bcdUSB` set to
0x0210 (a one-line change outside this repository, not made here). The
legs behind a High Speed hub need a High Speed hub as well, and the 5
Gbit/s leg a SuperSpeedPlus device, which no such change gives: real
hardware, or new QEMU device models. What QEMU does carry is
record 25 section 7.6 item 1's High Speed case: `usb-storage` behind
`usb-hub` runs at Full Speed, declares 0x0200 and answers
GET_DESCRIPTOR(DEVICE_QUALIFIER) with its High Speed descriptor, so it
counts, declared High Speed. Every leg of the notice's machinery below
(the balloon or tooltip, the red icon, the menu line, the list window,
two devices, one moved, clearing, the default, the setting) was read on
that case.

### The vehicle

- QEMU 11.1.0 under TCG, headless, no display client; 98 SE, ME and 2000
  1 vCPU (2000's image has the uniprocessor HAL with ACPI off), XP, 7 x86
  and 7 x64 2 vCPUs; 256 MB (98 SE, ME), 512 MB (2000, XP), 1536 MB (7).
  One guest at a time; 5 to 10 other agents' guests ran on the host.
- `qemu-xhci,p2=4,p3=2`: QEMU ports 1 and 2 are the controller's ports 1
  and 2 (USB 3) with ports 3 and 4 their USB 2.0 halves; QEMU ports 3 and 4
  are the controller's ports 5 and 6, USB 2.0 only. `usb-tablet` on QEMU
  port 3 or 1, `usb-hub` on QEMU port 4 (or 3), `usb-storage` sticks on
  VVFAT directories, plugged and unplugged over QMP.
- Guests, each on an overlay of its own over the read-only bases:
  `vm\t26\win98-gold`, `vm\t37me\winme-stock-base`,
  `vm\t26\win2k-clean`, `vm\t28pre\winxp-base`, `vm\t28pre\win7-base`,
  `vm\t28pre\win7-x64-base` (F8 "Disable Driver Signature Enforcement").
- Builds of `71e6c270`, x86 and amd64: the `qemu` flavour (98 SE, ME;
  `xhci98.sys` x86 sha256 `4f89a642...`), the `release` flavour on the NT
  systems (the installer takes only `release-` from a package's
  `release-x86` directory; the qemu flavour's marker is refused there, as
  it should be). XHCITR.EXE sha256 `508b6ae4...`. The package tree was the
  branch's `make-package.ps1` output for each architecture with the
  branch's XHCIINST, XHCIWIZ and XHCITR.
- How it was installed: 98 SE and ME by the controller's Update Driver
  wizard on the package's `release-x86` (98 SE's gold image also copies
  `XHCI98.SYS` at boot), XHCITR started by hand with `/tray`; 2000, XP, 7
  x86 and 7 x64 by `INSTALL /console /yes /nopause` (XP with
  `/nofaster`), exit 3, then a restart (7 x64 none: the tray was started by
  hand), XHCITR starting at login.

### Readings

| | 98 SE | ME | 2000 | XP | 7 x86 | 7 x64 |
|---|---|---|---|---|---|---|
| default with no value | on | on | on | (off, `/nofaster`) | on | on |
| a stick at High Speed on a USB 2.0-only root port | not counted | not counted | not counted | not counted | not counted | not counted |
| a stick behind the Full Speed hub: notice | tooltip, red | balloon, red | balloon, red | balloon, red (after the user's on) | balloon on a replug, red | balloon on a replug, red |
| two behind the hub: count | 2 | 2 | 2 | 2 (list) | 2 | 2 (list) |
| one moved to a USB 3 port (SuperSpeed) | 1 | 1 | 1 | 1 | 1 | 1 |
| the other moved or removed | blue | blue | blue | blue | blue | blue |
| balloon click opens the list | - (98 SE has none) | yes | not read | yes | not read (menu: yes) | yes |
| the user's setting off / on | blue at once / red, no balloon | the same | the same | on over the computer's off: red, no balloon | the same | not read |

The details, per system:

- **98 SE.** Stick behind the hub: `XHCITR /report` (sent to COM1) read
  "Can run faster ... Port 6.1 ... Behind xHCI98 USB Hub (Full Speed (12
  Mbit/s)); Runs at: Full Speed (12 Mbit/s); Can run at: High Speed (480
  Mbit/s); Try: Connect it to a port of this computer, or through a USB
  2.0 or USB 3 hub, rather than through this Full Speed hub." The icon
  turned red; its tooltip "xHCI98 USB Tree: 1 USB device can run faster";
  the left-click menu's first line "1 USB device can run faster...", which
  opened the list window. A second stick: "2 USB devices can run
  faster...". Each moved in turn to a SuperSpeed root port: 1, then the
  blue icon; the report read both at SuperSpeed and nothing counted. A
  stick on the USB 2.0-only root port: High Speed, `bcdUSB` 2.00, "device
  not SuperSpeed capable", not counted; the debug channel shows BOS reads
  at SuperSpeed only. The right-click item "Tell me when a device can run
  faster" was ticked; unticked, the icon went blue at once and the menu
  line went; ticked again, red at once, no tooltip change asked for. (98
  SE without a storage driver raises Add New Hardware for every stick;
  cancelled each time, the notice unaffected: the count comes from the
  connection, not the disk.)
- **ME.** The balloon "A USB device can run faster / USB Mass Storage
  Device runs at Full Speed (12 Mbit/s) behind a Full Speed hub; it can
  run at High Speed (480 Mbit/s). Click here for what to try.", and on a
  replug, with ME's disk driver bound, the name with its drive letter
  ("... (H:) runs at ..."); clicking it opened the list window, two
  entries with their drive letters. Moves, removal and the setting as on
  98 SE. A stick on the USB 2.0-only root port: not counted.
- **2000.** (The gold image's controller is disabled, Code 22, and the
  installer's update left it so, exit 2: an image fact, not this task's;
  the clean image was used.) The balloon "QEMU QEMU HARDDISK USB Device
  (F:) runs at Full Speed (12 Mbit/s) behind a Full Speed hub; it can run
  at High Speed (480 Mbit/s). Click here for what to try."; two, one moved,
  the other removed, the setting off and on, and the USB 2.0-only root
  port as above.
- **XP, the computer's setting off.** `INSTALL /console /yes /nofaster`:
  "off for this computer"; `HKLM\Software\xhci98` `TrayFasterNotice` DWORD
  0, no user value. A stick behind the hub, mounted: no balloon, the icon
  blue, the right-click item unticked; the report still listed it under
  "Can run faster" (a reading, not a notice). The user ticked the item:
  red at once, no balloon; `HKCU\Software\xHCI98\XHCITR` `FasterNotice`
  DWORD 1 (the user's choice over the computer's). A second stick: XP's
  own "Found New Hardware" balloon, then ours, "USB device 46F4:0001 runs
  at Full Speed ..." (named by VID:PID: the refresh came before Windows
  named the disk); clicking it opened the list, F: and G:. One moved to
  SuperSpeed: 1; the hub removed: blue. The USB 2.0-only root port: not
  counted.
- **7 x86.** The icon sits in the notification area's overflow (7 hides a
  new icon). **On the first plug of a stick Windows had not seen, no
  balloon of ours appeared**: Windows' own "Your device is ready to use"
  balloon held the area for about 35 s, and nothing of ours followed it
  in the next 40 s, while the icon turned red; unplugged and plugged
  again (no Windows balloon then), ours appeared at once: "QEMU QEMU
  HARDDISK USB Device (F:) runs at Full Speed (12 Mbit/s) behind a Full
  Speed hub; ...". The menu "2 USB devices can run faster...", the list,
  the setting off and on, a move, the hub's removal and the USB 2.0-only
  root port as above. (`XHCITR /report C:\r.txt` from an unelevated
  prompt: "The report could not be written to C:\r.txt" - 7's root is not
  writable without elevation; `%TEMP%` worked.)
- **7 x64.** The same as 7 x86, the first plug's balloon lost behind
  Windows' own in the same way; on replugs the balloons for F: and G:, and
  the click opened the list. (The unsigned x64 build raised 7's Program
  Compatibility Assistant box, "Windows requires a digitally signed
  driver", during the install on an F8 start; the controller and root hub
  started all the same.)

### Findings

1. **7's first balloon is lost behind Windows' own** (7 x86 and 7 x64,
   two of two first plugs). XP showed ours after its "Found New Hardware"
   balloon; 7 did not, and section 7.6 item 4 gives each new key one
   balloon, so that device gets none while it stays. On 7 the icon is in
   the overflow, so the user then sees nothing. Not read further (whether
   the shell answers `NIN_BALLOONTIMEOUT` or refuses the request); a code
   task, not changed here.
2. **`xhciinst\README.TXT` (shipped as `tools\xhciinst\readme.txt`) did not
   list `/faster` and `/nofaster`**
   (the tool's own usage text and record 26 do). Added to both of its
   switch lists on this branch.

### Owed (as the first session left it; see the second session)

- A USB 3 stick at High Speed on a USB 2.0 port and moved to a USB 3 port,
  two of them with one moved: read in the second session, on a QEMU whose
  `usb-storage` declares 0x0210 at High Speed. The same behind a High
  Speed hub, and a SuperSpeedPlus device at 5 Gbit/s: moved to the owner's
  checks on real hardware (coordinator, 2026-10-23; QEMU models neither).
- Finding 1, 7's lost first balloon: task 43.66.
- The two-controller 8 or 8.1 guest (record 13 section 6.12.11, "A
  computer with both drivers"): moved to 43.31's bench.
- 7 x64's setting, the balloon click on 2000 and 7 x86, XP's default:
  read in the second session (2000's click: finding 3, open).

### A QEMU whose stick declares 0x0210 (second session, 2026-10-23 05:00 to 07:15)

**The emulator.** Upstream QEMU 11.1.0 (`qemu-11.1.0.tar.xz`, the release
tarball), built on development host A with the `usb4-emu` repository's
MSYS2 UCRT64 toolchain (used, not changed) into a folder of this task's
own, outside every other kit (`E:\xhci98-vm\p43-28\qemu-bcd`; configure
`--target-list=x86_64-softmmu`, no display, slirp, VNC or tools). One
device change, `hw/usb/dev-storage.c`:

```
 static const USBDescDevice desc_device_high = {
-    .bcdUSB                        = 0x0200,
+    .bcdUSB                        = 0x0210,
```

and one build-only change, `scripts/symlink-install-tree.py` returning at
once on Windows (it makes the `qemu-bundle` symbolic links, which need
Developer Mode there; the firmware is given with `-L` from the installed
QEMU 11.1.0's `share` instead). `qemu-system-x86_64.exe` SHA-256
`a67b019f766c99314a9d992afc2ce2b278187ff31d8b647a6b680f4d72212905`.
So `usb-storage` at High Speed declares 2.10, answers BOS with a USB 2.0
Extension and a SuperSpeed capability (`desc.c`, unchanged), and at
SuperSpeed declares 3.00 as before: a USB 3 stick, as section 9.6.1 of
USB 3.2 has it, on either kind of port.

**The machine.** `qemu-xhci,p2=5,p3=2`: QEMU ports 1 and 2 have a USB 3
half (a stick attaches at SuperSpeed, controller ports 1 and 2), QEMU
ports 3 to 5 are USB 2.0 only (controller ports 5 to 7); the tablet on
QEMU port 5. The same overlays and builds as the first session (98 SE and ME
the qemu flavour, 2000, XP, 7 x86 and 7 x64 the release flavour).

| | 98 SE | ME | 2000 | XP | 7 x86 | 7 x64 |
|---|---|---|---|---|---|---|
| a USB 3 stick on a USB 2.0 root port (High Speed) | counted: red | red | balloon, red | balloon, red | balloon, red | balloon, red |
| a second one on another USB 2.0 root port | 2 | 2 | 2 | 2 (list) | 2 | 2 (list) |
| one moved to a USB 3 port (SuperSpeed) | 1 | 1 | 1 | 1 | 1 | 1 |
| the other moved to a USB 3 port | blue | blue | blue | blue | blue | blue |

The counts are the left-click menu's first line ("2 USB devices can run
faster...", "1 USB device can run faster...") or, where marked, the list
window's entries. ME, 2000, XP and 7 x64 were read after the review of
the first tick asked for them, on the same overlays (2000 and XP 1 and 2
vCPUs; 7 x64 on an F8 start).

- **98 SE.** The report (`XHCITR /report`, to COM1) for the stick on
  controller port 5: "Speed: High Speed (480 Mbit/s); Port supports: USB
  1.1, USB 2.0; device SuperSpeed capable; Can run faster ... Runs at:
  High Speed (480 Mbit/s); Can run at: SuperSpeed (5 Gbit/s); Try: Connect
  it to a USB 3 port of this computer (often blue, or marked SS). If it is
  already in one, it fell back to USB 2.0 there: try another cable or
  another port."; `bcdUSB` 2.10. The tablet on port 7 (2.00): "device not
  SuperSpeed capable", not counted. The debug channel logged the BOS read
  (`hcd: BOS, bytes/capabilities=00001602`, 22 bytes, two capabilities)
  for the stick at High Speed. The list window carried both sticks, ports
  5 and 6, each "Runs at: High Speed ... Can run at: SuperSpeed".
- **2000, XP, 7 x64.** The balloon "... runs at High Speed (480 Mbit/s) on a
  USB 2.0 port; it can run at SuperSpeed (5 Gbit/s). Click here for what
  to try."; the list window, where opened, read each stick "Port 5" or
  "Port 6", "Runs at: High Speed", "Can run at: SuperSpeed", with the
  USB 2.0-port "Try" text. ME: the icon red, the menu's counts.
- **7 x86.** The balloon on the first plug (a stick and port Windows had
  seen in the first session, so no balloon of Windows' own): "QEMU QEMU
  HARDDISK USB Device (G:) runs at High Speed (480 Mbit/s) on a USB 2.0
  port; it can run at SuperSpeed (5 Gbit/s). Click here for what to
  try."; clicking it opened the list window with that entry. Two, one
  moved, the other moved: as on 98 SE.

**The legs the first session left** (QEMU 11.1.0 as installed):

- **7 x64's setting** (High Speed case, a stick behind `usb-hub`): the
  item ticked; unticked, the icon blue at once; ticked, red at once.
- **7 x86's balloon click**: read above (it opened the list).
- **XP's default**: both values deleted (`reg delete` of HKLM
  `TrayFasterNotice` and HKCU `FasterNotice`), XHCITR restarted: a stick
  behind the hub raised the balloon "QEMU QEMU HARDDISK USB Device (F:)
  runs at Full Speed (12 Mbit/s) behind a Full Speed hub; ..." - on with
  no value, as on the other five.
- **2000's balloon click**: twice, the balloon closed and **no window
  opened** (once with the desktop clear, once with Windows' "Unsafe
  Removal of Device" box up); the menu's line opens it on 2000. Finding 3.

**Finding 3.** On 2000 a click on the notice's balloon dismisses it
without opening the list window (ME, XP, 7 x86 and 7 x64 open it). Not
read further: whether 2000's shell sends `NIN_BALLOONUSERCLICK` (WM_USER +
5) to an icon set to version 3, or something else. A code task; the menu's
first line is the way to the list there meanwhile.

## 43.71 - A File Lost Across Hibernation on a Native Hot-Plug Tunnel Port

Task 43.71 of `roadmap-usb4.md`, read on 2026-10-23. The roadmap entry
carries the task; this section carries what each run did and what it
read.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

### 43.71 - hibernation with a tunnelled NVMe on 7 x86, q35

**Verdict: read; not release-blocking** (owner, 2026-10-23). The loss is
the release notes' standby and hibernation limitation, seen through
native PCIe hot-plug. It is not the emulator's: the lost write was never
issued to the device. The release notes now describe it. A second cause,
the connection manager's own, found in two of the runs, is the
post-release item RESUME.1.

**Machine and build.** The kit and package of "43.52 the legs" above: 3.2.0.0 at
`062e02d7` (x86 release TEST cut), usb4-emu at QEMU `46d96fb`, TCG, 2
vCPUs, q35, `usb4-generic` `host-model=amd` with its PCIe tunnel on the
q35 `usb4-tunnel-port`, the ASM2464 enclosure with a FAT volume (`E:`)
on Stor98. For runs 2 to 4 the `qemu` flavour of `xhcicm.sys`
(`4A09A485...`) was copied over the installed one, for its log. 7 x86
offers hibernate only here (`powercfg /a`: the display driver vetoes
standby), so every run used `shutdown /h`, then the emulator started
again with the enclosure attached. The emulator traced the tunnel slot
(`usb4_tp_*`) and the NVMe's writes, flushes and shutdown notification.
After each hibernate the NVMe image was read on the host (`qemu-img
convert`, the FAT root directory listed).

| Run | What was done | What was read |
|---|---|---|
| 43.52's leg d (2026-10-23) | Enclosure hot-plugged; `W5.BIN` written and compared; hibernate at once; resume | `E:` missing after the resume; after a replug `W5.BIN` absent, older files intact |
| 1 | Enclosure attached at boot; `W7.BIN` written and compared; hibernate at once | Before the hibernate: the 1 MiB of data and the FAT sectors (LBA `0x3F`, `0x40`, `0x140`) written. At the hibernate: no further write, one Flush, the shutdown notification. The root-directory sector (LBA `0x240`) was never written. In the image afterwards, `W7.BIN` has no directory entry. After the resume `E:` is missing. Driver log: `u4.preserved` after a `cm.start` that followed the boot instance's `cm.stop`, then `u4.resume.held` 1 and `cm.reenum.resumed` 0 |
| 2 | As run 1, `W8.BIN` | `W8.BIN`'s entry reached the disk before the hibernate (in the image). After the resume `E:` is missing. The same log sequence: the boot instance built the tunnel (`u4.pcie.up`, `u4.pcie.built` 1), `cm.stop` 2, a new start found it preserved (`u4.preserved=00010809`), and the resume was held |
| 4 | Booted without the enclosure; hot-plugged after the boot settled; `W10.BIN` written; 60 s waited; hibernate | `W10.BIN` on disk (in the image). The connection manager's resume succeeded (`cm.reenum.resumed` 1, the tunnel rebuilt, an endpoint inserted). `E:` is still missing. The emulator's trace: Windows' first slot writes at the resume read Presence Detect clear and turn the slot off (`Slot Control` `0x17FB`, `Slot Status` `0x0010`), before the tunnel was up. After the tunnel came up, it blinked the indicator and turned the slot off again (`0x16F9`, then `0x17F9`), so the endpoint was removed |

**Why the file is lost.** At the hibernate the file's directory entry
was still in Windows' file-system cache. Windows keeps that cache in the
hibernation file rather than writing it out; the NVMe got its Flush and
shutdown notification with no write of the directory sector before
them. When the volume is gone at the resume, Windows drops it. A file
whose entry reached the disk before the hibernate survives (runs 2 and
4), and the drive is still missing.

**Why the drive is missing.** Two causes, read separately:

- Native hot-plug (run 4, and every run). `pci.sys` drives the q35
  port's slot itself. At the resume it reads the slot empty and turns it
  off before the connection manager has the tunnel back. The resume hold
  of design record 21 section 6.3 retains the bridge's D0 only after
  `pci.sys` has finished with it. So it cannot stop that reading, and
  the drive is reported missing even when the connection manager's
  resume succeeds. On the i440FX emulator's SHPC bridge Windows does not
  drive the slot, and 43.45's hibernate resume passed there. This is
  expected with this design on any native hot-plug tunnel port, an
  integrated USB4 root port included, unless something brings the tunnel
  back before Windows resumes the port (a pre-boot connection manager).
  Inferred for real hardware from the mechanism; not read on metal.
- The connection manager's (runs 1 and 2). With the enclosure attached at
  boot, Windows stopped and started the NHI once after the boot, and the
  new instance adopted the tunnel the first one built as preserved. At
  the resume the domain had lost its configuration. Design record 22
  section 7.6 rebuilds only a port this driver built, so the resume
  failed and the hold's failure path reported the disk missing. This
  applies to the SHPC targets too. It is RESUME.1, after the release.

**Not run** (owner, 2026-10-23: not needed): the same leg with a
non-tunnelled NVMe and with Microsoft's `stornvme`. The trace shows the
lost write was never issued.

## 43.42s - the installer's simplification (2026-10-23)

Roadmap-usb4 task 43.42's simplification, the owner's rulings of
2026-10-22 and 2026-10-23 (design record 26 section 5.0, draft 35;
record 23 revision 37, decision D20), read in QEMU guests on development
host A, one guest at a time, headless, keys by QMP `send-key` and screens
by QMP `screendump`; no display client. Branch `p43-42s`.

### The 98 SE Insert Disk prompt, read first (09:15 to 10:30)

The vehicle: the `p43-46` leg cut (`3.2.0.0` at `062e02d7` merged into
`p43-46` at `ac4eca4c`; `xhci98-3.1.0.0.zip`, SHA-256
`b29f3eeb357bc4a826b5cf812a42f0d4f52efd60b616146f0ebfa0e4b45fd22d`),
each run an update over 43.5's `w98-hcd310` image (`3.1.0.0` on the
controller) with the choices left as they were: `XhciSilentInstall` 0,
the UAS box and the storage row ticked. A QEMU `usb-uas` with a
`scsi-hd` under it, plugged after a cold start.

- After the install, before any plug (observed): `SYSTEM32\DRIVERS`
  holds `XHCI98.SYS` alone; `INF\OTHER` holds the controller's INF copy
  alone; `DevicePath` is `C:\WINDOWS\INF;C:\XHCI98`.
- First plug, port 1 (observed): the Add New Hardware wizard names
  `C:\XHCI98\XHCIUAS.INF`, then "Insert Disk 'xhci98 xHCI Host
  Controller Disk'"; the copy dialog: "The file 'xhciuas.sys' ... cannot
  be found", Copy files from `C:\WIN98`; its details: source
  `C:\WIN98\xhciuas.sys`, destination
  `C:\WINDOWS\SYSTEM32\DRIVERS\xhciuas.sys`, error 2. `C:\WIN98` is
  `SourcePath` ("c:\win98\") under `HKLM\Software\Microsoft\Windows\
  CurrentVersion\Setup`, Windows' own setup source.
- After typing `C:\XHCI98` (observed): the devnode
  `Enum\USB\VID_46F4&PID_0003\27842-0000:00:03.0-1`, Driver
  `USB\0002`, upper filter `USBNTMAP.SYS`; `Class\USB\0002` InfPath
  `YEOKHE~2.INF` (`INF\OTHER\Yeo Kheng Mengxhciuas.inf`, byte for byte
  the package's), InfSection `Uas.Dev`; `SetupX\INF\OEM Name` maps it to
  `xhciuas.inf`. No record of an INF's source path anywhere under
  `CurrentVersion`.
- Two INF variants, each on a first install (observed): the plain
  `[SourceDisksNames]` as `1=%DiskName%,,,` with the tag file under
  `.x86`, and the disk numbered 99: both still prompt from `C:\WIN98`.
- `xhciuas.sys` put in `SYSTEM32\DRIVERS` by hand (the package's bytes,
  27,876 bytes, SHA-256 `fd68f787...`), nothing else changed, a fresh
  update (observed): the first plug raises the wizard, installs with no
  prompt, and the disk mounts as F:.
- A replug at another port (port 3 in the same session, port 2 after a
  cold start; observed): the wizard again, no prompt, the disk mounted.

The cause (inferred from those readings): installing from an INF found on
`DevicePath`, 98 SE's wizard looks for a file not yet at its destination
at Windows' own `SourcePath`, not in the INF's folder. The fix is the
installer's (record 26 section 5.0): with the UAS box ticked it puts
`xhciuas.sys` in `SYSTEM32\DRIVERS`.

### The legs (11:30 to 12:55)

The vehicle: the leg cut above with the installer's programs
(`SETUP.EXE`, `XHCIWIZ.EXE`, `XHCIINST.EXE`, `XHCIWZ64.EXE`,
`XHCIIN64.EXE`) replaced by this branch's, built by `xhciinst\build.cmd`
inside the gated build of `c6b9d125` (`XHCIWIZ.EXE` SHA-256
`a9bc0d9659e41904...`, `SETUP.EXE` `c2083115866e6220...`); the drivers
are `3.2.0.0`'s, which this branch does not change.

#### 98 SE, an update with the storage row (`w98-hcd310`): PASS

- Welcome (640x480): the title, the system line, the log line, the
  device list (the controller and the root hub, "current (this
  release)") and the plan's text, all inside the screen.
- Components: the USB drivers with the UAS box under them, the storage
  row (ticked), Thunderbolt unticked with Stor98 and its request-size box
  greyed under it, the USB Tree with its "can run faster" box, "Tools
  folder: C:\XHCI98", Settings... and Change folder...; no "Replace the
  stored build" row.
- The keyboard: Tab to Thunderbolt and Space ticks it and shows Stor98
  and the request-size box ticked; Tab and Space on Stor98 unticks it
  and greys the box under it; Space again ticks it, and the box under it
  with it; the request-size box unticked by the user stays unticked
  across two more Spaces on Stor98. Every tick changed on the screen at
  the key that changed it. So the 43.11 reading (Space "left it
  unticked") was Space unticking a box the Thunderbolt tick had ticked.
- Settings...: one tab, USB (Thunderbolt unticked), the port switch
  greyed and `XhciSilentInstall` "0 off (default)"; its Advanced lists
  `XhciInterruptMode`, `XhciPollFineTimer`, `XhciSelectiveSuspend`,
  `XhciForceBulkOnly`, `XhciFastPollFsLs`. Seen: the note box took the
  focus when the dialog opened, and Esc there did not close the dialog
  (fixed after the leg: the note is no tab stop).
- Summary, Install, Steps ("A UAS disk plugged in later opens Windows'
  New Hardware wizard (again at another port); Next until Finish installs
  it, no disk asked for."), Finish; the log: "Copied xhciuas.sys to
  C:\WINDOWS\SYSTEM32\DRIVERS\xhciuas.sys.", the file there the package's
  bytes (SHA-256 `fd68f78782a264bc...`, read from the disk image).
- After the restart, the first UAS plug (port 1): the wizard, location
  `C:\XHCI98\XHCIUAS.INF`, Next to "Windows has finished installing", **no
  Insert Disk prompt**, F: "Orico" in My Computer. A replug at port 3:
  the wizard again (a new devnode), Next to Finish, no prompt.

#### 98 SE with NUSB 3.3 installed (`w98-nusb`): PASS

- Components: the storage row shown and unticked.
- Change folder...: `C:\PROGRA~1\X Y Z` refused in the dialog ("each
  folder name 8 characters at most ... no spaces"), OK staying in it;
  Cancel kept `C:\XHCI98`.
- Install: exit 3; the log "Copied xhciuas.sys to
  C:\WINDOWS\SYSTEM32\DRIVERS\xhciuas.sys.".

#### ME (`me-310u4`): PASS

- Components: no storage row; the rows otherwise as on 98 SE.
- Thunderbolt ticked, Settings...: three tabs, USB, Thunderbolt and
  Stor98; the Thunderbolt tab's three rows ("not set (default 1)") and
  its note; its Advanced lists the connection manager's values,
  `XhciCmNoIrqStart` "now: not set (default 1)" and `Restart` greyed
  "(Windows 2000 to 10 only)". Not installed.

#### 2000 (`win2k-clean`, no driver on the controller): PASS

- Welcome: the controller "no driver", "install (no driver)".
- Components: no storage row, no "Replace the stored build" row; the
  request-size box greyed "(Windows 98 SE and ME only)". Not installed.

#### 7 SP1 32-bit at 120 DPI on 800x600 (43.11's `w7` overlay, `2.4.0.0` on the controller): PASS

- The window opens moved to the top of the work area and lies inside it
  whole, its frame above the taskbar (the 43.11 finding: about 635 by
  575, the frame below the buttons under the taskbar).
- Welcome: the two devices "xhci98 2.4.0.0", "update (an older
  release)", and the plan's text.
- Components: as on 2000, with "Replace the stored build of this
  package's drivers" shown greyed (no store trap on this image).
- Settings... with Thunderbolt unticked: the USB tab alone. Its Advanced:
  `XhciInterruptMode` "now: 2" set to 3 ("3 (to be written)"), OK; the
  Settings dialog's Cancel; opened again, Advanced reads "now: 2" again:
  Cancel restored the choices as they stood at the opening.
- Thunderbolt ticked, Settings...: three tabs, USB, Thunderbolt and
  Stor98, each whole at 120 DPI; the Thunderbolt tab's Advanced ends
  with `XhciCmBridgeNumbering` and `XhciCmNoIrqStart` greyed "(Windows 98
  SE and ME only)" and `Restart` "now: not set (default 1)" with its
  note; Stor98's tab its two rows. Not installed.

#### XP x64 SP2, the 64-bit package (`winxp64`, no driver on the controller): PASS

The x64 tree: the owner-signed `release-x64` directory of the integration
build `5ad53f7d` (`E:\xhci98-scratch\signed-amd64-5ad53f7d`, the drivers
of `3.2.0.0`'s line, a `.cat` beside each INF) beside the x86 tree above,
with this branch's `XHCIWZ64.EXE` (SHA-256 `173ecfe17e99ecc7...`, built
at `4a465d83`); XP x64 enforces no signature.

- `xhcicm.cat` deleted from the tree: the Welcome page ends with
  "REFUSED: this 64-bit package carries the Thunderbolt driver without
  its catalog, xhcicm.cat: restore the complete package, with its .cat
  files, and run the installer again. Nothing was changed.", Next
  disabled. Seen: the device list showed its "No device of this package
  is present now" row under the refusal (fixed after the leg, `ac009c55`:
  a refusal shows its text alone).
- The complete tree: Welcome lists the controller "no driver", "install
  (no driver)"; Components, Summary ("Install and stage the package
  now"; Thunderbolt and Stor98 left out); Install: Windows' "has not
  passed Windows Logo testing" twice (the controller, the root hub's
  Found New Hardware wizard), Continue Anyway each time; Finish "Changes
  were made. Please restart Windows now.", "Staged xhci98.inf as
  oem0.inf. Staged xhciuas.inf as oem1.inf.", "Installing Universal
  Serial Bus (USB) Controller ... done."

#### Found on the legs, fixed after them

- The Settings dialog's note box took the focus at the opening and
  swallowed Esc (98 SE, 7): `4a465d83` makes it no tab stop.
- The Summary, Steps and Finish pages left a band empty under their
  text at the sheet's new 210 units: `4a465d83` grows their templates.
- A refusal's Welcome page showed the device list (XP x64): `ac009c55`.

These three were read in the window's self-test and the code review, not
again in a guest.

#### Guests

All on QMP ports 49080 to 49088, one at a time, 1 vCPU (98 SE, ME, 2000)
or 2 (7, XP x64), and all closed: 83716, 71232 (98 SE s1), 76272 (98 SE
with NUSB), 74216 (ME), 81112 (2000), 83296 (7), 78672 (XP x64); the
Insert Disk reading's 6036, 72536, 65796, 65632, 52680, 67296, 74712,
68900.

## 43.5 - the storage drivers on 98 SE in QEMU, and 43.17's VM legs

Roadmap-usb4 tasks 43.5 and 43.17, read on branch `p43-5` (off
`integ6b` at `5ad53f7d`, with the coordinator's packager fix `79fc4835`
cherry-picked as `86febb43`). Runtime, development host A, QEMU 11.1.0
(scoop), TCG, one guest at a time, 2026-10-22 23:00 to 2026-10-23.
The host was loaded throughout (other agents' guests and builds, CPU
mostly above 90 %): no timing below is a measurement.

### The vehicle

- **The build.** `build-driver.cmd all` passed at `5ad53f7d`
  (`xhciinst\build.cmd` and `xhcimark\build.cmd` first). The first
  `all -amd64` failed one packager self-test ("Test-PackageCatalog
  refused the members MakeCat wrote ... 'x.sys'"), the second build in a
  tree with x86 outputs, which `79fc4835` fixes; `all -amd64` passed at
  `86febb43`.
- **The cuts** (`make-release.ps1 -Arch x86 -Flavor release
  -SkipQualtool -TestLabel`, x86 only: the x64 half needs the test-signing
  key, which this run does not have):
  with the pack (`-DriverPackRoot` = `usb-wdm-nonfree` at `6d5ac49`),
  `out\p435up\xhci98-3.1.0.0.zip` SHA-256 `c8cdea46...6d796d4`; with
  `-NoDriverPack`, `out\p435np\xhci98-3.1.0.0.zip` SHA-256
  `aa6dfc45...440e719`. Both still read `3.1.0.0` (the version is not
  bumped), so `releases\3.2.0.0\` itself could not be read; see below.
  The pack's seven in the cut are byte-identical to the pack checkout.
- **The image.** `E:\xhci98-vm\p43-5\base\w98se-pristine.qcow2` (SHA-256
  `d5479ea5...ef09`, read-only), built 2026-10-20 from `D:\isos\Win98SE.iso`
  in this kit (`RECIPE.txt`: fdisk, format, `C:\WIN98` from the CD,
  `setup /p j`), never with NUSB on it. It was re-hashed before use, not
  rebuilt: the owner typed its product key, which this run does not have.
  The data-path legs use `base\w98-cap6.qcow2`, that image with only
  `SYSTEM.INI [vcache] MaxFileCache=16384` (read back from the frozen
  image). Guest: `pc`, `pentium3,-apic`, 1 vCPU, 256 MB, std VGA, the
  `qemu-xhci` with four USB 2 and four USB 3 ports, no USB device at boot.
- **The four files**, read off the pristine image again
  (`scripts\verinfo.py`): `IOS.VXD` 4.10.0.2222, 69,570 bytes,
  `bb133fb8...a70652`; `DISKTSD.VXD` 4.10.0.2222, 18,809 bytes,
  `180ca337...b44caf`; `NTMAP.SYS` and `NTMAPHLP.PDR` not on the image
  (in the CD's CAB set only, 4.10.0.2222 each); `NTMAP.INF` in
  `WINDOWS\INF` (`d7fc534e...f54183`). NUSB 3.3 lays its own:
  `IOS.VXD` 69,594 bytes `e34eee8d...`, `DISKTSD.VXD` 18,663 bytes
  `0239e574...`, `NTMAP.SYS` 8,816 bytes `3e94cb12...`, `NTMAPHLP.PDR`
  9,849 bytes `047393c8...`.
- **Large file**: `BIG.BIN`, 41,943,040 bytes (40 MiB, `c3cf57c9...6cca`),
  2.5 times the 16 MiB cache cap, copied D: to the disk, `fc /b`, the disk
  to C:, `fc /b`; the results also typed to COM1 and read from the serial
  log. Short copies: `SMALL.BIN`, 512 KiB.

### 43.5: the legs (kit `E:\xhci98-vm\p43-5`, its `RUN-NOTES.md` and `legs\r6\`)

**Verdict: not ticked.** The data path, the hot-plug, the unplug, Safely
Remove, the install with no device, the pre-states, the NUSB legs, the
INF partial states and the `-NoDriverPack` legs pass on this cut. Open:
the first plug still raises a wizard unless `XhciSilentInstall` is on
(the owner's 43.42 decision: silent install off by default), and a UAS
disk raises its wizard and an Insert Disk prompt even with it on; ME and
2000 show the storage row; an uninstall that finishes after a failed
removal leaves `USBNTMAP.INF` in `C:\WINDOWS\INF` (F1); a partial
installation is not "reported incomplete"; QEMU has no High Speed hub;
`releases\3.2.0.0\` cannot be read on a cut that still says `3.1.0.0`.

#### Data path (the fresh image with the cache cap; the row ticked by default)

| Leg | Result |
|---|---|
| Install with no storage device attached (window, defaults) | Row ticked by default (`pack 1, default 1`); the five binaries copied with the pack's hashes, `USBNTMAP.INF` then `USBSTOR.INF` placed in `C:\WINDOWS\INF` (`Placed\...`), SilentClasses `XHCI98S1`-`S3` (class 08, subclass 02/05/06, protocol 50) added, DevicePath `C:\WINDOWS\INF;C:\XHCI98`; Finish note "Windows' wizard still opens for each new USB disk unless the silent install (XhciSilentInstall) is turned on in the USB settings"; exit 3. The controller's wizard after the restart: Insert Disk, `C:\XHCI98` typed at "Copy files from", done. |
| First plug, SuperSpeed root port (`info usb` 5000 Mb/s), silent off (the default) | Add New Hardware Wizard "USB Mass Storage Device", location `C:\WINDOWS\INF\USBSTOR.INF`, **no file prompt**, no restart; F: after Finish. As the owner decided for 43.42, not "no wizard". |
| 40 MiB round trip, F: (SS root port) | **PASS**, "no differences" both ways |
| Second stick, SS root port 2 | wizard again (silent off), no prompt; G: |
| Two disks at once: the 40 MiB round trip on F: and G: in two DOS boxes at the same time | **PASS**, all four compares "no differences" (the G: box's copy of its result to COM1 failed, "Write fault error writing device COM1", while the F: box held the port: a harness clash; its compares passed on screen) |
| Safely Remove from XHCITR, G: | **PASS**: "This stops USB Mass Storage Device and everything on it: Disk drive QEMU QEMU HARDDISK, volume G:", then "Windows has stopped QEMU QEMU HARDDISK (G:). You can now unplug the device." |
| Unplug of the stopped G: and surprise unplug of the idle F:, replug both on their ports | **PASS**: no message, no wizard, both started (XHCITR `0x19886ACE (started)`); the 512 KiB round trip on both "no differences" |
| `INSTALL /console /eject`, no Thunderbolt or USB4 device | **PASS** against the amended wording: "No Thunderbolt or USB4 controller driven by this package was found. There is nothing to eject. For a USB drive, use Safely Remove in the xHCI98 USB Tree.", exit 1 |
| `XhciSilentInstall` 1 (`INSTALL /console /yes /set XhciSilentInstall=1`: "The SilentClasses list of SILENT.REG is in place (6 values)", exit 3), shut down, cold start, a new stick on a new port 3 | **no wizard**, F:, 512 KiB round trip "no differences" |
| `usb-uas` at a SS root port, silent on | wizard "xHCI98 USB Attached SCSI Storage" (`C:\XHCI98\XHCIUAS.INF`) **and** Insert Disk for 'xhci98 xHCI Host Controller Disk' (`xhciuas.sys`), `C:\XHCI98` typed; G: "Orico"; 40 MiB round trip **PASS**. The 43.42 retake's finding 3, unchanged: the silent install does not cover `xhciuas.inf`. |
| High Speed (`-Hs`, 480 Mb/s), new stick at a root port, silent on | no wizard; 40 MiB round trip **PASS** |
| Behind QEMU's `usb-hub` (12 Mb/s: QEMU's hub is Full Speed), new stick at port 2.1, silent on | no wizard (for the hub either); 40 MiB round trip **PASS**, at Full Speed |
| `usb-uas` behind a hub | not testable in QEMU (read 2026-10-21 in this kit: "speed mismatch ... full speed", nothing reaches the guest): a real check |

#### Installer logic (98 SE overlays over `w98-hcd310`: the fresh image with the published `3.1.0.0` installed)

| Leg | Result |
|---|---|
| Uninstall after the legs above (two sticks, a hub stick, an HS stick, a silent stick, a UAS disk) | exit 3: each stick's unit and device, the UAS disk, root hub and controller removed; `USBSTOR.INF`, `USBNTMAP.INF` and their setup-engine records deleted, the five binaries deleted, SilentClasses `XHCI98S1`-`S3` and the machine-wide `XhciSilentInstall` removed. On the disk afterwards: none of the seven, no `USBSTOR`/`USBNTMAP` INF anywhere, `xhci98.sys` gone, `IOS.VXD` and `DISKTSD.VXD` the CD's. **PASS** |
| ... then the HCD from the `-NoDriverPack` cut (`INSTALL /console /yes`, exit 3; the controller's wizard) | storage row not offered. Stick P435S1 (installed before the uninstall): wizard "QEMU USB HARDDRIVE", "Windows was unable to locate a driver", Device Manager **Code 28**; the UAS disk: wizard and Insert Disk (`xhciuas.sys`), then "xHCI98 USB Attached SCSI Storage" **Code 2** ("The NTKERN.VXD device loader(s) for this device could not load the device driver"), after a restart too. **PASS** |
| NUSB 3.3 installed (`w98-nusb`), row default (`/yes`) | `USB20U 1`: default unticked, nothing of the seven copied; HCD updated, exit 3; after the restart a new stick raises NUSB's own wizard (as NUSB alone does) and works (512 KiB round trip **PASS**) |
| ... then ticked (`/storage`) | "Every device is current and no setting changes", "No changes were made", exit 0 |
| ... then the uninstall | exit 3, the HCD removed; the seven and NUSB's `IOS.VXD`, `DISKTSD.VXD`, `NTMAP.SYS`, `NTMAPHLP.PDR` (11 files) **byte-identical** to the image before the leg. **PASS** |
| NUSB 3.3 installed after the pack (`w98-hcd310`, the cut with the row ticked, then `nusb33e.exe`, its licence accepted under the owner's 2026-10-20 permission for 43.5) | the seven unchanged (the pack's bytes), NUSB's four IOS files added |
| ... then an update (`/yes`) | `USB20U 1`: the row unticked (NUSB installed wins); DevicePath's storage copy withdrawn; "USBSTOR.INF stays: NUSB is installed (Uninstall\USB20U)", "USBNTMAP.INF stays while USBSTOR.INF is in the INF directory"; exit 3; every NUSB file unchanged |
| ... then this package's uninstall | each binary "kept: NUSB is installed (Uninstall\USB20U), and it lays down the same bytes"; SilentClasses removed; exit 3; every file unchanged |
| ... then NUSB's own uninstall (`Uninstall\USB20U`: `RUNDLL32.EXE ADVPACK.DLL,LaunchINFSection _USB2UN.INF,UNINSTALL`) | removes NUSB's USB 2.0 stack (`USBPORT.SYS`, `USBHUB20.SYS`, `USBEHCI.SYS`, `USB2.INF`, `_USB2UN.INF`) and nothing of the seven or the four IOS files: NUSB's uninstall never removes its storage files |
| Some of the seven (`w98-partbin`: the five binaries, no INF), default | `case 2`: default unticked, nothing copied, exit 3; first plug "unable to locate a driver" (nothing published); the files kept |
| ... then ticked | nothing copied over the five (same bytes), the two INFs placed, exit 3; a new stick's wizard finds `C:\WINDOWS\INF\USBSTOR.INF`, no prompt; round trip **PASS** |
| `w98-partsome` (`USBSTOR.SYS`, `USBNTMAP.SYS`), ticked | the three missing copied, the two kept (same bytes), both INFs placed, exit 3 |
| Older `USBSTOR.SYS` (4.10.2222.0), newer (4.90.3001.0), same version other bytes; ticked | each `case 3`: "A file found is not this download's, so nothing is copied over or beside it: the storage install is not complete", exit 4; the file kept through the install and a later first plug (no driver: the pack withheld), `fb9663e7...`, `c6343117...`, `5e860d31...` unchanged |
| A stick at "Unknown Device" before the run (`w98-unknown`), default | row ticked; the helper installed the stick from the pack: "The setup engine's copy `C:\WINDOWS\INF\OTHER\MicrosoftUSBSTOR.INF` of USBSTOR.INF is recorded as this installer's", the unit "USB Disk" recorded; exit 3; after the restart, F: round trip **PASS**. The uninstall: that copy, the placed pair and the five deleted, exit 3; nothing of the seven left. **PASS** |
| `USBSTOR.INF` alone in `C:\WINDOWS\INF` before the run, default | the hazard said ("USBSTOR.INF is in the INF directory without USBNTMAP.INF: Windows' own ..."), row ticked, `USBNTMAP.INF` placed beside it, exit 3: the pair whole |
| `USBSTOR.INF` alone in `INF\OTHER` (`MicrosoftUSBSTOR.INF`), `/nostorage` | the hazard said; at the end "USBSTOR.INF is still in the INF directory without USBNTMAP.INF. Plug in no new USB disk until both or neither are there", exit 4: a step left; no stick plugged |
| A placement cut between the two INFs (a file `C:\WINDOWS\INF\USBSTOR.TMP` made first) | "Copied USBNTMAP.INF ...", "USBSTOR.TMP could not be made (error 80): USBSTOR.INF is not copied", "the one this installer placed is taken out again", "Deleted C:\WINDOWS\INF\USBNTMAP.INF"; the pair stays whole in `C:\XHCI98` on DevicePath; "the drivers' INFs could not all be put in the INF directory, so Windows may open its New Hardware wizard ... it finds the driver in C:\XHCI98"; exit 4. **PASS** (the pair kept out of the INF directory, a step left) |
| A package copy cut between the two INFs | not provoked (the copy is staged and put in place whole; no way to cut it from inside the guest was found) |
| An uninstall that cannot remove `USBSTOR.INF` (a DOS box holding it open with `more <`) | "C:\WINDOWS\INF\USBSTOR.INF could not be deleted (error 5): restart Windows, then run the uninstall again", "USBNTMAP.INF stays: USBSTOR.INF is still in the INF directory" (the pair kept whole), the five deleted, "STOPPED", the Add/Remove entry kept "(incomplete: run again)", exit 4. After the restart, the uninstall again: "Deleted C:\WINDOWS\INF\USBSTOR.INF", exit 3, the entry removed - **but `C:\WINDOWS\INF\USBNTMAP.INF` is left** (F1) |
| A partial installation, binaries without their INFs | the same-bytes case (above: unticked by default; ticked, the INFs placed, exit 3), not "reported incomplete" as 43.5 reads; only a file of other bytes gives "not complete", exit 4. The 43.42 retake's finding 4, unchanged. |
| The hand install of the release notes (all seven in one folder) | read 2026-10-22 in `E:\xhci98-vm\p43-42k` on the same image (`w98-hcd310`), legs h1 and h2: without `USBNTMAP.INF` in `C:\WINDOWS\INF` first, stock `NTMAP.INF` takes the unit and Windows stops ("Invalid VxD dynamic link call from NTMAPHLP"); with it first, "USB Disk", F: readable. Not retaken (no installer involved). |
| ME and 2000 | **the storage row is shown** on the Components page, unticked and tickable ("USB storage drivers for Windows 98 SE (NUSB 3.3's)"); ticked there, it only copies the files to the tools folder (`inst_eng_stor_state`, `storTools`). 43.5 asks "never offered"; the owner's simplification (2026-10-22: the NUSB row on 98 SE only) is queued for `p43-42s`. 7 not booted: the same code path. |
| The upload set and `releases\` | `out\p435up\upload-3.1.0.0\NUSB33\` holds `NOTICE.md`, `SHA256.TXT` and `W98SE\` with the seven (the pack's bytes); `out\p435rel\3.1.0.0\` holds `drvpack.txt` only ("Included: yes", revision `6d5ac49a`, the nine hashes) and none of the files; the `-NoDriverPack` upload set holds none ("Included: no"). `releases\3.2.0.0\` itself: not readable until the version is bumped. |

#### Findings

- **F1 (defect, the uninstall).** `stor_un_rest` (`xhciinst\inst_eng.c`,
  the `"%s stays: USBSTOR.INF is still in the INF directory"` branch)
  deletes `USBNTMAP.INF`'s `Placed` record while the file stays, also
  when the `USBSTOR.INF` still there is this installer's own copy whose
  deletion just failed. The rerun after the restart deletes
  `USBSTOR.INF` but no longer knows `USBNTMAP.INF`, which is left in
  `C:\WINDOWS\INF` with nothing to remove it: harmless alone, but 43.5
  asks for none of the INF copies left. Proposed: keep the record (and
  set `left`) when the `USBSTOR.INF` still present is one of this run's
  failed deletions; forget it only for a foreign `USBSTOR.INF` (leg i4
  of 2026-10-22).
- **F2 (text).** On an update with NUSB installed (row unticked) and on
  an install from a `-NoDriverPack` download, the Steps text still ends
  "On Windows 98 SE any disk, UAS included, needs the USB storage
  drivers: the installer's row for them, or NUSB's mass-storage
  component." With NUSB installed storage is covered; a download without
  the pack offers no row.
- **F3 (text, wrapping).** Two console and log texts break badly: "USB
  storage: Windows' wizard still opens for each new USB disk unless the"
  / "  silent" / "install (XhciSilentInstall) is turned on in the USB
  settings." and "Stor98 3.1.0.0, the bundled NVMe driver: left out, it
  comes with the" / "  Thunderbolt 3 /" / "USB4 support."
- **Known, unchanged:** the UAS wizard and prompt under the silent
  install (43.42 retake finding 3); the storage row on ME and 2000; the
  "reported incomplete" wording for binaries without their INFs.

### 43.17: the installer on XP x64

`vm\winxp64.img` (XP Professional x64 5.2.3790 SP2, no xhci98) on an
overlay, `qemu64`, 2 vCPUs, 1536 MB. The package: the x86 cut's top
level (`INSTALL.BAT`, `SETUP.EXE`, `tools\`) with `release-x64\` from
`make-package.ps1 -Arch amd64 -Flavor release` of the gated amd64 build
at `86febb43` (unsigned: a test-signed x64 cut needs the key; XP x64
does not check signatures).

- `SETUP.EXE` (the 32-bit window) found "Windows XP x64 (5.2.3790
  Service Pack 2), x64", package `e:\pkg`, directory `release-x64`.
  Components: the storage row shown unticked (as on 2000 and ME). The
  install: "Copied 5 files to C:\WINDOWS\XHCI98\975AB27D", "Staged
  xhci98.inf as oem0.inf", XP's unsigned-driver question (Continue
  Anyway, three times), the root hub's Found New Hardware wizard (No,
  not this time; install automatically; Continue Anyway; Finish);
  Finish "Changes were made. Please restart Windows now."
- After the restart: a stick at a SS root port installed with no
  wizard, F: "ORICO"; a 1 MiB file copied off and back and `fc /b` "no
  differences". `INSTALL /console /list`: every component current,
  "XHCITR64.EXE ... is current at C:\XHCI98\XHCITR64.EXE; it starts at
  login", exit 0.
- `INSTALL /console /uninstall /yes`: root hub and controller removed,
  `oem0.inf` and `oem1.inf` removed, the service and `xhci98.sys`
  deleted, `C:\WINDOWS\XHCI98\975AB27D`, `C:\XHCI98` and the Add/Remove
  entry removed, exit 3. **PASS.**

### 43.17: 42-A.14 and U.11 on ME, U.11 on 98 SE

The `usb4-emu` QEMU of `p43-18g3` (build `660f085`, SHA-256
`9dfdcf9b...908b`, copied into this kit's `bin\`), i440FX, TCG, 1 vCPU,
256 MB, the `usb4-generic` profile: `usb4-host` at `00:04`, tunnel
bridges `tb0` at `00:05` (port 1) and `tb1` at `00:06` (port 2), ASM2464
NVMe enclosures `enc0` and `enc1` over the FAT image of `vm\u4-asm`
(`scripts\launch-u4.ps1`, `scripts\u11.ps1`). The cut above, release
flavour (no stage-code log, so `cm.nvlink` lines are not read: what is
read is the USB Tree and the file compares).

- **ME** (`me-310u4`: 3.1.0.0 with the Thunderbolt support and Stor98,
  from `p43-18g3`): `INSTALL /console /yes /thunderbolt /stor98` from the
  cut, exit 4 (an old "PCI Universal Serial Bus" entry recorded, not
  running: an old devnode, "needs nothing"); cold start.
- **42-A.14 on ME: passes.** `enc0` plugged: E: "ORICO", no wizard. The
  USB Tree's PCI page of the host router ("PCI configuration, read
  through xhcicm.sys"): the NHI `00:04.0` `1B36:00F4`, class `0C0340`;
  tunnel port `00:05.0` (SHPC bridge, buses 01-01) with "Functions below
  this port: PCI 01:01.0" `1B36:0010`, class `010802`, pin INTA line 10,
  its 64-bit memory BAR, PCI Express v2 (Gen 1 x1) and power-management
  capabilities; tunnel port `00:06.0` (no device), "Functions below this
  port: Not read on this system."; "68 configuration reads, 0 refused".
  A bridge chain hot-added behind port 1 (`usb4-asm-pci-usp` at `01:03`,
  `pci-bridge-seat`, `nvme`) was not listed until the next tunnel-up:
  after `enc0` was unplugged and plugged again, `xhcicm.sys` had numbered
  the chain (QEMU's `info pci`: `tb0` buses 1-3, the upstream port 2-3,
  the downstream port 3-3, `tb1` moved to bus 4), ME raised two "PCI
  standard PCI-to-PCI bridge" wizards (Next to Finish, both completed),
  and the page listed `01:01.0` (the enclosure's NVMe), `01:03.0`
  `1B21:2421` (the switch's upstream port), `02:01.0` `1B36:000A` (its
  downstream port) and `03:01.0` `1B36:0010` (the chain's NVMe), "135
  configuration reads, 0 refused". ME's wizards completed where 98 SE's
  stalled in run 42.
- **U.11 on ME: passes.** The chain removed, `enc1` plugged on port 2:
  two Thunderbolt/USB4 devices, E: (port 1) and F: (port 2); a 512 KiB
  file written and compared both ways on each; `enc0` unplugged and
  plugged again: E: back, then both files re-compared "no differences".
- **U.11 on 98 SE: passes**, on `w98-hcd310` (the fresh image with
  `3.1.0.0`) with this topology. The first start raised the wizards for
  the new devices (bridges, host router: all walked); `INSTALL /console
  /yes /thunderbolt /stor98`: "xHCI98 USB4 Host Router: xhcicm date
  10-23-2026, started", exit 3; cold start. The first NVMe raised the
  Add New Hardware Wizard "Stor98 Storage Controller for NVMe SSDs"
  (`C:\XHCI98\STOR98.INF`) and a "Copy files from" prompt for
  `stor98.mpd` (defaulting to `C:\WIN98`; answered with the package's
  `release-x86`), the second NVMe its own wizard with no prompt; then E:
  and F: each written and compared, `enc0` unplugged and plugged again
  (E: back with no wizard) and both re-compared "no differences". The
  prompt is the same 98 SE pattern as the controller's and the UAS
  disk's (the INF taken through `INF\OTHER`, the source path lost); the
  Steps text named no Stor98 step, since no NVMe was present at the
  install.

### 43.17: not read in this run (owed)

- **The cut's full matrix on every target.** The release matrix of
  43.12 (`scripts\vm-matrix\release-matrix.ps1`) holds the 98 SE and XP
  legs only and needs starting points per guest; this run read the
  install, a stick and the uninstall on 98 SE (many times), ME (the
  update), XP x64 (above) and 2000 (the Components page only). XP, Vista,
  Vista x64, 7, 7 x64, 8, 8.1 and 10 not run; an x64 cut for Vista and
  later needs the test-signing key, which this run does not have.
- **The USB4 eject behind ACPI's bus filter on 2000 to Vista**: not run.
- **The TESTING.md rows of Stor98's audit fixes** ("Two drives in one ICM
  domain", "Quarantined block after a surprise removal", both "Not run
  yet"): they need Stor98's debug-port build and the `tb3-alpine-ridge`
  ICM topology with its stage codes; not run.

### The fixes and their retake (2026-10-23)

The owner's decisions of 2026-10-23, relayed by the coordinator: 43.5's
first-plug criterion amended (with `XhciSilentInstall` on, no wizard;
off, the default, a wizard without a file prompt; for `usb-uas` a wizard
without an Insert Disk prompt either way, the silent install not
covering `xhciuas.inf`, the prompt still seen being a defect left to the
installer simplification `p43-42s`); the binaries-without-INFs criterion
reworded to the same-bytes behaviour; the uninstall leftover and the two
texts fixed now.

- `d651d27d`: `inst_stor_late_action` (`inst_core.c`, host vectors in
  `test_inst`) - the uninstall keeps `USBNTMAP.INF`'s record when the
  `USBSTOR.INF` beside it is this installer's own copy kept by the same
  run (its deletion failed, or its users unread), and drops it only
  beside another's; the Steps page's storage advice (`stor_advice`) left
  out while NUSB is installed, and naming NUSB or "a download of this
  release with them" where the download has no storage drivers; the two
  texts rewrapped inside the console's width; record 26 and roadmap 43.5
  amended. `bfb218b4`: wording after review.
- Gated `build-driver.cmd all` and `all -amd64` both passed at
  `bfb218b4` (`test_inst` 2080 checks, 0 failures). Cut as above:
  `out\p435up2\xhci98-3.1.0.0.zip` SHA-256 `5347167d...5fce60`.
- **The retake on 98 SE** (`w98-hcd310`, the fixed cut): install, exit 3
  (the Finish note now reads "USB storage: Windows' wizard still opens
  for each new USB disk unless" / "the silent install (XhciSilentInstall)
  is turned on in the USB settings."); cold start; `USBSTOR.INF` held
  open with `more <`; the uninstall: "C:\WINDOWS\INF\USBSTOR.INF could
  not be deleted (error 5)", "USBNTMAP.INF stays: USBSTOR.INF is still in
  the INF directory", "STOPPED", the entry kept, exit 4; the restart; the
  uninstall again: "Deleted C:\WINDOWS\INF\USBSTOR.INF.", "Deleted
  C:\WINDOWS\INF\USBNTMAP.INF.", exit 3. On the disk afterwards: none of
  the seven and no `USBSTOR`/`USBNTMAP` INF anywhere. **Passes.**

43.5 stays unticked only for the UAS Insert Disk prompt (`p43-42s`), the
storage row on ME and 2000 (`p43-42s`), the package copy cut not
provoked, the UAS behind a High Speed hub (a real check, relayed to the
owner) and `releases\3.2.0.0\` (readable once the version is bumped).

## 43.59 Stor98 wins a tunnelled NVMe drive first plugged after the install (2026-10-22)

Roadmap-usb4 task 43.59, the owner's "Make Stor98 win" from 43.23's
question 2. Designed in record 21 section 7.5 (record 26 section 6.3
amended), branch `p43-59` off `3.2.0.0`. Development host A, QEMU under
TCG (usb4-emu `bin-2a893ea`, SHA-256 `6a1dfcc8...bd66`), `-machine pc`,
two virtual CPUs, 2048 MB, headless; the Windows 10 x86 guest (build
19041, test mode) on an overlay of its pristine image, with the
emulator's `tb3-alpine-ridge` NHI, its tunnel bridge `tb0` (SHPC
auto-enable), an internal `nvme` at 00:09 (serial `S98INT59`) and the
tunnelled drive plugged at run time by QMP (`usb4-device`, ASM2464
router, serial `USB4EMU59`, a 128 MB FAT volume). The host was heavily
loaded by other agents' guests throughout, so no time below is a
performance reading.

### Before the code: stornvme and Stor98 at a disconnect under our connection manager

The owner asked (2026-10-22), before Stor98 replaces `stornvme` at all,
whether Windows' own `stornvme` handles a disconnect properly when
xHCI98's connection manager is installed and reports the removal.
43.23's hang came from a vehicle with no connection manager. The guest
had the connection manager alone (`/cmonly`, x86 `release` of
`7fda1598`'s gated build), the tunnelled drive on `stornvme`; a
PowerShell loop copied a 4 MB file onto it (a line per copy), and the
enclosure was surprise-unplugged (QMP `device_del`) about 45 s in. Then
the same with the drive moved to Stor98 by the mark service.

| Run | Driver | The copy | Disk events | After | Re-plug |
|---|---|---|---|---|---|
| A | `stornvme` | copy 57 failed 2.2 s after the unplug ("The volume for a file has been externally altered") | `disk` 157 "Disk 4 has been surprise removed" at once; `fastfat` 50 and the Delayed Write popup (26) | Disk 4 and F: gone from DiskPart | Disk 4 and F: back |
| A2 | `stornvme` | copy 28 failed within 3 s ("Could not find a part of the path") | 157 at once, two `disk` 51; `fastfat` 141 and popups | gone | back |
| B | Stor98 | copy 66 failed about 1 s after | 157 at once | gone | back, Stor98 running |
| 1 to 5 (a later boot) | Stor98 | failed within 2 s of each unplug | 157 each | gone | back; after each re-plug 234 to 600 copies, and in runs 4 and 5 all four copies compared equal to the source (`fc /b`) |

No event 129 (reset) or 153 (retry) in any run. So with the connection
manager installed both drivers end a disconnect promptly and cleanly and
both come back at a re-plug; the reading gives no disconnect reason
for or against replacing `stornvme`.

One unexplained reading: after run B's re-plug (the first re-plug of
the drive on Stor98), the next copy loop's PowerShell died before its
first copy line with `System.AccessViolationException` (Application
events 1026 and 1000, `powershell.exe`, faulting module unknown,
0xC0000005, several threads at once), about 35 s before that run's
unplug and with no disk event near it. A control loop on the system
drive (600 copies) was clean, and the sequence repeated five times on
the next boot (runs 1 to 5, the last two with a data compare) did not
show it again. Not read further: it may be PowerShell, the guest or the
driver; a memory reading at a recurrence would decide.

### The switch

On a fresh overlay of the pristine image, with the two-bridge chain
(`-Chain`: an ASM USP at 01:03.0, a downstream port and an NVMe behind it,
serial `CHAIN59`), the package of the gated build at `1f49eedb` (x86
`release`), its `XHCIMARK.EXE` built from the branch as it then stood:
`6b5439d2` at step 1, `946569b6` at step 2, `b9b72306` from step 3, the
console install `/cmonly /yes /stor98` with nothing plugged, then a
restart. The installer staged Stor98 (`oem4.inf`), left the internal
drive ("not behind a Thunderbolt or USB4 tunnel") and wrote
`Parameters\Stor98Inf`.

| Step | What was done | Reading |
|---|---|---|
| 1 | Enclosure plugged (both drives appear at once) | The chain's drive (`6&3A7C4023&0&00001828`, two bridges deep) began its switch 83 s after the plug and ran Stor98 12 s later, `done`, under `Devices` as `switched` (its first attempt waited for "a Windows installation is pending"). The enclosure's own drive (`4&1D29C6E2&0&0828`): its stop refused 14 s after the plug, with nothing of ours holding it, then `enable` (owed) and at the next poll `failed`, final: a defect (below). Internal drive: "not below a verified tunnel port" at each start, never touched. The chain's disk came up offline in DiskPart (the same base image as the enclosure's, so the same disk signature: a vehicle artefact) |
| 2 | The flag fix; record deleted; restart; re-plug | Switched 7 s after the plug, Stor98 37 s later, `done` |
| 3 | The drive put back on `stornvme` (its devnode removed and rescanned, record deleted), a file opened on its volume, the service started | Stop refused at 00:31:31, :42 and :54 (`busy` each, two retries 11 to 12 s apart), then nothing more while the drive stayed; the file closed, still nothing (the same arrival) |
| 4 | Unplug and re-plug (a new arrival) | Switched 7 s after the plug, Stor98 29 s later, `done`; a copy to F: compared equal (`fc /b`) |
| 5 | Console uninstall (`/uninstall /yes`) | The service stopped and removed, both switched drives removed as this installer's Stor98 devices, `oem4.inf` and the `stor98` service removed; after a rescan both drives `stornvme`, started, with no restart |
| 6 | The tip's service (`4a846846`): install again (`/cmonly /yes /stor98`), restart, the service stopped, plug, a file opened on the enclosure drive's volume, the service started | The enclosure drive refused at 01:30:37, :50 and 01:31:01, `busy`, nothing more; the chain's drive switched between them (one switch a poll), `done`; the file closed, then unplug and re-plug: switched 24 s after the plug ("a Windows installation is pending" first), Stor98 6 s later, `done`; both under `Devices`; the internal drive `stornvme` throughout |

The defect step 1 found, in two parts. A refused `DICS_DISABLE` leaves
`DI_NEEDRESTART` in the element's install parameters, and the
`DICS_ENABLE` that follows read it as its own answer (`946569b6` clears
both restart flags before each call). That was not all: with the flags
cleared, step 3's first run showed the enable itself answering a restart
while the devnode ran on, started with no problem, so the record still
went `enable` and then `failed`. `b9b72306` takes such an enable as held
when the current profile's `CSCONFIGFLAG_DISABLED` reads clear
(`CM_Get_HW_Prof_FlagsA`), and the record is `busy`, as section 7.5's
failure table says; step 3's readings are that build's. Before it, any
drive whose stop was refused at the switch stayed on `stornvme` for
good; at step 1 nothing of ours held the volume (what did, Windows' own
work at a first arrival, was not read).

### Builds and review

The design was reviewed twice before its code (the second's findings,
the installer's pause across the window's two threads, ownership before
the final state and the recovery under the pause, are `3402f535`). The
code was then reviewed seven times: `5e38173c` (the uninstall reads every
record, the recovery a cursor), `1f49eedb` (the uninstall's device lookup
tells an error from absence), `9aca6fdc`, `c3816947`, `88ec6e1c` and
`6b5439d2` (the arrival bookkeeping: every candidate and every present
devnode keeps its entry, a full table gives up only a gone one, 256 of
them); the seventh found nothing. The leg fixes were reviewed until clean
(`946569b6` clean; `b9b72306` led to `4a1f8a1a`, the global flag, and
`4a846846`, the recovery's reading; the last review found nothing).

The gated builds, x86 `all` and amd64 `all`, passed at `1f49eedb` (the
package for steps 1 to 5) and again at `4a846846`: amd64's records clean
at `4a846846`; x86's marked dirty, as `4a846846` was committed while that
run was under way (the mark service alone; x86's own XHCIMARK self-test
ran on `4a846846`'s source).

### Owed

- 8.1 (no x86 medium) and every x64 leg, after `p43-36s`'s test-signed
  catalog;
- the USB4 profile (these legs ran the `tb3-alpine-ridge` ICM profile);
- the installer's pause read live (an install run while the service is
  switching);
- a recurrence of the PowerShell fault above, read with the guest's
  memory.

### The x64 legs: 10 x64 and 8.1 x64 with the owner-signed package (2026-10-23)

The owner-signed amd64 package of integ6b `5ad53f7d` (every `.sys` and
`.cat` signed with the project test certificate), its drivers unchanged,
with this branch's user-mode tools after merging that same commit
(`abb70446`: this branch's `src\` is then `5ad53f7d`'s, so the signed
binaries are this branch's drivers; 43.59 changes no driver). The
certificate was added to Root and TrustedPublisher in each guest; no
publisher prompt appeared. Fresh overlays of the pristine images, two
virtual CPUs, 2048 MB, the same devices as the x86 legs (`-Chain`); the
console install `/cmonly /yes /stor98` with nothing plugged, a restart,
then the mark service stopped before the plug, so that what Windows does
on its own is read first.

| | 10 x64 (19045) | 8.1 x64 (9600) |
|---|---|---|
| Driver Windows selects at the tunnelled drives' first plug, the service stopped | Stor98, both drives: equal rank `00FF2005` (both match `PCI\CC_010802`), Stor98 "Authenticode" against `stornvme` "Inbox", Stor98 `Selected`, `stornvme` `Outranked` (setupapi.dev.log) | Stor98, both drives, the same ranks and signer scores |
| An internal NVMe first plugged after the install (hot-added on the root bus) | Stor98 | Stor98 |
| Stor98 starts | No, on any of the three: problem status `0xC00000C2`; System event 4 from `stor98` ("internal error in its data structures", final status `0xC00000C2`) | Yes, on all three; a copy to the tunnelled drive compared equal |
| The internal drive present at the install | `stornvme`, untouched | `stornvme`, untouched |
| The mark service | Never needed: no drive was on `stornvme` below the tunnel | The same: nothing to switch |
| The uninstall | Leaves Stor98 on all three (none is in its `Devices` record; the internal one "not this package's to remove"), so on 10 x64 the drives stay failed | Leaves Stor98 on all three, running |

So with a trusted signature the design's premise is reversed: section
7.5 rests on `stornvme` outranking an uncatalogued Stor98, and that holds
only while Stor98 is unsigned (x86, read above). Signed and trusted, Stor98
wins every new NVMe drive on its own, tunnelled or internal, on both
systems (by its newer date, the ranks and signer classes being otherwise
as above), which breaks the owner's rule for internal drives, and on 10
x64 Stor98 x64 does not start at all in this vehicle. Not read: why
Stor98 x64 fails on 10 x64 (a debug build's stage codes would show it).

### The surprise-unplug check under `stornvme` and Stor98, 8.1 x64 and 10 x64 (2026-10-23)

The owner's request after the x64 legs: does Windows' own `stornvme`
handle a surprise unplug, and transfers, with this connection manager?
The owner-signed amd64 package of integ6b `5ad53f7d` (the connection
manager alone, its test certificate trusted in the guest), fresh
overlays, two virtual CPUs, 2048 MB, an 8 GB blank qcow2 as the tunnelled
drive (formatted NTFS as T:) behind the ASM2464 router, an internal
`nvme` on the root bus. Loads: `seq.ps1` (four files of 256 MB written
write-through in 4 MiB chunks, read back and compared by MD5, stalls over
2 s logged) and `rnd.ps1` (200 files of 4 KiB to 1 MiB a round, written
write-through, read and compared), each 10 minutes; `copy.ps1` (a 4 MB
file copied in a loop). The host was at 97 % CPU throughout (ten guests
of other agents), so no throughput below is a performance reading.

| Run | Driver | Reading |
|---|---|---|
| 8.1 x64, sequential 10 min | `stornvme` | 31 passes, 31.7 GB written and compared, 0 mismatches, 0 chunks over 2 s; 95 to 134 MB/s writing, 85 to 112 MB/s reading and comparing |
| 8.1 x64, random 10 min | `stornvme`, tunnelled | 7000 files, 3.5 GB, 0 mismatches; 8 writes over 2 s (2.0 to 11.6 s); no storage event |
| 8.1 x64, random 10 min, control | `stornvme`, the internal drive | 5000 files, 2.5 GB, 0 mismatches; 6 writes over 2 s (2.0 to 12.6 s) at the same files: the host, not the tunnel |
| 8.1 x64, surprise unplug during `copy.ps1` (11:27:40) | `stornvme` | disk event 157 for every disk on the controller 1 to 3 s after; `stornvme` 129 (reset) at +12 s and 11 (controller error) at +19 s, once each; the copy blocked, the process alive, for as long as it was watched (2 h 50 min); the controller's devnode present, status `0x01806002` (not started, no problem), never removed; `diskpart` wedged for 25 minutes and more, and a new PowerShell did not start; a re-plug 50 minutes on brought the tunnel up and changed nothing |
| the same, what our side did (XHCITR /report on the hung guest) | | `tbt.icm.device-gone`, `cm.action.tunnel-down=00000003`, `cm.reenum.downs` 1, `cm.reenum.calls` 2, `cm.nvlink.withheld`; the tunnel port listed no function below it; the emulator's `tunnel down (surprise)`, `awaiting_ack=1` as on 10 x86 |
| 8.1 x64, surprise unplug during `seq.ps1` | Stor98 (put on the drive by the mark service of `p43-59`) | the read in flight failed at once ("The device is not ready"), the script ended, nothing hung |
| 10 x64, surprise unplug during `seq.ps1` | `stornvme` 10.0.19041 | the read failed at once ("The device is not ready"); 157 at once, no 129; the controller "Disconnected" (removed) |
| 10 x64, surprise unplug during `seq.ps1` | Stor98 (the signed `5ad53f7d` binary, `DmaRemappingCompatible` deleted, as `p43-59s` installs it) | started on both controllers; the read failed at once ("A device which does not exist was specified"); nothing hung |

43.23's 8.1 x64 leg with no connection manager also blocked under
`stornvme`, but differently: Windows never learned of the removal (8.1
ignored the bridge's SHPC), the resets repeated every 35 s, and the copy
completed once the tunnel came back. With the connection manager Windows
learns of it, begins the surprise removal, and 8.1's `stornvme`
(6.3.9600.16421, the 8.1 image's own; whether a later 8.1 update changes
it is unread) never completes the write it holds. The owner's decision
(2026-10-23): on 8.1 a tunnelled drive gets Stor98 by default (record 21
section 7.5, branch `p43-59k`).

The two investigations before these readings, for the record: the
07:44 sequential run that overran was the test script (PowerShell 4
copied every `[byte[]]` argument of a function element by element, 18 s
for 4 MiB under the emulator), fixed by inlining; the some 500 "Online
0 B" disks DiskPart listed on 8.1 are 8.1's `stornvme` listing all 256
namespace ids QEMU's NVMe controller reports, on the internal controller
as on the tunnelled one (10 lists only namespace 1).

### The 8.1-only switch: the legs on 8.1 x64 and 10 x64 (2026-10-23)

The package of `p43-59k` at `450aba31` (gated builds x86 and amd64 clean
at that commit; review clean), amd64 `release`, unsigned from the
packager, its catalogs made on the host by the packager's own
`New-PackageCatalogFile` (WDK 7.1 MakeCat) and every `.sys` and `.cat`
signed in the guest with a throwaway certificate (8.1: WDK 7.1 MakeCert
and SignTool; 10: `New-SelfSignedCertificate`, `Set-AuthenticodeSignature`)
in Root and TrustedPublisher. So the INFs are the ones that ship; the
owner's signature comes with the cut's bench candidate. Fresh overlays,
two virtual CPUs, 2048 MB, the 128 MB FAT tunnelled drive (ASM2464), an
internal `nvme` on the root bus; both controllers share the hardware id
`PCI\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02`.

| Leg | Reading |
|---|---|
| 8.1 x64, first install (`/cmonly /yes`), the enclosure plugged | Stor98 included unasked ("/yes: Y"); the tunnelled drive's place unreadable before the connection manager ran, so left; after the restart and a re-plug the mark service switched it (20:09:02 to 20:09:29, `done`); the internal drive "not below a verified tunnel port" |
| 8.1 x64, an internal NVMe hot-added after the install | `stornvme` (the INF's compatible-only match below `stornvme`'s, with Stor98 signed and trusted) |
| 8.1 x64, surprise unplug during `copy.ps1` under Stor98 | the copy failed 2 s after the pull ("The volume for a file has been externally altered"), PowerShell ended: no hang |
| 8.1 x64, uninstall (`/uninstall /yes`) | the switched drive removed as this installer's Stor98 device, `oem3.inf` and the `stor98` service removed |
| 8.1 x64, an update with the connection manager in place and the tunnel up (`/nostor98` first, restart, plug; then `/yes`), the hardware id shared with the internal drive | "is shared with ..., which this run leaves; this run changes neither: the safe-removal mark service puts Stor98 on the tunnel drive alone once this run ends" (exit code 4); the service switched the tunnelled drive 18 s later (`done`), the internal one stayed `stornvme` |
| 10 x64, install with `/stor98` (opt-in) | no `Stor98Inf` written, no "plugged in later gets Stor98" line |
| 10 x64, a stale `Stor98Inf` written by hand, a restart, the drive plugged | "Stor98Inf names C:\Windows\INF\oem4.inf, and Windows is 10.0 build 19045, not 8.1; no drive is switched"; both controllers `stornvme`, with Stor98 staged, signed and trusted |

Not read: an 8.1 install where the hardware id is the tunnelled drive's
own (the installer's own per-drive install, case 3b, on 8.1; unchanged
from 3.2.0.0's code); the owner-signed package; a real 8.1 machine.

## 43.59s Stor98 x64 does not start on Windows 10 x64 with DmaRemappingCompatible (2026-10-23)

Found by 43.59's x64 legs (branch `p43-59`, its run notes there): with the
owner-signed amd64 package of integ6b `5ad53f7d` and the project test
certificate trusted, Stor98 did not start on Windows 10 x64 22H2 (19045)
on any NVMe controller, tunnelled or internal: Code 10, problem status
`0xC00000C2`, System event 4 from `stor98` ("internal error in its data
structures", final status `0xC00000C2`). The same package's Stor98 ran on
8.1 x64, and 10 x86's Stor98 (whose INF never wrote the value) runs.

Read on the same 10 x64 guest (QEMU under TCG, `-machine pc`, no IOMMU,
so no Kernel DMA Protection), Stor98 bound to an internal NVMe controller
hot-added on the root bus, failing as above:

| Step | Device |
|---|---|
| `Services\stor98\Parameters\DmaRemappingCompatible` deleted, `pnputil /restart-device` | started |
| the value written back as 1, `pnputil /restart-device` | Code 10 (`CM_PROB_FAILED_START`) |

Where it stops, from the debug-port build's stage codes (Stor98 amd64 of
`5ad53f7d`'s source with `STOR98_DBGPORT=1`, signed in the guest with a
throwaway certificate, the guest started with `usb4-debug-port`; codes
from `src/stor98/src/dbgport.h`), the same device restarted each way:

| Stage | Value 1 | Value deleted |
|---|---|---|
| `01` DriverEntry, `04` options, `cc`, `150`, `160`, `de`, `d4`, `ca` clock | as the right-hand column | the same |
| `02` HwFindAdapter (bus 0, slot 10) | `0000000a` | `0000000a` |
| `05` the BAR | `0004000d` | `0004000d` |
| `153` / `152` interrupt and polling decision | `000a000a` / `0b000a01` | `000b000b` / `0b000b01` |
| `cb` DMA negotiated | `00410100` | `00410100` |
| **`d2` the uncached extension** | **`FFFFFFFF`: none** | **`00000017`: 23 contiguous pages** |
| `03` HwFindAdapter's result | **2, `SP_RETURN_ERROR`** | 1, found |
| after it | nothing; Code 10 | `154`, `140`, `30`-`33` HwInitialize, `40`/`41`/`47` bring-up, `10` and `b5` I/O |

So with the value SCSIport gives Stor98 no uncached extension, the common
buffer every queue lives in, and Stor98 refuses the adapter; the opt-in
written from `3.2.0.0` (Stor98 `docs/PLAN.md`, "Kernel DMA Protection",
2026-10-22) is what stops it. The x64 INF writes no value
again and `check-stor98-inf.ps1` now refuses one; why 10's SCSIport
refuses a miniport that opts in is not read. Owed: the fixed package
signed (the owner) and Stor98 read starting on 10 x64 from it; on a
machine with Kernel DMA Protection on, a tunnelled drive under Stor98
waits for sign-in again, as before `3.2.0.0`.

## 43.48 - The Other USB Drivers: the Installer's Legs

2026-10-23, branch `p43-48b` after the batch 7 merge (`07294a20`), with
the three fixes these legs led to (the record adopted after a lost key,
the flushes, the Summary's folder). The package: the dry cut's download
3 (`make-release.ps1 -Arch x86 -TestLabel`, real pins, release flavour:
`xhci98-3.1.0.0-nonfree.zip`'s tree, `NONFREE\` from usb-wdm-nonfree
v1.0.0), the installer's executables rebuilt with the fixes. QEMU 11.1.0,
`qemu-xhci p2=4,p3=2`, headless, one guest at a time, kit
`E:\xhci98-vm\p43-48` (QMP 49010), overlays over the read-only bases.
Readings are runtime, through the window's pages, the console's text,
`regedit /e` and `reg query` exports and the record file, read over the
guest's serial port.

### Windows 98 SE (the gold image, 640 by 480)

- **The page.** "Other USB drivers" after Components, at 640 by 480:
  the free set's line in the row's place ("The free USB class drivers are
  not in any download of this release yet: see
  https://github.com/yeokm1/usb-wdm-free."), the non-free row with its
  sentence, its stage box greyed until the row is ticked ("Stages 9 of
  their INFs for this Windows."), its address line. Everything fits; no
  control is cut. Next with the row ticked showed the non-free warning,
  OK went on.
- **The Summary** first said "copied to \NONFREE": the engine's text took
  the tools folder before the run had set it. Fixed to take the window's
  chosen folder; then "copied to C:\XHCI98\NONFREE, its INFs for this
  Windows staged".
- **A copy cut short.** Power cut through QMP 20 s into "Copying
  Non-free driver pack": the record file held 249 whole entries (F, T, D
  each), no open line, no temporary file. The next run stopped the set
  with "its record file could not be made": the registry's
  `Other\NonFree` key had not survived the cut (98 SE writes its registry
  out later) and the file was there. Fixed: a record file whose whole
  first line is this set's header is taken as this installer's and its
  key made again. The run after the fix copied the rest (a second run
  token, 224 entries), 457 distinct files in the record. Fifteen files
  the first run had recorded D were absent at the second and were copied
  again, and three (in `rtl815x\wxp`) were present with other bytes at
  the uninstall; that the cut lost their writes is the likely reading,
  not taken further. Fixed for later runs: each copy is flushed before
  its D line, and `RecordMade` with `RegFlushKey`.
- **The stage.** `DevicePath` after the run:
  `C:\WINDOWS\INF;C:\XHCI98;` and the five 98 SE folders of `STAGE.TXT`
  (ax88772, ftdi, ft2232d, ch341, x360c), each recorded under
  `Other\NonFree\Staged` with the data `DevicePath`.
- **The Finish page** paragraph: the free set's line, then "Non-free
  driver pack (usb-wdm-nonfree): in C:\XHCI98\NONFREE, its INFs for this
  Windows staged. Up to date: https://github.com/yeokm1/usb-wdm-nonfree."
  and the seven folders for this Windows to point the wizard at.
- **A plug.** QEMU's `usb-serial` (FTDI 0403:6001; it enumerates only with
  a chardev that opens - a `null` chardev leaves it unaddressed) on a
  root port: "New Hardware Found", no wizard: Windows took FTDIBUS.INF
  from the staged folder. Its file copy asked for the "FTDI USB Drivers
  Disk", offering `D:\WIN98`, not the folder; with the folder typed the
  driver installed. Windows then stopped responding (not read: the
  vendor's 98 driver on this stack, or QEMU's serial model; the owner's
  rule of 2026-10-22, no checks of the non-free drivers themselves).
- **The uninstall** (the window, from `C:\XHCI98`): the five folders
  taken out of `DevicePath` one by one, the copies deleted, the folders
  removed, the key gone; three files of `rtl815x\wxp` left as "changed
  since this installer copied it" (the cut's survivors with other bytes),
  so `C:\XHCI98` stayed with them. Exit 3.

### Windows XP SP3

- **The console**: `INSTALL /console /yes /nonfreestage /nopause`: the
  non-free warning under "WARNING:", the run, Windows' unsigned-package
  questions answered for each staged INF; the paragraph at the end (the
  free line, the copied folder, "its INFs for this Windows staged", six
  folders and "and 1 more: see C:\XHCI98\NONFREE\STAGE.TXT"). Exit 3.
- `reg query`: `Other\NonFree` with `RecordMade` 1 and six `own` values,
  `oem2.inf` to `oem7.inf` (ax88772, ax88179, rtl815x, ch341, x360c,
  x360wc), each with the INF's hash.
- **The uninstall** (`INSTALL /console /uninstall`, REMOVE): "oem2.inf
  was removed from the driver store" to oem7, the copies removed,
  `C:\XHCI98` removed; afterwards no `Software\xhci98` key, no
  `C:\XHCI98`, no `oem*.inf` in `%windir%\inf`. Exit 3.

### A lost key, on the final installer

The adoption of a record file whose key was lost (record 26 section
6.7.5) was reworked after the legs above; it was read again on a fresh
overlay of each guest, from a dry cut of the final code. Each time: the
console install with `/yes /nonfreestage`, the set's key deleted by hand
(`reg delete` on XP, a `REGEDIT4` file with `[-...]` on 98 SE), then
`INSTALL /console /uninstall`.

- **XP SP3**: the same six `own` INFs staged; the uninstall said the
  record "was found with no registry key, and is taken as this
  installer's again", kept eight folders (the six staged and two whose
  INFs hold the same bytes, `rtl815x\wxp\WINXP\64` and
  `ch341\w98se-xp\WIN 1X`), one to a line, removed every other copy
  and the record, and left `oem2.inf` to `oem7.inf` in the store (whose
  they are is not known). No `Software\xhci98` key afterwards. Exit 3.
  A first run took four minutes over the store (each INF of the set
  hashed every INF of the store); the store is now hashed once, and the
  run takes under a minute.
- **98 SE**: five folders on `DevicePath`; after the key's deletion the
  uninstall kept the five entries and their folders ("whose the entry is
  is not known"), removed `C:\XHCI98` from `DevicePath`, every other
  copy, the record and `Software\xhci98`. Exit 3.

### Verdict

The page at 640 by 480, a copy and a stage on 98 SE and on XP, a staged
folder found at a plug on 98 SE, a copy cut short and resumed, a lost
key adopted, and the uninstall on both: read, with the fixes above. Not read: the free
set (no release qualifies), ME, 2000, 7 and the x64 systems, a plug on
XP (QEMU emulates none of the INFs staged there), the flush fix under
another power cut.

## 43.70 - The Elevated Eject Under Native PCIe Hot-Plug

Task 43.70 of `roadmap-usb4.md`, read on 2026-10-23. The roadmap entry
carries the task; this section carries what each leg did and what it
read.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

### 43.70 - `XHCIINST /eject` on 7 x86, q35

**Verdict: done** (2026-10-23). The elevated `XHCIINST /eject` closes the
PCIe tunnel on a native hot-plug tunnel port, three times out of three.
Before the change it left the tunnel up three times out of three
(`run-43-52.md`, leg b).

**Machine.** The kit of `run-43-52.md`: Windows 7 SP1 x86, q35, TCG, 2
vCPUs, usb4-emu at QEMU `46d96fb`, `usb4-generic` `host-model=amd` with
its PCIe tunnel on the q35 `usb4-tunnel-port`. The enclosure is the
ASM2464 model with a FAT volume (`E:`) on Stor98. Each check copies a
1 MiB file to `E:` and compares it (`fc /b`).

**What the log read before the change.** The leg was run three times on
the 3.2.0.0 test cut; the driver log is from XHCITR's report.

- After a replug: `cm.reenum.confirm.refused` with reason 3, no entry of
  that serial. The tool's confirmation names a marks entry that had
  already become a tombstone: the ejected function had left the bridge's
  bus relations before the connection manager's thread read the
  confirmation.
- After a second replug, and again as the first eject after a restart:
  `cm.reenum.confirm`, then `cm.reenum.eject.stale`. The confirmation
  was accepted and recorded the bridge's population generation. The
  departure answer then started a new generation before the hand-off.

The emulator's trace shows Windows turning the slot off as part of the
eject (`guest turned the slot off`), and only then the function's
removal. Under native hot-plug the departure therefore races the tool's
confirmation, which the tool writes as soon as Windows answers, and
either order refused the eject. The mark service's confirmation, a few
seconds later, came after the departure every time it was read, and
worked (`run-43-52.md`, leg a). On the i440FX emulator's SHPC bridge
Windows does not drive the slot, and the eject has worked there since
43.45.

**The change.** In `xhcicm.sys` (record 21 section 7.4):

- A departure alone no longer starts a new population generation at the
  bridge (`CmPortsNewGeneration`). Only a function newly answering does,
  so a replacement device still ends an eject posted for its
  predecessor. A departure is logged (`cm.reenum.departure`).
- An entry that leaves the bridge's answer with its departure pending
  keeps that departure as a tombstone (`LeftPendingAt`,
  `LeftPendingGen`). A confirmation of its serial is accepted once,
  with the same identity and USB4 connection-generation checks, until a
  function newly answers behind the same member: then the kept departure
  is forgotten, so a late confirmation never ejects a replacement that
  arrived before it. A function that leaves with nothing pending is never
  confirmable.
- A stale eject logs which check refused it
  (`cm.reenum.eject.stale.why`).

Host vectors: `test_cm_ports`'s generation rule and `test_cm_marks`'s
confirmation of a tombstone (accepted once, not across a new connection,
not for another address, not for a surprise removal's tombstone, not
once a replacement has answered). The change was reviewed and revised
(the replacement rule was added). The host suite passed, and the gated
`all` builds passed on x86 and amd64 ("BUILD + GATES PASSED (debug
release qemu)").

**The legs after the change.** The release-flavour `xhcicm.sys` was
copied over the installed one: first the build before the replacement
rule (`7B05DEBD...`), then the final one (`21B41150...`).

| Leg | Result |
|---|---|
| Elevated `XHCIINST /eject 1`, three times, a replug between (`7B05DEBD...`) | **Pass**, three of three: "Windows has stopped PCI Express standard Upstream Switch Port. The PCIe tunnel was closed.", exit 0. The tunnel was down (`admin-down`) 4 to 5 s after the answer. Log: `cm.reenum.confirm`, `cm.reenum.departure`, `cm.reenum.eject` once; `cm.reenum.departure`, `cm.reenum.confirm`, `cm.reenum.eject` twice. Both orders were read |
| The same with the final driver (`21B41150...`) | **Pass**, three of three, the tunnel down 5 to 6 s after the answer; log `cm.reenum.departure`, `cm.reenum.confirm`, `cm.reenum.eject` each time; a write after the last replug compares clean |
| XHCITR's eject from the tray, without administrator rights (`7B05DEBD...`) | **Pass**: the tunnel closed 6 s after OK, "The PCIe tunnel was closed." |
| Replug after the ejects | The volume is back; a new write compares clean |

**On the kit.** Each new start of the emulator gave the q35 tunnel port a
new device instance, which had no filter: `XHCIINST /eject` said "There
is nothing to eject". `INSTALL` was run again to put the filter on it,
and the leg ran after guest restarts within the same emulator process.

## 43.69 - 98 SE's IOS fault on a stick pulled mid-copy, and the comparison leg (2026-10-23)

Roadmap-usb4 task 43.69 (design record 13 section 14, "The stopped TD,
read from the controller"). The owner held the task until 98 SE's
mid-copy pull was read as a comparison, because 98 SE's own IOS faults on
that pull with the `3.1.0.0` release driver too. Development host A,
2026-10-23. This section is the run and the finding.

### Finding: 98 SE faults in IOS on a stick pulled mid-copy

With a stick under NUSB 3.3's mass-storage driver (`USBSTOR.INF`) on 98 SE,
pulling it about 6 s into a 4 MB `copy` to it raises a Windows protection
screen: "An exception 0E has occurred at 0028:C003C413 in VxD IOS(01) +
0000156B. This was called from ... in VxD voltrack(01) + 00000267" (the
caller's address varies with voltrack's load address). The faulting address and
both offsets were the same in every run, whichever driver ran. The exception is in 98's own
storage stack (IOS, called from `voltrack`); `xhci98.sys` is not in the
frame. One key brings the desktop back in most runs, and after a replug
a fresh copy compares clean. It happens with the `3.1.0.0` release driver
(15 of 30 pulls), so it is not this task's. Some runs leave the guest
unusable after the pull (a black display, no shell, or a dialog nothing
answers): 4 of 30 with `3.1.0.0`. The
coordinator decides on a release note.

### Method

- Guest: 98 SE + NUSB 3.3 + xHCI98 `3.1.0.0` (43.13's `w98-nusb` chain,
  copied), a fresh overlay per series. TCG, one vCPU, 256 MB, headless,
  `qemu-xhci` `p2=4,p3=4`. Stick: a 128 MB FAT qcow2 (43.13's
  `usbfat128`, an overlay per series), `usb-storage` at root port 2, its
  devnode installed by the wizard once per overlay.
- Per run (`pullrun.ps1`): plug; `copy D:\RAND4.BIN F:\R.BIN` (4 MB);
  pull 6 s in; screendump 15 s later, classified by its mode and the
  title box (640x480 = desktop, 720x400 with the "Windows" box = the
  exception); keys to dismiss; replug; `copy` again and `fc /b | find
  "no differences"`, whose result DEBUG writes to port E9 (`#OK`, `#BD`).
  No marker within 150 s = the run did not survive, and the guest was
  restarted cold.
- QEMU 11.1.0 stock, and the `qemu-cancel` build from `usb-wdm-free`
  (`E:\usb-wdm-free-vm\qemu-cancel`, SHA-256 `390b66f1...`: Stop
  Endpoint saves the oldest fetched TD; a No Op TD sends no packet).
  Either QEMU sometimes exits at a `device_del` of the stick (both
  builds; 22 times in 88 attempts). The 8 times it exited during a run
  made that attempt invalid, and the attempt was repeated; none is counted below.
- Drivers: `3.1.0.0` release (the base's); branch `p43-69`'s `qemu`
  flavour (`3e4805f6...`) and `release` flavour (`dfec6f72...`); and as
  the like-for-like control the `release` flavour of the branch before
  43.69 (`p43-64` at `ee1434f4`, `637cd109...`, the gated build's hash).

### Readings (runtime, QEMU)

| Series | QEMU | Driver | Valid pulls | 0E faults | Survived | `fc /b` clean of survivors |
|---|---|---|---|---|---|---|
| rp | qemu-cancel | `3.1.0.0` release | 10 | 6 | 9 | 9/9 |
| mp | qemu-cancel | p43-69 qemu | 10 | 6 | 10 | 10/10 |
| rs | stock | `3.1.0.0` release | 10 | 5 | 9 | 9/9 |
| rs2 | stock | `3.1.0.0` release | 10 | 4 | 8 | 8/8 |
| ms | stock | p43-69 qemu | 10 | 8 | 8 | 8/8 |
| mr | stock | p43-69 release | 10 | 7 | 10 | 10/10 |
| mb | stock | ee1434f4 release (before 43.69) | 10 | 7 | 9 | 9/9 |
| mr2 | stock | p43-69 release | 10 | 5 | 10 | 10/10 |

The two series of each pair ran at the same time (rp with mp, rs with ms,
rs2 with mr, mb with mr2). Totals: `3.1.0.0` 15 faults in 30 pulls, 26
survived; p43-69, both flavours, 26 in 40 pulls, 38 survived; the
branch before 43.69, 7 in 10, 9 survived. Every survivor's `fc /b` was clean
for every driver. No difference in the fault counts is significant (Fisher
exact, two-sided: p43-69 against `3.1.0.0` p = 0.23 over all pulls, 0.53
for the release flavour; p43-69 against its own base p = 0.65).

### Verdict

43.69 is no worse than the branch it is built on: in the like-for-like
pair, the release flavour faulted 5 times and its base 7, and the
branch's release flavour survived every pull in two series. Against
`3.1.0.0` the branch faulted more often on stock QEMU (the release
flavour 12 of 20, the qemu flavour 8 of 10, against 9 of 20). On the patched QEMU the
two drivers faulted equally often (6 and 6). None of these differences is
significant, and the fault itself is IOS's. Every surviving run's
`fc /b` after the replug was clean.

### The owner's decision (2026-10-23)

The owner judged 43.69 against its base, the branch before it, on which
it is no worse, and ticked it. The difference between `3.1.0.0` and the
`3.2.0.0` line stays open for after the release.

### Owed

- The `3.1.0.0` against `3.2.0.0` fault rate: the line's higher count on
  stock QEMU (not significant, p = 0.15), open for after the release.
- The 98 SE IOS fault on real hardware, under NUSB 3.3 and a real stick.
- `qemu-system-x86_64` (stock and the `qemu-cancel` build) exiting at a
  `device_del` of a `usb-storage` stick, for the emulator's keepers.
