<#
.SYNOPSIS
Gate for src\txtsetup.oem and src\txtsetup-amd64.oem, the text-mode Setup
driver descriptions (roadmap-hcd.md task 33.3; design record 13 section 5.6).

.DESCRIPTION
Text-mode Setup of Windows 2000 and XP reads txtsetup.oem from drive A: when
the user presses F6, and reports a malformed one as a bare "file could not be
loaded" or simply does not offer the driver. This gate holds the file to the
shape section 5.6 settled, each rule named as check-inf.ps1 names its
families.

  FILE-*   ASCII only, no BOM, CRLF only, no control byte.
  OEM-SECTION  Exactly the five sections [Disks], [Defaults], [scsi],
           [Files.scsi.XHCI98] and [HardwareIds.scsi.XHCI98], each once.
           "scsi" is the one component class F6 offers; a keyboard,
           computer, display or mouse section is refused by name.
  OEM-QUOTE  A quote anywhere is balanced and wraps its whole field.
  OEM-DEFAULT  [Defaults] is "scsi = XHCI98" alone, and [scsi] names XHCI98
           alone, with a quoted description.
  OEM-DISK [Disks] is one disk, d1, whose tag file is \xhci98.sys and whose
           directory is the disk root - where the package stages the driver.
  OEM-FILES [Files.scsi.XHCI98] is "driver = d1, xhci98.sys, xhci98" and
           "inf = d1, xhci98.inf" and nothing else: no catalog (the package is
           not signed), no dll, no second driver (xhciuas.sys is not loaded in
           text mode; section 5.6).
  OEM-SERVICE  The driver line's service key is the one service the INF beside
           it adds (AddService, every model the same one), so GUI-mode
           Setup's INF install updates the service text mode created rather
           than adding a second one.
  OEM-IDS  [HardwareIds.scsi.XHCI98] maps exactly the INF's model ids
           (PCI\CC_0C0330 and XHCI98\ROOT_HUB) to that service. OEM-USBID: no
           USB\ id at all - USB\ROOT_HUB and the class ids belong to Setup's
           own drivers (txtsetup.sif's [HardwareIdsDatabase]), and taking one
           would put xhci98.sys on a device usbhub.sys or hidusb.sys drives.
  OEM-MSFILE  No Microsoft file is named anywhere: usbd.sys, the HID and
           storage drivers come from Setup's own source.
  OEM-83   Every file name is 8.3.
  OEM-ARCH The x86 file's descriptions say "32-bit" and the amd64 file's say
           "x64", so a user offered both at the F6 list can tell them apart.
  OEM-PAIR The two committed files carry the same entries everywhere except
           the [Disks] and [scsi] descriptions, since the accepted cost of two
           files is that they can drift.
  PKG-*    With -PackageDir: txtsetup.oem is at the package root (PKG-OEM),
           every file it names and its tag file are beside it (PKG-LAYOUT),
           and no Microsoft file is anywhere in the package (PKG-MSFILE).

-SelfTest mutates both committed files in memory, one rule at a time, and
fails unless the gate refuses every mutation and passes the originals, then
stages stand-in packages in a temporary directory (removed afterwards). It is
run by scripts\build-driver.cmd before the gate itself.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\inf-gate\check-txtsetup-oem.ps1 -Arch amd64
#>

[CmdletBinding()]
param(
    [string]$OemPath = "",
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
$oemFor = @{ x86 = (Join-Path $repo "src\txtsetup.oem"); amd64 = (Join-Path $repo "src\txtsetup-amd64.oem") }
$infFor = @{ x86 = (Join-Path $repo "src\xhci98.inf"); amd64 = (Join-Path $repo "src\xhci98-amd64.inf") }
$defaultOem = ($OemPath -eq "")
if ($OemPath -eq "") { $OemPath = $oemFor[$Arch] }
if ($InfPath -eq "") { $InfPath = $infFor[$Arch] }

$optionId = "XHCI98"
$binary = "xhci98.sys"
$infName = "xhci98.inf"
$sections = @("Disks", "Defaults", "scsi", "Files.scsi.$optionId", "HardwareIds.scsi.$optionId")
$msFiles = @("usbd.sys", "usbhub.sys", "usbport.sys", "usbui.dll", "usbhub20.sys",
             "usbehci.sys", "usbohci.sys", "usbuhci.sys", "openhci.sys", "uhcd.sys",
             "usbccgp.sys", "usbstor.sys", "disk.sys", "classpnp.sys", "hidusb.sys",
             "hidclass.sys", "hidparse.sys", "kbdhid.sys", "mouhid.sys", "kbdclass.sys",
             "mouclass.sys", "hid.dll", "ntmap.sys", "usbntmap.sys", "uaspstor.sys",
             "txtsetup.sif", "layout.inf", "usbd98.sys", "usbd2k.sys", "usbhub98.sys")
$classWords = @("keyboard", "computer", "display", "mouse", "keyboardlayout")

function Read-OemText {
    param([string]$Text)
    # Sections in order, each a list of (key, value) entries. '#' and ';'
    # outside quotes start a comment.
    $list = New-Object System.Collections.ArrayList
    $current = $null
    foreach ($rawLine in ($Text -split "`r`n")) {
        $line = $rawLine
        $inQuote = $false
        for ($i = 0; $i -lt $line.Length; $i++) {
            if ($line[$i] -eq '"') { $inQuote = -not $inQuote }
            elseif (($line[$i] -eq '#' -or $line[$i] -eq ';') -and -not $inQuote) { $line = $line.Substring(0, $i); break }
        }
        $line = $line.Trim()
        if ($line -eq "") { continue }
        if ($line -match '^\[(.+)\]$') {
            $current = @{ Name = $matches[1].Trim(); Lines = (New-Object System.Collections.ArrayList) }
            [void]$list.Add($current)
            continue
        }
        $key = $line; $value = ""
        $eq = $line.IndexOf('=')
        if ($eq -ge 0) { $key = $line.Substring(0, $eq).Trim(); $value = $line.Substring($eq + 1).Trim() }
        if ($null -eq $current) {
            $current = @{ Name = ""; Lines = (New-Object System.Collections.ArrayList) }
            [void]$list.Add($current)
        }
        [void]$current.Lines.Add(@{ Key = $key; Value = $value })
    }
    return $list
}

function Split-OemFields {
    param([string]$Value)
    # Comma-separated fields; a quoted field keeps its commas and loses its
    # quotes. Returns @{ Text; Quoted; Bad } per field: Bad when a quote is
    # unbalanced or does not wrap the whole field (OEM-QUOTE).
    $out = New-Object System.Collections.ArrayList
    $fields = New-Object System.Collections.ArrayList
    $buf = ""
    $inQuote = $false
    foreach ($ch in $Value.ToCharArray()) {
        if ($ch -eq '"') { $inQuote = -not $inQuote }
        if ($ch -eq ',' -and -not $inQuote) { [void]$fields.Add($buf); $buf = ""; continue }
        $buf += $ch
    }
    [void]$fields.Add($buf)
    for ($k = 0; $k -lt $fields.Count; $k++) {
        $raw = ([string]$fields[$k]).Trim()
        $n = @($raw.ToCharArray() | Where-Object { $_ -eq '"' }).Count
        $quoted = ($n -gt 0)
        $bad = $false
        if ($n -ne 0 -and ($n -ne 2 -or -not ($raw.StartsWith('"') -and $raw.EndsWith('"')))) { $bad = $true }
        if ($k -eq $fields.Count - 1 -and $inQuote) { $bad = $true }
        $text = $raw
        if ($quoted -and -not $bad) { $text = $raw.Substring(1, $raw.Length - 2).Trim() }
        elseif ($quoted) { $text = $raw.Replace('"', '').Trim() }
        [void]$out.Add(@{ Text = $text; Quoted = $quoted; Bad = $bad })
    }
    return $out
}

function Get-InfFacts {
    # The service names AddService adds and the ids every models section
    # binds, from the INF beside the oem file.
    param([string]$Text)
    $services = @{}
    $ids = @{}
    $models = @()
    $inMfg = $false
    $current = ""
    $mfgTargets = @()
    foreach ($rawLine in ($Text -split "`r`n")) {
        $line = $rawLine
        $semi = $line.IndexOf(';')
        if ($semi -ge 0) { $line = $line.Substring(0, $semi) }
        $line = $line.Trim()
        if ($line -eq "") { continue }
        if ($line -match '^\[(.+)\]$') { $current = $matches[1].Trim(); continue }
        if ($current -eq "Manufacturer" -and $line -match '=\s*(.+)$') {
            $f = @($matches[1].Split(',') | ForEach-Object { $_.Trim() })
            $mfgTargets += $f[0]
            for ($k = 1; $k -lt $f.Count; $k++) { $mfgTargets += ($f[0] + "." + $f[$k]) }
        }
        if ($line -match '^AddService\s*=\s*([^,\s]+)') { $services[$matches[1].ToLowerInvariant()] = $true }
    }
    $current = ""
    foreach ($rawLine in ($Text -split "`r`n")) {
        $line = $rawLine
        $semi = $line.IndexOf(';')
        if ($semi -ge 0) { $line = $line.Substring(0, $semi) }
        $line = $line.Trim()
        if ($line -eq "") { continue }
        if ($line -match '^\[(.+)\]$') { $current = $matches[1].Trim(); continue }
        if ($mfgTargets -contains $current -and $line -match '=\s*[^,]+,\s*(.+)$') {
            foreach ($id in @($matches[1].Split(',') | ForEach-Object { $_.Trim() })) {
                if ($id -ne "") { $ids[$id.ToUpperInvariant()] = $true }
            }
        }
    }
    return @{ Services = $services; Ids = $ids }
}

function Test-Is83 {
    param([string]$Name)
    return ($Name -match '^[A-Za-z0-9_\-]{1,8}(\.[A-Za-z0-9_\-]{1,3})?$')
}

function Test-OemText {
    param([byte[]]$Bytes, [string]$InfText, [string]$Package, [string]$Profile = "x86", [byte[]]$PairBytes = $null)

    $fail = New-Object System.Collections.ArrayList
    $add = { param($rule, $msg) [void]$fail.Add("$rule $msg") }

    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        & $add "FILE-BOM" "the file starts with a UTF-8 BOM."
    }
    for ($i = 0; $i -lt $Bytes.Length; $i++) {
        $b = $Bytes[$i]
        if ($b -ge 0x80) { & $add "FILE-ASCII" ("byte 0x{0:X2} at offset {1}." -f $b, $i); break }
        if ($b -lt 0x20 -and $b -ne 9 -and $b -ne 10 -and $b -ne 13) { & $add "FILE-CTRL" ("control byte 0x{0:X2} at offset {1}." -f $b, $i); break }
        if ($b -eq 10 -and ($i -eq 0 -or $Bytes[$i - 1] -ne 13)) { & $add "FILE-EOL" "a bare LF at offset $i."; break }
        if ($b -eq 13 -and ($i + 1 -ge $Bytes.Length -or $Bytes[$i + 1] -ne 10)) { & $add "FILE-EOL" "a bare CR at offset $i."; break }
    }

    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($Bytes)
    $parsed = Read-OemText -Text $text
    $byName = @{}
    foreach ($s in $parsed) {
        if ($s.Name -eq "") { & $add "OEM-SECTION" "entries before the first section header."; continue }
        $k = $s.Name.ToLowerInvariant()
        if ($byName.ContainsKey($k)) { & $add "OEM-SECTION" "[$($s.Name)] appears twice."; continue }
        $byName[$k] = $s
        if ($classWords -contains $k -or $k -match '^(files|hardwareids|config)\.(keyboard|computer|display|mouse)\.') {
            & $add "OEM-SECTION" "[$($s.Name)] is a component F6 does not offer; this file is a scsi component only."
        } elseif (-not ($sections | Where-Object { $_.ToLowerInvariant() -eq $k })) {
            & $add "OEM-SECTION" "[$($s.Name)] is not one of the five sections this file carries."
        }
    }
    $get = { param($n) if ($byName.ContainsKey($n.ToLowerInvariant())) { return $byName[$n.ToLowerInvariant()] } return $null }
    foreach ($n in $sections) {
        if ($null -eq (& $get $n)) { & $add "OEM-SECTION" "no [$n] section." }
    }

    foreach ($s in $parsed) {
        foreach ($l in $s.Lines) {
            foreach ($x in @(Split-OemFields -Value $l.Value)) {
                if ($x.Bad) { & $add "OEM-QUOTE" "[$($s.Name)] '$($l.Key) = $($l.Value)': a quote is unbalanced or does not wrap its whole field." }
            }
        }
    }

    $descs = @()
    $named = @()

    $def = & $get "Defaults"
    if ($null -ne $def) {
        if ($def.Lines.Count -ne 1 -or $def.Lines[0].Key -ne "scsi" -or $def.Lines[0].Value -ne $optionId) {
            & $add "OEM-DEFAULT" "[Defaults] must be 'scsi = $optionId' and nothing else."
        }
    }
    $scsi = & $get "scsi"
    if ($null -ne $scsi) {
        if ($scsi.Lines.Count -ne 1 -or $scsi.Lines[0].Key -ne $optionId) {
            & $add "OEM-DEFAULT" "[scsi] must name $optionId and nothing else."
        } else {
            $f = @(Split-OemFields -Value $scsi.Lines[0].Value)
            if ($f.Count -ne 1 -or -not $f[0].Quoted -or $f[0].Text -eq "") {
                & $add "OEM-DEFAULT" "[scsi] $optionId needs one quoted, non-empty description."
            } else { $descs += $f[0].Text }
        }
    }

    $disks = & $get "Disks"
    if ($null -ne $disks) {
        if ($disks.Lines.Count -ne 1 -or $disks.Lines[0].Key -ne "d1") {
            & $add "OEM-DISK" "[Disks] must be one disk, d1."
        } else {
            $f = @(Split-OemFields -Value $disks.Lines[0].Value)
            if ($f.Count -ne 3 -or -not $f[0].Quoted -or $f[0].Text -eq "" -or
                $f[1].Text -ne ("\" + $binary) -or $f[2].Text -ne "\") {
                & $add "OEM-DISK" "[Disks] d1 must be `"<description>`", \$binary, \ - the tag file is the driver, at the disk root."
            } else { $descs += $f[0].Text; $named += $binary }
        }
    }

    $files = & $get "Files.scsi.$optionId"
    $svc = ""
    if ($null -ne $files) {
        $seen = @{}
        foreach ($l in $files.Lines) {
            $f = @(Split-OemFields -Value $l.Value)
            foreach ($x in $f) { if ($x.Text -match '\.') { $named += $x.Text } }
            $kind = $l.Key.ToLowerInvariant()
            if ($seen.ContainsKey($kind)) { & $add "OEM-FILES" "a second '$($l.Key)' line."; continue }
            $seen[$kind] = $true
            if ($kind -eq "driver") {
                if ($f.Count -ne 3 -or $f[0].Text -ne "d1" -or $f[1].Text -ne $binary -or $f[2].Text -eq "") {
                    & $add "OEM-FILES" "the driver line must be 'driver = d1, $binary, <service>'."
                } else { $svc = $f[2].Text }
            } elseif ($kind -eq "inf") {
                if ($f.Count -ne 2 -or $f[0].Text -ne "d1" -or $f[1].Text -ne $infName) {
                    & $add "OEM-FILES" "the inf line must be 'inf = d1, $infName'."
                }
            } else {
                & $add "OEM-FILES" "'$($l.Key)' is not a line this file carries (driver and inf only)."
            }
        }
        foreach ($k in @("driver", "inf")) { if (-not $seen.ContainsKey($k)) { & $add "OEM-FILES" "no '$k' line." } }
    }

    $facts = Get-InfFacts -Text $InfText
    if ($svc -ne "" -and -not $facts.Services.ContainsKey($svc.ToLowerInvariant())) {
        & $add "OEM-SERVICE" "the driver's service key '$svc' is not a service the INF adds ($(@($facts.Services.Keys) -join ', '))."
    } elseif ($facts.Services.Count -gt 1) {
        # Every id below is mapped to the one service, so every model must
        # add that same service: an INF adding two would give some model a
        # service text mode did not create (whole-branch review of 2.1.0.0).
        & $add "OEM-SERVICE" "the INF adds more than one service ($(@($facts.Services.Keys) -join ', ')); every model this file maps must add '$svc' alone."
    }

    $hw = & $get "HardwareIds.scsi.$optionId"
    if ($null -ne $hw) {
        $mapped = @{}
        foreach ($l in $hw.Lines) {
            $f = @(Split-OemFields -Value $l.Value)
            if ($l.Key -ne "id" -or $f.Count -ne 2 -or -not $f[0].Quoted -or -not $f[1].Quoted) {
                & $add "OEM-IDS" "'$($l.Key) = $($l.Value)' is not an 'id = `"<id>`", `"<service>`"' line."
                continue
            }
            $id = $f[0].Text.ToUpperInvariant()
            if ($id.StartsWith("USB\")) {
                & $add "OEM-USBID" "'$($f[0].Text)' is a USB\ id; those belong to Setup's own drivers."
            }
            if ($mapped.ContainsKey($id)) { & $add "OEM-IDS" "'$($f[0].Text)' is mapped twice." }
            $mapped[$id] = $true
            if ($svc -ne "" -and $f[1].Text -ne $svc) {
                & $add "OEM-IDS" "'$($f[0].Text)' maps to '$($f[1].Text)', not the driver's service '$svc'."
            }
            if (-not $facts.Ids.ContainsKey($id)) {
                & $add "OEM-IDS" "'$($f[0].Text)' is not an id the INF's models bind."
            }
        }
        foreach ($id in $facts.Ids.Keys) {
            if (-not $mapped.ContainsKey($id)) { & $add "OEM-IDS" "the INF binds '$id' and this file does not map it." }
        }
    }

    foreach ($n in $named) {
        if ($msFiles -contains $n.TrimStart('\').ToLowerInvariant()) { & $add "OEM-MSFILE" "'$n' is Microsoft's; Setup loads it from its own source." }
        if (-not (Test-Is83 -Name $n.TrimStart('\'))) { & $add "OEM-83" "'$n' is not an 8.3 name." }
    }
    foreach ($s in $parsed) {
        foreach ($l in $s.Lines) {
            foreach ($x in @(Split-OemFields -Value $l.Value)) {
                $leaf = ($x.Text -split '\\')[-1].ToLowerInvariant()
                if ($msFiles -contains $leaf) {
                    & $add "OEM-MSFILE" "[$($s.Name)] names '$($x.Text)'; Setup loads it from its own source."
                }
            }
        }
    }

    $word = if ($Profile -eq "amd64") { "x64" } else { "32-bit" }
    $other = if ($Profile -eq "amd64") { "32-bit" } else { "x64" }
    foreach ($d in $descs) {
        if ($d -notmatch [regex]::Escape($word) -or $d -match [regex]::Escape($other)) {
            & $add "OEM-ARCH" "the $Profile description '$d' must say '$word' and not '$other'."
        }
    }

    if ($null -ne $PairBytes) {
        $strip = {
            param($t)
            $p = Read-OemText -Text $t
            $out = @()
            foreach ($s in $p) {
                foreach ($l in $s.Lines) {
                    $v = $l.Value
                    if ($s.Name -eq "Disks" -or $s.Name -eq "scsi") { $v = ($v -replace '"[^"]*"', '""') }
                    $out += ("[" + $s.Name + "] " + $l.Key + "=" + $v)
                }
            }
            return ($out -join "`n")
        }
        $mine = & $strip $text
        $theirs = & $strip ([System.Text.Encoding]::GetEncoding(28591).GetString($PairBytes))
        if ($mine -ne $theirs) { & $add "OEM-PAIR" "the x86 and amd64 files differ outside their two descriptions." }
    }

    if ($Package -ne "") {
        if (-not (Test-Path -LiteralPath (Join-Path $Package "txtsetup.oem"))) {
            & $add "PKG-OEM" "the package has no txtsetup.oem at its root; Setup reads it from A:\."
        }
        foreach ($n in ($named | Sort-Object -Unique)) {
            if (-not (Test-Path -LiteralPath (Join-Path $Package $n.TrimStart('\')))) {
                & $add "PKG-LAYOUT" "txtsetup.oem names '$n' and the package root has none."
            }
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

if ($SelfTest) {
    Write-Step "txtsetup.oem gate self-tests"
    $bad = 0
    $total = 0
    $enc = [System.Text.Encoding]::GetEncoding(28591)
    foreach ($profile in @("x86", "amd64")) {
        $orig = [System.IO.File]::ReadAllBytes($oemFor[$profile])
        $pairProfile = if ($profile -eq "x86") { "amd64" } else { "x86" }
        $pair = [System.IO.File]::ReadAllBytes($oemFor[$pairProfile])
        $inf = [System.IO.File]::ReadAllText($infFor[$profile])
        $text = $enc.GetString($orig)
        $base = @(Test-OemText -Bytes $orig -InfText $inf -Package "" -Profile $profile -PairBytes $pair)
        if ($base.Count -ne 0) {
            Write-Err ("the committed $profile file fails its own gate:`n  " + ($base -join "`n  "))
            exit 1
        }
        $word = if ($profile -eq "amd64") { "x64" } else { "32-bit" }
        $cases = @(
            @{ Rule = "FILE-EOL";     Text = $text.Replace("`r`n[Defaults]", "`n[Defaults]") },
            @{ Rule = "FILE-ASCII";   Text = $text.Replace('= "xHCI98 USB 3.x Host', ('= "' + [char]0xE9 + 'xHCI98 USB 3.x Host')) },
            @{ Rule = "FILE-CTRL";    Text = $text.Replace("scsi = XHCI98", ("scsi = XHCI98" + [char]7)) },
            @{ Rule = "FILE-BOM";     Text = ([string][char]0xEF + [char]0xBB + [char]0xBF + $text) },
            @{ Rule = "OEM-SECTION";  Text = $text.Replace("[Defaults]", "[Keyboard]`r`nXHCI98 = `"x`"`r`n`r`n[Defaults]") },
            @{ Rule = "OEM-SECTION";  Text = $text.Replace("[HardwareIds.scsi.XHCI98]", "[HardwareIds.scsi.OTHER]") },
            @{ Rule = "OEM-SECTION";  Text = $text.Replace("[Defaults]", "[Config.XHCI98]`r`nvalue = `"`", Tag, REG_DWORD, 5`r`n`r`n[Defaults]") },
            @{ Rule = "OEM-SECTION";  Text = ($text + "`r`n[scsi]`r`nXHCI98 = `"again`"`r`n") },
            @{ Rule = "OEM-DEFAULT";  Text = $text.Replace("scsi = XHCI98", "scsi = OTHER") },
            @{ Rule = "OEM-DEFAULT";  Text = $text.Replace("scsi = XHCI98", "keyboard = XHCI98") },
            @{ Rule = "OEM-DISK";     Text = $text.Replace(", \xhci98.sys, \", ", \disk1, \") },
            @{ Rule = "OEM-DISK";     Text = $text.Replace(", \xhci98.sys, \", ", \xhci98.sys, \i386") },
            @{ Rule = "OEM-FILES";    Text = $text.Replace("driver = d1, xhci98.sys, xhci98", "driver = d1, xhciuas.sys, xhci98") },
            @{ Rule = "OEM-FILES";    Text = $text.Replace("inf    = d1, xhci98.inf", "inf    = d1, xhci98.inf`r`ncatalog = d1, xhci98.cat") },
            @{ Rule = "OEM-FILES";    Text = $text.Replace("inf    = d1, xhci98.inf", "inf    = d1, xhci98.inf`r`ndriver = d1, xhciuas.sys, xhciuas") },
            @{ Rule = "OEM-FILES";    Text = $text.Replace("inf    = d1, xhci98.inf`r`n", "") },
            @{ Rule = "OEM-SERVICE";  Text = $text.Replace("driver = d1, xhci98.sys, xhci98", "driver = d1, xhci98.sys, xhcihcd") },
            @{ Rule = "OEM-IDS";      Text = $text.Replace("id = `"XHCI98\ROOT_HUB`", `"xhci98`"`r`n", "") },
            @{ Rule = "OEM-IDS";      Text = $text.Replace("`"XHCI98\ROOT_HUB`", `"xhci98`"", "`"XHCI98\ROOT_HUB`", `"usbhub`"") },
            @{ Rule = "OEM-IDS";      Text = $text.Replace("`"PCI\CC_0C0330`"", "`"PCI\CC_0C0320`"") },
            @{ Rule = "OEM-USBID";    Text = $text.Replace("`"XHCI98\ROOT_HUB`"", "`"USB\ROOT_HUB`"") },
            @{ Rule = "OEM-MSFILE";   Text = $text.Replace("inf    = d1, xhci98.inf", "inf    = d1, xhci98.inf`r`ndll = d1, usbd.sys") },
            @{ Rule = "OEM-83";       Text = $text.Replace("inf    = d1, xhci98.inf", "inf    = d1, xhci98-amd64.inf") },
            @{ Rule = "OEM-ARCH";     Text = $text.Replace($word, "any") },
            @{ Rule = "OEM-QUOTE";    Text = $text.Replace("`"XHCI98\ROOT_HUB`", `"xhci98`"", "`"XHCI98\ROOT_HUB`", `"xhci98") },
            @{ Rule = "OEM-QUOTE";    Text = $text.Replace("`"PCI\CC_0C0330`", `"xhci98`"", "`"PCI\CC_0C0330`" x, `"xhci98`"") },
            @{ Rule = "OEM-QUOTE";    Text = $text.Replace("scsi = XHCI98`r`n", "scsi = XHCI98`"`r`n") },
            @{ Rule = "OEM-PAIR";     Text = $text.Replace("id = `"PCI\CC_0C0330`", `"xhci98`"", "id = `"PCI\CC_0C0330`", `"xhci98`"`r`nid = `"PCI\CC_0C0330`", `"xhci98`"") }
        )
        foreach ($c in $cases) {
            # Twice: with the pair, as build-driver.cmd runs the gate, and
            # without it, as the packagers run it on a staged -OemPath. Every
            # rule but OEM-PAIR must refuse on both.
            foreach ($withPair in @($true, $false)) {
                if (-not $withPair -and $c.Rule -eq "OEM-PAIR") { continue }
                $total++
                $pb = if ($withPair) { $pair } else { $null }
                $got = @(Test-OemText -Bytes $enc.GetBytes($c.Text) -InfText $inf -Package "" -Profile $profile -PairBytes $pb)
                $hit = @($got | Where-Object { $_.StartsWith($c.Rule + " ") })
                $how = if ($withPair) { "paired" } else { "unpaired" }
                if ($hit.Count -eq 0) {
                    $bad++
                    Write-Err ("$profile $how mutation for $($c.Rule) was not refused by that rule. Got:`n  " + ($got -join "`n  "))
                } else {
                    Write-Ok "$profile $how $($c.Rule) refuses its mutation"
                }
            }
        }
        # The INF side of OEM-IDS and OEM-SERVICE: an INF that grows a model
        # or renames its service must fail the unchanged oem file.
        $infCases = @(
            @{ Rule = "OEM-IDS";     Inf = $inf.Replace("%RootHubDesc%=RootHub.Dev,XHCI98\ROOT_HUB", "%RootHubDesc%=RootHub.Dev,XHCI98\ROOT_HUB`r`n%RootHubDesc%=RootHub.Dev,XHCI98\OTHER_HUB") },
            @{ Rule = "OEM-SERVICE"; Inf = $inf.Replace("AddService=xhci98,", "AddService=xhcihcd,") },
            @{ Rule = "OEM-SERVICE"; Inf = ([regex]'(\[Hub\.Dev\.NT[a-z0-9]+\.Services\]\r?\nAddService=)xhci98,').Replace($inf, '${1}xhcihub,', 1) }
        )
        foreach ($c in $infCases) {
            $total++
            $got = @(Test-OemText -Bytes $orig -InfText $c.Inf -Package "" -Profile $profile -PairBytes $pair)
            if (@($got | Where-Object { $_.StartsWith($c.Rule + " ") }).Count -eq 0) {
                $bad++
                Write-Err ("$profile INF mutation for $($c.Rule) was not refused. Got:`n  " + ($got -join "`n  "))
            } else {
                Write-Ok "$profile $($c.Rule) refuses an INF that moved"
            }
        }
        # Stand-in packages: the staged set must pass; a missing oem file, a
        # missing driver, and a Microsoft file anywhere (hidden too) must not.
        $pkgCases = @(
            @{ Name = "clean";       Rule = "";           Drop = "";             Ms = "";      Hide = $false },
            @{ Name = "no-oem";      Rule = "PKG-OEM";    Drop = "txtsetup.oem"; Ms = "";      Hide = $false },
            @{ Name = "no-driver";   Rule = "PKG-LAYOUT"; Drop = "xhci98.sys";   Ms = "";      Hide = $false },
            @{ Name = "no-inf";      Rule = "PKG-LAYOUT"; Drop = "xhci98.inf";   Ms = "";      Hide = $false },
            @{ Name = "msfile";      Rule = "PKG-MSFILE"; Drop = "";             Ms = "";      Hide = $false; MsName = "usbd.sys" },
            @{ Name = "msfile-sub";  Rule = "PKG-MSFILE"; Drop = "";             Ms = "i386";  Hide = $true;  MsName = "hidclass.sys" }
        )
        foreach ($pc in $pkgCases) {
            $total++
            $pkg = Join-Path ([System.IO.Path]::GetTempPath()) ("xhci98-oemtest-" + [Guid]::NewGuid().ToString("N"))
            try {
                New-Item -ItemType Directory -Path $pkg | Out-Null
                foreach ($f in @("txtsetup.oem", "xhci98.inf", "xhci98.sys", "xhciuas.inf", "xhciuas.sys")) {
                    if ($f -ne $pc.Drop) { [System.IO.File]::WriteAllText((Join-Path $pkg $f), "stand-in") }
                }
                if ($pc.ContainsKey("MsName")) {
                    $at = $pkg
                    if ($pc.Ms -ne "") { $at = Join-Path $pkg $pc.Ms; New-Item -ItemType Directory -Path $at | Out-Null }
                    $msPath = Join-Path $at $pc.MsName
                    [System.IO.File]::WriteAllText($msPath, "stand-in")
                    if ($pc.Hide) { [System.IO.File]::SetAttributes($msPath, [System.IO.FileAttributes]::Hidden) }
                }
                $got = @(Test-OemText -Bytes $orig -InfText $inf -Package $pkg -Profile $profile)
            } finally {
                Remove-Item -LiteralPath $pkg -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($pc.Rule -eq "") {
                if ($got.Count -ne 0) { $bad++; Write-Err ("$profile package '$($pc.Name)' was refused:`n  " + ($got -join "`n  ")) }
                else { Write-Ok "$profile package '$($pc.Name)' passes" }
            } elseif (@($got | Where-Object { $_.StartsWith($pc.Rule + " ") }).Count -eq 0) {
                $bad++
                Write-Err ("$profile package '$($pc.Name)' was not refused by $($pc.Rule). Got:`n  " + ($got -join "`n  "))
            } else {
                Write-Ok "$profile package '$($pc.Name)' refused by $($pc.Rule)"
            }
        }
    }
    if ($bad -ne 0) { exit 1 }
    Write-Ok "txtsetup.oem gate self-tests PASSED ($total cases)"
    exit 0
}

Write-Step "txtsetup.oem gate ($Arch): $OemPath"
if (-not (Test-Path -LiteralPath $OemPath)) { Write-Err "no txtsetup.oem at '$OemPath'."; exit 1 }
if (-not (Test-Path -LiteralPath $InfPath)) { Write-Err "no INF at '$InfPath'."; exit 1 }
$pairBytes = $null
if ($defaultOem) {
    $pairArch = if ($Arch -eq "x86") { "amd64" } else { "x86" }
    $pairBytes = [System.IO.File]::ReadAllBytes($oemFor[$pairArch])
}
$fails = @(Test-OemText -Bytes ([System.IO.File]::ReadAllBytes($OemPath)) `
                        -InfText ([System.IO.File]::ReadAllText($InfPath)) `
                        -Package $PackageDir -Profile $Arch -PairBytes $pairBytes)
if ($fails.Count -ne 0) {
    foreach ($f in $fails) { Write-Err $f }
    Write-Err "txtsetup.oem gate FAILED ($($fails.Count))"
    exit 1
}
Write-Ok "txtsetup.oem gate PASSED"
exit 0
