# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng

<#
.SYNOPSIS
Sign an already-staged x64 stor98.sys with any certificate. Run by the
maintainer, between make-release.ps1 -Stage and make-release.ps1 -Finalize.
The default route is make-release.ps1 -Stage -TestSignPfx, which signs with
the stor98 project test certificate while staging; this script is the
alternative for another certificate (for example xhci98's test certificate,
so test machines trust one signer) or a production signature.

.DESCRIPTION
Signs out\stage\<version>\x64\stor98.sys with WDK 7.1's SignTool
(tools\WinDDK71\bin\x86\SignTool.exe) exactly as -TestSignPfx does, through
the same code (scripts\testsign-common.ps1): embedded Authenticode, SHA-1
file digest (/fd sha1, which Vista's code integrity can check), no page
hashes (/nph), no catalog, the INFs unchanged; a copy is signed and checked
(signer, SHA-1, vouched digest, and the unsigned staged image plus the
signature and nothing else) before it replaces the staged image, and the
signer's public certificate is staged as x64\stor98-test.cer. Only the
amd64 image is ever signed, and never a -Test stage. Releases up to 0.1.1.0
kept it in amd64\; from 0.2.0.0 the folder is x64\. Exactly one of:

  -Thumbprint <sha1>   a certificate with its private key in the current
                       user's personal store (Cert:\CurrentUser\My)
  -Pfx <file.pfx>      a .pfx file, any subject; its password comes from
                       STOR98_TESTSIGN_PW or a prompt that does not echo,
                       and is never written anywhere

-CrossCert <file>      a cross-certificate (/ac) for a kernel-mode production
                       signature; the result is then verified with
                       'SignTool verify /kp'. Without it the signature is
                       checked with 'SignTool verify /pa', which fails for a
                       self-signed test certificate (untrusted root); that
                       failure is reported, and the script then requires the
                       image to carry an Authenticode signature by exactly the
                       certificate it was asked to use.
-TimestampUrl <url>    an Authenticode (/t) timestamp server; none by default
                       (test signing takes none: the cut stays offline and
                       reproducible).

Refuses if the image is already signed (re-stage to start again). Nothing here
creates or imports a certificate.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\sign-x64.ps1 -Thumbprint 0123...cdef
powershell -ExecutionPolicy Bypass -File scripts\sign-x64.ps1 -Pfx <path>\stor98-test.pfx
#>

[CmdletBinding(DefaultParameterSetName = "Thumbprint")]
param(
    [Parameter(ParameterSetName = "Thumbprint", Mandatory = $true)]
    [string]$Thumbprint,
    [Parameter(ParameterSetName = "Pfx", Mandatory = $true)]
    [string]$Pfx,
    [string]$Version = "",
    [string]$CrossCert = "",
    [string]$TimestampUrl = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# The kits root, as scripts\build.cmd finds it: STOR98_TOOLS, else tools\ here,
# else the tools\ of the xhci98 tree that holds Stor98 as src\stor98.
$Tools = if ($env:STOR98_TOOLS) { $env:STOR98_TOOLS } elseif (Test-Path -LiteralPath (Join-Path $Repo "tools") -PathType Container) { Join-Path $Repo "tools" } else { [System.IO.Path]::GetFullPath((Join-Path $Repo "..\..\tools")) }
$SignTool = Join-Path $Tools "WinDDK71\bin\x86\SignTool.exe"
. (Join-Path $PSScriptRoot "testsign-common.ps1")
function Fail([string]$Message) { throw "sign-x64: $Message" }

if (-not (Test-Path -LiteralPath $SignTool)) { Fail "no SignTool at $SignTool (WDK 7.1, docs\BUILD.md)" }

if ($Version -eq "") {
    $h = Get-Content -LiteralPath (Join-Path $Repo "src\stor98ver.h") -Raw
    $m = [regex]::Match($h, '#define\s+STOR98_VER_STR\s+"(\d+\.\d+\.\d+\.\d+)"')
    if (-not $m.Success) { Fail "no STOR98_VER_STR in src\stor98ver.h" }
    $Version = $m.Groups[1].Value
}
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { Fail "-Version must be a release version a.b.c.d (a -Test stage is never signed)" }
$target = Join-Path $Repo "out\stage\$Version\x64\stor98.sys"
if (-not (Test-Path -LiteralPath $target)) { Fail "$target not found; run scripts\make-release.ps1 -Stage first" }

$unsignedRef = Join-Path $Repo "out\stage\$Version.x64-unsigned.sys"
if (-not (Test-Path -LiteralPath $unsignedRef)) { Fail "$unsignedRef not found; re-stage (make-release.ps1 -Stage -Force)" }

# Work out which certificate must end up as the signer.
if ($PSCmdlet.ParameterSetName -eq "Thumbprint") {
    $Thumbprint = ($Thumbprint -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()
    if ($Thumbprint.Length -ne 40) { Fail "-Thumbprint must be a 40-hex-digit SHA-1 certificate hash" }
    $cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$Thumbprint" -ErrorAction SilentlyContinue
    if ($null -eq $cert) { Fail "no certificate $Thumbprint in Cert:\CurrentUser\My" }
    if (-not $cert.HasPrivateKey) { Fail "certificate $Thumbprint has no private key in this store" }
    $expectThumb = $Thumbprint
    $cerBytes = $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert)
    $certArgs = @("/sha1", $Thumbprint, "/s", "My")
} else {
    if (-not (Test-Path -LiteralPath $Pfx -PathType Leaf)) { Fail "no .pfx at $Pfx" }
    $Pfx = (Resolve-Path -LiteralPath $Pfx).Path
    $password = Read-PfxPassword $Pfx
    $pfxCert = Open-PfxCertificate $Pfx $password
    $expectThumb = $pfxCert.Thumbprint
    $cerBytes = $pfxCert.CerBytes
    $certArgs = @("/f", $Pfx)
    if ($password -ne "") { $certArgs += @("/p", $password) }
}
if ($CrossCert -ne "") {
    if (-not (Test-Path -LiteralPath $CrossCert)) { Fail "no cross-certificate at $CrossCert" }
    $CrossCert = (Resolve-Path -LiteralPath $CrossCert).Path
}

try {
    Invoke-StagedSign -Target $target -UnsignedRef $unsignedRef -SignTool $SignTool -CertArgs $certArgs `
        -Thumbprint $expectThumb -CerBytes $cerBytes -CrossCert $CrossCert -TimestampUrl $TimestampUrl
} catch {
    Fail $_.Exception.Message
} finally {
    $password = $null
    $certArgs = $null
}

# Verify.
$policy = if ($CrossCert -ne "") { "/kp" } else { "/pa" }
$savedEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    $vout = & $SignTool verify $policy /v $target 2>&1 | Out-String
    $vrc = $LASTEXITCODE
} finally { $ErrorActionPreference = $savedEap }
Write-Host $vout

if ($vrc -ne 0) {
    if ($CrossCert -ne "") { Fail "SignTool verify /kp failed (exit $vrc); see above" }
    Write-Warning "SignTool verify /pa failed (exit $vrc). Expected for a self-signed test certificate (untrusted root); the signer is the requested certificate."
}
Write-Host "Next: powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Finalize"
