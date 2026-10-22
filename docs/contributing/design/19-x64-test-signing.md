# The x64 test signature

Design record for signing the published x64 binaries with a project test
certificate, so that Windows Vista x64 and Windows 7 x64 load the driver in
test-signing mode rather than only on a start where F8 -> Disable Driver
Signature Enforcement was chosen (roadmap-hcd Phase 36, task 36.50).
Revision 1, 2026-10-11, on branch `p36-tsign`, written with the packager
change it describes; the guest readings of section 10 follow the first
signed cut. Revision 2, 2026-10-19 (roadmap-usb4 task 42-A.30, owner): the
bundled Stor98's x64 `stor98.sys` is signed in the same step with the same
key, so one signature covers every driver of the package, and Stor98's own
test certificate (`stor98-test.cer`) and signing scripts are retired.
Revision 3, 2026-10-22 (roadmap-usb4 tasks 43.33 to 43.36, owner): **the
x64 package carries a catalog per INF**, made and signed by the packager
with the same key (section 3a), because Windows 8, 8.1 and 10 x64 refuse a
driver package with no catalog in test-signing mode (design record 13
section 6.14.5). It reverses this record's "no catalog, the INFs
unchanged" for the x64 package; the x86 package is unchanged.

## 1. Context

Vista x64 and Windows 7 x64 enforce kernel-mode code signing; XP x64 and
Server 2003 x64 do not, and 32-bit Windows does not either. Since
2026-09-16 the two enforcing systems are inside the tier this project
claims (design record 11 section 12, decision 1), and until this record
every package was unsigned, so on those two the driver loaded only on a
start where the user pressed F8 and chose Disable Driver Signature
Enforcement. That choice lasts one start: a machine with this driver as its
only USB host controller driver could never start unattended into working
USB. Without it the controller sat at Code 39 with nothing loaded (read on
Vista x64, `build-and-test.md`, "Vista x64 and Windows 7 x64 target VMs",
item 3).

Windows has a second documented route that lasts: test-signing mode
(`bcdedit /set testsigning on`), in which the loader accepts a driver
signed by any certificate, trusted or not, and the desktop shows "Test
Mode". It needs the driver to be signed. Design record 11 recorded on
2026-09-10 that a test-signed package works end to end on the host
(`build-and-test.md`, "A test-signed package was proved out host-side"),
and the owner removed the guest half of that route on 2026-09-16 untried,
because the published package was unsigned and no user could take it
without signing the driver themselves.

## 2. The decision

**Owner, 2026-10-11:** every published x64 package directory
(`release-x64`, `debug-x64`) carries `xhci98.sys` and `xhciuas.sys` with an
embedded Authenticode signature by the project test certificate - SHA-1
file digest, no timestamp, no catalog, both INFs unchanged - applied by the
packager at staging and never by hand. It was to go into the `2.2.0.3` TEST
build at once (re-cut with the same version and date); that re-cut was
cancelled the same day, so the first signed cut is `2.3.0.0`, and every cut
after it.

What it reverses, and how far. Design record 11 section 12, **decision 9**
(2026-09-10) decided "the package is not signed, and the line is not
added", on the ground that signing bought the claimed tier nothing, since
XP x64 and Server 2003 x64 do not enforce. That ground went on
2026-09-16 when Vista x64 and Windows 7 x64 joined the tier, and what the
decision then cost was the F8 start, every start. The reversal is for the
x64 package only and takes the narrowest form record 11 itself analysed:
the embedded signature alone, which the loader checks for a demand-start
service, and **not** the catalog, whose `CatalogFile.NTamd64` line decision
9 declined and which stays declined (section 3). The second half of
decision 9 - `src\xhci98-amd64.inf` byte-identical, the INF gate asked for
no opinion - is kept whole. Record 11 decision 9 now points here.
*(Both halves on the catalog reversed for the x64 package by the owner on
2026-10-22, section 3a: Windows 8 to 10 x64 install nothing without one.)*

## 3. What is signed, and how

- **The files:** `xhci98.sys`, `xhciuas.sys` and, since the Thunderbolt 3
  connection manager joined the package, `xhcicm.sys` of both published
  x64 flavours, each signed once. In `3.0.0.0` `nt\xhcicm.sys`, the
  filter INF's own copy, was the signed file staged a second time, the
  same bytes; from `3.1.0.0` the x64 package keeps the filter INF at the
  flavour directory's root beside the one `xhcicm.sys` and has no `nt\`.
  Since `3.1.0.0` also the
  bundled Stor98's `stor98.sys` of an amd64 package (`release-x64`; the
  debug directories carry no Stor98): Stor98 is built from `src\stor98`
  with the drivers and versioned with them, and the owner's decision of
  2026-10-19 is that one signature signs everything. Until then the
  bundled `stor98.sys` (Stor98 `0.3.0.0` to `0.4.0.0`) carried Stor98's
  own test signature, with its `stor98-test.cer` beside it; neither is
  shipped any more, and `xhci98-test.cer` covers every driver. The `qemu` flavour may be signed for a guest
  (`make-package.ps1 -Flavor qemu -Arch amd64 -TestSignPfx ...`) and is
  never published either way.
- **Never the x86 binaries.** 32-bit Windows does not enforce kernel-mode
  signing, so a signature there buys nothing and would change bytes every
  x86 reading was taken on. `make-package.ps1` refuses `-TestSignPfx` for
  the x86 package, and a re-cut's x86 halves are byte-identical to the
  unsigned cut's.
- **SHA-1 file digest** (`SignTool sign /fd sha1`). Vista's code integrity
  verifies SHA-1 Authenticode digests only; Windows 7 verifies SHA-256
  ones only after a later Microsoft update (the SHA-2 code-signing support
  update), which a user cannot be assumed to have. SHA-1 is what both
  verify as installed. The certificate's own signature is SHA-1 too
  (`MakeCert -a sha1`), for the same reader.
- **No timestamp.** A timestamp proves the signature was made while the
  certificate was valid, which matters once a certificate expires or is
  revoked. In test-signing mode the loader does not ask, the certificate is
  valid to the end of 2039, and a timestamp needs a network service at
  every cut - the cut is offline and stays so. Without one, signing the
  same bytes with the same key gives the same bytes (read 2026-10-11:
  two signing runs of one binary hashed identically), so a re-cut is
  reproducible.
- **No page hashes** (`/nph`): nothing here needs per-page verification,
  and leaving them out keeps the appended table small and the result
  independent of the host's `wintrust.dll` default.
- ~~**No catalog, and the INFs byte-identical.**~~ *(Until 3.1.0.0;
  reversed for the x64 package on 2026-10-22, section 3a.)* Both services are
  `StartType=3`, demand-start, so the loader checks the embedded signature
  on its own; on Vista and 7 x64 a catalog governs only the install-time
  "Windows can't verify the publisher" prompt (record 11 decision 9's
  analysis), so the prompt stayed and cost one click. Windows 8 to 10 x64
  do not prompt: their driver store refuses the package (section 3a).
- **What signing changes in the file:** the optional header's checksum,
  the certificate-table directory entry, up to seven bytes of zero padding
  to an 8-byte boundary, and the appended `WIN_CERTIFICATE` holding the
  PKCS #7 signature (about 1.5 KB with a 2048-bit key). Nothing else. That
  is the property the packager checks (section 5), and it is why every
  reading taken on the unsigned bytes - every matrix reading on the same
  build - speaks for the signed ones.

## 3a. The catalogs (owner, 2026-10-22; from 3.2.0.0)

**Why.** Measured on Windows 10 22H2 x64 in test-signing mode (record 13
section 6.14.5, 2026-10-22): the `3.1.0.0` x64 package, its binaries
signed as section 3 prescribes, was refused by the driver store with
`0xE000022F` and no prompt, the setup log reading "Driver package does not
contain a catalog file, and Code Integrity is in Test Signing mode". 8 and
8.1 x64 run the same driver store (by inference until a leg reads them).
The owner's decision the same day: a test-signed catalog in the x64
package, made and signed by the packager alone.

**What.** Every INF of a signed amd64 package names a catalog of its own,
undecorated, in `[Version]`:

| INF | Catalog | Covers |
|---|---|---|
| `xhci98.inf` | `xhci98.cat` | `xhci98.inf`, `xhci98.sys` |
| `xhciuas.inf` | `xhciuas.cat` | `xhciuas.inf`, `xhciuas.sys` |
| `xhcicm.inf` | `xhcicm.cat` | `xhcicm.inf`, `xhcicm.sys` |
| `xhcicmf.inf` | `xhcicmf.cat` | `xhcicmf.inf`, `xhcicm.sys` |
| `stor98.inf` (`release-x64`) | `stor98.cat` | `stor98.inf`, `stor98.sys` |

Each covers its INF and every file the INF's `[SourceDisksFiles]` names
(`$script:PackageCatalogs` in `package-common.ps1`; the packager refuses a
staged INF that names a file outside its row). The OS-supplied files
`xhci98.inf`'s NT 5.2 path fetches through `LayoutFile=layout.inf` are not
members: they come from the OS's own source.

- **Made by** WDK 7.1's `MakeCat` (`tools\WinDDK71\bin\x86\MakeCat.exe`)
  from a CDF the packager writes, each member tagged by its digest
  (`<HASH>`), with a `File` name-value and the catalog's `OSAttr`
  `2:5.2,2:6.0,2:6.1,2:6.2,2:6.3,2:10.0` (the form Inf2Cat writes; WDK
  7.1's Inf2Cat knows no system after 7, so it is not used). Members are
  SHA-1: an INF's over the file, a driver's the Authenticode image digest,
  which the embedded signature leaves unchanged.
- **Signed by** the project key, as the binaries are: `SignTool sign /fd
  sha1`, no timestamp, the key-container check of section 5.
- **Held to** `Test-PackageCatalog`: one signer, the project certificate by
  thumbprint, SHA-1, a PKCS #7 signature that verifies, no countersignature,
  a catalog list with that `OSAttr`, and exactly one member per covered
  file, by name, whose digest and tag are that file's and whose indirect
  data type is the PE one for a driver and another for an INF.
  `make-package.ps1` checks each as it is made and the staged package as a
  whole (`Test-CatalogOfPackage`); `make-release.ps1` checks each published
  x64 directory and the upload set's (from 3.2.0.0).
- **The INFs change by one line each** (`CatalogFile=`), held by the INF
  gates' `BOTH-CATALOG` (amd64: exactly that name, once; x86: none), and
  Stor98's `check-stor98-inf.ps1`. A published tree before 3.2.0.0 is not
  asked about the line (`-PublishedVersion`).
- **Not reproducible byte for byte**: a catalog carries the time it was
  made, so two cuts of the same build differ in their `.cat` files, unlike
  the binaries (section 3). What a catalog vouches for is checked, not its
  bytes.
- **Never the x86 package.** 32-bit 8, 8.1 and 10 take the unsigned
  package after "Windows can't verify the publisher" (measured on 10 x86,
  record 13 section 6.14.5); a catalog signed by a certificate no machine
  trusts would not remove that prompt; the x86 `xhci98.inf` is read by
  Windows 98's 16-bit engine, whose handling of a `CatalogFile` line is
  unmeasured and would cost every 9x install leg again; and the x86
  binaries are never signed (section 3), which a catalog would only half
  undo. The x86 INFs therefore name no catalog, and the gates hold that.
- **An unsigned amd64 package** (a clone without the key) names catalogs it
  does not carry, and the packager warns: Windows 8 to 10 x64 refuse it;
  Vista and 7 x64 treat it as unsigned (by inference).
- **What it does not change.** On Vista and 7 x64 the prompt stays ("Windows
  can't verify the publisher": the signer is trusted nowhere); test-signing
  mode is still needed for the loader; Secure Boot on still refuses a
  test-signed driver on 8 to 10 (record 23 section 5.12). It is not
  Microsoft signing (section 8).

## 4. The certificate and the key

**The certificate is the project's own, made once by the maintainer who
holds the key** (the owner made it on 2026-10-11; section 10 records it),
with WDK 7.1's MakeCert and pvk2pfx, both in
`tools\WinDDK71\bin\x86` (used in place, never redistributed;
`legal-provenance.md`). `scripts\package\make-testcert.ps1` runs exactly
the commands below with guards around them (it refuses to overwrite an
existing key or to write into a folder of the repository git does not
ignore, asks the .pfx password twice without echo, deletes the .pvk, and
prints the certificate's facts); the raw commands are:

```powershell
# The key folder, git-ignored (scripts\local\ is ignored as a whole):
New-Item -ItemType Directory -Force scripts\local\testsign
Set-Location scripts\local\testsign
$wdk = '<repo>\tools\WinDDK71\bin\x86'
# The certificate and its private key (MakeCert asks for the .pvk password in a dialog):
& "$wdk\MakeCert.exe" -r -pe -n "CN=xhci98 project test signing (not trusted)" -a sha1 -len 2048 -eku 1.3.6.1.5.5.7.3.3 -b 01/01/2000 -e 12/31/2039 -sv xhci98-test.pvk xhci98-test.cer
# The .pfx the packager signs with (pvk2pfx asks for the .pvk password, -po sets the .pfx's):
& "$wdk\pvk2pfx.exe" -pvk xhci98-test.pvk -spc xhci98-test.cer -pfx xhci98-test.pfx -po "<pfx password>"
# The check: subject, validity, code-signing usage and the SHA-1 thumbprint:
certutil -dump xhci98-test.cer | Select-String 'Subject:|NotBefore|NotAfter|Cert Hash\(sha1\)|Code Signing'
```

What each MakeCert flag is for:

- `-r` - self-signed: the certificate is its own root. Nothing chains to a
  public authority, which is the point: it is a test certificate.
- `-pe` - the private key is exportable, which pvk2pfx needs.
- `-n "CN=xhci98 project test signing (not trusted)"` - the subject, which
  says what it is to anyone who inspects a signed file. The packager
  refuses a .pfx with any other subject.
- `-a sha1` - the certificate's own signature algorithm, one Vista can
  verify (section 3).
- `-len 2048` - an RSA key of 2048 bits, which every target verifies.
- `-eku 1.3.6.1.5.5.7.3.3` - the Code Signing enhanced key usage, which
  SignTool requires of a signing certificate by default.
- `-b 01/01/2000` - valid from 2000, so it is already valid on a machine
  whose clock is earlier than the day it was made: the Windows 7 guests run
  at `-Rtc base=2026-09-20`, and a laptop with a flat CMOS battery starts
  in its BIOS's year.
- `-e 12/31/2039` - MakeCert's documented default end date, within the
  2049 limit of the UTCTime encoding MakeCert writes. Signing is offline
  with no timestamp, so the validity is chosen long.
- `-sv xhci98-test.pvk` - write the private key to a .pvk file, protected
  by the password MakeCert's GUI dialog asks for.

pvk2pfx combines the .pvk and the .cer into `xhci98-test.pfx`, asking for
the .pvk password and protecting the .pfx with the one given by `-po`. Once
the .pfx exists the .pvk may be deleted (`make-testcert.ps1` does), since
the .pfx holds the same key. **pvk2pfx takes the .pfx password only on its
command line**, where it is briefly visible to local processes that can read
command lines; so does SignTool at every cut (section 5).

The files:

| file | what it is | where it lives |
|---|---|---|
| `xhci98-test.pfx` | the private key and certificate | `scripts\local\testsign\` on the maintainer's disk, git-ignored. **Never tracked, never packaged, never printed, never copied into notes or memory.** |
| `xhci98-test.cer` | the public half | the same folder, and staged by the packager into every x64 package directory |
| the .pfx password | - | supplied at each cut from the `XHCI98_TESTSIGN_PW` environment variable or typed at a prompt that does not echo; never stored |

**The repository lives in a OneDrive folder on the maintainer's machine**,
so a key in `scripts\local\testsign\` is synchronised to that OneDrive
account (git-ignored is not the same as local-only). The packager takes the
.pfx by path (`-TestSignPfx`), so a folder outside any synchronised tree
works equally; `make-testcert.ps1 -OutDir` writes there.

The SHA-1 certificate hash `certutil` prints as `Cert Hash(sha1)` is the
**thumbprint**: what the packager checks every signature against and what
the docs record (section 10). The thumbprint is public; the .pfx and its
password are not.

## 5. Where it happens: the packager

- **`make-package.ps1 -TestSignPfx <path>`** (amd64 only) opens the .pfx
  (refusing a wrong password, a file with no private key, a subject other
  than the project's, or any certificate but the one whose thumbprint
  section 10 records - `$script:TestSignThumbprint` in
  `package-common.ps1`), lets the import gates read the build where it was
  linked (their `SITES` rule needs the objects, or for LTCG objects the
  sources, beside the image), and then signs **copies** of `xhci98.sys`,
  `xhciuas.sys`, `xhcicm.sys` and the bundled `stor98.sys` (from
  `src\stor98\out\free\amd64`, already held to its version, flavour marker,
  imports and source stamp; with `-SignOnly`, to the gated build's record
  like the others) in a temporary folder before staging. Each copy is
  held to two checks before it is used: its parsed signature is the
  project certificate's by thumbprint, an Authenticode PE-image signature
  (content `SPC_INDIRECT_DATA`, data `SPC_PE_IMAGE_DATA`, the
  `SpcIndirectDataContent` parsed in full) with SHA-1 as both the signer's
  and the file digest, the vouched digest equal to the image's, a PKCS #7
  signature that verifies and no countersignature
  (`Test-ProjectTestSignature`); and it is the build byte for byte apart
  from the checksum, the directory entry, the padding and the table
  (`Test-SignedImageOf`). So what the import gates read is what ships, and
  the version check against the INF, and every check `make-release.ps1`
  makes, read the signed copy itself. The image digest is computed over the
  file in order less the checksum, the directory entry and the table, which
  equals Authenticode's for a linker-produced image (checked 2026-10-11 on
  three Microsoft-signed Windows 11 drivers, whose SHA-256 vouched digests
  it reproduced); an image laid out otherwise fails as a mismatch. An input that is already signed is
  refused: nothing is signed by hand. The certificate's public half is
  staged at the package root as `xhci98-test.cer`. Without `-TestSignPfx`
  the package is staged unsigned, exactly as before - enough for a guest
  started with F8, and what a clone without the key builds.
- **`make-release.ps1` refuses an x64 cut without the key**, before
  anything is built, with no switch past it: a cut that publishes an x64
  directory and was not given `-TestSignPfx` stops with a message naming
  this record. With the key, it opens the .pfx once (so a wrong password
  costs a prompt, not two builds), hands the password to each x64 leg's
  `make-package.ps1` through `XHCI98_TESTSIGN_PW`, set in its own process
  environment around that one call and restored after it, and then holds each published x64 `xhci98.sys`,
  `xhciuas.sys` and `xhcicm.sys` to the same two checks against the build output beside its
  source stamp (`Assert-TestSigned`), and the published `xhci98-test.cer`
  to the key's public half byte for byte. The x86 legs are held, as
  before, to staged bytes equal to the build's.
- `-TestSignPfx` is refused with `-UploadSetOnly` (which re-assembles a
  published tree and stages nothing) and with an `-Arch` that has no x64.
- `scripts\package\test-package.ps1` covers the checks with synthetic PE
  images and mock parsed signatures, and the refusals through the scripts'
  arguments; it needs no key and never opens one.

**SignTool and key containers.** SignTool `/f` imports the .pfx's private
key into a CryptoAPI key container of its own to sign with. On 2026-10-11,
on development host A, two signing runs of WDK 7.1's SignTool with a
throw-away certificate (made before the owner's decision that he creates
the key, and deleted with its containers the same hour) each left one new
container under `%APPDATA%\Microsoft\Crypto\RSA\<user SID>\` - a copy of
that private key outside the key folder. `make-package.ps1` therefore
compares that folder before and after each signing and warns, naming any
new container; it does not delete them, since it cannot prove another
process did not create one in the same moment. After a cut that warns,
the maintainer deletes the named files. The warning prints each one's
full path as a ready `Remove-Item` line. The files are hidden and system,
inside the `<user SID>` subfolder, so a plain `Get-ChildItem` shows
nothing; list the newest with

```powershell
Get-ChildItem "$env:APPDATA\Microsoft\Crypto\RSA" -Recurse -Force -File | Sort-Object LastWriteTime -Descending | Select-Object -First 5 FullName, LastWriteTime
```

then, for each file the warning named and only those,

```powershell
Remove-Item -LiteralPath "<path the warning named>" -Force
```

and list again to see them gone. The owner's key leaves them too (section
10).

## 6. Verifying a signed binary

```
tools\WinDDK71\bin\x86\SignTool.exe verify /pa /v release-x64\xhci98.sys
```

prints `Hash of file (sha1)`, the signing certificate chain with the
subject above and its SHA-1 hash (the thumbprint), and `File is not
timestamped.`, and then **fails** with "A certificate chain processed, but
terminated in a root certificate which is not trusted by the trust
provider". That failure is the expected result: the certificate is trusted
nowhere, by design, and the loader in test-signing mode does not ask for
trust. What is checked is the thumbprint against section 10's and the
digest algorithm (sha1). `certutil -dump xhci98-test.cer` gives the same
thumbprint for the shipped public half.

## 7. What users do

On Vista x64 and Windows 7 x64, once: an elevated Command Prompt,
`bcdedit /set testsigning on`, a restart; "Test Mode" with the Windows
version and build then shows in a corner of the desktop, and the driver
loads at every start with nothing pressed. The install still asks about
the publisher ("Install this driver software anyway"). `bcdedit /enum
{current}` shows `testsigning Yes`; `bcdedit /set testsigning off` and a
restart leave it, after which the driver needs F8 again or does not load
(Code 39 on Vista, Code 52 on 7). The Driver tab says "Not digitally
signed" in either mode, which is expected (section 10).
F8 -> Disable Driver Signature Enforcement still works, without test mode,
for one start. With BitLocker on the system drive, BitLocker is suspended
before the boot setting is changed, or the next start asks for the
recovery key. Secure Boot does not arise: Vista and 7 do not support it.
32-bit Vista and 7 and XP x64 need none of this.

**Importing `xhci98-test.cer` is not needed and not recommended.** Test-
signing mode loads the driver without it, and it does not quiet the install
prompt either: Plug and Play judges the driver package by its catalog, and
until 3.1.0.0 this package deliberately had none (section 3), so trusting
the binaries' signer did not sign the package (this corrects an earlier
draft of this record). From 3.2.0.0 the catalogs are signed by the same
certificate (section 3a); importing it into the trusted stores would then
make the machine trust this project's packages outright, which is still
the user's decision and still advised against. Imported into a trusted store it would only
make the machine trust a certificate this project made - which is the
user's decision, and the docs advise against it. The `.cer` is shipped so
that anyone can compare a signed binary's signer with the thumbprint of
section 10. The user docs (`README.md`, `docs\using\release-notes.md`, the
release readme) carry this procedure in those words.

## 8. What it is not

**This is not Microsoft signing**, and nothing may say or imply it is. The
certificate is self-signed, chains to nothing, and no Windows trusts it by
itself; it does not make the driver load on a machine in its ordinary
mode, does not remove the install prompt, and is not WHQL, attestation or
cross-signing. Test-signing mode is a Windows development facility that
loads any signed driver, which is why Windows marks the desktop while it is
on. The release notes say so beside the tier.

## 9. The rule

Every published x64 package directory carries `xhci98.sys`, `xhciuas.sys`
and `xhcicm.sys` (and, in `3.0.0.0`, its `nt\xhcicm.sys` copy), and a
`release-x64` directory from `3.1.0.0` also the bundled `stor98.sys`, with an embedded
Authenticode signature by the project test
certificate - SHA-1 file digest, no timestamp - and, from `3.2.0.0`, a
catalog per INF signed by the same certificate the same way (section 3a),
applied by `make-package.ps1 -TestSignPfx` at staging and never by hand,
and `make-release.ps1` refuses a cut that would publish an x64 directory
otherwise. The private key is held only on the maintainer's
disk in a git-ignored or out-of-tree folder and is never tracked,
packaged, printed or copied into notes. x86 binaries are never signed. A
clone without the key stages unsigned packages for its own use and cannot
cut a release with an x64 half. `AGENTS.md` carries the rule in short and
points here.

## 10. Readings, and what remains unread

- Host, 2026-10-11, development host A, with a throw-away certificate
  (section 5): WDK 7.1 SignTool signs the `2.2.0.3` `release-x64`
  `xhci98.sys` (309,248 bytes) with `/fd sha1 /nph`, appending 1,496 bytes;
  `SignTool verify /pa /v` reports `Hash of file (sha1)`, the chain, "File
  is not timestamped" and the untrusted-root failure; two runs give
  identical bytes; `Get-EmbeddedSignature` parses the signature as SHA-1
  (`1.3.14.3.2.26`) by that certificate's thumbprint.
- **The project certificate**, made by the owner on 2026-10-11 with the
  commands of section 4 (`scripts\local\testsign\xhci98-test.pfx`, public
  half `scripts\local\testsign\xhci98-test.cer`): subject `CN=xhci98
  project test signing (not trusted)`, valid 2000-01-01 to 2039-12-31,
  `sha1RSA`, RSA 2048, **SHA-1 thumbprint
  `A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF`**. This is the value the
  packager checks every signature against and the only key a published x64
  binary may carry.
- **The signing rehearsal**, 2026-10-11, development host A: the owner ran
  `make-package.ps1 -Flavor release -Arch amd64 -TestSignPfx ...` on branch
  `p36-tsign` at `e8b1b09` (the `2.3.0.0` code before its stamp, version
  still `2.2.0.3`), into `out\p36-tsign\rehearsal\pkg-release-amd64`. Both
  binaries carry the signature this record prescribes: signer
  `A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF`, an Authenticode PE-image
  signature with SHA-1 as both digests, the vouched digest equal to the
  image's, no timestamp; each is the build plus the signature and nothing
  else (`xhci98.sys` 310,272 to 311,768 bytes, `xhciuas.sys` 36,352 to
  37,848); `SignTool verify /pa /v` shows the chain with that thumbprint,
  "File is not timestamped" and the expected untrusted-root failure. The
  INFs, `txtsetup.oem` and `portsw.vbs` are byte-identical to an unsigned
  package of the same build. **SignTool printed the key-container warning
  with the owner's key too**, so every signing run leaves them; the owner
  deleted the named files himself (section 5).
- The signed `2.2.0.3` re-cut was cancelled (owner, 2026-10-11); the first
  signed cut is `2.3.0.0`. The guest legs - Vista x64 and Windows 7
  x64 in test-signing mode with no F8, twice; the same without test-signing
  mode (the Code shown); XP x64 unaffected - are read on the rehearsal
  package and recorded below and in `runs/run-36.md`; the `2.3.0.0` cut's
  own signature check follows it.
- **The guest legs on the rehearsal package**, 2026-10-11, development
  host A, QEMU guests, the `release` flavour; every row passed
  (`out\p36-tsign\legs\{vx64,w7x64,xp64}\notes.txt`):
  - **Test-signing mode loads the driver with no F8, on both NT 6.x x64
    systems, and keeps loading across a restart.** `bcdedit /set
    testsigning on`, a restart, and every later start ordinary - no F8, no
    boot menu: controller and root hub started, `xhci98` running, the
    installed file `fc`-identical to the signed one, a USB mouse moving the
    pointer, a stick copy compared clean, `XHCISNAP` reading the release
    build; a second plain restart the same. `xhciuas.sys` loaded too, on a
    QEMU UAS disk, after the same install prompt. Nothing was imported into
    any certificate store.
  - **The install prompt stays**: "Windows can't verify the publisher of
    this driver software", answered with "Install this driver software
    anyway", as section 3 predicted for a package with no catalog. No
    Program Compatibility Assistant box appeared on either.
  - **The Driver tab says "Not digitally signed"** on both, in both modes.
    Expected for a package with no catalog: the field reports how the
    driver package was signed at install, while the loader checks the
    embedded signature of the binary.
  - **The watermark**: Windows 7 x64 shows "Test Mode / Windows 7 / Build
    7601" at the bottom right; Vista x64 shows "Test Mode" in all four
    corners (with the build line at the top) - read on an overlay whose
    activation was in grace; with Vista's activation expired, the
    not-genuine watermark takes that place and no "Test Mode" text shows,
    though the mode is on (`SystemStartOptions` `/TESTSIGNING`) and the
    driver loads.
  - **Without test-signing mode and without F8, the driver does not load,
    and the Code differs**: Vista x64 shows **Code 39**, "Windows cannot
    load the device driver for this hardware. The driver may be corrupted
    or missing. (Code 39)"; Windows 7 x64 shows **Code 52**, "Windows
    cannot verify the digital signature for the drivers required for this
    device. ... (Code 52)". On both `xhci98` and `xhciuas` are STOPPED
    with exit code 1077, and Code Integrity logs event 3004 for
    `xhci98.sys`; no notification is shown to the user.
  - **XP x64 ignores the signature**: the signed package installs after
    the usual Logo-testing box ("has not passed Windows Logo testing",
    Continue Anyway), the Driver tab says "Not digitally signed", and the
    controller, stick and mouse work across a restart, exactly as the
    unsigned package did.
  - Harness notes, not user facts: the Vista x64 and Windows 7 x64 base
    images carry `advancedoptions` on `{current}`, removed first so that
    the starts were ordinary; Vista's activation grace needs the launcher's
    `-Rtc base=...` for the "Test Mode" text to show.
- Unread, and not planned: real hardware under test-signing mode; a
  Windows 7 x64 with the SHA-2 update; Server 2003 x64 (identity with XP
  x64, which ignores signatures).

## 11. Files

- `scripts\package\package-common.ps1`: `Get-PeSecurityLayout`,
  `Test-SignedImageOf`, `Get-PeImageDigest`, `Get-EmbeddedSignature`,
  `Test-ProjectTestSignature`, `Get-TestSignPassword`,
  `Get-TestSignCertificate`; the catalogs' `$script:PackageCatalogs`,
  `Read-CatalogFile`, `Get-CatalogMemberDigest`, `Test-PackageCatalog` and
  `Test-CatalogOfPackage` (section 3a).
- `scripts\package\make-package.ps1`: `-TestSignPfx`, `Invoke-TestSign`,
  `Invoke-TestSignCatalog`, the staged `xhci98-test.cer` and catalogs.
- The INF gates' `BOTH-CATALOG` (`check-inf.ps1`, `check-uas-inf.ps1`,
  `check-cm-inf.ps1`) and `check-stor98-inf.ps1`.
- `scripts\package\make-release.ps1`: `-TestSignPfx`, the x64 refusal,
  `Assert-TestSigned`, the published `xhci98-test.cer`, the readme text.
- `scripts\package\make-testcert.ps1`: the certificate's creation (the one
  certificate; Stor98's own `scripts\make-testcert.ps1` and `sign-x64.ps1`
  were removed on 2026-10-19).
- `scripts\package\test-package.ps1`: the cases of section 5.
- Docs: design record 11 (section 6 and decision 9), `build-and-test.md`,
  `legal-provenance.md`, `README.md`, `docs\using\release-notes.md`,
  `AGENTS.md`, `roadmap-hcd.md` task 36.50.
