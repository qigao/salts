param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = "Stop"

function Get-Median([double[]]$Values) {
    if ($Values.Count -eq 0) { throw "median of empty sequence" }
    $sorted = @($Values | Sort-Object)
    $count = $sorted.Count
    if (($count % 2) -eq 1) { return [double]$sorted[[int]($count / 2)] }
    return ([double]$sorted[$count / 2 - 1] + [double]$sorted[$count / 2]) / 2.0
}

function Get-Mad([double[]]$Values) {
    $median = Get-Median $Values
    $deviations = @($Values | ForEach-Object { [math]::Abs([double]$_ - $median) })
    return Get-Median ([double[]]$deviations)
}

function Get-PairedSummary($BaselineRows, $CandidateRows, [string]$Metric) {
    $deltas = @()
    foreach ($repeat in 1..5) {
        $baseline = [double]$BaselineRows[$repeat].$Metric
        $candidate = [double]$CandidateRows[$repeat].$Metric
        if ($baseline -le 0 -or $candidate -le 0) {
            throw "non-positive paired metric $Metric repeat=$repeat"
        }
        $deltas += (($candidate / $baseline) - 1.0) * 100.0
    }
    return [pscustomobject]@{
        Median = Get-Median ([double[]]$deltas)
        Mad = Get-Mad ([double[]]$deltas)
    }
}

function Assert-Near([double]$Actual, [double]$Expected, [string]$Label) {
    if ([double]::IsNaN($Actual) -or [double]::IsInfinity($Actual)) {
        throw "$Label is not finite: $Actual"
    }
    if ([math]::Abs($Actual - $Expected) -gt 0.001) {
        throw "$Label mismatch: actual=$Actual expected=$Expected"
    }
}

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "CNet scaling benchmark CSV not found: $Path"
}
$pairedPath = $Path -replace '\.csv$', '.paired.csv'
if ($pairedPath -eq $Path -or -not (Test-Path -LiteralPath $pairedPath -PathType Leaf)) {
    throw "CNet paired scaling CSV not found: $pairedPath"
}

$rows = @(Import-Csv -LiteralPath $Path)
$pairedRows = @(Import-Csv -LiteralPath $pairedPath)
$connections = @(1, 4, 16, 64)
$payloads = @(1024, 8192, 32768, 65536)
$drivers = @("NativeIO direct", "CNet copy", "CNet retained")
$repeats = @(1, 2, 3, 4, 5)
$expectedRaw = $connections.Count * $payloads.Count * $drivers.Count * $repeats.Count
$expectedPaired = $connections.Count * $payloads.Count * 3

if ($rows.Count -ne $expectedRaw) {
    throw "expected $expectedRaw raw scaling rows, got $($rows.Count)"
}
if ($pairedRows.Count -ne $expectedPaired) {
    throw "expected $expectedPaired paired scaling rows, got $($pairedRows.Count)"
}

$rawByKey = @{}
foreach ($row in $rows) {
    $backend = [string]$row.backend
    $driver = [string]$row.driver
    $connectionCount = [int]$row.connections
    $payload = [int]$row.payload_bytes
    $repeat = [int]$row.repeat
    $samples = [int]$row.samples
    $operations = [int]$row.logical_operations
    $rate = [double]$row.operations_per_second
    $mib = [double]$row.mib_per_second
    $p50 = [double]$row.p50_ns
    $p95 = [double]$row.p95_ns
    $p99 = [double]$row.p99_ns
    $cpu = [double]$row.cpu_ns
    $progress = [int]$row.progress_calls

    if ($backend -notin @("epoll", "io_uring")) { throw "unexpected backend=$backend" }
    if ($drivers -notcontains $driver) { throw "unexpected driver=$driver" }
    if ($connections -notcontains $connectionCount) { throw "unexpected connections=$connectionCount" }
    if ($payloads -notcontains $payload) { throw "unexpected payload=$payload" }
    if ($repeats -notcontains $repeat) { throw "unexpected repeat=$repeat" }
    if ($samples -ne 32) { throw "unexpected samples=$samples" }
    if ($operations -ne $samples * $connectionCount) {
        throw "logical operation mismatch for $driver/$connectionCount/$payload/repeat$repeat"
    }
    if ($rate -le 0 -or $mib -le 0 -or $cpu -le 0) {
        throw "non-positive throughput/CPU for $driver/$connectionCount/$payload/repeat$repeat"
    }
    if ($p50 -le 0 -or $p95 -lt $p50 -or $p99 -lt $p95) {
        throw "invalid latency ordering for $driver/$connectionCount/$payload/repeat$repeat"
    }
    if ($progress -le 0) { throw "no progress calls for $driver/$connectionCount/$payload/repeat$repeat" }

    $key = "$driver|$connectionCount|$payload|$repeat"
    if ($rawByKey.ContainsKey($key)) { throw "duplicate scaling row $key" }
    $rawByKey[$key] = $row

    $ownerDrive = [double]$row.owner_drive_ns
    $ownerObserve = [double]$row.owner_observe_ns
    $clientPoll = [double]$row.client_poll_ns
    $requestLifecycle = [double]$row.owner_request_lifecycle_ns
    $requestStart = [double]$row.owner_request_start_ns
    $requestResubmit = [double]$row.owner_request_resubmit_ns
    $requestCompletion = [double]$row.owner_request_completion_ns
    $eventPublish = [double]$row.owner_event_publish_ns
    $dispatcherPrepare = [double]$row.dispatcher_prepare_ns
    $dispatcherInvoke = [double]$row.dispatcher_invoke_ns
    $dispatcherObserver = [double]$row.dispatcher_observer_ns
    $dispatcherRelease = [double]$row.dispatcher_release_ns
    $benchmarkCallback = [double]$row.benchmark_callback_ns
    $payloadCheck = [double]$row.benchmark_payload_check_ns

    if ($driver -eq "CNet copy" -or $driver -eq "CNet retained") {
        if ($ownerDrive -le 0 -or $ownerObserve -le 0 -or $clientPoll -le 0) {
            throw "missing CNet owner/poll attribution for $key"
        }
        $ownerNested = $requestLifecycle + $requestResubmit + $ownerObserve + $requestCompletion
        if ($ownerDrive -lt $ownerNested) { throw "owner stage nesting exceeds owner drive for $key" }
        if ($requestLifecycle -lt $requestStart) { throw "request start exceeds lifecycle for $key" }
        if ($requestCompletion -lt $eventPublish) { throw "event publish exceeds completion for $key" }
        if ($eventPublish -lt ($dispatcherPrepare + $dispatcherInvoke)) {
            throw "dispatcher stages exceed event publish for $key"
        }
        if ($dispatcherInvoke -lt ($dispatcherObserver + $dispatcherRelease)) {
            throw "dispatcher observer/release exceed invoke for $key"
        }
        if ($benchmarkCallback -le 0 -or $payloadCheck -le 0) {
            throw "missing benchmark callback/check timing for $key"
        }
        if ($dispatcherObserver -lt $benchmarkCallback) {
            throw "benchmark callback exceeds dispatcher observer for $key"
        }
        if ($benchmarkCallback -lt $payloadCheck) {
            throw "payload check exceeds benchmark callback for $key"
        }
        if ($driver -eq "CNet retained" -and $connectionCount -eq 16 -and
            $payload -in @(32768, 65536)) {
            $ownerResidualUs = ($ownerDrive - $ownerNested) / $operations / 1000.0
            $observerFrameworkUs = ($dispatcherObserver - $benchmarkCallback) / $operations / 1000.0
            $clientPollWrapperUs = ($clientPoll - $ownerDrive) / $operations / 1000.0
            if ($ownerResidualUs -gt 2.0) {
                throw "stable retained owner residual regression for $($key): $ownerResidualUs us/op"
            }
            if ($observerFrameworkUs -gt 1.0) {
                throw "stable retained observer framework regression for $($key): $observerFrameworkUs us/op"
            }
            if ($clientPollWrapperUs -gt 1.0) {
                throw "stable retained client poll wrapper regression for $($key): $clientPollWrapperUs us/op"
            }
        }
    } else {
        foreach ($value in @(
            $ownerDrive, $ownerObserve, $clientPoll, $requestLifecycle, $requestStart,
            $requestResubmit, $requestCompletion, $eventPublish, $dispatcherPrepare,
            $dispatcherInvoke, $dispatcherObserver, $dispatcherRelease, $benchmarkCallback,
            $payloadCheck
        )) {
            if ($value -ne 0) { throw "NativeIO row unexpectedly contains CNet attribution for $key" }
        }
    }
}

$expectedPairs = @(
    "NativeIO direct|CNet copy",
    "NativeIO direct|CNet retained",
    "CNet copy|CNet retained"
)
$pairedSeen = @{}
foreach ($row in $pairedRows) {
    $backend = [string]$row.backend
    $connectionCount = [int]$row.connections
    $payload = [int]$row.payload_bytes
    $repeatCount = [int]$row.repeats
    $baseline = [string]$row.baseline
    $candidate = [string]$row.candidate
    $pair = "$baseline|$candidate"

    if ($backend -notin @("epoll", "io_uring")) { throw "unexpected paired backend=$backend" }
    if ($connections -notcontains $connectionCount) { throw "unexpected paired connections=$connectionCount" }
    if ($payloads -notcontains $payload) { throw "unexpected paired payload=$payload" }
    if ($repeatCount -ne 5) { throw "unexpected paired repeats=$repeatCount" }
    if ($expectedPairs -notcontains $pair) { throw "unexpected paired comparison=$pair" }

    $pairKey = "$pair|$connectionCount|$payload"
    if ($pairedSeen.ContainsKey($pairKey)) { throw "duplicate paired row $pairKey" }
    $pairedSeen[$pairKey] = $true

    $baselineRows = @{}
    $candidateRows = @{}
    foreach ($repeat in $repeats) {
        $baselineRows[$repeat] = $rawByKey["$baseline|$connectionCount|$payload|$repeat"]
        $candidateRows[$repeat] = $rawByKey["$candidate|$connectionCount|$payload|$repeat"]
        if ($null -eq $baselineRows[$repeat] -or $null -eq $candidateRows[$repeat]) {
            throw "paired source row missing for $pairKey repeat=$repeat"
        }
    }

    $rate = Get-PairedSummary $baselineRows $candidateRows "operations_per_second"
    $p50 = Get-PairedSummary $baselineRows $candidateRows "p50_ns"
    $p95 = Get-PairedSummary $baselineRows $candidateRows "p95_ns"

    Assert-Near ([double]$row.rate_delta_median_percent) $rate.Median "$pairKey rate median"
    Assert-Near ([double]$row.rate_delta_mad_pp) $rate.Mad "$pairKey rate MAD"
    Assert-Near ([double]$row.p50_delta_median_percent) $p50.Median "$pairKey p50 median"
    Assert-Near ([double]$row.p50_delta_mad_pp) $p50.Mad "$pairKey p50 MAD"
    Assert-Near ([double]$row.p95_delta_median_percent) $p95.Median "$pairKey p95 median"
    Assert-Near ([double]$row.p95_delta_mad_pp) $p95.Mad "$pairKey p95 MAD"

    $stableLargePayload = $connectionCount -eq 16 -and $payload -in @(32768, 65536)
    if ($stableLargePayload -and $pair -eq "NativeIO direct|CNet retained") {
        if ($rate.Median -lt -5.0) {
            throw "retained throughput fell outside NativeIO performance class for $($pairKey): $($rate.Median)%"
        }
        if ($p50.Median -gt 10.0) {
            throw "retained p50 regression for $($pairKey): $($p50.Median)%"
        }
        if ($p95.Median -gt 15.0) {
            throw "retained p95 regression for $($pairKey): $($p95.Median)%"
        }
        foreach ($entry in @(
            [pscustomobject]@{ Name = "rate"; Mad = $rate.Mad; Limit = 5.0 },
            [pscustomobject]@{ Name = "p50"; Mad = $p50.Mad; Limit = 5.0 },
            [pscustomobject]@{ Name = "p95"; Mad = $p95.Mad; Limit = 5.0 }
        )) {
            if ($entry.Mad -gt $entry.Limit) {
                throw "retained $($entry.Name) paired noise exceeded gate for $($pairKey): $($entry.Mad)pp"
            }
        }
    }

    if ($stableLargePayload -and $pair -eq "CNet copy|CNet retained") {
        if ($rate.Median -lt 3.0) {
            throw "retained-send throughput benefit disappeared for $($pairKey): $($rate.Median)%"
        }
        if ($p50.Median -gt -2.0) {
            throw "retained-send p50 benefit disappeared for $($pairKey): $($p50.Median)%"
        }
        if ($p95.Median -gt 2.0) {
            throw "retained-send p95 regressed for $($pairKey): $($p95.Median)%"
        }
        if ($rate.Mad -gt 5.0 -or $p50.Mad -gt 5.0 -or $p95.Mad -gt 5.0) {
            throw "copy-vs-retained paired noise exceeded gate for $pairKey"
        }
    }
}

Write-Host "CNet scaling benchmark verified: $($rows.Count) raw rows, $($pairedRows.Count) paired rows"
