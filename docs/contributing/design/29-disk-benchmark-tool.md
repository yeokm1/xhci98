# Design Record 29 - XHCIPERF, the Disk Benchmark

Status: draft 9, 2026-10-22 (roadmap-usb4 task 43.50): the owner's
decisions of that day folded in (section 13: P3, P4, P7 and P9 are the
owner's; Stor98's change recorded as its decision MD-8), and the code
written against it. Draft 8, before code,
reviewed and revised seven times the same day, converged. Draft 8: the
volume's extents read on 2000 as well. Draft 7: the blocks of a
write that did not complete marked unknown, the planning limit equal to
the allocator's. Draft 6: the write
generation taken by a saturating allocator under a critical section.
Draft 5: the write generation guarded at issue, never wrapping. Draft 4: a write
generation in every stamp and in the expected-state map, so a dropped
repeated write is found; the random generator's zero state and its
redraws bounded. Draft 3: every reopened handle
bound to the created file, a partly prepared leftover recognised, writes
in flight never overlapping in sequential tests either, the preparation
flushed before the first measurement, one byte budget for the whole
sweep, compressed and encrypted folders refused, every counter a 64-bit
pair counted at `HwStartIo`, the physical layout counted per request
before Stor98 splits it, the inherited driver name no longer accepted as
Stor98's. Draft 2 (from draft 1, `67f69523`): the counter
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
  the run's identity, the planned size, the bytes prepared so far
  (rewritten after each 64 MiB step of the preparation), the volume
  serial number and, on NT, the file index from
  `GetFileInformationByHandle` once the file exists. The record is deleted
  only after the file is.
- **Created new, never reused blindly.** The tool opens the file with
  `CREATE_NEW`. If the name exists, the tool opens it with no sharing,
  reads its first sector and compares it, its size and (on NT) its file
  index with the ownership record - the header's run identity equal to
  the record's, and the size at least the header's 1 MiB and at most the
  planned size, so a file left half prepared is recognised too: all
  matching, it is a leftover of a
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
  FILE_SHARE_WRITE` only - no `FILE_SHARE_DELETE` - and that first handle
  is held for the whole run. Because it withholds delete sharing, no other
  process can open the file for deletion or renaming while the run lasts,
  so its name cannot be moved and replaced; every other handle of the
  tool's own (the workers', the verification's) is opened with access
  for reading and writing only and sharing `FILE_SHARE_READ |
  FILE_SHARE_WRITE | FILE_SHARE_DELETE` (the last because the first handle
  holds delete access). The file disappears when the last handle closes,
  also when the tool is killed during preparation or a test. On 98 SE and
  ME whether the flag does anything is to be read (P.1); the design does
  not rely on it there, and the ownership record covers both.
- **Every reopened handle is checked** before its first write: on NT its
  volume serial number and file index (`GetFileInformationByHandle`) must
  equal the first handle's; on every system its first sector, read
  through it, must carry this run's identity. A handle that fails the
  check is closed, nothing is written through it, and the run is refused
  ("the test file was changed by another program"). On 98 SE and ME a
  file open by the tool cannot be renamed or deleted by another program
  (its sharing refuses it), which the check confirms rather than
  assumes.
- **Prepared before it is measured.** The test region is written once from
  start to end with the pattern before the first measured test (the status
  bar: "Preparing the test file"), in 64 MiB steps with the free-space rule
  checked before each (section 4.3). A read of a never-written region
  measures nothing on NT, where NTFS answers reads past a file's valid data
  length with zeros, and an extending write is synchronous there whatever
  the queue depth; preparing the whole region removes both. The
  preparation ends with `FlushFileBuffers` on every handle that wrote, and
  only then are the first counter answer (section 6.2) and the first T0
  taken, so no dirty preparation data is paid for inside a measurement or
  counted in its delta. On a cached run the file's pages may still be in
  the system cache after the preparation; the result says "cache warm
  from the preparation" for the first test, which is a different thing
  from the warm-up of section 3.6. The preparation is not timed and not
  reported, except its duration in the log.
- **Deleted after.** At the end of a run, on Stop, on a refusal after
  creation and on the window's close, the tool closes every handle (on
  NT the cleanup handle last) and, where the file still exists, deletes
  it. A deletion that fails is reported, the ownership record kept, and
  the next start offers the deletion again after the check above.
- **Size.** 16, 32, 64, 128, 256 and 512 MiB, 1, 2, 4, 8 and 16 GiB, plus
  the 1 MiB header; the default 1 GiB on NT and 256 MiB on 98 SE and ME
  (43.46's file was 256 MiB). On a FAT or FAT32 volume the largest offered
  is 1 GiB of region, so the whole file, header included, stays below
  2^31 bytes and every offset is a positive 32-bit value on the 9x file
  APIs; on NTFS any size the free-space rule allows, with 64-bit offsets.
- **Attributes.** Normal; not hidden, so a user can see and delete it by
  hand if everything else fails. Right after creation the tool reads the
  file's attributes back: a file the folder made compressed, encrypted or
  sparse (NTFS inheritance) is deleted and the run refused ("this folder
  compresses or encrypts new files, which changes what is measured;
  choose another folder"), and the result's header records the attributes
  of every run.

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
    the run's identity, the unit's index in the file, and the write's
    generation (a 32-bit count, run-wide, taken by every write request at
    its issue, 0 for the preparation, so a block written again by a
    wrapped sequential test, a later pass or a warm-up gets a new value
    each time) - and 496 bytes from the generator of section 3.5 seeded
    from those three values. Every unit
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
- **The expected-state map.** For a verifiable run the tool keeps one
  32-bit word per block of the run's smallest write size over the test
  region: the generation of the last write that completed on the block (0
  for the preparation). A write is recorded in the map when it completes
  successfully; since no two writes to a block are ever in flight
  together, the map is the block's exact expected content. A write that
  fails, is cancelled or completes short may still have changed some of
  its units (Stor98, for one, lets the commands it already issued finish
  before it reports an aborted request with no bytes moved, `engine.c`),
  so its blocks are marked unknown (`0xFFFFFFFF`, which no write ever
  takes): verification skips them, checks every other block as usual, and
  reports itself "incomplete: 3 blocks written by a request that did not
  complete were not checked". The plan
  refuses a verifiable run whose map would exceed 16 MiB, four million
  blocks ("lower the file size, or raise the smallest write size, to
  verify"), or whose byte-budgeted tests, warm-ups and passes counted
  together could issue more than `0xFFFFFFFE` writes, the allocator's
  limit below. Timed tests have no count the plan can know, so the
  generation is also guarded where it is taken: one run-wide counter
  over every test, pass and warm-up, read, checked and advanced in one
  step under a critical section (`EnterCriticalSection`, every target)
  by the pure half's allocator, which hands out 1 to `0xFFFFFFFE` and
  then stays saturated: every later call, from any worker, gets "none",
  whatever that worker saw of the stop event before. A worker that gets
  "none" issues nothing, sets the run's stop event, and the run ends as
  Stop ends it (section 4.4: no new request, every request in
  flight collected, then the verification of what completed), reported
  as "the run reached the limit of four thousand million writes for a
  verified run". The generation never wraps to 0 or to a value used
  before.
- **No overlapping writes in flight.** Before any write is issued the
  worker checks its block against that thread's requests still in flight
  (at most 32, by a scan): a random write draws its offset again, a
  sequential write that has wrapped waits for the block's request to
  complete; threads own disjoint parts of the region (section 3.5). So no two outstanding writes ever
  cover the same unit, and the completed writes alone decide each unit's
  final content.
- **Verification**, a box (default on for the 9x presets, off otherwise):
  after each write test, an untimed pass reads every block that test
  wrote (unbuffered when the test was) and checks every unit against the
  map: the stamp's signature, run, index and generation, and the body. A
  unit whose stamp carries an older generation (a dropped write, a
  repeated one included), another index (a
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
  breaks the adapter's alignment mask is refused. Aligned is the supported
  configuration; Offset is a deliberately misaligned diagnostic, which a
  disk or driver may refuse: a request refused with
  `ERROR_INVALID_PARAMETER` or `ERROR_NOT_SUPPORTED` in this mode is
  reported as "this disk does not accept misaligned buffers", the test
  skipped, and the run goes on; it is not a disk failure (P.8 reads it on
  a 512e or 4Kn disk).
- **Scattered (attempted)**: an attempt to make the buffer's pages
  physically non-adjacent. A program cannot choose physical pages; it can
  choose the order in which they are first touched. The tool reserves the
  request buffer and a second region of the same size, then commits and
  touches them one page at a time in alternation (buffer page 0, other
  page 0, buffer page 1, ...), so that a system handing out free frames in
  order gives the buffer every other frame, and frees the second region
  afterwards. Nothing in the tool can see the result, so the label never
  claims it. Stor98's answer (section 6.3 item 4) counts the physical runs
  of each data request's caller buffer as it received it, before its own
  splitting or bouncing, per request-size class; the result states one of
  four conclusions for the test: "scattered, as seen by Stor98" (in the
  test's size class, requests arrived whole and most had more than one
  run per 64 KiB), "not scattered, as seen by Stor98", "not seen:
  requests were cut or double-buffered before Stor98" (they arrived as
  pieces, or IOS marked them double-buffered, so the original layout is
  hidden), or "not known" (no Stor98). The counts are the adapter's, so
  the conclusion carries "if no other program used this disk during the
  test". If P.2 finds that the first touch does not scatter on a system,
  the choice is withdrawn there rather than kept as a label.
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
  start of the thread's part, wrapping at its end; a write that wraps onto
  a block still in flight waits for that block's request to complete
  before it is issued (section 3.3), so a part smaller than the depth
  costs depth, which the plan warns of.
- **Random**: requests of the test's size at block-aligned offsets in the
  thread's part, chosen uniformly, so a run can be repeated; writes avoid
  blocks in flight (section 3.3).
- **The generator** (offsets, and the patterns' bodies): Marsaglia's
  32-bit xorshift (shifts 13, 17, 5) on a nonzero state. The run's seed
  is in the result ("seed 0x1A2B3C4D"), the user may set it, and a seed of
  0 is replaced by `0x9E3779B9`; a test's seed is the run's seed mixed
  (FNV-1a) with what the test is - its label, operation, buffer and which
  test of those it is - not its place in the plan, so one test draws the
  same offsets whatever else the run holds and a saved comparison
  compares like with like; thread n's state is that seed XOR
  `(n + 1) * 0x9E3779B9`, and the pattern's per-unit state the three stamp
  values mixed the same way, each replaced by `0x9E3779B9` whenever it
  comes out 0. An offset is the state modulo the part's block count (the
  small bias of a modulus below 2^32 is accepted and stated). A write
  that draws a block in flight draws again at most 8 times; then the
  worker waits for its oldest outstanding request to complete (collecting
  it, and checking Stop) and draws again, so a worker never spins without
  collecting completions.
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
  sizes over the same bytes: one budget for the whole sweep, the total
  rounded down to a multiple of the largest size times the depth times the
  threads, so every size moves exactly that many bytes in whole requests
  and every thread an equal share; a total below that unit is refused by
  the plan ("raise the total per size to at least 32 MB"). A "Time per
  size" choice makes the sweep time-based instead, with each size's
  actual bytes shown.
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
verifiable run whose map exceeds 16 MiB;
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
  `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` on the volume (2000 and later;
  on 2000 for removable media from SP4, the target's service pack), and
  more than one extent gives a generic run with no Stor98 page ("this
  volume spans several disks"); a query that fails gives "not known" and
  no Stor98 page.
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
  fits, the volume's total free bytes (not limited by a quota) decide the
  reserve; on a system without it, `GetDiskFreeSpaceA` with its 2 GiB cap
  gives both.
- A run needs the file's size and must leave a reserve: 64 MiB on a volume
  that is not the Windows volume or the swap file's; on those, the larger
  of 512 MiB and 10% of the volume's size as `GetDiskFreeSpaceExA` reports
  it to this user (which a disk quota can make smaller than the volume;
  the reserve is then smaller too, and never below 512 MiB), because a
  full Windows drive stops 98
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
  text is the saved file (`.TXT`, `GetSaveFileNameA`). When no test
  carries a Stor98 page, the text has one line in its place, `Stor98: `
  and the sentence the Stor98 page gives for what the run's search found
  (not Stor98's disk, administrator rights needed, no ASPI layer, the
  adapter not told apart with its two counts, no answer in time), or
  that Stor98 drives the disk but no test's counters were taken; so a
  report read away from the window still says why it has no counters.
  The state is the one the search found as the run started, changed only
  by the run's own counter reads.
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
[/passes <n>] [/time <s>] [/buffer aligned|offset|scattered] [/min <KiB>]
[/max <KiB>]
[/pattern verifiable|random|zeros] [/cached] [/writethrough] [/verify]
[/depth <n>] [/threads <n>] [/report <file>] [/exit]` starts the run at
once with those parameters over the defaults, writes the report if asked
and exits with 0 (every test passed), 1 (a test failed or a data error),
2 (refused) or 3 (stopped). A run that ends before its plan does gives
the reason in its report's header (`Stopped:`), and a run refused before
it begins still writes a report of three lines with the folder and the
refusal (`Refused:`), so a batch file's report always says what
happened; a stop by the user or by closing the window says so too. A
report is never written to a file named `XHCIPERF.TMP`, the test file's
name, which may be a kept file of another program's. The window shows the run; `/exit` closes it at
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
  `stor98`. The inherited name `nvme2k` is never accepted: it is also the
  upstream driver's, so it proves nothing (a laboratory image whose
  devnode names `NVME2K.MPD`, as 43.46's does, is changed to name
  `stor98.mpd` for the legs). Whether stock 98 SE and ME install
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
one of three kinds, written in the header: a **total** (a counter that
only rises, kept as a 32-bit low and high pair that the driver carries by
hand with no 64-bit arithmetic, so it cannot wrap in the life of a
machine and the tool subtracts two answers exactly), a **gauge or limit**
(shown as read, never subtracted) and a **fact**. Nothing resets; an
**epoch** (a value that changes at every adapter start) tells the tool
when two answers cannot be subtracted.

1. **Identity** (facts): signature `S98P`, the interface version, the
   answer's length, Stor98's file version, the build (9x or NT, x86 or
   amd64), the adapter's PCI bus, device and function, the namespace's
   logical block size and its size in blocks, the epoch.
2. **Requests received** (the SRBs; totals): read and write data SRBs
   (the six READ and WRITE CDBs), each counted once where the port hands
   it to the driver - at `HwStartIo`'s entry (`nvme2k.c`), before
   `EngStartIo` decides to dispatch, hold or refuse it - whatever happens
   to it after, so a held SRB later replayed through `ScsiDispatch` is
   not counted again and one that fails while held is still counted once;
   the engine's own admission counters (`ParentsAdmitted` and the rest)
   keep their meaning unchanged. Their sectors per direction; a size
   histogram per direction in 13 buckets
   (below 4 KiB, 4 KiB, to 8, to 16, to 32, below 63 KiB, exactly 63 KiB =
   64,512 bytes - IOS's piece -, to 64 KiB, to 128, to 256, to 512 KiB, to
   1 MiB, above).
3. **Commands sent** (the NVMe commands, "children"; totals): the same
   histogram per direction; parents split into more than one child;
   children bounced through Stor98's own buffer; read-modify-writes on a
   4,096-byte namespace; flushes; and an outstanding-commands histogram
   taken at each child's issue (1, 2, 3-4, 5-8, 9-16, 17-32, above), which
   shows the depth that reached the drive and whether two 9x threads ever
   overlapped. `ChildrenIo` as a gauge.
4. **Physical layout** (totals): for each data SRB admitted, the physical
   runs of the caller's buffer as the port describes it to Stor98, counted
   over the whole SRB before Stor98 cuts it into children or bounces any
   (the pages are walked once, as the children's mapping walks them
   already: runs are counted where two consecutive pages are not
   physically adjacent, across child boundaries, and an SRB of which a
   page could not be resolved is counted as "layout not seen"), in a
   histogram per request-size class: the classes 64 KiB, 128 KiB, 256 KiB,
   512 KiB and 1 MiB of item 2's buckets, each with the buckets 1, 2, 3-4,
   5-8, 9-16, 17, 18-32, 33-64, above, and "not seen". It describes the
   requests as they reached Stor98, after any cut or double buffer above
   it (section 3.4).
5. **Errors** (totals): data SRBs completed with an error, fast fails,
   busy returns, resets, gone episodes, gone deadlines taken, stale and
   late completions (section 6.1's counters, carried as they are).
6. **Limits and modes** (facts): `MaximumTransferLength` and
   `NumberOfPhysicalBreaks` as the port granted them, the controller's
   MDTS in bytes, the largest child Stor98 permits itself
   (`MaxChildBytes`, computed from those limits at bring-up, `lifecycle.c`;
   a limit, not an observed size), the children allowed at once
   (`ChildLimitIo`), the
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
KiB requests and one of 16 KiB at (2) and (3); the splits Stor98 made itself (3); the depth
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
  low and high `ULONG` pair incremented with a hand carry, at one named
  update site per field - data SRBs received at `HwStartIo`'s entry, the
  physical layout where `EngSubmitReadWrite` maps an admitted SRB,
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
  exactly, a total's hand carry), a held SRB counted once whether it is
  replayed or fails while held, the physical layout across child
  boundaries and with a page not resolved, and the 9x DCB accessor on a linear and a scatter/gather
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
under Windows' own `stornvme` on 8.1 and 10 are each a generic disk.

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
versions, the length), two answers subtracted (64-bit pairs, a
changed epoch refused, gauges and limits not subtracted), the scattered conclusion
of section 3.4, and the panel's text.

`test\test_pf.c` covers each with golden vectors: a plan per preset per
system family (9x refusing depth 8 from a command line, NT accepting it),
4,096-byte logical and physical sectors, a region too small, buffers and
maps over the cap; the pattern's unit for given seeds and its check
against the map finding a dropped write (an older generation's stamp,
also when the same block was written earlier in the same test or pass), a
misplaced write, another run's unit, a changed body, an untouched region
and a block written twice; the generation allocator from `0xFFFFFFF0`, saturating at
`0xFFFFFFFE`, with two workers forced to interleave at exhaustion, one of
them already past its stop check; plans of exactly `0xFFFFFFFE` and
`0xFFFFFFFF` budgeted writes (accepted, refused); a cancelled overwrite
whose first child completed, its blocks unknown and the rest checked; the in-flight exclusion at depth 32, the bounded redraw
and a part of one block; a zero seed and a derived zero state; the
arithmetic at its bounds (frequencies of 1,193,182 Hz, 3,579,545 Hz and
near 2^32, a 2-hour interval, a zero interval) and each rounding edge; a
backwards and an implausible timer reading; percentiles of a histogram;
the reports at their longest; a saved file read back, with a bad line, a
missing test, and a workload key differing in each field. `test\test_pfs98.c`
covers the answer as the tool reads it: truncated, a longer answer of
version 1, a wrong signature, a low word's carry between two answers, a changed epoch, the 63 KiB bucket, and the panel's sentences for
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
- **P3** (owner, 2026-10-22) The names `XHCIPERF.EXE` and `XHCIPF64.EXE` (8.3 names, as
  `XHCITR64.EXE`, `XHCIWZ64.EXE`), the title "xHCI98 Disk Benchmark".
- **P4** (owner, 2026-10-22, as Stor98 decision MD-8 in its
  `docs/PLAN.md`, the 9x ASPI carrier pending P.0) One carrier for Stor98's counters on every system, READ BUFFER
  buffer ID `53h`, through pass-through on NT and ASPI on 98 SE and ME,
  sent only to an adapter first established as Stor98's (section 6.2);
  the answer read-only, typed fields, no reset.
- **P5** The Stor98 page needs administrator rights on NT and asks for
  them by its own button; the benchmark never does.
- **P6** Queue depth by overlapped I/O on NT only; fixed at 1 on 98 SE
  and ME, said in the window and the results.
- **P7** (owner, 2026-10-22) XHCITR gains "Disk benchmark..." (section 11).
- **P8** Default presets per family (section 3.7): Standard on NT, the 9x
  standard on 98 SE and ME; 1 GiB and 256 MiB files.
- **P9** (owner, 2026-10-22) Threads offered on 98 SE and ME (up to 8), with the note that the
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
| P.8 | 98 SE and 7 x86 | refusals: free space, the Windows drive's reserve, the Windows directory, a junction into it (7), a SUBST drive, a CD drive, a foreign `XHCIPERF.TMP` kept, a header without a record asked about, a half-prepared leftover recognised, a compressing folder (7); the Offset mode on a 512e or 4Kn disk (7) |
| P.9 | 98 SE and 7 x64 | install, update with the tool running, uninstall: the file placed, "steps left", removed |
| P.10 | the owner's hardware | 98 SE on the P14s with Stor98, before and after 43.46's VSD; Windows 10 or 7 on the same drive; the saved files compared |

**Narrowed (the coordinator's overnight decision, 2026-10-23, pending the
owner's review).** 98 SE: every leg of the table but P.6 and P.10. ME: the
9x set again (P.1, P.2, P.5 with and without S98VSD.VXD, P.7, P.8). NT:
P.3, P.4 and P.6 on 2000, XP, 7 and 10, x86 (10 x64 only where cheap); on
8 and 8.1 one P.4 smoke run (Quick) each. The other cells of P.4, P.7 to
P.9 on 7, and XP x64, Vista and 8 x64 stay owed.

**On the owner's real-check list.** On real 98 SE hardware, whether the
Quick preset's 2 s timed steps on the IDE system disk end near 2 s. On the
emulator two of them ran 28 s (the 1 MiB read and the 4 KiB random write,
on ESDI_506 under TCG). The tick count was first suspected and is not the
cause: a probe timing 64 MiB of unbuffered 1 MiB reads and writes on that
disk read the tick count and the performance counter within 0.1 % of each
other (42.3 s and 224.6 s), and the step's figures agreed with both (a
result whose two clocks disagree takes the tick count, section 3.6). Why
the deadline did not end those steps is not yet read.

### 14.3 Decided by the owner (2026-10-22)

Every question draft 8 left for the owner is answered: the Stor98 change
of section 7 is approved as Stor98's decision MD-8, the 9x ASPI carrier
included pending P.0; threads are offered on 98 SE and ME with the note;
the names, the title and the XHCITR entry stand as section 13 says.

## 15. As built (2026-10-22)

The code of task 43.50 on branch `p43-50`, written against draft 9. What
it built differently from the sections above, or settled where they left
a choice:

- **P.0, read statically.** Stock 98 SE and ME install `WNASPI32.DLL` and
  `APIX.VXD` (their Setup's `subase.inf`, `[winbase]`), but `APIX.VXD`
  carries the fixed string "OEM SCSI HBA", so the ASPI adapter identity of
  section 6.2 is not shown to name the miniport (`legal-provenance.md`
  section 4, the row for this reading). The adapter is therefore proved
  Stor98's by the target itself: Stor98 lists a vendor INQUIRY page,
  `C9h`, in page `00h` and answers it with its signature, version and PCI
  location (`stor98pf.h`, "The identity page"; Stor98 MD-8's P.0 note).
  The 9x reader sends READ BUFFER only to a target that answered page
  `C9h`, and matches the disk to it by the present `Enum\SCSI` devnode
  holding the drive letter (`CurrentDriveLetterAssignment`; section 15's
  bench note) whose device id contains
  the target's INQUIRY vendor and product; two matches give "could not be
  told apart". INQUIRY and its pages are what the system itself sends
  every target, so asking them is safe. Live on P.5.
- **Stor98** (`cf223a2f`): as section 7, with the totals counted where
  section 6.3 says - data SRBs at `HwStartIo`'s entry (`EngPfSrbIn`), the
  physical runs of each request from the spans `EngMapCaller` already
  translates (no extra walk; a bounced child marks the request "not
  seen"), commands at issue, errors and run classes where the parent
  finishes - the 9x DCB facts through `Sg9xDcbFacts`, the epoch from the
  time-stamp counter at a `HwFindAdapter` that is not a resume, and the
  answer built into the device extension (`PfAnswer`) in `HwStartIo`.
  Host tests `pfstat_ut`, `sg9x_ut` and `inqid_ut`; Stor98's own build of
  every target; the amd64 warning count unchanged.
- **The pure half** (`30e5e0ed`): the latency histogram's 64 buckets
  each four thirds of the one before, from 1 us; the FAT limit 1 GiB of
  region (section 3.1); exit code 2 for a bad command line as for a
  refused run.
- **The window**: Depth and Threads offer "Preset" (each test's own) or
  one value for every test; the custom test list of section 3.7 and named
  presets are not built - File > Keep these settings keeps one set for
  the next start; the preparation shows its progress in the status bar and
  there is no progress bar; the `DBT_DEVICEQUERYREMOVE` notice of section
  4.4 is not built (the open file still makes Windows refuse the
  removal); `CancelIo` from the issuing worker is the only cancel used.
- **XHCITR**: Action > Disk benchmark... starts the benchmark beside it
  (the install directory) with `/folder` and the selected disk's first
  drive letter (`xhcitr\xhcitr.c` `bench`).
- **The installer**: the benchmark of the machine's architecture is one
  of the installer's own files (`own_names`, `inst_eng.c`), copied from
  `tools\xhciperf\` by their route and removed by the uninstall with them;
  the Components page's text is unchanged.
- **Packaging**: `make-release.ps1` stages `tools\xhciperf\` (`-PerfToolDir`,
  `-SkipPerfTool`) with the freshness, shared-version and file-version
  checks, a readme and a `NOTICE.TXT`; `test-package.ps1`'s tools list and
  `build-driver.cmd`'s rebuild and self-test follow.
- **Host-test coverage**: section 9's vectors cover what `pf_core.c` and
  `pf_s98.c` decide (the pattern and its check, the generation allocator,
  the plan, the statistics, the saved file). The in-flight exclusion, the
  bounded redraw and the marking of a write that did not complete are in
  `xhciperf.c` (`pick`, `complete_one`, `map_mark`) and are read on the legs,
  not by host vectors.
- **Verification**: a verified run that writes nothing checks the whole
  file against the preparation at its end; "Verify the whole file" includes
  the preparation's blocks; a verification that cannot read the file fails
  the step, and a whole-file verification that fails makes the run's exit 1.
  The window's workload controls are disabled while a run lasts. On 98 SE and
  ME the ASPI search runs on a thread of its own, waited for at most 5 s;
  its findings are published only by a caller still waiting, and once an
  ASPI request has not come back, or a search has not returned, ASPI is not
  asked again in that session.
- **Exit status**: a failed or data-error pass, a warm-up's included, and a
  `/report` that cannot be written make the exit 1. A worker thread that
  cannot be started fails its step rather than running with fewer threads.
- **The test file's end**: only a file the run created is deleted; on NT by
  the anchor's `FILE_FLAG_DELETE_ON_CLOSE` alone, on 98 SE and ME by name
  after an exclusive open shows the run's header.
- **Stor98 and the excluded flush**: with "Include the flush" off, the
  after-read of Stor98's counters is taken as the timed window ends, before
  the flush, so the counters cover what was timed.
- **The results keep their workload**: the report, the comparison key and
  the chart use the run's copy of the settings and of the disk's facts;
  only Show and the unit follow the window afterwards.
- **98 SE and ME attribution**: the adapter is proved Stor98's by its
  identity page (section 6.1); the disk is matched to the drive letter by
  its model, and that model must be the only one among the disks under
  `Enum\SCSI`, else the counters are not attributed. A SUBST drive is
  detected only when it names the Windows drive (a letter other than the
  Windows drive's with its volume serial number); section 4.1's wider
  configuration-manager check is not built.
- **Paths**: a full or long path name that does not fit, or leaves no room
  for `\XHCIPERF.TMP`, is refused.
- **The header first**: the ownership header is written right after
  `CREATE_NEW`, before the attribute and free-space checks, so a run that
  ends at any later point leaves a file the cleanup and the next start
  recognise; on 98 SE and ME a file that is not recognisable is kept with
  its record.
- **Timing**: on SMP before Vista the controller, which takes T0 and T1,
  stays on the first processor. A counter reading that runs back marks the
  result's timer unreliable, and such a result shows no latency and no
  stamping share. Stor98's before-read is taken after every handle has
  been opened and checked, as the workers are released.
- **The Stor98 page**: a counter read takes the target as it was when asked
  and changes the state only when it returns in time; the report carries
  every step's page.
- **The scattered conclusion** counts a request as scattered when its runs
  exceed one per 64 KiB of the test's size, and double-buffering before
  Stor98 always reads as "not seen".
- **Comparison**: loaded saved results are checked again against each new
  run's workload and dropped from the display when they differ.
- **A stopped verification** is never reported clean: a step's becomes
  Stopped, the whole file's says it is not known to be clean.
  One that skipped blocks a refused or unfinished request left unknown
  is not either: the whole file's then reads "no error found, but not
  complete" with the count (`XPRF-VERIFY 1 5`).
- **The plan is made aside**: a refused start leaves the shown results and
  their plan as they were.
- **Medians**: with Show set to Median, IOPS is the median pass's too.
- **The memory limit** counts the buffers as allocated: the bank of
  depth x threads + 1 page-rounded buffers, two more banks for the
  scattered layout (its own and the one it is made with), and the
  verification buffer.
- **The Stor98 page of a test** is its latest pass's and names that pass;
  a pass whose counters were not read says so instead of keeping an
  earlier page. Its scattered conclusion needs the whole, seen requests
  to outnumber those cut into 63 KiB pieces or not seen.
- **The millisecond timer** (no performance counter) takes its differences
  as DWORDs, across `GetTickCount`'s rollover.
- **The estimate** counts the pause before every pass, a warm-up's
  included. A test with a failed, stopped or refused pass shows that beside
  its rate. A start whose controller thread cannot be made is refused (exit
  2).
- **Pass status, the key, the end on 9x**: a stopped or refused pass stays the test's status
  beside the other passes' rate. The comparison key carries a byte-budgeted
  sweep's bytes per step (`budget`), so two largest sizes over one total
  do not compare. On 98 SE and ME the process does not end while an ASPI
  request is not given back: after its window closes it waits for it, so
  that ASPI never writes freed memory (a hung ASPI keeps it waiting).
- **`/min` and `/max`** (KiB, powers of two, 1 to 8192) set a sweep's
  smallest and largest sizes on the command line and make the run a sweep;
  added for 43.46's bench (the 9x study from 128 KiB to 2 MiB, with
  Stor98's 2 MiB maximum).
- **The 98 SE smoke runs (2026-10-22, emulator, kit `E:\xhci98-vm\p43-50`)**:
  the generic benchmark ran on C: (IDE) and on the NVMe drive Stor98
  drives, verified, and saved its reports. They changed two things. A
  timed step's deadline is also checked by the workers: a 2 s random
  write had run 22 s while synchronous IDE requests kept the controller
  from running. The 9x attribution (section 6.2) found `Enum\SCSI`
  holding the disk under two ids (an earlier driver's spelling) and an
  instance name whose PCI part (`08280000`) is not Stor98's bus and slot
  (the identity page said bus 1, device 1); it now counts instances in
  tiers - the id as the INQUIRY vendor and product spell it on the
  adapter's location, the id as spelt, the model - the first that finds
  any deciding, and only those the configuration manager finds present
  where it answers. With that the Stor98 page read Stor98's counters
  through ASPI on 98 SE. An ambiguity names its counts on the page.
  The counted instances must include the one holding the drive letter
  (Codex round 9: a second disk of the same id on another adapter,
  excluded by the first tier, would otherwise have been given Stor98's
  counters); otherwise the page says the adapter could not be told apart.
- **The owner's 98 SE bench (2026-10-22, c36c4752)** saved a report with
  no Stor98 page and nothing to say why: the search had not ended in
  Stor98's disk, and only the window's Stor98 tab named the state. The
  report now carries the `Stor98:` line of section 5.6 (host vectors for
  every state in `test/test_pfs98.c`). The cause, from the owner's
  `Enum\SCSI` export: five instances claimed `D` (USB sticks and a UAS
  disk seen before, each keeping the letter it last had), listed before
  the Stor98 disk in the order 9x keeps keys, and the search took the
  first claimant, a stale USB stick's, whose id names no Stor98 target:
  "not reported by this disk's driver". Reproduced on the 98 SE
  emulator (a stale claimant of `F` added before the Stor98 disk's key):
  c36c4752's tab gave that sentence and its report no page. The letter
  now goes to the one claimant the configuration manager finds present
  (`CM_Locate_DevNodeA` on `SCSI\<id>\<instance>`, which answers a
  32-bit program on 98 SE: with six claimants of `F` the same run read
  Stor98's counters); where it finds no SCSI instance present at all,
  the letter's only claimant, and with more than one the page and the
  report say the registry gives the letter to that many disks.
- **The buffer's physical order on 98 SE (2026-10-23, emulator)**: a
  second run in one boot had every aligned request double-buffered by IOS
  and cut into 63 KiB pieces. The cause is the physical order of the
  buffer's pages, not the tool: a probe that allocated a 128 KiB buffer
  400 times at the same linear address, wrote 16 unbuffered requests from
  it and freed it saw them alternate exactly, whole in one round and
  double-buffered in the next, as the page allocator handed the same
  pages back in reversed order (32 physical runs, over the DCB's 17
  scatter/gather elements). After a fresh start the free list is mostly
  ascending, so a first run's aligned buffer usually arrives whole; after
  other programs have freed memory it often does not. With S98VSD.VXD
  (43.46) the demand for physical scatter/gather is gone and every
  request arrived whole in both orders. The 9x facts now say so when the
  demand stands, and the release notes carry it as a known limitation.
- **ME, P.5 (2026-10-23, emulator, kit `E:\xhci98-vm\p43-50`, images `m2`
  and `m3` from `E:\stor98-vm\int\me\base.qcow2`, this branch's Stor98 as
  `stor98.mpd`)**: as on 98 SE. Without S98VSD.VXD the first study run
  after the start had aligned and offset requests whole and scattered ones
  double-buffered; the probe re-allocating its buffer then saw every round
  double-buffered, and the second run every test. With S98VSD.VXD (the
  layer claimed the Stor98 DCB) the first run had every test whole,
  scattered included, and the probe every round whole. Open: the second
  run with the layer found no Stor98 disk ("not reported by this disk's
  driver") and so carries no counters; 98 SE did not show it, and it is
  not yet read why (the attribution on ME after a program left a
  `PROBE.TMP` behind on the drive is the first thing to look at). Its
  scattered writes ran at 6 MB/s against 15 to 38 MB/s for the others.
- **Not shipped (owner, 2026-10-23).** XHCIPERF is dropped from the `3.2.0.0`
  package, its installer copy, its uninstall and XHCITR's Action menu; it
  stays in the tree as a developer diagnostic, built and host-tested at every
  gated build, and Stor98's counter answer (MD-8) stays in the driver. What
  is still open is roadmap-usb4's PERF.1.
- **Owed**: the legs of section 14 (P.0 live, P.1 to P.10), all of them on
  virtual machines or the owner's hardware; until then its readings are
  the 98 SE smoke runs and the owner's 98 SE bench above, not the
  acceptance matrix.
