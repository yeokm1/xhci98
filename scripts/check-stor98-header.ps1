# check-stor98-header.ps1 - fail if the two copies of stor98cm.h differ.
#
# Since roadmap-usb4 task 42-A.20 Stor98 is in this tree as src\stor98, and
# its interface header is here twice: src\stor98\include\stor98cm.h, the
# master (Stor98's own, and the file the contract text names), and
# src\cm\stor98cm.h, the copy xhcicm.sys compiles against
# (legal-provenance.md section 2a has its row). Until the two are made one,
# a change to either that is not made to the other is a contract the two
# drivers no longer share, and nothing else would say so: the packager's
# interface check compares src\cm\stor98cm.h with a release folder's copy,
# and only when a cut bundles one. This compares them byte for byte on every
# gated build. Both are checked out under the same attributes (neither
# .gitattributes names *.h), so a checkout's line-ending conversion treats
# them alike.
#
#   check-stor98-header.ps1             compare the two files in this tree
#   check-stor98-header.ps1 -SelfTest   the comparison's own refusals, in memory
#
# Exit 0 when they agree (or the self-test passes), 1 otherwise.

param(
    [switch]$SelfTest,
    [string]$Master = "",
    [string]$Copy = ""
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($Master -eq "") { $Master = Join-Path $repo "src\stor98\include\stor98cm.h" }
if ($Copy -eq "") { $Copy = Join-Path $repo "src\cm\stor98cm.h" }

function Get-HeaderFault {
    # "" when the two byte arrays are equal, else what differs.
    param([byte[]]$A, [byte[]]$B)
    $n = [Math]::Min($A.Length, $B.Length)
    for ($i = 0; $i -lt $n; $i++) {
        if ($A[$i] -ne $B[$i]) { return ("first difference at offset {0}" -f $i) }
    }
    if ($A.Length -ne $B.Length) {
        return ("one is a prefix of the other ({0} and {1} bytes)" -f $A.Length, $B.Length)
    }
    return ""
}

$same = [byte[]](0x2F, 0x2F, 0x0D, 0x0A)
$other = [byte[]](0x2F, 0x2A, 0x0D, 0x0A)
$longer = [byte[]](0x2F, 0x2F, 0x0D, 0x0A, 0x0A)
$checks = @(
    @{ What = "equal files pass"; Ok = ((Get-HeaderFault $same $same) -eq "") },
    @{ What = "a changed byte is refused"; Ok = ((Get-HeaderFault $same $other) -ne "") },
    @{ What = "an appended byte is refused"; Ok = ((Get-HeaderFault $same $longer) -ne "") },
    @{ What = "a truncated copy is refused"; Ok = ((Get-HeaderFault $longer $same) -ne "") },
    @{ What = "an empty copy is refused"; Ok = ((Get-HeaderFault $same ([byte[]]@())) -ne "") }
)
foreach ($c in $checks) {
    if (-not $c.Ok) {
        Write-Host "ERROR: stor98cm.h check SELF-TEST failed: $($c.What)"
        exit 1
    }
}
if ($SelfTest) {
    Write-Host ("stor98cm.h check self-test: {0} cases passed." -f $checks.Count)
    exit 0
}

foreach ($p in @($Master, $Copy)) {
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) {
        Write-Host "ERROR: '$p' does not exist; the two copies of stor98cm.h cannot be compared."
        exit 1
    }
}
$fault = Get-HeaderFault ([System.IO.File]::ReadAllBytes($Master)) ([System.IO.File]::ReadAllBytes($Copy))
if ($fault -ne "") {
    Write-Host "ERROR: the two copies of stor98cm.h differ ($fault):"
    Write-Host "  master  $Master"
    Write-Host "  copy    $Copy"
    Write-Host "Stor98's include\stor98cm.h is the master (docs\contributing\source-files.md):"
    Write-Host "make the change there and copy the file to src\cm unchanged, recording its"
    Write-Host "new SHA-256 in docs\contributing\legal-provenance.md section 2a."
    exit 1
}
Write-Host "stor98cm.h: src\cm\stor98cm.h is src\stor98\include\stor98cm.h byte for byte."
exit 0
