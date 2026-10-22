# check-readme-text.ps1 - fail if a tracked tool readme breaks the rule the
# release cut holds it to.
#
# make-release.ps1 publishes each tool's README.TXT, with a footer of its own,
# as that tool's readme.txt, and refuses the cut when a line is past 78
# columns or a character is not printable ASCII (TAB allowed): the files are
# read in Windows 98 Notepad and DOS EDIT, and Write-AsciiFile turns any other
# character into a question mark silently. Until roadmap-usb4 task 43.61 that
# was found only at a cut: on 2026-10-22 43.11's test cut stopped on one line
# in xhciinst\README.TXT and one in xhcitr\README.TXT, each left by a merge
# that had passed every gate. This runs the cut's own rule over the cut's own
# list on every gated build, so a merge cannot leave one.
#
# Nothing here is a second copy of the rule or the list. Both are
# scripts\package\package-common.ps1's, which make-release.ps1 reads too:
# Get-PlainTextFaults (the rule, and the messages the cut prints),
# $script:ToolReadmeDirs and $script:ToolReadmeName (the five tools and the
# file the cut reads in each), and Read-ToolReadmeLines (how it reads one).
# The footer the cut appends is make-release.ps1's own text and is checked
# there; what a merge changes is the tracked readme.
#
# And releases\history.md, which the cut embeds in the release's top-level
# readme.txt: rendered here as the cut renders it (Get-HistoryEmbedLines and
# ConvertFrom-MarkdownBlocks, package-common.ps1's too) and held to the same
# rule, since the wrap cannot shorten a word longer than a line and passes a
# non-ASCII character through. The rest of that readme is make-release.ps1's
# template, met at a cut.
#
#   check-readme-text.ps1             check the five tracked readmes and
#                                     the rendered release history
#   check-readme-text.ps1 -SelfTest   the rule's own refusals, in memory
#
# Exit 0 when every readme keeps the rule (or the self-test passes), 1
# otherwise.

param(
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
. (Join-Path $repo "scripts\package\package-common.ps1")

if ($SelfTest) {
    $failed = 0
    $cases = @(
        @{ What = "a 78-column line and a TAB";  Lines = @(("x" * 78), "a`tb", "");  Want = @() },
        @{ What = "a 79-column line";            Lines = @("fits", ("x" * 79));     Want = @("t.txt has 1 line(s) past 78 columns") },
        @{ What = "two long lines, one named";   Lines = @(("a" * 80), ("b" * 79)); Want = @("t.txt has 2 line(s) past 78 columns, the first being:`n" + ("a" * 80)) },
        @{ What = "a non-ASCII character";       Lines = @("ok", ("caf" + [char]0xE9)); Want = @("t.txt line 2 column 4 is not printable ASCII (U+00E9)") },
        @{ What = "a control character";         Lines = @(("bel" + [char]7));      Want = @("t.txt line 1 column 4 is not printable ASCII (U+0007)") },
        @{ What = "both, in that order";         Lines = @(("x" * 79), ("na" + [char]0x2013)); Want = @("past 78 columns", "not printable ASCII (U+2013)") }
    )
    foreach ($case in $cases) {
        $got = @(Get-PlainTextFaults -Name "t.txt" -Lines $case.Lines)
        $ok = ($got.Count -eq $case.Want.Count)
        for ($i = 0; $ok -and $i -lt $got.Count; $i++) {
            if (-not $got[$i].Contains($case.Want[$i])) { $ok = $false }
        }
        if (-not $ok) {
            Write-Host ("SELFTEST FAIL: {0}: got {1} fault(s): {2}" -f $case.What, $got.Count, ($got -join " | ")) -ForegroundColor Red
            $failed++
        }
    }

    # How a readme is read: CRLF and LF alike, trailing empty lines dropped.
    $tmp = [System.IO.Path]::GetTempFileName()
    try {
        [System.IO.File]::WriteAllText($tmp, "one`r`ntwo`nthree`r`n`r`n`n")
        $read = @(Read-ToolReadmeLines $tmp)
        if (($read -join "|") -ne "one|two|three") {
            Write-Host ("SELFTEST FAIL: Read-ToolReadmeLines read '{0}'" -f ($read -join "|")) -ForegroundColor Red
            $failed++
        }
    } finally {
        Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    }

    # The history as the cut renders it: from the first '##' heading, demoted,
    # wrapped, and a word too long for a line still refused.
    $hist = @(ConvertFrom-MarkdownBlocks -Lines @(Get-HistoryEmbedLines -Lines @(
        "# Title", "note", "", "## 1.0.0.0 - 01/01/2026", "", "- a bullet")) -Width $script:PlainTextColumns)
    if (($hist -join "|") -ne "  1.0.0.0 - 01/01/2026||    * a bullet") {
        Write-Host ("SELFTEST FAIL: the history rendered as '{0}'" -f ($hist -join "|")) -ForegroundColor Red
        $failed++
    }
    $hist = @(ConvertFrom-MarkdownBlocks -Lines @("## v", "", ("w" * 79)) -Width $script:PlainTextColumns)
    if (@(Get-PlainTextFaults -Name "h" -Lines $hist).Count -ne 1) {
        Write-Host "SELFTEST FAIL: a 79-character word in the history was not refused" -ForegroundColor Red
        $failed++
    }

    if ($script:ToolReadmeDirs.Count -lt 5) {
        Write-Host "SELFTEST FAIL: package-common.ps1 lists fewer than the five tool readmes" -ForegroundColor Red
        $failed++
    }
    if ($failed -gt 0) { exit 1 }
    Write-Host "readme text self-test: OK"
    exit 0
}

$faults = @()
foreach ($dir in $script:ToolReadmeDirs.Values) {
    $rel = Join-Path $dir $script:ToolReadmeName
    $path = Join-Path $repo $rel
    if (-not (Test-Path -LiteralPath $path)) {
        $faults += "$rel is missing; make-release.ps1 reads it."
        continue
    }
    $faults += @(Get-PlainTextFaults -Name $rel -Lines @(Read-ToolReadmeLines $path))
}

# The release history the top-level readme embeds, read as the cut reads it
# (Get-Content; make-release.ps1's default -ReleasesDir).
$histRel = "releases\history.md"
$histPath = Join-Path $repo $histRel
if (-not (Test-Path -LiteralPath $histPath)) {
    $faults += "$histRel is missing; make-release.ps1 embeds it in readme.txt."
} else {
    $histText = @(ConvertFrom-MarkdownBlocks -Lines @(Get-HistoryEmbedLines -Lines @(Get-Content -LiteralPath $histPath)) `
                                             -Width $script:PlainTextColumns)
    $faults += @(Get-PlainTextFaults -Name "readme.txt's release history ($histRel)" -Lines $histText)
}

if ($faults.Count -gt 0) {
    foreach ($f in $faults) { Write-Host "FAIL: $f" -ForegroundColor Red }
    Write-Host ("{0} fault(s) in the readmes; make-release.ps1 would refuse the cut." -f $faults.Count) -ForegroundColor Red
    exit 1
}
Write-Host ("readme text: OK ({0} tool readmes and the release history, {1} columns, printable ASCII)" -f $script:ToolReadmeDirs.Count, $script:PlainTextColumns)
exit 0
