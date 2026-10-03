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
$attributionPath = "$Prefix.cnet-retained-attribution.csv"
if (-not (Test-Path -LiteralPath $attributionPath -PathType Leaf)) {
    throw "Missing CNet retained attribution artifact: $attributionPath"
}
$attributionRows = @(Import-Csv -LiteralPath $attributionPath)
$expectedAttributionRows = $payloads.TCP.Count * $repeats
if ($attributionRows.Count -ne $expectedAttributionRows) {
    throw "Expected $expectedAttributionRows CNet retained attribution rows, got $($attributionRows.Count)"
}

$attributionIndex = @{}
$finiteFields = @(
    'client_cpu_ns_per_rt',
    'client_cpu_cycles_per_rt',
    'send_admission_ns_per_rt',
    'total_budget_ns',
    'send_public_control_ns',
    'queue_control_ns',
    'payload_copy_ns',
    'client_poll_wrapper_ns',
    'owner_control_ns',
    'request_control_ns',
    'native_request_start_ns',
    'native_request_resubmit_ns',
    'native_observe_ns',
    'completion_control_ns',
    'event_publish_residual_ns',
    'dispatcher_prepare_ns',
    'dispatcher_invoke_framework_ns',
    'client_observer_control_ns',
    'dispatcher_release_ns',
    'benchmark_payload_check_ns',
    'benchmark_callback_residual_ns',
    'fixed_control_total_ns',
    'shared_native_total_ns',
    'benchmark_work_total_ns',
    'closure_residual_ns'
)

foreach ($row in $attributionRows) {
    if ($row.backend -ne $backends[0]) {
        throw "Attribution backend mismatch: expected=$($backends[0]) actual=$($row.backend)"
    }
    $payload = [int]$row.payload_bytes
    $repeat = [int]$row.repeat
    if ($payload -notin $payloads.TCP -or $repeat -lt 1 -or $repeat -gt $repeats -or
        [int]$row.round_trips -ne $roundTrips) {
        throw "Invalid retained attribution identity: payload=$payload repeat=$repeat"
    }
    $key = "$($row.backend),$payload,$repeat"
    if ($attributionIndex.ContainsKey($key)) {
        throw "Duplicate retained attribution row: $key"
    }
    $attributionIndex[$key] = $true

    $p50 = [UInt64]$row.p50_ns
    $p95 = [UInt64]$row.p95_ns
    if ($p50 -eq 0 -or $p95 -lt $p50) {
        throw "Invalid retained attribution latency: $key p50=$p50 p95=$p95"
    }

    foreach ($field in $finiteFields) {
        $value = [double]::Parse(
            [string]$row.$field,
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture)
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) {
            throw "Non-finite retained attribution value: $key field=$field value=$value"
        }
        if ($field -ne 'closure_residual_ns' -and $value -lt 0.0) {
            throw "Negative retained attribution value: $key field=$field value=$value"
        }
    }

    if ([math]::Abs([double]$row.payload_copy_ns) -gt 0.001) {
        throw "Retained path unexpectedly copied payload bytes/time: $key payload_copy_ns=$($row.payload_copy_ns)"
    }
    if ([math]::Abs([double]$row.closure_residual_ns) -gt 1.0) {
        throw "Retained attribution does not close: $key residual_ns=$($row.closure_residual_ns)"
    }
}

foreach ($payload in $payloads.TCP) {
    foreach ($repeat in 1..$repeats) {
        $key = "$($backends[0]),$payload,$repeat"
        if (-not $attributionIndex.ContainsKey($key)) {
            throw "Missing retained attribution row: $key"
        }
    }
}

Write-Output "Verified $($runs.Count) runs, $sampleCount samples, and $($attributionRows.Count) CNet retained attribution rows: workload, phases, counters, p50/p95, zero-copy, closure."
