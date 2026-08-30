# The verdict: parse a matrix row's expectations and decide its outcome.
#
# The design is docs\contributing\design\06-device-matrix-verdict.md and this file is
# meant to be read beside it.  Nothing here decides anything the matrix file did
# not state in advance - that is the whole point.  A run that ends "no bugcheck"
# proves very little, and this repository has repeatedly measured counters that
# read zero BY CONSTRUCTION and had to retract the reading.

# ------------------------------------------------------------------ parsing ---
#
# Four forms.  They are parsed into records rather than evaluated as strings so
# that a malformed expectation is an ERROR at load time - before a boot is spent
# on it - rather than an expectation that silently never fires.
#
#   advance <label>
#   advance <label> >= <n>
#   zero <label>
#   inert <label> because <reason>
#   identity <label> [+ <label>]... == <label> [+ <label>]...
#
function ConvertTo-Expectation {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)]$Table
    )
    $t = $Text.Trim()

    if ($t -match '^advance\s+(.+?)\s*>=\s*(\d+)$') {
        $label = $Matches[1].Trim()
        return [pscustomobject]@{
            Kind = "advance"; Label = $label; Field = (Resolve-CounterLabel -Table $Table -Label $label)
            Min = [int]$Matches[2]; Text = $t
        }
    }
    if ($t -match '^advance\s+(.+)$') {
        $label = $Matches[1].Trim()
        return [pscustomobject]@{
            Kind = "advance"; Label = $label; Field = (Resolve-CounterLabel -Table $Table -Label $label)
            Min = 1; Text = $t
        }
    }
    if ($t -match '^zero\s+(.+)$') {
        $label = $Matches[1].Trim()
        return [pscustomobject]@{
            Kind = "zero"; Label = $label; Field = (Resolve-CounterLabel -Table $Table -Label $label)
            Text = $t
        }
    }
    if ($t -match '^inert\s+(.+?)\s+because\s+(.+)$') {
        $label = $Matches[1].Trim()
        return [pscustomobject]@{
            Kind = "inert"; Label = $label; Field = (Resolve-CounterLabel -Table $Table -Label $label)
            Reason = $Matches[2].Trim(); Text = $t
        }
    }
    # `inert` WITHOUT a reason is refused rather than defaulted.  An inert
    # expectation is the harness declining to test something, and design doc 06
    # requires the reason to be printed - an unexplained one is indistinguishable
    # from a `zero` someone got lazy about, which is how a vacuous check gets
    # counted as coverage.
    if ($t -match '^inert\s') {
        throw ("inert expectation with no 'because' clause: '{0}'. Every inert row must say why the path does not exist here." -f $t)
    }
    if ($t -match '^identity\s+(.+?)\s*==\s*(.+)$') {
        $lhs = @($Matches[1].Trim() -split '\s*\+\s*' | ForEach-Object { $_.Trim() })
        $rhs = @($Matches[2].Trim() -split '\s*\+\s*' | ForEach-Object { $_.Trim() })
        $lf = @($lhs | ForEach-Object { Resolve-CounterLabel -Table $Table -Label $_ })
        $rf = @($rhs | ForEach-Object { Resolve-CounterLabel -Table $Table -Label $_ })
        return [pscustomobject]@{
            Kind = "identity"; LeftLabels = $lhs; RightLabels = $rhs
            LeftFields = $lf; RightFields = $rf; Text = $t
        }
    }

    throw ("cannot parse expectation '{0}'. Expected one of: advance <label> [>= n] | zero <label> | inert <label> because <reason> | identity <sum> == <sum>" -f $t)
}

# --------------------------------------------------------------- evaluating ---
function Test-Expectation {
    param(
        [Parameter(Mandatory = $true)]$Expectation,
        [Parameter(Mandatory = $true)]$Delta
    )
    $d = $Delta.Values
    switch ($Expectation.Kind) {
        "advance" {
            $v = if ($d.ContainsKey($Expectation.Field)) { $d[$Expectation.Field] } else { $null }
            if ($null -eq $v) { return [pscustomobject]@{ Held = $false; Reading = "<unread>"; Unread = $true } }
            return [pscustomobject]@{
                Held = ($v -ge $Expectation.Min)
                Reading = ("{0}{1}" -f $(if ($v -ge 0) { "+" } else { "" }), $v)
                Unread = $false
            }
        }
        "zero" {
            $v = if ($d.ContainsKey($Expectation.Field)) { $d[$Expectation.Field] } else { $null }
            if ($null -eq $v) { return [pscustomobject]@{ Held = $false; Reading = "<unread>"; Unread = $true } }
            return [pscustomobject]@{ Held = ($v -eq 0); Reading = ("{0}" -f $v); Unread = $false }
        }
        "inert" {
            $v = if ($d.ContainsKey($Expectation.Field)) { $d[$Expectation.Field] } else { $null }
            if ($null -eq $v) { return [pscustomobject]@{ Held = $false; Reading = "<unread>"; Unread = $true } }
            # An inert counter that MOVED is not a failure of the driver; it is a
            # failure of this document's claim about the vehicle, and it is
            # reported as loudly as one because the claim is what licensed not
            # testing the path.
            return [pscustomobject]@{
                Held = ($v -eq 0)
                Reading = ("{0}  ({1})" -f $v, $Expectation.Reason)
                Unread = $false
            }
        }
        "identity" {
            $unread = $false
            $l = 0; $r = 0
            foreach ($f in $Expectation.LeftFields) {
                if ($d.ContainsKey($f)) { $l += $d[$f] } else { $unread = $true }
            }
            foreach ($f in $Expectation.RightFields) {
                if ($d.ContainsKey($f)) { $r += $d[$f] } else { $unread = $true }
            }
            if ($unread) { return [pscustomobject]@{ Held = $false; Reading = "<unread>"; Unread = $true } }
            return [pscustomobject]@{ Held = ($l -eq $r); Reading = ("{0} == {1}" -f $l, $r); Unread = $false }
        }
    }
    throw ("unknown expectation kind '{0}'" -f $Expectation.Kind)
}

# ------------------------------------------------------------- declarations ---
#
# `MayWedgeGuest = @('2a')` says a row is EXPECTED to be able to take the guest
# down on the named targets.  It was written into the matrix with the audio row
# and then read by nothing at all for the life of the harness - so when that
# group did end early, the report could not say whether the matrix had predicted
# it or whether an ordinary row had just killed a guest, which are opposite
# findings.  A declaration nothing reads is not a declaration.
#
# It deliberately changes no VERDICT.  Design doc 06's five outcomes stand and a
# group that ended early is still an ERROR: a row being allowed to wedge a guest
# is not a licence to report the wedge as a result.  All it does is tell the
# reader which of the two findings they are looking at.
function Test-RowMayWedge {
    param($Row, [Parameter(Mandatory = $true)][string]$TargetId)
    if ($null -eq $Row) { return $false }
    if (-not $Row.ContainsKey('MayWedgeGuest')) { return $false }
    return ([string[]]$Row.MayWedgeGuest -contains $TargetId)
}

# A typo in that field would silently mean "no target", which is the same
# failure the field already had.  Checked before a boot is spent, like every
# other thing the matrix can get wrong.
function Get-RowWedgeProblems {
    param($Row, [string[]]$TargetIds)
    $out = @()
    if ($null -eq $Row -or -not $Row.ContainsKey('MayWedgeGuest')) { return $out }
    foreach ($t in ([string[]]$Row.MayWedgeGuest)) {
        if ($TargetIds -notcontains $t) {
            $out += ("row {0}: MayWedgeGuest names '{1}', which is not a target in this configuration, so it declares nothing" -f $Row.Name, $t)
        }
    }
    return $out
}

# ------------------------------------------------------------------ outcome ---
#
# One of PASS / FAIL / NODRIVER / INERT / ERROR, per design doc 06 section 2.
# The order of the tests is the design: ERROR outranks everything because it
# means the reading was not taken; NODRIVER is checked before FAIL because a
# device the OS never claimed is a RESULT and must not be reported as a defect
# in this driver.
function Get-RowOutcome {
    param(
        [Parameter(Mandatory = $true)]$Results,       # array of {Expectation, Test}
        [Parameter(Mandatory = $true)]$Delta,
        [string]$HarnessError = "",
        [string]$AddressedLabel = "devices addressed",
        [string]$ClaimedLabel = "endpoints opened",
        [Parameter(Mandatory = $true)]$Table
    )
    if ($HarnessError -ne "") {
        return [pscustomobject]@{ Outcome = "ERROR"; Why = $HarnessError }
    }
    if ($Delta.Restarted) {
        return [pscustomobject]@{
            Outcome = "ERROR"
            Why = ("a counter went backwards across the window ({0}), so the driver restarted inside it and no expectation in this row describes one continuous load" -f `
                   (($Delta.WentBackwards | Select-Object -First 3) -join ", "))
        }
    }
    # THE @() IS LOAD-BEARING.  `(pipeline).Count` is $null when the pipeline
    # yields exactly ONE object, so `... .Count -gt 0` is $null -gt 0, which is
    # false - and this guard never fired for the single-unread-counter case that
    # is by far the likeliest one.  The self-test caught it: an unread counter
    # was being reported as FAIL, i.e. "the counter did not move", which is
    # exactly the reading a broken read must never produce.  The two guards
    # below happened to be written with @() already; this one was not.
    $unread = @($Results | Where-Object { $_.Test.Unread })
    if ($unread.Count -gt 0) {
        return [pscustomobject]@{
            Outcome = "ERROR"
            Why = ("{0} counter(s) could not be read out of the guest, starting with '{1}'" -f `
                   $unread.Count, $unread[0].Expectation.Text)
        }
    }

    # INERT before anything else that could call it a pass: a row with nothing
    # but inert expectations contains nothing a broken driver would have failed.
    $substantive = @($Results | Where-Object { $_.Expectation.Kind -ne "inert" })
    if ($substantive.Count -eq 0) {
        return [pscustomobject]@{ Outcome = "INERT"; Why = "every expectation in this row is inert on this vehicle" }
    }

    $failed = @($Results | Where-Object { -not $_.Test.Held })
    if ($failed.Count -eq 0) {
        return [pscustomobject]@{ Outcome = "PASS"; Why = "" }
    }

    # NODRIVER: this driver enumerated the device and nothing above usbport
    # opened a non-default endpoint.  Both halves are required - "no endpoints
    # opened" on a device that was never addressed is our failure, not the OS's
    # disinterest.
    $addrField = Resolve-CounterLabel -Table $Table -Label $AddressedLabel
    $claimField = Resolve-CounterLabel -Table $Table -Label $ClaimedLabel
    $addressed = $(if ($Delta.Values.ContainsKey($addrField)) { $Delta.Values[$addrField] } else { 0 })
    $claimed = $(if ($Delta.Values.ContainsKey($claimField)) { $Delta.Values[$claimField] } else { 0 })
    if ($addressed -gt 0 -and $claimed -eq 0) {
        # ...and only if every failure is explained by the missing bind.
        #
        # Two ways a failure is NOT explained by it, and the first was a real
        # defect the self-test found:
        #
        #   1. A failed expectation on a counter THIS DRIVER owns.  `advance
        #      slots enabled` not holding is our enumeration going wrong, and
        #      the first version of this test - "every failure is an `advance`"
        #      - reported it as NODRIVER, i.e. as the OS's disinterest.  A row
        #      that did not enable a slot has a defect in it whatever the OS
        #      then did or did not do.
        #   2. A tripped failure-shaped `zero`, or a broken identity.  Those are
        #      defects too, and calling the row NODRIVER would bury them.
        $ourFields = @()
        foreach ($lbl in @($AddressedLabel, 'slots enabled')) {
            try { $ourFields += (Resolve-CounterLabel -Table $Table -Label $lbl) } catch { }
        }
        $unexplained = @($failed | Where-Object {
            $_.Expectation.Kind -ne "advance" -or
            ($null -ne $_.Expectation.Field -and $ourFields -contains $_.Expectation.Field)
        })
        if ($unexplained.Count -eq 0) {
            return [pscustomobject]@{
                Outcome = "NODRIVER"
                Why = ("addressed (+{0}) but no function driver opened a non-default endpoint" -f $addressed)
            }
        }
    }

    return [pscustomobject]@{
        Outcome = "FAIL"
        Why = (($failed | ForEach-Object { $_.Expectation.Text }) -join "; ")
    }
}
