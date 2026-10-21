<#
    The release matrix's standard legs (build-and-test.md, "Faster emulator
    legs"). release-matrix.ps1 starts every leg here that is not Manual, in
    parallel within the vCPU budget, and gathers one results table; a Manual
    leg is listed in that table as owed, and run-leg.ps1 -Leg <id> runs it
    with an operator at VNC.

    Id         the leg's name in the table
    Guest      a guest Id of release.config.psd1
    Mode       console: the guest runs INSTALL /console /yes ... itself;
               window: the operator runs the installer's window (SETUP.EXE),
               then the guest runs the same checks
    Exits      the exit codes each step may end with (record 23 section 7.4)
    HostSteps  AfterPhaseN = a host step for the start that follows the one
               whose last phase was N: lib\leghost.ps1 Invoke-LegHostStep
    PlugStep   a host step right after the stick is plugged in
    Checks     the guest's checks that must each be reported PASS (default:
               driver-file, list-current, stick-seen, stick-copy, list-removed)
    Manual     $true: not started by release-matrix.ps1
    Minutes    the leg's deadline (default 90)

    On 98 SE the console install stages the package and ends with exit 4
    (steps left: the controller's Add New Hardware wizard at the next start,
    record 23 section 6.4), so the start after phase 1 takes the host's wizard
    step; after the uninstall (phase 2) the controller has no driver again and
    its wizard is escaped; the first stick raises its own wizard until
    roadmap-usb4 43.42 is done.
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
            Id    = 'xp-console'
            Guest = 'xp'
            Mode  = 'console'
            Exits = @{ install = @(3); list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
        @{
            Id        = 'w98se-window'
            Guest     = 'w98se'
            Mode      = 'window'
            Manual    = $true
            Exits     = @{ list = @(0); uninstall = @(0, 3, 4); 'list-final' = @(0) }
            HostSteps = @{ AfterPhase1 = 'wizard98-hcd'; AfterPhase2 = 'esc-wizard98' }
            PlugStep  = 'wizard98-stor'
        }
        @{
            Id     = 'xp-window'
            Guest  = 'xp'
            Mode   = 'window'
            Manual = $true
            Exits  = @{ list = @(0); uninstall = @(3); 'list-final' = @(0) }
        }
    )
}
