param([Parameter(Mandatory = $true)][string]$Prefix)

$ErrorActionPreference = 'Stop'
$repeats = 5
$roundTrips = 512
$drivers = @('libuv', 'NativeIO direct', 'NativeIO coroutine', 'CNet')
$passes = @('A', 'diagnostic', 'B')
$payloads = @{ TCP = @(1024, 4096, 8192, 16384, 32768, 65536); UDP = @(1024, 4096, 8192) }
$runs = @(Import-Csv -LiteralPath "$Prefix.runs.csv")
$backends = @($runs.backend | Sort-Object -Unique)
if ($backends.Count -ne 1 -or $backends[0] -notin @('epoll', 'io_uring', 'iocp', 'kqueue')) {
    throw 'Artifacts must contain exactly one supported NativeIO backend'
}
$expectedRuns = ($payloads.TCP.Count + $payloads.UDP.Count) * $drivers.Count * $passes.Count * $repeats
if ($runs.Count -ne $expectedRuns) { throw "Expected $expectedRuns runs, got $($runs.Count)" }

$index = @{}
foreach ($run in $runs) {
    if ($run.driver -notin $drivers -or $run.pass -notin $passes -or
        -not $payloads.ContainsKey($run.protocol) -or
        [int]$run.payload_bytes -notin $payloads[$run.protocol] -or
        [int]$run.repeat -lt 1 -or [int]$run.repeat -gt $repeats -or
        [int]$run.round_trips -ne $roundTrips) { throw 'Invalid run identity or workload' }
    $key = "$($run.backend),$($run.protocol),$($run.payload_bytes),$($run.driver),$($run.pass),$($run.repeat)"
    if ($index.ContainsKey($key)) { throw "Duplicate run: $key" }
    $index[$key] = @{
        Run = $run; Latencies = [UInt64[]]::new($roundTrips); Count = 0
        Starts = [UInt64]0; Drives = [UInt64]0; WallSum = [decimal]0
    }
    if ([UInt64]$run.wall_ns -eq 0) { throw "Empty wall duration: $key" }
    # A zero CPU reading may be clock quantization, not an unsupported metric.
    $null = [UInt64]$run.client_cpu_ns
    if ($run.backend -eq 'iocp') {
        if ([UInt64]$run.client_cpu_cycles -eq 0) { throw "Missing thread cycle count: $key" }
    } elseif ($run.client_cpu_cycles -ne '') {
        throw "Unsupported thread cycle counter must be blank: $key"
    }
    if ($run.backend -in @('epoll', 'io_uring')) {
        if ($run.voluntary_switches -eq '' -or $run.involuntary_switches -eq '') {
            throw "Missing Linux thread counters: $key"
        }
    } elseif ($run.voluntary_switches -ne '' -or $run.involuntary_switches -ne '') {
        throw "Unsupported thread counters must be blank: $key"
    }
}

$reader = [IO.File]::OpenText((Resolve-Path -LiteralPath "$Prefix.samples.csv").Path)
$sampleCount = 0
try {
    $header = 'backend,protocol,payload_bytes,driver,pass,repeat,sample,wall_ns,start_ns,drive_ns,check_ns,remainder_ns,start_calls,drive_calls'
    if ($reader.ReadLine() -ne $header) { throw 'Unexpected sample schema' }
    while ($null -ne ($line = $reader.ReadLine())) {
        $fields = $line.Split(',')
        if ($fields.Count -ne 14) { throw 'Unexpected sample field count' }
        $key = $fields[0..5] -join ','
        $entry = $index[$key]
        if ($null -eq $entry) { throw "Sample without run: $key" }
        if ([int]$fields[6] -ne $entry.Count + 1 -or $entry.Count -ge $roundTrips) {
            throw "Missing, duplicate, or unordered sample: $key"
        }
        $wall = [UInt64]$fields[7]
        if ($wall -eq 0) { throw "Empty sample: $key" }
        $entry.Latencies[$entry.Count] = $wall
        $entry.WallSum += [decimal]$wall
        $entry.Count++
        if ($fields[4] -eq 'diagnostic') {
            # Decimal avoids wrapping UInt64 when validating corrupted input.
            $sum = [decimal]([UInt64]$fields[8]) + [decimal]([UInt64]$fields[9]) +
                   [decimal]([UInt64]$fields[10]) + [decimal]([UInt64]$fields[11])
            if ($sum -ne [decimal]$wall) { throw "Overlapping or missing phase time: $key" }
            $entry.Starts += [UInt64]$fields[12]
            $entry.Drives += [UInt64]$fields[13]
        } elseif (($fields[8..13] -join '') -ne '') {
            throw "Uninstrumented samples must not invent stage measurements: $key"
        }
        $sampleCount++
    }
} finally {
    $reader.Dispose()
}

foreach ($key in $index.Keys) {
    $entry = $index[$key]
    if ($entry.Count -ne $roundTrips) { throw "Incomplete samples: $key" }
    if ($entry.WallSum -gt [decimal]$entry.Run.wall_ns) { throw "Samples exceed batch wall time: $key" }
    [Array]::Sort($entry.Latencies)
    $p50 = $entry.Latencies[[int][Math]::Floor(($roundTrips - 1) * 0.50)]
    $p95 = $entry.Latencies[[int][Math]::Floor(($roundTrips - 1) * 0.95)]
    if ($p50 -ne [UInt64]$entry.Run.p50_ns -or $p95 -ne [UInt64]$entry.Run.p95_ns) {
        throw "Percentiles cannot be reproduced from samples: $key"
    }
    if ($entry.Run.pass -eq 'diagnostic') {
        if ($entry.Starts -ne [UInt64]$entry.Run.start_calls -or
            $entry.Drives -ne [UInt64]$entry.Run.drive_calls) { throw "Call count mismatch: $key" }
    } elseif ($entry.Run.start_calls -ne '' -or $entry.Run.drive_calls -ne '') {
        throw "Uninstrumented run contains diagnostic counters: $key"
    }
}
Write-Output "Verified $($runs.Count) runs and $sampleCount samples: workload, phases, counters, p50/p95."
