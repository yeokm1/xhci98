@echo off
rem SPDX-License-Identifier: BSD-3-Clause
rem Copyright (c) 2026, Yeo Kheng Meng
rem
rem build.cmd - build and run the host unit tests (TESTING "SRB-level test
rem client", route 1) with MSVC 6.0 and the Windows 2000 DDK headers from
rem the git-ignored tools\ folder, once against the NT SRB.H and once against
rem the Windows 98 DDK's (NVME2K_W9X). Output in out\unit\. Exit code 0 when
rem every test passed.

setlocal
for %%I in ("%~dp0..\..") do set "REPO=%%~fI"
set "MSVC=%REPO%\tools\MSVC600"
set "PATH=%MSVC%\VC98\Bin;%MSVC%\Common\MSDev98\Bin;%PATH%"
set "LIB=%MSVC%\VC98\Lib"
set "DDKINC=%REPO%\tools\ntddk\inc"
set "W98INC=%REPO%\tools\w98ddk\inc"
set "OUT=%REPO%\out\unit"
if not exist "%OUT%" mkdir "%OUT%"
set "FAILED="

rem NT: miniport.h and srb.h from the DDK
set "INCLUDE=%REPO%\src;%DDKINC%\ddk;%DDKINC%;%MSVC%\VC98\Include"
cl /nologo /W3 /O2 /D_X86_=1 /D_WIN32_WINNT=0x0500 /Fo"%OUT%\sense_nt.obj" /Fe"%OUT%\sense_ut_nt.exe" "%~dp0sense_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\sense_ut_nt.exe" || set "FAILED=1"
set "NTFAILED=%FAILED%"
set "FAILED="

rem 9x: SCSI.H and SRB.H from the Windows 98 DDK first, as the driver build
set "INCLUDE=%REPO%\src;%W98INC%;%DDKINC%\ddk;%DDKINC%;%MSVC%\VC98\Include"
cl /nologo /W3 /O2 /D_X86_=1 /DNVME2K_W9X /Fo"%OUT%\sense_9x.obj" /Fe"%OUT%\sense_ut_9x.exe" "%~dp0sense_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\sense_ut_9x.exe" || set "FAILED=1"
if defined NTFAILED set "FAILED=1"

rem INQUIRY identity (src/inqid.c), the same on every target
set "INQFAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%DDKINC%\ddk;%DDKINC%;%MSVC%\VC98\Include"
cl /nologo /W3 /O2 /D_X86_=1 /D_WIN32_WINNT=0x0500 /Fo"%OUT%\inqid.obj" /Fe"%OUT%\inqid_ut.exe" "%~dp0inqid_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\inqid_ut.exe" || set "FAILED=1"
if defined INQFAILED set "FAILED=1"

rem The 9x pool's lock attempt and stall budget (src/p9lock.h), mocked lock
set "P9FAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%DDKINC%\ddk;%DDKINC%;%MSVC%\VC98\Include"
cl /nologo /W3 /O2 /D_X86_=1 /D_WIN32_WINNT=0x0500 /Fo"%OUT%\p9lock.obj" /Fe"%OUT%\p9lock_ut.exe" "%~dp0p9lock_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\p9lock_ut.exe" || set "FAILED=1"
if defined P9FAILED set "FAILED=1"

rem The NT CM link-state receiver (src/cmlink.h), tables built by the test
set "CMLFAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%DDKINC%\ddk;%DDKINC%;%MSVC%\VC98\Include"
cl /nologo /W3 /WX /O2 /D_X86_=1 /D_WIN32_WINNT=0x0500 /Fo"%OUT%\cmlink.obj" /Fe"%OUT%\cmlink_ut.exe" "%~dp0cmlink_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\cmlink_ut.exe" || set "FAILED=1"
if defined CMLFAILED set "FAILED=1"

rem The 9x entry binding and its late lookup (src/p9bind.h), tables built by the test
set "P9BFAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%DDKINC%\ddk;%DDKINC%;%MSVC%\VC98\Include"
cl /nologo /W3 /WX /O2 /D_X86_=1 /D_WIN32_WINNT=0x0500 /Fo"%OUT%\p9bind.obj" /Fe"%OUT%\p9bind_ut.exe" "%~dp0p9bind_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\p9bind_ut.exe" || set "FAILED=1"
if defined P9BFAILED set "FAILED=1"

rem The polling mode's settings and decision (src/polltun.h) and the
rem report-only PCI capability walk (src/pcicap.h), no DDK header needed
set "POLLFAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%MSVC%\VC98\Include"
cl /nologo /W3 /WX /O2 /Fo"%OUT%\poll.obj" /Fe"%OUT%\poll_ut.exe" "%~dp0poll_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\poll_ut.exe" || set "FAILED=1"
cl /nologo /W3 /WX /O2 /DNVME2K_W9X /Fo"%OUT%\poll9x.obj" /Fe"%OUT%\poll_ut_9x.exe" "%~dp0poll_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\poll_ut_9x.exe" || set "FAILED=1"
if defined POLLFAILED set "FAILED=1"

rem The maximum transfer size setting (src/xfertun.h), no DDK header needed
set "XFERFAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%MSVC%\VC98\Include"
cl /nologo /W3 /WX /O2 /Fo"%OUT%\xfer.obj" /Fe"%OUT%\xfer_ut.exe" "%~dp0xfer_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\xfer_ut.exe" || set "FAILED=1"
if defined XFERFAILED set "FAILED=1"

rem The namespace format decode (src/nsfmt.h), no DDK header needed
set "NSFFAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%MSVC%\VC98\Include"
cl /nologo /W3 /WX /O2 /Fo"%OUT%\nsfmt.obj" /Fe"%OUT%\nsfmt_ut.exe" "%~dp0nsfmt_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\nsfmt_ut.exe" || set "FAILED=1"
if defined NSFFAILED set "FAILED=1"

rem The OS-clock cross-check under short timer periods (src/clock.c), 9x
rem headers (the VMM clock), simulated TSC, OS clock and stall
set "CLKFAILED=%FAILED%"
set "FAILED="
set "INCLUDE=%REPO%\src;%W98INC%;%DDKINC%\ddk;%DDKINC%;%MSVC%\VC98\Include"
cl /nologo /W3 /O2 /D_X86_=1 /DNVME2K_W9X /DCLOCK_HOST_TEST /Fo"%OUT%\clock.obj" /Fe"%OUT%\clock_ut.exe" "%~dp0clock_ut.c" || set "FAILED=1"
if not defined FAILED "%OUT%\clock_ut.exe" || set "FAILED=1"
if defined CLKFAILED set "FAILED=1"

if defined FAILED (
    echo *** UNIT TESTS FAILED ***
    exit /b 1
)
echo All unit tests passed.
exit /b 0
