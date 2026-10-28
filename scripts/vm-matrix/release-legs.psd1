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
    Checks     the guest's checks that must each be reported PASS (default:
               driver-file, list-current, stick-seen, stick-copy, list-removed)
    Manual     $true: not started by release-matrix.ps1 (a window leg with
               no WindowKeys is Manual)
    Minutes    the leg's deadline (default 90)

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
