#Requires -Version 2.0
<#
test-gate.ps1 - the import gate's own cases (check-imports.ps1), run by
selftest.cmd: the built image against its allowlist passes, and each of
these fails - an allowlist missing a row the image imports, one with a row
the image does not import, one naming an ordinal by another name than its
import library gives it, one naming a row twice, an image that is not NE
(a PE image), and the image cut short inside its last relocation table. The allowlists are temporary copies; nothing tracked is
changed.

Exit code 0 = every case behaved.
#>
param([Parameter(Mandatory = $true)][string]$Image, [string]$PeImage = "")
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$gate = Join-Path $here 'check-imports.ps1'
$allow = Join-Path $here 'xhcidi-imports.allow'
$rows = @(Get-Content -LiteralPath $allow)
$bad = 0

function Run([string]$what, [string[]]$lines, [string]$img, [int]$want) {
    $tmp = [System.IO.Path]::GetTempFileName()
    try {
        if ($null -ne $lines) { Set-Content -LiteralPath $tmp -Value $lines -Encoding ASCII; $a = $tmp } else { $a = $allow }
        $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate -Path $img -Allow $a 2>&1
        $rc = $LASTEXITCODE
    } finally {
        Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    }
    if ($rc -ne $want) {
        Write-Output ("FAIL: {0}: the gate exited {1}, not {2}: {3}" -f $what, $rc, $want, ($out -join ' '))
        $script:bad++
    } else {
        Write-Output ("ok: {0}" -f $what)
    }
}

$data = @($rows | Where-Object { $_.Trim() -ne '' -and -not $_.Trim().StartsWith('#') })
$first = $data[0]
$fields = @($first.Trim() -split '\s+')

Run 'the image and its allowlist' $null $Image 0
Run 'a row the image imports left out' @($rows | Where-Object { $_ -ne $first }) $Image 1
Run 'a row the image does not import' (@($rows) + @('SETUPX 524 DiSelectBestCompatDrv')) $Image 1
Run 'an ordinal under another name' (@($rows | Where-Object { $_ -ne $first }) + @(("{0} {1} DIINSTALLDEVICE" -f $fields[0], $fields[1]))) $Image 1
Run 'a row twice' (@($rows) + @($first)) $Image 1
if ($PeImage -ne '' -and (Test-Path -LiteralPath $PeImage)) {
    Run 'a PE image' $null $PeImage 1
}
# The image cut short by six bytes: its last relocation record loses its
# tail, whichever kind it is.
$cut = [System.IO.Path]::GetTempFileName()
try {
    $bytes = [System.IO.File]::ReadAllBytes($Image)
    [System.IO.File]::WriteAllBytes($cut, $bytes[0..($bytes.Length - 7)])
    Run 'an image cut short inside its relocation table' $null $cut 1
} finally {
    Remove-Item -LiteralPath $cut -ErrorAction SilentlyContinue
}
if ($bad -ne 0) { exit 1 }
exit 0
