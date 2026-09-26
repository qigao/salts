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

function Get-SelfRepeatSummary($Rows, [string]$Metric) {
    # Adjacent same-driver repeats are the A/A control. The driver order rotates
    # every repeat, so these deltas expose runner drift and order sensitivity
    # without adding another full benchmark curve.
    $deltas = @()
    foreach ($repeat in 1..4) {
        $before = [double]$Rows[$repeat].$Metric
        $after = [double]$Rows[$repeat + 1].$Metric
        if ($before -le 0 -or $after -le 0) {
            throw "non-positive A/A metric $Metric repeat=$repeat"
        }
        $deltas += (($after / $before) - 1.0) * 100.0
    }
    return [pscustomobject]@{
        Median = Get-Median ([double[]]$deltas)
        Mad = Get-Mad ([double[]]$deltas)
    }
}

function Test-ControlValid($Summary) {
    return [math]::Abs([double]$Summary.Median) -le 5.0 -and
           [double]$Summary.Mad -le 5.0
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
$pairedPath = $Path -replace '\.csv
$rows = @(Import-Csv -LiteralPath $Path)
$pairedRows = @(Import-Csv -LiteralPath $pairedPath)
$connections = @(1, 4, 16, 64)
$payloads = @(1024, 8192, 32768, 65536)
$drivers = @("NativeIO direct", "CNet retained")
$repeats = @(1, 2, 3, 4, 5)
$expectedRaw = $connections.Count * $payloads.Count * $drivers.Count * $repeats.Count
$expectedPaired = $connections.Count * $payloads.Count

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

    if ($driver -eq "CNet retained") {
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
    "NativeIO direct|CNet retained"
)
$pairedSeen = @{}
$validityRows = [Collections.Generic.List[object]]::new()
$performanceFailures = [Collections.Generic.List[string]]::new()
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

    $baselineRateControl = Get-SelfRepeatSummary $baselineRows "operations_per_second"
    $baselineP50Control = Get-SelfRepeatSummary $baselineRows "p50_ns"
    $baselineP95Control = Get-SelfRepeatSummary $baselineRows "p95_ns"
    $candidateRateControl = Get-SelfRepeatSummary $candidateRows "operations_per_second"
    $candidateP50Control = Get-SelfRepeatSummary $candidateRows "p50_ns"
    $candidateP95Control = Get-SelfRepeatSummary $candidateRows "p95_ns"

    $controlValid =
        (Test-ControlValid $baselineRateControl) -and
        (Test-ControlValid $baselineP50Control) -and
        (Test-ControlValid $baselineP95Control) -and
        (Test-ControlValid $candidateRateControl) -and
        (Test-ControlValid $candidateP50Control) -and
        (Test-ControlValid $candidateP95Control)

    $pairedValid =
        $rate.Mad -le 5.0 -and
        $p50.Mad -le 5.0 -and
        $p95.Mad -le 10.0

    $performanceGateCell =
        $connectionCount -eq 16 -and $payload -in @(32768, 65536) -and
        $pair -eq "NativeIO direct|CNet retained"
    $verdict = if ($performanceGateCell) { "PASS" } else { "EVIDENCE_ONLY" }

    if ($performanceGateCell -and (-not $controlValid -or -not $pairedValid)) {
        $verdict = "UNSTABLE"
    } elseif ($performanceGateCell) {
        if ($rate.Median -lt -5.0) {
            $verdict = "FAIL"
            $performanceFailures.Add(
                "retained throughput fell outside NativeIO performance class for $($pairKey): $($rate.Median)%")
        }
        if ($p50.Median -gt 10.0) {
            $verdict = "FAIL"
            $performanceFailures.Add(
                "retained p50 regression for $($pairKey): $($p50.Median)%")
        }
        if ($p95.Median -gt 15.0) {
            $verdict = "FAIL"
            $performanceFailures.Add(
                "retained p95 regression for $($pairKey): $($p95.Median)%")
        }
    }

    $validityRows.Add([pscustomobject]@{
        backend = $backend
        connections = $connectionCount
        payload_bytes = $payload
        baseline = $baseline
        candidate = $candidate
        baseline_rate_aa_median_percent = $baselineRateControl.Median
        baseline_rate_aa_mad_pp = $baselineRateControl.Mad
        baseline_p50_aa_median_percent = $baselineP50Control.Median
        baseline_p50_aa_mad_pp = $baselineP50Control.Mad
        baseline_p95_aa_median_percent = $baselineP95Control.Median
        baseline_p95_aa_mad_pp = $baselineP95Control.Mad
        candidate_rate_aa_median_percent = $candidateRateControl.Median
        candidate_rate_aa_mad_pp = $candidateRateControl.Mad
        candidate_p50_aa_median_percent = $candidateP50Control.Median
        candidate_p50_aa_mad_pp = $candidateP50Control.Mad
        candidate_p95_aa_median_percent = $candidateP95Control.Median
        candidate_p95_aa_mad_pp = $candidateP95Control.Mad
        control_valid = $controlValid
        paired_valid = $pairedValid
        performance_gate_cell = $performanceGateCell
        verdict = $verdict
    })
}

$validityRows | Export-Csv -LiteralPath $validityPath -NoTypeInformation

Write-Output "CNet scaling benchmark verified: $($rows.Count) raw rows, $($pairedRows.Count) paired rows"
Write-Output "Measurement validity: adjacent same-driver repeats are A/A controls; |median| <= 5% and MAD <= 5pp are required before a performance verdict."
Write-Output ""
Write-Output "| backend | connections | payload | control valid | paired valid | verdict |"
Write-Output "| --- | ---: | ---: | --- | --- | --- |"
foreach ($entry in $validityRows | Where-Object { $_.performance_gate_cell }) {
    Write-Output ("| {0} | {1} | {2} | {3} | {4} | {5} |" -f
        $entry.backend, $entry.connections, $entry.payload_bytes,
        $entry.control_valid, $entry.paired_valid, $entry.verdict)
    if ($entry.verdict -eq "UNSTABLE") {
        Write-Warning ("UNSTABLE/no performance verdict: {0} {1}c/{2}B; " +
            "A/A control or paired dispersion exceeded the validity bound." -f
            $entry.backend, $entry.connections, $entry.payload_bytes)
    }
}

if ($performanceFailures.Count -ne 0) {
    throw ($performanceFailures -join [Environment]::NewLine)
}
, '.paired.csv'
if ($pairedPath -eq $Path -or -not (Test-Path -LiteralPath $pairedPath -PathType Leaf)) {
    throw "CNet paired scaling CSV not found: $pairedPath"
}
$validityPath = $Path -replace '\.csv
$rows = @(Import-Csv -LiteralPath $Path)
$pairedRows = @(Import-Csv -LiteralPath $pairedPath)
$connections = @(1, 4, 16, 64)
$payloads = @(1024, 8192, 32768, 65536)
$drivers = @("NativeIO direct", "CNet retained")
$repeats = @(1, 2, 3, 4, 5)
$expectedRaw = $connections.Count * $payloads.Count * $drivers.Count * $repeats.Count
$expectedPaired = $connections.Count * $payloads.Count

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

    if ($driver -eq "CNet retained") {
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
    "NativeIO direct|CNet retained"
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

    $stableRatioCell = $connectionCount -eq 16 -and $payload -eq 65536
    if ($stableRatioCell -and $pair -eq "NativeIO direct|CNet retained") {
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
            [pscustomobject]@{ Name = "p95"; Mad = $p95.Mad; Limit = 10.0 }
        )) {
            if ($entry.Mad -gt $entry.Limit) {
                throw "retained $($entry.Name) paired noise exceeded gate for $($pairKey): $($entry.Mad)pp"
            }
        }
    }

}

Write-Host "CNet scaling benchmark verified: $($rows.Count) raw rows, $($pairedRows.Count) paired rows"
, '.validity.csv'
if ($validityPath -eq $Path) {
    throw "cannot derive CNet scaling validity CSV path from: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
$pairedRows = @(Import-Csv -LiteralPath $pairedPath)
$connections = @(1, 4, 16, 64)
$payloads = @(1024, 8192, 32768, 65536)
$drivers = @("NativeIO direct", "CNet retained")
$repeats = @(1, 2, 3, 4, 5)
$expectedRaw = $connections.Count * $payloads.Count * $drivers.Count * $repeats.Count
$expectedPaired = $connections.Count * $payloads.Count

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

    if ($driver -eq "CNet retained") {
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
    "NativeIO direct|CNet retained"
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

    $stableRatioCell = $connectionCount -eq 16 -and $payload -eq 65536
    if ($stableRatioCell -and $pair -eq "NativeIO direct|CNet retained") {
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
            [pscustomobject]@{ Name = "p95"; Mad = $p95.Mad; Limit = 10.0 }
        )) {
            if ($entry.Mad -gt $entry.Limit) {
                throw "retained $($entry.Name) paired noise exceeded gate for $($pairKey): $($entry.Mad)pp"
            }
        }
    }

}

Write-Host "CNet scaling benchmark verified: $($rows.Count) raw rows, $($pairedRows.Count) paired rows"
