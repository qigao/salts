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
  (Get-CflowDriverOrder -Run 1 -WaitMode blocking -BackendIndex 0) `
  @("actor", "source") "blocking driver order"
Assert-Sequence `
  (Get-CflowDriverOrder -Run 1 -WaitMode busy -BackendIndex 0) `
  @("source", "actor") "busy driver order"
Assert-Sequence `
  (Get-CflowDriverOrder -Run 2 -WaitMode blocking -BackendIndex 0) `
  @("source", "actor") "next-run driver order"
Assert-Sequence `
  (Get-CflowDriverOrder -Run 1 -WaitMode blocking -BackendIndex 1) `
  @("source", "actor") "next-backend driver order"

$reports = @(
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; driver="actor"; wait_mode="blocking"; p50_ns=100; p99_ns=200; wall_ns=1000; process_cpu_ns=500; process_cpu_pct=50; application_mib_per_cpu_second=10; admission_mean_ns=10; completion_drive_mean_ns=90 },
  [pscustomobject]@{ benchmark_run=1; backend="epoll"; driver="source"; wait_mode="blocking"; p50_ns=110; p99_ns=240; wall_ns=1050; process_cpu_ns=550; process_cpu_pct=52; application_mib_per_cpu_second=9; admission_mean_ns=5; completion_drive_mean_ns=105 },
  [pscustomobject]@{ benchmark_run=2; backend="epoll"; driver="actor"; wait_mode="blocking"; p50_ns=1000; p99_ns=2000; wall_ns=2000; process_cpu_ns=1000; process_cpu_pct=51; application_mib_per_cpu_second=20; admission_mean_ns=20; completion_drive_mean_ns=180 },
  [pscustomobject]@{ benchmark_run=2; backend="epoll"; driver="source"; wait_mode="blocking"; p50_ns=1500; p99_ns=3000; wall_ns=2100; process_cpu_ns=1100; process_cpu_pct=53; application_mib_per_cpu_second=18; admission_mean_ns=10; completion_drive_mean_ns=210 },
  [pscustomobject]@{ benchmark_run=3; backend="epoll"; driver="actor"; wait_mode="blocking"; p50_ns=1100; p99_ns=2200; wall_ns=3000; process_cpu_ns=1500; process_cpu_pct=52; application_mib_per_cpu_second=30; admission_mean_ns=30; completion_drive_mean_ns=270 },
  [pscustomobject]@{ benchmark_run=3; backend="epoll"; driver="source"; wait_mode="blocking"; p50_ns=990; p99_ns=1980; wall_ns=3150; process_cpu_ns=1650; process_cpu_pct=54; application_mib_per_cpu_second=27; admission_mean_ns=15; completion_drive_mean_ns=315 }
)

$summary = Get-CflowPairedSourceSummary -Reports $reports `
  -Backend epoll -WaitMode blocking -ExpectedRuns 3
Assert-Equal $summary.runs 3 "paired run count"
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
Assert-Equal $summary.source_slower_p50_runs 2 "slower P50 run count"
Assert-Equal $summary.source_slower_p99_runs 2 "slower P99 run count"

$missingPair = @($reports | Select-Object -First 5)
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $missingPair `
    -Backend epoll -WaitMode blocking -ExpectedRuns 3
} "missing Source pair"

$duplicatePair = @($reports + $reports[1])
Assert-Throws {
  Get-CflowPairedSourceSummary -Reports $duplicatePair `
    -Backend epoll -WaitMode blocking -ExpectedRuns 3
} "duplicate Source pair"

Write-Output "cflow benchmark stats tests passed"
