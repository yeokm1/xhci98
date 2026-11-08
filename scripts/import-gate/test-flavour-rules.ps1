<#
.SYNOPSIS
Regression tests for the import allowlist's FLAVORS grammar.

.DESCRIPTION
Roadmap task 13-L.1 turned two build flavours into three - release, debug and
qemu - and roadmap-usb4 task 45.45 retired debug again (design record 08
section 14), leaving release and qemu; the whole of the enforcement is one
column in
scripts\import-gate\xhci98-imports.allow. This covers that column, on synthetic
allowlists under the host temporary directory plus the production file, so it
needs neither a built driver nor a Microsoft binary and can run before anything
is compiled. That timing is the point: the gate's own tests run at the top of
scripts\build-driver.cmd, and a wrongly flavoured row must be caught there
rather than by a binary reaching a bench.

What it proves:

  - the grammar accepts release, qemu and all, and rejects anything else;
  - "both" is REFUSED rather than reinterpreted. It was the word for "every
    flavour" while there were two of them, and a row written about two builds
    silently covering three is exactly the failure the third flavour existed
    to prevent; "debug" is refused the same way since task 45.45;
  - an image under objchk, the retired flavour's tree, is refused by name;
  - each production x86 allowlist that carries HAL.dll!WRITE_PORT_UCHAR
    (xhci98, xhcicm, xhcipfw) carries it in exactly one row, "qemu
    required". That row is task 13-L.1's repair: qemu is never published, so
    a published binary carrying the sole import delta of the build that gave
    the E460 a Code 2 fails the gate instead of being noted in a comment;
  - the qemu-only rows of every production allowlist are exactly the ones
    named here, so nothing else has quietly become unpublishable and none of
    these has quietly become publishable;
  - every common-format allowlist (*.allow but Stor98's) parses, and
    Stor98's parses under its own reader (check-stor98-build.ps1 -ParseOnly);
  - the net98 and net6 profiles' row rules (Test-Net98Rows, Test-Net6Rows)
    and the net6 baseline sets (Get-Net6Sets), on synthetic rows, before any
    networking image is linked;
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
        foreach ($name in @('Get-ImportPairs', 'Read-AllowFile', 'Get-ObjectImportRefs', 'Join-CSplices', 'Get-RepoIncludeClosure', 'Test-ImportSitesFromSource', 'Test-ImportSites', 'Test-Image')) {
            $function = $ast.Find({ param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($function.Extent.Text))
        }
        function Invoke-Dumpbin { @('    HAL.dll', '        0 WRITE_PORT_UCHAR', '    Summary') }
        function Test-NtkernName { $true }
        # No Windows 98 source is passed here, so the Win98 rule only warns;
        # the block below tests that rule.
        function Add-Warning { param($Message) }
        function Add-Failure { param($Message) $script:importFailures += $Message }
        foreach ($order in @(@('release', 'qemu'), @('qemu', 'release'))) {
            $split = Join-Path $work 'split.allow'
            Set-Content -LiteralPath $split -Encoding ASCII -Value @(
                '[imports]',
                "HAL.dll!WRITE_PORT_UCHAR $($order[0]) required",
                "HAL.dll!WRITE_PORT_UCHAR $($order[1]) required"
            )
            $rules = Read-AllowFile $split
            foreach ($flavour in @('qemu', 'release')) {
                $script:importFailures = @()
                Test-Image -Path synthetic -ImageFlavor $flavour -Rules $rules
                Assert-True ($script:importFailures.Count -eq 0) "split rows admit $flavour in either order: $($script:importFailures)"
            }
        }
        $split = Join-Path $work 'split.allow'
        Set-Content -LiteralPath $split -Encoding ASCII -Value @('[imports]', 'HAL.dll!WRITE_PORT_UCHAR qemu required')
        $rules = Read-AllowFile $split
        foreach ($flavour in @('qemu', 'release')) {
            $script:importFailures = @()
            Test-Image -Path synthetic -ImageFlavor $flavour -Rules $rules
            if ($flavour -eq 'release') {
                Assert-True ($script:importFailures.Count -eq 1) 'a qemu row still refuses release'
            } else {
                Assert-True ($script:importFailures.Count -eq 0) "a qemu row admits qemu: $($script:importFailures)"
            }
        }
        foreach ($case in @(
            @{ Rows = @('HAL.dll!WRITE_PORT_UCHAR all required'); Failures = 0; Name = 'all still admits every flavour' },
            @{ Rows = @('other.dll!WRITE_PORT_UCHAR qemu optional'); Failures = 1; Name = 'same symbol from a different provider is refused' },
            @{ Rows = @('HAL.dll!write_port_uchar qemu optional'); Failures = 1; Name = 'symbol matching stays case-sensitive' },
            @{ Rows = @('HAL.dll!WRITE_PORT_UCHAR release optional', 'HAL.dll!WRITE_PORT_UCHAR qemu required', 'HAL.dll!Missing qemu required'); Failures = 1; Name = 'a missing required import still fails' },
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
        # A header above the sources, which a sub-driver includes by path
        # (src\cm's ..\xhci_*.h): found through the compiled sources'
        # #include "..." closure, bounded by the repository root.
        $repo = $work
        Set-Content -LiteralPath (Join-Path $work 'shared.h') -Encoding ASCII -Value '#define HCD_FREE(p) ExFreePool(p)'
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd_pnp.c') -Encoding ASCII -Value @('#include "../shared.h"', 'void g(void) { HCD_FREE(0); }')
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'named in the header .*shared\.h' }).Count -eq 1) "SITES over LTCG objects refuses a restricted name in a header above the sources that a source includes: $($script:importFailures)"
        Remove-Item -LiteralPath (Join-Path $work 'shared.h')
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True ($script:importFailures.Count -eq 0) "an include that resolves to no file is not followed and is no failure: $($script:importFailures)"
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd_pnp.c') -Encoding ASCII -Value 'void g(void) { IoCallDriver(0, 0); }'
        Remove-Variable -Name repo
        Set-Content -LiteralPath (Join-Path $siteSrc 'notes.h') -Encoding ASCII -Value @('/* why ExFreePool and not', ' * ExFreePoolWithTag */', '// ExFreePool too', '#ifdef ExFreePool', '#undef ExFreePool', '#endif')
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'named in the header' }).Count -eq 0) "a restricted name only in a header's comments is no reference: $($script:importFailures)"
        Remove-Item -LiteralPath (Join-Path $siteSrc 'notes.h')
        Set-Content -LiteralPath (Join-Path $siteSrc 'hcd_pnp.c') -Encoding ASCII -Value @('void g(void *p) { ExFree\', 'Pool(p); }')
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'named in .*hcd_pnp\.c' }).Count -eq 1) "SITES over LTCG objects joins a backslash-newline before matching: $($script:importFailures)"
        $script:mixedObjects = $true
        $script:importFailures = @()
        Test-Image -Path $siteImage -ImageFlavor release -Rules $rules
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'mixed obj directory' }).Count -eq 1) "SITES over a mixed obj directory fails: $($script:importFailures)"
    }

    # ---------------------------------------------------------------------
    # The Windows 98 evidence rule on an x86 image, with the real
    # Test-NtkernName. With neither ntkern.vxd nor a precedent binary staged
    # the pair is a warning, not a failure: until 2026-10-29 a [string]
    # parameter turned the absent name table into "", which read as staged,
    # so a clone without the Windows 98 files failed every kernel pair. A
    # name table without the symbol fails, one with it passes, and an empty
    # precedent set with no name table still fails. The deny list matches a
    # name exactly, as the allow rows and the loader do.
    # ---------------------------------------------------------------------
    & {
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($gate, [ref]$null, [ref]$null)
        foreach ($name in @('Get-ImportPairs', 'Read-AllowFile', 'Get-ObjectImportRefs', 'Join-CSplices', 'Get-RepoIncludeClosure', 'Test-ImportSitesFromSource', 'Test-ImportSites', 'Test-Image', 'Test-NtkernName')) {
            $function = $ast.Find({ param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($function.Extent.Text))
        }
        function Write-Ok { param($Message) }
        function Add-Warning { param($Message) $script:importWarnings += $Message }
        function Add-Failure { param($Message) $script:importFailures += $Message }
        function Invoke-Dumpbin { @('    ntoskrnl.exe', '        0 IofCallDriver', '    Summary') }
        $w98 = Join-Path $work 'w98.allow'
        Set-Content -LiteralPath $w98 -Encoding ASCII -Value @('[imports]', 'ntoskrnl.exe!IofCallDriver all required synthetic')
        $rules = Read-AllowFile $w98
        $noPrecedent = $null
        $noNames = $null
        $script:importFailures = @()
        $script:importWarnings = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules -Precedent $noPrecedent -NtkernText $noNames -ImageArch x86
        Assert-True ($script:importFailures.Count -eq 0) "Win98 rule: no ntkern.vxd and no precedent staged is no failure: $($script:importFailures)"
        Assert-True (@($script:importWarnings | Where-Object { $_ -match 'Windows 98 evidence sources are not staged' }).Count -eq 1) "Win98 rule: no ntkern.vxd and no precedent staged is the warning: $($script:importWarnings)"
        $script:importFailures = @()
        $script:importWarnings = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules
        Assert-True ($script:importFailures.Count -eq 0) "Win98 rule: the two sources omitted is no failure either: $($script:importFailures)"
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules -NtkernText "`0OtherName`0" -ImageArch x86
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'has no WINDOWS 98 evidence' }).Count -eq 1) "Win98 rule: a name table without the symbol fails: $($script:importFailures)"
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules -NtkernText "`0IofCallDriver`0" -ImageArch x86
        Assert-True ($script:importFailures.Count -eq 0) "Win98 rule: a name table with the symbol passes: $($script:importFailures)"
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules -NtkernText "`0iofcalldriver`0" -ImageArch x86
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'has no WINDOWS 98 evidence' }).Count -eq 1) "Win98 rule: the name table matches case-sensitively: $($script:importFailures)"
        $emptyPrecedent = [pscustomobject]@{ Map = (New-Object System.Collections.Hashtable([System.StringComparer]::Ordinal)); Scanned = @('x.sys') }
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules -Precedent $emptyPrecedent -NtkernText $noNames -ImageArch x86
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'has no WINDOWS 98 evidence' }).Count -eq 1) "Win98 rule: a precedent set without the pair and no name table fails: $($script:importFailures)"
        Assert-True (-not (Test-NtkernName -Text $noNames -Symbol 'IofCallDriver')) 'Test-NtkernName: no name table is no evidence'

        Set-Content -LiteralPath $w98 -Encoding ASCII -Value @('[imports]', 'ntoskrnl.exe!IofCallDriver all required synthetic', '[deny]', 'IOFCALLDRIVER synthetic denial', 'iofcalldriver another spelling')
        $rules = Read-AllowFile $w98
        Assert-True ($rules.Deny.Count -eq 2) "deny: two spellings are two rows, not a duplicate: $($rules.Deny.Count)"
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $rules -NtkernText "`0IofCallDriver`0" -ImageArch x86
        Assert-True ($script:importFailures.Count -eq 0) "deny: a row in another case does not deny the import: $($script:importFailures)"
    }

    # ---------------------------------------------------------------------
    # The hlprx86 profile (design record 18 section 15.11, task 38.3):
    # xhcihlpr.sys against every kernel and HAL of Windows 2000, XP, Vista
    # and 7 x86, each set required. Synthetic sets, each a kernel and a HAL
    # authenticated by a manifest of its own, and dumpbin's exports and
    # imports modelled per file.
    # ---------------------------------------------------------------------
    & {
        . (Join-Path $PSScriptRoot "evidence-common.ps1")
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($gate, [ref]$null, [ref]$null)
        foreach ($name in @('Get-ImportPairs', 'Get-ExportNames', 'Read-AllowFile', 'Get-ObjectImportRefs', 'Join-CSplices', 'Get-RepoIncludeClosure', 'Test-ImportSitesFromSource', 'Test-ImportSites', 'Test-Image', 'Get-Win2kBaseline', 'Get-HlprBaseline')) {
            $function = $ast.Find({ param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($function.Extent.Text))
        }
        function Write-Ok { param($Message) }
        function Add-Warning { param($Message) }
        function Test-NtkernName { $false }
        function Add-Failure { param($Message) $script:importFailures += $Message }
        # Each set's kernel exports Common; XP and later add XpOnly; Vista
        # and later add VistaOnly. The HALs export HalCommon.
        $script:exportsOf = @{}
        $script:importsNow = @()
        function Invoke-Dumpbin {
            param($Exe, $Mode, $Path)
            if ($Mode -eq '/imports') {
                return @('    ntoskrnl.exe') + @($script:importsNow | ForEach-Object { "        0 $_" }) + @('    Summary')
            }
            $names = $script:exportsOf[[System.IO.Path]::GetFullPath($Path)]
            $rows = @()
            $n = 1
            foreach ($x in $names) { $rows += ("    {0} 0 00001000 {1}" -f $n, $x); $n++ }
            return $rows
        }
        $hRoot = Join-Path $work 'hlpr'
        $sets = @()
        foreach ($set in @(
            @{ Tag = 'w2k'; Label = 'W2K'; Extra = @() },
            @{ Tag = 'xp'; Label = 'XP'; Extra = @('XpOnly') },
            @{ Tag = 'vista'; Label = 'VISTA'; Extra = @('XpOnly', 'VistaOnly') },
            @{ Tag = 'win7'; Label = 'WIN7'; Extra = @('XpOnly', 'VistaOnly') })) {
            $dir = Join-Path $hRoot $set.Tag
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
            $k = Join-Path $dir 'kernel.bin'
            $h = Join-Path $dir 'hal.bin'
            [System.IO.File]::WriteAllBytes($k, [System.Text.Encoding]::ASCII.GetBytes("k " + $set.Tag))
            [System.IO.File]::WriteAllBytes($h, [System.Text.Encoding]::ASCII.GetBytes("h " + $set.Tag))
            $script:exportsOf[[System.IO.Path]::GetFullPath($k)] = @('Common', 'IoConnectInterruptEx') + $set.Extra
            $script:exportsOf[[System.IO.Path]::GetFullPath($h)] = @('HalCommon')
            $man = Join-Path $hRoot ($set.Tag + '.expected')
            Set-Content -LiteralPath $man -Encoding ASCII -Value @(
                ("kernel.bin kernel.bin ntoskrnl.exe - {0} {1}" -f (Get-Item $k).Length, (Get-FileHash $k -Algorithm SHA256).Hash),
                ("hal.bin hal.bin hal.dll - {0} {1}" -f (Get-Item $h).Length, (Get-FileHash $h -Algorithm SHA256).Hash))
            $sets += @{ Label = $set.Label; Dir = $dir; Manifest = $man; Tag = $set.Tag; Extract = '-x' }
        }
        $hAllow = Join-Path $hRoot 'hlpr.allow'
        Set-Content -LiteralPath $hAllow -Encoding ASCII -Value @(
            '[imports]',
            'ntoskrnl.exe!Common all optional synthetic',
            'ntoskrnl.exe!XpOnly all optional synthetic',
            'ntoskrnl.exe!VistaOnly all optional synthetic',
            'ntoskrnl.exe!IoConnectInterruptEx all optional synthetic')
        $hRules = Read-AllowFile $hAllow

        $loaded = Get-HlprBaseline -Sets $sets -Dumpbin 'x'
        Assert-True ($loaded.Missing.Count -eq 0) "hlprx86: four complete sets load: $($loaded.Missing)"
        $script:importsNow = @('Common')
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $hRules -Win2k $loaded.Baseline -ImageArch hlprx86
        Assert-True ($script:importFailures.Count -eq 0) "hlprx86: a pair Windows 2000 exports passes: $($script:importFailures)"
        $script:importsNow = @('XpOnly')
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $hRules -Win2k $loaded.Baseline -ImageArch hlprx86
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'XpOnly is not exported by .*w2k kernel\.bin' }).Count -eq 1) "hlprx86: an XP-only pair fails (2000 lacks it): $($script:importFailures)"
        $script:importsNow = @('VistaOnly')
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $hRules -Win2k $loaded.Baseline -ImageArch hlprx86
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'VistaOnly is not exported by' -and $_ -match 'xp kernel\.bin' }).Count -eq 1) "hlprx86: a Vista-only pair fails: $($script:importFailures)"
        $script:importsNow = @('IoConnectInterruptEx')
        $script:importFailures = @()
        Test-Image -Path synthetic -ImageFlavor release -Rules $hRules -Win2k $loaded.Baseline -ImageArch hlprx86
        Assert-True (@($script:importFailures | Where-Object { $_ -match 'may not import a Vista-only routine' }).Count -eq 1) "hlprx86: IoConnectInterruptEx is refused by name, whatever the allowlist says: $($script:importFailures)"
        $script:importsNow = @('Common')

        # A whole set missing: the run fails for it (the caller turns each
        # missing label into a failure); one image of a set missing throws.
        Remove-Item -LiteralPath (Join-Path $sets[0].Dir 'kernel.bin'), (Join-Path $sets[0].Dir 'hal.bin')
        $loaded = Get-HlprBaseline -Sets $sets -Dumpbin 'x'
        Assert-True (($loaded.Missing -join ',') -eq 'W2K') "hlprx86: a whole Windows 2000 set missing is reported: $($loaded.Missing)"
        Remove-Item -LiteralPath (Join-Path $sets[1].Dir 'kernel.bin'), (Join-Path $sets[1].Dir 'hal.bin')
        $loaded = Get-HlprBaseline -Sets $sets -Dumpbin 'x'
        Assert-True (($loaded.Missing -join ',') -eq 'W2K,XP') "hlprx86: a whole XP set missing is reported: $($loaded.Missing)"
        Remove-Item -LiteralPath (Join-Path $sets[2].Dir 'hal.bin')
        $threw = $false
        try { $null = Get-HlprBaseline -Sets $sets -Dumpbin 'x' } catch { $threw = ($_.Exception.Message -match 'incomplete or unauthenticated') }
        Assert-True $threw "hlprx86: one image of a set missing fails the run"
    }

    # The gate itself, end to end: a missing set FAILS under hlprx86 (never a
    # warning), and -NoTargetEvidence says no target evidence was checked.
    $emptyDir = Join-Path $work 'no-such-set'
    New-Item -ItemType Directory -Path $emptyDir | Out-Null
    $fakeHelper = Join-Path $work 'objfre\i386\xhcihlpr.sys'
    New-Item -ItemType Directory -Path (Split-Path -Parent $fakeHelper) -Force | Out-Null
    [System.IO.File]::WriteAllBytes($fakeHelper, [byte[]](0x4D, 0x5A))
    $hlprAllow = Join-Path $PSScriptRoot "xhcihlpr-imports.allow"
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate -Image $fakeHelper -Flavor release `
        -Arch x86 -GateProfile hlprx86 -AllowPath $hlprAllow -WinXpDir $emptyDir 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -ne 0 -and $out -match 'Windows XP SP3 x86 kernel/HAL baselines are staged - the hlprx86') "hlprx86 end to end: an unstaged XP set fails the gate: $out"
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate -Image $fakeHelper -Flavor release `
        -Arch x86 -GateProfile hlprx86 -AllowPath $hlprAllow -NoTargetEvidence 2>&1 | Out-String
    Assert-True ($out -match 'no target evidence was checked') "hlprx86 end to end: -NoTargetEvidence says no target evidence was checked: $out"

    $badSites = Join-Path $work 'bad-sites.allow'
    Set-Content -LiteralPath $badSites -Encoding ASCII -Value @('[imports]', 'ntoskrnl.exe!ExFreePool all required SITES=hcd_pool.c')
    $parsed = Invoke-Parse -AllowPath $badSites
    Assert-True ($parsed.Code -ne 0 -and $parsed.Text -match 'SITES= must list object file names') "a SITES= naming no .obj file is refused: $($parsed.Text)"

    # ---------------------------------------------------------------------
    # The grammar, on synthetic files.
    # ---------------------------------------------------------------------
    $accepted = @("release", "qemu", "all")
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

    # "debug" the same way since task 45.45: the flavour it named is retired.
    $debugPath = Join-Path $work "debug.allow"
    Set-Content -LiteralPath $debugPath -Encoding ASCII -Value @(
        "[imports]",
        "ntoskrnl.exe!RtlAssert   debug   required  synthetic"
    )
    $r = Invoke-Parse -AllowPath $debugPath
    Assert-True ($r.Code -eq 1) "FLAVORS 'debug' must be refused"
    Assert-True ($r.Text -match "retired" -and $r.Text -match "45\.45") "the refusal of 'debug' must say it is retired, by task 45.45"

    foreach ($word in @("checked", "free", "standard", "release,qemu", "")) {
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
        @{ Dir = "objchk_qemu"; Flavour = "qemu";    Under = "" },
        # **An ANCESTOR directory whose name is another flavour's tree.** These
        # are substring matches against an absolute path, so a clone under
        # D:\objchk\work\ made its own src\objfre\...\xhci98.sys match the debug
        # branch first and be gated as the wrong flavour. Nobody would choose
        # that directory name deliberately; a checkout under a scratch tree can
        # end up with one, and the cost is a binary tested against the wrong
        # flavour's rules. The two cases above pass either way, which is why
        # these are here.
        @{ Dir = "objfre";      Flavour = "release"; Under = "objchk" },
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

    # The retired debug flavour's tree (task 45.45): an image under objchk is
    # refused by name, never inferred as a flavour.
    $chkDir = Join-Path $work "src\objchk\i386"
    New-Item -ItemType Directory -Path $chkDir -Force | Out-Null
    $chkImg = Join-Path $chkDir "xhci98.sys"
    Set-Content -LiteralPath $chkImg -Encoding ASCII -Value "not a real driver"
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
        -Image $chkImg -NoTargetEvidence 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -ne 0 -and $out -match "retired debug flavour") `
        ("an image under objchk was not refused as the retired debug flavour's. Output:`n" + $out)

    # ---------------------------------------------------------------------
    # The networking profiles' own rules (design record 35), before any
    # networking image is linked: Test-Net98Rows, Test-Net6Rows and
    # Get-Net6Sets loaded from the gate and run on synthetic rows, manifests,
    # NDIS.VXD name tables and ndis.h headers. The functions read their
    # manifests beside the gate ($PSScriptRoot), which a function made from
    # text has none of, so that one name is pointed at a synthetic directory.
    # ---------------------------------------------------------------------
    & {
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($gate, [ref]$null, [ref]$null)
        foreach ($name in @('Read-AllowFile', 'Test-Net98Rows', 'Test-Net6Rows', 'Get-Net6Sets')) {
            $function = $ast.Find({ param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($function.Extent.Text.Replace('$PSScriptRoot', '$netGateDir')))
        }
        function Write-Ok { param($Message) }
        function Add-Warning { param($Message) $script:importWarnings += $Message }
        function Add-Failure { param($Message) $script:importFailures += $Message }

        $netGateDir = Join-Path $work "net-gate"
        $repo = Join-Path $work "net-repo"
        $Ndis98Dir = Join-Path $work "net-vxd98"
        $NdisMeDir = Join-Path $work "net-vxdme"
        foreach ($d in @($netGateDir, $Ndis98Dir, $NdisMeDir, (Join-Path $repo "tools\ntddk\inc\ddk"), (Join-Path $work "net-wdk\inc\ddk"))) {
            New-Item -ItemType Directory -Force -Path $d | Out-Null
        }
        Set-Content -LiteralPath (Join-Path $netGateDir "xhci98-imports.allow") -Encoding ASCII -Value @('[imports]', 'ntoskrnl.exe!KeClearEvent all required synthetic')
        Set-Content -LiteralPath (Join-Path $netGateDir "net98-runtime.list") -Encoding ASCII -Value @(
            '[record both]', 'repository r', 'commit c', 'observation o', 'imports i', 'systems 98SE ME', 'NDIS.SYS!NdisGood',
            '[record half]', 'repository r', 'commit c', 'observation o', 'imports i', 'systems 98SE', 'NDIS.SYS!NdisHalf')
        $vxdBytes = [System.Text.Encoding]::ASCII.GetBytes("MZ`0NdisGood`0NdisOwed`0NdisHalf`0NdisNoTag`0NdisUndeclared`0")
        $vxdRows = @()
        foreach ($pair in @(@("98SE", $Ndis98Dir), @("ME", $NdisMeDir))) {
            $vxd = Join-Path $pair[1] "NDIS.VXD"
            [System.IO.File]::WriteAllBytes($vxd, $vxdBytes)
            $vxdRows += ("{0} NDIS.VXD {1} {2}" -f $pair[0], $vxdBytes.Length, (Get-FileHash -LiteralPath $vxd -Algorithm SHA256).Hash)
        }
        Set-Content -LiteralPath (Join-Path $netGateDir "ndisvxd-baselines.expected") -Encoding ASCII -Value $vxdRows
        $hdr = @()
        foreach ($s in @("NdisGood", "NdisOwed", "NdisHalf", "NdisNoTag", "NdisNotExported", "NdisSix")) { $hdr += @("VOID", "$s(", "    IN PVOID P", "    );") }
        $hdr += @("/*++", "VOID", "NdisInComment(", "--*/", "#define NdisMacro(a) (a)")
        Set-Content -LiteralPath (Join-Path $repo "tools\ntddk\inc\ddk\ndis.h") -Encoding ASCII -Value $hdr
        Set-Content -LiteralPath (Join-Path $work "net-wdk\inc\ddk\ndis.h") -Encoding ASCII -Value $hdr
        $netAllow = Join-Path $work "net.allow"

        $good = @('ntoskrnl.exe!KeClearEvent all required synthetic',
                  'NDIS.SYS!NdisGood all required bc-header; w98-runtime both',
                  'NDIS.SYS!NdisOwed all required bc-header; w98-owed')
        Set-Content -LiteralPath $netAllow -Encoding ASCII -Value (@('[imports]') + $good)
        $script:importFailures = @(); $script:importWarnings = @()
        Test-Net98Rows -AllowPath $netAllow
        Assert-True ($script:importFailures.Count -eq 0) "net98: the good rows were refused: $($script:importFailures -join ' | ')"
        Assert-True (@($script:importWarnings | Where-Object { $_ -match '1 NDIS\.SYS pair\(s\) are w98-owed' }).Count -eq 1) "net98: a w98-owed row is not reported owed: $($script:importWarnings -join ' | ')"
        foreach ($c in @(
            @{ Row = 'NDIS.SYS!NdisGood all required w98-runtime both'; Says = 'carries no bc-header tag' },
            @{ Row = 'NDIS.SYS!NdisUndeclared all required bc-header; w98-owed'; Says = "ndis.h declares no routine of that name" },
            @{ Row = 'NDIS.SYS!NdisInComment all required bc-header; w98-owed'; Says = "ndis.h declares no routine of that name" },
            @{ Row = 'NDIS.SYS!NdisNotExported all required bc-header; w98-owed'; Says = "is not among 98SE's NDIS.VXD export names" },
            @{ Row = 'NDIS.SYS!NdisGood all required bc-header; w98-runtime nosuch'; Says = "which net98-runtime.list does not hold" },
            @{ Row = 'NDIS.SYS!NdisHalf all required bc-header; w98-runtime half'; Says = "does not list both 98SE and ME" },
            @{ Row = 'NDIS.SYS!NdisOwed all required bc-header; w98-runtime both'; Says = "is not among record 'both''s imports" },
            @{ Row = 'NDIS.SYS!NdisNoTag all required bc-header'; Says = "carries neither w98-runtime <record> nor w98-owed" },
            @{ Row = 'ntoskrnl.exe!KeSetEvent all required synthetic'; Says = "is not a row of xhci98-imports.allow" },
            @{ Row = 'usbd.sys!USBD_Foo all required synthetic'; Says = "imports from NTOSKRNL.EXE, HAL.DLL and NDIS.SYS alone" }
        )) {
            Set-Content -LiteralPath $netAllow -Encoding ASCII -Value (@('[imports]') + $good + @($c.Row))
            $script:importFailures = @(); $script:importWarnings = @()
            Test-Net98Rows -AllowPath $netAllow
            Assert-True (@($script:importFailures | Where-Object { $_.Contains($c.Says) }).Count -ge 1) "net98: '$($c.Row)' was not refused with '$($c.Says)': $($script:importFailures -join ' | ')"
        }
        # A staged NDIS.VXD that is not the manifest's file is a failure, not
        # a skipped set; an unstaged one a warning.
        [System.IO.File]::WriteAllBytes((Join-Path $NdisMeDir "NDIS.VXD"), [System.Text.Encoding]::ASCII.GetBytes("MZ`0NdisGood`0"))
        Set-Content -LiteralPath $netAllow -Encoding ASCII -Value (@('[imports]') + $good)
        $script:importFailures = @(); $script:importWarnings = @()
        Test-Net98Rows -AllowPath $netAllow
        Assert-True (@($script:importFailures | Where-Object { $_.Contains("is not the ME NDIS.VXD ndisvxd-baselines.expected names") }).Count -eq 1) "net98: a changed NDIS.VXD was not refused: $($script:importFailures -join ' | ')"
        Remove-Item -LiteralPath (Join-Path $NdisMeDir "NDIS.VXD")
        $script:importFailures = @(); $script:importWarnings = @()
        Test-Net98Rows -AllowPath $netAllow
        Assert-True ($script:importFailures.Count -eq 0 -and @($script:importWarnings | Where-Object { $_.Contains("ME's NDIS.VXD is not staged") }).Count -eq 1) "net98: an unstaged NDIS.VXD is not the warning: $($script:importFailures -join ' | ') / $($script:importWarnings -join ' | ')"

        # net6: the three modules, and each NDIS.SYS pair tagged and declared
        # in the kit's ndis.h (WDKROOT, as the build takes it).
        $savedWdk = $env:WDKROOT
        try {
            $env:WDKROOT = Join-Path $work "net-wdk"
            $good6 = @('ntoskrnl.exe!KeTickCount all required synthetic', 'HAL.dll!KeGetCurrentIrql all required synthetic',
                       'NDIS.SYS!NdisSix all required ndis620-header')
            Set-Content -LiteralPath $netAllow -Encoding ASCII -Value (@('[imports]') + $good6)
            $script:importFailures = @()
            Test-Net6Rows -AllowPath $netAllow
            Assert-True ($script:importFailures.Count -eq 0) "net6: the good rows were refused: $($script:importFailures -join ' | ')"
            foreach ($c in @(
                @{ Row = 'NDIS.SYS!NdisSix all required synthetic'; Says = 'carries no ndis620-header tag' },
                @{ Row = 'NDIS.SYS!NdisUndeclared all required ndis620-header'; Says = "WDK 7.1's ndis.h declares no routine of that name" },
                @{ Row = 'usbd.sys!USBD_Foo all required synthetic'; Says = "imports from NTOSKRNL.EXE, HAL.DLL and NDIS.SYS alone" }
            )) {
                Set-Content -LiteralPath $netAllow -Encoding ASCII -Value (@('[imports]') + $good6 + @($c.Row))
                $script:importFailures = @()
                Test-Net6Rows -AllowPath $netAllow
                Assert-True (@($script:importFailures | Where-Object { $_.Contains($c.Says) }).Count -eq 1) "net6: '$($c.Row)' was not refused with '$($c.Says)': $($script:importFailures -join ' | ')"
            }
            $env:WDKROOT = Join-Path $work "net-no-wdk"
            Set-Content -LiteralPath $netAllow -Encoding ASCII -Value (@('[imports]') + $good6)
            $script:importFailures = @()
            Test-Net6Rows -AllowPath $netAllow
            Assert-True (@($script:importFailures | Where-Object { $_.Contains("is not there") }).Count -eq 1) "net6: a kit with no ndis.h is not a failure: $($script:importFailures -join ' | ')"
        } finally {
            $env:WDKROOT = $savedWdk
        }

        # Get-Net6Sets: a committed manifest is a set whose files are
        # required, for that architecture's tag; an absent one is owed.
        Set-Content -LiteralPath (Join-Path $netGateDir "net6-win81-x64-baselines.expected") -Encoding ASCII -Value @("# synthetic")
        $script:importWarnings = @()
        $sets = @(Get-Net6Sets -ForArch amd64)
        Assert-True ($sets.Count -eq 1 -and $sets[0].Tag -eq "win81x64" -and $sets[0].AllowNdis -and $sets[0].Label -eq "Windows 8.1 x64") "net6 sets: one committed x64 manifest is not one set: $(@($sets | ForEach-Object { $_.Tag }) -join ', ')"
        Assert-True (@($script:importWarnings | Where-Object { $_ -match 'no Windows 10 x64 baseline manifest' }).Count -eq 1) "net6 sets: the absent Windows 10 x64 manifest is not reported owed: $($script:importWarnings -join ' | ')"
        $script:importWarnings = @()
        $sets = @(Get-Net6Sets -ForArch x86)
        Assert-True ($sets.Count -eq 0 -and $script:importWarnings.Count -eq 2) "net6 sets: no x86 manifest is not two owed sets: $($sets.Count), $($script:importWarnings -join ' | ')"
    }

    # The production manifests the networking profiles read parse under
    # those functions' own readers too: net98-runtime.list and
    # ndisvxd-baselines.expected through Test-Net98Rows on a row-less
    # allowlist (a parse error throws; an unstaged NDIS.VXD only warns).
    & {
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($gate, [ref]$null, [ref]$null)
        foreach ($name in @('Read-AllowFile', 'Test-Net98Rows')) {
            $function = $ast.Find({ param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($function.Extent.Text.Replace('$PSScriptRoot', '$netGateDir')))
        }
        function Write-Ok { param($Message) }
        function Add-Warning { param($Message) }
        function Add-Failure { param($Message) $script:importFailures += $Message }
        $netGateDir = $PSScriptRoot
        $repo = Join-Path $work "net-prod-repo"
        $Ndis98Dir = Join-Path $work "net-prod-none98"
        $NdisMeDir = Join-Path $work "net-prod-noneme"
        $empty = Join-Path $work "net-empty.allow"
        Set-Content -LiteralPath $empty -Encoding ASCII -Value @('[imports]')
        $script:importFailures = @()
        Test-Net98Rows -AllowPath $empty
        Assert-True ($script:importFailures.Count -eq 0) "the production net98-runtime.list or ndisvxd-baselines.expected does not read cleanly: $($script:importFailures -join ' | ')"
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
    # record 13). The qemu-only rows of it and of every other production
    # allowlist are asserted by name below; what else holds from the
    # scaffold on:

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
    }

    # The helper's production allowlist (record 18 section 15.11): the timer
    # routine required, the Vista interrupt pair denied and in no row.
    $r = Invoke-Parse -AllowPath (Join-Path $PSScriptRoot "xhcihlpr-imports.allow")
    Assert-True ($r.Code -eq 0) "the helper's allowlist must parse"
    Assert-True ($r.Text -match "allow ntoskrnl\.exe!ExSetTimerResolution all required") "the helper's allowlist requires ExSetTimerResolution"
    Assert-True ($r.Text -match "deny IoConnectInterruptEx" -and $r.Text -match "deny IoDisconnectInterruptEx") "the helper's allowlist denies the Vista interrupt pair"
    Assert-True ($r.Text -notmatch "allow ntoskrnl\.exe!Io(Dis)?connectInterruptEx") "the helper's allowlist admits neither Vista interrupt routine"

    # Every common-format allowlist parses before anything is linked, and
    # its qemu-only rows are exactly these: the port-0xE9 mirror's
    # HAL.dll!WRITE_PORT_UCHAR in the three x86 images that carry the
    # mirror (task 13-L.1, AGENTS.md "Build flavours": the import that must
    # never reach a published binary), and the checked build's RtlAssert,
    # with the helper's checked-only registry read. A row moved to "all"
    # makes a qemu-only import publishable; a new qemu-only row makes
    # something unpublishable; either fails here.
    $qemuOnly = @{
        "xhci98-imports.allow"         = @("HAL.dll!WRITE_PORT_UCHAR")
        "xhci98-imports-amd64.allow"   = @("ntoskrnl.exe!RtlAssert")
        "xhcicm-imports.allow"         = @("HAL.dll!WRITE_PORT_UCHAR")
        "xhcicm-imports-amd64.allow"   = @("ntoskrnl.exe!RtlAssert")
        "xhcihlpr-imports.allow"       = @("ntoskrnl.exe!RtlAssert", "ntoskrnl.exe!RtlQueryRegistryValues")
        "xhcinet-imports.allow"        = @()
        "xhcinet-imports-amd64.allow"  = @("ntoskrnl.exe!RtlAssert")
        "xhcinet6-imports.allow"       = @("ntoskrnl.exe!RtlAssert")
        "xhcinet6-imports-amd64.allow" = @("ntoskrnl.exe!RtlAssert")
        "xhcipfw-imports.allow"        = @("HAL.dll!WRITE_PORT_UCHAR")
        "xhciuas-imports.allow"        = @()
        "xhciuas-imports-amd64.allow"  = @("ntoskrnl.exe!RtlAssert")
    }
    $mirrorFiles = @("xhci98-imports.allow", "xhcicm-imports.allow", "xhcipfw-imports.allow")
    $commonFiles = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter "*.allow" -File | Where-Object { $_.Name -ne "stor98-imports.allow" } | Sort-Object Name)
    Assert-True ((@($commonFiles | ForEach-Object { $_.Name }) -join "|") -ceq (@($qemuOnly.Keys | Sort-Object) -join "|")) `
        ("the common-format allowlists are not the twelve this test names: " + (@($commonFiles | ForEach-Object { $_.Name }) -join ", "))
    foreach ($af in $commonFiles) {
        $ar = Invoke-Parse -AllowPath $af.FullName
        Assert-True ($ar.Code -eq 0) "$($af.Name) must parse:`n$($ar.Text)"
        $aRows = @()
        foreach ($line in ($ar.Text -split "`r?`n")) {
            if ($line -match "^allow (\S+)!(\S+) (\S+) (\S+)$") {
                $aRows += [pscustomobject]@{ Module = $Matches[1]; Symbol = $Matches[2]; Flavors = $Matches[3]; Requirement = $Matches[4] }
            }
        }
        Assert-True ($aRows.Count -gt 0) "$($af.Name) parsed to no allow row"
        $gotQ = @($aRows | Where-Object { $_.Flavors -ceq "qemu" } | ForEach-Object { "$($_.Module)!$($_.Symbol)" } | Sort-Object)
        $wantQ = @($qemuOnly[$af.Name] | Sort-Object)
        Assert-True (($gotQ -join "|") -ceq ($wantQ -join "|")) ("{0}'s qemu-only rows are '{1}', not '{2}'" -f $af.Name, ($gotQ -join ", "), ($wantQ -join ", "))
        $wp = @($aRows | Where-Object { $_.Module -ieq "HAL.dll" -and $_.Symbol -ceq "WRITE_PORT_UCHAR" })
        if ($mirrorFiles -contains $af.Name) {
            Assert-True ($wp.Count -eq 1 -and $wp[0].Flavors -ceq "qemu" -and $wp[0].Requirement -ceq "required") `
                ("{0} must carry HAL.dll!WRITE_PORT_UCHAR in exactly one row, 'qemu required': {1}" -f $af.Name, (@($wp | ForEach-Object { "$($_.Flavors) $($_.Requirement)" }) -join "; "))
        } else {
            Assert-True ($wp.Count -eq 0) "$($af.Name) carries a HAL.dll!WRITE_PORT_UCHAR row, and only the three mirror images may"
        }
    }

    # Stor98's allowlist, in its own grammar, by its own reader.
    $sOut = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path (Split-Path -Parent $PSScriptRoot) "check-stor98-build.ps1") -ParseOnly 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -eq 0 -and $sOut -match "Stor98 allowlist: \d+ row") "stor98-imports.allow must parse under check-stor98-build.ps1's reader:`n$sOut"
    $sBad = Join-Path $work "stor98-bad.allow"
    foreach ($bad in @(@{ Line = "win98 SCSIPORT.SYS!ScsiPortInitialize"; Why = "an unknown target" },
                       @{ Line = "x86 NTOSKRNL.EXE"; Why = "no function" },
                       @{ Line = "x86 NTOSKRNL.EXE!ZwClose extra"; Why = "a third field" })) {
        Set-Content -LiteralPath $sBad -Encoding ASCII -Value @("# synthetic", "x86 NTOSKRNL.EXE!ZwClose", $bad.Line)
        $sOut = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path (Split-Path -Parent $PSScriptRoot) "check-stor98-build.ps1") -ParseOnly -AllowPath $sBad 2>&1 | Out-String
        Assert-True ($LASTEXITCODE -ne 0) "the Stor98 reader took a row with $($bad.Why): $($bad.Line)"
    }
    Set-Content -LiteralPath $sBad -Encoding ASCII -Value @("x86 NTOSKRNL.EXE!ZwClose", "x86 ntoskrnl.exe!ZwClose")
    $sOut = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path (Split-Path -Parent $PSScriptRoot) "check-stor98-build.ps1") -ParseOnly -AllowPath $sBad 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -ne 0) "the Stor98 reader took one row twice"
} catch {
    # An exception mid-suite is a FAILED TEST, not a crashed script. Without
    # this the run died at the throw with $ErrorActionPreference = "Stop",
    # printed no summary line, and left the reader to tell a broken harness
    # from a broken gate by reading a stack trace - while its sibling
    # test-evidence-manifests.ps1 had recorded exactly this case as a failure
    # since it was written (the audit of 2026-09-07).
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
