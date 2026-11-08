<#
.SYNOPSIS
Assert that a linked image of this project's - xhci98.sys, and the helper,
UAS, connection manager and two networking images build-driver.cmd gates
the same way - carries exactly its own build-flavour marker.

.DESCRIPTION
Roadmap task 13-L.1 split the build three ways - release, debug and qemu - and
two of the three were checked DDK builds, so `VS_FF_DEBUG` was set in both and
could not tell them apart; roadmap-usb4 task 45.45 retired debug (design record
08 section 14). The marker still separates what must never be confused: the
release image that ships and the qemu one, which carries the port-0xE9
mirror (`HAL.dll!WRITE_PORT_UCHAR`, the sole import delta between 0.0.0.4's two
published binaries, of which the debug one gave the ThinkPad E460 a Code 2 under
Windows 98 SE; why it failed is not established) and is never published.

So each image carries one `XHCI98_FLAVOUR_*` string, emitted by
`src/hcd_entry.c` (`src/xhci_dispatch.c` in the miniport) from a define `src/sources` derives from `BUILD_ALT_DIR` -
the same variable that decides the output tree, so the defines and the directory
cannot disagree. `DriverEntry` reads it so the linker cannot drop it.

This checks the ARTIFACT rather than the build files that were meant to produce
it, which is the same reason `scripts\build-driver.cmd :checkflavour` reads the
do-not-deploy string out of the image. **Both halves are the check**: exactly
one marker, and it must be this flavour's. An image with two would be one the
preprocessor let through with more than one flavour define; an image with the
wrong one is a qemu build wearing a release name, or an old debug image.

It lives in a file rather than inline in build-driver.cmd because it did not
survive being a `powershell -Command` continuation: the first version compared
`$found[0]` against the wanted name without wrapping the pipeline in `@()`, so a
single match arrived as a plain string whose `[0]` is its first CHARACTER and
the gate refused every correct build; the second tripped over `|` inside a
caret-continued quoted batch line.

.PARAMETER Image
The linked .sys to read.

.PARAMETER Flavour
Which of release or qemu it is meant to be. The debug flavour was retired
with task 45.45 (design record 08 section 14); common.ps1's marker list
still knows XHCI98_FLAVOUR_DEBUG, so an old debug image is found and
refused as the wrong marker rather than read as carrying none.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Image,

    [Parameter(Mandatory = $true)]
    [ValidateSet("release", "qemu")]
    [string]$Flavour
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Image)) {
    Write-Host "ERROR: no image at '$Image'."
    exit 1
}

# The name list and the scan are `common.ps1`'s, so this gate and the packager
# cannot disagree about what a marker is (the audit of 2026-09-07). HOSTTEST is in that list
# on purpose; the reason is given there.
. (Join-Path $PSScriptRoot "common.ps1")

$found = @(Get-XhciFlavourMarkers -Path $Image)
$want = "XHCI98_FLAVOUR_" + $Flavour.ToUpper()

if ($found.Count -eq 1 -and $found[0] -eq $want) {
    Write-Host ("  flavour marker: {0}" -f $want)
    exit 0
}

$shown = "(none)"
if ($found.Count -gt 0) {
    $shown = $found -join ", "
}
Write-Host ("  image carries: {0}" -f $shown)
Write-Host ("  expected:      {0}" -f $want)
exit 1
