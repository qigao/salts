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

function Assert-ThrowsLike([scriptblock]$Action, [string]$Pattern,
                           [string]$Message) {
  try {
    & $Action
  } catch {
    if ($_.Exception.Message -notlike $Pattern) {
      throw "$Message`: exception '$($_.Exception.Message)' did not match '$Pattern'"
    }
    return
  }
  throw "$Message`: expected an exception"
}

function Add-StageTimingFixture([object]$Report) {
  $operations = [int64]$Report.attempted
  $completionTotal = [int64]([double]$Report.completion_drive_mean_ns *
    $operations)
  $driveTotal = [int64]($completionTotal / 5)
  $waitTotal = [int64]($completionTotal / 2)
  $processTotal = [int64]($completionTotal / 10)
  $residualTotal = $completionTotal - $driveTotal - $waitTotal - $processTotal
  $fields = [ordered]@{
    stage_timing = $true
    stage_timing_version = 2
    io_operations = $operations
    admission_ns = [int64]([double]$Report.admission_mean_ns * $operations)
    completion_drive_ns = $completionTotal
    drive_ns = $driveTotal
    wait_ns = $waitTotal
    completion_process_ns = $processTotal
    completion_residual_ns = $residualTotal
    drive_mean_ns = [double]$driveTotal / $operations
    wait_mean_ns = [double]$waitTotal / $operations
    completion_process_mean_ns = [double]$processTotal / $operations
    completion_residual_mean_ns = [double]$residualTotal / $operations
  }
  foreach ($field in $fields.Keys) {
    $Report | Add-Member -NotePropertyName $field -NotePropertyValue $fields[$field]
  }
}

$helperPath = Join-Path (Split-Path -Parent $PSScriptRoot) `
  "scripts/cflow-benchmark-stats.ps1"
. $helperPath

Assert-Equal (Get-CflowMedian -Values @(3.0, 1.0, 2.0)) 2.0 `
  "odd median"
Assert-Equal (Get-CflowMedian -Values @(3.0, 1.0)) 2.0 `
  "even median"
Assert-Throws { Get-CflowMedian -Values @() } "empty median"

$validStageTiming = [pscustomobject]@{
  stage_timing=$true; stage_timing_version=2; io_operations=4
  admission_ns=20; completion_drive_ns=100
  drive_ns=20; wait_ns=50; completion_process_ns=10
  completion_residual_ns=20; admission_mean_ns=5.0
  completion_drive_mean_ns=25.0; drive_mean_ns=5.0; wait_mean_ns=12.5
  completion_process_mean_ns=2.5; completion_residual_mean_ns=5.0
}
Assert-CflowStageTimingReport -Report $validStageTiming
$binaryRoundingBoundary = [pscustomobject]@{
  stage_timing=$true; stage_timing_version=2; io_operations=12800
  admission_ns=1363943; completion_drive_ns=296307423
  drive_ns=96744140; wait_ns=189813324; completion_process_ns=1561191
  completion_residual_ns=8188768; admission_mean_ns=106.558
  completion_drive_mean_ns=23149.017; drive_mean_ns=7558.136
  wait_mean_ns=14829.166; completion_process_mean_ns=121.968
  completion_residual_mean_ns=639.747
}
Assert-CflowStageTimingReport -Report $binaryRoundingBoundary
$outsideThreeDecimalPrecision = $binaryRoundingBoundary.psobject.Copy()
$outsideThreeDecimalPrecision.completion_residual_mean_ns = 639.746
Assert-ThrowsLike {
  Assert-CflowStageTimingReport -Report $outsideThreeDecimalPrecision
} "*completion_residual_mean_ns*total=8188768*operations=12800*" `
  "stage mean outside three-decimal precision"
$invalidStageTiming = $validStageTiming.psobject.Copy()
$invalidStageTiming.completion_residual_ns = 19
Assert-Throws {
  Assert-CflowStageTimingReport -Report $invalidStageTiming
} "stage residual mismatch"
$negativeStageTiming = $validStageTiming.psobject.Copy()
$negativeStageTiming.drive_ns = -1
$negativeStageTiming.completion_residual_ns = 41
$negativeStageTiming.drive_mean_ns = -0.25
$negativeStageTiming.completion_residual_mean_ns = 10.25
Assert-Throws {
  Assert-CflowStageTimingReport -Report $negativeStageTiming
} "negative stage component"
$disabledStageTiming = [pscustomobject]@{
  stage_timing=$false; stage_timing_version=0; io_operations=0
  admission_ns=0; completion_drive_ns=0; drive_ns=0; wait_ns=0
  completion_process_ns=0; completion_residual_ns=0
  admission_mean_ns=0.0; completion_drive_mean_ns=0.0; drive_mean_ns=0.0
  wait_mean_ns=0.0; completion_process_mean_ns=0.0
  completion_residual_mean_ns=0.0
}
Assert-CflowStageTimingReport -Report $disabledStageTiming
$legacyStageTiming = [pscustomobject]@{
  stage_timing=$true; io_operations=4; admission_ns=20
  completion_drive_ns=100; admission_mean_ns=5.0
  completion_drive_mean_ns=25.0
}
Assert-CflowStageTimingReport -Report $legacyStageTiming

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
  (Get-CflowSourceWindowOrder -SourceWindows @(1, 4, 8) -Run 1 `
    -WaitMode blocking -BackendIndex 0 -PayloadIndex 0) `
  @("actor", "source-1", "source-4", "source-8") `
  "first Source window order"
Assert-Sequence `
  (Get-CflowSourceWindowOrder -SourceWindows @(1, 4, 8) -Run 2 `
    -WaitMode blocking -BackendIndex 0 -PayloadIndex 0) `
  @("source-1", "source-4", "source-8", "actor") `
  "next-run Source window order"
Assert-Sequence `
  (Get-CflowSourceWindowOrder -SourceWindows @(1, 4, 8) -Run 1 `
    -WaitMode busy -BackendIndex 0 -PayloadIndex 0) `
  @("source-1", "source-4", "source-8", "actor") `
  "busy Source window order"
Assert-Throws {
  Get-CflowSourceWindowOrder -SourceWindows @(1, 1) -Run 1 `
    -WaitMode blocking -BackendIndex 0 -PayloadIndex 0
} "duplicate Source windows"
Assert-Sequence `
  (Get-CflowBaselineDriverOrder -Run 1 -BackendIndex 0 -PayloadIndex 0) `
  @("direct", "actor") "first baseline driver order"
Assert-Sequence `
  (Get-CflowBaselineDriverOrder -Run 2 -BackendIndex 0 -PayloadIndex 0) `
  @("actor", "direct") "next-run baseline driver order"
Assert-Sequence `
  (Get-CflowBaselineDriverOrder -Run 1 -BackendIndex 1 -PayloadIndex 0) `
  @("actor", "direct") "next-backend baseline driver order"
Assert-Sequence `
  (Get-CflowPipelineDriverOrder -Run 1 -BackendIndex 0 -PayloadIndex 0 `
    -WindowIndex 0 -WaitMode blocking) `
  @("direct", "actor", "source") "first pipeline driver order"
Assert-Sequence `
  (Get-CflowPipelineDriverOrder -Run 2 -BackendIndex 0 -PayloadIndex 0 `
    -WindowIndex 0 -WaitMode blocking) `
  @("actor", "source", "direct") "next-run pipeline driver order"
Assert-Sequence `
  (Get-CflowPipelineDriverOrder -Run 1 -BackendIndex 0 -PayloadIndex 0 `
    -WindowIndex 0 -WaitMode busy) `
  @("actor", "source", "direct") "busy pipeline driver order"

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
$pairedMetricFixtures = @(
  @{ p95_ns=150; exchanges_per_second=100; application_mib_per_second=10 },
  @{ p95_ns=180; exchanges_per_second=90; application_mib_per_second=9 },
  @{ p95_ns=1500; exchanges_per_second=1000; application_mib_per_second=100 },
  @{ p95_ns=2400; exchanges_per_second=800; application_mib_per_second=80 },
  @{ p95_ns=1650; exchanges_per_second=1100; application_mib_per_second=110 },
  @{ p95_ns=1485; exchanges_per_second=990; application_mib_per_second=99 },
  @{ p95_ns=300; exchanges_per_second=200; application_mib_per_second=20 },
  @{ p95_ns=345; exchanges_per_second=180; application_mib_per_second=18 }
)
for ($index = 0; $index -lt $reports.Count; ++$index) {
  $reports[$index] | Add-Member -NotePropertyName protocol `
    -NotePropertyValue "tcp"
  foreach ($field in $pairedMetricFixtures[$index].Keys) {
    $reports[$index] | Add-Member -NotePropertyName $field `
      -NotePropertyValue $pairedMetricFixtures[$index][$field]
  }
  Add-StageTimingFixture $reports[$index]
}

$summary = Get-CflowPairedSourceSummary -Reports $reports `
  -Backend epoll -Protocol tcp -WaitMode blocking -PayloadBytes 64 `
  -ExpectedRuns 3
Assert-Equal $summary.runs 3 "paired run count"
Assert-Equal $summary.payload_bytes 64 "paired payload"
Assert-Equal $summary.actor_median_p50_ns 1000.0 "Actor P50 median"
Assert-Equal $summary.source_median_p50_ns 990.0 "Source P50 median"
Assert-Equal $summary.actor_median_p99_ns 2000.0 "Actor P99 median"
Assert-Equal $summary.source_median_p99_ns 1980.0 "Source P99 median"
Assert-Equal $summary.actor_median_p95_ns 1500.0 "Actor P95 median"
Assert-Equal $summary.source_median_p95_ns 1485.0 "Source P95 median"
Assert-Equal $summary.paired_p95_delta_pct 20.0 "paired P95 delta"
Assert-Equal $summary.paired_source_actor_echo_ratio 0.9 `
  "paired Source/Actor Echo ratio"
Assert-Equal $summary.paired_source_actor_application_mib_ratio 0.9 `
  "paired Source/Actor application throughput ratio"
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
Assert-Equal $summary.actor_median_drive_mean_ns 36.0 `
  "Actor drive median"
Assert-Equal $summary.source_median_drive_mean_ns 42.0 `
  "Source drive median"
Assert-Equal $summary.paired_drive_delta_ns 6.0 `
  "paired drive absolute delta"
Assert-Equal $summary.paired_wait_delta_ns 15.0 `
  "paired wait absolute delta"
Assert-Equal $summary.paired_completion_process_delta_ns 3.0 `
  "paired completion-process absolute delta"
Assert-Equal $summary.paired_completion_residual_delta_ns 6.0 `
  "paired completion-residual absolute delta"
Assert-Equal $summary.source_slower_p50_runs 2 "slower P50 run count"
Assert-Equal $summary.source_slower_p99_runs 2 "slower P99 run count"
$udpReports = @($reports | ForEach-Object {
    $copy = $_.psobject.Copy()
    $copy.protocol = "udp"
    $copy
  })
$mixedProtocolSummary = Get-CflowPairedSourceSummary `
  -Reports @($reports + $udpReports) -Backend epoll -Protocol tcp `
  -WaitMode blocking -PayloadBytes 64 -ExpectedRuns 3
Assert-Equal $mixedProtocolSummary.protocol "tcp" `
  "Source summary filters TCP from mixed protocol records"

$windowedReports = @()
foreach ($report in @($reports | Where-Object { $_.payload_bytes -eq 64 })) {
  $copy = $report.psobject.Copy()
  $copy | Add-Member -NotePropertyName source_window_capacity `
    -NotePropertyValue $(if ($copy.driver -eq "source") { 1 } else { 0 })
  $copy | Add-Member -NotePropertyName source_peak_occupied `
    -NotePropertyValue $(if ($copy.driver -eq "source") { 1 } else { 0 })
  $windowedReports += $copy
  if ($copy.driver -eq "source") {
    $windowFour = $copy.psobject.Copy()
    $windowFour.source_window_capacity = 4
    $windowFour.p50_ns = [double]$windowFour.p50_ns + 400.0
    $windowedReports += $windowFour
  }
}
$windowFourSummary = Get-CflowPairedSourceSummary `
  -Reports $windowedReports -Backend epoll -Protocol tcp -WaitMode blocking `
  -PayloadBytes 64 -SourceWindow 4 -ExpectedRuns 3
Assert-Equal $windowFourSummary.source_window 4 `
  "windowed Source summary capacity"
Assert-Equal $windowFourSummary.source_median_p50_ns 1390.0 `
  "windowed Source summary filters the requested capacity"
Assert-Equal $windowFourSummary.source_median_peak_occupied 1.0 `
  "windowed Source summary peak occupancy"

$pipelineReports = @(
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; workload="pipeline"; payload_bytes=64; driver="source"; wait_mode="blocking"; source_window_capacity=1; source_peak_occupied=1; attempted=10; p50_ns=100; p95_ns=150; p99_ns=200; exchanges_per_second=100; application_mib_per_second=10; wall_ns=1000; process_cpu_ns=500; process_cpu_pct=50; application_mib_per_cpu_second=20; admission_mean_ns=10; completion_drive_mean_ns=90 },
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; workload="pipeline"; payload_bytes=64; driver="source"; wait_mode="blocking"; source_window_capacity=4; source_peak_occupied=4; attempted=10; p50_ns=80; p95_ns=120; p99_ns=160; exchanges_per_second=200; application_mib_per_second=20; wall_ns=600; process_cpu_ns=450; process_cpu_pct=75; application_mib_per_cpu_second=30; admission_mean_ns=5; completion_drive_mean_ns=55 },
  [pscustomobject]@{ benchmark_run=2; backend="epoll"; workload="pipeline"; payload_bytes=64; driver="source"; wait_mode="blocking"; source_window_capacity=1; source_peak_occupied=1; attempted=10; p50_ns=1000; p95_ns=1500; p99_ns=2000; exchanges_per_second=1000; application_mib_per_second=100; wall_ns=2000; process_cpu_ns=1000; process_cpu_pct=50; application_mib_per_cpu_second=100; admission_mean_ns=20; completion_drive_mean_ns=180 },
  [pscustomobject]@{ benchmark_run=2; backend="epoll"; workload="pipeline"; payload_bytes=64; driver="source"; wait_mode="blocking"; source_window_capacity=4; source_peak_occupied=4; attempted=10; p50_ns=900; p95_ns=1200; p99_ns=1800; exchanges_per_second=1500; application_mib_per_second=150; wall_ns=1500; process_cpu_ns=900; process_cpu_pct=60; application_mib_per_cpu_second=150; admission_mean_ns=10; completion_drive_mean_ns=140 }
)
foreach ($report in $pipelineReports) {
  Add-StageTimingFixture $report
}
$pipelineSummary = Get-CflowPairedSourceWindowSummary `
  -Reports $pipelineReports -Backend epoll -WaitMode blocking `
  -PayloadBytes 64 -BaselineWindow 1 -SourceWindow 4 -ExpectedRuns 2
Assert-Equal $pipelineSummary.runs 2 "pipeline paired run count"
Assert-Equal $pipelineSummary.baseline_window 1 "pipeline baseline window"
Assert-Equal $pipelineSummary.source_window 4 "pipeline target window"
Assert-Equal $pipelineSummary.paired_echo_ratio 1.75 `
  "pipeline paired Echo/s ratio"
Assert-Equal $pipelineSummary.paired_application_mib_ratio 1.75 `
  "pipeline paired application throughput ratio"
Assert-Equal $pipelineSummary.paired_p50_delta_pct -15.0 `
  "pipeline paired P50 delta"
Assert-Equal $pipelineSummary.paired_p95_delta_pct -20.0 `
  "pipeline paired P95 delta"
Assert-Equal $pipelineSummary.paired_p99_delta_pct -15.0 `
  "pipeline paired P99 delta"
Assert-Equal $pipelineSummary.paired_cpu_time_delta_pct -10.0 `
  "pipeline paired CPU-time delta"
Assert-Equal $pipelineSummary.source_median_peak_occupied 4.0 `
  "pipeline target peak occupancy"
Assert-Equal $pipelineSummary.paired_drive_delta_ns -7.5 `
  "pipeline paired drive absolute delta"
Assert-Equal $pipelineSummary.paired_wait_delta_ns -18.75 `
  "pipeline paired wait absolute delta"
Assert-Equal $pipelineSummary.paired_completion_process_delta_ns -3.75 `
  "pipeline paired completion-process absolute delta"
Assert-Equal $pipelineSummary.paired_completion_residual_delta_ns -7.5 `
  "pipeline paired completion-residual absolute delta"
Assert-Throws {
  Get-CflowPairedSourceWindowSummary `
    -Reports @($pipelineReports | Select-Object -First 3) `
    -Backend epoll -WaitMode blocking -PayloadBytes 64 `
    -BaselineWindow 1 -SourceWindow 4 -ExpectedRuns 2
} "missing pipeline window pair"

$pipelineLayerReports = @(
  [pscustomobject]@{ benchmark_run=1; comparison_backend="epoll"; backend="socket"; protocol="udp"; workload="pipeline"; payload_bytes=64; driver="direct"; wait_mode="blocking"; workload_window_capacity=4; workload_peak_in_flight=4; attempted=10; p50_ns=50; p95_ns=80; p99_ns=100; exchanges_per_second=100; application_mib_per_second=10; process_cpu_ns=1000; process_cpu_pct=10 },
  [pscustomobject]@{ benchmark_run=1; comparison_backend="epoll"; backend="epoll"; protocol="udp"; workload="pipeline"; payload_bytes=64; driver="actor"; wait_mode="blocking"; workload_window_capacity=4; workload_peak_in_flight=4; attempted=10; p50_ns=70; p95_ns=100; p99_ns=130; exchanges_per_second=80; application_mib_per_second=8; process_cpu_ns=1200; process_cpu_pct=12 },
  [pscustomobject]@{ benchmark_run=1; comparison_backend="epoll"; backend="epoll"; protocol="udp"; workload="pipeline"; payload_bytes=64; driver="source"; wait_mode="blocking"; workload_window_capacity=4; workload_peak_in_flight=4; attempted=10; p50_ns=90; p95_ns=140; p99_ns=180; exchanges_per_second=60; application_mib_per_second=6; process_cpu_ns=1500; process_cpu_pct=15 },
  [pscustomobject]@{ benchmark_run=2; comparison_backend="epoll"; backend="socket"; protocol="udp"; workload="pipeline"; payload_bytes=64; driver="direct"; wait_mode="blocking"; workload_window_capacity=4; workload_peak_in_flight=4; attempted=10; p50_ns=500; p95_ns=800; p99_ns=1000; exchanges_per_second=1000; application_mib_per_second=100; process_cpu_ns=10000; process_cpu_pct=20 },
  [pscustomobject]@{ benchmark_run=2; comparison_backend="epoll"; backend="epoll"; protocol="udp"; workload="pipeline"; payload_bytes=64; driver="actor"; wait_mode="blocking"; workload_window_capacity=4; workload_peak_in_flight=4; attempted=10; p50_ns=450; p95_ns=700; p99_ns=900; exchanges_per_second=500; application_mib_per_second=50; process_cpu_ns=8000; process_cpu_pct=16 },
  [pscustomobject]@{ benchmark_run=2; comparison_backend="epoll"; backend="epoll"; protocol="udp"; workload="pipeline"; payload_bytes=64; driver="source"; wait_mode="blocking"; workload_window_capacity=4; workload_peak_in_flight=4; attempted=10; p50_ns=600; p95_ns=900; p99_ns=1200; exchanges_per_second=250; application_mib_per_second=25; process_cpu_ns=9000; process_cpu_pct=18 }
)
foreach ($report in $pipelineLayerReports) {
  $report | Add-Member schema "cflow-network-benchmark/v1"
  $report | Add-Member profile "throughput"
  $report | Add-Member peer_mode "raw"
  $report | Add-Member stage_timing $false
  $report | Add-Member samples 2
  $report | Add-Member exchanges_per_sample 5
  $report | Add-Member errors 0
  $report | Add-Member rejections 0
  $report | Add-Member stale_completions 0
}
$pipelineLayerSummary = Get-CflowPairedTransportSummary `
  -Reports $pipelineLayerReports -Backend epoll -Protocol udp `
  -WaitMode blocking `
  -PayloadBytes 64 -WindowCapacity 4 -ExpectedRuns 2
Assert-Equal $pipelineLayerSummary.runs 2 "pipeline layer paired run count"
Assert-Equal $pipelineLayerSummary.protocol "udp" `
  "pipeline layer protocol identity"
Assert-Equal $pipelineLayerSummary.window_capacity 4 "pipeline layer window"
Assert-Equal $pipelineLayerSummary.direct_median_p50_ns 275.0 `
  "pipeline Direct P50 median"
Assert-Equal $pipelineLayerSummary.actor_median_p95_ns 400.0 `
  "pipeline Actor P95 median"
Assert-Equal $pipelineLayerSummary.source_median_application_mib_per_second 15.5 `
  "pipeline Source application throughput median"
Assert-Equal $pipelineLayerSummary.paired_actor_direct_echo_ratio 0.65 `
  "pipeline Actor/direct Echo/s ratio"
Assert-Equal $pipelineLayerSummary.paired_source_actor_echo_ratio 0.625 `
  "pipeline Source/Actor Echo/s ratio"
Assert-Equal $pipelineLayerSummary.paired_source_direct_echo_ratio 0.425 `
  "pipeline Source/direct Echo/s ratio"
Assert-Equal $pipelineLayerSummary.paired_actor_direct_p99_ratio 1.1 `
  "pipeline Actor/direct P99 ratio"
Assert-Equal $pipelineLayerSummary.paired_source_actor_p99_ratio 1.358974 `
  "pipeline Source/Actor P99 ratio"
Assert-Equal $pipelineLayerSummary.paired_source_direct_p99_ratio 1.5 `
  "pipeline Source/direct P99 ratio"
Assert-Equal $pipelineLayerSummary.paired_actor_direct_cpu_time_ratio 1.0 `
  "pipeline Actor/direct CPU-time ratio"
Assert-Equal $pipelineLayerSummary.paired_source_actor_cpu_time_ratio 1.1875 `
  "pipeline Source/Actor CPU-time ratio"
Assert-Equal $pipelineLayerSummary.paired_source_direct_cpu_time_ratio 1.2 `
  "pipeline Source/direct CPU-time ratio"
$tcpLayerReports = @($pipelineLayerReports | ForEach-Object {
    $copy = $_.psobject.Copy()
    $copy.protocol = "tcp"
    $copy.workload = "round-trip"
    $copy.workload_window_capacity = 1
    $copy.workload_peak_in_flight = 1
    $copy
  })
$tcpLayerSummary = Get-CflowPairedTransportSummary `
  -Reports @($pipelineLayerReports + $tcpLayerReports) -Backend epoll `
  -Protocol tcp -WaitMode blocking -PayloadBytes 64 `
  -WindowCapacity 1 -ExpectedRuns 2
Assert-Equal $tcpLayerSummary.protocol "tcp" `
  "transport summary filters TCP from mixed protocol records"
Assert-Equal $tcpLayerSummary.workload "round-trip" `
  "TCP transport summary uses the byte-stream-safe workload"
Assert-Throws {
  Get-CflowPairedTransportSummary `
    -Reports @($pipelineLayerReports | Select-Object -First 5) `
    -Backend epoll -Protocol udp -WaitMode blocking -PayloadBytes 64 `
    -WindowCapacity 4 -ExpectedRuns 2
} "missing pipeline layer report"
$mismatchedPipelineLayerAttempts = @($pipelineLayerReports | ForEach-Object {
    $_.psobject.Copy()
  })
$mismatchedPipelineLayerAttempts[2].attempted = 9
Assert-Throws {
  Get-CflowPairedTransportSummary `
    -Reports $mismatchedPipelineLayerAttempts -Backend epoll -Protocol udp `
    -WaitMode blocking -PayloadBytes 64 -WindowCapacity 4 -ExpectedRuns 2
} "mismatched pipeline layer attempts"
$mismatchedPipelineLayerProfile = @($pipelineLayerReports | ForEach-Object {
    $_.psobject.Copy()
  })
$mismatchedPipelineLayerProfile[2].profile = "latency"
Assert-Throws {
  Get-CflowPairedTransportSummary `
    -Reports $mismatchedPipelineLayerProfile -Backend epoll -Protocol udp `
    -WaitMode blocking -PayloadBytes 64 -WindowCapacity 4 -ExpectedRuns 2
} "mismatched pipeline layer profile"
$failedPipelineLayer = @($pipelineLayerReports | ForEach-Object {
    $_.psobject.Copy()
  })
$failedPipelineLayer[1].errors = 1
Assert-Throws {
  Get-CflowPairedTransportSummary `
    -Reports $failedPipelineLayer -Backend epoll -Protocol udp `
    -WaitMode blocking `
    -PayloadBytes 64 -WindowCapacity 4 -ExpectedRuns 2
} "failed pipeline layer record"

$largeSummary = Get-CflowPairedSourceSummary -Reports $reports `
  -Backend epoll -Protocol tcp -WaitMode blocking -PayloadBytes 1024 `
  -ExpectedRuns 1
Assert-Equal $largeSummary.payload_bytes 1024 "large paired payload"
Assert-Equal $largeSummary.paired_p50_delta_ns 30.0 `
  "large paired P50 absolute delta"
Assert-Equal $largeSummary.paired_wall_delta_ns_per_exchange 8.0 `
  "large paired wall absolute delta per exchange"

$missingPair = @($reports | Select-Object -First 5)
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $missingPair `
    -Backend epoll -Protocol tcp -WaitMode blocking -PayloadBytes 64 `
    -ExpectedRuns 3
} "missing Source pair"

$duplicatePair = @($reports + $reports[1])
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $duplicatePair `
    -Backend epoll -Protocol tcp -WaitMode blocking -PayloadBytes 64 `
    -ExpectedRuns 3
} "duplicate Source pair"

$mismatchedAttempts = @($reports | ForEach-Object { $_.psobject.Copy() })
$mismatchedAttempts[1].attempted = 9
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $mismatchedAttempts `
    -Backend epoll -Protocol tcp -WaitMode blocking -PayloadBytes 64 `
    -ExpectedRuns 3
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

$ioModelOutput = @(
  "      | mock direct-control | 2 | 4 | - | 10.000 | 0.040 | 0.040 | 0.040 | 100000000 | - |"
  'CFLOW_IO_MODEL_BENCH_JSON {"schema":"cflow-io-model-benchmark/v1","model":"direct-control","capacity":4,"values_per_sample":4,"samples":2,"timed_values":8,"processed_values":12,"errors":0,"rejections":0,"stale_completions":0}'
  "      | mock Actor | 2 | 4 | - | 50.000 | 0.200 | 0.200 | 0.200 | 20000000 | - |"
  'CFLOW_IO_MODEL_BENCH_JSON {"schema":"cflow-io-model-benchmark/v1","model":"actor","capacity":4,"values_per_sample":4,"samples":2,"timed_values":8,"processed_values":12,"accepted":12,"acknowledged":12,"errors":0,"rejections":0,"stale_completions":0}'
  "      | mock IO Source adapter | 2 | 4 | - | 80.000 | 0.320 | 0.320 | 0.320 | 12500000 | - |"
  'CFLOW_IO_MODEL_BENCH_JSON {"schema":"cflow-io-model-benchmark/v1","model":"io-source-adapter","capacity":4,"values_per_sample":4,"samples":2,"timed_values":8,"processed_values":12,"accepted":12,"acknowledged":12,"drive_calls":3,"driver_calls":3,"peak_occupied":4,"errors":0,"rejections":0,"stale_completions":0}'
  "      | mock coroutine Source adapter | 2 | 4 | - | 110.000 | 0.440 | 0.440 | 0.440 | 9090909 | - |"
  'CFLOW_IO_MODEL_BENCH_JSON {"schema":"cflow-io-model-benchmark/v1","model":"coroutine-source-adapter","capacity":4,"values_per_sample":4,"samples":2,"timed_values":8,"processed_values":12,"accepted":12,"acknowledged":12,"drive_calls":3,"driver_calls":3,"peak_occupied":4,"added_worker_threads":0,"errors":0,"rejections":0,"stale_completions":0}'
  "      | mock Source runtime window=4 values=4 | 2 | 4 | - | 100.000 | 0.400 | 0.400 | 0.400 | 10000000 | - |"
  'CFLOW_IO_MODEL_BENCH_JSON {"schema":"cflow-io-model-benchmark/v1","model":"source-runtime","capacity":4,"values_per_sample":4,"samples":2,"timed_values":8,"processed_values":12,"accepted":12,"acknowledged":12,"drive_calls":3,"driver_calls":3,"peak_occupied":4,"errors":0,"rejections":0,"stale_completions":0}'
)
$ioModelReports = @(ConvertFrom-CflowIoModelBenchmarkOutput `
  -Lines $ioModelOutput -ExpectedCapacity 4 -ExpectedSamples 2 `
  -ExpectedValuesPerSample 4 -BenchmarkRun 1)
Assert-Equal $ioModelReports.Count 5 "IO model report count"
Assert-Equal $ioModelReports[0].model "direct-control" "direct model identity"
Assert-Equal $ioModelReports[1].model "actor" "Actor model identity"
Assert-Equal $ioModelReports[2].model "io-source-adapter" `
  "IO Source adapter model identity"
Assert-Equal $ioModelReports[3].model "coroutine-source-adapter" `
  "coroutine Source adapter model identity"
Assert-Equal $ioModelReports[4].model "source-runtime" `
  "Source runtime model identity"
Assert-Equal $ioModelReports[0].mean_ns_per_value 10.0 "direct model mean"
Assert-Equal $ioModelReports[1].mean_ns_per_value 50.0 "Actor model mean"
Assert-Equal $ioModelReports[2].mean_ns_per_value 80.0 `
  "IO Source adapter model mean"
Assert-Equal $ioModelReports[3].mean_ns_per_value 110.0 `
  "coroutine Source adapter model mean"
Assert-Equal $ioModelReports[3].added_worker_threads 0 `
  "coroutine Source adapter added worker count"
Assert-Equal $ioModelReports[4].mean_ns_per_value 100.0 `
  "Source runtime model mean"
$secondIoModelReports = @($ioModelReports | ForEach-Object {
    $_.psobject.Copy()
  })
foreach ($report in $secondIoModelReports) { $report.benchmark_run = 2 }
$secondIoModelReports[0].mean_ns_per_value = 20.0
$secondIoModelReports[1].mean_ns_per_value = 80.0
$secondIoModelReports[2].mean_ns_per_value = 100.0
$secondIoModelReports[3].mean_ns_per_value = 140.0
$secondIoModelReports[4].mean_ns_per_value = 120.0
$ioModelSummary = Get-CflowPairedIoModelSummary `
  -Reports @($ioModelReports + $secondIoModelReports) `
  -Capacity 4 -ExpectedRuns 2
Assert-Equal $ioModelSummary.paired_actor_direct_cost_ratio 4.5 `
  "mock Actor/direct cost ratio"
Assert-Equal $ioModelSummary.paired_adapter_actor_cost_ratio 1.425 `
  "mock IO Source adapter/Actor cost ratio"
Assert-Equal $ioModelSummary.paired_runtime_adapter_cost_ratio 1.225 `
  "mock Source runtime/adapter cost ratio"
Assert-Equal $ioModelSummary.paired_coroutine_adapter_cost_ratio 1.3875 `
  "mock coroutine Source adapter/adapter cost ratio"
Assert-Equal $ioModelSummary.paired_actor_direct_delta_ns 50.0 `
  "mock Actor/direct absolute cost"
Assert-Equal $ioModelSummary.paired_adapter_actor_delta_ns 25.0 `
  "mock IO Source adapter/Actor absolute cost"
Assert-Equal $ioModelSummary.paired_runtime_adapter_delta_ns 20.0 `
  "mock Source runtime/adapter absolute cost"
Assert-Equal $ioModelSummary.paired_coroutine_adapter_delta_ns 35.0 `
  "mock coroutine Source adapter/adapter absolute cost"
Assert-Throws {
  Get-CflowPairedIoModelSummary `
    -Reports @($ioModelReports + $secondIoModelReports | Select-Object -First 9) `
    -Capacity 4 -ExpectedRuns 2
} "missing mock model pair"
$invalidIoModelLifecycle = @($ioModelReports | ForEach-Object {
    $_.psobject.Copy()
  })
$invalidIoModelLifecycle[1].acknowledged = 11
Assert-Throws {
  Get-CflowPairedIoModelSummary -Reports $invalidIoModelLifecycle `
    -Capacity 4 -ExpectedRuns 1
} "invalid mock Actor lifecycle counts"
$invalidIoModelDrive = @($ioModelReports | ForEach-Object {
    $_.psobject.Copy()
  })
$invalidIoModelDrive[2].drive_calls = 0
Assert-Throws {
  Get-CflowPairedIoModelSummary -Reports $invalidIoModelDrive `
    -Capacity 4 -ExpectedRuns 1
} "invalid mock IO Source adapter drive count"
$invalidCoroutineThreadCount = @($ioModelReports | ForEach-Object {
    $_.psobject.Copy()
  })
$invalidCoroutineThreadCount[3].added_worker_threads = 1
Assert-Throws {
  Get-CflowPairedIoModelSummary -Reports $invalidCoroutineThreadCount `
    -Capacity 4 -ExpectedRuns 1
} "invalid mock coroutine Source adapter worker count"

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

$transportMatrix = Get-CflowTransportBenchmarkMatrix
$transportCases = @(Get-CflowTransportBenchmarkCases -Matrix $transportMatrix)
Assert-Equal $transportCases.Count 17 "transport benchmark case count"
Assert-Equal (Get-CflowTransportSampleCount -PayloadBytes 1024 `
    -ExchangesPerSample 64 -MaximumSamples 100 `
    -TargetApplicationBytes 67108864) 100 `
  "small payload transport sample cap"
Assert-Equal (Get-CflowTransportSampleCount -PayloadBytes 16384 `
    -ExchangesPerSample 64 -MaximumSamples 100 `
    -TargetApplicationBytes 67108864) 64 `
  "16 KiB byte-bounded transport samples"
Assert-Equal (Get-CflowTransportSampleCount -PayloadBytes 32768 `
    -ExchangesPerSample 64 -MaximumSamples 100 `
    -TargetApplicationBytes 67108864) 32 `
  "32 KiB byte-bounded transport samples"
Assert-Equal (Get-CflowTransportSampleCount -PayloadBytes 65507 `
    -ExchangesPerSample 64 -MaximumSamples 100 `
    -TargetApplicationBytes 67108864) 16 `
  "maximum UDP datagram byte-bounded transport samples"
Assert-Sequence `
  @($transportCases | Where-Object {
      $_.protocol -eq "udp" -and $_.payload_bytes -eq 32768
    } | ForEach-Object { $_.window_capacity }) `
  @("1", "2") "UDP 32 KiB in-flight-byte-bounded windows"
Assert-Sequence `
  @($transportCases | Where-Object {
      $_.protocol -eq "udp" -and $_.payload_bytes -eq 65507
    } | ForEach-Object { $_.window_capacity }) `
  @("1") "UDP maximum datagram boundary window"
Assert-ThrowsLike {
  Get-CflowTransportBenchmarkCases -Matrix @{
    tcp = $transportMatrix.tcp
    udp = [pscustomobject]@{
      payload_bytes = [int64[]]@(1024, 65536)
      throughput_window = [int64]8
      maximum_payload_bytes = [int64]65507
      maximum_in_flight_bytes = [int64]65536
      boundary_payload_bytes = [int64[]]@(65536)
    }
  }
} "*UDP payload 65536 exceeds maximum 65507*" `
  "invalid UDP payload matrix"

$transportComparisons = @($transportCases | ForEach-Object {
    [pscustomobject]@{
      backend = "io_uring"
      protocol = $_.protocol
      payload_bytes = $_.payload_bytes
      window_capacity = $_.window_capacity
      direct_median_p50_ns = 10000.0
      direct_median_p99_ns = 20000.0
      direct_median_echo_per_second = 1000.0
      direct_median_application_mib_per_second = 10.0
      actor_median_p50_ns = 30000.0
      actor_median_p99_ns = 40000.0
      actor_median_echo_per_second = 800.0
      actor_median_application_mib_per_second = 8.0
      source_median_p50_ns = 50000.0
      source_median_p99_ns = 60000.0
      source_median_echo_per_second = 700.0
      source_median_application_mib_per_second = 7.0
    }
  })
$transportReports = Format-CflowTransportReports `
  -Comparisons $transportComparisons -Backends @("io_uring") `
  -Matrix $transportMatrix
Assert-Equal ($transportReports.latency -match
    '\| 32 KiB \| Direct \| 10\.000 \| 20\.000 \|') $true `
  "UDP 32 KiB latency row"
Assert-Equal ($transportReports.latency -match
    '\| 65,507 B \(max datagram\) \| Direct \| 10\.000 \| 20\.000 \|') `
  $true "UDP maximum datagram latency row"
Assert-Equal ($transportReports.throughput -match
    '\| 32 KiB \| Direct \| 2 \| 1,000\.000 \| 10\.000 \|') $true `
  "UDP 32 KiB throughput row"
Assert-Equal ($transportReports.throughput -match
    '\| 65,507 B \(max datagram\) \| Direct \| 1 \| 1,000\.000 \| 10\.000 \|') `
  $true "UDP maximum datagram throughput boundary row"
Assert-Equal ($transportReports.latency -match 'N/A') $false `
  "transport latency contains no placeholder rows"
Assert-Equal ($transportReports.throughput -match 'N/A') $false `
  "transport throughput contains no placeholder rows"
Assert-Equal ($transportReports.latency -match 'P50/P99') $false `
  "transport latency uses atomic metric columns"
Assert-Equal ($transportReports.throughput -match 'Echo/s, MiB/s') $false `
  "transport throughput uses atomic metric columns"

Write-Output "cflow benchmark stats tests passed"
