<#
.SYNOPSIS
Assert that a linked xhci98.sys carries exactly its own build-flavour marker.

.DESCRIPTION
Roadmap task 13-L.1 split the build three ways - release, debug and qemu - and
**two of the three are checked DDK builds**, so `VS_FF_DEBUG` is set in both and
cannot tell them apart. Those two are exactly the pair that must never be
confused: one ships as the diagnostic download and one carries the port-0xE9
mirror (`HAL.dll!WRITE_PORT_UCHAR`, the sole import delta between 0.0.0.4's two
published binaries, of which the debug one gave the ThinkPad E460 a Code 2 under
Windows 98 SE; why it failed is not established) and is never published.

So each image carries one `XHCI98_FLAVOUR_*` string, emitted by
`src/xhci_dispatch.c` from a define `src/sources` derives from `BUILD_ALT_DIR` -
the same variable that decides the output tree, so the defines and the directory
cannot disagree. `DriverEntry` reads it so the linker cannot drop it.

This checks the ARTIFACT rather than the build files that were meant to produce
it, which is the same reason `scripts\build-driver.cmd :checkmarker` reads the
do-not-deploy string out of the image. **Both halves are the check**: exactly
one marker, and it must be this flavour's. An image with two would be one the
preprocessor let through with more than one flavour define; an image with the
wrong one is a qemu build wearing a debug name.

It lives in a file rather than inline in build-driver.cmd because it did not
survive being a `powershell -Command` continuation: the first version compared
`$found[0]` against the wanted name without wrapping the pipeline in `@()`, so a
single match arrived as a plain string whose `[0]` is its first CHARACTER and
the gate refused every correct build; the second tripped over `|` inside a
caret-continued quoted batch line.

.PARAMETER Image
The linked .sys to read.

.PARAMETER Flavour
Which of release, debug or qemu it is meant to be.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Image,

    [Parameter(Mandatory = $true)]
    [ValidateSet("release", "debug", "qemu")]
    [string]$Flavour
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Image)) {
    Write-Host "ERROR: no image at '$Image'."
    exit 1
}

# The name list and the scan are `common.ps1`'s, so this gate and the packager
# cannot disagree about what a marker is (audit J6). HOSTTEST is in that list
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
