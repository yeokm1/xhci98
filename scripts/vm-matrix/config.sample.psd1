<#
    Per-host configuration for the Phase 10 device matrix.

    Copy this to a file of your own - `scripts\vm-matrix\matrix.config.psd1` is git-ignored for
    the purpose - and edit the paths.  NOTHING in the committed harness knows
    where your QEMU or your images are, which is task 10.5's requirement and
    which is why every scripts\local\ launcher had to be edited per host.

    Every path may be absolute, or relative to the repository root.
#>
@{
    # Blank means "find it": an explicit -Qemu argument, then $env:XHCI98_QEMU,
    # then PATH, then the install layouts this project has met (winget's
    # C:\Program Files\qemu and a scoop prefix).  Set it here to pin one.
    Qemu = ''

    VmDir = 'vm'

    # The Windows 98 SE CD image, used ONLY by prepare-image.ps1 - the matrix
    # itself never needs it.  Windows 98's driver-install wizard asks for files
    # that `C:\WINDOWS\OPTIONS\CABS` on the 2a image does not all carry, and
    # without media it stalls on a modal dialog that blocks the bind.
    #
    # NOT `tools\w98se.img`: that is a 1.44 MB BOOT FLOPPY, whatever its name
    # suggests, and prepare-image.ps1 checks the size for exactly that reason.
    # The CABs live in \WIN98 on the CD.  This repo hardcodes no source; the
    # image is proprietary and where you get it from is your business.
    Win98Cd = 'D:\isos\w98se.iso'

    # $true (the default) boots every group with -snapshot, so the guest images
    # are never written to.  A matrix exists to be re-run, and a run that
    # mutates its own starting state is not reproducible.  It also means a row
    # that wedges the guest costs a group rather than an image.
    #
    # Set it $false only to do something you MEAN to persist - installing a new
    # build of the driver, for instance - and set it back afterwards.
    Snapshot = $true

    # Where reports and per-run evidence go.  A run writes one report plus a
    # debug console log and a QEMU trace per group.
    OutDir = 'out\phase10'

    # Where the post-release run (run-matrix.ps1 -PostRelease) writes; the
    # version under test is appended, so two releases' runs sit side by side
    # and diff against each other.  This is the default when the key is absent.
    PostReleaseOutDir = 'out\post-release'

    # The trace event list handed to QEMU's single -trace argument.  Keep it
    # small: usb_xhci_fetch_trb and the per-512-byte data events bury a run's
    # own evidence under hundreds of thousands of lines, and an isochronous
    # stream is ~1,000 TDs a second with two trace lines each - writing them is
    # host work inside the very scheduling window a stage is measuring.
    TraceEvents = @(
        'usb_xhci_slot_address'
        'usb_xhci_slot_enable'
        'usb_xhci_slot_disable'
        'usb_xhci_ep_stop'
        'usb_xhci_ep_set_dequeue'
        'usb_xhci_xfer_error'
        'usb_xhci_unimplemented'
        'usb_xhci_enforced_limit'
    )

    Targets = @(
        @{
            Id       = '2a'
            Name     = 'Windows 98 SE'
            Image    = 'win98.img'
            Format   = 'qcow2'
            Machine  = 'pc'
            Cpu      = 'pentium3'
            Memory   = 256
            Accel    = ''          # '' = QEMU's default (TCG here); 'whpx' where it works
            # The monitor port must lie outside Windows' excluded TCP ranges
            # (`netsh interface ipv4 show excludedportrange protocol=tcp`);
            # Hyper-V and WSL reserve new blocks after a reboot, and QEMU then
            # fails to bind with "Input/output error" and the harness only sees
            # a monitor that refuses connections.
            Monitor  = 55591
            # How long to give the guest to reach a loaded driver before giving
            # up.  The harness does not wait blindly for this: it polls the
            # debug console for the driver's own StartController line and
            # proceeds the moment it appears.  This is only the deadline.
            BootSeconds = 240
        }
        @{
            Id       = '2b'
            Name     = 'Windows 2000 SP4'
            Image    = 'win2k.img'
            Format   = 'qcow2'
            Machine  = 'pc'
            Cpu      = 'pentium3'
            Memory   = 256
            Accel    = ''
            Monitor  = 55592
            BootSeconds = 300
        }
        # THE SMP GUEST.  Optional - delete this block if you have no such image.
        # It is the only vehicle in this project where two CPUs execute the
        # driver simultaneously, which is why batch 11-V stage F's stability
        # matrix has an SMP leg: the controller lock and the start epoch live in
        # the DRIVER IMAGE rather than the miniport extension (Phase 4 task 7),
        # so this is the configuration that would expose a defect in either.
        #
        # `Smp` is the only key here the other targets do not have.  A target
        # WITHOUT one gets no -smp argument at all, which is what a uniprocessor
        # guest needs.
        #
        # If WHPX is unavailable, plain '' (TCG) is a legitimate fallback -
        # it is already MTTCG on the hosts this project runs on, which is the
        # rung the Phase 2d checkpoint was met on.  Do NOT substitute
        # 'tcg,thread=single': it shares one host thread, which removes the very
        # simultaneity this target exists to provide.
        @{
            Id       = '2d'
            Name     = 'Windows 2000 SP4 SMP'
            Image    = 'win2k-smp.img'
            Format   = 'qcow2'
            Machine  = 'pc'
            Cpu      = 'pentium3'
            Memory   = 512
            Smp      = 2
            Accel    = 'whpx,kernel-irqchip=off'
            Monitor  = 55593
            BootSeconds = 420
        }

        # THE FRESH TARGETS, for the post-release run (Phase 16 task 16.1;
        # docs\contributing\design\09-post-release-unattended-run.md).  Two
        # keys the other targets do not have, and they are what make a target
        # one of these:
        #
        #   CloneFrom  the Phase 10 image and the pre-driver snapshot the fresh
        #              image is cloned out of (`prepare-image.ps1 -Clone`).
        #              The source is only ever read.
        #   Like       the Phase 10 target whose per-target matrix entries
        #              (ExcludedOnTarget, ExpectByTarget, MayWedgeGuest,
        #              ExpectNoDriver) this target inherits - it is the same
        #              operating system.
        #
        # The ordinary matrix never boots one of these, and `-PostRelease`
        # boots nothing else.  The image must carry a `base-<DriverVer>-qemu`
        # stamp as its newest snapshot (`prepare-image.ps1 -Stamp`) or the run
        # refuses before booting anything.  Uniprocessor, TCG: the design's
        # section 2.4 and 2.5.
        @{
            Id       = '2a-fresh'
            Name     = 'Windows 98 SE, fresh install'
            Image    = 'fresh-2a.img'
            Format   = 'qcow2'
            Machine  = 'pc'
            Cpu      = 'pentium3'
            Memory   = 256
            Accel    = ''
            Monitor  = 55594
            BootSeconds = 240
            Like     = '2a'
            CloneFrom = @{ Image = 'win98.img'; Snapshot = 'post-nusb' }
        }
        @{
            Id       = '2b-fresh'
            Name     = 'Windows 2000 SP4, fresh install'
            Image    = 'fresh-2b.img'
            Format   = 'qcow2'
            Machine  = 'pc'
            Cpu      = 'pentium3'
            Memory   = 256
            Accel    = ''
            Monitor  = 55595
            BootSeconds = 300
            Like     = '2b'
            CloneFrom = @{ Image = 'win2k.img'; Snapshot = 'phase2b-clean' }
        }
    )
}
