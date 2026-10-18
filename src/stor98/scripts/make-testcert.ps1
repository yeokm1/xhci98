# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng

<#
.SYNOPSIS
Create the stor98 project test-signing certificate. Run once, interactively,
by the maintainer who holds the key.

.DESCRIPTION
Runs WDK 7.1's MakeCert and pvk2pfx (tools\WinDDK71\bin\x86, used in place)
and writes into -OutDir:

    stor98-test.pfx  the private key and certificate. Never committed,
                     packaged, printed, shared or copied into notes.
                     scripts\make-release.ps1 -Stage -TestSignPfx takes its
                     path at every cut.
    stor98-test.cer  the public half. make-release.ps1 stages a copy of it
                     next to the signed x64\stor98.sys.

The certificate (the commands are in docs\BUILD.md, "Test signing"):

    subject   CN=stor98 project test signing (not trusted)
    self-signed (-r), RSA 2048, SHA-1 certificate signature (Vista checks
    SHA-1 only), Code Signing EKU 1.3.6.1.5.5.7.3.3, valid 2000-01-01 to
    2039-12-31 (already valid on a machine whose clock is behind, and long
    enough for offline signing with no timestamp).

MakeCert asks for a private-key (.pvk) password in a dialog, and pvk2pfx asks
for the same password in a second one. The .pfx password is asked for here,
twice, without echo, and is never stored. pvk2pfx takes it only on its
command line, so for the moment it runs it is visible to other local
processes that can read command lines. The intermediate .pvk is deleted at
the end, since the .pfx holds the key.

Guards: refuses to overwrite an existing stor98-test.pvk, .cer or .pfx in
-OutDir (a second key would make earlier signatures unverifiable against the
recorded thumbprint), and refuses an -OutDir inside the repository that git
does not ignore. An -OutDir outside the repository is accepted; that keeps
the key out of a synchronised folder if the repository lives in one.

Prints the certificate's subject, validity, signature algorithm, key size,
enhanced key usage and SHA-1 thumbprint at the end. Needs no network.

.PARAMETER OutDir
Where to write the key and certificate. Defaults to scripts\local\testsign in
the repository (git-ignored), created if missing.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\make-testcert.ps1
#>

[CmdletBinding()]
param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Wdk = Join-Path $Repo "tools\WinDDK71\bin\x86"
$MakeCert = Join-Path $Wdk "MakeCert.exe"
$Pvk2Pfx = Join-Path $Wdk "pvk2pfx.exe"
$Subject = "CN=stor98 project test signing (not trusted)"
$Base = "stor98-test"

function Fail([string]$Message) { throw "make-testcert: $Message" }

foreach ($tool in @($MakeCert, $Pvk2Pfx)) {
    if (-not (Test-Path -LiteralPath $tool)) { Fail "not found: $tool (WDK 7.1 belongs in tools\WinDDK71; docs\BUILD.md)" }
}

if ($OutDir -eq "") { $OutDir = Join-Path $Repo "scripts\local\testsign" }
if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $PWD.ProviderPath $OutDir }
$OutDir = [IO.Path]::GetFullPath($OutDir).TrimEnd('\')

foreach ($ext in @("pvk", "cer", "pfx")) {
    $f = Join-Path $OutDir "$Base.$ext"
    if (Test-Path -LiteralPath $f) {
        Fail "$f already exists - refusing to overwrite an existing key. Move it away first if you really mean to make a new one."
    }
}

# Inside the repository the key folder must be git-ignored, so the .pfx can
# never be committed by a 'git add -A'. Checked before anything is created.
if (($OutDir + '\').StartsWith($Repo.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & git -C $Repo check-ignore -q (Join-Path $OutDir "$Base.pfx") 2>$null
        $ignored = ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if (-not $ignored) {
        Fail "$OutDir is inside the repository and not git-ignored - refusing to create a private key where it could be committed."
    }
}

if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

Push-Location -LiteralPath $OutDir
try {
    Write-Host ""
    Write-Host "Step 1 of 3: MakeCert (a dialog asks for a private-key password)..."
    & $MakeCert -r -pe -n $Subject -a sha1 -len 2048 -eku 1.3.6.1.5.5.7.3.3 -b 01/01/2000 -e 12/31/2039 -sv "$Base.pvk" "$Base.cer"
    if ($LASTEXITCODE -ne 0) { Fail "MakeCert failed (exit $LASTEXITCODE)" }

    Write-Host ""
    Write-Host "Step 2 of 3: choose the .pfx password (asked for at every release cut)."
    $p1 = Read-Host -AsSecureString "PFX password"
    $p2 = Read-Host -AsSecureString "PFX password again"
    $b1 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($p1)
    $b2 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($p2)
    $s1 = $null; $s2 = $null
    try {
        $s1 = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b1)
        $s2 = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b2)
        if (-not [string]::Equals($s1, $s2, [StringComparison]::Ordinal)) {
            Fail "the two passwords differ. Only $Base.pvk and $Base.cer were written; delete them and run again."
        }
        if ($s1.Length -eq 0) { Fail "an empty password is refused. Delete $Base.pvk and $Base.cer and run again." }
        Write-Host "pvk2pfx (a dialog asks for the private-key password from step 1)..."
        & $Pvk2Pfx -pvk "$Base.pvk" -spc "$Base.cer" -pfx "$Base.pfx" -po $s1
        if ($LASTEXITCODE -ne 0) { Fail "pvk2pfx failed (exit $LASTEXITCODE)" }
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b1)
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b2)
        $s1 = $null; $s2 = $null
    }

    Write-Host ""
    Write-Host "Step 3 of 3: the certificate:"
    $cert = New-Object Security.Cryptography.X509Certificates.X509Certificate2 (Join-Path $OutDir "$Base.cer")
    Write-Host ("  Subject    : " + $cert.Subject)
    Write-Host ("  NotBefore  : " + $cert.NotBefore.ToString("yyyy-MM-dd"))
    Write-Host ("  NotAfter   : " + $cert.NotAfter.ToString("yyyy-MM-dd"))
    Write-Host ("  Sig. alg.  : " + $cert.SignatureAlgorithm.FriendlyName)
    Write-Host ("  Key size   : " + $cert.PublicKey.Key.KeySize)
    $eku = @($cert.Extensions | Where-Object { $_.Oid.Value -eq "2.5.29.37" })
    if ($eku.Count -gt 0) {
        Write-Host ("  EKU        : " + (($eku[0].EnhancedKeyUsages | ForEach-Object { $_.FriendlyName }) -join ", "))
    }
    Write-Host ("  Thumbprint : " + $cert.Thumbprint + "   (SHA-1 certificate hash)")
    Write-Host ""

    Remove-Item -LiteralPath (Join-Path $OutDir "$Base.pvk")
    Write-Host "Removed $Base.pvk (the .pfx holds the key)."
    Write-Host ("Key: " + (Join-Path $OutDir "$Base.pfx"))
    Write-Host "Done. The thumbprint may be shared and recorded; the password and the .pfx never."
    Write-Host "Record the thumbprint in scripts\testsign-common.ps1 (`$Stor98TestSignThumbprint) so"
    Write-Host "make-release.ps1 -TestSignPfx accepts this key and no other."
} finally {
    Pop-Location
}
