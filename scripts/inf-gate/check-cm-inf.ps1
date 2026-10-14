<#
.SYNOPSIS
Setup-engine gate for the four INFs of xhcicm.sys, the Thunderbolt 3
connection manager (design record 21 sections 3.3 and 10.3; roadmap-usb4.md
task 39-A.1): src\cm\xhcicm.inf and xhcicm-amd64.inf (the NHI's) and
src\cm\xhcicmf.inf and xhcicmf-amd64.inf (the NT-only tunnel-bridge filter's).

.DESCRIPTION
check-inf.ps1 is built around xhci98.inf's facts and check-uas-inf.ps1 around
a USB class driver's; this is the third driver's gate, the same failure
classes restated for a driver with two PnP roles and two kinds of INF, each
rule named as the other gates name their families.

  FILE-*   ASCII only, no BOM, CRLF only (Windows 98's 16-bit parser).
  W98-*    The 28-character section-name limit, no duplicate section (first
           wins on Windows 98), no copy section sent to dirid 12, 8.3 names.
  BOTH-*   Class System and its GUID, one DriverVer equal to
           src\xhci_version.h's date and version, defined %strings%,
           sections that every reference names, [SourceDisksFiles] carrying
           exactly the files the package stages for that INF.
  CM-NO9XBRIDGE
           No bridge id in any $CHICAGO$ INF of this driver (a PCI class
           06/xx id, or a Thunderbolt 3 bridge device id of the public PCI id
           list, or any id the filter INFs bind), and the filter INFs' signature
           held to $Windows NT$, which Windows 98's engine ignores: a WDM
           driver on a bridge devnode breaks the bridge on Windows 98 (record
           21 section 6.1).
  NHI INFs (default):
  PATH-*   x86 (xhcicm.inf): [Manufacturer] exactly CmModels,NTx86.6.0
           (PATH-MFGDEC); every model of the undecorated section reaching the
           Windows 98 path (DevLoader *NTKERN, NTMPDriver xhcicm.sys, the
           driver copied: PATH-9X) and the .NTx86 path (the driver copied, the
           xhcicm service as function driver: PATH-NT); every model of the
           .NTx86.6.0 section reaching its .NTx86 install and service
           (PATH-NT6). amd64 (xhcicm-amd64.inf): [Manufacturer] exactly
           CmModels,NTamd64,NTamd64.6.0 and no undecorated models, install
           or right-click section (PATH-NO9X).
  CM-ID    Every NHI model binds an Intel (8086) or AMD (1022) device id or the
           USB4 NHI class PCI\CC_0C0340 and nothing else, and
           every models section of the file binds the same set (CM-IDSET).
  VAL-*    Every install path writes every value the driver reads, each once,
           at the driver's own default (read from src\cm\cm_vals.c's rule
           table) under FLG_ADDREG_NOCLOBBER, and no other Xhci* value
           (VAL-MISSING, VAL-DEFAULT, VAL-FLAG, VAL-EXTRA; the 2026-10-11
           rule). XhciCmTunnelBridge is a REG_SZ written empty.
  CM-NHIHW No [<install>.HW] section: the NHI writes nothing to its
           hardware key, so no filter value can ride on it.
  CM-TMPNAME (x86) xhcicm.sys is copied with no temporary-file name.
  Filter INFs (-Filter):
  CM-FILTER Every model binds a PCI class 06/04 id (the code tells the roles
           apart by it, cm_vals.c); no undecorated install or right-click
           section; each install section includes and needs machine.inf (pci.sys
           stays the bridge's function driver), copies the driver, writes
           HKR,,UpperFilters,0x00010008,"xhcicm" in its .HW section and
           nothing else there, and adds the xhcicm service WITHOUT the
           function-driver flag; no Xhci* value is written.
           [Manufacturer] is CmFilterModels,NTx86.6.0 with an undecorated
           models section for Windows 2000 (x86), or the NTamd64 pair with
           none (amd64, PATH-MFGDEC and PATH-NO9X).
  CM-LAYOUT No LayoutFile: the driver needs no OS-supplied file.
  CM-MSFILE No file of Microsoft's is copied or listed.
  CM-TWIN  The x86 and amd64 INF of each kind bind the same ids and, for the
           NHI, write the same values (run beside every non-self-test gate).
  PKG-*    With -PackageDir: every [SourceDisksFiles] entry is in the staged
           package (PKG-LAYOUT), and no Microsoft file is (PKG-MSFILE).

-SelfTest mutates all four committed INFs in memory, one rule at a time, and
fails unless the gate refuses every mutation and passes the originals - the
gate's own regression suite, run by scripts\build-driver.cmd before the gate.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\inf-gate\check-cm-inf.ps1 -Arch x86
powershell -ExecutionPolicy Bypass -File scripts\inf-gate\check-cm-inf.ps1 -Filter -Arch amd64
#>

[CmdletBinding()]
param(
    [string]$InfPath = "",
    [string]$PackageDir = "",
    [ValidateSet("x86", "amd64")]
    [string]$Arch = "x86",
    [switch]$Filter,
    [switch]$SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")

$repo = Get-RepoRoot
$infFor = @{
    "nhi-x86"      = (Join-Path $repo "src\cm\xhcicm.inf")
    "nhi-amd64"    = (Join-Path $repo "src\cm\xhcicm-amd64.inf")
    "filter-x86"   = (Join-Path $repo "src\cm\xhcicmf.inf")
    "filter-amd64" = (Join-Path $repo "src\cm\xhcicmf-amd64.inf")
}
$kind = if ($Filter) { "filter" } else { "nhi" }
if ($InfPath -eq "") { $InfPath = $infFor["$kind-$Arch"] }
$versionHeader = Join-Path $repo "src\xhci_version.h"
$valsSource = Join-Path $repo "src\cm\cm_vals.c"

$binary = "xhcicm.sys"
$service = "xhcicm"
$classGuid = "{4D36E97D-E325-11CE-BFC1-08002BE10318}"
$msFiles = @("pci.sys", "usbd.sys", "usbhub.sys", "usbport.sys", "usbui.dll", "usbhub20.sys",
             "machine.inf", "pci.inf", "configmg.vxd", "ntkern.vxd", "isapnp.sys")
# Intel Thunderbolt bridge (switch port) device ids: Linux
# drivers/thunderbolt/nhi.h's PCI_DEVICE_ID_INTEL_*_BRIDGE (Falcon, Win,
# Alpine, Titan and Barlow Ridge), which no $CHICAGO$ INF may bind.
$knownBridgeDevs = @("156B", "156D", "157E", "1576", "1578", "15C0", "15D3", "15DA", "15E7", "15EA", "15EF", "5786", "57A4")

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

# The values the driver reads and their defaults, from cm_vals.c's rule
# table: "{ <default>, <min>, <max>, <kind> }, /* <ValueName> */".
function Get-CmValueTable {
    param([string]$SourceText)
    $table = [ordered]@{}
    foreach ($m in [regex]::Matches($SourceText, '(?m)^\s*\{\s*(\d+)\s*,[^}]*\}\s*,?\s*/\*\s*(Xhci[A-Za-z0-9]+)\s*\*/')) {
        $table[$m.Groups[2].Value] = @{ Flag = "0x00010003"; Data = $m.Groups[1].Value }
    }
    # The one REG_SZ (cm_vals.h, CM_TUNNEL_BRIDGE_CHARS): written empty.
    $table["XhciCmTunnelBridge"] = @{ Flag = "0x00000002"; Data = '""' }
    return $table
}

# The ids every models section of an INF binds, as one sorted list per
# section name.
function Get-ModelIds {
    param([string]$Text)
    $sections = Read-InfText $Text
    $mfg = @($sections | Where-Object { $_.Name -ieq "Manufacturer" })
    $out = @{}
    if ($mfg.Count -ne 1 -or $mfg[0].Lines.Count -lt 1) { return $out }
    $fields = @($mfg[0].Lines[0].Value -split ',' | ForEach-Object { $_.Trim() })
    $names = @($fields[0]) + @($fields | Select-Object -Skip 1 | ForEach-Object { $fields[0] + "." + $_ })
    foreach ($n in $names) {
        $s = @($sections | Where-Object { $_.Name -ieq $n })
        if ($s.Count -eq 0) { continue }
        $ids = @()
        foreach ($l in $s[0].Lines) {
            $parts = @($l.Value -split ',' | ForEach-Object { $_.Trim() })
            if ($parts.Count -ge 2) { $ids += $parts[1..($parts.Count - 1)] }
        }
        $out[$n] = @($ids | ForEach-Object { $_.ToUpperInvariant() } | Sort-Object -Unique)
    }
    return $out
}

function Test-CmInfText {
    param([byte[]]$Bytes, [string]$HeaderText, [string]$ValsText, [string]$Package,
          [string]$Profile = "x86", [string]$Kind = "nhi", [string[]]$FilterIds = @())

    $ntSuffix = if ($Profile -eq "amd64") { ".NTamd64" } else { ".NTx86" }
    $infName = if ($Kind -eq "filter") { "xhcicmf.inf" } else { "xhcicm.inf" }
    $sdfWant = if ($Kind -eq "filter") { @($binary) } else { @($binary, "xhcicm.inf") }

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
    if ($null -eq $ver) { & $add "BOTH-VERSION" "no [Version] section."; return $fail }
    $sig = @(& $directive $ver "Signature")
    if ($Kind -eq "filter") {
        if ($sig.Count -ne 1 -or $sig[0] -ne '"$Windows NT$"') { & $add "CM-NO9XBRIDGE" 'the filter INF''s Signature must be "$Windows NT$", which Windows 98''s engine ignores.' }
    } elseif ($sig.Count -ne 1 -or $sig[0] -ne '"$CHICAGO$"') { & $add "BOTH-SIGNATURE" 'Signature must be "$CHICAGO$".' }
    $isChicago = ($sig -contains '"$CHICAGO$"')
    if (@(& $directive $ver "Class") -notcontains "System") { & $add "BOTH-CLASS" "Class must be System." }
    if (@(& $directive $ver "ClassGUID") -notcontains $classGuid) { & $add "BOTH-CLASS" "ClassGUID must be the System class's, $classGuid." }
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
    if (@(& $directive $ver "LayoutFile").Count -ne 0) { & $add "CM-LAYOUT" "LayoutFile: this driver needs no OS-supplied file." }

    # [DestinationDirs].
    $dest = & $get "DestinationDirs"
    $destOf = @{}
    if ($null -eq $dest) { & $add "BOTH-DEST" "no [DestinationDirs] section." }
    else { foreach ($l in $dest.Lines) { $destOf[$l.Key.ToLowerInvariant()] = ($l.Value -replace '\s', '') } }
    foreach ($k in @($destOf.Keys)) {
        if ($destOf[$k] -match '^12(,|$)') { & $add "W98-DIRID12" "[$k] goes to dirid 12, which Windows 98 resolves to \Windows\System\Iosubsys." }
    }

    # Every file a CopyFiles list copies; emits the files, so each install
    # path is held to its own set.
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
                $fields = @($l.Raw -split ',')
                $file = $fields[0].Trim()
                if ($file -notmatch '^[A-Za-z0-9_\-]{1,8}(\.[A-Za-z0-9_]{1,3})?$') { & $add "W98-83" "[$name] copies '$file', not an 8.3 name." }
                if ($Kind -eq "nhi" -and $Profile -eq "x86" -and $file -ieq $binary -and $fields.Count -ge 3 -and $fields[2].Trim() -ne "") {
                    & $add "CM-TMPNAME" "[$name] gives $binary the temporary name '$($fields[2].Trim())': Windows 98 then copies through WININIT.INI and asks for a restart at a first install, when the driver is not loaded."
                }
                $copied[$file.ToLowerInvariant()] = $destOf[$name.ToLowerInvariant()]
                $file.ToLowerInvariant()
            }
        }
    }

    # The AddReg lines an install section writes, through every section it names.
    $regLines = {
        param($sect, $where)
        foreach ($r in ((& $directive $sect "AddReg") -join ',' -split ',')) {
            $r = $r.Trim()
            if ($r -eq "") { continue }
            $rs = & $get $r
            if ($null -eq $rs) { & $add "BOTH-XREF" "$where AddReg names [$r], which does not exist."; continue }
            foreach ($l in $rs.Lines) { , @($l.Raw -split ',' | ForEach-Object { $_.Trim() }) }
        }
    }

    $valueTable = Get-CmValueTable $ValsText
    if ($Kind -eq "nhi" -and $valueTable.Count -lt 2) { & $add "VAL-SOURCE" "src\cm\cm_vals.c's rule table could not be read." }
    $checkValues = {
        param($sect, $where)
        $seen = @{}
        foreach ($f in @(& $regLines $sect $where)) {
            if ($f.Count -lt 3 -or $f[0] -ine "HKR" -or $f[1] -ne "") { continue }
            $name = $f[2]
            if ($name -notmatch '^Xhci') { continue }
            if (-not $valueTable.Contains($name)) { & $add "VAL-EXTRA" "$where writes $name, which the driver does not read."; continue }
            if ($seen.ContainsKey($name)) { & $add "VAL-EXTRA" "$where writes $name twice."; continue }
            $seen[$name] = $true
            $want = $valueTable[$name]
            if ($f.Count -ne 5 -or $f[3] -ine $want.Flag) { & $add "VAL-FLAG" "$where writes $name without flag $($want.Flag) (its type under FLG_ADDREG_NOCLOBBER)." }
            elseif ($f[4] -ne $want.Data) { & $add "VAL-DEFAULT" "$where writes $name as $($f[4]); the driver's default is $($want.Data) (src\cm\cm_vals.c)." }
        }
        foreach ($name in $valueTable.Keys) {
            if (-not $seen.ContainsKey($name)) { & $add "VAL-MISSING" "$where does not write $name; every install path writes every value the driver reads." }
        }
    }

    # The service section an AddService line names; flagTwo says whether the
    # function-driver flag 0x2 must be set (NHI) or clear (filter).
    $checkService = {
        param($ssv, $where, $flagTwo)
        $as = @(& $directive $ssv "AddService")
        if ($as.Count -ne 1) { & $add "PATH-NT" "$where needs exactly one AddService."; return }
        $f = @($as[0] -split ',' | ForEach-Object { $_.Trim() })
        $flags = 0
        if ($f.Count -ge 2 -and $f[1] -ne "") { $flags = [Convert]::ToInt32(($f[1] -replace '^0x', ''), 16) }
        if ($f.Count -lt 3 -or $f[0] -ine $service) { & $add "PATH-NT" "$where must add the $service service with a service section."; return }
        if ($flagTwo -and ($flags -band 2) -eq 0) { & $add "PATH-NT" "$where must add $service with flag 0x2: it is the NHI's function driver." }
        if (-not $flagTwo -and ($flags -band 2) -ne 0) { & $add "CM-FILTER" "$where adds $service with flag 0x2, which would displace pci.sys as the bridge's function driver." }
        $svc = & $get $f[2]
        if ($null -eq $svc) { & $add "BOTH-XREF" "$where names [$($f[2])], which does not exist."; return }
        if (@(& $directive $svc "ServiceType") -notcontains "1") { & $add "PATH-NT" "ServiceType must be 1 (kernel driver)." }
        if (@(& $directive $svc "StartType") -notcontains "3") { & $add "PATH-NT" "StartType must be 3 (demand)." }
        if (@(& $directive $svc "ErrorControl") -notcontains "1") { & $add "PATH-NT" "ErrorControl must be 1 (normal)." }
        if (@(& $directive $svc "ServiceBinary") -notcontains "%12%\$binary") { & $add "PATH-NT" "ServiceBinary must be %12%\$binary." }
    }

    # [Manufacturer] and the models.
    $mfg = & $get "Manufacturer"
    $base = ""
    $modelSets = @()   # @{ Name; Nt6 }
    if ($null -eq $mfg -or $mfg.Lines.Count -ne 1) { & $add "PATH-MFG" "[Manufacturer] must carry exactly one models line." }
    else {
        $fields = @($mfg.Lines[0].Value -split ',' | ForEach-Object { $_.Trim() })
        $base = $fields[0]
        $wantBase = if ($Kind -eq "filter") { "CmFilterModels" } else { "CmModels" }
        if ($base -ine $wantBase) { & $add "PATH-MFG" "the models section must be [$wantBase]." }
        $decor = @($fields | Select-Object -Skip 1)
        if ($Profile -eq "amd64") {
            if ($decor.Count -ne 2 -or $decor[0] -ine "NTamd64" -or $decor[1] -ine "NTamd64.6.0") { & $add "PATH-MFGDEC" "[Manufacturer] must be decorated NTamd64,NTamd64.6.0 and nothing else." }
            if ($null -ne (& $get $base)) { & $add "PATH-NO9X" "an undecorated models section [$base] is one a 32-bit engine would fall back to." }
            $modelSets = @(@{ Name = "$base.NTamd64"; Nt6 = $false }, @{ Name = "$base.NTamd64.6.0"; Nt6 = $true })
        } else {
            if ($decor.Count -ne 1 -or $decor[0] -ine "NTx86.6.0") { & $add "PATH-MFGDEC" "[Manufacturer] must be decorated NTx86.6.0 alone: the one field Windows 98's engine was read taking (roadmap task 22.5)." }
            $modelSets = @(@{ Name = $base; Nt6 = $false }, @{ Name = "$base.NTx86.6.0"; Nt6 = $true })
        }
    }

    $idSets = @{}
    $installs = @()   # @{ Name; Nt6 }
    foreach ($ms in $modelSets) {
        $sect = & $get $ms.Name
        if ($null -eq $sect) { & $add "BOTH-XREF" "models section [$($ms.Name)] does not exist."; continue }
        if ($sect.Lines.Count -eq 0) { & $add "CM-ID" "models section [$($ms.Name)] is empty." }
        $ids = @()
        foreach ($l in $sect.Lines) {
            $parts = @($l.Value -split ',' | ForEach-Object { $_.Trim() })
            if ($parts.Count -lt 2) { & $add "BOTH-XREF" "model line '$($l.Raw)' names no id."; continue }
            $installs += @{ Name = $parts[0]; Nt6 = $ms.Nt6 }
            foreach ($id in $parts[1..($parts.Count - 1)]) {
                $ids += $id.ToUpperInvariant()
                if ($Kind -eq "filter") {
                    if ($id -notmatch '(?i)^PCI\\VEN_[0-9A-F]{4}&DEV_[0-9A-F]{4}&CC_0604$') { & $add "CM-FILTER" "model binds '$id'; the filter binds PCI\VEN_xxxx&DEV_xxxx&CC_0604 ids alone (the code takes the filter role by class 06/04)." }
                } else {
                    if ($id -notmatch '(?i)^PCI\\VEN_(8086|1022)&DEV_[0-9A-F]{4}$' -and $id -ine 'PCI\CC_0C0340') { & $add "CM-ID" "model binds '$id'; an NHI model binds PCI\VEN_8086&DEV_xxxx, PCI\VEN_1022&DEV_xxxx or the USB4 NHI class PCI\CC_0C0340 alone." }
                }
                if ($isChicago) {
                    $isBridge = ($id -match '(?i)CC_06') -or ($FilterIds -contains $id.ToUpperInvariant())
                    foreach ($dev in $knownBridgeDevs) { if ($id -match "(?i)DEV_$dev(&|$)") { $isBridge = $true } }
                    if ($isBridge) { & $add "CM-NO9XBRIDGE" "a `$CHICAGO`$ INF binds the bridge id '$id': Windows 98 would load this driver on a bridge devnode and break the bridge." }
                }
            }
        }
        $idSets[$ms.Name] = (@($ids | Sort-Object -Unique) -join "|")
    }
    if (@($idSets.Values | Sort-Object -Unique).Count -gt 1) { & $add "CM-IDSET" "the models sections bind different ids; every target of this file must see the same set." }

    foreach ($inst in @($installs | ForEach-Object { "$($_.Name)|$($_.Nt6)" } | Sort-Object -Unique)) {
        $name = $inst.Split('|')[0]
        $nt6 = ($inst.Split('|')[1] -eq "True")
        $s9 = & $get $name
        $nt = & $get "$name$ntSuffix"
        $ssv = & $get "$name$ntSuffix.Services"
        $pathRule = if ($nt6) { "PATH-NT6" } else { "PATH-NT" }
        if ($Kind -eq "filter") {
            if ($null -ne $s9) { & $add "CM-FILTER" "an undecorated [$name]: the filter has no install section any engine but NT's may read." }
            if ($null -eq $nt) { & $add $pathRule "no [$name$ntSuffix]." }
            else {
                $own = @(& $checkCopy ((& $directive $nt "CopyFiles") -join ',') "[$name$ntSuffix]")
                if ($own -notcontains $binary) { & $add $pathRule "[$name$ntSuffix] does not copy $binary." }
                if (@(& $directive $nt "Include") -notcontains "machine.inf" -or @(& $directive $nt "Needs").Count -ne 1) { & $add "CM-FILTER" "[$name$ntSuffix] must include and need machine.inf's PCI-to-PCI bridge section, so pci.sys stays the bridge's function driver." }
                if (@(& $regLines $nt "[$name$ntSuffix]").Count -ne 0) { & $add "CM-FILTER" "[$name$ntSuffix] writes registry values; the filter reads none." }
            }
            $shw = & $get "$name$ntSuffix.HW"
            if ($null -eq $shw) { & $add "CM-FILTER" "no [$name$ntSuffix.HW]: the UpperFilters value is the whole of the filter's install." }
            else {
                $hw = @(& $regLines $shw "[$name$ntSuffix.HW]")
                $good = @($hw | Where-Object { $_.Count -eq 5 -and $_[0] -ieq "HKR" -and $_[1] -eq "" -and $_[2] -ieq "UpperFilters" -and $_[3] -ieq "0x00010008" -and $_[4] -ieq ('"' + $service + '"') })
                if ($good.Count -ne 1 -or $hw.Count -ne 1) { & $add "CM-FILTER" "[$name$ntSuffix.HW] must write HKR,,UpperFilters,0x00010008,`"$service`" and nothing else." }
                foreach ($d in @($shw.Lines | Where-Object { $_.Key -ine "AddReg" })) { & $add "CM-FILTER" "[$name$ntSuffix.HW] carries '$($d.Raw)'." }
            }
            if ($null -eq $ssv) { & $add $pathRule "no [$name$ntSuffix.Services]." }
            else {
                if (@(& $directive $ssv "Include") -notcontains "machine.inf" -or @(& $directive $ssv "Needs").Count -ne 1) { & $add "CM-FILTER" "[$name$ntSuffix.Services] must include and need machine.inf's bridge services section, or the bridge loses pci.sys." }
                & $checkService $ssv "[$name$ntSuffix.Services]" $false
            }
            continue
        }
        # The NHI.
        if ($Profile -eq "amd64") {
            if ($null -ne $s9) { & $add "PATH-NO9X" "an undecorated [$name] is one a 32-bit engine would fall back to." }
        } elseif (-not $nt6) {
            if ($null -eq $s9) { & $add "PATH-9X" "no undecorated [$name] for Windows 98." }
            else {
                $haveLoader = $false; $haveMp = $false
                foreach ($f in @(& $regLines $s9 "[$name]")) {
                    if ($f.Count -ge 5 -and $f[0] -ieq "HKR" -and $f[2] -ieq "DevLoader" -and $f[4] -ieq "*NTKERN") { $haveLoader = $true }
                    if ($f.Count -ge 5 -and $f[0] -ieq "HKR" -and $f[2] -ieq "NTMPDriver" -and $f[4] -ieq $binary) { $haveMp = $true }
                }
                if (-not $haveLoader) { & $add "PATH-9X" "[$name] writes no HKR,,DevLoader,,*NTKERN." }
                if (-not $haveMp) { & $add "PATH-9X" "[$name] writes no HKR,,NTMPDriver,,$binary." }
                $own = @(& $checkCopy ((& $directive $s9 "CopyFiles") -join ',') "[$name]")
                if ($own -notcontains $binary) { & $add "PATH-9X" "[$name] does not copy $binary." }
                & $checkValues $s9 "[$name]"
            }
        }
        foreach ($hwName in @("$name.HW", "$name$ntSuffix.HW")) {
            if ($null -ne (& $get $hwName)) { & $add "CM-NHIHW" "[$hwName]: the NHI writes nothing to its hardware key." }
        }
        if ($null -eq $nt) { & $add $pathRule "no [$name$ntSuffix]." }
        else {
            $own = @(& $checkCopy ((& $directive $nt "CopyFiles") -join ',') "[$name$ntSuffix]")
            if ($own -notcontains $binary) { & $add $pathRule "[$name$ntSuffix] does not copy $binary." }
            & $checkValues $nt "[$name$ntSuffix]"
        }
        if ($null -eq $ssv) { & $add $pathRule "no [$name$ntSuffix.Services]: the NT targets would install a devnode with no service." }
        else { & $checkService $ssv "[$name$ntSuffix.Services]" $true }
    }

    foreach ($d in @("DefaultInstall", "DefaultInstall$ntSuffix")) {
        $ds = & $get $d
        if ($null -eq $ds) { continue }
        if ($Kind -eq "filter") { & $add "CM-FILTER" "[$d]: the filter is installed on its devnode only, never by right-click." ; continue }
        if ($Profile -eq "amd64" -and $d -ieq "DefaultInstall") { & $add "PATH-NO9X" "an undecorated [DefaultInstall] is one a 32-bit engine would run." }
        $null = @(& $checkCopy ((& $directive $ds "CopyFiles") -join ',') "[$d]")
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
    foreach ($want in $sdfWant) {
        if ($listed -notcontains $want) { & $add "BOTH-SDF" "[SourceDisksFiles] does not list $want." }
    }
    foreach ($f in $listed) {
        if ($sdfWant -notcontains $f) { & $add "BOTH-SDF" "[SourceDisksFiles] lists '$f', which the package does not carry for $infName." }
    }
    foreach ($ms in $msFiles) {
        if ($copied.ContainsKey($ms) -or $listed -contains $ms) { & $add "CM-MSFILE" "$ms is Microsoft's; this INF copies and lists no file of theirs." }
    }

    if ($Package -ne "") {
        foreach ($f in $listed) {
            if (-not (Test-Path -LiteralPath (Join-Path $Package $f))) { & $add "PKG-LAYOUT" "[SourceDisksFiles] lists '$f' and the package has none." }
        }
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

# CM-TWIN: the x86 and amd64 INF of one kind bind the same ids and, for the
# NHI, write the same value lines. Each file's own sections must agree
# already (CM-IDSET, VAL-*), so one set per file is compared.
function Test-CmTwin {
    param([string]$X86Text, [string]$Amd64Text, [string]$Kind)
    $fail = New-Object System.Collections.ArrayList
    $a = Get-ModelIds $X86Text
    $b = Get-ModelIds $Amd64Text
    $setA = (@($a.Values | ForEach-Object { $_ }) | Sort-Object -Unique) -join "|"
    $setB = (@($b.Values | ForEach-Object { $_ }) | Sort-Object -Unique) -join "|"
    if ($setA -ne $setB) { [void]$fail.Add("CM-TWIN the x86 and amd64 $Kind INFs bind different ids ($setA against $setB).") }
    if ($Kind -eq "nhi") {
        $va = @([regex]::Matches($X86Text, '(?m)^HKR,,Xhci[^\r\n]*') | ForEach-Object { $_.Value } | Sort-Object -Unique) -join "|"
        $vb = @([regex]::Matches($Amd64Text, '(?m)^HKR,,Xhci[^\r\n]*') | ForEach-Object { $_.Value } | Sort-Object -Unique) -join "|"
        if ($va -ne $vb) { [void]$fail.Add("CM-TWIN the x86 and amd64 NHI INFs write different values.") }
    }
    return $fail
}

$headerText = if (Test-Path -LiteralPath $versionHeader) { [System.IO.File]::ReadAllText($versionHeader) } else { "" }
$valsText = if (Test-Path -LiteralPath $valsSource) { [System.IO.File]::ReadAllText($valsSource) } else { "" }
# The bridge ids the filter INFs bind, which no $CHICAGO$ INF may bind.
$filterIds = @()
foreach ($k in @("filter-x86", "filter-amd64")) {
    if (Test-Path -LiteralPath $infFor[$k]) {
        $ids = Get-ModelIds ([System.IO.File]::ReadAllText($infFor[$k]))
        foreach ($v in $ids.Values) { $filterIds += $v }
    }
}
$filterIds = @($filterIds | Sort-Object -Unique)

if ($SelfTest) {
    Write-Step "CM INF gate self-tests"
    $bad = 0
    $total = 0
    $texts = @{}
    foreach ($key in @("nhi-x86", "nhi-amd64", "filter-x86", "filter-amd64")) {
        $orig = [System.IO.File]::ReadAllBytes($infFor[$key])
        $texts[$key] = [System.Text.Encoding]::ASCII.GetString($orig)
        $kindOf = $key.Split('-')[0]; $prof = $key.Split('-')[1]
        $base = @(Test-CmInfText -Bytes $orig -HeaderText $headerText -ValsText $valsText -Package "" -Profile $prof -Kind $kindOf -FilterIds $filterIds)
        if ($base.Count -ne 0) {
            Write-Err ("the committed $key INF fails its own gate:`n  " + ($base -join "`n  "))
            exit 1
        }
        $t = $texts[$key]
        switch ($key) {
            "nhi-x86" {
                $cases = @(
                    @{ Rule = "FILE-EOL";       Text = $t.Replace("`r`n[Strings]", "`n[Strings]") },
                    @{ Rule = "FILE-ASCII";     Text = $t.Replace('Provider="Yeo', ('Provider="' + [char]0xE9 + 'Yeo')) },
                    @{ Rule = "BOTH-SIGNATURE"; Text = $t.Replace('Signature="$CHICAGO$"', 'Signature="$Windows NT$"') },
                    @{ Rule = "BOTH-CLASS";     Text = $t.Replace("Class=System", "Class=USB") },
                    @{ Rule = "BOTH-VERSION";   Text = ($t -replace 'DriverVer=\d\d/\d\d/\d{4},[0-9.]+', 'DriverVer=01/01/2020,9.9.9.9') },
                    @{ Rule = "CM-NO9XBRIDGE";  Text = $t.Replace("[CmModels]`r`n", "[CmModels]`r`n%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15C0&CC_0604`r`n") },
                    @{ Rule = "CM-NO9XBRIDGE";  Text = $t.Replace("[CmModels]`r`n", "[CmModels]`r`n%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15C0`r`n") },
                    @{ Rule = "CM-ID";          Text = $t.Replace("[CmModels]`r`n", "[CmModels]`r`n%NhiDesc%=Cm.Dev,PCI\CC_0880`r`n") },
                    @{ Rule = "CM-ID";          Text = $t.Replace("[CmModels]`r`n", "[CmModels]`r`n%NhiDesc%=Cm.Dev,PCI\VEN_1B21&DEV_2142`r`n") },
                    @{ Rule = "CM-IDSET";       Text = $t.Replace("[CmModels.NTx86.6.0]`r`n", "[CmModels.NTx86.6.0]`r`n%NhiDesc%=Cm.Dev6,PCI\VEN_8086&DEV_1234`r`n") },
                    @{ Rule = "PATH-MFGDEC";    Text = $t.Replace("%Mfg%=CmModels,NTx86.6.0", "%Mfg%=CmModels,NTx86,NTx86.6.0") },
                    @{ Rule = "PATH-9X";        Text = $t.Replace("HKR,,NTMPDriver,,xhcicm.sys", "HKR,,NTMPDriver,,other.sys") },
                    @{ Rule = "PATH-9X";        Text = $t.Replace("HKR,,DevLoader,,*NTKERN", "HKR,,DevLoader,,*CONFIGMG") },
                    @{ Rule = "PATH-NT";        Text = $t.Replace("[Cm.Dev.NTx86.Services]", "[Cm.Dev.NTx86.Svc]") },
                    @{ Rule = "PATH-NT";        Text = $t.Replace("AddService=xhcicm,0x00000002,Cm.AddService`r`n`r`n; --- Windows Vista", "AddService=xhcicm,,Cm.AddService`r`n`r`n; --- Windows Vista") },
                    @{ Rule = "PATH-NT6";       Text = $t.Replace("[Cm.Dev6.NTx86]", "[Cm.Dev6.NTx86.Old]") },
                    @{ Rule = "VAL-MISSING";    Text = $t.Replace("HKR,,XhciCmReenum,0x00010003,1`r`n", "") },
                    @{ Rule = "VAL-MISSING";    Text = $t.Replace("[Cm.Dev.NTx86]`r`nAddReg=Cm.Values", "[Cm.Dev.NTx86]") },
                    @{ Rule = "VAL-DEFAULT";    Text = $t.Replace("XhciCmPollIdleMs,0x00010003,200", "XhciCmPollIdleMs,0x00010003,250") },
                    @{ Rule = "VAL-DEFAULT";    Text = $t.Replace("XhciCmMode,0x00010003,1", "XhciCmMode,0x00010003,0") },
                    @{ Rule = "VAL-FLAG";       Text = $t.Replace("XhciCmApprove,0x00010003,1", "XhciCmApprove,0x00010001,1") },
                    @{ Rule = "VAL-FLAG";       Text = $t.Replace('XhciCmTunnelBridge,0x00000002,""', 'XhciCmTunnelBridge,0,""') },
                    @{ Rule = "VAL-EXTRA";      Text = $t.Replace("HKR,,XhciLogVerbosity,0x00010003,0", "HKR,,XhciLogVerbosity,0x00010003,0`r`nHKR,,XhciCmUsb3Tunnel,0x00010003,1") },
                    @{ Rule = "CM-NHIHW";       Text = $t.Replace("[Cm.Dev.NTx86.Services]", "[Cm.Dev.NTx86.HW]`r`nAddReg=Cm.AddReg`r`n`r`n[Cm.Dev.NTx86.Services]") },
                    @{ Rule = "CM-TMPNAME";     Text = $t.Replace("[Cm.CopyFiles]`r`nxhcicm.sys", "[Cm.CopyFiles]`r`nxhcicm.sys,,xhcicm.tmp") },
                    @{ Rule = "CM-LAYOUT";      Text = $t.Replace("Provider=%Provider%", "Provider=%Provider%`r`nLayoutFile=layout.inf") },
                    @{ Rule = "CM-MSFILE";      Text = $t.Replace("[Cm.CopyFiles]`r`nxhcicm.sys", "[Cm.CopyFiles]`r`nxhcicm.sys`r`npci.sys") },
                    @{ Rule = "W98-DIRID12";    Text = $t.Replace("Cm.CopyFiles=10,System32\Drivers", "Cm.CopyFiles=12") },
                    @{ Rule = "W98-SECTLEN";    Text = $t.Replace("[Cm.AddService]", "[Cm.AddService.Very.Long.Section.Name]").Replace(",Cm.AddService", ",Cm.AddService.Very.Long.Section.Name") },
                    @{ Rule = "BOTH-STRINGS";   Text = $t.Replace('NhiDesc="', 'NhiDescription="') },
                    @{ Rule = "BOTH-SDF";       Text = $t.Replace("xhcicm.inf=1", "xhcicmf.inf=1") }
                )
            }
            "nhi-amd64" {
                $cases = @(
                    @{ Rule = "PATH-MFGDEC";    Text = $t.Replace("%Mfg%=CmModels,NTamd64,NTamd64.6.0", "%Mfg%=CmModels,NTamd64") },
                    @{ Rule = "PATH-NO9X";      Text = $t.Replace("[CmModels.NTamd64]", "[CmModels]`r`n%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15BF`r`n`r`n[CmModels.NTamd64]") },
                    @{ Rule = "PATH-NO9X";      Text = $t.Replace("[Cm.Dev.NTamd64]", "[Cm.Dev]`r`nCopyFiles=Cm.CopyFiles`r`n`r`n[Cm.Dev.NTamd64]") },
                    @{ Rule = "PATH-NO9X";      Text = $t.Replace("[DefaultInstall.NTamd64]", "[DefaultInstall]`r`nCopyFiles=Cm.CopyFiles`r`n`r`n[DefaultInstall.NTamd64]") },
                    @{ Rule = "PATH-NT";        Text = $t.Replace("[Cm.Dev.NTamd64.Services]", "[Cm.Dev.NTamd64.Svc]") },
                    @{ Rule = "PATH-NT6";       Text = $t.Replace("[Cm.Dev6.NTamd64.Services]", "[Cm.Dev6.NTamd64.Svc]") },
                    @{ Rule = "VAL-DEFAULT";    Text = $t.Replace("XhciCmApproveTimeoutMs,0x00010003,10000", "XhciCmApproveTimeoutMs,0x00010003,1000") },
                    @{ Rule = "CM-NO9XBRIDGE";  Text = $t.Replace("[CmModels.NTamd64]`r`n", "[CmModels.NTamd64]`r`n%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15D3`r`n") },
                    @{ Rule = "BOTH-VERSION";   Text = ($t -replace 'DriverVer=\d\d/\d\d/\d{4},[0-9.]+', 'DriverVer=01/01/2020,9.9.9.9') }
                )
            }
            "filter-x86" {
                $cases = @(
                    @{ Rule = "CM-NO9XBRIDGE";  Text = $t.Replace('Signature="$Windows NT$"', 'Signature="$CHICAGO$"') },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace("DEV_15EA&CC_0604", "DEV_15EA") },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace('UpperFilters,0x00010008,"xhcicm"', 'LowerFilters,0x00010008,"xhcicm"') },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace('UpperFilters,0x00010008,"xhcicm"', 'UpperFilters,0x00010000,"xhcicm"') },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace("AddService=xhcicm,,CmFilter.AddService`r`n`r`n; --- Windows Vista", "AddService=xhcicm,0x00000002,CmFilter.AddService`r`n`r`n; --- Windows Vista") },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace("[CmF.Dev.NTx86]`r`nInclude=machine.inf`r`nNeeds=PCI_DRV", "[CmF.Dev.NTx86]") },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace("[CmF.Dev.NTx86.Services]`r`nInclude=machine.inf`r`nNeeds=PCI_DRV.Services", "[CmF.Dev.NTx86.Services]") },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace("[CmF.Dev.NTx86.HW]", "[CmF.Dev.NTx86.Hw2]") },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace("[CmF.Dev.NTx86]", "[CmF.Dev]`r`nCopyFiles=CmFilter.CopyFiles`r`n`r`n[CmF.Dev.NTx86]") },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace("[CmF.Dev.NTx86]`r`nInclude=machine.inf", "[CmF.Dev.NTx86]`r`nAddReg=CmFilter.HW.AddReg`r`nInclude=machine.inf") },
                    @{ Rule = "PATH-MFGDEC";    Text = $t.Replace("%Mfg%=CmFilterModels,NTx86.6.0", "%Mfg%=CmFilterModels") },
                    @{ Rule = "PATH-NT6";       Text = $t.Replace("[CmF.Dev6.NTx86.Services]", "[CmF.Dev6.NTx86.Svc]") },
                    @{ Rule = "BOTH-SDF";       Text = $t.Replace("xhcicm.sys=1", "xhcicm.sys=1`r`nxhcicm.inf=1") },
                    @{ Rule = "BOTH-CLASS";     Text = $t.Replace("ClassGUID={4D36E97D-E325-11CE-BFC1-08002BE10318}", "ClassGUID={36FC9E60-C465-11CF-8056-444553540000}") }
                )
            }
            "filter-amd64" {
                $cases = @(
                    @{ Rule = "CM-NO9XBRIDGE";  Text = $t.Replace('Signature="$Windows NT$"', 'Signature="$CHICAGO$"') },
                    @{ Rule = "PATH-NO9X";      Text = $t.Replace("[CmFilterModels.NTamd64]", "[CmFilterModels]`r`n%TunnelDesc%=CmF.Dev,PCI\VEN_8086&DEV_15C0&CC_0604`r`n`r`n[CmFilterModels.NTamd64]") },
                    @{ Rule = "PATH-MFGDEC";    Text = $t.Replace("%Mfg%=CmFilterModels,NTamd64,NTamd64.6.0", "%Mfg%=CmFilterModels,NTamd64") },
                    @{ Rule = "CM-FILTER";      Text = $t.Replace('UpperFilters,0x00010008,"xhcicm"', 'UpperFilters,0x00010008,"other"') }
                )
            }
        }
        foreach ($c in $cases) {
            $total++
            $got = @(Test-CmInfText -Bytes ([System.Text.Encoding]::GetEncoding(28591).GetBytes($c.Text)) -HeaderText $headerText -ValsText $valsText -Package "" -Profile $prof -Kind $kindOf -FilterIds $filterIds)
            $hit = @($got | Where-Object { $_.StartsWith($c.Rule + " ") })
            if ($c.Text -eq $t) {
                $bad++
                Write-Err "$key mutation for $($c.Rule) did not change the file - the self-test is stale."
            } elseif ($hit.Count -eq 0) {
                $bad++
                Write-Err ("$key mutation for $($c.Rule) was not refused by that rule. Got:`n  " + ($got -join "`n  "))
            } else {
                Write-Ok "$key $($c.Rule) refuses its mutation"
            }
        }
    }
    # VAL-SOURCE: the INF's defaults follow the code; a default changed in
    # cm_vals.c alone is refused.
    $total++
    $valsMut = $valsText -replace '\{\s*200,(\s*50,)', '{ 300,$1'
    $got = @(Test-CmInfText -Bytes ([System.IO.File]::ReadAllBytes($infFor["nhi-x86"])) -HeaderText $headerText -ValsText $valsMut -Package "" -Profile "x86" -Kind "nhi" -FilterIds $filterIds)
    if ($valsMut -eq $valsText -or @($got | Where-Object { $_.StartsWith("VAL-DEFAULT ") }).Count -eq 0) {
        $bad++; Write-Err "a default changed in src\cm\cm_vals.c alone was not refused by VAL-DEFAULT."
    } else { Write-Ok "VAL-DEFAULT follows src\cm\cm_vals.c" }
    # CM-TWIN, both kinds: the originals agree, a dropped id does not.
    foreach ($k in @("nhi", "filter")) {
        $total++
        $x = $texts["$k-x86"]; $y = $texts["$k-amd64"]
        $ok = @(Test-CmTwin -X86Text $x -Amd64Text $y -Kind $k)
        $mutY = if ($k -eq "nhi") { $y.Replace("%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15BF", "%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15BE").Replace("%NhiDesc%=Cm.Dev6,PCI\VEN_8086&DEV_15BF", "%NhiDesc%=Cm.Dev6,PCI\VEN_8086&DEV_15BE") } else { $y.Replace("DEV_15C0&CC_0604", "DEV_15C1&CC_0604") }
        $mut = @(Test-CmTwin -X86Text $x -Amd64Text $mutY -Kind $k)
        if ($ok.Count -ne 0 -or $mut.Count -eq 0 -or $mutY -eq $y) {
            $bad++; Write-Err ("CM-TWIN ($k) self-test failed:`n  " + (($ok + $mut) -join "`n  "))
        } else { Write-Ok "CM-TWIN ($k) passes the twins and refuses a drifted id" }
    }
    if ($bad -ne 0) { exit 1 }
    Write-Ok "CM INF gate self-tests PASSED ($total mutations)"
    exit 0
}

$label = if ($Filter) { "filter, $Arch" } else { "NHI, $Arch" }
Write-Step "CM INF gate ($label): $InfPath"
if (-not (Test-Path -LiteralPath $InfPath)) { Write-Err "no INF at '$InfPath'."; exit 1 }
$fails = @(Test-CmInfText -Bytes ([System.IO.File]::ReadAllBytes($InfPath)) -HeaderText $headerText -ValsText $valsText -Package $PackageDir -Profile $Arch -Kind $kind -FilterIds $filterIds)
# The twin comparison reads the committed pair; a staged copy (-PackageDir)
# is compared against its committed twin.
$twinX = if ($Arch -eq "x86") { $InfPath } else { $infFor["$kind-x86"] }
$twinA = if ($Arch -eq "amd64") { $InfPath } else { $infFor["$kind-amd64"] }
if ((Test-Path -LiteralPath $twinX) -and (Test-Path -LiteralPath $twinA)) {
    $fails += @(Test-CmTwin -X86Text ([System.IO.File]::ReadAllText($twinX)) -Amd64Text ([System.IO.File]::ReadAllText($twinA)) -Kind $kind)
}
if ($fails.Count -ne 0) {
    foreach ($f in $fails) { Write-Err $f }
    Write-Err "CM INF gate FAILED ($($fails.Count))"
    exit 1
}
Write-Ok "CM INF gate PASSED"
exit 0
