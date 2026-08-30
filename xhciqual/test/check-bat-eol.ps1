# check-bat-eol.ps1 - fail if any tracked *.BAT has a bare LF line ending.
#
# MS-DOS 7.1 (Windows 98) COMMAND.COM parses batch lines on CR. A .BAT saved
# with Unix LF-only endings mangles the tokens and dies with "Bad command or
# file name", silently skipping the :logerr safety branches. .gitattributes
# marks these files eol=crlf, but that only converts on checkout - a stale
# working-tree copy (or one written LF-only by an editor) still ships broken.
# This guard is the deploy-time backstop: build.cmd runs it before wmake.

$ErrorActionPreference = "Stop"
$testDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$qualDir = Split-Path -Parent $testDir
$repo = Split-Path -Parent $qualDir

# Every .BAT in the repo, not just xhciqual's: .gitattributes covers *.BAT,
# so a wrapper added elsewhere must be guarded too. Skip test staging dirs -
# those are generated per run and are not what ships to the field.
$bad = @()
$batFiles = Get-ChildItem -LiteralPath $repo -Filter *.BAT -Recurse -File |
    Where-Object { $_.FullName -notmatch '\\(\.git|external|tools|vm)\\' -and
                   $_.FullName -notmatch '\\xhciqual\\test\\' }
foreach ($bat in $batFiles) {
    $bytes = [System.IO.File]::ReadAllBytes($bat.FullName)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -eq 0x0A -and ($i -eq 0 -or $bytes[$i - 1] -ne 0x0D)) {
            $bad += $bat.FullName.Substring($repo.Length + 1)
            break
        }
    }
}

if ($bad.Count -gt 0) {
    Write-Host "ERROR: DOS batch file(s) have bare-LF line endings (need CRLF):"
    foreach ($name in $bad) { Write-Host "  $name" }
    Write-Host "Win98 COMMAND.COM needs CRLF. Fix with:"
    Write-Host "  git rm --cached --ignore-unmatch <file>; git checkout -- <file>"
    Write-Host "or re-save the file(s) with CRLF endings."
    exit 1
}

Write-Host ("BAT line-ending check: all {0} tracked *.BAT are CRLF." -f $batFiles.Count)
exit 0
