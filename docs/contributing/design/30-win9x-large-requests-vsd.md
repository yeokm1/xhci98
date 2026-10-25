# Design Record 30 - S98VSD.VXD: Large Requests Reaching Stor98 Whole on Windows 98 SE and ME

Status: draft 13, 2026-10-24 (roadmap 43.79: a list that arrives with
`IORF_PHYS_SGDS` is replaced, never passed down, after IOS's double
buffer was found sending one with a linear address, which corrupted
every unbuffered transfer at an unaligned buffer; sections 4.1, 4.6 and
9.2 item 1); draft 12, 2026-10-22 (the file renamed `S98VSD.VXD`: as
`STOR98.VXD` it stopped Stor98's start, Code 10, on the owner's bench and
in the emulator, section 3.1; owner, 2026-10-22); draft 11, 2026-10-22,
reviewed and revised (the sense-fetch
leg's stimulus, a test-only Stor98 build); draft 10, 2026-10-22, reviewed
and revised (the double buffer's
substitution order a hyp, `SCSIPORT.PDR`'s own requests compared in the
three builds with a leg provoking the sense fetch, the provenance row for
the new readings); draft 9, 2026-10-22 (D8 the owner's: the layer follows Stor98's
rule when Stor98's box is unticked, the recovery table's modes with it);
draft 8, 2026-10-22 (section 2.3 item 3: `SCSIPORT.PDR`'s own
requests read, the one with data a `REQUEST SENSE` that Stor98 answers
without DMA); draft 7, 2026-10-22 (section 2.3: the first pass of the
readings owed before the code, the disk SCSI'izer named, and
`SCSIPORT.PDR`'s own requests an open item); draft 6, 2026-10-22,
reviewed and revised (a request with no
SRB admitted only for functions whose SRB construction is read before the
code; `WININIT.INI`'s lines absent, exact or conflicting; an owned
`.TMP` withdrawn under a foreign destination); draft 5, 2026-10-22,
reviewed and revised (the SRB's data range
checked for wrap, the double buffer's substitution part of the reading
owed before the code, a foreign `.TMP` with nothing in flight a row of
the recovery table); draft 4, 2026-10-22, reviewed and revised (the installer's
recovery by the run's mode and the selected package, `Pending` deleted
last, the leftovers of a finished step; the refusal path given a
fault-injection leg); draft 3, 2026-10-22, reviewed and revised (the list held to the
IOR's count on every request, an SRB or none; page residency argued from
the translation window and the layers between, with the reading it rests
on owed before the code; the installer's own key and a recovery table for
every step of a placement or an update; the fragment limit counting the
terminator apart; the release notes' wording); draft 2, 2026-10-22,
reviewed and revised (the page-table
service's result checked, the list's room bounded by two partial pages a
fragment, checked arithmetic and a fragment limit on the logical list, the
SRB's data covered before every call down, the callback table's depth,
the dynamic exit refused, page locking as a section of its own, the
installer's record of a pending update, the legs widened); draft 1,
2026-10-22, before code. Written for `roadmap-usb4.md`
task 43.46, release `3.2.0.0`, from the task's readings
(`runs/run-43.md`, "43.46 read first", "The C port" and "Requests over
512 KiB", 2026-10-21 and 2026-10-22) and the owner's decisions:

- 2026-10-21: the layer lives in this repository under GPL-2.0, not in
  Stor98; nothing of it moves into `src/stor98/`;
- 2026-10-22 08:15: it is written in C as far as C can do it, assembly
  only where C cannot, each such line saying why; Open Watcom first, MSVC
  6.0 with the DDK's `VXDWRAPS` only if Watcom failed (it did not:
  `runs/run-43.md`, "The C port");
- 2026-10-22, option (b) of the readings: Stor98's 9x maximum transfer is
  raised to 2 MiB as Stor98's own change, and the layer only builds the
  physical list - no cutting of a request, no rewriting of an SRB or a CDB;
- 2026-10-22: the layer is its own choice in the installer, a box under
  Stor98's box, on Windows 98 SE and ME only (section 7);
- 2026-10-22: its file was named after Stor98, `STOR98.VXD` or `STOR98.VSD`
  by what IOS loads (`STOR98.VXD`), with a device name that cannot
  collide (`S98VSD`);
- 2026-10-22, after the first bench: the file is `S98VSD.VXD`, the device
  name's, everywhere (the installer, its switches `/s98vsd` and
  `/nos98vsd`, the package, the cut, the documents); an update removes
  this project's `STOR98.VXD`; a gate and a host test keep every IOSUBSYS
  driver's base name apart from Stor98's (section 3.1).

Section 10 lists the decisions taken in writing it, each the
orchestrator's for the owner to confirm unless it names the owner;
section 11 what is open.

Every statement below about what `IOS.VXD` or `SCSIPORT.PDR` does
carries one of four tags: **[obs]**, seen on the 98 SE emulator and
recorded in `runs/run-43.md`; **[static]**, read from the binary's
disassembly with nothing executed (`legal-provenance.md` section 4, the
two 43.46 rows); **[hdr]**, stated by a Windows 98 SE DDK header; **[hyp]**,
this record's inference, which a leg of section 9.2 confirms or refutes
before the release relies on it. Addresses are LE object offsets in the
three `IOS.VXD` builds read (section 2.1).

## 1. What it is for

### 1.1 The problem

On Windows 98 SE and ME an unbuffered sequential read or write to a disk
Stor98 runs reaches Stor98 in 63 KiB pieces sent one after another
whenever the request is over 512 KiB, or its buffer spans more than 17
physically discontiguous runs (Stor98 `docs/INVESTIGATION-9X-REQUEST-SIZE.md`;
`runs/run-43.md`). The cause is not Stor98's: `SCSIPORT.PDR` demands
physical scatter/gather lists from IOS (`DCB_dmd_phys_sgd`), and on that
path IOS's criteria routine refuses a request over 512 KiB or of more
than 17 runs, both limits hard-coded, and cuts it [static]. No miniport
setting changes either limit.

A vendor-supplied driver (VSD) inserted between the layers above and
`SCSIPORT.PDR` on Stor98's disk that does not demand `phys_sgd` moves the
request to IOS's logical path, where only `DCB_max_xfer_len` limits its
size, provided the VSD then builds the physical list `SCSIPORT.PDR`
reads. The read-first prototype did exactly that and measured it [obs]:
on a fragmented buffer, 128 to 512 KiB requests reached Stor98 whole, at
1.5 to 2.2 times the rate without it; with Stor98's maximum raised to
2 MiB as well (option (b)), 1 and 2 MiB requests went from 28-38 MB/s to
58-92 MB/s in the emulator.

### 1.2 Scope

- `S98VSD.VXD`, a static IOS VSD for Windows 98 SE (4.10.2222) and ME
  (4.90.3000), in `src/vsd/`, built with Open Watcom 2.0 and the Windows
  98 SE DDK's `LINK.EXE` 5.12 by the gated build (section 6);
- its claim of Stor98's disks only, its calldown entry, its physical
  list, and its behaviour on every request that reaches it (sections 3
  and 4);
- the installer's box, console switches, placement, update and removal of
  the file in `SYSTEM\IOSUBSYS` on 98 SE and ME (section 7; record 26's
  Components table carries the row);
- Stor98's 9x maximum transfer at 2 MiB, as Stor98's own change recorded
  in its `docs/PLAN.md` (section 5), which this layer relies on for
  requests over 512 KiB;
- host vectors and legs (section 9).

### 1.3 Not in scope

- **Queue depth.** This layer does not change queueing. Win32 file I/O
  on 9x is synchronous, so a single-threaded program issues one request
  at a time with or without it, which is what every reading measured; a
  queue depth above 1 needs several threads and is unmeasured (Stor98's
  investigation, section 3). The owner's Windows 10 figure at Q8T1 has no
  measured 9x counterpart.
- **I/O through VCACHE** (ordinary buffered file copies): VCACHE issues
  reads of 64 KiB and writes of 32 and 64 KiB [obs], so this layer
  changes nothing for them, and the readings saw no change (copy rates
  equal with and without it).
- **Requests over 2 MiB** (over `DCB_max_xfer_len`): IOS still marks them
  too big and cuts them into 63 KiB pieces (section 2.2), as it cuts a
  1 MiB request today. No cutting of a request is the owner's (b).
- **Windows 2000 and later**, and every disk Stor98 does not run.
- **SweetLow's `NVMEVSD.VXD`**: nothing is taken from it beyond what
  Stor98 `docs/gates/G9.md` records of its binary; it matches only a DCB
  whose port name is `nvme2k` and keeps `phys_sgd` demanded, and this
  layer matches only `stor98` (section 3.3), so the two can sit in one
  `IOSUBSYS` without claiming the same disk [static, G9].

What it does not change goes into the release notes in those words
(section 8).

## 2. What IOS and `SCSIPORT.PDR` do (the readings)

### 2.1 The binaries

| File | Build | Bytes | SHA-256 | Where read |
|---|---|---|---|---|
| `IOS.VXD` | gold 98 SE, 4.10.2222 | 69,570 | `bb133fb8edc9464f38bda9ee94349014eebdfd3164583432304a2e88e1a70652` | 98 SE CD, `WIN98\BASE4.CAB` |
| `IOS.VXD` | the QuickInstall 98 SE image's update (Stor98 G9's image) | 69,594 | `7adb5fe3f458bc2c5f747679fef32e0d799883a9c8fb6bc5fd096dbba3e16ec2` | that image, `SYSTEM\VMM32\` |
| `IOS.VXD` | ME, 4.90.3000 | 80,147 | `d897ae9986719fb7475c240653b5b23cd9707458f1d1da14b1f5dcb311768da5` | ME's `BASE2.CAB` (an installed ME links it into `VMM32.VXD`) |
| `SCSIPORT.PDR` | gold 98 SE | 23,650 | `8a0712513067edb75e2146beef889845bf5d77bc293558b9c359af8ced0f4be7` | 98 SE CD, `WIN98\BASE4.CAB` |
| `SCSIPORT.PDR` | QuickInstall update, 4.10.2223 | 23,674 | `af7610d41970a5895ad0bbdda2443d4accae6401b1e1a74badd13c6e8f8a08c6` | that image |
| `SCSIPORT.PDR` | ME | 26,957 | `bfac66edd87be878be4f1965b53d712ad924b56c7ee1256d809e84980d6df74b` | ME's `BASE2.CAB` |

### 2.2 What each reading found

1. **What IOS loads from `IOSUBSYS`** [static]. IOS builds
   `<system directory>\IOSUBSYS\` and appends one pattern, `*.vxd` (the
   string `IOSUBSYS\*.vxd` at object 5 `0x33A`, the pattern at `0x343`;
   ME object 8 `0x33B`), then walks the directory with find-first and
   find-next (`int 21h` functions `4Eh` and `4Fh`, or IFSMgr's ring-0
   equivalents), loading each match (QuickInstall and gold object 4
   `0x1E80`-`0x1F53`; ME object 5 `0x1D20`-`0x1DF3`). No other extension
   is enumerated: a `*.mpd` string follows the pattern but no
   instruction names its address, and `.mpd` appears only in a test of an extension on a
   file being loaded by name (object 1 `0x211F`). A `.VSD` file is never
   loaded; `.PDR` files are loaded by name from the registry and the
   devnodes (`ESDI_506.PDR`) or from IOS's own list (`scsiport.pdr`,
   `rmm.pdr`). Hence `S98VSD.VXD`.
2. **The criteria routine** (`ILB_int_io_criteria_rtn`; QuickInstall and
   gold object 1 `0x1592`/`0x15A0`, the same instruction sequence in
   both; ME object 1 `0x19A0`, the same checks with its stack frame
   aligned) [static]. For every request it computes the byte count from
   `IOR_xfer_count` (sectors unless `IORF_CHAR_COMMAND`); a count over
   `DCB_max_xfer_len` sets `IORF_IO_TOO_BIG`; with `DCB_dmd_phys_sgd` in
   the DCB's `DCB_dmd_flags` (`0x15E6`, ME `0x19E3`) it builds the
   physical list itself and fails a count over `0x80000` (`0x16CC`, ME
   `0x1AC8`) or more than `min(17, DCB_max_sg_elements)` runs (`0x16E7`,
   ME `0x1AE5`) as too big; without it, a logical scatter/gather list
   longer than about `DCB_max_sg_elements` entries makes it return
   non-zero (`0x15F3`), which its callers take as "not whole" [hyp for
   every caller]. A request marked too big is cut into pieces of
   `(0xFFFF >> DCB_apparent_blk_shift) - 1` sectors, 63 KiB at 512-byte
   sectors, sent one after another (Stor98's investigation, section 1).
   The list builder has **no test of the IOR function**: it builds from
   `IOR_buffer_ptr`, `IOR_xfer_count`, `IORF_SCATTER_GATHER` and
   `IORF_CHAR_COMMAND` for whatever request it is given; the one
   function it treats apart is `IOR_SCSI_PASS_THROUGH` (`0x0F`), which
   skips the alignment check (`0x1672`). It reads the page tables with
   **one `_CopyPageTable` call per fragment** (the whole fragment's pages
   into a stack array, `0x1739`), merges adjacent pages into runs of up to
   about 60 KiB, **locks nothing**, and sets `IOR_sgd_lin_phys`,
   `IOR_num_sgds` and `IORF_PHYS_SGDS`. A request misaligned for the
   DCB's word or dword demand (`0x600`) is marked `IORF_DOUBLE_BUFFER`
   (`0x1631`).
3. **`ISP_INSERT_CALLDOWN`** (QuickInstall and gold object 4 `0x1266`,
   ME object 5 `0x1165`, the same instruction sequence in all three)
   [static]: keeps the DCB's calldown chain in ascending load-group
   order, gives an entry of an earlier insertion that carries
   `DCB_dmd_preload` the new entry's bits, and sets `DCB_dmd_flags` to
   the first entry's. On the QuickInstall image the VSD's
   `AEP_CONFIG_DCB` for Stor98's DCB arrived before DISKTSD's (load group
   7) and before the load group 0x10 layer's, which copied its bits, so
   at the first request `phys_sgd` was gone from every entry above
   `SCSIPORT.PDR` (four boots) [obs].
4. **`ScsiPortGetPhysicalAddress`** (QuickInstall object 1 `0x03A6`; the
   same 50 bytes at file offset `0xDA6` of the QuickInstall and gold
   images and `0x1446` of ME's) [static]: for an address inside the SRB's
   `DataBuffer` .. `DataBuffer + DataTransferLength`, it takes the IOP
   from the SRB's port extension (`+0x40`), reads `IOR_sgd_lin_phys`
   (IOP `+0xAC`) and walks the list by byte offset, **without testing
   `IORF_PHYS_SGDS` or `IOR_num_sgds`**; any other address it translates
   with `_CopyPageTable` itself. `IOR_num_sgds` (IOP `+0xB0`) is read in
   one place (`0x1017`, a comparison with 1) in a routine Stor98 does not
   reach [hyp: the system-DMA path, which a bus-master miniport never
   takes]. So, once `phys_sgd` is dropped, **every request that carries
   data to `SCSIPORT.PDR` needs a list at `IOR_sgd_lin_phys` covering its
   SRB's data, from whoever drops it**; a request without one makes
   `SCSIPORT.PDR` walk whatever that field holds.
5. **The SRB at this layer** [obs]: a read or write arrives with
   `IORF_SRB_VALID`, the disk SCSI'izer (`DISKVSD.VXD`, "SCSI Disk VSD",
   load group `0x10`, above this layer [static: its DRP; the read-first
   sheet's `0x0B` is `APIX.VXD`'s, "SCSI API XLATOR"])
   having built the SRB `SCSIPORT.PDR` executes at `IOP_srb` (function 0,
   a 10-byte `READ(10)`/`WRITE(10)` CDB, `DataTransferLength` the whole
   request, `DataBuffer` the linear buffer or, with
   `IORF_SCATTER_GATHER`, the first fragment's). Stor98's 9x engine
   resolves the SRB's data through the IOR's logical list the same way
   (Stor98 `docs/PLAN.md` 0.4, "9x caller buffer"; pass-through requests,
   IOR function `0x0F`, arrive with linear buffers there).
6. **What it is worth** [obs]: section 1.1, and `runs/run-43.md`'s
   tables. The C port was 12 to 29 % slower than Stor98 alone on a
   near-contiguous buffer at 64 to 512 KiB; it built its list with one
   `_CopyPageTable` call a page (section 4.4).

### 2.3 The readings owed before the code, first pass (2026-10-22)

`runs/run-43.md`, "Static readings before the code", has them with their
addresses; in short [static, QuickInstall builds]:

1. **The layers above.** Every store at `IOR_buffer_ptr`'s place in
   `DISKTSD.VXD` and `DISKVSD.VXD` (a sweep of their code) is in code that
   builds a request of the layer's own (an IOP from `ISP_CREATE_IOP`, or
   the layer's own, its `IOR_flags` written whole), none in the path that
   passes a request from above down. DISKVSD's own requests (`READ
   CAPACITY`, `START STOP UNIT` and the like) take buffers from
   `_HeapAllocate` or its own structures and pass down through this layer,
   which builds their list like any other (section 4.1). Item 2 of
   section 4.7 holds for these two layers on this reading; a pass over
   every register-relative form is the code step's first task.
2. **IOS's double buffer.** `IOS_SendCommand` (service 4) sends a request
   flagged `IORF_IO_TOO_BIG` or `IORF_DOUBLE_BUFFER` to its splitting and
   double-buffering branch before the first calldown is made, the branch
   allocating its pages there [static]; that the substitution itself
   precedes the chain, so that both translations see the same buffer, is
   a [hyp] until the fields that branch rewrites and the order of its
   dispatch are read, which section 4.7 item 2 keeps as a condition for
   the code.
3. **`SCSIPORT.PDR`'s own requests.** The one SRB construction found is
   for `SCSIPORT.PDR`'s own internal requests: an IOP from
   `ISP_CREATE_IOP`, its SRB inside the IOP, `DataTransferLength` the
   IOR's count, a one-entry list slot set at `IOR_sgd_lin_phys`, and a
   call of the criteria routine, the only one in the image. **New
   finding for this design:** with `phys_sgd` gone from the DCB, the
   criteria routine builds no list for such a request [hyp: it reads the
   DCB's demand]; a request of `SCSIPORT.PDR`'s own that carries data and
   does not pass through this layer would then be translated through an
   unfilled slot. The builder has two callers [static]: a `REQUEST
   SENSE` of its own (`SCSIPORT.PDR`'s sense fetch after a check
   condition without autosense; a 0x40-byte buffer in the IOP), and a
   request with no buffer from its initialisation object; the builder is
   the same instruction sequence, with the same two callers and the one
   `REQUEST SENSE` CDB set-up, in the gold 98 SE, QuickInstall and ME
   `SCSIPORT.PDR` [static]. The `REQUEST
   SENSE` carries data, but Stor98 answers `REQUEST SENSE` in software
   from its kept sense (`ScsiCompletesInSoftware`, `nvme2k.c`: "no child,
   no DMA"; Stor98 calls `ScsiPortGetPhysicalAddress` only in its
   engine's mapping of read and write children, `engine.c`, and for its
   own uncached extension), so the slot is never walked. The hazard is
   therefore closed, in those three builds, by a property of Stor98's
   [static, with the leg of section 9.2 item 10 provoking the sense fetch],
   which the code's comment
   and Stor98's `docs/PLAN.md` record as a condition: a Stor98 that ever
   moved `REQUEST SENSE` to a DMA path would reopen it. No construction
   for an incoming IOR without an SRB was found, so the code admits no
   no-SRB function (section 4.3) until one is.

## 3. The layer

### 3.1 The file, its place and its names

- **File**: `S98VSD.VXD` in `<system directory>\IOSUBSYS\`, where
  `stor98.mpd` already is (`stor98.inf`'s 98 SE and ME section, DIRID 12).
  IOS's directory walk loads `S98VSD.VXD` (item 1 of section 2.2), and
  `stor98.mpd` is loaded through its devnode's `PortDriver` as today.
  **The two must differ in base name, not only in extension** [obs]:
  named `STOR98.VXD` (drafts 1 to 11), the layer stopped Stor98's start.
  On the owner's ThinkPad P14s (98 SE, the first bench, 2026-10-22) and in
  the emulator with Stor98's file `stor98.mpd`, its devnode reported
  problem 10 (Code 10) when the drive arrived, no DCB was made, and the
  same image renamed `S98VSD.VXD` let it start (`runs/run-43.md`,
  "Code 10: the file name"). The mechanism is a [hyp]: Windows loads an
  IOSUBSYS driver by its base name, and with a `STOR98` already loaded
  the miniport `stor98.mpd` is not; it is not read in `IOS.VXD` or
  `VXDLDR.VXD`. Every earlier leg had passed because the read-first
  image names Stor98's file `NVME2K.MPD`; one earlier run (`sm2`, with
  `PortDriver` set to `stor98.mpd`) had shown the same problem 10 and was
  set aside. `make-package.ps1` refuses two IOSUBSYS drivers with one base
  name, `test_inst` holds Stor98's IOSUBSYS files against the layer's
  name, and the installer removes this project's file under the old
  name (section 7.3).
- **Device name** (the DDB's `Name`, which VMM and VXDLDR know a device
  by, not the file name): `S98VSD`, padded with spaces to eight
  characters. No Windows 98 SE or ME VxD, and no file of this package,
  uses it. It is now also the file's base name, which no other file of
  the package or of Windows in IOSUBSYS has.
- **DDB**: `Req_Device_Number` `Undefined_Device_ID` (it exports no
  service), `Init_Order` `Undefined_Init_Order`, the version fields 1.0
  (the layer's own; the package's version is the stamp below,
  `src/xhci_version.h` carrying no separate major and minor), the
  control procedure of
  section 3.6, and `Reference_Data` the DRP (section 3.2), which is how
  IOS finds the registration packet of a VxD it loads [hdr: `DRP.H`].
- **Stamp**: the image carries the string `S98VSD.VXD <version>` from
  `XHCI_VER_STR`, which the build's gate requires (section 6.4), as
  `XHCIDI.EXE` carries `XHCIDI <version>`.

### 3.2 Registration

At `Sys_Dynamic_Device_Init` (IOS loads `IOSUBSYS` VxDs dynamically
[obs]) the control procedure calls `IOS_Register` with its DRP: eye
catcher `XXXXXXXX`, load group `DRP_VSD_9` (bit `0x11`, between the
load group `0x10` layer and `SCSIPORT.PDR`'s `0x14` [hdr: `DRP.H`]), the
AER of section 3.3, an ILB IOS fills, the name `STOR98 VSD`, revision
1, no feature code. `DRP_REMAIN_RESIDENT` or `DRP_MINIMIZE` is success;
any other result fails the init, and VMM unloads the device [obs, the
prototype's `IOS_Register` result was 1, `DRP_REMAIN_RESIDENT`]. It
stays resident whether or not a Stor98 disk is present: a disk behind
the tunnel arrives after boot, and its DCB is configured then.

### 3.3 Claiming only Stor98's disks

The AER answers `AEP_SUCCESS` to every function and acts on
`AEP_CONFIG_DCB` alone. It inserts its calldown entry on a DCB only when
all of these hold, read from the DCB at that call:

1. a physical DCB: neither `DCB_DEV_LOGICAL` nor `DCB_DEV_RMM` in
   `DCB_device_flags`;
2. its port name, `DCB_port_name`, is `stor98` (ASCII, compared without
   regard to letter case, followed by a NUL or the field's end). The
   port name is the miniport file's base name [obs: a Stor98 image named
   `NVME2K.MPD` gave `nvme2k`]. `nvme2k` is **not** matched: it is
   another driver's name (SweetLow's `nvme2k`, Stor98's ancestor, and the
   name `NVMEVSD.VXD` claims), and this layer is for Stor98's disks only;
3. `DCB_dmd_phys_sgd` is in `DCB_dmd_flags` (without it there is nothing
   to lift);
4. `DCB_max_xfer_len` is at most 2 MiB (`VSD_MAX_XFER`, the largest
   request the list's room is sized for, section 3.4). A Stor98 of a
   later release that advertises more is left alone, counted, rather than
   given a list that could overflow;
5. no entry of the DCB's calldown chain already has this layer's request
   routine (IOS may configure a DCB more than once [hyp]; the chain
   itself says whether it is claimed, so the layer keeps no table of
   DCBs, and nothing of it can outlive a DCB, section 4.8).

A DCB that fails a condition is left untouched and counted by the
condition. Matching the DDB of the port driver, or the INQUIRY data, was
considered and not taken: every Stor98 DCB has `SCSIPORT.PDR`'s DDB, and
the INQUIRY vendor field is the drive's, not the driver's.

### 3.4 The calldown entry

`ISP_INSERT_CALLDOWN` with:

- `ISP_i_cd_flags`: the DCB's `DCB_dmd_flags` at that moment
  (`SCSIPORT.PDR`'s, `0x00000E09` on the readings [obs]) without
  `DCB_dmd_phys_sgd`; the word and dword alignment demands stay, so IOS
  still double-buffers a misaligned request (section 4.6);
- `ISP_i_cd_lgn`: the load group the AEP names;
- `ISP_i_cd_expan_len`: the layer's area in every IOP of the DCB:
  a 16-byte header (the saved `IOR_sgd_lin_phys`, `IOR_num_sgds` and
  flags), the list, room for `P + 2F` runs of 8 bytes, and the page
  scratch, `P + 2` dwords, where `P = VSD_MAX_XFER / 4096` (the area is
  sized for 2 MiB whatever the DCB's maximum, which section 3.3 holds at
  or below it, so no per-DCB size need be kept) and `F` is
  the layer's own limit on the fragments of a logical list,
  `VSD_MAX_FRAGS` = 255 (the most a byte-wide count of IOS's could name;
  section 4.2 refuses more). The bound: a fragment of `n` bytes touches
  at most `ceil(n / 4096) + 1` pages (one partial page at each end), the
  fragments' bytes sum to at most `DCB_max_xfer_len` (section 4.2), so
  the pages touched, and so the runs, number at most
  `sum(ceil(n_i / 4096)) + F <= P + F + F`. A fragment's own pages are at
  most `P + 1`, which the scratch holds with one to spare. The bound does
  not rest on IOS's own logical-list check (section 2.2 item 2), which
  is read only for some callers. At Stor98's 2 MiB: 1,022 runs and 514
  dwords, about 10 KiB in each IOP of the disk [obs: a 520-run area
  worked at 2 MiB in the BG arm; 200 overflowed; no larger area was
  tried, so the IOP size is read again on the legs].

After the insertion the AER reads `DCB_dmd_flags` again. If
`phys_sgd` is still there (an entry above this one inserted earlier
kept it), the layer edits nothing: IOS then builds every list itself
and each request arrives with `IORF_PHYS_SGDS`. Until roadmap 43.79 the
layer passed such a request down untouched; since then it replaces the
list with its own translation of the same buffer (section 4.1 item 1),
the same runs, held to the same checks and refusals. The case is
counted and printed in the `qemu` build (section 4.9). The prototype's fallback,
clearing `phys_sgd` from other layers' entries, is not carried: it
writes into structures other drivers own, it was never needed in four
boots [obs], and the legs of section 9.2 item 3 read whether it is
needed on the gold 98 SE and ME IOS and with the disk present at boot.

### 3.5 What it does not do

It does not change `DCB_max_xfer_len`, `DCB_max_sg_elements` or any
other DCB field, cut a request, build, read or rewrite an SRB or a CDB
beyond the two reads of section 4.3, issue a request of its own, allocate
memory, keep a timer, or touch the registry. It exports no service and
answers no `DeviceIoControl`.

### 3.6 The control procedure

The one place C cannot reach: VMM passes the message in `EAX` and reads
success or failure from the carry flag. Three lines of inline assembly,
each with its reason in the source: the call to the C handler (message
in, result out in `EAX`; Watcom's register calling convention with a
`#pragma aux` for that one function), `neg eax` (the carry flag from a
non-zero result: C has no access to the flags), and `ret` (a naked
function has no epilogue). `Sys_Dynamic_Device_Init` registers (section
3.2). `Sys_Dynamic_Device_Exit` is answered with failure once the
registration has succeeded: IOS holds the AER, the request routine and
the callbacks of a registered layer, and there is no call that takes
them back, so the image must never be unloaded [hdr: `VMM.H`, a dynamic
device may fail its exit; hyp: VXDLDR then keeps it, and IOS never asks
- a leg cannot provoke it, so the code's answer is the guard]. Every
other message is success. The two services it calls,
`_CopyPageTable` (VMM) and `IOS_Register` (IOS), are reached through
`#pragma aux` byte sequences: `int 20h` and the service's dword, which
VMM patches into a direct call on first use [hdr: `VMM.H`'s
`VMMCall`/`VxDCall`]; both are C-callable (`cdecl`) services, so their
parameters are ordinary C arguments.

## 4. The request path

### 4.1 Which requests get a list

The request routine looks at each IOP in this order:

1. `IORF_PHYS_SGDS` already set: **not** a reason to pass the request
   down. Until roadmap 43.79 such a request was passed down untouched,
   taken to carry a physical list IOS or a layer above had built. On
   this layer's DCB it does not: with `phys_sgd` no longer demanded,
   IOS's double buffer (section 4.6) arrives with `IORF_PHYS_SGDS` set
   and a list that holds the double buffer's **linear** address, and
   `SCSIPORT.PDR` handed that address to Stor98 as a physical one
   [static and obs, section 4.6]. A list that arrives is therefore
   counted, set aside and replaced by this layer's own, built by items 2
   and 3 like any other request's; the callback puts the one that came
   back. No request with data goes down with a list this layer did not
   build (`VsdPlan`, `vsd_list.c`).
2. `IOR_xfer_count` 0, or a function that carries no data by its
   definition [hdr: `IOR.H`] - `IOR_CANCEL`, `IOR_MEDIA_CHECK`,
   `IOR_MEDIA_CHECK_RESET`, `IOR_LOAD_MEDIA`, `IOR_EJECT_MEDIA`,
   `IOR_LOCK_MEDIA`, `IOR_UNLOCK_MEDIA`, `IOR_CLEAR_QUEUE`,
   `IOR_DOS_RESET`, `IOR_RESTART_QUEUE`, `IOR_ABORT_QUEUE`,
   `IOR_SPIN_DOWN`, `IOR_SPIN_UP`, `IOR_FLUSH_DRIVE`,
   `IOR_FLUSH_DRIVE_AND_DISCARD`: passed down untouched, **unless**
   `IORF_SRB_VALID` is set and the SRB's `DataTransferLength` is not zero:
   such a request carries data `SCSIPORT.PDR` would translate through no
   list the IOR can describe, and is refused (section 4.5). Section 4.3's
   invariant is therefore held before every call down, items 1 and 2
   included: a request goes down either with no SRB data, or with a list
   that covers it.
3. Every other request: a list is built from the fields IOS's own
   builder reads (section 2.2 item 2) - `IOR_buffer_ptr` and
   `IOR_xfer_count`, as a logical scatter/gather list when
   `IORF_SCATTER_GATHER` is set, in bytes when `IORF_CHAR_COMMAND` is set
   and in sectors of `DCB_apparent_blk_shift` otherwise. This is the
   same set of requests IOS built a list for while `phys_sgd` was
   demanded, read, write, verify, write-and-verify, SCSI pass-through,
   `IOR_SCSI_REQUEST`, general IOCTL and format included, because the
   builder tests no function; so no request that had a list before goes
   down without one now.

A function outside both lists of `IOR.H` (a value the header does not
define) is taken as item 3: building a list for a request that carries
no data costs a few calls, while sending down one that carries data
without a list makes `SCSIPORT.PDR` walk a stale pointer (section 2.2
item 4).

### 4.2 Building the list

The input first, every step checked, a failure being a refusal:

- the count to bytes: a sector count is shifted by
  `DCB_apparent_blk_shift` only after checking it is below
  `2^(32 - shift)`; a byte count is taken as it is;
- a logical list is walked one 8-byte descriptor at a time: up to
  `VSD_MAX_FRAGS` (255) descriptors with a non-zero count, then one more
  descriptor read, which must be the zero-count terminator (a zero count
  is the terminator by the list's definition [hdr: `BLOCKDEV.H`], so
  there is no zero-length fragment); the descriptor pointer is advanced,
  and the 256th descriptor's address formed, only after checking that
  the advance does not wrap; a list whose first descriptor is the
  terminator (no data) under a non-zero `IOR_xfer_count`, and a 256th
  descriptor with a non-zero count, are refusals;
- the fragments' bytes are summed with a check against wrap, and the sum
  (or the one linear buffer's count) must be at most `DCB_max_xfer_len`
  (IOS checks it too, section 2.2 item 2; the layer does not rely on it);
- each fragment's address plus its length must not wrap; and the range
  `SCSIPORT.PDR` treats as the SRB's data, the first byte plus the
  request's length, must not wrap either: `DataBuffer +
  DataTransferLength` when `IORF_SRB_VALID` is set, the linear buffer or
  first fragment plus `IOR_xfer_count`'s bytes always (the SRB
  `SCSIPORT.PDR` builds for a request without one, section 4.3). Stor98
  translates `DataBuffer + offset` past the first fragment
  (`EngMapCaller`), and an address that wrapped would fall outside the
  SRB's range and be translated from the page tables directly (section
  2.2 item 4), so a wrapping range is a refusal even where every fragment
  is whole.

- the list must cover the request: its bytes are at least
  `IOR_xfer_count`'s (as bytes), on every request, an SRB or none;
  `SCSIPORT.PDR`'s own SRB for a request that arrives without one takes
  its data from the first fragment and its length from the IOR (Stor98's
  investigation, section 7, read in all three `SCSIPORT.PDR` builds:
  `DataBuffer` the first fragment when `IORF_SCATTER_GATHER`)
  [static; the length's source is a hyp, which the per-function count of
  section 4.3 and its legs read], and an SRB that does arrive is held to
  the list by section 4.3. More bytes in the list than the count is
  allowed (IOS's builder walks to the terminator too); fewer is a
  refusal.

Then for each fragment:

1. one `_CopyPageTable` call for all the pages the fragment touches
   (`(offset + bytes + 4095) / 4096` of them from the fragment's first
   page) into the page scratch, as IOS's builder makes one call per
   fragment; the scratch is zeroed for those pages first, and **the
   service's result must be non-zero** (success; Stor98's own reader,
   `src/stor98/src/pool9x.c`, takes it so) or the request is refused
   before any entry is read;
2. each page table entry must have its present bit set; one that does
   not is a refusal (section 4.5), never a physical address made from a
   zero frame;
3. the fragment's bytes become runs: the frame address plus the in-page
   offset for the first page, the frame address for the others, each run
   extended while the next page's frame follows the run's end, across
   fragments too (a run continues into the next fragment when that
   fragment's first byte is physically next), as the prototype and IOS's
   builder merge. Runs are not capped at 60 KiB as IOS caps them [static]:
   `SCSIPORT.PDR` walks by length (section 2.2 item 4), and Stor98 builds
   its PRPs from `ScsiPortGetPhysicalAddress`'s lengths, whatever they
   are [obs: runs of up to 2 MiB in the BG arm];
4. more runs than the room is a refusal (it cannot happen, section 3.4).

Before any field of the IOR is changed, the callback table must have a
free entry: `IOP_callback_ptr` below the end of `IOP_callback_table`,
`IOP_CALLBACK_TABLE_DEPTH` (6) entries [hdr: `IOP.H`]; with none free the
request is refused. [obs: the prototype's entry was pushed on every
request of every arm; how many entries the layers above use is printed
by the `qemu` build at the first request.]

Then `IOR_sgd_lin_phys` points at the list, `IOR_num_sgds` is the run
count capped at 255 (the field is a byte; `SCSIPORT.PDR` does not read
it on Stor98's path, section 2.2 item 4; [obs] lists of 513 runs with the
field at 255 compared clean), `IORF_PHYS_SGDS` is set, a callback entry
is pushed whose routine puts the three back as they were, and the IOP is
passed to the next calldown entry.

The arithmetic is unsigned 32-bit throughout; every sum of an address
and a length is checked against wrap before it is used, a wrap being a
refusal.

### 4.3 The SRB's data covered

`SCSIPORT.PDR` walks the list by the offset of an address from the
SRB's `DataBuffer`, up to `DataTransferLength` (section 2.2 item 4). So
when `IORF_SRB_VALID` is set and the SRB's `DataTransferLength` is not
zero, two reads of `IOP_srb` hold before the request goes down (and
section 4.1 item 2 refuses such an SRB on a request that has no list):

- the list's total bytes are at least `DataTransferLength`; and
- `DataBuffer` is the first byte the list describes: `IOR_buffer_ptr`
  for a linear request, the first fragment's address for a logical
  list (Stor98's own classification, `Sg9xClassify`, refuses a request
  whose first fragment is not `DataBuffer` for the same reason).

Either failing is a refusal. Under `phys_sgd` IOS built the list from
the same IOR fields without these checks, so a request that fails one
was one whose SRB `SCSIPORT.PDR` already translated through an
inadequate list; refusing it is stricter than today, never looser
[hyp: no requester sends one; the pass-through and SMART legs of section
9.2 read it]. **Without `IORF_SRB_VALID`** `SCSIPORT.PDR` builds the SRB
itself, `DataBuffer` the linear buffer or the first fragment [static,
Stor98's investigation section 7]; its length is not read yet. So a
request with data and no SRB is admitted only for a function whose
SRB construction in `SCSIPORT.PDR` (its buffer and its length, in the
three builds of section 2.1) is read and recorded with its provenance
row before the code is written - read, write, verify and
write-and-verify are the ones to read, being what IOS's own requests
are - and only when section 4.2's coverage check covers that length. A
request with data, no SRB and any other function, an undefined value
included, is refused (section 4.5), counted per function in the `qemu`
build; section 9.2 item 5 runs the admitted paths and reads that no
refusal of this kind occurs in a boot, a format and the file phases.

### 4.4 The list's cost

The owner asked whether the list can be skipped when a request is
already within 17 runs, which would remove the 12 to 29 % loss the C
port showed on near-contiguous buffers. It cannot: the demand is a
property of the DCB, read by the criteria routine from `DCB_dmd_flags`
for every request (section 2.2 item 2), not of a request, so once
`phys_sgd` is dropped IOS builds no list for any request on the disk,
and `SCSIPORT.PDR` needs one for every request with data (item 4). There
is no request this layer could leave for IOS to build.

What can be removed is the difference between the prototype's builder
and IOS's: the prototype made one `_CopyPageTable` call a page (128 for
a 512 KiB request), IOS makes one a fragment. Section 4.2 makes one a
fragment too, so on a contiguous linear buffer this layer's work is one
call, a pass over the scratch and one callback, against IOS's one call
and pass. Whether that removes the measured loss is read on the legs
(section 9.2 item 2, the same `PERF2` phases as `runs/run-43.md`, Stor98
alone against Stor98 with this layer, back to back); the larger area of
section 3.4 is part of what is measured. If a loss of more
than the run-to-run spread remains on the near-contiguous phases, the
record says so and the owner decides whether it ships (section 11).

The cost leg is dropped for 3.2.0.0 (owner, 2026-10-22): on the owner's
P14s the layer gained 4 to 5 times at 1 and 2 MiB and twice at 128 KiB
(section 9.2).

### 4.5 Refusals

A refusal sets `IOR_status` to `IORS_INVALID_COMMAND` ("command not
supported or invalid" [hdr: `IOR.H`]), puts the IOR's
fields back as they came, and completes the request at once through the
callback above this layer, the entry `IOP_callback_ptr` points below
(the layer pushes none on that path, so a full table does not stop the
completion); nothing goes down [hyp: completing from the request routine
is how IOS's own layers fail a request (the prototype did it on an
overflow, and requests completed); section 9.2 item 9 reads it]. Each kind is counted (the
page-table service, the present bit, the room, the scratch, the
fragment limit, a wrap, the maximum, the SRB's coverage, the SRB's first
byte, SRB data on a no-data request, the callback table). The readings
measured only the room [obs: overflow 0 in every arm but the first BG
try, whose 200-run room was too small]; the prototype did not have the
other checks, so that none of them fires is a [hyp] the legs of
section 9.2 read. A refusal is a
failed read or write for the requester, which the file system reports
as it reports a disk error, so section 9.2's legs require every counter
to stay 0.

### 4.6 Pass-through, SMART and `IORF_DOUBLE_BUFFER`

- **Pass-through** (`IOR_SCSI_PASS_THROUGH`, ASPI and the like) and
  **`IOR_SCSI_REQUEST`**: item 3 of section 4.1. Pass-through requests
  carry their SRB from the requester, with a linear buffer [obs, Stor98
  PLAN 0.4, for function `0x0F`]; `IOR_SCSI_REQUEST`'s buffer is not read
  [hyp: linear too, IOS layers' own internal requests]. Either way the
  list is the buffer's runs and section 4.3's checks hold it to the
  SRB. Stor98's SMART answers on 9x go through pass-through SRBs to the
  same path (Stor98 `docs/TESTING.md`, `tests/scsichk` and
  `tests/logsense`); section 9.2 item 5 runs both through this layer.
- **`IORF_DOUBLE_BUFFER`** (read 2026-10-24, roadmap 43.79, after the
  odd-offset leg found every dword wrong). IOS marks a request whose
  buffer is misaligned for the alignment the DCB demands, which this
  layer keeps demanding (section 3.4), and serves it from a buffer of
  its own [static]. In the three `IOS.VXD` builds of section 2.1 (the
  QuickInstall addresses; gold the same, ME object 1 `0x10119F` to
  `0x101267`):
  - the criteria routine tests the word and dword demand (`0x600`)
    **before** `phys_sgd` (`0x1015BE` to `0x101631`): a misaligned
    request is marked whether or not the DCB demands physical lists,
    and without `phys_sgd` the routine returns at once (`0x1016A9`); its
    caller then sets `IORF_DOUBLE_BUFFER` (`0x100317`), and
    `IOS_SendCommand` takes its double-buffer branch (`0x100BD3`,
    `0x100D0F`) before the first calldown;
  - that branch allocates the buffer (`_PageAllocate` at `0x100DD3`, or
    from IOS's own allocator at `0x100DFC`, whose block is page-rounded
    only when `DCB_max_xfer_len` is 4 KiB, so a piece may straddle a
    page [hyp]), sets `IOR_buffer_ptr` to it with the old pointer saved, clears
    `IORF_SCATTER_GATHER`, sets **`IORF_PHYS_SGDS`** and points
    `IOR_sgd_lin_phys` at a one-entry list in its own block, address and
    length (`0x100E65` to `0x100E9C`), and sends the request down in
    pieces of at most 4 KiB, each from the top of the chain
    (`0x100FC8`), copying in before a write and out after a read;
  - the entry's address is made physical, by `_CopyPageTable`, **only
    when the DCB's `DCB_dmd_flags` hold `phys_sgd`** (`0x100EC1`); the
    list pointer itself only when they hold `DCB_dmd_phys_sgd_ptr`
    (`0x100E76`). IOR.H says the same of `IORF_DOUBLE_BUFFER`: the
    client must ensure that both the SGD buffer pointers and
    `IOR_sgd_lin_phys` are virtual.
  On this layer's DCB the demand is `0x609`, so the entry keeps the
  double buffer's linear address. Up to 43.79 the layer passed such a
  request down untouched (section 4.1 item 1 as it was), `SCSIPORT.PDR`
  walked the list (section 2.2 item 4) and gave Stor98 the linear
  address as the physical one: page-aligned, so Stor98's PRP checks held,
  and the controller read or wrote whatever physical memory sat at that
  address. Every buffer not at a 4-byte boundary was affected, both
  directions, with no error: writes put other memory's bytes or zeros on
  the disk, reads left the caller's buffer with other bytes and wrote
  the drive's data into memory that was not the buffer's [obs, the
  emulator, 98 SE and ME, `3.2.0.0`: PERF3 at +1 with every dword
  wrong; N3IO at +1, +2, +3, +511 and +4094, reads returning foreign
  bytes; without the layer, and at +0, +512 and a sector-aligned +3584,
  clean]. Since 43.79 the request is built like any other (section 4.1):
  `IOR_buffer_ptr` is the double buffer, `IOR_xfer_count` the piece, so
  the list is the double buffer's physical runs; the callback puts the
  list IOS left, and its flag, back before IOS's completion of the piece
  runs. The rule covers the class, not only this branch: the only two
  instructions in the image's code that set `IORF_PHYS_SGDS` (a search
  for a byte, dword or bit set of it) are this branch's (`0x100E9C`) and
  the criteria routine's own builder's (`0x1017B1`), which runs only
  under `phys_sgd` [static], and a list from a layer above is replaced the same way, so whatever
  shape arrives, what reaches `SCSIPORT.PDR` is this layer's
  translation of the buffer `SCSIPORT.PDR` itself takes `DataBuffer`
  from.
  A misaligned request that IOS does not double-buffer (a pass-through,
  IOR function `0x0F`, which skips the alignment check, `0x101672`)
  reaches Stor98 with a list whose first run has the odd offset, and
  Stor98 bounces it: a first PRP entry that is not DWORD aligned is
  never sent (Stor98 `src/prpchk.h`, `EngMapCaller`).

### 4.7 Page locking

The layer locks nothing, and IOS's own builder locks nothing either
(section 2.2 item 2) [static]. The question is therefore not whether the
layer must lock, but whether moving the translation from IOS's builder
to this layer asks more of the pages' residency than today. The argument,
and what it rests on:

1. **The window.** A physical list is safe while every page it names
   stays resident and mapped to the same frame from the moment it is
   read until the DMA that uses it ends. Under `phys_sgd` IOS reads the
   page tables in the criteria routine when the request is sent
   (`IOS_SendCommand`, before the calldown chain, Stor98's investigation
   section 1), so today's requirement covers the window from that call
   to the end of the DMA. This layer reads them later, when the request
   reaches it below the layers above it, for the same transfer; its
   window is the later part of today's [static, for the order of the
   calls].
2. **The same pages.** The window is a subset of today's only if the
   buffer translated is the one IOS would have translated: no layer
   between `IOS_SendCommand` and this layer replaces `IOR_buffer_ptr` or
   the logical list with other memory. IOS's own double buffer is
   substituted at `IOS_SendCommand` (section 4.6), before the first
   calldown, so it is the same buffer in both [static, read 2026-10-24
   for roadmap 43.79: it sets `IOR_buffer_ptr` to its buffer, clears
   `IORF_SCATTER_GATHER` and sets `IORF_PHYS_SGDS` with a one-entry list,
   physical only under `phys_sgd`; the pages are IOS's own allocation].
   Up to 43.79 that list reached `SCSIPORT.PDR` as it came, which is the
   corruption section 4.6 records; since then this layer translates the
   double buffer itself like any other buffer. For the layers above
   this one on Stor98's disk (load group 7, `DISKTSD.VXD`; `0x10`,
   `DISKVSD.VXD`; the chain the prototype printed) it is a [hyp], **read
   before the code is written** (a static reading of each layer's request
   routine, whether it writes `IOR_buffer_ptr`, `IOR_flags`'
   `IORF_SCATTER_GATHER` or the list it points at, recorded with its
   provenance row); a layer that does replaces the argument for its
   requests, and the design is revised before the code. The first pass of
   that reading (`runs/run-43.md`, "Static readings before the code") is
   section 2.3.
3. **What follows.** Where 1 and 2 hold, a request whose pages this
   layer translates had pages resident over a longer window under
   today's builder, whoever locked them (the file system and VCACHE for
   file I/O, IOS for its double buffer, the ASPI layer for pass-through);
   the layer needs no lock of its own and admits no request that did not
   already rely on that residency. Who locks for each family is not
   needed for the argument and is not claimed.

The present-bit check of section 4.2 refuses a page that is not present
at translation; it is a guard, not evidence of residency later. The
memory-pressure leg of section 9.2 item 8 corroborates the argument; it
does not replace item 2's reading, which is the condition for the code.

### 4.8 Surprise removal, link-down and `AEP_UNCONFIG_DCB`

The layer holds no state for a request outside that request's IOP (its
area, section 3.4) and the stack of the call that builds the list. It
never completes a request it did not refuse, never waits, and never
starts one. So:

- a **surprise removal or a link-down of the tunnel with a large request
  in flight** is Stor98's and `SCSIPORT.PDR`'s to complete, as without
  this layer: Stor98 fails its outstanding requests (its own `AGENTS.md`,
  surprise removal), `SCSIPORT.PDR` completes the IOP with an error
  status, and this layer's callback puts the IOR's three fields back and
  passes the completion up unchanged; its list, in the IOP, goes with
  the IOP;
- **`AEP_UNCONFIG_DCB`** frees nothing: the layer has no per-DCB state,
  IOS removes the calldown chain with the DCB [hyp, `DCB.H` and `ISP.H`
  name no removal call for a layer], and the next `AEP_CONFIG_DCB` of a
  DCB at the same address is claimed afresh by section 3.3's chain
  test. The AER answers it `AEP_SUCCESS`.

The legs of section 9.2 item 6 pull the tunnel during a 2 MiB transfer
and require the machine to stay up, the requester to get an error, and
the disk to come back and compare clean after it is plugged in again.

### 4.9 Counters and the `qemu` build

The counters (lists built; arrived with `IORF_PHYS_SGDS`, and with
`IORF_DOUBLE_BUFFER` too, each list then replaced; passed with no
data; refused by kind; the most runs and bytes in one list; DCBs claimed
and left by condition; `phys_sgd` still demanded after an insertion)
live in the layer's data. The `qemu` flavour's build also writes each
claimed DCB's fields, the chain after the insertion and at the first
request, and a count line every 4,096 lists to port `0E9h`, QEMU's debug
console, as the prototype did; the release build writes nothing to any
port and is the one the package carries (record 08's flavours; the
`debug` flavour uses the release image, since a debug-port write on a
real machine reaches whatever device decodes port `0E9h`).

## 5. Stor98's side: 2 MiB (the owner's option (b))

Stor98's 9x maximum transfer (`STOR98_MAX_TRANSFER`, 128 pages, 512 KiB
today) becomes 512 pages, 2 MiB, on 98 SE and ME. It is Stor98's change,
made under `src/stor98/AGENTS.md` and recorded as decided in Stor98's
`docs/PLAN.md` (owner, 2026-10-22), with its host vectors and build; this
record only relies on it. What the readings found about it [obs]: Stor98's
PRP-list and NLB checks hold at 512 pages, and Stor98 splits a 2 MiB
request into children of the controller's MDTS (512 KiB on the emulator),
so the disk sees the commands it saw before; on a controller whose MDTS is
over 512 KiB the commands are larger.

| Stor98's 9x maximum | Without `S98VSD.VXD` | With `S98VSD.VXD` |
|---|---|---|
| 512 KiB (before) | today: whole up to 512 KiB on a near-contiguous buffer, 63 KiB pieces over 17 runs or over 512 KiB | whole up to 512 KiB however fragmented; 63 KiB pieces over 512 KiB |
| 2 MiB (option (b)) | unchanged from the row above: the phys-path limit of 512 KiB still cuts (section 2.2 item 2) | whole up to 2 MiB however fragmented; 63 KiB pieces over 2 MiB |

So neither half harms the other's absence: the raised maximum changes
nothing without this layer, and this layer sizes its room from the
maximum it finds (section 3.4), up to the 2 MiB it is built for.

## 6. Toolchain and build

### 6.1 The compiler and the linker

- **Open Watcom 2.0 `wcc386`** (`C:\WATCOM`, or `WATCOM`; the install
  `docs/contributing/build-and-test.md` already requires for `xhciqual`
  and `XHCIDI.EXE`), flags `-bt=windows -3r -zp1 -zc -s -zl -zls -oxi -w4
  -we`: packed structures (`-zp1`, the DDK's C headers mirror the packed
  MASM layouts; the prototype checked 42 offsets against MASM's), string
  literals in the code segment, no stack probes, no default-library
  records, no C runtime. C89, the arithmetic rules and ASCII of this
  repository's `AGENTS.md` apply.
- **The Windows 98 SE DDK's `LINK.EXE` 5.12.8181 with `/VXD`** and its
  `MSPDB50.DLL` (`BINS_DDK` members `BIN_LINK.EXE` and
  `BIN_MSPDB50.DLL`), which reads Watcom's OMF objects (warning `LNK4033`,
  ignored by name) and writes the LE image 98 SE loads. Open Watcom's own
  `wlink ... format windows vxd` image was never loaded by 98 SE [obs].
- **Three steps around the linker**, Python scripts of this repository
  run with `python -I`, because `LINK` 5.12 mislinks two OMF shapes
  Watcom writes (`runs/run-43.md`, "Why no Watcom-built VSD loaded at
  first"): `omfpub.py` moves every `PUBDEF` record of an object to just
  before its `MODEND`; `omfcheck.py` fails the build on any static-symbol
  record (`B4h`-`BBh`) or any `EXTDEF` after its own object's `PUBDEF`;
  `callcheck.py` reads every self-relative call fixup the compiler wrote
  (Open Watcom's `dmpobj`) and fails the build if the call in the linked
  image lands anywhere but the map address of the symbol the compiler
  named. The source rules that go with them: no file-scope `static`, and
  the data in an object of its own (`vsd_data.c`).

### 6.2 What of the DDK enters tracked source: nothing

The DDK's C headers the layer is compiled against - `BASEDEF.H`,
`SGD.H`, `BLOCKDEV.H`, `IOR.H`, `ISP.H`, `AEP.H`, `ILB.H`, `DRP.H` (the
`incs\` members `INC_WIN98_*.H`), `IOP.H` and `DCB.H` (the
`BLCK_DDK` members `SRC_BLOCK_INC_*.H`) - and the linker pair are staged
at build time by `src\vsd\stage-ddk.ps1`, on `xhciinst\di\stage-ddk.ps1`'s
pattern: each member held to the SHA-256 the row naming it in
`docs/references/README.md`, "Windows 98 DDK", records, read from that
row, then copied under its plain name into the git-ignored
`src\vsd\obj\`. The staged copies are changed in two ways, by the
script, never in a tracked file: a trailing Ctrl-Z is cut (Watcom reads
past it), and `AEP.H`'s three `} AEP_x; *PAEP_x;` typedefs, which define
an implicit-`int` global in every object, are made `} AEP_x, *PAEP_x;`
(each edit named and counted by the script, which fails if it matches
other than three times).

Three things the layer needs have no Watcom-compilable header: VMM's
`VxD_Desc_Block` (`VMM.H` assumes Microsoft's compiler), the two
service numbers (`_CopyPageTable`, VMM `0x0001`/`0x0061`; `IOS_Register`,
IOS `0x0010`/`0x0007`), and the two SRB fields section 4.3 reads by
offset (`DataTransferLength` `0x10`, `DataBuffer` `0x18`; `SRB.H` brings
the NT port's types with it). They are written in `src/vsd/vsd_vmm.h` under
names of the layer's own, each field and value cited by header and line,
as `src/cm/cm_cfgmg.h` cites `CONFIGMG.H` and `VMM.H` (`legal-provenance.md`
section 4, the CONFIGMG row's practice). No declaration, macro body,
comment or sample code of the DDK is reproduced; `HELPVSD`, the DDK's
sample the prototype was read against for IOS's calling conventions, is
not read for the code.

The image holds this project's code and data alone: no library is
linked, and the linker adds its MS-DOS stub and its `Rich` record of tool
identifiers, as it does for every PE driver of this package
(`legal-provenance.md` section 2 gets a row for `S98VSD.VXD` saying so).

### 6.3 The tree

| Path | What |
|---|---|
| `src/vsd/vsd.c` | the AER, the request routine, the callback, the control handler |
| `src/vsd/vsd_data.c` | the DDB, the DRP, the ILB, the counters (the data object of section 6.1) |
| `src/vsd/vsd_list.c`, `vsd_list.h` | the pure half: the list from page-table entries, the room, the SRB checks, the claim's tests on a DCB's bytes (section 9.1) |
| `src/vsd/vsd_vmm.h` | section 6.2's own declarations, cited |
| `src/vsd/vsd.def` | `VXD S98VSD DYNAMIC`, the segments (`LCODE`, `ICODE`), `S98VSD_DDB @1` |
| `src/vsd/build.cmd` | stage, compile, `omfpub`, `omfcheck`, link, `callcheck`, the gate; `[qemu]` builds the debug-console image; exit 2 "not built" without Open Watcom or the DDK, as `xhciinst\di\build.cmd` |
| `src/vsd/stage-ddk.ps1` | section 6.2 |
| `src/vsd/omfpub.py`, `omfcheck.py`, `callcheck.py` | section 6.1 |
| `src/vsd/check-vsd.ps1`, `vsd-services.allow` | section 6.4 |
| `test/test_vsd_list.c` | the host suite of section 9.1 |

### 6.4 The gate

`scripts\build-driver.cmd` runs `src\vsd\build.cmd` once per run (the
image is the same in every flavour but `qemu`), after Stor98's build,
going on after a 2 with a line that it was not built, as it does for
`XHCIDI.EXE`; `make-release.ps1` refuses a cut without it.
`check-vsd.ps1` holds the image to: an LE image whose one entry is the
DDB named `S98VSD  `; every `int 20h` service dword in its code a row of
`vsd-services.allow` (the two of section 6.2, and nothing else) and
every row used; the stamp `S98VSD.VXD <version>`; no write to port
`0E9h` in the release image (no `out` instruction at all). The package
carries the image beside `stor98.mpd` in each x86 flavour directory that
carries Stor98 (`make-package.ps1`, with `-NoStor98` leaving both out).

## 7. The installer (98 SE and ME)

### 7.1 The box

A Components row of its own (record 26 section 5.3), a **child of
Stor98's box**, indented under it as Stor98's is under the Thunderbolt
box (owner, 2026-10-22):

- one line, "Large transfers for Stor98 (S98VSD.VXD), up to 2 MiB",
  indented under Stor98's sentence; the page stays 223 dialog units high
  (record 26 section 5.4, "640 by 480 on 98 SE") by setting its
  multi-line texts at whole lines (two lines 16 units, three 24), which
  frees the row. The longer sentence ("Lets Windows 98 SE and ME send
  Stor98 reads and writes of up to 2 MiB whole, instead of in 63 KiB
  pieces. Changes nothing for copies through the file cache.") is the
  console's, and the Summary's line says what the run does with the
  file;
- **on 98 SE and ME**: ticked whenever Stor98's box is ticked (ticking
  Stor98 ticks it); open only while Stor98's box is ticked; unticked and
  greyed while Stor98's box is unticked or greyed; shown only when the
  flavour directory carries `S98VSD.VXD`;
- **on every other system**: shown greyed, unticked, its line
  "Large transfers for Stor98 (Windows 98 SE and ME only)", and never taken by the page
  or the engine - the convention record 26 section 5.4 gives a row of
  another system (owner, 2026-10-19, "grey out if system does not
  support that option"; `inst_value_greyed`, `inst_value_only`). This
  row is the owner's exception (2026-10-22) to section 5.3.1's "every box
  shown and tickable on every system": the layer is a 9x IOS component
  with nothing to install elsewhere. Shown greyed rather than hidden, so
  the page reads the same on every system, which is that section's aim.

### 7.2 The console and the switches

`/s98vsd` and `/nos98vsd`, in `/stor98`'s and `/nostor98`'s style:
`/s98vsd` preselects the box ticked and needs Stor98 in the run
(`/stor98`, or Stor98 ticked by the Thunderbolt support), and is refused
otherwise; `/nos98vsd` preselects it unticked. On any system but 98 SE
and ME **both are refused** with the console's refusal text,
"/s98vsd and /nos98vsd are for Windows 98 SE and ME only", as the
owner asked; the window refuses them on its welcome page in the same
words (record 26 section 10). The console tool asks, on 98 SE and ME when
Stor98 is in the run and neither switch is given, "Install S98VSD.VXD,
large transfers for Stor98? (Y/n)" after Stor98's question; Enter and
`/yes` give Y.

### 7.3 What a run does with it

The file is the installer's to place: no INF names it, and nothing in the
setup engine copies it. On 98 SE and ME:

- **First, the old name** (section 3.1), on every run, the uninstall
  and an untick included: `IOSUBSYS\STOR98.VXD`, when it is this
  project's (the record's `File` or `Pending` bytes, or an image with the
  device name `S98VSD` and the stamp `STOR98.VXD `, as the test cuts and
  the first bench placed it), is deleted, and a `WININIT.INI` line
  `STOR98.VXD=S98VSD.TMP` (a test cut's queued update) is taken back; the
  `.TMP` is then the table's as below. Another program's `STOR98.VXD` is
  left, and the Summary says Stor98 cannot start while it is there. A
  delete that fails stops the layer's part and leaves a step.

- **Ticked, `IOSUBSYS\S98VSD.VXD` absent**: the record's `Pending`
  value written with the package file's hash (the intent, section 7.4),
  then `IOSUBSYS\S98VSD.TMP` written with `CREATE_NEW`, compared byte for
  byte with the package's, then moved to `S98VSD.VXD` with `MoveFileA`,
  which never replaces, then `File` takes the hash and `Pending` is
  deleted (record 26 section 6.5.4's sequence, with the intent first).
  `.TMP` is not `*.vxd`, so IOS never loads the half-written file
  (section 2.2 item 1). A step that fails is recovered by the table
  below; Stor98 is unaffected.
- **Ticked, present with the package's bytes**: nothing is copied or
  recorded (a file this installer placed is already `File`'s; one of
  these bytes `File` does not name stays another hand's).
- **Ticked, present with other bytes, `File`'s** (an
  older release): updated as Stor98's own file is (record 26 section 6.3,
  record 23 revision 23): `S98VSD.TMP` beside it and a `WININIT.INI`
  `[rename]` entry, since the loaded copy is IOS's for the session:
  `Pending` written with the new hash first, then the `.TMP`
  (`CREATE_NEW`), compared, then the entry
  `<system>\IOSUBSYS\S98VSD.VXD=<system>\IOSUBSYS\S98VSD.TMP`; `File`
  keeps the hash of the file in place until the start has done the
  rename (section 7.4).
- **Ticked, present with bytes neither value names**: another
  installation's file of that name. Left as it is, said on the Summary
  and in the log, and never recorded; the row's note says so.
- **Unticked by the user on 98 SE or ME, while Stor98's box is ticked,
  over a file of `File`'s bytes**: removed (owner, 2026-10-22: "unticking
  it on an update removes it"). A queued update is withdrawn first (the
  table below), then the file is deleted (`DeleteFileA`), then `File`. A
  delete that fails is a step with the restart flag, "restart and run the
  installer again", as record 26 section 9.3 leaves a driver file in use.
  The loaded copy keeps serving the disks until the shutdown, which is
  harmless (it is the same layer that was installed).
- **Greyed because Stor98's box is unticked**: `S98VSD.VXD` follows
  Stor98 (owner, 2026-10-22): whatever the run does to the bundled
  Stor98 with its box unticked, it does to `S98VSD.VXD` too, so the two
  never disagree. Today an unticked Stor98 is "not now" and stays (record
  26 section 5.3), so an installed `S98VSD.VXD` stays with it; a rule that
  removes Stor98 in such a run (now or later) removes `S98VSD.VXD` in the
  same run, by the deselected column of the table below. Without
  Stor98's disks it claims nothing (section 3.3).

**Every run first recovers** (before it classifies the file), from five
facts: the destination's bytes (absent, `File`'s hash, `Pending`'s hash,
other), the `.TMP`'s (absent, `Pending`'s or `File`'s hash, other),
the state of `WININIT.INI`'s `[rename]` section for the two files -
**absent** (no line names either file), **exact** (this installer's one
line above, both paths compared without regard to case, and no other
line naming either file) or **conflicting** (any other line naming
either file, with or without the exact one; the section is read from the
file's own bytes, after 9x's profile cache is flushed to it, never
through `GetPrivateProfileSectionA`, under which an update over older
bytes on 98 SE and ME never saw its own line and repeated the write until
the run failed, task 43.80) - the record's values, and
the run's **mode** for the layer:

- **selected**: the box ticked, its target the package's file (hash `H`);
- **deselected**: the box unticked by the user while Stor98's box is
  ticked;
- **inactive**: the box greyed because Stor98's box is unticked or greyed,
  and the run keeps Stor98 (D8: nothing of the layer is changed); a run
  that removes Stor98 with its box unticked takes the **deselected**
  column instead (D8, owner, 2026-10-22: the layer follows Stor98);
- **uninstall**.

Only a file whose bytes a value names, and only that exact entry, are
ever this installer's; anything else is left, said and logged. Every
multi-step change runs in one order and stops at the first failure:
`Pending` is written first and deleted last, `File` is written before
the leftovers are removed.

| `Pending` | Destination | `.TMP` | Entry | Meaning | selected | deselected, uninstall | inactive |
|---|---|---|---|---|---|---|---|
| absent | - | absent | absent | nothing in flight | classify the destination by `File`, then place or update as above | remove a destination of `File`'s bytes | nothing |
| absent | `File`'s hash | `File`'s hash, or absent | present, or absent with a `.TMP` | the leftovers of a finished step | remove the entry, then the `.TMP`; then as the first row | the same, then as the first row | the same (only leftovers this installer's values name, of a step already finished) |
| absent | any | other bytes | any | a `.TMP` that is not this installer's, nothing of its own in flight | left and said; the exact entry removed; a placement or an update blocked (a step: the user removes the file) | the destination of `File`'s bytes removed as the first row says; the `.TMP` left | the exact entry removed only |
| set | `Pending`'s hash | absent, `Pending`'s hash or other | any | the start (or the move) did it | `File` takes the hash; then the entry and a `.TMP` of that hash removed (a `.TMP` of other bytes left, the row above then applying); then `Pending` deleted; then as the first row | the same, then as the first row | the same, then nothing |
| set | absent or `File`'s hash | `Pending`'s hash | present | an update queued | kept when `Pending` is `H`; otherwise withdrawn (entry, `.TMP`, `Pending`) and `H` placed or queued afresh | withdrawn, then as the first row | nothing: it stays queued |
| set | absent or `File`'s hash | `Pending`'s hash | absent | cut short before the entry (an update) or the move (a first placement) | when `Pending` is `H`, finished (the entry written, or the `.TMP` moved into an absent destination); otherwise withdrawn and `H` placed afresh | withdrawn, then as the first row | nothing |
| set | absent or `File`'s hash | absent | any | cut short before the `.TMP` was whole, or the `.TMP` gone | the exact entry removed; `Pending` deleted; the step started again with `H` | the same, then as the first row | the same (nothing of the layer is left in flight to keep) |
| set | any | other bytes | any | a `.TMP` that is not this installer's | left; the exact entry removed (it would rename a foreign file); no placement or update until the user removes it, a step that says so | the same, then as the first row for the destination | the exact entry removed only |
| any | other bytes, not `Pending`'s | - | not conflicting | another installation's file | left and said; the exact entry removed; a `.TMP` of `Pending`'s hash deleted, then `Pending` deleted; `File` deleted when it names no file present | the same | the exact entry removed only |
| any | any | any | conflicting | another program's rename names one of the files | every line left as it is, said; no placement, update or queue change (a step: the user resolves it, then runs again) | a destination of `File`'s bytes deleted, a `.TMP` of `Pending`'s hash deleted, then the values; the foreign lines left and said (they may put a file there at the next start) | nothing |

The rows are read top to bottom, the conflicting row first of all. In the other rows, an entry "present" is the exact one. A
step that fails leaves the record and the state for the next run (an
uninstall that cannot finish says so and keeps the key). The table's every row is a state some step cut short can leave, and each
of its actions, cut short in turn, leaves another row of it; section 9.1
holds the table and every cut. So an untick or an uninstall over a queued
update withdraws it before it removes the destination of `File`'s hash,
also when the destination is absent, and a queued file can never arrive
after the user removed the layer; a later package (`H` not `Pending`)
replaces a queued one rather than letting the start install the older.

Any run that places, updates or removes the file ends with the shutdown
or restart flag Stor98's file update uses: the layer is loaded at boot
only. The Summary lists it as its own line ("Large transfers for Stor98:
installed / updated at the next start / removed / left: another
installation's file").

### 7.4 The record and the uninstall

A key of its own, `HKLM\Software\xhci98\Stor98Vxd`, apart from the
bundled Stor98's record (`HKLM\Software\xhci98\Stor98`, record 26
section 6.3), whose `PreExisting` baseline and devices it must not
change the meaning of: a run that places only `S98VSD.VXD` (Stor98 at the
package version already, or a Stor98 installed by hand) writes nothing
under Stor98's key, and Stor98's own rules read that key as before.
Values:

- `File` (`REG_SZ`): the SHA-256 of the `S98VSD.VXD` bytes this
  installer placed and that are in place, written once the file is in
  place (the move done, or the start's rename seen), deleted after the
  file;
- `Pending` (`REG_SZ`): the SHA-256 of the package's file a placement or
  an update is about to put in place, written before the `.TMP` is
  created, deleted once `File` names those bytes or the step is
  withdrawn.

The uninstall (record 26 section 9.3 step 4, with the driver files)
recovers by section 7.3's table, withdraws a queued update, removes a
`S98VSD.VXD` whose bytes `File` names by the delete rule of section 7.3,
whether or not Stor98 itself stays, leaves a file of other bytes and
says so, then deletes the values and the key.

### 7.5 What the code changes in `xhciinst`

Narrow, since other branches edit the installer: one component bit and
its decision in `inst_core.c` (`inst_vsd_decide`: the defaults, the
child rule, the greyed rule, the switches and their refusals,
host-tested), the row in the window's Components template and its
greying, the console question, the placement, update and removal of
section 7.3 in `inst_eng.c` beside Stor98's own file steps with the
recovery table as a pure function host-tested, the `Stor98Vxd` key, and
the package's file list. Nothing of the device install, the
helper or the NT route changes.

## 8. The release notes

To say, in these words or close: "On Windows 98 SE and ME, S98VSD.VXD
lets reads and writes of up to 2 MiB reach Stor98 whole, which helps
large unbuffered transfers (disk benchmarks, some copy and imaging
tools). It does not change copies through the file cache, whose
requests are 64 KiB or smaller, and it does not change queueing; the
gains were measured with one program reading and writing one request at
a time. Requests over 2 MiB are still cut into 63 KiB pieces." And what
no vehicle has read (section 11).

## 9. Verification

### 9.1 Host vectors (`test\test_vsd_list.c`, `test\run-host-tests.cmd`)

The pure half, DDK-free: page-table entries in, runs out. A linear buffer
at offsets 0, 1, 512 and 4095 over 1 to 513 pages, contiguous,
alternating and reversed frames; logical lists of 1 to 17 fragments,
fragments that merge across their boundary and that do not; byte and
sector counts with shifts 9 and 12; the room exactly full and one over;
a page not present at the first, a middle and the last page; a wrap of
address plus length; the SRB checks (total below, equal to and above
`DataTransferLength`, `DataBuffer` equal and unequal to the first byte);
the claim's tests on DCB bytes (`stor98`, `STOR98`, `stor98x`, `nvme2k`,
a full eight-character name, a logical DCB, no `phys_sgd`, a maximum over
2 MiB, already claimed); section 4.1's function table, with SRB data on
a no-data function and on a zero count refused. The checks of section
4.2: the page-table service failing with the scratch prefilled with
present, wrong entries (refused, no entry read); sixteen 512-byte
fragments and one of 2 MiB - 8 KiB, each starting at page offset 4092,
on discontiguous frames (543 pages, within the room of 1,022); 255
fragments and a terminator, 255 and a 256th non-zero descriptor; a
fragment count of 0 as the first descriptor under a non-zero count;
fragments each whole while `DataBuffer + DataTransferLength`, or the first
byte plus the count, wraps (with and without an SRB); a
list covering fewer, as many and more bytes than `IOR_xfer_count`, with
and without an SRB; a sector count whose shift overflows; a sum over
`DCB_max_xfer_len`; a descriptor pointer that wraps; the callback table
full and one free. The installer's recovery table of section 7.3,
every row, and every step of a placement and of an update cut short,
an altered `.TMP`, an altered or extra `WININIT.INI` line (a line
renaming another file onto `S98VSD.VXD`, a line naming `S98VSD.TMP` as a
destination), and a queued update whose destination another installer
replaced; and the request side's admission of a no-SRB request by
function, a function not admitted refused. And the installer's
decision (`inst_vsd_decide`) in the installer's host suite: every system
by every state of Stor98's box and both switches.

### 9.2 The legs

On the emulator (`usb4-emu`'s QEMU under TCG, one guest at a time,
256 MB, the NVMe behind the emulated tunnel), Stor98 installed by the
installer so that its file is `stor98.mpd` and its port name `stor98`:

1. **Data at every size up to the maximum and over it**: `PERF2` file
   phases at 64 KiB to 2 MiB and at 4 MiB (IOS's cut of a request over
   the maximum, with the IOP reused for each piece), aligned, at +512
   and at an odd offset (+1, the double buffer), on 98 SE (QuickInstall
   IOS) and ME, every verify clean and every refusal counter 0. The
   buffers' fragmentation is verified, not assumed (an offset and a
   fragmentation are different properties, `runs/run-43.md`): the
   `qemu` build's most-runs counter must read over 17 and over 255 runs
   in the phases that claim them, with a test-tool mode that fragments
   its buffer on purpose (pages allocated and freed in a pattern before
   the buffer) where the default buffer does not reach it. A run with
   Stor98's `STOR98_TEST_BOUNCE` build (every data child bounced, both
   directions) repeats the 2 MiB phases.
2. **The cost**: the same phases, Stor98 alone against Stor98 with the
   layer, back to back with the host load recorded (section 4.4).
   Dropped for 3.2.0.0 (owner, 2026-10-22): the first real-hardware
   reading below settles that the layer ships. A cost or speed task opens
   after 3.2.0.0 only if Windows 7 reads the same drive much faster at one
   request (Q1T1).
3. **Insertion order**: the chain and `DCB_dmd_flags` at the first
   request on the gold 98 SE IOS set, on ME, and on each with the disk
   present at boot (an enclosure attached at power-on) as well as plugged
   after (section 3.4).
4. **Coexistence**: `S98VSD.VXD` and `stor98.mpd` in one `IOSUBSYS`, both
   loaded (the layer's init line, Stor98's disk running); a disk of
   another driver (the emulator's IDE disk) left unclaimed.
5. **Pass-through, SMART and the requests without an SRB**: Stor98's
   `tests/scsichk` and `tests/logsense` through the layer on 98 SE and
   ME; the `qemu` build's per-function counts of requests that arrived
   without `IORF_SRB_VALID`, and of `IOR_SCSI_REQUEST`, read after a boot,
   a format of the disk and the file phases, each such path then run
   with data compared where it carries data.
6. **Surprise removal**: the tunnel pulled during a 2 MiB write, then a
   2 MiB read, on 98 SE and ME (section 4.8).
7. **Install and uninstall**: a first install ticked; an update over it
   (same bytes, older bytes); unticked on an update; the uninstall; each
   read back from `IOSUBSYS`, `WININIT.INI` and the record, on 98 SE and
   ME; an update then an untick, and an update then the uninstall, with
   no start between (the queued file must not arrive); a placement and
   an update each cut short at every step of section 7.3 (the guest
   stopped there), then the next run; `S98VSD.VXD` placed over a Stor98
   installed by hand, then updated and uninstalled, Stor98's key read
   unchanged; `/s98vsd` and `/nos98vsd` refused on 2000.
8. **Memory pressure** (section 4.7): the 2 MiB phases while a second
   program commits and touches more memory than the guest has, on 98 SE
   and ME, data compared.
9. **The refusal path**: a fault-injection build of the `qemu` flavour
   (`VSD_TEST_REFUSE`, never packaged: each kind of refusal forced on
   every N-th request of a chosen function, and the callback table
   treated as full) on 98 SE and ME, with requests with and without
   `IORF_SRB_VALID`: the requester gets an error, nothing reaches
   Stor98 for that request (its counters), no hang and no second
   completion, the IOR's fields as they came, and the next request
   compares clean.
10. **`SCSIPORT.PDR`'s own sense fetch** (section 2.3 item 3). Stor98's
   pass-through clients cannot set SRB flags on 9x (Stor98
   `docs/TESTING.md`), so the stimulus is a test-only Stor98 build in the
   pattern of its `STOR98_TEST_BOUNCE`: `STOR98_TEST_NOAUTOSENSE=N`
   completes every N-th read with `SRB_STATUS_ERROR`, check-condition
   status, the sense kept and `SRB_STATUS_AUTOSENSE_VALID` clear, the
   condition on which `SCSIPORT.PDR` fetches the sense itself [hyp: the
   trigger is read in `SCSIPORT.PDR`'s completion path, before the leg,
   and recorded with the provenance row]. On 98 SE with the gold and the
   QuickInstall `SCSIPORT.PDR` and on ME, with this layer installed: the
   build's debug-port log shows a `REQUEST SENSE` arriving with its
   `DataBuffer` inside an IOP of `SCSIPORT.PDR`'s (the evidence that the
   port made it), answered in software, and no `ScsiPortGetPhysicalAddress`
   call for it (the build logs each call's SRB), and the read is retried
   and the file compares clean. The test build is part of the code step's
   scope, under Stor98's `AGENTS.md`; it is never packaged.

**The legs' results (2026-10-23)** [obs]. Each row ran one guest at a time
under TCG with one vCPU and 256 MB. Every verify was 0 unless the row says
otherwise. `runs/run-43.md`, "The legs of section 9.2", has the details.

| Item | Vehicle | Result |
|---|---|---|
| 1, data | 98 SE gold IOS set (the installer's install, the drive plugged after the desktop), 98 SE QuickInstall, ME | 1 and 2 MiB at +0, +512 and on a fragmented buffer: verify 0 and no refusal. With the `qemu` image the lists reached 512 runs at 2 MiB (fragmented) and 257 runs at 1 MiB +512, so the requests reached Stor98 whole. The +1 offset, the 4 MiB phase and the `STOR98_TEST_BOUNCE` arm did not run. |
| 1, data, unaligned (43.79, 2026-10-24) | 98 SE QuickInstall IOS and ME, each from the matrix's installed image, the drive at boot; `S98VSD.VXD` of `7abba43a`, the `qemu` image then the release image, Stor98's free image of the same tree | Before the fix (`3.2.0.0`'s image, the matrix's N3 leg): every dword wrong at +1 for 4, 64 and 128 KiB, both directions, no error; reads at +2, +3, +511 and +4094 returned foreign memory bytes. After it, with each image: a matrix tool of this task's (`unalio`, 4 MiB a case) at offsets 1, 2, 3, 511, 4094, 4095, 0, 4, 512 and 3584 for 512 B, 4 KiB, 64 KiB, 128 KiB and 2 MiB, each a read at the offset after an aligned write and a write at the offset with an aligned verify (100 cases a system), 64 guard bytes either side of the buffer untouched and a 16 MiB sentinel block unchanged; `PERF3` (16 MiB) at +1 for 4, 64 and 128 KiB, +2 and +511 at 128 KiB, +0 at 64 KiB and 2 MiB and +512 at 2 MiB; `N3IO` (8 MiB) write at +1 read at +0, write at +0 read at +1, +2, +3, +511 both ways, +4094 at 4 KiB and +3584 at 1 KiB: every compare 0, no error, no refusal. The `qemu` image printed the first list that came: flags `0x04008441`, its one entry the double buffer's linear address (`0xE433E000` on 98 SE, `0xE3E80000` on ME, equal to `IOR_buffer_ptr`), 512 bytes. Unaligned rates 3 to 10 MB/s (4 KiB pieces), aligned 2 MiB 92 to 167 MB/s. |
| 2, the cost | - | dropped for 3.2.0.0 (owner, 2026-10-22) |
| 3, insertion | all three, plugged after the desktop; QuickInstall with the disk present at boot | the claim at load group `0x11` with demand `0x609`, above `SCSIPORT.PDR`'s `0x14` with `0xE09`, every time |
| 4, coexistence | all three | `S98VSD.VXD` and `stor98.mpd` both loaded; the IDE disk left (`ESDI_506`) |
| 5, pass-through and SMART | QuickInstall, `qemu` image | `t9x aspi` (ASPI: INQUIRY, VPD 80h, READ CAPACITY, LOG SENSE 2Fh, ATA PASS-THROUGH IDENTIFY and SMART READ DATA, READ/WRITE(10) at five offsets) 0 failures; `t9x abs` (VWIN32 absolute) and `t9x file` 0 failures. The per-function counts of requests without `IORF_SRB_VALID` were not read. |
| 6, surprise removal | ME | the enclosure pulled twice during 2 MiB phases: the requester got errors, with no hang; after a replug, 1 and 2 MiB verified 0 |
| 7, install and uninstall | 98 SE gold, console route; ME install | install; an update over the same bytes; `/nos98vsd`; `/s98vsd`; the old name with a test cut's queued update, and the old name alone; the uninstall: each read back from IOSUBSYS, `WININIT.INI` and the record (section 7.3). Not run: the window route, an update over older bytes, a placement cut short at each step, and `/s98vsd` on 2000 (host vectors only). |
| 8, memory pressure | QuickInstall, `qemu` image | `PERF2 ... eat=230` (230 MiB committed and touched beside the phases, more than the guest has): 1 and 2 MiB at +0 and fragmented, verify 0 |
| 9, refusal path | QuickInstall, `VSD_TEST_REFUSE=37`, kinds 1 (no list) and 2 (callback table full) | 121 refusals each; the requests were retried above the layer; verify 0, no hang |
| 10, `SCSIPORT.PDR`'s sense fetch | `STOR98_TEST_NOAUTOSENSE=7` with the gold and the QuickInstall `SCSIPORT.PDR`, and ME | every injected CHECK CONDITION (323, 330 and 320) was followed by a REQUEST SENSE with a `DataBuffer` in VxD memory, not the caller's; the read was retried and verify was 0. The `ScsiPortGetPhysicalAddress` check was not made. |

The free memory's state, as roadmap 43.50 found it: on 98 SE, a buffer
freed and allocated again comes back in reversed page order, so the
physical runs depend on what ran before in the same boot. The emulator
readings above were each a new boot's first runs, except where a row
says "fragmented" (`PERF2`'s own every-other-page pattern) or the row
reports a second run in the same boot. On the P14s, the "no layer" arm
ran `PERF2` after an XHCIPERF run in the same boot, so it may be the
double-buffered case. The 4 to 5 times is then the gain in that
realistic, fragmented state, and may be larger than against a fresh
boot's first run.

On the owner's real hardware: the throughput before and after
(roadmap 43.46's last clause), the owner's.

**The first real-hardware reading** (owner, 2026-10-22; `runs/run-43.md`,
"The first real-hardware reading") [obs]. The machine is a ThinkPad P14s
running 98 SE 4.10.2222 A, 32 GB under R. Loew's PATCHMEM, with the IOS of
2001-01-09, the gold `SCSIPORT.PDR` and a 2007 `DISKTSD.VXD` (ME's by its
size). The drive is an NVMe drive behind the USB4 tunnel, FAT32.

With the 2 MiB `stor98.mpd` in both arms, the layer as `S98VSD.VXD`
started cleanly. `PERF2` (unbuffered, write-through, 256 MiB, +0) read
every verify 0 and every bad count 0, in MB/s:

| Request | write, no layer | write, layer | read, no layer | read, layer |
|---|---|---|---|---|
| 128 KiB | 104.0 | 220.1 | 142.4 | 350.6 |
| 256 KiB | 190.1 | 533.3 | 170.5 | 541.2 |
| 512 KiB | 210.8 | 682.6 | 187.8 | 785.2 |
| 1 MiB | 222.4 | 800.0 | 200.0 | 996.1 |
| 2 MiB | 229.3 | 876.7 | 209.1 | 1,122.8 |

At +512 the rates were within 5 % of these (2 MiB: 836.6 write, 1,075.6
read). Requests under 512 KiB gain too. On the physical-list path
`SCSIPORT.PDR` caps the DCB's `DCB_max_sg_elements` at 17 [static: the
gold build, object 2 offset `0x1A1D`, `min(breaks, 0x11)`], so IOS cuts
any buffer of more than 17 physical runs. On a 32 GB machine a 128 KiB
buffer's 32 pages are seldom that contiguous; in the 256 MB emulator
they mostly are, and the emulator gained less. That the layer claimed is
inferred from these rates: the release image writes no log.

This run counts as the layer's pass on real hardware (owner, 2026-10-22);
no confirming run with the second bench package is needed. The run's file
was the first package's image renamed (SHA-256
`1b04c554fb4524d53cf91c715075ae9c3a5d5d5cf51771662e77c5addcb60e47`). The
image built from `7bff66a2` and shipped differs from it only in the 10-byte
stamp, `S98VSD.VXD` where it read `STOR98.VXD` (SHA-256
`85c2ac4b7488ca745e53d0293441a83389c9341189e6cfa08d5af7fda583b9f3`).

## 10. Decisions taken in writing this (the orchestrator's, owner to confirm)

- **D1** `nvme2k` is not matched; the legs install Stor98 under its own
  file name (section 3.3).
- **D2** No fallback that edits other layers' calldown entries; a
  `phys_sgd` left demanded is counted and harmless (section 3.4).
- **D3** A list for every request with data whatever its function, from
  IOS's own builder's inputs (section 4.1).
- **D4** The SRB coverage and first-byte checks, refusing what IOS would
  have sent (section 4.3).
- **D5** One `_CopyPageTable` call a fragment; no skipping is possible,
  and the cost is measured (section 4.4).
- **D6** Room sized from the DCB at insertion, a DCB over 2 MiB left
  alone (sections 3.3 and 3.4).
- **D7** The row greyed with "(Windows 98 SE and ME only)" rather than
  hidden on other systems; `/s98vsd` and `/nos98vsd` both refused
  there (section 7.1, 7.2).
- **D8** (owner, 2026-10-22) `S98VSD.VXD` follows Stor98's rule: with
  Stor98's box unticked, the run does to the layer what it does to Stor98,
  keeping it when Stor98 is kept and removing it when Stor98 is removed;
  with Stor98 ticked, the user's untick of the layer's box removes it
  (section 7.3).
- **D9** An update of an installer-placed file through `WININIT.INI`, as
  Stor98's file; a removal by a direct delete (section 7.3).
- **D10** The release image carries no debug-port writes; the `qemu`
  flavour's image does (section 4.9).
- **D11** The list's room bounded by the layer's own fragment limit, not
  by IOS's check (section 3.4); the dynamic exit refused (section 3.6).

## 11. Open, and the risks

- Whether section 4.2's builder removes the 12 to 29 % loss on a
  near-contiguous buffer: read on the legs (section 9.2 item 2). If not,
  the owner decides whether the layer ships ticked by default.
- The insertion order on the gold IOS, ME and a disk present at boot
  (section 9.2 item 3); if `phys_sgd` stays demanded, the layer lifts
  no limit there (D2; it still rebuilds each list, section 3.4) and the
  owner decides whether to edit the entries above.
- `IORF_DOUBLE_BUFFER`'s buffer at this layer: read and fixed
  (section 4.6, roadmap 43.79). No real-hardware reading at an
  unaligned buffer yet.
- `SCSIPORT.PDR`'s own requests (section 2.3 item 3): closed by Stor98
  answering `REQUEST SENSE` in software; a condition on Stor98, recorded in
  its `docs/PLAN.md`.
- Page residency (section 4.7): the static reading of the layers above
  this one on Stor98's disk is owed before the code; the memory-pressure
  leg corroborates it.
- About 10 KiB in each IOP of the disk (section 3.4): IOS's allocation of
  IOPs of that size is read on the legs, not in IOS.
- Whether `DeleteFileA` succeeds on a loaded `S98VSD.VXD` (VXDLDR closing
  the file after the load) [hyp]; if not, the removal is the step of
  section 7.3 every time.
- Real hardware: no reading on a controller whose MDTS is over 512 KiB,
  nor with another vendor's VSD in `IOSUBSYS`. Windows 98 FE was not read
  and is not a target.

## 12. Files

| File | Change |
|---|---|
| `src/vsd/` | new (section 6.3) |
| `test/test_vsd_list.c`, `test/run-host-tests.cmd` | the host suite |
| `scripts/build-driver.cmd`, `scripts/package/make-package.ps1`, `make-release.ps1` | the build, the gate, the package's file |
| `xhciinst/inst_core.c`, `inst_core.h`, `inst_eng.c`, `xhciwiz.c`, `xhciwiz.rc` | section 7.5 |
| `src/stor98/` | Stor98's own 2 MiB change (section 5), under its `AGENTS.md` |
| `docs/contributing/design/26-windowed-installer.md` | the Components row, the switches |
| `docs/references/README.md` | the DDK members of section 6.2 |
| `docs/contributing/legal-provenance.md` | section 2's row for the image, section 4's row for these readings |
| `docs/contributing/build-and-test.md`, `source-files.md`, `README.md`, the release notes | the build, the files, the user's text |

## Sources

- `docs/contributing/runs/run-43.md`, "43.46 read first", "The C port",
  "Requests over 512 KiB".
- Stor98 `docs/INVESTIGATION-9X-REQUEST-SIZE.md` (the criteria routine
  and the cut), `docs/PLAN.md` 0.4 ("9x caller buffer"),
  `docs/gates/G9.md` (`NVMEVSD.VXD`, the IOS sets).
- The Windows 98 SE DDK's `IOR.H`, `IOP.H`, `DCB.H`, `ISP.H`, `AEP.H`,
  `ILB.H`, `DRP.H`, `SGD.H`, `BLOCKDEV.H`, `SRB.H` and `VMM.H` (`SRB.H`
  and `VMM.H` for the values `src/vsd/vsd_vmm.h` cites)
  (`docs/references/README.md`, "Windows 98 DDK").
- `legal-provenance.md` section 4: "Task 43.46 read-first", "Task 43.46
  design: what IOS loads from `IOSUBSYS`, in three IOS builds" and "Task
  43.46 design: the layers above Stor98's disk and `SCSIPORT.PDR`'s own
  requests".
- Design records 08 (flavours), 23 (the console tool), 26 (the window,
  sections 5.3, 5.4, 6.3, 6.5.4 and 9.3) and 27 (the 9x device install).
