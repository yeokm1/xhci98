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
    $fsi = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
    # 1 = ISO 9660, 2 = Joliet (FsiFileSystems).
    $fsi.FileSystemsToCreate = 3
    $fsi.VolumeName = $Label
    $fsi.FreeMediaBlocks = 0
    $fsi.Root.AddTree((Resolve-Path -LiteralPath $SourceDir).Path, $false)
    $img = $fsi.CreateResultImage()
    [XhciLegIsoWriter]::Save($img.ImageStream, $Path)
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($img)
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($fsi)
}
