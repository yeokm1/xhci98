<#
.SYNOPSIS
Hold a build of xhcihlpr.sys, the x86 helper, to the toolchain it was decided
on: WDK 7.1's WXP environment, its libraries, subsystem 5.00.

.DESCRIPTION
Design record 18 section 15.10 (roadmap-hcd.md task 38.3, decision H5). WDK
7.1 has no Windows 2000 build environment, and for a target it does not know
its makefile.new prints that the value is unknown and builds for the latest
target instead of stopping - so a mistaken environment would build silently
for Windows 7, and no field of the image's header shows it, because
SUBSYSTEM_VERSION=5.00 in src\hlpr\sources overrides the subsystem whatever
the target. So two things are read, each refused on its own:

  - **The build log** (src\hlpr\build<fre|chk|chk_qemu>.log): the kit's own
    line "BUILDMSG: _NT_TARGET_VERSION SET TO WINXP" must be there, and every
    library the link names must come from lib\wxp\i386 - except the one the
    kit adds for a driver whose subsystem is below 6.01,
    lib\crt\i386\safecrtnt.lib, admitted by that name alone (record 18
    section 15.10). A wlh, win7 or wnet library path is
    refused by name, and so is any other directory.
  - **The image's header** (link /dump /headers): subsystem Native at version
    5.00, section alignment 0x80 (the WXP driver default) and a non-zero
    checksum, so a toolchain change cannot move those fields unseen. That is
    all the header shows.

scripts\build-driver.cmd runs it after every helper link. -SelfTest proves
the rules on synthetic logs and headers: a valid WXP subsystem 5.00 build
(accepted), the same with a lib\wlh\i386 library (refused), with the WINXP
line missing (refused), and each header field moved (refused).

.PARAMETER LogPath
The helper build's log.

.PARAMETER Image
The linked xhcihlpr.sys.

.PARAMETER Dumper
WDK 7.1's x86 link.exe, run as `link /dump /headers`. Defaults to
tools\WinDDK71\bin\x86\x86\link.exe, or WDKROOT's.

.PARAMETER SelfTest
Run the rules on synthetic input and stop.
#>

[CmdletBinding()]
param(
    [string]$LogPath = "",
    [string]$Image = "",
    [string]$Dumper = "",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

# The log's refusals, one string each; none is a pass.
function Get-HelperLogErrors {
    param([string[]]$Lines)

    $errors = @()
    $targetLine = @($Lines | Where-Object { $_ -match "BUILDMSG:\s*_NT_TARGET_VERSION SET TO WINXP\s*$" })
    if ($targetLine.Count -eq 0) {
        $errors += "the build log lacks the kit's 'BUILDMSG: _NT_TARGET_VERSION SET TO WINXP' line - the helper was not built for the WXP target (record 18 section 15.10)"
    }
    # Every library operand, quoted or not (a quoted one may hold spaces),
    # and every /DEFAULTLIB: argument, with or without its extension; a
    # /NODEFAULTLIB: exclusion links nothing and is passed over (task
    # 38.3).
    $libs = @()
    foreach ($line in $Lines) {
        $text = ($line -replace "^\d+>", "").Trim()
        foreach ($m in [regex]::Matches($text, '(?:[^\s"]|"[^"]*")+')) {
            $op = $m.Value.Replace('"', '')
            if ($op -match '(?i)^[/-]NODEFAULTLIB') { continue }
            if ($op -match '(?i)^[/-]DEFAULTLIB:(.+)$') {
                $lib = $Matches[1]
                if ($lib -notmatch '(?i)\.lib$') { $lib = $lib + ".lib" }
                $libs += $lib
                continue
            }
            if ($op -match '(?i)\.lib$' -and $op -notmatch '^[/-]') {
                $libs += $op
            }
        }
    }
    if ($libs.Count -eq 0) {
        $errors += "the build log names no library - no link line was read"
    }
    foreach ($lib in $libs) {
        $norm = $lib.Replace("/", "\").ToLowerInvariant()
        $dir = Split-Path -Parent $norm
        $leaf = Split-Path -Leaf $norm
        if ($dir -match "\\lib\\wxp\\i386$") {
            continue
        }
        if ($dir -match "\\lib\\crt\\i386$" -and $leaf -eq "safecrtnt.lib") {
            continue
        }
        if ($norm -match "\\lib\\(wlh|win7|wnet)\\") {
            $errors += "the link names $lib, a $($Matches[1]) library - the helper links lib\wxp\i386 alone (record 18 section 15.10)"
        } else {
            $errors += "the link names $lib, outside lib\wxp\i386 and not the admitted lib\crt\i386\safecrtnt.lib"
        }
    }
    return $errors
}

# The log tied to the image (task 38.3): its link wrote this
# very file (/out: names it) and the log was finished no earlier than the
# image, so a log of another build cannot vouch for it.
function Get-HelperLogTieErrors {
    param([string[]]$Lines, [string]$ImagePath, [datetime]$LogTime, [datetime]$ImageTime)

    $errors = @()
    $want = [System.IO.Path]::GetFullPath($ImagePath).ToLowerInvariant()
    $outs = @()
    foreach ($line in $Lines) {
        $text = ($line -replace "^\d+>", "").Trim()
        if ($text -match '(?i)^/out:"?([^"]+?)"?\s*$') {
            $outs += [System.IO.Path]::GetFullPath($Matches[1]).ToLowerInvariant()
        }
    }
    if ($outs.Count -ne 1 -or $outs[0] -ne $want) {
        $errors += "the build log's link wrote '$($outs -join ', ')', not '$ImagePath' - it is not this image's log"
    }
    if ($LogTime -lt $ImageTime.AddSeconds(-2)) {
        $errors += "the build log is older than the image - the image was linked after it, by another build"
    }
    return $errors
}

# The header's refusals, from `link /dump /headers` text.
function Get-HelperHeaderErrors {
    param([string[]]$Lines)

    $errors = @()
    $subsystem = @($Lines | Where-Object { $_ -match "^\s*([0-9A-Fa-f]+)\s+subsystem\s+\(" })
    if ($subsystem.Count -ne 1 -or $subsystem[0] -notmatch "^\s*1\s+subsystem\s+\(Native\)") {
        $errors += "the image's subsystem is not Native"
    }
    $version = @($Lines | Where-Object { $_ -match "^\s*(\S+)\s+subsystem version\s*$" } | ForEach-Object { $Matches[1] })
    if ($version.Count -ne 1 -or $version[0] -ne "5.00") {
        $errors += "the image's subsystem version is '$($version -join ',')', not 5.00 (src\hlpr\sources, SUBSYSTEM_VERSION)"
    }
    $align = @($Lines | Where-Object { $_ -match "^\s*([0-9A-Fa-f]+)\s+section alignment\s*$" } | ForEach-Object { $Matches[1] })
    if ($align.Count -ne 1 -or $align[0] -ne "80") {
        $errors += "the image's section alignment is '$($align -join ',')', not 0x80 (the WXP driver default)"
    }
    $sum = @($Lines | Where-Object { $_ -match "^\s*([0-9A-Fa-f]+)\s+checksum\s*$" } | ForEach-Object { $Matches[1] })
    if ($sum.Count -ne 1 -or [Convert]::ToUInt32($sum[0], 16) -eq 0) {
        $errors += "the image's checksum is zero or unread"
    }
    return $errors
}

if ($SelfTest) {
    . (Join-Path $PSScriptRoot "test-harness.ps1")
    $kit = "1>d:\x\tools\winddk71\lib"
    $good = @(
        "1>BUILDMSG: _NT_TARGET_VERSION SET TO WINXP",
        "1>/subsystem:native,5.00",
        "$kit\wxp\i386\BufferOverflowK.lib ",
        "1>D:\x\tools\WinDDK71\lib\crt\i386\safecrtnt.lib ",
        "$kit\wxp\i386\ntoskrnl.lib ",
        "$kit\wxp\i386\wdmsec.lib "
    )
    $e = @(Get-HelperLogErrors -Lines $good)
    Assert-True ($e.Count -eq 0) "a WXP subsystem 5.00 build log is accepted: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "$kit\wlh\i386\wdmsec.lib "))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "wlh library") "a lib\wlh\i386 library is refused by name: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "$kit\win7\i386\ntoskrnl.lib "))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "win7 library") "a lib\win7\i386 library is refused by name: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "$kit\wnet\i386\hal.lib "))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "wnet library") "a lib\wnet\i386 library is refused by name: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>d:\elsewhere\other.lib "))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "outside lib") "a library from any other directory is refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>D:\x\tools\WinDDK71\lib\crt\i386\other.lib "))
    Assert-True ($e.Count -eq 1) "lib\crt\i386 admits safecrtnt.lib alone: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>""$kit\wlh\i386\wdmsec.lib"" "))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "wlh library") "a quoted lib\wlh\i386 library is refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>""d:\Program Files\kit\lib\win7\i386\hal.lib"" "))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "win7 library") "a quoted path with spaces is read whole and refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>/DEFAULTLIB:d:\x\tools\winddk71\lib\wnet\i386\ntoskrnl.lib"))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "wnet library") "a /DEFAULTLIB: library is read and refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>/DEFAULTLIB:d:\x\tools\winddk71\lib\wlh\i386\wdmsec"))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "wlh library") "an extensionless /DEFAULTLIB: library is read and refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>/DEFAULTLIB:""d:\Program Files\kit\lib\wlh\i386\wdmsec"""))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "wlh library") "a quoted extensionless /DEFAULTLIB: library is read and refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines ($good + "1>/NODEFAULTLIB:libcmt.lib" + "1>/NODEFAULTLIB:""c:\a b\x.lib"""))
    Assert-True ($e.Count -eq 0) "a /NODEFAULTLIB: exclusion links nothing and is not refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines @($good | Where-Object { $_ -notmatch "BUILDMSG" }))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "WINXP") "a log without the WINXP line is refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines @("1>BUILDMSG: _NT_TARGET_VERSION SET TO WIN7", "$kit\wxp\i386\ntoskrnl.lib "))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "WINXP") "a WIN7 target line is refused: $($e -join '; ')"
    $e = @(Get-HelperLogErrors -Lines @("1>BUILDMSG: _NT_TARGET_VERSION SET TO WINXP"))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "no library") "a log with no link line is refused: $($e -join '; ')"

    $hdr = @(
        "              80 section alignment",
        "              80 file alignment",
        "            6.01 operating system version",
        "            5.00 subsystem version",
        "            9AFA checksum",
        "               1 subsystem (Native)"
    )
    $e = @(Get-HelperHeaderErrors -Lines $hdr)
    Assert-True ($e.Count -eq 0) "a native 5.00 header with 0x80 alignment and a checksum is accepted: $($e -join '; ')"
    $e = @(Get-HelperHeaderErrors -Lines ($hdr -replace "5\.00 subsystem version", "5.01 subsystem version"))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "5.00") "subsystem 5.01 is refused: $($e -join '; ')"
    $e = @(Get-HelperHeaderErrors -Lines ($hdr -replace "^(\s*)80 section", '${1}1000 section'))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "alignment") "another section alignment is refused: $($e -join '; ')"
    $e = @(Get-HelperHeaderErrors -Lines ($hdr -replace "9AFA checksum", "0 checksum"))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "checksum") "a zero checksum is refused: $($e -join '; ')"
    $e = @(Get-HelperHeaderErrors -Lines ($hdr -replace "1 subsystem \(Native\)", "3 subsystem (Windows CUI)"))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "Native") "a subsystem other than Native is refused: $($e -join '; ')"

    $img = "d:\x\src\hlpr\objfre\i386\xhcihlpr.sys"
    $t0 = [datetime]"2026-10-14T00:00:00"
    $e = @(Get-HelperLogTieErrors -Lines @("1>/out:$img") -ImagePath $img -LogTime $t0.AddSeconds(1) -ImageTime $t0)
    Assert-True ($e.Count -eq 0) "a log whose link wrote this image, finished after it, vouches for it: $($e -join '; ')"
    $e = @(Get-HelperLogTieErrors -Lines @("1>/out:d:\x\src\hlpr\objchk\i386\xhcihlpr.sys") -ImagePath $img -LogTime $t0.AddSeconds(1) -ImageTime $t0)
    Assert-True ($e.Count -eq 1 -and $e[0] -match "not this image") "a log of another image is refused: $($e -join '; ')"
    $e = @(Get-HelperLogTieErrors -Lines @("1>/out:$img") -ImagePath $img -LogTime $t0 -ImageTime $t0.AddMinutes(5))
    Assert-True ($e.Count -eq 1 -and $e[0] -match "older than the image") "a log older than the image is refused: $($e -join '; ')"

    Write-Host ""
    if ($script:failures.Count -gt 0) {
        Write-Host ("helper build check: {0} check(s), {1} FAILED." -f $script:checks, $script:failures.Count) -ForegroundColor Red
        exit 1
    }
    Write-Host ("helper build check: {0} check(s), 0 failures." -f $script:checks) -ForegroundColor Green
    exit 0
}

if ($LogPath -eq "" -or $Image -eq "") {
    Write-Err "pass -LogPath and -Image, or -SelfTest."
    exit 1
}
if (-not (Test-Path -LiteralPath $LogPath)) {
    Write-Err "no build log at '$LogPath'."
    exit 1
}
if (-not (Test-Path -LiteralPath $Image)) {
    Write-Err "no image at '$Image'."
    exit 1
}
if ($Dumper -eq "") {
    $wdk = Join-Path (Get-RepoRoot) "tools\WinDDK71"
    if ($env:WDKROOT) {
        $wdk = $env:WDKROOT
    }
    $Dumper = Join-Path $wdk "bin\x86\x86\link.exe"
}
if (-not (Test-Path -LiteralPath $Dumper)) {
    Write-Err "no WDK 7.1 link.exe at '$Dumper' to read the image's header."
    exit 1
}

$logLines = @(Get-Content -LiteralPath $LogPath)
$errors = @(Get-HelperLogErrors -Lines $logLines)
$errors += @(Get-HelperLogTieErrors -Lines $logLines -ImagePath $Image `
    -LogTime (Get-Item -LiteralPath $LogPath).LastWriteTime `
    -ImageTime (Get-Item -LiteralPath $Image).LastWriteTime)
$saved = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    $headers = @(& $Dumper /dump /headers $Image 2>&1 | ForEach-Object { "$_" })
} finally {
    $ErrorActionPreference = $saved
}
$errors += @(Get-HelperHeaderErrors -Lines $headers)
if ($errors.Count -gt 0) {
    foreach ($e in $errors) {
        Write-Err $e
    }
    Write-Err "the helper build at '$Image' is not the WXP, subsystem 5.00 build design record 18 section 15.10 decided. Do not deploy it."
    exit 1
}
Write-Ok "xhcihlpr.sys: built for WXP from lib\wxp\i386, subsystem Native 5.00, section alignment 0x80, checksummed"
exit 0
