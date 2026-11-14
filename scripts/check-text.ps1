# check-text.ps1 - every text check of the gated build and of the release
# cut, run before anything is built (roadmap-usb4 task 44.1).
#
# A text failure used to cost a build. The gated build met its text checks
# only after its import, INF and packager self-tests, and the cut met the
# texts only it renders after a gated build of both architectures: on
# 2026-10-24 the 3.2.0.0 builds stopped twice on a line past 78 columns,
# one in xhciinst\README.TXT and one in a paragraph of the download's
# readme in scripts\package\package-common.ps1. build-driver.cmd runs this
# first, before its other self-tests, and it runs on its own:
#
#   powershell -ExecutionPolicy Bypass -File scripts\check-text.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\check-text.ps1 -SelfTest
#
# THE BUILD'S TEXT CHECKS, each its own script, run here in turn:
#   - xhciqual\test\check-bat-eol.ps1: every tracked .BAT and .CMD is CRLF
#   - scripts\check-source-charset.ps1: no control byte, byte >= 0x80 or
#     forbidden BOM in tracked source
#   - scripts\check-readme-text.ps1: the tool readmes the cut publishes and
#     releases\history.md as the readme embeds it, at 78 columns of
#     printable ASCII (-SelfTest runs its self-test too)
#
# THE CUT'S TEXT CHECKS that need no build, read from the cut's own code:
#   - every text make-release.ps1 publishes from a template: each here-string
#     it assigns to a variable (the readme template and its parts, the
#     qualifier's readme, each tool's readme footer and NOTICE.TXT), and each
#     line it adds to the readme's file list ($contents) or to the list an
#     incomplete cut prints ($missingTools), a paragraph it wraps with
#     Format-Wrapped included, read out of its syntax tree.
#     Every line is held to Get-PlainTextFaults (78 columns, printable
#     ASCII) with its placeholders replaced by stand-ins at the width the
#     cut gives them, or by nothing where the cut puts a block of its own
#     there; a line's expansions and format items count as nothing (or as
#     their alignment's width), so its width is a lower bound. A placeholder
#     a text carries that no Replace() on that same text substitutes is a
#     fault, and so is a TODO(...) or TBD in any of them (the cut's
#     Find-DraftMarkers). An error message is thrown or passed to
#     Stop-CutCheck as it is written, never assigned, so make-release.ps1
#     keeps that shape: a here-string assigned to a variable is published.
#   - the readme template's two rules of its own: no four-part version
#     written by hand ($allowedTemplateVersions) and no snapshot schema but
#     src\xhci.h's XHCI_SNAPSHOT_SCHEMA (Get-SnapSchema), each read from
#     make-release.ps1 too
#   - the parts each download's readme is rendered from
#     (Get-ReadmeDownloadText, Get-ReadmeOtherContentsText and
#     Get-ReadmeMsFilesText in package-common.ps1), for every download of
#     every set of downloads, with and without the driver pack, to the same
#     rule and with no draft marker
#   - the changelog entry for src\xhci_version.h's version: one
#     releases\history.md heading, dated as src\xhci98.inf's DriverVer
#     (package-common.ps1's Get-ChangelogEntryFaults, the cut's own check),
#     so a version bump carries its entry's heading from the start
#   - docs\using\release-notes.md's head describing src\xhci_version.h's
#     version ("package version `X`" in its first lines)
#   - no TODO(...) or TBD in docs\using\release-notes.md or in
#     releases\history.md from its first entry down, which the readme embeds
#   - no claim of package-common.ps1's $script:ReadmeForbiddenPhrases in any
#     published text or download readme part
#   - the shipped scripts the cut holds to their content rules:
#     src\silent-on.bat and silent-off.bat (Get-SilentBatchFaults),
#     src\silent.reg (Get-SilentRegFaults) and each field report batch
#     (Get-ReportBatchSpecs, Get-ReportBatchFaults), all package-common.ps1's
#     (the port switchover's scripts until task 49.47 removed them)
#   - the name by which src\silent.vbs finds the controller: its
#     IsXhciDesc takes every name
#     src\xhci98.inf and xhci98-amd64.inf give the controller and the names
#     earlier releases gave it, and refuses the INFs' other device names
#     (package-common.ps1's Get-ControllerDescFaults, task 44.75)
#
# THE TRACKED FILES' WORDING RULES (2026-11-06), in this script:
#   - docs\**\*.md: no TAB and no other control byte but CR and LF, and no
#     "releases" followed by "." and a digit - the marks an escape leaves
#     when it is eaten as text is written (a "\3" read as a back-reference
#     or an octal escape left releases<0x03>.4.1.0 in a roadmap line, and
#     three "\t" became TABs in design records) (Get-DocByteFaults)
#   - AGENTS.md, "What NOT to Do": no tracked file names the review tool, a
#     review round or a finding number, outside the dated run sheets
#     (docs\contributing\runs\run-*.md) and README.md's "AI usage" section,
#     which names the agents on purpose (Get-WordingFaults). Published
#     releases\<version>\ trees are outside it, as they are never edited.
#     Stor98's tree (src\stor98\, with src\cm\stor98cm.h, its interface
#     header's byte-for-byte copy) is inside it since 2026-11-06 (owner;
#     src\stor98\AGENTS.md), apart from what is not Stor98's own text or is
#     never edited: its ignored out\, the unmodified upstream reference copy
#     third_party\, and the published releases\<version>\. String
#     literals are read as any other text, a test's printed output included.
#   - the owner's decision of 2026-11-13: no tracked file names the separate
#     component the 98 SE / ME message-interrupt client served, outside
#     the vendored src\msi4nt.h (Get-ComponentFaults)
#   All are enforced ($script:WordingRulesEnforced): each fault is listed
#   and fails the run. They were report-only from their addition until the
#   tree was clean of what they found then (2026-11-06).
#
# Out of scope, as no text: the cut's checks that two sources agree on a
# number (XHCITR's snapshot schema against the driver's, each tool's
# shared version macro). They need no build either, and stay the cut's.
#
# What it does not check is what the cut renders from a build or a pin: the
# sizes and hashes in the readme's file list, the linker's copyright lines
# in a NOTICE.TXT ({MAPLINES}), the driver pack's own texts and the release
# notes' Stor98 declaration. make-release.ps1 checks those at the cut, and
# -CheckOnly before it, once the builds exist.
#
# Every check runs and every fault is listed. Exit 0 when every check
# passes (or the self-test does), 1 otherwise.

param(
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
. (Join-Path $repo "scripts\package\package-common.ps1")

# The width a stand-in takes is the widest the cut writes there: version and
# date from the tree; the directory names for a cut of both architectures,
# make-release.ps1's default, whose words are longer than one
# architecture's.
function Get-PlaceholderStandIns {
    param([string]$Version)
    return @{
        "{VERSION}"    = $Version
        "{DATE}"       = "2026-12-31"
        "{RELEASEDIR}" = "RELEASE-X86 or RELEASE-X64"
    }
}

# The variables make-release.ps1 builds a published text from line by line,
# with what the cut puts before each line.
$script:LineTextPrefixes = @{
    '$contents'     = ""
    '$missingTools' = "      "
}

# The functions the checks take from make-release.ps1.
$script:CutFunctions = @("Find-DraftMarkers", "Get-SnapSchema", "Get-InfDriverDate")

function Get-LiteralOfExpression {
    # The text an expression a line is built from writes at least: a string
    # as it is, an expandable string or a -f format with its expansions and
    # format items as nothing (or as the width an alignment pads them to).
    # $null for anything else.
    param($Expr)
    $L = [System.Management.Automation.Language.Ast]
    while ($Expr -is [System.Management.Automation.Language.ParenExpressionAst]) {
        $p = $Expr.Pipeline
        if ($p -isnot [System.Management.Automation.Language.PipelineAst] -or $p.PipelineElements.Count -ne 1 -or
            $p.PipelineElements[0] -isnot [System.Management.Automation.Language.CommandExpressionAst]) { return $null }
        $Expr = $p.PipelineElements[0].Expression
    }
    if ($Expr -is [System.Management.Automation.Language.StringConstantExpressionAst]) { return $Expr.Value }
    if ($Expr -is [System.Management.Automation.Language.ExpandableStringExpressionAst]) {
        return (Remove-Expansions $Expr.Value)
    }
    if ($Expr -is [System.Management.Automation.Language.BinaryExpressionAst] -and
        [string]$Expr.Operator -eq "Plus") {
        # A concatenation: its literal parts, and nothing for the rest.
        $l = Get-LiteralOfExpression $Expr.Left
        $r = Get-LiteralOfExpression $Expr.Right
        if ($null -eq $l -and $null -eq $r) { return $null }
        return ([string]$l + [string]$r)
    }
    if ($Expr -is [System.Management.Automation.Language.BinaryExpressionAst] -and
        [string]$Expr.Operator -eq "Format") {
        $fmt = Get-LiteralOfExpression $Expr.Left
        if ($null -eq $fmt) { return $null }
        return [regex]::Replace($fmt, '\{\d+(?:,(-?\d+))?(?::[^}]*)?\}', {
            param($m)
            if ($m.Groups[1].Success) { return (" " * [Math]::Abs([int]$m.Groups[1].Value)) }
            return ""
        })
    }
    return $null
}

function Get-WrappedLines {
    # The lines of an @(Format-Wrapped -Text ... -Width ...) the cut adds to
    # a text, wrapped by package-common.ps1's Format-Wrapped from the
    # literal parts of its -Text (Get-LiteralOfExpression): a word too long
    # for a line, a character or a draft marker shows in them as in the
    # cut's. $null for any other expression.
    param($Expr)
    if ($Expr -isnot [System.Management.Automation.Language.ArrayExpressionAst]) { return $null }
    $st = @($Expr.SubExpression.Statements)
    if ($st.Count -ne 1 -or $st[0] -isnot [System.Management.Automation.Language.PipelineAst] -or
        $st[0].PipelineElements.Count -ne 1 -or
        $st[0].PipelineElements[0] -isnot [System.Management.Automation.Language.CommandAst]) { return $null }
    $cmd = $st[0].PipelineElements[0]
    if ($cmd.GetCommandName() -ne "Format-Wrapped") { return $null }
    $els = @($cmd.CommandElements)
    $params = @{}
    for ($i = 1; $i -lt $els.Count; $i++) {
        if ($els[$i] -isnot [System.Management.Automation.Language.CommandParameterAst] -or $i + 1 -ge $els.Count) { return $null }
        $pname = $els[$i].ParameterName
        $val = $els[$i + 1]
        $i++
        if ($pname -eq "Width") {
            if ($val -isnot [System.Management.Automation.Language.ConstantExpressionAst]) { return $null }
            $params.Width = [int]$val.Value
        } else {
            $lit = Get-LiteralOfExpression $val
            if ($null -eq $lit) { return $null }
            $params[$pname] = $lit
        }
    }
    if (-not $params.ContainsKey("Text") -or -not $params.ContainsKey("Width")) { return $null }
    return @(Format-Wrapped @params)
}

function Remove-Expansions {
    # A double-quoted string's text with each $name, ${name} and $(...) taken
    # out.
    param([string]$Text)
    return [regex]::Replace($Text, '\$\((?:[^()]|\([^()]*\))*\)|\$\{[^}]*\}|\$[A-Za-z_][A-Za-z0-9_:]*', "")
}

function Get-RootVariable {
    # The variable a member chain such as $a.Replace(..).Replace(..) starts
    # from, as written ("$a"), or "".
    param($Expr)
    while ($Expr -is [System.Management.Automation.Language.MemberExpressionAst]) { $Expr = $Expr.Expression }
    while ($Expr -is [System.Management.Automation.Language.ParenExpressionAst] -and
           $Expr.Pipeline.PipelineElements.Count -eq 1 -and
           $Expr.Pipeline.PipelineElements[0] -is [System.Management.Automation.Language.CommandExpressionAst]) {
        $Expr = $Expr.Pipeline.PipelineElements[0].Expression
        while ($Expr -is [System.Management.Automation.Language.MemberExpressionAst]) { $Expr = $Expr.Expression }
    }
    if ($Expr -is [System.Management.Automation.Language.VariableExpressionAst]) { return $Expr.Extent.Text }
    return ""
}

function Read-CutScript {
    # make-release.ps1 (or a self-test's stand-in) as the checks below read
    # it: each published text (a here-string assigned to a variable, or a
    # line added to one of $script:LineTextPrefixes), the placeholders each
    # variable's Replace() calls substitute, and the source of the functions
    # and the allow-list the cut applies.
    param([string]$Path)

    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors)
    if ($errors.Count -gt 0) {
        throw ("'{0}' does not parse: {1}" -f $Path, $errors[0].Message)
    }

    $texts = @()
    $assigns = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.AssignmentStatementAst] }, $true))
    foreach ($a in $assigns) {
        $name = $a.Left.Extent.Text
        $hs = @($a.Right.FindAll({ param($n)
                    ($n -is [System.Management.Automation.Language.StringConstantExpressionAst] -or
                     $n -is [System.Management.Automation.Language.ExpandableStringExpressionAst]) -and
                    ([string]$n.StringConstantType) -like "*HereString" -and
                    -not ($n.Parent -is [System.Management.Automation.Language.ExpandableStringExpressionAst]) }, $true))
        foreach ($h in $hs) {
            $lines = @($h.Value -split "`r?`n")
            if ([string]$h.StringConstantType -eq "DoubleQuotedHereString") {
                $lines = @($lines | ForEach-Object { Remove-Expansions $_ })
            }
            $texts += [pscustomobject]@{ Name = $name; Line = $h.Extent.StartLineNumber + 1; OneLine = $false; Lines = $lines }
        }
        if ([string]$a.Operator -eq "PlusEquals" -and $script:LineTextPrefixes.ContainsKey($name) -and
            $a.Right -is [System.Management.Automation.Language.CommandExpressionAst]) {
            $lit = Get-LiteralOfExpression $a.Right.Expression
            if ($null -ne $lit) {
                $texts += [pscustomobject]@{ Name = $name; Line = $a.Extent.StartLineNumber; OneLine = $true
                                             Lines = @($script:LineTextPrefixes[$name] + $lit) }
            } else {
                $wrapped = Get-WrappedLines $a.Right.Expression
                if ($null -ne $wrapped) {
                    $texts += [pscustomobject]@{ Name = $name; Line = $a.Extent.StartLineNumber; OneLine = $true
                                                 Lines = @($wrapped | ForEach-Object { $script:LineTextPrefixes[$name] + $_ }) }
                }
            }
        }
    }

    $functions = @{}
    $defs = $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)
    foreach ($d in $defs) { $functions[$d.Name] = $d.Extent.Text }

    $allowed = $null
    foreach ($a in $assigns) {
        if ($a.Left.Extent.Text -eq '$allowedTemplateVersions') { $allowed = $a.Right.Extent.Text }
    }

    # Which placeholders each variable's own Replace() chain substitutes:
    # one substituted on another text is still published as written here.
    $replaced = @{}
    $calls = $ast.FindAll({ param($n)
                 $n -is [System.Management.Automation.Language.InvokeMemberExpressionAst] -and
                 $n.Member.Extent.Text -eq "Replace" -and $n.Arguments.Count -ge 1 -and
                 $n.Arguments[0] -is [System.Management.Automation.Language.StringConstantExpressionAst] -and
                 $n.Arguments[0].Value -cmatch '^\{[A-Z]+\}$' }, $true)
    foreach ($c in $calls) {
        $root = Get-RootVariable $c.Expression
        if ($root -eq "") { continue }
        if (-not $replaced.ContainsKey($root)) { $replaced[$root] = @() }
        $replaced[$root] += $c.Arguments[0].Value
    }

    return [pscustomobject]@{
        Texts       = $texts
        Functions   = $functions
        AllowedText = $allowed
        Replaced    = $replaced
    }
}

function Get-CutTextFaults {
    # The faults of the texts Read-CutScript found, against the rule, the
    # draft markers and the readme template's own two rules. $Script is
    # what Read-CutScript returned; $Label names its file in each fault.
    param($Script, [string]$Label, [string]$Version, [int]$DriverSchema)

    $faults = @()
    $standIns = Get-PlaceholderStandIns -Version $Version

    $templates = @($Script.Texts | Where-Object { $_.Name -eq '$template' }).Count
    if ($templates -ne 1) {
        $faults += "$Label assigns `$template, the readme template, $templates time(s), not once: this check cannot find what the cut publishes."
    }
    foreach ($fn in $script:CutFunctions) {
        if (-not $Script.Functions.ContainsKey($fn)) {
            $faults += "$Label has no function $fn, which this check takes from the cut."
        }
    }
    if ($null -eq $Script.AllowedText) {
        $faults += "$Label has no `$allowedTemplateVersions, the cut's list of versions its readme template may name."
    }
    if ($faults.Count -gt 0) { return $faults }

    # The cut's own function and list, defined here from its source.
    . ([scriptblock]::Create($Script.Functions["Find-DraftMarkers"]))
    $allowedVersions = & ([scriptblock]::Create($Script.AllowedText))

    foreach ($t in $Script.Texts) {
        $subst = @()
        if ($Script.Replaced.ContainsKey($t.Name)) { $subst = $Script.Replaced[$t.Name] }
        for ($i = 0; $i -lt $t.Lines.Count; $i++) {
            $line = $t.Lines[$i]
            # A line-built text's lines all come from its one statement.
            $at = if ($t.OneLine) { $t.Line } else { $t.Line + $i }
            $where = "{0} line {1} ({2})" -f $Label, $at, $t.Name
            foreach ($m in @([regex]::Matches($line, '\{[A-Z]+\}') | ForEach-Object { $_.Value } | Sort-Object -Unique)) {
                if ($subst -notcontains $m) {
                    $faults += "$where carries $m, which $Label never substitutes in $($t.Name), so it would be published as written."
                }
                $with = if ($standIns.ContainsKey($m)) { $standIns[$m] } else { "" }
                $line = $line.Replace($m, $with)
            }
            $faults += @(Get-PlainTextFaults -Name $where -Lines @($line))
            $faults += @(Find-DraftMarkers -Lines @($line) -Label $where | ForEach-Object { "$_ (a draft marker; the cut refuses it)" })
        }
        $faults += @(Get-ForbiddenPhraseFaults -Lines $t.Lines -Name ("{0} line {1} ({2})" -f $Label, $t.Line, $t.Name))
        if ($t.Name -ne '$template') { continue }

        $body = $t.Lines -join "`n"
        $named = @([regex]::Matches($body, '\b\d+\.\d+\.\d+\.\d+\b') | ForEach-Object { $_.Value } | Sort-Object -Unique |
                   Where-Object { -not $allowedVersions.ContainsKey($_) })
        if ($named.Count -gt 0) {
            $faults += "$Label's readme template (from line $($t.Line)) names version(s) $($named -join ', ') by hand; the cut refuses it. Write it with {VERSION}, or put the sentence in releases\history.md."
        }
        $schemas = @([regex]::Matches($body, 'snapshot schema (\d+)') | ForEach-Object { [int]$_.Groups[1].Value } |
                     Sort-Object -Unique | Where-Object { $_ -ne $DriverSchema })
        if ($schemas.Count -gt 0) {
            $faults += "$Label's readme template (from line $($t.Line)) names snapshot schema $($schemas -join ', '), and src\xhci.h declares XHCI_SNAPSHOT_SCHEMA $DriverSchema; the cut refuses it."
        }
    }
    return $faults
}

function Get-ForbiddenPhraseFaults {
    # package-common.ps1's $script:ReadmeForbiddenPhrases in the text of
    # -Lines, matched without regard to case as test-package.ps1 matches
    # them, over the lines joined with their whitespace collapsed: a wrap
    # or a line break may not split a claim out of sight.
    param([string[]]$Lines, [string]$Name)
    $faults = @()
    $text = [regex]::Replace(($Lines -join " "), '\s+', " ")
    foreach ($p in $script:ReadmeForbiddenPhrases) {
        if ($text.IndexOf($p, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $faults += "$Name says '$p', a claim no published text may make (package-common.ps1, `$script:ReadmeForbiddenPhrases)."
        }
    }
    return $faults
}

function Get-DownloadReadmeFaults {
    # The parts each download's readme is rendered from, for every download
    # of every set a cut can publish, with and without the driver pack (a
    # download with USBDRV\NONFREE\ is cut only with it), held to the rule
    # and to $Script's Find-DraftMarkers. Tags are stand-ins at a width no
    # pinned tag has reached.
    param($Script, [string]$Version)

    . ([scriptblock]::Create($Script.Functions["Find-DraftMarkers"]))
    $faults = @()
    $tag = "v99.99.99"
    $tools = @($script:WdmFreeToolWords.Keys)
    foreach ($pack in @($true, $false)) {
        foreach ($kinds in @(@("base"), @("base", "free"), @("base", "nonfree"), @("base", "free", "nonfree"))) {
            if (-not $pack -and $kinds -contains "nonfree") { continue }
            $freeTag = if ($kinds -contains "free") { $tag } else { "" }
            $nonFreeTag = if ($kinds -contains "nonfree") { $tag } else { "" }
            foreach ($kind in $kinds) {
                $what = "download '{0}' of ({1}), driver pack {2}" -f $kind, ($kinds -join ", "), $(if ($pack) { "in" } else { "out" })
                $parts = @(
                    @{ Name = "Get-ReadmeDownloadText";
                       Text = (Get-ReadmeDownloadText -Version $Version -Kind $kind -Kinds $kinds -FreeTag $freeTag -NonFreeTag $nonFreeTag -DriverPackIncluded $pack) },
                    @{ Name = "Get-ReadmeOtherContentsText";
                       Text = (Get-ReadmeOtherContentsText -Kind $kind -FreeTag $freeTag -NonFreeTag $nonFreeTag -FreeTools $tools) },
                    @{ Name = "Get-ReadmeMsFilesText";
                       Text = (Get-ReadmeMsFilesText -DriverPackIncluded $pack -Kind $kind) }
                )
                foreach ($p in $parts) {
                    $name = "package-common.ps1's {0} for {1}" -f $p.Name, $what
                    $lines = @($p.Text -split "`r?`n")
                    $faults += @(Get-PlainTextFaults -Name $name -Lines $lines)
                    $faults += @(Find-DraftMarkers -Lines $lines -Label $name | ForEach-Object { "$_ (a draft marker; the cut refuses it)" })
                    $faults += @(Get-ForbiddenPhraseFaults -Lines $lines -Name $name)
                }
            }
        }
    }
    return $faults
}

function Get-ReleaseNotesHeadFaults {
    # The release notes open by naming the version they describe ("This
    # file describes package version `X`"), and it must be -Version, the
    # package's: on 2026-10-29 the head still named 3.2.0.0 with the tree,
    # every INF and the history at 3.3.0.0.
    param([string[]]$Lines, [string]$Version)
    $head = (@($Lines | Select-Object -First 12)) -join " "
    $m = [regex]::Match($head, 'package version\s+`(\d+\.\d+\.\d+\.\d+)`')
    if (-not $m.Success) { return @("docs\using\release-notes.md: its first 12 lines name no package version; its head must describe $Version.") }
    if ($m.Groups[1].Value -ne $Version) { return @("docs\using\release-notes.md: its head describes package version $($m.Groups[1].Value), but src\xhci_version.h is $Version.") }
    return @()
}

function Get-HistoryFaults {
    # The changelog entry for -Version, dated as -ReleaseDate (the cut's own
    # check), and TODO(...) or TBD in the release notes or in history.md
    # from its first entry down: the cut refuses both, the history because
    # the readme embeds every entry.
    param($Script, [string]$Root, [string]$Version, [string]$ReleaseDate)

    . ([scriptblock]::Create($Script.Functions["Find-DraftMarkers"]))
    $faults = @()
    $hist = Join-Path $Root "releases\history.md"
    if (-not (Test-Path -LiteralPath $hist)) {
        return @("releases\history.md is missing; the cut embeds it and requires its entry for $Version.")
    }
    $lines = @(Get-Content -LiteralPath $hist)
    $faults += @(Get-ChangelogEntryFaults -Lines $lines -Version $Version -ReleaseDate $ReleaseDate -Label "releases\history.md")
    $first = 0
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^##\s') { $first = $i; break } }
    foreach ($h in @(Find-DraftMarkers -Lines $lines -Label "releases\history.md")) {
        if ($h -match ' line (\d+): ' -and [int]$Matches[1] -gt $first) { $faults += "$h (a draft marker; the cut refuses it)" }
    }
    $notes = Join-Path $Root "docs\using\release-notes.md"
    if (Test-Path -LiteralPath $notes -PathType Leaf) {
        $faults += @(Get-ReleaseNotesHeadFaults -Lines @(Get-Content -LiteralPath $notes) -Version $Version)
        $faults += @(Find-DraftMarkers -Lines @(Get-Content -LiteralPath $notes) -Label "docs\using\release-notes.md" |
                     ForEach-Object { "$_ (a draft marker; the cut refuses it)" })
    } else {
        # Missing, the head and draft-marker checks would pass by having
        # nothing to read.
        $faults += "docs\using\release-notes.md is missing; its head must describe $Version."
    }
    return $faults
}

function Get-ShippedScriptFaults {
    # The batch, registry and script files the cut publishes, read as bytes
    # and Latin-1 as the packager's self-tests read them, held to their
    # content rules: the silent install's two batches and SILENT.REG, and
    # each field report batch.
    param([string]$Root)

    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $faults = @()
    $files = @()
    foreach ($b in @(@{ Rel = "src\silent-on.bat"; Value = "1" }, @{ Rel = "src\silent-off.bat"; Value = "0" })) {
        $files += @{ Rel = $b.Rel; Check = { param($t, $x) Get-SilentBatchFaults -Text $t -Value $x }; Arg = $b.Value }
    }
    $files += @{ Rel = "src\silent.reg"; Check = { param($t, $x) Get-SilentRegFaults -Text $t }; Arg = $null }
    foreach ($spec in @(Get-ReportBatchSpecs)) {
        $files += @{ Rel = ($spec.Dir + "\" + $spec.Name); Check = { param($t, $x) Get-ReportBatchFaults -Text $t -Spec $x }; Arg = $spec }
    }
    foreach ($f in $files) {
        $path = Join-Path $Root $f.Rel
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $faults += "$($f.Rel) is missing; the cut publishes it."
            continue
        }
        $text = $latin1.GetString([System.IO.File]::ReadAllBytes($path))
        foreach ($why in @(& $f.Check $text $f.Arg)) { $faults += "$($f.Rel): $why" }
    }
    return $faults
}

function Get-CutFaults {
    # Every check of the cut's texts above over the tree at -Root, with the
    # cut script at -CutPath.
    param([string]$Root, [string]$CutPath, [string]$Label)

    $cut = Read-CutScript -Path $CutPath
    $faults = @()
    foreach ($fn in $script:CutFunctions) {
        if (-not $cut.Functions.ContainsKey($fn)) {
            # Get-CutTextFaults names each one missing, and nothing else runs.
            return [pscustomobject]@{ Faults = @(Get-CutTextFaults -Script $cut -Label $Label -Version "" -DriverSchema 0); Texts = 0 }
        }
        . ([scriptblock]::Create($cut.Functions[$fn]))
    }
    $version = Get-PackageVersion -Repo $Root
    $schema = Get-SnapSchema -Path (Join-Path $Root "src\xhci.h") -Macro "XHCI_SNAPSHOT_SCHEMA"
    $releaseDate = Get-InfDriverDate -Path (Join-Path $Root "src\xhci98.inf")
    $faults += @(Get-CutTextFaults -Script $cut -Label $Label -Version $version -DriverSchema $schema)
    $faults += @(Get-DownloadReadmeFaults -Script $cut -Version $version)
    $faults += @(Get-HistoryFaults -Script $cut -Root $Root -Version $version -ReleaseDate $releaseDate)
    $faults += @(Get-ShippedScriptFaults -Root $Root)
    $faults += @(Get-ControllerDescFaults -Root $Root)
    return [pscustomobject]@{ Faults = $faults; Texts = @($cut.Texts).Count }
}

# ---- the tracked files' wording rules (2026-11-06) ----
#
# On since the tree was clean of what they found when added (2026-11-06);
# $false makes them report-only again.
$script:WordingRulesEnforced = $true

# The review tool's name, spelled by character code so that this file, which
# the rule reads too, does not name it.
$script:ReviewToolWord = -join ([char[]](0x63, 0x6F, 0x64, 0x65, 0x78))
$script:WordingPatterns = @(
    @{ Rx = ('\b' + $script:ReviewToolWord + '\b'); Why = "names the review tool" },
    @{ Rx = '\breview\s+round\s+\d+'; Why = "names a review round" },
    @{ Rx = '\bround\s+\d+\s+of\s+(?:the\s+)?review'; Why = "names a review round" },
    @{ Rx = '\bfindings?\s+#?\d+'; Why = "names a finding number" }
)

# The owner's decision of 2026-11-13 (release 3.5.0.0): the 98 SE / ME
# message-interrupt client left the tree, and no tracked file names the
# separate component it served. The name is spelled by character code, as
# the review tool's, so that this file does not carry it. Outside the rule:
# src\msi4nt.h alone, the vendored header kept byte for byte, whose own
# comments name it (docs\ came under the rule once its rewrite landed).
$script:ComponentWord = -join ([char[]](0x6D, 0x73, 0x69, 0x34, 0x76, 0x78, 0x64))
$script:ComponentSkip = @('^src\\msi4nt\.h$')

function Get-ComponentFaults {
    # The lines of one tracked file against the component's name, any case.
    param([string[]]$Lines, [string]$Label)
    $faults = @()
    for ($n = 0; $n -lt $Lines.Count; $n++) {
        if ($Lines[$n] -match $script:ComponentWord) {
            $faults += "$Label line $($n + 1): names the separate component the 98 SE / ME message-interrupt client served"
        }
    }
    return $faults
}

function Get-DocByteFaults {
    # A docs\ Markdown file's bytes: a TAB or any other control byte but CR
    # and LF, and the text "releases" followed by "." and a digit. Each fault
    # is "<Label> line N: what".
    param([byte[]]$Bytes, [string]$Label)
    $faults = @()
    $line = 1
    for ($i = 0; $i -lt $Bytes.Length; $i++) {
        $b = $Bytes[$i]
        if ($b -eq 0x0A) { $line++; continue }
        if ($b -eq 0x09) { $faults += "$Label line ${line}: a TAB (an eaten \t?)"; continue }
        if ($b -lt 0x20 -and $b -ne 0x0D) { $faults += ("{0} line {1}: control byte 0x{2:X2} (an eaten escape?)" -f $Label, $line, $b) }
    }
    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($Bytes)
    $lines = $text -split "`n"
    for ($n = 0; $n -lt $lines.Count; $n++) {
        if ($lines[$n] -cmatch 'releases\.[0-9]') { $faults += "$Label line $($n + 1): 'releases.<digit>', a path whose backslash and digit were eaten" }
    }
    return $faults
}

function Get-WordingFaults {
    # The lines of one tracked file against $script:WordingPatterns. With
    # -Readme, the "AI usage" section (its heading to the next heading of
    # the same or a higher level) is skipped: its credit line names the
    # agents on purpose.
    param([string[]]$Lines, [string]$Label, [switch]$Readme)
    $faults = @()
    $skipLevel = 0
    for ($n = 0; $n -lt $Lines.Count; $n++) {
        $l = $Lines[$n]
        if ($Readme -and $l -match '^(#+)\s') {
            $level = $Matches[1].Length
            if ($skipLevel -gt 0 -and $level -le $skipLevel) { $skipLevel = 0 }
            if ($l -match '^#+\s+AI usage\s*$') { $skipLevel = $level; continue }
        }
        if ($skipLevel -gt 0) { continue }
        foreach ($p in $script:WordingPatterns) {
            if ($l -match $p.Rx) {
                $faults += "$Label line $($n + 1): $($p.Why) ('$($Matches[0])')"
                break
            }
        }
    }
    return $faults
}

function Get-TreeWordingFaults {
    # Both rules over the tracked text of the tree at -Root, found by a walk
    # (no git): the source trees, docs\ and .github\, and the named files at
    # the root and under releases\ and external\. What is skipped is
    # git-ignored, or outside the rules (see the file's head).
    param([string]$Root)
    $faults = @()
    $kinds = @(".c", ".h", ".asm", ".rc", ".def", ".inf", ".inx", ".bat", ".cmd", ".oem", ".vbs", ".reg", ".manifest",
               ".ps1", ".psd1", ".md", ".txt", ".list", ".expected", ".allow", ".py", ".sh", ".yml", ".example")
    $names = @("makefile", "sources")
    $skip = @(
        '^src\\(.+\\)?obj[^\\]*\\',
        '^src\\stor98\\out\\',
        '^src\\stor98\\third_party\\',
        '^src\\stor98\\releases\\[0-9][^\\]*\\',
        '^docs\\contributing\\runs\\run-[^\\]*\.md$',
        '^docs\\references\\',
        '^scripts\\local\\',
        '^xhciqual\\results\\',
        '^xhciqual\\test\\(win98hdd|hdd)\\'
    )
    $files = @()
    foreach ($tree in @("src", "test", "scripts", "xhciinst", "xhcimark", "xhcitr", "xhciqual", "xhciperf", "docs", ".github")) {
        $tp = Join-Path $Root $tree
        if (Test-Path -LiteralPath $tp -PathType Container) { $files += @(Get-ChildItem -LiteralPath $tp -Recurse -File -Force) }
    }
    foreach ($rel in @("README.md", "AGENTS.md", "CLAUDE.md", "LICENSE", "LICENSE-docs.md", "releases\README.md", "releases\history.md", "external\README.md")) {
        $fp = Join-Path $Root $rel
        if (Test-Path -LiteralPath $fp -PathType Leaf) { $files += @(Get-Item -LiteralPath $fp -Force) }
    }
    $rootFull = (Get-Item -LiteralPath $Root).FullName.TrimEnd('\')
    $docByteCount = 0
    $wordCount = 0
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($rootFull.Length + 1)
        $isDocMd = ($rel -match '^docs\\' -and $f.Extension -ieq ".md" -and $rel -notmatch '^docs\\references\\')
        if ($isDocMd) {
            $docByteCount++
            $faults += @(Get-DocByteFaults -Bytes ([System.IO.File]::ReadAllBytes($f.FullName)) -Label $rel)
        }
        if (($kinds -notcontains $f.Extension.ToLowerInvariant() -and $names -notcontains $f.Name.ToLowerInvariant()) -and $f.Name -ne "LICENSE") { continue }
        $skipped = $false
        foreach ($pattern in $skip) { if ($rel -match $pattern) { $skipped = $true; break } }
        if ($skipped) { continue }
        $wordCount++
        $text = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes($f.FullName))
        $faults += @(Get-WordingFaults -Lines ($text -split "`n") -Label $rel -Readme:($rel -ieq "README.md"))
        $named = $true
        foreach ($pattern in $script:ComponentSkip) { if ($rel -match $pattern) { $named = $false; break } }
        if ($named) { $faults += @(Get-ComponentFaults -Lines ($text -split "`n") -Label $rel) }
    }
    return [pscustomobject]@{ Faults = $faults; DocFiles = $docByteCount; Files = $wordCount }
}

function Invoke-Child {
    # One of the build's own text checks, as a child process, so its exit
    # code is its verdict.
    param([string]$Rel, [string[]]$Arguments = @())
    Write-Host ""
    Write-Host ("--- {0} {1}" -f $Rel, ($Arguments -join " ")).TrimEnd()
    # To the host, not the pipeline, or its lines would be this function's
    # return value beside the verdict, and a list of lines is true.
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo $Rel) @Arguments | Out-Host
    return ($LASTEXITCODE -eq 0)
}

if ($SelfTest) {
    function Assert-Self([string]$What, [bool]$Ok) {
        if (-not $Ok) {
            Write-Host "SELFTEST FAIL: $What" -ForegroundColor Red
            $script:selfFailed++
        }
    }
    $script:selfFailed = 0
    function Test-Has([string[]]$Got, [string]$Find) {
        return (@($Got | Where-Object { $_.Contains($Find) }).Count -eq 1)
    }

    # A stand-in cut script: the template, a tool footer and a notice whose
    # placeholder only it substitutes, lines of the two line-built texts,
    # the cut's functions and list, and two error messages that are not
    # published.
    $fixture = @(
        'function Find-DraftMarkers {',
        '    param([string[]]$Lines, [string]$Label)',
        '    $hits = @()',
        '    for ($i = 0; $i -lt $Lines.Count; $i++) {',
        '        if ($Lines[$i] -cmatch ''TODO\(|\bTBD\b'') { $hits += ("{0} line {1}: {2}" -f $Label, ($i + 1), $Lines[$i].Trim()) }',
        '    }',
        '    return $hits',
        '}',
        'function Get-SnapSchema { param([string]$Path, [string]$Macro) return 0 }',
        'function Get-InfDriverDate { param([string]$Path) return "2026-01-01" }',
        '$allowedTemplateVersions = @{ "1.2.3.4" = "a reason" }',
        '$template = @''',
        'A line that fits.',
        ('Fits until the version: ' + ("x" * 47) + ' {VERSION}'),
        'Install from {RELEASEDIR}\.',
        'Since 9.9.9.9 and 1.2.3.4.',
        'Read with XHCITR, snapshot schema 7.',
        ('Caf' + [char]0xE9 + ' TODO(bench)'),
        '{MSFILES}',
        'An {UNKNOWN} placeholder and {MAPLINES}.',
        '''@',
        '$readme = $template.Replace("{VERSION}", $v).',
        '    Replace("{RELEASEDIR}", $d).Replace("{MSFILES}", $m)',
        '$footer = @"',
        ('Version $Version, ' + ("y" * 61)),
        ('$(Get-Thing -Name "z") ' + ("w" * 79)),
        'A footer line, TBD.',
        '"@',
        '$notice = @"',
        '{MAPLINES}',
        '"@',
        '$notice = $notice.Replace("{MAPLINES}", $x)',
        ('$contents += "' + ("c" * 79) + '"'),
        ('$contents += ("  {0,-30} ' + ("f" * 47) + '" -f $z)'),
        ('$contents += ("  {0} ' + ("g" * 70) + '" -f $z)'),
        ('if ($null -eq $q) { $missingTools += "' + ("m" * 73) + '" }'),
        'throw @"',
        ("An error message " + ("e" * 90)),
        '"@',
        'Stop-CutCheck @"',
        ("Another " + ("e" * 90)),
        '"@',
        '$footer2 = @"',
        'It redistributes nothing.',
        '"@',
        ('$contents += @(Format-Wrapped -Text ("Two words " + $(Get-X) + " and " + "' + ("w" * 80) + '") -Width 78 -FirstPrefix "  " -RestPrefix "  ")'),
        '$contents += @(Format-Wrapped -Text "short TODO(bench) words" -Width 78)',
        ('$contents += @(Format-Wrapped -Text "' + ("x" * 68) + ' WINDOWS 98 ONLY" -Width 78 -FirstPrefix "  " -RestPrefix "  ")')
    ) -join "`r`n"
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("check-text-" + [System.IO.Path]::GetRandomFileName() + ".ps1")
    try {
        # With a BOM, as Windows PowerShell 5.1 reads a non-ASCII script
        # correctly only with one.
        $utf8Bom = New-Object System.Text.UTF8Encoding($true)
        [System.IO.File]::WriteAllText($tmp, $fixture, $utf8Bom)
        $cut = Read-CutScript -Path $tmp
        Assert-Self "four here-strings and seven line-built texts found, not the two error messages ($(@($cut.Texts).Count) found)" (@($cut.Texts).Count -eq 11)
        Assert-Self "the template's own substitutions read from its Replace() chain" ((($cut.Replaced['$template'] | Sort-Object) -join ",") -eq "{MSFILES},{RELEASEDIR},{VERSION}")
        $got = @(Get-CutTextFaults -Script $cut -Label "fixture" -Version "10.20.30.40" -DriverSchema 6)
        $want = @(
            @{ What = "{VERSION} at its stand-in width";             Find = "fixture line 14 (`$template) has 1 line(s) past 78 columns" },
            @{ What = "a non-ASCII character";                       Find = "fixture line 18 (`$template) line 1 column 4 is not printable ASCII (U+00E9)" },
            @{ What = "a draft marker in the template";              Find = "fixture line 18 (`$template) line 1: Caf" },
            @{ What = "a placeholder never substituted";             Find = "fixture line 20 (`$template) carries {UNKNOWN}" },
            @{ What = "a placeholder another text substitutes";      Find = "fixture line 20 (`$template) carries {MAPLINES}" },
            @{ What = "a hand-written version, not 1.2.3.4";         Find = "names version(s) 9.9.9.9 by hand" },
            @{ What = "a stale snapshot schema";                     Find = "names snapshot schema 7, and src\xhci.h declares XHCI_SNAPSHOT_SCHEMA 6" },
            @{ What = "a double-quoted line's literal part";         Find = "fixture line 26 (`$footer) has 1 line(s) past 78 columns" },
            @{ What = "a draft marker in a footer";                  Find = "fixture line 27 (`$footer) line 1: A footer line, TBD." },
            @{ What = "a line added to `$contents";                  Find = "fixture line 33 (`$contents) has 1 line(s) past 78 columns" },
            @{ What = "a format item's alignment width";             Find = "fixture line 34 (`$contents) has 1 line(s) past 78 columns" },
            @{ What = "the prefix of a `$missingTools line";         Find = "fixture line 36 (`$missingTools) has 1 line(s) past 78 columns" },
            @{ What = "a forbidden claim";                           Find = "fixture line 44 (`$footer2) says 'redistributes nothing'" },
            @{ What = "a forbidden claim a wrap splits";             Find = "fixture line 48 (`$contents) says 'WINDOWS 98 ONLY'" },
            @{ What = "a word too long in wrapped prose";            Find = "fixture line 46 (`$contents) has 1 line(s) past 78 columns" },
            @{ What = "a draft marker in wrapped prose";             Find = "fixture line 47 (`$contents) line 1: short TODO(bench) words" }
        )
        foreach ($w in $want) {
            Assert-Self ("{0}: not reported in: {1}" -f $w.What, ($got -join " | ")) (Test-Has $got $w.Find)
        }
        Assert-Self ("{0} fault(s), not {1}: {2}" -f $got.Count, $want.Count, ($got -join " | ")) ($got.Count -eq $want.Count)

        # A cut script without its template is a fault, not a silent pass.
        [System.IO.File]::WriteAllText($tmp, ($fixture -replace '\$template = @', '$other = @'), $utf8Bom)
        $none = @(Get-CutTextFaults -Script (Read-CutScript -Path $tmp) -Label "fixture" -Version "1.0.0.0" -DriverSchema 6)
        Assert-Self "a script with no `$template refused ($($none -join ' | '))" ($none.Count -eq 1 -and $none[0].Contains("assigns `$template"))
    } finally {
        Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    }

    # The download readme parts read with stand-in tags.
    $stub = [pscustomobject]@{ Functions = @{ "Find-DraftMarkers" =
        'function Find-DraftMarkers { param([string[]]$Lines, [string]$Label) for ($i = 0; $i -lt $Lines.Count; $i++) { if ($Lines[$i] -cmatch ''TODO\(|\bTBD\b'') { "$Label line $($i + 1): $($Lines[$i])" } } }' } }
    $savedWords = $script:WdmFreeToolWords
    try {
        $script:WdmFreeToolWords = [ordered]@{ "t" = ("a" * 80) }
        Assert-Self "a word too long for a line in a free tool's description" (@(Get-DownloadReadmeFaults -Script $stub -Version "1.0.0.0").Count -ge 1)
        $script:WdmFreeToolWords = [ordered]@{ "t" = "a tool, TODO(words)" }
        Assert-Self "a draft marker in a free tool's description" (@(Get-DownloadReadmeFaults -Script $stub -Version "1.0.0.0" | Where-Object { $_.Contains("draft marker") }).Count -ge 1)
        $script:WdmFreeToolWords = [ordered]@{ "t" = "a tool" }
        Assert-Self "a short version passes" (@(Get-DownloadReadmeFaults -Script $stub -Version "1.0.0.0").Count -eq 0)
        Assert-Self "a version too long for the list of downloads" (@(Get-DownloadReadmeFaults -Script $stub -Version ("1" * 30 + ".0.0.0")).Count -ge 1)
    } finally {
        $script:WdmFreeToolWords = $savedWords
    }

    # The history: the changelog entry's refusals, and a draft marker in an
    # entry but not in the file's own preamble.
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("check-text-" + [System.IO.Path]::GetRandomFileName())
    try {
        [void](New-Item -ItemType Directory -Path (Join-Path $root "releases"))
        [void](New-Item -ItemType Directory -Path (Join-Path $root "docs\using"))
        $histPath = Join-Path $root "releases\history.md"
        $notesPath = Join-Path $root "docs\using\release-notes.md"
        [System.IO.File]::WriteAllText($notesPath, ('This file describes package version `2.0.0.0`.' + "`r`n"))
        $cases = @(
            @{ What = "an entry dated as the INF";     Text = "# H`r`nTODO( in the preamble`r`n## 2.0.0.0 - 2026-01-01`r`n`r`ntext`r`n"; Want = "" },
            @{ What = "no entry";                      Text = "# H`r`n## 1.0.0.0 - 2026-01-01`r`n";                                    Want = "has no entry for 2.0.0.0" },
            @{ What = "two entries";                   Text = "## 2.0.0.0 - 2026-01-01`r`n## 2.0.0.0 - 2026-01-01`r`n";                Want = "has 2 headings for 2.0.0.0" },
            @{ What = "an undated entry";              Text = "## 2.0.0.0`r`n";                                                       Want = "is not a dated entry" },
            @{ What = "a date not a calendar date";    Text = "## 2.0.0.0 - 2026-13-45`r`n";                                          Want = "which is not a calendar date" },
            @{ What = "a date not the INF's";          Text = "## 2.0.0.0 - 2026-01-02`r`n";                                          Want = "but src\xhci98.inf's" },
            @{ What = "a draft marker in an entry";    Text = "## 2.0.0.0 - 2026-01-01`r`nTBD`r`n";                                   Want = "releases\history.md line 2: TBD (a draft marker" }
        )
        foreach ($c in $cases) {
            [System.IO.File]::WriteAllText($histPath, $c.Text)
            $hf = @(Get-HistoryFaults -Script $stub -Root $root -Version "2.0.0.0" -ReleaseDate "2026-01-01")
            if ($c.Want -eq "") {
                Assert-Self ("{0}: {1}" -f $c.What, ($hf -join " | ")) ($hf.Count -eq 0)
            } else {
                Assert-Self ("{0}: {1}" -f $c.What, ($hf -join " | ")) ($hf.Count -eq 1 -and $hf[0].Contains($c.Want))
            }
        }
        [System.IO.File]::WriteAllText($histPath, "## 2.0.0.0 - 2026-01-01`r`n")
        [System.IO.File]::WriteAllText((Join-Path $root "docs\using\release-notes.md"), ('This file describes package version `2.0.0.0`.' + "`r`nTODO(bench)`r`n"))
        $hf = @(Get-HistoryFaults -Script $stub -Root $root -Version "2.0.0.0" -ReleaseDate "2026-01-01")
        Assert-Self ("a draft marker in the release notes: " + ($hf -join " | ")) ($hf.Count -eq 1 -and $hf[0].Contains("release-notes.md line 2: TODO(bench)"))
        Remove-Item -LiteralPath $notesPath -Force
        $hf = @(Get-HistoryFaults -Script $stub -Root $root -Version "2.0.0.0" -ReleaseDate "2026-01-01")
        Assert-Self ("missing release notes: " + ($hf -join " | ")) ($hf.Count -eq 1 -and $hf[0].Contains("release-notes.md is missing"))

        # The tracked files' wording rules, on stand-in texts. The words
        # under test are assembled here, as the rules assemble them, so
        # that this file names none of them.
        $tw = $script:ReviewToolWord
        $rr = "review" + " round 2"
        $fn = "Finding" + " 3"
        $docBytes = [System.Text.Encoding]::ASCII.GetBytes("ok`r`na`tb`r`nreleases" + [char]3 + ".4.1.0`r`nsee releases.4.1.0`r`nreleases\3.4.1.0`r`n")
        $df = @(Get-DocByteFaults -Bytes $docBytes -Label "d.md")
        Assert-Self ("docs bytes: a TAB, a control byte and 'releases.<digit>' each found, a whole path not: " + ($df -join " | ")) `
            ($df.Count -eq 3 -and (Test-Has $df "d.md line 2: a TAB") -and (Test-Has $df "d.md line 3: control byte 0x03") -and (Test-Has $df "d.md line 4: 'releases.<digit>'"))
        Assert-Self "docs bytes: CR, LF and printable text pass" (@(Get-DocByteFaults -Bytes ([System.Text.Encoding]::ASCII.GetBytes("a`r`nb`n")) -Label "d.md").Count -eq 0)
        $wl = @("Reviewed and revised.", "The $tw reviewer said so.", "Converged at $rr.", "See $fn above.", "Findings are listed below.", "codexes and $($tw)ish are not the name")
        $wf = @(Get-WordingFaults -Lines $wl -Label "w.c")
        Assert-Self ("wording: the tool's name, a review round and a finding number each found, and nothing else: " + ($wf -join " | ")) `
            ($wf.Count -eq 3 -and (Test-Has $wf "w.c line 2: names the review tool") -and (Test-Has $wf "w.c line 3: names a review round") -and (Test-Has $wf "w.c line 4: names a finding number"))
        $rm = @("# Title", "Text by $tw.", "## AI usage", "", "- $tw as a reviewer", "### Detail", "- $tw again", "## Licence", "Not $tw here.")
        $rf = @(Get-WordingFaults -Lines $rm -Label "README.md" -Readme)
        Assert-Self ("wording: README.md's AI usage section is skipped and nothing else is: " + ($rf -join " | ")) `
            ($rf.Count -eq 2 -and (Test-Has $rf "README.md line 2:") -and (Test-Has $rf "README.md line 9:"))
        $rf = @(Get-WordingFaults -Lines $rm -Label "other.md")
        Assert-Self "wording: the AI usage section is skipped in README.md alone" ($rf.Count -eq 4)
        $cw = $script:ComponentWord
        $cl = @("/* the message library (msi4nt) */", "#include `"$cw.h`"", "Uses $($cw.ToUpperInvariant()).", "msi4 alone")
        $cf = @(Get-ComponentFaults -Lines $cl -Label "c.c")
        Assert-Self ("component: its name found in any case, msi4nt and a prefix not: " + ($cf -join " | ")) `
            ($cf.Count -eq 2 -and (Test-Has $cf "c.c line 2:") -and (Test-Has $cf "c.c line 3:"))

        # The tree walk's scope (2026-11-06): Stor98's own texts and the CM's
        # copy of its header are read; a dated run sheet, Stor98's ignored
        # out\, its upstream reference copy and its published releases are
        # not. Each stand-in file names the tool once.
        $wroot = Join-Path $root "wordtree"
        $wfiles = [ordered]@{
            "src\stor98\docs\PLAN.md" = $true; "src\stor98\src\engine.c" = $true
            "src\cm\stor98cm.h" = $true; "src\stor98\tests\unit\other_ut.c" = $true
            "docs\contributing\runs\run-99.md" = $false; "src\stor98\out\build.txt" = $false
            "src\stor98\third_party\nvme2k-1.2.0.0\nvme.c" = $false
            "src\stor98\releases\0.4.0.0\readme.txt" = $false
        }
        foreach ($wk in $wfiles.Keys) {
            $wp = Join-Path $wroot $wk
            [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $wp))
            [System.IO.File]::WriteAllText($wp, "line one`r`nThe $tw reviewer.`r`n")
        }
        $tf = @((Get-TreeWordingFaults -Root $wroot).Faults)
        $wwant = @($wfiles.Keys | Where-Object { $wfiles[$_] })
        Assert-Self ("wording: the tree walk reads Stor98's texts and the CM's header copy, and skips run sheets, out\, third_party\ and releases\: " + ($tf -join " | ")) `
            ($tf.Count -eq $wwant.Count -and @($wwant | Where-Object { -not (Test-Has $tf ($_ + " line 2: names the review tool")) }).Count -eq 0)

        # A string literal is text like any other: a Stor98 unit test's
        # printed line naming the tool, and one naming a review round, are
        # each a fault, and the same printf with neither is not.
        $wroot2 = Join-Path $root "wordtree2"
        $up = Join-Path $wroot2 "src\stor98\tests\unit\x_ut.c"
        [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $up))
        [System.IO.File]::WriteAllText($up, ('    printf("x_ut: a case (' + $tw + ')\n");' + "`r`n" + '    printf("x_ut: a case (' + $rr + ')\n");' + "`r`n" + '    printf("x_ut: a case (sense ownership)\n");' + "`r`n"))
        $hf = @((Get-TreeWordingFaults -Root $wroot2).Faults)
        Assert-Self ("wording: a unit test's printed text is read: " + ($hf -join " | ")) `
            ($hf.Count -eq 2 -and (Test-Has $hf "x_ut.c line 1: names the review tool") -and (Test-Has $hf "x_ut.c line 2: names a review round"))

        # The shipped scripts: this tree's copies pass, and a changed value
        # in SILENT.REG and a missing report batch are each refused.
        $copies = @("src\silent-on.bat", "src\silent-off.bat", "src\silent.reg") +
                  @(Get-ReportBatchSpecs | ForEach-Object { $_.Dir + "\" + $_.Name })
        foreach ($rel in $copies) {
            $to = Join-Path $root $rel
            [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to))
            Copy-Item -LiteralPath (Join-Path $repo $rel) -Destination $to
        }
        $sf = @(Get-ShippedScriptFaults -Root $root)
        Assert-Self ("this tree's shipped scripts: " + ($sf -join " | ")) ($sf.Count -eq 0)
        $regPath = Join-Path $root "src\silent.reg"
        $reg = [System.IO.File]::ReadAllText($regPath)
        [System.IO.File]::WriteAllText($regPath, $reg.Replace("hex:c3,08,06,50", "hex:c3,08,06,62"))
        Remove-Item -LiteralPath (Join-Path $root $copies[3])
        $sf = @(Get-ShippedScriptFaults -Root $root)
        Assert-Self ("the mutated scripts: " + ($sf -join " | ")) (
            @($sf | Where-Object { $_.StartsWith("src\silent.reg: ") }).Count -ge 1 -and
            @($sf | Where-Object { $_.Contains("is missing; the cut publishes it") }).Count -eq 1)

        # The controller's names (task 44.75): this tree's script and two
        # INFs pass; the whole-name match the scripts had before 44.75, a
        # listed model's name it would refuse and a hub's name it would
        # take are each refused.
        foreach ($rel in @("src\silent.vbs", "src\xhci98.inf", "src\xhci98-amd64.inf")) {
            Copy-Item -LiteralPath (Join-Path $repo $rel) -Destination (Join-Path $root $rel)
        }
        $cf = @(Get-ControllerDescFaults -Root $root)
        Assert-Self ("this tree's controller names: " + ($cf -join " | ")) ($cf.Count -eq 0)
        $silentPath = Join-Path $root "src\silent.vbs"
        $silentText = [System.IO.File]::ReadAllText($silentPath)
        $infPath = Join-Path $root "src\xhci98.inf"
        $infText = [System.IO.File]::ReadAllText($infPath)
        $amdPath = Join-Path $root "src\xhci98-amd64.inf"
        $amdText = [System.IO.File]::ReadAllText($amdPath)
        $nameCases = @(
            @{ What = "the whole-name match of 3.2.0.0"; Path = $silentPath; Orig = $silentText
               Text = $silentText.Replace("If IsXhciDesc(desc) Then", "If LCase(desc) = LCase(XHCI_DESC) Or LCase(desc) = LCase(XHCI_DESC_OLD) Then"); Want = "FindKeys does not take" },
            @{ What = "IsXhciDesc changed"; Path = $silentPath; Orig = $silentText
               Text = $silentText.Replace('p & " (" And', 'p & "(" And'); Want = "word for word" },
            @{ What = "a model named as before task 44.61"; Path = $infPath; Orig = $infText
               Text = $infText.Replace('XRedHatQEMUxHCIDesc="xHCI98 USB 3 Controller (Red Hat QEMU xHCI)"', 'XRedHatQEMUxHCIDesc="xHCI98 USB 3.x Controller (Red Hat QEMU xHCI)"'); Want = "is refused by IsXhciDesc" },
            @{ What = "an indented model line with an old name"; Path = $infPath; Orig = $infText
               Text = $infText.Replace("`r`n%XRedHatQEMUxHCIDesc%=", "`r`n  %XOldNameDesc%=Xhci.Dev,PCI\VEN_FFFF&DEV_FFFF`r`n%XRedHatQEMUxHCIDesc%=").Replace("`r`n[Strings]`r`n", "`r`n[Strings]`r`nXOldNameDesc=`"xHCI98 USB 3.x Controller (Old)`"`r`n"); Want = "(%XOldNameDesc%) is refused" },
            @{ What = "the root hub named as a controller"; Path = $amdPath; Orig = $amdText
               Text = $amdText.Replace('RootHubDesc="xHCI98 USB 3 Root Hub"', 'RootHubDesc="xHCI98 USB 3 Controller (Root Hub)"'); Want = "is not the controller and IsXhciDesc takes it" }
        )
        foreach ($nc in $nameCases) {
            Assert-Self ("{0}: the mutation did not apply" -f $nc.What) ($nc.Text -cne $nc.Orig)
            [System.IO.File]::WriteAllText($nc.Path, $nc.Text)
            $cf = @(Get-ControllerDescFaults -Root $root)
            Assert-Self ("{0}: {1}" -f $nc.What, ($cf -join " | ")) ($cf.Count -ge 1 -and @($cf | Where-Object { $_.Contains($nc.Want) }).Count -ge 1)
            [System.IO.File]::WriteAllText($nc.Path, $nc.Orig)
        }
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }

    # The release notes' head against the package version.
    $rnHead = @('# xhci98 - Release Notes', '', 'This file describes package version `9.8.7.6`, the', 'nth release.')
    Assert-Self "the release notes' head at the package version is refused" (@(Get-ReleaseNotesHeadFaults -Lines $rnHead -Version "9.8.7.6").Count -eq 0)
    Assert-Self "the release notes' head at another version is taken" (Test-Has @(Get-ReleaseNotesHeadFaults -Lines $rnHead -Version "9.8.7.7") "describes package version 9.8.7.6")
    Assert-Self "release notes naming no version in their head are taken" (Test-Has @(Get-ReleaseNotesHeadFaults -Lines @("# xhci98 - Release Notes", "", "Nothing here.") -Version "9.8.7.6") "name no package version")

    $childOk = Invoke-Child -Rel "scripts\check-readme-text.ps1" -Arguments @("-SelfTest")
    if (-not $childOk) { $script:selfFailed++ }
    if ($script:selfFailed -gt 0) {
        Write-Host ("text check self-test: {0} failure(s)" -f $script:selfFailed) -ForegroundColor Red
        exit 1
    }
    Write-Host "text check self-test: OK"
    exit 0
}

# The build's text checks, each in its own process and every one run, so one
# run lists every failure.
$failedChecks = @()
foreach ($c in @("xhciqual\test\check-bat-eol.ps1", "scripts\check-source-charset.ps1", "scripts\check-readme-text.ps1")) {
    if (-not (Invoke-Child -Rel $c)) { $failedChecks += $c }
}

# The cut's.
Write-Host ""
Write-Host "--- the release cut's texts (scripts\package\make-release.ps1, package-common.ps1)"
$texts = 0
try {
    $result = Get-CutFaults -Root $repo -CutPath (Join-Path $repo "scripts\package\make-release.ps1") -Label "make-release.ps1"
    $faults = @($result.Faults)
    $texts = $result.Texts
} catch {
    $faults = @("the cut's texts could not be read: $($_.Exception.Message)")
}
if ($faults.Count -gt 0) {
    foreach ($f in $faults) { Write-Host "FAIL: $f" -ForegroundColor Red }
    Write-Host ("{0} fault(s) in what the release cut publishes; make-release.ps1 would refuse the cut." -f $faults.Count) -ForegroundColor Red
    $failedChecks += "the release cut's texts"
} else {
    Write-Host ("the release cut's texts: OK ({0} texts of make-release.ps1, the download readme parts, the changelog entry, the release notes, the shipped scripts, the scripts' controller names; {1} columns, printable ASCII)" -f $texts, $script:PlainTextColumns)
}

# The tracked files' wording rules.
Write-Host ""
Write-Host "--- the tracked files' wording (docs' control bytes and TABs; AGENTS.md's review-tool, review-round and finding-number rule)"
$wordFaults = @()
$wordResult = $null
try {
    $wordResult = Get-TreeWordingFaults -Root $repo
    $wordFaults = @($wordResult.Faults)
} catch {
    $wordFaults = @("the tracked files could not be read: $($_.Exception.Message)")
}
if ($wordFaults.Count -gt 0) {
    $colour = if ($script:WordingRulesEnforced) { "Red" } else { "Yellow" }
    $tag = if ($script:WordingRulesEnforced) { "FAIL" } else { "WARN" }
    foreach ($f in $wordFaults) { Write-Host "${tag}: $f" -ForegroundColor $colour }
    if ($script:WordingRulesEnforced) {
        Write-Host ("{0} fault(s) in the tracked files' wording." -f $wordFaults.Count) -ForegroundColor Red
        $failedChecks += "the tracked files' wording"
    } else {
        Write-Host ("{0} warning(s) in the tracked files' wording; these rules are report-only until the tree is clean (`$script:WordingRulesEnforced in this file)." -f $wordFaults.Count) -ForegroundColor Yellow
    }
} else {
    Write-Host ("the tracked files' wording: OK ({0} files, {1} of them docs\ Markdown)" -f $wordResult.Files, $wordResult.DocFiles)
}

Write-Host ""
if ($failedChecks.Count -gt 0) {
    Write-Host ("TEXT CHECKS FAILED: {0}" -f ($failedChecks -join "; ")) -ForegroundColor Red
    exit 1
}
Write-Host "text checks: OK"
exit 0
