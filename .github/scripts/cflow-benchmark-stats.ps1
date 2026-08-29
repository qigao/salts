function Get-CflowMedian {
  param(
    [Parameter(Mandatory = $true)]
    [double[]]$Values
  )

  if ($Values.Count -eq 0) {
    throw "Cannot calculate a median from an empty value set"
  }
  $ordered = @($Values | Sort-Object)
  $middle = [int][math]::Floor($ordered.Count / 2)
  if (($ordered.Count % 2) -eq 1) {
    return [double]$ordered[$middle]
  }
  return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}

function Get-CflowRotatedOrder {
  param(
    [Parameter(Mandatory = $true)]
    [string[]]$Values,

    [Parameter(Mandatory = $true)]
    [int]$Run
  )

  if ($Values.Count -eq 0) {
    throw "Cannot rotate an empty value set"
  }
  if ($Run -le 0) {
    throw "Run must be positive"
  }
  $start = ($Run - 1) % $Values.Count
  $ordered = @()
  for ($offset = 0; $offset -lt $Values.Count; ++$offset) {
    $ordered += $Values[($start + $offset) % $Values.Count]
  }
  return $ordered
}

function Get-CflowDriverOrder {
  param(
    [Parameter(Mandatory = $true)]
    [int]$Run,

    [Parameter(Mandatory = $true)]
    [ValidateSet("blocking", "busy")]
    [string]$WaitMode,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$BackendIndex,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$PayloadIndex
  )

  if ($Run -le 0) {
    throw "Run must be positive"
  }
  $waitOffset = if ($WaitMode -eq "busy") { 1 } else { 0 }
  $sourceFirst = (($Run - 1 + $BackendIndex + $PayloadIndex + $waitOffset) % 2) -eq 1
  if ($sourceFirst) {
    return @("source", "actor")
  }
  return @("actor", "source")
}

function Get-CflowSourceWindowOrder {
  param(
    [Parameter(Mandatory = $true)]
    [long[]]$SourceWindows,

    [Parameter(Mandatory = $true)]
    [int]$Run,

    [Parameter(Mandatory = $true)]
    [ValidateSet("blocking", "busy")]
    [string]$WaitMode,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$BackendIndex,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$PayloadIndex
  )

  if ($Run -le 0) {
    throw "Run must be positive"
  }
  if ($SourceWindows.Count -eq 0 -or
      @($SourceWindows | Where-Object { $_ -le 0 }).Count -ne 0 -or
      @($SourceWindows | Sort-Object -Unique).Count -ne $SourceWindows.Count) {
    throw "SourceWindows must contain unique positive capacities"
  }
  [string[]]$values = @("actor") + @(
    $SourceWindows | ForEach-Object { "source-$_" })
  $waitOffset = if ($WaitMode -eq "busy") { 1 } else { 0 }
  $rotation = 1 + (($Run - 1 + $BackendIndex + $PayloadIndex +
        $waitOffset) % $values.Count)
  return Get-CflowRotatedOrder -Values $values -Run $rotation
}

function Get-CflowBaselineDriverOrder {
  param(
    [Parameter(Mandatory = $true)]
    [int]$Run,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$BackendIndex,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$PayloadIndex
  )

  if ($Run -le 0) {
    throw "Run must be positive"
  }
  $directFirst = (($Run - 1 + $BackendIndex + $PayloadIndex) % 2) -eq 0
  if ($directFirst) {
    return @("direct", "actor")
  }
  return @("actor", "direct")
}

function Get-CflowPercentDelta {
  param(
    [Parameter(Mandatory = $true)]
    [double]$Baseline,

    [Parameter(Mandatory = $true)]
    [double]$Candidate,

    [Parameter(Mandatory = $true)]
    [string]$Metric
  )

  if ([double]::IsNaN($Baseline) -or
      [double]::IsInfinity($Baseline) -or
      $Baseline -le 0.0 -or
      [double]::IsNaN($Candidate) -or
      [double]::IsInfinity($Candidate) -or
      $Candidate -le 0.0) {
    throw "$Metric values must be finite and positive"
  }
  return 100.0 * ($Candidate - $Baseline) / $Baseline
}

function ConvertFrom-CflowIoSourceBenchmarkOutput {
  param(
    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string[]]$Lines,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$ExpectedCapacity,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$ExpectedSamples,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$ExpectedValuesPerSample,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [int]::MaxValue)]
    [int]$BenchmarkRun
  )

  $tableRecords = @()
  $tablePattern =
    '^\s*\|\s*window=(?<capacity>[0-9]+)\s+values=(?<values>[0-9]+)\s*\|' +
    '\s*(?<samples>[0-9]+)\s*\|\s*(?<operations>[0-9]+)\s*\|' +
    '\s*[^|]+\|\s*(?<mean>[0-9]+(?:\.[0-9]+)?)\s*\|'
  foreach ($line in $Lines) {
    if ($line -match $tablePattern) {
      $tableRecords += [pscustomobject]@{
        capacity = [int64]$Matches.capacity
        values_per_sample = [int64]$Matches.values
        samples = [int64]$Matches.samples
        operations_per_sample = [int64]$Matches.operations
        mean_ns_per_value = [double]$Matches.mean
      }
    }
  }
  if ($tableRecords.Count -ne 1) {
    throw "Expected exactly one IO Source benchmark table record, found $($tableRecords.Count)"
  }

  $jsonLines = @($Lines | Where-Object {
      $_ -match '^CFLOW_IO_SOURCE_BENCH_JSON '
    })
  if ($jsonLines.Count -ne 1) {
    throw "Expected exactly one IO Source JSON record, found $($jsonLines.Count)"
  }
  $jsonText = $jsonLines[0].Substring("CFLOW_IO_SOURCE_BENCH_JSON ".Length)
  try {
    $report = $jsonText | ConvertFrom-Json
  } catch {
    throw "Invalid IO Source JSON record: $jsonText"
  }
  foreach ($field in @(
      "schema", "capacity", "values_per_sample", "samples",
      "processed_values", "drive_calls", "driver_calls", "peak_occupied",
      "errors", "rejections", "stale_completions")) {
    if ($null -eq $report.$field) {
      throw "Missing IO Source report field '$field': $jsonText"
    }
  }

  if ($ExpectedSamples -ge [long]::MaxValue) {
    throw "IO Source expected value count overflows int64"
  }
  $maximumValuesPerSample = [decimal]::Floor(
    ([decimal]([long]::MaxValue)) /
      ([decimal]$ExpectedSamples + [decimal]1))
  if ([decimal]$ExpectedValuesPerSample -gt $maximumValuesPerSample) {
    throw "IO Source expected value count overflows int64"
  }
  $timedValues = [long](
    [decimal]$ExpectedSamples * [decimal]$ExpectedValuesPerSample)
  $processedValues = [long](
    ([decimal]$ExpectedSamples + [decimal]1) *
      [decimal]$ExpectedValuesPerSample)
  $table = $tableRecords[0]
  $meanNs = [double]$table.mean_ns_per_value
  $driveCalls = [int64]$report.drive_calls
  $driverCalls = [int64]$report.driver_calls
  $pendingDriveCredit = if ($null -eq $report.pending_drive_credit) {
    [int64]0
  } else {
    [int64]$report.pending_drive_credit
  }
  if ($report.schema -ne "cflow-io-source-benchmark/v1" -or
      [int64]$report.capacity -ne $ExpectedCapacity -or
      [int64]$report.values_per_sample -ne $ExpectedValuesPerSample -or
      [int64]$report.samples -ne $ExpectedSamples -or
      [int64]$report.processed_values -ne $processedValues -or
      [int64]$table.capacity -ne $ExpectedCapacity -or
      [int64]$table.values_per_sample -ne $ExpectedValuesPerSample -or
      [int64]$table.samples -ne $ExpectedSamples -or
      [int64]$table.operations_per_sample -ne $ExpectedValuesPerSample -or
      [double]::IsNaN($meanNs) -or [double]::IsInfinity($meanNs) -or
      $meanNs -le 0.0 -or $driveCalls -le 0 -or
      $driverCalls -le 0 -or
      ($pendingDriveCredit -ne 0 -and $pendingDriveCredit -ne 1) -or
      $driverCalls -ne ($driveCalls - $pendingDriveCredit) -or
      [int64]$report.peak_occupied -le 0 -or
      [int64]$report.peak_occupied -gt $ExpectedCapacity -or
      [int64]$report.errors -ne 0 -or
      [int64]$report.rejections -ne 0 -or
      [int64]$report.stale_completions -ne 0) {
    throw "Invalid or unsuccessful IO Source benchmark report: $jsonText"
  }

  return [pscustomobject][ordered]@{
    schema = "cflow-io-source-benchmark/v1"
    benchmark_run = $BenchmarkRun
    capacity = $ExpectedCapacity
    values_per_sample = $ExpectedValuesPerSample
    samples = $ExpectedSamples
    timed_values = $timedValues
    processed_values = $processedValues
    mean_ns_per_value = $meanNs
    drive_calls = $driveCalls
    driver_calls = $driverCalls
    pending_drive_credit = $pendingDriveCredit
    drive_calls_per_value = [double]$driveCalls / [double]$processedValues
    peak_occupied = [int64]$report.peak_occupied
    errors = [int64]$report.errors
    rejections = [int64]$report.rejections
    stale_completions = [int64]$report.stale_completions
  }
}

function Get-CflowIoSourceSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Reports,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  if ($Reports.Count -ne $ExpectedRuns) {
    throw "Expected $ExpectedRuns IO Source reports, found $($Reports.Count)"
  }
  $first = $Reports[0]
  for ($run = 1; $run -le $ExpectedRuns; ++$run) {
    $matching = @($Reports | Where-Object {
        [int]$_.benchmark_run -eq $run
      })
    if ($matching.Count -ne 1) {
      throw "Expected one IO Source report for run $run, found $($matching.Count)"
    }
    $report = $matching[0]
    if ($report.schema -ne "cflow-io-source-benchmark/v1" -or
        [int64]$report.capacity -ne [int64]$first.capacity -or
        [int64]$report.values_per_sample -ne
          [int64]$first.values_per_sample -or
        [int64]$report.samples -ne [int64]$first.samples -or
        [int64]$report.timed_values -ne [int64]$first.timed_values -or
        [int64]$report.processed_values -ne
          [int64]$first.processed_values -or
        [double]$report.mean_ns_per_value -le 0.0 -or
        [int64]$report.drive_calls -le 0 -or
        [int64]$report.driver_calls -le 0 -or
        ([int64]$report.pending_drive_credit -ne 0 -and
         [int64]$report.pending_drive_credit -ne 1) -or
        [int64]$report.driver_calls -ne
          ([int64]$report.drive_calls -
            [int64]$report.pending_drive_credit) -or
        [double]$report.drive_calls_per_value -le 0.0 -or
        [int64]$report.errors -ne 0 -or
        [int64]$report.rejections -ne 0 -or
        [int64]$report.stale_completions -ne 0) {
      throw "Invalid or incompatible IO Source report for run $run"
    }
  }

  return [pscustomobject][ordered]@{
    capacity = [int64]$first.capacity
    values_per_sample = [int64]$first.values_per_sample
    samples = [int64]$first.samples
    runs = $ExpectedRuns
    timed_values_per_run = [int64]$first.timed_values
    processed_values_per_run = [int64]$first.processed_values
    median_mean_ns_per_value = Get-CflowMedian @($Reports.mean_ns_per_value)
    median_drive_calls = Get-CflowMedian @($Reports.drive_calls)
    median_driver_calls = Get-CflowMedian @($Reports.driver_calls)
    median_pending_drive_credit =
      Get-CflowMedian @($Reports.pending_drive_credit)
    median_drive_calls_per_value =
      Get-CflowMedian @($Reports.drive_calls_per_value)
    median_peak_occupied = Get-CflowMedian @($Reports.peak_occupied)
  }
}

function Get-CflowPairedSourceSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Reports,

    [Parameter(Mandatory = $true)]
    [string]$Backend,

    [Parameter(Mandatory = $true)]
    [ValidateSet("blocking", "busy")]
    [string]$WaitMode,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$PayloadBytes,

    [ValidateRange(1, [long]::MaxValue)]
    [long]$SourceWindow = 1,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  $matching = @($Reports | Where-Object {
      $_.backend -eq $Backend -and
      [int64]$_.payload_bytes -eq $PayloadBytes -and
      $_.wait_mode -eq $WaitMode -and
      ($_.driver -eq "actor" -or
       ($_.driver -eq "source" -and
        [int64]$(if ($null -eq $_.source_window_capacity) {
            1
          } else {
            $_.source_window_capacity
          }) -eq $SourceWindow))
    })
  if ($matching.Count -ne 2 * $ExpectedRuns) {
    throw "Expected $ExpectedRuns Actor/Source pairs for $Backend/$PayloadBytes/$WaitMode, found $($matching.Count) records"
  }

  $actors = @()
  $sources = @()
  $p50Deltas = @()
  $p95Deltas = @()
  $p99Deltas = @()
  $echoRatios = @()
  $applicationThroughputRatios = @()
  $wallDeltas = @()
  $cpuTimeDeltas = @()
  $cpuEfficiencyDeltas = @()
  $combinedStageDeltas = @()
  $p50AbsoluteDeltas = @()
  $p95AbsoluteDeltas = @()
  $p99AbsoluteDeltas = @()
  $wallAbsoluteDeltasPerExchange = @()
  $cpuTimeAbsoluteDeltasPerExchange = @()
  $combinedStageAbsoluteDeltas = @()

  for ($run = 1; $run -le $ExpectedRuns; ++$run) {
    $actor = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "actor"
      })
    $source = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "source"
      })
    if ($actor.Count -ne 1 -or $source.Count -ne 1) {
      throw "Expected one Actor and one Source report for $Backend/$PayloadBytes/$WaitMode run $run, found $($actor.Count) and $($source.Count)"
    }

    $actor = $actor[0]
    $source = $source[0]
    $actorAttempts = [int64]$actor.attempted
    $sourceAttempts = [int64]$source.attempted
    if ($actorAttempts -le 0 -or $sourceAttempts -ne $actorAttempts) {
      throw "Actor and Source attempted counts must be equal and positive for $Backend/$PayloadBytes/$WaitMode run $run"
    }
    $actorCombinedStage =
      [double]$actor.admission_mean_ns + [double]$actor.completion_drive_mean_ns
    $sourceCombinedStage =
      [double]$source.admission_mean_ns + [double]$source.completion_drive_mean_ns

    $p50Deltas += Get-CflowPercentDelta $actor.p50_ns $source.p50_ns "P50"
    $p95Deltas += Get-CflowPercentDelta $actor.p95_ns $source.p95_ns "P95"
    $p99Deltas += Get-CflowPercentDelta $actor.p99_ns $source.p99_ns "P99"
    $echoRatios += [double]$source.exchanges_per_second /
      [double]$actor.exchanges_per_second
    $applicationThroughputRatios +=
      [double]$source.application_mib_per_second /
      [double]$actor.application_mib_per_second
    $wallDeltas += Get-CflowPercentDelta $actor.wall_ns $source.wall_ns "wall"
    $cpuTimeDeltas += Get-CflowPercentDelta `
      $actor.process_cpu_ns $source.process_cpu_ns "CPU time"
    $cpuEfficiencyDeltas += Get-CflowPercentDelta `
      $actor.application_mib_per_cpu_second `
      $source.application_mib_per_cpu_second "CPU efficiency"
    $combinedStageDeltas += Get-CflowPercentDelta `
      $actorCombinedStage $sourceCombinedStage "combined stage"
    $p50AbsoluteDeltas += [double]$source.p50_ns - [double]$actor.p50_ns
    $p95AbsoluteDeltas += [double]$source.p95_ns - [double]$actor.p95_ns
    $p99AbsoluteDeltas += [double]$source.p99_ns - [double]$actor.p99_ns
    $wallAbsoluteDeltasPerExchange +=
      ([double]$source.wall_ns - [double]$actor.wall_ns) / $actorAttempts
    $cpuTimeAbsoluteDeltasPerExchange +=
      ([double]$source.process_cpu_ns - [double]$actor.process_cpu_ns) / $actorAttempts
    $combinedStageAbsoluteDeltas += $sourceCombinedStage - $actorCombinedStage
    $actors += $actor
    $sources += $source
  }

  return [pscustomobject][ordered]@{
    backend = $Backend
    payload_bytes = $PayloadBytes
    wait_mode = $WaitMode
    source_window = $SourceWindow
    runs = $ExpectedRuns
    actor_median_p50_ns = Get-CflowMedian @($actors.p50_ns)
    source_median_p50_ns = Get-CflowMedian @($sources.p50_ns)
    actor_median_p95_ns = Get-CflowMedian @($actors.p95_ns)
    source_median_p95_ns = Get-CflowMedian @($sources.p95_ns)
    actor_median_p99_ns = Get-CflowMedian @($actors.p99_ns)
    source_median_p99_ns = Get-CflowMedian @($sources.p99_ns)
    actor_median_echo_per_second = Get-CflowMedian @($actors.exchanges_per_second)
    source_median_echo_per_second = Get-CflowMedian @($sources.exchanges_per_second)
    paired_source_actor_echo_ratio =
      [math]::Round((Get-CflowMedian $echoRatios), 6)
    actor_median_application_mib_per_second =
      Get-CflowMedian @($actors.application_mib_per_second)
    source_median_application_mib_per_second =
      Get-CflowMedian @($sources.application_mib_per_second)
    paired_source_actor_application_mib_ratio =
      [math]::Round((Get-CflowMedian $applicationThroughputRatios), 6)
    paired_p50_delta_pct = [math]::Round((Get-CflowMedian $p50Deltas), 6)
    paired_p95_delta_pct = [math]::Round((Get-CflowMedian $p95Deltas), 6)
    paired_p99_delta_pct = [math]::Round((Get-CflowMedian $p99Deltas), 6)
    paired_wall_delta_pct = [math]::Round((Get-CflowMedian $wallDeltas), 6)
    paired_cpu_time_delta_pct =
      [math]::Round((Get-CflowMedian $cpuTimeDeltas), 6)
    paired_cpu_efficiency_delta_pct =
      [math]::Round((Get-CflowMedian $cpuEfficiencyDeltas), 6)
    paired_combined_stage_delta_pct =
      [math]::Round((Get-CflowMedian $combinedStageDeltas), 6)
    paired_p50_delta_ns = [math]::Round((Get-CflowMedian $p50AbsoluteDeltas), 6)
    paired_p95_delta_ns = [math]::Round((Get-CflowMedian $p95AbsoluteDeltas), 6)
    paired_p99_delta_ns = [math]::Round((Get-CflowMedian $p99AbsoluteDeltas), 6)
    paired_wall_delta_ns_per_exchange =
      [math]::Round((Get-CflowMedian $wallAbsoluteDeltasPerExchange), 6)
    paired_cpu_time_delta_ns_per_exchange =
      [math]::Round((Get-CflowMedian $cpuTimeAbsoluteDeltasPerExchange), 6)
    paired_combined_stage_delta_ns =
      [math]::Round((Get-CflowMedian $combinedStageAbsoluteDeltas), 6)
    source_slower_p50_runs = @($p50Deltas | Where-Object { $_ -gt 0.0 }).Count
    source_slower_p99_runs = @($p99Deltas | Where-Object { $_ -gt 0.0 }).Count
    actor_median_cpu_pct = Get-CflowMedian @($actors.process_cpu_pct)
    source_median_cpu_pct = Get-CflowMedian @($sources.process_cpu_pct)
    source_median_peak_occupied = Get-CflowMedian @(
      $sources | ForEach-Object {
        if ($null -eq $_.source_peak_occupied) { 1.0 }
        else { [double]$_.source_peak_occupied }
      })
    actor_median_admission_mean_ns = Get-CflowMedian @($actors.admission_mean_ns)
    source_median_admission_mean_ns = Get-CflowMedian @($sources.admission_mean_ns)
    actor_median_completion_drive_mean_ns =
      Get-CflowMedian @($actors.completion_drive_mean_ns)
    source_median_completion_drive_mean_ns =
      Get-CflowMedian @($sources.completion_drive_mean_ns)
    actor_median_combined_stage_mean_ns = Get-CflowMedian @(
      $actors | ForEach-Object {
        [double]$_.admission_mean_ns + [double]$_.completion_drive_mean_ns
      })
    source_median_combined_stage_mean_ns = Get-CflowMedian @(
      $sources | ForEach-Object {
        [double]$_.admission_mean_ns + [double]$_.completion_drive_mean_ns
      })
  }
}

function Get-CflowPairedSourceWindowSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Reports,

    [Parameter(Mandatory = $true)]
    [string]$Backend,

    [Parameter(Mandatory = $true)]
    [ValidateSet("blocking", "busy")]
    [string]$WaitMode,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$PayloadBytes,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$BaselineWindow,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$SourceWindow,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  if ($BaselineWindow -eq $SourceWindow) {
    throw "BaselineWindow and SourceWindow must differ"
  }
  $matching = @($Reports | Where-Object {
      $_.backend -eq $Backend -and $_.driver -eq "source" -and
      $_.workload -eq "pipeline" -and
      $_.wait_mode -eq $WaitMode -and
      [int64]$_.payload_bytes -eq $PayloadBytes -and
      ([int64]$_.source_window_capacity -eq $BaselineWindow -or
       [int64]$_.source_window_capacity -eq $SourceWindow)
    })
  if ($matching.Count -ne 2 * $ExpectedRuns) {
    throw "Expected $ExpectedRuns Source window pairs for $Backend/$PayloadBytes/$WaitMode, found $($matching.Count) records"
  }

  $baselines = @()
  $sources = @()
  $echoRatios = @()
  $applicationRatios = @()
  $p50Deltas = @()
  $p95Deltas = @()
  $p99Deltas = @()
  $wallDeltas = @()
  $cpuTimeDeltas = @()
  $cpuEfficiencyDeltas = @()
  $combinedStageDeltas = @()

  for ($run = 1; $run -le $ExpectedRuns; ++$run) {
    $baseline = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and
        [int64]$_.source_window_capacity -eq $BaselineWindow
      })
    $source = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and
        [int64]$_.source_window_capacity -eq $SourceWindow
      })
    if ($baseline.Count -ne 1 -or $source.Count -ne 1) {
      throw "Expected one Source window $BaselineWindow and $SourceWindow report for $Backend/$PayloadBytes/$WaitMode run $run, found $($baseline.Count) and $($source.Count)"
    }
    $baseline = $baseline[0]
    $source = $source[0]
    $baselineAttempts = [int64]$baseline.attempted
    if ($baselineAttempts -le 0 -or
        [int64]$source.attempted -ne $baselineAttempts) {
      throw "Source window attempted counts must be equal and positive for $Backend/$PayloadBytes/$WaitMode run $run"
    }
    $baselineCombinedStage = [double]$baseline.admission_mean_ns +
      [double]$baseline.completion_drive_mean_ns
    $sourceCombinedStage = [double]$source.admission_mean_ns +
      [double]$source.completion_drive_mean_ns

    [void](Get-CflowPercentDelta $baseline.exchanges_per_second `
        $source.exchanges_per_second "Echo/s")
    [void](Get-CflowPercentDelta $baseline.application_mib_per_second `
        $source.application_mib_per_second "application throughput")
    $echoRatios += [double]$source.exchanges_per_second /
      [double]$baseline.exchanges_per_second
    $applicationRatios += [double]$source.application_mib_per_second /
      [double]$baseline.application_mib_per_second
    $p50Deltas += Get-CflowPercentDelta $baseline.p50_ns $source.p50_ns "P50"
    $p95Deltas += Get-CflowPercentDelta $baseline.p95_ns $source.p95_ns "P95"
    $p99Deltas += Get-CflowPercentDelta $baseline.p99_ns $source.p99_ns "P99"
    $wallDeltas += Get-CflowPercentDelta $baseline.wall_ns $source.wall_ns "wall"
    $cpuTimeDeltas += Get-CflowPercentDelta $baseline.process_cpu_ns `
      $source.process_cpu_ns "CPU time"
    $cpuEfficiencyDeltas += Get-CflowPercentDelta `
      $baseline.application_mib_per_cpu_second `
      $source.application_mib_per_cpu_second "CPU efficiency"
    $combinedStageDeltas += Get-CflowPercentDelta `
      $baselineCombinedStage $sourceCombinedStage "combined stage"
    $baselines += $baseline
    $sources += $source
  }

  return [pscustomobject][ordered]@{
    backend = $Backend
    payload_bytes = $PayloadBytes
    wait_mode = $WaitMode
    workload = "pipeline"
    baseline_window = $BaselineWindow
    source_window = $SourceWindow
    runs = $ExpectedRuns
    baseline_median_echo_per_second =
      Get-CflowMedian @($baselines.exchanges_per_second)
    source_median_echo_per_second =
      Get-CflowMedian @($sources.exchanges_per_second)
    paired_echo_ratio = [math]::Round((Get-CflowMedian $echoRatios), 6)
    baseline_median_application_mib_per_second =
      Get-CflowMedian @($baselines.application_mib_per_second)
    source_median_application_mib_per_second =
      Get-CflowMedian @($sources.application_mib_per_second)
    paired_application_mib_ratio =
      [math]::Round((Get-CflowMedian $applicationRatios), 6)
    baseline_median_p50_ns = Get-CflowMedian @($baselines.p50_ns)
    source_median_p50_ns = Get-CflowMedian @($sources.p50_ns)
    baseline_median_p95_ns = Get-CflowMedian @($baselines.p95_ns)
    source_median_p95_ns = Get-CflowMedian @($sources.p95_ns)
    baseline_median_p99_ns = Get-CflowMedian @($baselines.p99_ns)
    source_median_p99_ns = Get-CflowMedian @($sources.p99_ns)
    paired_p50_delta_pct = [math]::Round((Get-CflowMedian $p50Deltas), 6)
    paired_p95_delta_pct = [math]::Round((Get-CflowMedian $p95Deltas), 6)
    paired_p99_delta_pct = [math]::Round((Get-CflowMedian $p99Deltas), 6)
    paired_wall_delta_pct = [math]::Round((Get-CflowMedian $wallDeltas), 6)
    paired_cpu_time_delta_pct =
      [math]::Round((Get-CflowMedian $cpuTimeDeltas), 6)
    paired_cpu_efficiency_delta_pct =
      [math]::Round((Get-CflowMedian $cpuEfficiencyDeltas), 6)
    paired_combined_stage_delta_pct =
      [math]::Round((Get-CflowMedian $combinedStageDeltas), 6)
    baseline_median_cpu_pct = Get-CflowMedian @($baselines.process_cpu_pct)
    source_median_cpu_pct = Get-CflowMedian @($sources.process_cpu_pct)
    baseline_median_peak_occupied =
      Get-CflowMedian @($baselines.source_peak_occupied)
    source_median_peak_occupied =
      Get-CflowMedian @($sources.source_peak_occupied)
  }
}

function Get-CflowPairedDirectSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Reports,

    [Parameter(Mandatory = $true)]
    [string]$Backend,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$PayloadBytes,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  $matching = @($Reports | Where-Object {
      $_.comparison_backend -eq $Backend -and
      [int64]$_.payload_bytes -eq $PayloadBytes
    })
  if ($matching.Count -ne 2 * $ExpectedRuns) {
    throw "Expected $ExpectedRuns Actor/direct pairs for $Backend/$PayloadBytes, found $($matching.Count) records"
  }

  $actors = @()
  $directs = @()
  $echoRatios = @()
  $throughputRatios = @()
  $p99Deltas = @()
  for ($run = 1; $run -le $ExpectedRuns; ++$run) {
    $actor = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "actor"
      })
    $direct = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "direct"
      })
    if ($actor.Count -ne 1 -or $direct.Count -ne 1) {
      throw "Expected one Actor and one direct report for $Backend/$PayloadBytes run $run, found $($actor.Count) and $($direct.Count)"
    }
    $actor = $actor[0]
    $direct = $direct[0]
    $actorAttempts = [int64]$actor.attempted
    $directAttempts = [int64]$direct.attempted
    if ($directAttempts -le 0 -or $actorAttempts -ne $directAttempts) {
      throw "Actor and direct attempted counts must be equal and positive for $Backend/$PayloadBytes run $run"
    }
    foreach ($metric in @("exchanges_per_second", "application_mib_per_second", "p99_ns")) {
      $actorValue = [double]$actor.$metric
      $directValue = [double]$direct.$metric
      if ([double]::IsNaN($actorValue) -or [double]::IsInfinity($actorValue) -or
          $actorValue -le 0.0 -or [double]::IsNaN($directValue) -or
          [double]::IsInfinity($directValue) -or $directValue -le 0.0) {
        throw "$metric values must be finite and positive for $Backend/$PayloadBytes run $run"
      }
    }
    $echoRatios +=
      [double]$actor.exchanges_per_second / [double]$direct.exchanges_per_second
    $throughputRatios +=
      [double]$actor.application_mib_per_second /
        [double]$direct.application_mib_per_second
    $p99Deltas += [double]$actor.p99_ns - [double]$direct.p99_ns
    $actors += $actor
    $directs += $direct
  }

  return [pscustomobject][ordered]@{
    backend = $Backend
    payload_bytes = $PayloadBytes
    runs = $ExpectedRuns
    direct_median_echo_per_second = Get-CflowMedian @($directs.exchanges_per_second)
    actor_median_echo_per_second = Get-CflowMedian @($actors.exchanges_per_second)
    paired_actor_direct_echo_ratio =
      [math]::Round((Get-CflowMedian $echoRatios), 6)
    direct_median_application_mib_per_second =
      Get-CflowMedian @($directs.application_mib_per_second)
    actor_median_application_mib_per_second =
      Get-CflowMedian @($actors.application_mib_per_second)
    paired_actor_direct_application_mib_ratio =
      [math]::Round((Get-CflowMedian $throughputRatios), 6)
    direct_median_p99_ns = Get-CflowMedian @($directs.p99_ns)
    actor_median_p99_ns = Get-CflowMedian @($actors.p99_ns)
    paired_p99_delta_ns = [math]::Round((Get-CflowMedian $p99Deltas), 6)
  }
}
