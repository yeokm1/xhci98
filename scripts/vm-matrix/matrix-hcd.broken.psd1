<#
    matrix.broken.psd1 for the HCD's expectation set (roadmap-hcd.md 26-A.10):
    every row here is deliberately WRONG about a working HCD, and the harness
    is required to report a failure rather than a pass.  matrix.broken.psd1's
    header has the reasoning; run it the same way, against a guest that has
    just produced a clean report from matrix-hcd.psd1:

        powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 `
                   -Matrix scripts\vm-matrix\matrix-hcd.broken.psd1 -Target 2b `
                   -ReportName device-matrix-hcd-broken.txt

    REQUIRED OUTCOME: a nonzero exit and FAIL lines.  The row's ExpectedSpeed
    is the point of this file beside the miniport's: a High Speed keyboard
    declared Full Speed must read FAIL through the whole pipeline - parse,
    the harness's own speed lines, the read and the verdict - which is the
    claim 26-A.10 makes for a wrong speed report.
#>
@{
    Schema = 2
    Driver = 'hcd'

    Always = @(
        # TRUE, and here on purpose, as in matrix.broken.psd1.
        'advance devices addressed'

        # FALSE.  A device that enumerates certainly enables a slot.
        'zero slots enabled'
    )

    Groups = @(
        @{
            Name = 'broken'
            Description = 'Deliberately false expectations. Every row must FAIL.'
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-kbd/hs-broken'
                    Model = 'usb-kbd'
                    AddArgs = 'usb_version=2'
                    Settle = 20
                    # FALSE: usb_version=2 presents the keyboard at 480 Mb/s.
                    ExpectedSpeed = 'FS'
                    Expect = @(
                        # FALSE: no device in this vehicle opens 99 endpoints.
                        'advance endpoints opened >= 99'
                        # FALSE as an identity: one slot is not two.
                        'identity slots enabled == endpoints opened + devices addressed'
                    )
                }
            )
        }
    )
}
