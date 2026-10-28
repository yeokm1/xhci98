<#
.SYNOPSIS
  Capture and compare the request traces of the USB4 connection manager's
  host suites and of the usb4-emu cross-check (roadmap-usb4.md task 44.38).

.DESCRIPTION
  Capture (default): builds test_u4cm, test_tbt_ring and test_tbt_icm as
  test\run-host-tests.cmd does (MSVC 6.0, /W3 /WX /Za /DXHCI_HOST_TEST),
  runs each with its trace variable set (U4_TRACE for the fake fabric of
  test\u4_fakefab.c, TBT_TRACE for the host interface of test\tbt_fake.h)
  and writes <Out>\test_u4cm.trace, test_tbt_ring.trace and
  test_tbt_icm.trace, with each suite's output beside it. Each trace holds,
  in order, every frame the code under test transmitted (route, the
  request's space, adapter, offset and count, the values written, every
  acknowledgement), every host-interface register write and every log line, each with the virtual clock, and a "=="
  line naming each vector before its lines.

  -CrossCheck also runs scripts\usb4-crosscheck\run-crosscheck.sh through
  usb4-emu's MSYS2 shell (USB4EMU names the checkout, as runs/run-40.md
  says) with XC_REQ_TRACE set, and writes <Out>\crosscheck.trace and
  crosscheck.log.

  -Compare <golden> compares every trace in <Out> with the one of the same
  name in <golden>, vector by vector, and prints each vector that differs
  with its first differing line. Exit code 0 when every trace is identical.

  A trace is whole only when its last line is "== end" (the suite writes
  it after its last vector; a failed open or write ends the run), and each
  run removes its old traces first, so a stale or truncated trace never
  compares. Every trace the run captured must exist whole in <golden>.

  The traces pin one build's exact behaviour and are large, so they are
  kept under the git-ignored out\, never tracked: a rewrite captures the
  golden set before it starts and compares after each step.

.EXAMPLE
  powershell -File scripts\usb4-trace\u4-trace.ps1 -Out out\trace\golden -CrossCheck
  powershell -File scripts\usb4-trace\u4-trace.ps1 -Out out\trace\step1 -CrossCheck -Compare out\trace\golden
#>
param(
    [Parameter(Mandatory = $true)] [string] $Out,
    [switch] $CrossCheck,
    [string] $Compare
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$testDir = Join-Path $repo 'test'
if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $repo $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

$suites = @(
    @{ Name = 'test_u4cm'; Var = 'U4_TRACE';
       Src = 'test_u4cm.c u4_fakefab.c ..\src\cm\tbt_u4cm.c ..\src\cm\tbt_hw.c ..\src\cm\tbt_icm.c ..\src\cm\tbt_ring.c' },
    @{ Name = 'test_tbt_ring'; Var = 'TBT_TRACE'; Src = 'test_tbt_ring.c ..\src\cm\tbt_ring.c' },
    @{ Name = 'test_tbt_icm'; Var = 'TBT_TRACE'; Src = 'test_tbt_icm.c ..\src\cm\tbt_icm.c ..\src\cm\tbt_ring.c' }
)

# A trace counts only when this run wrote it whole: removed first, and its
# last line the "== end" the suite writes after its last vector.
function Test-TraceWhole([string] $path) {
    if (-not (Test-Path $path)) { return $false }
    $last = Get-Content $path -Tail 1
    return ($last -ceq '== end')
}

function Invoke-Capture {
    $msvc = $env:MSVC6
    if (-not $msvc) { $msvc = Join-Path $repo 'tools\MSVC600' }
    $cl = Join-Path $msvc 'VC98\BIN\cl.exe'
    if (-not (Test-Path $cl)) { throw "no MSVC 6.0 at $msvc" }
    $env:PATH = (Join-Path $msvc 'VC98\BIN') + ';' + (Join-Path $msvc 'Common\MSDev98\Bin') + ';' + $env:PATH
    $env:INCLUDE = Join-Path $msvc 'VC98\INCLUDE'
    $env:LIB = Join-Path $msvc 'VC98\LIB'
    $obj = Join-Path $Out 'obj'
    New-Item -ItemType Directory -Force $obj | Out-Null
    $failed = $false
    Push-Location $testDir
    try {
        foreach ($s in $suites) {
            $exe = Join-Path $obj ($s.Name + '.exe')
            if (Test-Path $exe) { Remove-Item $exe -Force }
            $clArgs = @('/nologo', '/W3', '/WX', '/Za', '/DXHCI_HOST_TEST', "/Fe$exe", "/Fo$obj\") + ($s.Src -split ' ')
            & $cl @clArgs | Out-Host
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) { throw "$($s.Name): build failed" }
            $trace = Join-Path $Out ($s.Name + '.trace')
            $log = Join-Path $Out ($s.Name + '.out')
            if (Test-Path $trace) { Remove-Item $trace -Force }
            foreach ($v in 'U4_TRACE', 'TBT_TRACE') { Remove-Item "env:$v" -ErrorAction SilentlyContinue }
            for ($try = 1; $try -le 5; $try++) {
                Set-Item "env:$($s.Var)" $trace
                & $exe > $log 2>&1
                $rc = $LASTEXITCODE
                Remove-Item "env:$($s.Var)"
                $text = Get-Content $log -Raw
                if ($text -match 'checks, ') { break }
                if ($text -notmatch 'Device Guard|blocked by') { break }
                Start-Sleep -Seconds 2
            }
            $verdict = (Get-Content $log | Select-String 'checks, ' | Select-Object -Last 1)
            Write-Host ("{0}: {1} (exit {2})" -f $s.Name, $verdict, $rc)
            if ($rc -ne 0) { $failed = $true }
            if (-not (Test-TraceWhole $trace)) { Write-Host "$($s.Name): no whole trace"; $failed = $true }
        }
    } finally {
        Pop-Location
    }
    return -not $failed
}

function Invoke-CrossCheck {
    $emu = $env:USB4EMU
    if (-not $emu) { throw 'set USB4EMU to the usb4-emu checkout (runs/run-40.md)' }
    $shell = Join-Path $emu 'scripts\msys2-shell.sh'
    if (-not (Test-Path $shell)) { throw "no $shell" }
    $bash = 'C:\Program Files\Git\bin\sh.exe'
    if (-not (Test-Path $bash)) { $bash = 'sh.exe' }
    $u = { param($p) $q = $p -replace '\\', '/'; if ($q -match '^([A-Za-z]):(.*)$') { '/' + $Matches[1].ToLower() + $Matches[2] } else { $q } }
    $trace = & $u (Join-Path $Out 'crosscheck.trace')
    $xcOut = & $u (Join-Path $Out 'xc')
    $script = & $u (Join-Path $repo 'scripts\usb4-crosscheck\run-crosscheck.sh')
    $emuU = & $u $emu
    $cmd = "export XC_REQ_TRACE='$trace' OUT='$xcOut' USB4EMU='$emuU'; sh '$script'"
    $log = Join-Path $Out 'crosscheck.log'
    $whole = Join-Path $Out 'crosscheck.trace'
    if (Test-Path $whole) { Remove-Item $whole -Force }
    & $bash (& $u $shell) $cmd > $log 2>&1
    $rc = $LASTEXITCODE
    $verdict = (Get-Content $log | Select-String 'checks, ' | Select-Object -Last 1)
    Write-Host ("crosscheck: {0} (exit {1})" -f $verdict, $rc)
    if (-not (Test-TraceWhole $whole)) { Write-Host 'crosscheck: no whole trace'; return $false }
    return ($rc -eq 0)
}

function Split-Vectors([string] $path) {
    $map = [ordered]@{}
    $name = '(head)'
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($l in [IO.File]::ReadLines($path)) {
        if ($l.StartsWith('== ')) {
            $map[$name] = $lines
            $name = $l.Substring(3)
            while ($map.Contains($name)) { $name += "'" }
            $lines = New-Object System.Collections.Generic.List[string]
        } else {
            $lines.Add($l)
        }
    }
    $map[$name] = $lines
    return $map
}

# Every trace this run captured is compared, and each must exist whole in
# both sets: a missing or truncated one is a difference, never a skip.
function Invoke-Compare([string] $golden, [string[]] $names) {
    if (-not [IO.Path]::IsPathRooted($golden)) { $golden = Join-Path $repo $golden }
    $same = $true
    foreach ($name in $names) {
        $old = Join-Path $golden $name
        $new = Join-Path $Out $name
        if (-not (Test-TraceWhole $old)) { Write-Host "${name}: no whole golden trace"; $same = $false; continue }
        if (-not (Test-TraceWhole $new)) { Write-Host "${name}: no whole new trace"; $same = $false; continue }
        $t = Get-Item $old
        $a = Split-Vectors $t.FullName
        $b = Split-Vectors $new
        $diff = 0
        foreach ($k in $a.Keys) {
            if (-not $b.Contains($k)) { Write-Host "  $($t.Name) [$k]: missing"; $diff++; continue }
            $x = $a[$k]; $y = $b[$k]
            $n = [Math]::Min($x.Count, $y.Count)
            $i = 0
            while ($i -lt $n -and $x[$i] -ceq $y[$i]) { $i++ }
            if ($i -lt $n -or $x.Count -ne $y.Count) {
                $diff++
                Write-Host ("  {0} [{1}]: differs at line {2} of {3}/{4}" -f $t.Name, $k, ($i + 1), $x.Count, $y.Count)
                if ($i -lt $x.Count) { Write-Host "    golden: $($x[$i])" }
                if ($i -lt $y.Count) { Write-Host "    new:    $($y[$i])" }
            }
        }
        foreach ($k in $b.Keys) { if (-not $a.Contains($k)) { Write-Host "  $($t.Name) [$k]: new vector"; $diff++ } }
        $lines = 0; foreach ($k in $a.Keys) { $lines += $a[$k].Count }
        if ($diff -eq 0) {
            Write-Host ("{0}: identical ({1} vectors, {2} lines)" -f $t.Name, $a.Count, $lines)
        } else {
            Write-Host ("{0}: {1} vector(s) differ" -f $t.Name, $diff)
            $same = $false
        }
    }
    return $same
}

$ok = Invoke-Capture
if ($CrossCheck) { if (-not (Invoke-CrossCheck)) { $ok = $false } }
$names = @($suites | ForEach-Object { $_.Name + '.trace' })
if ($CrossCheck) { $names += 'crosscheck.trace' }
if ($Compare) { if (-not (Invoke-Compare $Compare $names)) { $ok = $false } }
if ($ok) { exit 0 } else { exit 1 }
