# Design Record 29 - XHCIPERF, the Disk Benchmark

Status: draft 1, 2026-10-22, before code (roadmap-usb4 task 43.50). Written
for the owner's request of 2026-10-22, about 12:20: a disk benchmark with a
window, shipped with xHCI98 as one of its tools and working on every system
the package supports; ATTO Disk Benchmark and CrystalDiskMark as the
references for the window; it reads Stor98's counters where Stor98 drives
the disk and is a generic speed test for any other disk; a window to show
the results and parameters the user can set. The owner asked for the design
first: no code is written against this record until the owner has seen it
converged. Section 13 lists the decisions taken in writing it, each the
author's for the owner to confirm unless it names the owner; section 14
what is open.

Facts in this record are labelled by how they were taken: **read** (from
this tree's code or documents, or a run sheet's measurement, cited),
**static** (from a binary or a DDK header, nothing executed), or **to be
read** (a reading owed before or during the code, named by its leg).

## 1. What it is for

### 1.1 The problem

- The benchmarks people run on these systems were written for systems
  that can queue requests. On Windows 98 SE and ME Win32 file I/O is
  synchronous: one request outstanding per thread, queue depth 1 (Stor98
  `docs/INVESTIGATION-9X-REQUEST-SIZE.md` section 3, read). A test labelled
  with a queue depth above 1 measures nothing there that its label says.
- On 98 SE and ME, IOS fails an unbuffered request of more than 512 KiB,
  or of more than 17 physical runs, on the `DCB_dmd_phys_sgd` path that
  `SCSIPORT.PDR` demands, and cuts it into 126-sector (63 KiB) pieces sent
  one after another (the same document, section 1, static). A 1 MiB
  sequential test therefore lands on the worst case on every 98 SE and ME
  machine, and a 128 to 512 KiB test lands on it or not depending on how
  physically fragmented the program's buffer happened to be at that boot
  (the same document, "file I/O was whole on some boots and split on
  others").
- No common benchmark lets the user choose that fragmentation, and the
  one that verifies data with a pattern does not make a fragmented buffer.
  Task 43.46's read-first step needed exactly that and wrote a throwaway
  tool for it (`PERF2.EXE`, MSVC 6.0, outside the tree;
  `runs/run-43.md`, "43.46 read first"): for each size from 64 KiB to
  1 MiB, a file written and read unbuffered with the buffer at +0 and at
  +512 bytes, both timed, then verified in an untimed pass. Its reading
  is the case this tool exists to show a user: on a fragmented buffer
  512 KiB unbuffered rose from 35-41 MB/s to 72-79 MB/s with the
  prototype VSD, 1 MiB stayed cut to 63 KiB pieces with or without it,
  and VCACHE copies did not change (read).
- What reached the disk was learnt only from QEMU's NVMe traces. A user on
  real hardware has no such trace; Stor98 counts requests internally (its
  device extension's `ParentsAdmitted`, `ChildrenIssued`, `MaxChildBytes`
  and the rest, `src\stor98\src\nvme2k.h`, read) but nothing a program can
  read in a release build carries them out (section 6.1).

### 1.2 Scope

- **XHCIPERF.EXE** (x86, every target: 98 SE, ME, 2000, XP, Vista, 7, 8,
  8.1, 10) and **XHCIPF64.EXE** (x64: XP x64, Vista x64, 7 x64, 8 x64,
  8.1 x64, 10 x64), a Win32 window program built as XHCITR is (section 10),
  shipped in `tools\xhciperf\` and installed with the package's tools
  (section 11).
- A generic benchmark of any local disk through a file of its own: never
  a raw disk write, on any system (section 4).
- Tests: sequential and random, read, write or both; a sweep of request
  sizes between a chosen smallest and largest; a list of fixed tests
  (presets, and the user's own); file size, passes, a measuring time per
  test, a pause between tests; unbuffered or through the cache; the
  buffer page-aligned, at an offset, or scattered over physical memory;
  a verified data pattern; queue depth and threads where the system can
  queue (section 3).
- The window: a summary grid of the fixed tests and a per-size chart of
  the sweep, in MB/s, IOPS and latency; a Stor98 panel; a disk panel for
  any disk; copy as text, save as a file, and a saved result compared
  with a new one, which is how a user shows a before and an after
  (section 5).
- Stor98's counters, read where Stor98 drives the disk under test, through
  a read-only interface Stor98 would add, proposed here under Stor98's own
  rules (section 6, and section 7 for the change); the tool works the
  same with no Stor98 at all.
- Unattended runs from the command line, for the emulator legs and for a
  user who wants a report without the window (section 5.7).

### 1.3 Not in scope

- Raw disk access of any kind: no `\\.\PhysicalDriveN` read or write, no
  `INT 13h`, no `INT 25h`/`26h`, no IOS request built by the tool. The one
  device control the tool sends to a disk is section 6's counter read,
  which moves no data to or from the medium.
- Changing a driver setting, the VSD's presence or Stor98's tunables from
  the tool. The before and after of section 5.6 is two runs with a
  restart and a change made elsewhere between them.
- SMART or health data, firmware, secure erase, TRIM.
- A "score" or any single number that folds the tests together.
- Network drives, CD and DVD drives (section 4.2).
- Anything of ATTO Disk Benchmark's or CrystalDiskMark's own (section 12).

## 2. Where it lives

- Source: `xhciperf\` (new): `xhciperf.c` (the window, the worker thread,
  every system call), `pf_core.c` / `pf_core.h` (pure: the test plan, the
  pattern and its check, the statistics, the text of every result and
  report, the saved-result reader, the path and free-space rules),
  `pf_s98.c` / `pf_s98.h` (pure: Stor98's answer checked and decoded, two
  answers subtracted, the panel's text), `xhciperf.rc`, `xhciperf.manifest`,
  `xhciperf.ico`, `build.cmd`, `selftest.cmd`, `check-imports.ps1`,
  `README.TXT`.
- Stor98's side (section 7): `src\stor98\include\stor98pf.h`, a new
  interface header, BSD-3-Clause, which the tool includes in place from
  `src\stor98\include\` (it is not copied, so there is no second copy to
  hold equal, unlike `stor98cm.h`); the answer's builder in Stor98's
  sources.
- Package: `tools\xhciperf\XHCIPERF.EXE`, `XHCIPF64.EXE`, `readme.txt`.
- Installed copy: `<install directory>\XHCIPERF.EXE` (or `XHCIPF64.EXE`),
  the install directory of record 23 section 9.6 (`C:\XHCI98` by default).
  No Run value: it is started by hand, or from XHCITR (section 11).
- Host tests: `test\test_pf.c` (the pure half), `test\test_pfs98.c`
  (Stor98's answer as the tool reads it); Stor98's builder in Stor98's own
  unit tests (`src\stor98\tests\unit\`).

## 3. Tests and parameters

### 3.1 The test file

Every test reads and writes one file of the tool's own,
`<folder>\XHCIPERF.TMP`, in the folder the user picks (section 4.1).

- **Created new, never reused blindly.** The tool opens it with
  `CREATE_NEW`. If the name exists, the tool reads its first sector: a
  file whose header is the tool's own (section 3.6, the signature and the
  run's identity) is a leftover of a run that did not finish, and the
  tool offers to delete it ("A test file from an earlier run is still on
  F:. Delete it?"); any other file of that name is left alone and the run
  refused ("F:\XHCIPERF.TMP is not a file of this tool's; choose another
  folder"). The tool never truncates or overwrites a file it did not
  create in this run.
- **Prepared before it is measured.** The file is extended to its full
  size and written once from start to end with the pattern before the
  first measured test (the status bar: "Preparing the test file"). A read
  of a never-written region measures nothing on NT, where NTFS answers
  reads past a file's valid data length with zeros without reading the
  disk, and an extending write is synchronous there whatever the queue
  depth; preparing the whole file removes both. The preparation is not
  timed and not reported, except its duration in the log.
- **Deleted after.** At the end of a run, on Stop, on a refusal after
  creation and on the window's close, the tool closes every handle and
  deletes the file. A deletion that fails is reported and remembered
  (`HKCU\Software\xHCI98\XHCIPERF`, value `Leftover`, the path), and the
  next start offers the deletion again after checking the header as above.
  On NT the file is also opened with `FILE_FLAG_DELETE_ON_CLOSE` on its
  last handle when the run ends, so a tool killed afterwards still leaves
  nothing; 98 SE and ME have no such flag on a file (to be read, P.1; the
  remembered path covers them).
- **Size.** 16, 32, 64, 128, 256 and 512 MiB, 1, 2, 4, 8 and 16 GiB; the
  default 1 GiB on NT and 256 MiB on 98 SE and ME (43.46's file was
  256 MiB). On a FAT or FAT32 volume the largest offered is 2 GiB, which
  keeps every offset below 2^31 on the 9x file APIs; on NTFS any size the
  free-space rule allows. A smaller file than the largest request times the
  queue depth times the threads is refused by the plan (section 3.8).
- **Attributes.** Normal; not hidden, so a user can see and delete it by
  hand if everything else fails.

### 3.2 Access modes

- **Unbuffered** (the default): `FILE_FLAG_NO_BUFFERING`, so every request
  goes to the disk as the program issued it, with the alignment rules that
  flag carries (section 3.4). On 98 SE and ME VFAT passes unbuffered
  requests of 128 KiB to 1 MiB to IOS whole (Stor98's E0B and E1A readings,
  `INVESTIGATION-9X-REQUEST-SIZE.md` section 2, read), which is what makes
  section 1.1's cut visible.
- **Write-through**: a box, with unbuffered or cached;
  `FILE_FLAG_WRITE_THROUGH` on the write handle. 43.46's reading used it;
  the 9x presets of section 3.7 tick it. On NT it makes each write a
  forced-unit-access write, which Stor98 maps under its `WriteThrough`
  setting (its README, "Settings"); the panel shows the mode (section 6.3).
- **Cached**: neither flag; the system cache in the path. Offered because
  it is what a file copy sees, and labelled so in every result ("cached:
  measures the system cache as much as the disk"). A cached write test
  ends with `FlushFileBuffers`, inside the measured time by default (a box,
  "Include the flush in the write time"). A cached read test right after a
  write reads the cache; the plan warns when the file is not larger than
  physical memory ("the file fits in memory: cached reads will come from
  the cache"). On 98 SE and ME, VCACHE's requests are capped near 68 KiB
  above IOS whatever the program asks (`INVESTIGATION-9X-REQUEST-SIZE.md`
  section 2, static); the Stor98 panel shows it.

### 3.3 Patterns and verification

- **The pattern.** Every 512-byte unit of the file carries a 16-byte header
  - the signature `XPRF`, the run's 32-bit identity (from the start time
  and the process id), the unit's index in the file, the pass and write
  count - and 496 bytes from a 32-bit xorshift generator seeded from the
  run's identity and the unit's index. The data is incompressible to any
  controller that compresses, and every unit can be checked alone, at any
  offset, with no copy of what was written. A second pattern, all zero
  bytes, is offered for a comparison (a disk that compresses runs faster
  on it); verification is not available with it.
- **Writes** fill the buffer for each request before the request is timed
  (the fill is outside the request's time; the pure half's generator is
  fast enough that on a 98 SE machine it is not the limit, to be read on
  P.2).
- **Verification**, a box (default on for the 9x presets, off otherwise):
  after each write test, an untimed pass reads the region written,
  unbuffered when the test was, and checks every unit's header and body.
  A second box, "Check during the timed read" (off), checks inside a read
  test instead, with the cost in the read's time. A mismatch is a result,
  not a crash: the test's cell shows "data error", the log the first five
  mismatched units (file offset, expected and found header) and the count,
  and the run goes on to the next test unless "Stop on a data error" is
  ticked (the default).
- On a cached run the verification reads back through the cache and says
  so; on an unbuffered run it reads the disk.

### 3.4 Buffers

The owner's case is the buffer's layout, so the tool makes it a parameter:

- **Page-aligned** (the default): `VirtualAlloc`'s pages, offset 0.
- **Offset**: the same pages, the request's buffer starting 512 bytes into
  the first page, as 43.46's +512 pass did. Unbuffered I/O requires the
  buffer aligned to the volume's sector size; the offset is the sector
  size when that is above 512, and then the same as page-aligned when the
  sector is 4,096 bytes, which the plan says ("offset equals the page on
  this disk"). On NT the tool reads the alignment the adapter requires
  (`IOCTL_STORAGE_QUERY_PROPERTY`, `StorageAdapterProperty`'s
  `AlignmentMask`, section 5.4) and refuses an offset that breaks it.
- **Scattered**: a buffer whose pages are made physically non-adjacent as
  far as a program can. A program cannot choose physical pages; it can
  choose the order in which they are first touched. The tool reserves the
  request buffer and a second region of the same size, then commits and
  touches them one page at a time in alternation (buffer page 0, other
  page 0, buffer page 1, ...), so that a system handing out free frames in
  order gives the buffer every other frame, and frees the second region
  afterwards. Whether 98 SE, ME and the NT systems hand out frames that
  way is to be read (P.2, through Stor98's physical-runs histogram of
  section 6.3, which is the only place the result is visible); the window
  never claims more than "scattered as far as the system allows", and the
  Stor98 panel shows what the disk actually got.
- 43.46's reading found that "+512" and "fragmented" were different things
  that happened together (`runs/run-43.md`: the +512 pass's buffers had up
  to 129 runs, a run a page; the +0 pass's no more than 8). The tool keeps
  them as two separate choices, and the 9x study preset (section 3.7) runs
  page-aligned, offset and scattered side by side.
- The buffers are allocated once per run, the largest request times the
  queue depth times the threads, plus one request's worth for
  verification; the plan refuses a run whose buffers exceed a quarter of
  physical memory or 256 MiB ("Lower the queue depth, the threads or the
  largest size"). On NT the tool calls `VirtualLock` on them where the
  working set allows and goes on without it otherwise; 98 SE and ME lock
  the pages of each unbuffered request themselves.

### 3.5 Sequential, random, queue depth and threads

- **Sequential**: requests of the test's size, one after another from the
  file's start, wrapping at its end.
- **Random**: requests of the test's size at offsets aligned to that size,
  chosen uniformly over the file by a xorshift generator whose seed is in
  the result ("seed 0x1A2B3C4D"), so a run can be repeated exactly.
- **Queue depth** (1, 2, 4, 8, 16, 32): requests outstanding per thread.
  On NT, by overlapped I/O (`FILE_FLAG_OVERLAPPED`), one event per
  outstanding request and `WaitForMultipleObjects` (no I/O completion port:
  the same code then serves 2000 to 10 and stays within 64 handles). **Not
  available on 98 SE and ME**: their file systems do not do overlapped I/O
  on a disk file (to be read on P.1 as `ReadFile` with an `OVERLAPPED`
  failing; the investigation's section 3 states it), so the depth box is
  fixed at 1 there, greyed, with "Queue depth 1: Windows 98 and ME file
  I/O waits for each request". A saved 9x result therefore never carries a
  depth above 1.
- **Threads** (1, 2, 4, 8): each its own handle on the file, its own region
  of the file (the file cut into equal parts, so two threads never write
  the same unit) and its own queue. On NT threads multiply the queue as
  usual. On 98 SE and ME threads are the only way to have more than one
  request at the disk at once, and whether IOS lets two through is
  unmeasured (the investigation's section 3: at most 2 outstanding seen,
  "a queue depth above 1 needs several threads and is unmeasured"); the
  box is offered there with the note "several threads on Windows 98 and
  ME: whether requests overlap is shown by the Stor98 panel" (section 13,
  P9, for the owner).
- A test's label states what ran, in this tool's own words (section 12):
  "Sequential 1 MiB, depth 8, 1 thread", short form `Seq 1M d8 t1`;
  "Random 4 KiB, depth 1, 1 thread", `Rnd 4K d1 t1`.

### 3.6 Time, passes and the measurement

- **Measuring time** per test: 5 s by default (1, 2, 5, 10, 20, 30, 60).
  A fixed test runs its requests until the time has passed and the
  request in flight has completed; its result is the bytes moved over the
  time from the first request's start to the last request's completion.
- **The sweep** runs each size over a fixed amount of data instead
  ("Total per size", by default the file size, as a per-size length
  that is the same for every size), so its chart compares sizes over the
  same bytes; a "Time per size" choice makes it time-based too.
- **Passes**: 1 to 9 (default 3 for fixed tests, 1 for the sweep). Each
  pass repeats every test. The result shown is the best pass by default,
  with a choice of the mean or the median ("Show: best / mean / median");
  every pass's value is kept and goes into the copied text and the saved
  file.
- **Pause between tests**: 0, 1, 5, 10, 30 s (default 0), for a disk that
  heats or caches.
- **Warm-up**: none by default; a box runs each test once untimed first.
- **Timer**: `QueryPerformanceCounter` on every system where
  `QueryPerformanceFrequency` succeeds with a nonzero frequency, which the
  tool records in the result; `GetTickCount` otherwise, with the result
  marked "timer: milliseconds". On 98 SE and ME the performance counter
  is the 8254 timer's 1.193182 MHz (to be read, P.1, as the frequency the
  result records). On NT the frequency is whatever the HAL chose; the tool
  takes every timestamp of one queue on the thread that issues it, so the
  per-processor counter drift some XP-era multiprocessor machines showed
  touches only cross-thread comparisons, which the tool does not make
  within a test.
- **Latency**: the time from a request's issue to its completion as the
  issuing thread sees it, per request, with the performance counter. Min,
  mean and max exact; the median and the 99th percentile from a histogram
  of 64 logarithmic buckets (1 us to 100 s), so no per-request list is
  kept. At depth above 1 a request's latency includes its wait in the
  queue, and the label says so. With the millisecond timer, latency is not
  shown ("needs a high-resolution timer").
- **Units**: MB/s with MB = 1,000,000 bytes by default, the unit disk
  makers quote; a View choice shows MiB/s (1,048,576), which is what
  `runs/run-43.md`'s tables are ("MiB moved / s"). IOPS is requests per
  second. Every copied and saved result names its unit.
- **Arithmetic**: the user-mode tool uses `__int64` for byte counts,
  offsets and timer ticks (MSVC 6.0 and WDK 7.1 both have it, and the
  static C runtime carries its helpers, as `xhcitr\tr_snap.c` already
  uses); the pure half computes rates as fixed-point integers (tenths of a
  unit) so its host vectors give the same text from both compilers.

### 3.7 Presets

Each preset is a list of fixed tests or a sweep with every parameter set;
choosing one fills the parameters, which the user may then change (the
list then reads "Custom").

- **Standard** (the default on NT): `Seq 1M d8 t1`, `Seq 1M d1 t1`,
  `Rnd 4K d32 t1`, `Rnd 4K d1 t1`; read and write; unbuffered; 1 GiB;
  3 passes of 5 s.
- **Standard for Windows 98 and ME** (the default on 98 SE and ME):
  `Seq 512K d1 t1` (the largest request IOS passes whole), `Seq 1M d1 t1`
  (the size IOS always cuts), `Seq 64K d1 t1`, `Rnd 4K d1 t1`; read and
  write; unbuffered, write-through; 256 MiB; 3 passes of 5 s. Its note
  says why the list differs.
- **Request-size study for Windows 98 and ME** (43.46's reading as a
  preset): sizes 64, 128, 256, 512 and 1,024 KiB; sequential write then
  read, unbuffered, write-through; page-aligned, offset and scattered
  buffers, each a block of the chart; verification on; 256 MiB; 1 pass.
- **Sweep**: sequential, sizes from 512 bytes to 8 MiB in powers of two
  (smallest and largest chosen from that list), read and write,
  unbuffered, depth 1 (4 on NT selectable), total per size the file size.
- **Quick**: `Seq 1M d1 t1` and `Rnd 4K d1 t1`, 64 MiB, 1 pass of 2 s, for
  a first look and for the self-test leg.
- **The user's own**: "Save preset..." stores the current parameters under
  a name in `HKCU\Software\xHCI98\XHCIPERF\Presets`; the list shows them
  after the built-in ones.

In unbuffered mode a size below the volume's sector size is not offered,
and every size is a multiple of it; the sweep's smallest size is the
sector size on a 4,096-byte-sector disk.

### 3.8 The plan

Before anything is created the pure half turns the parameters into a plan
and refuses one that cannot run, each refusal one sentence naming what to
change: a size below the sector size in unbuffered mode; a file smaller
than the largest request times the depth times the threads; buffers above
section 3.4's cap; the free-space rule of section 4.3; a depth above 1 on
98 SE or ME (not reachable from the window, reachable from a command line
or a saved preset); a duration and pass count above 2 hours in all ("this
run would take about 3 h; lower the passes or the time"). The plan's
estimate of the run's length is shown before Start ("about 4 min").

## 4. Safety

### 4.1 What may be tested

- The user picks a drive (a drop-down of every drive letter whose type is
  fixed or removable, with its label, file system, size and free space)
  or a folder on one (Browse..., `SHBrowseForFolderA`, present in 98 SE's
  `SHELL32.DLL`). The default is the root of the first fixed drive that
  is not the Windows drive, else the Windows drive.
- Refused: `GetDriveType` remote, CD-ROM, unknown or no root; a folder the
  tool cannot create a file in ("You cannot write to F:\X; choose another
  folder"); the Windows directory and the system directory and their
  subfolders (the test file is a stranger there and gains nothing over the
  drive's root); a RAM disk is allowed and its results say "RAM disk".
- Allowed with a note: the Windows drive ("the drive Windows runs from:
  other programs' activity will show in the results") and the drive of the
  98 SE or ME swap file.
- The tool writes nothing anywhere but its test file, its `HKCU` key and,
  when the user asks, the result file the user names.

### 4.2 Never a raw disk write

The benchmark is file-based on every system; it never opens a physical
drive, a volume or a partition for data, never locks or dismounts a
volume, and sends a disk one device control only, section 6's counter
read, which Stor98 answers from memory without touching the medium (and
the disk panel's read-only property queries of section 5.4). This is a
rule of the code: `check-imports.ps1` cannot see it, so the self-test
(section 10.3) greps the system half for `PhysicalDrive` and `\\.\` and
admits exactly the two openings section 6.2 names, and the host suite
holds the plan to never produce any other target.

### 4.3 Free space

- The free space comes from `GetDiskFreeSpaceExA` (looked up at run time;
  98 SE has it), else `GetDiskFreeSpaceA` with its 2 GiB cap.
- A run needs the file's size plus a reserve: 64 MiB on a drive that is
  not the Windows drive or the swap file's drive; there, the larger of
  512 MiB and 10% of the drive, because a full Windows drive stops 98 SE
  and ME (the swap file grows into it) and starves other programs' writes
  on NT. The refusal names both numbers ("F: has 900 MB free; this run
  needs 1,024 MB and leaves 64 MB").
- Free space is read again just before the file is created; nothing is
  ever written into a reserve.

### 4.4 Removal, errors and Stop during a run

- **Stop** sets a flag the worker reads between requests; a request in
  flight finishes (on NT, `CancelIoEx` where present, else `CancelIo` on
  the worker's own handles, looked up at run time; on 98 SE and ME there
  is nothing to cancel). The results so far are kept and marked "stopped".
- **A failed request** (any `ReadFile`, `WriteFile` or wait that fails:
  `ERROR_DEVICE_NOT_CONNECTED`, `ERROR_NOT_READY`, `ERROR_GEN_FAILURE`,
  `ERROR_IO_DEVICE`, `ERROR_CRC`, a short transfer, ...) stops the run:
  the cell shows "failed", the log the error code and its text, the file
  offset and the size, and the window says "The disk stopped answering
  (error 1167: The device is not connected). The run was stopped." No
  retry, no second request after the first failure.
- **The disk removed** (unplugged, a tunnel dropped, ejected elsewhere) is
  that case: the first request after it fails, the run stops, and the
  tool tries once to close and delete the file; a deletion that fails is
  remembered (section 3.1). `SetErrorMode(SEM_FAILCRITICALERRORS)` is set
  for the whole process, so 98 SE and ME raise no critical-error box over
  the window; whether a removed Stor98 disk on 98 SE or ME answers the
  tool with an error or with a stall is Stor98's fast-fail behaviour
  (its `AGENTS.md`, "Surprise removal is a normal event") and is read on
  P.4.
- **Safely Remove during a run**: the open test file makes the system
  refuse the removal (Windows' own applet, XHCITR, `INSTALL /eject`); the
  tool does not answer the query itself. On NT it watches
  `WM_DEVICECHANGE` with `DBT_DEVICEQUERYREMOVE` for the volume's handle
  (`RegisterDeviceNotificationA`, looked up at run time) and, while
  running, says in its status bar "Windows asked to remove F:; stop the
  test first"; it does not stop by itself.
- **Sleep and the screen saver**: `SetThreadExecutionState`
  (`ES_SYSTEM_REQUIRED | ES_CONTINUOUS`, looked up at run time) during a
  run, cleared after.
- **Closing the window during a run** asks "Stop the test and close?",
  then stops, deletes the file and exits.

## 5. The window

### 5.1 Layout

Title "xHCI98 Disk Benchmark". One resizable top-level window, at least
640 by 480 so it fits 98 SE's smallest screen, laid out top to bottom:

- **The parameters band**: Drive or folder (drop-down and Browse...),
  Preset (drop-down), File size, Passes, Time per test; Access
  (Unbuffered / Cached, Write-through box), Buffer (Page-aligned / Offset /
  Scattered), Pattern (Random / Zeros), Verify box; Depth and Threads (the
  depth greyed at 1 on 98 SE and ME); Tests (Read, Write, Both); the
  plan's estimate; **Start** and **Stop**. "More..." opens a dialog for
  the rest (the sweep's sizes and total, the pause, warm-up, the flush in
  the write time, check during the read, stop on a data error, the seed,
  the custom test list).
- **A tab control** (`COMCTL32`'s tab, present in 98 SE) with five pages:
  - **Summary**: the grid of fixed tests (section 5.2);
  - **Sweep**: the per-size chart and its table (section 5.3);
  - **Disk**: what the system says about the disk (section 5.4);
  - **Stor98**: the panel (section 5.5);
  - **Log**: every step and every pass's value, as text.
- **A status bar**: the current test and pass, a progress bar, the time
  left, and the timer source.
- **Menus**: File (Save results..., Compare with saved results...,
  Save preset..., Exit); Edit (Copy results, Copy the current page); View
  (MB/s or MiB/s; IOPS and latency columns; Show best / mean / median);
  Help (Readme, About).

The parameters, the window's size and the last tab persist per user under
`HKCU\Software\xHCI98\XHCIPERF`; a missing or bad value reads as the
default.

### 5.2 Summary

A grid with one row per fixed test and two columns, Read and Write, each
cell a large MB/s number (or "-" not run, "running", "stopped", "failed",
"data error") and under it, in the cell's smaller line, the IOPS and the
mean latency. The row header is the test's long label; a Start button per
row runs that row alone, as the references do. Below the grid, one line:
the drive, its file system, the file size, the access mode, the buffer,
the passes and which value is shown. The grid is a window class of the
tool's own, painted with GDI (no owner-drawn common control), with the
dialog font scaled for its large numbers, so 98 SE and 10 draw it the
same.

### 5.3 Sweep

A horizontal bar chart, one band per request size from smallest (top) to
largest, two bars in each band, write and read, their length in MB/s on a
common axis whose scale is chosen from the largest value (rounded up to
1, 2 or 5 times a power of ten), the value printed at each bar's end;
beside it a table of the same numbers with IOPS and latency. When the
preset ran several buffers (the 9x study), the chart groups them: a block
of bands per buffer layout, the same sizes in each, so page-aligned,
offset and scattered sit one above another. Painted with GDI in the
system's colours (`COLOR_HIGHLIGHT` for read, `COLOR_BTNSHADOW` hatched
for write) with no colour of the tool's own, so it reads under 98 SE's
16 colours and high contrast.

### 5.4 Disk

What the system reports about the disk under the folder, read-only, no
administrator rights:

- every system: the volume's label, file system, cluster and sector size,
  size and free space;
- NT: `IOCTL_STORAGE_QUERY_PROPERTY` on the volume opened with no access
  rights (`\\.\F:`, `CreateFileA` with access 0): `StorageDeviceProperty`
  (vendor, product, revision, bus type, removable) and
  `StorageAdapterProperty` (maximum transfer length, maximum physical
  pages, alignment mask); `IOCTL_SCSI_GET_ADDRESS` (port, path, target,
  lun), which also finds the SCSI port for section 6.2; each answer that
  fails reads "not reported by this system";
- 98 SE and ME: the volume's facts above, and the disk's devnode found
  through the configuration manager (section 6.2) with its hardware id and
  its adapter's driver.

### 5.5 Stor98

When section 6.2 finds that Stor98 drives the disk, the page shows the
counters as the difference between Stor98's answer before and after each
test (section 6.3), the test chosen from a drop-down of the run's tests
("Since the window opened" also offered): the request sizes the driver
received and the sizes of the commands it sent to the drive as two
histograms, the splits, the bounces, the physical runs per request, the
outstanding commands, the errors, and the driver's limits and modes. Its
first lines are the answer in words, for example on 98 SE: "Of 512
requests of 512 KiB, 512 reached the drive whole (IOS did not cut them);
the system demands physical scatter/gather: no (an IOS layer above
SCSIPORT removed it)". When Stor98 does not drive the disk: "This disk is
not driven by Stor98; the Stor98 page has nothing to show." When Stor98
drives it but cannot be read: the reason (section 6.2) and, on NT
without administrator rights, a button "Read as administrator..." that
starts the tool elevated with the same parameters and the Stor98 page
open (`ShellExecuteA` verb `runas` on Vista and later; on 2000 and XP the
button says to run the tool as an administrator).

### 5.6 Copy, save and compare

- **Copy results** puts the whole result on the clipboard as plain ASCII
  text, 79 columns: a header (the tool's version, the date, the system and
  its version, the drive and file system, the disk's model where known,
  the parameters, the timer and its frequency, the seed), the Summary as a
  table, the Sweep as a table, the Stor98 page's differences per test
  when it was read, and every pass's value. The same text is the saved
  file (`.TXT`, `GetSaveFileNameA`).
- The saved file also carries one machine-readable line per test result
  (`XPRF-RESULT 1 <label> <op> <buffer> <MB/s x10> <IOPS> <lat-mean-us>
  ...`) after the human text, so that **Compare with saved results...**
  can read it back: the chosen file's values appear beside the current
  run's in both Summary and Sweep (the saved value grey, the current
  black, the change in percent), matched by test label, operation and
  buffer. This is the before and after the owner asked for: a run with
  the 43.46 VSD absent, saved; the VSD installed and the machine
  restarted; the same preset run again and compared. A saved file whose
  lines do not parse is refused with the line number; one written by a
  different parameter set compares only the tests both have, and says so.

### 5.7 The command line

`XHCIPERF /run [/preset <name>] [/folder <path>] [/size <MiB>]
[/passes <n>] [/time <s>] [/buffer aligned|offset|scattered]
[/cached] [/writethrough] [/verify] [/depth <n>] [/threads <n>]
[/report <file>] [/exit]` starts the run at once with those parameters
over the defaults, writes the report if asked and exits with 0 (every
test passed), 1 (a test failed or a data error), 2 (refused) or 3
(stopped). The window shows the run; `/exit` closes it at the end. This is
what the emulator legs drive (43.12's unattended route) and what a user
pastes into a batch file. `/selftest` renders every text of the pure half
at its longest to a file and exits, for section 10.3.

## 6. Stor98's counters

### 6.1 What Stor98 exposes today (read, 2026-10-22, `src\stor98\src`)

- **No statistics path a program can read in a release build.** The
  device extension (`nvme2k.h`) carries counters - `ParentsAdmitted`,
  `ParentsCompleted`, `ChildrenIssued`, `ChildrenIo`, `MaxChildBytes`,
  `ChildLimitIo`, `FastFails`, `BusyReturned`, `StaleCqes`,
  `GoneDeadlineCount`, `ResetCalls`, `WtFuaWrites`, `PollTicks` and
  more - which reach only the debug port of test builds
  (`dbgport.h`, `g9dbg.h`: QEMU's port `0xE9` and `usb4-emu`'s debug
  port).
- **`SRB_FUNCTION_IO_CONTROL`** (`nvme2k.c` `ScsiDispatch`, NT's
  `IOCTL_SCSI_MINIPORT` route) is answered by two handlers: the inherited
  `NVME2KDB` signature, whose one code `0x1000` returns success with no
  data, and the SMART route (`HandleIO_SCSIDISK`). `include\stor98cm.h`
  declares `STOR98CM_CODE_QUERY` ("who is this adapter") as interface 1,
  diagnostic only, but no handler for the `STOR98CM` signature is in the
  sources.
- **The link-state table** (interface 2 on NT, a callback object; interface
  3 on 98 SE and ME, a table in the system arena found through
  `HKEY_DYN_DATA\Stor98LinkState` and `Services\stor98\LinkState`) is the
  connection manager's channel, ring 0 to ring 0; it carries link state,
  not request counts, and a program reads none of it.
- **No other program of this package talks to Stor98**: XHCITR and the
  installer read only its files, service and settings (`xhciinst\inst_core.c`,
  read).
- `SCSIOP_READ_DATA_BUFF` (`3Ch`, READ BUFFER) is not handled: it falls to
  the default case, CHECK CONDITION, INVALID COMMAND OPERATION CODE
  (`nvme2k.c` `ScsiDispatch`, read).

So the tool needs one change in Stor98: a read-only answer carrying the
counters. Section 7 proposes it.

### 6.2 Finding Stor98 and the carrier

**The carrier is one SCSI command, READ BUFFER (`3Ch`), on every system.**
Stor98 answers it from its device extension, with no NVMe command and no
access to the medium, like INQUIRY. One command serves both families,
because each has a documented program route for a SCSI command and none
for a miniport control on 98 SE and ME:

- **NT (2000 to 10)**: `IOCTL_SCSI_PASS_THROUGH` with a data-in buffer, on
  `\\.\PhysicalDriveN` (N from `IOCTL_STORAGE_GET_DEVICE_NUMBER` on the
  volume), opened `GENERIC_READ | GENERIC_WRITE`, which needs
  administrator rights (and elevation on Vista and later); the port turns
  it into `SRB_FUNCTION_EXECUTE_SCSI` with the CDB as given. Stor98 is
  found first with no rights: `IOCTL_SCSI_GET_ADDRESS` on the volume gives
  the port number, and `HKLM\HARDWARE\DEVICEMAP\Scsi\Scsi Port <n>`, value
  `Driver`, reads `stor98` (to be read, P.3, on 2000, XP, 7 and 10).
  `IOCTL_SCSI_MINIPORT` on `\\.\Scsi<n>:` was considered and not taken: it
  needs the same rights, a second handler in Stor98 and a route that has
  no 9x counterpart.
- **98 SE and ME**: the ASPI layer (`WNASPI32.DLL`, loaded at run time,
  and `APIX.VXD` under it), whose `SendASPI32Command` with
  `SC_EXEC_SCSI_CMD` sends a CDB to a SCSI target of a `SCSIPORT.PDR`
  miniport. The Windows 98 DDK carries its interface (`block\wnaspi32`,
  `WNASPI32.H`, static: the extracted DDK's file list); whether stock
  98 SE and ME install `WNASPI32.DLL` and `APIX.VXD`, and whether APIX
  lists a target of Stor98's adapter, is to be read first (P.0). The disk
  under the folder is matched to an ASPI target by its devnode: the
  configuration manager's walk from the volume's drive letter to the disk
  devnode (`CurrentDriveLetterAssignment`) and to its parent, the adapter,
  whose driver key names `stor98.mpd` (or `NVME2K.MPD` where a devnode
  still names the inherited file, as the 43.46 image does); then the ASPI
  targets whose INQUIRY vendor and product equal the disk's hardware id
  are asked, and the answer's PCI location (section 6.3) is matched to the
  adapter's. READ BUFFER is sent to no target that has not first matched
  by INQUIRY, so no other device ever receives it.
- Where neither route works (no ASPI on 9x, no rights on NT, Stor98 in its
  gone state, where it may fast-fail the command), the page says which, and the
  benchmark is unaffected.
- **When it is read**: once before the run, and between tests, when no
  request of the tool is outstanding; never during a measured test. The
  G3 reading that a miniport control waits for a free slot under a full
  queue (`stor98cm.h`, interface 1) does not bite an idle disk; a read
  that takes more than 2 s is abandoned and the page says "Stor98 did not
  answer in time".

**The CDB**: READ BUFFER, mode `02h` (data), buffer ID `53h`, offset 0,
allocation length the answer's size; mode `03h` (descriptor) with the same
ID returns the size, so a tool and a driver of different versions agree on
the length first. The answer starts with a signature and versions, so a
device that answered READ BUFFER for its own reasons is never read as
Stor98.

### 6.3 What a benchmark needs from the answer

Grouped as `stor98pf.h` would lay them out (section 7.2); every count is a
32-bit counter that only rises and wraps, every size in bytes, so the tool
subtracts two answers modulo 2^32 and never needs a reset:

1. **Identity**: signature `S98P`, the interface version, the answer's
   length, Stor98's file version, the build (9x or NT, x86 or amd64), the
   adapter's PCI bus, device and function, the namespace's logical block
   size and its size in blocks, the boot's identity (a value that changes
   at every adapter start, so a tool comparing two answers across a
   restart or a re-plug knows not to subtract them).
2. **Requests received** (the SRBs, "parents"): reads and writes counted,
   and the sectors of each (512-byte units, which wrap only after 2 TiB
   between two reads); a size histogram per direction in 13 buckets
   (below 4 KiB, 4 KiB, to 8, to 16, to 32, below 63 KiB, exactly 63 KiB =
   64,512 bytes - IOS's piece -, to 64 KiB, to 128, to 256, to 512 KiB,
   to 1 MiB, above).
3. **Commands sent** (the NVMe commands, "children"): the same histogram
   per direction; the parents split into more than one child; children
   bounced through Stor98's own buffer; read-modify-writes on a 4,096-byte
   namespace; flushes; and an outstanding-commands histogram taken at each
   child's issue (1, 2, 3-4, 5-8, 9-16, 17-32, above), which shows the
   queue depth that reached the drive, and whether two 9x threads ever
   overlapped.
4. **Physical layout**: the physical runs of each data request's buffer as
   Stor98 maps it (1, 2, 3-4, 5-8, 9-16, 17, 18-32, 33-64, above), the
   histogram that says whether the tool's scattered buffer was scattered.
5. **Errors**: requests completed with an error, fast fails, busy
   returns, resets, gone episodes, gone deadlines taken, stale and late
   completions (the counters of section 6.1, carried as they are).
6. **Limits and modes**: `MaximumTransferLength` and
   `NumberOfPhysicalBreaks` as the port granted them, the largest child
   (`MaxChildBytes`), the controller's MDTS in bytes, the children allowed
   at once (`ChildLimitIo`), the write-through mode as resolved (off,
   cache off or FUA) and whether a volatile write cache is present,
   polling or interrupts and why.
7. **On 98 SE and ME, what IOS hands Stor98**: of the data requests, how
   many arrived with a linear buffer and how many with a logical
   scatter/gather list (`sg9x.h`'s classification, already made on every
   request that needs a byte offset), how many IOS marked double-buffered;
   and the DCB's `DCB_max_xfer_len`, `DCB_max_sg_elements` and
   `DCB_dmd_flags` as last seen through the request's IOP (the documented
   `IOP_physical_dcb`, the offsets Stor98's `INVESTIGATION-9X-REQUEST-SIZE.md`
   section 7 already records). `DCB_dmd_flags` without `DCB_dmd_phys_sgd`
   (`0x800`) is the proof that an IOS layer above `SCSIPORT.PDR` removed
   the demand - 43.46's reading saw `0x00040600` at the first request with
   its prototype, `0x00000E09` from `SCSIPORT.PDR` alone - which is how the
   panel says whether a VSD is in effect without knowing which VSD.

What a benchmark needs and the answer gives: the request sizes at the
drive (3) against those the program issued (the tool's own plan) and those
the driver received (2), so a user sees a 1 MiB request become sixteen 63
KiB requests at (2) and (3); the splits Stor98 made itself (3); the queue
depth reached (3); the errors (5); and why (6, 7).

### 6.4 The 43.46 VSD

- **Whether it is installed**: the tool looks for 43.46's VSD file in
  `%windir%\SYSTEM\IOSUBSYS` (its name is 43.46's design's; the panel
  shows the file and its version resource, or "not installed").
- **Whether it is in effect**: section 6.3 item 7's `DCB_dmd_flags`; a file
  present with `phys_sgd` still demanded reads "installed but not in effect
  on this disk", which is a finding for 43.46's legs.
- **Its own counters** (lists built, list overflows) are 43.46's to design;
  if its record gives them a reading route, the panel adds them. This
  record asks nothing of the VSD.

## 7. The change proposed to Stor98

Stor98 is its own project in this tree with its own rules
(`src\stor98\AGENTS.md`). This section is the proposal written for it; it
is made in Stor98 only once its maintainer (the owner) records it decided
at the end of Stor98's `docs/PLAN.md` ("do not implement an option there
that is not marked decided").

### 7.1 What changes

- One SCSI command answered: READ BUFFER (`3Ch`) modes `02h` and `03h`
  with buffer ID `53h`, in `ScsiDispatch` beside INQUIRY, answered from
  the device extension with no NVMe command, and listed in
  `ScsiCompletesInSoftware` (which `nvme2k.c` says to keep in step with the
  switch). Any other mode or buffer ID: CHECK CONDITION, ILLEGAL REQUEST,
  INVALID FIELD IN CDB, as INQUIRY refuses what it does not support. A
  short allocation length cuts the answer; data underrun reported as the
  port expects.
- Counters added on the existing paths: the histograms and counts of
  section 6.3 items 2 to 4 and 7, each a `ULONG` increment where the
  request is admitted, split, issued or completed, under the
  serialisation those paths already have (the port calls `HwStartIo` and
  the interrupt and timer callbacks synchronised with one another, so a
  snapshot built in `HwStartIo` is consistent with no lock of its own).
  No allocation, no timer, no new state in the request path.
- Item 7's DCB fields copied from the IOP's DCB on the 9x build only, at
  the point `sg9x.h` already resolves the IOP, under the same checks
  (`Sg9xClassify`'s arena and back-pointer checks); an unknown shape leaves
  them at their last value with a flag "not seen".
- Nothing else: no setting, no write route, no reset, no change to the
  CM channel, to `stor98cm.h` or to any interface version.

### 7.2 The header

`src\stor98\include\stor98pf.h`, new, BSD-3-Clause with the licence note
`stor98cm.h` carries ("an interface definition, licensed on its own ...
including it in a GPL-2.0 work does not change the licence of this
file"): the CDB values, the answer's layout (a header with signature
`S98P` = `0x50383953`, `STOR98PF_VERSION` 1, the length, then fixed
sections of `ULONG`s at 4-byte offsets, compile-time size and offset
checks as `stor98cm.h` has), the bucket edges as macros, and the rule that
a reader accepts a longer answer of the same major version and reads only
what it knows. `STOR98PF_VERSION` is a protocol version, never tied to
the package version (Stor98's `AGENTS.md`, "Releases"). The tool
includes the header from `src\stor98\include\`; nothing of the tool's
GPL-2.0 code goes into Stor98, and nothing of Stor98's code into the tool
beyond the header's definitions.

### 7.3 Stor98's tests and gates

- Unit tests in `src\stor98\tests\unit\`: the answer's builder (pure, from
  a device extension's fields: every section, a short allocation length,
  mode `03h`, a wrong buffer ID or mode, the histogram edges at each bucket
  boundary and at 63 KiB exactly), the 9x DCB copy under each
  `Sg9xClassify` outcome.
- Emulator tests as its `AGENTS.md` requires ("Every fix gets a test in the
  emulator"): the answer read while idle, after a large transfer and in
  the gone state (fast-failed, no answer), on 98 SE and on 2000 or 7; a
  surprise removal with the tool reading between tests.
- This package's gates: Stor98's import allowlist is unchanged (no new
  import); `check-stor98-build.ps1` unchanged; the source stamp covers the
  new header.

### 7.4 Why not another route

- **`IOCTL_SCSI_MINIPORT`** (an `SRB_IO_CONTROL` signature `STOR98PF`):
  NT only; the same rights as pass-through; a second carrier for the 9x
  side anyway.
- **A registry value Stor98 writes from time to time** (as interface 3's
  locator): 9x registry services are not safe from the miniport's timer
  context, and the counters would be stale by design.
- **A table in the 9x system arena read by the program** (interface 3's
  shape): whether a ring 3 program may read the system arena on 98 SE and
  ME is not something this project has read, and the CM's table is a
  ring 0 contract; not used.
- **LOG SENSE with a vendor page**: Stor98's LOG SENSE handler answers
  every page with SMART data today (`ScsiHandleLogSense`, "we assume any
  log sense is for SMART data"); a vendor page would change that existing
  behaviour for every other caller.

## 8. Generic disks

With no Stor98, everything but the Stor98 page works the same: the tests,
the verification, the Disk page (section 5.4) and the results. The tool
names no driver it does not know, guesses no request size it cannot see,
and its texts never suggest that a result without Stor98's counters says
what reached the disk. A USB stick under 98 SE's `USBSTOR.SYS`, a UAS disk
under `xhciuas.sys`, an IDE disk under `ESDI_506.PDR` and an NVMe drive
under Windows' own `stornvme` on 8 and 10 are each a generic disk.

## 9. The pure half

`pf_core.c` and `pf_s98.c` hold no Windows call: the plan from the
parameters (presets, the sweep's sizes, the sector and alignment rules,
the buffer and file caps, the 9x depth rule, the estimate); the region
each thread uses; the random offsets from the seed; the pattern's unit and
its check; the statistics (bytes and ticks to MB/s and MiB/s in tenths,
IOPS, the latency histogram, its median and 99th percentile, best, mean
and median over passes); the free-space rule; the path rules; every text
of the window, the copied result and the saved file, at 79 columns; the
saved file's reader; the compare's matching and percentages; Stor98's
answer checked (every offset bounded by the bytes returned, the
signature, the versions, the length), two answers subtracted (wrap, a
changed boot identity refused), and the panel's text.

`test\test_pf.c` covers each with golden vectors: a plan per preset per
system family (9x refusing depth 8 from a command line, NT accepting it),
4,096-byte sectors, a file too small, buffers over the cap; the pattern's
unit for given seeds and its check finding a changed header, a changed
body, a unit from another run and a unit from another offset; rates at
the boundary of each rounding; a latency histogram's percentiles; the
reports at their longest; a saved file read back, with a bad line, a
missing test and a different parameter set. `test\test_pfs98.c` covers
the answer as the tool reads it: truncated, a longer answer of version 1,
a wrong signature, a wrap between two answers, a changed boot identity,
the 63 KiB bucket, the "whole" sentence of section 5.5 for 98 SE with and
without `phys_sgd`.

## 10. Build, imports and runtime

### 10.1 Toolchain

As XHCITR (record 25 section 9) and the windowed installer (record 26
section 4):

- `XHCIPERF.EXE`: the in-repo MSVC 6.0, `/W3 /WX /O2`, the C runtime
  static and multithreaded (`/MT`: the run is a worker thread made with
  `_beginthreadex`, as XHCIWIZ's engine is), `/SUBSYSTEM:WINDOWS,4.0`, so
  it starts on 98 SE, ME, 2000, XP, Vista, 7, 8, 8.1 and 10.
- `XHCIPF64.EXE`: the same sources with WDK 7.1's amd64 compiler, `/MT`,
  `/SUBSYSTEM:WINDOWS,5.02`, for the x64 systems. Like XHCIWIZ, the 32-bit
  program under WOW64 (`IsWow64Process`, looked up at run time) starts
  the x64 one from its own directory with its arguments and exits with
  its code, so section 6.2's pass-through reaches the 64-bit port without
  a 32-bit thunk question; without the x64 image beside it, it runs
  itself (the benchmark works under WOW64; only the Stor98 page may then
  be missing, and says so).
- ANSI C89 as the other tools, with `__int64` (section 3.6) and `double`
  nowhere in the pure half.
- `xhciperf\build.cmd` builds both, `build.cmd x86` the 32-bit one;
  `scripts\build-driver.cmd` builds and self-tests it as it does XHCITR
  (its "xhcicln and xhcitr, rebuilt" step), and `make-release.ps1` takes
  them as it takes XHCITR's pair.

### 10.2 Imports

Static imports: `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32` (the `HKCU`
key), `SHELL32` (Browse..., the readme, `runas`), `COMCTL32` (tab,
progress bar, status bar), `COMDLG32` (Save As, Open) - the seven XHCITR
has, every one present on 98 SE; `check-imports.ps1` allows exactly them.
Looked up at run time: `GetDiskFreeSpaceExA`, `CancelIo`, `CancelIoEx`,
`SetThreadExecutionState`, `RegisterDeviceNotificationA`,
`IsWow64Process`, `GetNativeSystemInfo`, and every configuration-manager
routine (`cfgmgr32.dll`, as XHCITR's 9x walk does); `WNASPI32.DLL` by
`LoadLibraryA` on 98 SE and ME alone. No `WINMM` (the millisecond
fallback is `GetTickCount`), no `SETUPAPI` static import.

### 10.3 Manifest, icon, version, self-test

- Manifest: `asInvoker` and the Common Controls 6 dependency, as XHCITR;
  never elevated at start; elevated only by the Stor98 page's button.
- Icon `xhciperf.ico`, drawn for this project: a disk with a speed gauge
  needle, in the 16 VGA colours, at 16, 32 and 48 pixels, 4 and 32 bits
  per pixel, as XHCITR's is drawn. Nothing taken from either reference's
  icon or artwork.
- Version resource from `src\xhci_version.h`.
- `selftest.cmd`: the import check; the built binary's `/selftest`
  rendering of every text at its longest, compared with the host suite's;
  the source check of section 4.2. It runs the built tool only under
  `/selftest`, which touches no disk and opens no window, as XHCITR's
  self-test does; on a development host it runs only from
  `build-driver.cmd`'s gated self-test step.

## 11. Install, package and release

- **Package**: `tools\xhciperf\XHCIPERF.EXE`, `XHCIPF64.EXE`,
  `readme.txt` in both architectures' zips, beside `tools\xhcitr\`;
  `make-release.ps1`'s `$toolNames` gains `xhciperf`, with XHCITR's
  freshness and version checks; `test-package.ps1` checks the layout.
- **Installer**: the xHCI98 tools component of record 26 section 6.2 and
  record 23 section 9.5 copies the tool for the machine's architecture
  into the install directory by the same `.TMP` route, outside every
  `XHCIINST.LST`, never left pending for a restart; no question of its
  own, no Run value; the uninstall removes it with the other tools. A
  running copy during an update is "still in use", as XHCITR's rule: the
  installed tool is left, the run ends with "steps left". The window's
  Components page names it in the tools row's text.
- **XHCITR**: an Action menu entry "Disk benchmark..." on a disk or volume
  node that starts XHCIPERF with that drive chosen (section 13, P7, for
  the owner).
- **Documents**: `README.md`'s tools list, `docs/using/release-notes.md`
  (the tool, its 9x queue-depth note, and that its results are the user's
  machine's), `tools\xhciperf\readme.txt` (78 columns, as the other
  tools' readmes), `AGENTS.md`'s repository layout and its list of tool
  executables that carry statically linked runtimes,
  `docs/contributing/source-files.md`, and `legal-provenance.md` section 2
  (a row for the published binaries) and section 2a (the static runtimes),
  as XHCITR's.

## 12. ATTO Disk Benchmark and CrystalDiskMark: references for the window only

The owner named both as the references for the window. They are that and
nothing more:

- What is taken is the idea of a layout and a workflow: a band of
  parameters above the results, a Start button, a grid of a few fixed
  tests with read and write side by side (CrystalDiskMark's), and a
  per-size chart with a read and a write bar per size and a numbers table
  beside it (ATTO's). Both are common to disk benchmarks.
- Nothing of their code is used or read; neither program was
  disassembled, run or examined for this record, and none will be for the
  code. Their names, test labels as they print them, icons, colours,
  artwork, themes, fonts and texts are not used: the tests carry this
  tool's own labels (section 3.5), the chart this tool's own drawing in
  the system's colours, the icon is drawn for this project.
- The tool's documents may name them as what it was modelled on ("laid
  out after the familiar disk benchmarks"); no text says it is compatible
  with them, equivalent to them, or that its numbers are comparable to
  theirs.
- `legal-provenance.md` gains no section 4 row for this record: no fact is
  taken from either program. Its section 2 and 2a rows for the binaries
  are the code's (section 11).

## 13. Decisions

Each the author's, for the owner to confirm, unless it names the owner.

- **P1** (owner) A benchmark with a window, shipped with the package's
  tools, on every supported system; ATTO and CrystalDiskMark as the
  window's references; Stor98's counters where it drives the disk and a
  generic test otherwise; parameters the user sets; the design before the
  code.
- **P2** File-based only, its own file, created new and deleted after;
  never a raw disk read or write (sections 3.1, 4.2).
- **P3** The names `XHCIPERF.EXE` and `XHCIPF64.EXE` (8.3 names, as
  `XHCITR64.EXE`, `XHCIWZ64.EXE`), the title "xHCI98 Disk Benchmark".
- **P4** One carrier for Stor98's counters on every system, READ BUFFER
  buffer ID `53h`, through pass-through on NT and ASPI on 98 SE and ME
  (section 6.2); the answer read-only, counters that only rise, no reset.
- **P5** The Stor98 page needs administrator rights on NT and asks for
  them by its own button; the benchmark never does.
- **P6** Queue depth by overlapped I/O on NT only; fixed at 1 on 98 SE
  and ME, said in the window and the results.
- **P7** XHCITR gains "Disk benchmark..." (section 11).
- **P8** Default presets per family (section 3.7): Standard on NT, the 9x
  standard on 98 SE and ME; 1 GiB and 256 MiB files.
- **P9** Threads offered on 98 SE and ME (up to 8), with the note that the
  Stor98 page shows whether they overlapped.
- **P10** MB/s decimal by default, MiB/s a View choice.
- **P11** The buffer layout a parameter of its own, scattered by the
  interleaved first touch (section 3.4), its effect shown only by Stor98's
  runs histogram.
- **P12** Results compared with a saved file (section 5.6) rather than
  a history kept by the tool.

## 14. Open, and the legs

### 14.1 Readings owed before or during the code

- **P.0** (before Stor98's change is written) on 98 SE and ME guests,
  static then live: whether stock `WNASPI32.DLL` and `APIX.VXD` are
  installed; whether APIX enumerates a target on Stor98's adapter; an
  INQUIRY through it; if not, the 9x carrier is reopened (the Stor98 page
  on 9x would then be absent, the benchmark unchanged).
- **P.1** every target: the performance counter's frequency; on 98 SE and
  ME, `ReadFile` with an `OVERLAPPED` on a disk file refused, and
  `FILE_FLAG_DELETE_ON_CLOSE`'s behaviour.
- **P.2** 98 SE, ME, 2000 and 7: whether the interleaved first touch
  scatters a buffer (Stor98's runs histogram), and the pattern's cost on a
  slow guest.
- **P.3** 2000, XP, 7 and 10: the `DEVICEMAP` value and the pass-through
  route to Stor98.

### 14.2 Legs

| Leg | Systems | What it reads |
|---|---|---|
| P.4 | 98 SE, ME, 2000, XP, XP x64, Vista x86/x64, 7 x86/x64, 8.1 x64, 10 x86/x64 | the tool starts as a standard user with no UAC prompt; the Quick preset on the system disk and on a USB stick (`/run ... /report /exit` over the serial route of 43.12); the file deleted; the report's header |
| P.5 | 98 SE and ME, Stor98's disk behind the emulated tunnel | the 9x study preset with 43.46's VSD absent and present, each saved, then compared: the Stor98 page shows 63 KiB pieces without it and whole requests with it on the scattered buffer, `phys_sgd` demanded and not; every verification clean |
| P.6 | 2000, 7 x64, 10 x64 with Stor98 | Standard at depth 8: the outstanding-commands histogram above 1; the Stor98 page as administrator, and its refusal and button as a standard user |
| P.7 | 98 SE and 7 x86 | the tunnel dropped during a write test: the run stops with the error, the window responds, the file deleted or remembered and offered at the next start; Safely Remove refused while running |
| P.8 | 98 SE and 7 x86 | refusals: free space, the Windows drive's reserve, the Windows directory, a CD drive, a foreign `XHCIPERF.TMP` kept |
| P.9 | 98 SE and 7 x64 | install, update with the tool running, uninstall: the file placed, "steps left", removed |
| P.10 | the owner's hardware | 98 SE on the P14s with Stor98, before and after 43.46's VSD; Windows 10 or 7 on the same drive; the saved files compared |

### 14.3 Open for the owner

1. The Stor98 change of section 7: approve it as a Stor98 decision (to be
   recorded at the end of Stor98's `docs/PLAN.md`), or keep Stor98 as it is
   and ship the tool without a Stor98 page.
2. The 9x carrier through ASPI (section 6.2), pending P.0.
3. Threads on 98 SE and ME (P9): offered with the note, or fixed at 1.
4. The XHCITR entry (P7).
5. Which release carries the tool: `3.2.0.0` with Phase 43, or the next.
