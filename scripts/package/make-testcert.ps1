<#
.SYNOPSIS
Create the xhci98 project test-signing certificate (design record 19).

.DESCRIPTION
Run once, interactively, by the maintainer who holds the key. It runs the two
WDK 7.1 commands design record 19 section 4 gives verbatim - MakeCert, then
pvk2pfx - and writes two files into -OutDir:

    xhci98-test.pfx  the private key. Never tracked, packaged, printed,
                     shared or copied into notes. make-release.ps1
                     -TestSignPfx takes its path at every cut.
    xhci98-test.cer  the public half, which make-package.ps1 -TestSignPfx
                     stages into every x64 package directory.

MakeCert shows a GUI dialog for the private-key (.pvk) password, and pvk2pfx
asks for that same password in a second dialog. The .pfx password is asked
for here, twice, and is never echoed or stored. **pvk2pfx takes it only on
its command line**, so for the moment that command runs it is visible to
other processes on the machine that can read command lines; run this where
none you do not trust are running. The intermediate .pvk is deleted at the
end, since the .pfx holds the key.

Guards: it refuses to overwrite any of the three files where one already
exists (a second key would make every published signature unverifiable
against the recorded thumbprint), and it refuses an -OutDir inside the
repository that git does not ignore. An -OutDir outside the repository is
accepted: that is the way to keep the key off a synced folder (the
repository's own location may be one; record 19 section 4).

At the end it prints the certificate's subject, validity, signature
algorithm, key size, enhanced key usage and SHA-1 thumbprint - the
thumbprint is what the packager checks every signature against and what
the docs record. Nothing here needs a network.

.PARAMETER OutDir
Where to write the key and certificate. Defaults to scripts\local\testsign
in the repository (git-ignored), created if missing.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\make-testcert.ps1
#>

[CmdletBinding()]
param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).ProviderPath
$wdk = Join-Path $repo "tools\WinDDK71\bin\x86"
$makecert = Join-Path $wdk "MakeCert.exe"
$pvk2pfx = Join-Path $wdk "pvk2pfx.exe"

foreach ($tool in @($makecert, $pvk2pfx)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "not found: $tool (WDK 7.1 belongs in tools\WinDDK71; docs\contributing\build-and-test.md)"
    }
}

if ($OutDir -eq "") { $OutDir = Join-Path $repo "scripts\local\testsign" }
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $PWD.ProviderPath $OutDir }
$OutDir = [System.IO.Path]::GetFullPath($OutDir).TrimEnd('\')
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

foreach ($f in @("xhci98-test.pvk", "xhci98-test.cer", "xhci98-test.pfx")) {
    if (Test-Path -LiteralPath (Join-Path $OutDir $f)) {
        throw "$f already exists in $OutDir - refusing to overwrite an existing key. Move it away first if you really mean to make a new one."
    }
}

# Inside the repository the key folder must be git-ignored, so the .pfx can
# never be committed by a `git add -A`; outside it there is nothing to ask.
$repoPrefix = $repo.TrimEnd('\') + '\'
if (($OutDir + '\').StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & git -C $repo check-ignore -q (Join-Path $OutDir "xhci98-test.pfx") 2>$null
        $ignored = ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if (-not $ignored) {
        throw "$OutDir is inside the repository and not git-ignored - refusing to create a private key where it could be committed."
    }
}

Push-Location -LiteralPath $OutDir
try {
    Write-Host ""
    Write-Host "Step 1 of 3: MakeCert (a dialog asks for a private-key password)..."
    & $makecert -r -pe -n "CN=xhci98 project test signing (not trusted)" -a sha1 -len 2048 -eku 1.3.6.1.5.5.7.3.3 -b 01/01/2000 -e 12/31/2039 -sv xhci98-test.pvk xhci98-test.cer
    if ($LASTEXITCODE -ne 0) { throw "MakeCert failed (exit $LASTEXITCODE)" }

    Write-Host ""
    Write-Host "Step 2 of 3: choose the .pfx password (asked for at every release cut)."
    $p1 = Read-Host -AsSecureString "PFX password"
    $p2 = Read-Host -AsSecureString "PFX password again"
    $b1 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($p1)
    $b2 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($p2)
    try {
        $s1 = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b1)
        $s2 = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b2)
        if ($s1 -ne $s2) { throw "the two passwords differ - nothing written beyond the .pvk and .cer; delete them and run again." }
        if ($s1.Length -eq 0) { throw "an empty password is refused." }
        Write-Host "pvk2pfx (a dialog asks for the private-key password from step 1)..."
        & $pvk2pfx -pvk xhci98-test.pvk -spc xhci98-test.cer -pfx xhci98-test.pfx -po $s1
        if ($LASTEXITCODE -ne 0) { throw "pvk2pfx failed (exit $LASTEXITCODE)" }
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b1)
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b2)
        $s1 = $null; $s2 = $null
    }

    Write-Host ""
    Write-Host "Step 3 of 3: the certificate:"
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 (Join-Path $OutDir "xhci98-test.cer")
    Write-Host ("  Subject    : " + $cert.Subject)
    Write-Host ("  NotBefore  : " + $cert.NotBefore.ToString("yyyy-MM-dd"))
    Write-Host ("  NotAfter   : " + $cert.NotAfter.ToString("yyyy-MM-dd"))
    Write-Host ("  Sig. alg.  : " + $cert.SignatureAlgorithm.FriendlyName)
    Write-Host ("  Key size   : " + $cert.PublicKey.Key.KeySize)
    $eku = @($cert.Extensions | Where-Object { $_.Oid.Value -eq "2.5.29.37" })
    if ($eku.Count -gt 0) {
        Write-Host ("  EKU        : " + (($eku[0].EnhancedKeyUsages | ForEach-Object { $_.FriendlyName }) -join ", "))
    }
    Write-Host ("  Thumbprint : " + $cert.Thumbprint + "   (the SHA-1 certificate hash the packager checks)")
    Write-Host ""

    Remove-Item -LiteralPath (Join-Path $OutDir "xhci98-test.pvk")
    Write-Host "Removed xhci98-test.pvk (the .pfx holds the key)."
    Write-Host ("Key: " + (Join-Path $OutDir "xhci98-test.pfx"))
    Write-Host "Done. The thumbprint may be shared and recorded; the password and the .pfx never."
} finally {
    Pop-Location
}
