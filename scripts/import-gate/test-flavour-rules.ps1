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

. (Join-Path (Split-Path -Parent $PSScriptRoot) "test-harness.ps1")

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

    # Exercise the real image matcher with synthetic dumpbin output. No built
    # image is needed, so split-flavour rows are checked before the first link.
    & {
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($gate, [ref]$null, [ref]$null)
        foreach ($name in @('Get-ImportPairs', 'Read-AllowFile', 'Get-ObjectImportRefs', 'Test-ImportSitesFromSource', 'Test-ImportSites', 'Test-Image')) {
            $function = $ast.Find({ param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($function.Extent.Text))
        }
        function Invoke-Dumpbin { @('    HAL.dll', '        0 WRITE_PORT_UCHAR', '    Summary') }
        function Test-NtkernName { $true }
        function Add-Failure { param($Message) $script:importFailures += $Message }
        foreach ($order in @(@('debug', 'qemu'), @('qemu', 'debug'))) {
            $split = Join-Path $work 'split.allow'
            Set-Content -LiteralPath $split -Encoding ASCII -Value @(
                '[imports]',
                "HAL.dll!WRITE_PORT_UCHAR $($order[0]) required",
                "HAL.dll!WRITE_PORT_UCHAR $($order[1]) required"
            )
            $rules = Read-AllowFile $split
            foreach ($flavour in @('debug', 'qemu', 'release')) {
                $script:importFailures = @()
                Test-Image -Path synthetic -ImageFlavor $flavour -Rules $rules
                if ($flavour -eq 'release') {
                    Assert-True ($script:importFailures.Count -eq 1) 'split debug/qemu rows still refuse release'
                } else {
                    Assert-True ($script:importFailures.Count -eq 0) "split rows admit $flavour in either order: $($script:importFailures)"
                }
            }
        }
        foreach ($case in @(
            @{ Rows = @('HAL.dll!WRITE_PORT_UCHAR all required'); Failures = 0; Name = 'all still admits every flavour' },
            @{ Rows = @('other.dll!WRITE_PORT_UCHAR qemu optional'); Failures = 1; Name = 'same symbol from a different provider is refused' },
            @{ Rows = @('HAL.dll!write_port_uchar qemu optional'); Failures = 1; Name = 'symbol matching stays case-sensitive' },
            @{ Rows = @('HAL.dll!WRITE_PORT_UCHAR debug optional', 'HAL.dll!WRITE_PORT_UCHAR qemu required', 'HAL.dll!Missing qemu required'); Failures = 1; Name = 'a missing required import still fails' },
            @{ Rows = @('HAL.dll!WRITE_PORT_UCHAR qemu optional', '[deny]', 'WRITE_PORT_UCHAR synthetic denial'); Failures = 1; Name = 'deny still wins over an allowed row' }
        )) {
            Set-Content -LiteralPath $split -Encoding ASCII -Value (@('[imports]') + $case.Rows)
            $rules = Read-AllowFile $split
            $script:importFailures = @()
            Test-Image -Path synthetic -ImageFlavor qemu -Rules $rules
            Assert-True ($script:importFailures.Count -eq $case.Failures) $case.Name
        }

        # The HCD's own two rules, on the same matcher. A usbport.sys import is
        # refused even when a row admits it: the rule is unconditional, not a
        # missing allowlist row.
        function Invoke-Dumpbin { @('    USBPORT.SYS', '        0 USBPORT_RegisterUSBPortDriver', '    Summary') }
        Set-Content -LiteralPath $split -Encoding ASCII -Value @('[imports]', 'USBPORT.SYS!USBPORT_RegisterUSBPortDriver all required')
        $rules = Read-AllowFile $split
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'imports nothing from usbport.sys' }).Count -ge 1) "a USBPORT.SYS import is refused even with an allowlist row: $($script:importFailures)"

        # An empty import table passes only with the scaffold marker in the
        # image's bytes. An empty allowlist, so no missing-required-import
        # failure can stand in for the rule under test.
        function Invoke-Dumpbin { @('    Summary') }
        Set-Content -LiteralPath $split -Encoding ASCII -Value @('[imports]')
        $rules = Read-AllowFile $split
        $marked = Join-Path $work 'marked.sys'
        $bare = Join-Path $work 'bare.sys'
        [System.IO.File]::WriteAllBytes($marked, [System.Text.Encoding]::ASCII.GetBytes("MZ`0XHCI98_SCAFFOLD_DO_NOT_STAGE`0"))
        [System.IO.File]::WriteAllBytes($bare, [System.Text.Encoding]::ASCII.GetBytes("MZ`0"))
        $script:importFailures = @()
        Test-Image -Path $marked -ImageFlavor release -Rules $rules
        Assert-True ($script:importFailures.Count -eq 0) "an empty import table with the scaffold marker is accepted: $($script:importFailures)"
        $script:importFailures = @()
        Test-Image -Path $bare -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'does not carry the scaffold marker' }).Count -eq 1) "an empty import table without the scaffold marker is refused: $($script:importFailures)"

        # SITES=: a restricted pair referenced from a named object passes, and
        # from any other object fails, on the objects beside the image. The
        # three thunk spellings dumpbin prints are recognised, and so is a plain
        # symbol an import library's stub resolves (the rogue object's).
        function Write-Ok { param($Message) }
        function Invoke-Dumpbin {
            param($Exe, $Mode, $Path)
            if ($Mode -eq '/imports') {
                return @('    ntoskrnl.exe', '        0 ExFreePool', '        0 IofCallDriver', '        0 ExAllocatePoolWithTag', '    Summary')
            }
            switch ([System.IO.Path]::GetFileName($Path)) {
                'hcd_pool.obj' { return @('010 00000000 UNDEF  notype       External     | __imp__ExFreePool@4', '011 00000000 UNDEF  notype       External     | __imp_ExAllocatePoolWithTag') }
                'hcd_pnp.obj'  { return @('012 00000000 UNDEF  notype       External     | __imp_@IofCallDriver@8') }
                'hcd_rogue.obj' { return @('013 00000000 UNDEF  notype       External     | _ExFreePool@4') }
            }
            return @()
        }
        # The image sits where build.exe puts it, src\obj<fl>\<arch>, so the
        # LTCG source scan below finds the .c files two levels up.
        $siteSrc = Join-Path $work 'src'
        $siteDir = Join-Path $siteSrc 'objfre\i386'
        New-Item -ItemType Directory -Path $siteDir -Force | Out-Null
        $siteImage = Join-Path $siteDir 'xhci98.sys'
        [System.IO.File]::WriteAllBytes($siteImage, [System.Text.Encoding]::ASCII.GetBytes("MZ`0"))
        foreach ($objName in @('hcd_pool.obj', 'hcd_pnp.obj')) {
            [System.IO.File]::WriteAllBytes((Join-Path $siteDir $objName), [byte[]](0))
        }
        Set-Content -LiteralPath $split -Encoding ASCII -Value @(
            '[imports]',
            'ntoskrnl.exe!ExFreePool all required SITES=hcd_pool.obj pool',
            'ntoskrnl.exe!ExAllocatePoolWithTag all required SITES=hcd_pool.obj pool',
            'ntoskrnl.exe!IofCallDriver all required unrestricted'
        )
        $rules = Read-AllowFile $split
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True ($script:importFailures.Count -eq 0) "SITES admits references from the named object only: $($script:importFailures)"
        [System.IO.File]::WriteAllBytes((Join-Path $siteDir 'hcd_rogue.obj'), [byte[]](0))
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'hcd_rogue\.obj.*SITES=hcd_pool\.obj' }).Count -eq 1) "SITES refuses a reference from an object it does not name: $($script:importFailures)"

        # Link-time-code-generation objects (WDK 7.1 amd64) list no symbols: the
        # rule then reads each object's .c source - a restricted name in a file
        # outside SITES fails, a missing source fails, and a mix of readable and
        # unreadable objects fails.
        function Invoke-Dumpbin {
            param($Exe, $Mode, $Path)
            if ($Mode -eq '/imports') {
                return @('    ntoskrnl.exe', '        0 ExFreePool', '        0 IofCallDriver', '        0 ExAllocatePoolWithTag', '    Summary')
            }
            if ([System.IO.Path]::GetFileName($Path) -eq 'hcd_pnp.obj' -and $script:mixedObjects) {
                return @('012 00000000 UNDEF  notype       External     | __imp_@IofCallDriver@8')
            }
            return @('File Type: ANONYMOUS OBJECT')
        }
        Remove-Item -LiteralPath (Join-Path $siteDir 'hcd_rogue.obj')
        $script:mixedObjects = $false
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd_pool.c') -Encoding ASCII -Value 'void f(void *p) { ExFreePool(p); }'
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd_pnp.c') -Encoding ASCII -Value 'void g(void) { IoCallDriver(0, 0); }'
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True ($script:importFailures.Count -eq 0) "SITES over LTCG objects reads the sources and admits the named file: $($script:importFailures)"
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd_pnp.c') -Encoding ASCII -Value 'void g(void *p) { ExFreePool(p); }'
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'named in .*hcd_pnp\.c' }).Count -eq 1) "SITES over LTCG objects refuses a restricted name in another source: $($script:importFailures)"
        Remove-Item -LiteralPath (Join-Path $siteSrc 'hcd_pnp.c')
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'source .* was not found' }).Count -eq 1) "SITES over LTCG objects fails on a missing source: $($script:importFailures)"
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd_pnp.c') -Encoding ASCII -Value 'void g(void) { HCD_FREE(0); }'
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd.h') -Encoding ASCII -Value '#define HCD_FREE(p) ExFreePool(p)'
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'named in the header' }).Count -eq 1) "SITES over LTCG objects refuses a restricted name in a header, where a macro hides it: $($script:importFailures)"
        Remove-Item -LiteralPath (Join-Path $siteSrc 'hcd.h')
        New-Item -ItemType Directory -Path (Join-Path $siteSrc 'compat') -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $siteSrc 'compat\alias.h') -Encoding ASCII -Value '#define HCD_FREE(p) ExFreePool(p)'
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'named in the header .*compat' }).Count -eq 1) "SITES over LTCG objects refuses a restricted name in a nested header: $($script:importFailures)"
        Remove-Item -LiteralPath (Join-Path $siteSrc 'compat\alias.h')
        $script:mixedObjects = $true
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'mixed obj directory' }).Count -eq 1) "SITES over a mixed obj directory fails: $($script:importFailures)"
    }

    $badSites = Join-Path $work 'bad-sites.allow'
    Set-Content -LiteralPath $badSites -Encoding ASCII -Value @('[imports]', 'ntoskrnl.exe!ExFreePool all required SITES=hcd_pool.c')
    $parsed = Invoke-Parse -AllowPath $badSites
    Assert-True ($parsed.Code -ne 0 -and $parsed.Text -match 'SITES= must list object file names') "a SITES= naming no .obj file is refused: $($parsed.Text)"

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
    # Since 2026-10-02 the production file is the successor HCD's (design
    # record 13). The miniport's assertions about its own rows - the qemu-only
    # WRITE_PORT_UCHAR mirror, the retired file sink, DbgPrint in every flavour
    # - left with it and come back as the HCD earns each row (26-A.1, 26-A.8).
    # What holds from the scaffold on:

    # No usbport.sys pair, ever: the HCD replaces it.
    Assert-True (@($rows | Where-Object { $_.Module -ieq "USBPORT.SYS" }).Count -eq 0) "the HCD's allowlist must carry no USBPORT.SYS row - it replaces usbport.sys"

    # The pool pair 25.3 evidenced is the only one the HCD may ever use, and
    # until 26-A.1 adds its rows the whole family is denied.
    $denied = @()
    foreach ($line in ($r.Text -split "`r?`n")) {
        if ($line -match "^deny (\S+)$") { $denied += $Matches[1] }
    }
    foreach ($never in @("ExFreePoolWithTag", "ExAllocatePool", "MmGetPhysicalAddress", "HalGetAdapter", "HalAllocateCommonBuffer")) {
        Assert-True ($denied -ccontains $never) "$never must be denied: design record 13 sections 7 and 11 found no Windows 98 precedent for it"
    }
    foreach ($row in $rows) {
        Assert-True ($denied -cnotcontains $row.Symbol) "$($row.Symbol) is both allowed and denied"
    }} catch {
    # An exception mid-suite is a FAILED TEST, not a crashed script. Without
    # this the run died at the throw with $ErrorActionPreference = "Stop",
    # printed no summary line, and left the reader to tell a broken harness
    # from a broken gate by reading a stack trace - while its sibling
    # test-evidence-manifests.ps1 had recorded exactly this case as a failure
    # since it was written (the 2026-09-07 audit's J6).
    $script:failures += $_.Exception.Message
    Write-Host "FAIL: $($_.Exception.Message)" -ForegroundColor Red
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
