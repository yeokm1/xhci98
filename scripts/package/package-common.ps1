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

    # SpcIndirectDataContent ends with the DigestInfo's OCTET STRING, so the
    # digest it vouches for is the content's last bytes: 20 for SHA-1.
    $content = $cms.ContentInfo.Content
    $digestLen = if ($signer.DigestAlgorithm.Value -eq $script:Sha1Oid) { 20 } else { 32 }
    $vouched = ""
    if ($content.Length -ge $digestLen + 2 -and $content[$content.Length - $digestLen - 2] -eq 0x04 -and
        $content[$content.Length - $digestLen - 1] -eq $digestLen) {
        $vouched = (($content[($content.Length - $digestLen)..($content.Length - 1)] |
                     ForEach-Object { $_.ToString("X2") }) -join "")
    }

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
        VouchedDigest  = $vouched
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
    if ($Signature.DigestOid -ne $script:Sha1Oid) {
        return ("its file digest is {0}, not SHA-1 ({1}), which Vista x64 cannot verify" -f $Signature.DigestOid, $script:Sha1Oid)
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
        return [pscustomobject]@{
            Thumbprint = $cert.Thumbprint
            Subject    = $cert.Subject
            CerBytes   = $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
        }
    } finally {
        $cert.Reset()
    }
}
