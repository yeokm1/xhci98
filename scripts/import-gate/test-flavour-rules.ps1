<#
.SYNOPSIS
Regression tests for the import allowlist's three-flavour FLAVORS grammar.

.DESCRIPTION
Roadmap task 13-L.1 turned two build flavours into three - release, debug and
qemu - and the whole of its enforcement is one column in
scripts\import-gate\xhci98-imports.allow. This covers that column, on synthetic
allowlists under the host temporary directory plus the production file, so it
needs neither a built driver nor a Microsoft binary and can run before anything
is compiled. That timing is the point: the gate's own tests run at the top of
scripts\build-driver.cmd, and a wrongly flavoured row must be caught there
rather than by a binary reaching a bench.

What it proves:

  - the grammar accepts release, debug, qemu and all, and rejects anything else;
  - "both" is REFUSED rather than reinterpreted. It was the word for "every
    flavour" while there were two of them, and a row written about two builds
    silently covering three is exactly the failure the third flavour exists to
    prevent;
  - the production allowlist puts HAL.dll!WRITE_PORT_UCHAR in "qemu required"
    and nowhere else. That single row is task 13-L.1's repair: qemu is never
    published, so a published binary carrying the sole import delta of the
    build that gave the E460 a Code 2 now fails the gate instead of being noted
    in a comment;
  - no other row is qemu-only, so nothing else has quietly become
    unpublishable;
  - the three retired file-sink imports are gone (task 13-L.2);
  - **`-Flavor auto` can actually infer a flavour from a Windows path**, which
    it could not when this file was first written: the release and debug
    branches used `[\/]`, which .NET reads as an escaped forward slash and
    nothing else, so neither ever matched a path containing backslashes and the
    documented no-argument invocation refused every image it found. Nothing
    caught it because nothing tested the inference. This does.
#>

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")

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

$gate = Join-Path $PSScriptRoot "check-imports.ps1"

# Runs the gate in -ParseOnly, which reads the allowlist and stops - no
# dumpbin, no image, no evidence scan. Returns the exit code and the output.
function Invoke-Parse {
    param([string]$AllowPath)
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
        -ParseOnly -AllowPath $AllowPath 2>&1
    return [pscustomobject]@{ Code = $LASTEXITCODE; Text = ($out | Out-String) }
}

$tempBase = [System.IO.Path]::GetFullPath($env:TEMP)
$work = Join-Path $tempBase ("xhci98-flavour-rules-test-" + [System.IO.Path]::GetRandomFileName())

try {
    New-Item -ItemType Directory -Path $work | Out-Null

    # ---------------------------------------------------------------------
    # The grammar, on synthetic files.
    # ---------------------------------------------------------------------
    $accepted = @("release", "debug", "qemu", "all")
    foreach ($word in $accepted) {
        $path = Join-Path $work "ok-$word.allow"
        Set-Content -LiteralPath $path -Encoding ASCII -Value @(
            "[imports]",
            "ntoskrnl.exe!DbgPrint   $word   required  synthetic"
        )
        $r = Invoke-Parse -AllowPath $path
        Assert-True ($r.Code -eq 0) "FLAVORS '$word' must be accepted (exit $($r.Code))"
        Assert-True ($r.Text -match "allow ntoskrnl.exe!DbgPrint $word required") "FLAVORS '$word' must parse back unchanged"
    }

    # "both" is a hard cut, like "standard" -> "release" before it: the word is
    # refused with a message that names its replacement, never silently read as
    # "all". A row that meant two builds must not come to mean three by itself.
    $bothPath = Join-Path $work "both.allow"
    Set-Content -LiteralPath $bothPath -Encoding ASCII -Value @(
        "[imports]",
        "ntoskrnl.exe!DbgPrint   both   required  synthetic"
    )
    $r = Invoke-Parse -AllowPath $bothPath
    Assert-True ($r.Code -eq 1) "FLAVORS 'both' must be refused"
    Assert-True ($r.Text -match "retired") "the refusal of 'both' must say it is retired"
    Assert-True ($r.Text -match "all") "the refusal of 'both' must name 'all' as the replacement"

    foreach ($word in @("checked", "free", "standard", "release,debug", "")) {
        $path = Join-Path $work ("bad-" + ($word -replace "[^a-z]", "x") + ".allow")
        $row = "ntoskrnl.exe!DbgPrint   $word   required  synthetic"
        if ($word -eq "") {
            # Two fields where three are needed, which is the other way a row
            # loses its flavour.
            $row = "ntoskrnl.exe!DbgPrint   required"
        }
        Set-Content -LiteralPath $path -Encoding ASCII -Value @("[imports]", $row)
        $r = Invoke-Parse -AllowPath $path
        Assert-True ($r.Code -eq 1) "FLAVORS '$word' must be refused"
    }

    # ---------------------------------------------------------------------
    # -Flavor auto, against paths shaped like the ones the gate discovers.
    #
    # Driven end to end rather than by re-implementing the match here, because
    # a copy of the pattern in this file could be right while the gate's was
    # wrong - which is exactly the state this test was written to end.
    #
    # The stand-in is a text file, so the run fails inside dumpbin either way.
    # What is asserted is the ONE message the inference produces when it
    # cannot decide, and its absence is the whole reading.
    # ---------------------------------------------------------------------
    Write-Step "-Flavor auto infers from a Windows path"
    foreach ($case in @(
        @{ Dir = "objfre";      Flavour = "release"; Under = "" },
        @{ Dir = "objchk";      Flavour = "debug";   Under = "" },
        @{ Dir = "objchk_qemu"; Flavour = "qemu";    Under = "" },
        # **An ANCESTOR directory whose name is another flavour's tree.** These
        # are substring matches against an absolute path, so a clone under
        # D:\objchk\work\ made its own src\objfre\...\xhci98.sys match the debug
        # branch first and be gated as the wrong flavour. Nobody would choose
        # that directory name deliberately; a checkout under a scratch tree can
        # end up with one, and the cost is a binary tested against the wrong
        # flavour's rules. The three cases above pass either way, which is why
        # this one is here.
        @{ Dir = "objfre";      Flavour = "release"; Under = "objchk" },
        @{ Dir = "objchk";      Flavour = "debug";   Under = "objchk_qemu" },
        @{ Dir = "objchk_qemu"; Flavour = "qemu";    Under = "objfre" }
    )) {
        $root = $work
        if ($case.Under -ne "") {
            $root = Join-Path $work ($case.Under + "\clone")
        }
        $imgDir = Join-Path $root ("src\" + $case.Dir + "\i386")
        New-Item -ItemType Directory -Path $imgDir -Force | Out-Null
        $img = Join-Path $imgDir "xhci98.sys"
        Set-Content -LiteralPath $img -Encoding ASCII -Value "not a real driver"

        $where = $case.Dir + "\"
        if ($case.Under -ne "") {
            $where = $case.Under + "\...\" + $where
        }
        $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
            -Image $img -NoTargetEvidence 2>&1 | Out-String
        Assert-True ($out -notmatch "cannot infer the build flavor") `
            ("-Flavor auto could not infer '$($case.Flavour)' from a path under $where. Output:`n" + $out)
        Assert-True ($out -match ("{0} build:" -f $case.Flavour)) `
            ("-Flavor auto did not report '$($case.Flavour)' for a path under $where. Output:`n" + $out)
    }

    # ---------------------------------------------------------------------
    # The production allowlist. These are statements about this project's own
    # decisions, not about the parser.
    # ---------------------------------------------------------------------
    $prod = Join-Path $PSScriptRoot "xhci98-imports.allow"
    $r = Invoke-Parse -AllowPath $prod
    Assert-True ($r.Code -eq 0) "the production allowlist must parse"

    $rows = @()
    foreach ($line in ($r.Text -split "`r?`n")) {
        if ($line -match "^allow (\S+)!(\S+) (\S+) (\S+)$") {
            $rows += [pscustomobject]@{
                Module = $Matches[1]; Symbol = $Matches[2]
                Flavors = $Matches[3]; Requirement = $Matches[4]
            }
        }
    }
    Assert-True ($rows.Count -gt 0) "the production allowlist must yield rows"

    $e9 = @($rows | Where-Object { $_.Symbol -ceq "WRITE_PORT_UCHAR" })
    Assert-True ($e9.Count -eq 1) "WRITE_PORT_UCHAR must appear exactly once"
    if ($e9.Count -eq 1) {
        Assert-True ($e9[0].Module -ieq "HAL.dll") "WRITE_PORT_UCHAR must come from HAL.dll - the PE descriptor names the provider"
        Assert-True ($e9[0].Flavors -eq "qemu") "WRITE_PORT_UCHAR must be qemu-only: it is the sole import delta of the build that gave the E460 a Code 2, and qemu is never published"
        Assert-True ($e9[0].Requirement -eq "required") "WRITE_PORT_UCHAR must be REQUIRED in qemu, so a qemu build that lost the mirror is caught too"
    }

    $qemuOnly = @($rows | Where-Object { $_.Flavors -eq "qemu" } | ForEach-Object { $_.Symbol })
    Assert-True (($qemuOnly -join "|") -ceq "WRITE_PORT_UCHAR") "WRITE_PORT_UCHAR must be the ONLY qemu-only row - anything else here is an import no shipping binary may have, and that is a decision, not an accident"

    # Task 13-L.2 retired the file sink. Its three imports must be gone rather
    # than left allowed-but-unused: an allowlist row is a permission, and a
    # permission nothing needs is one a later change can spend without notice.
    foreach ($gone in @("ZwCreateFile", "ZwWriteFile", "ZwClose")) {
        Assert-True (@($rows | Where-Object { $_.Symbol -ceq $gone }).Count -eq 0) "$gone must be gone with the file sink (task 13-L.2)"
    }

    # The flush's IRQL guard outlives the sink that motivated it, and DbgPrint
    # is still every flavour's.
    foreach ($kept in @("KeGetCurrentIrql", "DbgPrint")) {
        $row = @($rows | Where-Object { $_.Symbol -ceq $kept })
        Assert-True ($row.Count -eq 1) "$kept must still be allowed"
        if ($row.Count -eq 1) {
            Assert-True ($row[0].Flavors -eq "all") "$kept must be allowed in every flavour"
        }
    }
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force
    }
}

Write-Host ""
if ($script:failures.Count -gt 0) {
    Write-Host ("import-gate flavour rules: {0} check(s), {1} FAILED." -f $script:checks, $script:failures.Count) -ForegroundColor Red
    exit 1
}
Write-Host ("import-gate flavour rules: {0} check(s), 0 failures." -f $script:checks) -ForegroundColor Green
exit 0
