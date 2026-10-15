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
# has always demanded exactly one; this did not until a Codex review pointed
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
    # cannot disagree about what a marker is (audit J6). What differs is the
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
    # byte, is refused - review round 2).
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
    # own schema rather than walked as generic DER (review rounds 2 to 4):
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
    # anything (review rounds 1 and 2). Returns the data type's OID, the
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
    # bytes (review round 1).
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

# The field reports' batch files (roadmap-hcd task 38.1): SNAPON.BAT,
# SNAPGET.BAT and SNAPOFF.BAT beside XHCISNAP.EXE, run by COMMAND.COM on
# Windows 98 SE and ME and by cmd.exe from 2000 to 7, so they carry
# portsw-on.bat's pinned split; QUALREP.BAT beside XHCIQUAL.EXE, run by
# MS-DOS's COMMAND.COM alone, so it carries none. One definition of what each
# must say, read by test-package.ps1 against the repository's sources and by
# make-release.ps1 against the batches a cut stages and an upload set
# assembles (Assert-ReportBatchContent), so a stale or malformed batch in a
# -SnapToolDir, a -QualtoolDir or a published tree is refused as the
# repository's own would be. Name is the source name in the tool's own
# directory (Dir); make-release.ps1's $reportBatches maps it to the
# published 8.3 name.
#
# ArgPass, where a spec has it, is the batch's optional argument (owner,
# 2026-10-13: SNAPON.BAT LEVEL, SNAPGET.BAT NAME), passed on as it came:
# Args ends in %1, and that one run line is the only line that may carry
# %1. The default and the check are the tool's (XHCISNAP -on, -report
# NAME), so no `if` reads %1 (Codex review of 38.1, round 3: a quoted or
# metacharacter %1 broke cmd.exe's `if "%1"==` before the tool could refuse
# it, and a goto change could route around a level test). No other percent
# variable (%2, %0, %*, %~1, %DATE%, ...) is admitted anywhere.
#
# ToolLast (owner, 2026-10-13: the batch's last line of output names the
# files to attach): the tool line is the last command before pause, reached
# by `if exist TOOL goto X` with :X just above it, and nothing reads the exit
# code - the tool prints the outcome. QUALREP.BAT keeps its flag logic.
function Get-ReportBatchSpecs {
    return @(
        @{ Name = "snapon.bat";  Dir = "xhcisnap"; Win = $true;  Tool = "XHCISNAP"; Args = "-on %1";       Names = @(); ArgPass = $true; ToolLast = $true },
        @{ Name = "snapget.bat"; Dir = "xhcisnap"; Win = $true;  Tool = "XHCISNAP"; Args = "-report %1";   Names = @(".DEV", ".TXT"); ArgPass = $true; ToolLast = $true },
        @{ Name = "snapoff.bat"; Dir = "xhcisnap"; Win = $true;  Tool = "XHCISNAP"; Args = "-disable";     Names = @(); ToolLast = $true },
        @{ Name = "QUALREP.BAT"; Dir = "xhciqual"; Win = $false; Tool = "XHCIQUAL"; Args = "--probe-only --no-page --log-stamp"; Names = @("LOG"); Flag = "QUALREP.FLG" }
    )
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
# published tools\silent\ to (Assert-SilentInstallContent; Codex review of
# 38.2, round 1). The batches are run by COMMAND.COM on Windows 98 SE and
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

# ---- nvme98, the bundled NVMe driver ----------------------------------------
#
# nvme98 is a separate project under BSD-3-Clause. A release package may
# carry one of its released builds, copied unchanged from that build's
# release folder at packaging time: never its source, and never through git
# (nothing of it is tracked here, and make-release.ps1 copies it into the
# upload set only, not into releases\). The input is pinned three ways: the
# version, the folder, and the SHA-256 of the folder's SHA256SUMS.txt, which
# in turn pins every file. There are two layouts:
#
#   0.1.x, from 0.1.1.0 (old): win9x\, x86\ and amd64\, each with its own
#       nvme98.inf, bundled unchanged under nvme98\ in the flavour directory:
#       nvme98\win9x\ and nvme98\x86\ in an x86 package, nvme98\amd64\ in an
#       amd64 one;
#   0.2.0.0 and later (new): x86\ (one $CHICAGO$ INF, nvme98.mpd for 98 and
#       ME and nvme98.sys for 2000 to 7) copied to an x86 package's root, and
#       x64\ to an amd64 package's root; every name there starts with
#       "nvme98", so none can clash with this project's own files.
#
# LICENSE, NOTICE.TXT and readme.txt go beside the driver files as
# nvme98-LICENSE.txt, nvme98-NOTICE.txt and nvme98-readme.txt. 0.1.0.0 is
# never bundled: it installed nvme98 as a boot-start driver. The package
# checks that would otherwise judge these files as this project's own are
# given exactly the list Get-Nvme98PackageFiles returns, and nothing wider.

$script:Nvme98RefusedVersions = @("0.1.0.0")
$script:Nvme98OldLayoutFrom = [version]"0.1.1.0"
$script:Nvme98NewLayoutFrom = [version]"0.2.0.0"
$script:Nvme98RenamedTop = [ordered]@{
    "LICENSE"    = "nvme98-LICENSE.txt"
    "NOTICE.TXT" = "nvme98-NOTICE.txt"
    "readme.txt" = "nvme98-readme.txt"
}
# A copy of the interface header a release may carry; used for the
# interface check only and never shipped.
$script:Nvme98ReleaseHeader = "include\nvme98cm.h"

function Get-Nvme98Layout {
    # "old" or "new" for a version a package may bundle; throws otherwise.
    param([string]$Version)
    if ($Version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}\.\d{1,5}$') {
        throw "-Nvme98Version '$Version' is not a four-part version such as 0.1.1.0: the bundled nvme98 build is pinned explicitly."
    }
    if ($script:Nvme98RefusedVersions -contains $Version) {
        throw "nvme98 $Version is never bundled: it installed nvme98 as a boot-start driver, and Windows 7 x64 then fails to start without test-signing mode (nvme98's release history, 0.1.1.0)."
    }
    $v = [version]$Version
    if ($v -lt $script:Nvme98OldLayoutFrom) {
        throw "nvme98 $Version is older than 0.1.1.0, the first build a package may bundle."
    }
    if ($v -ge $script:Nvme98NewLayoutFrom) { return "new" }
    return "old"
}

function Read-Nvme98Sums {
    # SHA256SUMS.txt's text, "<sha256>  <path>" or "<sha256> *<path>" per
    # line, with / or \ separators. Returns an ordered map of relative path
    # (with \) to upper-case hash; refuses anything else.
    param([string]$Text)
    $map = [ordered]@{}
    $seen = @{}
    $n = 0
    foreach ($line in ($Text -split "\r?\n")) {
        $n++
        if ($line.Trim() -eq "") { continue }
        if ($line -notmatch '^([0-9A-Fa-f]{64}) [ *](.+)$') {
            throw "SHA256SUMS.txt line $n is not '<sha256>  <path>': $line"
        }
        $hash = $Matches[1].ToUpperInvariant()
        $rel = $Matches[2].Trim().Replace('/', '\')
        $parts = @($rel.Split('\'))
        if ($rel -eq "" -or $rel.StartsWith('\') -or $rel.Contains(':') -or
            ($parts -contains '..') -or ($parts -contains '.') -or ($parts -contains '')) {
            throw "SHA256SUMS.txt line $n names '$rel', which is not a plain path inside the release folder."
        }
        if ($rel -ieq "SHA256SUMS.txt") {
            throw "SHA256SUMS.txt lists itself (line $n)."
        }
        $key = $rel.ToLowerInvariant()
        if ($seen.ContainsKey($key)) {
            throw "SHA256SUMS.txt lists '$rel' twice (line $n)."
        }
        $seen[$key] = $true
        $map[$rel] = $hash
    }
    if ($map.Count -eq 0) { throw "SHA256SUMS.txt lists no file." }
    return $map
}

function Get-Nvme98InfVersion {
    param([string]$Path)
    $text = [System.IO.File]::ReadAllText($Path)
    $m = [regex]::Match($text, '(?m)^\s*DriverVer\s*=\s*[^,\r\n]+,\s*([0-9.]+)')
    if (-not $m.Success) { return "" }
    return $m.Groups[1].Value
}

function Assert-Nvme98Release {
    # The pinned release folder, verified: SHA256SUMS.txt's own hash first,
    # then every file against it, every file on disk listed, the layout's
    # files present and nothing it does not know, and every INF's DriverVer
    # at the pinned version. Returns what the package step needs; throws on
    # any mismatch, before anything is copied.
    param([string]$Dir, [string]$Version, [string]$SumsSha256)

    $layout = Get-Nvme98Layout -Version $Version
    if ($SumsSha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw "-Nvme98SumsSha256 must be the SHA-256 (64 hex digits) of the release folder's SHA256SUMS.txt."
    }
    if ($Dir -eq "" -or -not (Test-Path -LiteralPath $Dir -PathType Container)) {
        throw "no nvme98 release folder at '$Dir' (-Nvme98Dir)."
    }
    $Dir = (Resolve-Path -LiteralPath $Dir).ProviderPath.TrimEnd('\')
    $sumsPath = Join-Path $Dir "SHA256SUMS.txt"
    if (-not (Test-Path -LiteralPath $sumsPath -PathType Leaf)) {
        throw "'$Dir' has no SHA256SUMS.txt, so nothing in it can be checked; it is not an nvme98 release folder."
    }
    # Read once: the bytes that are hashed are the bytes that are parsed, so
    # the list cannot be swapped between the pin check and its use.
    $sumsBytes = [System.IO.File]::ReadAllBytes($sumsPath)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $sumsHash = (($sha.ComputeHash($sumsBytes) | ForEach-Object { $_.ToString("X2") }) -join "") }
    finally { $sha.Dispose() }
    if ($sumsHash -ne $SumsSha256.ToUpperInvariant()) {
        throw "'$sumsPath' hashes to $sumsHash, not the pinned $($SumsSha256.ToUpperInvariant()): this is not the nvme98 $Version release that was pinned. Nothing was copied."
    }
    if (@($sumsBytes | Where-Object { $_ -ge 0x80 }).Count -gt 0) {
        throw "'$sumsPath' is not ASCII."
    }
    $sums = Read-Nvme98Sums -Text ([System.Text.Encoding]::ASCII.GetString($sumsBytes))
    $listed = @{}
    foreach ($rel in $sums.Keys) {
        $listed[$rel.ToLowerInvariant()] = $rel
        $full = Join-Path $Dir $rel
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
            throw "SHA256SUMS.txt lists '$rel', which is not in '$Dir'."
        }
        $h = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
        if ($h -ne $sums[$rel]) {
            throw "'$rel' in '$Dir' hashes to $h, not the $($sums[$rel]) SHA256SUMS.txt lists. Release folders are write-once; this one has changed. Nothing was copied."
        }
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $Dir -File -Recurse -Force)) {
        $rel = $item.FullName.Substring($Dir.Length + 1)
        if ($rel -ieq "SHA256SUMS.txt") { continue }
        if (-not $listed.ContainsKey($rel.ToLowerInvariant())) {
            throw "'$rel' is in '$Dir' but not in its SHA256SUMS.txt, so it is not part of the release."
        }
    }

    if ($layout -eq "old") {
        $archDirs = @("win9x", "x86", "amd64")
        $required = @("win9x\nvme98.inf", "win9x\nvme98.mpd", "x86\nvme98.inf", "x86\nvme98.sys",
                      "amd64\nvme98.inf", "amd64\nvme98.sys", "amd64\nvme98-test.cer", "LICENSE", "NOTICE.TXT",
                      "readme.txt")
    } else {
        $archDirs = @("x86", "x64")
        $required = @("x86\nvme98.inf", "x86\nvme98.mpd", "x86\nvme98.sys",
                      "x64\nvme98.inf", "x64\nvme98.sys", "x64\nvme98-test.cer", "LICENSE", "NOTICE.TXT",
                      "readme.txt")
    }
    foreach ($r in $required) {
        if (-not $listed.ContainsKey($r.ToLowerInvariant())) {
            throw "the nvme98 $Version release has no '$r', which its $layout layout carries (version $Version selects the $layout layout)."
        }
    }
    $header = $null
    foreach ($rel in $sums.Keys) {
        $parts = @($rel.Split('\'))
        if ($rel -ieq $script:Nvme98ReleaseHeader) {
            $header = Join-Path $Dir $rel
            continue
        }
        if ($parts.Count -eq 1) {
            if (-not ($script:Nvme98RenamedTop.Keys | Where-Object { $_ -ieq $rel })) {
                throw "the nvme98 $Version release has '$rel' at its top, which this script does not know where to put. Update scripts\package\package-common.ps1 for the new layout rather than bundling a file blind."
            }
            continue
        }
        if ($parts.Count -ne 2 -or -not ($archDirs -contains $parts[0].ToLowerInvariant())) {
            throw "the nvme98 $Version release has '$rel', outside the $($archDirs -join '\, ')\ directories of its $layout layout."
        }
        if ($layout -eq "new" -and -not $parts[1].StartsWith("nvme98", [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "the nvme98 $Version release has '$rel': in the new layout every file goes to the package root, and a name not starting with nvme98 could clash with this project's own files."
        }
    }
    foreach ($rel in $sums.Keys) {
        if ($rel -notmatch '(?i)\.inf$') { continue }
        $infVer = Get-Nvme98InfVersion -Path (Join-Path $Dir $rel)
        if ($infVer -ne $Version) {
            throw "'$rel' of the nvme98 release declares DriverVer '$infVer', not the pinned $Version."
        }
    }
    return [pscustomobject]@{
        Dir        = $Dir
        Version    = $Version
        Layout     = $layout
        Sums       = $sums
        SumsSha256 = $sumsHash
        Header     = $header
    }
}

function Get-Nvme98PackageFiles {
    # The nvme98 files one package carries: Source (relative to the release
    # folder), Dest (relative to the package root) and Sha256. -Arch is the
    # packager's: x86 or amd64.
    param($Release, [ValidateSet("x86", "amd64")][string]$Arch)
    if ($Release.Layout -eq "old") {
        $dirs = if ($Arch -eq "amd64") { @("amd64") } else { @("win9x", "x86") }
        $prefix = "nvme98\"
    } else {
        $dirs = if ($Arch -eq "amd64") { @("x64") } else { @("x86") }
        $prefix = ""
    }
    $out = @()
    $dests = @{}
    foreach ($rel in $Release.Sums.Keys) {
        $parts = @($rel.Split('\'))
        $dest = $null
        if ($parts.Count -eq 1) {
            $top = @($script:Nvme98RenamedTop.Keys | Where-Object { $_ -ieq $rel })
            if ($top.Count -eq 1) { $dest = $prefix + $script:Nvme98RenamedTop[$top[0]] }
        } elseif ($parts.Count -eq 2 -and ($dirs -contains $parts[0].ToLowerInvariant())) {
            $dest = if ($Release.Layout -eq "old") { $prefix + $rel } else { $parts[1] }
        }
        if ($null -eq $dest) { continue }
        $key = $dest.ToLowerInvariant()
        if ($dests.ContainsKey($key)) {
            throw "two files of the nvme98 $($Release.Version) release would land at '$dest' in the $Arch package."
        }
        $dests[$key] = $true
        $out += [pscustomobject]@{ Source = $rel; Dest = $dest; Sha256 = $Release.Sums[$rel] }
    }
    return $out
}

function Get-Nvme98HeaderVersion {
    # NVME98LS_VERSION of an nvme98cm.h, or "" where it has none.
    param([string]$Path)
    $m = [regex]::Match([System.IO.File]::ReadAllText($Path), '(?m)^\s*#\s*define\s+NVME98LS_VERSION\s+(\d+)\b')
    if (-not $m.Success) { return "" }
    return $m.Groups[1].Value
}

function Test-Nvme98Interface {
    # The pinned build's link-state interface version (NVME98LS_VERSION, or
    # "none" for a build with no receiver, such as 0.1.x) against the header
    # xhci98 compiles against; and, where the release carries its header, the
    # two byte for byte. Returns "" when they agree, else why not.
    param($Release, [string]$Pinned, [string]$HeaderPath)
    if ($Pinned -notmatch '^(none|\d+)$') {
        throw "-Nvme98InterfaceVersion is the pinned build's NVME98LS_VERSION (a number), or 'none' for a build with no link-state receiver (0.1.x)."
    }
    if ($Release.Layout -eq "old" -and $Pinned -ne "none") {
        throw "nvme98 $($Release.Version) is a 0.1.x build, which has no link-state receiver: its -Nvme98InterfaceVersion is 'none'."
    }
    $have = ($HeaderPath -ne "") -and (Test-Path -LiteralPath $HeaderPath -PathType Leaf)
    $ours = if ($have) { Get-Nvme98HeaderVersion -Path $HeaderPath } else { "" }
    if ($have -and $ours -eq "") {
        return "'$HeaderPath' defines no NVME98LS_VERSION."
    }
    if ($null -ne $Release.Header) {
        if (-not $have) {
            return "the nvme98 $($Release.Version) release carries $($script:Nvme98ReleaseHeader), and xhci98 has no '$HeaderPath' to compare it with."
        }
        $a = [System.IO.File]::ReadAllBytes($Release.Header)
        $b = [System.IO.File]::ReadAllBytes($HeaderPath)
        if ([Convert]::ToBase64String($a) -ne [Convert]::ToBase64String($b)) {
            return "$($script:Nvme98ReleaseHeader) of the nvme98 $($Release.Version) release is not byte for byte '$HeaderPath'."
        }
        $theirs = Get-Nvme98HeaderVersion -Path $Release.Header
        if ($Pinned -ne $theirs) {
            return "the nvme98 $($Release.Version) release's header declares interface version '$theirs', and -Nvme98InterfaceVersion pins '$Pinned'."
        }
    }
    if ($Pinned -eq "none") {
        if ($have) {
            return "xhci98 compiles against nvme98cm.h interface version $ours ('$HeaderPath'), and nvme98 $($Release.Version) is pinned as having no link-state receiver ('none')."
        }
        return ""
    }
    if (-not $have) {
        return "nvme98 $($Release.Version) is pinned at interface version $Pinned, and xhci98 has no '$HeaderPath' (it compiles against no nvme98 interface)."
    }
    if ($ours -ne $Pinned) {
        return "nvme98 $($Release.Version) is pinned at interface version $Pinned, and '$HeaderPath' declares NVME98LS_VERSION $ours."
    }
    return ""
}

function Get-Nvme98NotesFault {
    # The release notes' one bundle declaration, "This download bundles
    # nvme98 `<version>`." (a parenthesised note may follow the version, as
    # in a dry cut's "(dry cut only)") or "This download bundles no nvme98.",
    # against what the cut bundles ($Version, "" for none). Returns "" when
    # they agree, else why not; the cut fills the placeholder in.
    param([string]$Text, [string]$Version)
    $ms = [regex]::Matches($Text, '(?m)^This download bundles (.+?)\.[ \t]*\r?$')
    if ($ms.Count -ne 1) {
        return "docs\using\release-notes.md has $($ms.Count) 'This download bundles ...' declarations, not one ('NVMe drives behind Thunderbolt or USB4')."
    }
    $said = $ms[0].Groups[1].Value.Trim()
    if ($said.Contains("filled in at the cut")) {
        $want = if ($Version -eq "") { "no nvme98" } else { "nvme98 ``$Version``" }
        return "docs\using\release-notes.md still has the bundled nvme98 placeholder: write 'This download bundles $want.' in its place ('NVMe drives behind Thunderbolt or USB4')."
    }
    if ($Version -eq "") {
        if ($said -ne "no nvme98") {
            return "docs\using\release-notes.md says 'This download bundles $said.', and this cut bundles no nvme98: write 'This download bundles no nvme98.'"
        }
        return ""
    }
    $bt = [string][char]96
    if ($said -notmatch ('^nvme98 ' + $bt + '?' + [regex]::Escape($Version) + $bt + '?( \([^)]*\))?$')) {
        return "docs\using\release-notes.md says 'This download bundles $said.', and this cut bundles nvme98 $Version."
    }
    return ""
}

function Resolve-Nvme98Input {
    # The packaging scripts' one entry point: $null when no nvme98 input was
    # named (the package is built as before, with no nvme98), else the
    # verified release with its interface verdict. The four pins go
    # together; a mismatch of the interface is refused unless -Allow, and the
    # override is reported, never silent.
    param([string]$Version, [string]$Dir, [string]$SumsSha256, [string]$InterfaceVersion,
          [string]$HeaderPath, [switch]$Allow)
    $named = @(@($Version, $Dir, $SumsSha256, $InterfaceVersion) | Where-Object { $_ -ne "" })
    if ($named.Count -eq 0) {
        if ($Allow) { throw "-Nvme98AllowInterfaceMismatch was given with no nvme98 input to apply it to." }
        return $null
    }
    if ($named.Count -ne 4) {
        throw "the bundled nvme98 build is pinned explicitly: -Nvme98Version, -Nvme98Dir, -Nvme98SumsSha256 and -Nvme98InterfaceVersion go together, and only $($named.Count) of them were given."
    }
    $release = Assert-Nvme98Release -Dir $Dir -Version $Version -SumsSha256 $SumsSha256
    $why = Test-Nvme98Interface -Release $release -Pinned $InterfaceVersion -HeaderPath $HeaderPath
    if ($why -ne "" -and -not $Allow) {
        throw "the nvme98 interface does not match: $why Bundle a build whose interface matches, or pass -Nvme98AllowInterfaceMismatch to bundle it anyway (the override is logged)."
    }
    $release | Add-Member -NotePropertyName Interface -NotePropertyValue $InterfaceVersion
    $release | Add-Member -NotePropertyName Mismatch -NotePropertyValue $why
    $release | Add-Member -NotePropertyName Overridden -NotePropertyValue ($why -ne "")
    return $release
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
# "sha256.<name>=<64 hex>". Written by scripts\package\write-gated-manifest.ps1.
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
            if ($name -notmatch '^[a-z0-9_]+\.sys$') { throw "'$key' does not name a .sys" }
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
    param($Manifest, [string]$Arch, [string]$Flavor, [hashtable]$Binaries,
          [string]$Head, [bool]$Dirty, [bool]$NoTargetEvidence)
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
        return ("it was built at commit {0}, and the tree is at {1}: the INFs and scripts this package would be staged with are not the ones the build was gated beside" -f $Manifest.Commit, $Head)
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
    # HEAD, and whether the tree differs from it (a tracked change, or an
    # untracked file that is not ignored). Head "none" when git cannot say,
    # which the sign-only mode refuses.
    param([string]$Repo)
    $head = ""
    $status = ""
    $ok = $false
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $head = (& git -C $Repo rev-parse HEAD 2>$null | Out-String).Trim()
        $headRc = $LASTEXITCODE
        $status = (& git -C $Repo status --porcelain 2>$null | Out-String).Trim()
        $ok = ($headRc -eq 0 -and $LASTEXITCODE -eq 0 -and $head -match '^[0-9a-f]{40}$')
    } catch {
        $ok = $false
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if (-not $ok) { return @{ Head = "none"; Dirty = $true } }
    return @{ Head = $head; Dirty = ($status -ne "") }
}
