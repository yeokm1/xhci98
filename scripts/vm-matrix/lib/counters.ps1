# Read the xhci98 miniport extension's counters out of a live guest.
#
# WHY NOT THE TRACE.  The driver publishes each counter through
# XHCI_DBG_VALUE_CHANGED, which prints only when the value changes AND only for
# the first 32 samples per site for the life of the driver load - nothing resets
# it.  A device matrix walking twenty rows in one boot exhausts that budget in
# seconds and then goes silent, so a run that read the trace would report its
# later rows as "nothing moved".  This reads the fields themselves.
#
# The base address is the extension's VA, which appears as the `a=` in every
# `cb <callback> irql=NN a=<VA>` line the qemu build writes to the debug
# console.  It is NOT stable across binds: Windows 2000 unloads and reloads the
# image on disable/enable and the common buffer moves between binds (Phase 3
# task 9), so the harness re-reads it per boot rather than caching it.

# Load the offset table and the label map that scripts\vm-matrix\gen-offsets.ps1
# produced.  Both are derived from the driver's own sources, so a counter that
# was renamed there is renamed here, and an expectation naming the old name
# fails loudly rather than matching nothing.
function Import-CounterTable {
    param(
        [string]$OffsetsFile = "",
        [string]$LabelsFile = ""
    )
    if ($OffsetsFile -eq "") { $OffsetsFile = Join-Path $PSScriptRoot "..\offsets.txt" }
    if ($LabelsFile -eq "") { $LabelsFile = [IO.Path]::ChangeExtension($OffsetsFile, ".labels.txt") }
    if (-not (Test-Path -LiteralPath $OffsetsFile)) {
        throw ("offset table not found: {0}. Run scripts\vm-matrix\gen-offsets.ps1 first." -f $OffsetsFile)
    }
    if (-not (Test-Path -LiteralPath $LabelsFile)) {
        throw ("label map not found: {0}. It is written beside the offsets by gen-offsets.ps1." -f $LabelsFile)
    }

    $offsets = @{}
    $sizeof = $null
    foreach ($line in Get-Content -LiteralPath $OffsetsFile) {
        $parts = $line.Trim() -split '\s+'
        if ($parts.Count -ne 2) { continue }
        if ($parts[0] -eq "SIZEOF") { $sizeof = [int]$parts[1]; continue }
        $offsets[$parts[0]] = [int]$parts[1]
    }
    if ($null -eq $sizeof) {
        throw ("{0} has no SIZEOF line, so its freshness cannot be checked." -f $OffsetsFile)
    }

    $fieldOf = @{}
    foreach ($line in Get-Content -LiteralPath $LabelsFile) {
        $parts = $line -split "`t", 2
        if ($parts.Count -ne 2) { continue }
        $fieldOf[$parts[1].Trim()] = $parts[0].Trim()
    }

    return [pscustomobject]@{
        Offsets     = $offsets
        FieldOfLabel = $fieldOf
        Sizeof      = $sizeof
        OffsetsFile = (Resolve-Path -LiteralPath $OffsetsFile).Path
    }
}

# Resolve a human label - the form every result box in this repository quotes -
# to the extension field it is printed from.  An unknown label is an ERROR and
# never a zero: a matrix expectation naming a counter that no longer exists must
# not quietly evaluate as "it did not move".
function Resolve-CounterLabel {
    param(
        [Parameter(Mandatory = $true)]$Table,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Table.FieldOfLabel.ContainsKey($Label)) {
        $field = $Table.FieldOfLabel[$Label]
        if (-not $Table.Offsets.ContainsKey($field)) {
            throw ("counter '{0}' resolves to field {1}, which has no offset in {2}. Regenerate the table." -f `
                $Label, $field, $Table.OffsetsFile)
        }
        return $field
    }
    # Accept a raw field name too, so a counter with no scalar print site can
    # still be named if it ever gains an offset.
    if ($Table.Offsets.ContainsKey($Label)) { return $Label }
    throw ("no counter named '{0}'. It is neither a printed label nor an extension field in {1}." -f `
        $Label, $Table.OffsetsFile)
}

# WHY THE FAILING READ ESTABLISHES ITS CAUSE BEFORE IT WRITES ITS MESSAGE.
#
# The read below maps its reply POSITIONALLY, so a reply that came back short
# must void the run - that guard is real and it fired on the 2a `audio` group on
# in Phase 10's matrix.  But `Send-Mon` catches a connection refusal and returns nothing,
# so a QEMU that is GONE arrives here as a reply containing zero words, and the
# guard said "asked for 32 words and got 0 after 4 attempts" - a sentence about a
# busy guest, for a run whose guest no longer existed.  The four
# `connection actively refused` lines were on the console immediately above it,
# and that reading was then written up as the group's result, attributing the
# group's end to a device driver that had never been bound (lessons.md, the
# Phase 10 device-matrix entry: a refused monitor port is a dead QEMU process).
#
# That is this harness's own rule - a wrong value is worse than an error -
# turned one notch inward: a right refusal with the wrong reason sends the
# reader looking in the wrong place.  So the two causes are separated before the
# message is composed, and neither is guessed: whether anything is listening on
# the monitor port is one connect (`Test-MonitorPortFree`, monitor.ps1), and
# whether the process is gone is `Get-ProcessStateText` (qemu.ps1) over the
# handle the runner has been holding all along.
function New-CounterReadFailure {
    param(
        [Parameter(Mandatory = $true)][uint32]$Addr,
        [Parameter(Mandatory = $true)][int]$Take,
        [Parameter(Mandatory = $true)][int]$Got,
        [Parameter(Mandatory = $true)][int]$Attempts,
        [Parameter(Mandatory = $true)][bool]$MonitorListening,
        [string]$ProcessState = ""
    )
    if (-not $MonitorListening) {
        return ("counter read at 0x{0:X8} got NO REPLY AT ALL after {1} attempts, because nothing is listening on the monitor port: there is no guest left to read. {2}. This is not a short reply from a busy guest and must not be read as one - whatever ended this group is upstream of the driver and of the counters." -f `
            $Addr, $Attempts, $ProcessState)
    }
    return ("counter read at 0x{0:X8} asked for {1} words and got {2} after {3} attempts, from a monitor that is still answering. A positional map of a short reply files every later counter under the wrong name, so this run is void rather than approximate." -f `
        $Addr, $Take, $Got, $Attempts)
}

# Read every counter in one pass.  Contiguous runs are coalesced into single
# monitor reads because a read per counter is a TCP round trip per counter -
# several hundred of them - and the guest is running while they happen, so the
# readings would not be of the same moment.
#
# -Process is the guest's QEMU process handle.  It is optional only so that a
# caller with no handle still works; without it a failed read can say that the
# monitor is gone but not why, which is exactly the half-answer this parameter
# was added to end.
function Read-Counters {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$BaseVa,
        [Parameter(Mandatory = $true)]$Table,
        $Process = $null
    )
    $baseAddr = [Convert]::ToUInt32(($BaseVa -replace '^0x', ''), 16)

    $sorted = $Table.Offsets.Values | Sort-Object -Unique
    $chunks = @()
    $start = $null; $prev = $null
    foreach ($o in $sorted) {
        if ($null -eq $start) { $start = $o; $prev = $o; continue }
        if ($o - $prev -le 128) { $prev = $o; continue }
        $chunks += , @($start, $prev)
        $start = $o; $prev = $o
    }
    if ($null -ne $start) { $chunks += , @($start, $prev) }

    $words = @{}
    foreach ($chunk in $chunks) {
        $from = $chunk[0]; $to = $chunk[1]
        $count = [int](($to - $from) / 4) + 1
        while ($count -gt 0) {
            $take = [Math]::Min($count, 32)
            $addr = $baseAddr + $from

            # THE WORD COUNT IS CHECKED, AND A SHORT REPLY IS RETRIED THEN
            # FATAL.  The reply lists words in address order from the requested
            # base and they are mapped POSITIONALLY - deriving each word's
            # offset from the printed line address needs unsigned arithmetic
            # PowerShell will not do, and a signed overflow there would misfile
            # every counter above 0x7FFFFFFF.  But positional mapping means a
            # reply that came back SHORT, because the monitor read timed out
            # against a busy guest mid-transfer, silently shifts every remaining
            # counter in the chunk onto the wrong field.
            #
            # That is not hypothetical: it is what the first 2b matrix run did.
            # Row 2 reported `devices addressed +3777495686` and `interrupt mask
            # failures 2172581320` - values of the form 0x8180xxxx, which are
            # kernel ADDRESSES, read out of the extension and filed under
            # counter names.  A wrong value, never an error, and every row after
            # it was junk.  So the count is now verified.
            $seq = @()
            $attempt = 0
            while ($true) {
                $attempt++
                $reply = Send-Mon -Port $Port -Command ("x/{0}wx 0x{1:X8}" -f $take, $addr) -Reply -IdleMs 1500 -HardMs 15000
                $seq = @()
                foreach ($line in (($reply -replace "\x1b\[[0-9;]*[A-Za-z]", "") -split "`r?`n")) {
                    if ($line -match '^[0-9a-fA-Fx]+:\s+(.*)$') {
                        foreach ($tok in ($Matches[1].Trim() -split '\s+')) {
                            if ($tok -match '^0x[0-9a-fA-F]+$') { $seq += [Convert]::ToUInt32($tok, 16) }
                        }
                    }
                }
                if ($seq.Count -eq $take) { break }
                if ($attempt -ge 4) {
                    # Establish the cause, then write the message.  A monitor
                    # port with nothing listening is a different finding from a
                    # guest too busy to finish a reply, and only one of them is
                    # about the guest at all.
                    $listening = -not (Test-MonitorPortFree -Port $Port)
                    throw (New-CounterReadFailure -Addr $addr -Take $take -Got $seq.Count -Attempts $attempt `
                        -MonitorListening $listening -ProcessState (Get-ProcessStateText -Process $Process))
                }
                Start-Sleep -Milliseconds 400
            }

            for ($i = 0; $i -lt $seq.Count; $i++) { $words[$from + 4 * $i] = $seq[$i] }
            $from += $take * 4
            $count -= $take
        }
    }

    $out = @{}
    $unread = 0
    foreach ($field in $Table.Offsets.Keys) {
        $off = $Table.Offsets[$field]
        if ($words.ContainsKey($off)) { $out[$field] = [int64]$words[$off] } else { $unread++ }
    }
    return [pscustomobject]@{
        Values = $out
        Unread = $unread
        Read   = $out.Count
    }
}

# Deltas across a window.  Every expectation in the matrix is over one of these
# and never over an absolute: usbport RtlZeroMemory's the miniport extension
# before every StartController (Phase 4 task 7), so an absolute reading means
# nothing across a rebind, and a counter is cumulative within one.
function Get-CounterDelta {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$After
    )
    $d = @{}
    foreach ($k in $After.Values.Keys) {
        $b = if ($Before.Values.ContainsKey($k)) { $Before.Values[$k] } else { 0 }
        $d[$k] = $After.Values[$k] - $b
    }
    # A NEGATIVE delta means the extension was zeroed between the two readings -
    # a controller restart, a disable/enable, a Windows 2000 image reload.  It is
    # not a small anomaly to be clamped: every expectation in the window is
    # void, because the window no longer describes one continuous driver load.
    $wentBackwards = @()
    foreach ($k in $d.Keys) { if ($d[$k] -lt 0) { $wentBackwards += $k } }
    return [pscustomobject]@{
        Values        = $d
        WentBackwards = $wentBackwards
        Restarted     = ($wentBackwards.Count -gt 0)
    }
}

# The extension VA and size, out of the debug console log the qemu build
# writes.  (The DEBUG build writes nothing there - since task 13-L.1 the 0xE9
# sink and XHCI_DBG_CB are both qemu-only.)
# Returned together, with everything DISTINCT that was seen, because
# the two traps below are only visible in the plural.
#
# TRAP A: `a=` IS NOT ALWAYS THE EXTENSION.  A callback line is
# `xhci98: cb <Name> irql=NN a=<VA> b=... c=...` and there `a=` is the miniport
# extension - but the driver writes its probe records in the SAME shape:
# `xhci98: cb probe.ep irql=02 a=04030101 b=00000000 c=00000000`, where `a=` is
# packed endpoint shape data.  A reader that took the last `a=` in the file
# would hand the counter reader an address like 0x04030101 and read the whole
# counter table out of arbitrary guest memory, with no error anywhere.  Anchoring on `cb` and
# `irql=` is NOT enough and a first version of this function did exactly that -
# measured against vm\2b-9v-boot1-debugcon.log, which yielded 24 distinct
# "extension addresses" of which two were real.  Two further conditions do it:
# the callback name must be a plain identifier (a probe record's name has a dot
# in it), and the value must be a KERNEL address, which the real extension
# always is on both targets and a packed shape word never is.
#
# TRAP B: A DEBUG CONSOLE LOG ACCUMULATES ACROSS BOOTS.  That same file holds
# `MiniPortExtensionSize=0000C4F4` (50,420 - the batch 8-V.2 era) and
# `MiniPortExtensionSize=000113CC` (70,604 - after task 9-A.2), so it spans two
# different binaries.  Taking the first match would check the offset table
# against a layout that stopped existing weeks ago, and taking the last without
# noticing the plural would hide that the "before" counter reading may belong to
# a different driver load than the "after" one.  The harness starts each boot
# with a fresh file, and this reports every distinct value so that a stale one
# is loud rather than silently resolved.
function Find-ExtensionIdentity {
    param([Parameter(Mandatory = $true)][string]$DebugconLog)
    if (-not (Test-Path -LiteralPath $DebugconLog)) {
        return [pscustomobject]@{ Va = $null; Size = $null; AllVas = @(); AllSizes = @(); Spans = $false }
    }
    $vas = @()
    $sizes = @()
    foreach ($line in Get-Content -LiteralPath $DebugconLog) {
        if ($line -match '\bcb\s+([A-Za-z_][A-Za-z0-9_]*)\s+irql=[0-9A-Fa-f]{2}\s+a=([0-9A-Fa-f]{8})\b') {
            $va = $Matches[2]
            # The mask is spelled in decimal: `0x80000000` is a negative Int32
            # literal in Windows PowerShell 5.1, and `[uint32]0x80000000` throws
            # on that literal rather than converting it.
            if (([Convert]::ToUInt32($va, 16) -band [uint32]2147483648) -ne 0) { $vas += $va }
        }
        if ($line -match 'MiniPortExtensionSize=([0-9A-Fa-f]{8})') { $sizes += [Convert]::ToInt32($Matches[1], 16) }
    }
    $distinctVas = @($vas | Sort-Object -Unique)
    $distinctSizes = @($sizes | Sort-Object -Unique)
    return [pscustomobject]@{
        Va       = $(if ($vas.Count) { $vas[$vas.Count - 1] } else { $null })
        Size     = $(if ($sizes.Count) { $sizes[$sizes.Count - 1] } else { $null })
        AllVas   = $distinctVas
        AllSizes = $distinctSizes
        # More than one extension size in one log means more than one BINARY
        # wrote to it; more than one VA means more than one driver LOAD did.
        # Neither is a condition to resolve by picking one - the first voids the
        # offset check and the second means a before/after pair may straddle a
        # restart, which Get-CounterDelta would then see as negative deltas.
        Spans    = (($distinctSizes.Count -gt 1) -or ($distinctVas.Count -gt 1))
    }
}

# WHAT COUNTS AS A TEARDOWN, read from the same debug console, for batch 11-V
# stage G (roadmap task 11-V.1).  The measured shape on both targets is
#
#   cb SuspendController irql=00 a=<ext>
#   cb DisableInterrupts irql=02 a=<ext>
#   cb StopController    irql=00 a=<ext>
#
# and the two trace lines stage G wants beside it are written by the QUIESCE,
# which on that shape runs inside SuspendController - ahead of the stop, not at
# it.
#
# `-From` is the log's line count at the moment the stop was committed, and it
# is not optional in a real run: an idle suspend writes the identical
# `cb SuspendController` line, so a scan of the whole file finds one on any 2a
# boot whose image predates task 11-V.6's registry value.
#
# It lives here rather than in the stage script because the stage script cannot
# be dot-sourced to test it - and a log scanner whose positive AND negative
# answers have never been watched is the kind of instrument this harness's own
# selftest exists to refuse.  Both directions are asserted in selftest.ps1.
function Find-Teardown {
    param([Parameter(Mandatory = $true)][string]$Path, [int]$From = 0)
    $result = [pscustomobject]@{
        Lines           = @()      # the callback sequence, in order, as text
        Sequence        = @()      # just the callback names, in order
        HasSuspend      = $false
        HasStop         = $false
        StopIrqls       = @()
        CommandsTrace   = @()      # 'command: abandoned outstanding TRB', nonzero only
        RhRetiredTrace  = @()      # 'root hub: port operations retired by the quiesce'
        SuspendedStop   = $false   # the "stop arrived on a suspended controller" line
        DriverEntries   = 0
        Total           = 0
    }
    if (-not (Test-Path -LiteralPath $Path)) { return $result }
    $all = @(Get-Content -LiteralPath $Path)
    $result.Total = $all.Count
    if ($From -ge $all.Count) { return $result }
    foreach ($line in @($all[$From..($all.Count - 1)])) {
        if ($line -match '\bcb\s+(SuspendController|StopController|DisableInterrupts|ResumeController|StartController)\s+irql=([0-9A-Fa-f]{2})') {
            $result.Lines += $line.Trim()
            $result.Sequence += $Matches[1]
            if ($Matches[1] -eq 'SuspendController') { $result.HasSuspend = $true }
            if ($Matches[1] -eq 'StopController') { $result.HasStop = $true; $result.StopIrqls += $Matches[2] }
        }
        if ($line -match 'DriverEntry') { $result.DriverEntries++ }
        # 'command: abandoned outstanding TRB' is an XHCI_DBG_VALUE_CHANGED site,
        # so it is BOTH the trace line stage G wants and a row of the periodic
        # counter dump - and that macro prints its first sample even when the
        # sample is zero.  The value it carries is the abandoned TRB's physical
        # address, so a zero means there was none: only a nonzero line is a
        # witness.  Its sibling, 'root hub: port operations retired by the
        # quiesce', is a plain XHCI_DBG_VALUE at the site and needs no such
        # filter - and it is worded differently from the 'RH operations retired
        # by the quiesce' counter row, which is why those two do not collide.
        if ($line -match 'command: abandoned outstanding TRB(=([0-9A-Fa-f]+))?') {
            if (-not $Matches[1] -or ($Matches[2] -replace '^0+', '') -ne "") { $result.CommandsTrace += $line.Trim() }
        }
        if ($line -match 'port operations retired by the quiesce') { $result.RhRetiredTrace += $line.Trim() }
        if ($line -match 'the stop arrived on a suspended controller') { $result.SuspendedStop = $true }
    }
    return $result
}
