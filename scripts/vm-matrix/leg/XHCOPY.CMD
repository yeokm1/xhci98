@echo off
rem XHCOPY.CMD - one copy check of the standard leg on Windows 2000 to 10,
rem called by LEGNT.CMD (:dcopy): %1 the source, %2 the destination file,
rem %3 the stem of its logs (%3RM.TXT, %3CP.TXT, %3FC.TXT). Says the result
rem in XR ("PASS on X:", "PASS on X:, an earlier NAME removed" or "FAIL on
rem X: why") and sets XOK to 1 on a PASS, empty otherwise.
rem A destination there from before is removed first and must then be gone:
rem a stick image may already hold it (the data and the names are fixed),
rem and a copy that failed over a file with the same bytes would still
rem compare equal. A folder of that name is left alone and fails the check.
rem Then copy's and fc's exit codes must both be 0 and fc must say "no
rem differences". No parenthesised blocks, so each ERRORLEVEL is read on
rem its own line, as it is when that line runs.
rem "XHCOPY.CMD /verdict CE FE FCLOG" runs the judgment alone on given exit
rem codes and fc output (the host's self-test, selftest.ps1).
setlocal EnableExtensions DisableDelayedExpansion
set "XKR="
set "XKW="
if /i "%~1"=="/verdict" goto verdict
set "XKD=%~d2"
set "XKN=%~nx2"
if not exist "%~2\*" goto rm0
set "XKR=FAIL on %XKD% %XKN% from before is a folder"
goto out
:rm0
if not exist "%~2" goto cp0
attrib -r -h -s "%~2" >"%~3RM.TXT" 2>&1
del /q "%~2" >>"%~3RM.TXT" 2>&1
if not exist "%~2" goto rm1
set "XKR=FAIL on %XKD% %XKN% from before cannot be removed"
goto out
:rm1
set "XKW=, an earlier %XKN% removed"
:cp0
cmd /d /c exit 0
copy /y "%~1" "%~2" >"%~3CP.TXT" 2>&1
set "XKCE=%ERRORLEVEL%"
fc /b "%~1" "%~2" >"%~3FC.TXT" 2>&1
set "XKFE=%ERRORLEVEL%"
call :judge %XKCE% %XKFE% "%~3FC.TXT"
goto out

:verdict
set "XKD=X:"
call :judge %~2 %~3 "%~4"
goto out

rem PASS only when copy exited 0, fc exited 0 and fc said "no differences".
:judge
set "XKR=FAIL on %XKD% copy exit %1, fc exit %2"
if not "%~1"=="0" goto :eof
if not "%~2"=="0" goto :eof
find "no differences" %3 >NUL 2>&1
if errorlevel 1 goto :eof
set "XKR=PASS on %XKD%%XKW%"
goto :eof

:out
set "XKOK="
if "%XKR:~0,4%"=="PASS" set "XKOK=1"
endlocal & set "XR=%XKR%" & set "XOK=%XKOK%"
goto :eof
