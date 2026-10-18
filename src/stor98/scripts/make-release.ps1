# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng

<#
.SYNOPSIS
Cut a Stor98 release in two steps: -Stage (with -TestSignPfx, which signs
the x64 image while staging), then -Finalize.

.DESCRIPTION
-Stage
    Builds every target (free) with scripts\build.cmd, checks that the version
    in src\stor98ver.h, both INFs' DriverVer and the releases\history.md entry
    agree, and stages the release into out\stage\<version>\:

        x86\stor98.inf      (inf\stor98-x86.inf: 98 SE/ME and 2000-7 32-bit)
        x86\stor98.mpd      (out\free\win9x, Windows 98 SE / ME)
        x86\stor98.sys      (out\free\x86, Windows 2000 - 7, 32-bit)
        x64\stor98.inf      (inf\stor98-x64.inf)
        x64\stor98.sys      (out\free\amd64, XP x64 - 7 x64)
        x64\stor98-test.cer (once signed)
        readme.txt  LICENSE  NOTICE.TXT

    This is the layout from 0.3.0.0 (the first Stor98 release) on.

    It refuses to run while a test-only build switch (STOR98_DBGPORT,
    STOR98_TESTDEF, STOR98_G9, STOR98_Q9X) is set, and refuses an image in
    out\free that the build it just ran did not write.

    A release is staged from a commit: -Stage refuses a work tree with
    changes (to a tracked file, or an untracked file under src, include,
    inf or scripts, LICENSE, NOTICE or releases\history.md), and readme.txt
    names the commit.

    The .pdb files go to out\stage\<version>-symbols\{9x,x86,x64}\ (not part
    of the release) and every staged file's SHA-256, the x64 image's as built
    (unsigned), with the commit, to out\stage\<version>.stage.json, which
    -Finalize holds the stage to.

-Stage -Test
    A test stage for install checks, never a release: written to
    out\stage\<version>-test\ with the x64 image unsigned, and readme.txt
    headed TEST BUILD. It does not need a history.md entry, does not refuse
    a version already in releases\ or a tree with changes (its readme says
    so), takes no -TestSignPfx and cannot be finalized. With -NoBuild it
    stages the images already in out\free.

-Stage -TestSignPfx <file.pfx>
    Also signs x64\stor98.sys with the stor98 project test certificate
    (scripts\make-testcert.ps1 makes it once): embedded Authenticode, SHA-1
    file digest, no timestamp, no page hashes, no catalog, INFs unchanged.
    The .pfx is opened before anything is built (password from
    STOR98_TESTSIGN_PW, otherwise a prompt that does not echo) and refused
    unless its subject is the project's. A copy is signed, held to the
    signer, SHA-1 and its vouched digest, and to the build's bytes apart
    from the checksum, the certificate-table entry, padding and the
    appended WIN_CERTIFICATE, before it replaces the staged image; the
    public certificate is staged as x64\stor98-test.cer. Only the amd64
    image is ever signed. Without -TestSignPfx the image is staged
    unsigned, and scripts\sign-x64.ps1 can sign it afterwards with any
    certificate, to the same layout.

-Finalize
    Refuses unless out\stage\<version>\x64\stor98.sys carries an
    Authenticode signature (SHA-1, vouching for this image),
    x64\stor98-test.cer is its signer's certificate, every other staged
    file is byte-identical to what -Stage wrote, and the signed image is the
    staged image plus a signature (only the PE checksum, the security
    directory entry, padding and the appended certificate table may
    differ). It also refuses unless HEAD is the commit the stage was built
    from, the work tree has no changes, and the readme, INFs, LICENSE and
    NOTICE.TXT made again from the tree equal the staged ones. Then copies
    the stage to releases\<version>\ and writes SHA256SUMS.txt there.

releases\<version>\ is write-once once published; see releases\README.md.
-Force (Stage) re-stages over an existing stage, and re-stages a version
that already has a releases\<version>\; (Finalize) replaces an existing
releases\<version>\. Both are allowed for a version in releases\ only if it
has never been uploaded (releases\README.md, rule 3).

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Stage -TestSignPfx scripts\local\testsign\stor98-test.pfx
powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Finalize
#>

[CmdletBinding(DefaultParameterSetName = "Stage")]
param(
    [Parameter(ParameterSetName = "Stage", Mandatory = $true)]
    [switch]$Stage,
    [Parameter(ParameterSetName = "Finalize", Mandatory = $true)]
    [switch]$Finalize,
    # Defaults to the version in src\stor98ver.h; given, it must match it.
    [string]$Version = "",
    # -Stage -Test only: use the images already in out\free instead of
    # rebuilding.
    [Parameter(ParameterSetName = "Stage")]
    [switch]$NoBuild,
    # Stage only: sign x64\stor98.sys with this .pfx (the project test
    # certificate) while staging.
    [Parameter(ParameterSetName = "Stage")]
    [string]$TestSignPfx = "",
    # Stage only: a test stage (out\stage\<version>-test), never finalized.
    [Parameter(ParameterSetName = "Stage")]
    [switch]$Test,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$StageRoot = Join-Path $Repo "out\stage"
$ReleasesRoot = Join-Path $Repo "releases"
$SignTool = Join-Path $Repo "tools\WinDDK71\bin\x86\SignTool.exe"
. (Join-Path $PSScriptRoot "testsign-common.ps1")

function Fail([string]$Message) { throw "make-release: $Message" }

function Get-SourceVersion {
    $h = Get-Content -LiteralPath (Join-Path $Repo "src\stor98ver.h") -Raw
    $m = [regex]::Match($h, '#define\s+STOR98_VER_STR\s+"(\d+\.\d+\.\d+\.\d+)"')
    if (-not $m.Success) { Fail "no STOR98_VER_STR in src\stor98ver.h" }
    $parts = foreach ($n in "MAJOR", "MINOR", "PATCH", "BUILD") {
        $mm = [regex]::Match($h, "#define\s+STOR98_VER_$n\s+(\d+)")
        if (-not $mm.Success) { Fail "no STOR98_VER_$n in src\stor98ver.h" }
        $mm.Groups[1].Value
    }
    if (($parts -join ".") -ne $m.Groups[1].Value) {
        Fail "src\stor98ver.h: STOR98_VER_STR $($m.Groups[1].Value) disagrees with the numeric fields ($($parts -join '.'))"
    }
    return $m.Groups[1].Value
}

function Get-InfDriverVer([string]$Path) {
    $t = Get-Content -LiteralPath $Path -Raw
    $m = [regex]::Matches($t, '(?im)^\s*DriverVer\s*=\s*(\d{2})/(\d{2})/(\d{4})\s*,\s*(\d+\.\d+\.\d+\.\d+)\s*$')
    if ($m.Count -ne 1) { Fail "$Path must have exactly one DriverVer=MM/DD/YYYY,a.b.c.d line" }
    $g = $m[0].Groups
    return @{ Date = "{0}-{1}-{2}" -f $g[3].Value, $g[1].Value, $g[2].Value; Version = $g[4].Value }
}

function Get-HistoryEntry([string]$Ver) {
    $path = Join-Path $ReleasesRoot "history.md"
    # The entries go into readme.txt, so they are held to the same plain
    # ASCII as the other text files.
    $lines = @(Get-TextLines $path)
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match ('^## ' + [regex]::Escape($Ver) + ' - (\d{4}-\d{2}-\d{2})\s*$')) { $start = $i; $date = $Matches[1]; break }
    }
    if ($start -lt 0) { Fail "releases\history.md has no '## $Ver - YYYY-MM-DD' entry; write it first" }
    $end = $lines.Count
    for ($i = $start + 1; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^## ') { $end = $i; break } }
    return @{ Date = $date; Lines = $lines[$start..($end - 1)]; All = $lines[$start..($lines.Count - 1)] }
}

function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function ConvertTo-CrlfBytes([string[]]$Lines) {
    return [Text.Encoding]::ASCII.GetBytes(($Lines -join "`r`n") + "`r`n")
}

function Write-Crlf([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllBytes($Path, (ConvertTo-CrlfBytes $Lines))
}

function Assert-PlainText([string]$What, [string]$Text) {
    # Printable ASCII, TAB and line ends only. Write-Crlf would turn anything
    # above 126 into '?', and a control character has no place in a shipped
    # text file.
    foreach ($ch in $Text.ToCharArray()) {
        $c = [int]$ch
        if ($c -gt 126 -or ($c -lt 32 -and $c -ne 9 -and $c -ne 10 -and $c -ne 13)) {
            Fail ("{0} is not plain ASCII text (character 0x{1:X2})" -f $What, $c)
        }
    }
}

function Get-TextLines([string]$Path) {
    # Normalised to lines whatever the checkout's line endings are.
    $raw = [IO.File]::ReadAllText($Path)
    Assert-PlainText $Path $raw
    $text = $raw -replace "`r`n", "`n"
    if ($text.Contains("`r")) { Fail "$Path has a CR that does not end a line" }
    return $text.TrimEnd("`n").Split("`n")
}

function Get-StagedFiles([string]$Dir) {
    Get-ChildItem -LiteralPath $Dir -Recurse -File | ForEach-Object {
        $_.FullName.Substring($Dir.Length + 1).Replace("\", "/")
    } | Sort-Object
}

function Invoke-Git {
    if ($null -eq (Get-Command git -ErrorAction SilentlyContinue)) { Fail "git is not on PATH; a release is tied to a commit" }
    $out = & git -C $Repo @args
    if ($LASTEXITCODE -ne 0) { Fail "git $($args -join ' ') failed (exit $LASTEXITCODE)" }
    return @($out)
}

function Get-SourceCommit {
    $c = "$((Invoke-Git rev-parse --verify HEAD) | Select-Object -First 1)".Trim()
    if ($c -notmatch '^[0-9a-f]{40}$') { Fail "git rev-parse HEAD gave '$c', not a commit" }
    return $c
}

function Get-TreeChanges {
    # What could make a stage differ from the commit it names: a change to
    # any tracked file, and an untracked file where the build or the stage
    # reads (src, include, inf, scripts, LICENSE, NOTICE, history.md).
    # Ignored files (tools\, out\, scripts\local\, ...) do not count.
    return @(Invoke-Git status --porcelain --untracked-files=all | Where-Object {
        $_ -ne "" -and ($_ -notmatch '^\?\? ' -or
            $_ -match '^\?\? "?(src|include|inf|scripts)/' -or
            $_ -match '^\?\? "?(LICENSE|NOTICE|releases/history\.md)"?$')
    })
}

# ---------------------------------------------------------------- version
$srcVer = Get-SourceVersion
if ($Version -eq "") { $Version = $srcVer }
if ($Version -ne $srcVer) { Fail "-Version $Version but src\stor98ver.h says $srcVer" }
$infX86 = Join-Path $Repo "inf\stor98-x86.inf"
$infX64 = Join-Path $Repo "inf\stor98-x64.inf"
$dvX86 = Get-InfDriverVer $infX86
$dvX64 = Get-InfDriverVer $infX64
foreach ($dv in @($dvX86, $dvX64)) {
    if ($dv.Version -ne $Version) { Fail "an INF's DriverVer version is $($dv.Version), not $Version" }
}
if ($dvX86.Date -ne $dvX64.Date) { Fail "the INFs' DriverVer dates differ ($($dvX86.Date) vs $($dvX64.Date))" }
if ($Test) {
    if ($TestSignPfx -ne "") { Fail "-Test stages are never signed; drop -TestSignPfx" }
    $history = @{ Date = $dvX64.Date; All = @("TEST BUILD of $Version, staged $((Get-Date).ToString('yyyy-MM-dd HH:mm')).",
        "Not a release; no release history.") }
} else {
    $history = Get-HistoryEntry $Version
    if ($history.Date -ne $dvX64.Date) { Fail "history.md dates $Version $($history.Date), DriverVer says $($dvX64.Date)" }
}

# A -Test stage has its own name, so -Finalize (which takes only the release
# version) can never pick it up.
$stageName = if ($Test) { "$Version-test" } else { $Version }
$stageDir = Join-Path $StageRoot $stageName
$stageJson = Join-Path $StageRoot "$stageName.stage.json"
$symDir = Join-Path $StageRoot "$stageName-symbols"
# The unsigned x64 image as staged, kept outside the stage so -Finalize can
# prove the signed file is this image plus a signature and nothing else.
$unsignedRef = Join-Path $StageRoot "$stageName.x64-unsigned.sys"

# The text files of a stage, as lines, made from the tree: -Stage writes
# them and -Finalize makes them again to prove the tree still gives the
# same files.
function Get-TextOutputs([string]$Commit, [bool]$Clean) {
    $date = $history.Date
    $title = if ($Test) { "Stor98 $Version TEST BUILD ($date) - not a release" } else { "Stor98 $Version ($date)" }
    # A -Test stage is never signed and carries no certificate, so its readme
    # must not promise that test-signing mode loads the x64 driver.
    if ($Test) {
        $testNote = @(
            "THIS IS AN UNSIGNED TEST BUILD. x64\stor98.sys is not signed and there",
            "is no x64\stor98-test.cer. Vista x64 and 7 x64 refuse it even in",
            "test-signing mode (Code 52) until the maintainer signs a stage. Test",
            "those two systems only from a signed stage or release; XP x64, Server",
            "2003 x64 and all 32-bit systems do not check and can use this build.",
            ""
        )
        $x64Install = @(
            "  As above, using the x64 folder. In this test build the image is",
            "  UNSIGNED: XP x64 and Server 2003 x64 load it; Vista x64 and 7 x64",
            "  do not, not even with test-signing on (the controller shows Code",
            "  52). Use a signed stage or release for those two."
        )
        $signingIntro = @(
            "This test build is unsigned, so nothing in this section makes it load",
            "on Vista x64 or 7 x64. It applies to signed stages and releases, where",
            "x64\stor98.sys is signed with a test certificate of this project",
            "whose public half is x64\stor98-test.cer. That is not a Microsoft",
            "signature, and no Windows trusts the certificate by itself."
        )
    } else {
        $testNote = @()
        $x64Install = @(
            "  As above, using the x64 folder. On Vista x64 and 7 x64 turn on",
            "  test-signing mode first; see the next section. With test-signing",
            "  already on, the driver starts as soon as it is installed and no",
            "  restart is asked for (also with: pnputil -i -a stor98.inf, from an",
            "  elevated Command Prompt in the x64 folder)."
        )
        $signingIntro = @(
            "x64\stor98.sys is signed with a test certificate of this project,",
            "whose public half is x64\stor98-test.cer. It is not signed by",
            "Microsoft, and no Windows trusts the certificate by itself."
        )
    }
    $readme = @(
        $title,
        ("=" * $title.Length),
        ""
    ) + $testNote + @(
        "Stor98 is the storage driver for NVMe SSDs that xhci98's connection",
        "manager brings up behind a USB4 or Thunderbolt tunnel. It is meant to",
        "be used with xhci98 (https://github.com/yeokm1/xhci98) only, not",
        "standalone. It runs on:",
        "",
        "  - Windows 98 SE and Windows ME                 x86\",
        "  - Windows 2000 SP4, XP, Vista and 7 (32-bit)   x86\",
        "  - Windows XP x64, Vista x64 and 7 x64          x64\",
        "",
        "x86\ holds one INF for all the 32-bit systems, stor98.mpd for Windows",
        "98 SE and ME, and stor98.sys for Windows 2000 to 7; each system copies",
        "only its own file. x64\ holds the 64-bit stor98.sys and its INF.",
        "",
        "It derives from nvme2k by Dominik Behr and its Windows 9x port nvme9x by",
        "SweetLow, and is distributed under the BSD-3-Clause licence in LICENSE.",
        "NOTICE.TXT says where each part came from. Stor98 is not endorsed by",
        "either upstream author.",
        "",
        "NVM Express and NVMe are trademarks of NVM Express, Inc.; Stor98 is",
        "independent and not endorsed by NVM Express or Microsoft.",
        "",
        "STATUS",
        "",
        "This is an early build for testing. Do not use it with data you cannot",
        "afford to lose, and do not install it on a system that boots from an",
        "NVMe disk through another driver.",
        "",
        "Stor98 is for data disks only: Windows cannot start from a disk on a",
        "Stor98 controller. On Windows 2000 and later it is installed as a",
        "demand-start driver, which Plug and Play loads when it finds the NVMe",
        "controller; if Windows refuses the driver, only that device stops and",
        "Windows still starts.",
        "",
        "WINDOWS ME: EJECT BEFORE UNPLUGGING",
        "",
        "On Windows ME, eject the disk cleanly (with the eject of the USB4 /",
        "Thunderbolt host driver) before you unplug it: the clean eject makes",
        "the drive flush its cache first. A 'Disk Write Error' after a disk is",
        "unplugged without an eject is data that ME's file system (VFAT) still",
        "held in its write-behind cache: Stor98 never received it, so it could",
        "not write it. If disks may be unplugged without an eject, you can",
        "turn write-behind caching off, at a cost in speed: Control Panel,",
        "System, Performance, File System, Troubleshooting, 'Disable",
        "write-behind caching for all drives'. (The Removable Disk tab's",
        "option covers only drives Windows treats as removable, which a Stor98",
        "disk is not.)",
        "",
        "INSTALLING",
        "",
        "Install xhci98 first. Stor98 is installed for the NVMe controller of a",
        "drive that xhci98 has brought up behind a USB4 or Thunderbolt tunnel.",
        "",
        "Windows 98 SE / ME",
        "  Device Manager lists the NVMe controller as a PCI device without a",
        "  driver. Open it, choose Update Driver (or Reinstall Driver), pick",
        "  'Display a list of all the drivers in a specific location', then Have",
        "  Disk, and point it at the x86 folder. stor98.mpd is copied to",
        "  WINDOWS\SYSTEM\IOSUBSYS. Restart when asked. The Found New Hardware",
        "  wizard works the same way: search a specific location, the x86",
        "  folder.",
        "",
        "Windows 2000, XP, Vista, 7 (32-bit)",
        "  In Device Manager open the controller (often shown as 'Mass Storage",
        "  Controller' or 'PCI Device' with a yellow mark), choose Update Driver,",
        "  install from a specific location, Have Disk, and point it at the x86",
        "  folder. Restart when asked.",
        "",
        "Windows XP x64, Vista x64, 7 x64"
    ) + $x64Install + @(

        "",
        "TEST SIGNING (64-bit Vista and 7)",
        ""
    ) + $signingIntro + @(
        "",
        "Windows XP x64 and Server 2003 x64 do not check driver signatures, and",
        "neither does any 32-bit Windows: nothing below is needed there. Vista",
        "x64 and 7 x64 load a test-signed driver only in test-signing mode.",
        "Turn it on once:",
        "",
        "  1. Open an elevated Command Prompt: Start, type cmd, right-click it,",
        "     Run as administrator.",
        "  2. Type:  bcdedit /set testsigning on",
        "  3. Restart. 'Test Mode' then shows on the desktop, and the driver",
        "     loads at every start with nothing to press.",
        "",
        "To check: bcdedit /enum {current} shows 'testsigning Yes'. With",
        "BitLocker on the system drive, suspend BitLocker before changing the",
        "boot setting, or the next start asks for the recovery key.",
        "",
        "Without test-signing mode Windows refuses the driver: the controller",
        "shows Code 52 in Device Manager and its disk is not available.",
        "",
        "For one start only, without test-signing mode: press F8 at start-up",
        "and choose 'Disable Driver Signature Enforcement'.",
        "",
        "TURNING TEST-SIGNING OFF AGAIN",
        "",
        "Stop using the driver first, then turn the mode off:",
        "",
        "  1. Uninstall Stor98 (Device Manager, the controller, Uninstall), or",
        "     in an elevated Command Prompt: sc config stor98 start= disabled",
        "     (the space after 'start=' is required).",
        "  2. bcdedit /set testsigning off, and restart.",
        "",
        "Leaving Stor98 installed is not harmful: Windows starts, and the",
        "controller shows Code 52.",
        "",
        "Setup still warns that it cannot verify the publisher, because the",
        "package has no catalog; choose 'Install this driver software anyway'.",
        "",
        "Importing stor98-test.cer is optional and not needed: test-signing",
        "mode loads the driver without it, and it does not remove that warning.",
        "Imported into 'Trusted Root Certification Authorities' and 'Trusted",
        "Publishers' (certmgr.msc) it makes Windows show the signer as trusted,",
        "and makes the machine trust anything signed with this certificate.",
        "That is your decision; it is not recommended.",
        "",
        "SETTINGS",
        "",
        "Stor98 needs no settings; every value below is optional. The INF writes",
        "none of them, so an absent value means the built-in default on every",
        "system, and a later Stor98 with a better default applies it without you",
        "doing anything. Updating the driver keeps any value you set, with one",
        "exception: 0.3.0.0 and 0.3.0.1 read the write-through value under the",
        "name WriteThrough; 0.4.0.0 and later read only StorWriteThrough and",
        "ignore the old name, so rename a WriteThrough value you set.",
        "",
        "Windows 98 SE / ME: values with the 9x names below in",
        "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98, as a string",
        "(REG_SZ, numbers in decimal) or, for the numeric ones, a DWORD. Read",
        "whenever the controller starts (at boot, or when it is plugged in).",
        "",
        "Windows 2000 to 7: one REG_SZ value DriverParameter in",
        "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\stor98\",
        "Parameters\Device, holding lower-case key=value pairs separated by",
        "semicolons, for example maxtransferkb=128;pollmode=1. Read when the",
        "controller starts (at boot, or after disabling and enabling it; a resume",
        "keeps what was read at the start).",
        "",
        "  9x name / NT key        Unit  Default            Range",
        "  ----------------------  ----  -----------------  ------------------------",
        "  StorMaxTransferKB       KiB   512 on 98 SE / ME  64 to the default, in",
        "  maxtransferkb=                256 on 2000 to 7   multiples of 4; anything",
        "                                                   else is refused and the",
        "                                                   default stands",
        "  StorWriteThrough        -     auto               auto, off, cacheoff, fua",
        "  writethrough=                 (cacheoff on ME,   (string only on 9x); an",
        "                                off elsewhere)     unknown word is auto",
        "  StorPollMode            -     0                  0 = poll only without an",
        "  pollmode=                                        interrupt, 1 = always",
        "  StorPollBusyUs          us    1000               1000-100000, 0 = default;",
        "  pollbusyus=                                      2000 to 7 use at least",
        "                                                   10 ms (their clock tick)",
        "  StorPollIdleMs          ms    1000               100-60000, 0 = no polling",
        "  pollidlems=                                      when idle",
        "  StorPollSpinUs          us    500 on 2000 to 7,  0-500, 0 = none (check",
        "  pollspinus=                   0 on 98 SE / ME    for a completion right",
        "                                                   after a request)",
        "  (NT only) gone=         ms    5000               1-600000: how long held",
        "                                                   requests wait for proof",
        "                                                   after the drive is gone;",
        "                                                   advanced: leave at the",
        "                                                   default",
        "  (NT only) idle=         ms    1000               10 and up: write-idle",
        "                                                   time before a flush",
        "",
        "Polling values outside their range are clamped to the nearer end;",
        "refusals and clamps are recorded only in the driver's debug log.",
        "",
        "Write-through: cacheoff turns the drive's volatile write cache off (fua",
        "if the drive refuses), fua marks every write Force Unit Access, off",
        "leaves the drive as it is.",
        "",
        "Polling: when Windows gives the controller no usable interrupt (no",
        "interrupt pin, as on an MSI-only controller, none assigned, or no",
        "interrupt line set), Stor98 polls the controller for completed requests",
        "instead.",
        "",
        "INTERRUPT REPORT",
        "",
        "At every start (on Windows 2000 to 7 not at a resume from hibernation)",
        "Stor98 records what interrupt it found, in a REG_SZ value named",
        "'StorInterrupt <bus>.<device>.<function>' in",
        "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98, for",
        "example 'pin 1 line 11 msi no msix yes port 11/11 interrupt': the",
        "controller's Interrupt Pin and Line, whether it has MSI and MSI-X",
        "(not used by Stor98), the interrupt Windows gave the driver, and",
        "'interrupt' or 'polling (reason)'. When reporting a problem, include",
        "it: open it with Regedit, or in a Command Prompt run (one line)",
        "  regedit /e report.reg",
        "  ""HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98""",
        "and send report.reg.",
        "",
        "CHECKING THE FILES",
        "",
        "SHA256SUMS.txt lists the SHA-256 of every file in this directory. On a",
        "modern system: certutil -hashfile <file> SHA256, or sha256sum -c",
        "SHA256SUMS.txt.",
        "",
        "These files were built from source commit",
        ("$Commit" + $(if ($Clean) { "." } else { ", with uncommitted changes." })),
        "",
        "RELEASE HISTORY",
        ""
    ) + $history.All
    foreach ($l in $readme) {
        if ($l.Length -gt 78) { Fail "readme.txt line longer than 78 columns: '$l'" }
        Assert-PlainText "readme.txt line '$l'" $l
    }
    $out = [ordered]@{}
    $out["x86/stor98.inf"] = Get-TextLines $infX86
    $out["x64/stor98.inf"] = Get-TextLines $infX64
    $out["LICENSE"] = Get-TextLines (Join-Path $Repo "LICENSE")
    $out["NOTICE.TXT"] = Get-TextLines (Join-Path $Repo "NOTICE")
    $out["readme.txt"] = $readme
    return $out
}

# ------------------------------------------------------------------ stage
if ($Stage) {
    if (-not $Test -and (Test-Path -LiteralPath (Join-Path $ReleasesRoot $Version))) {
        # releases\README.md rule 3: a version that has never been uploaded may
        # be re-cut under its number (-Stage -Force, sign, -Finalize -Force).
        if (-not $Force) {
            Fail "releases\$Version already exists and is write-once (releases\README.md). -Force re-stages it, only if it was never uploaded"
        }
        Write-Warning "releases\$Version exists; re-staging it (-Force) is allowed only because it has never been uploaded (releases\README.md, rule 3)"
    }
    if ((Test-Path -LiteralPath $stageDir) -and -not $Force) {
        Fail "out\stage\$stageName exists; pass -Force to re-stage (this discards any signature in it)"
    }
    # Test-only build switches must not reach a stage. With STOR98_DBGPORT=1
    # left over from a test session, build.cmd builds into out\free-dbgport\
    # and the stage would take whatever older images sit in out\free\.
    foreach ($v in "STOR98_DBGPORT", "STOR98_TESTDEF", "STOR98_G9", "STOR98_Q9X") {
        $val = [Environment]::GetEnvironmentVariable($v)
        if ($null -ne $val -and $val -ne "") {
            Fail "$v=$val is set in this environment; it selects a test-only build. Clear it (`$env:$v = `$null) and stage again"
        }
    }
    # A release is built from a commit: the stage records it, the readme
    # names it and -Finalize refuses if HEAD or the tree has moved since. A
    # -Test stage may come from a tree with changes and says so.
    if ($NoBuild -and -not $Test) {
        Fail "-NoBuild is for -Test stages only: a release stage builds its images from the commit it records"
    }
    $commit = Get-SourceCommit
    $changes = @(Get-TreeChanges)
    if ($changes.Count -gt 0 -and -not $Test) {
        Fail "the work tree has changes; a release is staged from a commit. Commit or stash them first:`n  $($changes -join "`n  ")"
    }
    # Made before the build, so a readme or text file problem costs no build.
    $textOutputs = Get-TextOutputs $commit ($changes.Count -eq 0)

    # The key is opened before anything is built or deleted, so a wrong
    # password or certificate costs a prompt, not a build.
    $signCert = $null
    if ($TestSignPfx -ne "") {
        if (-not (Test-Path -LiteralPath $SignTool)) { Fail "no SignTool at $SignTool (WDK 7.1, docs\BUILD.md)" }
        if (-not (Test-Path -LiteralPath $TestSignPfx -PathType Leaf)) {
            Fail "no test-signing key at '$TestSignPfx' (-TestSignPfx). scripts\make-testcert.ps1 makes it; docs\BUILD.md, 'Test signing'."
        }
        $TestSignPfx = (Resolve-Path -LiteralPath $TestSignPfx).Path
        $signPassword = Read-PfxPassword $TestSignPfx
        try {
            $signCert = Open-PfxCertificate $TestSignPfx $signPassword
            Assert-ProjectTestCertificate $signCert $TestSignPfx
        } catch { Fail $_.Exception.Message }
    }

    # Every image staged after a build must come from that build. Two seconds
    # of slack cover the file system's time resolution.
    $buildStart = $null
    if (-not $NoBuild) {
        $buildStart = (Get-Date).AddSeconds(-2)
        & cmd.exe /c "`"$Repo\scripts\build.cmd`" all free"
        if ($LASTEXITCODE -ne 0) { Fail "scripts\build.cmd all free failed" }
    }

    # Build output folder (scripts\build.cmd) -> release folder. The 9x and
    # the NT x86 image share x86\ and its one $CHICAGO$ INF; symbols are kept
    # apart by target because both images build an stor98.pdb.
    $images = @(
        @{ Out = "win9x"; Dir = "x86"; Sym = "9x";  Image = "stor98.mpd"; Machine = 0x14C },
        @{ Out = "x86";   Dir = "x86"; Sym = "x86"; Image = "stor98.sys"; Machine = 0x14C },
        @{ Out = "amd64"; Dir = "x64"; Sym = "x64"; Image = "stor98.sys"; Machine = 0x8664 }
    )
    foreach ($im in $images) {
        $src = Join-Path $Repo ("out\free\{0}\{1}" -f $im.Out, $im.Image)
        if (-not (Test-Path -LiteralPath $src)) { Fail "missing build output $src; run scripts\build.cmd all free" }
        if ($null -ne $buildStart -and (Get-Item -LiteralPath $src).LastWriteTime -lt $buildStart) {
            Fail "$src is older than this build ($((Get-Item -LiteralPath $src).LastWriteTime.ToString('s'))); the build did not write it"
        }
        $fv = [Diagnostics.FileVersionInfo]::GetVersionInfo($src)
        if ($fv.FileVersion -ne $Version -or $fv.ProductVersion -ne $Version) {
            Fail "$src carries version $($fv.FileVersion)/$($fv.ProductVersion), not $Version; rebuild"
        }
        $b = [IO.File]::ReadAllBytes($src)
        $machine = [BitConverter]::ToUInt16($b, [BitConverter]::ToInt32($b, 0x3C) + 4)
        if ($machine -ne $im.Machine) { Fail ("{0} is machine 0x{1:X}, expected 0x{2:X}" -f $src, $machine, $im.Machine) }
        if ($null -ne (Get-EmbeddedSignature $src)) { Fail "$src is already signed; stage only unsigned build output" }
    }

    foreach ($d in @($stageDir, $symDir, $unsignedRef)) {
        if (Test-Path -LiteralPath $d) {
            $full = (Resolve-Path -LiteralPath $d).Path
            if (-not $full.StartsWith($StageRoot + "\", [StringComparison]::OrdinalIgnoreCase)) { Fail "refusing to delete $full" }
            Remove-Item -LiteralPath $full -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Path $stageDir | Out-Null
    foreach ($d in "x86", "x64") { New-Item -ItemType Directory -Path (Join-Path $stageDir $d) | Out-Null }
    foreach ($f in $textOutputs.Keys) { Write-Crlf (Join-Path $stageDir $f) $textOutputs[$f] }
    foreach ($im in $images) {
        Copy-Item -LiteralPath (Join-Path $Repo ("out\free\{0}\{1}" -f $im.Out, $im.Image)) -Destination (Join-Path (Join-Path $stageDir $im.Dir) $im.Image)
        $pdb = Join-Path $Repo ("out\free\{0}\stor98.pdb" -f $im.Out)
        if (Test-Path -LiteralPath $pdb) {
            New-Item -ItemType Directory -Path (Join-Path $symDir $im.Sym) -Force | Out-Null
            Copy-Item -LiteralPath $pdb -Destination (Join-Path $symDir $im.Sym)
        }
    }
    Copy-Item -LiteralPath (Join-Path $stageDir "x64\stor98.sys") -Destination $unsignedRef

    $manifest = [ordered]@{ version = $Version; staged = (Get-Date).ToString("s"); commit = $commit
        clean = ($changes.Count -eq 0); files = [ordered]@{} }
    foreach ($f in Get-StagedFiles $stageDir) { $manifest.files[$f] = Get-Sha256 (Join-Path $stageDir $f) }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath $stageJson -Encoding ASCII

    # Signed after the manifest, which records the image as built: -Finalize
    # holds the signed image to it, the same whichever route signed it.
    if ($null -ne $signCert) {
        $certArgs = @("/f", $TestSignPfx)
        if ($signPassword -ne "") { $certArgs += @("/p", $signPassword) }
        try {
            Invoke-StagedSign -Target (Join-Path $stageDir "x64\stor98.sys") -UnsignedRef $unsignedRef `
                -SignTool $SignTool -CertArgs $certArgs -Thumbprint $signCert.Thumbprint -CerBytes $signCert.CerBytes
        } catch {
            Fail "$($_.Exception.Message)`nout\stage\$stageName holds the unsigned stage; fix the cause and re-stage with -Force."
        } finally {
            $certArgs = $null
            $signPassword = $null
        }
    }

    Write-Host ""
    $state = if ($null -ne $signCert) { "SIGNED by $($signCert.Subject), $($signCert.Thumbprint)" } else { "UNSIGNED" }
    Write-Host "Staged $stageName in $stageDir (x64\stor98.sys $state):"
    foreach ($f in Get-StagedFiles $stageDir) { Write-Host ("  {0}  {1}" -f (Get-Sha256 (Join-Path $stageDir $f)), $f) }
    Write-Host ""
    if ($Test) {
        Write-Host "TEST stage: for install checks only. It is never signed or finalized."
        exit 0
    }
    if ($null -ne $signCert) {
        Write-Host "Next, finalize:"
    } else {
        Write-Host "Next, the maintainer signs (re-stage with -TestSignPfx <file.pfx>, or sign this stage"
        Write-Host "with scripts\sign-x64.ps1 -Pfx <file.pfx> | -Thumbprint <sha1>) and finalizes:"
    }
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Finalize"
    exit 0
}

# --------------------------------------------------------------- finalize
if (-not (Test-Path -LiteralPath $stageDir) -or -not (Test-Path -LiteralPath $stageJson)) {
    Fail "nothing staged for $Version; run -Stage first"
}
$manifest = Get-Content -LiteralPath $stageJson -Raw | ConvertFrom-Json
# The stage must still be what this commit gives: the same HEAD, a tree
# without changes, and the readme, INFs, LICENSE and NOTICE.TXT made again
# from the tree byte-equal to the staged ones.
$names = @($manifest.PSObject.Properties | ForEach-Object { $_.Name })
if (-not ($names -contains "commit") -or -not ($names -contains "clean")) {
    Fail "the stage manifest names no source commit (staged by an older make-release.ps1); re-stage"
}
if (-not $manifest.clean) { Fail "out\stage\$Version was staged from a tree with changes; re-stage from a commit" }
$commit = Get-SourceCommit
if ($commit -ne $manifest.commit) {
    Fail "HEAD is $commit, but out\stage\$Version was staged from $($manifest.commit); check that commit out or re-stage"
}
$changes = @(Get-TreeChanges)
if ($changes.Count -gt 0) {
    Fail "the work tree has changes since -Stage; commit or stash them, or re-stage:`n  $($changes -join "`n  ")"
}
$textOutputs = Get-TextOutputs $commit $true
foreach ($f in $textOutputs.Keys) {
    $p = Join-Path $stageDir $f
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Fail "out\stage\$Version\$f is missing; re-stage" }
    $want = [BitConverter]::ToString((ConvertTo-CrlfBytes $textOutputs[$f]))
    if ([BitConverter]::ToString([IO.File]::ReadAllBytes($p)) -ne $want) {
        Fail "staged $f differs from the one this tree gives; re-stage"
    }
}
$expected =@($manifest.files.PSObject.Properties | ForEach-Object { $_.Name } | Sort-Object)
$present = @(Get-StagedFiles $stageDir)
$signedPath = Join-Path $stageDir "x64\stor98.sys"
if (-not (Test-Path -LiteralPath $signedPath)) { Fail "out\stage\$Version\x64\stor98.sys is missing; re-stage" }
if ($null -eq (Get-EmbeddedSignature $signedPath)) {
    Fail "out\stage\$Version\x64\stor98.sys is not signed. Re-stage with -TestSignPfx, or run scripts\sign-x64.ps1 first."
}
$x64Cer = "x64/$Stor98TestSignCerName"
if ($expected -contains $x64Cer) { Fail "the stage manifest lists $x64Cer; re-stage" }
# The signing step adds the signer's public certificate to what -Stage wrote.
$expected = @(@($expected) + $x64Cer | Sort-Object)
if (($expected -join "|") -ne ($present -join "|")) {
    Fail "out\stage\$Version holds different files from the ones -Stage wrote:`n  staged: $($expected -join ', ')`n  now:    $($present -join ', ')"
}
# The release layout from 0.3.0.0 on (releases\README.md), and nothing else.
$layout = @("LICENSE", "NOTICE.TXT", "readme.txt", "x64/stor98.inf", "x64/stor98.sys", $x64Cer,
    "x86/stor98.inf", "x86/stor98.mpd", "x86/stor98.sys") | Sort-Object
if (($expected -join "|") -ne ($layout -join "|")) {
    Fail "out\stage\$Version is not the release layout:`n  staged:   $($expected -join ', ')`n  expected: $($layout -join ', ')"
}
$x64 = "x64/stor98.sys"
foreach ($f in $expected) {
    if ($f -eq $x64 -or $f -eq $x64Cer) { continue }
    if ((Get-Sha256 (Join-Path $stageDir $f)) -ne $manifest.files.$f) { Fail "staged $f changed since -Stage; re-stage" }
}
$facts = Get-SignatureFacts $signedPath
if ($facts.Problem -ne "" -or $null -eq $facts.Signer) { Fail "x64\stor98.sys has a certificate table but no readable Authenticode signature: $($facts.Problem)" }
$why = Get-SignatureProblem $signedPath $facts.Signer.Thumbprint $true
if ($why -ne "") { Fail "the signature of x64\stor98.sys is refused: $why" }
$cerPath = Join-Path $stageDir "x64\$Stor98TestSignCerName"
if ([BitConverter]::ToString([IO.File]::ReadAllBytes($cerPath)) -ne [BitConverter]::ToString($facts.Signer.RawData)) {
    Fail "x64\$Stor98TestSignCerName is not the certificate x64\stor98.sys is signed with; re-stage and sign again"
}
if (-not (Test-Path -LiteralPath $unsignedRef) -or (Get-Sha256 $unsignedRef) -ne $manifest.files.$x64) {
    Fail "out\stage\$Version.x64-unsigned.sys is missing or differs from the staged image; re-stage"
}
$why = Test-SignedFromStaged $signedPath $unsignedRef
if ($why -ne "") { Fail "the signed x64\stor98.sys is not the staged image plus a signature: $why" }

$relDir = Join-Path $ReleasesRoot $Version
if (Test-Path -LiteralPath $relDir) {
    if (-not $Force) { Fail "releases\$Version exists and is write-once (releases\README.md); -Force only if it was never uploaded" }
    Write-Warning "replacing releases\$Version (-Force): allowed only because it has not been uploaded"
    Remove-Item -LiteralPath $relDir -Recurse -Force
}
Copy-Item -LiteralPath $stageDir -Destination $relDir -Recurse
$sums = foreach ($f in Get-StagedFiles $relDir) { "{0}  {1}" -f (Get-Sha256 (Join-Path $relDir $f)), $f }
# LF, unlike the other text files, on purpose: GNU sha256sum -c (coreutils
# 8.32, as in Git for Windows) takes a CR as part of the file name and fails
# every line of a CRLF list. The readme's check is "sha256sum -c".
[IO.File]::WriteAllText((Join-Path $relDir "SHA256SUMS.txt"), (($sums -join "`n") + "`n"), [Text.Encoding]::ASCII)

Write-Host ""
$stamp = if ($facts.Timestamped) { "timestamped" } else { "no timestamp" }
Write-Host "Finalized releases\$Version (signer: $($facts.Signer.Subject), thumbprint $($facts.Signer.Thumbprint), SHA-1, $stamp):"
Get-Content -LiteralPath (Join-Path $relDir "SHA256SUMS.txt") | ForEach-Object { Write-Host "  $_" }
Write-Host ""
Write-Host "Commit releases\$Version (its history.md entry is already in commit $commit). Once uploaded, the directory is never edited."
