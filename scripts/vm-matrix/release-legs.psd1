<#
    The release matrix's standard legs (build-and-test.md, "Faster emulator
    legs"). release-matrix.ps1 starts every leg here that is not Manual, in
    parallel within the vCPU budget, and gathers one results table; a Manual
    leg is listed in that table as owed, and run-leg.ps1 -Leg <id> runs it
    with an operator at VNC. A leg whose guest the configuration does not
    have is listed as not configured.

    Id         the leg's name in the table
    Guest      a guest Id of release.config.psd1
    Mode       console: the guest runs INSTALL /console /yes ... itself;
               window: the installer's window (SETUP.EXE), driven by the
               host with WindowKeys or, without them, by an operator; then
               the guest runs the same checks
    WindowKeys the window's page keys, in order, from the Welcome page up
               to and including Install (lib\legwin.ps1 Invoke-LegWindow):
               each is sent again until the screen changes. 'until-install'
               walks the pages left, however many the package shows (the
               Other USB drivers pages follow the download's sets), to
               Install: Alt+I on each, Alt+N where that changed nothing.
               After the run the host takes the "Steps left for you" page
               and Finish (Restart Windows now) with Enter
    ConsentKeys a window leg's keys for the consent prompt the window's
               manifest raises on Vista and 7, each sent once the screen is
               still, after SETUP.EXE and before the first page key (Vista:
               Allow, Alt+A, for a program with no publisher; 7: Yes, Alt+Y)
    Exits      the exit codes each step may end with (record 23 section 7.4)
    HostSteps  AfterPhaseN = a host step for the start that follows the one
               whose last phase was N: lib\leghost.ps1 Invoke-LegHostStep
    PlugStep   a host step right after the stick is plugged in
    PromptStep xp: whenever it is on the screen, XP's unsigned-driver
               question ("has not passed Windows Logo testing") answered
               Continue Anyway, and the Found New Hardware wizard the
               install raises for the root hub walked page by page ("No, not
               this time", install automatically, Finish)
               nt6: whenever it is on the screen, the unsigned-driver
               question of Vista and 7 ("Windows can't verify the publisher
               of this driver software") answered "Install this driver
               software anyway"
    InstallArgs more switches for a console leg's install (letters, digits,
               blanks and / : = , - _; on 98 SE and ME no = or , which
               COMMAND.COM splits on): the -nonfree download's driver pack
               copied (/wdmnonfree:copy, 98 SE and ME) or copied with the
               packages STAGE.TXT gives this Windows staged
               (/wdmnonfree:stage=<class/package>,...), roadmap-usb4 43.48
    Needs      a path, relative to the package's top, that the leg needs:
               USBDRV\NONFREE for the 43.48 legs, which only the -nonfree
               download carries. release-matrix.ps1 lists a leg whose
               package lacks it as not in the package and does not start
               it; run-leg.ps1 refuses it
    Audio      $true (2000 to 7): after list-current the guest asks for
               QEMU's usb-audio, which the host plugs into the controller
               (its output to -audiodev none: nothing plays on the host);
               WAVCHK.EXE (wavchk\, built by its build.cmd) plays a 2 s WAV
               through the waveOut device named USB once Windows has
               installed it and must play it to the end (audio-play), and
               XHCITR's report must show the counters with no UrbsMalformed
               (urbs-malformed): Windows 2000's usbaudio.sys selecting its
               streaming interface, refused by 3.4.2.0 (docs/issues/30)
    Checks     the guest's checks that must each be reported PASS (default:
               driver-file, list-current, stick-seen, stick-copy, list-removed,
               and with Audio audio-play and urbs-malformed)
    Manual     $true: not started by release-matrix.ps1 (a window leg with
               no WindowKeys is Manual)
    Minutes    the leg's deadline (default 90)
    Accel      the accelerator the leg runs on: tcg (the default) or whpx.
               Only the two Windows 10 legs say whpx (owner, 2026-11-11,
               roadmap-usb4 49.4; selftest.ps1 holds this file to it). A
               WHPX leg starts cold (QEMU restores no saved state under
               WHPX), is refused where WHPX cannot run its guest, and runs
               under TCG only with -ForceTcg, the periodic cross-check
    Cold       $true: the leg always starts cold from the starting point's
               disk, under TCG too, so a WHPX leg forced to TCG is the same
               leg on the other accelerator
    TcgMinutes, TcgStallSeconds
               the deadline, and the longest silence on COM1, of a WHPX leg
               forced to TCG (10 x64's uninstall wrote nothing for 47 min
               under TCG, runs/run-49.md "49.4"); Minutes and StallSeconds
               (default 900) are for the leg's own accelerator
    Devices    $true (Windows 10): after the stick's copy the controller is
               disabled, enabled, removed and rescanned with Windows' own
               pnputil, the stick copied to again after the enable and the
               rescan (checks ctrl-disable, ctrl-enable, stick-copy-enable,
               ctrl-remove, ctrl-rescan, stick-copy-rescan). Whether the
               remove asked for a restart is recorded and not judged: the
               same leg has given both answers. An uninstall that then
               stops for the pending restart (exit 4, uninstall-pending)
               is run again after the next start (uninstall-again)
    TrustCert  $true (Windows 10 x64): the guest adds the package's own
               nt-x64\xhci98-test.cer to Root and TrustedPublisher before
               the install (cert-trust) and must be in test-signing mode
               (test-signing). Such a leg Needs that file: an unsigned x64
               package has no catalog, and Windows 10 x64 refuses it

    On 98 SE the console install stages the package and ends with exit 4
    (steps left: the controller's Add New Hardware wizard at the next start,
    record 23 section 6.4), so the start after phase 1 takes the host's wizard
    step; after the uninstall (phase 2) the controller has no driver again and
    its wizard is escaped; the first stick raises its own wizard until
    roadmap-usb4 43.42 is done.

    On ME the controller found at the start after an install is installed
    by Windows itself, which then asks to restart before the desktop
    (restart-me answers Yes).
#>
@{
    Legs = @(
        @{
            Id        = 'w98se-console'
            Guest     = 'w98se'
            Mode      = 'console'
            Exits     = @{ install = @(3, 4); list = @(0); uninstall = @(0, 3, 4); 'list-final' = @(0) }
            HostSteps = @{ AfterPhase1 = 'wizard98-hcd'; AfterPhase2 = 'esc-wizard98' }
            PlugStep  = 'wizard98-stor'
        }
        @{
            Id         = 'xp-console'
            Guest      = 'xp'
            Mode       = 'console'
            PromptStep = 'xp'
            Exits = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id         = 'w98se-window'
            Guest      = 'w98se'
            Mode       = 'window'
            WindowKeys = @('alt-n', 'until-install')
            Exits      = @{ install = @(3, 4); list = @(0); uninstall = @(0, 3, 4); 'list-final' = @(0) }
            HostSteps = @{ AfterPhase1 = 'wizard98-hcd'; AfterPhase2 = 'esc-wizard98' }
            PlugStep  = 'wizard98-stor'
        }
        @{
            Id         = 'xp-window'
            Guest      = 'xp'
            Mode       = 'window'
            WindowKeys = @('alt-n', 'until-install')
            PromptStep = 'xp'
            Exits      = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id        = 'me-console'
            Guest     = 'me'
            Mode      = 'console'
            Exits     = @{ install = @(3, 4); list = @(0); uninstall = @(3); 'list-final' = @(0) }
            HostSteps = @{ AfterPhase1 = 'restart-me'; AfterPhase2 = 'esc-wizard98' }
        }
        @{
            Id         = 'me-window'
            Guest      = 'me'
            Mode       = 'window'
            WindowKeys = @('alt-n', 'until-install')
            Exits      = @{ install = @(3, 4); list = @(0); uninstall = @(3); 'list-final' = @(0) }
            HostSteps  = @{ AfterPhase1 = 'restart-me'; AfterPhase2 = 'esc-wizard98' }
        }
        @{
            Id         = 'w2k-console'
            Guest      = 'w2k'
            Mode       = 'console'
            PromptStep = 'xp'
            Audio      = $true
            Exits      = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id         = 'w2k-window'
            Guest      = 'w2k'
            Mode       = 'window'
            WindowKeys = @('alt-n', 'until-install')
            PromptStep = 'xp'
            Exits      = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id    = 'vista-console'
            Guest = 'vista'
            Mode  = 'console'
            PromptStep = 'nt6'
            Exits = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id          = 'vista-window'
            Guest       = 'vista'
            Mode        = 'window'
            PromptStep = 'nt6'
            ConsentKeys = @('alt-a')
            WindowKeys  = @('alt-n', 'until-install')
            Exits       = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id    = 'w7-console'
            Guest = 'w7'
            Mode  = 'console'
            PromptStep = 'nt6'
            Exits = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id          = 'w7-window'
            Guest       = 'w7'
            Mode        = 'window'
            PromptStep = 'nt6'
            ConsentKeys = @('alt-y')
            WindowKeys  = @('alt-n', 'until-install')
            Exits       = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id          = 'w10x86-console'
            Guest       = 'w10x86'
            Mode        = 'console'
            Accel       = 'whpx'
            Cold        = $true
            InstallArgs = '/usb'
            PromptStep  = 'nt6'
            Devices     = $true
            Minutes     = 60
            TcgMinutes  = 240; TcgStallSeconds = 3600
            Exits       = @{ install = @(3); list = @(0); uninstall = @(3, 4); 'uninstall-again' = @(3); 'list-final' = @(0) }
        }
        @{
            Id          = 'w10x64-console'
            Needs       = 'nt-x64\xhci98-test.cer'
            Guest       = 'w10x64'
            Mode        = 'console'
            Accel       = 'whpx'
            Cold        = $true
            InstallArgs = '/usb'
            TrustCert   = $true
            Devices     = $true
            Minutes     = 60
            TcgMinutes  = 240; TcgStallSeconds = 3600
            Exits       = @{ install = @(3); list = @(0); uninstall = @(3, 4); 'uninstall-again' = @(3); 'list-final' = @(0) }
        }
        @{
            Id          = 'me-nonfree'
            Needs       = 'USBDRV\NONFREE'
            Guest       = 'me'
            Mode        = 'console'
            InstallArgs = '/wdmnonfree:copy'
            Exits       = @{ install = @(3, 4); list = @(0); uninstall = @(3); 'list-final' = @(0) }
            HostSteps   = @{ AfterPhase1 = 'restart-me'; AfterPhase2 = 'esc-wizard98' }
        }
        @{
            Id          = 'w2k-nonfree'
            Needs       = 'USBDRV\NONFREE'
            Guest       = 'w2k'
            Mode        = 'console'
            InstallArgs = '/wdmnonfree:stage=ethernet/ax88772,hid/x360c,hid/x360wc,serial/ch341,serial/ftdi'
            PromptStep  = 'xp'
            Exits       = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id          = 'xp-nonfree'
            Needs       = 'USBDRV\NONFREE'
            Guest       = 'xp'
            Mode        = 'console'
            InstallArgs = '/wdmnonfree:stage=ethernet/ax88179,ethernet/ax88772,ethernet/rtl815x,hid/x360c,hid/x360wc,serial/ch341'
            PromptStep  = 'xp'
            Exits       = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id          = 'w7-nonfree'
            Needs       = 'USBDRV\NONFREE'
            Guest       = 'w7'
            Mode        = 'console'
            InstallArgs = '/wdmnonfree:stage=ethernet/ax88179,ethernet/rtl815x,hid/x360c,hid/x360wc'
            PromptStep  = 'nt6'
            Exits       = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
    )
}
