<#
.SYNOPSIS
Setup-engine gate for src\uas\xhciuas.inf, the UAS class driver's INF
(roadmap-hcd.md task 31-A.2).

.DESCRIPTION
check-inf.ps1 is built around xhci98.inf's own facts - the controller and
root-hub roles, the OS-supplied files fetched through LayoutFile, the per-device
registry values, the NT 6.x models section - and its rules would refuse a
second INF that has none of them for having none of them. This is the second
INF's gate: the same failure classes, restated for a class driver that needs no
OS file, each rule named as check-inf.ps1 names its families.

  FILE-*   ASCII only, no BOM, CRLF only (Windows 98's 16-bit parser).
  W98-*    The 28-character section-name limit, no duplicate section (first
           wins on Windows 98), no copy section sent to dirid 12 (it is
           \Windows\System\Iosubsys there), 8.3 file names.
  BOTH-*   $CHICAGO$, the USB class and its GUID, one DriverVer equal to
           src\xhci_version.h's date and version, defined %strings%,
           sections that every reference names, SourceDisksFiles carrying
           exactly xhciuas.sys and xhciuas.inf.
  PATH-*   Under -Arch x86 (src\uas\xhciuas.inf): one undecorated models
           section, and every model in it reaching both install paths: the undecorated section with DevLoader *NTKERN
           and NTMPDriver xhciuas.sys, and the .NTx86 section whose .Services
           adds the xhciuas kernel service (type 1, demand start, normal error
           control, %12%\xhciuas.sys), each copying xhciuas.sys to
           System32\Drivers. Under -Arch amd64 (src\uas\xhciuas-amd64.inf,
           the 64-bit package's): [Manufacturer] decorated NTamd64 and
           nothing else (PATH-MFGDEC), the .NTamd64 install and .Services
           sections, and no undecorated models, install or right-click
           section at all (PATH-NO9X) - one is what a 32-bit engine would
           fall back to, putting an amd64 binary on a 32-bit machine.
  UAS-*    The models bind USB\Class_08&SubClass_06&Prot_62 and nothing else:
           a Bulk-Only id (Prot_50) here would put this driver on a device the
           bus chose Bulk-Only for (task 31-A.3). No file of Microsoft's is
           copied or listed anywhere. UAS-FILTER: the Windows 98 install's
           [<install>.HW] writes HKR,,upperfilters,0,"USBNTMAP.SYS" and
           nothing else - NUSB's USBSTOR.INF value, without which NUSB's
           port driver fails every unit (Code 10) - naming the file, never
           copying it; no .NTx86.HW or .NTamd64.HW section exists, so the NT
           path is unchanged. UAS-TMPNAME (x86): xhciuas.sys is copied with
           no temporary-file name, which on Windows 98 routes the copy
           through WININIT.INI and a restart even at a first install.
  PKG-*    With -PackageDir: every [SourceDisksFiles] entry is in the staged
           package (PKG-LAYOUT), and no Microsoft file is, under any of the
           names UAS-MSFILE knows, at the root or below it (PKG-MSFILE).

-SelfTest mutates both committed INFs in memory, one rule at a time, and fails
unless the gate refuses every mutation and passes the original - the gate's own
regression suite, run by scripts\build-driver.cmd before the gate itself. It
also stages stand-in packages in a temporary directory (removed afterwards):
the four files the packager stages must pass, and a USBNTMAP.SYS at the root,
in a subdirectory, hidden, or inside a hidden subdirectory must be refused by
PKG-MSFILE.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\inf-gate\check-uas-inf.ps1
#>

[CmdletBinding()]
param(
    [string]$InfPath = "",
    [string]$PackageDir = "",
    [ValidateSet("x86", "amd64")]
    [string]$Arch = "x86",
    [switch]$SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")

$repo = Get-RepoRoot
$infFor = @{ x86 = (Join-Path $repo "src\uas\xhciuas.inf"); amd64 = (Join-Path $repo "src\uas\xhciuas-amd64.inf") }
if ($InfPath -eq "") { $InfPath = $infFor[$Arch] }
$versionHeader = Join-Path $repo "src\xhci_version.h"

$binary = "xhciuas.sys"
$service = "xhciuas"
$uasId = "USB\Class_08&SubClass_06&Prot_62"
$msFiles = @("usbd.sys", "usbhub.sys", "usbport.sys", "usbui.dll", "usbhub20.sys",
             "usbstor.sys", "disk.sys", "classpnp.sys", "ntmap.sys", "usbntmap.sys",
             "usbmphlp.pdr", "uaspstor.sys", "usbccgp.sys")

function Read-InfText {
    param([string]$Text)
    # Sections in order, each a list of (key, value, raw) entries, comments
    # and blank lines dropped. A quoted ';' is not a comment.
    $sections = New-Object System.Collections.ArrayList
    $current = $null
    foreach ($rawLine in ($Text -split "`r`n")) {
        $line = $rawLine
        $inQuote = $false
        for ($i = 0; $i -lt $line.Length; $i++) {
            if ($line[$i] -eq '"') { $inQuote = -not $inQuote }
            elseif ($line[$i] -eq ';' -and -not $inQuote) { $line = $line.Substring(0, $i); break }
        }
        $line = $line.Trim()
        if ($line -eq "") { continue }
        if ($line -match '^\[(.+)\]$') {
            $current = @{ Name = $matches[1].Trim(); Lines = (New-Object System.Collections.ArrayList) }
            [void]$sections.Add($current)
            continue
        }
        if ($null -eq $current) { continue }
        $key = $line; $value = ""
        $eq = $line.IndexOf('=')
        if ($eq -ge 0) { $key = $line.Substring(0, $eq).Trim(); $value = $line.Substring($eq + 1).Trim() }
        [void]$current.Lines.Add(@{ Key = $key; Value = $value; Raw = $line })
    }
    return $sections
}

function Test-UasInfText {
    param([byte[]]$Bytes, [string]$HeaderText, [string]$Package, [string]$Profile = "x86")

    $ntSuffix = if ($Profile -eq "amd64") { ".NTamd64" } else { ".NTx86" }

    $fail = New-Object System.Collections.ArrayList
    $add = { param($rule, $msg) [void]$fail.Add("$rule $msg") }

    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        & $add "FILE-BOM" "the file starts with a UTF-8 BOM."
    }
    for ($i = 0; $i -lt $Bytes.Length; $i++) {
        $b = $Bytes[$i]
        if ($b -ge 0x80) { & $add "FILE-ASCII" ("byte 0x{0:X2} at offset {1}." -f $b, $i); break }
        if ($b -lt 0x20 -and $b -ne 9 -and $b -ne 10 -and $b -ne 13) { & $add "FILE-CTRL" ("control byte 0x{0:X2} at offset {1}." -f $b, $i); break }
        if ($b -eq 10 -and ($i -eq 0 -or $Bytes[$i - 1] -ne 13)) { & $add "FILE-EOL" "a bare LF at offset $i; Windows 98's parser wants CRLF."; break }
        if ($b -eq 13 -and ($i + 1 -ge $Bytes.Length -or $Bytes[$i + 1] -ne 10)) { & $add "FILE-EOL" "a bare CR at offset $i."; break }
    }
    $text = [System.Text.Encoding]::ASCII.GetString($Bytes)
    $sections = Read-InfText $text
    $byName = @{}
    foreach ($s in $sections) {
        $k = $s.Name.ToLowerInvariant()
        if ($byName.ContainsKey($k)) { & $add "W98-DUP" "section [$($s.Name)] appears twice; Windows 98 reads the first only."; continue }
        $byName[$k] = $s
        if ($s.Name.Length -gt 28) { & $add "W98-SECTLEN" "section [$($s.Name)] is longer than 28 characters." }
    }
    $get = { param($name) $k = $name.ToLowerInvariant(); if ($byName.ContainsKey($k)) { $byName[$k] } else { $null } }
    $directive = { param($sect, $key) @($sect.Lines | Where-Object { $_.Key -ieq $key } | ForEach-Object { $_.Value }) }

    # [Strings] and every %token%.
    $strings = & $get "Strings"
    $defined = @{}
    if ($null -eq $strings) { & $add "BOTH-STRINGS" "no [Strings] section." }
    else { foreach ($l in $strings.Lines) { $defined[$l.Key.ToLowerInvariant()] = $true } }
    foreach ($m in [regex]::Matches($text, '%([A-Za-z_][A-Za-z0-9_]*)%')) {
        if (-not $defined.ContainsKey($m.Groups[1].Value.ToLowerInvariant())) {
            & $add "BOTH-STRINGS" "%$($m.Groups[1].Value)% is used and not defined."
        }
    }

    # [Version].
    $ver = & $get "Version"
    if ($null -eq $ver) { & $add "BOTH-VERSION" "no [Version] section." ; return $fail }
    if (@(& $directive $ver "Signature") -notcontains '"$CHICAGO$"') { & $add "BOTH-SIGNATURE" 'Signature must be "$CHICAGO$".' }
    if (@(& $directive $ver "Class") -notcontains "USB") { & $add "BOTH-CLASS" "Class must be USB." }
    if (@(& $directive $ver "ClassGUID") -notcontains "{36FC9E60-C465-11CF-8056-444553540000}") { & $add "BOTH-CLASS" "ClassGUID must be the USB class's." }
    $dv = @(& $directive $ver "DriverVer")
    if ($dv.Count -ne 1 -or $dv[0] -notmatch '^(\d{2}/\d{2}/\d{4})\s*,\s*(\d+\.\d+\.\d+\.\d+)$') {
        & $add "BOTH-VERSION" "[Version] needs exactly one DriverVer of the form MM/DD/YYYY,a.b.c.d."
    } else {
        $date = $matches[1]; $number = $matches[2]
        if ($HeaderText -match '(?m)^\s*#define\s+XHCI_VER_STR\s+"([^"]+)"') {
            if ($matches[1] -ne $number) { & $add "BOTH-VERSION" "DriverVer version $number is not src\xhci_version.h's $($matches[1])." }
        } else { & $add "BOTH-VERSION" "src\xhci_version.h has no XHCI_VER_STR." }
        if ($HeaderText -match '(?m)^\s*#define\s+XHCI_DRIVERVER_DATE\s+"([^"]+)"') {
            if ($matches[1] -ne $date) { & $add "BOTH-VERSION" "DriverVer date $date is not src\xhci_version.h's $($matches[1])." }
        } else { & $add "BOTH-VERSION" "src\xhci_version.h has no XHCI_DRIVERVER_DATE." }
    }
    if (@(& $directive $ver "LayoutFile").Count -ne 0) { & $add "UAS-LAYOUT" "LayoutFile: this driver needs no OS-supplied file." }

    # [DestinationDirs].
    $dest = & $get "DestinationDirs"
    $destOf = @{}
    if ($null -eq $dest) { & $add "BOTH-DEST" "no [DestinationDirs] section." }
    else { foreach ($l in $dest.Lines) { $destOf[$l.Key.ToLowerInvariant()] = ($l.Value -replace '\s', '') } }
    foreach ($k in @($destOf.Keys)) {
        if ($destOf[$k] -match '^12(,|$)') { & $add "W98-DIRID12" "[$k] goes to dirid 12, which Windows 98 resolves to \Windows\System\Iosubsys." }
    }

    # Every file named in a copy section, and where it goes.
    $copied = @{}
    $checkCopy = {
        param($list, $where)
        foreach ($name in ($list -split ',')) {
            $name = $name.Trim()
            if ($name -eq "") { continue }
            if ($name.StartsWith("@")) { & $add "BOTH-COPY" "$where copies a single file by @; name a section."; continue }
            $cs = & $get $name
            if ($null -eq $cs) { & $add "BOTH-XREF" "$where names [$name], which does not exist."; continue }
            if (-not $destOf.ContainsKey($name.ToLowerInvariant())) { & $add "BOTH-DEST" "[$name] is not in [DestinationDirs]." }
            foreach ($l in $cs.Lines) {
                $file = ($l.Raw -split ',')[0].Trim()
                if ($file -notmatch '^[A-Za-z0-9_\-]{1,8}(\.[A-Za-z0-9_]{1,3})?$') { & $add "W98-83" "[$name] copies '$file', not an 8.3 name." }
                $tmpField = @($l.Raw -split ',')
                if ($Profile -eq "x86" -and $file -ieq $binary -and $tmpField.Count -ge 3 -and $tmpField[2].Trim() -ne "") {
                    & $add "UAS-TMPNAME" "[$name] gives $binary the temporary name '$($tmpField[2].Trim())': Windows 98 then copies through WININIT.INI and asks for a restart at a first install, when the driver is not loaded."
                }
                $copied[$file.ToLowerInvariant()] = $destOf[$name.ToLowerInvariant()]
                $file.ToLowerInvariant()
            }
        }
    }
    # $checkCopy emits the files its CopyFiles list copies, so each install
    # path is held to its OWN set: $copied is the union over every path, and
    # a file one path copies says nothing about another (Codex review of
    # 31-A.2, round 1).

    # [Manufacturer] and the models.
    $mfg = & $get "Manufacturer"
    $modelSections = @()
    if ($null -eq $mfg -or $mfg.Lines.Count -ne 1) { & $add "PATH-MFG" "[Manufacturer] must carry exactly one models line." }
    else {
        $fields = @($mfg.Lines[0].Value -split ',' | ForEach-Object { $_.Trim() })
        if ($Profile -eq "amd64") {
            if ($fields.Count -ne 2 -or $fields[1] -ine "NTamd64") { & $add "PATH-MFGDEC" "[Manufacturer] must be decorated NTamd64 and nothing else: it serves NT 5.2 x64 and Vista and 7 x64 alike, with no OS file to keep off an NT 6.x queue." }
            if ($null -ne (& $get $fields[0])) { & $add "PATH-NO9X" "an undecorated models section [$($fields[0])] is one a 32-bit engine would fall back to." }
            $modelSections = @($fields[0] + ".NTamd64")
        } else {
            if ($fields.Count -ne 1) { & $add "PATH-MFGDEC" "[Manufacturer] must be undecorated: the .NTx86 install path serves every NT target, and an NT 6.x models section would be a third path with nothing to do." }
            $modelSections = @($fields[0])
        }
    }
    $installs = @()
    foreach ($ms in $modelSections) {
        $sect = & $get $ms
        if ($null -eq $sect) { & $add "BOTH-XREF" "models section [$ms] does not exist."; continue }
        if ($sect.Lines.Count -eq 0) { & $add "UAS-ID" "models section [$ms] is empty." }
        foreach ($l in $sect.Lines) {
            $parts = @($l.Value -split ',' | ForEach-Object { $_.Trim() })
            if ($parts.Count -lt 2) { & $add "BOTH-XREF" "model line '$($l.Raw)' names no id."; continue }
            $installs += $parts[0]
            foreach ($id in $parts[1..($parts.Count - 1)]) {
                if ($id -ine $uasId) { & $add "UAS-ID" "model binds '$id'; only $uasId may be bound (a Prot_50 id would take a device the bus chose Bulk-Only for)." }
            }
        }
    }

    foreach ($inst in ($installs | Select-Object -Unique)) {
        # Windows 98, or on amd64 its absence.
        $s9 = & $get $inst
        if ($Profile -eq "amd64") {
            if ($null -ne $s9) { & $add "PATH-NO9X" "an undecorated [$inst] is one a 32-bit engine would fall back to." }
        } elseif ($null -eq $s9) { & $add "PATH-9X" "no undecorated [$inst] for Windows 98." }
        else {
            $reg = @(& $directive $s9 "AddReg")
            $haveLoader = $false; $haveMp = $false
            foreach ($r in ($reg -join ',' -split ',')) {
                $rs = & $get $r.Trim()
                if ($null -eq $rs) { if ($r.Trim() -ne "") { & $add "BOTH-XREF" "[$inst] AddReg names [$($r.Trim())], which does not exist." }; continue }
                foreach ($l in $rs.Lines) {
                    $f = @($l.Raw -split ',' | ForEach-Object { $_.Trim() })
                    if ($f.Count -ge 5 -and $f[0] -ieq "HKR" -and $f[2] -ieq "DevLoader" -and $f[4] -ieq "*NTKERN") { $haveLoader = $true }
                    if ($f.Count -ge 5 -and $f[0] -ieq "HKR" -and $f[2] -ieq "NTMPDriver" -and $f[4] -ieq $binary) { $haveMp = $true }
                }
            }
            if (-not $haveLoader) { & $add "PATH-9X" "[$inst] writes no HKR,,DevLoader,,*NTKERN." }
            if (-not $haveMp) { & $add "PATH-9X" "[$inst] writes no HKR,,NTMPDriver,,$binary." }
            $own = @(& $checkCopy ((& $directive $s9 "CopyFiles") -join ',') "[$inst]")
            if ($own -notcontains $binary) { & $add "PATH-9X" "[$inst] does not copy $binary." }
            # The unit's port driver on Windows 98 (usbntmap.inf's
            # USBMPHLP.PDR -> USBNTMAP.SYS) serves only a unit USBNTMAP.SYS
            # has filtered, and it filters the units of a parent it sits on
            # as an upper filter: NUSB's own USBSTOR.INF value, exactly.
            $shw = & $get "$inst.HW"
            if ($null -eq $shw) { & $add "UAS-FILTER" "no [$inst.HW]: Windows 98 needs HKR,,upperfilters,0,`"USBNTMAP.SYS`" on the device's hardware key, or every unit is Code 10." }
            else {
                $haveFilter = $false
                foreach ($r in ((& $directive $shw "AddReg") -join ',' -split ',')) {
                    if ($r.Trim() -eq "") { continue }
                    $rs = & $get $r.Trim()
                    if ($null -eq $rs) { & $add "BOTH-XREF" "[$inst.HW] AddReg names [$($r.Trim())], which does not exist."; continue }
                    foreach ($l in $rs.Lines) {
                        $f = @($l.Raw -split ',' | ForEach-Object { $_.Trim() })
                        if ($f.Count -eq 5 -and $f[0] -ieq "HKR" -and $f[1] -eq "" -and $f[2] -ieq "upperfilters" -and $f[3] -eq "0" -and $f[4] -ieq '"USBNTMAP.SYS"') { $haveFilter = $true }
                        else { & $add "UAS-FILTER" "[$($rs.Name)] writes '$($l.Raw)'; the hardware key carries the USBNTMAP.SYS upper filter and nothing else." }
                    }
                }
                foreach ($d in @($shw.Lines | Where-Object { $_.Key -ine "AddReg" })) { & $add "UAS-FILTER" "[$inst.HW] carries '$($d.Raw)'; it names the filter only (the file is Microsoft's, placed by NUSB)." }
                if (-not $haveFilter) { & $add "UAS-FILTER" "[$inst.HW] writes no HKR,,upperfilters,0,`"USBNTMAP.SYS`"." }
            }
        }
        if ($Profile -eq "amd64" -and $null -ne (& $get "$inst.HW")) { & $add "PATH-NO9X" "an undecorated [$inst.HW] is one a 32-bit engine would read." }
        # The NT path is unchanged: disk.sys takes the units with no filter,
        # and a filter named there with no service would fail the stack.
        if ($null -ne (& $get "$inst$ntSuffix.HW")) { & $add "UAS-FILTER" "[$inst$ntSuffix.HW]: the NT targets take no filter." }
        # NT.
        $snt = & $get "$inst$ntSuffix"
        if ($null -eq $snt) { & $add "PATH-NT" "no [$inst$ntSuffix] for the NT targets." }
        else {
            $own = @(& $checkCopy ((& $directive $snt "CopyFiles") -join ',') "[$inst$ntSuffix]")
            if ($own -notcontains $binary) { & $add "PATH-NT" "[$inst$ntSuffix] does not copy $binary." }
        }
        $ssv = & $get "$inst$ntSuffix.Services"
        if ($null -eq $ssv) { & $add "PATH-NT" "no [$inst$ntSuffix.Services]: the NT targets would install a devnode with no service." }
        else {
            $as = @(& $directive $ssv "AddService")
            if ($as.Count -ne 1) { & $add "PATH-NT" "[$inst$ntSuffix.Services] needs exactly one AddService." }
            else {
                $f = @($as[0] -split ',' | ForEach-Object { $_.Trim() })
                if ($f.Count -lt 3 -or $f[0] -ine $service -or ([Convert]::ToInt32(($f[1] -replace '^0x', ''), 16) -band 2) -eq 0) {
                    & $add "PATH-NT" "AddService must be $service with flag 0x2 (the function driver) and a service section."
                } else {
                    $svc = & $get $f[2]
                    if ($null -eq $svc) { & $add "BOTH-XREF" "AddService names [$($f[2])], which does not exist." }
                    else {
                        if (@(& $directive $svc "ServiceType") -notcontains "1") { & $add "PATH-NT" "ServiceType must be 1 (kernel driver)." }
                        if (@(& $directive $svc "StartType") -notcontains "3") { & $add "PATH-NT" "StartType must be 3 (demand)." }
                        if (@(& $directive $svc "ErrorControl") -notcontains "1") { & $add "PATH-NT" "ErrorControl must be 1 (normal)." }
                        if (@(& $directive $svc "ServiceBinary") -notcontains "%12%\$binary") { & $add "PATH-NT" "ServiceBinary must be %12%\$binary." }
                    }
                }
            }
        }
    }
    if ($Profile -eq "amd64" -and $null -ne (& $get "DefaultInstall")) {
        & $add "PATH-NO9X" "an undecorated [DefaultInstall] is one a 32-bit engine would run."
    }
    foreach ($d in @("DefaultInstall", "DefaultInstall$ntSuffix")) {
        $ds = & $get $d
        if ($null -ne $ds) { $null = @(& $checkCopy ((& $directive $ds "CopyFiles") -join ',') "[$d]") }
    }
    if ($copied.ContainsKey($binary) -and $copied[$binary] -ne "10,System32\Drivers") {
        & $add "BOTH-DEST" "$binary goes to '$($copied[$binary])', not 10,System32\Drivers."
    }

    # SourceDisks*.
    $sdf = & $get "SourceDisksFiles"
    $listed = @()
    if ($null -eq $sdf) { & $add "BOTH-SDF" "no [SourceDisksFiles] section." }
    else { $listed = @($sdf.Lines | ForEach-Object { $_.Key.ToLowerInvariant() }) }
    if ($null -eq (& $get "SourceDisksNames")) { & $add "BOTH-SDF" "no [SourceDisksNames] section." }
    foreach ($want in @($binary, "xhciuas.inf")) {
        if ($listed -notcontains $want) { & $add "BOTH-SDF" "[SourceDisksFiles] does not list $want." }
    }
    foreach ($f in $listed) {
        if (@($binary, "xhciuas.inf") -notcontains $f) { & $add "BOTH-SDF" "[SourceDisksFiles] lists '$f', which the package does not carry." }
    }
    foreach ($ms in $msFiles) {
        if ($copied.ContainsKey($ms) -or $listed -contains $ms) { & $add "UAS-MSFILE" "$ms is Microsoft's; this INF names no file of theirs." }
    }

    if ($Package -ne "") {
        foreach ($f in $listed) {
            if (-not (Test-Path -LiteralPath (Join-Path $Package $f))) { & $add "PKG-LAYOUT" "[SourceDisksFiles] lists '$f' and the package has none." }
        }
        # The INF names USBNTMAP.SYS as a filter and never ships it; the
        # media carries no Microsoft file (legal-provenance.md section 5).
        if (Test-Path -LiteralPath $Package) {
            foreach ($item in (Get-ChildItem -LiteralPath $Package -File -Recurse -Force)) {
                if ($msFiles -contains $item.Name.ToLowerInvariant()) {
                    & $add "PKG-MSFILE" "the staged package holds '$($item.Name)', a Microsoft file; the media carries none."
                }
            }
        }
    }
    return $fail
}

$headerText = if (Test-Path -LiteralPath $versionHeader) { [System.IO.File]::ReadAllText($versionHeader) } else { "" }

if ($SelfTest) {
    Write-Step "UAS INF gate self-tests"
    $bad = 0
    $total = 0
    foreach ($profile in @("x86", "amd64")) {
        $orig = [System.IO.File]::ReadAllBytes($infFor[$profile])
        $text = [System.Text.Encoding]::ASCII.GetString($orig)
        $base = @(Test-UasInfText -Bytes $orig -HeaderText $headerText -Package "" -Profile $profile)
        if ($base.Count -ne 0) {
            Write-Err ("the committed $profile INF fails its own gate:`n  " + ($base -join "`n  "))
            exit 1
        }
        if ($profile -eq "x86") {
            $cases = @(
                @{ Rule = "FILE-EOL";      Text = $text.Replace("`r`n[Strings]", "`n[Strings]") },
                @{ Rule = "FILE-ASCII";    Text = $text.Replace('Provider="Yeo', ('Provider="' + [char]0xE9 + 'Yeo')) },
                @{ Rule = "UAS-ID";        Text = $text.Replace("Prot_62", "Prot_50") },
                @{ Rule = "PATH-NT";       Text = $text.Replace("[Uas.Dev.NTx86.Services]", "[Uas.Dev.NTx86.Svc]") },
                @{ Rule = "PATH-NT";       Text = $text.Replace("[Uas.Dev.NTx86]`r`nCopyFiles=Uas.CopyFiles", "[Uas.Dev.NTx86]") },
                @{ Rule = "PATH-9X";       Text = $text.Replace("HKR,,NTMPDriver,,xhciuas.sys", "HKR,,NTMPDriver,,other.sys") },
                @{ Rule = "BOTH-VERSION";  Text = ($text -replace 'DriverVer=\d\d/\d\d/\d{4},[0-9.]+', 'DriverVer=01/01/2020,9.9.9.9') },
                @{ Rule = "W98-DIRID12";   Text = $text.Replace("Uas.CopyFiles=10,System32\Drivers", "Uas.CopyFiles=12") },
                @{ Rule = "W98-SECTLEN";   Text = $text.Replace("[Uas.AddService]", "[Uas.AddService.Very.Long.Section]").Replace("Uas.AddService`r`n", "Uas.AddService.Very.Long.Section`r`n").Replace(",Uas.AddService", ",Uas.AddService.Very.Long.Section") },
                @{ Rule = "UAS-MSFILE";    Text = $text.Replace("[Uas.CopyFiles]`r`nxhciuas.sys", "[Uas.CopyFiles]`r`nxhciuas.sys`r`nusbd.sys,,,16") },
                @{ Rule = "PATH-MFGDEC";   Text = $text.Replace("%Mfg%=UasModels", "%Mfg%=UasModels,NTx86.6.0") },
                @{ Rule = "BOTH-STRINGS";  Text = $text.Replace('UasDesc="', 'UasDescription="') },
                @{ Rule = "UAS-LAYOUT";    Text = $text.Replace("Provider=%Provider%", "Provider=%Provider%`r`nLayoutFile=layout.inf") },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace("[Uas.Dev.HW]", "[Uas.Dev.Hw2]") },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace("[Uas.Dev.HW]`r`nAddReg=Uas.HW.AddReg", "[Uas.Dev.HW]") },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace('upperfilters,0,"USBNTMAP.SYS"', 'lowerfilters,0,"USBNTMAP.SYS"') },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace('upperfilters,0,"USBNTMAP.SYS"', 'upperfilters,0,"USBSTOR.SYS"') },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace('upperfilters,0,"USBNTMAP.SYS"', 'upperfilters,0x00010000,"USBNTMAP.SYS"') },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace("[Uas.Dev.HW]`r`nAddReg=Uas.HW.AddReg", "[Uas.Dev.HW]`r`nAddReg=Uas.HW.AddReg`r`nCopyFiles=Uas.CopyFiles") },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace("[Uas.Dev.NTx86.Services]", "[Uas.Dev.NTx86.HW]`r`nAddReg=Uas.HW.AddReg`r`n`r`n[Uas.Dev.NTx86.Services]") },
                @{ Rule = "UAS-MSFILE";    Text = $text.Replace("[Uas.CopyFiles]`r`nxhciuas.sys", "[Uas.CopyFiles]`r`nxhciuas.sys`r`nusbntmap.sys") },
                @{ Rule = "UAS-TMPNAME";   Text = $text.Replace("[Uas.CopyFiles]`r`nxhciuas.sys", "[Uas.CopyFiles]`r`nxhciuas.sys,,xhciuas.tmp") }
            )
        } else {
            $cases = @(
                @{ Rule = "UAS-ID";        Text = $text.Replace("Prot_62", "Prot_50") },
                @{ Rule = "PATH-NT";       Text = $text.Replace("[Uas.Dev.NTamd64.Services]", "[Uas.Dev.NTamd64.Svc]") },
                @{ Rule = "PATH-NT";       Text = $text.Replace("[Uas.Dev.NTamd64]`r`nCopyFiles=Uas.CopyFiles", "[Uas.Dev.NTamd64]") },
                @{ Rule = "PATH-MFGDEC";   Text = $text.Replace("%Mfg%=UasModels,NTamd64", "%Mfg%=UasModels,NTamd64,NTamd64.6.0") },
                @{ Rule = "PATH-NO9X";     Text = $text.Replace("[UasModels.NTamd64]", "[UasModels]`r`n%UasDesc%=Uas.Dev,USB\Class_08&SubClass_06&Prot_62`r`n`r`n[UasModels.NTamd64]") },
                @{ Rule = "PATH-NO9X";     Text = $text.Replace("[Uas.Dev.NTamd64]", "[Uas.Dev]`r`nCopyFiles=Uas.CopyFiles`r`n`r`n[Uas.Dev.NTamd64]") },
                @{ Rule = "BOTH-VERSION";  Text = ($text -replace 'DriverVer=\d\d/\d\d/\d{4},[0-9.]+', 'DriverVer=01/01/2020,9.9.9.9') },
                @{ Rule = "UAS-MSFILE";    Text = $text.Replace("xhciuas.sys,,xhciuas.tmp", "xhciuas.sys,,xhciuas.tmp`r`nusbd.sys,,,16") },
                @{ Rule = "UAS-FILTER";    Text = $text.Replace("[Uas.Dev.NTamd64.Services]", "[Uas.Dev.NTamd64.HW]`r`nAddReg=Uas.HW.AddReg`r`n`r`n[Uas.HW.AddReg]`r`nHKR,,upperfilters,0,`"USBNTMAP.SYS`"`r`n`r`n[Uas.Dev.NTamd64.Services]") },
                @{ Rule = "PATH-NO9X";     Text = $text.Replace("[Uas.Dev.NTamd64.Services]", "[Uas.Dev.HW]`r`nAddReg=Uas.HW.AddReg`r`n`r`n[Uas.HW.AddReg]`r`nHKR,,upperfilters,0,`"USBNTMAP.SYS`"`r`n`r`n[Uas.Dev.NTamd64.Services]") }
            )
        }
        foreach ($c in $cases) {
            $total++
            $got = @(Test-UasInfText -Bytes ([System.Text.Encoding]::GetEncoding(28591).GetBytes($c.Text)) -HeaderText $headerText -Package "" -Profile $profile)
            $hit = @($got | Where-Object { $_.StartsWith($c.Rule + " ") })
            if ($hit.Count -eq 0) {
                $bad++
                Write-Err ("$profile mutation for $($c.Rule) was not refused by that rule. Got:`n  " + ($got -join "`n  "))
            } else {
                Write-Ok "$profile $($c.Rule) refuses its mutation"
            }
        }
        # Stand-in packages: the four files the packager stages must pass, and
        # a Microsoft file at the root, below it, hidden, or in a hidden
        # directory must be refused - the INF names USBNTMAP.SYS, and drift
        # could put it on the media.
        $pkgCases = @(
            @{ Name = "clean";      Sub = "";       HideFile = $false; HideDir = $false; Ms = $false },
            @{ Name = "root";       Sub = "";       HideFile = $false; HideDir = $false; Ms = $true },
            @{ Name = "subdir";     Sub = "osfiles"; HideFile = $false; HideDir = $false; Ms = $true },
            @{ Name = "hidden";     Sub = "";       HideFile = $true;  HideDir = $false; Ms = $true },
            @{ Name = "hidden-dir"; Sub = "osfiles"; HideFile = $false; HideDir = $true;  Ms = $true }
        )
        foreach ($pc in $pkgCases) {
            $total++
            $pkg = Join-Path ([System.IO.Path]::GetTempPath()) ("xhciuas-pkgtest-" + [Guid]::NewGuid().ToString("N"))
            try {
                New-Item -ItemType Directory -Path $pkg | Out-Null
                foreach ($f in @("xhci98.inf", "xhci98.sys", "xhciuas.sys", "xhciuas.inf")) { [System.IO.File]::WriteAllText((Join-Path $pkg $f), "stand-in") }
                if ($pc.Ms) {
                    $at = $pkg
                    if ($pc.Sub -ne "") { $at = Join-Path $pkg $pc.Sub; New-Item -ItemType Directory -Path $at | Out-Null }
                    $ms = Join-Path $at "USBNTMAP.SYS"
                    [System.IO.File]::WriteAllText($ms, "stand-in")
                    if ($pc.HideFile) { [System.IO.File]::SetAttributes($ms, [System.IO.FileAttributes]::Hidden) }
                    if ($pc.HideDir) { [System.IO.File]::SetAttributes($at, [System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::Hidden) }
                }
                $got = @(Test-UasInfText -Bytes $orig -HeaderText $headerText -Package $pkg -Profile $profile)
            } finally {
                Remove-Item -LiteralPath $pkg -Recurse -Force -ErrorAction SilentlyContinue
            }
            $hits = @($got | Where-Object { $_.StartsWith("PKG-MSFILE ") }).Count
            if ($pc.Ms -and $hits -eq 0) {
                $bad++
                Write-Err ("$profile package '$($pc.Name)' holding USBNTMAP.SYS was not refused by PKG-MSFILE. Got:`n  " + ($got -join "`n  "))
            } elseif (-not $pc.Ms -and $got.Count -ne 0) {
                $bad++
                Write-Err ("$profile package '$($pc.Name)' (the four staged files) was refused:`n  " + ($got -join "`n  "))
            } else {
                Write-Ok "$profile PKG-MSFILE package case '$($pc.Name)'"
            }
        }
    }
    if ($bad -ne 0) { exit 1 }
    Write-Ok "UAS INF gate self-tests PASSED ($total mutations)"
    exit 0
}

Write-Step "UAS INF gate ($Arch): $InfPath"
if (-not (Test-Path -LiteralPath $InfPath)) { Write-Err "no INF at '$InfPath'."; exit 1 }
$fails = @(Test-UasInfText -Bytes ([System.IO.File]::ReadAllBytes($InfPath)) -HeaderText $headerText -Package $PackageDir -Profile $Arch)
if ($fails.Count -ne 0) {
    foreach ($f in $fails) { Write-Err $f }
    Write-Err "UAS INF gate FAILED ($($fails.Count))"
    exit 1
}
Write-Ok "UAS INF gate PASSED"
exit 0
