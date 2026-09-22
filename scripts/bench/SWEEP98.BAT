@echo off
rem SWEEP98.BAT - one boot of a moderation sweep on Windows 98 SE (roadmap
rem tasks 23.2 and 23.3.5): a timed XHCISNAP dump either side of a benchmark
rem run.  Run it from the
rem folder holding XHCISNAP.EXE, on the bench machine.
rem
rem   SWEEP98 ARM A     before the benchmark: starts C:\Iarm.LOG, dump C:\IarmA.*
rem   SWEEP98 ARM B     after the benchmark:  appends to the log, dump C:\IarmB.*
rem
rem   ARM is a label for this boot, four characters at most so every name stays
rem   8.3: CTL1 4000 1000 200 160 40 10 CTL2 is the ladder it was written for.
rem
rem WHY TWO CALLS AND NOT ONE WITH A PAUSE.  The first version took dump A,
rem waited on PAUSE while ATTO Disk Benchmark ran in the GUI, and took dump B.
rem On the P14s Gen 1 the PAUSE never returned once ATTO had run, with the DOS
rem box focused and a key pressed.  Two calls cost one more line typed per boot
rem and cannot hang.  (runs/run-23.md, task 23.2.)
rem
rem WHAT THE DUMPS ARE FOR.  isr.entries and dpc.count are NOT in the .TXT.
rem They are counter-block fields, and the counter block reaches the note ring
rem only on a DebugView flush, so with XHCISNAP alone they exist only in the
rem .BIN extension image.  Bring the .BIN back and decode it with
rem scripts\local\readsnap.py against an offsets table from the same tree;
rem build-and-test.md, the 23.2 staging section, has the recipe.  The .TXT
rem carries the interval itself since 1.1.1.0 - the XhciImodInterval250ns
rem lines under registry values - which is enough to check on the machine
rem that a value took.  (Task 23.2 ran this on its experimental build, whose
rem interval fields were in the .BIN only.)
rem
rem The times in the log come from TIME fed an empty line, so the gap between
rem the two dumps is the benchmark plus whatever the operator did around it.
rem Use the interrupt count per run, not per second, as the comparable figure.
rem
rem No angle brackets or pipes anywhere in a REM or ECHO below except the one
rem pipe that feeds TIME: COMMAND.COM performs redirection even on a REM line.

if "%1"=="" goto usage
if not exist XHCISNAP.EXE goto nosnap
if "%2"=="A" goto dumpa
if "%2"=="a" goto dumpa
if "%2"=="B" goto dumpb
if "%2"=="b" goto dumpb
goto usage

:dumpa
echo SWEEP98 arm %1 > C:\I%1.LOG
echo === dump A >> C:\I%1.LOG
echo.|time >> C:\I%1.LOG
XHCISNAP -dump -o C:\I%1A
if errorlevel 1 echo XHCISNAP A returned an error >> C:\I%1.LOG
echo.
echo Dump A taken.  Now run the benchmark, then type:  SWEEP98 %1 B
goto end

:dumpb
if not exist C:\I%1.LOG goto noa
echo === dump B >> C:\I%1.LOG
echo.|time >> C:\I%1.LOG
XHCISNAP -dump -o C:\I%1B
if errorlevel 1 echo XHCISNAP B returned an error >> C:\I%1.LOG
echo.
echo Done.  Log: C:\I%1.LOG   Dumps: C:\I%1A.* and C:\I%1B.*
echo Save the benchmark result, then set the next value and reboot.
goto end

:noa
echo ERROR: C:\I%1.LOG not found - run  SWEEP98 %1 A  before the benchmark.
goto end

:nosnap
echo ERROR: XHCISNAP.EXE is not in this folder.  cd to the kit first.
goto end

:usage
echo Usage:  SWEEP98 ARM A     before the benchmark
echo         SWEEP98 ARM B     after the benchmark
echo   ARM  a label of four characters at most, for example CTL1 4000 200
:end
