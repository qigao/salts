param([Parameter(Mandatory = $true)][string]$Directory)

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
$details = [Collections.Generic.List[object]]::new()
$summary = [Collections.Generic.List[object]]::new()
$clients = @{}

foreach ($file in Get-ChildItem -LiteralPath $Directory -File -Filter '*.trace.*') {
    $active = $false
    $finished = $false
    $rounds = 0
    $calls = @{}
    $pending = $null
    $reader = [IO.File]::OpenText($file.FullName)
    try {
      while ($null -ne ($line = $reader.ReadLine())) {
        if ($line -match 'write\(2, "IO_BENCH_MEASURE_BEGIN rounds=(\d+)') {
            if ($active -or $finished) { throw "Duplicate measurement window: $($file.Name)" }
            $rounds = [int]$Matches[1]
            if ($rounds -le 0) { throw 'Invalid trace workload' }
            $active = $true
            continue
        }
        if ($line -match 'write\(2, "IO_BENCH_MEASURE_END rounds=(\d+)') {
            if (-not $active -or [int]$Matches[1] -ne $rounds -or $null -ne $pending) {
                throw "Unmatched measurement window: $($file.Name)"
            }
            $active = $false
            $finished = $true
            continue
        }
        if (-not $active) { continue }
        if ($line -match '^\d+\.\d+\s+--- ') { continue }
        if ($line -match '^(.*)<unfinished \.\.\.>$') {
            if ($null -ne $pending) { throw "Nested unfinished syscall: $line" }
            $pending = $Matches[1]
            continue
        }
        if ($line -match '^\d+\.\d+\s+<\.\.\. (\w+) resumed>(.*)$') {
            $resumedCall = $Matches[1]
            $suffix = $Matches[2]
            if ($null -eq $pending -or $pending -notmatch "\s+$resumedCall\(") {
                throw "Unmatched resumed syscall: $line"
            }
            $line = $pending + $suffix
            $pending = $null
        }
        if ($line -notmatch '^\d+\.\d+\s+(\w+)\(.*\)\s+=\s+(.+?)\s+<([\d.]+)>$') {
            throw "Unparsed syscall inside measurement window: $line"
        }
        $name = $Matches[1]
        $result = $Matches[2]
        $elapsed = [double]::Parse($Matches[3], $culture) * 1000000.0
        if (-not $calls.ContainsKey($name)) {
            $calls[$name] = @{ Count = 0; Elapsed = 0.0; Errors = 0; Again = 0; Zero = 0 }
        }
        $call = $calls[$name]
        $call.Count++
        $call.Elapsed += $elapsed
        if ($result -match '^-1 ') { $call.Errors++ }
        if ($result -match '^-1 EAGAIN\b') { $call.Again++ }
        if ($result -match '^0(?:\s|$)') { $call.Zero++ }
      }
    } finally {
        $reader.Dispose()
    }
    if ($active) { throw "Incomplete trace window: $($file.Name)" }
    if (-not $finished) { continue } # Echo peer, never added to client counts.
    if ($file.Name -notmatch '^(libuv|native|coroutine|cnet)-(tcp|udp)-(\d+)\.trace\.(\d+)$') {
        throw "Unknown trace identity: $($file.Name)"
    }
    $driver = $Matches[1]; $protocol = $Matches[2]; $bytes = [int]$Matches[3]; $thread = $Matches[4]
    $case = "$driver-$protocol-$bytes"
    if ($clients.ContainsKey($case)) { throw "Duplicate client trace: $case" }
    $clients[$case] = $true
    $dataCalls = 0; $pollCalls = 0; $uringCalls = 0; $registrationCalls = 0; $again = 0; $emptyPolls = 0
    $fdPollCalls = 0; $emptyFdPolls = 0
    foreach ($name in ($calls.Keys | Sort-Object)) {
        $call = $calls[$name]
        $details.Add([pscustomobject]@{
            driver = $driver; protocol = $protocol; payload_bytes = $bytes; client_tid = $thread
            measured_round_trips = $rounds; syscall = $name; calls = $call.Count
            calls_per_rt = ($call.Count / $rounds).ToString('F6', $culture)
            traced_elapsed_us = $call.Elapsed.ToString('F3', $culture)
            errors = $call.Errors; eagain = $call.Again; zero_returns = $call.Zero
        })
        if ($name -match '^(read|readv|write|writev|send.*|recv.*)$') { $dataCalls += $call.Count }
        if ($name -match '^epoll_(wait|pwait|pwait2)$') { $pollCalls += $call.Count }
        if ($name -eq 'io_uring_enter') { $uringCalls += $call.Count }
        if ($name -eq 'epoll_ctl') { $registrationCalls += $call.Count }
        if ($name -match '^epoll_(wait|pwait|pwait2)$') { $emptyPolls += $call.Zero }
        if ($name -in @('poll', 'ppoll')) { $fdPollCalls += $call.Count; $emptyFdPolls += $call.Zero }
        $again += $call.Again
    }
    $summary.Add([pscustomobject]@{
        case = $case; data_calls_per_rt = ($dataCalls / $rounds).ToString('F3', $culture)
        epoll_poll_per_rt = ($pollCalls / $rounds).ToString('F3', $culture)
        uring_enter_per_rt = ($uringCalls / $rounds).ToString('F3', $culture)
        epoll_ctl_syscalls_per_rt = ($registrationCalls / $rounds).ToString('F3', $culture)
        eagain_per_rt = ($again / $rounds).ToString('F3', $culture)
        empty_epoll_per_rt = ($emptyPolls / $rounds).ToString('F3', $culture)
        poll_wait_per_rt = ($fdPollCalls / $rounds).ToString('F3', $culture)
        empty_poll_per_rt = ($emptyFdPolls / $rounds).ToString('F3', $culture)
    })
}
if ($clients.Count -eq 0) { throw 'No complete client measurement windows found' }
foreach ($log in Get-ChildItem -LiteralPath $Directory -File -Filter '*.log') {
    if (-not $clients.ContainsKey($log.BaseName)) { throw "Missing client trace: $($log.Name)" }
}
$details | Export-Csv -LiteralPath (Join-Path $Directory 'syscalls.csv') -NoTypeInformation
$summary | Sort-Object case | Export-Csv -LiteralPath (Join-Path $Directory 'summary.csv') -NoTypeInformation
Write-Output 'Trace evidence only: syscall durations include ptrace/scheduling overhead, not baseline latency.'
Write-Output 'Data calls include client read/write syscalls; io_uring SQEs are NOT syscall-count equivalents.'
Write-Output 'Zero direct epoll_ctl calls do NOT mean no registration: libuv can submit epoll control through io_uring.'
Write-Output ''
Write-Output '| case | data/RT | epoll poll/RT | uring enter/RT | direct ctl/RT | EAGAIN/RT | empty epoll/RT | poll+ppoll/RT | empty poll+ppoll/RT |'
Write-Output '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |'
foreach ($row in ($summary | Sort-Object case)) {
    Write-Output ('| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} |' -f
        $row.case, $row.data_calls_per_rt, $row.epoll_poll_per_rt, $row.uring_enter_per_rt,
        $row.epoll_ctl_syscalls_per_rt, $row.eagain_per_rt, $row.empty_epoll_per_rt,
        $row.poll_wait_per_rt, $row.empty_poll_per_rt)
}
