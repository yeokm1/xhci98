# Design Record 29 - XHCIPERF, the Disk Benchmark

Status: draft 2, 2026-10-22, before code (roadmap-usb4 task 43.50),
reviewed and revised from draft 1 (`67f69523`) the same day: the counter
read sent only to an adapter established as Stor98's, with its requests
owned until they complete; the test file's volume identified by the file
itself; an ownership header and a cleanup handle from creation; an
expected-state map for verification; counters typed and sampled around the
measured interval alone; comparisons by a full workload key; and the
corrections of section 6.1. Written for the owner's request of
2026-10-22, about 12:20: a disk benchmark with a window, shipped with
xHCI98 as one of its tools and working on every system the package
supports; ATTO Disk Benchmark and CrystalDiskMark as the references for the
window; it reads Stor98's counters where Stor98 drives the disk and is a
generic speed test for any other disk; a window to show the results and
parameters the user can set; in Phase 43 (owner: "put it in this phase").
The owner asked for the design first: no code is written against this
record until the owner has seen it converged. Section 13 lists the
decisions taken in writing it, each the author's for the owner to confirm
unless it names the owner; section 14 what is open.

Facts in this record are labelled by how they were taken: **read** (from
this tree's code or documents, or a run sheet's measurement, cited),
**static** (from a binary or a DDK header, nothing executed), or **to be
read** (a reading owed before or during the code, named by its leg).
Win32 behaviour stated without a label is the platform's documented
contract, and every such statement a leg depends on is also read on that
leg.

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
- The common benchmarks do not offer the buffer's physical layout as a
  parameter. Task 43.46's read-first step needed exactly that and wrote a
  throwaway tool for it (`PERF2.EXE`, MSVC 6.0, outside the tree;
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
  buffer aligned, at an offset, or scattered over physical memory as far
  as a program can; a verified data pattern; queue depth and threads where
  the system can queue (section 3).
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
  `INT 13h`, no `INT 25h`/`26h`, no IOS request built by the tool. The
  only device controls the tool sends are read-only property queries and
  section 6's counter read, which moves no data to or from the medium.
- Changing a driver setting, the VSD's presence or Stor98's tunables from
  the tool. The before and after of section 5.6 is two runs with a
  restart and a change made elsewhere between them.
- SMART or health data, firmware, secure erase, TRIM.
- A "score" or any single number that folds the tests together.
- Network drives, CD and DVD drives, SUBST drives (section 4.1).
- Anything of ATTO Disk Benchmark's or CrystalDiskMark's own (section 12).

## 2. Where it lives

- Source: `xhciperf\` (new): `xhciperf.c` (the window, the worker and
  counter threads, every system call), `pf_core.c` / `pf_core.h` (pure: the
  test plan, the pattern and its check, the expected-state map, the
  statistics, the text of every result and report, the saved-result
  reader, the path, volume and free-space rules), `pf_s98.c` / `pf_s98.h`
  (pure: Stor98's answer checked and decoded, two answers subtracted, the
  panel's text), `xhciperf.rc`, `xhciperf.manifest`, `xhciperf.ico`,
  `build.cmd`, `selftest.cmd`, `check-imports.ps1`, `README.TXT`.
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

- **The ownership header.** The file's first 1 MiB is a header region the
  tests never touch: its first 512 bytes hold the signature
  `XHCIPERF-TESTFILE`, a header version, the run's 64-bit identity (the
  start time from `GetSystemTimeAsFileTime` and the process id), the
  file's planned size, the volume's serial number and the creation time;
  the rest is zero. It is written and flushed before anything else. Every
  test's region is the file from 1 MiB to its end, so every offset a test
  uses is aligned to any request size up to 1 MiB and to 1 MiB above it.
  The header does not depend on the data pattern, so a run with the zeros
  pattern leaves a recognisable file too.
- **The ownership record.** Before the header is written, the tool records
  the run under `HKCU\Software\xHCI98\XHCIPERF\Leftover`: the full path,
  the run's identity, the planned size, the volume serial number and, on
  NT, the file index from `GetFileInformationByHandle` once the file
  exists. The record is deleted only after the file is.
- **Created new, never reused blindly.** The tool opens the file with
  `CREATE_NEW`. If the name exists, the tool opens it with no sharing,
  reads its first sector and compares it, its size and (on NT) its file
  index with the ownership record: all matching, it is a leftover of a
  run that did not finish, and the tool offers to delete it ("A test file
  from an earlier run, 1,024 MB, created 2026-10-22 12:40, is still on F:.
  Delete it?"). A header of the tool's own with no matching record (the
  record was lost) is offered with the stronger prompt "F:\XHCIPERF.TMP
  looks like a test file of this tool's, but this computer has no record
  of creating it. Delete it?" and never deleted unasked. Anything else of
  that name is left alone and the run refused ("F:\XHCIPERF.TMP is not a
  file of this tool's; choose another folder"). The tool never truncates
  or overwrites a file it did not create in this run. The check holds the
  exclusive handle until the deletion: on Vista and later the deletion is
  made through that handle (`SetFileInformationByHandle`,
  `FileDispositionInfo`, looked up at run time); before Vista and on 98 SE
  and ME the handle is closed and the file deleted at once by name, which
  leaves a window of no practical size in which the file could be
  replaced, stated here rather than closed.
- **The cleanup handle (NT).** The file is created with
  `FILE_FLAG_DELETE_ON_CLOSE` and sharing `FILE_SHARE_READ |
  FILE_SHARE_WRITE | FILE_SHARE_DELETE`; that first handle is held for the
  whole run, and every other handle (the workers', the verification's) is
  opened with the same three sharing flags, so the file disappears when
  the last handle closes, also when the tool is killed during preparation
  or a test. On 98 SE and ME whether the flag does anything is to be read
  (P.1); the design does not rely on it there, and the ownership record
  covers both.
- **Prepared before it is measured.** The test region is written once from
  start to end with the pattern before the first measured test (the status
  bar: "Preparing the test file"), in 64 MiB steps with the free-space rule
  checked before each (section 4.3). A read of a never-written region
  measures nothing on NT, where NTFS answers reads past a file's valid data
  length with zeros, and an extending write is synchronous there whatever
  the queue depth; preparing the whole region removes both. The
  preparation is not timed and not reported, except its duration in the
  log.
- **Deleted after.** At the end of a run, on Stop, on a refusal after
  creation and on the window's close, the tool closes every handle (on
  NT the cleanup handle last) and, where the file still exists, deletes
  it. A deletion that fails is reported, the ownership record kept, and
  the next start offers the deletion again after the check above.
- **Size.** 16, 32, 64, 128, 256 and 512 MiB, 1, 2, 4, 8 and 16 GiB, plus
  the 1 MiB header; the default 1 GiB on NT and 256 MiB on 98 SE and ME
  (43.46's file was 256 MiB). On a FAT or FAT32 volume the largest offered
  is 2 GiB, which keeps every offset below 2^31 on the 9x file APIs; on
  NTFS any size the free-space rule allows.
- **Attributes.** Normal; not hidden, so a user can see and delete it by
  hand if everything else fails.

### 3.2 Access modes

- **Unbuffered** (the default): `FILE_FLAG_NO_BUFFERING`, so the file
  system's cache is bypassed and each request goes towards the disk at the
  size the program issued, with the alignment rules that flag carries
  (section 3.4). It does not stop a driver below from splitting it - that
  is what section 6 makes visible - nor the drive from caching it. On 98
  SE and ME VFAT passes unbuffered requests of 128 KiB to 1 MiB to IOS
  whole (Stor98's E0B and E1A readings, `INVESTIGATION-9X-REQUEST-SIZE.md`
  section 2, read), which is what makes section 1.1's cut visible.
- **Write-through**: a box, with unbuffered or cached;
  `FILE_FLAG_WRITE_THROUGH` on the write handles. 43.46's reading used it;
  the 9x presets of section 3.7 tick it. On NT it asks for forced unit
  access on each write, which Stor98 handles under its `WriteThrough`
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

- **Three patterns.**
  - **Verifiable** (the default when verification is on): every 512-byte
    unit carries a 16-byte stamp - the signature `XPRF`, the low 32 bits of
    the run's identity, the unit's index in the file, the number of the
    test that wrote it (0 for the preparation) - and 496 bytes from a
    32-bit xorshift generator seeded from those three values. Every unit
    can be checked alone, at any offset, with no copy of what was written.
    The buffer of each request is stamped before the request is issued,
    which costs processor time inside the test (see below).
  - **Random, fixed** (the default when verification is off): one buffer
    filled once from the generator at the run's start and written as it
    is by every request; no per-request cost; not verifiable.
  - **Zeros**: for a comparison with a disk that compresses; not
    verifiable.
  The two generated patterns are meant to defeat a controller's
  compression; no claim is made for every controller.
- **The expected-state map.** For a verifiable run the tool keeps one byte
  per block of the run's smallest write size over the test region (the
  number of the last test that wrote the block, 0 for the preparation).
  A write is recorded in the map when it completes successfully. The plan
  refuses a verifiable run whose map would exceed 16 MiB ("lower the file
  size, or raise the smallest write size, to verify"), and a run of more
  than 254 write tests.
- **No overlapping writes in flight.** A random write test draws its next
  offset again while the drawn block is one of that thread's requests
  still in flight (at most 32, checked by a scan); threads own disjoint
  parts of the region (section 3.5). So no two outstanding writes ever
  cover the same unit, and the completed writes alone decide each unit's
  final content.
- **Verification**, a box (default on for the 9x presets, off otherwise):
  after each write test, an untimed pass reads every block that test
  wrote (unbuffered when the test was) and checks every unit against the
  map: the stamp's signature, run, index and test number, and the body. A
  unit whose stamp names an older test (a dropped write), another index (a
  misplaced write), another run, or whose body differs, is a mismatch.
  "Verify the whole file at the end" (a box, off) checks every block of
  the region against the map once more after the last test. A second box,
  "Check during the timed read" (off), checks inside a read test instead,
  with the cost in the read's time. A mismatch is a result, not a crash:
  the test's cell shows "data error", the log the first five mismatched
  units (file offset, expected and found stamp) and the count, and the run
  stops unless "Stop on a data error" (on by default) is cleared.
- On a cached run the verification reads back through the cache and says
  so; on an unbuffered run it reads past the cache.
- **The processor's share.** Stamping happens between a request's
  completion and its re-issue, inside the measured interval. The worker
  times its stamping with the performance counter; the result carries
  "processor time stamping buffers: 7% of the test", and a test where it
  exceeds 10% is marked "may be limited by the processor" (the 9x presets
  on a slow machine are the case; read on P.2). With the fixed random
  pattern the share is zero.

### 3.4 Buffers and alignment

The owner's case is the buffer's layout, so the tool makes it a parameter.
Alignment is read first:

- the volume's **logical sector** size (`GetDiskFreeSpaceA`), every
  system;
- the disk's **physical sector** size, on Vista and later from
  `IOCTL_STORAGE_QUERY_PROPERTY` with `StorageAccessAlignmentProperty`;
  "not reported" before Vista and on 98 SE and ME, where the logical size
  is taken for it;
- the adapter's **alignment mask** (`StorageAdapterProperty`'s
  `AlignmentMask`), 2000 and later.

Each is shown on the Disk page (section 5.4) and in the result's header.
The buffer choices:

- **Aligned** (the default): `VirtualAlloc`'s pages, offset 0, which meets
  every alignment above up to 4 KiB.
- **Offset**: the request's buffer starting one logical sector into the
  first page, as 43.46's +512 pass did. Unbuffered I/O requires the buffer
  aligned to the logical sector; the offset is a diagnostic of
  misalignment against the page and, on a disk whose physical sector is
  larger than its logical one, against the physical sector too, which the
  label says ("offset 512: not aligned to the page or to this disk's
  4,096-byte physical sector"). A logical sector of 4,096 bytes makes the
  offset a page, the same as Aligned, which the plan says. An offset that
  breaks the adapter's alignment mask is refused.
- **Scattered (attempted)**: an attempt to make the buffer's pages
  physically non-adjacent. A program cannot choose physical pages; it can
  choose the order in which they are first touched. The tool reserves the
  request buffer and a second region of the same size, then commits and
  touches them one page at a time in alternation (buffer page 0, other
  page 0, buffer page 1, ...), so that a system handing out free frames in
  order gives the buffer every other frame, and frees the second region
  afterwards. Nothing in the tool can see the result, so the label never
  claims it: the Stor98 panel's physical-runs histogram (section 6.3)
  shows the layout of the requests as they reached Stor98, after IOS, and
  the result states one of three conclusions for the test: "scattered, as
  seen by Stor98" (requests of the test's size arrived whole with more
  than one physical run each), "not seen: requests were cut or
  double-buffered before Stor98" (they arrived as pieces, or IOS marked
  them double-buffered, so the original layout is hidden), or "not known"
  (no Stor98). If P.2 finds that the first touch does not scatter on a
  system, the choice is withdrawn there rather than kept as a label.
- 43.46's reading found that "+512" and "fragmented" were different things
  that happened together (`runs/run-43.md`: the +512 pass's buffers had up
  to 129 runs, a run a page; the +0 pass's no more than 8). The tool keeps
  them as two separate choices, and the 9x study preset (section 3.7) runs
  aligned, offset and scattered side by side.
- The buffers are allocated once per run, the largest request times the
  queue depth times the threads, plus one request's worth for
  verification; the plan refuses a run whose buffers exceed a quarter of
  physical memory or 256 MiB ("Lower the queue depth, the threads or the
  largest size"). On NT the tool calls `VirtualLock` on them where the
  working set allows and goes on without it otherwise; 98 SE and ME lock
  the pages of each unbuffered request themselves.

### 3.5 Sequential, random, queue depth and threads

- **Threads** (1, 2, 4, 8): the test region is cut into as many equal
  parts as threads, each a whole number of the test's blocks; each thread
  has its own handle on the file, its own part and its own queue, so two
  threads never touch the same unit.
- **Sequential**: requests of the test's size, one after another from the
  start of the thread's part, wrapping at its end.
- **Random**: requests of the test's size at block-aligned offsets in the
  thread's part, chosen uniformly by a xorshift generator whose seed is in
  the result ("seed 0x1A2B3C4D"; each thread's seed derived from it), so a
  run can be repeated; writes avoid blocks in flight (section 3.3).
- **Queue depth** (1, 2, 4, 8, 16, 32): requests outstanding per thread.
  On NT, by overlapped I/O (`FILE_FLAG_OVERLAPPED`), one event per
  outstanding request and `WaitForMultipleObjects` on those events and the
  run's stop event (no I/O completion port: the same code then serves 2000
  to 10 and stays within 64 handles). **Not available on 98 SE and ME**:
  their file systems do not do overlapped I/O on a disk file (the
  investigation's section 3; to be read on P.1 as `ReadFile` with an
  `OVERLAPPED` failing), so the depth box is fixed at 1 there, greyed,
  with "Queue depth 1: Windows 98 and ME file I/O waits for each request".
  A saved 9x result therefore never carries a depth above 1.
- **Threads on 98 SE and ME** are the only way to have more than one
  request at the disk at once, and whether IOS lets two through is
  unmeasured (the investigation's section 3: at most 2 outstanding seen;
  "a queue depth above 1 needs several threads and is unmeasured"); the
  box is offered there with the note "several threads on Windows 98 and
  ME: whether requests overlapped is shown by the Stor98 page" (P9, for
  the owner).
- A test's label states what ran, in this tool's own words (section 12):
  "Sequential 1 MiB, depth 8, 1 thread", short form `Seq 1M d8 t1`;
  "Random 4 KiB, depth 1, 1 thread", `Rnd 4K d1 t1`.

### 3.6 Time, passes and the measurement

- **The interval.** A test's interval is measured on the controlling
  thread: T0 when it releases the workers (one manual-reset event), T1
  when the last worker has reported its last completion. Each worker
  issues until the test's budget is spent (the time, or its share of the
  bytes) and then only collects what is in flight. The result is the sum
  of the workers' completed bytes over T1 - T0.
- **Measuring time** per fixed test: 5 s by default (1, 2, 5, 10, 20, 30,
  60).
- **The sweep** runs each size over a fixed amount of data instead
  ("Total per size", by default the region's size), so its chart compares
  sizes over the same bytes: the total is rounded down to whole requests
  of that size, at least one per thread and per queue slot, and shared
  equally among the threads with the remainder to the first; a "Time per
  size" choice makes it time-based too.
- **Passes**: 1 to 9 (default 3 for fixed tests, 1 for the sweep). Each
  pass repeats every test. The result shown is the best pass by default,
  with a choice of the mean or the median ("Show: best / mean / median");
  the latency shown beside a best value is that pass's, and beside a mean
  or median the passes' histograms merged. Every pass's values go into the
  copied text and the saved file.
- **Pause between tests**: 0, 1, 5, 10, 30 s (default 0), for a disk that
  heats or caches.
- **Warm-up**: none by default; a box runs each test once untimed first.
- **Timer**: `QueryPerformanceCounter` where `QueryPerformanceFrequency`
  succeeds with a nonzero frequency, which the result records;
  `GetTickCount` otherwise, with the result marked "timer: milliseconds".
  On 98 SE and ME the performance counter is the 8254 timer's 1.193182 MHz
  (to be read, P.1, as the frequency the result records). On NT before
  Vista with more than one processor, the counter may differ between
  processors and a thread may move between its two readings, so for the
  run the tool pins the controlling thread and each worker to one
  processor each (`SetThreadAffinityMask`); on every system each reading is
  checked against the one before on the same thread, and against
  `GetTickCount` over the interval: a reading that goes backwards, or an
  interval more than 1.5 times and 100 ms away from the tick count's, marks
  the test "timer unreliable: milliseconds used" and the tick count's
  interval is taken for it.
- **Latency**: the time from a request's issue to its completion as the
  issuing thread sees it, with the performance counter. Min, mean and max
  exact; the median and the 99th percentile from a histogram of 64
  logarithmic buckets (1 us to 100 s, a bucket's value its upper edge), so
  no per-request list is kept. At depth above 1 a request's latency
  includes its wait in the queue, and the label says so ("latency at depth
  8 includes queueing"). With the millisecond timer, latency is not shown
  ("needs a high-resolution timer").
- **Units**: MB/s with MB = 1,000,000 bytes by default, the unit disk
  makers quote; a View choice shows MiB/s (1,048,576), which is what
  `runs/run-43.md`'s tables are ("MiB moved / s"). IOPS is requests per
  second. The copied text names its unit; the saved file's machine lines
  carry bytes, requests and nanoseconds, whatever the View setting.
- **Arithmetic**: the user-mode tool uses `__int64` for byte counts,
  offsets and timer ticks (MSVC 6.0 and WDK 7.1 both have it, and the
  static C runtime carries its helpers, as `xhcitr\tr_snap.c` already
  uses); the pure half has no floating point. Ticks become microseconds as
  `(t / f) * 1000000 + (t % f) * 1000000 / f`, which cannot overflow for
  any frequency below 2^32 Hz and any run the plan allows; a rate in tenths
  of MB/s is `bytes * 10 / us` (bytes below 2^59); rounding is to the
  nearest, halves up; a zero interval or count prints "-". Host vectors
  hold each at its bounds.

### 3.7 Presets

Each preset is a list of fixed tests or a sweep with every parameter set;
choosing one fills the parameters, which the user may then change (the
list then reads "Custom").

- **Standard** (the default on NT): `Seq 1M d8 t1`, `Seq 1M d1 t1`,
  `Rnd 4K d32 t1`, `Rnd 4K d1 t1`; read and write; unbuffered; 1 GiB;
  3 passes of 5 s; random fixed pattern.
- **Standard for Windows 98 and ME** (the default on 98 SE and ME):
  `Seq 512K d1 t1` (the largest request IOS passes whole), `Seq 1M d1 t1`
  (the size IOS always cuts), `Seq 64K d1 t1`, `Rnd 4K d1 t1`; read and
  write; unbuffered, write-through; 256 MiB; 3 passes of 5 s; verifiable
  pattern, verification on. Its note says why the list differs.
- **Request-size study for Windows 98 and ME** (43.46's reading as a
  preset): sizes 64, 128, 256, 512 and 1,024 KiB; sequential write then
  read, unbuffered, write-through; aligned, offset and scattered
  buffers, each a block of the chart; verification on; 256 MiB; 1 pass.
- **Sweep**: sequential, sizes from 512 bytes to 8 MiB in powers of two
  (smallest and largest chosen from that list), read and write,
  unbuffered, depth 1 (up to 4 on NT, selectable), total per size the
  region's size.
- **Quick**: `Seq 1M d1 t1` and `Rnd 4K d1 t1`, 64 MiB, 1 pass of 2 s, for
  a first look and for the legs.
- **The user's own**: "Save preset..." stores the current parameters under
  a name in `HKCU\Software\xHCI98\XHCIPERF\Presets`; the list shows them
  after the built-in ones.

In unbuffered mode a size below the volume's logical sector size is not
offered, and every size is a multiple of it.

### 3.8 The plan

Before anything is created the pure half turns the parameters into a plan
and refuses one that cannot run, each refusal one sentence naming what to
change: a size below the logical sector in unbuffered mode; a region
smaller than the largest request times the depth times the threads; a
thread part smaller than one block; buffers above section 3.4's cap; a
verifiable run whose map exceeds 16 MiB or with more than 254 write tests;
the free-space rule of section 4.3; a depth above 1 on 98 SE or ME (not
reachable from the window, reachable from a command line or a saved
preset); an offset breaking the adapter's alignment; a run over 2 hours
("this run would take about 3 h; lower the passes or the time"). The
plan's estimate of the run's length is shown before Start ("about 4
min").

## 4. Safety

### 4.1 What may be tested

- The user picks a drive (a drop-down of every drive letter whose type is
  fixed or removable, with its label, file system, size and free space)
  or a folder on one (Browse..., section 10.2). The default folder is the
  first of: the root of the first fixed drive that is not the Windows
  drive, if the tool can create a file there; the Windows drive's root,
  likewise; the user's `TEMP` folder (the case of a standard user on
  Vista and later, whose drive roots are usually not writable).
- **The path** is made canonical before any check (`GetFullPathNameA`, and
  `GetLongPathNameA` where present, looked up at run time). Refused: a UNC
  path; a drive whose `GetDriveType` is remote, CD-ROM, unknown or no
  root; on NT, a drive letter that `QueryDosDeviceA` shows to be a SUBST
  alias (`\??\` target), and a path any of whose folders is a reparse
  point (junction or mounted folder: `FILE_ATTRIBUTE_REPARSE_POINT`), so
  the folder chosen is on the volume its drive letter names; on 98 SE and
  ME, a SUBST drive (whose volume serial number equals another letter's
  while `GetDriveType` and the configuration manager find no disk of its
  own) is refused as "not a disk drive"; a folder the tool cannot create a
  file in ("You cannot write to F:\X; choose another folder"); the Windows
  directory and the system directory and their subfolders, compared on
  the canonical paths. A RAM disk is allowed and its results say "RAM
  disk".
- **The volume** of every later decision - the reserve, the file system,
  the sector sizes, Stor98 - is the volume the test file was created on,
  identified by the file itself: its volume serial number from
  `GetFileInformationByHandle` on the open file (98 SE and later), checked
  against the serial numbers of the Windows directory's volume and the
  9x swap file's. On NT the volume must lie on one disk:
  `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` (XP and later) on the volume, and
  more than one extent gives a generic run with no Stor98 page ("this
  volume spans several disks"); on 2000, where the volume is basic it is
  one disk, and a dynamic volume is reported as "not known".
- Allowed with a note: the Windows drive ("the drive Windows runs from:
  other programs' activity will show in the results") and the drive of the
  98 SE or ME swap file.
- The tool writes nothing anywhere but its test file, its `HKCU` key and,
  when the user asks, the result file the user names.

### 4.2 Never a raw disk write

The benchmark is file-based on every system; it never opens a physical
drive, a volume or a partition for data, never locks or dismounts a
volume, and the device controls it sends are read-only property queries
(section 5.4) and section 6's counter read, which Stor98 answers from
memory without touching the medium. This is a rule of the code:
`check-imports.ps1` cannot see it, so the self-test (section 10.3) greps
the system half for `PhysicalDrive` and `\\.\` and admits exactly the
openings sections 5.4 and 6.2 name, and the host suite holds the plan to
never produce any other target.

### 4.3 Free space

- Free space comes from `GetDiskFreeSpaceExA` (looked up at run time; 98
  SE has it): the bytes available to this user decide whether the file
  fits, the volume's total free bytes decide the reserve; on a system
  without it, `GetDiskFreeSpaceA` with its 2 GiB cap gives both.
- A run needs the file's size and must leave a reserve: 64 MiB on a volume
  that is not the Windows volume or the swap file's; on those, the larger
  of 512 MiB and 10% of the volume, because a full Windows drive stops 98
  SE and ME (the swap file grows into it) and starves other programs'
  writes on NT. The refusal names the numbers ("F: has 900 MB free; this
  run needs 1,024 MB and leaves 64 MB").
- Free space is read again before the file is created and before each
  64 MiB step of the preparation; a step that would cross the reserve
  stops the preparation and refuses the run, and the file is deleted. The
  reserve is kept against the tool's own writes; another program writing
  to the volume at the same time can still use it, which the refusal's
  help text says.

### 4.4 Removal, errors, Stop and outstanding requests

- **Stop** sets the run's stop event. A worker never stops waiting for
  its own requests: on NT it wakes on the stop event, calls `CancelIo` on
  its own handle itself (which before Vista cancels only the calling
  thread's requests; `CancelIoEx` is not needed and not used), and then
  waits for every one of its `OVERLAPPED` requests to complete or be
  cancelled before its buffers, events or handle are released. On 98 SE
  and ME the request in flight cannot be cancelled; the worker returns
  when it does. The results so far are kept and marked "stopped".
- **The window stays responsive**: the window thread never waits on a
  worker or on the counter thread. While a worker still has a request
  outstanding after Stop, the status bar says "Waiting for the disk to
  finish the last request" and the close button asks again; the tool
  never terminates a thread and never frees memory a request may still
  write. If the disk never answers (a stalled 9x request), the window
  says so and offers only to keep waiting; the user may end the program
  with the system's own task list, which the text says, and the ownership
  record then covers the file.
- **A failed request** (any `ReadFile`, `WriteFile` or wait that fails:
  `ERROR_DEVICE_NOT_CONNECTED`, `ERROR_NOT_READY`, `ERROR_GEN_FAILURE`,
  `ERROR_IO_DEVICE`, `ERROR_CRC`, a short transfer, ...) stops the run as
  Stop does: the cell shows "failed", the log the error code and its
  text, the file offset and the size, and the window says "The disk
  stopped answering (error 1167: The device is not connected). The run was
  stopped." No retry, and no new request after the first failure.
- **The disk removed** (unplugged, a tunnel dropped, ejected elsewhere) is
  that case: the first request after it fails, the run stops, and the
  tool tries once to delete the file; a deletion that fails keeps the
  ownership record (section 3.1). `SetErrorMode(SEM_FAILCRITICALERRORS)`
  is set for the whole process, so 98 SE and ME raise no critical-error
  box over the window. Whether a removed Stor98 disk answers on 98 SE or
  ME with an error or a stall is Stor98's fast-fail behaviour (its
  `AGENTS.md`, "Surprise removal is a normal event") and is read on P.7.
- **Safely Remove during a run**: the open test file makes the system
  refuse the removal (Windows' own applet, XHCITR, `INSTALL /eject`); the
  tool does not answer the query itself. On NT it registers for the
  volume's handle (`RegisterDeviceNotificationA`, looked up at run time)
  and, on `DBT_DEVICEQUERYREMOVE` while running, says in its status bar
  "Windows asked to remove F:; stop the test first"; it does not stop by
  itself.
- **Sleep**: `SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_CONTINUOUS)`
  (looked up at run time) keeps the system from sleeping during a run and
  is cleared after; it does not stop a screen saver, which the readme
  says.
- **Closing the window during a run** asks "Stop the test and close?",
  then stops as above, deletes the file and exits once nothing is
  outstanding.

## 5. The window

### 5.1 Layout

Title "xHCI98 Disk Benchmark". One resizable top-level window, at least
640 by 480 so it fits 98 SE's smallest screen, laid out top to bottom:

- **The parameters band**: Drive or folder (drop-down and Browse...),
  Preset (drop-down), File size, Passes, Time per test; Access
  (Unbuffered / Cached, Write-through box), Buffer (Aligned / Offset /
  Scattered), Pattern (Verifiable / Random / Zeros), Verify box; Depth and
  Threads (the depth greyed at 1 on 98 SE and ME); Tests (Read, Write,
  Both); the plan's estimate; **Start** and **Stop**. "More..." opens a
  dialog for the rest (the sweep's sizes and total, the pause, warm-up,
  the flush in the write time, check during the read, verify the whole
  file, stop on a data error, the seed, the custom test list).
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
mean latency, with a mark for "may be limited by the processor" and
"timer unreliable". The row header is the test's long label; a Start
button per row runs that row alone. Below the grid, one line: the drive,
its file system, the file size, the access mode, the buffer, the passes
and which value is shown. The grid is a window class of the tool's own,
painted with GDI, with the dialog font scaled for its large numbers, so 98
SE and 10 draw it the same.

### 5.3 Sweep

A horizontal bar chart, one band per request size from smallest (top) to
largest, two bars in each band, write and read, their length in MB/s on a
common axis whose scale is chosen from the largest value (rounded up to
1, 2 or 5 times a power of ten), the value printed at each bar's end;
beside it a table of the same numbers with IOPS and latency. When the
preset ran several buffers (the 9x study), the chart groups them: a block
of bands per buffer layout, the same sizes in each, so aligned, offset and
scattered sit one above another. Painted with GDI in the system's colours
(`COLOR_HIGHLIGHT` for read, `COLOR_BTNSHADOW` hatched for write) with no
colour of the tool's own, so it reads under 98 SE's 16 colours and high
contrast.

### 5.4 Disk

What the system reports about the disk under the test file, read-only,
with no administrator rights:

- every system: the volume's label, file system, serial number, cluster
  and logical sector size, size and free space;
- NT: on the volume opened with no access rights (`\\.\F:`, `CreateFileA`
  with access 0): `IOCTL_STORAGE_QUERY_PROPERTY` for
  `StorageDeviceProperty` (vendor, product, revision, bus type, removable),
  `StorageAdapterProperty` (maximum transfer length, maximum physical
  pages, alignment mask) and, on Vista and later,
  `StorageAccessAlignmentProperty` (logical and physical sector sizes and
  their alignment); `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`;
  `IOCTL_SCSI_GET_ADDRESS` (port, path, target, lun), which section 6.2
  also uses; each answer that fails reads "not reported by this system";
- 98 SE and ME: the volume's facts above, and the disk's devnode found
  through the configuration manager (section 6.2) with its hardware id and
  its adapter's driver.

### 5.5 Stor98

When section 6.2 establishes that Stor98 drives the disk, the page shows
the counters as the difference between Stor98's answers taken just before
and just after each measured interval (section 6.2, "When it is read"),
the test chosen from a drop-down of the run's tests: the request sizes the
driver received and the sizes of the commands it sent to the drive as two
histograms, the splits, the bounces, the physical runs per request, the
outstanding commands, the errors, and the driver's limits and modes.
Every figure is the adapter's, from every program using the disk during
the test, and the page says so in its first line ("Everything Stor98's
adapter did during this test, from any program"). Its next lines put the
two sides together without attributing one to the other, for example on
98 SE: "The test issued 512 requests of 512 KiB. During it Stor98 received
512 requests of 512 KiB and none of 63 KiB, and sent 512 commands of
512 KiB. The system demands physical scatter/gather: no (an IOS layer
above SCSIPORT removed the demand)." When Stor98 does not drive the disk:
"This disk is not driven by Stor98; the Stor98 page has nothing to show."
When Stor98 drives it but cannot be read: the reason (section 6.2) and,
on NT without administrator rights, a button "Read as administrator..."
that starts the tool elevated with the same parameters and the Stor98 page
open (`ShellExecuteA` verb `runas` on Vista and later; on 2000 and XP the
button says to run the tool as an administrator).

### 5.6 Copy, save and compare

- **Copy results** puts the whole result on the clipboard as plain ASCII
  text, 79 columns: a header (the tool's version, the date, the system and
  its version, the drive, file system and sector sizes, the disk's model
  where known, every parameter, the timer and its frequency, the seed),
  the Summary as a table, the Sweep as a table, the Stor98 page's
  differences per test when it was read, and every pass's value. The same
  text is the saved file (`.TXT`, `GetSaveFileNameA`).
- The saved file also carries machine-readable lines after the human
  text: one `XPRF-RUN 1` line with the **workload key** - every parameter
  that changes a measurement (file size, access mode, write-through,
  flush in the write time, buffer, pattern, verification and where it
  ran, depth, threads, time or total per size, passes, warm-up, pause,
  seed, the value shown, the timer kind) - and one `XPRF-RESULT 1` line
  per test and pass with the test's label, operation and buffer and its
  bytes, requests, interval in nanoseconds and latency histogram summary,
  in those canonical units whatever the View setting.
- **Compare with saved results...** reads such a file back. Results whose
  test label, operation, buffer and whole workload key equal the current
  run's are shown beside it in Summary and Sweep (the saved value grey,
  the current black, the change in percent). Results that differ in any
  key field are listed apart under "Not comparable", with the fields that
  differ, and get no percentage. A file whose lines do not parse is
  refused with the line number. This is the before and after the owner
  asked for: a run with the 43.46 VSD absent, saved; the VSD installed and
  the machine restarted; the same preset run again and compared.

### 5.7 The command line

`XHCIPERF /run [/preset <name>] [/folder <path>] [/size <MiB>]
[/passes <n>] [/time <s>] [/buffer aligned|offset|scattered]
[/pattern verifiable|random|zeros] [/cached] [/writethrough] [/verify]
[/depth <n>] [/threads <n>] [/report <file>] [/exit]` starts the run at
once with those parameters over the defaults, writes the report if asked
and exits with 0 (every test passed), 1 (a test failed or a data error),
2 (refused) or 3 (stopped). The window shows the run; `/exit` closes it at
the end. This is what the emulator legs drive (43.12's unattended route)
and what a user pastes into a batch file. `/selftest` renders every text
of the pure half at its longest to a file and exits, for section 10.3.

## 6. Stor98's counters

### 6.1 What Stor98 exposes today (read, 2026-10-22, `src\stor98\src`)

- **No statistics path a program can read in a release build.** The
  device extension (`nvme2k.h`) carries totals - `ParentsAdmitted`,
  `ParentsCompleted`, `ChildrenIssued`, `FastFails`, `BusyReturned`,
  `StaleCqes`, `GoneDeadlineCount`, `ResetCalls`, `WtFuaWrites`,
  `PollTicks` and more - a gauge (`ChildrenIo`, children issued on the I/O
  queue and outstanding), a maximum (`MaxChildBytes`) and limits
  (`ChildLimitIo`), which reach only the debug port of test builds
  (`dbgport.h`, `g9dbg.h`: QEMU's port `0xE9` and `usb4-emu`'s debug
  port).
- **`SRB_FUNCTION_IO_CONTROL`** (`nvme2k.c` `ScsiDispatch`, NT's
  `IOCTL_SCSI_MINIPORT` route) is answered by two handlers: the inherited
  `NVME2KDB` signature, whose one code `0x1000` returns success with no
  data, and `HandleIO_SCSIDISK`, every one of whose control codes returns
  FALSE today, so it answers nothing (`nvme2k_scsi.c`, its header
  comment). `include\stor98cm.h` declares `STOR98CM_CODE_QUERY` ("who is
  this adapter") as interface 1, diagnostic only, but no handler for the
  `STOR98CM` signature is in the sources.
- **LOG SENSE** (`ScsiHandleLogSense`) answers only the informational
  exceptions page, through an NVMe Get Log Page, and refuses every other
  page with INVALID FIELD IN CDB (despite its comment's "we assume any log
  sense is for SMART data").
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
- **States**: outside the running state a request is held and replayed
  through `ScsiDispatch` once the adapter is running (`engine.c`
  `EngDispatchEx`), and in the gone state new requests are refused before
  dispatch (`lifecycle.c`). A counter read is therefore answered only
  while the adapter runs.

So the tool needs one change in Stor98: a read-only answer carrying the
counters. Section 7 proposes it.

### 6.2 Finding Stor98, the carrier, and who owns a request

**The carrier is one SCSI command, READ BUFFER (`3Ch`), on every system.**
Stor98 answers it from its device extension, with no NVMe command and no
access to the medium, like INQUIRY. One command serves both families,
because each has a documented program route for a SCSI command and none
for a miniport control on 98 SE and ME. **The rule: READ BUFFER is sent
only to a target already established, before either READ BUFFER mode is
sent, to be on an adapter that Stor98 drives.** Matching by INQUIRY
strings is never that proof (two disks of one model, or one on another
driver, share them).

- **NT (2000 to 10).** With no rights: `IOCTL_SCSI_GET_ADDRESS` on the
  volume gives the SCSI port number; `HKLM\HARDWARE\DEVICEMAP\Scsi\Scsi
  Port <n>`, value `Driver`, must read `stor98` (to be read, P.3, on 2000,
  XP, 7, 8.1 and 10); and the volume must be on one disk (section 4.1).
  Then, with administrator rights (elevated on Vista and later), the tool
  opens **the same volume** (`\\.\F:`, `GENERIC_READ | GENERIC_WRITE`,
  every sharing flag), checks on that handle that `IOCTL_SCSI_GET_ADDRESS`
  still gives the same port, path, target and lun and the `DEVICEMAP`
  value still names `stor98`, and only then sends
  `IOCTL_SCSI_PASS_THROUGH` with a data-in buffer on that handle, so no
  physical-drive number found earlier can have been reused by another
  disk in between. A 32-bit process on x64 sends the 32-bit
  `SCSI_PASS_THROUGH` layout, which the system converts; the 32-bit
  tool's own path is kept and read on P.6, and the x64 relaunch of
  section 10.1 is a packaging choice, not a requirement of it.
  `IOCTL_SCSI_MINIPORT` on `\\.\Scsi<n>:` was considered and not taken: it
  needs the same rights, a second handler in Stor98 and a route that has
  no 9x counterpart.
- **98 SE and ME.** The ASPI layer (`WNASPI32.DLL`, loaded at run time,
  and `APIX.VXD` under it), whose `SendASPI32Command` with
  `SC_EXEC_SCSI_CMD` sends a CDB to a target of a `SCSIPORT.PDR`
  miniport's adapter. The Windows 98 DDK carries its interface
  (`block\wnaspi32`, `WNASPI32.H`; static: the extracted DDK's file list).
  The adapter is established as Stor98's by ASPI itself before any READ
  BUFFER: `SC_HA_INQUIRY`'s adapter identifier string, which for a
  `SCSIPORT.PDR` adapter is expected to name the miniport, must name
  `stor98` (or `nvme2k` where a devnode still names the inherited file,
  as the 43.46 image does). Whether stock 98 SE and ME install
  `WNASPI32.DLL` and `APIX.VXD`, whether APIX lists Stor98's adapter, and
  what its identifier string holds are to be read first (P.0); if no ASPI
  field identifies the adapter's driver, the 9x Stor98 page is not built
  and the benchmark is unchanged. Among Stor98's adapters (a machine may
  have more than one), the disk under the test file is matched by its
  devnode - the configuration manager's walk from the drive letter to the
  disk devnode (`CurrentDriveLetterAssignment`) and to its parent adapter
  and that adapter's PCI location - against the PCI location in each
  Stor98 answer (section 6.3); sending READ BUFFER to another Stor98
  adapter on the way is harmless, since Stor98 answers it from memory. No
  match, or two, and the page says "this disk's Stor98 adapter could not
  be told apart".
- Where neither route works (no ASPI on 9x, no rights on NT, the adapter
  not running), the page says which, and the benchmark is unaffected.
- **Who owns a request.** Counter reads run on a counter thread of their
  own, one request at a time; the window and the workers never wait on
  it. On NT the pass-through carries a `TimeOutValue` of 2 s, which the
  port applies to the command, and the call returns when the port
  completes it; its buffer and structure belong to the counter thread
  until then. On 98 SE and ME the request is posted with
  `SRB_EVENT_NOTIFY` and its SRB, buffer and event are held by the counter
  thread until ASPI marks it done; after 2 s without an answer the tool
  asks ASPI to abort it (`SC_ABORT_SRB`) and keeps waiting for completion,
  and the DLL is never unloaded while the program runs. A read not
  answered in 2 s marks the counters "Stor98 did not answer in time" for
  the rest of the run, and no further read is sent; at exit the tool waits
  for the outstanding read as section 4.4 waits for a worker.
- **When it is read**: once before the run; then just before and just
  after each measured interval, with no request of the tool outstanding -
  after the preparation, any warm-up and the pause, and before any
  verification or flush outside the interval - so the delta covers the
  interval alone. A cached test's flush inside the interval is inside its
  delta. The G3 reading that a miniport control waits for a free slot
  under a full queue (`stor98cm.h`, interface 1) does not bite then.

**The CDB**: READ BUFFER, mode `02h` (data), buffer ID `53h`, offset 0,
allocation length the answer's size; mode `03h` (descriptor) with the same
ID returns the size, so a tool and a driver of different versions agree on
the length first. The answer starts with a signature and versions, which
the tool checks as a second guard.

### 6.3 What a benchmark needs from the answer

Grouped as `stor98pf.h` would lay them out (section 7.2). Every field has
one of four kinds, written in the header: a **total** (a counter that only
rises, 32 bits, wrapping; the tool subtracts two answers modulo 2^32, and
since a test lasts at most 2 hours no total can wrap twice between two
reads of it), a **wide total** (sectors, a 32-bit low and high pair carried
with no 64-bit arithmetic in the driver), a **gauge or maximum** (shown as
read, never subtracted) and a **fact**. Nothing resets; an **epoch** (a
value that changes at every adapter start) tells the tool when two
answers cannot be subtracted.

1. **Identity** (facts): signature `S98P`, the interface version, the
   answer's length, Stor98's file version, the build (9x or NT, x86 or
   amd64), the adapter's PCI bus, device and function, the namespace's
   logical block size and its size in blocks, the epoch.
2. **Requests received** (the SRBs, "parents"; totals): read and write
   data SRBs, each counted once when it is first admitted and not again
   when a held SRB is replayed (the engine marks the replay,
   `ReplayAdmitTime`, `engine.c` `EngDispatchEx`); their sectors per
   direction (wide totals); a size histogram per direction in 13 buckets
   (below 4 KiB, 4 KiB, to 8, to 16, to 32, below 63 KiB, exactly 63 KiB =
   64,512 bytes - IOS's piece -, to 64 KiB, to 128, to 256, to 512 KiB, to
   1 MiB, above).
3. **Commands sent** (the NVMe commands, "children"; totals): the same
   histogram per direction; parents split into more than one child;
   children bounced through Stor98's own buffer; read-modify-writes on a
   4,096-byte namespace; flushes; and an outstanding-commands histogram
   taken at each child's issue (1, 2, 3-4, 5-8, 9-16, 17-32, above), which
   shows the depth that reached the drive and whether two 9x threads ever
   overlapped. `ChildrenIo` as a gauge, `MaxChildBytes` as a maximum.
4. **Physical layout** (totals): the physical runs of each data child's
   buffer as Stor98 maps it (1, 2, 3-4, 5-8, 9-16, 17, 18-32, 33-64,
   above). It describes the requests as they reached Stor98, after any
   cut or double buffer above it (section 3.4).
5. **Errors** (totals): data SRBs completed with an error, fast fails,
   busy returns, resets, gone episodes, gone deadlines taken, stale and
   late completions (section 6.1's counters, carried as they are).
6. **Limits and modes** (facts): `MaximumTransferLength` and
   `NumberOfPhysicalBreaks` as the port granted them, the controller's
   MDTS in bytes, the children allowed at once (`ChildLimitIo`), the
   write-through mode as resolved (off, cache off or FUA) and whether a
   volatile write cache is present, polling or interrupts and why.
7. **On 98 SE and ME, what IOS hands Stor98**: of the data SRBs, how many
   arrived with a linear buffer, with a logical scatter/gather list, or in
   a shape not recognised (`sg9x.h`'s classification), and how many IOS
   marked double-buffered (totals); and the DCB's `DCB_max_xfer_len`,
   `DCB_max_sg_elements` and `DCB_dmd_flags` (facts) as last observed,
   with a flag saying whether they were observed since the epoch began.
   `DCB_dmd_flags` without `DCB_dmd_phys_sgd` (`0x800`) is the proof that
   an IOS layer above `SCSIPORT.PDR` removed the demand - 43.46's reading
   saw `0x00040600` at the first request with its prototype and
   `0x00000E09` from `SCSIPORT.PDR` alone - which is how the panel says
   whether a VSD is in effect without knowing which VSD.

What a benchmark needs and the answer gives: the request sizes at the
drive (3) beside those the test issued (the tool's own count) and those
the driver received (2), so a user sees a 1 MiB request become sixteen 63
KiB requests at (2) and (3); the splits Stor98 made itself (3); the depth
reached (3); the errors (5); and why (6, 7).

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
  switch). A CDB shorter than 10 bytes, any other mode, buffer ID or a
  nonzero offset: CHECK CONDITION, ILLEGAL REQUEST, INVALID FIELD IN CDB,
  as INQUIRY refuses what it does not support. The copy is bounded by the
  allocation length and the SRB's `DataTransferLength`, whichever is less,
  and a short copy is reported as a data underrun with the bytes moved.
- Counters added on the existing paths: section 6.3's totals, each a
  `ULONG` increment (a pair for the wide totals) at one named update site
  per field - data SRBs at their first admission in `EngSubmitReadWrite`,
  children at issue, completions where the parent completes - under the
  serialisation those paths already have (`engine.h`'s contract: the port
  calls `HwStartIo`, the interrupt and the timer callbacks, polling
  included, synchronised with one another, so a snapshot built in
  `HwStartIo` is consistent with no lock of its own). No allocation, no
  timer, no new state in the request path.
- Item 7's DCB fields copied on the 9x build only, through a checked
  accessor of their own, valid for linear and scatter/gather requests
  alike: the IOP pointer in the system arena with its `IOP_srb` pointing
  back at the SRB (as `Sg9xClassify` checks), the `IOP_physical_dcb`
  pointer in the arena, and only then the three fields. `Sg9xClassify`
  returns for a linear buffer before it reads the DCB (`sg9x.h`), so its
  checks are not reused as they stand. The three fields' offsets and
  widths come from the Windows 98 DDK's `DCB.H` and are recorded with
  their provenance in Stor98's `INVESTIGATION-9X-REQUEST-SIZE.md` section
  7 beside `DCB_apparent_blk_shift`'s before they are used (static). A
  shape that fails a check leaves the fields at their last value and
  counts the request as "not recognised".
- Nothing else: no setting, no write route, no reset, no change to the
  CM channel, to `stor98cm.h` or to any interface version.

### 7.2 The header

`src\stor98\include\stor98pf.h`, new, BSD-3-Clause with the licence note
`stor98cm.h` carries ("an interface definition, licensed on its own ...
including it in a GPL-2.0 work does not change the licence of this
file"): the CDB values, the answer's layout (a header with signature
`S98P` = `0x50383953`, `STOR98PF_VERSION` 1, the length, then fixed
sections of `ULONG`s at 4-byte offsets, each field's kind of section 6.3
in its comment, compile-time size and offset checks as `stor98cm.h` has),
the bucket edges as macros, and the rule that a reader accepts a longer
answer of the same version and reads only what it knows.
`STOR98PF_VERSION` is a protocol version, never tied to the package
version (Stor98's `AGENTS.md`, "Releases"). The tool includes the header
from `src\stor98\include\`; nothing of the tool's GPL-2.0 code goes into
Stor98, and nothing of Stor98's code into the tool beyond the header's
definitions.

### 7.3 Stor98's tests and gates

- Unit tests in `src\stor98\tests\unit\`: the answer's builder (pure, from
  a device extension's fields: every section, a short allocation length,
  a short `DataTransferLength`, mode `03h`, a short CDB, a wrong buffer ID,
  mode or offset, the histogram edges at each bucket boundary and at 63 KiB
  exactly, a wide total's carry, a total's wrap), a replayed held SRB
  counted once, and the 9x DCB accessor on a linear and a scatter/gather
  request with a good, a foreign and an out-of-arena IOP and DCB.
- Emulator tests as its `AGENTS.md` requires ("Every fix gets a test in the
  emulator", including the 4 KiB sector, surprise-removal-during-I/O and
  tunnel-reconnect cases): the answer read while idle and between large
  transfers on 98 SE and on 2000 or 7, under interrupts and under polling
  mode, on a 512-byte and a 4,096-byte namespace; a read sent during
  bring-up (held, then answered in order once running); a read with I/O
  completing around it; a read in the gone state (refused); a surprise
  removal and a reconnect with the tool reading between tests (the epoch
  changes); and, from the emulator's NVMe trace, no NVMe command issued for
  any counter read.
- This package's gates: Stor98's import allowlist is unchanged (no new
  import); `check-stor98-build.ps1` unchanged; the source stamp covers the
  new header.

### 7.4 Why this route

- **READ BUFFER** is unused by Stor98 and by the systems' own drivers
  above it, has a descriptor mode that gives the answer's size, and a
  buffer ID that names the answer; a vendor page of LOG SENSE would also
  be new (the handler refuses every page but one, section 6.1) and would
  serve as well, at the cost of LOG SENSE's parameter framing around the
  answer. READ BUFFER is chosen; LOG SENSE is the fallback if P.0 or P.3
  finds a layer that refuses READ BUFFER.
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
parameters (presets, the sweep's sizes and byte budget, the thread parts,
the sector and alignment rules, the buffer, file and map caps, the 9x
depth rule, the estimate); the random offsets from the seed and the
in-flight exclusion; the pattern's unit, its check and the expected-state
map; the statistics (ticks to microseconds, rates in tenths, IOPS, the
latency histogram, its median and 99th percentile, best, mean and median
over passes, the processor share, the timer's plausibility check); the
canonical path checks and the free-space and reserve rule; every text of
the window, the copied result and the saved file, at 79 columns; the
workload key, the saved file's reader and the compare; Stor98's answer
checked (every offset bounded by the bytes returned, the signature, the
versions, the length), two answers subtracted (wrap, wide totals, a
changed epoch refused, gauges not subtracted), the scattered conclusion
of section 3.4, and the panel's text.

`test\test_pf.c` covers each with golden vectors: a plan per preset per
system family (9x refusing depth 8 from a command line, NT accepting it),
4,096-byte logical and physical sectors, a region too small, buffers and
maps over the cap; the pattern's unit for given seeds and its check
against the map finding a dropped write (an older test's stamp), a
misplaced write, another run's unit, a changed body, an untouched region
and a block written twice; the in-flight exclusion at depth 32; the
arithmetic at its bounds (frequencies of 1,193,182 Hz, 3,579,545 Hz and
near 2^32, a 2-hour interval, a zero interval) and each rounding edge; a
backwards and an implausible timer reading; percentiles of a histogram;
the reports at their longest; a saved file read back, with a bad line, a
missing test, and a workload key differing in each field. `test\test_pfs98.c`
covers the answer as the tool reads it: truncated, a longer answer of
version 1, a wrong signature, a wrap between two answers, a wide total's
carry, a changed epoch, the 63 KiB bucket, and the panel's sentences for
98 SE with and without `phys_sgd` and for each scattered conclusion.

## 10. Build, imports and runtime

### 10.1 Toolchain

As XHCITR (record 25 section 9) and the windowed installer (record 26
section 4):

- `XHCIPERF.EXE`: the in-repo MSVC 6.0, `/W3 /WX /O2`, the C runtime
  static and multithreaded (`/MT`: the run uses worker and counter threads
  made with `_beginthreadex`, as XHCIWIZ's engine is), `/SUBSYSTEM:WINDOWS,4.0`,
  so it starts on 98 SE, ME, 2000, XP, Vista, 7, 8, 8.1 and 10.
- `XHCIPF64.EXE`: the same sources with WDK 7.1's amd64 compiler, `/MT`,
  `/SUBSYSTEM:WINDOWS,5.02`, for the x64 systems. Like XHCIWIZ, the 32-bit
  program under WOW64 (`IsWow64Process`, looked up at run time) starts
  the x64 one from its own directory with its arguments and exits with
  its code, so a user who starts the wrong one gets the native one; with
  no x64 image beside it, the 32-bit program runs itself, its counter read
  included (section 6.2).
- ANSI C89 as the other tools, with `__int64` (section 3.6) and no
  floating point in the pure half.
- `xhciperf\build.cmd` builds both, `build.cmd x86` the 32-bit one;
  `scripts\build-driver.cmd` builds and self-tests it as it does XHCITR
  (its "xhcicln and xhcitr, rebuilt" step), and `make-release.ps1` takes
  them as it takes XHCITR's pair.

### 10.2 Imports

Static imports: `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32` (the `HKCU`
key), `SHELL32` (Browse..., the readme, `runas`), `COMCTL32` (tab,
progress bar, status bar), `COMDLG32` (Save As, Open) - the seven XHCITR
has, every one present on 98 SE; `check-imports.ps1` allows exactly them.
Looked up at run time: `GetDiskFreeSpaceExA`, `GetLongPathNameA`,
`CancelIo`, `SetThreadExecutionState`, `RegisterDeviceNotificationA`,
`SetFileInformationByHandle`, `IsWow64Process`, `QueryDosDeviceA`, every
configuration-manager routine (`cfgmgr32.dll`, as XHCITR's 9x walk does),
and `OLE32.DLL`'s `CoInitialize` and `CoUninitialize` for Browse...;
`WNASPI32.DLL` by `LoadLibraryA` on 98 SE and ME alone. No `WINMM` (the
millisecond fallback is `GetTickCount`), no `SETUPAPI` static import.

**Browse...** is `SHBrowseForFolderA` (`SHELL32.DLL` 4.0 and later, so 98
SE) with `BIF_RETURNONLYFSDIRS`, called on the window thread after
`CoInitialize` (where `OLE32.DLL` is present, as on every target; without
it, Browse... is greyed and the folder is typed); the returned item list
is turned into a path with `SHGetPathFromIDListA` and freed with the
shell's allocator (`SHGetMalloc`, then its `Free`), so no `OLE32` import is
needed for the free. Browse is read on the oldest target (P.4).

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
  node that starts XHCIPERF with that drive chosen (P7, for the owner).
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
- The tool's documents may say it is laid out after the familiar disk
  benchmarks; no text says it is compatible with them, equivalent to them,
  or that its numbers are comparable to theirs.
- `legal-provenance.md` gains no section 4 row for this record: no fact is
  taken from either program. Its section 2 and 2a rows for the binaries
  are the code's (section 11).

## 13. Decisions

Each the author's, for the owner to confirm, unless it names the owner.

- **P1** (owner) A benchmark with a window, shipped with the package's
  tools, on every supported system, in Phase 43; ATTO and CrystalDiskMark
  as the window's references; Stor98's counters where it drives the disk
  and a generic test otherwise; parameters the user sets; the design
  before the code.
- **P2** File-based only, its own file, created new with an ownership
  header and record, deleted after; never a raw disk read or write
  (sections 3.1, 4.2).
- **P3** The names `XHCIPERF.EXE` and `XHCIPF64.EXE` (8.3 names, as
  `XHCITR64.EXE`, `XHCIWZ64.EXE`), the title "xHCI98 Disk Benchmark".
- **P4** One carrier for Stor98's counters on every system, READ BUFFER
  buffer ID `53h`, through pass-through on NT and ASPI on 98 SE and ME,
  sent only to an adapter first established as Stor98's (section 6.2);
  the answer read-only, typed fields, no reset.
- **P5** The Stor98 page needs administrator rights on NT and asks for
  them by its own button; the benchmark never does.
- **P6** Queue depth by overlapped I/O on NT only; fixed at 1 on 98 SE
  and ME, said in the window and the results.
- **P7** XHCITR gains "Disk benchmark..." (section 11).
- **P8** Default presets per family (section 3.7): Standard on NT, the 9x
  standard on 98 SE and ME; 1 GiB and 256 MiB files.
- **P9** Threads offered on 98 SE and ME (up to 8), with the note that the
  Stor98 page shows whether they overlapped.
- **P10** MB/s decimal by default, MiB/s a View choice; canonical units in
  the saved file.
- **P11** The buffer layout a parameter of its own; scattered as an
  attempt by the interleaved first touch (section 3.4), its effect stated
  only from Stor98's runs histogram and never claimed otherwise.
- **P12** Results compared with a saved file by a full workload key
  (section 5.6) rather than a history kept by the tool.
- **P13** Verification against an expected-state map, with no two
  writes to one unit in flight (section 3.3).

## 14. Open, and the legs

### 14.1 Readings owed before or during the code

- **P.0** (before Stor98's change is written) on 98 SE and ME guests,
  static then live: whether stock `WNASPI32.DLL` and `APIX.VXD` are
  installed; whether APIX lists Stor98's adapter and what
  `SC_HA_INQUIRY`'s identifier holds; an INQUIRY through it; if no ASPI
  field names the adapter's driver, the 9x carrier is reopened (the
  Stor98 page on 9x would then be absent, the benchmark unchanged).
- **P.1** every target: the performance counter's frequency; on 98 SE and
  ME, `ReadFile` with an `OVERLAPPED` on a disk file refused, and
  `FILE_FLAG_DELETE_ON_CLOSE`'s behaviour.
- **P.2** 98 SE, ME, 2000 and 7: whether the interleaved first touch
  scatters a buffer (Stor98's runs histogram at a size IOS passes whole),
  and the stamping's processor share on a slow guest.
- **P.3** 2000, XP, 7, 8.1 and 10: the `DEVICEMAP` value, the
  pass-through route to Stor98 on the volume handle, and that no layer
  refuses READ BUFFER.

### 14.2 Legs

| Leg | Systems | What it reads |
|---|---|---|
| P.4 | 98 SE, ME, 2000, XP, XP x64, Vista x86/x64, 7 x86/x64, 8 x86/x64, 8.1 x86/x64, 10 x86/x64 | the tool starts as a standard user with no UAC prompt; Browse...; the default folder writable (`TEMP` where the roots are not); the Quick preset on the system disk and on a USB stick (`/run ... /report /exit` over the serial route of 43.12); the file deleted; the report's header |
| P.5 | 98 SE and ME, Stor98's disk behind the emulated tunnel | the 9x study preset with 43.46's VSD absent and present, each saved, then compared: the Stor98 page shows 63 KiB pieces without it and whole requests with it on the scattered buffer, `phys_sgd` demanded and not; every verification clean |
| P.6 | 2000, 7 x64, 10 x64 with Stor98 | Standard at depth 8: the outstanding-commands histogram above 1; the Stor98 page as administrator, its refusal and button as a standard user; on 7 x64 and 10 x64 the 32-bit tool's own pass-through with the x64 image removed |
| P.7 | 98 SE and 7 x86 | the tunnel dropped during a write test: the run stops with the error, the window responds, the file deleted or its record kept and offered at the next start; Safely Remove refused while running; the tool killed during preparation on 7 (the file gone) and on 98 SE (offered at the next start) |
| P.8 | 98 SE and 7 x86 | refusals: free space, the Windows drive's reserve, the Windows directory, a junction into it (7), a SUBST drive, a CD drive, a foreign `XHCIPERF.TMP` kept, a header without a record asked about |
| P.9 | 98 SE and 7 x64 | install, update with the tool running, uninstall: the file placed, "steps left", removed |
| P.10 | the owner's hardware | 98 SE on the P14s with Stor98, before and after 43.46's VSD; Windows 10 or 7 on the same drive; the saved files compared |

### 14.3 Open for the owner

1. The Stor98 change of section 7: approve it as a Stor98 decision (to be
   recorded at the end of Stor98's `docs/PLAN.md`), or keep Stor98 as it is
   and ship the tool without a Stor98 page.
2. The 9x carrier through ASPI (section 6.2), pending P.0.
3. Threads on 98 SE and ME (P9): offered with the note, or fixed at 1.
4. The XHCITR entry (P7).
