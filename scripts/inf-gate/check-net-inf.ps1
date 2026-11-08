<#
.SYNOPSIS
Setup-engine gate for the two INFs of Thunderbolt networking, which install
xhcinet.sys, the NDIS 5 miniport, and xhcinet6.sys, the NDIS 6.20 one
(design record 35 sections 7.8, 7.9, 8.2 and 8.3; roadmap-usb4.md task
45.59 steps S6 and S7): src\net\xhcinet.inf (x86) and
src\net\xhcinet-amd64.inf (amd64).

.DESCRIPTION
The rule families, each named as the other INF gates name theirs:

  FILE-*    ASCII only, no BOM, CRLF only (Windows 98's 16-bit parser).
  W98-*     The 28-character section-name limit and no duplicate section
            (the first wins on Windows 98).
  BOTH-*    Class Net and its GUID; one DriverVer equal to
            src\xhci_version.h's date and version; every %string% defined;
            every section named by AddReg, CopyFiles, AddService and a
            model line present; [SourceDisksFiles] exactly xhcinet.sys
            and xhcinet6.sys;
            CatalogFile on the amd64 file and on no other (record 19
            section 3a).
  PATH-*    [Manufacturer] exactly NetModels,NTx86,NTx86.6.2 (x86) or
            NetModels,NTamd64,NTamd64.6.2 (amd64) (PATH-MFGDEC); on x86
            [NetModels.NTx86] holding the undecorated section's one model
            and install, since Vista's and 7's driver store stages only
            the files of the decorated models sections once the line has
            a decoration, which left 7 without xhcinet.sys (PATH-NTSTAGE,
            record 35 section 8.2); the one
            model XHCICM\TBNET (record 35 section 6.2), in the undecorated
            section on x86 and in NetModels.NTamd64 on amd64 (PATH-ID); the
            .6.2 models section holding the one model XHCICM\TBNET too,
            with an install section of its own, not the NDIS 5 one
            (PATH-NT62); no undecorated models or install section in the
            amd64 file, and no undecorated twin of the NDIS 6 install in
            either (PATH-NO9X).
  NET9X-*   The x86 file's 98 SE and ME install: DevLoader *ndis,
            DeviceVxDs xhcinet.sys, EnumPropPages netdi.dll, the NDIS
            version 5.0, Ndi\DeviceID the hardware id, the Interfaces and
            Install keys (record 35 section 7.8). NET9X-NOCOPY: neither
            the install section nor a section its Ndi\Install keys name
            copies a file, or reaches another section by Include or Needs
            (record 35 section 9.5, revision 17): there any copy asked for
            the disk at Windows' own source path when the adapter
            installed, so Setup places xhcinet.sys in SYSTEM itself.
  NETNT-*   Each .NT install, the NDIS 5 one and the NDIS 6 one alike:
            Characteristics 0x84 (NCF_PHYSICAL and NCF_HAS_UI), a BusType,
            its image copied to dirid 12 (xhcinet.sys on the NDIS 5 path,
            xhcinet6.sys on the 6.2 one), the xhcinet service as the
            function driver (AddService flag 2) with ServiceType 1,
            StartType 3, ServiceBinary %12%\<that image> and the NDIS load
            order group, Ndi\Service xhcinet, UpperRange ndis5 and
            LowerRange ethernet. NETNT-IMAGE: no path copies the other
            path's image (the 98 SE and ME path's is xhcinet.sys).
  NET6-*    The NDIS 6 install's media keywords (record 35 section 7.9):
            *IfType 6, *MediaType 0 and *PhysicalMediaType 0, each once,
            at those values, on the 6.2 path and on no other
            (NET6-KEYWORD); the keyword table below is the one place they
            are listed. Values Windows owns, which NDIS reads itself; the
            install writes each at its one value.
  BOTH-DUPREG, BOTH-REGFLAGS
            One write a registry target on each path, and each a plain
            string write but the binary NDIS version and the values below.
  VAL-*     The owner's rule of 2026-10-30: every value the driver reads
            written by every install path at its default with no-overwrite
            (0x00000002, a string), and its Ndi\params entry (ParamDesc,
            type dword, default the same as the written value, min, max)
            on every path; the value table below is the one place the
            values are listed, and a value in an INF that is not in the
            table is refused too (VAL-UNKNOWN).

-SelfTest mutates good copies of both files in memory, one rule at a time,
and fails unless each mutation is refused by the rule it names and the
unmutated files pass.
#>
[CmdletBinding()]
param(
    [ValidateSet("x86", "amd64")]
    [string]$Arch = "x86",
    [string]$InfPath = "",
    [switch]$SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# The driver's values (record 35 section 8.3): name, default, min, max.
$script:Values = @(
    @{ Name = "XhciNetMtu"; Default = "1500"; Min = "1500"; Max = "65522" }
)

# The NDIS 6 install's media keywords (record 35 section 7.9): directives
# of the 6.2 install section, which Setup writes to the driver key and
# NDIS reads; name and the one value written.
$script:Keywords = @(
    @{ Name = "*IfType"; Value = "6" },
    @{ Name = "*MediaType"; Value = "0" },
    @{ Name = "*PhysicalMediaType"; Value = "0" }
)

function Get-InfFor([string]$a) {
    if ($a -eq "amd64") { return (Join-Path $repo "src\net\xhcinet-amd64.inf") }
    return (Join-Path $repo "src\net\xhcinet.inf")
}

# The version and DriverVer date src\xhci_version.h defines, each "" when
# its #define is not there (anchored at a line's start, as the other INF
# gates read them); Test-NetInf fails either missing rather than skipping
# its comparison.
function Get-Version {
    $h = Get-Content -LiteralPath (Join-Path $repo "src\xhci_version.h") -Raw
    $m = [regex]::Match($h, '(?m)^#define\s+XHCI_VER_STR\s+"(\d+\.\d+\.\d+\.\d+)"')
    $d = [regex]::Match($h, '(?m)^#define\s+XHCI_DRIVERVER_DATE\s+"(\d{2}/\d{2}/\d{4})"')
    $version = ""
    $date = ""
    if ($m.Success) { $version = $m.Groups[1].Value }
    if ($d.Success) { $date = $d.Groups[1].Value }
    return @{ Version = $version; Date = $date }
}

# Parses INF text into ordered sections of raw lines (comments and blank
# lines dropped). Section names are compared case-insensitively.
function Read-Inf([string]$text) {
    $sections = [ordered]@{}
    $order = New-Object System.Collections.ArrayList
    $dups = New-Object System.Collections.ArrayList
    $cur = $null
    foreach ($raw in ($text -split "`r`n")) {
        $line = $raw
        $semi = $line.IndexOf(';')
        $quote = $line.IndexOf('"')
        if ($semi -ge 0 -and ($quote -lt 0 -or $semi -lt $quote)) {
            $line = $line.Substring(0, $semi)
        }
        $line = $line.Trim()
        if ($line -eq "") { continue }
        if ($line -match '^\[(.+)\]$') {
            $name = $Matches[1].Trim()
            $key = $name.ToLowerInvariant()
            if ($sections.Contains($key)) {
                [void]$dups.Add($name)
                $cur = $sections[$key]
            } else {
                $cur = New-Object System.Collections.ArrayList
                $sections[$key] = $cur
                [void]$order.Add($name)
            }
            continue
        }
        if ($null -ne $cur) { [void]$cur.Add($line) }
    }
    return @{ Sections = $sections; Order = $order; Dups = $dups }
}

function Get-Section($inf, [string]$name) {
    $key = $name.ToLowerInvariant()
    if ($inf.Sections.Contains($key)) { return ,@($inf.Sections[$key]) }
    return $null
}

function Get-Key($lines, [string]$key) {
    foreach ($l in $lines) {
        if ($l -match ('^' + [regex]::Escape($key) + '\s*=\s*(.*)$')) {
            return $Matches[1].Trim()
        }
    }
    return $null
}

function Get-Keys($lines, [string]$key) {
    $out = @()
    foreach ($l in $lines) {
        if ($l -match ('^' + [regex]::Escape($key) + '\s*=\s*(.*)$')) {
            $out += ($Matches[1].Trim() -split '\s*,\s*')
        }
    }
    return $out
}

# AddReg lines of the named sections, split: root, subkey, value, flags, data.
function Get-RegLines($inf, [string[]]$names) {
    $rows = @()
    foreach ($n in $names) {
        $sec = Get-Section $inf $n
        if ($null -eq $sec) { continue }
        foreach ($l in $sec) {
            $f = $l -split ',', 5
            while ($f.Count -lt 5) { $f += "" }
            # Every field unquoted: Setup reads "XhciNetMtu" as XhciNetMtu.
            $rows += ,@($f[0].Trim().Trim('"'), $f[1].Trim().Trim('"'), $f[2].Trim().Trim('"'), $f[3].Trim().Trim('"'), $f[4].Trim().Trim('"'))
        }
    }
    return ,$rows
}

function Find-Reg($rows, [string]$sub, [string]$value) {
    foreach ($r in $rows) {
        if ($r[0] -ieq "HKR" -and $r[1] -ieq $sub -and $r[2] -ieq $value) { return $r }
    }
    return $null
}

# A string value's flags: empty, 0 or 0x00000000 (FLG_ADDREG_TYPE_SZ, no
# operation bit); anything else changes the type or deletes (0x4).
function Test-StrFlags([string]$f) {
    return ($f -eq "" -or $f -match '^(0|0x0+)$')
}

function Test-NetInf {
    param([string]$Text, [byte[]]$Bytes, [string]$A, [string]$Label)
    $fails = New-Object System.Collections.ArrayList
    function Fail([string]$rule, [string]$msg) { [void]$fails.Add("$rule $Label`: $msg") }

    # FILE-*
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        Fail "FILE-BOM" "a UTF-8 BOM"
    }
    for ($i = 0; $i -lt $Bytes.Length; $i++) {
        $b = $Bytes[$i]
        if ($b -ge 0x80) { Fail "FILE-ASCII" "byte 0x$('{0:X2}' -f $b) at offset $i"; break }
        if ($b -eq 0x0A -and ($i -eq 0 -or $Bytes[$i - 1] -ne 0x0D)) { Fail "FILE-CRLF" "a bare LF at offset $i"; break }
        if ($b -eq 0x0D -and ($i + 1 -ge $Bytes.Length -or $Bytes[$i + 1] -ne 0x0A)) { Fail "FILE-CRLF" "a bare CR at offset $i"; break }
    }

    $inf = Read-Inf $Text
    foreach ($d in $inf.Dups) { Fail "W98-DUPSECTION" "[$d] appears twice" }
    foreach ($n in $inf.Order) {
        if ($n.Length -gt 28) { Fail "W98-SECTIONLEN" "[$n] is longer than 28 characters" }
    }

    # BOTH-*
    $ver = Get-Section $inf "Version"
    if ($null -eq $ver) { Fail "BOTH-VERSION" "no [Version]"; return $fails }
    if ((Get-Key $ver "Class") -ne "Net") { Fail "BOTH-CLASS" "Class is not Net" }
    if ((Get-Key $ver "ClassGUID") -ne "{4D36E972-E325-11CE-BFC1-08002BE10318}") { Fail "BOTH-CLASS" "ClassGUID is not the Net class's" }
    if ((Get-Key $ver "Signature") -ne '"$CHICAGO$"') { Fail "BOTH-SIGNATURE" 'Signature is not "$CHICAGO$"' }
    if ((Get-Key $ver "Compatible") -ne "1") { Fail "BOTH-COMPATIBLE" "a `$CHICAGO`$ network INF carries Compatible=1 (Windows 2000 DDK network guide 1.2.1)" }
    $v = Get-Version
    $dv = @($ver | Where-Object { $_ -match '^DriverVer\s*=' })
    if ($dv.Count -ne 1) {
        Fail "BOTH-DRIVERVER" "$($dv.Count) DriverVer lines"
    } else {
        $val = @((Get-Key $ver "DriverVer") -split '\s*,\s*')
        if ($v.Version -eq "" -or $v.Date -eq "") {
            Fail "BOTH-DRIVERVER" "src\xhci_version.h defines no XHCI_VER_STR or no XHCI_DRIVERVER_DATE in its #define form, so DriverVer cannot be compared"
        } else {
            if ($val.Count -ne 2 -or $val[1] -ne $v.Version) { Fail "BOTH-DRIVERVER" "DriverVer '$((Get-Key $ver 'DriverVer'))' is not version $($v.Version)" }
            if ($val[0] -ne $v.Date) { Fail "BOTH-DRIVERVER" "DriverVer date '$($val[0])' is not $($v.Date)" }
        }
    }
    $cat = Get-Key $ver "CatalogFile"
    if ($A -eq "amd64" -and $cat -ne "xhcinet.cat") { Fail "BOTH-CATALOG" "the amd64 file names no CatalogFile=xhcinet.cat" }
    if ($A -eq "x86" -and $null -ne $cat) { Fail "BOTH-CATALOG" "the x86 package carries no catalog" }

    $strings = Get-Section $inf "Strings"
    $defined = @{}
    if ($null -ne $strings) {
        foreach ($l in $strings) { if ($l -match '^([^=\s]+)\s*=') { $defined[$Matches[1].ToLowerInvariant()] = $true } }
    }
    foreach ($m in [regex]::Matches($Text, '%([A-Za-z_][A-Za-z0-9_]*)%')) {
        $k = $m.Groups[1].Value.ToLowerInvariant()
        if (-not $defined.ContainsKey($k)) { Fail "BOTH-STRING" "%$($m.Groups[1].Value)% is not defined in [Strings]" }
    }
    $sdf = Get-Section $inf "SourceDisksFiles"
    $files = @()
    if ($null -ne $sdf) { foreach ($l in $sdf) { $files += ($l -split '=')[0].Trim().ToLowerInvariant() } }
    if (($files -join ",") -ne "xhcinet.sys,xhcinet6.sys") { Fail "BOTH-FILES" "[SourceDisksFiles] is '$($files -join ",")', not xhcinet.sys and xhcinet6.sys" }

    # PATH-*
    $mfg = Get-Section $inf "Manufacturer"
    $want = "NetModels,NTx86,NTx86.6.2"
    if ($A -eq "amd64") { $want = "NetModels,NTamd64,NTamd64.6.2" }
    $got = ""
    if ($null -ne $mfg -and $mfg.Count -eq 1 -and $mfg[0] -match '^%Mfg%\s*=\s*(.*)$') { $got = ($Matches[1] -replace '\s', '') }
    if ($got -ne $want) { Fail "PATH-MFGDEC" "[Manufacturer] is '$got', not %Mfg%=$want" }

    $modelSec = "NetModels"
    $ntSuffix = ".NTx86"
    $sixTwo = "NetModels.NTx86.6.2"
    if ($A -eq "amd64") { $modelSec = "NetModels.NTamd64"; $ntSuffix = ".NTamd64"; $sixTwo = "NetModels.NTamd64.6.2" }
    $models = Get-Section $inf $modelSec
    $installs = @()
    if ($null -eq $models -or $models.Count -ne 1) {
        Fail "PATH-ID" "[$modelSec] does not hold exactly one model"
    } else {
        if ($models[0] -notmatch '^%NetDesc%\s*=\s*([^,\s]+)\s*,\s*XHCICM\\TBNET$') {
            Fail "PATH-ID" "[$modelSec]'s model is not %NetDesc%=<install>,XHCICM\TBNET"
        } else {
            $installs += $Matches[1]
        }
    }
    if ($A -eq "x86") {
        $stage = Get-Section $inf "NetModels.NTx86"
        if ($null -eq $stage -or $stage.Count -ne 1 -or $null -eq $models -or $models.Count -ne 1 -or
            ($stage[0] -replace '\s', '') -ne ($models[0] -replace '\s', '')) {
            Fail "PATH-NTSTAGE" "[NetModels.NTx86] does not hold [NetModels]'s one model and install"
        }
    }
    $six = Get-Section $inf $sixTwo
    $installs6 = @()
    if ($null -eq $six) { Fail "PATH-NT62" "no [$sixTwo]" }
    elseif ($six.Count -ne 1) { Fail "PATH-NT62" "[$sixTwo] does not hold exactly one model" }
    elseif ($six[0] -notmatch '^%NetDesc%\s*=\s*([^,\s]+)\s*,\s*XHCICM\\TBNET$') {
        Fail "PATH-NT62" "[$sixTwo]'s model is not %NetDesc%=<install>,XHCICM\TBNET"
    } elseif ($installs -contains $Matches[1]) {
        Fail "PATH-NT62" "[$sixTwo] names the NDIS 5 install [$($Matches[1])]: 8 to 10 need an install of xhcinet6.sys of its own"
    } else {
        $installs6 += $Matches[1]
    }
    foreach ($i in $installs6) {
        if ($null -ne (Get-Section $inf $i)) { Fail "PATH-NO9X" "an undecorated twin [$i] of the NDIS 6 install" }
    }
    if ($A -eq "amd64") {
        if ($null -ne (Get-Section $inf "NetModels")) { Fail "PATH-NO9X" "an undecorated [NetModels] in the 64-bit file" }
        foreach ($i in $installs) {
            if ($null -ne (Get-Section $inf $i)) { Fail "PATH-NO9X" "an undecorated install section [$i] in the 64-bit file" }
        }
    }

    $paths = @()
    foreach ($i in $installs) {
        if ($A -eq "x86") { $paths += @{ Kind = "9x"; Name = $i; Image = "xhcinet.sys" } }
        $paths += @{ Kind = "nt"; Name = ($i + $ntSuffix); Image = "xhcinet.sys" }
    }
    foreach ($i in $installs6) {
        $paths += @{ Kind = "nt6"; Name = ($i + $ntSuffix); Image = "xhcinet6.sys" }
    }

    foreach ($p in $paths) {
        $sec = Get-Section $inf $p.Name
        if ($null -eq $sec) { Fail "BOTH-SECTION" "no install section [$($p.Name)]"; continue }
        $addReg = @(Get-Keys $sec "AddReg")
        $copy = @(Get-Keys $sec "CopyFiles")
        foreach ($n in ($addReg + $copy)) {
            if ($null -eq (Get-Section $inf $n)) { Fail "BOTH-SECTION" "[$($p.Name)] names [$n], which is missing" }
        }
        $rows = Get-RegLines $inf $addReg

        # One write a target: a second row for the same value would undo
        # the first (an overwrite after the no-overwrite default, say).
        $seen = @{}
        foreach ($r in $rows) {
            $t = ($r[0] + "," + $r[1] + "," + $r[2]).ToLowerInvariant()
            if ($seen.ContainsKey($t)) { Fail "BOTH-DUPREG" "[$($p.Name)] writes $($r[0]),$($r[1]),$($r[2]) twice" }
            $seen[$t] = $true
        }
        # Every row but the driver's values and the binary NDIS version a
        # plain string write.
        foreach ($r in $rows) {
            $isValue = ($r[1] -eq "" -and $r[2] -like "XhciNet*")
            $isBin = ($r[1] -ieq "NDIS" -and ($r[2] -ieq "MajorNdisVersion" -or $r[2] -ieq "MinorNdisVersion"))
            if ($isBin) {
                if ($r[3] -ne "1") { Fail "BOTH-REGFLAGS" "[$($p.Name)] HKR,$($r[1]),$($r[2]) flags '$($r[3])' are not 1 (binary)" }
            } elseif (-not $isValue -and -not (Test-StrFlags $r[3])) {
                Fail "BOTH-REGFLAGS" "[$($p.Name)] HKR,$($r[1]),$($r[2]) flags '$($r[3])' are not a plain string write"
            }
        }
        $dirs = Get-Section $inf "DestinationDirs"
        $copiesImage = $false
        $dirOk = $true
        $other = "xhcinet6.sys"
        if ($p.Image -eq "xhcinet6.sys") { $other = "xhcinet.sys" }
        foreach ($c in $copy) {
            $cs = Get-Section $inf $c
            if ($null -ne $cs -and ($cs -contains $other)) {
                Fail "NETNT-IMAGE" "[$($p.Name)] copies $other, the other path's image"
            }
            if ($null -ne $cs -and ($cs -contains $p.Image)) {
                $copiesImage = $true
                $d = Get-Key $dirs $c
                if ($null -eq $d) { $d = Get-Key $dirs "DefaultDestDir" }
                $wantDir = "12"
                if ($p.Kind -eq "9x") { $wantDir = "11" }
                if ($d -ne $wantDir) { $dirOk = $false }
            }
        }

        if ($p.Kind -eq "9x") {
            # NET9X-NOCOPY: the install section and its Ndi\Install sections.
            $noCopy = @(@{ Name = $p.Name; Lines = $sec })
            foreach ($k in @("ndis3", "ndis4", "ndis5")) {
                $r = Find-Reg $rows "Ndi\Install" $k
                if ($null -ne $r) {
                    $s = Get-Section $inf $r[4]
                    if ($null -ne $s) { $noCopy += @{ Name = $r[4]; Lines = $s } }
                }
            }
            foreach ($nc in $noCopy) {
                foreach ($l in $nc.Lines) {
                    if ($l -match '^(CopyFiles|Include|Needs)\s*=') {
                        Fail "NET9X-NOCOPY" "[$($nc.Name)] has $($Matches[1]): the 98 SE and ME install copies nothing (Setup places xhcinet.sys)"
                    }
                }
            }
            $need = @(
                @("", "DevLoader", "*ndis"), @("", "DeviceVxDs", "xhcinet.sys"),
                @("", "EnumPropPages", "netdi.dll,EnumPropPages"),
                @("NDIS", "MajorNdisVersion", "05"), @("NDIS", "MinorNdisVersion", "00"),
                @("Ndi", "DeviceID", "XHCICM\TBNET"),
                @("Ndi\Interfaces", "DefLower", "ethernet"), @("Ndi\Interfaces", "LowerRange", "ethernet"),
                @("Ndi\Interfaces", "DefUpper", "ndis3"))
            foreach ($n in $need) {
                $r = Find-Reg $rows $n[0] $n[1]
                if ($null -eq $r -or $r[4] -ne $n[2]) { Fail "NET9X-KEY" "[$($p.Name)] does not write HKR,$($n[0]),$($n[1]) = $($n[2])" }
            }
            $up = Find-Reg $rows "Ndi\Interfaces" "UpperRange"
            if ($null -eq $up -or $up[4] -notmatch 'ndis5') { Fail "NET9X-KEY" "[$($p.Name)]'s UpperRange does not include ndis5" }
            foreach ($k in @("ndis3", "ndis4", "ndis5")) {
                $r = Find-Reg $rows "Ndi\Install" $k
                if ($null -eq $r -or $null -eq (Get-Section $inf $r[4])) { Fail "NET9X-KEY" "[$($p.Name)]'s Ndi\Install,$k does not name a section" }
            }
        } else {
            if ((Get-Key $sec "Characteristics") -ne "0x84") { Fail "NETNT-CHAR" "[$($p.Name)] Characteristics is not 0x84" }
            if ($null -eq (Get-Key $sec "BusType")) { Fail "NETNT-BUSTYPE" "[$($p.Name)] has no BusType" }
            if (-not $copiesImage -or -not $dirOk) { Fail "NETNT-COPY" "[$($p.Name)] does not copy $($p.Image) to dirid 12" }
            if ($null -ne (Find-Reg $rows "" "DevLoader")) { Fail "NETNT-NO9XKEY" "[$($p.Name)] writes the 9x DevLoader" }
            $svcSec = Get-Section $inf ($p.Name + ".Services")
            $svcOk = $false
            if ($null -ne $svcSec) {
                foreach ($l in $svcSec) {
                    if ($l -match '^AddService\s*=\s*xhcinet\s*,\s*0x0*2\s*,\s*([^,\s]+)$') {
                        $svc = Get-Section $inf $Matches[1]
                        if ($null -ne $svc -and (Get-Key $svc "ServiceType") -eq "1" -and (Get-Key $svc "StartType") -eq "3" -and
                            (Get-Key $svc "ServiceBinary") -eq ('%12%\' + $p.Image) -and (Get-Key $svc "LoadOrderGroup") -eq "NDIS") {
                            $svcOk = $true
                        }
                    }
                }
            }
            if (-not $svcOk) { Fail "NETNT-SERVICE" "[$($p.Name).Services] does not add the xhcinet service as the function driver (flag 2, kernel driver, demand start, %12%\$($p.Image), group NDIS)" }
            $r = Find-Reg $rows "Ndi" "Service"
            if ($null -eq $r -or $r[4] -ne "xhcinet") { Fail "NETNT-NDI" "[$($p.Name)] does not write Ndi,Service xhcinet" }
            $r = Find-Reg $rows "Ndi\Interfaces" "UpperRange"
            if ($null -eq $r -or $r[4] -ne "ndis5") { Fail "NETNT-NDI" "[$($p.Name)] UpperRange is not ndis5" }
            $r = Find-Reg $rows "Ndi\Interfaces" "LowerRange"
            if ($null -eq $r -or $r[4] -ne "ethernet") { Fail "NETNT-NDI" "[$($p.Name)] LowerRange is not ethernet" }
        }

        # NET6-*
        foreach ($kw in $script:Keywords) {
            $lines = @($sec | Where-Object { $_ -match ('^' + [regex]::Escape($kw.Name) + '\s*=') })
            if ($p.Kind -ne "nt6") {
                if ($lines.Count -ne 0) { Fail "NET6-KEYWORD" "[$($p.Name)] carries $($kw.Name), an NDIS 6 keyword, off the 6.2 path" }
                continue
            }
            if ($lines.Count -ne 1) { Fail "NET6-KEYWORD" "[$($p.Name)] carries $($kw.Name) $($lines.Count) times, not once"; continue }
            $got = (Get-Key $sec $kw.Name)
            if ($got -ne $kw.Value) { Fail "NET6-KEYWORD" "[$($p.Name)] $($kw.Name) is '$got', not $($kw.Value)" }
        }
        foreach ($l in $sec) {
            if ($l -match '^(\*[A-Za-z]+)\s*=') {
                $known = $false
                foreach ($kw in $script:Keywords) { if ($Matches[1] -ieq $kw.Name) { $known = $true } }
                if (-not $known) { Fail "NET6-KEYWORD" "[$($p.Name)] carries $($Matches[1]), which is not in the keyword table" }
            }
        }

        # VAL-*
        foreach ($val in $script:Values) {
            $r = Find-Reg $rows "" $val.Name
            if ($null -eq $r) { Fail "VAL-WRITTEN" "[$($p.Name)] does not write $($val.Name)"; continue }
            if ($r[3] -ne "0x00000002") { Fail "VAL-NOCLOBBER" "[$($p.Name)] writes $($val.Name) with flags '$($r[3])', not 0x00000002 (a string, no-overwrite)" }
            if ($r[4] -ne $val.Default) { Fail "VAL-DEFAULT" "[$($p.Name)] writes $($val.Name) = '$($r[4])', not its default $($val.Default)" }
            $pk = "Ndi\params\" + $val.Name
            $pd = Find-Reg $rows $pk "ParamDesc"
            $ty = Find-Reg $rows $pk "type"
            $de = Find-Reg $rows $pk "default"
            $mi = Find-Reg $rows $pk "min"
            $ma = Find-Reg $rows $pk "max"
            if ($null -eq $pd) { Fail "VAL-PARAM" "[$($p.Name)] has no $pk ParamDesc" }
            if ($null -eq $ty -or $ty[4] -ne "dword") { Fail "VAL-PARAM" "[$($p.Name)] $pk type is not dword" }
            if ($null -eq $de -or $de[4] -ne $val.Default) { Fail "VAL-PARAM" "[$($p.Name)] $pk default is not $($val.Default)" }
            if ($null -eq $mi -or $mi[4] -ne $val.Min) { Fail "VAL-PARAM" "[$($p.Name)] $pk min is not $($val.Min)" }
            if ($null -eq $ma -or $ma[4] -ne $val.Max) { Fail "VAL-PARAM" "[$($p.Name)] $pk max is not $($val.Max)" }
        }
        foreach ($r in $rows) {
            $known = $false
            if ($r[1] -eq "" -and $r[2] -like "XhciNet*") {
                foreach ($val in $script:Values) { if ($r[2] -ieq $val.Name) { $known = $true } }
                if (-not $known) { Fail "VAL-UNKNOWN" "[$($p.Name)] writes $($r[2]), which is not in the value table" }
            }
        }
    }
    return $fails
}

function Invoke-Check([string]$path, [string]$a) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    return @(Test-NetInf -Text $text -Bytes $bytes -A $a -Label (Split-Path -Leaf $path))
}

if ($SelfTest) {
    $bad = 0
    # The DriverVer mutation takes the version from the header, so a bump
    # leaves it a mutation of the committed INF.
    $curVer = (Get-Version).Version
    $curDate = (Get-Version).Date
    $cases = @(
        @{ A = "x86"; Rule = "FILE-CRLF"; From = "[Version]`r`n"; To = "[Version]`n" },
        @{ A = "x86"; Rule = "FILE-ASCII"; From = 'MtuDesc="MTU"'; To = ('MtuDesc="MT' + [char]0xE9 + '"') },
        @{ A = "x86"; Rule = "W98-DUPSECTION"; From = "[Net.Ndis]"; To = "[Net.Ndis]`r`n[Net.Ndis]" },
        @{ A = "x86"; Rule = "W98-SECTIONLEN"; From = "[Net.Ndis]"; To = "[Net.Ndis.AVeryLongSectionNameIndeed]" },
        @{ A = "x86"; Rule = "BOTH-CLASS"; From = "Class=Net"; To = "Class=System" },
        @{ A = "x86"; Rule = "BOTH-COMPATIBLE"; From = "Compatible=1`r`n"; To = "" },
        @{ A = "x86"; Rule = "BOTH-DRIVERVER"; From = ("," + $curVer); To = ",9.9.9.9" },
        @{ A = "amd64"; Rule = "BOTH-DRIVERVER"; From = ("DriverVer=" + $curDate + ","); To = "DriverVer=01/02/2001," },
        @{ A = "x86"; Rule = "BOTH-DRIVERVER"; From = ("DriverVer=" + $curDate + ","); To = "DriverVer=1/2/2001," },
        @{ A = "x86"; Rule = "BOTH-CATALOG"; From = "Provider=%Provider%"; To = "Provider=%Provider%`r`nCatalogFile=xhcinet.cat" },
        @{ A = "amd64"; Rule = "BOTH-CATALOG"; From = "CatalogFile=xhcinet.cat`r`n"; To = "" },
        @{ A = "x86"; Rule = "BOTH-STRING"; From = 'MtuDesc="MTU"'; To = 'MtuDescX="MTU"' },
        @{ A = "x86"; Rule = "BOTH-FILES"; From = "xhcinet.sys=1"; To = "xhcinet.sys=1`r`nxhcicm.sys=1" },
        @{ A = "amd64"; Rule = "BOTH-FILES"; From = "xhcinet6.sys=1`r`n"; To = "" },
        @{ A = "x86"; Rule = "BOTH-SECTION"; From = "AddReg=Net.Reg9x,Net.Values,Net.Params"; To = "AddReg=Net.Reg9x,Net.Values,Net.Params,Net.Missing" },
        @{ A = "x86"; Rule = "PATH-MFGDEC"; From = "NetModels,NTx86,NTx86.6.2"; To = "NetModels,NTx86.6.2" },
        @{ A = "x86"; Rule = "PATH-NTSTAGE"; From = "[NetModels.NTx86]`r`n%NetDesc%=Net.Dev,XHCICM\TBNET`r`n"; To = "[NetModels.NTx86]`r`n" },
        @{ A = "x86"; Rule = "PATH-NTSTAGE"; From = "[NetModels.NTx86]`r`n%NetDesc%=Net.Dev,"; To = "[NetModels.NTx86]`r`n%NetDesc%=Net6.Dev," },
        @{ A = "amd64"; Rule = "PATH-MFGDEC"; From = "NetModels,NTamd64,NTamd64.6.2"; To = "NetModels,NTamd64" },
        @{ A = "x86"; Rule = "PATH-ID"; From = "Net.Dev,XHCICM\TBNET"; To = "Net.Dev,XHCI98\TBNET" },
        @{ A = "x86"; Rule = "PATH-NT62"; From = "%NetDesc%=Net6.Dev,XHCICM\TBNET"; To = "%NetDesc%=Net.Dev,XHCICM\TBNET" },
        @{ A = "amd64"; Rule = "PATH-NT62"; From = "%NetDesc%=Net6.Dev,XHCICM\TBNET`r`n"; To = "" },
        @{ A = "x86"; Rule = "PATH-NT62"; From = "%NetDesc%=Net6.Dev,XHCICM\TBNET"; To = "%NetDesc%=Net6.Dev,XHCICM\TBNET6" },
        @{ A = "x86"; Rule = "PATH-NO9X"; From = "[Net6.Dev.NTx86]"; To = "[Net6.Dev]`r`nCopyFiles=Net6.CopyFiles`r`n[Net6.Dev.NTx86]" },
        @{ A = "x86"; Rule = "NETNT-IMAGE"; From = "[Net6.CopyFiles]`r`nxhcinet6.sys"; To = "[Net6.CopyFiles]`r`nxhcinet6.sys`r`nxhcinet.sys" },
        @{ A = "amd64"; Rule = "NETNT-IMAGE"; From = "[Net.CopyFiles]`r`nxhcinet.sys"; To = "[Net.CopyFiles]`r`nxhcinet.sys`r`nxhcinet6.sys" },
        @{ A = "x86"; Rule = "NETNT-COPY"; From = "Net6.CopyFiles=12"; To = "Net6.CopyFiles=11" },
        @{ A = "amd64"; Rule = "NETNT-SERVICE"; From = "ServiceBinary=%12%\xhcinet6.sys"; To = "ServiceBinary=%12%\xhcinet.sys" },
        @{ A = "x86"; Rule = "NETNT-CHAR"; From = "[Net6.Dev.NTx86]`r`nCharacteristics=0x84"; To = "[Net6.Dev.NTx86]`r`nCharacteristics=0x4" },
        @{ A = "x86"; Rule = "NET6-KEYWORD"; From = "*IfType=6"; To = "*IfType=71" },
        @{ A = "amd64"; Rule = "NET6-KEYWORD"; From = "*MediaType=0`r`n"; To = "" },
        @{ A = "x86"; Rule = "NET6-KEYWORD"; From = "*PhysicalMediaType=0"; To = "*PhysicalMediaType=0`r`n*PhysicalMediaType=14" },
        @{ A = "amd64"; Rule = "NET6-KEYWORD"; From = "*PhysicalMediaType=0"; To = "*PhysicalMediaType=0`r`n*IfConnectorPresent=1" },
        @{ A = "x86"; Rule = "NET6-KEYWORD"; From = "[Net.Dev.NTx86]`r`nCharacteristics=0x84"; To = "[Net.Dev.NTx86]`r`nCharacteristics=0x84`r`n*IfType=6" },
        @{ A = "amd64"; Rule = "VAL-WRITTEN"; From = "[Net6.Dev.NTamd64]`r`nCharacteristics=0x84`r`nBusType=15`r`n*IfType=6`r`n*MediaType=0`r`n*PhysicalMediaType=0`r`nAddReg=Net.RegNt,Net.Values,Net.Params"; To = "[Net6.Dev.NTamd64]`r`nCharacteristics=0x84`r`nBusType=15`r`n*IfType=6`r`n*MediaType=0`r`n*PhysicalMediaType=0`r`nAddReg=Net.RegNt,Net.Params" },
        @{ A = "amd64"; Rule = "PATH-NO9X"; From = "[ControlFlags]"; To = "[NetModels]`r`n%NetDesc%=Net.Dev,XHCICM\TBNET`r`n[ControlFlags]" },
        @{ A = "x86"; Rule = "NET9X-KEY"; From = "HKR,,DevLoader,,*ndis"; To = "HKR,,DevLoader,,*ntkern" },
        @{ A = "x86"; Rule = "NET9X-KEY"; From = "HKR,NDIS,MajorNdisVersion,1,05"; To = "HKR,NDIS,MajorNdisVersion,1,04" },
        @{ A = "x86"; Rule = "NET9X-NOCOPY"; From = "[Net.Dev]`r`nAddReg="; To = "[Net.Dev]`r`nCopyFiles=Net.CopyFiles`r`nAddReg=" },
        @{ A = "x86"; Rule = "NET9X-NOCOPY"; From = "[Net.Ndis]`r`n"; To = "[Net.Ndis]`r`nCopyFiles=Net.CopyFiles`r`n" },
        @{ A = "x86"; Rule = "NET9X-NOCOPY"; From = "[Net.Dev]`r`nAddReg="; To = "[Net.Dev]`r`nNeeds=Net.Dev.NTx86`r`nAddReg=" },
        @{ A = "x86"; Rule = "NET9X-NOCOPY"; From = "[Net.Ndis]`r`n"; To = "[Net.Ndis]`r`nInclude=netoem.inf`r`n" },
        @{ A = "x86"; Rule = "NETNT-CHAR"; From = "Characteristics=0x84"; To = "Characteristics=0x4" },
        @{ A = "amd64"; Rule = "NETNT-BUSTYPE"; From = "BusType=15`r`n"; To = "" },
        @{ A = "x86"; Rule = "NETNT-SERVICE"; From = "AddService=xhcinet,0x00000002,"; To = "AddService=xhcinet,0x00000000," },
        @{ A = "amd64"; Rule = "NETNT-SERVICE"; From = "LoadOrderGroup=NDIS"; To = "LoadOrderGroup=Base" },
        @{ A = "x86"; Rule = "NETNT-NDI"; From = 'HKR,Ndi\Interfaces,UpperRange,,"ndis5"'; To = 'HKR,Ndi\Interfaces,UpperRange,,"ndis4"' },
        @{ A = "x86"; Rule = "VAL-NOCLOBBER"; From = 'XhciNetMtu,0x00000002,"1500"'; To = 'XhciNetMtu,0x00000000,"1500"' },
        @{ A = "amd64"; Rule = "VAL-DEFAULT"; From = 'XhciNetMtu,0x00000002,"1500"'; To = 'XhciNetMtu,0x00000002,"9000"' },
        @{ A = "x86"; Rule = "VAL-WRITTEN"; From = "AddReg=Net.RegNt,Net.Values,Net.Params"; To = "AddReg=Net.RegNt,Net.Params" },
        @{ A = "x86"; Rule = "VAL-PARAM"; From = 'XhciNetMtu,max,,"65522"'; To = 'XhciNetMtu,max,,"65535"' },
        @{ A = "amd64"; Rule = "VAL-PARAM"; From = 'XhciNetMtu,type,,dword'; To = 'XhciNetMtu,type,,int' },
        @{ A = "x86"; Rule = "BOTH-DUPREG"; From = 'HKR,,XhciNetMtu,0x00000002,"1500"'; To = "HKR,,XhciNetMtu,0x00000002,`"1500`"`r`nHKR,,XhciNetMtu,0,`"9000`"" },
        @{ A = "amd64"; Rule = "BOTH-DUPREG"; From = 'HKR,,XhciNetMtu,0x00000002,"1500"'; To = "HKR,,XhciNetMtu,0x00000002,`"1500`"`r`nHKR,,`"XhciNetMtu`",0,`"9000`"" },
        @{ A = "amd64"; Rule = "BOTH-REGFLAGS"; From = "HKR,Ndi,Service,,xhcinet"; To = "HKR,Ndi,Service,0x00000004,xhcinet" },
        @{ A = "x86"; Rule = "BOTH-REGFLAGS"; From = "HKR,NDIS,MajorNdisVersion,1,05"; To = "HKR,NDIS,MajorNdisVersion,0,05" },
        @{ A = "x86"; Rule = "BOTH-REGFLAGS"; From = "HKR,,DevLoader,,*ndis"; To = "HKR,,DevLoader,0x00010001,*ndis" },
        @{ A = "x86"; Rule = "VAL-UNKNOWN"; From = 'HKR,,XhciNetMtu,0x00000002,"1500"'; To = "HKR,,XhciNetMtu,0x00000002,`"1500`"`r`nHKR,,XhciNetPoll,0x00000002,`"1`"" }
    )
    foreach ($a in @("x86", "amd64")) {
        $f = @(Invoke-Check (Get-InfFor $a) $a)
        if ($f.Count -ne 0) { Write-Host "FAIL selftest: the committed $a INF does not pass:"; $f | ForEach-Object { Write-Host "  $_" }; $bad++ }
    }
    foreach ($c in $cases) {
        $path = Get-InfFor $c.A
        $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path))
        if (-not $text.Contains($c.From)) { Write-Host "FAIL selftest: '$($c.From)' not found in $($c.A) INF ($($c.Rule))"; $bad++; continue }
        $idx = $text.IndexOf($c.From)
        $mut = $text.Substring(0, $idx) + $c.To + $text.Substring($idx + $c.From.Length)
        $bytes = New-Object byte[] $mut.Length
        for ($i = 0; $i -lt $mut.Length; $i++) { $bytes[$i] = [byte]([int][char]$mut[$i] -band 0xFF) }
        $f = @(Test-NetInf -Text $mut -Bytes $bytes -A $c.A -Label "mutant")
        $hit = @($f | Where-Object { $_ -like ($c.Rule + " *") })
        if ($hit.Count -eq 0) {
            Write-Host "FAIL selftest: mutation for $($c.Rule) ($($c.A)) was not refused by that rule; got: $($f -join ' | ')"
            $bad++
        }
    }
    if ($bad -ne 0) { Write-Host "check-net-inf self-test: $bad failures"; exit 1 }
    Write-Host ("check-net-inf self-test: {0} mutations refused by their rules, both INFs pass" -f $cases.Count)
    exit 0
}

if ($InfPath -eq "") { $InfPath = Get-InfFor $Arch }
$fails = @(Invoke-Check $InfPath $Arch)
if ($fails.Count -ne 0) {
    $fails | ForEach-Object { Write-Host "FAIL $_" }
    Write-Host "check-net-inf: $($fails.Count) failures in $InfPath"
    exit 1
}
Write-Host "check-net-inf: $InfPath passes ($Arch)"
exit 0
