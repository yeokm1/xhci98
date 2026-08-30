<#
    Task 10.4's INVERTED matrix: every row here is deliberately WRONG, and the
    harness is required to report a failure rather than a pass.

    "A harness that has never disagreed with a hand-run is untested, not
    correct."  This is the cheapest possible disagreement: a matrix that
    asserts things which are false about a working driver.  Run it against a
    guest that is known good - one that has just produced a clean report from
    matrix.psd1 - so that the ONLY reason for a failure is the expectation.

        powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 `
                   -Matrix scripts\vm-matrix\matrix.broken.psd1 -Target 2b `
                   -ReportName device-matrix-broken.txt

    REQUIRED OUTCOME: a nonzero exit and FAIL lines.  A PASS here means the
    harness cannot detect anything and every green report it has ever produced
    is worthless.

    The complementary, boot-free test of the same property is selftest.ps1,
    which drives the evaluator with synthetic deltas.  Both exist because they
    fail differently: selftest.ps1 cannot catch a runner that never applies the
    expectations it parsed, and this file cannot cover the outcome space.
#>
@{
    Schema = 1

    Always = @(
        # TRUE, and here on purpose: if the harness is broken in a way that
        # fails everything, this row still passing is what says so.
        'advance devices addressed'

        # FALSE.  A HID device that enumerates certainly enables a slot, so an
        # assertion that it does not must be reported as a failure.
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
                    Expect = @(
                        # FALSE: no device in this vehicle opens 99 endpoints.
                        'advance endpoints opened >= 99'
                        # FALSE: a High Speed device on a root port this driver
                        # reports as High Speed produces no mismatch, which the
                        # real matrix asserts as `zero`.
                        'advance endpoint speed mismatches'
                        # FALSE as an identity: these two are unrelated.
                        'identity devices addressed == endpoint opens seen'
                    )
                }
            )
        }
    )
}
