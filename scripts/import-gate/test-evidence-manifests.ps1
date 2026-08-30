<#
.SYNOPSIS
Regression tests for the import gate's authenticated evidence manifests.

.DESCRIPTION
Covers both manifests - the Windows 2000 kernel/HAL baselines and the Win98
evidence sources - with synthetic files under the host temporary directory, so
it needs neither Microsoft binaries nor a built driver. It proves that a
complete set passes, that a missing file fails for the Win2000 half and is
skipped for the optional Win98 half, and that same-length tampering fails by
SHA-256 in both.
#>

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "evidence-common.ps1")

$script:failures = @()
$script:checks = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks++
    if (-not $Condition) {
        $script:failures += $Message
        Write-Host "FAIL: $Message" -ForegroundColor Red
    }
}

$tempBase = [System.IO.Path]::GetFullPath($env:TEMP)
$work = Join-Path $tempBase ("xhci98-evidence-manifest-test-" + [System.IO.Path]::GetRandomFileName())

try {
    New-Item -ItemType Directory -Path $work | Out-Null

    # ---------------------------------------------------------------------
    # The production Windows 2000 manifest must name every image on the media.
    # ---------------------------------------------------------------------
    $win2kRows = @(Read-Win2kBaselineManifest -Path (Join-Path $PSScriptRoot "win2k-baselines.expected"))
    $expectedOutputs = @(
        "hal.dll",
        "halaacpi.dll",
        "halacpi.dll",
        "halapic.dll",
        "halborg.dll",
        "halmacpi.dll",
        "halmps.dll",
        "halsp.dll",
        "ntkrnlmp.exe",
        "ntoskrnl.exe"
    )
    $actualOutputs = @($win2kRows | ForEach-Object { $_.Out } | Sort-Object)
    Assert-True ($actualOutputs.Count -eq $expectedOutputs.Count) "Win2000 manifest must contain two kernels and eight HALs"
    Assert-True (($actualOutputs -join "|") -ceq ($expectedOutputs -join "|")) "Win2000 manifest must name every SP4 kernel/HAL variant, halborg.dll included"

    # ---------------------------------------------------------------------
    # The production Win98 manifest must carry identity for both roles.
    # ---------------------------------------------------------------------
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
    $win98Rows = @(Read-Win98EvidenceManifest -Path (Join-Path $PSScriptRoot "win98-evidence.list") -RepoRoot $repoRoot)
    $precedentRows = @($win98Rows | Where-Object { $_.Role -eq "precedent" })
    $nameTableRows = @($win98Rows | Where-Object { $_.Role -eq "nametable" })
    Assert-True ($precedentRows.Count -ge 2) "Win98 manifest must list precedent binaries"
    Assert-True ($nameTableRows.Count -eq 1) "Win98 manifest must list exactly one name-table file"
    Assert-True ($nameTableRows[0].Label -ieq "ntkern.vxd") "the Win98 name-table file must be ntkern.vxd"
    Assert-True (@($win98Rows | Where-Object { $_.Sha256 -notmatch "^[0-9A-F]{64}$" }).Count -eq 0) "every Win98 row must carry a SHA-256"
    Assert-True (@($win98Rows | Where-Object { $_.Relative -notmatch "^tools\\" }).Count -eq 0) "Win98 evidence must come from tools\"
    # usbport.sys is the module this miniport registers with, so it is the
    # strongest precedent file there is; keep it named.
    Assert-True (@($precedentRows | Where-Object { $_.Label -ieq "USBPORT.SYS" }).Count -eq 1) "NUSB's USBPORT.SYS must be a named precedent binary"

    # ---------------------------------------------------------------------
    # Synthetic identity fixtures, shared validator.
    # ---------------------------------------------------------------------
    $kernelPath = Join-Path $work "kernel.bin"
    $halPath = Join-Path $work "hal.bin"
    [System.IO.File]::WriteAllBytes($kernelPath, [byte[]](1, 2, 3, 4))
    [System.IO.File]::WriteAllBytes($halPath, [byte[]](5, 6, 7, 8))

    $kernelHash = (Get-FileHash -LiteralPath $kernelPath -Algorithm SHA256).Hash
    $halHash = (Get-FileHash -LiteralPath $halPath -Algorithm SHA256).Hash
    $fixtureManifest = Join-Path $work "fixture.expected"
    [System.IO.File]::WriteAllLines(
        $fixtureManifest,
        [string[]]@(
            "KERNEL.BI_ kernel.bin ntoskrnl.exe - 4 $kernelHash",
            "HAL.BI_ hal.bin hal.dll - 4 $halHash"
        ),
        [System.Text.Encoding]::ASCII
    )

    $fixtureRows = @(Read-Win2kBaselineManifest -Path $fixtureManifest)
    $errors = @(Get-Win2kBaselineValidationErrors -Dir $work -Rows $fixtureRows)
    Assert-True ($errors.Count -eq 0) "complete authenticated fixture must pass"

    Remove-Item -LiteralPath $halPath
    $errors = @(Get-Win2kBaselineValidationErrors -Dir $work -Rows $fixtureRows)
    Assert-True ($errors.Count -eq 1 -and $errors[0] -match "^missing hal\.bin$") "missing HAL variant must fail"

    [System.IO.File]::WriteAllBytes($halPath, [byte[]](5, 6, 7, 9))
    $errors = @(Get-Win2kBaselineValidationErrors -Dir $work -Rows $fixtureRows)
    Assert-True ($errors.Count -eq 1 -and $errors[0] -match "^hal\.bin: SHA256 ") "same-length tampering must fail by SHA256"

    # ---------------------------------------------------------------------
    # The Win98 half is optional-but-authenticated: absent is silence, present
    # and wrong is an error. That asymmetry is what the gate depends on.
    # ---------------------------------------------------------------------
    $win98Fixture = @(
        [pscustomobject]@{
            Path = $kernelPath; Label = "kernel.bin"; Version = "-"; Length = 4; Sha256 = $kernelHash
        },
        [pscustomobject]@{
            Path = (Join-Path $work "absent.bin"); Label = "absent.bin"; Version = "-"; Length = 4; Sha256 = $kernelHash
        }
    )
    $errors = @(Get-FileIdentityErrors -Rows $win98Fixture -SkipMissing)
    Assert-True ($errors.Count -eq 0) "an absent optional evidence file must be skipped, not reported"
    $errors = @(Get-FileIdentityErrors -Rows $win98Fixture)
    Assert-True ($errors.Count -eq 1 -and $errors[0] -match "^missing absent\.bin$") "the same absence must be an error when files are mandatory"

    [System.IO.File]::WriteAllBytes($kernelPath, [byte[]](1, 2, 3, 5))
    $errors = @(Get-FileIdentityErrors -Rows $win98Fixture -SkipMissing)
    Assert-True ($errors.Count -eq 1 -and $errors[0] -match "^kernel\.bin: SHA256 ") "a present optional file that is not the recorded build must fail"

    # A wrong length is caught before the hash, so the message names the
    # cheaper discrepancy.
    [System.IO.File]::WriteAllBytes($kernelPath, [byte[]](1, 2, 3))
    $errors = @(Get-FileIdentityErrors -Rows $win98Fixture -SkipMissing)
    Assert-True ($errors.Count -eq 1 -and $errors[0] -match "^kernel\.bin: length 3, expected 4$") "a length mismatch must be reported as such"
} catch {
    $script:failures += $_.Exception.Message
    Write-Host "FAIL: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if (Test-Path -LiteralPath $work) {
        $resolvedWork = [System.IO.Path]::GetFullPath($work)
        if ($resolvedWork.StartsWith($tempBase, [System.StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolvedWork).StartsWith("xhci98-evidence-manifest-test-")) {
            Remove-Item -LiteralPath $resolvedWork -Recurse -Force
        }
    }
}

if ($script:failures.Count -gt 0) {
    Write-Host "Evidence manifest tests FAILED: $($script:failures.Count) problem(s), $($script:checks) checks." -ForegroundColor Red
    exit 1
}

Write-Host "Evidence manifest tests PASSED: $($script:checks) checks." -ForegroundColor Green
exit 0
