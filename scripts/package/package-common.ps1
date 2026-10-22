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
# file digest, no timestamp, no catalog. make-package.ps1 -TestSignPfx
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
    if ($dataType.End -lt $data.End) {
        $value = Read-DerTlv -B $Content -At $dataType.End -Limit $data.End
        if ($value.End -ne $data.End) { throw "SpcIndirectDataContent.data has more than a type and a value" }
        Assert-SpcPeImageData -B $Content -At $dataType.End -End $data.End
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
        DataOid   = ConvertFrom-DerOid -B $Content -At $dataType.ValueAt -Length $dataType.Length
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

    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $ck = [int]$layout.ChecksumOffset
        $en = [int]$layout.EntryOffset
        [void]$sha1.TransformBlock($bytes, 0, $ck, $null, 0)
        [void]$sha1.TransformBlock($bytes, $ck + 4, $en - ($ck + 4), $null, 0)
        [void]$sha1.TransformBlock($bytes, $en + 8, $off - ($en + 8), $null, 0)
        [void]$sha1.TransformFinalBlock($bytes, 0, 0)
        $actual = (($sha1.Hash | ForEach-Object { $_.ToString("X2") }) -join "")
    } finally {
        $sha1.Dispose()
    }

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
#          src\stor98\out\free\win9x and \x86, and inf\stor98-x86.inf as
#          stor98.inf
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
# compiles (test\run-host-tests.cmd) and the same kinds in its directory;
# test\ itself; the import gates' scripts\import-gate\; Stor98's sources,
# build scripts and unit tests; and the scripts that build the binaries and
# judge them for the record, with scripts\common.ps1, which they dot-source.
# Over-covering is the safe side: a file wrongly
# counted costs a rebuild, one wrongly left out lets the host suite's and the
# import gates' old verdict stand for a tree they never read. What the
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
    foreach ($m in [regex]::Matches([System.IO.File]::ReadAllText($hostSuite), '\.\.\\[^\s"]+')) {
        $full = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $Repo "test") $m.Value))
        if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
        $rel = $full.Substring($root.Length)
        [void]$exact.Add($rel)
        [void]$dirs.Add((Split-Path -Parent $rel))
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
$script:DriverPackFolder = "NUSB33"
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
