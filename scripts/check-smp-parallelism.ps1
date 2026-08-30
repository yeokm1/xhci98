<#
.SYNOPSIS
Host-side check that the running Phase 2d SMP VM meets the prerequisites for
parallel vCPU execution.

.DESCRIPTION
The Phase 2d VM exists to expose cross-CPU ISR/DPC races, and every check that
"the MP kernel landed" is guest-side: Device Manager's Computer node, two Task
Manager CPU graphs, ntoskrnl.exe's original filename. Because the virtual CPU
topology is unchanged, those checks are expected - but were not observed - to
remain satisfied when both vCPUs share one host thread or the process is
confined to one logical processor. Such a VM lacks simultaneous execution and
has weaker race coverage, although round-robin interleaving can still expose
some defects. This script asks the two independent host-side questions the docs
require at every 2d checkpoint:

  1. Did QEMU create SEPARATE HOST THREADS for the vCPUs?
     `info cpus` must report every configured CPU and a distinct thread_id per CPU.
     A single shared id means round-robin single-thread TCG: interleaved, so
     some interleaving-dependent races can still fire, but never simultaneous.

  2. Is the QEMU process ALLOWED MORE THAN ONE logical processor?
     The machine-wide processor count does not answer this - a Windows process
     inherits an affinity mask and can be confined to one CPU while the system
     reports many. At least two bits must be set in the process affinity mask.

Both are PREREQUISITES for parallel execution, not proof of it: whether the host
actually schedules the threads concurrently depends on load, and whether the
guest's races are exercised depends on the workload. Only contended driver load
demonstrates that. This script checks the configured vCPU-thread mapping and
process-level affinity prerequisites; it does not claim that overlap occurred.

Exit code 0 = both prerequisites hold. 1 = at least one fails, or the VM could
not be identified.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\check-smp-parallelism.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\check-smp-parallelism.ps1 -SelfTest
#>

[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$MonitorPort = 55557,
    [ValidateRange(2, 64)]
    [int]$MinimumProcessors = 2,
    [switch]$SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

# Set-bit counting and monitor parsing can both fail silently, so each lives in
# a function that -SelfTest exercises without a VM. A full 64-bit mask arrives
# as a negative Int64; its two's-complement shifts still carry the true bits.
function Get-SetBitCount {
    param([long]$Mask)
    $n = 0
    for ($i = 0; $i -lt 64; $i++) {
        if (($Mask -shr $i) -band 1) { $n++ }
    }
    return $n
}

function Test-VcpuThreadMapping {
    param(
        [string]$Text,
        [int]$ExpectedCpuCount
    )

    $cpuToThread = @{}
    $conflicts = @()
    # Not $matches: that is a PowerShell automatic variable populated by -match,
    # and shadowing it here would surprise anyone who later adds one nearby.
    $cpuLines = [regex]::Matches(
        $Text,
        "(?m)^[ `t]*\*?[ `t]*CPU #(\d+):[^\r\n]*\bthread_id=(\d+)"
    )
    foreach ($match in $cpuLines) {
        $cpu = [int]$match.Groups[1].Value
        $thread = $match.Groups[2].Value
        if ($cpuToThread.ContainsKey($cpu) -and
            $cpuToThread[$cpu] -ne $thread) {
            $conflicts += "CPU #$cpu reported thread_id $($cpuToThread[$cpu]) and $thread"
        } else {
            $cpuToThread[$cpu] = $thread
        }
    }

    $missing = @()
    for ($cpu = 0; $cpu -lt $ExpectedCpuCount; $cpu++) {
        if (-not $cpuToThread.ContainsKey($cpu)) { $missing += $cpu }
    }
    $extra = @($cpuToThread.Keys | Where-Object {
        [int]$_ -lt 0 -or [int]$_ -ge $ExpectedCpuCount
    })
    $threadIds = @($cpuToThread.Values | Select-Object -Unique)
    $complete = (
        $conflicts.Count -eq 0 -and
        $missing.Count -eq 0 -and
        $extra.Count -eq 0 -and
        $cpuToThread.Count -eq $ExpectedCpuCount
    )

    # Name which CPUs collided on which thread. "One or more vCPUs share a host
    # thread" is true but leaves the reader to go and re-run `info cpus` by
    # hand; the mapping that proved it is already in hand here.
    $shared = @()
    foreach ($id in $threadIds) {
        $cpus = @($cpuToThread.Keys |
            Where-Object { $cpuToThread[$_] -eq $id } | Sort-Object)
        if ($cpus.Count -gt 1) {
            $shared += ("thread_id {0} shared by CPU #{1}" -f
                $id, ($cpus -join ", #"))
        }
    }
    $mapping = (@($cpuToThread.Keys | Sort-Object | ForEach-Object {
        "CPU #${_}=$($cpuToThread[$_])"
    }) -join ", ")

    $problems = @()
    if ($conflicts.Count -gt 0) { $problems += $conflicts }
    if ($missing.Count -gt 0) {
        $problems += "missing CPU entries: $($missing -join ', ')"
    }
    if ($extra.Count -gt 0) {
        $problems += "unexpected CPU entries: $($extra -join ', ')"
    }
    if ($complete -and
        $threadIds.Count -ne $ExpectedCpuCount) {
        $problems += ("vCPUs share a host thread: {0}" -f ($shared -join "; "))
    }

    $result = New-Object PSObject -Property @{
        Passed = ($problems.Count -eq 0)
        Problems = $problems
        CpuCount = $cpuToThread.Count
        ThreadIds = $threadIds
        Mapping = $mapping
        Complete = $complete
        ShowSingleThreadHint = (
            $complete -and
            $threadIds.Count -eq 1 -and
            $cpuToThread.Count -gt 1
        )
    }
    return $result
}

function Test-HmpReplyEnded {
    param([string]$Text)

    # Readline prints a newline before executing the command, then QEMU shows
    # and flushes the next prompt after the command finishes. Requiring the
    # prompt at the start of the final line avoids mistaking command echo for
    # the response boundary.
    return [regex]::IsMatch($Text, "(?:^|\n)\(qemu\)[ `t]*$")
}

function Read-HmpPromptTerminatedText {
    param(
        [System.IO.Stream]$Stream = $null,
        [scriptblock]$ReadChunk = $null,
        [int]$HardTimeoutMs,
        [int]$IdleTimeoutMs,
        [string]$Description
    )

    if ($null -eq $Stream -and $null -eq $ReadChunk) {
        throw "Read-HmpPromptTerminatedText requires a stream or chunk reader."
    }

    $enc = [System.Text.Encoding]::ASCII
    $buf = New-Object byte[] 65536
    $sb = New-Object System.Text.StringBuilder
    $hardDeadline = (Get-Date).AddMilliseconds($HardTimeoutMs)

    while ($true) {
        $remainingMs = [int][Math]::Ceiling(
            ($hardDeadline - (Get-Date)).TotalMilliseconds
        )
        if ($remainingMs -le 0) {
            throw "$Description did not reach the final HMP prompt within the $HardTimeoutMs ms hard limit."
        }

        # NetworkStream's timeout applies to each Read, so every received chunk
        # earns a fresh idle window while the hard deadline still bounds a peer
        # that trickles forever. The injected reader keeps this loop testable
        # without opening a monitor socket.
        if ($null -ne $ReadChunk) {
            $piece = & $ReadChunk
            if ($null -eq $piece) {
                throw "$Description ended before the final HMP prompt."
            }
        } else {
            $Stream.ReadTimeout = [Math]::Min($IdleTimeoutMs, $remainingMs)
            try {
                $n = $Stream.Read($buf, 0, $buf.Length)
            } catch [System.IO.IOException] {
                throw "$Description did not reach the final HMP prompt within $IdleTimeoutMs ms of the last byte: $($_.Exception.Message)"
            }
            if ($n -eq 0) {
                throw "$Description connection closed before the final HMP prompt."
            }
            $piece = $enc.GetString($buf, 0, $n)
        }

        [void]$sb.Append($piece)
        if (Test-HmpReplyEnded -Text $sb.ToString()) {
            return $sb.ToString()
        }
    }
}

function Get-MonitorText {
    param(
        [int]$Port,
        [string]$Command,
        [int]$HardTimeoutMs = 10000,
        [int]$IdleTimeoutMs = 3000
    )

    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect("127.0.0.1", $Port)
    try {
        $stream = $client.GetStream()
        $enc = [System.Text.Encoding]::ASCII

        # Synchronize on the initial prompt before sending the command; a fixed
        # banner sleep can otherwise leave that prompt to masquerade as the end
        # of the command response on a loaded host.
        $null = Read-HmpPromptTerminatedText -Stream $stream `
            -HardTimeoutMs $HardTimeoutMs -IdleTimeoutMs $IdleTimeoutMs `
            -Description "Monitor banner"

        $bytes = $enc.GetBytes($Command + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        return Read-HmpPromptTerminatedText -Stream $stream `
            -HardTimeoutMs $HardTimeoutMs -IdleTimeoutMs $IdleTimeoutMs `
            -Description "Monitor reply"
    } finally {
        $client.Close()
    }
}

function Get-VcpuThreadMappingWithRetry {
    param(
        [int]$ExpectedCpuCount,
        [scriptblock]$ReadText,
        [int]$MaxAttempts = 2,
        [int]$RetryDelayMs = 500,
        [string]$Description = "monitor",
        [switch]$Quiet
    )

    $threadCheck = $null
    $attemptsUsed = 0
    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        $attemptsUsed = $attempt
        try {
            $text = & $ReadText $attempt
            $threadCheck = Test-VcpuThreadMapping -Text $text `
                -ExpectedCpuCount $ExpectedCpuCount
        } catch {
            if (-not $Quiet) {
                Write-Warn ("Could not read the {0}: {1}" -f
                    $Description, $_.Exception.Message)
            }
            $threadCheck = $null
        }

        if ($null -ne $threadCheck -and $threadCheck.Complete) { break }
        if ($attempt -lt $MaxAttempts) {
            if (-not $Quiet) {
                Write-Warn "Monitor response was incomplete or unreadable; re-reading once."
            }
            if ($RetryDelayMs -gt 0) {
                Start-Sleep -Milliseconds $RetryDelayMs
            }
        }
    }

    return New-Object PSObject -Property @{
        Check = $threadCheck
        Attempts = $attemptsUsed
    }
}

if ($SelfTest) {
    Write-Step "Self-test: set-bit counting"
    $cases = @(
        @{ Mask = [long]0x1;                Expect = 1 },   # confined to one CPU
        @{ Mask = [long]0x3;                Expect = 2 },
        @{ Mask = [long]0xFF;               Expect = 8 },   # the Phase 2d record
        @{ Mask = [long]0x101;              Expect = 2 },   # non-contiguous
        @{ Mask = [long]0;                  Expect = 0 },
        @{ Mask = [long]-1;                 Expect = 64 }   # all 64 bits set
    )
    $failures = 0
    foreach ($c in $cases) {
        $got = Get-SetBitCount -Mask $c.Mask
        if ($got -ne $c.Expect) {
            Write-Err ("mask 0x{0:X}: expected {1} set bits, got {2}" -f $c.Mask, $c.Expect, $got)
            $failures++
        }
    }

    Write-Step "Self-test: complete vCPU-to-thread mappings"
    $mappingCases = @(
        @{
            Name = "two distinct"
            Text = "* CPU #0: thread_id=100`r`n  CPU #1: thread_id=200`r`n"
            CpuCount = 2
            Expect = $true
            ExpectComplete = $true
        },
        @{
            Name = "shared thread"
            Text = "* CPU #0: thread_id=100`r`n  CPU #1: thread_id=100`r`n"
            CpuCount = 2
            Expect = $false
            # The failure must say which id and which CPUs, not just that
            # something is wrong - a generic message sends the reader back to
            # the monitor to rediscover what the check already knew.
            ExpectProblemMatch = "thread_id 100 shared by CPU #0, #1"
            ExpectSingleThreadHint = $true
            ExpectComplete = $true
        },
        @{
            Name = "partial shared thread"
            Text = "* CPU #0: thread_id=100`r`n  CPU #1: thread_id=100`r`n"
            CpuCount = 4
            Expect = $false
            # Do not diagnose the accelerator from an incomplete monitor reply:
            # the missing vCPUs may have distinct host threads.
            ExpectSingleThreadHint = $false
            # Complete=false is what makes the caller re-read the monitor
            # instead of failing a healthy VM on a truncated reply.
            ExpectComplete = $false
        },
        @{
            Name = "duplicate terminal output"
            Text = "* CPU #0: thread_id=100`r`n  CPU #1: thread_id=200`r`n* CPU #0: thread_id=100`r`n  CPU #1: thread_id=200`r`n"
            CpuCount = 2
            Expect = $true
        },
        @{
            Name = "partial response"
            Text = "* CPU #0: thread_id=100`r`n  CPU #1: thread_id=200`r`n"
            CpuCount = 3
            Expect = $false
            ExpectComplete = $false
        },
        @{
            Name = "missing expected index"
            Text = "* CPU #0: thread_id=100`r`n  CPU #2: thread_id=200`r`n"
            CpuCount = 2
            Expect = $false
        },
        @{
            Name = "conflicting duplicate"
            Text = "* CPU #0: thread_id=100`r`n  CPU #0: thread_id=200`r`n  CPU #1: thread_id=300`r`n"
            CpuCount = 2
            Expect = $false
        }
    )
    $diagnosticChecks = 0
    foreach ($c in $mappingCases) {
        $got = Test-VcpuThreadMapping -Text $c.Text -ExpectedCpuCount $c.CpuCount
        if ($got.Passed -ne $c.Expect) {
            Write-Err ("{0}: expected Passed={1}, got {2}" -f
                $c.Name, $c.Expect, $got.Passed)
            $failures++
        }
        if ($c.ContainsKey("ExpectProblemMatch")) {
            $diagnosticChecks++
            $joined = ($got.Problems -join "; ")
            if (-not $joined.Contains($c.ExpectProblemMatch)) {
                Write-Err ("{0}: expected the failure to name '{1}'; got '{2}'" -f
                    $c.Name, $c.ExpectProblemMatch, $joined)
                $failures++
            }
        }
        if ($c.ContainsKey("ExpectSingleThreadHint")) {
            $diagnosticChecks++
            if ($got.ShowSingleThreadHint -ne $c.ExpectSingleThreadHint) {
                Write-Err ("{0}: expected ShowSingleThreadHint={1}, got {2}" -f
                    $c.Name, $c.ExpectSingleThreadHint, $got.ShowSingleThreadHint)
                $failures++
            }
        }
        # Complete drives the caller's re-read, so a wrong value here is the
        # difference between a healthy VM being re-read and being failed.
        if ($c.ContainsKey("ExpectComplete")) {
            $diagnosticChecks++
            if ($got.Complete -ne $c.ExpectComplete) {
                Write-Err ("{0}: expected Complete={1}, got {2}" -f
                    $c.Name, $c.ExpectComplete, $got.Complete)
                $failures++
            }
        }
    }

    Write-Step "Self-test: prompt-framed monitor transport"
    $transportChecks = 0
    $completePrefix = "* CPU #0: thread_id=100`r`n  CPU #1: thread_id=200`r`n"
    $promptCases = @(
        @{
            Name = "complete mapping without final prompt"
            Text = $completePrefix
            Expect = $false
        },
        @{
            Name = "final line-start prompt"
            Text = $completePrefix + "`r`n(qemu) "
            Expect = $true
        },
        @{
            Name = "command echo containing prompt text"
            Text = "info (qemu) cpus`e[K"
            Expect = $false
        }
    )
    foreach ($c in $promptCases) {
        $transportChecks++
        $got = Test-HmpReplyEnded -Text $c.Text
        if ($got -ne $c.Expect) {
            Write-Err ("{0}: expected reply-ended={1}, got {2}" -f
                $c.Name, $c.Expect, $got)
            $failures++
        }
    }

    # The first chunk already contains every expected CPU. The reader must wait
    # for the final prompt and retain the late unexpected CPU in the next chunk.
    $chunkQueue = New-Object System.Collections.Queue
    $chunkQueue.Enqueue($completePrefix)
    $chunkQueue.Enqueue("  CPU #2: thread_id=300`r`n(qemu) ")
    $chunkReader = {
        $chunkQueue.Dequeue()
    }.GetNewClosure()
    $transportChecks++
    try {
        $framedText = Read-HmpPromptTerminatedText -ReadChunk $chunkReader `
            -HardTimeoutMs 1000 -IdleTimeoutMs 100 -Description "Synthetic reply"
        $framedCheck = Test-VcpuThreadMapping -Text $framedText -ExpectedCpuCount 2
        if ($framedCheck.Passed -or $framedCheck.Complete) {
            Write-Err "chunked reply: late unexpected CPU was discarded"
            $failures++
        }
    } catch {
        Write-Err "chunked reply: $($_.Exception.Message)"
        $failures++
    }
    $retryQueue = New-Object System.Collections.Queue
    $retryQueue.Enqueue("* CPU #0: thread_id=100`r`n(qemu) ")
    $retryQueue.Enqueue($completePrefix + "`r`n(qemu) ")
    $retryReader = {
        param([int]$Attempt)
        $retryQueue.Dequeue()
    }.GetNewClosure()
    $retryResult = Get-VcpuThreadMappingWithRetry -ExpectedCpuCount 2 `
        -ReadText $retryReader -RetryDelayMs 0 -Quiet
    $transportChecks++
    if ($retryResult.Attempts -ne 2) {
        Write-Err "retry reader: expected two attempts, got $($retryResult.Attempts)"
        $failures++
    }
    $transportChecks++
    if ($null -eq $retryResult.Check -or -not $retryResult.Check.Passed) {
        Write-Err "retry reader: second complete response did not pass"
        $failures++
    }

    # A complete shared mapping is a real result, not a truncated response; it
    # must fail immediately instead of being replaced by a second clean read.
    $sharedQueue = New-Object System.Collections.Queue
    $sharedQueue.Enqueue("* CPU #0: thread_id=100`r`n  CPU #1: thread_id=100`r`n(qemu) ")
    $sharedQueue.Enqueue($completePrefix + "`r`n(qemu) ")
    $sharedReader = {
        param([int]$Attempt)
        $sharedQueue.Dequeue()
    }.GetNewClosure()
    $sharedResult = Get-VcpuThreadMappingWithRetry -ExpectedCpuCount 2 `
        -ReadText $sharedReader -RetryDelayMs 0 -Quiet
    $transportChecks++
    if ($sharedResult.Attempts -ne 1) {
        Write-Err "shared mapping: expected one attempt, got $($sharedResult.Attempts)"
        $failures++
    }
    $transportChecks++
    if ($null -eq $sharedResult.Check -or $sharedResult.Check.Passed -or
        -not $sharedResult.Check.ShowSingleThreadHint) {
        Write-Err "shared mapping: complete failure diagnostic was not preserved"
        $failures++
    }
    $totalChecks = $cases.Count + $mappingCases.Count + $diagnosticChecks + $transportChecks
    if ($failures -gt 0) {
        Write-Err "Self-test FAILED ($failures of $totalChecks checks)"
        exit 1
    }
    Write-Ok ("Self-test passed ({0} checks)" -f $totalChecks)
    exit 0
}

$problems = @()

Write-Step "Locating the Phase 2d QEMU process (monitor port $MonitorPort)"
$procs = @(Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -eq "qemu-system-x86_64.exe" -and
        $null -ne $_.CommandLine -and
        $_.CommandLine.Contains(":$MonitorPort")
    })
if ($procs.Count -eq 0) {
    Write-Err "No qemu-system-x86_64.exe found with monitor port $MonitorPort. Start the 2d VM first (scripts\local\qemu-win2k-smp-run.cmd)."
    exit 1
}
if ($procs.Count -gt 1) {
    Write-Err "$($procs.Count) QEMU processes match monitor port $MonitorPort; cannot tell which is the 2d VM."
    exit 1
}
$qemuPid = $procs[0].ProcessId
Write-Ok "PID $qemuPid"

$smpMatch = [regex]::Match(
    $procs[0].CommandLine,
    "(?i)(?:^|\s)-smp\s+(\d+)(?=\s|$)"
)
if (-not $smpMatch.Success) {
    Write-Err "Could not read the configured vCPU count from the QEMU -smp argument."
    exit 1
}
$expectedVcpus = [int]$smpMatch.Groups[1].Value
if ($expectedVcpus -lt 2) {
    Write-Err "QEMU is configured with -smp $expectedVcpus; Phase 2d requires at least two vCPUs."
    exit 1
}
Write-Ok "Configured vCPUs: $expectedVcpus"

Write-Step "Prerequisite 1: separate host threads per vCPU (monitor 'info cpus')"

# Each attempt reads through QEMU's final prompt before parsing, so a valid
# prefix cannot hide a late unexpected or conflicting CPU entry. Retry once
# when the full reply is incomplete or the transport times out; a complete
# shared-thread mapping remains a first-attempt failure.
$readMonitor = {
    param([int]$Attempt)
    Get-MonitorText -Port $MonitorPort -Command "info cpus"
}
$mappingRead = Get-VcpuThreadMappingWithRetry `
    -ExpectedCpuCount $expectedVcpus -ReadText $readMonitor `
    -Description "monitor on port $MonitorPort"
$threadCheck = $mappingRead.Check
if ($null -ne $threadCheck -and $threadCheck.Passed) {
    Write-Ok ("{0} vCPU(s), all with distinct thread_id(s): {1}" -f
        $threadCheck.CpuCount, ($threadCheck.ThreadIds -join ", "))
} elseif ($null -ne $threadCheck) {
    Write-Err ("Incomplete or shared vCPU thread mapping: {0}" -f
        ($threadCheck.Problems -join "; "))
    if ($threadCheck.Mapping -ne "") {
        Write-Host "  observed mapping : $($threadCheck.Mapping)"
    }
    if ($threadCheck.ShowSingleThreadHint) {
        Write-Host "  All vCPUs on one host thread is what -accel tcg,thread=single does; check the accelerator string in the launcher."
    }
    $problems += "vCPU thread mapping invalid"
} else {
    Write-Warn "Could not verify the vCPU thread model."
    $problems += "vCPU thread model unverified"
}

Write-Step "Prerequisite 2: the process may use at least $MinimumProcessors logical processors"
$mask = (Get-Process -Id $qemuPid).ProcessorAffinity.ToInt64()
$allowed = Get-SetBitCount -Mask $mask
$systemCpus = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
Write-Host ("  process affinity : 0x{0:X}" -f $mask)
Write-Host ("  allowed CPUs     : $allowed of $systemCpus system logical processors")
if ($allowed -ge $MinimumProcessors) {
    Write-Ok "Affinity permits parallel execution"
} else {
    Write-Err "Affinity allows $allowed logical processor(s); at least $MinimumProcessors are needed for the vCPU threads to overlap."
    $problems += "affinity too narrow"
}

Write-Step "Result"
if ($problems.Count -gt 0) {
    Write-Err ("Phase 2d parallelism prerequisites NOT met: {0}" -f ($problems -join "; "))
    exit 1
}
Write-Ok "The checked vCPU-thread and process-affinity prerequisites hold."
Write-Host "  This is capacity, not proof: only contended driver load demonstrates that races are actually exercised."
exit 0
