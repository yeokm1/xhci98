NVMe driver for Windows 9x.

Base: nvme2k 1.0.0.2 https://github.com/techomancer/nvme2k
Changes: Port for Windows 9x, SCSI 64-bit LBA & Fixes

1. Generic
- added SCSI 64-bit LBA commands processing. Tested on Windows 9x ONLY
- fixed controler shutdown code (NvmeShutdownController)
- Fallback Timer is Guard Timer now 
  (FallbackTimer & HwInterrupt & NvmeSubmitCommand)
  so fixed problems with requests queueing (now it is faster and less
  CPU consuming) and handling of large requests (enormously low speed
  or errors)
- code cleared, for example debug messages now possible in NVME2K_DBG
enabled builds only

2. Windows NT specific
- Windows 2000+ DDK with target NT4 (nvme2k.h -> #define NVME2K_NT4).
The base for Windows 9x port too as Windows 9x version of drivers
requires exactly NT4 version for binary compatibility.
- procedure of save & restore state of controller (HwAdapterState) disabled 
as not needed. May be used in NT, of course, but in any case not the original
version.

3. Windows 9x specific
- port (nvme2k.h -> #define NVME2K_W9X) with 2 bug fixes. All changes
locate in nvme2k.* & utils.*
- procedure of save & restore state of controller (HwAdapterState) only
shuts down controller on restore now
- TODO: Make real save & restore state of controller if it is even possible

Search for SweetLow in code for my comments about some problems.

Builded (Free Build) by Windows 2000 DDK & VC6 with two files (SCSI.H, SRB.H)
taken from Windows NT4 DDK or Windows 98 DDK or Windows 98SE DDK.
Sources support 3 targets: 9x (NVME2K_W9X define), NT4 (NVME2K_NT4) and
2000 (no NVME2K_W9X nor NVME2K_NT4). 2000 target requires using original
SCSI.H and SRB.H from 2000 DDK of course (i.e just not using old versions).
Buildable by Windows 2003 SP1 DDK too, but needs right compiler options.
