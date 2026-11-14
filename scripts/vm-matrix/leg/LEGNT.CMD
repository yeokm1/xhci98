@echo off
rem LEGNT.CMD - the standard leg on Windows 2000 to 10, run by XHPOLL.CMD from
rem the leg's CD as XHLEG.CMD, %1 the CD's drive letter (build-and-test.md,
rem "Faster emulator legs"). One phase per Windows start, chosen by the marker
rem files in C:\XHLEG; each phase ends by switching the machine off, and the
rem host starts it again cold. The host reads COM1 and gives the verdict.
rem XHMODE.CMD on the CD sets XHMODE: console (INSTALL /console /yes) or
rem window (the host or an operator runs the installer's window, then the
rem same checks). In window mode the first phase says the CD's letter, then
rem waits for the installer's log to end with "Exit code N." and says that
rem code and "WINDOW done": the run's end, which the screen cannot tell the
rem host. A log from before is moved to C:\XHLEG\PRIOR.LOG first. XHARGS,
rem set there too, is added to the console install (a leg's InstallArgs);
rem XHAUDIO, when set, adds the playback check (a leg's Audio, :audio).
rem XHTRUST, when set (a leg's TrustCert, Windows 10 x64), trusts the
rem package's own test certificate before the install (:trust). XHDEVSEQ,
rem when set (a leg's Devices, Windows 10), adds the controller's disable,
rem enable, remove and rescan after the stick's copy (:devseq), and an
rem uninstall that stops for a pending restart is run again after the next
rem start (:un4).
setlocal
set "XC=%~1"
call %XC%:\XHMODE.CMD
set "XL=C:\XHLEG"
if exist %XL%\PH4.ON goto again
if exist %XL%\PH3.ON goto ph3
if exist %XL%\PH2.ON goto ph2

echo x>%XL%\PH2.ON
echo XHLEG PHASE 1 install %XHMODE%>COM1
ver>%XL%\VER.TXT
call :send ver %XL%\VER.TXT
if defined XHTRUST call :trust
if "%XHMODE%"=="window" goto operator
pushd %XC%:\PKG
call INSTALL.BAT /console /yes /nopause /log %XL%\INSTALL.LOG %XHARGS% >%XL%\INSTALL.TXT 2>&1
>COM1 echo XHLEG EXIT install %ERRORLEVEL%
popd
call :send install %XL%\INSTALL.LOG
goto off
:operator
if exist C:\XHCIINST.LOG move /y C:\XHCIINST.LOG %XL%\PRIOR.LOG >NUL
echo XHLEG OPERATOR %XC% install with the window, then restart or shut down>COM1
:wwait
if exist C:\XHCIINST.LOG (
    find "Exit code " C:\XHCIINST.LOG >NUL 2>&1
    if not errorlevel 1 goto wdone
)
ping -n 6 127.0.0.1 >NUL 2>&1
goto wwait
:wdone
call :wrep
echo XHLEG WINDOW done>COM1
goto end

rem The window's run on COM1: its exit code and its log, once. The code is
rem the word after "Exit code " on the log's last such line, wherever that
rem phrase sits: every log line now begins with its [hh:mm:ss.mmm] stamp,
rem which moved the code from the line's third word to its fifth.
:wrep
set "XE=unread"
set "XEL="
for /f "delims=" %%l in ('type C:\XHCIINST.LOG ^| find "Exit code "') do set "XEL=%%l"
if defined XEL set "XEL=%XEL:*Exit code =%"
if defined XEL for /f "tokens=1 delims=. " %%c in ("%XEL%") do set "XE=%%c"
>COM1 echo XHLEG EXIT install %XE%
call :send install C:\XHCIINST.LOG
echo x>%XL%\WSENT.ON
goto :eof

rem The package's test certificate trusted (XHTRUST, a leg's TrustCert):
rem a released x64 package carries the public half of the certificate its
rem drivers and catalogs are signed with, nt-x64\xhci98-test.cer. Added to
rem Root and TrustedPublisher with Windows' own certutil, Windows 10 x64
rem installs the package with no publisher question. Windows loads such a
rem driver only in test-signing mode, which the guest's image must already
rem have on: the leg checks it and does not change it.
:trust
set "XCER=%XC%:\PKG\nt-x64\xhci98-test.cer"
if exist "%XCER%" goto trust1
echo XHLEG CHECK cert-trust FAIL the package has no nt-x64\xhci98-test.cer>COM1
goto trust2
:trust1
certutil -addstore -f Root "%XCER%" >%XL%\CERT.TXT 2>&1
set "XT1=%ERRORLEVEL%"
certutil -addstore -f TrustedPublisher "%XCER%" >>%XL%\CERT.TXT 2>&1
set "XT2=%ERRORLEVEL%"
call :send cert %XL%\CERT.TXT
set "XR=FAIL certutil exits %XT1% and %XT2%"
if "%XT1%%XT2%"=="00" set "XR=PASS"
>COM1 echo XHLEG CHECK cert-trust %XR%
:trust2
bcdedit /enum {current} >%XL%\BCD.TXT 2>&1
set "XR=FAIL the guest is not in test-signing mode"
find /i "testsigning" %XL%\BCD.TXT | find /i "Yes" >NUL
if not errorlevel 1 set "XR=PASS"
>COM1 echo XHLEG CHECK test-signing %XR%
goto :eof

rem The controller's disable, enable, remove and rescan (XHDEVSEQ, a leg's
rem Devices; Windows 10 only: its own pnputil has the four device
rem commands, so no helper program is carried). Run after the stick's
rem copy, the stick still in. The controller is QEMU's xHCI, found by its
rem PCI ids among the connected devices; its instance id holds "&", so it
rem is only ever used inside quotes.
rem   ctrl-disable       pnputil exit 0 and the stick's drive gone 15 s on.
rem                      Windows may defer a disable to the next restart
rem                      (pnputil exit 3010, the controller still
rem                      started): the first forced-TCG leg's disable,
rem                      about 60 s after the stick's first plug, was
rem                      (2026-11-11). What stood in its way was not
rem                      read; Windows' own use of a newly plugged stick
rem                      is suspected, so the leg first waits, as best it
rem                      can, for the stick's volume to show no transfer
rem                      (:dstill)
rem   ctrl-enable        the disable passed, pnputil exit 0 with no
rem                      "Failed" in its answer, and a drive with
rem                      TESTDATA.BIN back within 360 s
rem   stick-copy-enable  the 4 MiB file copied to it again and compared
rem   ctrl-remove        pnputil exit 0 (removed) or 3010 (removed, Windows
rem                      asks for a restart); which of the two is said in a
rem                      NOTE and is not part of the check: the same leg has
rem                      given both answers (runs/run-49.md, "49.4"). The
rem                      part of Windows' device-install log the remove
rem                      wrote goes to COM1: it names what a removal waits
rem                      for
rem   ctrl-rescan        pnputil /scan-devices exit 0 and, once the install
rem                      log is quiet, a drive with TESTDATA.BIN back within
rem                      420 s
rem   stick-copy-rescan  the file copied and compared once more
rem After each step the controller's own pnputil entry (its driver, its
rem state) goes to COM1 as a log.
:devseq
set "XID="
for /f "tokens=1,* delims=:" %%a in ('pnputil /enum-devices /connected ^| findstr /i /c:"VEN_1B36&DEV_000D"') do for /f "tokens=*" %%t in ("%%b") do set "XID=%%t"
if defined XID goto dev1
echo XHLEG CHECK ctrl-disable FAIL pnputil lists no connected PCI device VEN_1B36 DEV_000D>COM1
goto :eof
:dev1
>COM1 echo XHLEG NOTE controller "%XID%"
call :devsay before
call :dstill

pnputil /disable-device "%XID%" >%XL%\DV1.TXT 2>&1
set "XE1=%ERRORLEVEL%"
call :send ctrl-disable %XL%\DV1.TXT
ping -n 16 127.0.0.1 >NUL 2>&1
set "XD1="
set "XR=FAIL pnputil exit %XE1%"
if "%XE1%"=="3010" set "XR=FAIL pnputil exit 3010: Windows deferred the disable to a restart"
if "%XE1%"=="0" set "XR=PASS"
if "%XE1%"=="0" if exist %XS%:\TESTDATA.BIN set "XR=FAIL drive %XS%: is still there"
if "%XR%"=="PASS" set "XD1=1"
>COM1 echo XHLEG CHECK ctrl-disable %XR%
call :devsay disable

pnputil /enable-device "%XID%" >%XL%\DV2.TXT 2>&1
set "XE2=%ERRORLEVEL%"
call :send ctrl-enable %XL%\DV2.TXT
call :dwait 360
set "XR=FAIL pnputil exit %XE2%"
if "%XE2%"=="0" set "XR=FAIL no drive with TESTDATA.BIN in 360 s"
if "%XE2%"=="0" if defined XS2 set "XR=PASS %XS2%: after %XW% s"
find "Failed" %XL%\DV2.TXT >NUL 2>&1
if not errorlevel 1 set "XR=FAIL pnputil says it failed"
if not defined XD1 set "XR=FAIL the disable did not complete, so nothing was enabled"
>COM1 echo XHLEG CHECK ctrl-enable %XR%
call :devsay enable
call :dcopy stick-copy-enable R2.BIN

set "XLC=0"
for /f %%c in ('type "%SystemRoot%\inf\setupapi.dev.log" ^| find /c /v ""') do set "XLC=%%c"
pnputil /remove-device "%XID%" >%XL%\DV3.TXT 2>&1
set "XE3=%ERRORLEVEL%"
call :send ctrl-remove %XL%\DV3.TXT
set "XR=FAIL pnputil exit %XE3%"
if "%XE3%"=="0" set "XR=PASS"
if "%XE3%"=="3010" set "XR=PASS"
if "%XE3%"=="0" echo XHLEG NOTE ctrl-remove done>COM1
if "%XE3%"=="3010" echo XHLEG NOTE ctrl-remove restart asked>COM1
>COM1 echo XHLEG CHECK ctrl-remove %XR%
ping -n 31 127.0.0.1 >NUL 2>&1
powershell -NoProfile -Command "Get-Content -LiteralPath $env:SystemRoot\inf\setupapi.dev.log | Select-Object -Skip %XLC% | Set-Content -Encoding ASCII -LiteralPath %XL%\DV3LOG.TXT" >NUL 2>&1
call :send ctrl-remove-log %XL%\DV3LOG.TXT
call :devsay remove

pnputil /scan-devices >%XL%\DV4.TXT 2>&1
set "XE4=%ERRORLEVEL%"
call :send ctrl-rescan %XL%\DV4.TXT
call :dquiet
call :dwait 420
set "XR=FAIL pnputil exit %XE4%"
if "%XE4%"=="0" set "XR=FAIL no drive with TESTDATA.BIN in 420 s"
if "%XE4%"=="0" if defined XS2 set "XR=PASS %XS2%: after %XW% s"
>COM1 echo XHLEG CHECK ctrl-rescan %XR%
call :devsay rescan
call :dcopy stick-copy-rescan R3.BIN
goto :eof

rem The controller's entry as pnputil lists it, to COM1.
:devsay
pnputil /enum-devices /instanceid "%XID%" >%XL%\DVS.TXT 2>&1
call :send ctrl-after-%1 %XL%\DVS.TXT
goto :eof

rem A bounded, best-effort wait before the disable: the stick's volume's
rem transfers a second, read with Windows' own typeperf in rounds of ten
rem samples 2 s apart, until a round has no transfer in any sample; at
rem most 24 rounds (8 min of samples). No transfer does not prove that
rem nothing has the volume open. What the last round read is said in a
rem NOTE; the disable follows in any case and is what is judged.
:dstill
set /a XT=0
:dstill1
set /a XT+=1
set "XQ0=0"
for /f %%c in ('typeperf "\LogicalDisk(%XS%:)\Disk Transfers/sec" -si 2 -sc 10 2^>NUL ^| find /c """0.000000"""') do set "XQ0=%%c"
if "%XQ0%"=="10" goto dstill2
if %XT% GEQ 24 goto dstill2
goto dstill1
:dstill2
>COM1 echo XHLEG NOTE stick volume idle: %XQ0% of 10 samples with no transfer in round %XT%
goto :eof

rem XS2: a drive with TESTDATA.BIN, looked for every 5 s for %1 s (XW).
:dwait
set "XS2="
set /a XW=0
:dwait1
for %%d in (D E F G H I J K L M) do if exist %%d:\TESTDATA.BIN set "XS2=%%d"
if defined XS2 goto :eof
if %XW% GEQ %1 goto :eof
ping -n 6 127.0.0.1 >NUL 2>&1
set /a XW+=5
goto dwait1

rem Windows' device-install log not growing for 15 s (checked every 5 s),
rem at most 300 s: after a rescan Windows installs the controller's whole
rem tree again, the stick's volume last, and no drive is looked at before.
:dquiet
set /a XN=0
set /a XQ=0
set "XZ0=none"
:dquiet1
set /a XN+=1
set "XZ=absent"
for %%f in ("%SystemRoot%\inf\setupapi.dev.log") do set "XZ=%%~zf"
if "%XZ%"=="%XZ0%" (set /a XQ+=1) else (set /a XQ=0)
set "XZ0=%XZ%"
if %XQ% GEQ 3 goto dquiet2
if %XN% GEQ 60 goto dquiet2
ping -n 6 127.0.0.1 >NUL 2>&1
goto dquiet1
:dquiet2
>COM1 echo XHLEG NOTE install log quiet after %XN% checks
goto :eof

rem The 4 MiB file to the drive XS2 as %2 and compared: check %1, by
rem XHCOPY.CMD from the CD. A file of that name there from before is
rem removed first and must be gone; then copy's and fc's exit codes must
rem be 0 and fc must say "no differences". The logs go to COM1 on a FAIL.
:dcopy
if defined XS2 goto dcopy1
>COM1 echo XHLEG CHECK %1 FAIL no drive
goto :eof
:dcopy1
call %XC%:\XHCOPY.CMD %XC%:\XHDATA\RAND.BIN %XS2%:\%2 %XL%\%~n2
>COM1 echo XHLEG CHECK %1 %XR%
if defined XOK goto :eof
call :send remove %XL%\%~n2RM.TXT
call :send copy %XL%\%~n2CP.TXT
call :send fc %XL%\%~n2FC.TXT
goto :eof

rem An uninstall that ended with exit 4 on a leg with Devices: after a
rem remove that asked for a restart the installer removes what it can and
rem stops ("removed, restart pending": the controller's removal waits for
rem the restart), and the uninstall is run again after the next start
rem (:unagain). Exit 4 for any other reason fails uninstall-pending.
:un4
find "restart pending" %XL%\UNINST.LOG >NUL 2>&1
if errorlevel 1 goto un4bad
echo x>%XL%\UN4.ON
echo XHLEG CHECK uninstall-pending PASS>COM1
goto :eof
:un4bad
echo XHLEG CHECK uninstall-pending FAIL exit 4 and no removal waiting for a restart in the log>COM1
goto :eof

rem The playback check (XHAUDIO, a leg's Audio): the host plugs QEMU's
rem usb-audio into the controller; WAVCHK.EXE waits up to 300 s for the
rem waveOut device named USB that Windows installs for it, plays the CD's
rem 2 s WAV through it and says each call's MMSYSERR result and the time
rem the play took; exit 0 only when it played to the end. Then XHCITR's
rem report must hold xhci98.sys's counters (TransfersSubmitted, nonzero
rem once a device is there) and no UrbsMalformed among them: a URB the driver refused, as 3.4.2.0 refused
rem Windows 2000's streaming select (docs/issues/30). The report is read
rem after the play, so a refusal the play caused is counted.
:audio
echo XHLEG WANT plug-audio>COM1
%XC%:\XHWAV\WAVCHK.EXE %XC%:\XHWAV\TONE.WAV /name USB /wait 300 >%XL%\WAV.TXT 2>&1
set "XWE=%ERRORLEVEL%"
call :send wavchk %XL%\WAV.TXT
if "%XWE%"=="0" (
    echo XHLEG CHECK audio-play PASS>COM1
) else (
    >COM1 echo XHLEG CHECK audio-play FAIL WAVCHK exit %XWE%
)
if exist %XL%\TR.TXT del %XL%\TR.TXT
start "" /wait %XC%:\PKG\tools\xhcitr\XHCITR.EXE /report %XL%\TR.TXT
set "XTE=%ERRORLEVEL%"
if not exist %XL%\TR.TXT (
    >COM1 echo XHLEG CHECK urbs-malformed FAIL no report from XHCITR, exit %XTE%
    goto :eof
)
call :send xhcitr %XL%\TR.TXT
find " TransfersSubmitted " %XL%\TR.TXT >NUL
if errorlevel 1 (
    >COM1 echo XHLEG CHECK urbs-malformed FAIL the report holds no xhci98 counters, XHCITR exit %XTE%
    goto :eof
)
find " UrbsMalformed " %XL%\TR.TXT >%XL%\TRM.TXT
if errorlevel 1 (
    echo XHLEG CHECK urbs-malformed PASS>COM1
) else (
    echo XHLEG CHECK urbs-malformed FAIL>COM1
    call :send urbs %XL%\TRM.TXT
)
goto :eof

rem Windows 2000 asks "System Settings Change ... restart your computer
rem now?" once the audio device's drivers are in (read on q35, 2026-11-10,
rem with the tip's package; not with 3.4.2.0's, whose open failed). While
rem it is open the install is still pending, so the uninstall below would
rem wait a minute and stop with exit 4. XHNORST.VBS answers it with No,
rem waiting up to 120 s for it; its result goes to COM1 as a log, not a
rem check.
:norst
cscript //nologo %XC%:\XHWAV\XHNORST.VBS 120 >%XL%\NORST.TXT 2>&1
call :send norst %XL%\NORST.TXT
goto :eof

:ph2
rem A restart taken before the wait above read the log (an operator's quick
rem Finish): the run's exit code and log, said now.
if "%XHMODE%"=="window" if not exist %XL%\WSENT.ON call :wrep
echo x>%XL%\PH3.ON
echo XHLEG PHASE 2 check>COM1
if exist %SystemRoot%\System32\drivers\xhci98.sys (
    echo XHLEG CHECK driver-file PASS>COM1
) else (
    echo XHLEG CHECK driver-file FAIL no xhci98.sys in System32\drivers>COM1
)
pushd %XC%:\PKG
call INSTALL.BAT /console /list >%XL%\LIST2.TXT 2>&1
>COM1 echo XHLEG EXIT list %ERRORLEVEL%
popd
call :send list %XL%\LIST2.TXT
rem A driver at the package's version: "Keep (same version)" from
rem 3.4.3.0 on, "Reinstall (same version)" in 3.4.2.0 and earlier.
set "XCUR="
find "Keep (same version)" %XL%\LIST2.TXT >NUL
if not errorlevel 1 set "XCUR=1"
find "Reinstall (same version)" %XL%\LIST2.TXT >NUL
if not errorlevel 1 set "XCUR=1"
if defined XCUR (
    echo XHLEG CHECK list-current PASS>COM1
) else (
    echo XHLEG CHECK list-current FAIL>COM1
)
if defined XHAUDIO call :audio
if defined XHAUDIO call :norst
rem At a stick's first plug Windows installs its disk and then its volume,
rem and on XP the volume's install restarts the volume after its letter is
rem there: a file copied just before that restart was not on the volume
rem after it ("FC: cannot open E:\RAND.BIN", XP on q35, 2026-10-27). And on
rem 2000 a look at the stick's drive while its volume is being installed
rem holds the volume open, so the install's query-remove is vetoed (veto
rem type 5) and Windows asks for a restart (w2k-nonfree, 2026-11-10). So
rem no drive is looked at until the volume's install is finished in
rem Windows' device-install log: on 2000 and XP (setupapi.log) one more
rem line than before the plug naming a STORAGE\ device and "finished
rem successfully"; on Vista and 7 (setupapi.dev.log) more lines naming a
rem STORAGE\ device than before and every section started also ended. Then
rem the log must not grow for 15 s; checked every 5 s, at most 300 s from
rem the plug request. At the bound, a quiet log with nothing new (the stick
rem known from before: no install ran) goes on and says so; anything else
rem fails stick-seen. Only then is the stick's drive searched for. Copy's
rem and fc's own messages are kept, and a failed compare is never retried.
set "XSA=%SystemRoot%\setupapi.log"
if exist %SystemRoot%\inf\setupapi.dev.log set "XSA=%SystemRoot%\inf\setupapi.dev.log"
call :scount
set "XC0=%XCNT%"
echo XHLEG WANT plug-stick>COM1
set /a XN=0
set /a XQ=0
set "XZ0=none"
:sqwait
set /a XN+=1
call :scount
set "XZ=absent"
if exist "%XSA%" for %%f in ("%XSA%") do set "XZ=%%~zf"
if "%XZ%"=="%XZ0%" (set /a XQ+=1) else (set /a XQ=0)
set "XZ0=%XZ%"
if %XQ% GEQ 3 if %XCNT% GTR %XC0% if %XOPEN% LEQ 0 goto squiet
if %XN% GEQ 60 goto sbound
ping -n 6 127.0.0.1 >NUL 2>&1
goto sqwait
:sbound
if %XQ% LSS 3 goto sfail
if %XCNT% GTR %XC0% goto sfail
if %XOPEN% GTR 0 goto sfail
>COM1 echo XHLEG NOTE stick settle: no STORAGE\ install logged since the plug (%XC0% lines before)
goto squiet
:sfail
>COM1 echo XHLEG CHECK stick-seen FAIL no finished volume install in %XN% checks (STORAGE\ lines %XC0% then %XCNT%, sections open %XOPEN%, quiet %XQ%)
goto unin
:squiet
>COM1 echo XHLEG NOTE stick settle: %XN% checks, STORAGE\ lines %XC0% then %XCNT%
set "XS="
for /l %%n in (1,1,40) do (
    if not defined XS (
        for %%d in (D E F G H I J K L M) do if exist %%d:\TESTDATA.BIN set "XS=%%d"
        if not defined XS ping -n 4 127.0.0.1 >NUL 2>&1
    )
)
if not defined XS (
    echo XHLEG CHECK stick-seen FAIL no drive with TESTDATA.BIN in 120 s after the settle>COM1
    goto unin
)
echo XHLEG CHECK stick-seen PASS %XS%:>COM1
set "XS2=%XS%"
call :dcopy stick-copy RAND.BIN
if defined XHDEVSEQ call :devseq
echo XHLEG WANT unplug-stick>COM1
ping -n 6 127.0.0.1 >NUL 2>&1

:unin
pushd %XC%:\PKG
call INSTALL.BAT /console /uninstall /yes /nopause /log %XL%\UNINST.LOG >%XL%\UNINST.TXT 2>&1
set "XUE=%ERRORLEVEL%"
>COM1 echo XHLEG EXIT uninstall %XUE%
popd
call :send uninstall %XL%\UNINST.LOG
if defined XHDEVSEQ if "%XUE%"=="4" call :un4
goto off

:unagain
echo x>%XL%\UN4B.ON
echo XHLEG PHASE 2b uninstall-again>COM1
pushd %XC%:\PKG
call INSTALL.BAT /console /uninstall /yes /nopause /log %XL%\UNINST2.LOG >%XL%\UNINST2.TXT 2>&1
set "XUE=%ERRORLEVEL%"
>COM1 echo XHLEG EXIT uninstall-again %XUE%
popd
call :send uninstall %XL%\UNINST2.LOG
goto off

:ph3
if exist %XL%\UN4.ON if not exist %XL%\UN4B.ON goto unagain
echo x>%XL%\PH4.ON
echo XHLEG PHASE 3 removed>COM1
pushd %XC%:\PKG
call INSTALL.BAT /console /list >%XL%\LIST3.TXT 2>&1
>COM1 echo XHLEG EXIT list-final %ERRORLEVEL%
popd
call :send list %XL%\LIST3.TXT
find "Keep (same version)" %XL%\LIST3.TXT >NUL
if errorlevel 2 (
    echo XHLEG CHECK list-removed FAIL find could not read the list>COM1
    goto l3done
)
if not errorlevel 1 (
    echo XHLEG CHECK list-removed FAIL a device still current>COM1
    goto l3done
)
find "Reinstall (same version)" %XL%\LIST3.TXT >NUL
if errorlevel 1 (
    echo XHLEG CHECK list-removed PASS>COM1
) else (
    echo XHLEG CHECK list-removed FAIL a device still current>COM1
)
:l3done
echo XHLEG END>COM1
goto off

:again
echo XHLEG END already>COM1
:off
echo XHLEG POWEROFF>COM1
if exist %SystemRoot%\system32\shutdown.exe (
    shutdown -s -f -t 0
) else (
    cscript //nologo C:\XHLEG\XHOFF.VBS
)
goto end

rem XCNT: on 2000 and XP the log's lines naming a STORAGE\ device and
rem "finished successfully", on Vista to 10 its lines naming a STORAGE\
rem device; XOPEN: on Vista to 10 the sections started less those ended.
:scount
set "XCNT=0"
set /a XOPEN=0
if not exist "%XSA%" goto :eof
if "%XSA%"=="%SystemRoot%\setupapi.log" (
    for /f %%c in ('type "%XSA%" ^| find /i "storage\" ^| find /c "finished successfully"') do set "XCNT=%%c"
    goto :eof
)
for /f %%c in ('type "%XSA%" ^| find /i /c "storage\"') do set "XCNT=%%c"
set "XST=0"
set "XEN=0"
for /f %%c in ('type "%XSA%" ^| find /c "Section start"') do set "XST=%%c"
for /f %%c in ('type "%XSA%" ^| find /c "Section end"') do set "XEN=%%c"
set /a XOPEN=XST-XEN
goto :eof

:send
if not exist "%~2" (
    echo XHLEG LOG-MISSING %1 %2>COM1
    goto :eof
)
echo XHLEG LOG-BEGIN %1>COM1
type "%~2">COM1
echo.>COM1
echo XHLEG LOG-END %1>COM1
goto :eof

:end
