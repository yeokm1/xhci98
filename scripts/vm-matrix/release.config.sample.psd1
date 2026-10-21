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
    # New Hardware wizard for a controller with no driver at every start).
    # Every guest runs on TCG (owner, 2026-10-20: no WHPX). A 9x guest takes
    # a CPU with a local APIC (pentium3, not pentium3,-apic): QEMU 11.1
    # cannot load a qemu-xhci's saved state when MSI-X could not be set up.
    Guests = @(
        @{
            Id = 'w98se'; Name = 'Windows 98 SE'; Family = '9x'
            Base = 'E:\xhci98-vm\release-matrix\base\w98se-pristine.qcow2'
            Cpu = 'pentium3'; Smp = 1; Memory = 256; Layout = 'qwerty'
            ColdStep = 'esc-wizard98'; CdLetter = 'D'; BootSeconds = 600; SettleSeconds = 30
        }
        @{
            Id = 'xp'; Name = 'Windows XP SP3'; Family = 'nt'
            Base = 'vm\winxp.img'
            Cpu = 'pentium3'; Smp = 2; Memory = 1024; Layout = 'dvorak'
            BootSeconds = 900; SettleSeconds = 90
        }
    )
}
