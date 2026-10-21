@echo off
rem LEG9X.BAT - the standard leg on Windows 98 SE and ME, run by XHPOLL.BAT
rem from the leg's CD as XHLEG.BAT, %1 the CD's drive letter (build-and-test.md,
rem "Faster emulator legs"). One phase per Windows start, chosen by the marker
rem files in C:\XHLEG; each phase ends by switching the machine off, and the
rem host starts it again cold. The host reads COM1 and gives the verdict.
rem XHMODE.BAT on the CD sets XHMODE: console (INSTALL /console /yes) or
rem window (the operator runs the installer's window, then the same checks).
set XC=%1
call %XC%:\XHMODE.BAT
if exist C:\XHLEG\PH4.ON goto again
if exist C:\XHLEG\PH3.ON goto ph3
if exist C:\XHLEG\PH2.ON goto ph2

echo x>C:\XHLEG\PH2.ON
echo XHLEG PHASE 1 install %XHMODE%>COM1
ver>C:\XHLEG\VER.TXT
call %XC%:\XHSEND.BAT ver C:\XHLEG\VER.TXT
if "%XHMODE%"=="window" goto operator
%XC%:
cd \PKG
call INSTALL.BAT /console /yes /nopause /log C:\XHLEG\INSTALL.LOG
call %XC%:\XHEXIT.BAT install
C:
cd \
call %XC%:\XHSEND.BAT install C:\XHLEG\INSTALL.LOG
goto off
:operator
echo XHLEG OPERATOR install with the window, then shut down>COM1
goto end

:ph2
if exist C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS goto ph2go
echo XHLEG CHECK driver-file FAIL no XHCI98.SYS in SYSTEM32\DRIVERS>COM1
echo x>C:\XHLEG\PH3.ON
goto off
:ph2go
echo x>C:\XHLEG\PH3.ON
echo XHLEG PHASE 2 check>COM1
echo XHLEG CHECK driver-file PASS>COM1
%XC%:
cd \PKG
TOOLS\XHCIINST\XHCIINST.EXE /dir release-x86 /list>C:\XHLEG\LIST2.TXT
call %XC%:\XHEXIT.BAT list
C:
cd \
call %XC%:\XHSEND.BAT list C:\XHLEG\LIST2.TXT
find "current (this" C:\XHLEG\LIST2.TXT>NUL
if errorlevel 1 goto l2fail
echo XHLEG CHECK list-current PASS>COM1
goto stick
:l2fail
echo XHLEG CHECK list-current FAIL>COM1

:stick
echo XHLEG WANT plug-stick>COM1
set XS=
set XN=
:waits
for %%d in (D E F G H I J K L M) do if exist %%d:\TESTDATA.BIN set XS=%%d
if not "%XS%"=="" goto gots
set XN=%XN%x
if "%XN%"=="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" goto nos
choice /c:x /t:x,3 /n >NUL
goto waits
:nos
echo XHLEG CHECK stick-seen FAIL no drive with TESTDATA.BIN in 120 s>COM1
goto unin
:gots
echo XHLEG CHECK stick-seen PASS %XS%:>COM1
copy %XC%:\XHDATA\RAND.BIN %XS%:\RAND.BIN>NUL
fc /b %XC%:\XHDATA\RAND.BIN %XS%:\RAND.BIN>C:\XHLEG\FC.TXT
find "no differences" C:\XHLEG\FC.TXT>NUL
if errorlevel 1 goto fcfail
echo XHLEG CHECK stick-copy PASS>COM1
goto unplug
:fcfail
echo XHLEG CHECK stick-copy FAIL>COM1
call %XC%:\XHSEND.BAT fc C:\XHLEG\FC.TXT
:unplug
echo XHLEG WANT unplug-stick>COM1
choice /c:x /t:x,5 /n >NUL

:unin
%XC%:
cd \PKG
call INSTALL.BAT /console /uninstall /yes /nopause /log C:\XHLEG\UNINST.LOG
call %XC%:\XHEXIT.BAT uninstall
C:
cd \
call %XC%:\XHSEND.BAT uninstall C:\XHLEG\UNINST.LOG
goto off

:ph3
echo x>C:\XHLEG\PH4.ON
echo XHLEG PHASE 3 removed>COM1
%XC%:
cd \PKG
TOOLS\XHCIINST\XHCIINST.EXE /dir release-x86 /list>C:\XHLEG\LIST3.TXT
C:
cd \
call %XC%:\XHSEND.BAT list C:\XHLEG\LIST3.TXT
find "current (this" C:\XHLEG\LIST3.TXT>NUL
if errorlevel 1 goto l3ok
echo XHLEG CHECK list-removed FAIL a device still current>COM1
goto done
:l3ok
echo XHLEG CHECK list-removed PASS>COM1
:done
echo XHLEG END>COM1
goto off

:again
echo XHLEG END already>COM1
:off
echo XHLEG POWEROFF>COM1
rundll32.exe shell32.dll,SHExitWindowsEx 5
:end
