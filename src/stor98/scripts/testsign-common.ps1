# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng

# Shared by scripts\make-release.ps1 (-Stage -TestSignPfx, -Finalize) and
# scripts\sign-x64.ps1: how the staged x64\stor98.sys is signed and checked,
# so both routes leave the same stage layout. Dot-source it; it defines
# functions and constants only. See docs\BUILD.md, "Test signing".
#
# What a signature here is: an embedded Authenticode signature on the amd64
# image only (32-bit Windows does not enforce kernel-mode signing, so x86 and
# 9x images are never signed), SHA-1 file digest (what Vista's code integrity
# and an un-updated Windows 7 verify), no page hashes, no catalog, the INFs
# unchanged. The public certificate is staged next to the image as
# x64\stor98-test.cer (amd64\ in releases up to 0.1.1.0).

$Stor98TestSignSubject = "CN=stor98 project test signing (not trusted)"
# The SHA-1 thumbprint of the project test certificate, recorded once the
# maintainer has made it with scripts\make-testcert.ps1. While empty, any
# certificate with the subject above is accepted by -TestSignPfx; once set,
# only this one.
$Stor98TestSignThumbprint = "461311C79165DC7985522D46274DCAD8832DD919"
$Stor98TestSignCerName = "stor98-test.cer"
$Stor98TestSignPasswordVariable = "STOR98_TESTSIGN_PW"

$OidSha1 = "1.3.14.3.2.26"
$OidSpcIndirectData = "1.3.6.1.4.1.311.2.1.4"
$OidCounterSignature = "1.2.840.113549.1.9.6"
$OidRfc3161Timestamp = "1.3.6.1.4.1.311.3.3.1"

function Get-EmbeddedSignature([string]$Path) {
    # The PE security directory (Authenticode certificate table), or $null.
    $b = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $magic = [BitConverter]::ToUInt16($b, $pe + 24)
    $ddBase = if ($magic -eq 0x20B) { $pe + 24 + 112 } else { $pe + 24 + 96 }
    $secOff = $ddBase + 4 * 8
    $rva = [BitConverter]::ToUInt32($b, $secOff)
    $size = [BitConverter]::ToUInt32($b, $secOff + 4)
    if ($rva -eq 0 -or $size -eq 0) { return $null }
    return @{ Offset = [int]$rva; Size = [int]$size; ChecksumOffset = $pe + 24 + 64; SecDirOffset = $secOff }
}

function Assert-Amd64Image([string]$Path) {
    # Only a PE32+ amd64 image is ever signed: a signature buys 32-bit Windows
    # nothing and would change bytes every x86 and 9x reading was taken on.
    $b = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $machine = [BitConverter]::ToUInt16($b, $pe + 4)
    $magic = [BitConverter]::ToUInt16($b, $pe + 24)
    if ($machine -ne 0x8664 -or $magic -ne 0x20B) {
        throw ("refusing to sign '{0}' (machine 0x{1:X}, optional header 0x{2:X}): only the amd64 image is signed, never a 32-bit one." -f $Path, $machine, $magic)
    }
}

function Test-SignedFromStaged([string]$Signed, [string]$Unsigned) {
    # The signed image must be the unsigned one with only the PE checksum, the
    # security directory entry, zero padding to 8 bytes and the appended
    # certificate table (WIN_CERTIFICATE) changed. Returns "" when it is,
    # otherwise the reason.
    $s = [IO.File]::ReadAllBytes($Signed)
    $u = [IO.File]::ReadAllBytes($Unsigned)
    $sig = Get-EmbeddedSignature $Signed
    if ($null -eq $sig) { return "it has no certificate table" }
    if ($null -ne (Get-EmbeddedSignature $Unsigned)) { return "the reference image is itself signed" }
    if ($sig.Offset -lt $u.Length) { return "its certificate table starts inside the unsigned image" }
    if ($sig.Offset - $u.Length -gt 7) { return "more than 7 bytes between the image and its certificate table" }
    if ($sig.Offset + $sig.Size -ne $s.Length) { return "its certificate table does not end the file" }
    for ($i = $u.Length; $i -lt $sig.Offset; $i++) { if ($s[$i] -ne 0) { return "non-zero padding before the certificate table" } }
    $skip = @{}
    foreach ($k in 0..3) { $skip[$sig.ChecksumOffset + $k] = $true }
    foreach ($k in 0..7) { $skip[$sig.SecDirOffset + $k] = $true }
    for ($i = 0; $i -lt $u.Length; $i++) {
        if ($s[$i] -ne $u[$i] -and -not $skip.ContainsKey($i)) {
            return ("byte 0x{0:X} differs from the unsigned image" -f $i)
        }
    }
    return ""
}

function Get-SignatureFacts([string]$Path) {
    # Parses the embedded PKCS #7 signature without consulting any store:
    # signer certificate, digest algorithms, whether it is countersigned
    # (timestamped), whether the vouched-for image digest is this image's,
    # and whether the PKCS #7 signature itself verifies. $null if unsigned.
    $sig = Get-EmbeddedSignature $Path
    if ($null -eq $sig) { return $null }
    Add-Type -AssemblyName System.Security
    $b = [IO.File]::ReadAllBytes($Path)
    $len = [BitConverter]::ToInt32($b, $sig.Offset)
    $rev = [BitConverter]::ToUInt16($b, $sig.Offset + 4)
    $type = [BitConverter]::ToUInt16($b, $sig.Offset + 6)
    $f = [ordered]@{ Problem = ""; Signer = $null; SignerDigest = ""; FileDigest = ""; Timestamped = $false; DigestMatches = $false; SignatureValid = $false }
    if ($type -ne 2 -or $rev -ne 0x200 -or $len -lt 8 -or $len -gt $sig.Size) {
        $f.Problem = ("the certificate table is not one PKCS #7 WIN_CERTIFICATE (revision 0x{0:X}, type {1}, length {2})" -f $rev, $type, $len)
        return [pscustomobject]$f
    }
    $pkcs7 = New-Object byte[] ($len - 8)
    [Array]::Copy($b, $sig.Offset + 8, $pkcs7, 0, $len - 8)
    $cms = New-Object Security.Cryptography.Pkcs.SignedCms
    try { $cms.Decode($pkcs7) } catch { $f.Problem = "the PKCS #7 signature does not decode: $($_.Exception.Message)"; return [pscustomobject]$f }
    if ($cms.ContentInfo.ContentType.Value -ne $OidSpcIndirectData) { $f.Problem = "the signed content is not an Authenticode SpcIndirectDataContent"; return [pscustomobject]$f }
    if ($cms.SignerInfos.Count -ne 1) { $f.Problem = "it has $($cms.SignerInfos.Count) signers, not one"; return [pscustomobject]$f }
    $si = $cms.SignerInfos[0]
    $f.Signer = $si.Certificate
    $f.SignerDigest = $si.DigestAlgorithm.Value
    foreach ($a in $si.UnsignedAttributes) {
        if ($a.Oid.Value -eq $OidCounterSignature -or $a.Oid.Value -eq $OidRfc3161Timestamp) { $f.Timestamped = $true }
    }
    if ($si.CounterSignerInfos.Count -gt 0) { $f.Timestamped = $true }
    try { $cms.CheckSignature($true); $f.SignatureValid = $true } catch { }

    # SpcIndirectDataContent ends with DigestInfo { AlgorithmIdentifier,
    # OCTET STRING digest }: the vouched digest is the content's tail, and
    # the algorithm identifier precedes it. Only SHA-1 is accepted here.
    $c = $cms.ContentInfo.Content
    $sha1AlgId = [byte[]](0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00, 0x04, 0x14)
    $sha1AlgIdNoNull = [byte[]](0x30, 0x07, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x04, 0x14)
    $vouched = $null
    foreach ($pfx in @($sha1AlgId, $sha1AlgIdNoNull)) {
        $at = $c.Length - 20 - $pfx.Length
        if ($at -lt 0) { continue }
        $ok = $true
        for ($i = 0; $i -lt $pfx.Length; $i++) { if ($c[$at + $i] -ne $pfx[$i]) { $ok = $false; break } }
        if ($ok) { $vouched = $c[($c.Length - 20)..($c.Length - 1)]; break }
    }
    if ($null -eq $vouched) { $f.FileDigest = "not SHA-1"; return [pscustomobject]$f }
    $f.FileDigest = $OidSha1

    # The Authenticode image digest of a linker-produced image: every byte up
    # to the certificate table, less the checksum and the security directory
    # entry.
    $sha = [Security.Cryptography.SHA1]::Create()
    $ranges = @(
        @(0, $sig.ChecksumOffset),
        @(($sig.ChecksumOffset + 4), ($sig.SecDirOffset - $sig.ChecksumOffset - 4)),
        @(($sig.SecDirOffset + 8), ($sig.Offset - $sig.SecDirOffset - 8))
    )
    foreach ($r in $ranges) { [void]$sha.TransformBlock($b, $r[0], $r[1], $null, 0) }
    [void]$sha.TransformFinalBlock((New-Object byte[] 0), 0, 0)
    $f.DigestMatches = ([BitConverter]::ToString($sha.Hash) -eq [BitConverter]::ToString([byte[]]$vouched))
    return [pscustomobject]$f
}

function Get-SignatureProblem([string]$Path, [string]$Thumbprint, [bool]$AllowTimestamp) {
    # "" when $Path carries the signature this project prescribes by the
    # certificate $Thumbprint, otherwise the reason.
    $f = Get-SignatureFacts $Path
    if ($null -eq $f) { return "it is not signed" }
    if ($f.Problem -ne "") { return $f.Problem }
    if ($null -eq $f.Signer) { return "its signer certificate is not embedded" }
    if ($f.Signer.Thumbprint.ToUpperInvariant() -ne $Thumbprint.ToUpperInvariant()) { return "it is signed by $($f.Signer.Thumbprint), not $Thumbprint" }
    if ($f.SignerDigest -ne $OidSha1) { return "its signer digest is $($f.SignerDigest), not SHA-1" }
    if ($f.FileDigest -ne $OidSha1) { return "its file digest is not SHA-1" }
    if (-not $f.DigestMatches) { return "the digest it vouches for is not this image's" }
    if (-not $f.SignatureValid) { return "its PKCS #7 signature does not verify" }
    if ($f.Timestamped -and -not $AllowTimestamp) { return "it carries a timestamp (countersignature), which test signing does not take" }
    return ""
}

function Read-PfxPassword([string]$Pfx) {
    # From STOR98_TESTSIGN_PW when set (process environment), otherwise a
    # prompt that does not echo. Never printed and never written anywhere.
    $pw = [Environment]::GetEnvironmentVariable($Stor98TestSignPasswordVariable, "Process")
    if ($null -ne $pw) { return $pw }
    $secure = Read-Host -AsSecureString "Password for $Pfx (input hidden; empty if none)"
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

function Open-PfxCertificate([string]$Pfx, [string]$Password) {
    # Opens a .pfx without importing anything into a store (EphemeralKeySet
    # keeps the key in memory only) and returns its one certificate with a
    # private key: thumbprint, subject and the public DER bytes.
    if (-not (Test-Path -LiteralPath $Pfx -PathType Leaf)) { throw "no .pfx at '$Pfx'" }
    if (-not [Enum]::IsDefined([Security.Cryptography.X509Certificates.X509KeyStorageFlags], "EphemeralKeySet")) {
        throw "opening a .pfx needs .NET Framework 4.7.2 or later (X509KeyStorageFlags.EphemeralKeySet), so the key is never persisted."
    }
    $flags = [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet
    $coll = New-Object Security.Cryptography.X509Certificates.X509Certificate2Collection
    try { $coll.Import($Pfx, $Password, $flags) }
    catch { throw "'$Pfx' cannot be opened with the password given: $($_.Exception.Message)" }
    try {
        $withKey = @($coll | Where-Object { $_.HasPrivateKey })
        if ($withKey.Count -ne 1) { throw "'$Pfx' must hold exactly one certificate with a private key (found $($withKey.Count))" }
        $c = $withKey[0]
        return [pscustomobject]@{
            Thumbprint = $c.Thumbprint.ToUpperInvariant()
            Subject    = $c.Subject
            CerBytes   = $c.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert)
        }
    } finally {
        foreach ($c in $coll) { $c.Reset() }
    }
}

function Assert-ProjectTestCertificate($Cert, [string]$Pfx) {
    # -TestSignPfx signs with the stor98 project test certificate and no other
    # (scripts\sign-x64.ps1 is the route for any other certificate).
    if ($Cert.Subject -ne $Stor98TestSignSubject) {
        throw "'$Pfx' is the certificate '$($Cert.Subject)', not the stor98 project test certificate '$Stor98TestSignSubject'. Make it with scripts\make-testcert.ps1, or sign with another certificate through scripts\sign-x64.ps1."
    }
    if ($Stor98TestSignThumbprint -ne "" -and $Cert.Thumbprint -ne $Stor98TestSignThumbprint.ToUpperInvariant()) {
        throw "'$Pfx' holds the certificate $($Cert.Thumbprint), not the recorded project test certificate $Stor98TestSignThumbprint (scripts\testsign-common.ps1)."
    }
}

function Invoke-StagedSign {
    # Signs the staged amd64 image $Target: a copy is signed in a temporary
    # folder with SignTool, held to Get-SignatureProblem and to
    # Test-SignedFromStaged against $UnsignedRef, and only then put in place;
    # the public certificate goes next to it as stor98-test.cer. Every
    # failure throws and leaves the stage as it was. $CertArgs selects the
    # certificate (/f <pfx> [/p <pw>] or /sha1 <thumb> /s My) and may hold the
    # password, so it is never printed.
    param([string]$Target, [string]$UnsignedRef, [string]$SignTool, [string[]]$CertArgs,
          [string]$Thumbprint, [byte[]]$CerBytes, [string]$CrossCert = "", [string]$TimestampUrl = "")

    if (-not (Test-Path -LiteralPath $SignTool)) { throw "no SignTool at $SignTool (WDK 7.1, docs\BUILD.md)" }
    Assert-Amd64Image $Target
    if ($null -ne (Get-EmbeddedSignature $Target)) {
        throw "'$Target' is already signed. Re-stage (make-release.ps1 -Stage -Force) to sign afresh."
    }
    if ((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $UnsignedRef -Algorithm SHA256).Hash) {
        throw "'$Target' is not the unsigned reference image '$UnsignedRef'; re-stage."
    }

    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("stor98-sign-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    try {
        $work = Join-Path $tmp "stor98.sys"
        Copy-Item -LiteralPath $Target -Destination $work
        $signArgs = @("sign", "/q") + $CertArgs + @("/fd", "sha1", "/nph")
        if ($CrossCert -ne "") { $signArgs += @("/ac", $CrossCert) }
        if ($TimestampUrl -ne "") { $signArgs += @("/t", $TimestampUrl) }
        $signArgs += @($work)

        # SignTool /f imports the .pfx key into a CryptoAPI key container of
        # the current user and may leave it there; report any that appears.
        $keyDir = Join-Path $env:APPDATA ("Microsoft\Crypto\RSA\" + [Security.Principal.WindowsIdentity]::GetCurrent().User.Value)
        $before = @(if (Test-Path -LiteralPath $keyDir) { Get-ChildItem -LiteralPath $keyDir -File -Force | ForEach-Object { $_.Name } })
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            # Not echoed: the argument list may hold the .pfx password.
            $out = & $SignTool @signArgs 2>&1 | Out-String
            $rc = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $savedEap
            $signArgs = $null
        }
        $after = @(if (Test-Path -LiteralPath $keyDir) { Get-ChildItem -LiteralPath $keyDir -File -Force | ForEach-Object { $_.Name } })
        $new = @($after | Where-Object { $before -notcontains $_ })
        if ($new.Count -gt 0) {
            Write-Warning ("SignTool left {0} new key container file(s), each possibly a copy of the private key. They are hidden system files; delete each one named here once you are sure it is not one of yours:" -f $new.Count)
            foreach ($n in $new) {
                Write-Warning ("  Remove-Item -LiteralPath '{0}' -Force" -f (Join-Path $keyDir $n).Replace("'", "''"))
            }
        }
        if ($rc -ne 0) { throw "SignTool sign failed (exit $rc):`n$out" }

        $why = Get-SignatureProblem $work $Thumbprint ($TimestampUrl -ne "")
        if ($why -ne "") { throw "the signed image is not what test signing prescribes: $why" }
        $why = Test-SignedFromStaged $work $UnsignedRef
        if ($why -ne "") { throw "the signed image is not the staged image plus a signature: $why" }

        $cerPath = Join-Path (Split-Path -Parent $Target) $Stor98TestSignCerName
        Copy-Item -LiteralPath $work -Destination $Target -Force
        [IO.File]::WriteAllBytes($cerPath, $CerBytes)
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    $f = Get-SignatureFacts $Target
    Write-Host ("Signed {0}`n  signer     {1}`n  thumbprint {2}`n  digest     SHA-1, {3}`n  sha256     {4}`n  staged     {5}" -f $Target,
        $f.Signer.Subject, $f.Signer.Thumbprint, $(if ($f.Timestamped) { "timestamped" } else { "no timestamp" }),
        (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant(), $cerPath)
}
