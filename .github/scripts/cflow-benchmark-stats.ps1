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

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  $matching = @($Reports | Where-Object {
      $_.backend -eq $Backend -and
      [int64]$_.payload_bytes -eq $PayloadBytes -and
      $_.wait_mode -eq $WaitMode
    })
  if ($matching.Count -ne 2 * $ExpectedRuns) {
    throw "Expected $ExpectedRuns Actor/Source pairs for $Backend/$PayloadBytes/$WaitMode, found $($matching.Count) records"
  }

  $actors = @()
  $sources = @()
  $p50Deltas = @()
  $p99Deltas = @()
  $wallDeltas = @()
  $cpuTimeDeltas = @()
  $cpuEfficiencyDeltas = @()
  $combinedStageDeltas = @()
  $p50AbsoluteDeltas = @()
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
    $p99Deltas += Get-CflowPercentDelta $actor.p99_ns $source.p99_ns "P99"
    $wallDeltas += Get-CflowPercentDelta $actor.wall_ns $source.wall_ns "wall"
    $cpuTimeDeltas += Get-CflowPercentDelta `
      $actor.process_cpu_ns $source.process_cpu_ns "CPU time"
    $cpuEfficiencyDeltas += Get-CflowPercentDelta `
      $actor.application_mib_per_cpu_second `
      $source.application_mib_per_cpu_second "CPU efficiency"
    $combinedStageDeltas += Get-CflowPercentDelta `
      $actorCombinedStage $sourceCombinedStage "combined stage"
    $p50AbsoluteDeltas += [double]$source.p50_ns - [double]$actor.p50_ns
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
    runs = $ExpectedRuns
    actor_median_p50_ns = Get-CflowMedian @($actors.p50_ns)
    source_median_p50_ns = Get-CflowMedian @($sources.p50_ns)
    actor_median_p99_ns = Get-CflowMedian @($actors.p99_ns)
    source_median_p99_ns = Get-CflowMedian @($sources.p99_ns)
    paired_p50_delta_pct = [math]::Round((Get-CflowMedian $p50Deltas), 6)
    paired_p99_delta_pct = [math]::Round((Get-CflowMedian $p99Deltas), 6)
    paired_wall_delta_pct = [math]::Round((Get-CflowMedian $wallDeltas), 6)
    paired_cpu_time_delta_pct =
      [math]::Round((Get-CflowMedian $cpuTimeDeltas), 6)
    paired_cpu_efficiency_delta_pct =
      [math]::Round((Get-CflowMedian $cpuEfficiencyDeltas), 6)
    paired_combined_stage_delta_pct =
      [math]::Round((Get-CflowMedian $combinedStageDeltas), 6)
    paired_p50_delta_ns = [math]::Round((Get-CflowMedian $p50AbsoluteDeltas), 6)
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
