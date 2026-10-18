@echo off
rem selftest.cmd - XHCIDI.EXE's checks on a build host (design record 27
rem sections 3.2, 7 and 11). A Win16 image does not run on 64-bit Windows,
rem so what is checked here is the image, not a run of it:
rem
rem   - its import table (check-imports.ps1): KERNEL, USER and SETUPX alone,
rem     every import by ordinal and each the allowlist's, each name the one
rem     its import library binds to that ordinal; and the gate's refusals
rem     (test-gate.ps1);
rem   - the version stamp "XHCIDI <version>" in its data, at
rem     src\xhci_version.h's XHCI_VER_STR, which make-release.ps1 reads too.
rem
rem The pure part's suite is test\test_di.c, which test\run-host-tests.cmd
rem runs; the engine calls are proved on the 42-V.1 legs.
rem
rem Exit code 0 = both held. Run it after build.cmd.
setlocal
cd /d "%~dp0"
if not exist xhcidi.exe (
    echo ERROR: XHCIDI.EXE is not here; run build.cmd first
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-imports.ps1" -Path "%~dp0xhcidi.exe"
if errorlevel 1 exit /b 1
rem The gate's own cases: what it must refuse (test-gate.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test-gate.ps1" -Image "%~dp0xhcidi.exe" -PeImage "%SystemRoot%\System32\notepad.exe"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -Command "$v = [regex]::Match((Get-Content -LiteralPath '%~dp0..\..\src\xhci_version.h' -Raw), '#define\s+XHCI_VER_STR\s+\"([0-9.]+)\"').Groups[1].Value; $t = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes('%~dp0xhcidi.exe')); if ($v -eq '' -or -not $t.Contains('XHCIDI ' + $v + [char]0)) { Write-Output ('ERROR: XHCIDI.EXE carries no stamp XHCIDI ' + $v); exit 1 }; Write-Output ('XHCIDI.EXE: stamp XHCIDI ' + $v)"
if errorlevel 1 exit /b 1
echo OK: xhcidi self-test passed
exit /b 0
