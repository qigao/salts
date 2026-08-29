$ErrorActionPreference = "Stop"

function Assert-Equal($Actual, $Expected, [string]$Message) {
  if ($Actual -ne $Expected) {
    throw "$Message`: expected '$Expected', got '$Actual'"
  }
}

function Assert-Sequence($Actual, [string[]]$Expected, [string]$Message) {
  $actualText = @($Actual) -join ","
  $expectedText = $Expected -join ","
  Assert-Equal $actualText $expectedText $Message
}

function Assert-Throws([scriptblock]$Action, [string]$Message) {
  try {
    & $Action
  } catch {
    return
  }
  throw "$Message`: expected an exception"
}

$helperPath = Join-Path (Split-Path -Parent $PSScriptRoot) `
  "scripts/cflow-benchmark-stats.ps1"
. $helperPath

Assert-Equal (Get-CflowMedian -Values @(3.0, 1.0, 2.0)) 2.0 `
  "odd median"
Assert-Equal (Get-CflowMedian -Values @(3.0, 1.0)) 2.0 `
  "even median"
Assert-Throws { Get-CflowMedian -Values @() } "empty median"

$backends = @("epoll", "io_uring", "poll")
Assert-Sequence (Get-CflowRotatedOrder -Values $backends -Run 1) `
  @("epoll", "io_uring", "poll") "first backend rotation"
Assert-Sequence (Get-CflowRotatedOrder -Values $backends -Run 2) `
  @("io_uring", "poll", "epoll") "second backend rotation"
Assert-Sequence (Get-CflowRotatedOrder -Values $backends -Run 3) `
  @("poll", "epoll", "io_uring") "third backend rotation"

Assert-Sequence `
  (Get-CflowDriverOrder -Run 1 -WaitMode blocking -BackendIndex 0 -PayloadIndex 0) `
  @("actor", "source") "blocking driver order"
Assert-Sequence `
  (Get-CflowDriverOrder -Run 1 -WaitMode busy -BackendIndex 0 -PayloadIndex 0) `
  @("source", "actor") "busy driver order"
Assert-Sequence `
  (Get-CflowDriverOrder -Run 2 -WaitMode blocking -BackendIndex 0 -PayloadIndex 0) `
  @("source", "actor") "next-run driver order"
Assert-Sequence `
  (Get-CflowDriverOrder -Run 1 -WaitMode blocking -BackendIndex 1 -PayloadIndex 0) `
  @("source", "actor") "next-backend driver order"
Assert-Sequence `
  (Get-CflowDriverOrder -Run 1 -WaitMode blocking -BackendIndex 0 -PayloadIndex 1) `
  @("source", "actor") "next-payload driver order"
Assert-Sequence `
  (Get-CflowBaselineDriverOrder -Run 1 -BackendIndex 0 -PayloadIndex 0) `
  @("direct", "actor") "first baseline driver order"
Assert-Sequence `
  (Get-CflowBaselineDriverOrder -Run 2 -BackendIndex 0 -PayloadIndex 0) `
  @("actor", "direct") "next-run baseline driver order"
Assert-Sequence `
  (Get-CflowBaselineDriverOrder -Run 1 -BackendIndex 1 -PayloadIndex 0) `
  @("actor", "direct") "next-backend baseline driver order"

$reports = @(
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; payload_bytes=64; driver="actor"; wait_mode="blocking"; attempted=10; p50_ns=100; p99_ns=200; wall_ns=1000; process_cpu_ns=500; process_cpu_pct=50; application_mib_per_cpu_second=10; admission_mean_ns=10; completion_drive_mean_ns=90 },
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; payload_bytes=64; driver="source"; wait_mode="blocking"; attempted=10; p50_ns=110; p99_ns=240; wall_ns=1050; process_cpu_ns=550; process_cpu_pct=52; application_mib_per_cpu_second=9; admission_mean_ns=5; completion_drive_mean_ns=105 },
  [pscustomobject]@{ benchmark_run=2; backend="epoll"; payload_bytes=64; driver="actor"; wait_mode="blocking"; attempted=10; p50_ns=1000; p99_ns=2000; wall_ns=2000; process_cpu_ns=1000; process_cpu_pct=51; application_mib_per_cpu_second=20; admission_mean_ns=20; completion_drive_mean_ns=180 },
  [pscustomobject]@{ benchmark_run=2; backend="epoll"; payload_bytes=64; driver="source"; wait_mode="blocking"; attempted=10; p50_ns=1500; p99_ns=3000; wall_ns=2100; process_cpu_ns=1100; process_cpu_pct=53; application_mib_per_cpu_second=18; admission_mean_ns=10; completion_drive_mean_ns=210 },
  [pscustomobject]@{ benchmark_run=3; backend="epoll"; payload_bytes=64; driver="actor"; wait_mode="blocking"; attempted=10; p50_ns=1100; p99_ns=2200; wall_ns=3000; process_cpu_ns=1500; process_cpu_pct=52; application_mib_per_cpu_second=30; admission_mean_ns=30; completion_drive_mean_ns=270 },
  [pscustomobject]@{ benchmark_run=3; backend="epoll"; payload_bytes=64; driver="source"; wait_mode="blocking"; attempted=10; p50_ns=990; p99_ns=1980; wall_ns=3150; process_cpu_ns=1650; process_cpu_pct=54; application_mib_per_cpu_second=27; admission_mean_ns=15; completion_drive_mean_ns=315 },
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; payload_bytes=1024; driver="actor"; wait_mode="blocking"; attempted=10; p50_ns=200; p99_ns=400; wall_ns=1000; process_cpu_ns=500; process_cpu_pct=50; application_mib_per_cpu_second=10; admission_mean_ns=10; completion_drive_mean_ns=90 },
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; payload_bytes=1024; driver="source"; wait_mode="blocking"; attempted=10; p50_ns=230; p99_ns=460; wall_ns=1080; process_cpu_ns=560; process_cpu_pct=52; application_mib_per_cpu_second=9; admission_mean_ns=5; completion_drive_mean_ns=125 }
)

$summary = Get-CflowPairedSourceSummary -Reports $reports `
  -Backend epoll -WaitMode blocking -PayloadBytes 64 -ExpectedRuns 3
Assert-Equal $summary.runs 3 "paired run count"
Assert-Equal $summary.payload_bytes 64 "paired payload"
Assert-Equal $summary.actor_median_p50_ns 1000.0 "Actor P50 median"
Assert-Equal $summary.source_median_p50_ns 990.0 "Source P50 median"
Assert-Equal $summary.actor_median_p99_ns 2000.0 "Actor P99 median"
Assert-Equal $summary.source_median_p99_ns 1980.0 "Source P99 median"
Assert-Equal (100.0 * ($summary.source_median_p50_ns -
      $summary.actor_median_p50_ns) / $summary.actor_median_p50_ns) -1.0 `
  "unpaired P50 delta differs from paired delta"
Assert-Equal $summary.paired_p50_delta_pct 10.0 "paired P50 delta"
Assert-Equal $summary.paired_p99_delta_pct 20.0 "paired P99 delta"
Assert-Equal $summary.paired_wall_delta_pct 5.0 "paired wall delta"
Assert-Equal $summary.paired_cpu_time_delta_pct 10.0 "paired CPU-time delta"
Assert-Equal $summary.paired_cpu_efficiency_delta_pct -10.0 `
  "paired CPU-efficiency delta"
Assert-Equal $summary.paired_combined_stage_delta_pct 10.0 `
  "paired combined-stage delta"
Assert-Equal $summary.paired_p50_delta_ns 10.0 "paired P50 absolute delta"
Assert-Equal $summary.paired_p99_delta_ns 40.0 "paired P99 absolute delta"
Assert-Equal $summary.paired_wall_delta_ns_per_exchange 10.0 `
  "paired wall absolute delta per exchange"
Assert-Equal $summary.paired_cpu_time_delta_ns_per_exchange 10.0 `
  "paired CPU-time absolute delta per exchange"
Assert-Equal $summary.paired_combined_stage_delta_ns 20.0 `
  "paired combined-stage absolute delta"
Assert-Equal $summary.source_slower_p50_runs 2 "slower P50 run count"
Assert-Equal $summary.source_slower_p99_runs 2 "slower P99 run count"

$largeSummary = Get-CflowPairedSourceSummary -Reports $reports `
  -Backend epoll -WaitMode blocking -PayloadBytes 1024 -ExpectedRuns 1
Assert-Equal $largeSummary.payload_bytes 1024 "large paired payload"
Assert-Equal $largeSummary.paired_p50_delta_ns 30.0 `
  "large paired P50 absolute delta"
Assert-Equal $largeSummary.paired_wall_delta_ns_per_exchange 8.0 `
  "large paired wall absolute delta per exchange"

$missingPair = @($reports | Select-Object -First 5)
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $missingPair `
    -Backend epoll -WaitMode blocking -PayloadBytes 64 -ExpectedRuns 3
} "missing Source pair"

$duplicatePair = @($reports + $reports[1])
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $duplicatePair `
    -Backend epoll -WaitMode blocking -PayloadBytes 64 -ExpectedRuns 3
} "duplicate Source pair"

$mismatchedAttempts = @($reports | ForEach-Object { $_.psobject.Copy() })
$mismatchedAttempts[1].attempted = 9
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $mismatchedAttempts `
    -Backend epoll -WaitMode blocking -PayloadBytes 64 -ExpectedRuns 3
} "mismatched pair attempt counts"

$baselineReports = @(
  [pscustomobject]@{ benchmark_run=1; comparison_backend="epoll"; payload_bytes=64; driver="direct"; attempted=10; exchanges_per_second=100; application_mib_per_second=10; p99_ns=100 },
  [pscustomobject]@{ benchmark_run=1; comparison_backend="epoll"; payload_bytes=64; driver="actor"; attempted=10; exchanges_per_second=80; application_mib_per_second=8; p99_ns=120 },
  [pscustomobject]@{ benchmark_run=2; comparison_backend="epoll"; payload_bytes=64; driver="direct"; attempted=10; exchanges_per_second=1000; application_mib_per_second=100; p99_ns=1000 },
  [pscustomobject]@{ benchmark_run=2; comparison_backend="epoll"; payload_bytes=64; driver="actor"; attempted=10; exchanges_per_second=1000; application_mib_per_second=100; p99_ns=900 },
  [pscustomobject]@{ benchmark_run=3; comparison_backend="epoll"; payload_bytes=64; driver="direct"; attempted=10; exchanges_per_second=1100; application_mib_per_second=110; p99_ns=1100 },
  [pscustomobject]@{ benchmark_run=3; comparison_backend="epoll"; payload_bytes=64; driver="actor"; attempted=10; exchanges_per_second=550; application_mib_per_second=55; p99_ns=1300 }
)
$baselineSummary = Get-CflowPairedDirectSummary -Reports $baselineReports `
  -Backend epoll -PayloadBytes 64 -ExpectedRuns 3
Assert-Equal $baselineSummary.runs 3 "direct baseline paired run count"
Assert-Equal $baselineSummary.direct_median_echo_per_second 1000.0 `
  "direct Echo/s median"
Assert-Equal $baselineSummary.actor_median_echo_per_second 550.0 `
  "Actor Echo/s median"
Assert-Equal $baselineSummary.paired_actor_direct_echo_ratio 0.8 `
  "paired Echo/s ratio"
Assert-Equal $baselineSummary.direct_median_application_mib_per_second 100.0 `
  "direct application throughput median"
Assert-Equal $baselineSummary.actor_median_application_mib_per_second 55.0 `
  "Actor application throughput median"
Assert-Equal $baselineSummary.paired_actor_direct_application_mib_ratio 0.8 `
  "paired application throughput ratio"
Assert-Equal $baselineSummary.direct_median_p99_ns 1000.0 "direct P99 median"
Assert-Equal $baselineSummary.actor_median_p99_ns 900.0 "Actor P99 median"
Assert-Equal $baselineSummary.paired_p99_delta_ns 20.0 "paired P99 delta"

$missingDirectPair = @($baselineReports | Select-Object -First 5)
Assert-Throws {
  Get-CflowPairedDirectSummary -Reports $missingDirectPair `
    -Backend epoll -PayloadBytes 64 -ExpectedRuns 3
} "missing direct baseline pair"

$mismatchedDirectAttempts = @($baselineReports | ForEach-Object { $_.psobject.Copy() })
$mismatchedDirectAttempts[1].attempted = 9
Assert-Throws {
  Get-CflowPairedDirectSummary -Reports $mismatchedDirectAttempts `
    -Backend epoll -PayloadBytes 64 -ExpectedRuns 3
} "mismatched direct baseline attempts"

$ioSourceOutput = @(
  ""
  "CFlow windowed IO source benchmarks"
  "      | benchmark | samples | ops/sample | bytes/sample | avg/op(ns) | avg/sample(us) | min/sample(us) | max/sample(us) | ops/s | MiB/s |"
  "      | window=1 values=4 | 2 | 4 | - | 500.000 | 2.000 | 1.500 | 2.500 | 2000000 | - |"
  'CFLOW_IO_SOURCE_BENCH_JSON {"schema":"cflow-io-source-benchmark/v1","capacity":1,"values_per_sample":4,"samples":2,"processed_values":12,"drive_calls":7,"driver_calls":6,"pending_drive_credit":1,"peak_occupied":1,"errors":0,"rejections":0,"stale_completions":0}'
  ""
)
$ioSourceReport = ConvertFrom-CflowIoSourceBenchmarkOutput `
  -Lines $ioSourceOutput -ExpectedCapacity 1 -ExpectedSamples 2 `
  -ExpectedValuesPerSample 4 -BenchmarkRun 1
Assert-Equal $ioSourceReport.schema "cflow-io-source-benchmark/v1" `
  "IO Source report schema"
Assert-Equal $ioSourceReport.benchmark_run 1 "IO Source benchmark run"
Assert-Equal $ioSourceReport.timed_values 8 "IO Source timed values"
Assert-Equal $ioSourceReport.processed_values 12 "IO Source processed values"
Assert-Equal $ioSourceReport.mean_ns_per_value 500.0 `
  "IO Source mean nanoseconds per value"
Assert-Equal $ioSourceReport.drive_calls_per_value 0.5833333333333334 `
  "IO Source drive calls per processed value"
Assert-Equal $ioSourceReport.pending_drive_credit 1 `
  "IO Source pending drive credit"

$legacyIoSourceOutput = @($ioSourceOutput | ForEach-Object {
    $_ -replace '"drive_calls":7,"driver_calls":6,"pending_drive_credit":1', `
      '"drive_calls":6,"driver_calls":6'
  })
$legacyIoSourceReport = ConvertFrom-CflowIoSourceBenchmarkOutput `
  -Lines $legacyIoSourceOutput -ExpectedCapacity 1 -ExpectedSamples 2 `
  -ExpectedValuesPerSample 4 -BenchmarkRun 1
Assert-Equal $legacyIoSourceReport.pending_drive_credit 0 `
  "legacy IO Source pending drive credit"

$invalidPendingDriveCredit = @($ioSourceOutput | ForEach-Object {
    $_ -replace '"pending_drive_credit":1', '"pending_drive_credit":2'
  })
Assert-Throws {
  ConvertFrom-CflowIoSourceBenchmarkOutput `
    -Lines $invalidPendingDriveCredit -ExpectedCapacity 1 -ExpectedSamples 2 `
    -ExpectedValuesPerSample 4 -BenchmarkRun 1
} "invalid pending IO Source drive credit"

$mismatchedDriverCalls = @($ioSourceOutput | ForEach-Object {
    $_ -replace '"driver_calls":6', '"driver_calls":5'
  })
Assert-Throws {
  ConvertFrom-CflowIoSourceBenchmarkOutput `
    -Lines $mismatchedDriverCalls -ExpectedCapacity 1 -ExpectedSamples 2 `
    -ExpectedValuesPerSample 4 -BenchmarkRun 1
} "mismatched IO Source drive and driver calls"

$secondIoSourceReport = $ioSourceReport.psobject.Copy()
$secondIoSourceReport.benchmark_run = 2
$secondIoSourceReport.mean_ns_per_value = 300.0
$secondIoSourceReport.drive_calls = 3
$secondIoSourceReport.driver_calls = 3
$secondIoSourceReport.pending_drive_credit = 0
$secondIoSourceReport.drive_calls_per_value = 0.25
$ioSourceSummary = Get-CflowIoSourceSummary `
  -Reports @($ioSourceReport, $secondIoSourceReport) -ExpectedRuns 2
Assert-Equal $ioSourceSummary.runs 2 "IO Source summary run count"
Assert-Equal $ioSourceSummary.median_mean_ns_per_value 400.0 `
  "IO Source median nanoseconds per value"
Assert-Equal $ioSourceSummary.median_drive_calls 5.0 `
  "IO Source median drive calls"
Assert-Equal $ioSourceSummary.median_drive_calls_per_value 0.4166666666666667 `
  "IO Source median drive calls per processed value"
Assert-Equal $ioSourceSummary.median_pending_drive_credit 0.5 `
  "IO Source median pending drive credit"

Assert-Throws {
  ConvertFrom-CflowIoSourceBenchmarkOutput `
    -Lines @($ioSourceOutput | Where-Object {
        $_ -notmatch '^CFLOW_IO_SOURCE_BENCH_JSON '
      }) `
    -ExpectedCapacity 1 -ExpectedSamples 2 `
    -ExpectedValuesPerSample 4 -BenchmarkRun 1
} "missing IO Source JSON record"
$wrongProcessedValues = @($ioSourceOutput | ForEach-Object {
    $_ -replace '"processed_values":12', '"processed_values":11'
  })
Assert-Throws {
  ConvertFrom-CflowIoSourceBenchmarkOutput `
    -Lines $wrongProcessedValues -ExpectedCapacity 1 -ExpectedSamples 2 `
    -ExpectedValuesPerSample 4 -BenchmarkRun 1
} "mismatched IO Source processed value count"
$failedIoSourceOutput = @($ioSourceOutput | ForEach-Object {
    $_ -replace '"errors":0', '"errors":1'
  })
Assert-Throws {
  ConvertFrom-CflowIoSourceBenchmarkOutput `
    -Lines $failedIoSourceOutput -ExpectedCapacity 1 -ExpectedSamples 2 `
    -ExpectedValuesPerSample 4 -BenchmarkRun 1
} "failed IO Source report"
Assert-Throws {
  Get-CflowIoSourceSummary `
    -Reports @($ioSourceReport, $ioSourceReport) -ExpectedRuns 2
} "duplicate IO Source benchmark run"

Write-Output "cflow benchmark stats tests passed"
