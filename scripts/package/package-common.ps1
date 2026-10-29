Set-StrictMode -Version 2.0

# Shared pieces of the two packaging scripts: the media-layout reader that
# keeps make-package.ps1 and make-release.ps1 on check-inf.ps1's own parse of
# [SourceDisksFiles], the binary-versus-INF version comparison, and the
# flavour-marker reader. Until release 1.0.0.1 this file also read
# usbd-sources.expected, the manifest of the Microsoft files the media then
# carried; the media carries none now (the OS supplies usbd.sys, usbhub.sys,
# usbport.sys on the NT targets and, since 1.0.2.0, usbui.dll on every target,
# all through the INF's LayoutFile), so there is nothing to
# authenticate and the reader is gone with the manifest.

function Read-MediaLayout {
    # Reads what check-inf.ps1 -EmitMediaLayout wrote: the place each
    # [SourceDisksFiles] entry has to occupy on the media, as that script's own
    # parse of the INF computed it. Keys are lower-cased source names.
    #
    # The packager reads this rather than parsing [SourceDisksFiles] itself.
    # Two parsers would be free to disagree, and the only way they can disagree
    # is a file staged at one path and authenticated at another - which is the
    # bug the per-file subdirectory handling exists to close.
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "media layout not written: $Path"
    }

    $layout = @{}
    $lineNo = 0
    foreach ($raw in Get-Content -LiteralPath $Path) {
        $lineNo++
        $line = $raw.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) { continue }
        if ($line -notmatch '^([^=]+)=(.+)$') {
            throw "$Path line ${lineNo}: expected 'sourcename=relative\path'"
        }
        $name = $Matches[1].Trim()
        $relative = $Matches[2].Trim()
        if ([System.IO.Path]::IsPathRooted($relative) -or $relative -match "\.\.") {
            throw "$Path line ${lineNo}: '$relative' must be relative to the package root and must not escape it"
        }
        $layout[$name.ToLowerInvariant()] = $relative
    }

    if ($layout.Count -eq 0) {
        throw "$Path names no media files; the INF's [SourceDisksFiles] is empty or was not parsed"
    }
    return $layout
}

# ------------------------------------------------------------------
# Task 8-A.4: does a binary's reported FileVersion equal the INF's DriverVer?
# ------------------------------------------------------------------
#
# Its own function so that scripts\package\test-package.ps1 can drive it on
# strings. The alternative - testing it only through make-package.ps1 - needs a
# stand-in binary carrying a *chosen* version resource, and there is none in the
# tree to use: the reference binaries are gitignored and the driver's own version
# is whatever the last build produced. So the comparison is the unit, and the one
# vector that matters is a string this used to accept and must not.
#
# **The whole reported string has to be four numbers and nothing else.** An
# earlier version split on separators and compared the first four tokens, which
# silently accepted "1.0.0.1 stale" as 1.0.0.1 - and that trailing text is
# exactly what a user reads off the Version tab and quotes in a bug report. A
# resource may legitimately spell the version with dots or commas, so both are
# accepted as separators; anything that is neither a separator nor a digit is
# not. A `Declared` value shorter than four parts is zero-extended, since an INF
# `DriverVer` of "1.0" means 1.0.0.0.
#
# Which of the three build flavours an image is, read out of the file (task
# 13-L.1). Returns "release", "debug", "qemu" or "" when the image carries no
# marker at all - which means a binary built before the marker existed.
#
# **This is shared because both packaging scripts need the same answer and must
# not derive it two ways.** The path is not evidence: -DriverPath can name a
# binary anywhere, and "objchk_qemu" contains "objchk", so a path match is wrong
# in exactly the direction that would publish the one flavour that must never
# ship. VS_FF_DEBUG is not evidence either - `debug` and `qemu` are both checked
# DDK builds and both set it.
#
# **Exactly one marker, or none.** Returning the first of several would accept
# an ambiguous image as whichever name happened to be tested first, and both
# callers present this as an artifact-level identity gate on a `-DriverPath`
# that may name a binary from anywhere - which is precisely why they read the
# artifact instead of trusting the build wrapper. An image carrying two markers
# is one the preprocessor let through with more than one flavour define, and it
# is not a debug build with a stray string in it. `scripts\check-flavour-marker.ps1`
# has always demanded exactly one; this did not until a review pointed
# out that the two gates disagreed. (That review named commit `efa86a3`, which
# is not in this repository's history: it predates the history rewrite and
# cannot be resolved. The finding stands; the citation does not.)
#
# HOSTTEST is in the list for the same reason it is in that script's: it can
# never be in a linked driver, so finding one means the flavour defines have
# gone somewhere very strange, and a check that silently ignored a name it
# knows about is how a fourth flavour arrives unnoticed.
#
function Get-ImageFlavourMarker {
    param([string]$Path)

    # The scan is common.ps1's, so this and scripts\check-flavour-marker.ps1
    # cannot disagree about what a marker is (the audit of 2026-09-07). What
    # differs is the
    # answer's shape: that gate wants "exactly one, and this one", and this
    # returns which one it is.
    $found = @(Get-XhciFlavourMarkers -Path $Path |
        ForEach-Object { $_.Substring("XHCI98_FLAVOUR_".Length) })
    if ($found.Count -eq 1) {
        return $found[0].ToLower()
    }
    if ($found.Count -gt 1) {
        return "ambiguous (" + (($found | ForEach-Object { $_.ToLower() }) -join ", ") + ")"
    }
    return ""
}

# The connection manager's tunnel-port filter INF, where a flavour directory
# keeps it (design record 21 section 3.3, record 23 section 4.1). The x86
# package keeps it in nt\, beside nt\xhcicm.sys, its own copy of the
# binary: Windows 98 SE and ME read a $Windows NT$ INF beside the one they
# are pointed at and would offer the filter for the tunnel bridge
# (runs/run-39.md, 39-0.4), and Vista's and 7's driver store takes no
# source outside the INF's directory. The x64 package keeps it at the root,
# beside xhcicm.inf and the one xhcicm.sys, from 3.1.0.0: 98 and ME never
# read it, and no other INF there binds a PCI bridge. -PublishedVersion
# reads a published tree in the layout it was published with; 3.0.0.0, the
# one x64 version before, has nt\ as the x86 package does. -Arch takes the
# packager's amd64 and the release legs' x64 alike.
function Get-CmFilterRel {
    param([string]$Arch, [string]$PublishedVersion = "")
    $isX64 = ($Arch -eq "amd64" -or $Arch -eq "x64")
    if ($isX64 -and ($PublishedVersion -eq "" -or [version]$PublishedVersion -ge [version]"3.1.0.0")) {
        return "xhcicmf.inf"
    }
    return "nt\xhcicmf.inf"
}

# The connection manager's published files in a flavour directory of that
# layout: the NHI INF, the filter INF and the binary, and in nt\'s layout
# the filter INF's own copy of the binary, nt\xhcicm.sys.
function Get-CmPublishable {
    param([string]$Arch, [string]$PublishedVersion = "")
    $rel = Get-CmFilterRel -Arch $Arch -PublishedVersion $PublishedVersion
    if ($rel -eq "xhcicmf.inf") {
        return @("xhcicm.inf", "xhcicmf.inf", "xhcicm.sys")
    }
    return @("xhcicm.inf", "nt\xhcicmf.inf", "xhcicm.sys", "nt\xhcicm.sys")
}

function Test-DriverVersionMatches {
    param(
        [string]$Reported,      # the binary's VersionInfo.FileVersion
        [string]$Declared       # the version half of the INF's DriverVer
    )

    if ($null -eq $Reported -or $null -eq $Declared) { return $false }
    $r = $Reported.Trim()
    $d = $Declared.Trim()
    if ($r -notmatch '^\d+\s*[.,]\s*\d+\s*[.,]\s*\d+\s*[.,]\s*\d+$') { return $false }
    if ($d -notmatch '^\d+(\.\d+){0,3}$') { return $false }

    $a = @($r -split '[.,\s]+' | Where-Object { $_ -ne "" })
    $b = @($d -split '\.')
    while ($b.Count -lt 4) { $b += "0" }
    for ($i = 0; $i -lt 4; $i++) {
        if ([int]$a[$i] -ne [int]$b[$i]) { return $false }
    }
    return $true
}

# ------------------------------------------------------------------
# The x64 test signature (design record 19; owner, 2026-10-11)
# ------------------------------------------------------------------
#
# Every published x64 directory carries xhci98.sys and xhciuas.sys with an
# embedded Authenticode signature by the project test certificate: SHA-1
# file digest, no timestamp; and since 3.2.0.0 (owner, 2026-10-22) one
# catalog per INF, made and signed the same way (Read-CatalogFile and
# Test-PackageCatalog below). make-package.ps1 -TestSignPfx
# applies it at staging and make-release.ps1 refuses an x64 cut without it.
# What follows is the part both scripts and test-package.ps1 share: reading
# a PE image's certificate table, deciding whether a signed image is exactly
# a given unsigned one plus a signature, and judging a parsed signature
# against the certificate the cut was given. The judgements are pure
# functions of their inputs so that test-package.ps1 can drive them with
# synthetic images and mock signatures - no key is ever needed to test them.

$script:TestSignSubject = "CN=xhci98 project test signing (not trusted)"
$script:TestSignCerName = "xhci98-test.cer"
$script:TestSignPasswordVariable = "XHCI98_TESTSIGN_PW"
$script:Sha1Oid = "1.3.14.3.2.26"
$script:SpcIndirectDataOid = "1.3.6.1.4.1.311.2.1.4"
$script:SpcPeImageDataOid = "1.3.6.1.4.1.311.2.1.15"
# The project test certificate's SHA-1 thumbprint (record 19 section 10):
# the one key a published x64 binary may be signed with. A new key is a
# decision recorded there first, and then this line.
$script:TestSignThumbprint = "A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF"

function Read-DerTlv {
    # One DER tag-length-value at $At: the tag, where the value starts and
    # where the element ends. DER only: definite lengths, in their minimal
    # form (a long form for a length under 128, or with a leading zero
    # byte, is refused).
    param([byte[]]$B, [int]$At, [int]$Limit)
    if ($At + 2 -gt $Limit) { throw "DER: element at $At runs past its container" }
    $tag = $B[$At]
    if (($tag -band 0x1F) -eq 0x1F) { throw "DER: multi-byte tag at $At" }
    $l = [int]$B[$At + 1]
    $p = $At + 2
    if ($l -band 0x80) {
        $n = $l -band 0x7F
        if ($n -lt 1 -or $n -gt 3 -or $p + $n -gt $Limit) { throw "DER: unsupported length form at $At" }
        if ($B[$p] -eq 0) { throw "DER: non-minimal length at $At" }
        $l = 0
        for ($i = 0; $i -lt $n; $i++) { $l = ($l -shl 8) -bor [int]$B[$p + $i] }
        if ($l -lt 0x80) { throw "DER: non-minimal length at $At" }
        $p += $n
    }
    if ($p + $l -gt $Limit) { throw "DER: element at $At runs past its container" }
    return @{ Tag = $tag; ValueAt = $p; Length = $l; End = $p + $l }
}

function Assert-SpcPeImageData {
    # The data value of a PE-image Authenticode signature, parsed against its
    # own schema rather than walked as generic DER:
    #   SpcPeImageData ::= SEQUENCE { flags BIT STRING OPTIONAL,
    #                                 file [0] EXPLICIT SpcLink OPTIONAL }
    #   SpcLink   ::= CHOICE { url [0] IMPLICIT IA5String,
    #                          moniker [1] IMPLICIT SEQUENCE { OCTET STRING, OCTET STRING },
    #                          file [2] EXPLICIT SpcString }
    #   SpcString ::= CHOICE { unicode [0] IMPLICIT BMPString, ascii [1] IMPLICIT IA5String }
    # Every element in its place, in its form, with DER contents (a BIT
    # STRING's unused bits 0 to 7 and zero, a BMPString of even length, IA5
    # bytes below 0x80), and nothing else anywhere.
    param([byte[]]$B, [int]$At, [int]$End)
    $seq = Read-DerTlv -B $B -At $At -Limit $End
    if ($seq.Tag -ne 0x30 -or $seq.End -ne $End) { throw "SpcPeImageData is not one SEQUENCE" }
    $p = $seq.ValueAt
    if ($p -lt $seq.End -and $B[$p] -eq 0x03) {
        $bits = Read-DerTlv -B $B -At $p -Limit $seq.End
        $unused = if ($bits.Length -ge 1) { [int]$B[$bits.ValueAt] } else { -1 }
        if ($bits.Length -lt 1 -or $unused -gt 7 -or ($bits.Length -eq 1 -and $unused -ne 0) -or
            ($bits.Length -gt 1 -and ([int]$B[$bits.End - 1] -band ((1 -shl $unused) - 1)) -ne 0)) {
            throw "SpcPeImageData.flags is not a DER BIT STRING"
        }
        $p = $bits.End
    }
    if ($p -lt $seq.End) {
        $file = Read-DerTlv -B $B -At $p -Limit $seq.End
        if ($file.Tag -ne 0xA0 -or $file.End -ne $seq.End) { throw "SpcPeImageData has something other than flags and [0] file" }
        $link = Read-DerTlv -B $B -At $file.ValueAt -Limit $file.End
        if ($link.End -ne $file.End) { throw "SpcPeImageData.file holds more than one SpcLink" }
        switch ([int]$link.Tag) {
            0x80 { Assert-Ia5 -B $B -At $link.ValueAt -End $link.End }
            0xA1 {
                $c1 = Read-DerTlv -B $B -At $link.ValueAt -Limit $link.End
                if ($c1.Tag -ne 0x04) { throw "SpcSerializedObject.classId is not an OCTET STRING" }
                $c2 = Read-DerTlv -B $B -At $c1.End -Limit $link.End
                if ($c2.Tag -ne 0x04 -or $c2.End -ne $link.End) { throw "SpcSerializedObject is not two OCTET STRINGs" }
            }
            0xA2 {
                $str = Read-DerTlv -B $B -At $link.ValueAt -Limit $link.End
                if ($str.End -ne $link.End) { throw "SpcLink.file holds more than one SpcString" }
                if ($str.Tag -eq 0x80) {
                    if ($str.Length % 2 -ne 0) { throw "SpcString.unicode is a BMPString of odd length" }
                } elseif ($str.Tag -eq 0x81) {
                    Assert-Ia5 -B $B -At $str.ValueAt -End $str.End
                } else {
                    throw "SpcLink.file is not an SpcString"
                }
            }
            default { throw ("SpcLink has the tag 0x{0:X2}, which is none of its three choices" -f $link.Tag) }
        }
    }
}

function Assert-Ia5 {
    param([byte[]]$B, [int]$At, [int]$End)
    for ($i = $At; $i -lt $End; $i++) { if ($B[$i] -ge 0x80) { throw "IA5String byte 0x{0:X2} is not ASCII" -f $B[$i] } }
}

function ConvertFrom-DerOid {
    # An OBJECT IDENTIFIER's value, decoded strictly: every subidentifier
    # terminated, minimally encoded (no leading 0x80 byte) and below 2^31,
    # the first split into the first two arcs per X.690 8.19.4.
    param([byte[]]$B, [int]$At, [int]$Length)
    if ($Length -lt 1) { throw "DER: empty OID" }
    $subs = New-Object System.Collections.ArrayList
    $v = [int64]0
    $open = $false
    for ($i = $At; $i -lt $At + $Length; $i++) {
        $byte = [int]$B[$i]
        if (-not $open -and $byte -eq 0x80) { throw "DER: non-minimal OID subidentifier" }
        $v = ($v -shl 7) -bor ($byte -band 0x7F)
        if ($v -gt 0x7FFFFFFF) { throw "DER: OID subidentifier too large" }
        if (($byte -band 0x80) -eq 0) { [void]$subs.Add($v); $v = 0; $open = $false } else { $open = $true }
    }
    if ($open) { throw "DER: unterminated OID subidentifier" }
    $first = [int64]$subs[0]
    $arcs = @()
    if ($first -lt 40) { $arcs = @(0, $first) } elseif ($first -lt 80) { $arcs = @(1, ($first - 40)) } else { $arcs = @(2, ($first - 80)) }
    for ($i = 1; $i -lt $subs.Count; $i++) { $arcs += $subs[$i] }
    return ($arcs -join ".")
}

function ConvertFrom-SpcIndirectData {
    # SpcIndirectDataContent ::= SEQUENCE { data SEQUENCE { type OID, value
    # ANY OPTIONAL }, messageDigest SEQUENCE { digestAlgorithm SEQUENCE { OID,
    # parameters NULL OPTIONAL }, digest OCTET STRING } }, parsed strictly:
    # every element where it belongs, the optional value an SpcPeImageData by
    # its schema, the parameters absent or exactly NULL, and nothing after
    # anything. Returns the data type's OID, the
    # digest algorithm's OID and the digest as hex.
    param([byte[]]$Content)
    if ($null -eq $Content -or $Content.Length -lt 2) { throw "no SpcIndirectDataContent" }
    $outer = Read-DerTlv -B $Content -At 0 -Limit $Content.Length
    if ($outer.Tag -ne 0x30 -or $outer.End -ne $Content.Length) { throw "SpcIndirectDataContent is not one SEQUENCE" }
    $data = Read-DerTlv -B $Content -At $outer.ValueAt -Limit $outer.End
    if ($data.Tag -ne 0x30) { throw "SpcIndirectDataContent.data is not a SEQUENCE" }
    $dataType = Read-DerTlv -B $Content -At $data.ValueAt -Limit $data.End
    if ($dataType.Tag -ne 0x06) { throw "SpcIndirectDataContent.data.type is not an OID" }
    $dataOid = ConvertFrom-DerOid -B $Content -At $dataType.ValueAt -Length $dataType.Length
    if ($dataType.End -lt $data.End) {
        $value = Read-DerTlv -B $Content -At $dataType.End -Limit $data.End
        if ($value.End -ne $data.End) { throw "SpcIndirectDataContent.data has more than a type and a value" }
        # A PE image's value by its schema. A catalog's flat-file member
        # (an INF) carries another type, whose value is read as one element
        # and no further; Test-ProjectTestSignature and Test-PackageCatalog
        # refuse whichever type does not belong where they read it.
        if ($dataOid -eq $script:SpcPeImageDataOid) {
            Assert-SpcPeImageData -B $Content -At $dataType.End -End $data.End
        }
    }
    $digestInfo = Read-DerTlv -B $Content -At $data.End -Limit $outer.End
    if ($digestInfo.Tag -ne 0x30 -or $digestInfo.End -ne $outer.End) { throw "SpcIndirectDataContent.messageDigest is not the last SEQUENCE" }
    $alg = Read-DerTlv -B $Content -At $digestInfo.ValueAt -Limit $digestInfo.End
    if ($alg.Tag -ne 0x30) { throw "DigestInfo.digestAlgorithm is not a SEQUENCE" }
    $algOid = Read-DerTlv -B $Content -At $alg.ValueAt -Limit $alg.End
    if ($algOid.Tag -ne 0x06) { throw "DigestInfo.digestAlgorithm.algorithm is not an OID" }
    if ($algOid.End -lt $alg.End) {
        $params = Read-DerTlv -B $Content -At $algOid.End -Limit $alg.End
        if ($params.Tag -ne 0x05 -or $params.Length -ne 0 -or $params.End -ne $alg.End) {
            throw "DigestInfo.digestAlgorithm.parameters are not absent or exactly NULL"
        }
    }
    $octets = Read-DerTlv -B $Content -At $alg.End -Limit $digestInfo.End
    if ($octets.Tag -ne 0x04 -or $octets.End -ne $digestInfo.End) { throw "DigestInfo.digest is not the last OCTET STRING" }
    $hex = ""
    if ($octets.Length -gt 0) {
        $hex = (($Content[$octets.ValueAt..($octets.End - 1)] | ForEach-Object { $_.ToString("X2") }) -join "")
    }
    return [pscustomobject]@{
        DataOid   = $dataOid
        DigestOid = ConvertFrom-DerOid -B $Content -At $algOid.ValueAt -Length $algOid.Length
        Digest    = $hex
    }
}

function Get-PeSecurityLayout {
    # Where a PE image keeps its checksum and its certificate-table directory
    # entry, and what that entry says. Throws on anything that is not a PE
    # image, so a stand-in text file is never mistaken for an unsigned driver.
    param([byte[]]$Bytes)

    if ($null -eq $Bytes -or $Bytes.Length -lt 0x40 -or $Bytes[0] -ne 0x4D -or $Bytes[1] -ne 0x5A) {
        throw "not a PE image (no MZ header)"
    }
    $pe = [BitConverter]::ToInt32($Bytes, 0x3C)
    if ($pe -lt 0x40 -or $pe + 24 + 2 -gt $Bytes.Length -or
        [BitConverter]::ToUInt32($Bytes, $pe) -ne 0x00004550) {
        throw "not a PE image (no PE signature at e_lfanew)"
    }
    $opt = $pe + 24
    $magic = [BitConverter]::ToUInt16($Bytes, $opt)
    $dirBase = switch ($magic) {
        0x10b { $opt + 96 }
        0x20b { $opt + 112 }
        default { throw ("not a PE image (optional header magic 0x{0:X})" -f $magic) }
    }
    $numDirs = [BitConverter]::ToUInt32($Bytes, $dirBase - 4)
    $secEntry = $dirBase + 4 * 8
    if ($numDirs -lt 5 -or $secEntry + 8 -gt $Bytes.Length) {
        throw "PE image has no certificate-table directory entry"
    }
    return [pscustomobject]@{
        ChecksumOffset = $opt + 64
        EntryOffset    = $secEntry
        TableOffset    = [BitConverter]::ToUInt32($Bytes, $secEntry)
        TableSize      = [BitConverter]::ToUInt32($Bytes, $secEntry + 4)
        Magic          = $magic
    }
}

function Test-SignedImageOf {
    # Is $Signed exactly $Unsigned with an embedded signature appended? That
    # is what signing changes and all it changes: the optional header's
    # checksum, the certificate-table entry, up to seven bytes of zero
    # padding to the table's 8-byte alignment, and the table itself at the
    # end of the file. Everything else must be byte-for-byte the build, which
    # is what lets make-release.ps1 keep tying the published bytes to the
    # source stamp beside the build output.
    param([byte[]]$Signed, [byte[]]$Unsigned)

    try {
        $u = Get-PeSecurityLayout -Bytes $Unsigned
        $s = Get-PeSecurityLayout -Bytes $Signed
    } catch {
        return $false
    }
    if ($u.TableOffset -ne 0 -or $u.TableSize -ne 0) { return $false }
    if ($s.TableOffset -eq 0 -or $s.TableSize -eq 0) { return $false }
    if ($s.ChecksumOffset -ne $u.ChecksumOffset -or $s.EntryOffset -ne $u.EntryOffset) { return $false }
    if ($s.TableOffset -lt $Unsigned.Length -or $s.TableOffset - $Unsigned.Length -gt 7) { return $false }
    if ($s.TableOffset % 8 -ne 0) { return $false }
    if ([int64]$s.TableOffset + [int64]$s.TableSize -ne [int64]$Signed.Length) { return $false }
    for ($i = 0; $i -lt $Unsigned.Length; $i++) {
        if ($i -ge $s.ChecksumOffset -and $i -lt $s.ChecksumOffset + 4) { continue }
        if ($i -ge $s.EntryOffset -and $i -lt $s.EntryOffset + 8) { continue }
        if ($Signed[$i] -ne $Unsigned[$i]) { return $false }
    }
    for ($i = $Unsigned.Length; $i -lt $s.TableOffset; $i++) {
        if ($Signed[$i] -ne 0) { return $false }
    }
    return $true
}

function Get-PeImageDigest {
    # A PE image's Authenticode SHA-1 digest, as hex: the whole file in
    # order, less the checksum, the certificate-table entry and the table
    # (which, where there is one, is the end of the file - the layout the
    # linker and SignTool write). It is the same for the image before and
    # after an embedded signature, which is why a catalog made of the signed
    # binary also covers the build, and the reverse. A hashed length that is
    # not a multiple of 8 is followed by zero bytes up to the next multiple,
    # as Windows' own image hash does (MakeCat's member digest of an x86
    # image with its debug data after the sections, 2026-10-22): signing
    # pads such an image with the same zeros before the table, so the
    # digest stays the same either way.
    # -End (Get-EmbeddedSignature's): where the hashed bytes stop, the
    # table's offset as that image's entry gives it, checked by the caller.
    param([byte[]]$Bytes, [int]$End = -1)
    # A certificate table starts on a multiple of 8 (the PE format's
    # attribute certificate table), so the padding is only ever added after
    # the last byte of an unsigned image: a table, or an -End, that is not
    # aligned is refused, or the padding would stand in for zeros missing
    # from the file before it.
    $layout = Get-PeSecurityLayout -Bytes $Bytes
    $stop = $Bytes.Length
    if ($End -ge 0) {
        if ($End % 8 -ne 0) {
            throw ("the certificate table's offset {0} is not a multiple of 8" -f $End)
        }
        $stop = $End
    } elseif ($layout.TableOffset -ne 0 -or $layout.TableSize -ne 0) {
        if ([int64]$layout.TableOffset + [int64]$layout.TableSize -ne [int64]$Bytes.Length) {
            throw "the certificate table is not the end of the image"
        }
        if ($layout.TableOffset % 8 -ne 0) {
            throw ("the certificate table's offset {0} is not a multiple of 8" -f $layout.TableOffset)
        }
        $stop = [int]$layout.TableOffset
    }
    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $ck = [int]$layout.ChecksumOffset
        $en = [int]$layout.EntryOffset
        [void]$sha1.TransformBlock($Bytes, 0, $ck, $null, 0)
        [void]$sha1.TransformBlock($Bytes, $ck + 4, $en - ($ck + 4), $null, 0)
        [void]$sha1.TransformBlock($Bytes, $en + 8, $stop - ($en + 8), $null, 0)
        $pad = (8 - ($stop % 8)) % 8
        [void]$sha1.TransformFinalBlock((New-Object byte[] $pad), 0, $pad)
        return (($sha1.Hash | ForEach-Object { $_.ToString("X2") }) -join "")
    } finally {
        $sha1.Dispose()
    }
}

function Get-EmbeddedSignature {
    # The embedded Authenticode signature of a PE image, parsed: $null when
    # the image carries none, otherwise the signer's thumbprint and subject,
    # the digest algorithm, the digest the signature vouches for, the SHA-1
    # digest of the image as it is now, and whether the PKCS #7 signature
    # verifies over its own content. No chain is built: the project
    # certificate is self-signed and trusted nowhere by design (record 19),
    # so a chain verdict would say nothing this does not.
    #
    # The image digest is taken over the whole file in order, less the
    # checksum, the certificate-table entry and the table - Authenticode's
    # hash for an image whose sections lie in file order with nothing after
    # them but the table, which is what the linker writes. An image laid out
    # otherwise reads as a digest mismatch and is refused, never passed.
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $layout = Get-PeSecurityLayout -Bytes $bytes
    if ($layout.TableOffset -eq 0 -and $layout.TableSize -eq 0) { return $null }
    if ([int64]$layout.TableOffset + [int64]$layout.TableSize -gt [int64]$bytes.Length -or $layout.TableSize -lt 8) {
        throw "the certificate table runs past the end of the image"
    }
    $off = [int]$layout.TableOffset
    $wlen = [BitConverter]::ToInt32($bytes, $off)
    $wrev = [BitConverter]::ToUInt16($bytes, $off + 4)
    $wtype = [BitConverter]::ToUInt16($bytes, $off + 6)
    if ($wlen -lt 8 -or $wlen -gt $layout.TableSize -or $wrev -ne 0x0200 -or $wtype -ne 0x0002) {
        throw ("the certificate table is not one PKCS #7 WIN_CERTIFICATE (length {0}, revision 0x{1:X4}, type 0x{2:X4})" -f $wlen, $wrev, $wtype)
    }
    $pkcs7 = New-Object byte[] ($wlen - 8)
    [Array]::Copy($bytes, $off + 8, $pkcs7, 0, $wlen - 8)

    Add-Type -AssemblyName System.Security
    $cms = New-Object System.Security.Cryptography.Pkcs.SignedCms
    $cms.Decode($pkcs7)
    if ($cms.SignerInfos.Count -ne 1) {
        throw ("the signature has {0} signers, not one" -f $cms.SignerInfos.Count)
    }
    $signer = $cms.SignerInfos[0]
    $valid = $true
    try { $cms.CheckSignature($true) } catch { $valid = $false }

    # What the signature vouches for is the SpcIndirectDataContent it signs:
    # parsed in full, so that only an Authenticode PE-image signature, with
    # its own digest algorithm and digest, can pass the verdict - not any
    # CMS message by the right certificate that happens to end in the right
    # bytes.
    $indirect = ConvertFrom-SpcIndirectData -Content $cms.ContentInfo.Content

    $actual = Get-PeImageDigest -Bytes $bytes -End $off

    return [pscustomobject]@{
        Thumbprint     = $(if ($null -ne $signer.Certificate) { $signer.Certificate.Thumbprint } else { "" })
        Subject        = $(if ($null -ne $signer.Certificate) { $signer.Certificate.Subject } else { "" })
        DigestOid      = $signer.DigestAlgorithm.Value
        ContentType    = $cms.ContentInfo.ContentType.Value
        DataOid        = $indirect.DataOid
        IndirectDigestOid = $indirect.DigestOid
        VouchedDigest  = $indirect.Digest
        ImageDigest    = $actual
        SignatureValid = $valid
        Timestamped    = ($signer.UnsignedAttributes.Count -gt 0)
    }
}

function Test-ProjectTestSignature {
    # The verdict on a parsed signature (Get-EmbeddedSignature's object, or a
    # mock of one): "" when it is the signature record 19 prescribes, by the
    # certificate whose thumbprint is given, otherwise the reason it is not.
    param($Signature, [string]$Thumbprint)

    if ($null -eq $Signature) { return "it carries no embedded signature" }
    if ($Signature.Thumbprint -ne $Thumbprint) {
        return ("it is signed by certificate {0}, not by the project test certificate {1}" -f $Signature.Thumbprint, $Thumbprint)
    }
    if ($Signature.ContentType -ne $script:SpcIndirectDataOid -or $Signature.DataOid -ne $script:SpcPeImageDataOid) {
        return ("it is not an Authenticode PE-image signature (content {0}, data {1})" -f $Signature.ContentType, $Signature.DataOid)
    }
    if ($Signature.DigestOid -ne $script:Sha1Oid -or $Signature.IndirectDigestOid -ne $script:Sha1Oid) {
        return ("its digests are {0} (signer) and {1} (file), not SHA-1 ({2}), which Vista x64 cannot verify" -f `
            $Signature.DigestOid, $Signature.IndirectDigestOid, $script:Sha1Oid)
    }
    if (-not $Signature.SignatureValid) { return "its PKCS #7 signature does not verify" }
    if ($Signature.VouchedDigest -eq "" -or $Signature.VouchedDigest -ne $Signature.ImageDigest) {
        return ("the digest it vouches for ({0}) is not the image's ({1})" -f $Signature.VouchedDigest, $Signature.ImageDigest)
    }
    if ($Signature.Timestamped) { return "it carries a countersignature (timestamp), which record 19 does not take" }
    return ""
}

# ------------------------------------------------------------------
# The x64 catalogs (design record 19 section 3a; owner, 2026-10-22)
# ------------------------------------------------------------------
#
# Windows 8, 8.1 and 10 x64 refuse a driver package with no catalog in
# test-signing mode, before any prompt (record 13 section 6.14.5: "Driver
# package does not contain a catalog file, and Code Integrity is in Test
# Signing mode", 0xE000022F). So every INF of a signed amd64 package names a
# catalog of its own (CatalogFile=, held by the INF gates' BOTH-CATALOG), and
# make-package.ps1 makes each one with WDK 7.1's MakeCat from the staged
# files and signs it with the key that signs the binaries: SHA-1, no
# timestamp. A catalog lists each member by its SHA-1 digest - an INF's over
# the whole file, a driver's the Authenticode image digest, which the
# embedded signature does not change - so the staged bytes are what it
# vouches for. The x86 package carries none.
#
# Which files each catalog covers: the INF and every file its
# [SourceDisksFiles] names, which is exactly this table (make-package.ps1
# refuses a staged INF that names a file outside it). One table, read by the
# packager, make-release.ps1 and test-package.ps1.
$script:PackageCatalogs = [ordered]@{
    "xhci98.cat"  = @{ Inf = "xhci98.inf";  Files = @("xhci98.inf", "xhci98.sys") }
    "xhciuas.cat" = @{ Inf = "xhciuas.inf"; Files = @("xhciuas.inf", "xhciuas.sys") }
    "xhcicm.cat"  = @{ Inf = "xhcicm.inf";  Files = @("xhcicm.inf", "xhcicm.sys") }
    "xhcicmf.cat" = @{ Inf = "xhcicmf.inf"; Files = @("xhcicmf.inf", "xhcicm.sys") }
    "stor98.cat"  = @{ Inf = "stor98.inf";  Files = @("stor98.inf", "stor98.sys") }
}
# The systems the catalogs are made for, as the catalog's OSAttr attribute
# (major version 2 = NT; 5.2 XP x64 and Server 2003 x64, 6.0 Vista, 6.1 7,
# 6.2 8, 6.3 8.1, 10.0 Windows 10), the form Inf2Cat writes.
$script:CatalogOsAttr = "2:5.2,2:6.0,2:6.1,2:6.2,2:6.3,2:10.0"
$script:CtlOid = "1.3.6.1.4.1.311.10.1"
$script:CatalogListOid = "1.3.6.1.4.1.311.12.1.1"
$script:CatalogMemberOid = "1.3.6.1.4.1.311.12.1.2"
$script:CatNameValueOid = "1.3.6.1.4.1.311.12.2.1"
$script:CatMemberInfoOid = "1.3.6.1.4.1.311.12.2.2"
# What MakeCat writes for each member (read 2026-10-22 from a catalog it made
# of the 3.1.0.0 release-x64 xhci98.inf and xhci98.sys): the indirect data
# type and the subject interface package's GUID in the member info, for a
# flat file (an INF) and for a PE image, and the member info's version.
$script:CatFlatDataOid = "1.3.6.1.4.1.311.2.1.25"
$script:CatFlatSipGuid = "{DE351A42-8E59-11D0-8C47-00C04FC295EE}"
$script:CatPeSipGuid = "{C689AAB8-8E78-11D0-8C47-00C04FC295EE}"
$script:CatMemberInfoVersion = 512

function ConvertFrom-CatNameValue {
    # CAT_NAMEVALUE ::= SEQUENCE { tag BMPString, flags INTEGER, value
    # OCTET STRING (UTF-16LE, NUL-terminated) }: the name and the value.
    param([byte[]]$B, [int]$At, [int]$End)
    $seq = Read-DerTlv -B $B -At $At -Limit $End
    if ($seq.Tag -ne 0x30) { throw "a catalog name-value is not a SEQUENCE" }
    $name = Read-DerTlv -B $B -At $seq.ValueAt -Limit $seq.End
    if ($name.Tag -ne 0x1E -or $name.Length % 2 -ne 0) { throw "a catalog name-value's name is not a BMPString" }
    $flags = Read-DerTlv -B $B -At $name.End -Limit $seq.End
    # The flags this project's CDF writes for every name-value,
    # 0x10010001 (authenticated, name and value ASCII), and nothing else.
    if ($flags.Tag -ne 0x02 -or $flags.Length -ne 4 -or $B[$flags.ValueAt] -ne 0x10 -or $B[$flags.ValueAt + 1] -ne 0x01 -or
        $B[$flags.ValueAt + 2] -ne 0x00 -or $B[$flags.ValueAt + 3] -ne 0x01) {
        throw "a catalog name-value's flags are not the INTEGER 0x10010001"
    }
    $val = Read-DerTlv -B $B -At $flags.End -Limit $seq.End
    if ($val.Tag -ne 0x04 -or $val.End -ne $seq.End) { throw "a catalog name-value's value is not its last OCTET STRING" }
    if ($seq.End -ne $End) { throw "a catalog name-value is followed by something else" }
    $nameText = [System.Text.Encoding]::BigEndianUnicode.GetString($B, $name.ValueAt, $name.Length)
    $valText = [System.Text.Encoding]::Unicode.GetString($B, $val.ValueAt, $val.Length).TrimEnd([char]0)
    return @{ Name = $nameText; Value = $valText }
}

function ConvertFrom-CatMemberInfo {
    # CAT_MEMBERINFO ::= SEQUENCE { subjGuid BMPString, certVersion INTEGER }:
    # the subject interface package's GUID and the version.
    param([byte[]]$B, [int]$At, [int]$End)
    $seq = Read-DerTlv -B $B -At $At -Limit $End
    if ($seq.Tag -ne 0x30 -or $seq.End -ne $End) { throw "a catalog member info is not one SEQUENCE" }
    $guid = Read-DerTlv -B $B -At $seq.ValueAt -Limit $seq.End
    if ($guid.Tag -ne 0x1E -or $guid.Length % 2 -ne 0) { throw "a catalog member info's subject is not a BMPString" }
    $ver = Read-DerTlv -B $B -At $guid.End -Limit $seq.End
    if ($ver.Tag -ne 0x02 -or $ver.End -ne $seq.End -or $ver.Length -lt 1 -or $ver.Length -gt 3) { throw "a catalog member info's version is not its last small INTEGER" }
    $v = 0
    for ($i = $ver.ValueAt; $i -lt $ver.End; $i++) { $v = ($v -shl 8) -bor [int]$B[$i] }
    return @{ Guid = [System.Text.Encoding]::BigEndianUnicode.GetString($B, $guid.ValueAt, $guid.Length); Version = $v }
}

function Read-CatalogFile {
    # A catalog file, parsed: its signer (as Get-EmbeddedSignature reports
    # one: thumbprint, digest algorithm, whether the PKCS #7 signature
    # verifies over its content, whether it is timestamped; empty when the
    # catalog is unsigned), the certificate trust list's usage and member
    # algorithm, its OSAttr, and each member: its tag, its File name-value,
    # its indirect data type and its digest. The structure is read strictly
    # where this project depends on it and no further. No chain is built,
    # for the reason Get-EmbeddedSignature gives.
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    Add-Type -AssemblyName System.Security
    $cms = New-Object System.Security.Cryptography.Pkcs.SignedCms
    $cms.Decode($bytes)
    $signerCount = $cms.SignerInfos.Count
    $thumb = ""; $subject = ""; $digestOid = ""; $valid = $false; $stamped = $false
    if ($signerCount -ge 1) {
        $signer = $cms.SignerInfos[0]
        if ($null -ne $signer.Certificate) { $thumb = $signer.Certificate.Thumbprint; $subject = $signer.Certificate.Subject }
        $digestOid = $signer.DigestAlgorithm.Value
        $valid = $true
        try { $cms.CheckSignature($true) } catch { $valid = $false }
        $stamped = ($signer.UnsignedAttributes.Count -gt 0)
    }
    $contentType = $cms.ContentInfo.ContentType.Value
    if ($contentType -ne $script:CtlOid) {
        throw ("the catalog's content is {0}, not a certificate trust list ({1})" -f $contentType, $script:CtlOid)
    }
    $list = ConvertFrom-CatalogTrustList -Content $cms.ContentInfo.Content
    return [pscustomobject]@{
        SignerCount    = $signerCount
        Thumbprint     = $thumb
        Subject        = $subject
        DigestOid      = $digestOid
        SignatureValid = $valid
        Timestamped    = $stamped
        ContentType    = $contentType
        Usage          = $list.Usage
        MemberAlgOid   = $list.MemberAlgOid
        OsAttr         = $list.OsAttr
        Members        = $list.Members
    }
}

function ConvertFrom-CatalogTrustList {
    # A catalog's certificate trust list (the signed content), parsed: its
    # usage, member algorithm, OSAttr and members, as Read-CatalogFile
    # reports them. Separate so test-package.ps1 can drive it with encoded
    # lists that no signer has signed.
    param([byte[]]$Content)
    $c = $Content
    # CertificateTrustList ::= SEQUENCE { version INTEGER OPTIONAL,
    #   subjectUsage SEQUENCE OF OID, listIdentifier OCTET STRING OPTIONAL,
    #   sequenceNumber INTEGER OPTIONAL, thisUpdate Time, nextUpdate Time
    #   OPTIONAL, subjectAlgorithm AlgorithmIdentifier, trustedSubjects
    #   SEQUENCE OF TrustedSubject OPTIONAL, extensions [0] OPTIONAL }
    $ctl = Read-DerTlv -B $c -At 0 -Limit $c.Length
    if ($ctl.Tag -ne 0x30 -or $ctl.End -ne $c.Length) { throw "the catalog's trust list is not one SEQUENCE" }
    $p = $ctl.ValueAt
    $el = Read-DerTlv -B $c -At $p -Limit $ctl.End
    if ($el.Tag -eq 0x02) { $p = $el.End; $el = Read-DerTlv -B $c -At $p -Limit $ctl.End }
    if ($el.Tag -ne 0x30) { throw "the catalog's trust list has no subjectUsage" }
    $usage = @()
    $q = $el.ValueAt
    while ($q -lt $el.End) {
        $o = Read-DerTlv -B $c -At $q -Limit $el.End
        if ($o.Tag -ne 0x06) { throw "the catalog's subjectUsage holds something other than an OID" }
        $usage += ConvertFrom-DerOid -B $c -At $o.ValueAt -Length $o.Length
        $q = $o.End
    }
    $p = $el.End
    $el = Read-DerTlv -B $c -At $p -Limit $ctl.End
    if ($el.Tag -eq 0x04) { $p = $el.End; $el = Read-DerTlv -B $c -At $p -Limit $ctl.End }
    if ($el.Tag -eq 0x02) { $p = $el.End; $el = Read-DerTlv -B $c -At $p -Limit $ctl.End }
    if ($el.Tag -ne 0x17 -and $el.Tag -ne 0x18) { throw "the catalog's trust list has no thisUpdate" }
    $p = $el.End
    $el = Read-DerTlv -B $c -At $p -Limit $ctl.End
    if ($el.Tag -eq 0x17 -or $el.Tag -eq 0x18) { $p = $el.End; $el = Read-DerTlv -B $c -At $p -Limit $ctl.End }
    if ($el.Tag -ne 0x30) { throw "the catalog's trust list has no subjectAlgorithm" }
    $algOid = Read-DerTlv -B $c -At $el.ValueAt -Limit $el.End
    if ($algOid.Tag -ne 0x06) { throw "the catalog's subjectAlgorithm is not an OID" }
    $memberAlg = ConvertFrom-DerOid -B $c -At $algOid.ValueAt -Length $algOid.Length
    $p = $el.End

    $members = New-Object System.Collections.ArrayList
    $osAttr = New-Object System.Collections.ArrayList
    if ($p -lt $ctl.End -and $c[$p] -eq 0x30) {
        $subjects = Read-DerTlv -B $c -At $p -Limit $ctl.End
        $q = $subjects.ValueAt
        while ($q -lt $subjects.End) {
            $ts = Read-DerTlv -B $c -At $q -Limit $subjects.End
            if ($ts.Tag -ne 0x30) { throw "a catalog member is not a SEQUENCE" }
            $id = Read-DerTlv -B $c -At $ts.ValueAt -Limit $ts.End
            if ($id.Tag -ne 0x04 -or $id.Length % 2 -ne 0) { throw "a catalog member's identifier is not a UTF-16 OCTET STRING" }
            $tag = [System.Text.Encoding]::Unicode.GetString($c, $id.ValueAt, $id.Length).TrimEnd([char]0)
            $m = [pscustomobject]@{ Tag = $tag; File = ""; DataOid = ""; DigestOid = ""; Digest = ""; Indirect = 0; Names = 0;
                                    SipGuid = ""; InfoVersion = -1; Infos = 0 }
            if ($id.End -lt $ts.End) {
                $attrs = Read-DerTlv -B $c -At $id.End -Limit $ts.End
                if ($attrs.Tag -ne 0x31 -or $attrs.End -ne $ts.End) { throw "catalog member '$tag' has something other than one attribute SET after its identifier" }
                $r = $attrs.ValueAt
                while ($r -lt $attrs.End) {
                    $a = Read-DerTlv -B $c -At $r -Limit $attrs.End
                    if ($a.Tag -ne 0x30) { throw "catalog member '$tag' has an attribute that is not a SEQUENCE" }
                    $aOid = Read-DerTlv -B $c -At $a.ValueAt -Limit $a.End
                    if ($aOid.Tag -ne 0x06) { throw "catalog member '$tag' has an attribute with no OID" }
                    $aType = ConvertFrom-DerOid -B $c -At $aOid.ValueAt -Length $aOid.Length
                    $vals = Read-DerTlv -B $c -At $aOid.End -Limit $a.End
                    if ($vals.Tag -ne 0x31 -or $vals.End -ne $a.End) { throw "catalog member '$tag' has an attribute with no value SET" }
                    $first = Read-DerTlv -B $c -At $vals.ValueAt -Limit $vals.End
                    if ($first.End -ne $vals.End) { throw "catalog member '$tag' has an attribute with more than one value" }
                    if ($aType -eq $script:SpcIndirectDataOid) {
                        $sub = New-Object byte[] ($first.End - $vals.ValueAt)
                        [Array]::Copy($c, $vals.ValueAt, $sub, 0, $sub.Length)
                        $ind = ConvertFrom-SpcIndirectData -Content $sub
                        $m.DataOid = $ind.DataOid; $m.DigestOid = $ind.DigestOid; $m.Digest = $ind.Digest
                        $m.Indirect++
                    } elseif ($aType -eq $script:CatNameValueOid) {
                        $nv = ConvertFrom-CatNameValue -B $c -At $vals.ValueAt -End $vals.End
                        if ($nv.Name -eq "File") { $m.File = $nv.Value; $m.Names++ }
                    } elseif ($aType -eq $script:CatMemberInfoOid) {
                        $mi = ConvertFrom-CatMemberInfo -B $c -At $vals.ValueAt -End $vals.End
                        $m.SipGuid = $mi.Guid; $m.InfoVersion = $mi.Version; $m.Infos++
                    }
                    $r = $a.End
                }
            }
            [void]$members.Add($m)
            $q = $ts.End
        }
        $p = $subjects.End
    }
    if ($p -lt $ctl.End) {
        # extensions [0] EXPLICIT SEQUENCE OF Extension; the catalog's own
        # name-values (OSAttr) are extensions of the name-value OID.
        $ext = Read-DerTlv -B $c -At $p -Limit $ctl.End
        if ($ext.Tag -ne 0xA0 -or $ext.End -ne $ctl.End) { throw "the catalog's trust list has something other than extensions after its members" }
        $exts = Read-DerTlv -B $c -At $ext.ValueAt -Limit $ext.End
        if ($exts.Tag -ne 0x30 -or $exts.End -ne $ext.End) { throw "the catalog's extensions are not one SEQUENCE filling their [0]" }
        $q = $exts.ValueAt
        while ($q -lt $exts.End) {
            # Extension ::= SEQUENCE { extnID OID, critical BOOLEAN DEFAULT
            # FALSE, extnValue OCTET STRING }, consumed exactly.
            $x = Read-DerTlv -B $c -At $q -Limit $exts.End
            if ($x.Tag -ne 0x30) { throw "a catalog extension is not a SEQUENCE" }
            $xOid = Read-DerTlv -B $c -At $x.ValueAt -Limit $x.End
            if ($xOid.Tag -ne 0x06) { throw "a catalog extension has no OID" }
            $xVal = Read-DerTlv -B $c -At $xOid.End -Limit $x.End
            if ($xVal.Tag -eq 0x01) {
                if ($xVal.Length -ne 1 -or $c[$xVal.ValueAt] -ne 0xFF) {
                    throw "a catalog extension's critical flag is not DER (one byte, TRUE; FALSE is the default and is not encoded)"
                }
                $xVal = Read-DerTlv -B $c -At $xVal.End -Limit $x.End
            }
            if ($xVal.Tag -ne 0x04 -or $xVal.End -ne $x.End) { throw "a catalog extension's value is not its last OCTET STRING" }
            if ((ConvertFrom-DerOid -B $c -At $xOid.ValueAt -Length $xOid.Length) -eq $script:CatNameValueOid) {
                $nv = ConvertFrom-CatNameValue -B $c -At $xVal.ValueAt -End $xVal.End
                if ($nv.Name -eq "OSAttr") { [void]$osAttr.Add($nv.Value) }
            }
            $q = $x.End
        }
    }
    return @{ Usage = $usage; MemberAlgOid = $memberAlg; OsAttr = @($osAttr); Members = @($members) }
}

function New-PackageCatalogFile {
    # Makes $CatName, unsigned, for the files $Files of $StageDir with WDK
    # 7.1's MakeCat in a folder of its own under $WorkDir, and returns its
    # path: each member tagged by its digest, with its File name-value, and
    # the catalog's OSAttr. make-package.ps1's Invoke-TestSignCatalog signs
    # what this makes; test-package.ps1 reads it unsigned. Every failure
    # throws.
    param([string]$StageDir, [string]$CatName, [string[]]$Files, [string]$WorkDir, [string]$MakeCat)
    $catDir = Join-Path $WorkDir ([System.IO.Path]::GetFileNameWithoutExtension($CatName) + "-cat")
    if (-not (Test-Path -LiteralPath $catDir)) { New-Item -ItemType Directory -Path $catDir | Out-Null }
    $cdf = New-Object System.Collections.ArrayList
    [void]$cdf.Add("[CatalogHeader]")
    [void]$cdf.Add("Name=$CatName")
    [void]$cdf.Add("PublicVersion=0x0000001")
    [void]$cdf.Add("EncodingType=0x00010001")
    [void]$cdf.Add("CATATTR1=0x10010001:OSAttr:$($script:CatalogOsAttr)")
    [void]$cdf.Add("[CatalogFiles]")
    foreach ($f in $Files) {
        $fp = Join-Path $StageDir $f
        if (-not (Test-Path -LiteralPath $fp -PathType Leaf)) {
            throw "$CatName covers '$f', which the package does not hold."
        }
        # MakeCat reads the copy in its work folder, by a name relative to
        # it, so no path - one with a space, an equals sign or a character
        # the ASCII CDF cannot carry - ever reaches the CDF.
        Copy-Item -LiteralPath $fp -Destination (Join-Path $catDir $f) -Force
        [void]$cdf.Add("<HASH>$f=$f")
        [void]$cdf.Add("<HASH>${f}ATTR1=0x10010001:File:$f")
    }
    $cdfPath = Join-Path $catDir "catalog.cdf"
    [System.IO.File]::WriteAllLines($cdfPath, [string[]]$cdf, (New-Object System.Text.ASCIIEncoding))
    $catPath = Join-Path $catDir $CatName
    if (Test-Path -LiteralPath $catPath) { Remove-Item -LiteralPath $catPath -Force }
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Push-Location -LiteralPath $catDir
    $savedCwd = [System.IO.Directory]::GetCurrentDirectory()
    try {
        # The process's own directory too, which a native child inherits.
        [System.IO.Directory]::SetCurrentDirectory($catDir)
        $mkOut = & $MakeCat "catalog.cdf" 2>&1 | Out-String
        $mkRc = $LASTEXITCODE
    } finally {
        [System.IO.Directory]::SetCurrentDirectory($savedCwd)
        Pop-Location
        $ErrorActionPreference = $savedEap
    }
    if ($mkRc -ne 0 -or -not (Test-Path -LiteralPath $catPath)) {
        throw "MakeCat could not make $CatName (exit $mkRc):`n$mkOut"
    }
    return $catPath
}

function Get-CatalogMemberDigest {
    # What a catalog member's digest must be for the file at $Path: the
    # Authenticode image digest for a PE image, SHA-1 over the file for any
    # other (an INF, or the self-tests' text stand-ins). Returns the digest
    # and whether the file is a PE image.
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $isPe = $true
    try { [void](Get-PeSecurityLayout -Bytes $bytes) } catch { $isPe = $false }
    if ($isPe) { return @{ Digest = (Get-PeImageDigest -Bytes $bytes); Pe = $true } }
    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try { return @{ Digest = (($sha1.ComputeHash($bytes) | ForEach-Object { $_.ToString("X2") }) -join ""); Pe = $false } }
    finally { $sha1.Dispose() }
}

function Test-PackageCatalog {
    # The verdict on a parsed catalog (Read-CatalogFile's object, or a mock
    # of one): "" when it is the catalog section 3a of record 19 prescribes
    # for the files given - signed once by the certificate whose thumbprint
    # is given, SHA-1, verifying, no timestamp, a catalog list of SHA-1
    # members carrying $script:CatalogOsAttr, and exactly one member per
    # file, by name, with that file's digest (Get-CatalogMemberDigest) as its
    # tag and its indirect data, of the PE type for a PE image and of another
    # for any other file, and no member more - otherwise the reason it is not.
    # $Expected maps each file name to Get-CatalogMemberDigest's result.
    param($Catalog, [hashtable]$Expected, [string]$Thumbprint)

    if ($null -eq $Catalog) { return "there is no catalog" }
    if ($Catalog.SignerCount -eq 0) { return "the catalog is not signed" }
    if ($Catalog.SignerCount -ne 1) { return ("the catalog has {0} signers, not one" -f $Catalog.SignerCount) }
    if ($Catalog.Thumbprint -ne $Thumbprint) {
        return ("the catalog is signed by certificate {0}, not by the project test certificate {1}" -f $Catalog.Thumbprint, $Thumbprint)
    }
    if ($Catalog.DigestOid -ne $script:Sha1Oid) { return ("the catalog's signature digest is {0}, not SHA-1 ({1})" -f $Catalog.DigestOid, $script:Sha1Oid) }
    if (-not $Catalog.SignatureValid) { return "the catalog's PKCS #7 signature does not verify" }
    if ($Catalog.Timestamped) { return "the catalog carries a countersignature (timestamp), which record 19 does not take" }
    if ($Catalog.ContentType -ne $script:CtlOid -or @($Catalog.Usage) -notcontains $script:CatalogListOid -or
        $Catalog.MemberAlgOid -ne $script:CatalogMemberOid) {
        return "it is not a catalog list (content, usage or member algorithm)"
    }
    if (@($Catalog.OsAttr).Count -ne 1 -or @($Catalog.OsAttr)[0] -ne $script:CatalogOsAttr) {
        return ("its OSAttr is '{0}', not '{1}'" -f (@($Catalog.OsAttr) -join ' | '), $script:CatalogOsAttr)
    }
    $seen = @{}
    foreach ($m in @($Catalog.Members)) {
        $name = [string]$m.File
        if ($name -eq "" -or $m.Names -ne 1) { return ("member '{0}' does not carry one File name" -f $m.Tag) }
        $key = $name.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { return "it lists '$name' twice" }
        $seen[$key] = $true
        if (-not $Expected.ContainsKey($name)) { return "it lists '$name', which is not one of its package files" }
        $want = $Expected[$name]
        if ($m.Indirect -ne 1) { return "member '$name' does not carry one indirect data attribute" }
        if ($m.DigestOid -ne $script:Sha1Oid) { return ("member '{0}' is hashed with {1}, not SHA-1" -f $name, $m.DigestOid) }
        if ($m.Digest -ne $want.Digest) { return ("member '{0}' vouches for digest {1}, and the file's is {2}" -f $name, $m.Digest, $want.Digest) }
        if ($m.Tag -ne $want.Digest) { return ("member '{0}' is tagged '{1}', not by its digest" -f $name, $m.Tag) }
        $wantOid = if ($want.Pe) { $script:SpcPeImageDataOid } else { $script:CatFlatDataOid }
        $wantSip = if ($want.Pe) { $script:CatPeSipGuid } else { $script:CatFlatSipGuid }
        if ($m.DataOid -ne $wantOid) {
            return ("member '{0}' has the indirect data type {1}, not {2}, the one for a{3} PE image" -f $name, $m.DataOid, $wantOid, $(if ($want.Pe) { "" } else { " file that is not a" }))
        }
        if ($m.Infos -ne 1 -or $m.SipGuid -ne $wantSip -or $m.InfoVersion -ne $script:CatMemberInfoVersion) {
            return ("member '{0}' does not carry one member info naming the subject interface package {1}, version {2} (it carries {3}: '{4}', {5})" -f `
                $name, $wantSip, $script:CatMemberInfoVersion, $m.Infos, $m.SipGuid, $m.InfoVersion)
        }
    }
    foreach ($name in $Expected.Keys) {
        if (-not $seen.ContainsKey($name.ToLowerInvariant())) { return "it does not list '$name'" }
    }
    return ""
}

function Test-CatalogOfPackage {
    # Every catalog of $script:PackageCatalogs whose INF is in $Dir, read
    # and judged against the files beside it: "" when each is present and
    # passes Test-PackageCatalog, otherwise the first reason one does not.
    # make-package.ps1 runs it on the staged package and make-release.ps1
    # on the published directory.
    param([string]$Dir, [string]$Thumbprint)
    foreach ($catName in $script:PackageCatalogs.Keys) {
        $spec = $script:PackageCatalogs[$catName]
        if (-not (Test-Path -LiteralPath (Join-Path $Dir $spec.Inf))) { continue }
        $catPath = Join-Path $Dir $catName
        if (-not (Test-Path -LiteralPath $catPath)) { return "$($spec.Inf) has no $catName beside it" }
        $expected = @{}
        foreach ($f in $spec.Files) {
            $fp = Join-Path $Dir $f
            if (-not (Test-Path -LiteralPath $fp)) { return "$catName covers '$f', which is not in the package" }
            try { $expected[$f] = Get-CatalogMemberDigest -Path $fp }
            catch { return "${catName}: '$f' cannot be hashed: $($_.Exception.Message)" }
        }
        $why = ""
        try { $why = Test-PackageCatalog -Catalog (Read-CatalogFile -Path $catPath) -Expected $expected -Thumbprint $Thumbprint }
        catch { $why = "it cannot be read: $($_.Exception.Message)" }
        if ($why -ne "") { return "${catName}: $why" }
    }
    return ""
}

function Get-TestSignPassword {
    # The .pfx password, from XHCI98_TESTSIGN_PW or, where that is unset, a
    # prompt that does not echo. Never printed and never written anywhere;
    # make-release.ps1 hands it to its make-package.ps1 children through the
    # same variable, in its own process environment only.
    $pw = [Environment]::GetEnvironmentVariable($script:TestSignPasswordVariable, "Process")
    if ($null -ne $pw) { return $pw }
    $secure = Read-Host -AsSecureString "Password of the test-signing .pfx (input hidden)"
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

function Get-TestSignCertificate {
    # Opens the .pfx with its password and returns what the scripts need from
    # it: the thumbprint and the public certificate's DER bytes (the
    # xhci98-test.cer the x64 directories carry). The private key is loaded
    # only to prove it is there, as an ephemeral key, and released at once.
    param([string]$PfxPath, [string]$Password)

    if (-not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) {
        throw "no test-signing key at '$PfxPath'."
    }
    # EphemeralKeySet keeps the key out of every key store; it exists from
    # .NET Framework 4.7.2. Refused rather than replaced by a flag that
    # would persist the private key in the user's profile.
    if (-not [Enum]::IsDefined([System.Security.Cryptography.X509Certificates.X509KeyStorageFlags], "EphemeralKeySet")) {
        throw "opening the test-signing key needs .NET Framework 4.7.2 or later (X509KeyStorageFlags.EphemeralKeySet), which this host lacks."
    }
    $flags = [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet
    try {
        $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($PfxPath, $Password, $flags)
    } catch {
        throw "the test-signing key '$PfxPath' cannot be opened with the password given (XHCI98_TESTSIGN_PW or the prompt): $($_.Exception.Message)"
    }
    try {
        if (-not $cert.HasPrivateKey) {
            throw "'$PfxPath' holds no private key, so nothing can be signed with it."
        }
        if ($cert.Subject -ne $script:TestSignSubject) {
            throw "'$PfxPath' is the certificate '$($cert.Subject)', not the project test certificate '$($script:TestSignSubject)' (design record 19)."
        }
        if ($cert.Thumbprint -ne $script:TestSignThumbprint) {
            throw "'$PfxPath' holds the certificate $($cert.Thumbprint), not the project test certificate $($script:TestSignThumbprint) that design record 19 section 10 records. A new key is recorded there before it signs anything."
        }
        return [pscustomobject]@{
            Thumbprint = $cert.Thumbprint
            Subject    = $cert.Subject
            CerBytes   = $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
        }
    } finally {
        $cert.Reset()
    }
}

# --- the port switchover's two batch files (roadmap-hcd task 36.56) ------
#
# portsw-on.bat and portsw-off.bat, published in tools\portsw\, are run by
# COMMAND.COM on Windows 98 SE and ME and by cmd.exe elsewhere (design
# record 16 section 5a). Every line is held to a short list of the commands
# both interpreters run, rather than searched for constructs one of them
# lacks: a list of forbidden words misses the one nobody listed (four such
# escapes were found that way). The only lines outside that list are the
# split's: the OS test, and the cd /d that only cmd.exe reaches. Here since
# task 44.1, so scripts\check-text.ps1 holds the files to it before any
# build as test-package.ps1 does.
function Get-PswBatchFaults {
    param([string]$Text, [string]$Value)
    $faults = @()
    if ($Text -match '[^\x20-\x7E\r\n]') { $faults += "a byte outside printable ASCII, CR and LF" }
    if ($Text -match "(?<!`r)`n" -or $Text -match "`r(?!`n)") { $faults += "a line not ended by CRLF" }
    if (-not $Text.EndsWith("`r`n")) { $faults += "no CRLF after the last line, so COMMAND.COM may not run it"; return $faults }
    $lines = @($Text.Substring(0, $Text.Length - 2) -split "`r`n")
    if ($lines[0] -cne "@echo off") { $faults += "line 1 is not @echo off" }
    if ($lines[$lines.Count - 1] -cne "pause") { $faults += "the last line is not pause" }
    $code = @()
    for ($i = 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -cnotmatch '^rem( |$)') { $code += $i }
    }
    $split = @('if "%OS%"=="Windows_NT" goto nt', 'goto run', ':nt', 'cd /d "%~dp0"', ':run')
    for ($k = 0; $k -lt $split.Count; $k++) {
        if ($code.Count -le $k -or $lines[$code[$k]] -cne $split[$k]) {
            $faults += ("command " + ($k + 1) + " after @echo off is not '" + $split[$k] + "'")
        }
    }
    $osLine = -1
    $ntLine = -1
    if ($code.Count -gt 0) { $osLine = $code[0] }
    if ($code.Count -gt 3) { $ntLine = $code[3] }
    $allowed = @('^rem( [ -~]*)?$', '^echo( [ -~]*|\.)$', '^:[a-z]+$', '^goto [a-z]+$', '^pause$',
                 '^if not exist portsw\.vbs goto [a-z]+$', '^if errorlevel [0-9] goto [a-z]+$',
                 '^if not "%OS%"=="Windows_NT" goto [a-z]+$', '^cscript //nologo portsw\.vbs (show|[0-9])$')
    $labels = @{}
    $gotos = @()
    for ($i = 1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line.Length -gt 127) { $faults += "line $($i + 1) is past COMMAND.COM's 127 characters" }
        if ($i -ne $osLine -and $i -ne $ntLine) {
            $ok = $false
            foreach ($p in $allowed) { if ($line -cmatch $p) { $ok = $true; break } }
            if (-not $ok) { $faults += "line $($i + 1) is not one of the commands both interpreters run: $line" }
        }
        if ($line -cmatch '^(rem|echo)\b' -and $line -match '[<>|&%^]') {
            $faults += "line $($i + 1) has a redirection, pipe or percent character on an echo or rem line: $line"
        }
        if ($line -match '^echo +(on|off) *$') { $faults += "line $($i + 1) is an echo COMMAND.COM reads as a switch: $line" }
        if ($line -cmatch '^:([a-z]+)$') {
            if ($labels.ContainsKey($Matches[1])) { $faults += "label $($Matches[1]) twice" }
            $labels[$Matches[1]] = $i
        }
        if ($line -cmatch '^(if .* )?goto ([a-z]+)$') { $gotos += , @($i, $Matches[2]) }
    }
    foreach ($g in $gotos) {
        if (-not $labels.ContainsKey($g[1])) { $faults += "line $($g[0] + 1) jumps to a label the file does not have: $($g[1])" }
        if ($g[1] -ceq "nt" -and $g[0] -ne $osLine) { $faults += "line $($g[0] + 1) is a goto nt that COMMAND.COM could take" }
    }
    $sets = @($lines | Where-Object { $_ -cmatch '^cscript //nologo portsw\.vbs [0-9]$' })
    if ($sets.Count -ne 1 -or $sets[0] -cne ('cscript //nologo portsw.vbs ' + $Value)) {
        $faults += "it does not set exactly one value, $Value (2 is a hand edit)"
    }
    $last = -1
    foreach ($pin in @('if not exist portsw.vbs goto nofile', 'cscript //nologo portsw.vbs show',
                       'if errorlevel 1 goto end', ('cscript //nologo portsw.vbs ' + $Value),
                       'if errorlevel 3 goto elevate', 'if errorlevel 1 goto refused')) {
        $at = [Array]::IndexOf($lines, $pin)
        if ($at -le $last) { $faults += "'$pin' is missing or out of order" } else { $last = $at }
    }
    foreach ($needle in @('Vista and 7: right-click it', 'choose Run as administrator')) {
        if (-not $Text.Contains($needle)) { $faults += "it no longer says '$needle'" }
    }
    # portsw.vbs says to restart after a set; an echo of its own printed it twice (task 38.6).
    if ($Text.Contains('echo RESTART WINDOWS')) { $faults += "it echoes 'RESTART WINDOWS', which portsw.vbs already prints" }
    return $faults
}

function Get-PlainScriptFaults {
    # A script a 1998-era host reads as text (src\portsw.vbs, which Windows
    # 98's Script Host runs): a byte outside printable ASCII, TAB, CR and LF,
    # or a line not ended by CRLF. Read as bytes.
    param([byte[]]$Bytes)
    $faults = @()
    if (@($Bytes | Where-Object { $_ -ge 0x80 -or ($_ -lt 0x20 -and $_ -ne 9 -and $_ -ne 10 -and $_ -ne 13) }).Count -gt 0) {
        $faults += "a byte outside printable ASCII, TAB, CR and LF"
    }
    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($Bytes)
    if ($text -match "(?<!`r)`n") { $faults += "a line not ended by CRLF" }
    return $faults
}

# How src\silent.vbs and src\portsw.vbs know this driver's controller
# (roadmap-usb4 task 44.75): by the DriverDesc of its class key, through one
# function, IsXhciDesc, which both carry word for word. 3.3.0.0's INF gave
# each controller it lists a name of its own (task 44.55), and the two
# scripts, which took the plain name and the pre-3.2.0.0 one alone, found
# none of those controllers. Get-ControllerDescFaults holds both scripts to
# the pinned function and runs Test-XhciDesc, its rule in PowerShell, over
# every name the two INFs give a controller (each must be taken), the names
# earlier releases gave it, which a machine keeps until it is updated (each
# must be taken), and every other device name of those INFs (each must be
# refused). A change to the function changes the pin and Test-XhciDesc
# together.
$script:XhciDescFunction = @(
    'Function IsXhciDesc(desc)',
    '    Dim d, p',
    '    d = LCase(desc)',
    '    p = LCase(XHCI_DESC)',
    '    IsXhciDesc = False',
    '    If d = p Or d = LCase(XHCI_DESC_OLD) Then',
    '        IsXhciDesc = True',
    '    ElseIf Len(d) >= Len(p) + 4 Then',
    '        If Left(d, Len(p) + 2) = p & " (" And Right(d, 1) = ")" Then IsXhciDesc = True',
    '    End If',
    'End Function'
) -join "`r`n"
$script:XhciDescEarlier = @(
    @{ Name = "xHCI98 USB 3 Controller";                   Release = "3.2.0.0" },
    @{ Name = "xHCI98 USB 3.x eXtensible Host Controller"; Release = "2.0.0.0 to 3.1.0.0" }
)

function Test-XhciDesc {
    # IsXhciDesc, with XHCI_DESC as -Plain and XHCI_DESC_OLD as -Old.
    param([string]$Desc, [string]$Plain, [string]$Old)
    $d = $Desc.ToLowerInvariant()
    $p = $Plain.ToLowerInvariant()
    if ($d -ceq $p -or $d -ceq $Old.ToLowerInvariant()) { return $true }
    return ($d.Length -ge ($p.Length + 4) -and
            $d.StartsWith($p + " (", [System.StringComparison]::Ordinal) -and
            $d.EndsWith(")", [System.StringComparison]::Ordinal))
}

function Get-InfModelNames {
    # Any line of the form %key%=section,... counts, wherever it is and
    # however indented, so a models line the check misread could only add a
    # name to hold, never drop one; a key with no [Strings] entry is kept as
    # $null so the caller can refuse it.
    param([string]$Text)
    $strings = @{}
    $inStrings = $false
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*\[([^\]]+)\]') { $inStrings = ($Matches[1].Trim() -eq "Strings"); continue }
        if ($inStrings -and $line -match '^\s*([A-Za-z0-9_.]+)\s*=\s*"([^"]*)"') { $strings[$Matches[1]] = $Matches[2] }
    }
    $models = @()
    foreach ($m in [regex]::Matches($Text, '(?m)^[ \t]*%([A-Za-z0-9_.]+)%\s*=\s*([A-Za-z0-9_.]+)\s*,')) {
        $key = $m.Groups[1].Value
        $name = $null
        if ($strings.ContainsKey($key)) { $name = $strings[$key] }
        $models += [pscustomobject]@{ Key = $key; Name = $name; Section = $m.Groups[2].Value }
    }
    return $models
}

function Get-ControllerDescFaults {
    # -Root is a tree, not this repository, so check-text.ps1's self-test can
    # run it over mutated copies.
    param([string]$Root)
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $faults = @()
    $plain = $null
    $old = $null
    foreach ($rel in @("src\silent.vbs", "src\portsw.vbs")) {
        $path = Join-Path $Root $rel
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $faults += "$rel is missing; the cut publishes it."; continue }
        $text = $latin1.GetString([System.IO.File]::ReadAllBytes($path))
        $c = @([regex]::Matches($text, '(?m)^Const XHCI_DESC = "([^"]*)"\r?$'))
        $o = @([regex]::Matches($text, '(?m)^Const XHCI_DESC_OLD = "([^"]*)"\r?$'))
        if ($c.Count -ne 1 -or $o.Count -ne 1) {
            $faults += "${rel}: not one Const XHCI_DESC and one Const XHCI_DESC_OLD this check can read."
            continue
        }
        if (-not $text.Contains($script:XhciDescFunction + "`r`n")) {
            $faults += "${rel}: its IsXhciDesc is not package-common.ps1's `$script:XhciDescFunction word for word (change both, and Test-XhciDesc)."
        }
        if (@([regex]::Matches($text, '(?m)^\s*If IsXhciDesc\(desc\) Then\r?$')).Count -ne 1) {
            $faults += "${rel}: FindKeys does not take a controller by one 'If IsXhciDesc(desc) Then'."
        }
        if ($null -eq $plain) {
            $plain = $c[0].Groups[1].Value
            $old = $o[0].Groups[1].Value
        } elseif ($c[0].Groups[1].Value -cne $plain -or $o[0].Groups[1].Value -cne $old) {
            $faults += "${rel}: its XHCI_DESC or XHCI_DESC_OLD differs from src\silent.vbs's."
        }
    }
    if ($null -eq $plain) { return $faults }
    foreach ($e in $script:XhciDescEarlier) {
        if (-not (Test-XhciDesc -Desc $e.Name -Plain $plain -Old $old)) {
            $faults += "the scripts' IsXhciDesc refuses '$($e.Name)', the controller's name in $($e.Release), which a machine keeps until it is updated."
        }
    }
    foreach ($rel in @("src\xhci98.inf", "src\xhci98-amd64.inf")) {
        $path = Join-Path $Root $rel
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $faults += "$rel is missing."; continue }
        $models = @(Get-InfModelNames -Text $latin1.GetString([System.IO.File]::ReadAllBytes($path)))
        $controllers = 0
        foreach ($m in $models) {
            if ($null -eq $m.Name) { $faults += "${rel}: models line %$($m.Key)% has no [Strings] entry this check can read."; continue }
            $isController = ($m.Section -match '^Xhci\.Dev')
            if ($isController) { $controllers++ }
            $taken = Test-XhciDesc -Desc $m.Name -Plain $plain -Old $old
            if ($isController -and -not $taken) {
                $faults += "${rel}: the controller's name '$($m.Name)' (%$($m.Key)%) is refused by IsXhciDesc in src\silent.vbs and src\portsw.vbs, so they would find no controller of that model."
            } elseif (-not $isController -and $taken) {
                $faults += "${rel}: '$($m.Name)' (%$($m.Key)%, $($m.Section)) is not the controller and IsXhciDesc takes it."
            }
        }
        if ($controllers -eq 0) { $faults += "${rel}: no models line installs Xhci.Dev*, so this check read no controller name." }
    }
    return $faults
}

# The field reports' batch files (roadmap-hcd task 38.1): QUALREP.BAT beside
# XHCIQUAL.EXE, run by MS-DOS's COMMAND.COM alone, so it carries no split. A
# Win spec (run by COMMAND.COM on Windows 98 SE and ME and by cmd.exe from
# 2000 to 7) carries portsw-on.bat's pinned split: none is published since
# task 42-A.4 retired SNAPON, SNAPGET and SNAPOFF.BAT for XHCITR's /setlevel
# and /snapshot, and the rule stays for the next. One definition of what each
# must say, read by test-package.ps1 against the repository's sources and by
# make-release.ps1 against the batches a cut stages and an upload set
# assembles (Assert-ReportBatchContent), so a stale or malformed batch in a
# -QualtoolDir or a published tree is refused as the
# repository's own would be. Name is the source name in the tool's own
# directory (Dir); make-release.ps1's $reportBatches maps it to the
# published 8.3 name.
#
# ArgPass, where a spec has it, is the batch's optional argument (owner,
# 2026-10-13, for the retired SNAPON.BAT LEVEL, SNAPGET.BAT NAME), passed on
# as it came: Args ends in %1, and that one run line is the only line that may
# carry %1. The default and the check are the tool's, so no `if` reads %1
# (task 38.1: a quoted or
# metacharacter %1 broke cmd.exe's `if "%1"==` before the tool could refuse
# it, and a goto change could route around a level test). No other percent
# variable (%2, %0, %*, %~1, %DATE%, ...) is admitted anywhere.
#
# ToolLast (owner, 2026-10-13: the batch's last line of output names the
# files to attach): the tool line is the last command before pause, reached
# by `if exist TOOL goto X` with :X just above it, and nothing reads the exit
# code - the tool prints the outcome. QUALREP.BAT keeps its flag logic.
#
# -PublishedVersion (the upload assembly, never the cut): a tree published
# from 2.4.0.0 to 3.0.0.0 carries XHCISNAP's SNAPON.BAT, SNAPGET.BAT and
# SNAPOFF.BAT in tools\xhcisnap\, retired in 3.1.0.0 (task 42-A.4), so it is
# still held to their specs (Test-LegacyReportBatchVersion).
function Test-LegacyReportBatchVersion {
    param([string]$PublishedVersion = "")
    return ($PublishedVersion -ne "" -and [version]$PublishedVersion -lt [version]"3.1.0.0")
}

function Get-ReportBatchSpecs {
    param([string]$PublishedVersion = "")
    $specs = @(
        @{ Name = "QUALREP.BAT"; Dir = "xhciqual"; Win = $false; Tool = "XHCIQUAL"; Args = "--probe-only --no-page --log-stamp"; Names = @("LOG"); Flag = "QUALREP.FLG" }
    )
    if (Test-LegacyReportBatchVersion -PublishedVersion $PublishedVersion) {
        $specs += @(
            @{ Name = "snapon.bat";  Dir = "xhcisnap"; Win = $true;  Tool = "XHCISNAP"; Args = "-on %1";       Names = @(); ArgPass = $true; ToolLast = $true },
            @{ Name = "snapget.bat"; Dir = "xhcisnap"; Win = $true;  Tool = "XHCISNAP"; Args = "-report %1";   Names = @(".DEV", ".TXT"); ArgPass = $true; ToolLast = $true },
            @{ Name = "snapoff.bat"; Dir = "xhcisnap"; Win = $true;  Tool = "XHCISNAP"; Args = "-disable";     Names = @(); ToolLast = $true }
        )
    }
    return $specs
}

# The batch map a tree is held to: tool -> source name -> published name,
# with the retired XHCISNAP batches added for a tree published before
# 3.1.0.0 (above). Never for the cut, whose -PublishedVersion is empty.
function Get-ReportBatchesFor {
    param([hashtable]$Batches, [string]$PublishedVersion = "")
    if (-not (Test-LegacyReportBatchVersion -PublishedVersion $PublishedVersion)) { return $Batches }
    $all = @{}
    foreach ($k in $Batches.Keys) { $all[$k] = $Batches[$k] }
    $all["xhcisnap"] = @{ "snapon.bat" = "SNAPON.BAT"; "snapget.bat" = "SNAPGET.BAT"; "snapoff.bat" = "SNAPOFF.BAT" }
    return $all
}

function Get-ReportBatchFaults {
    param([string]$Text, [hashtable]$Spec)
    $faults = @()
    if ($Text -match '[^\x20-\x7E\r\n]') { $faults += "a byte outside printable ASCII, CR and LF" }
    if ($Text -match "(?<!`r)`n" -or $Text -match "`r(?!`n)") { $faults += "a line not ended by CRLF" }
    if (-not $Text.EndsWith("`r`n")) { $faults += "no CRLF after the last line, so COMMAND.COM may not run it"; return $faults }
    $lines = @($Text.Substring(0, $Text.Length - 2) -split "`r`n")
    if ($lines[0] -cne "@echo off") { $faults += "line 1 is not @echo off" }
    if ($lines[$lines.Count - 1] -cne "pause") { $faults += "the last line is not pause" }
    $code = @()
    for ($i = 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -cnotmatch '^rem( |$)') { $code += $i }
    }
    $osLine = -1
    $ntLine = -1
    if ($Spec.Win) {
        $split = @('if "%OS%"=="Windows_NT" goto nt', 'goto run', ':nt', 'cd /d "%~dp0"', ':run')
        for ($k = 0; $k -lt $split.Count; $k++) {
            if ($code.Count -le $k -or $lines[$code[$k]] -cne $split[$k]) {
                $faults += ("command " + ($k + 1) + " after @echo off is not '" + $split[$k] + "'")
            }
        }
        if ($code.Count -gt 0) { $osLine = $code[0] }
        if ($code.Count -gt 3) { $ntLine = $code[3] }
    }
    $toolRe = '(?i:' + $Spec.Tool + '(\.exe)?)'
    $allowed = @('^rem( [ -~]*)?$', '^echo( [ -~]*|\.)$', '^:[a-z]+$', '^goto [a-z]+$', '^pause$',
                 ('^if not exist ' + $toolRe + ' goto [a-z]+$'), '^if errorlevel [0-9] goto [a-z]+$',
                 ('^' + $toolRe + ' [-a-z0-9 ]+( --done-flag [A-Z0-9]+\.FLG)?$'))
    if ($Spec.Win) { $allowed += '^if not "%OS%"=="Windows_NT" goto [a-z]+$' }
    if ($Spec.ContainsKey("ToolLast")) { $allowed += ('^if exist ' + $toolRe + ' goto [a-z]+$') }
    $argRunRe = $null
    if ($Spec.ContainsKey("ArgPass")) {
        $argRunRe = '^' + $toolRe + ' ' + [regex]::Escape($Spec.Args) + '$'
        $allowed += $argRunRe
    }
    # A completion flag, where the spec names one (QUALREP.BAT's, through
    # XHCIQUAL's --done-flag): tested, and deleted - that file alone.
    $flagArgs = ""
    if ($Spec.ContainsKey("Flag")) {
        $fl = [regex]::Escape($Spec.Flag)
        $allowed += @(('^if (not )?exist ' + $fl + ' goto [a-z]+$'), ('^if exist ' + $fl + ' del ' + $fl + '$'), ('^del ' + $fl + '$'))
        $flagArgs = '( --done-flag ' + $fl + ')?'
    }
    $labels = @{}
    $gotos = @()
    $runs = @()
    $runAt = @()
    for ($i = 1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line.Length -gt 127) { $faults += "line $($i + 1) is past COMMAND.COM's 127 characters" }
        $isSplit = ($i -eq $osLine -or $i -eq $ntLine)
        if (-not $isSplit) {
            $ok = $false
            foreach ($p in $allowed) { if ($line -cmatch $p) { $ok = $true; break } }
            if (-not $ok) { $faults += "line $($i + 1) is not one of the commands both interpreters run: $line" }
        }
        $isText = ($line -cmatch '^(rem|echo)\b')
        if ($isText -and $line -match '[<>|&%^]') {
            $faults += "line $($i + 1) has a redirection, pipe or percent character on an echo or rem line: $line"
        }
        if ($line -match '^echo +(on|off) *$') { $faults += "line $($i + 1) is an echo COMMAND.COM reads as a switch: $line" }
        if ($line -match '%(DATE|TIME)%') { $faults += "line $($i + 1) reads %DATE% or %TIME%, which COMMAND.COM on 98 SE and ME lacks: $line" }
        $isArg = ($null -ne $argRunRe -and $line -cmatch $argRunRe)
        if (-not $isSplit -and -not $isArg -and $line -cnotmatch '^if not "%OS%"=="Windows_NT" goto [a-z]+$' -and -not $isText -and $line.Contains('%')) {
            $faults += "line $($i + 1) reads a percent variable: $line"
        }
        if ($isArg -and ([regex]::Matches($line, '%')).Count -ne 1) {
            $faults += "line $($i + 1) reads a percent variable other than %1: $line"
        }
        if (-not $isSplit -and -not $isText -and $line -match '[A-Za-z]:\\|^cd\b|^chdir\b') {
            $faults += "line $($i + 1) names a drive path or changes directory, so its output would not land in the current directory: $line"
        }
        if ($line -cmatch '^:([a-z]+)$') {
            if ($labels.ContainsKey($Matches[1])) { $faults += "label $($Matches[1]) twice" }
            $labels[$Matches[1]] = $i
        }
        if ($line -cmatch '^(if .* )?goto ([a-z]+)$') { $gotos += , @($i, $Matches[2]) }
        if ($line -match ('^' + $toolRe + '( |$)')) { $runs += $line; $runAt += $i }
    }
    foreach ($g in $gotos) {
        if (-not $labels.ContainsKey($g[1])) { $faults += "line $($g[0] + 1) jumps to a label the file does not have: $($g[1])" }
        if ($g[1] -ceq "nt" -and $g[0] -ne $osLine) { $faults += "line $($g[0] + 1) is a goto nt that COMMAND.COM could take" }
    }
    $want = @($runs | Where-Object { $_ -cmatch ('^' + $toolRe + ' ' + [regex]::Escape($Spec.Args) + $flagArgs + '$') })
    if ($runs.Count -ne 1 -or $want.Count -ne 1) {
        $faults += ("it does not run {0} exactly once, as '{0} {1}' (it runs: {2})" -f $Spec.Tool, $Spec.Args, ($runs -join "; "))
    }
    if ($Spec.ContainsKey("ToolLast") -and $runAt.Count -eq 1) {
        # The tool runs last and prints the outcome, its last line what to
        # attach (owner, 2026-10-13): after its line only labels and the
        # final pause, no IF ERRORLEVEL anywhere, and it is reached by
        # `if exist TOOL goto X` with X the label just above it.
        $r = $runAt[0]
        for ($k = $r + 1; $k -lt $lines.Count - 1; $k++) {
            if ($lines[$k] -cnotmatch '^(:[a-z]+|rem( [ -~]*)?)$') { $faults += "line $($k + 1) follows the $($Spec.Tool) line; only labels and the last pause may: $($lines[$k])" }
        }
        if (@($lines | Where-Object { $_ -cmatch '^if errorlevel' }).Count -gt 0) { $faults += "it reads the exit code with IF ERRORLEVEL; $($Spec.Tool) prints the outcome itself" }
        $above = $r - 1
        while ($above -gt 0 -and $lines[$above] -cmatch '^rem( |$)') { $above-- }
        $entry = $null
        if ($lines[$above] -cmatch '^:([a-z]+)$') { $entry = $Matches[1] }
        $guard = @()
        $guardAt = -1
        for ($k = 1; $k -lt $r; $k++) { if ($lines[$k] -cmatch ('^if exist ' + $toolRe + ' goto ([a-z]+)$')) { $guard += $Matches[2]; $guardAt = $k } }
        # From the split (or @echo off) to the guard, only rem and echo, so
        # no jump can route around the tool line.
        $from = 1
        if ($Spec.Win -and $code.Count -gt 4) { $from = $code[4] + 1 }
        for ($k = $from; $k -lt $guardAt; $k++) {
            if ($lines[$k] -cnotmatch '^(rem|echo)( [ -~]*|\.)?$') { $faults += "line $($k + 1) comes before the 'if exist $($Spec.Tool).EXE' test; only rem and echo may: $($lines[$k])" }
        }
        if ($null -eq $entry -or $guard.Count -ne 1 -or $guard[0] -cne $entry) {
            $faults += "the $($Spec.Tool) line is not reached by one 'if exist $($Spec.Tool).EXE goto X' before it, with :X just above it"
        }
        foreach ($g in $gotos) {
            if ($null -ne $entry -and $g[1] -ceq $entry -and $lines[$g[0]] -cnotmatch '^if exist ') { $faults += "line $($g[0] + 1) jumps to the $($Spec.Tool) line other than through 'if exist'" }
        }
    }
    if ($Spec.Name -eq "snapget.bat" -and @($runs | Where-Object { $_ -match '-disable|-verbosity +0' }).Count -gt 0) {
        $faults += "it turns the channel off; SNAPGET.BAT leaves it on and SNAPOFF.BAT turns it off"
    }
    $echoes = (@($lines | Where-Object { $_ -cmatch '^echo ' }) -join "`n")
    if ($Spec.Names.Count -gt 0 -and -not $echoes.ToUpperInvariant().Contains("ATTACH")) { $faults += "no echo line says what to attach" }
    foreach ($ext in $Spec.Names) {
        if (-not $echoes.ToUpperInvariant().Contains($ext)) { $faults += "no echo line names the $ext file it writes" }
    }
    if ($Spec.Win -and -not $Text.Contains('Run as administrator')) { $faults += "it no longer says 'Run as administrator' for Vista and 7" }
    return $faults
}

# The silent install's two batch files and SILENT.REG (roadmap-hcd task
# 38.2, design record 13 section 6.11.8): what test-package.ps1 holds the
# repository's sources to, and what make-release.ps1 holds a staged or
# published tools\silent\ to (Assert-SilentInstallContent; task 38.2). The
# batches are run by COMMAND.COM on Windows 98 SE and
# ME and by cmd.exe elsewhere, so they are held as portsw-on.bat's are:
# every line from a short list both interpreters run, and a pinned split
# whose cmd.exe branch only says it changed nothing. silent-on.bat merges
# SILENT.REG before it sets 1, and silent-off.bat never merges it.
function Get-SilentBatchFaults {
    param([string]$Text, [string]$Value)
    $faults = @()
    if ($Text -match '[^\x20-\x7E\r\n]') { $faults += "a byte outside printable ASCII, CR and LF" }
    if ($Text -match "(?<!`r)`n" -or $Text -match "`r(?!`n)") { $faults += "a line not ended by CRLF" }
    if (-not $Text.EndsWith("`r`n")) { $faults += "no CRLF after the last line, so COMMAND.COM may not run it"; return $faults }
    $lines = @($Text.Substring(0, $Text.Length - 2) -split "`r`n")
    if ($lines[0] -cne "@echo off") { $faults += "line 1 is not @echo off" }
    if ($lines[$lines.Count - 1] -cne "pause") { $faults += "the last line is not pause" }
    $code = @()
    for ($i = 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -cnotmatch '^rem( |$)') { $code += $i }
    }
    # The split: cmd.exe takes nt, says nothing was changed, and ends.
    $split = @('if "%OS%"=="Windows_NT" goto nt', 'goto run', ':nt', $null, 'goto end', ':run')
    for ($k = 0; $k -lt $split.Count; $k++) {
        if ($code.Count -le $k) { $faults += "the file ends inside the split"; break }
        $line = $lines[$code[$k]]
        if ($null -eq $split[$k]) {
            if ($line -cnotmatch '^echo [ -~]*nothing was changed\.$') { $faults += "command 4 after @echo off is not the echo that says nothing was changed" }
        } elseif ($line -cne $split[$k]) {
            $faults += ("command " + ($k + 1) + " after @echo off is not '" + $split[$k] + "'")
        }
    }
    $osLine = -1
    if ($code.Count -gt 0) { $osLine = $code[0] }
    $allowed = @('^rem( [ -~]*)?$', '^echo( [ -~]*|\.)$', '^:[a-z]+$', '^goto [a-z]+$', '^pause$',
                 '^if not exist silent\.(vbs|reg) goto [a-z]+$', '^if errorlevel [0-9] goto [a-z]+$',
                 '^cscript //nologo silent\.vbs (show|[0-9])$', '^regedit /s silent\.reg$')
    $labels = @{}
    $gotos = @()
    for ($i = 1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line.Length -gt 127) { $faults += "line $($i + 1) is past COMMAND.COM's 127 characters" }
        if ($i -ne $osLine) {
            $ok = $false
            foreach ($p in $allowed) { if ($line -cmatch $p) { $ok = $true; break } }
            if (-not $ok) { $faults += "line $($i + 1) is not one of the commands both interpreters run: $line" }
        }
        if ($line -cmatch '^(rem|echo)\b' -and $line -match '[<>|&%^]') {
            $faults += "line $($i + 1) has a redirection, pipe or percent character on an echo or rem line: $line"
        }
        if ($line -match '^echo +(on|off) *$') { $faults += "line $($i + 1) is an echo COMMAND.COM reads as a switch: $line" }
        if ($line -cmatch '^:([a-z]+)$') {
            if ($labels.ContainsKey($Matches[1])) { $faults += "label $($Matches[1]) twice" }
            $labels[$Matches[1]] = $i
        }
        if ($line -cmatch '^(if .* )?goto ([a-z]+)$') { $gotos += , @($i, $Matches[2]) }
    }
    foreach ($g in $gotos) {
        if (-not $labels.ContainsKey($g[1])) { $faults += "line $($g[0] + 1) jumps to a label the file does not have: $($g[1])" }
        if ($g[1] -ceq "nt" -and $g[0] -ne $osLine) { $faults += "line $($g[0] + 1) is a goto nt that COMMAND.COM could take" }
    }
    $sets = @($lines | Where-Object { $_ -cmatch '^cscript //nologo silent\.vbs [0-9]$' })
    if ($sets.Count -ne 1 -or $sets[0] -cne ('cscript //nologo silent.vbs ' + $Value)) {
        $faults += "it does not set exactly one value, $Value"
    }
    $merges = @($lines | Where-Object { $_ -cmatch '^regedit ' })
    if ($Value -eq "1") {
        $pins = @('if not exist silent.vbs goto nofile', 'if not exist silent.reg goto nofile',
                  'cscript //nologo silent.vbs show', 'if errorlevel 1 goto nochange',
                  'regedit /s silent.reg', 'cscript //nologo silent.vbs 1',
                  'if errorlevel 5 goto unverified', 'if errorlevel 4 goto nolist',
                  'if errorlevel 3 goto partial', 'if errorlevel 1 goto notset')
        if ($merges.Count -ne 1) { $faults += "it does not merge SILENT.REG exactly once" }
    } else {
        $pins = @('if not exist silent.vbs goto nofile', 'cscript //nologo silent.vbs 0',
                  'if errorlevel 5 goto unverified', 'if errorlevel 3 goto partial',
                  'if errorlevel 1 goto notset', ':list')
        if ($merges.Count -ne 0) { $faults += "it merges a .reg file; silent-off.bat leaves the list alone" }
        foreach ($needle in @('The list was left in place', 'delete the', 'SilentClasses key')) {
            if (-not $Text.Contains($needle)) { $faults += "it no longer says '$needle'" }
        }
    }
    $last = -1
    foreach ($pin in $pins) {
        $at = [Array]::IndexOf($lines, $pin)
        if ($at -le $last) { $faults += "'$pin' is missing or out of order" } else { $last = $at }
    }
    if (-not $Text.Contains('Windows 98 SE and ME only')) { $faults += "it no longer says 'Windows 98 SE and ME only'" }
    return $faults
}

# SILENT.REG: REGEDIT4, the one key, and the six values of design record
# 13 section 6.11.8 with their exact bytes, nothing else (SweetLow's
# list; names kept so a merge over his leaves six values, not twelve).
function Get-SilentRegFaults {
    param([string]$Text)
    $want = "REGEDIT4`r`n`r`n[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB\SilentClasses]`r`n" +
            "`"0`"=hex:80,09,00,00`r`n`"1`"=hex:43,03,01,01`r`n`"2`"=hex:43,03,01,02`r`n" +
            "`"3`"=hex:c3,08,06,50`r`n`"4`"=hex:83,03,01,01`r`n`"5`"=hex:83,03,01,02`r`n`r`n"
    $faults = @()
    if ($Text -match '[^\x20-\x7E\r\n]') { $faults += "a byte outside printable ASCII, CR and LF" }
    if ($Text -match "(?<!`r)`n" -or $Text -match "`r(?!`n)") { $faults += "a line not ended by CRLF" }
    if ($Text -cne $want) { $faults += "it is not REGEDIT4, the SilentClasses key and its six values, exactly" }
    return $faults
}

# ---- Stor98, the bundled NVMe driver ----------------------------------------
#
# Stor98 is the NVMe SCSI miniport in src\stor98 (BSD-3-Clause; roadmap-usb4
# task 42-A.20 brought it into this tree). Since 3.1.0.0 (task 42-A.30,
# owner 2026-10-19) a package bundles the Stor98 that scripts\build-driver.cmd
# built and gated from src\stor98, not a pinned release, and it carries the
# package's one version and date: its version resources take XHCI_VER_STR
# from src\xhci_version.h (src\stor98\src\stor98ver.h) and its two INFs'
# DriverVer is the package's (scripts\inf-gate\check-stor98-inf.ps1). The
# x64 stor98.sys is signed by the packager with the other x64 drivers, by the
# same key, and Stor98 has no certificate of its own any more.
#
# What one package carries, every name at its root starting with "stor98" so
# none can clash with this project's own files:
#   x86    stor98.mpd (98 SE, ME) and stor98.sys (2000 to 7) from
#          src\stor98\out\free\win9x and \x86, inf\stor98-x86.inf as
#          stor98.inf, and inf\stor98-9x.inf as stor98w9.inf, the INF the
#          installer stages on 98 SE and ME alone (design record 31
#          section 5.2)
#   amd64  stor98.sys from src\stor98\out\free\amd64, and inf\stor98-x64.inf
#          as stor98.inf
#   both   LICENSE, NOTICE and docs\package-readme.txt as stor98-LICENSE.txt,
#          stor98-NOTICE.txt and stor98-readme.txt
# The package checks that would otherwise judge these files as this
# project's own are given exactly the list Get-Stor98PackageFiles returns.

$script:Stor98Images = @{
    "x86"   = @(@{ Source = "out\free\win9x\stor98.mpd"; Dest = "stor98.mpd"; Target = "win9x"; Stamp = "stor98mpd" },
                @{ Source = "out\free\x86\stor98.sys";   Dest = "stor98.sys"; Target = "x86";   Stamp = "stor98" })
    "amd64" = @(@{ Source = "out\free\amd64\stor98.sys"; Dest = "stor98.sys"; Target = "amd64"; Stamp = "stor98" })
}
$script:Stor98Infs = @{ "x86" = "inf\stor98-x86.inf"; "amd64" = "inf\stor98-x64.inf" }
$script:Stor98Inf9x = "inf\stor98-9x.inf"
$script:Stor98RenamedTop = [ordered]@{
    "LICENSE"                  = "stor98-LICENSE.txt"
    "NOTICE"                   = "stor98-NOTICE.txt"
    "docs\package-readme.txt"  = "stor98-readme.txt"
}
$script:Stor98MasterHeader = "include\stor98cm.h"

function Get-Stor98InfVersion {
    param([string]$Path)
    $text = [System.IO.File]::ReadAllText($Path)
    $m = [regex]::Match($text, '(?m)^\s*DriverVer\s*=\s*[^,\r\n]+,\s*([0-9.]+)')
    if (-not $m.Success) { return "" }
    return $m.Groups[1].Value
}

function Get-Stor98HeaderVersion {
    # STOR98LS_VERSION of a stor98cm.h, or "" where it has none.
    param([string]$Path)
    $m = [regex]::Match([System.IO.File]::ReadAllText($Path), '(?m)^\s*#\s*define\s+STOR98LS_VERSION\s+(\d+)\b')
    if (-not $m.Success) { return "" }
    return $m.Groups[1].Value
}

function Get-PackageVersion {
    # XHCI_VER_STR of src\xhci_version.h.
    param([string]$Repo)
    $h = [System.IO.File]::ReadAllText((Join-Path $Repo "src\xhci_version.h"))
    $m = [regex]::Match($h, '(?m)^#define\s+XHCI_VER_STR\s+"(\d+\.\d+\.\d+\.\d+)"')
    if (-not $m.Success) { throw "src\xhci_version.h has no XHCI_VER_STR." }
    return $m.Groups[1].Value
}

function Resolve-Stor98Build {
    # The Stor98 build one package (-Arch) bundles, verified before anything
    # is staged: every file present; both INFs at the package's DriverVer
    # (check-stor98-inf.ps1); each image's version, STOR98_FLAVOUR_FREE marker
    # and imports (check-stor98-build.ps1) and its source stamp
    # (source-stamp.ps1 -Check: built from the tree as it is), unless
    # -SkipBinaryGates (the packager's self-tests, whose images are stand-ins);
    # and Stor98's interface header byte for byte the copy xhcicm.sys
    # compiles against (-HeaderPath, src\cm\stor98cm.h). -Root is src\stor98,
    # or the self-tests' stand-in tree. Returns Version, Dir, Interface and
    # Files (per architecture: Source relative to Dir, FullPath, Dest, Sha256).
    param([string]$Repo, [string]$Root = "", [string[]]$Arch, [string]$HeaderPath,
          [switch]$SkipBinaryGates)
    if ($Root -eq "") { $Root = Join-Path $Repo "src\stor98" }
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "no Stor98 tree at '$Root'." }
    $Root = (Resolve-Path -LiteralPath $Root).ProviderPath.TrimEnd('\')
    $version = Get-PackageVersion -Repo $Repo
    $files = @{}
    foreach ($a in $Arch) {
        $list = @()
        $items = @($script:Stor98Images[$a]) + @(@{ Source = $script:Stor98Infs[$a]; Dest = "stor98.inf" })
        if ($a -eq "x86") { $items += @{ Source = $script:Stor98Inf9x; Dest = "stor98w9.inf" } }
        foreach ($k in $script:Stor98RenamedTop.Keys) { $items += @{ Source = $k; Dest = $script:Stor98RenamedTop[$k] } }
        foreach ($it in $items) {
            $full = Join-Path $Root $it.Source
            if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
                throw ("Stor98 has no '{0}' for the {1} package. Build and gate it first: scripts\build-driver.cmd{2} builds Stor98 with this project's drivers." -f `
                    $full, $a, $(if ($a -eq "amd64") { " <flavour> -amd64" } else { "" }))
            }
            $list += [pscustomobject]@{ Source = $it.Source; FullPath = $full; Dest = $it.Dest
                                        Sha256 = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash }
        }
        $infVer = Get-Stor98InfVersion -Path (Join-Path $Root $script:Stor98Infs[$a])
        if ($infVer -ne $version) {
            throw "'$($script:Stor98Infs[$a])' of Stor98 declares DriverVer version '$infVer', and the package is $version (src\xhci_version.h)."
        }
        if (-not $SkipBinaryGates) {
            foreach ($img in $script:Stor98Images[$a]) {
                $full = Join-Path $Root $img.Source
                $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Repo "scripts\check-stor98-build.ps1") -Image $full -Target $img.Target 2>&1
                if ($LASTEXITCODE -ne 0) { throw ("Stor98's {0} failed its gate:`n{1}" -f $img.Source, (($out | Out-String).TrimEnd())) }
                $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Repo "scripts\source-stamp.ps1") -Driver $img.Stamp -Check (Split-Path -Parent $full) 2>&1
                if ($LASTEXITCODE -ne 0) {
                    throw ("Stor98's {0} is not the build of src\stor98 as it is now (source stamp, exit {1}):`n{2}`nRebuild it with scripts\build-driver.cmd." -f `
                        $img.Source, $LASTEXITCODE, (($out | Out-String).TrimEnd()))
                }
            }
        }
        $files[$a] = $list
    }
    if (-not $SkipBinaryGates) {
        $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Repo "scripts\inf-gate\check-stor98-inf.ps1") 2>&1
        if ($LASTEXITCODE -ne 0) { throw ("Stor98's INFs failed their gate:`n" + (($out | Out-String).TrimEnd())) }
    }
    # The interface: Stor98's master header and the copy xhcicm.sys compiles
    # against, byte for byte (scripts\check-stor98-header.ps1 holds the same
    # on every gated build), and the link-state version it declares.
    $master = Join-Path $Root $script:Stor98MasterHeader
    if (-not (Test-Path -LiteralPath $master -PathType Leaf)) { throw "Stor98 has no '$master'." }
    if ($HeaderPath -eq "" -or -not (Test-Path -LiteralPath $HeaderPath -PathType Leaf)) {
        throw "xhci98 has no '$HeaderPath' to hold Stor98's interface header against."
    }
    if ([Convert]::ToBase64String([System.IO.File]::ReadAllBytes($master)) -ne
        [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($HeaderPath))) {
        throw "Stor98's '$master' is not byte for byte '$HeaderPath': the connection manager would be built against another interface than the Stor98 it is bundled with."
    }
    $iface = Get-Stor98HeaderVersion -Path $master
    if ($iface -eq "") { throw "'$master' defines no STOR98LS_VERSION." }
    return [pscustomobject]@{ Version = $version; Dir = $Root; Interface = $iface; Files = $files }
}

function Get-Stor98PackageFiles {
    # The Stor98 files one package carries (Resolve-Stor98Build's list).
    # -Arch is the packager's: x86 or amd64.
    param($Release, [ValidateSet("x86", "amd64")][string]$Arch)
    if (-not $Release.Files.ContainsKey($Arch)) {
        throw "the Stor98 build was resolved without the $Arch package's files."
    }
    return @($Release.Files[$Arch])
}

function Assert-Stor98SettingsTable {
    # make-release.ps1's check of the bundled Stor98's readme.
    param([string]$ReadmePath, [string]$Version)

    # The installer's Stor98 settings (record 26 section 5.6) are copied from
    # this readme's SETTINGS table; test\stor98-settings.txt is that table as
    # the installer's rows were checked against it (test_inst). A readme
    # whose table differs - a value renamed, retyped or given another range -
    # fails here rather than the user.
    $tableCopy = Join-Path $PSScriptRoot "..\..\test\stor98-settings.txt"
    $readmeLines = @(([System.IO.File]::ReadAllText($ReadmePath)) -split "\r?\n")
    $start = -1
    $end = -1
    for ($i = 0; $i -lt $readmeLines.Count; $i++) {
        if ($start -lt 0 -and $readmeLines[$i] -match '^  9x name / NT key') { $start = $i }
        if ($start -ge 0 -and $readmeLines[$i] -match '^Polling values outside') { $end = $i; break }
    }
    if ($start -lt 0 -or $end -lt 0) {
        throw "the Stor98 $Version readme has no SETTINGS table where the installer reads one (from '  9x name / NT key' to 'Polling values outside'). Update xhciinst\inst_core.c's Stor98 rows and test\stor98-settings.txt to that readme."
    }
    $table = @($readmeLines[$start..($end - 1)])
    while ($table.Count -gt 0 -and $table[$table.Count - 1].Trim() -eq "") { $table = @($table[0..($table.Count - 2)]) }
    $copy = @(([System.IO.File]::ReadAllText($tableCopy)) -split "\r?\n")
    while ($copy.Count -gt 0 -and $copy[$copy.Count - 1].Trim() -eq "") { $copy = @($copy[0..($copy.Count - 2)]) }
    if (($table -join "`n") -cne ($copy -join "`n")) {
        throw "the Stor98 $Version readme's SETTINGS table differs from test\stor98-settings.txt, the table the installer's Stor98 rows were checked against. Update xhciinst\inst_core.c's instS98Values and INST_S98_TABLE_VERSION and that file to this readme, and run test\run-host-tests.cmd."
    }
}

function Get-Stor98NotesFault {
    # The release notes' one bundle declaration, exactly "This download
    # bundles Stor98 `<version>`." or "This download bundles no Stor98.",
    # against what the cut bundles ($Version, "" for none). Returns "" when
    # they agree, else why not; the cut fills the placeholder in.
    param([string]$Text, [string]$Version)
    $ms = [regex]::Matches($Text, '(?m)^This download bundles (.+?)\.[ \t]*\r?$')
    if ($ms.Count -ne 1) {
        return "docs\using\release-notes.md has $($ms.Count) 'This download bundles ...' declarations, not one ('NVMe drives behind Thunderbolt or USB4')."
    }
    $said = $ms[0].Groups[1].Value.Trim()
    $bt = [string][char]96
    $want = if ($Version -eq "") { "no Stor98" } else { "Stor98 " + $bt + $Version + $bt }
    if ($said.Contains("filled in at the cut")) {
        return "docs\using\release-notes.md still has the bundled Stor98 placeholder: write 'This download bundles $want.' in its place ('NVMe drives behind Thunderbolt or USB4')."
    }
    if ($said -cne $want) {
        $what = if ($Version -eq "") { "no Stor98" } else { "Stor98 $Version" }
        return "docs\using\release-notes.md says 'This download bundles $said.', and this cut bundles ${what}: write 'This download bundles $want.'"
    }
    return ""
}

# ------------------------------------------------------------------
# The gated build's manifest, and the sign-only mode that trusts it
# (roadmap-usb4.md SIGN.1)
# ------------------------------------------------------------------
#
# scripts\build-driver.cmd writes out\gated-<arch>-<flavour>.txt at the end of
# a flavour, after every gate for it has passed (the self-tests and the host
# suite before the builds, then each binary's import gate, flavour marker and
# source stamp): each gated binary's SHA-256, the commit and whether the tree
# was dirty, whether the import gates had their target evidence, and the time.
# make-package.ps1 -SignOnly reads it and skips the host suite and the import
# gates for a binary whose bytes are exactly the ones those gates read. The
# hash is the guarantee; the commit is what says the INFs and scripts the
# package is staged with are the ones that build was gated beside.
#
# One "key=value" per line, ASCII, CRLF, "#" lines ignored; the binaries are
# "sha256.<name>=<64 hex>". Written by scripts\package\write-gated-manifest.ps1,
# from the hash each binary had right after its own import gate passed, and
# only if every binary still has it once the flavour's last gate has passed.
#
# What this protects against is a mismatch by accident and another process on
# the same machine changing a binary while the build or the packager runs. It
# is not a defence against someone who deliberately forges the record.
#
function Get-GatedManifestPath {
    param([string]$Repo, [string]$Arch, [string]$Flavor)
    return (Join-Path $Repo ("out\gated-{0}-{1}.txt" -f $Arch, $Flavor))
}

function Get-FileSha256 {
    # .NET rather than Get-FileHash: build-driver.cmd notes that a PS7 module
    # path inherited through cmd can hide the 5.1 cmdlet.
    param([string]$Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        return (($sha.ComputeHash($fs) | ForEach-Object { $_.ToString("X2") }) -join "")
    } finally {
        $fs.Dispose()
        $sha.Dispose()
    }
}

function ConvertFrom-GatedManifest {
    # Parses the manifest's text. Throws on anything malformed: a manifest
    # that cannot be read exactly is one no gate can be reused from.
    param([string]$Text)
    $m = @{ Format = ""; Arch = ""; Flavor = ""; Commit = ""; Dirty = ""; TargetEvidence = ""; Time = ""; Sha256 = @{} }
    $seen = @{}
    foreach ($raw in ($Text -split "`r?`n")) {
        $line = $raw.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) { continue }
        $eq = $line.IndexOf("=")
        if ($eq -lt 1) { throw "malformed line '$line'" }
        $key = $line.Substring(0, $eq).Trim().ToLowerInvariant()
        $val = $line.Substring($eq + 1).Trim()
        if ($seen.ContainsKey($key)) { throw "'$key' appears twice" }
        $seen[$key] = $true
        if ($key.StartsWith("sha256.")) {
            $name = $key.Substring(7)
            # stor98.mpd, Stor98's 98 SE / ME image, since task 42-A.30.
            if ($name -notmatch '^[a-z0-9_]+\.sys$' -and $name -ne 'stor98.mpd') { throw "'$key' does not name a .sys or stor98.mpd" }
            if ($val -notmatch '^[0-9A-Fa-f]{64}$') { throw "'$key' is not a SHA-256" }
            $m.Sha256[$name] = $val.ToUpperInvariant()
            continue
        }
        switch ($key) {
            "format"          { $m.Format = $val }
            "arch"            { $m.Arch = $val }
            "flavor"          { $m.Flavor = $val }
            "commit"          { $m.Commit = $val.ToLowerInvariant() }
            "dirty"           { $m.Dirty = $val }
            "target_evidence" { $m.TargetEvidence = $val }
            "time"            { $m.Time = $val }
            default           { throw "unknown key '$key'" }
        }
    }
    if ($m.Format -ne "1") { throw "format '$($m.Format)' is not 1" }
    foreach ($k in @("Arch", "Flavor", "Commit", "Dirty", "TargetEvidence", "Time")) {
        if ($m[$k] -eq "") { throw "no '$k' line" }
    }
    if ($m.Dirty -notmatch '^[01]$') { throw "dirty '$($m.Dirty)' is not 0 or 1" }
    if ($m.TargetEvidence -notmatch '^[01]$') { throw "target_evidence '$($m.TargetEvidence)' is not 0 or 1" }
    if ($m.Sha256.Count -eq 0) { throw "no sha256 line" }
    return $m
}

function ConvertTo-GatedManifest {
    # The writer's half, here so the format has one definition. $Sha256 maps
    # a binary's file name to its hash.
    param([string]$Arch, [string]$Flavor, [string]$Commit, [bool]$Dirty,
          [bool]$TargetEvidence, [string]$Time, [hashtable]$Sha256)
    $lines = @(
        "# The gated build's record (scripts\build-driver.cmd), read by",
        "# scripts\package\make-package.ps1 -SignOnly. Generated; do not edit.",
        "format=1",
        "arch=$Arch",
        "flavor=$Flavor",
        "commit=$Commit",
        ("dirty=" + $(if ($Dirty) { "1" } else { "0" })),
        ("target_evidence=" + $(if ($TargetEvidence) { "1" } else { "0" })),
        "time=$Time"
    )
    foreach ($name in ($Sha256.Keys | Sort-Object)) {
        $lines += ("sha256.{0}={1}" -f $name.ToLowerInvariant(), $Sha256[$name].ToUpperInvariant())
    }
    return (($lines -join "`r`n") + "`r`n")
}

function Get-GatedManifestFault {
    # "" when the manifest admits a sign-only package of $Binaries (file name
    # -> path of the bytes to be staged), else the reason it does not. The
    # order decides only which reason is given: the build it describes, then
    # each binary's bytes, then the tree the package is staged from.
    #
    # A HEAD other than the recorded commit is admitted only through
    # $Advance, Get-GatedHeadAdvance's answer for that commit and $Head
    # (roadmap-usb4 task 43.9): the recorded commit an ancestor of HEAD, and
    # no file changed between them a build input. Without it any other
    # commit is refused, as it always was.
    param($Manifest, [string]$Arch, [string]$Flavor, [hashtable]$Binaries,
          [string]$Head, [bool]$Dirty, [bool]$NoTargetEvidence, $Advance = $null)
    if ($Manifest.Arch -ne $Arch -or $Manifest.Flavor -ne $Flavor) {
        return ("it records the {0} {1} build, and this is the {2} {3} package" -f $Manifest.Flavor, $Manifest.Arch, $Flavor, $Arch)
    }
    foreach ($name in ($Binaries.Keys | Sort-Object)) {
        $key = $name.ToLowerInvariant()
        if (-not $Manifest.Sha256.ContainsKey($key)) {
            return "it records no hash for $name, so that binary was not gated by the build it describes"
        }
        if (-not (Test-Path -LiteralPath $Binaries[$name] -PathType Leaf)) {
            return "$name is not at '$($Binaries[$name])'"
        }
        $actual = Get-FileSha256 -Path $Binaries[$name]
        if ($actual -ne $Manifest.Sha256[$key]) {
            return ("{0} at '{1}' is not the binary the build gated: SHA-256 {2}, recorded {3}" -f $name, $Binaries[$name], $actual, $Manifest.Sha256[$key])
        }
    }
    if ($Manifest.Dirty -ne "0") {
        return "the build ran in a tree with uncommitted changes, so no commit says what it was built from"
    }
    if ($Manifest.Commit -ne $Head.ToLowerInvariant()) {
        if ($null -eq $Advance -or $Advance.From -ne $Manifest.Commit -or $Advance.To -ne $Head.ToLowerInvariant()) {
            return ("it was built at commit {0}, and the tree is at {1}: the INFs and scripts this package would be staged with are not the ones the build was gated beside" -f $Manifest.Commit, $Head)
        }
        if ($Advance.Fault -ne "") {
            return ("it was built at commit {0}, and the tree is at {1}: {2}" -f $Manifest.Commit, $Head, $Advance.Fault)
        }
    }
    if ($Dirty) {
        return "the tree has uncommitted changes since that commit"
    }
    if ($Manifest.TargetEvidence -ne "1" -and -not $NoTargetEvidence) {
        return "its import gates ran with -NoTargetEvidence, on the allowlists alone, and this run did not ask for that"
    }
    return ""
}

function Get-GitTreeState {
    # HEAD, and whether the tree differs from it: a tracked change, any
    # untracked file that is not ignored, or a tracked file git has been told
    # not to look at (assume-unchanged, a lowercase tag in `git ls-files -v`,
    # or skip-worktree, "S"), since git status cannot see a change to that
    # one. Head "none" when git cannot say, which the sign-only mode refuses.
    param([string]$Repo)
    $head = ""
    $status = ""
    $hidden = @()
    $ok = $false
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $head = (& git -C $Repo rev-parse HEAD 2>$null | Out-String).Trim()
        $headRc = $LASTEXITCODE
        $status = (& git -C $Repo status --porcelain --untracked-files=all 2>$null | Out-String).Trim()
        $statusRc = $LASTEXITCODE
        $hidden = @(& git -C $Repo ls-files -v 2>$null | Where-Object { $_ -cmatch '^([a-z]|S) ' })
        $ok = ($headRc -eq 0 -and $statusRc -eq 0 -and $LASTEXITCODE -eq 0 -and $head -match '^[0-9a-f]{40}$')
    } catch {
        $ok = $false
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if (-not $ok) { return @{ Head = "none"; Dirty = $true } }
    return @{ Head = $head; Dirty = ($status -ne "" -or $hidden.Count -gt 0) }
}

# ------------------------------------------------------------------
# A HEAD newer than the gated build's commit (roadmap-usb4 task 43.9)
# ------------------------------------------------------------------
#
# The record names the commit the gated build ran at, and -SignOnly refused
# any other HEAD, so a documentation commit after the build cost a rebuild
# (owner, 2026-10-20, after the 3.1.0.0 cut). A newer HEAD is admitted when
# the recorded commit is its ancestor and no file changed between the two is
# a build input: a file any of the six source stamps lists (scripts\
# source-stamp.ps1 -List, the stamp's own list); a file of the kinds a build
# reads (Get-BuildInputRules) in those files' directories, since a file added
# or removed since is not in today's list; a file the host test suite
# compiles (test\run-host-tests.cmd), and every header those files and the
# suite's own test\*.c include by a quoted #include, followed to its end;
# test\ itself; the import gates' scripts\import-gate\; Stor98's sources,
# build scripts and unit tests; and the scripts that build the binaries and
# judge them for the record, with scripts\common.ps1, which they dot-source.
# Over-covering is the safe side: a file wrongly
# counted costs a rebuild, one wrongly left out lets the host suite's and the
# import gates' old verdict stand for a tree they never read. The host
# suite's files count by name and by what they include, not by their
# directory (roadmap-usb4 task 44.6; owner, 2026-10-24): xhciinst\ holds the
# installer's window (xhciwiz.c, xhciwiz.rc) beside wiz_lay.c, which the
# suite compiles, and a change to the window alone costs a rebuild of the
# installer (xhciinst\build.cmd, which the cut's staleness check asks for),
# not of the drivers. A file the suite starts to compile changes
# test\run-host-tests.cmd, and a header newly included changes the file
# that includes it, so neither escapes the rule. What the
# record's commit also stood for - the INFs and the packaging scripts the
# package is staged with - is read again by every run that stages: the INF
# gates and the package's own gate run under -SignOnly too.
$script:BuildInputExtensions = @(".c", ".h", ".rc", ".asm", ".def", ".mc", ".inc")
$script:BuildInputNames = @("sources", "makefile", "makefile.inc", "dirs")
$script:BuildInputPrefixes = @(
    "test\",
    "scripts\import-gate\",
    "src\stor98\src\",
    "src\stor98\include\",
    "src\stor98\scripts\",
    "src\stor98\tests\",
    "src\stor98\third_party\"
)
$script:BuildInputScripts = @(
    "scripts\build-driver.cmd",
    "scripts\build-all.cmd",
    "scripts\source-stamp.ps1",
    "scripts\check-flavour-marker.ps1",
    "scripts\check-helper-build.ps1",
    "scripts\check-stor98-build.ps1",
    "scripts\check-stor98-header.ps1",
    "scripts\common.ps1"
)
$script:StampDrivers = @("xhci98", "xhciuas", "xhcicm", "xhcihlpr", "stor98", "stor98mpd")

function Get-BuildInputRules {
    # The rules Test-BuildInputPath applies, read from the tree at $Repo:
    # Exact (repository-relative paths, backslashed), Dirs (the directories
    # whose build-kind files count whatever their name) and Prefixes.
    param([string]$Repo)
    $exact = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $dirs = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $stamp = Join-Path $Repo "scripts\source-stamp.ps1"
    foreach ($d in $script:StampDrivers) {
        $list = @(& $stamp -Driver $d -List)
        if ($list.Count -eq 0) { throw "scripts\source-stamp.ps1 -Driver $d -List named no file." }
        foreach ($p in $list) {
            [void]$exact.Add([string]$p)
            [void]$dirs.Add((Split-Path -Parent ([string]$p)))
        }
    }
    $root = [System.IO.Path]::GetFullPath($Repo).TrimEnd('\') + '\'
    $hostSuite = Join-Path $Repo "test\run-host-tests.cmd"
    # The host suite's files by name, then every quoted #include of them
    # and of test\*.c, resolved beside the including file, to its end.
    $queue = New-Object System.Collections.Generic.Queue[string]
    foreach ($m in [regex]::Matches([System.IO.File]::ReadAllText($hostSuite), '\.\.\\[^\s"]+')) {
        $full = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $Repo "test") $m.Value))
        if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
        $queue.Enqueue($full)
    }
    foreach ($t in @(Get-ChildItem -LiteralPath (Join-Path $Repo "test") -Filter "*.c" -File)) {
        $queue.Enqueue($t.FullName)
    }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count -gt 0) {
        $full = $queue.Dequeue()
        if (-not $seen.Add($full)) { continue }
        [void]$exact.Add($full.Substring($root.Length))
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
        $here = Split-Path -Parent $full
        foreach ($inc in [regex]::Matches([System.IO.File]::ReadAllText($full), '(?m)^\s*#\s*include\s+"([^"]+)"')) {
            $f = [System.IO.Path]::GetFullPath((Join-Path $here ($inc.Groups[1].Value.Replace('/', '\'))))
            if (-not $f.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
            $queue.Enqueue($f)
        }
    }
    foreach ($s in $script:BuildInputScripts) { [void]$exact.Add($s) }
    return @{ Exact = $exact; Dirs = $dirs; Prefixes = $script:BuildInputPrefixes }
}

function Test-BuildInputPath {
    # Whether one repository-relative path (either separator) is a build
    # input under $Rules (Get-BuildInputRules).
    param($Rules, [string]$Path)
    $p = $Path.Replace('/', '\')
    if ($Rules.Exact.Contains($p)) { return $true }
    foreach ($pre in $Rules.Prefixes) {
        if ($p.StartsWith($pre, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    $leaf = Split-Path -Leaf $p
    if ($Rules.Dirs.Contains((Split-Path -Parent $p)) -and
        ($script:BuildInputExtensions -contains [System.IO.Path]::GetExtension($leaf).ToLowerInvariant() -or
         $script:BuildInputNames -contains $leaf.ToLowerInvariant())) {
        return $true
    }
    return $false
}

function Get-GatedHeadAdvance {
    # Whether HEAD ($To) may stand in for the commit a gated build recorded
    # ($From): Fault is "" when $From is an ancestor of $To and no file
    # changed between them is a build input, else why not. Also returns
    # From, To, Commits (how many $To is ahead) and Changed (every file the
    # two differ in, for the caller to print). It reads the commits alone; a
    # tree with uncommitted changes is the caller's to refuse. -Rules is the
    # self-tests', whose fixture repository has no tree to read them from.
    param([string]$Repo, [string]$From, [string]$To, $Rules = $null)
    $r = @{ From = $From.ToLowerInvariant(); To = $To.ToLowerInvariant(); Fault = ""; Commits = 0; Changed = @() }
    $ancestorRc = -1
    $diffRc = -1
    $countRc = -1
    $count = ""
    $names = @()
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & git -C $Repo merge-base --is-ancestor $From $To 2>$null | Out-Null
        $ancestorRc = $LASTEXITCODE
        if ($ancestorRc -eq 0) {
            $names = @(& git -C $Repo -c core.quotePath=false diff --name-only --no-renames $From $To 2>$null)
            $diffRc = $LASTEXITCODE
            $count = (& git -C $Repo rev-list --count "$From..$To" 2>$null | Out-String).Trim()
            $countRc = $LASTEXITCODE
        }
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if ($ancestorRc -eq 1) {
        $r.Fault = "commit $From is not an ancestor of $To, so no diff between them says what changed since the build"
        return $r
    }
    if ($ancestorRc -ne 0 -or $diffRc -ne 0 -or $countRc -ne 0 -or $count -notmatch '^\d+$') {
        $r.Fault = "git could not say whether commit $From is an ancestor of $To and what changed between them"
        return $r
    }
    $r.Commits = [int]$count
    $r.Changed = @($names | Where-Object { $_ -ne "" })
    if ($null -eq $Rules) { $Rules = Get-BuildInputRules -Repo $Repo }
    $hits = @($r.Changed | Where-Object { Test-BuildInputPath -Rules $Rules -Path $_ })
    if ($hits.Count -gt 0) {
        $shown = @($hits | Select-Object -First 8) -join ", "
        if ($hits.Count -gt 8) { $shown += (", and {0} more" -f ($hits.Count - 8)) }
        $r.Fault = ("{0} of the {1} file(s) changed in the {2} commit(s) since are build inputs ({3}), so the build's gates did not read the tree as it is" -f `
                    $hits.Count, $r.Changed.Count, $r.Commits, $shown)
    }
    return $r
}

# ------------------------------------------------------------------
# The cut's amd64 build taken from a second worktree (roadmap-usb4 task 44.31)
# ------------------------------------------------------------------
#
# A cut's two gated builds run at once in two worktrees of this repository
# at one commit: the x86 build in the cut's tree, the amd64 build in a
# second (docs\contributing\build-and-test.md, "The cut's two builds at
# once"). Neither the source stamps nor the gated record name a path, so
# the second tree's amd64 output directories and records are copied into
# the cut's tree whole, and every later step - make-release.ps1 -CheckOnly,
# the cut with every gate, make-package.ps1 -SignOnly - reads them there as
# if the cut's tree had built amd64 itself. Each record is held, before the
# copy against the second tree's files and after it against the copies, to
# the same comparison -SignOnly makes (Get-GatedManifestFault), and written
# into the cut's tree last, byte for byte, so a failure part-way leaves no
# record that admits anything. Like the record itself this protects against
# a mismatch by accident, not against someone who forges one.
$script:ImportArch = "amd64"
$script:ImportObjDirs = [ordered]@{ "release" = "objfre"; "debug" = "objchk"; "qemu" = "objchk_qemu" }
$script:ImportStor98Dir = "src\stor98\out\free\amd64"

function Get-GatedImportDirs {
    # The directories an amd64 build of one flavour writes and the packager
    # reads, relative to a tree's root.
    param([string]$Flavor)
    $obj = $script:ImportObjDirs[$Flavor]
    return @(("src\{0}\amd64" -f $obj), ("src\uas\{0}\amd64" -f $obj), ("src\cm\{0}\amd64" -f $obj))
}

function Get-GatedImportBinaries {
    # Each binary an amd64 record of one flavour names, as a path under $Root
    # (write-gated-manifest.ps1 -Finish's expected set for amd64).
    param([string]$Root, [string]$Flavor)
    $obj = $script:ImportObjDirs[$Flavor]
    return @{
        "xhci98.sys"  = (Join-Path $Root ("src\{0}\amd64\xhci98.sys" -f $obj))
        "xhciuas.sys" = (Join-Path $Root ("src\uas\{0}\amd64\xhciuas.sys" -f $obj))
        "xhcicm.sys"  = (Join-Path $Root ("src\cm\{0}\amd64\xhcicm.sys" -f $obj))
        "stor98.sys"  = (Join-Path $Root ($script:ImportStor98Dir + "\stor98.sys"))
    }
}

function Get-GitWorktreeIdentity {
    # A tree's top directory and its repository's common git directory, both
    # absolute and normalised, or $null when git cannot say.
    param([string]$Path)
    $top = ""
    $common = ""
    $topRc = -1
    $commonRc = -1
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $top = (& git -C $Path rev-parse --path-format=absolute --show-toplevel 2>$null | Out-String).Trim()
        $topRc = $LASTEXITCODE
        $common = (& git -C $Path rev-parse --path-format=absolute --git-common-dir 2>$null | Out-String).Trim()
        $commonRc = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if ($topRc -ne 0 -or $commonRc -ne 0 -or $top -eq "" -or $common -eq "") { return $null }
    return @{ Top = [System.IO.Path]::GetFullPath($top.Replace('/', '\')).TrimEnd('\')
              Common = [System.IO.Path]::GetFullPath($common.Replace('/', '\')).TrimEnd('\') }
}

function Resolve-TreePath {
    # $Path absolute, a relative one taken against PowerShell's location as
    # the caller sees it (not the process's working directory, which
    # Set-Location does not move), with no trailing separator.
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) { $Path = (Resolve-Path -LiteralPath $Path).ProviderPath }
    elseif (-not [System.IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $PWD.ProviderPath $Path }
    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Get-GatedImportTreeFault {
    # "" when $From is the top of another worktree of $Repo's repository,
    # else why not. "Another" is read from the files, not the names: a file
    # written into $Repo's out\ must not appear in $From's, which catches a
    # junction to $Repo or another spelling of its path.
    param([string]$Repo, [string]$From)
    if (-not (Test-Path -LiteralPath $From -PathType Container)) { return "there is no directory '$From'" }
    $a = Get-GitWorktreeIdentity -Path $Repo
    $b = Get-GitWorktreeIdentity -Path $From
    if ($null -eq $a) { return "git could not say what tree '$Repo' is" }
    if ($null -eq $b) { return "'$From' is not a git worktree" }
    if (-not [string]::Equals($b.Top, (Resolve-TreePath $From), [System.StringComparison]::OrdinalIgnoreCase)) {
        return "'$From' is not the top of a worktree (that is '$($b.Top)')"
    }
    if ([string]::Equals($a.Top, $b.Top, [System.StringComparison]::OrdinalIgnoreCase)) {
        return "'$From' is this tree itself"
    }
    if (-not [string]::Equals($a.Common, $b.Common, [System.StringComparison]::OrdinalIgnoreCase)) {
        return "'$From' is a worktree of another repository ($($b.Common), not $($a.Common))"
    }
    # out\ holds the probe, the locks and the records this deletes and
    # writes, so in neither tree may it be, or lie under, a link.
    foreach ($t in @($Repo, $From)) {
        $rp = Get-ReparseOnPath -Root $t -Rel "out"
        if ($rp -ne "") { return "'$rp' is a junction or a symbolic link, and the import writes and deletes in a tree's own out\ only" }
    }
    Ensure-Directory (Join-Path $Repo "out")
    $probe = "import-probe-" + [System.IO.Path]::GetRandomFileName() + ".tmp"
    $mine = Join-Path $Repo ("out\" + $probe)
    [System.IO.File]::WriteAllText($mine, "")
    try {
        if (Test-Path -LiteralPath (Join-Path $From ("out\" + $probe))) { return "'$From' is this tree itself, under another name" }
    } finally {
        Remove-Item -LiteralPath $mine -Force -ErrorAction SilentlyContinue
    }
    return ""
}

function Get-ReparseOnPath {
    # The first existing directory from $Root down to $Root\$Rel (both
    # included) that is a junction or a symbolic link, or "" when none is.
    param([string]$Root, [string]$Rel)
    $p = $Root
    $parts = @("") + @($Rel.Split('\') | Where-Object { $_ -ne "" })
    foreach ($part in $parts) {
        if ($part -ne "") { $p = Join-Path $p $part }
        if (-not (Test-Path -LiteralPath $p)) { break }
        $item = Get-Item -LiteralPath $p -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { return $p }
    }
    return ""
}

function Get-ReparseUnder {
    # The first file or directory under $Dir that is a junction or a
    # symbolic link, or "" when none is; the walk never enters one. $Dir
    # itself is Get-ReparseOnPath's. "" when $Dir is not there.
    param([string]$Dir)
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return "" }
    $stack = New-Object System.Collections.Stack
    $stack.Push((New-Object System.IO.DirectoryInfo $Dir))
    while ($stack.Count -gt 0) {
        $d = $stack.Pop()
        foreach ($e in $d.GetFileSystemInfos()) {
            if (($e.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { return $e.FullName }
            if ($e -is [System.IO.DirectoryInfo]) { $stack.Push($e) }
        }
    }
    return ""
}

function Get-GatedRunFault {
    # "" when no build-driver.cmd run of $Arch holds $Root's lock or has a
    # record pending there, else what does.
    param([string]$Root, [string]$Arch, [string[]]$Flavor)
    $lock = Join-Path $Root ("out\gated-{0}.lock" -f $Arch)
    if (Test-Path -LiteralPath $lock) {
        return "'$lock' is held: a build-driver.cmd $Arch run or an import is in progress there, or one was stopped and left it (delete it when none is running)"
    }
    foreach ($f in $Flavor) {
        $pending = [System.IO.Path]::ChangeExtension((Get-GatedManifestPath -Repo $Root -Arch $Arch -Flavor $f), ".pending")
        if (Test-Path -LiteralPath $pending) { return "'$pending' is there: the $f $Arch build in that tree did not finish" }
    }
    return ""
}

function Enter-GatedLock {
    # Takes $Root's out\gated-<arch>.lock as build-driver.cmd does, with
    # mkdir, which fails when the directory is there; $true when taken. Its
    # run.txt names $Owner, not a build-driver.cmd run's token, so
    # write-gated-manifest.ps1 refuses to record anything under it.
    # -BeforeMarker is the self-tests', run between the two.
    param([string]$Root, [string]$Arch, [string]$Owner = "import", [scriptblock]$BeforeMarker = $null)
    Ensure-Directory (Join-Path $Root "out")
    $lock = Join-Path $Root ("out\gated-{0}.lock" -f $Arch)
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & cmd.exe /d /c ('mkdir "{0}" 2>nul' -f $lock) | Out-Null
        $rc = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if ($rc -ne 0) { return $false }
    # The lock is this caller's from the mkdir on; if its marker cannot be
    # written it is given up here, since no caller has it to drop yet.
    try {
        if ($null -ne $BeforeMarker) { & $BeforeMarker }
        [System.IO.File]::WriteAllText((Join-Path $lock "run.txt"), ($Owner + "`r`n"))
    } catch {
        $why = $_.Exception.Message
        Remove-Item -LiteralPath $lock -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $lock) {
            throw "'$lock' was taken and its run.txt could not be written ($why), and the lock could not be removed again: delete it by hand."
        }
        throw "'$lock' was taken and its run.txt could not be written ($why); the lock was removed again."
    }
    return $true
}

function Import-GatedBuild {
    # Copies the amd64 build of each flavour in $Flavor from the worktree
    # $From into the same directories of the tree $Repo, with its records,
    # under the rules above. With no -Flavor: release and debug, and qemu
    # where $From has its record. Throws with every reason when it refuses;
    # returns one object per flavour (Flavor, Commit, Time, Advance, Sha256,
    # Record). -RepoLockHeld: the caller (cut-builds.ps1) already holds
    # $Repo's amd64 lock, taken with Enter-GatedLock -Owner cut-builds, and
    # keeps it; it is checked to be there and is not dropped here. -Rules
    # -AfterCopy and -BeforeWrite are the self-tests': the build-input
    # rules for a fixture repository, a step run between the copy and its
    # check, and one run with each flavour before its record is written.
    param([string]$Repo, [string]$From, [string[]]$Flavor = @(), [bool]$NoTargetEvidence = $false,
          [switch]$RepoLockHeld, $Rules = $null, [scriptblock]$AfterCopy = $null, [scriptblock]$BeforeWrite = $null)
    $arch = $script:ImportArch
    $Repo = Resolve-TreePath $Repo
    $From = Resolve-TreePath $From
    $why = Get-GatedImportTreeFault -Repo $Repo -From $From
    if ($why -ne "") { throw "the amd64 build cannot be imported: $why." }
    if ($Flavor.Count -eq 0) {
        $Flavor = @("release", "debug")
        if (Test-Path -LiteralPath (Get-GatedManifestPath -Repo $From -Arch $arch -Flavor "qemu") -PathType Leaf) { $Flavor += "qemu" }
    }
    foreach ($f in $Flavor) {
        if (-not $script:ImportObjDirs.Contains($f)) { throw "'$f' is not a flavour (release, debug or qemu)." }
    }
    $dirs = @()
    foreach ($f in $Flavor) { $dirs += Get-GatedImportDirs -Flavor $f }
    $dirs += $script:ImportStor98Dir
    foreach ($rel in $dirs) {
        foreach ($t in @($From, $Repo)) {
            $rp = Get-ReparseOnPath -Root $t -Rel $rel
            if ($rp -eq "") { $rp = Get-ReparseUnder -Dir (Join-Path $t $rel) }
            if ($rp -ne "") { throw "the amd64 build cannot be imported: '$rp' is a junction or a symbolic link, and the import deletes and copies '$rel' only in a tree's own directories." }
        }
    }
    $held = @()
    try {
        $toTake = @($From)
        if ($RepoLockHeld) {
            $mine = Join-Path $Repo ("out\gated-{0}.lock\run.txt" -f $arch)
            if (-not (Test-Path -LiteralPath $mine -PathType Leaf) -or
                [System.IO.File]::ReadAllText($mine).Trim() -ne "cut-builds") {
                throw "the amd64 build cannot be imported: -RepoLockHeld, and '$Repo' holds no amd64 lock of cut-builds.ps1's."
            }
        } else {
            $toTake += $Repo
        }
        foreach ($t in $toTake) {
            if (-not (Enter-GatedLock -Root $t -Arch $arch)) {
                throw ("the amd64 build cannot be imported: {0}." -f (Get-GatedRunFault -Root $t -Arch $arch -Flavor @()))
            }
            $held += Join-Path $t ("out\gated-{0}.lock" -f $arch)
        }
        # Every flavour's, whichever are imported: a stopped qemu build
        # leaves its .pending and no record.
        foreach ($f in $script:ImportObjDirs.Keys) {
            $pending = [System.IO.Path]::ChangeExtension((Get-GatedManifestPath -Repo $From -Arch $arch -Flavor $f), ".pending")
            if (Test-Path -LiteralPath $pending) { throw "the amd64 build cannot be imported: '$pending' is there: the $f amd64 build in that tree did not finish." }
        }
        $tree = Get-GitTreeState -Repo $Repo
        if ($tree.Head -eq "none") { throw "the amd64 build cannot be imported: git could not say what '$Repo' is at." }
        if ($tree.Dirty) {
            throw "the amd64 build cannot be imported: '$Repo' has uncommitted changes, so no record could be admitted there (commit them first)."
        }

        # Every record read once, as bytes, and judged against $From's files.
        $records = @()
        $faults = @()
        $advances = @{}
        foreach ($f in $Flavor) {
            $src = Get-GatedManifestPath -Repo $From -Arch $arch -Flavor $f
            if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
                $faults += ("{0}: no gated build record at '{1}' (scripts\build-driver.cmd {0} -amd64 writes it there)" -f $f, $src)
                continue
            }
            $bytes = [System.IO.File]::ReadAllBytes($src)
            try {
                $m = ConvertFrom-GatedManifest -Text ([System.Text.Encoding]::ASCII.GetString($bytes))
            } catch {
                $faults += ("{0}: '{1}' is not a gated build record this script can read: {2}" -f $f, $src, $_.Exception.Message)
                continue
            }
            $advance = $null
            if ($m.Commit -ne $tree.Head.ToLowerInvariant()) {
                if (-not $advances.ContainsKey($m.Commit)) {
                    $advances[$m.Commit] = Get-GatedHeadAdvance -Repo $Repo -From $m.Commit -To $tree.Head -Rules $Rules
                }
                $advance = $advances[$m.Commit]
            }
            $bins = Get-GatedImportBinaries -Root $From -Flavor $f
            $extra = @($m.Sha256.Keys | Where-Object { -not $bins.ContainsKey($_) })
            if ($extra.Count -ne 0) {
                $faults += ("{0}: '{1}' records {2}, which an amd64 build does not make" -f $f, $src, ($extra -join ", "))
                continue
            }
            $why = Get-GatedManifestFault -Manifest $m -Arch $arch -Flavor $f -Binaries $bins `
                -Head $tree.Head -Dirty $false -NoTargetEvidence $NoTargetEvidence -Advance $advance
            if ($why -ne "") {
                $faults += ("{0}: '{1}' does not admit its build: {2}" -f $f, $src, $why)
                continue
            }
            $records += [pscustomobject]@{ Flavor = $f; Bytes = $bytes; Manifest = $m; Advance = $advance }
        }
        if ($faults.Count -ne 0) {
            throw ("the amd64 build in '{0}' cannot be imported, and nothing in '{1}' was changed:`n  {2}" -f $From, $Repo, ($faults -join "`n  "))
        }

        # Every amd64 record in $Repo goes first, whichever flavours are
        # imported: Stor98's directory, replaced below, is in each of them.
        foreach ($f in $script:ImportObjDirs.Keys) {
            $dst = Get-GatedManifestPath -Repo $Repo -Arch $arch -Flavor $f
            if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }
        }
        foreach ($rel in $dirs) {
            $s = Join-Path $From $rel
            $d = Join-Path $Repo $rel
            if (-not (Test-Path -LiteralPath $s -PathType Container)) { throw "the amd64 build in '$From' has no '$rel'; no amd64 record was written." }
            if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
            Ensure-Directory (Split-Path -Parent $d)
            Copy-Item -LiteralPath $s -Destination $d -Recurse -Force
        }
        if ($null -ne $AfterCopy) { & $AfterCopy }

        # The copies, held to the records again, against this tree as it is now.
        $now = Get-GitTreeState -Repo $Repo
        $faults = @()
        foreach ($r in $records) {
            $why = Get-GatedManifestFault -Manifest $r.Manifest -Arch $arch -Flavor $r.Flavor `
                -Binaries (Get-GatedImportBinaries -Root $Repo -Flavor $r.Flavor) `
                -Head $now.Head -Dirty $now.Dirty -NoTargetEvidence $NoTargetEvidence -Advance $r.Advance
            if ($why -ne "") { $faults += ("{0}: {1}" -f $r.Flavor, $why) }
        }
        if ($faults.Count -ne 0) {
            throw ("the amd64 build was copied from '{0}', and the copies in '{1}' are not what its records admitted; no amd64 record was written:`n  {2}" -f $From, $Repo, ($faults -join "`n  "))
        }
        $out = @()
        $written = @()
        try {
            foreach ($r in $records) {
                $dst = Get-GatedManifestPath -Repo $Repo -Arch $arch -Flavor $r.Flavor
                if ($null -ne $BeforeWrite) { & $BeforeWrite $r.Flavor }
                $tmp = $dst + ".tmp"
                [System.IO.File]::WriteAllBytes($tmp, $r.Bytes)
                Move-Item -LiteralPath $tmp -Destination $dst -Force
                $written += $dst
                $out += [pscustomobject]@{ Flavor = $r.Flavor; Commit = $r.Manifest.Commit; Time = $r.Manifest.Time
                                           Advance = $r.Advance; Sha256 = $r.Manifest.Sha256; Record = $dst }
            }
        } catch {
            foreach ($w in $written) { Remove-Item -LiteralPath $w -Force -ErrorAction SilentlyContinue }
            throw ("writing the amd64 records into '{0}' failed, and every one written was deleted again: {1}" -f $Repo, $_.Exception.Message)
        }
        return $out
    } finally {
        foreach ($l in $held) { Remove-Item -LiteralPath $l -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

function Start-CutBuild {
    # One build in $Tree, its output in out\cut-build-<arch>.log and TEMP and
    # TMP its own out\tmp-<arch>\; returns the build (Arch, Process, Log).
    # $Command is the cmd.exe command line (Get-CutBuildCommand's, or the
    # self-tests' stand-in). The process is added to $Started before
    # anything else can fail, so the caller's cleanup waits for it.
    param([string]$Tree, [string]$Arch, [string]$Command, [System.Collections.ArrayList]$Started)
    $tmp = Join-Path $Tree ("out\tmp-" + $Arch)
    Ensure-Directory $tmp
    $log = Join-Path $Tree ("out\cut-build-" + $Arch + ".log")
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $env:SystemRoot "System32\cmd.exe"
    $psi.Arguments = ('/d /c "{0} > "{1}" 2>&1"' -f $Command, $log)
    $psi.WorkingDirectory = $Tree
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables["TEMP"] = $tmp
    $psi.EnvironmentVariables["TMP"] = $tmp
    $p = [System.Diagnostics.Process]::Start($psi)
    [void]$Started.Add($p)
    Write-Host ("  {0}: {1} in {2} (PID {3}), log {4}" -f $Arch, $Command, $Tree, $p.Id, $log)
    return [pscustomobject]@{ Arch = $Arch; Process = $p; Log = $log }
}

function Get-CutBuildCommand {
    # The build each tree runs: build-driver.cmd all, or all -amd64.
    param([string]$Tree, [string]$Arch)
    $c = '"{0}" all' -f (Join-Path $Tree "scripts\build-driver.cmd")
    if ($Arch -eq "amd64") { $c += " -amd64" }
    return $c
}

function Wait-CutBuild {
    # Waits for one build; its exit code and its own run time, from its
    # process's start to its end (not to when this caller got to it).
    param($Build)
    $Build.Process.WaitForExit()
    $Build | Add-Member -NotePropertyName ExitCode -NotePropertyValue $Build.Process.ExitCode
    $Build | Add-Member -NotePropertyName Elapsed -NotePropertyValue ($Build.Process.ExitTime - $Build.Process.StartTime)
    return $Build
}

function Format-CutSpan {
    param([TimeSpan]$Span)
    return ("{0}:{1:00}:{2:00}" -f [int][Math]::Floor($Span.TotalHours), $Span.Minutes, $Span.Seconds)
}

function Invoke-CutBuilds {
    # scripts\package\cut-builds.ps1's work (its help, and build-and-test.md,
    # "The cut's two builds at once"): the checks of the two trees, the two
    # reservation locks, the two builds at once (or -Serial) and the import,
    # or the import alone (-ImportOnly). Throws when it refuses or a build
    # fails; returns Builds (Arch, ExitCode, Elapsed, Log), Wall and
    # Imported. Its locks are dropped only once every build it started has
    # ended. -CommandFor (a scriptblock of Tree and Arch giving a cmd.exe
    # command line), -BeforeStart (run with Arch before each start),
    # -NoToolchainCheck and -Rules are the self-tests'.
    param([string]$Repo, [string]$X64Tree, [switch]$Serial, [switch]$ImportOnly, [switch]$NoTargetEvidence,
          [scriptblock]$CommandFor = $null, [scriptblock]$BeforeStart = $null, [switch]$NoToolchainCheck,
          $Rules = $null)
    if ($Serial -and $ImportOnly) { throw "-Serial and -ImportOnly contradict each other: -ImportOnly builds nothing." }
    if ($NoTargetEvidence -and -not $ImportOnly) {
        throw "-NoTargetEvidence goes with -ImportOnly alone: the x86 build a cut takes needs the target evidence (the helper's import gate), so build without it, or build by hand and import with -ImportOnly."
    }
    if (-not (Test-Path -LiteralPath $X64Tree -PathType Container)) { throw "-X64Tree: there is no directory '$X64Tree'." }
    $Repo = Resolve-TreePath $Repo
    $X64Tree = Resolve-TreePath $X64Tree
    $why = Get-GatedImportTreeFault -Repo $Repo -From $X64Tree
    if ($why -ne "") { throw "-X64Tree: $why." }
    if ($null -eq $CommandFor) { $CommandFor = { param($Tree, $Arch) Get-CutBuildCommand -Tree $Tree -Arch $Arch } }

    $reserved = @()
    $started = New-Object System.Collections.ArrayList
    $result = [pscustomobject]@{ Builds = @(); Wall = [TimeSpan]::Zero; Imported = @() }
    try {
        if (-not $ImportOnly) {
            Write-Step "the two trees, before any build"
            $a = Get-GitTreeState -Repo $Repo
            $b = Get-GitTreeState -Repo $X64Tree
            $faults = @()
            if ($a.Head -eq "none" -or $b.Head -eq "none") { $faults += "git could not say what one of the trees is at" }
            if ($a.Dirty) { $faults += "'$Repo' has uncommitted changes" }
            if ($b.Dirty) { $faults += "'$X64Tree' has uncommitted changes" }
            if ($a.Head -ne $b.Head) {
                $faults += ("'{0}' is at {1} and '{2}' at {3}: move it with git -C ""{2}"" checkout --detach {1}" -f $Repo, $a.Head, $X64Tree, $b.Head)
            }
            # Either architecture's lock in either tree: a build of the other
            # architecture in the same tree is the collision two trees avoid.
            foreach ($t in @($Repo, $X64Tree)) {
                foreach ($ar in @("x86", "amd64")) {
                    $w = Get-GatedRunFault -Root $t -Arch $ar -Flavor @($script:ImportObjDirs.Keys)
                    if ($w -ne "") { $faults += $w }
                }
            }
            if (-not $NoToolchainCheck) {
                foreach ($t in @($Repo, $X64Tree)) {
                    if (-not (Test-Path -LiteralPath (Join-Path $t "tools\WinDDK71\bin\setenv.bat") -PathType Leaf)) {
                        $faults += "'$t' has no tools\WinDDK71 (junction the toolchain into its tools\)"
                    }
                }
            }
            if ($faults.Count -ne 0) { throw ("the builds were not started:`n  " + ($faults -join "`n  ")) }
            Write-Ok ("both trees clean at {0}" -f $a.Head)

            # The two locks the builds do not take, held until every build
            # started here has ended: no build of the other architecture can
            # start beside either.
            foreach ($c in @(@{ Tree = $Repo; Arch = "amd64" }, @{ Tree = $X64Tree; Arch = "x86" })) {
                if (-not (Enter-GatedLock -Root $c.Tree -Arch $c.Arch -Owner "cut-builds")) {
                    throw ("the builds were not started: {0}." -f (Get-GatedRunFault -Root $c.Tree -Arch $c.Arch -Flavor @()))
                }
                $reserved += Join-Path $c.Tree ("out\gated-{0}.lock" -f $c.Arch)
            }

            $wall = [System.Diagnostics.Stopwatch]::StartNew()
            $plan = @(@{ Tree = $Repo; Arch = "x86" }, @{ Tree = $X64Tree; Arch = "amd64" })
            $done = @()
            if ($Serial) {
                Write-Step "the two builds, one after the other (-Serial)"
                foreach ($s in $plan) {
                    if ($null -ne $BeforeStart) { & $BeforeStart $s.Arch }
                    $done += Wait-CutBuild (Start-CutBuild -Tree $s.Tree -Arch $s.Arch -Command (& $CommandFor $s.Tree $s.Arch) -Started $started)
                }
            } else {
                Write-Step "the two builds, at once"
                $running = @()
                foreach ($s in $plan) {
                    if ($null -ne $BeforeStart) { & $BeforeStart $s.Arch }
                    $running += Start-CutBuild -Tree $s.Tree -Arch $s.Arch -Command (& $CommandFor $s.Tree $s.Arch) -Started $started
                }
                foreach ($r in $running) { $done += Wait-CutBuild $r }
            }
            $wall.Stop()
            $result.Builds = $done
            $result.Wall = $wall.Elapsed
            foreach ($d in $done) {
                $line = "{0}: exit {1} after {2} ({3})" -f $d.Arch, $d.ExitCode, (Format-CutSpan $d.Elapsed), $d.Log
                if ($d.ExitCode -eq 0) { Write-Ok $line } else { Write-Err $line }
            }
            Write-Host ("  both: {0} from the first start to the last end" -f (Format-CutSpan $wall.Elapsed))
            $failed = @($done | Where-Object { $_.ExitCode -ne 0 })
            if ($failed.Count -ne 0) {
                throw ("the {0} build failed; nothing was imported. Its log says why." -f (($failed | ForEach-Object { $_.Arch }) -join " and "))
            }
        }

        Write-Step ("the amd64 build of {0}, into this tree" -f $X64Tree)
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $result.Imported = @(Import-GatedBuild -Repo $Repo -From $X64Tree -NoTargetEvidence ([bool]$NoTargetEvidence) `
                                               -RepoLockHeld:(-not $ImportOnly) -Rules $Rules)
        $watch.Stop()
        foreach ($r in $result.Imported) {
            $at = if ($null -eq $r.Advance) { "" } else { (", HEAD {0} commit(s) past it with no build input changed" -f $r.Advance.Commits) }
            Write-Ok ("{0}: the build of commit {1} at {2}{3}, now {4}" -f $r.Flavor, $r.Commit.Substring(0, 8), $r.Time, $at, $r.Record)
            foreach ($n in ($r.Sha256.Keys | Sort-Object)) { Write-Host ("  {0,-13} {1}" -f $n, $r.Sha256[$n]) }
        }
        Write-Host ("  imported in {0}" -f (Format-CutSpan $watch.Elapsed))
        return $result
    } finally {
        # The reservations go only once every build started here has ended:
        # a build still running beside a dropped lock is the collision they
        # exist to prevent. If this wait is itself interrupted, they stay,
        # to be deleted by hand once no build runs.
        foreach ($p in $started) {
            if (-not $p.HasExited) {
                Write-Warn ("waiting for the build (PID {0}) to end before the locks this run holds are dropped" -f $p.Id)
                $p.WaitForExit()
            }
        }
        foreach ($l in $reserved) { Remove-Item -LiteralPath $l -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

# --- NUSB 3.3's 98 SE storage files, from the driver pack (task 43.3) -----
#
# The one exception to "no Microsoft file in the download"
# (docs\contributing\legal-provenance.md section 5, the owner's decision of
# 2026-10-20): the seven files of the driver pack's nusb33/w98se/ folder,
# with that folder's nusb33/NOTICE.md, in the upload set alone, at its root
# as NUSB33\, a folder of its own beside the installer and never inside a
# NONFREE\ (owner, 2026-10-22: NUSB is kept apart from the non-free pack,
# in the pack's repository and in the download), in 8.3 names:
# NUSB33\NOTICE.md, NUSB33\SHA256.TXT (the seven's lines) and
# NUSB33\W98SE\, where the installer takes them from (task 43.4). Never in
# a package of make-package.ps1's and never in releases\. The pack is the owner's
# usb-wdm-nonfree repository, a git
# checkout, and its files are read from the checkout's object store at one
# revision rather than from its working tree, so what is staged is exactly
# the bytes of the revision the release records. Each of the seven is held
# to the pack's SHA256.TXT at that revision and to this allowlist, which is
# legal-provenance.md section 5's table: changing it is a decision recorded
# there first.
# The owner, 2026-10-23: NUSB 3.3's files and both sets under one
# folder, USBDRV\, every level 8.3 (record 26 section 6.7.2).
$script:UsbDrvFolder = "USBDRV"
$script:DriverPackFolder = "USBDRV\NUSB33"
$script:DriverPackStorageRel = "nusb33/w98se"
# Where the seven sit under NUSB33\ in the download.
$script:DriverPackStorageDir = "W98SE"
# The notice, under NUSB33\ in the download, and in the pack.
$script:DriverPackNotice = "NOTICE.md"
$script:DriverPackNoticeRel = "nusb33/NOTICE.md"
$script:DriverPackSums = "SHA256.TXT"
$script:DriverPackRecordName = "drvpack.txt"
$script:DriverPackSeven = [ordered]@{
    "USBAUTH.SYS"  = "12B09E67A5A9A8A883AB40B91C59BE294F2DDC5940B9780C056176DF04A463D1"
    "USBMPHLP.PDR" = "E8E0604FDDCD1772F1C5CBA99DE9D72D79288D0B303ED3E7BF02D36160698C1C"
    "USBNTMAP.INF" = "372D0A4829EAB33CF5C6A1FCA347A3BE45DA82D69A692E89B33D28DC02848657"
    "USBNTMAP.SYS" = "D1595CDBFDC22925C4E47D1720F117FE6C9BEDA36417BA41A1FD7398878CC6E4"
    "USBSTOR.INF"  = "E78B5F2D2D30B039D74D670F09BA41C39BDFCA058B72FD7C22EB7D55F81F0F6B"
    "USBSTOR.SYS"  = "B92A6A1582287867D4950495C0A746F0F5D8831BC311061423F8645073E42D8C"
    "USBU2A.SYS"   = "1C5656DCF3DBBCA97236FC0239537A34AE5A6CA1597F29CADC9C38537B2A7B78"
}

function Get-DefaultDriverPackRoot {
    # A checkout of usb-wdm-nonfree (or a junction to one) in the git-ignored
    # scripts\local\.
    return (Join-Path (Get-DefaultLocalScriptDir) "usb-wdm-nonfree")
}

function Read-DriverPackHashList {
    # A SHA256.TXT ("<64 hex>  <path>" a line) as an ordered map of path to
    # upper-case hash. Anything else on a line that is not blank is refused.
    param([string]$Text, [string]$Label)
    $map = [ordered]@{}
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line.Trim() -eq "") { continue }
        $m = [regex]::Match($line, '^([0-9A-Fa-f]{64}) [ *]?(\S.*?)\s*$')
        if (-not $m.Success) { throw "$Label has a line that is not '<sha256>  <path>': '$line'." }
        if ($map.Contains($m.Groups[2].Value)) { throw "$Label lists '$($m.Groups[2].Value)' twice." }
        $map[$m.Groups[2].Value] = $m.Groups[1].Value.ToUpperInvariant()
    }
    return $map
}

function Read-DriverPackAllowlist {
    # An allowlist in SHA256.TXT form, names only, for the packaging scripts'
    # self-tests (-DriverPackAllowlist), which stage stand-ins.
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "no driver pack allowlist at '$Path'." }
    $map = Read-DriverPackHashList -Text ([System.IO.File]::ReadAllText($Path)) -Label "'$Path'"
    if ($map.Count -eq 0) { throw "'$Path' names no file." }
    return $map
}

function Invoke-DriverPackGit {
    # git in the driver pack's checkout, in a process of its own: its output
    # is returned as text or, with -OutFile, written to a new file byte for
    # byte (a blob through PowerShell's pipeline would be re-encoded).
    param([string]$Root, [string[]]$Arguments, [string]$OutFile = "")
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "git"
    $psi.Arguments = ((@("-C", $Root.TrimEnd('\')) + $Arguments) | ForEach-Object { '"' + $_ + '"' }) -join " "
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $psi.CreateNoWindow = $true
    try {
        $p = [System.Diagnostics.Process]::Start($psi)
    } catch {
        throw "git could not be started ($($_.Exception.Message)); the driver pack is read from its checkout with git."
    }
    try {
        $errTask = $p.StandardError.ReadToEndAsync()
        $text = ""
        if ($OutFile -ne "") {
            $fs = New-Object System.IO.FileStream($OutFile, [System.IO.FileMode]::CreateNew)
            try { $p.StandardOutput.BaseStream.CopyTo($fs) } finally { $fs.Dispose() }
        } else {
            $text = $p.StandardOutput.ReadToEnd()
        }
        $p.WaitForExit()
        $rc = $p.ExitCode
        $err = $errTask.Result.Trim()
    } finally {
        $p.Dispose()
    }
    if ($rc -ne 0) {
        throw "git $($Arguments -join ' ') in '$Root' failed (exit $rc): $err"
    }
    return $text
}

function Export-DriverPack {
    # Writes the download's NUSB33\ folder into -Dest (a new directory): the
    # pack's seven storage files as they are at -Revision in the checkout at
    # -Root, under W98SE\, the pack's nusb33/NOTICE.md as NOTICE.md, and a
    # SHA256.TXT of the seven. The pack's folder is held to the allowlist exactly - the
    # seven names, nothing else, each a plain file - and each file to the
    # pack's SHA256.TXT at that revision and to its allowlisted hash. Returns
    # the revision and an ordered map of each written file's path under
    # -Dest to its hash: what make-release.ps1 records.
    param([string]$Root, [string]$Revision = "HEAD", [string]$Dest, $Allowlist = $null)
    if ($null -eq $Allowlist) { $Allowlist = $script:DriverPackSeven }
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw @"
no driver pack at '$Root'.
The x86 packages carry NUSB 3.3's Windows 98 SE storage files from the owner's
usb-wdm-nonfree repository (roadmap-usb4 task 43.3): clone it there, or make a
junction there to a clone, or name a clone with -DriverPackRoot. A package
without them is built with -NoDriverPack.
"@
    }
    $Root = (Resolve-Path -LiteralPath $Root).ProviderPath.TrimEnd('\')
    $rel = $script:DriverPackStorageRel
    $rev = ""
    try {
        $rev = (Invoke-DriverPackGit -Root $Root -Arguments @("rev-parse", "--verify", "--quiet", "$Revision^{commit}")).Trim()
    } catch {
        $rev = ""
    }
    if ($rev -notmatch '^[0-9a-f]{40}$') {
        throw "the driver pack at '$Root' has no commit '$Revision': it is not a git checkout of usb-wdm-nonfree, or that revision is not in it."
    }
    $tree = Invoke-DriverPackGit -Root $Root -Arguments @("ls-tree", $rev, "$rel/")
    $names = @()
    foreach ($line in @($tree -split "`n" | Where-Object { $_.Trim() -ne "" })) {
        $m = [regex]::Match($line.TrimEnd("`r"), '^(\d{6}) (\w+) [0-9a-f]{40}\t(.+)$')
        if (-not $m.Success) { throw "git ls-tree printed a line this script cannot read: '$line'." }
        $name = $m.Groups[3].Value.Substring($m.Groups[3].Value.LastIndexOf('/') + 1)
        if ($m.Groups[2].Value -ne "blob" -or $m.Groups[1].Value -ne "100644") {
            throw "the driver pack's $rel/$name at $rev is not a plain file (mode $($m.Groups[1].Value), $($m.Groups[2].Value))."
        }
        $names += $name
    }
    $want = @($Allowlist.Keys)
    $extra = @($names | Where-Object { $want -cnotcontains $_ })
    $missing = @($want | Where-Object { $names -cnotcontains $_ })
    if ($extra.Count -gt 0 -or $missing.Count -gt 0) {
        throw @"
the driver pack's $rel/ at $rev is not the allowlisted folder.
  Not allowlisted: $(if ($extra.Count) { $extra -join ', ' } else { '(none)' })
  Missing:         $(if ($missing.Count) { $missing -join ', ' } else { '(none)' })
The folder is staged whole or not at all, and holds exactly these: $($want -join ', ').
Widening it is the owner's decision, recorded in
docs\contributing\legal-provenance.md section 5 before the allowlist changes.
"@
    }
    $sums = Read-DriverPackHashList -Text (Invoke-DriverPackGit -Root $Root -Arguments @("show", "${rev}:SHA256.TXT")) `
                                    -Label "the driver pack's SHA256.TXT at $rev"
    if (Test-Path -LiteralPath $Dest) { throw "'$Dest' already exists; the driver pack is exported into a new directory." }
    $storageDest = Join-Path $Dest $script:DriverPackStorageDir
    New-Item -ItemType Directory -Path $storageDest | Out-Null
    $files = [ordered]@{}
    $sumLines = @()
    foreach ($name in $want) {
        $out = Join-Path $storageDest $name
        Invoke-DriverPackGit -Root $Root -Arguments @("cat-file", "blob", "${rev}:$rel/$name") -OutFile $out | Out-Null
        $hash = Get-FileSha256 -Path $out
        if (-not $sums.Contains("$rel/$name") -or $sums["$rel/$name"] -ne $hash) {
            throw "the driver pack's $rel/$name at $rev does not hash as its SHA256.TXT lists it ($hash)."
        }
        if ($Allowlist[$name] -ne $hash) {
            throw "the driver pack's $rel/$name at $rev is not the allowlisted file: $hash, not $($Allowlist[$name]) (legal-provenance.md section 5)."
        }
        $files[(Join-Path $script:DriverPackStorageDir $name)] = $hash
        $sumLines += ("{0}  {1}/{2}" -f $hash.ToLowerInvariant(), $script:DriverPackStorageDir.Replace('\', '/'), $name)
    }
    $noticeOut = Join-Path $Dest $script:DriverPackNotice
    Invoke-DriverPackGit -Root $Root -Arguments @("cat-file", "blob", "${rev}:$($script:DriverPackNoticeRel)") -OutFile $noticeOut | Out-Null
    $files[$script:DriverPackNotice] = Get-FileSha256 -Path $noticeOut
    # The download's own SHA256.TXT: the seven's hashes at their paths
    # under NUSB33\ (the pack's lists its other folders too).
    $sumsOut = Join-Path $Dest $script:DriverPackSums
    [System.IO.File]::WriteAllText($sumsOut, (($sumLines -join "`r`n") + "`r`n"), (New-Object System.Text.ASCIIEncoding))
    $files[$script:DriverPackSums] = Get-FileSha256 -Path $sumsOut
    return [pscustomobject]@{ Root = $Root; Revision = $rev; Dir = $Dest; Files = $files }
}

function Get-DriverPackFolderFault {
    # "" when -Dir holds exactly the files of -Files (path relative to -Dir
    # to hash) and nothing else; otherwise what is wrong.
    param([string]$Dir, $Files)
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return "'$Dir' does not exist" }
    $full = (Get-Item -LiteralPath $Dir).FullName.TrimEnd('\')
    $names = @(Get-ChildItem -LiteralPath $Dir -Force -File -Recurse | ForEach-Object { $_.FullName.Substring($full.Length + 1) })
    $want = @($Files.Keys)
    $extra = @($names | Where-Object { $want -notcontains $_ })
    if ($extra.Count -gt 0) { return "'$Dir' holds $($extra -join ', '), which the driver pack's folder does not" }
    foreach ($name in $want) {
        $path = Join-Path $Dir $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return "'$Dir' has no $name" }
        $hash = Get-FileSha256 -Path $path
        if ($hash -ne $Files[$name]) { return "'$Dir\$name' is $hash, not $($Files[$name])" }
    }
    return ""
}

function Test-DriverPackRecordRequired {
    # Every release cut from 3.2.0.0 writes drvpack.txt, so its absence from
    # one of those is a damaged tree, never "a release without the pack";
    # only an older release may lack it.
    param([string]$Version)
    return ([version]$Version -ge [version]"3.2.0.0")
}

function Find-DriverPackTrackedNames {
    # The tracked files of the repository at -Repo whose name is one of
    # -Names, read NUL-separated so that git's quoting of a path with an
    # unusual character cannot hide its name.
    param([string]$Repo, [string[]]$Names)
    $listing = Invoke-DriverPackGit -Root $Repo -Arguments @("ls-files", "-z")
    $hits = @()
    foreach ($path in ($listing -split "`0")) {
        if ($path -eq "") { continue }
        $leaf = ($path -split '/')[-1]
        if ($Names -contains $leaf) { $hits += $path }
    }
    return $hits
}

function ConvertTo-DriverPackRecord {
    # The tracked record in releases\<version>\ of whether the download
    # carries the driver pack and, when it does, which revision and bytes:
    # what -UploadSetOnly rebuilds the download from. $Pack is
    # Export-DriverPack's result, or $null for a release without the pack.
    param([string]$Version, $Pack)
    $lines = @(
        "xhci98 $Version - the driver pack in this release's download",
        "Written by scripts\package\make-release.ps1 (roadmap-usb4 task 43.3) and",
        "read back by its -UploadSetOnly. The files are never in this directory.",
        ""
    )
    if ($null -eq $Pack) {
        $lines += "Included: no"
        return $lines
    }
    $lines += "Included: yes"
    $lines += "Repository: usb-wdm-nonfree"
    $lines += "Revision: $($Pack.Revision)"
    $lines += "Folder: $($script:DriverPackFolder)"
    foreach ($name in $Pack.Files.Keys) {
        $lines += "SHA256: $($Pack.Files[$name]) $name"
    }
    return $lines
}

function ConvertFrom-DriverPackRecord {
    # The record ConvertTo-DriverPackRecord writes, read back strictly:
    # Included, and for a release with the pack its revision, folder and
    # files (NOTICE.md among them).
    param([string]$Text, [string]$Label)
    $included = $null
    $revision = ""
    $folder = ""
    $files = [ordered]@{}
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^Included: (yes|no)$') {
            if ($null -ne $included) { throw "$Label says Included twice." }
            $included = ($Matches[1] -eq "yes")
        } elseif ($line -match '^Revision: ([0-9a-f]{40})$') {
            $revision = $Matches[1]
        } elseif ($line -match '^Folder: (\S+)$') {
            $folder = $Matches[1]
        } elseif ($line -match '^SHA256: ([0-9A-F]{64}) (\S+)$') {
            if ($files.Contains($Matches[2])) { throw "$Label lists $($Matches[2]) twice." }
            $files[$Matches[2]] = $Matches[1]
        } elseif ($line -match '^(Included|Revision|Folder|SHA256):') {
            throw "$Label has a line it cannot read: '$line'."
        }
    }
    if ($null -eq $included) { throw "$Label says neither 'Included: yes' nor 'Included: no'." }
    if ($included) {
        if ($revision -eq "") { throw "$Label names no revision." }
        if ($folder -ne $script:DriverPackFolder) { throw "$Label names the folder '$folder', not $($script:DriverPackFolder)." }
        if (-not $files.Contains($script:DriverPackNotice) -or $files.Count -lt 2) {
            throw "$Label does not list the pack's files and $($script:DriverPackNotice)."
        }
    } elseif ($revision -ne "" -or $files.Count -gt 0) {
        throw "$Label says 'Included: no' and still names a revision or files."
    }
    return [pscustomobject]@{ Included = $included; Revision = $revision; Files = $files }
}

# --- a packaged plain-text file's rule (task 43.61) --------------------------
#
# **One rule, read by the cut and by the gated build.** make-release.ps1's
# Write-GeneratedText refuses a generated text with a line past 78 columns or
# a character that is not printable ASCII (TAB allowed), since
# Write-AsciiFile turns such a character into a question mark silently.
# Until task 43.61 that refusal was met only at a cut: on 2026-10-22 43.11's
# test cut stopped on a line in xhciinst\README.TXT and one in
# xhcitr\README.TXT that two merges had left, each passing every gate. The
# rule is here so that scripts\check-readme-text.ps1, which build-driver.cmd
# runs, holds the tracked readmes the cut reads to the same function.
#
# The tool readmes the cut reads, by the tool's directory under the
# repository: make-release.ps1 takes its default -MarkToolDir,
# -InstallToolDir and -TrayToolDir from this table and reads README.TXT in
# each, and the gate reads all four, xhciperf's included (a developer tool
# since 3.2.0.0, not shipped: task 43.50). XHCICLN's was the fifth until the
# tool was dropped in 3.3.0.0 (task 44.82). A further tool's readme is added
# here, or the gate does not see it.
$script:PlainTextColumns = 78
$script:ToolReadmeName = "README.TXT"
$script:ToolReadmeDirs = [ordered]@{
    Mark    = "xhcimark"
    Install = "xhciinst"
    Tray    = "xhcitr"
    Perf    = "xhciperf"
}

function Get-PlainTextFaults {
    # What a packaged plain-text file breaks of the rule above, as the
    # messages the cut prints: none, or the first line past 78 columns (with
    # how many there are) and then the first character that is not printable
    # ASCII. $Name is how the file is named in them.
    param(
        [string]$Name,
        [string[]]$Lines
    )

    $faults = @()
    if ($null -eq $Lines) { $Lines = @() }
    $overLong = @($Lines | Where-Object { $_.Length -gt $script:PlainTextColumns })
    if ($overLong.Count -gt 0) {
        $faults += ("{0} has {1} line(s) past {2} columns, the first being:`n{3}" -f `
                    $Name, $overLong.Count, $script:PlainTextColumns, $overLong[0])
    }

    :lines for ($i = 0; $i -lt $Lines.Count; $i++) {
        $line = $Lines[$i]
        for ($c = 0; $c -lt $line.Length; $c++) {
            $code = [int][char]$line[$c]
            if ($code -lt 0x20 -or $code -gt 0x7E) {
                if ($code -eq 9) { continue }
                $faults += ("{0} line {1} column {2} is not printable ASCII (U+{3:X4}); Write-AsciiFile would replace it with a question mark. The line reads:`n{4}" -f `
                            $Name, ($i + 1), ($c + 1), $code, $line)
                break lines
            }
        }
    }
    return $faults
}

function Read-ToolReadmeLines {
    # A tool's README.TXT as the cut reads it before adding the release's
    # footer: split on CRLF or LF, trailing empty lines dropped.
    param([string]$Path)

    $lines = @([System.IO.File]::ReadAllText($Path) -split "`r?`n")
    while ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -eq "") {
        $lines = @($lines | Select-Object -First ($lines.Count - 1))
    }
    return $lines
}

# --- two claims no published text may make (roadmap Phase 20, F7) ----------
#
# "WINDOWS 98 ONLY" described DisableSelectiveSuspend as a Windows 98
# setting after 1.0.1.0 had made the NT path write it too, and the shipped
# readme contradicted itself; "redistributes nothing" is the sentence
# AGENTS.md forbids, since the tools carry statically linked runtimes.
# test-package.ps1 holds make-release.ps1's source to the list and
# scripts\check-text.ps1 every text the cut publishes (task 44.1), each
# case-insensitively.
$script:ReadmeForbiddenPhrases = @("WINDOWS 98 ONLY", "redistributes nothing")

# --- the changelog entry a cut requires (task 44.1) --------------------------
#
# make-release.ps1 refuses a cut whose version has no single releases\
# history.md heading dated as the INF's DriverVer; scripts\check-text.ps1
# holds the tree to the same function before any build. The messages are
# the cut's.

function Get-ChangelogEntryFaults {
    # The faults of history.md's entry for -Version against -ReleaseDate
    # (yyyy-mm-dd, the INF's): none, or the one the cut stops on. -Label
    # names the file in the messages.
    param([string[]]$Lines, [string]$Version, [string]$ReleaseDate, [string]$Label)

    if ($null -eq $Lines) { $Lines = @() }
    $versionPattern = '^##\s+' + [regex]::Escape($Version) + '(\s|$)'
    $headings = @($Lines | Where-Object { $_ -match $versionPattern })
    if ($headings.Count -eq 0) {
        return @(@"
'$Label' has no entry for $Version.
Add one - a '## $Version - $ReleaseDate' heading and what changed for the
person installing it - then cut the release again. A published version nobody
wrote a changelog entry for is one no user can tell apart from its predecessor.
"@)
    # **Exactly one, and dated, and dated the same as the INF.** All three parts
    # were missing until a later review. The heading match alone accepted a bare
    # "## 0.0.0.2" with no date at all, so the cross-check below silently did
    # nothing; and a second heading for the same version would have made "the"
    # entry ambiguous. The INF is the authority for the version everywhere else
    # in the cut, so it is the authority for the date - the readme prints
    # that date beside this very entry, and the two disagreeing is what this
    # replaced.
    }
    if ($headings.Count -gt 1) {
        return @("'$Label' has $($headings.Count) headings for $Version; a version has one changelog entry.")
    }
    $headingDate = [datetime]::MinValue
    if ($headings[0] -match '^##\s+' + [regex]::Escape($Version) + '\s+-\s+(\S+)\s*$' -and
        -not [datetime]::TryParseExact($Matches[1], 'yyyy-MM-dd',
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::None, [ref]$headingDate)) {
        return @("'$Label' dates $Version as '$($Matches[1])', which is not a calendar date in yyyy-mm-dd form.")
    }
    if ($headings[0] -notmatch '^##\s+' + [regex]::Escape($Version) + '\s+-\s+(\d{4}-\d{2}-\d{2})\s*$') {
        return @(@"
'$Label' heading for $Version is not a dated entry:
  $($headings[0])
Write it as '## $Version - $ReleaseDate'. The generated readme.txt prints the
release date beside this entry, so an undated or differently shaped heading
leaves that date unchecked - which is how a release came to date itself two
different ways in one file.
"@)
    }
    if ($Matches[1] -ne $ReleaseDate) {
        return @(@"
releases\history.md dates $Version as $($Matches[1]), but src\xhci98.inf's
DriverVer dates it $ReleaseDate. A release has one date; the generated
readme.txt prints it beside the history entry, so the two cannot disagree.
"@)
    }
    return @()
}

# --- the release history the top-level readme embeds (task 43.61) -----------
#
# make-release.ps1 embeds releases\history.md in the release's readme.txt,
# from its first '##' heading down, each entry heading demoted one level,
# rendered to plain text by ConvertFrom-MarkdownBlocks at the rule's width.
# The three are here so scripts\check-readme-text.ps1 renders it the same
# way: a word too long to wrap or a non-ASCII character in a history entry
# stops a cut as a readme line does.

function Get-HistoryEmbedLines {
    # history.md's lines from its first '##' heading down, each '##' heading
    # made '###' to sit under the readme's "Release history" section, so the
    # file's own title and maintenance note stay out of the readme.
    param([string[]]$Lines)

    if ($null -eq $Lines -or $Lines.Count -eq 0) { return @() }
    $firstHeading = 0
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match '^##\s') { $firstHeading = $i; break }
    }
    return @($Lines[$firstHeading..($Lines.Count - 1)] | ForEach-Object {
        if ($_ -match '^##\s') { "#" + $_ } else { $_ }
    })
}

function Format-Wrapped {
    # Greedy word wrap with a separate first-line and continuation prefix, so a
    # bullet hangs under its own text rather than under the marker.
    param(
        [string]$Text,
        [int]$Width,
        [string]$FirstPrefix = "",
        [string]$RestPrefix = ""
    )

    $words = @($Text -split '\s+' | Where-Object { $_ -ne "" })
    if ($words.Count -eq 0) { return @() }

    $out = @()
    $line = $FirstPrefix + $words[0]
    for ($i = 1; $i -lt $words.Count; $i++) {
        if (($line + " " + $words[$i]).Length -gt $Width) {
            $out += $line
            $line = $RestPrefix + $words[$i]
        } else {
            $line = $line + " " + $words[$i]
        }
    }
    # @() or a single-line block returns a bare string, and Set-StrictMode 2.0
    # then makes .Count on it a runtime error at the call site.
    return @($out + $line)
}

function ConvertFrom-MarkdownBlocks {
    # The narrow subset releases\history.md actually uses: '###' headings,
    # '-' bullets that may continue on indented lines, and plain paragraphs.
    # Inline '**bold**', '*emphasis*' and `code` are stripped rather than
    # rendered - there is nothing to render them with in a .txt file.
    param([string[]]$Lines, [int]$Width = 78)

    $out = @()
    $pending = ""          # text of the block being accumulated
    $kind = "none"         # none | bullet | para

    function Flush {
        param([string]$Text, [string]$Kind, [int]$W, [object[]]$Acc)
        if ($Text -eq "") { return @($Acc) }
        if ($Kind -eq "bullet") {
            return @(@($Acc) + (Format-Wrapped -Text $Text -Width $W -FirstPrefix "    * " -RestPrefix "      "))
        }
        return @(@($Acc) + (Format-Wrapped -Text $Text -Width $W -FirstPrefix "  " -RestPrefix "  "))
    }

    foreach ($raw in $Lines) {
        $line = $raw -replace '\*\*', '' -replace '`', ''
        # Single-asterisk emphasis, but only around a word - never a bullet
        # marker, which lives at the start of the line and is handled below.
        $line = $line -replace '(?<=\S)\*(\S[^*]*?)\*', '$1' -replace '(?<=\s)\*(\S[^*]*?)\*', '$1'

        if ($line.Trim() -eq "") {
            $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
            $pending = ""; $kind = "none"
            if ($out.Count -gt 0 -and $out[-1] -ne "") { $out += "" }
            continue
        }
        if ($line -match '^###\s+(.*)$') {
            $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
            $pending = ""; $kind = "none"
            if ($out.Count -gt 0 -and $out[-1] -ne "") { $out += "" }
            $out += ("  " + $Matches[1].Trim())
            $out += ""
            continue
        }
        if ($line -match '^\s*-\s+(.*)$') {
            $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
            $pending = $Matches[1].Trim(); $kind = "bullet"
            continue
        }
        # A continuation of whatever block is open, or the start of a paragraph.
        if ($kind -eq "none") { $kind = "para" }
        $pending = if ($pending -eq "") { $line.Trim() } else { $pending + " " + $line.Trim() }
    }
    $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
    return $out
}

# --- the other USB drivers: usb-wdm-free and usb-wdm-nonfree (task 43.48) --
#
# Three release downloads, each a superset of the one before (the owner's
# decision of 2026-10-22; design record 26 section 6.7): download 1 the
# package with NUSB33\ (above), download 2 that with WDMFREE\ (the
# qualifying drivers of a tagged usb-wdm-free release, each file held to a
# SHA-256 of wdmfree-pin.txt beside this file), download 3 that with
# NONFREE\ (the non-free/ folder of a tagged usb-wdm-nonfree release, each
# file held to the pack's SHA256.TXT at the pinned commit, whose own hash
# is pinned here). Neither folder is ever in releases\ or in a package of
# make-package.ps1's. Each folder's SHA256.TXT is written here, one line
# "<sha256, lower case>  <path>" per other file of the folder, '/'
# separated, in ordinal order of the path, CRLF; the installer carries the
# hash of that file for this release's pins (INST_NONFREE_SUMS and
# INST_WDMFREE_SUMS, xhciinst\inst_core.h), which the cut holds equal to
# what it writes.
$script:NonFreeFolder = "USBDRV\NONFREE"
$script:NonFreePrefix = "non-free"
$script:NonFreeExtras = @("NOTICE.md", "README.md", "STAGE.TXT")
$script:NonFreePin = [pscustomobject]@{
    Tag        = "v1.1.0"
    Commit     = "565d11b40fbeb53f5a576c01b5ceba331892c610"
    # The SHA-256 of the pack's own SHA256.TXT blob at that commit.
    PackSums   = "2208E5B4F68DE00938C20DFE6B3520DA22A1AD3CDF7489BAED01F3AF4FDE8D87"
}
# The folders of non-free/ that download 3 leaves out, and their STAGE.TXT
# lines with them. The owner, 2026-10-28: the Wi-Fi drivers stay in
# usb-wdm-nonfree and its own release, where people get them; download 3
# keeps the Ethernet, HID and serial drivers.
$script:NonFreeExcluded = @("wifi")
$script:WdmFreeFolder = "USBDRV\FREE"
$script:WdmFreePrefix = "free"
$script:WdmFreeExtras = @("LICENSE", "NOTICE.md", "STAGE.TXT")
# The release's tools (owner, 2026-10-24): free/tools/<name>/, pinned by a
# "Tool:" line each, carried at USBDRV\FREE\tools\<name>\. Not driver
# packages: no INF and no STAGE.TXT line. The readme's words for each.
$script:WdmFreeToolsDir = "tools"
$script:WdmFreeToolWords = [ordered]@{ "camcheck" = "CamCheck, a webcam preview for the uvc98 driver" }
# The free set's certificate's public half, at the usb-wdm-free release's top
# and at the set's own (USBDRV\FREE\), where the installer looks for it
# (INST_WDMFREE_CER; design record 26 section 6.7.11): pinned by a
# "Certificate:" line (its SHA-1 thumbprint) and its "SHA256:" line.
$script:WdmFreeCer = "usb-wdm-free-test.cer"
$script:OtherSums = "SHA256.TXT"
$script:DownloadsRecordName = "downloads.txt"
# The systems STAGE.TXT and READ.TXT name (usb-wdm-nonfree v1.0.0's
# header, with 8 and 8.1, and 10, added here).
$script:StageSystems = @("W98SE", "WME", "W2K", "WXP", "WXP64", "WVISTA", "WVISTA64",
                         "W7", "W7X64", "W8", "W8X64", "W10", "W10X64")
# The three downloads, by kind: the zip's and the workspace's suffix, and
# the folders each adds to the one before.
$script:DownloadKindTable = [ordered]@{
    "base"    = [pscustomobject]@{ Number = 1; Suffix = "";         Adds = "" }
    "free"    = [pscustomobject]@{ Number = 2; Suffix = "-free";    Adds = "USBDRV\FREE" }
    "nonfree" = [pscustomobject]@{ Number = 3; Suffix = "-nonfree"; Adds = "USBDRV\NONFREE" }
}

function Get-DefaultWdmFreeRoot {
    # A checkout of usb-wdm-free (or a junction to one) in the git-ignored
    # scripts\local\.
    return (Join-Path (Get-DefaultLocalScriptDir) "usb-wdm-free")
}

function Get-DefaultWdmFreePinPath {
    return (Join-Path $PSScriptRoot "wdmfree-pin.txt")
}

function Test-OtherSetRelPath {
    # A path under a set's folder the installer will accept
    # (inst_other_rel_ok): '/' separated, relative, no empty, "." or ".."
    # part, printable ASCII with no character Windows refuses in a name and
    # no ';' (DevicePath's separator), at most 140 characters (design record
    # 26 section 6.7.2).
    param([string]$Rel)
    if ($Rel -eq "" -or $Rel.Length -gt 140) { return $false }
    if ($Rel -notmatch '^[\x20-\x7E]+$') { return $false }
    if ($Rel -match '[\\:*?"<>|;]') { return $false }
    foreach ($part in ($Rel -split '/')) {
        if ($part -eq "" -or $part -eq "." -or $part -eq ".." -or $part.EndsWith(" ") -or $part.EndsWith(".") -or $part.StartsWith(" ")) {
            return $false
        }
    }
    return $true
}

function Get-OtherSetBlob {
    # One blob of the checkout at -Root, at -Rev, into -OutFile (a new file),
    # and its SHA-256.
    param([string]$Root, [string]$Rev, [string]$Path, [string]$OutFile)
    $parent = Split-Path -Parent $OutFile
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Invoke-DriverPackGit -Root $Root -Arguments @("cat-file", "blob", "${Rev}:$Path") -OutFile $OutFile | Out-Null
    return (Get-FileSha256 -Path $OutFile)
}

function Get-OtherSetTree {
    # The files under -Prefix/ at -Rev: an ordered map of path to "mode type".
    param([string]$Root, [string]$Rev, [string]$Prefix)
    $tree = Invoke-DriverPackGit -Root $Root -Arguments @("ls-tree", "-r", $Rev, "$Prefix/")
    $map = [ordered]@{}
    foreach ($line in @($tree -split "`n" | Where-Object { $_.Trim() -ne "" })) {
        $m = [regex]::Match($line.TrimEnd("`r"), '^(\d{6}) (\w+) [0-9a-f]{40}\t(.+)$')
        if (-not $m.Success) { throw "git ls-tree printed a line this script cannot read: '$line'." }
        $map[$m.Groups[3].Value] = ($m.Groups[1].Value + " " + $m.Groups[2].Value)
    }
    return $map
}

function Resolve-OtherSetTag {
    # The commit -Tag names in the checkout at -Root, held to -Commit.
    param([string]$Root, [string]$Tag, [string]$Commit, [string]$What)
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "no $What checkout at '$Root'."
    }
    $rev = ""
    try {
        $rev = (Invoke-DriverPackGit -Root $Root -Arguments @("rev-parse", "--verify", "--quiet", "refs/tags/$Tag^{commit}")).Trim()
    } catch {
        $rev = ""
    }
    if ($rev -notmatch '^[0-9a-f]{40}$') {
        throw "the $What checkout at '$Root' has no tag $Tag."
    }
    if ($rev -ne $Commit) {
        throw "the $What checkout's tag $Tag is commit $rev, not the pinned ${Commit}: the tag was moved, or this is another repository."
    }
    return $rev
}

function Read-StageText {
    # STAGE.TXT (usb-wdm-nonfree v1.0.0's format): its data lines as
    # objects (Action, Systems, Inf, Note, Line), every other line a comment
    # or blank. A malformed line is refused. A PACKAGE, EXCLUDES or COEXISTS
    # line is metadata, Read-OtherStageMeta's (record 26 section 6.7.11),
    # and skipped here; any other first field is refused, as before.
    param([string]$Text, [string]$Label)
    $out = @()
    foreach ($line in ($Text -split "`r?`n")) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith("#")) { continue }
        $f = @($line -split '\|')
        if (@("PACKAGE", "EXCLUDES", "COEXISTS") -ccontains $f[0].Trim()) { continue }
        if ($f.Count -ne 4) { throw "$Label has a line that is not 'ACTION | SYSTEMS | INF | NOTE': '$line'." }
        $action = $f[0].Trim()
        if ($action -cne "STAGE" -and $action -cne "COPY") { throw "$Label has a line whose action is not STAGE or COPY: '$line'." }
        $systems = @($f[1].Trim() -split '\s+' | Where-Object { $_ -ne "" })
        if ($systems.Count -eq 0) { throw "$Label has a line naming no system: '$line'." }
        foreach ($s in $systems) {
            if ($script:StageSystems -cnotcontains $s) { throw "$Label names a system it cannot read, '$s': '$line'." }
        }
        $inf = $f[2].Trim()
        if ($inf -eq "") { throw "$Label has a line naming no INF: '$line'." }
        $out += [pscustomobject]@{ Action = $action; Systems = $systems; Inf = $inf; Note = $f[3].Trim(); Line = $line }
    }
    return $out
}

function Assert-StageCovers {
    # Every INF of -Infs (paths from the release's top) on exactly one line
    # of -Lines, and every line's INF among them.
    param($Lines, [string[]]$Infs, [string]$Label)
    $seen = @{}
    foreach ($l in $Lines) {
        if ($Infs -cnotcontains $l.Inf) { throw "$Label names $($l.Inf), which is not an INF of the set." }
        $k = $l.Inf.ToLowerInvariant()
        if ($seen.ContainsKey($k)) { throw "$Label names $($l.Inf) on two lines." }
        $seen[$k] = $true
    }
    $missing = @($Infs | Where-Object { -not $seen.ContainsKey($_.ToLowerInvariant()) })
    if ($missing.Count -gt 0) { throw "$Label puts no line on $(@($missing | Select-Object -First 5) -join ', ')." }
}

function Write-OtherSetSums {
    # The folder's SHA256.TXT from -Files (path under the folder with '\'
    # to hash); returns its own hash.
    param([string]$Dir, $Files)
    $keys = [string[]]@($Files.Keys | ForEach-Object { $_.Replace('\', '/') })
    [Array]::Sort($keys, [System.StringComparer]::Ordinal)
    $lines = @($keys | ForEach-Object { "{0}  {1}" -f $Files[$_.Replace('/', '\')].ToLowerInvariant(), $_ })
    $path = Join-Path $Dir $script:OtherSums
    [System.IO.File]::WriteAllText($path, (($lines -join "`r`n") + "`r`n"), (New-Object System.Text.ASCIIEncoding))
    return (Get-FileSha256 -Path $path)
}

function Export-NonFreeSet {
    # Writes the download's NONFREE\ into -Dest (a new directory): the
    # pack's non-free/ at the pinned tag's commit, its NOTICE.md, README.md
    # and STAGE.TXT, and the folder's SHA256.TXT. The tag must be the pinned
    # commit, the pack's SHA256.TXT there the pinned bytes, and non-free/
    # exactly the files it lists under non-free/, each a plain file with
    # its listed bytes and a path the installer accepts; STAGE.TXT must
    # read and put every INF of non-free/ on exactly one line. The folders
    # of $script:NonFreeExcluded are held to the list too but not written,
    # and their STAGE.TXT lines are left out of the folder's STAGE.TXT
    # (every other line kept as it is).
    param([string]$Root, $Pin = $null, [string]$Dest)
    if ($null -eq $Pin) { $Pin = $script:NonFreePin }
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "no checkout at '$Root' for the download's driver set: clone it there, make a junction there to a clone, or name one." }
    $Root = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).ProviderPath.TrimEnd('\')
    $rev = Resolve-OtherSetTag -Root $Root -Tag $Pin.Tag -Commit $Pin.Commit -What "usb-wdm-nonfree"
    if (Test-Path -LiteralPath $Dest) { throw "'$Dest' already exists; the non-free set is exported into a new directory." }
    New-Item -ItemType Directory -Path $Dest | Out-Null
    $scratch = Join-Path $Dest ".pack-sums"
    $sumsHash = Get-OtherSetBlob -Root $Root -Rev $rev -Path "SHA256.TXT" -OutFile $scratch
    $sumsText = [System.IO.File]::ReadAllText($scratch)
    Remove-Item -LiteralPath $scratch -Force
    if ($sumsHash -ne $Pin.PackSums.ToUpperInvariant()) {
        throw "usb-wdm-nonfree $($Pin.Tag)'s SHA256.TXT is $sumsHash, not the pinned $($Pin.PackSums)."
    }
    $sums = Read-DriverPackHashList -Text $sumsText -Label "usb-wdm-nonfree $($Pin.Tag)'s SHA256.TXT"
    $pre = $script:NonFreePrefix + "/"
    $want = [ordered]@{}
    foreach ($p in $sums.Keys) { if ($p.StartsWith($pre, [System.StringComparison]::Ordinal)) { $want[$p] = $sums[$p] } }
    $tree = Get-OtherSetTree -Root $Root -Rev $rev -Prefix $script:NonFreePrefix
    $extra = @($tree.Keys | Where-Object { -not $want.Contains($_) })
    $missing = @($want.Keys | Where-Object { -not $tree.Contains($_) })
    if ($extra.Count -gt 0 -or $missing.Count -gt 0) {
        throw "usb-wdm-nonfree $($Pin.Tag)'s non-free/ is not the files its SHA256.TXT lists. Not listed: $(if ($extra.Count) { @($extra | Select-Object -First 5) -join ', ' } else { '(none)' }); missing: $(if ($missing.Count) { @($missing | Select-Object -First 5) -join ', ' } else { '(none)' })."
    }
    $files = [ordered]@{}
    $infs = @()
    $allInfs = @()
    foreach ($p in $want.Keys) {
        if ($tree[$p] -ne "100644 blob") { throw "usb-wdm-nonfree $($Pin.Tag)'s $p is not a plain file ($($tree[$p]))." }
        $isInf = $p.EndsWith(".inf", [System.StringComparison]::OrdinalIgnoreCase)
        if ($isInf) { $allInfs += $p }
        if (Test-NonFreeExcluded -Path $p) {
            # Held to its listed bytes too, then not written.
            $xf = Join-Path $Dest ".excluded-blob"
            $h = Get-OtherSetBlob -Root $Root -Rev $rev -Path $p -OutFile $xf
            Remove-Item -LiteralPath $xf -Force
            if ($h -ne $want[$p]) { throw "usb-wdm-nonfree $($Pin.Tag)'s $p does not hash as its SHA256.TXT lists it ($h)." }
            continue
        }
        $rel = $p.Substring($pre.Length)
        if (-not (Test-OtherSetRelPath -Rel $rel)) { throw "usb-wdm-nonfree $($Pin.Tag)'s $p has a path the installer does not accept." }
        $h = Get-OtherSetBlob -Root $Root -Rev $rev -Path $p -OutFile (Join-Path $Dest $rel.Replace('/', '\'))
        if ($h -ne $want[$p]) { throw "usb-wdm-nonfree $($Pin.Tag)'s $p does not hash as its SHA256.TXT lists it ($h)." }
        $files[$rel.Replace('/', '\')] = $h
        if ($isInf) { $infs += $p }
    }
    foreach ($x in $script:NonFreeExtras) {
        if ($files.Contains($x)) { throw "usb-wdm-nonfree $($Pin.Tag)'s non-free/ holds a $x, which would clash with the pack's own." }
        $files[$x] = Get-OtherSetBlob -Root $Root -Rev $rev -Path $x -OutFile (Join-Path $Dest $x)
    }
    $stagePath = Join-Path $Dest "STAGE.TXT"
    $stageText = [System.IO.File]::ReadAllText($stagePath)
    $stage = Read-StageText -Text $stageText -Label "usb-wdm-nonfree $($Pin.Tag)'s STAGE.TXT"
    Assert-StageCovers -Lines $stage -Infs $allInfs -Label "usb-wdm-nonfree $($Pin.Tag)'s STAGE.TXT"
    $kept = Remove-NonFreeExcludedStageLines -Text $stageText
    if ($kept -cne $stageText) {
        [System.IO.File]::WriteAllText($stagePath, $kept, (New-Object System.Text.ASCIIEncoding))
        $files["STAGE.TXT"] = Get-FileSha256 -Path $stagePath
        $stage = Read-StageText -Text $kept -Label "the download's NONFREE\STAGE.TXT"
        Assert-StageCovers -Lines $stage -Infs $infs -Label "the download's NONFREE\STAGE.TXT"
    }
    $own = Write-OtherSetSums -Dir $Dest -Files $files
    $files[$script:OtherSums] = $own
    return [pscustomobject]@{ Kind = "nonfree"; Folder = $script:NonFreeFolder; Root = $Root; Tag = $Pin.Tag; Revision = $rev; FullStage = $stageText;
                              PackSums = $Pin.PackSums.ToUpperInvariant(); Dir = $Dest; Sums = $own; Files = $files }
}

function Test-NonFreeExcluded {
    # A path of the pack ("non-free/wifi/...") under a folder of
    # $script:NonFreeExcluded.
    param([string]$Path)
    foreach ($x in $script:NonFreeExcluded) {
        if ($Path.StartsWith($script:NonFreePrefix + "/" + $x + "/", [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Remove-NonFreeExcludedStageLines {
    # The pack's STAGE.TXT without the INF lines and PACKAGE lines of the
    # excluded folders, under one comment line that says so; every other
    # line, and each line's end, as it was, so a pack with nothing excluded
    # gives the same bytes. EXCLUDES and COEXISTS lines are kept whatever
    # they name, as the free set's are.
    param([string]$Text)
    $out = New-Object System.Text.StringBuilder
    $dropped = 0
    foreach ($line in [regex]::Split($Text, '(?<=\n)')) {
        $f = @($line -split '\|' | ForEach-Object { $_.Trim() })
        $drop = $false
        if (-not $line.Trim().StartsWith("#")) {
            if ($f.Count -eq 4 -and ($f[0] -ceq "STAGE" -or $f[0] -ceq "COPY")) { $drop = Test-NonFreeExcluded -Path $f[2] }
            elseif ($f[0] -ceq "PACKAGE" -and $f.Count -ge 2) { $drop = Test-NonFreeExcluded -Path ($script:NonFreePrefix + "/" + $f[1] + "/") }
        }
        if ($drop) { $dropped++ } else { [void]$out.Append($line) }
    }
    if ($dropped -eq 0) { return $Text }
    $dirs = @($script:NonFreeExcluded | ForEach-Object { $script:NonFreePrefix + "/" + $_ + "/" }) -join ", "
    return ("# xHCI98's download leaves out $dirs and its lines here (make-release.ps1).`r`n" + $out.ToString())
}

function Read-NonFreePinFile {
    # A stand-in for $script:NonFreePin (-NonFreePinFile): "Tag: <tag>",
    # "Commit: <40 hex>" and "PackSums: <64 hex>".
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "no non-free pin at '$Path'." }
    $v = @{}
    foreach ($line in ([System.IO.File]::ReadAllText($Path) -split "`r?`n")) {
        if ($line.Trim() -eq "") { continue }
        if ($line -match '^(Tag): (\S+)$' -or $line -match '^(Commit): ([0-9a-f]{40})$' -or $line -match '^(PackSums): ([0-9A-Fa-f]{64})$') {
            $v[$Matches[1]] = $Matches[2]
        } else {
            throw "'$Path' has a line it cannot read: '$line'."
        }
    }
    foreach ($k in @("Tag", "Commit", "PackSums")) { if (-not $v.ContainsKey($k)) { throw "'$Path' names no $k." } }
    return [pscustomobject]@{ Tag = $v["Tag"]; Commit = $v["Commit"]; PackSums = $v["PackSums"].ToUpperInvariant() }
}

function Read-WdmFreePin {
    # wdmfree-pin.txt: $null while it names no release (no "Tag:" line);
    # otherwise its tag, commit, packages (class/package to systems) and
    # files (path from the release's top to hash). Read strictly.
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "no usb-wdm-free pin at '$Path'." }
    $tag = ""; $commit = ""; $thumb = ""
    $packages = [ordered]@{}
    $tools = @()
    $files = [ordered]@{}
    foreach ($line in ([System.IO.File]::ReadAllText($Path) -split "`r?`n")) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith("#")) { continue }
        if ($t -match '^Tag: (\S+)$') {
            if ($tag -ne "") { throw "'$Path' names two tags." }
            $tag = $Matches[1]
        } elseif ($t -match '^Commit: ([0-9a-f]{40})$') {
            $commit = $Matches[1]
        } elseif ($t -match '^Certificate: ([0-9A-Fa-f]{40})$') {
            if ($thumb -ne "") { throw "'$Path' names two certificates." }
            $thumb = $Matches[1].ToUpperInvariant()
        } elseif ($t -match '^Tool: ([a-z0-9]{1,8})$') {
            if ($tools -ccontains $Matches[1]) { throw "'$Path' names the tool $($Matches[1]) twice." }
            if (-not $script:WdmFreeToolWords.Contains($Matches[1])) { throw "'$Path' names the tool $($Matches[1]), which the readme has no words for (`$script:WdmFreeToolWords)." }
            $tools += $Matches[1]
        } elseif ($t -match '^Package: ([a-z0-9]+/[a-z0-9]+) \| (.+)$') {
            $pk = $Matches[1]; $sy = @($Matches[2].Trim() -split '\s+')
            if ($pk.StartsWith($script:WdmFreeToolsDir + "/", [System.StringComparison]::Ordinal)) { throw "'$Path' names $pk as a package: the release's $($script:WdmFreeToolsDir)/ holds tools, pinned by Tool: lines." }
            foreach ($s in $sy) { if ($script:StageSystems -cnotcontains $s) { throw "'$Path' names a system it cannot read, '$s'." } }
            if ($packages.Contains($pk)) { throw "'$Path' names $pk twice." }
            $packages[$pk] = $sy
        } elseif ($t -match '^SHA256: ([0-9A-Fa-f]{64}) (\S.*)$') {
            if ($files.Contains($Matches[2])) { throw "'$Path' lists $($Matches[2]) twice." }
            $files[$Matches[2]] = $Matches[1].ToUpperInvariant()
        } else {
            throw "'$Path' has a line it cannot read: '$line'."
        }
    }
    if ($tag -eq "") {
        # The Certificate line alone may stand before a release qualifies:
        # it records the owner's certificate, and nothing is exported.
        if ($commit -ne "" -or $packages.Count -gt 0 -or $tools.Count -gt 0 -or $files.Count -gt 0) { throw "'$Path' names no tag and still names a commit, packages, tools or files." }
        return $null
    }
    if ($commit -eq "" -or $packages.Count -eq 0) { throw "'$Path' names a tag with no commit or no package." }
    foreach ($x in $script:WdmFreeExtras) { if (-not $files.Contains($x)) { throw "'$Path' does not pin the release's $x." } }
    if (($thumb -ne "") -ne $files.Contains($script:WdmFreeCer)) {
        throw "'$Path' names $(if ($thumb -ne '') { 'a certificate and pins no ' + $script:WdmFreeCer } else { $script:WdmFreeCer + ' and names no Certificate: thumbprint' }): the two go together."
    }
    return [pscustomobject]@{ Tag = $tag; Commit = $commit; Packages = $packages; Tools = $tools; Files = $files; Thumbprint = $thumb }
}

function Test-WdmFreeCer {
    # The free set's certificate file: one X.509 certificate, DER, with no
    # private key (a .cer cannot carry one; a .pfx renamed would be refused
    # by the parse below or by HasPrivateKey), its SHA-1 thumbprint the
    # pin's. Throws with the reason; nothing is added to any store.
    param([string]$Path, [string]$Thumbprint, [string]$Label)
    $b = [System.IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 64 -or $b[0] -ne 0x30) { throw "$Label is not a DER certificate." }
    try { $ct = [System.Security.Cryptography.X509Certificates.X509Certificate2]::GetCertContentType($b) } catch { throw "$Label is not a DER certificate ($($_.Exception.InnerException.Message))." }
    if ($ct -ne [System.Security.Cryptography.X509Certificates.X509ContentType]::Cert) { throw "$Label is a $ct, not a certificate." }
    $x = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 (, $b)
    try {
        $raw = $x.RawData
        $whole = ($raw.Length -eq $b.Length)
        for ($i = 0; $whole -and $i -lt $b.Length; $i++) { if ($raw[$i] -ne $b[$i]) { $whole = $false } }
        if (-not $whole) { throw "$Label is not one DER certificate alone ($($b.Length) bytes, the certificate in it $($raw.Length))." }
        if ($x.HasPrivateKey) { throw "$Label carries a private key." }
        if ($x.Thumbprint.ToUpperInvariant() -ne $Thumbprint) { throw "$Label's thumbprint is $($x.Thumbprint), not the pinned $Thumbprint." }
    } finally {
        $x.Reset()
    }
}

function Export-WdmFreeSet {
    # Writes the download's WDMFREE\ into -Dest (a new directory): each
    # pinned package's free/<class>/<package>/ at the pinned tag's commit,
    # exactly the files the pin lists under it with their pinned bytes, at
    # WDMFREE\<class>\<package>\; each pinned tool's free/tools/<name>/ the
    # same way, at WDMFREE\tools\<name>\, with no INF; the release's LICENSE
    # and NOTICE.md; a
    # STAGE.TXT of the release's lines for those packages' INFs, every one
    # of them on exactly one line; a READ.TXT of the systems each package's
    # legs passed on; and the folder's SHA256.TXT.
    param([string]$Root, $Pin, [string]$Dest)
    if ($null -eq $Pin) { throw "the usb-wdm-free pin names no release." }
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "no checkout at '$Root' for the download's driver set: clone it there, make a junction there to a clone, or name one." }
    $Root = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).ProviderPath.TrimEnd('\')
    $rev = Resolve-OtherSetTag -Root $Root -Tag $Pin.Tag -Commit $Pin.Commit -What "usb-wdm-free"
    if (Test-Path -LiteralPath $Dest) { throw "'$Dest' already exists; the free set is exported into a new directory." }
    New-Item -ItemType Directory -Path $Dest | Out-Null
    $pre = $script:WdmFreePrefix + "/"
    $files = [ordered]@{}
    $infs = @()
    $claimed = @{}
    foreach ($pk in $Pin.Packages.Keys) {
        $tree = Get-OtherSetTree -Root $Root -Rev $rev -Prefix ($pre + $pk)
        $pinned = @($Pin.Files.Keys | Where-Object { $_.StartsWith($pre + $pk + "/", [System.StringComparison]::Ordinal) })
        $extra = @($tree.Keys | Where-Object { $pinned -cnotcontains $_ })
        $missing = @($pinned | Where-Object { -not $tree.Contains($_) })
        if ($tree.Count -eq 0 -or $extra.Count -gt 0 -or $missing.Count -gt 0) {
            throw "usb-wdm-free $($Pin.Tag)'s $pre$pk/ is not the files the pin lists. Not pinned: $(if ($extra.Count) { @($extra | Select-Object -First 5) -join ', ' } else { '(none)' }); missing: $(if ($missing.Count) { @($missing | Select-Object -First 5) -join ', ' } elseif ($tree.Count -eq 0) { 'the whole folder' } else { '(none)' })."
        }
        foreach ($p in $pinned) {
            $claimed[$p] = $true
            if ($tree[$p] -ne "100644 blob") { throw "usb-wdm-free $($Pin.Tag)'s $p is not a plain file ($($tree[$p]))." }
            $rel = $p.Substring($pre.Length)
            if (-not (Test-OtherSetRelPath -Rel $rel)) { throw "usb-wdm-free $($Pin.Tag)'s $p has a path the installer does not accept." }
            $h = Get-OtherSetBlob -Root $Root -Rev $rev -Path $p -OutFile (Join-Path $Dest $rel.Replace('/', '\'))
            if ($h -ne $Pin.Files[$p]) { throw "usb-wdm-free $($Pin.Tag)'s $p is $h, not the pinned $($Pin.Files[$p])." }
            $files[$rel.Replace('/', '\')] = $h
            if ($p.EndsWith(".inf", [System.StringComparison]::OrdinalIgnoreCase)) { $infs += $p }
        }
    }
    $tools = @()
    if ($Pin.PSObject.Properties.Name -contains "Tools") { $tools = @($Pin.Tools) }
    foreach ($tl in $tools) {
        $tp = $pre + $script:WdmFreeToolsDir + "/" + $tl
        $tree = Get-OtherSetTree -Root $Root -Rev $rev -Prefix $tp
        $pinned = @($Pin.Files.Keys | Where-Object { $_.StartsWith($tp + "/", [System.StringComparison]::Ordinal) })
        $extra = @($tree.Keys | Where-Object { $pinned -cnotcontains $_ })
        $missing = @($pinned | Where-Object { -not $tree.Contains($_) })
        if ($tree.Count -eq 0 -or $extra.Count -gt 0 -or $missing.Count -gt 0) {
            throw "usb-wdm-free $($Pin.Tag)'s $tp/ is not the files the pin lists. Not pinned: $(if ($extra.Count) { @($extra | Select-Object -First 5) -join ', ' } else { '(none)' }); missing: $(if ($missing.Count) { @($missing | Select-Object -First 5) -join ', ' } elseif ($tree.Count -eq 0) { 'the whole folder' } else { '(none)' })."
        }
        foreach ($p in $pinned) {
            $claimed[$p] = $true
            if ($tree[$p] -ne "100644 blob") { throw "usb-wdm-free $($Pin.Tag)'s $p is not a plain file ($($tree[$p]))." }
            if ($p.EndsWith(".inf", [System.StringComparison]::OrdinalIgnoreCase)) { throw "usb-wdm-free $($Pin.Tag)'s $p is an INF in a tool's folder: a tool is not a driver package." }
            $rel = $p.Substring($pre.Length)
            if (-not (Test-OtherSetRelPath -Rel $rel)) { throw "usb-wdm-free $($Pin.Tag)'s $p has a path the installer does not accept." }
            $h = Get-OtherSetBlob -Root $Root -Rev $rev -Path $p -OutFile (Join-Path $Dest $rel.Replace('/', '\'))
            if ($h -ne $Pin.Files[$p]) { throw "usb-wdm-free $($Pin.Tag)'s $p is $h, not the pinned $($Pin.Files[$p])." }
            $files[$rel.Replace('/', '\')] = $h
        }
    }
    foreach ($x in $script:WdmFreeExtras) { $claimed[$x] = $true }
    $thumb = ""
    if ($Pin.PSObject.Properties.Name -contains "Thumbprint") { $thumb = [string]$Pin.Thumbprint }
    if ($thumb -ne "") { $claimed[$script:WdmFreeCer] = $true }
    $stray = @($Pin.Files.Keys | Where-Object { -not $claimed.ContainsKey($_) })
    if ($stray.Count -gt 0) { throw "the usb-wdm-free pin lists files of no pinned package or tool: $(@($stray | Select-Object -First 5) -join ', ')." }
    foreach ($x in @("LICENSE", "NOTICE.md")) {
        $h = Get-OtherSetBlob -Root $Root -Rev $rev -Path $x -OutFile (Join-Path $Dest $x)
        if ($h -ne $Pin.Files[$x]) { throw "usb-wdm-free $($Pin.Tag)'s $x is $h, not the pinned $($Pin.Files[$x])." }
        $files[$x] = $h
    }
    if ($thumb -ne "") {
        $cerOut = Join-Path $Dest $script:WdmFreeCer
        $h = Get-OtherSetBlob -Root $Root -Rev $rev -Path $script:WdmFreeCer -OutFile $cerOut
        if ($h -ne $Pin.Files[$script:WdmFreeCer]) { throw "usb-wdm-free $($Pin.Tag)'s $($script:WdmFreeCer) is $h, not the pinned $($Pin.Files[$script:WdmFreeCer])." }
        Test-WdmFreeCer -Path $cerOut -Thumbprint $thumb -Label "usb-wdm-free $($Pin.Tag)'s $($script:WdmFreeCer)"
        $files[$script:WdmFreeCer] = $h
    }
    $stageSrc = Join-Path $Dest ".stage-src"
    $h = Get-OtherSetBlob -Root $Root -Rev $rev -Path "STAGE.TXT" -OutFile $stageSrc
    $stageText = [System.IO.File]::ReadAllText($stageSrc)
    Remove-Item -LiteralPath $stageSrc -Force
    if ($h -ne $Pin.Files["STAGE.TXT"]) { throw "usb-wdm-free $($Pin.Tag)'s STAGE.TXT is $h, not the pinned $($Pin.Files['STAGE.TXT'])." }
    $mine = @(Read-StageText -Text $stageText -Label "usb-wdm-free $($Pin.Tag)'s STAGE.TXT" | Where-Object {
        $inf = $_.Inf
        @($Pin.Packages.Keys | Where-Object { $inf.StartsWith($pre + $_ + "/", [System.StringComparison]::Ordinal) }).Count -gt 0
    })
    Assert-StageCovers -Lines $mine -Infs $infs -Label "usb-wdm-free $($Pin.Tag)'s STAGE.TXT, for the pinned packages,"
    $stageOut = @(
        "# STAGE.TXT - which of these drivers' INFs may be staged, on which Windows",
        "#",
        "# The lines of usb-wdm-free $($Pin.Tag)'s STAGE.TXT for the drivers in this",
        "# download, unchanged; its format is that file's (and usb-wdm-nonfree's):",
        "#   ACTION | SYSTEMS | INF | NOTE",
        "# Written by xHCI98's make-release.ps1 (roadmap-usb4 task 43.48).",
        "") + @($mine | ForEach-Object { $_.Line.TrimEnd() })
    # The metadata kept (record 26 section 6.7.11): a qualifying package's
    # PACKAGE line, and every EXCLUDES and COEXISTS line whatever its
    # endpoints, so filtering never drops a constraint; verbatim, after the
    # INF lines.
    foreach ($ml in ($stageText -split "`r?`n")) {
        $mf = @($ml -split '\|' | ForEach-Object { $_.Trim() })
        if ($mf[0] -ceq "PACKAGE" -and $mf.Count -ge 2 -and $Pin.Packages.Contains($mf[1])) { $stageOut += $ml.TrimEnd() }
        elseif ($mf[0] -ceq "EXCLUDES" -or $mf[0] -ceq "COEXISTS") { $stageOut += $ml.TrimEnd() }
    }
    [System.IO.File]::WriteAllText((Join-Path $Dest "STAGE.TXT"), (($stageOut -join "`r`n") + "`r`n"), (New-Object System.Text.ASCIIEncoding))
    $files["STAGE.TXT"] = Get-FileSha256 -Path (Join-Path $Dest "STAGE.TXT")
    $readOut = @(
        "# READ.TXT - the systems each driver here was tested on",
        "#",
        "# One line a driver: <class>/<package> | <systems>, the systems in",
        "# STAGE.TXT's tokens: those its legs passed on, by xHCI98's pin of",
        "# usb-wdm-free $($Pin.Tag) (roadmap-usb4 task 43.48).",
        "") + @($Pin.Packages.Keys | ForEach-Object { "{0} | {1}" -f $_, ($Pin.Packages[$_] -join " ") })
    [System.IO.File]::WriteAllText((Join-Path $Dest "READ.TXT"), (($readOut -join "`r`n") + "`r`n"), (New-Object System.Text.ASCIIEncoding))
    $files["READ.TXT"] = Get-FileSha256 -Path (Join-Path $Dest "READ.TXT")
    $own = Write-OtherSetSums -Dir $Dest -Files $files
    $files[$script:OtherSums] = $own
    return [pscustomobject]@{ Kind = "free"; Folder = $script:WdmFreeFolder; Root = $Root; Tag = $Pin.Tag; Revision = $rev; FullStage = $stageText;
                              Dir = $Dest; Sums = $own; Files = $files; Tools = $tools }
}

# ---- The other sets' cut-side checks (design record 26 section 6.7.11) ----
#
# What the cut holds both driver sets to before it writes a download that
# carries them, and the history it keeps of the installer's package table:
# the 98 SE and ME one-package-a-folder rule and each STAGE INF's
# self-containment (Test-OtherStagePackages), one hardware id per system over
# both sets (Test-OtherStageIds), the package table and any PACKAGE lines
# against the sets and the brand list (Test-OtherPkgTable,
# scripts\package\brand-words.txt), the history in
# releases\<version>\otherpkgs.txt (Test-OtherPkgsTransition), and the
# overlap listing for the maintainer (Get-OtherOverlaps). Invoke-OtherSetChecks
# runs them all for make-release.ps1.

$script:OtherPkgsRecordName = "otherpkgs.txt"
# The records a release directory keeps for the maintainer, which no download
# carries (the owner's decision of 2026-10-24): nothing at runtime reads them,
# the installer having its own compiled table. New-UploadSet leaves exactly
# these out, at the top level of the published tree alone.
$script:ReleaseRecordOnlyNames = @($script:DriverPackRecordName, $script:DownloadsRecordName,
                                   $script:OtherPkgsRecordName, "wdmfree-pin.txt")
$script:OtherMetaKinds = @("PACKAGE", "EXCLUDES", "COEXISTS")
$script:BrandWordsName = "brand-words.txt"

function Get-BrandWordsPath {
    return (Join-Path $PSScriptRoot $script:BrandWordsName)
}

function Read-BrandWords {
    # brand-words.txt: one word a line, '#' comments and blank lines aside;
    # a word is letters and digits only, matched whole and without case.
    param([string]$Path = "")
    if ($Path -eq "") { $Path = Get-BrandWordsPath }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "no brand list at '$Path'." }
    $out = @()
    foreach ($line in ([System.IO.File]::ReadAllText($Path) -split "`r?`n")) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith("#")) { continue }
        if ($t -notmatch '^[A-Za-z0-9]+$') { throw "'$Path' has a line that is not one word of letters and digits: '$line'." }
        if (@($out | Where-Object { $_ -ieq $t }).Count -gt 0) { throw "'$Path' names $t twice." }
        $out += $t
    }
    if ($out.Count -eq 0) { throw "'$Path' names no word." }
    return $out
}

function Find-BrandWord {
    # The first word of -Words found whole, without case, in -Text; "" none.
    param([string]$Text, [string[]]$Words)
    foreach ($w in $Words) {
        if ($Text -match ('(?i)(^|[^A-Za-z0-9])' + [regex]::Escape($w) + '($|[^A-Za-z0-9])')) { return $w }
    }
    return ""
}

function Test-OtherPkgDesc {
    # A package's description by inst_other_desc_ok's rules: 1 to 80
    # printable ASCII characters, no '|', and for a CLASS package no word of
    # the brand list. "" when it passes, else the reason.
    param([string]$Kind, [string]$Desc, [string[]]$Brands)
    if ($Desc -eq "" -or $Desc.Length -gt 80) { return "is $($Desc.Length) characters, not 1 to 80" }
    if ($Desc -notmatch '^[\x20-\x7E]+$' -or $Desc.Contains("|")) { return "holds a character other than printable ASCII, or a '|'" }
    if ($Kind -ceq "CLASS") {
        $b = Find-BrandWord -Text $Desc -Words $Brands
        if ($b -ne "") { return "is a CLASS package's and names '$b', a brand (scripts\package\brand-words.txt)" }
    }
    return ""
}

function Test-OtherPkgId {
    param([string]$Id)
    return ($Id -cmatch '^[a-z0-9_-]+/[a-z0-9_-]+$' -and $Id.Length -lt 48)
}

function Read-OtherStageMeta {
    # The PACKAGE, EXCLUDES and COEXISTS lines of a STAGE.TXT (section
    # 6.7.11's format, inst_stage_meta's rules) as objects: Kind ("PACKAGE",
    # "EXCLUDES" or "COEXISTS"), A, B, Systems, Desc and Line. PACKAGE: A the
    # id, B CLASS or DEVICE, Desc its description; the others: A and B
    # set-qualified ids, Systems their tokens, Desc the why. A malformed one
    # is refused; a NEEDS line is unsupported metadata.
    param([string]$Text, [string]$Label, [string[]]$Brands)
    $out = @()
    foreach ($line in ($Text -split "`r?`n")) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith("#")) { continue }
        $f = @($line -split '\|' | ForEach-Object { $_.Trim() })
        if ($f[0] -ceq "NEEDS") { throw "$Label has a NEEDS line, metadata this release does not read: '$line'." }
        if ($script:OtherMetaKinds -cnotcontains $f[0]) { continue }
        # inst_stage_meta reads each field into a buffer of 173 bytes
        # (INST_OTHER_REL_CHARS + 32): a longer one makes the installer refuse
        # the whole set, so the cut refuses it first.
        foreach ($x in $f) { if ($x.Length -gt 172) { throw "$Label has a $($f[0]) line with a field of $($x.Length) characters; the installer reads at most 172: '$line'." } }
        if ($f[0] -ceq "PACKAGE") {
            if ($f.Count -ne 4 -or -not (Test-OtherPkgId -Id $f[1]) -or ($f[2] -cne "CLASS" -and $f[2] -cne "DEVICE")) {
                throw "$Label has a PACKAGE line that is not 'PACKAGE | <class>/<package> | CLASS or DEVICE | <description>': '$line'."
            }
            $why = Test-OtherPkgDesc -Kind $f[2] -Desc $f[3] -Brands $Brands
            if ($why -ne "") { throw "$Label's PACKAGE line for $($f[1]): its description $why." }
            $out += [pscustomobject]@{ Kind = "PACKAGE"; A = $f[1]; B = $f[2]; Systems = @(); Desc = $f[3]; Line = $line }
        } else {
            if ($f.Count -ne 5) { throw "$Label has a $($f[0]) line that is not '$($f[0]) | <set>:<id> | <set>:<id> | <systems> | <why>': '$line'." }
            foreach ($q in @($f[1], $f[2])) {
                if ($q -notmatch '^(free|nonfree):(.+)$' -or -not (Test-OtherPkgId -Id $Matches[2])) { throw "$Label's $($f[0]) line names '$q', which is not free:<class>/<package> or nonfree:<class>/<package>: '$line'." }
            }
            $sy = @($f[3] -split '\s+' | Where-Object { $_ -ne "" })
            if ($sy.Count -eq 0) { throw "$Label's $($f[0]) line names no system: '$line'." }
            foreach ($s in $sy) { if ($script:StageSystems -cnotcontains $s) { throw "$Label's $($f[0]) line names a system it cannot read, '$s'." } }
            $out += [pscustomobject]@{ Kind = $f[0]; A = $f[1]; B = $f[2]; Systems = $sy; Desc = $f[4]; Line = $line }
        }
    }
    return $out
}

function Get-InstOtherTable {
    # The installer's package table, read from xhciinst\inst_core.c:
    # instOtherPkgs (Set, Id, Kind, Heading, Desc, Withdrawn), instOtherRels
    # (A, B set-qualified, Systems as tokens, Kind EXCLUDES or COEXISTS) and
    # instBrandWords. Each initializer is read whole; a row this reader
    # cannot take is refused, so the C and the cut cannot read two tables.
    param([string]$Repo)
    $text = [System.IO.File]::ReadAllText((Join-Path $Repo "xhciinst\inst_core.c"))
    function Get-Block([string]$Name) {
        $m = [regex]::Match($text, [regex]::Escape($Name) + '\[\]\s*=\s*\{(.*?)\n\};', [System.Text.RegularExpressions.RegexOptions]::Singleline)
        if (-not $m.Success) { throw "xhciinst\inst_core.c has no initializer of $Name that this reader can find." }
        return $m.Groups[1].Value
    }
    function Join-Literals([string]$Body) {
        # Adjacent string literals made one, as the C compiler does.
        return [regex]::Replace($Body, '"\s*\r?\n\s*"', '')
    }
    $pkgs = @()
    $body = Join-Literals (Get-Block "instOtherPkgs")
    foreach ($m in [regex]::Matches($body, '\{\s*(INST_SET_FREE|INST_SET_NONFREE)\s*,\s*"([^"]*)"\s*,\s*(INST_PKG_CLASS|INST_PKG_DEVICE)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*([01])\s*\}')) {
        $pkgs += [pscustomobject]@{
            Set = $(if ($m.Groups[1].Value -eq "INST_SET_FREE") { "free" } else { "nonfree" })
            Id = $m.Groups[2].Value
            Kind = $(if ($m.Groups[3].Value -eq "INST_PKG_CLASS") { "CLASS" } else { "DEVICE" })
            Heading = $m.Groups[4].Value
            Desc = $m.Groups[5].Value
            Withdrawn = ($m.Groups[6].Value -eq "1")
        }
    }
    $rows = @([regex]::Matches($body, '\{\s*INST_SET_')).Count
    if ($pkgs.Count -eq 0 -or $pkgs.Count -ne $rows) { throw "xhciinst\inst_core.c's instOtherPkgs has $rows rows and this reader took $($pkgs.Count): change the reader with the table." }
    $rels = @()
    $body = Get-Block "instOtherRels"
    foreach ($m in [regex]::Matches($body, '\{\s*(INST_SET_FREE|INST_SET_NONFREE)\s*,\s*"([^"]*)"\s*,\s*(INST_SET_FREE|INST_SET_NONFREE)\s*,\s*"([^"]*)"\s*,\s*([A-Z_]+|0x[0-9A-Fa-f]+UL?)\s*,\s*([01])\s*\}')) {
        $mask = $m.Groups[5].Value
        if ($mask -eq "INST_SYS_ALL") { $sys = @($script:StageSystems) }
        elseif ($mask -match '^0x([0-9A-Fa-f]+)') {
            $v = [Convert]::ToInt64($Matches[1], 16)
            $sys = @(for ($i = 0; $i -lt $script:StageSystems.Count; $i++) { if ($v -band ([int64]1 -shl $i)) { $script:StageSystems[$i] } })
        } else { throw "xhciinst\inst_core.c's instOtherRels names a system mask this reader cannot read, '$mask'." }
        $rels += [pscustomobject]@{
            Kind = $(if ($m.Groups[6].Value -eq "1") { "EXCLUDES" } else { "COEXISTS" })
            A = ($(if ($m.Groups[1].Value -eq "INST_SET_FREE") { "free:" } else { "nonfree:" }) + $m.Groups[2].Value)
            B = ($(if ($m.Groups[3].Value -eq "INST_SET_FREE") { "free:" } else { "nonfree:" }) + $m.Groups[4].Value)
            Systems = $sys
            Desc = "the installer's table"
        }
    }
    $relRows = @([regex]::Matches($body, '\{\s*INST_SET_')).Count
    if ($rels.Count -ne $relRows) { throw "xhciinst\inst_core.c's instOtherRels has $relRows rows and this reader took $($rels.Count): change the reader with the table." }
    $words = @([regex]::Matches((Get-Block "instBrandWords"), '"([^"]*)"') | ForEach-Object { $_.Groups[1].Value })
    return [pscustomobject]@{ Pkgs = $pkgs; Rels = $rels; Brands = $words }
}

function Read-InfText {
    # An INF's sections, for the cut's own checks: an ordered map of the
    # section name in lower case to its entries (comments after ';' outside
    # quotes dropped, blank lines dropped, a '\' continuation joined), with
    # %token%s replaced from [Strings] when asked for.
    param([string]$Path)
    $raw = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::Default)
    if ($raw.Length -ge 1 -and [int]$raw[0] -eq 0xFEFF) { $raw = $raw.Substring(1) }
    $secs = [ordered]@{}
    $cur = $null
    $pending = ""
    foreach ($line in ($raw -split "`r?`n")) {
        $l = $pending + $line
        $pending = ""
        $sb = New-Object System.Text.StringBuilder
        $q = $false
        foreach ($ch in $l.ToCharArray()) {
            if ($ch -eq '"') { $q = -not $q }
            if ($ch -eq ';' -and -not $q) { break }
            [void]$sb.Append($ch)
        }
        $l = $sb.ToString().Trim()
        if ($l.EndsWith("\")) { $pending = $l.Substring(0, $l.Length - 1); continue }
        if ($l -eq "") { continue }
        if ($l -match '^\[(.+)\]$') {
            $cur = $Matches[1].Trim().ToLowerInvariant()
            if (-not $secs.Contains($cur)) { $secs[$cur] = New-Object System.Collections.ArrayList }
            continue
        }
        if ($null -ne $cur) { [void]$secs[$cur].Add($l) }
    }
    $strings = @{}
    foreach ($k in @($secs.Keys | Where-Object { $_ -match '^strings(\..+)?$' })) {
        foreach ($e in $secs[$k]) {
            if ($e -match '^([^=]+?)\s*=\s*(.*)$') {
                $n = $Matches[1].Trim().ToLowerInvariant()
                if (-not $strings.ContainsKey($n)) { $strings[$n] = $Matches[2].Trim().Trim('"') }
            }
        }
    }
    return [pscustomobject]@{ Sections = $secs; Strings = $strings; Path = $Path }
}

function Expand-InfValue {
    param($Inf, [string]$Text)
    return [regex]::Replace($Text, '%([^%]+)%', {
        param($m)
        $k = $m.Groups[1].Value.ToLowerInvariant()
        if ($Inf.Strings.ContainsKey($k)) { return $Inf.Strings[$k] }
        return $m.Value
    }).Trim().Trim('"')
}

function Split-InfFields {
    # An INF value's comma-separated fields, a comma inside double quotes
    # not a separator (a quoted disk description may hold one), each field
    # trimmed and its quotes kept for Expand-InfValue to drop.
    param([string]$Text)
    $out = New-Object System.Collections.ArrayList
    $sb = New-Object System.Text.StringBuilder
    $q = $false
    foreach ($ch in $Text.ToCharArray()) {
        if ($ch -eq '"') { $q = -not $q }
        if ($ch -eq ',' -and -not $q) { [void]$out.Add($sb.ToString().Trim()); [void]$sb.Clear(); continue }
        [void]$sb.Append($ch)
    }
    [void]$out.Add($sb.ToString().Trim())
    return [string[]]$out.ToArray()
}

function Get-InfEntries {
    # The "key = value" entries of a section as Key (lower case) and Value.
    param($Inf, [string]$Section)
    $k = $Section.ToLowerInvariant()
    if (-not $Inf.Sections.Contains($k)) { return @() }
    $out = @()
    foreach ($e in $Inf.Sections[$k]) {
        if ($e -match '^([^=]+?)\s*=\s*(.*)$') { $out += [pscustomobject]@{ Key = $Matches[1].Trim().Trim('"').ToLowerInvariant(); Value = $Matches[2].Trim(); Raw = $e } }
        else { $out += [pscustomobject]@{ Key = ""; Value = $e; Raw = $e } }
    }
    return $out
}

function Get-OtherPkgOf {
    # "<class>/<package>" of a set path ("non-free/serial/ftdi/w2k/x.inf").
    param([string]$Path, [string]$Prefix)
    $rel = $Path.Substring($Prefix.Length + 1)
    $p = @($rel -split '/')
    if ($p.Count -lt 3) { return "" }
    return ($p[0] + "/" + $p[1])
}

function Test-OtherStagePackages {
    # Record 26 section 6.7.11, "Each package its own folders" and "No
    # dependencies between packages", over one set: -Lines its STAGE.TXT's
    # (Read-StageText), -Dir the exported folder (paths under -Prefix/ there
    # without the prefix), -AllInfs every INF file name of both sets (lower
    # case), for the Include rule. Throws the first fault with its reason.
    param($Lines, [string]$Dir, [string]$Prefix, [string]$Label, [string[]]$AllInfs = @())
    $pre = $Prefix + "/"
    $byFolder = @{}
    foreach ($l in $Lines) {
        if (-not $l.Inf.StartsWith($pre, [System.StringComparison]::Ordinal)) { throw "$Label names $($l.Inf), which is not under $pre." }
        $pk = Get-OtherPkgOf -Path $l.Inf -Prefix $Prefix
        if ($pk -eq "") { throw "$Label names $($l.Inf), which is not under a <class>/<package>/ folder." }
        $folder = $l.Inf.Substring(0, $l.Inf.LastIndexOf('/')).ToLowerInvariant()
        if (-not $byFolder.ContainsKey($folder)) { $byFolder[$folder] = @() }
        $byFolder[$folder] += [pscustomobject]@{ Line = $l; Pkg = $pk }
    }
    # The 98 SE and ME folder rule: a folder staged there belongs to one
    # package, and every other STAGE INF in it is staged on that system too.
    foreach ($folder in $byFolder.Keys) {
        $here = $byFolder[$folder]
        foreach ($sys in @("W98SE", "WME")) {
            $staged = @($here | Where-Object { $_.Line.Action -ceq "STAGE" -and $_.Line.Systems -ccontains $sys })
            if ($staged.Count -eq 0) { continue }
            $pkgs = @($here | ForEach-Object { $_.Pkg } | Sort-Object -Unique)
            if ($pkgs.Count -gt 1) { throw "$Label stages $($staged[0].Line.Inf) on $sys, and its folder holds INFs of $($pkgs -join ' and '): on 98 SE and ME the wizard reads every INF of a folder on DevicePath, so one package a folder (design record 26 section 6.7.11)." }
            foreach ($o in @($here | Where-Object { $_.Line.Action -ceq "STAGE" -and $_.Line.Systems -cnotcontains $sys })) {
                throw "$Label stages $($staged[0].Line.Inf) on $sys, and $($o.Line.Inf) beside it is a STAGE line that does not name ${sys}: the wizard there would see it (design record 26 section 6.7.11)."
            }
        }
    }
    # Self-containment: every file a STAGE INF takes from its source disks is
    # under its own package's folder and in the set.
    foreach ($folder in $byFolder.Keys) {
        foreach ($o in @($byFolder[$folder] | Where-Object { $_.Line.Action -ceq "STAGE" })) {
            $relInf = $o.Line.Inf.Substring($pre.Length)
            $infPath = Join-Path $Dir $relInf.Replace('/', '\')
            if (-not (Test-Path -LiteralPath $infPath -PathType Leaf)) { throw "$Label names $($o.Line.Inf), which the set does not hold." }
            $inf = Read-InfText -Path $infPath
            $pkgDir = [System.IO.Path]::GetFullPath((Join-Path $Dir ($o.Pkg.Replace('/', '\')))).TrimEnd('\') + '\'
            $infDir = Split-Path -Parent $infPath
            # [SourceDisksNames] and its platform forms: a decorated
            # [SourceDisksFiles.<p>] resolves a disk through
            # [SourceDisksNames.<p>] first and the undecorated section after,
            # as the engine does; an undecorated one is checked through every
            # platform's view, since each platform's engine reads it.
            $diskMaps = @{}
            foreach ($sn in @($inf.Sections.Keys | Where-Object { $_ -match '^sourcedisksnames(\.(.+))?$' })) {
                $deco = ""
                if ($sn -match '^sourcedisksnames\.(.+)$') { $deco = $Matches[1] }
                $map = @{}
                foreach ($e in (Get-InfEntries -Inf $inf -Section $sn)) {
                    $f = @(Split-InfFields -Text $e.Value)
                    $sub = ""
                    if ($f.Count -ge 4) { $sub = (Expand-InfValue -Inf $inf -Text $f[3]).Trim('\') }
                    $map[$e.Key] = $sub
                }
                $diskMaps[$deco] = $map
            }
            $decos = @($diskMaps.Keys | Where-Object { $_ -ne "" })
            foreach ($sf in @($inf.Sections.Keys | Where-Object { $_ -match '^sourcedisksfiles(\..+)?$' })) {
                $fdeco = ""
                if ($sf -match '^sourcedisksfiles\.(.+)$') { $fdeco = $Matches[1] }
                $views = @($fdeco)
                if ($fdeco -eq "") { $views = @("") + $decos }
                foreach ($e in (Get-InfEntries -Inf $inf -Section $sf)) {
                    $name = Expand-InfValue -Inf $inf -Text $e.Key
                    $f = @(Split-InfFields -Text $e.Value)
                    $disk = $f[0].Trim()
                    $sub = ""
                    if ($f.Count -ge 2) { $sub = (Expand-InfValue -Inf $inf -Text $f[1]).Trim('\') }
                    foreach ($v in $views) {
                        $dp = ""
                        if ($v -ne "" -and $diskMaps.ContainsKey($v) -and $diskMaps[$v].ContainsKey($disk)) { $dp = $diskMaps[$v][$disk] }
                        elseif ($diskMaps.ContainsKey("") -and $diskMaps[""].ContainsKey($disk)) { $dp = $diskMaps[""][$disk] }
                        $base = $infDir
                        if ($dp -ne "") { $base = Join-Path $base $dp }
                        if ($sub -ne "") { $base = Join-Path $base $sub }
                        $full = [System.IO.Path]::GetFullPath((Join-Path $base $name))
                        if (-not $full.StartsWith($pkgDir, [System.StringComparison]::OrdinalIgnoreCase)) {
                            throw "$Label's $($o.Line.Inf) takes $name from outside its package's folder ($($o.Pkg)/): a package is self-contained (design record 26 section 6.7.11)."
                        }
                    }
                    # Not held to exist: a vendor's INF lists both
                    # architectures' files where each folder holds its own
                    # (Realtek's rtuxp.INF), and the engine copies only what
                    # the system's install section names.
                }
            }
            # Include names an INF of Windows' own %windir%\INF; one that
            # names an INF of either set takes another package's file.
            foreach ($sec in $inf.Sections.Keys) {
                foreach ($e in @(Get-InfEntries -Inf $inf -Section $sec | Where-Object { $_.Key -eq "include" })) {
                    foreach ($n in @(Split-InfFields -Text $e.Value | ForEach-Object { (Expand-InfValue -Inf $inf -Text $_).ToLowerInvariant() } | Where-Object { $_ -ne "" })) {
                        if ($AllInfs -contains $n) { throw "$Label's $($o.Line.Inf) includes $n, an INF of a driver set: Include takes Windows' own %windir%\INF, so a package may need no other (design record 26 section 6.7.11)." }
                    }
                }
            }
        }
    }
}

# Each token's engines: the NT version(s) it stands for (8 also 8.1, 10 its
# 22H2 build), the architecture, whether decorations are read at all (not
# on 98 SE, ME and 2000).
$script:OtherSysInfo = [ordered]@{
    "W98SE" = @{ Nt = $false; Arch = "x86"; Vers = @(@(0, 0, 0)); NoDeco = $true }
    "WME" = @{ Nt = $false; Arch = "x86"; Vers = @(@(0, 0, 0)); NoDeco = $true }
    "W2K" = @{ Nt = $true; Arch = "x86"; Vers = @(, @(5, 0, 2195)); NoDeco = $true }
    "WXP" = @{ Nt = $true; Arch = "x86"; Vers = @(, @(5, 1, 2600)); NoDeco = $false }
    "WXP64" = @{ Nt = $true; Arch = "amd64"; Vers = @(, @(5, 2, 3790)); NoDeco = $false }
    "WVISTA" = @{ Nt = $true; Arch = "x86"; Vers = @(, @(6, 0, 6002)); NoDeco = $false }
    "WVISTA64" = @{ Nt = $true; Arch = "amd64"; Vers = @(, @(6, 0, 6002)); NoDeco = $false }
    "W7" = @{ Nt = $true; Arch = "x86"; Vers = @(, @(6, 1, 7601)); NoDeco = $false }
    "W7X64" = @{ Nt = $true; Arch = "amd64"; Vers = @(, @(6, 1, 7601)); NoDeco = $false }
    "W8" = @{ Nt = $true; Arch = "x86"; Vers = @(@(6, 2, 9200), @(6, 3, 9600)); NoDeco = $false }
    "W8X64" = @{ Nt = $true; Arch = "amd64"; Vers = @(@(6, 2, 9200), @(6, 3, 9600)); NoDeco = $false }
    "W10" = @{ Nt = $true; Arch = "x86"; Vers = @(, @(10, 0, 19045)); NoDeco = $false }
    "W10X64" = @{ Nt = $true; Arch = "amd64"; Vers = @(, @(10, 0, 19045)); NoDeco = $false }
}

function Get-InfModelDecoScore {
    # How well a [Manufacturer] decoration (NT[arch][.major[.minor[.type
    # [.suite[.build]]]]]) fits a workstation of -Arch at -Ver (major,
    # minor, build): -1 when it does not apply, else a score where a higher
    # version, a build, a product type and an architecture rank higher, as
    # the engine prefers the most specific match. On 64-bit Windows a
    # decoration must name the architecture; a server product type (2 or 3)
    # or a suite mask never applies to the systems the sets serve; a build
    # number is read by 10 only (1607 and later).
    param([string]$Deco, [string]$Arch, $Ver)
    $m = [regex]::Match($Deco, '^(?i)NT(x86|amd64|ia64|arm|arm64)?(?:\.(\d+)(?:\.(\d+)(?:\.(0x[0-9A-Fa-f]+|\d*)(?:\.(0x[0-9A-Fa-f]+|\d*)(?:\.(\d+))?)?)?)?)?$')
    if (-not $m.Success) { return -1 }
    $a = $m.Groups[1].Value
    if ($a -ne "" -and $a -ine $Arch) { return -1 }
    if ($a -eq "" -and $Arch -ne "x86") { return -1 }
    $maj = 0; $min = 0; $build = 0
    if ($m.Groups[2].Success) { $maj = [int]$m.Groups[2].Value }
    if ($m.Groups[3].Success) { $min = [int]$m.Groups[3].Value }
    if ($maj -gt $Ver[0] -or ($maj -eq $Ver[0] -and $min -gt $Ver[1])) { return -1 }
    $type = 0
    if ($m.Groups[4].Success -and $m.Groups[4].Value -ne "") {
        $type = [Convert]::ToInt32($m.Groups[4].Value, $(if ($m.Groups[4].Value -match '^0x') { 16 } else { 10 }))
        if ($type -ne 1) { return -1 }
    }
    if ($m.Groups[5].Success -and $m.Groups[5].Value -ne "") {
        $suite = [Convert]::ToInt64($m.Groups[5].Value, $(if ($m.Groups[5].Value -match '^0x') { 16 } else { 10 }))
        if ($suite -ne 0) { return -1 }
    }
    if ($m.Groups[6].Success -and $m.Groups[6].Value -ne "") {
        $build = [int]$m.Groups[6].Value
        if ($Ver[0] -lt 10 -or $build -gt $Ver[2]) { return -1 }
    }
    return ([int64]$maj * 1000000000 + [int64]$min * 10000000 + [int64]$build * 10 + $(if ($type -ne 0) { 2 } else { 0 }) + $(if ($a -ne "") { 1 } else { 0 }))
}

function Get-InfModelIds {
    # The ids an INF's models match on one system token: the [Manufacturer]
    # entries' models sections, each with the decoration that system's
    # engine takes (none on 98 SE, ME and 2000; on XP and later the best
    # applying decoration the entry lists, else the undecorated section,
    # which 64-bit Windows does not read), over each version the token
    # stands for.
    param($Inf, [string]$Sys)
    $si = $script:OtherSysInfo[$Sys]
    $ids = @()
    foreach ($e in (Get-InfEntries -Inf $Inf -Section "Manufacturer")) {
        $f = @(Split-InfFields -Text $e.Value | Where-Object { $_ -ne "" })
        if ($f.Count -eq 0) { continue }
        $models = Expand-InfValue -Inf $Inf -Text $f[0]
        $sections = @()
        if ($si.NoDeco) {
            $sections += $models
        } else {
            foreach ($ver in $si.Vers) {
                $best = $null; $bestScore = -1
                foreach ($d in @($f | Select-Object -Skip 1 | ForEach-Object { Expand-InfValue -Inf $Inf -Text $_ })) {
                    $sc = Get-InfModelDecoScore -Deco $d -Arch $si.Arch -Ver $ver
                    if ($sc -gt $bestScore) { $best = $d; $bestScore = $sc }
                }
                if ($null -ne $best) { $sections += ($models + "." + $best) }
                elseif ($si.Arch -eq "x86") { $sections += $models }
            }
        }
        foreach ($section in @($sections | Sort-Object -Unique)) {
            foreach ($m in (Get-InfEntries -Inf $Inf -Section $section)) {
                $mf = @(Split-InfFields -Text $m.Value | ForEach-Object { (Expand-InfValue -Inf $Inf -Text $_).Trim() })
                foreach ($id in @($mf | Select-Object -Skip 1)) { if ($id -ne "") { $ids += $id.ToUpperInvariant() } }
            }
        }
    }
    return @($ids | Sort-Object -Unique)
}

function Test-OtherStageIds {
    # The pack's rule that no two STAGE lines match one id on a system,
    # held over the union of both sets (record 26 section 6.7.11): -Sets a
    # list of @{ Lines; Dir; Prefix; Name }. Throws the first id two INFs of
    # the union both match on one system.
    param($Sets)
    $seen = @{}
    foreach ($s in $Sets) {
        $pre = $s.Prefix + "/"
        foreach ($l in @($s.Lines | Where-Object { $_.Action -ceq "STAGE" })) {
            $inf = Read-InfText -Path (Join-Path $s.Dir ($l.Inf.Substring($pre.Length).Replace('/', '\')))
            foreach ($sys in $l.Systems) {
                foreach ($id in (Get-InfModelIds -Inf $inf -Sys $sys)) {
                    $k = "$sys|$id"
                    $who = "$($s.Name):$($l.Inf)"
                    if ($seen.ContainsKey($k) -and $seen[$k] -ne $who) {
                        throw "two INFs staged on $sys match $($id): $($seen[$k]) and $who. One id one STAGE line a system, over both sets (design record 26 section 6.7.11)."
                    }
                    $seen[$k] = $who
                }
            }
        }
    }
}

function Get-OtherScopes {
    # Each package of -Sets with the systems its STAGE lines name:
    # "<set>:<class>/<package>" to a list of tokens.
    param($Sets)
    $scope = [ordered]@{}
    foreach ($s in $Sets) {
        foreach ($l in @($s.Lines | Where-Object { $_.Action -ceq "STAGE" })) {
            $key = $s.Name + ":" + (Get-OtherPkgOf -Path $l.Inf -Prefix $s.Prefix)
            if (-not $scope.Contains($key)) { $scope[$key] = @() }
            $scope[$key] = @($scope[$key] + $l.Systems | Sort-Object -Unique)
        }
    }
    return $scope
}

function Get-OtherPresent {
    # Each set's packages, from every line, STAGE or COPY:
    # "<set>:<class>/<package>" list.
    param($Sets)
    $out = @()
    foreach ($s in $Sets) {
        foreach ($l in $s.Lines) { $out += ($s.Name + ":" + (Get-OtherPkgOf -Path $l.Inf -Prefix $s.Prefix)) }
    }
    return @($out | Sort-Object -Unique)
}

function Get-OtherEffectiveRows {
    # The rows the cut records and holds: the table's, a carried set's
    # PACKAGE line winning over its row (its kind and words, the table's
    # heading), a PACKAGE line with no row added, and a PACKAGE line of a
    # package the set's release has and does not ship (a package that does
    # not qualify) recorded as known, not shipped.
    param($Table, $Meta)
    $rows = @()
    foreach ($p in $Table.Pkgs) {
        $q = $p.Set + ":" + $p.Id
        $m = @($Meta | Where-Object { $_.Kind -ceq "PACKAGE" -and ($_.Set + ":" + $_.A) -ceq $q })
        if ($m.Count -gt 0 -and $m[0].Shipped) {
            $rows += [pscustomobject]@{ Id = $q; Kind = $m[0].B; Heading = $p.Heading; Desc = $m[0].Desc; State = "current" }
        } else {
            $rows += [pscustomobject]@{ Id = $q; Kind = $p.Kind; Heading = $p.Heading; Desc = $p.Desc; State = $(if ($p.Withdrawn) { "withdrawn" } else { "current" }) }
        }
    }
    foreach ($m in @($Meta | Where-Object { $_.Kind -ceq "PACKAGE" })) {
        $q = $m.Set + ":" + $m.A
        if (@($rows | Where-Object { $_.Id -ceq $q }).Count -gt 0) { continue }
        # The heading the installer gives a package its table does not
        # know (inst_other_heading_index's last, "Other").
        $rows += [pscustomobject]@{ Id = $q; Kind = $m.B; Heading = "Other"; Desc = $m.Desc; State = $(if ($m.Shipped) { "current" } else { "known" }) }
    }
    return $rows
}

function Test-OtherPkgTable {
    # The installer's table (Get-InstOtherTable) and any PACKAGE lines held
    # to the sets and the rules (record 26 section 6.7.11): every row's id,
    # heading and description by the rules, a CLASS row with no brand word;
    # every package of a carried set with exactly one current row (or its
    # PACKAGE line), and no current row of a carried set naming a package
    # it does not have; the relationship lines naming known ids, and no pair
    # named by both kinds, or twice by one kind, on a common system.
    param($Table, $Sets, $Meta, [string[]]$Brands, [string[]]$Headings)
    $known = @{}
    foreach ($p in $Table.Pkgs) {
        $q = $p.Set + ":" + $p.Id
        if (-not (Test-OtherPkgId -Id $p.Id)) { throw "the installer's table names '$q', which is not <class>/<package>." }
        if ($known.ContainsKey($q)) { throw "the installer's table names $q twice." }
        if ($Headings -cnotcontains $p.Heading) { throw "the installer's table gives $q the heading '$($p.Heading)', which is not one of its headings." }
        $why = Test-OtherPkgDesc -Kind $p.Kind -Desc $p.Desc -Brands $Brands
        if ($why -ne "") { throw "the installer's table's description of $q $why." }
        $known[$q] = $p
    }
    $present = Get-OtherPresent -Sets $Sets
    $seenMeta = @{}
    foreach ($m in @($Meta | Where-Object { $_.Kind -ceq "PACKAGE" })) {
        $q = $m.Set + ":" + $m.A
        if ($seenMeta.ContainsKey($q)) { throw "the $($m.Set) set's STAGE.TXT has two PACKAGE lines for $($m.A)." }
        $seenMeta[$q] = $true
        $known[$q] = $true
    }
    foreach ($s in $Sets) {
        $metaPkgs = @($Meta | Where-Object { $_.Set -eq $s.Name -and $_.Kind -ceq "PACKAGE" -and $_.Shipped } | ForEach-Object { $_.A })
        $have = @($present | Where-Object { $_.StartsWith($s.Name + ":") })
        foreach ($k in $have) {
            $id = $k.Substring($s.Name.Length + 1)
            if ($metaPkgs -ccontains $id) { continue }
            $rows = @($Table.Pkgs | Where-Object { $_.Set -eq $s.Name -and $_.Id -ceq $id -and -not $_.Withdrawn })
            if ($rows.Count -ne 1) { throw "$k is a package of the carried $($s.Name) set, and the installer's table has $($rows.Count) current rows for it, not one: add its row (xhciinst\inst_core.c, instOtherPkgs)." }
        }
        foreach ($p in @($Table.Pkgs | Where-Object { $_.Set -eq $s.Name -and -not $_.Withdrawn })) {
            if ($have -cnotcontains ($s.Name + ":" + $p.Id) -and $metaPkgs -cnotcontains $p.Id) {
                throw "the installer's table has a current row for $($s.Name):$($p.Id), which the carried $($s.Name) set does not have: mark it withdrawn (its words kept)."
            }
        }
    }
    # Every line of both sources is checked against every other: two kinds
    # for a pair on a common system are refused wherever they come from (the
    # installer reads the pack's lines first and then its table's, so a
    # table EXCLUDES under a pack COEXISTS would still exclude). Two lines
    # of the same kind are refused within one source; a pack line of the
    # same kind as the installer's own line for the pair (the installer's
    # proposal for the maintainer) is the pack's declaration, not a second.
    $metaRels = @($Meta | Where-Object { $_.Kind -cne "PACKAGE" })
    $tableRels = @($Table.Rels)
    $rels = @($tableRels) + @($metaRels)
    $nTable = $tableRels.Count
    for ($i = 0; $i -lt $rels.Count; $i++) {
        foreach ($q in @($rels[$i].A, $rels[$i].B)) {
            if (-not $known.ContainsKey($q)) { throw "a $($rels[$i].Kind) line names $q, which no package of either set's known metadata has ever had." }
        }
        for ($j = $i + 1; $j -lt $rels.Count; $j++) {
            $same = ($rels[$i].A -ceq $rels[$j].A -and $rels[$i].B -ceq $rels[$j].B) -or ($rels[$i].A -ceq $rels[$j].B -and $rels[$i].B -ceq $rels[$j].A)
            if (-not $same) { continue }
            $common = @($rels[$i].Systems | Where-Object { $rels[$j].Systems -ccontains $_ })
            $tableAndPack = ($i -lt $nTable) -ne ($j -lt $nTable)
            if ($common.Count -gt 0 -and -not ($tableAndPack -and $rels[$i].Kind -ceq $rels[$j].Kind)) {
                throw "$($rels[$i].A) and $($rels[$i].B) are named by a $($rels[$i].Kind) and a $($rels[$j].Kind) line on $($common -join ' '): one line a pair a system."
            }
        }
    }
}

function Get-OtherOverlaps {
    # For the maintainer, not a refusal: each pair of a CLASS package with
    # another package that stage on a common system, with the systems no
    # EXCLUDES or COEXISTS line of the pair names (record 26 section 6.7.11).
    param($Table, $Sets, $Meta)
    $scope = Get-OtherScopes -Sets $Sets
    $kind = @{}
    foreach ($p in $Table.Pkgs) { $kind[$p.Set + ":" + $p.Id] = $p.Kind }
    foreach ($m in @($Meta | Where-Object { $_.Kind -ceq "PACKAGE" })) { $kind[$m.Set + ":" + $m.A] = $m.B }
    $rels = @($Table.Rels) + @($Meta | Where-Object { $_.Kind -cne "PACKAGE" })
    $keys = @($scope.Keys)
    $out = @()
    for ($i = 0; $i -lt $keys.Count; $i++) {
        for ($j = $i + 1; $j -lt $keys.Count; $j++) {
            $a = $keys[$i]; $b = $keys[$j]
            if ($kind[$a] -cne "CLASS" -and $kind[$b] -cne "CLASS") { continue }
            $common = @($scope[$a] | Where-Object { $scope[$b] -ccontains $_ })
            if ($common.Count -eq 0) { continue }
            $stated = @()
            foreach ($r in @($rels | Where-Object { ($_.A -ceq $a -and $_.B -ceq $b) -or ($_.A -ceq $b -and $_.B -ceq $a) })) { $stated += $r.Systems }
            $open = @($common | Where-Object { $stated -cnotcontains $_ })
            if ($open.Count -gt 0) { $out += ("{0} and {1} both stage on {2}, and no EXCLUDES or COEXISTS line names them there" -f $a, $b, ($open -join " ")) }
        }
    }
    return $out
}

function ConvertTo-OtherPkgsRecord {
    # releases\<version>\otherpkgs.txt: the table's rows, current and
    # withdrawn, and every relationship line of the table and both sets.
    param([string]$Version, $Table, $Meta)
    $lines = @(
        "# otherpkgs.txt - the other USB drivers' package table this release was cut with",
        "#",
        "# Written by scripts\package\make-release.ps1 (design record 26 section",
        "# 6.7.11). The next cut reads it and refuses a table that drops a row or a",
        "# line: a row is only ever marked withdrawn, with its words kept.",
        "#",
        "#   ROW | <set>:<class>/<package> | CLASS or DEVICE | <heading> | <description> | current, withdrawn or known",
        "#   EXCLUDES or COEXISTS | <set>:<id> | <set>:<id> | <systems> | <why>",
        "",
        "Version: $Version")
    foreach ($r in (Get-OtherEffectiveRows -Table $Table -Meta $Meta)) {
        $lines += ("ROW | {0} | {1} | {2} | {3} | {4}" -f $r.Id, $r.Kind, $r.Heading, $r.Desc, $r.State)
    }
    foreach ($r in (@($Table.Rels) + @($Meta | Where-Object { $_.Kind -cne "PACKAGE" }))) {
        $lines += ("{0} | {1} | {2} | {3} | {4}" -f $r.Kind, $r.A, $r.B, ($r.Systems -join " "), $r.Desc)
    }
    return $lines
}

function ConvertFrom-OtherPkgsRecord {
    # -Fallback: the version of the release folder the record is in, for
    # a record whose Version line is empty (3.2.0.0's, cut before the
    # version reached the check; task 44.34).
    param([string]$Text, [string]$Label, [string]$Fallback = "")
    $rows = @(); $rels = @(); $ver = ""
    foreach ($line in ($Text -split "`r?`n")) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith("#")) { continue }
        if ($t -match '^Version:\s*(\S*)$') { $ver = $Matches[1]; continue }
        $f = @($line -split '\|' | ForEach-Object { $_.Trim() })
        if ($f[0] -ceq "ROW" -and $f.Count -eq 6 -and @("current", "withdrawn", "known") -ccontains $f[5]) {
            if (@($rows | Where-Object { $_.Id -ceq $f[1] }).Count -gt 0) { throw "$Label has two rows for $($f[1])." }
            $rows += [pscustomobject]@{ Id = $f[1]; Kind = $f[2]; Heading = $f[3]; Desc = $f[4]; State = $f[5] }
        } elseif (($f[0] -ceq "EXCLUDES" -or $f[0] -ceq "COEXISTS") -and $f.Count -eq 5) {
            $rels += [pscustomobject]@{ Kind = $f[0]; A = $f[1]; B = $f[2]; Systems = @($f[3] -split '\s+' | Where-Object { $_ -ne "" }); Desc = $f[4] }
        } else {
            throw "$Label has a line it cannot read: '$line'."
        }
    }
    if ($ver -eq "") { $ver = $Fallback }
    if ($ver -eq "") { throw "$Label names no version." }
    return [pscustomobject]@{ Version = $ver; Rows = $rows; Rels = $rels }
}

function Test-OtherPkgsTransition {
    # The previous release's record (-Prev, ConvertFrom-OtherPkgsRecord's)
    # against this cut's (-Now, the same): every row is still there with its
    # kind; a withdrawn row stays withdrawn with its words, or returns to
    # current only as a package a carried set ships in this cut (-Shipped,
    # Get-OtherPresent's "<set>:<class>/<package>" ids; task 44.34); every
    # relationship line is still there, on at least its systems.
    param($Prev, $Now, [string[]]$Shipped = @())
    foreach ($r in $Prev.Rows) {
        $n = @($Now.Rows | Where-Object { $_.Id -ceq $r.Id })
        if ($n.Count -eq 0) { throw "the package table drops $($r.Id), which $($Prev.Version) carried: a row is never removed, only marked withdrawn (design record 26 section 6.7.11)." }
        if ($n[0].Kind -cne $r.Kind) { throw "the package table makes $($r.Id) a $($n[0].Kind) package; $($Prev.Version) had it $($r.Kind)." }
        if ($r.State -cne "known" -and $n[0].State -ceq "known") {
            throw "$($r.Id) was $($r.State) in $($Prev.Version), and this cut has it only known, not shipped: a package a release carried is withdrawn, with its words."
        }
        if ($r.State -ceq "withdrawn" -and $n[0].State -cne "withdrawn" -and @($Shipped) -cnotcontains $r.Id) {
            throw "$($r.Id) was withdrawn in $($Prev.Version), and this table makes it $($n[0].State) though no carried set ships it: a withdrawn row stays withdrawn and keeps its words, or returns to current only with a set that ships it."
        }
        if ($n[0].State -ceq "withdrawn" -and ($n[0].Desc -cne $r.Desc -or $n[0].Heading -cne $r.Heading)) {
            throw "$($r.Id) is withdrawn here with words other than $($Prev.Version)'s: a withdrawn row keeps its words."
        }
    }
    foreach ($r in $Prev.Rels) {
        $n = @($Now.Rels | Where-Object { $_.Kind -ceq $r.Kind -and (($_.A -ceq $r.A -and $_.B -ceq $r.B) -or ($_.A -ceq $r.B -and $_.B -ceq $r.A)) })
        $kept = $false
        foreach ($x in $n) { if (@($r.Systems | Where-Object { $x.Systems -cnotcontains $_ }).Count -eq 0) { $kept = $true } }
        if (-not $kept) { throw "the package table drops the $($r.Kind) line of $($r.A) and $($r.B) on $($r.Systems -join ' '), which $($Prev.Version) carried: a line is never removed (design record 26 section 6.7.11)." }
    }
}

function Find-PreviousOtherPkgs {
    # The newest releases\<v>\otherpkgs.txt below -Version, or $null.
    param([string]$ReleasesDir, [string]$Version)
    if (-not (Test-Path -LiteralPath $ReleasesDir -PathType Container)) { return $null }
    $best = $null
    foreach ($d in (Get-ChildItem -LiteralPath $ReleasesDir -Directory)) {
        $v = $null
        if (-not [version]::TryParse($d.Name, [ref]$v)) { continue }
        if ($v -ge [version]$Version) { continue }
        $p = Join-Path $d.FullName $script:OtherPkgsRecordName
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { continue }
        if ($null -eq $best -or $v -gt $best.V) { $best = [pscustomobject]@{ V = $v; Path = $p } }
    }
    return $best
}

function Invoke-OtherSetChecks {
    # Every check of this block over the carried sets (Export-WdmFreeSet's
    # and Export-NonFreeSet's results, either $null), the installer's table
    # and the previous release's record. Returns the record's lines and the
    # overlap listing; throws the first refusal.
    param([string]$Repo, [string]$ReleasesDir, [string]$Version, $Free, $NonFree, [string]$BrandPath = "")
    $brands = Read-BrandWords -Path $BrandPath
    $table = Get-InstOtherTable -Repo $Repo
    $missing = @($brands | Where-Object { $table.Brands -cnotcontains $_ }) + @($table.Brands | Where-Object { $brands -cnotcontains $_ })
    if ($missing.Count -gt 0) { throw "xhciinst\inst_core.c's instBrandWords and scripts\package\brand-words.txt differ ($($missing -join ', ')): change the two together." }
    $hText = [System.IO.File]::ReadAllText((Join-Path $Repo "xhciinst\inst_core.c"))
    $hm = [regex]::Match($hText, 'instOtherHeadings\[\]\s*=\s*\{([^}]*)\}')
    $headings = @([regex]::Matches($hm.Groups[1].Value, '"([^"]*)"') | ForEach-Object { $_.Groups[1].Value })
    $sets = @()
    $meta = @()
    foreach ($pair in @(@("free", $Free, $script:WdmFreePrefix), @("nonfree", $NonFree, $script:NonFreePrefix))) {
        if ($null -eq $pair[1]) { continue }
        $stageText = [System.IO.File]::ReadAllText((Join-Path $pair[1].Dir "STAGE.TXT"))
        $lines = Read-StageText -Text $stageText -Label "$($pair[0])'s STAGE.TXT"
        $set = [pscustomobject]@{ Name = $pair[0]; Lines = $lines; Dir = $pair[1].Dir; Prefix = $pair[2] }
        $sets += $set
        # The metadata of the release's whole STAGE.TXT, before the
        # exporter's filtering: a package that does not qualify is known.
        $full = $stageText
        if ($pair[1].PSObject.Properties.Name -contains "FullStage") { $full = $pair[1].FullStage }
        $here = Get-OtherPresent -Sets @($set)
        foreach ($m in (Read-OtherStageMeta -Text $full -Label "$($pair[0])'s STAGE.TXT" -Brands $brands)) {
            $m | Add-Member -NotePropertyName Set -NotePropertyValue $pair[0]
            $m | Add-Member -NotePropertyName Shipped -NotePropertyValue ($m.Kind -ceq "PACKAGE" -and $here -ccontains ($pair[0] + ":" + $m.A))
            $meta += $m
        }
    }
    # The free set's tools are not packages: no STAGE.TXT line and no INF.
    foreach ($s in @($sets | Where-Object { $_.Name -ceq "free" })) {
        $tpre = $s.Prefix + "/" + $script:WdmFreeToolsDir + "/"
        $tl = @($s.Lines | Where-Object { $_.Inf.StartsWith($tpre, [System.StringComparison]::OrdinalIgnoreCase) })
        if ($tl.Count -gt 0) { throw "the free set's STAGE.TXT names $($tl[0].Inf): $tpre holds tools, not driver packages." }
        $tdir = Join-Path $s.Dir $script:WdmFreeToolsDir
        if (Test-Path -LiteralPath $tdir) {
            $ti = @(Get-ChildItem -LiteralPath $tdir -Recurse -File -Filter "*.inf")
            if ($ti.Count -gt 0) { throw "the free set's $($script:WdmFreeToolsDir)\ holds an INF, $($ti[0].FullName): a tool is not a driver package." }
        }
    }
    $allInfs = @()
    foreach ($s in $sets) { $allInfs += @(Get-ChildItem -LiteralPath $s.Dir -Recurse -File -Filter "*.inf" | ForEach-Object { $_.Name.ToLowerInvariant() }) }
    foreach ($s in $sets) {
        Test-OtherStagePackages -Lines $s.Lines -Dir $s.Dir -Prefix $s.Prefix -Label "the $($s.Name) set's STAGE.TXT" -AllInfs $allInfs
    }
    Test-OtherStageIds -Sets $sets
    Test-OtherPkgTable -Table $table -Sets $sets -Meta $meta -Brands $brands -Headings $headings
    $record = ConvertTo-OtherPkgsRecord -Version $Version -Table $table -Meta $meta
    $prev = $null
    $vParsed = $null
    if ([version]::TryParse($Version, [ref]$vParsed)) { $prev = Find-PreviousOtherPkgs -ReleasesDir $ReleasesDir -Version $Version }
    if ($null -ne $prev) {
        $p = ConvertFrom-OtherPkgsRecord -Text ([System.IO.File]::ReadAllText($prev.Path)) -Label "'$($prev.Path)'" -Fallback ($prev.V.ToString())
        $n = ConvertFrom-OtherPkgsRecord -Text ($record -join "`r`n") -Label "this cut's otherpkgs.txt"
        Test-OtherPkgsTransition -Prev $p -Now $n -Shipped (Get-OtherPresent -Sets $sets)
    }
    return [pscustomobject]@{ Record = $record; Overlaps = @(Get-OtherOverlaps -Table $table -Sets $sets -Meta $meta); Previous = $prev }
}

function Get-InstOtherSums {
    # The hashes the installer carries for this release's sets
    # (xhciinst\inst_core.h): Free and NonFree, upper case, "" for none.
    param([string]$Repo)
    $text = [System.IO.File]::ReadAllText((Join-Path $Repo "xhciinst\inst_core.h"))
    $r = @{}
    foreach ($pair in @(@("Free", "INST_WDMFREE_SUMS"), @("NonFree", "INST_NONFREE_SUMS"))) {
        $m = [regex]::Match($text, '(?m)^#define\s+' + $pair[1] + '\s+"([0-9A-Fa-f]{64})?"')
        if (-not $m.Success) { throw "xhciinst\inst_core.h has no #define $($pair[1]) of a SHA-256 or an empty string." }
        $r[$pair[0]] = $m.Groups[1].Value.ToUpperInvariant()
    }
    return $r
}

function Get-DownloadPaths {
    # The workspace and the zip of one download under -UploadDir.
    param([string]$UploadDir, [string]$Version, [string]$Kind, [switch]$Staged)
    $suffix = if ($Staged) { ".staging" } else { "" }
    $k = $script:DownloadKindTable[$Kind].Suffix
    return [pscustomobject]@{
        Root = Join-Path $UploadDir ("upload-" + $Version + $k + $suffix)
        Zip  = Join-Path $UploadDir ("xhci98-" + $Version + $k + ".zip" + $suffix)
    }
}

function Get-DownloadZipName {
    param([string]$Version, [string]$Kind)
    return ("xhci98-" + $Version + $script:DownloadKindTable[$Kind].Suffix + ".zip")
}

function Test-DownloadsRecordRequired {
    param([string]$Version)
    return ([version]$Version -ge [version]"3.2.0.0")
}

function ConvertTo-DownloadsRecord {
    # The tracked record in releases\<version>\ of which downloads the
    # release has and which sets they carry: what -UploadSetOnly rebuilds
    # downloads 2 and 3 from. $Free and $NonFree are the exports, or $null.
    param([string]$Version, [string[]]$Kinds, $Free, $NonFree)
    $lines = @(
        "xhci98 $Version - the release's downloads",
        "Written by scripts\package\make-release.ps1 (roadmap-usb4 task 43.48) and",
        "read back by its -UploadSetOnly. The sets' files are never in this directory.",
        "",
        ("Downloads: " + ($Kinds -join " "))
    )
    $lines += $(if ($null -eq $Free) { "Free: none" } else { "Free: $($Free.Tag) $($Free.Revision) $($Free.Sums)" })
    $lines += $(if ($null -eq $NonFree) { "NonFree: none" } else { "NonFree: $($NonFree.Tag) $($NonFree.Revision) $($NonFree.Sums) $($NonFree.PackSums)" })
    return $lines
}

function ConvertFrom-DownloadsRecord {
    # The record ConvertTo-DownloadsRecord writes, read back strictly.
    param([string]$Text, [string]$Label)
    $kinds = $null
    $sets = @{}
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^Downloads: ([a-z ]+)$') {
            if ($null -ne $kinds) { throw "$Label says Downloads twice." }
            $kinds = @($Matches[1].Trim() -split ' ')
        } elseif ($line -match '^(Free|NonFree): none$') {
            if ($sets.ContainsKey($Matches[1])) { throw "$Label says $($Matches[1]) twice." }
            $sets[$Matches[1]] = $null
        } elseif ($line -match '^Free: (\S+) ([0-9a-f]{40}) ([0-9A-F]{64})$') {
            if ($sets.ContainsKey("Free")) { throw "$Label says Free twice." }
            $sets["Free"] = [pscustomobject]@{ Tag = $Matches[1]; Revision = $Matches[2]; Sums = $Matches[3]; PackSums = "" }
        } elseif ($line -match '^NonFree: (\S+) ([0-9a-f]{40}) ([0-9A-F]{64}) ([0-9A-F]{64})$') {
            if ($sets.ContainsKey("NonFree")) { throw "$Label says NonFree twice." }
            $sets["NonFree"] = [pscustomobject]@{ Tag = $Matches[1]; Revision = $Matches[2]; Sums = $Matches[3]; PackSums = $Matches[4] }
        } elseif ($line -match '^(Downloads|Free|NonFree):') {
            throw "$Label has a line it cannot read: '$line'."
        }
    }
    if ($null -eq $kinds -or -not $sets.ContainsKey("Free") -or -not $sets.ContainsKey("NonFree")) {
        throw "$Label does not say Downloads, Free and NonFree."
    }
    foreach ($k in $kinds) { if (-not $script:DownloadKindTable.Contains($k)) { throw "$Label names a download '$k'." } }
    if ($kinds[0] -ne "base" -or @($kinds | Sort-Object -Unique).Count -ne $kinds.Count) { throw "$Label's downloads do not start with base, once each." }
    if (($kinds -contains "free") -ne ($null -ne $sets["Free"])) { throw "$Label's free download and its Free line disagree." }
    if (($kinds -contains "nonfree") -ne ($null -ne $sets["NonFree"])) { throw "$Label's non-free download and its NonFree line disagree." }
    return [pscustomobject]@{ Kinds = $kinds; Free = $sets["Free"]; NonFree = $sets["NonFree"] }
}

# --- each download's readme (task 43.48; design record 26 section 6.7.1) ---
#
# The published readme.txt is download 1's. The other downloads' readmes
# are the same text with three of its parts replaced by their own, each a
# function of the version, the record of the downloads and whether the
# download carries NUSB 3.3's files: so -UploadSetOnly renders the same
# bytes from the published readme and downloads.txt alone.

function Get-ReadmeMsFilesText {
    # The readme's {MSFILES} paragraph for a download.
    param([bool]$DriverPackIncluded, [string]$Kind)
    if ($Kind -eq "nonfree") {
        if (-not $DriverPackIncluded) { throw "a download with USBDRV\NONFREE\ is cut only with the driver pack, which also gives USBDRV\NUSB33\." }
        return @'
Microsoft files are in this download in two places. The first is the seven
Windows 98 SE storage files of section 3, in a folder of their own, copied
unmodified from NUSB 3.3 (Microsoft's, apart from USBU2A.SYS, a third
party's). The second is the KMDF co-installers WdfCoInstaller01007.dll and
WdfCoInstaller01009.dll. Makers ship them with some of their USB Ethernet
drivers, and they are in several of those drivers' folders in
USBDRV\NONFREE\, unmodified. None of them is this project's work. No
licence to redistribute them has been established, nothing here grants you
any right in them, and the NOTICE.md beside each says whose they are. The
usbd.sys and usbui.dll the install needs are not in this download: Windows
copies them from your own Windows installation source (section 3).
'@.TrimEnd()
    }
    if ($DriverPackIncluded) {
        return @'
Microsoft files are in this download in one place only: the seven Windows
98 SE storage files of section 3, in a folder of their own, copied
unmodified from NUSB 3.3 (Microsoft's, apart from USBU2A.SYS, a third
party's). They are not this project's work. No licence to redistribute them
has been established, nothing here grants you any right in them, and the
NOTICE.md beside them says whose they are. The usbd.sys and usbui.dll the
install needs are not in this download: Windows copies them from your own
Windows installation source (section 3).
'@.TrimEnd()
    }
    return @'
No Microsoft file is in this download. The usbd.sys and usbui.dll the
install needs are copied by Windows from your own Windows installation
source (section 3), and on Windows 98 SE the storage files of section 3 come
from NUSB; nothing here grants you any right in them, and nothing here
copies them. xhciuas.inf names NUSB's USBNTMAP.SYS on Windows 98 SE; the
file is named, never shipped.
'@.TrimEnd()
}

function Format-ReadmeWrap {
    # A paragraph's words in lines of at most -Width columns, -Lead before
    # the first and -Rest before every other.
    param([string]$Text, [int]$Width = 76, [string]$Lead = "", [string]$Rest = "")
    $lines = @()
    $line = $Lead
    $fresh = $true
    foreach ($w in @($Text -split ' ' | Where-Object { $_ -ne "" })) {
        if (-not $fresh -and ($line.Length + 1 + $w.Length) -gt $Width) {
            $lines += $line
            $line = $Rest + $w
        } elseif ($fresh) {
            $line = $line + $w
        } else {
            $line = $line + " " + $w
        }
        $fresh = $false
    }
    if (-not $fresh) { $lines += $line }
    return $lines
}

function Get-ReadmeDownloadText {
    # The readme's "THIS DOWNLOAD" body for download -Kind of a release
    # whose downloads are -Kinds; -FreeTag and -NonFreeTag the sets' tags,
    # "" for none.
    param([string]$Version, [string]$Kind, [string[]]$Kinds, [string]$FreeTag, [string]$NonFreeTag,
          [bool]$DriverPackIncluded = $true)
    $z1 = Get-DownloadZipName -Version $Version -Kind "base"
    $z2 = Get-DownloadZipName -Version $Version -Kind "free"
    $z3 = Get-DownloadZipName -Version $Version -Kind "nonfree"
    $L = @()
    switch ($Kind) {
        "base" {
            if ($DriverPackIncluded) {
                $L += "This is $z1, the base download of xhci98 ${Version}: the drivers,"
                $L += "their tools and installer, and NUSB 3.3's Windows 98 SE storage files"
                $L += "(USBDRV\NUSB33\)."
            } else {
                $L += "This is $z1, the base download of xhci98 ${Version}: the drivers,"
                $L += "their tools and installer."
            }
        }
        "free" {
            $L += Format-ReadmeWrap -Text ("This is ${z2}: the base download, $z1, with the free USB " +
                                          "class drivers of usb-wdm-free $FreeTag added in USBDRV\FREE\.")
        }
        "nonfree" {
            if ($FreeTag -ne "") {
                $L += Format-ReadmeWrap -Text ("This is ${z3}: $z2 (the base download with the free USB " +
                                              "class drivers of usb-wdm-free $FreeTag in USBDRV\FREE\), with " +
                                              "the makers' Ethernet, HID and serial drivers of usb-wdm-nonfree " +
                                              "$NonFreeTag added in USBDRV\NONFREE\.")
            } else {
                $L += Format-ReadmeWrap -Text ("This is ${z3}: the base download, $z1, with the makers' " +
                                              "Ethernet, HID and serial drivers of usb-wdm-nonfree $NonFreeTag " +
                                              "added in USBDRV\NONFREE\.")
            }
            $L += Format-ReadmeWrap -Text ("Wi-Fi drivers are not in this download. Get them from the " +
                                          "usb-wdm-nonfree release (https://github.com/yeokm1/usb-wdm-nonfree).")
        }
        default { throw "no download '$Kind'." }
    }
    $L += ""
    $L += "This release has these downloads, each holding the one before it:"
    $L += ""
    $L += ("  {0,-26} the base download" -f $z1)
    if ($Kinds -contains "free") { $L += ("  {0,-26} adds the free USB class drivers (USBDRV\FREE\)" -f $z2) }
    # Download 1's own text, which ConvertTo-DownloadReadme finds in a
    # published readme: keep it as 3.2.0.0 published it.
    if ($Kinds -contains "nonfree") { $L += ("  {0,-26} adds the non-free driver pack (USBDRV\NONFREE\)" -f $z3) }
    $L += ""
    if ($Kinds -notcontains "free") {
        $L += "No free USB class driver is in any download of this release yet: none"
        $L += "has passed its tests on its systems."
        $L += ""
    }
    $L += "The installer copies those drivers only when you tick them, and never"
    $L += "installs one by itself. The up-to-date drivers, free and non-free, are at:"
    $L += ""
    $L += "  https://github.com/yeokm1/usb-wdm-free"
    $L += "  https://github.com/yeokm1/usb-wdm-nonfree"
    if ($Kind -ne "base" -and $FreeTag -ne "") {
        $L += ""
        $L += "The free USB class drivers: These drivers are experimental. They may"
        $L += "not work with every device and can make Windows unstable. Report"
        $L += "problems at https://github.com/yeokm1/usb-wdm-free."
    }
    if ($Kind -eq "nonfree") {
        $L += ""
        $L += "USBDRV\NONFREE\ holds other manufacturers' drivers (USB Ethernet, serial"
        $L += "and game pad drivers), unmodified, as their manufacturers released them."
        $L += "They are not this project's work. USBDRV\NONFREE\NOTICE.md says whose"
        $L += "each one is, and quotes what some of their manufacturers said about"
        $L += "passing them on; apart from that, no licence to redistribute them has"
        $L += "been found."
    }
    return ($L -join "`r`n")
}

function Get-ReadmeOtherContentsText {
    # The readme's "What is in this directory" lines for the other drivers.
    param([string]$Kind, [string]$FreeTag, [string]$NonFreeTag, [string[]]$FreeTools = @())
    $L = @()
    if ($Kind -eq "base" -or ($Kind -eq "nonfree" -and $FreeTag -eq "")) {
        if ($Kind -eq "base") {
            $L += "  (No other USB drivers in this download: see THIS DOWNLOAD, at the top.)"
            return ($L -join "`r`n")
        }
    } else {
        $L += Format-ReadmeWrap -Lead "  USBDRV\FREE\    - " -Rest "                    " `
                  -Text "the free USB class drivers (experimental), from usb-wdm-free $FreeTag"
        $L += ""
        $L += "      LICENSE      their licence, the GNU GPL v2"
        $L += "      NOTICE.md    the code in them adapted from other projects"
        $L += "      READ.TXT     the systems each driver was tested on"
        $L += "      SHA256.TXT   every file's hash"
        $L += "      STAGE.TXT    which INF Windows may use on which system"
        foreach ($tl in $FreeTools) {
            $tlLead = "      tools\" + $tl + "\ "
            $L += Format-ReadmeWrap -Lead $tlLead -Rest (" " * $tlLead.Length) -Text $script:WdmFreeToolWords[$tl]
        }
    }
    if ($Kind -eq "nonfree") {
        if ($L.Count -gt 0) { $L += "" }
        $L += Format-ReadmeWrap -Lead "  USBDRV\NONFREE\ - " -Rest "                    " `
                  -Text "other manufacturers' drivers, not this project's work, from usb-wdm-nonfree $NonFreeTag"
        $L += ""
        $L += "      NOTICE.md    whose each one is"
        $L += "      README.md    which folder serves which Windows"
        $L += "      SHA256.TXT   every file's hash"
        $L += "      STAGE.TXT    which INF Windows may use on which system"
    }
    return ($L -join "`r`n")
}

function ConvertTo-DownloadReadme {
    # Download -Kind's readme from the published (download 1's) -Text: each
    # of the three parts, which must be there exactly once as download 1's,
    # replaced by -Kind's.
    param([string]$Text, [string]$Version, [string]$Kind, [string[]]$Kinds, [string]$FreeTag, [string]$NonFreeTag,
          [bool]$DriverPackIncluded, [string]$Label, [string[]]$FreeTools = @())
    if ($Kind -eq "base") { return $Text }
    $pairs = @(
        @((Get-ReadmeDownloadText -Version $Version -Kind "base" -Kinds $Kinds -FreeTag $FreeTag -NonFreeTag $NonFreeTag -DriverPackIncluded $DriverPackIncluded),
          (Get-ReadmeDownloadText -Version $Version -Kind $Kind -Kinds $Kinds -FreeTag $FreeTag -NonFreeTag $NonFreeTag -DriverPackIncluded $DriverPackIncluded)),
        @((Get-ReadmeOtherContentsText -Kind "base" -FreeTag $FreeTag -NonFreeTag $NonFreeTag),
          (Get-ReadmeOtherContentsText -Kind $Kind -FreeTag $FreeTag -NonFreeTag $NonFreeTag -FreeTools $FreeTools)),
        @((Get-ReadmeMsFilesText -DriverPackIncluded $DriverPackIncluded -Kind "base"),
          (Get-ReadmeMsFilesText -DriverPackIncluded $DriverPackIncluded -Kind $Kind))
    )
    foreach ($p in $pairs) {
        $at = $Text.IndexOf($p[0], [System.StringComparison]::Ordinal)
        if ($at -lt 0 -or $Text.IndexOf($p[0], $at + 1, [System.StringComparison]::Ordinal) -ge 0) {
            throw "$Label does not hold download 1's text exactly once where download '$Kind' replaces it (the first line: '$(($p[0] -split "`r`n")[0])'): the published readme is not this release's."
        }
        $Text = $Text.Substring(0, $at) + $p[1] + $Text.Substring($at + $p[0].Length)
    }
    return $Text
}
