# A CD image from a directory, with Windows' own IMAPI2 file-system image
# builder (no tool to fetch). ISO 9660 plus Joliet: Windows 98 SE and every
# NT guest read the Joliet names, so the package's long directory names
# (release-x86, tools\xhciinst) arrive as they are. Dot-source it.

if (-not ('XhciLegIsoWriter' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
public static class XhciLegIsoWriter {
    public static void Save(object comStream, string path) {
        IStream s = (IStream)comStream;
        byte[] buf = new byte[1 << 20];
        IntPtr pRead = Marshal.AllocHGlobal(4);
        try {
            using (FileStream f = new FileStream(path, FileMode.Create, FileAccess.Write)) {
                while (true) {
                    s.Read(buf, buf.Length, pRead);
                    int n = Marshal.ReadInt32(pRead);
                    if (n <= 0) break;
                    f.Write(buf, 0, n);
                }
            }
        } finally {
            Marshal.FreeHGlobal(pRead);
        }
    }
}
'@
}

function New-LegIso {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDir,
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Label = 'XHLEG'
    )
    if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) { throw "no directory $SourceDir" }
    # The image builder's COM class can refuse an instance for a moment
    # (0xC0AAB138, read on 2026-10-25 with two legs started together), so
    # it is asked again a few times.
    $fsi = $null
    for ($i = 1; -not $fsi; $i++) {
        try { $fsi = New-Object -ComObject IMAPI2FS.MsftFileSystemImage } catch {
            if ($i -ge 5) { throw }
            Start-Sleep -Seconds (5 * $i)
        }
    }
    # 1 = ISO 9660, 2 = Joliet (FsiFileSystems).
    $fsi.FileSystemsToCreate = 3
    $fsi.VolumeName = $Label
    $fsi.FreeMediaBlocks = 0
    $fsi.Root.AddTree((Resolve-Path -LiteralPath $SourceDir).Path, $false)
    $img = $fsi.CreateResultImage()
    [XhciLegIsoWriter]::Save($img.ImageStream, $Path)
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($img)
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($fsi)
    # The image builder holds its source files until its objects are
    # collected; the caller usually deletes the staging directory next.
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}

# Deletes a staging directory, retrying while a scanner or the image builder
# still holds a file in it; what cannot be deleted is left and said.
function Remove-LegTree {
    param([string]$Path)
    for ($i = 0; $i -lt 10; $i++) {
        if (-not (Test-Path -LiteralPath $Path)) { return }
        try { Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop; return } catch { Start-Sleep -Seconds 2 }
    }
    Write-Warning "could not delete $Path; left in place"
}
