# check-source-charset.ps1 - fail if tracked source carries a byte no part of
# this project's toolchain is meant to see.
#
# Three rules, roadmap task 22.7:
#
#   1. A CONTROL BYTE below 0x20 that is not TAB, CR or LF, anywhere in the
#      file. On 2026-09-12 a diagnostic comment written into src\xhci_slot.c
#      through a DOUBLE-quoted PowerShell here-string put a literal BEL (0x07)
#      into the source - the backtick is PowerShell's escape character and
#      backtick-a is its alert escape - and build-driver.cmd then compiled all
#      three x86 flavours and passed EVERY gate with that byte present. It was
#      caught by eye, because the text rendered as "ddress" in git diff.
#      Writing source through a PowerShell string layer is the documented
#      method in this repository, so every source edit passes through an
#      escape layer that can inject a byte silently. It landed in a comment
#      that time; in a string literal it would have reached the shipping
#      binary, and the driver's strings go out the debugcon channel this
#      project reads its evidence from. docs\contributing\lessons.md, "A
#      comment written through a PowerShell here-string put a BEL into src\,
#      and every gate passed", is the record.
#
#   2. A BYTE >= 0x80, anywhere in the file. The targets are Windows 98 and
#      MSVC 6.0 with C89, and the argument is NOT the same as rule 1's: a
#      non-ASCII byte in a **string literal** reaches the debugcon channel and
#      a Windows 98 console, where the encoding is not UTF-8, so a character
#      that looks right in an editor arrives as mojibake on the screen this
#      project reads its evidence from. Taking this rule cost exactly one
#      character: a UTF-8 section sign in an src\xhci.h comment reading
#      "Design record 08 (section)13.2's dated amendment", rewritten as
#      "section 13.2" on 2026-09-12 - which is what line 7788 of the same file
#      already said, 32 lines away, so the tree was inconsistent with itself.
#      A permitted BOM (see rule 3) is not counted against this rule.
#
#   3. A UTF-8 BOM (EF BB BF) at the head of a file the 1998-era toolchain
#      reads: C and assembler source, the resource script, the DDK's `sources`
#      and makefiles, the INFs, and batch and cmd files. MSVC 6.0's compiler
#      and Win98's 16-bit setup engine predate the BOM convention and read
#      those three bytes as content; COMMAND.COM reads them as the first
#      token of the first line.
#
# WHY RULE 3 IS NARROWER THAN THE OTHER TWO, measured 2026-09-12 and re-read
# 2026-09-17. Three tracked files under the scanned trees carry a UTF-8 BOM
# today (five did on 2026-09-12; scripts\inf-gate\check-inf.ps1 and
# xhcisnap\README.md have since lost theirs):
#
#     scripts\inf-gate\test-inf-checks.ps1    scripts\vm-matrix\offsets.labels.txt
#     scripts\package\test-package.ps1
#
# None is read by the 1998-era toolchain, and on a .ps1 a BOM is not a defect
# at all: Windows PowerShell 5.1 reads a BOM-less script as the system ANSI
# codepage, so the BOM is what makes a non-ASCII script read correctly. A rule
# covering every kind would have failed the build on files that work, so it
# covers the kinds where the byte does damage. Rules 1 and 2 have no such
# carve-out and cover every kind.

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $scriptDir

# The trees that hold source. src\ is the one that ships; the other four were
# measured clean on 2026-09-12 under all three rules, so including them costs
# nothing today and puts the guard where a future edit would land.
$trees = @("src", "test", "scripts", "xhcisnap", "xhciqual")

# File kinds the 1998-era toolchain reads. All three rules apply to these.
$toolchainExt = @(".c", ".h", ".asm", ".rc", ".def", ".inf", ".bat", ".cmd")
$toolchainName = @("makefile", "sources")

# Everything else that is source in this tree. Rules 1 and 2 only - see above.
# `.allow` is the import allowlist, which is tracked source this project edits
# by hand and which no other gate reads for encoding (the 2026-09-16 audit's
# D7).
$otherExt = @(".ps1", ".psd1", ".md", ".txt", ".list", ".expected", ".allow")

# Repo-relative paths that are not tracked source, each git-ignored for the
# reason given. A filesystem walk finds them; git would not, but no other gate
# in this build shells out to git and this one does not start.
$skip = @(
    '^src\\obj',                                  # build.exe output: obj, objchk, objchk_qemu, objfre
    '^src\\uas\\obj',                             # the same under src\uas, xhciuas.sys (task 31-A.2)
    '^scripts\\local\\',                          # per-operator bench tooling
    '^xhciqual\\test\\(win98hdd|hdd)\\',          # generated guest disks
    '^scripts\\vm-matrix\\matrix\.config\.psd1$'  # per-host device matrix, a copy of the tracked sample
)

# ---- the detector, one function over bytes so it can be self-tested ----

function Get-CharsetFaults {
    param([byte[]] $Bytes, [bool] $BomForbidden)

    $faults = @()
    $start = 0

    $hasBom = ($Bytes.Length -ge 3 -and
               $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF)
    if ($hasBom) {
        if ($BomForbidden) {
            $faults += "UTF-8 BOM (EF BB BF) at offset 0"
        }
        # Either way rule 2 skips those three bytes. A permitted BOM is not a
        # fault at all, and a forbidden one is already reported above - counting
        # it a second time as three non-ASCII bytes would bury the real message.
        $start = 3
    }

    # Line and column are what make these actionable: a control byte is
    # invisible in an editor, which is how the BEL survived a review, and a
    # non-ASCII byte usually looks exactly like the character it should be.
    $line = 1
    $col = 1
    for ($i = $start; $i -lt $Bytes.Length; $i++) {
        $b = $Bytes[$i]
        if ($b -lt 0x20 -and $b -ne 0x09 -and $b -ne 0x0A -and $b -ne 0x0D) {
            $faults += ("control byte 0x{0:X2} at offset {1} (line {2}, column {3})" -f $b, $i, $line, $col)
        } elseif ($b -ge 0x80) {
            $faults += ("non-ASCII byte 0x{0:X2} at offset {1} (line {2}, column {3})" -f $b, $i, $line, $col)
        }
        if ($b -eq 0x0A) { $line++; $col = 1 } else { $col++ }
    }

    return $faults
}

# ---- self-test, in memory, every run ----
#
# A gate whose failure path has never executed is a gate that reports a pass it
# did not establish - which is the whole of what task 22.7 is about. These run
# on every invocation because they cost microseconds and touch no disk.

function Assert-SelfTest {
    param([string] $What, [bool] $Condition)
    if (-not $Condition) {
        Write-Host "ERROR: charset check SELF-TEST failed: $What"
        Write-Host "The detector is broken, so a clean scan would prove nothing."
        exit 1
    }
}

$belBytes = [byte[]](0x2F, 0x2A, 0x07, 0x2A, 0x2F)           # /*<BEL>*/
$bomBytes = [byte[]](0xEF, 0xBB, 0xBF, 0x69, 0x6E, 0x74)     # <BOM>int
$okBytes  = [byte[]](0x69, 0x09, 0x6E, 0x0D, 0x0A, 0x74)     # i<TAB>n<CRLF>t
$nulBytes = [byte[]](0x61, 0x00, 0x62)                       # a<NUL>b
$hiBytes  = [byte[]](0x2F, 0x2A, 0xC2, 0xA7, 0x2A, 0x2F)     # /*<C2 A7 = section sign>*/
$bomHi    = [byte[]](0xEF, 0xBB, 0xBF, 0x69, 0xC2, 0xA7)     # <BOM>i<C2 A7>

Assert-SelfTest "a BEL is caught" `
    ((Get-CharsetFaults $belBytes $true).Count -eq 1)
Assert-SelfTest "a NUL is caught" `
    ((Get-CharsetFaults $nulBytes $true).Count -eq 1)
Assert-SelfTest "a BOM is caught where it is forbidden" `
    ((Get-CharsetFaults $bomBytes $true).Count -eq 1)
Assert-SelfTest "a permitted BOM is not a fault, and is not read as non-ASCII" `
    ((Get-CharsetFaults $bomBytes $false).Count -eq 0)
Assert-SelfTest "TAB, CR and LF pass" `
    ((Get-CharsetFaults $okBytes $true).Count -eq 0)
Assert-SelfTest "a UTF-8 section sign is caught, as two bytes" `
    ((Get-CharsetFaults $hiBytes $true).Count -eq 2)
Assert-SelfTest "a permitted BOM does not hide a non-ASCII byte after it" `
    ((Get-CharsetFaults $bomHi $false).Count -eq 2)

# ---- the scan ----

$bad = @()
$scanned = 0

foreach ($tree in $trees) {
    $treePath = Join-Path $repo $tree
    if (-not (Test-Path -LiteralPath $treePath)) { continue }

    foreach ($file in (Get-ChildItem -LiteralPath $treePath -Recurse -File -Force)) {
        $rel = $file.FullName.Substring($repo.Length + 1)

        $skipped = $false
        foreach ($pattern in $skip) {
            if ($rel -match $pattern) { $skipped = $true; break }
        }
        if ($skipped) { continue }

        $ext = $file.Extension.ToLowerInvariant()
        $name = $file.Name.ToLowerInvariant()

        if ($toolchainExt -contains $ext -or $toolchainName -contains $name) {
            $bomForbidden = $true
        } elseif ($otherExt -contains $ext) {
            $bomForbidden = $false
        } else {
            continue
        }

        $scanned++
        $faults = Get-CharsetFaults ([System.IO.File]::ReadAllBytes($file.FullName)) $bomForbidden

        # One line per file however many bytes are wrong: a file saved in the
        # wrong encoding would otherwise print thousands.
        if ($faults.Count -gt 4) {
            $bad += ("  {0}: {1} faults, first is {2}" -f $rel, $faults.Count, $faults[0])
        } else {
            foreach ($fault in $faults) { $bad += "  ${rel}: $fault" }
        }
    }
}

if ($bad.Count -gt 0) {
    Write-Host "ERROR: tracked source carries bytes the toolchain is not meant to see:"
    foreach ($entry in $bad) { Write-Host $entry }
    Write-Host ""
    Write-Host "A control byte is almost always an escape that got away: a PowerShell"
    Write-Host "DOUBLE-quoted string turns backtick-a into BEL, backtick-b into"
    Write-Host "backspace, backtick-e into ESC. Use a SINGLE-quoted here-string for"
    Write-Host "anything containing a backtick."
    Write-Host "A non-ASCII byte must not appear in source at all: in a string literal"
    Write-Host "it reaches the debugcon channel and a Windows 98 console, where the"
    Write-Host "encoding is not UTF-8. Spell it out in ASCII - write 'section', not the"
    Write-Host "section sign, and '-', not an en dash."
    Write-Host "A UTF-8 BOM must not head a file MSVC 6.0, rc.exe, build.exe,"
    Write-Host "COMMAND.COM or Win98 setup reads; re-save that file without one."
    exit 1
}

Write-Host ("Source charset check: {0} files clean (ASCII only, no stray control bytes, no BOM where it bites)." -f $scanned)
exit 0
