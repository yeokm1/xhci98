<#
    Per-host configuration of the release matrix (make-start.ps1, run-leg.ps1,
    release-matrix.ps1; build-and-test.md, "Faster emulator legs"). Copy it
    to scripts\vm-matrix\release.config.psd1 (git-ignored) and edit the
    paths. Every path may be absolute or relative to the repository root.
    EXAMPLE VALUES: nothing here is a statement of where any host keeps them.
#>
@{
    # QEMU, as lib\qemu.ps1 resolves it ('' = -Qemu, XHCI98_QEMU, PATH, then
    # the install layouts this project has met). qemu-img.exe must sit beside
    # it. A starting point records the version it was saved under; a leg on
    # another version refuses it.
    Qemu = ''

    # QemuDllPath: the folder of the DLLs a QEMU build needs on PATH. The q35
    # guests run on the build their bases were made and checked with (a copy
    # of E:\xhci98-vm\q35-bases\bin\...\qemu-system-x86_64.exe, usb4-emu's
    # QEMU 11.1.2, with usb4-emu's toolchain\msys64\ucrt64\bin here and its
    # vms\qemu-baseline\share as -L in each guest's Extra): read on 2026-10-26,
    # QEMU 11.1.0 (scoop) stopped 98 SE's start on q35 at the logo, where
    # that build reached the desktop with the same disk.
    QemuDllPath = ''

    # Starting points (start\<guest>\), leg runs (runs\<run>\<leg>\) and
    # pids.txt, the record of every QEMU started and stopped.
    WorkDir = 'E:\xhci98-vm\release-matrix'

    # The port block: three ports a leg (QMP, VNC, the migration socket), so
    # PortCount / 3 legs at once at most. Keep it outside Windows' excluded
    # ranges (netsh interface ipv4 show excludedportrange protocol=tcp).
    PortBase  = 48430
    PortCount = 9

    # The vCPUs the matrix may hold at once: 0 = the host's logical processors.
    # Every QEMU on the host counts, not only the matrix's own.
    MaxVcpu = 0

    # A 128 MiB FAT16 qcow2 with TESTDATA.BIN at its root: the stick the
    # guest asks for. Each plug takes an overlay of it.
    StickImage = 'vm\usb4\base\usbfat128.qcow2'

    # Base: the installed Windows with no package of this project's on it,
    # only ever read (each starting point is an overlay of it). Family 9x or
    # nt picks the guest scripts; Layout is the guest keyboard's (the NT
    # images of build-and-test.md are US Dvorak). ColdStep is the host step a
    # cold start needs before the poller can run (a 9x guest raises the Add
    # New Hardware wizard for a controller with no driver at every start),
    # or before an install can (XP's Found New Hardware wizard, esc-fnh).
    # PrepSteps are host steps make-start.ps1 takes once, after the poller's
    # setup (XP's unsigned-driver policy at Ignore, xp-signing-ignore).
    # Every guest runs on TCG (owner, 2026-10-20: no WHPX). A 9x guest takes
    # a CPU with a local APIC (pentium3, not pentium3,-apic): QEMU 11.1
    # cannot load a qemu-xhci's saved state when MSI-X could not be set up.
    # Machine: q35 for every new leg (owner, 2026-10-25), as the q35 bases
    # run (E:\xhci98-vm\q35-bases, launch-q35.ps1); without it, pc
    # (i440FX), the machine of the legs read before. On q35: DiskBus ahci
    # (the board's ICH9 AHCI) or piix (a piix3-ide at 00:1e.0: 2000 and XP
    # have no inbox AHCI driver, ME fails on AHCI); CdBus, the leg's CD, the
    # same unless given, or disk: a read-only FAT disk on AHCI port 1 (98 SE
    # reaches AHCI through INT 13h alone and sees no CD there; a piix3-ide
    # beside it stopped its start), and that guest's legs start cold; Hole64Off and Win2kHack
    # as the bases were made (2000, XP); Rtc a fixed date (Vista and 7 say
    # "not genuine" at the host's date). Vga overrides std. HostPowerOff: the
    # guest cannot switch the machine off (2000 with the Standard PC HAL
    # stops at "It is now safe to turn off your computer"), so the host quits
    # QEMU once the disk is quiet after the shut down. ShutdownCommand
    # replaces make-start.ps1's shut down (2000 has no shutdown.exe).
    # SetupElevated (Vista, 7): the poller's setup is started elevated from
    # the Start menu's search box, and its consent prompt answered with
    # ConsentKey (default alt-y, Yes on 7; Vista's Continue is alt-c).
    Guests = @(
        @{
            Id = 'w98se'; Name = 'Windows 98 SE'; Family = '9x'
            Base = 'E:\xhci98-vm\q35-bases\w98se\disk.qcow2'
            Extra = @('-L', 'D:\OneDrive\git-projects\usb4-emu\vms\qemu-baseline\share')
            Machine = 'q35'; DiskBus = 'ahci'; CdBus = 'disk'
            Cpu = 'pentium3'; Smp = 1; Memory = 512; Layout = 'qwerty'
            ColdStep = 'esc-wizard98'; CdLetter = 'D'; BootSeconds = 600; SettleSeconds = 30
        }
        @{
            Id = 'me'; Name = 'Windows ME'; Family = '9x'
            Base = 'E:\xhci98-vm\q35-bases\me\disk.qcow2'
            Extra = @('-L', 'D:\OneDrive\git-projects\usb4-emu\vms\qemu-baseline\share')
            Machine = 'q35'; DiskBus = 'piix'
            Cpu = 'pentium3'; Smp = 1; Memory = 512; Layout = 'qwerty'
            ColdStep = 'esc-wizard98'; CdLetter = 'D'; BootSeconds = 600; SettleSeconds = 30
        }
        @{
            Id = 'w2k'; Name = 'Windows 2000 SP4'; Family = 'nt'
            Base = 'E:\xhci98-vm\q35-bases\w2k\disk.qcow2'
            Extra = @('-L', 'D:\OneDrive\git-projects\usb4-emu\vms\qemu-baseline\share')
            Machine = 'q35'; DiskBus = 'piix'; Hole64Off = $true; Win2kHack = $true; Vga = 'cirrus'
            Cpu = 'pentium3'; Smp = 1; Memory = 512; Layout = 'dvorak'
            ColdStep = 'walk-fnh'; HostPowerOff = $true; ShutdownCommand = 'cscript //nologo C:\XHLEG\XHOFF.VBS'
            BootSeconds = 900; SettleSeconds = 30
        }
        @{
            Id = 'xp'; Name = 'Windows XP SP3'; Family = 'nt'
            Base = 'E:\xhci98-vm\q35-bases\xp\disk.qcow2'
            Extra = @('-L', 'D:\OneDrive\git-projects\usb4-emu\vms\qemu-baseline\share')
            Machine = 'q35'; DiskBus = 'piix'; Hole64Off = $true
            Cpu = 'pentium3'; Smp = 2; Memory = 1024; Layout = 'dvorak'
            ColdStep = 'esc-fnh'; PrepSteps = @('xp-signing-ignore')
            BootSeconds = 900; SettleSeconds = 30
        }
        @{
            Id = 'vista'; Name = 'Windows Vista SP2'; Family = 'nt'
            Base = 'E:\xhci98-vm\q35-bases\vx86\disk.qcow2'; ConsentKey = 'alt-c'
            Extra = @('-L', 'D:\OneDrive\git-projects\usb4-emu\vms\qemu-baseline\share')
            Machine = 'q35'; DiskBus = 'ahci'; Rtc = '2026-09-20T12:00:00'
            Cpu = 'qemu64'; Smp = 2; Memory = 2048; Layout = 'dvorak'; SetupElevated = $true
            BootSeconds = 1200; SettleSeconds = 60
        }
        @{
            Id = 'w7'; Name = 'Windows 7 SP1'; Family = 'nt'
            Base = 'E:\xhci98-vm\q35-bases\w7x86\disk.qcow2'
            Extra = @('-L', 'D:\OneDrive\git-projects\usb4-emu\vms\qemu-baseline\share')
            Machine = 'q35'; DiskBus = 'ahci'; Rtc = '2026-09-20T12:00:00'
            Cpu = 'qemu64'; Smp = 2; Memory = 2048; Layout = 'dvorak'; SetupElevated = $true
            BootSeconds = 1200; SettleSeconds = 60
        }
    )
}