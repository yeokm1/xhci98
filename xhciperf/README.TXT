xHCI98 Disk Benchmark (XHCIPERF.EXE, XHCIPF64.EXE)
==================================================

A disk benchmark for every system xHCI98 supports: Windows 98 SE, ME, 2000,
XP, Vista, 7, 8, 8.1 and 10. XHCIPERF.EXE is for 32-bit Windows (and starts
XHCIPF64.EXE by itself on 64-bit Windows when it is beside it); XHCIPF64.EXE
is for 64-bit Windows. SETUP.EXE copies the one for your Windows into the
install directory (C:\XHCI98 unless you chose another).

It measures any local disk through a test file of its own, XHCIPERF.TMP, in
the folder you choose, and deletes it afterwards. It never writes to a disk
in any other way. Where the bundled Stor98 driver drives the disk, it also
shows what reached the drive: the sizes of the requests Stor98 received and
of the commands it sent, how many were split, the queue depth reached, and
on Windows 98 SE and ME what Windows' disk layer handed over.

Using it
--------

  Folder        the drive or folder to test (Browse...). Network, CD and DVD
                drives are not tested, nor the Windows folder.
  Preset        a set of tests: Standard; Standard for Windows 98 and ME;
                Request-size study for Windows 98 and ME (64 KiB to 1 MiB
                with aligned, offset and scattered buffers); Sweep (a size
                range, a bar chart); Quick.
  File          the test file's size. On a FAT drive at most 1 GiB.
  Passes, Time  how often each test runs, and how long.
  Unbuffered    requests go past Windows' file cache (the default);
  Cached        through it, as a file copy does.
  Write-through each write is made durable before it completes.
  Buffer        Aligned (the default), Offset (one sector into a page, a
                misalignment test some disks refuse), Scattered (the
                buffer's pages spread over memory as far as a program can;
                the Stor98 page shows whether they were).
  Pattern       Verifiable (every block checked after it is written),
                Random, or Zeros.
  Depth         requests at once (Windows 2000 and later). On Windows 98
                and ME file I/O waits for each request, so the depth is 1
                there.
  Threads       requests from several threads at once. On Windows 98 and
                ME, the Stor98 page shows whether they overlapped.
  More...       a size sweep, the pause between tests, a warm-up, the
                seed, and the verification's choices.

Start runs the tests; Stop ends them after the requests already sent. The
results are on the Summary and Sweep pages, in MB/s (or MiB/s, View menu),
with the requests per second and the mean time per request. Edit > Copy
results copies them as text; File > Save results saves them; File >
Compare with saved results shows a saved run beside this one (the before
and after of a change you made between them, for example a driver
installed and the computer restarted).

The Stor98 page needs administrator rights on Windows 2000 and later
("Read as administrator..."); the benchmark itself does not.

Before you start
----------------

The test file takes space: the tool refuses a run that would leave less
than 64 MB free, or on the drive Windows runs from less than 512 MB or a
tenth of the drive. Other programs using the same disk during a run change
its results. Unplugging or ejecting the disk during a run stops it; the
test file is deleted then, or offered for deletion at the next start.
The tool keeps the computer from going to sleep during a run, not the
screen saver.

Command line
------------

  XHCIPERF /run [/preset standard|standard9x|study9x|sweep|quick]
           [/folder <path>] [/size <MiB>] [/passes <n>] [/time <s>]
           [/buffer aligned|offset|scattered]
           [/pattern verifiable|random|zeros] [/cached] [/writethrough]
           [/verify] [/depth <n>] [/threads <n>] [/report <file>] [/exit]

Exit codes: 0 every test passed; 1 a test failed or found a data error;
2 refused (a bad command line, or the run could not start); 3 stopped.

The tool is laid out after the familiar disk benchmarks. None of their
code, names or artwork is used, and its numbers are its own.
