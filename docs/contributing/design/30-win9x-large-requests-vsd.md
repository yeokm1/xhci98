# Design Record 30 - STOR98.VXD: Large Requests Reaching Stor98 Whole on Windows 98 SE and ME

Status: draft 1, 2026-10-22, before code. Written for `roadmap-usb4.md`
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
- 2026-10-22: its file is named after Stor98, `STOR98.VXD` or `STOR98.VSD`
  by what IOS loads (`STOR98.VXD`, section 3.1), with a device name that
  cannot collide (`S98VSD`).

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

- `STOR98.VXD`, a static IOS VSD for Windows 98 SE (4.10.2222) and ME
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

- **Queue depth 1.** Win32 file I/O on 9x is synchronous, so a single
  program issues one request at a time with or without this layer; the
  owner's Windows 10 figure at Q8T1 has no 9x counterpart.
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
   `rmm.pdr`). Hence `STOR98.VXD`.
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
   `IORF_SRB_VALID`, the SCSI'izer (load group `0x0B`, above this layer)
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

## 3. The layer

### 3.1 The file, its place and its names

- **File**: `STOR98.VXD` in `<system directory>\IOSUBSYS\`, where
  `stor98.mpd` already is (`stor98.inf`'s 98 SE and ME section, DIRID 12).
  The two differ by extension; IOS's directory walk loads `STOR98.VXD`
  (item 1 of section 2.2) and never treats `stor98.mpd` as a VxD, and
  `stor98.mpd` is loaded through its devnode's `PortDriver` as today.
  Neither replaces nor names the other.
- **Device name** (the DDB's `Name`, which VMM and VXDLDR know a device
  by, not the file name): `S98VSD`, padded with spaces to eight
  characters. No Windows 98 SE or ME VxD, and no file of this package,
  uses it; it is not the file's base name, so a module named `STOR98`
  in any loader's list cannot clash with it [hyp, the leg of section
  9.2 item 4 loads both with Stor98 running].
- **DDB**: `Req_Device_Number` `Undefined_Device_ID` (it exports no
  service), `Init_Order` `Undefined_Init_Order`, the version fields from
  `src/xhci_version.h`'s major and minor, the control procedure of
  section 3.6, and `Reference_Data` the DRP (section 3.2), which is how
  IOS finds the registration packet of a VxD it loads [hdr: `DRP.H`].
- **Stamp**: the image carries the string `STOR98.VXD <version>` from
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
   DCBs, and nothing of it can outlive a DCB, section 4.7).

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
  flags), the list, room for `P + G + 1` runs of 8 bytes, and the page
  scratch, `P + 2` dwords, where `P = DCB_max_xfer_len / 4096` and `G =
  DCB_max_sg_elements`. `P + 1` runs bound a linear buffer of
  `DCB_max_xfer_len` bytes at any offset; each further fragment of a
  logical list can add one run, and IOS refuses a logical list of more
  than `G` fragments before this layer sees it (section 2.2 item 2).
  At Stor98's 2 MiB and `G` 17: 531 runs, about 6.3 KiB a request
  [obs: a 520-run area worked at 2 MiB in the BG arm; 200 overflowed].

After the insertion the AER reads `DCB_dmd_flags` again. If
`phys_sgd` is still there (an entry above this one inserted earlier
kept it), the layer edits nothing: IOS then builds every list itself,
each request arrives with `IORF_PHYS_SGDS`, and the layer passes it down
untouched (section 4.1), which is today's behaviour; the case is counted
and printed in the `qemu` build (section 4.8). The prototype's fallback,
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
3.2); every other message is success. The two services it calls,
`_CopyPageTable` (VMM) and `IOS_Register` (IOS), are reached through
`#pragma aux` byte sequences: `int 20h` and the service's dword, which
VMM patches into a direct call on first use [hdr: `VMM.H`'s
`VMMCall`/`VxDCall`]; both are C-callable (`cdecl`) services, so their
parameters are ordinary C arguments.

## 4. The request path

### 4.1 Which requests get a list

The request routine looks at each IOP in this order:

1. `IORF_PHYS_SGDS` already set (IOS or a layer above built the list):
   passed down untouched.
2. `IOR_xfer_count` 0, or a function that carries no data by its
   definition [hdr: `IOR.H`] - `IOR_CANCEL`, `IOR_MEDIA_CHECK`,
   `IOR_MEDIA_CHECK_RESET`, `IOR_LOAD_MEDIA`, `IOR_EJECT_MEDIA`,
   `IOR_LOCK_MEDIA`, `IOR_UNLOCK_MEDIA`, `IOR_CLEAR_QUEUE`,
   `IOR_DOS_RESET`, `IOR_RESTART_QUEUE`, `IOR_ABORT_QUEUE`,
   `IOR_SPIN_DOWN`, `IOR_SPIN_UP`, `IOR_FLUSH_DRIVE`,
   `IOR_FLUSH_DRIVE_AND_DISCARD`: passed down untouched.
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

For each fragment (the one linear buffer, or each entry of the logical
list up to its zero-count terminator):

1. one `_CopyPageTable` call for all the pages the fragment touches
   (`(offset + bytes + 4095) / 4096` of them from the fragment's first
   page) into the page scratch, as IOS's builder makes one call per
   fragment; a fragment of more pages than the scratch holds is a
   refusal (it cannot happen within `DCB_max_xfer_len`);
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
zero, two reads of `IOP_srb` hold before the request goes down:

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
9.2 read it]. Without `IORF_SRB_VALID` (no SRB yet; `SCSIPORT.PDR`
builds its own from the IOR [hyp]) the list is built and sent as it is.

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
alone against Stor98 with this layer, back to back); if a loss of more
than the run-to-run spread remains on the near-contiguous phases, the
record says so and the owner decides whether it ships (section 11).

### 4.5 Refusals

A refusal sets `IOR_status` to `IORS_INVALID_COMMAND` ("command not
supported or invalid" [hdr: `IOR.H`]), puts the IOR's
fields back as they came, and completes the request through the callback
above this layer at once; nothing goes down. Each kind is counted (the
present bit, the room, the scratch, a wrap, the SRB's coverage, the SRB's
first byte). None was seen in any reading [obs: overflow 0 in every arm
but the first BG try, whose 200-run room was too small]. A refusal is a
failed read or write for the requester, which the file system reports
as it reports a disk error, so section 9.2's legs require every counter
to stay 0.

### 4.6 Pass-through, SMART and `IORF_DOUBLE_BUFFER`

- **Pass-through** (`IOR_SCSI_PASS_THROUGH`, ASPI and the like) and
  **`IOR_SCSI_REQUEST`**: item 3 of section 4.1. They carry their SRB
  from the requester, with a linear buffer [obs, Stor98 PLAN 0.4], so
  the list is the buffer's runs and section 4.3's checks hold it to the
  SRB. Stor98's SMART answers on 9x go through pass-through SRBs to the
  same path (Stor98 `docs/TESTING.md`, `tests/scsichk` and
  `tests/logsense`); section 9.2 item 5 runs both through this layer.
- **`IORF_DOUBLE_BUFFER`**: IOS marks a request whose buffer is
  misaligned for the alignment the DCB demands, which this layer keeps
  demanding (section 3.4), and serves it from a buffer of its own
  [static]; this layer builds the list from whatever `IOR_buffer_ptr`
  holds when the request reaches it, the double buffer then [hyp:
  IOS substitutes the buffer above the calldown chain; the odd-offset
  leg of section 9.2 item 1 reads it with data compared].

### 4.7 Surprise removal, link-down and `AEP_UNCONFIG_DCB`

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

### 4.8 Counters and the `qemu` build

The counters (lists built; passed with `IORF_PHYS_SGDS`; passed with no
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

| Stor98's 9x maximum | Without `STOR98.VXD` | With `STOR98.VXD` |
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
`incs\` members `INC_WIN98_*.H`), `IOP.H`, `DCB.H` and `SRB.H` (the
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

Two layouts the layer needs have no Watcom-compilable header: VMM's
`VxD_Desc_Block` (`VMM.H` assumes Microsoft's compiler) and the two
service numbers (`_CopyPageTable`, VMM `0x0001`/`0x0061`; `IOS_Register`,
IOS `0x0010`/`0x0007`). They are written in `src/vsd/vsd_vmm.h` under
names of the layer's own, each field and value cited by header and line,
as `src/cm/cm_cfgmg.h` cites `CONFIGMG.H` and `VMM.H` (`legal-provenance.md`
section 4, the CONFIGMG row's practice). No declaration, macro body,
comment or sample code of the DDK is reproduced; `HELPVSD`, the DDK's
sample the prototype was read against for IOS's calling conventions, is
not read for the code.

The image holds this project's code and data alone: no library is
linked, and the linker adds its MS-DOS stub and its `Rich` record of tool
identifiers, as it does for every PE driver of this package
(`legal-provenance.md` section 2 gets a row for `STOR98.VXD` saying so).

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
every row used; the stamp `STOR98.VXD <version>`; no write to port
`0E9h` in the release image (no `out` instruction at all). The package
carries the image beside `stor98.mpd` in each x86 flavour directory that
carries Stor98 (`make-package.ps1`, with `-NoStor98` leaving both out).

## 7. The installer (98 SE and ME)

### 7.1 The box

A Components row of its own (record 26 section 5.3), a **child of
Stor98's box**, indented under it as Stor98's is under the Thunderbolt
box (owner, 2026-10-22):

- label "Large transfers for Stor98 (STOR98.VXD)", sentence "Lets
  Windows 98 SE and ME send Stor98 reads and writes of up to 2 MiB
  whole, instead of in 63 KiB pieces. Changes nothing for copies through
  the file cache.";
- **on 98 SE and ME**: ticked whenever Stor98's box is ticked (ticking
  Stor98 ticks it); open only while Stor98's box is ticked; unticked and
  greyed while Stor98's box is unticked or greyed; shown only when the
  flavour directory carries `STOR98.VXD`;
- **on every other system**: shown greyed, unticked, its sentence
  replaced by "(Windows 98 SE and ME only)", and never taken by the page
  or the engine - the convention record 26 section 5.4 gives a row of
  another system (owner, 2026-10-19, "grey out if system does not
  support that option"; `inst_value_greyed`, `inst_value_only`). This
  row is the owner's exception (2026-10-22) to section 5.3.1's "every box
  shown and tickable on every system": the layer is a 9x IOS component
  with nothing to install elsewhere. Shown greyed rather than hidden, so
  the page reads the same on every system, which is that section's aim.

### 7.2 The console and the switches

`/stor98vxd` and `/nostor98vxd`, in `/stor98`'s and `/nostor98`'s style:
`/stor98vxd` preselects the box ticked and needs Stor98 in the run
(`/stor98`, or Stor98 ticked by the Thunderbolt support), and is refused
otherwise; `/nostor98vxd` preselects it unticked. On any system but 98 SE
and ME **both are refused** with the console's refusal text,
"/stor98vxd and /nostor98vxd are for Windows 98 SE and ME only", as the
owner asked; the window refuses them on its welcome page in the same
words (record 26 section 10). The console tool asks, on 98 SE and ME when
Stor98 is in the run and neither switch is given, "Install STOR98.VXD,
large transfers for Stor98? (Y/n)" after Stor98's question; Enter and
`/yes` give Y.

### 7.3 What a run does with it

The file is the installer's to place: no INF names it, and nothing in the
setup engine copies it. On 98 SE and ME:

- **Ticked, `IOSUBSYS\STOR98.VXD` absent**: written as
  `IOSUBSYS\S98VSD.TMP` with `CREATE_NEW`, compared byte for byte with the
  package's, the record's `Vsd` value written (section 7.4), then moved
  to `STOR98.VXD` with `MoveFileA`, which never replaces (record 26
  section 6.5.4's sequence). `.TMP` is not `*.vxd`, so IOS never loads the
  half-written file (section 2.2 item 1). A failed step deletes the
  `.TMP` and the value; Stor98 is unaffected.
- **Ticked, present with the package's bytes**: nothing is copied or
  recorded (a file this installer placed is already in the record; one
  of these bytes the record does not name stays another hand's).
- **Ticked, present with other bytes, recorded as this installer's** (an
  older release): updated as Stor98's own file is (record 26 section 6.3,
  record 23 revision 23): `S98VSD.TMP` beside it and a `WININIT.INI`
  `[rename]` entry, since the loaded copy is IOS's for the session; the
  record's value takes the new hash once the entry is written.
- **Ticked, present with other bytes, not recorded**: another
  installation's file of that name. Left as it is, said on the Summary
  and in the log, and never recorded; the row's note says so.
- **Unticked by the user on 98 SE or ME, while Stor98's box is ticked,
  over a file the record counts as this installer's**: removed (owner,
  2026-10-22: "unticking it on an update removes it"). The file is
  deleted (`DeleteFileA`); a pending `[rename]` entry and `S98VSD.TMP` of
  an earlier run go with it; the value is deleted after the file. A
  delete that fails is a step with the restart flag, "restart and run the
  installer again", as record 26 section 9.3 leaves a driver file in use.
  The loaded copy keeps serving the disks until the shutdown, which is
  harmless (it is the same layer that was installed).
- **Greyed because Stor98's box is unticked**: nothing; an installed
  `STOR98.VXD` stays, as an unticked Stor98 is "not now" and stays
  (record 26 section 5.3). Without Stor98's disks it claims nothing
  (section 3.3).

Any run that places, updates or removes the file ends with the shutdown
or restart flag Stor98's file update uses: the layer is loaded at boot
only. The Summary lists it as its own line ("Large transfers for Stor98:
installed / updated at the next start / removed / left: another
installation's file").

### 7.4 The record and the uninstall

Under `HKLM\Software\xhci98\Stor98`, the bundled Stor98's record (record
26 section 6.3), one value: `Vsd` (`REG_SZ`), the SHA-256 of the
`STOR98.VXD` bytes this installer placed, written before the move into
place and deleted after the file. The key may then exist for this value
alone (a run that placed the layer and changed nothing of Stor98). The
uninstall (record 26 section 9.3 step 4, with the driver files) removes a
`STOR98.VXD` whose bytes the value names, with its pending `[rename]`
entry and `.TMP`, by the delete rule of section 7.3, whether or not
Stor98 itself stays; a file of other bytes is left and said. Then the
value, and the key by record 26's rule.

### 7.5 What the code changes in `xhciinst`

Narrow, since other branches edit the installer: one component bit and
its decision in `inst_core.c` (`inst_vsd_decide`: the defaults, the
child rule, the greyed rule, the switches and their refusals,
host-tested), the row in the window's Components template and its
greying, the console question, the placement, update and removal of
section 7.3 in `inst_eng.c` beside Stor98's own file steps, the `Vsd`
value, and the package's file list. Nothing of the device install, the
helper or the NT route changes.

## 8. The release notes

To say, in these words or close: "On Windows 98 SE and ME, STOR98.VXD
lets reads and writes of up to 2 MiB reach Stor98 whole, which helps
large unbuffered transfers (disk benchmarks, some copy and imaging
tools). It does not change copies through the file cache, which Windows
98 SE and ME make in 64 KiB pieces, or the queue depth, which is one
request at a time on these systems. Requests over 2 MiB are still cut
into 63 KiB pieces." And what no vehicle has read (section 11).

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
2 MiB, already claimed); section 4.1's function table. The installer's
decision (`inst_vsd_decide`) in the installer's host suite: every system
by every state of Stor98's box and both switches.

### 9.2 The legs

On the emulator (`usb4-emu`'s QEMU under TCG, one guest at a time,
256 MB, the NVMe behind the emulated tunnel), Stor98 installed by the
installer so that its file is `stor98.mpd` and its port name `stor98`:

1. **Data at every size up to the maximum**: `PERF2` file phases at 64 KiB
   to 2 MiB, aligned, at +512 and at an odd offset (+1, the double
   buffer), on 98 SE (QuickInstall IOS) and ME, every verify clean and
   every refusal counter 0.
2. **The cost**: the same phases, Stor98 alone against Stor98 with the
   layer, back to back with the host load recorded (section 4.4).
3. **Insertion order**: the chain and `DCB_dmd_flags` at the first
   request on the gold 98 SE IOS set, on ME, and on each with the disk
   present at boot (an enclosure attached at power-on) as well as plugged
   after (section 3.4).
4. **Coexistence**: `STOR98.VXD` and `stor98.mpd` in one `IOSUBSYS`, both
   loaded (the layer's init line, Stor98's disk running); a disk of
   another driver (the emulator's IDE disk) left unclaimed.
5. **Pass-through and SMART**: Stor98's `tests/scsichk` and
   `tests/logsense` through the layer on 98 SE.
6. **Surprise removal**: the tunnel pulled during a 2 MiB write, then a
   2 MiB read, on 98 SE and ME (section 4.7).
7. **Install and uninstall**: a first install ticked; an update over it
   (same bytes, older bytes); unticked on an update; the uninstall; each
   read back from `IOSUBSYS`, `WININIT.INI` and the record, on 98 SE and
   ME; `/stor98vxd` and `/nostor98vxd` refused on 2000.

On the owner's real hardware: the throughput before and after
(roadmap 43.46's last clause), the owner's.

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
  hidden on other systems; `/stor98vxd` and `/nostor98vxd` both refused
  there (section 7.1, 7.2).
- **D8** The box greyed by an unticked Stor98 removes nothing; only the
  user's untick of the box itself removes the file (section 7.3).
- **D9** An update of an installer-placed file through `WININIT.INI`, as
  Stor98's file; a removal by a direct delete (section 7.3).
- **D10** The release image carries no debug-port writes; the `qemu`
  flavour's image does (section 4.8).

## 11. Open, and the risks

- Whether section 4.2's builder removes the 12 to 29 % loss on a
  near-contiguous buffer: read on the legs (section 9.2 item 2). If not,
  the owner decides whether the layer ships ticked by default.
- The insertion order on the gold IOS, ME and a disk present at boot
  (section 9.2 item 3); if `phys_sgd` stays demanded, the layer is inert
  there (D2) and the owner decides whether to edit the entries above.
- `IORF_DOUBLE_BUFFER`'s buffer at this layer (section 4.6) [hyp].
- The room's bound of `G` fragments (section 3.4) rests on IOS's logical
  list check (section 2.2 item 2) [hyp for every caller]; a list over the
  room is a counted refusal, and the legs' VCACHE copies and file phases
  read that counter at 0.
- Whether `DeleteFileA` succeeds on a loaded `STOR98.VXD` (VXDLDR closing
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
  `ILB.H`, `DRP.H`, `SGD.H`, `BLOCKDEV.H`, `SRB.H` and `VMM.H`
  (`docs/references/README.md`, "Windows 98 DDK").
- `legal-provenance.md` section 4: "Task 43.46 read-first" and "Task 43.46
  design: what IOS loads from `IOSUBSYS`, in three IOS builds".
- Design records 08 (flavours), 23 (the console tool), 26 (the window,
  sections 5.3, 5.4, 6.3, 6.5.4 and 9.3) and 27 (the 9x device install).
