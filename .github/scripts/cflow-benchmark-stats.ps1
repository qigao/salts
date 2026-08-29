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

function Get-CflowStageTimingVersion {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Report
  )

  if ($null -eq $Report.PSObject.Properties["stage_timing_version"]) {
    throw "Stage timing report is missing stage_timing_version"
  }
  return [int]$Report.stage_timing_version
}

function Assert-CflowStageTimingReport {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Report
  )

  $version = Get-CflowStageTimingVersion -Report $Report
  if ($version -notin @(0, 3)) {
    throw "Unsupported CFlow stage timing version $version"
  }

  $commonFields = @(
    "io_operations", "admission_ns", "completion_drive_ns",
    "drive_ns", "wait_ns", "completion_process_ns",
    "completion_residual_ns", "admission_mean_ns",
    "completion_drive_mean_ns", "drive_mean_ns", "wait_mean_ns",
    "completion_process_mean_ns", "completion_residual_mean_ns"
  )
  $handoffFields = @(
    "dispatch_ns", "executor_ns", "drive_residual_ns",
    "completion_ready_ns", "wake_resume_ns", "wait_residual_ns",
    "dispatch_mean_ns", "executor_mean_ns", "drive_residual_mean_ns",
    "completion_ready_mean_ns", "wake_resume_mean_ns",
    "wait_residual_mean_ns"
  )
  $requiredFields = @($commonFields + $handoffFields)
  foreach ($field in $requiredFields) {
    if ($null -eq $Report.PSObject.Properties[$field]) {
      throw "Stage timing version $version report is missing $field"
    }
  }

  if ($version -eq 0) {
    if ([bool]$Report.stage_timing) {
      throw "Stage timing version 0 report must be disabled"
    }
    foreach ($field in $requiredFields) {
      if ([decimal]$Report.$field -ne 0) {
        throw "Disabled stage timing field $field must be zero"
      }
    }
    return
  }

  if (-not [bool]$Report.stage_timing) {
    throw "Stage timing version $version report must be enabled"
  }
  foreach ($field in $requiredFields) {
    if ([decimal]$Report.$field -lt 0) {
      throw "Stage timing field $field must not be negative"
    }
  }
  $operations = [decimal]$Report.io_operations
  if ($operations -le 0) {
    throw "Enabled stage timing report must contain operations"
  }
  $componentTotal = [decimal]$Report.drive_ns + [decimal]$Report.wait_ns +
    [decimal]$Report.completion_process_ns
  $completionTotal = [decimal]$Report.completion_drive_ns
  if ($componentTotal -gt $completionTotal) {
    throw "Stage timing components exceed the post-admission interval"
  }
  $expectedResidual = $completionTotal - $componentTotal
  if ([decimal]$Report.completion_residual_ns -ne $expectedResidual) {
    throw "Stage timing residual does not match the post-admission interval"
  }
  if ($version -eq 3) {
    $driveComponents = [decimal]$Report.dispatch_ns +
      [decimal]$Report.executor_ns
    if ($driveComponents -gt [decimal]$Report.drive_ns) {
      throw "Stage timing handoff components exceed the drive interval"
    }
    $expectedDriveResidual = [decimal]$Report.drive_ns - $driveComponents
    if ([decimal]$Report.drive_residual_ns -ne $expectedDriveResidual) {
      throw "Stage timing drive residual does not match the drive interval"
    }

    $waitComponents = [decimal]$Report.completion_ready_ns +
      [decimal]$Report.wake_resume_ns
    if ($waitComponents -gt [decimal]$Report.wait_ns) {
      throw "Stage timing handoff components exceed the wait interval"
    }
    $expectedWaitResidual = [decimal]$Report.wait_ns - $waitComponents
    if ([decimal]$Report.wait_residual_ns -ne $expectedWaitResidual) {
      throw "Stage timing wait residual does not match the wait interval"
    }
  }

  $meanPairs = @(
    @("admission_ns", "admission_mean_ns"),
    @("completion_drive_ns", "completion_drive_mean_ns"),
    @("drive_ns", "drive_mean_ns"),
    @("wait_ns", "wait_mean_ns"),
    @("completion_process_ns", "completion_process_mean_ns"),
    @("completion_residual_ns", "completion_residual_mean_ns")
  )
  if ($version -eq 3) {
    $meanPairs += @(
      ,@("dispatch_ns", "dispatch_mean_ns"),
      ,@("executor_ns", "executor_mean_ns"),
      ,@("drive_residual_ns", "drive_residual_mean_ns"),
      ,@("completion_ready_ns", "completion_ready_mean_ns"),
      ,@("wake_resume_ns", "wake_resume_mean_ns"),
      ,@("wait_residual_ns", "wait_residual_mean_ns")
    )
  }
  $threeDecimalHalfUnit = [decimal]5 / [decimal]10000
  foreach ($pair in $meanPairs) {
    $exactMean = [decimal]$Report.($pair[0]) / $operations
    $meanDifference = [decimal]$Report.($pair[1]) - $exactMean
    if ($meanDifference -lt 0) { $meanDifference = -$meanDifference }
    if ($meanDifference -gt $threeDecimalHalfUnit) {
      throw "Stage timing mean $($pair[1]) is outside emitted precision: reported=$($Report.($pair[1])) exact=$exactMean total=$($Report.($pair[0])) operations=$operations"
    }
  }
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

function Get-CflowSourceOperationCapacity {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("direct", "actor", "source")]
    [string]$Driver,

    [Parameter(Mandatory = $true)]
    [ValidateSet("round-trip", "pipeline")]
    [string]$Workload,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$WorkloadWindow
  )

  if ($Driver -ne "source") {
    return [long]0
  }
  if ($Workload -eq "round-trip") {
    $receiveAndSendOperationCapacity = [long]2
    return $receiveAndSendOperationCapacity
  }
  return $WorkloadWindow
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

function Get-CflowPipelineDriverOrder {
  param(
    [Parameter(Mandatory = $true)]
    [int]$Run,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$BackendIndex,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$PayloadIndex,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$WindowIndex,

    [Parameter(Mandatory = $true)]
    [ValidateSet("blocking", "busy")]
    [string]$WaitMode
  )

  if ($Run -le 0) {
    throw "Run must be positive"
  }
  $waitOffset = if ($WaitMode -eq "busy") { 1 } else { 0 }
  $rotation = 1 + (($Run - 1 + $BackendIndex + $PayloadIndex +
        $WindowIndex + $waitOffset) % 3)
  return Get-CflowRotatedOrder -Values @("direct", "actor", "source") `
    -Run $rotation
}

function ConvertFrom-CflowIoModelBenchmarkOutput {
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

  if ($ExpectedSamples -ge [long]::MaxValue) {
    throw "IO model expected value count overflows int64"
  }
  $maximumValuesPerSample = [decimal]::Floor(
    ([decimal]([long]::MaxValue)) /
      ([decimal]$ExpectedSamples + [decimal]1))
  if ([decimal]$ExpectedValuesPerSample -gt $maximumValuesPerSample) {
    throw "IO model expected value count overflows int64"
  }
  $timedValues = [long](
    [decimal]$ExpectedSamples * [decimal]$ExpectedValuesPerSample)
  $processedValues = [long](
    ([decimal]$ExpectedSamples + [decimal]1) *
      [decimal]$ExpectedValuesPerSample)

  $tablePatterns = [ordered]@{
    "direct-control" =
      '^\s*\|\s*mock direct-control\s*\|\s*(?<samples>[0-9]+)\s*\|' +
      '\s*(?<operations>[0-9]+)\s*\|\s*[^|]+\|' +
      '\s*(?<mean>[0-9]+(?:\.[0-9]+)?)\s*\|'
    actor =
      '^\s*\|\s*mock Actor\s*\|\s*(?<samples>[0-9]+)\s*\|' +
      '\s*(?<operations>[0-9]+)\s*\|\s*[^|]+\|' +
      '\s*(?<mean>[0-9]+(?:\.[0-9]+)?)\s*\|'
    "io-source-adapter" =
      '^\s*\|\s*mock IO Source adapter\s*\|' +
      '\s*(?<samples>[0-9]+)\s*\|\s*(?<operations>[0-9]+)\s*\|' +
      '\s*[^|]+\|\s*(?<mean>[0-9]+(?:\.[0-9]+)?)\s*\|'
    "coroutine-source-adapter" =
      '^\s*\|\s*mock coroutine Source adapter\s*\|' +
      '\s*(?<samples>[0-9]+)\s*\|\s*(?<operations>[0-9]+)\s*\|' +
      '\s*[^|]+\|\s*(?<mean>[0-9]+(?:\.[0-9]+)?)\s*\|'
    "source-runtime" =
      '^\s*\|\s*mock Source runtime window=(?<capacity>[0-9]+)\s+' +
      'values=(?<values>[0-9]+)\s*\|\s*(?<samples>[0-9]+)\s*\|' +
      '\s*(?<operations>[0-9]+)\s*\|\s*[^|]+\|' +
      '\s*(?<mean>[0-9]+(?:\.[0-9]+)?)\s*\|'
  }
  $tableRecords = @{}
  foreach ($model in $tablePatterns.Keys) {
    $matching = @()
    foreach ($line in $Lines) {
      if ($line -match $tablePatterns[$model]) {
        $matching += [pscustomobject]@{
          samples = [int64]$Matches.samples
          operations_per_sample = [int64]$Matches.operations
          mean_ns_per_value = [double]$Matches.mean
          capacity = if ($model -eq "source-runtime") {
            [int64]$Matches.capacity
          } else {
            $ExpectedCapacity
          }
          values_per_sample = if ($model -eq "source-runtime") {
            [int64]$Matches.values
          } else {
            $ExpectedValuesPerSample
          }
        }
      }
    }
    if ($matching.Count -ne 1) {
      throw "Expected exactly one $model IO model table record, found $($matching.Count)"
    }
    $tableRecords[$model] = $matching[0]
  }

  $jsonLines = @($Lines | Where-Object {
      $_ -match '^CFLOW_IO_MODEL_BENCH_JSON '
    })
  if ($jsonLines.Count -ne 5) {
    throw "Expected exactly five IO model JSON records, found $($jsonLines.Count)"
  }
  $jsonRecords = @{}
  foreach ($line in $jsonLines) {
    $jsonText = $line.Substring("CFLOW_IO_MODEL_BENCH_JSON ".Length)
    try {
      $report = $jsonText | ConvertFrom-Json
    } catch {
      throw "Invalid IO model JSON record: $jsonText"
    }
    foreach ($field in @(
        "schema", "model", "capacity", "values_per_sample", "samples",
        "timed_values", "processed_values", "errors", "rejections",
        "stale_completions")) {
      if ($null -eq $report.$field) {
        throw "Missing IO model report field '$field': $jsonText"
      }
    }
    if ($report.model -notin $tablePatterns.Keys) {
      throw "Unknown IO model '$($report.model)': $jsonText"
    }
    if ($jsonRecords.ContainsKey($report.model)) {
      throw "Duplicate IO model '$($report.model)'"
    }
    $jsonRecords[$report.model] = $report
  }

  $reports = @()
  foreach ($model in $tablePatterns.Keys) {
    if (-not $jsonRecords.ContainsKey($model)) {
      throw "Missing IO model '$model'"
    }
    $report = $jsonRecords[$model]
    $table = $tableRecords[$model]
    $meanNs = [double]$table.mean_ns_per_value
    if ($report.schema -ne "cflow-io-model-benchmark/v1" -or
        [int64]$report.capacity -ne $ExpectedCapacity -or
        [int64]$report.values_per_sample -ne $ExpectedValuesPerSample -or
        [int64]$report.samples -ne $ExpectedSamples -or
        [int64]$report.timed_values -ne $timedValues -or
        [int64]$report.processed_values -ne $processedValues -or
        [int64]$table.capacity -ne $ExpectedCapacity -or
        [int64]$table.values_per_sample -ne $ExpectedValuesPerSample -or
        [int64]$table.samples -ne $ExpectedSamples -or
        [int64]$table.operations_per_sample -ne $ExpectedValuesPerSample -or
        [double]::IsNaN($meanNs) -or [double]::IsInfinity($meanNs) -or
        $meanNs -le 0.0 -or [int64]$report.errors -ne 0 -or
        [int64]$report.rejections -ne 0 -or
        [int64]$report.stale_completions -ne 0) {
      throw "Invalid or unsuccessful $model IO model report"
    }

    $accepted = [int64]0
    $acknowledged = [int64]0
    if ($model -ne "direct-control") {
      foreach ($field in @("accepted", "acknowledged")) {
        if ($null -eq $report.$field) {
          throw "Missing $model IO model report field '$field'"
        }
      }
      $accepted = [int64]$report.accepted
      $acknowledged = [int64]$report.acknowledged
      if ($accepted -ne $processedValues -or
          $acknowledged -ne $processedValues) {
        throw "$model IO model accepted/acknowledged counts are invalid"
      }
    }

    $driveCalls = [int64]0
    $driverCalls = [int64]0
    $peakOccupied = [int64]0
    if ($model -in @(
        "io-source-adapter", "coroutine-source-adapter", "source-runtime")) {
      foreach ($field in @("drive_calls", "driver_calls", "peak_occupied")) {
        if ($null -eq $report.$field) {
          throw "Missing $model IO model report field '$field'"
        }
      }
      $driveCalls = [int64]$report.drive_calls
      $driverCalls = [int64]$report.driver_calls
      $peakOccupied = [int64]$report.peak_occupied
      if ($driveCalls -le 0 -or $driverCalls -le 0 -or
          $peakOccupied -le 0 -or $peakOccupied -gt $ExpectedCapacity) {
        throw "$model IO model scheduling counters are invalid"
      }
    }

    $addedWorkerThreads = [int64]0
    if ($model -eq "coroutine-source-adapter") {
      if ($null -eq $report.added_worker_threads) {
        throw "Missing coroutine-source-adapter IO model report field 'added_worker_threads'"
      }
      $addedWorkerThreads = [int64]$report.added_worker_threads
      if ($addedWorkerThreads -ne 0) {
        throw "Coroutine Source adapter must not add worker threads"
      }
    }

    $reports += [pscustomobject][ordered]@{
      schema = "cflow-io-model-benchmark/v1"
      benchmark_run = $BenchmarkRun
      model = $model
      capacity = $ExpectedCapacity
      values_per_sample = $ExpectedValuesPerSample
      samples = $ExpectedSamples
      timed_values = $timedValues
      processed_values = $processedValues
      mean_ns_per_value = $meanNs
      accepted = $accepted
      acknowledged = $acknowledged
      drive_calls = $driveCalls
      driver_calls = $driverCalls
      peak_occupied = $peakOccupied
      added_worker_threads = $addedWorkerThreads
      errors = [int64]$report.errors
      rejections = [int64]$report.rejections
      stale_completions = [int64]$report.stale_completions
    }
  }
  return $reports
}

function Get-CflowPairedIoModelSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Reports,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$Capacity,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  $matching = @($Reports | Where-Object {
      $_.schema -eq "cflow-io-model-benchmark/v1" -and
      [int64]$_.capacity -eq $Capacity -and
      $_.model -in @(
        "direct-control", "actor", "io-source-adapter",
        "coroutine-source-adapter", "source-runtime")
    })
  if ($matching.Count -ne 5 * $ExpectedRuns) {
    throw "Expected $ExpectedRuns mock IO model layer sets for capacity $Capacity, found $($matching.Count) records"
  }

  $directs = @()
  $actors = @()
  $adapters = @()
  $coroutines = @()
  $runtimes = @()
  $actorDirectRatios = @()
  $adapterDirectRatios = @()
  $coroutineDirectRatios = @()
  $runtimeDirectRatios = @()
  $actorDirectDeltas = @()
  $adapterDirectDeltas = @()
  $coroutineDirectDeltas = @()
  $runtimeDirectDeltas = @()
  for ($run = 1; $run -le $ExpectedRuns; ++$run) {
    $direct = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.model -eq "direct-control"
      })
    $actor = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.model -eq "actor"
      })
    $adapter = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and
        $_.model -eq "io-source-adapter"
      })
    $coroutine = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and
        $_.model -eq "coroutine-source-adapter"
      })
    $runtime = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.model -eq "source-runtime"
      })
    if ($direct.Count -ne 1 -or $actor.Count -ne 1 -or
        $adapter.Count -ne 1 -or $coroutine.Count -ne 1 -or
        $runtime.Count -ne 1) {
      throw "Expected one direct-control, Actor, IO Source adapter, coroutine Source adapter, and Source runtime report for capacity $Capacity run $run"
    }
    $direct = $direct[0]
    $actor = $actor[0]
    $adapter = $adapter[0]
    $coroutine = $coroutine[0]
    $runtime = $runtime[0]
    foreach ($report in @(
        $direct, $actor, $adapter, $coroutine, $runtime)) {
      if ([int64]$report.values_per_sample -ne
            [int64]$direct.values_per_sample -or
          [int64]$report.samples -ne [int64]$direct.samples -or
          [int64]$report.timed_values -ne [int64]$direct.timed_values -or
          [int64]$report.processed_values -ne
            [int64]$direct.processed_values) {
        throw "Mock IO model workload mismatch for capacity $Capacity run $run"
      }
      $meanNs = [double]$report.mean_ns_per_value
      if ([double]::IsNaN($meanNs) -or [double]::IsInfinity($meanNs) -or
          $meanNs -le 0.0 -or [int64]$report.errors -ne 0 -or
          [int64]$report.rejections -ne 0 -or
          [int64]$report.stale_completions -ne 0) {
        throw "Invalid mock IO model report for capacity $Capacity run $run"
      }
      if ($report.model -ne "direct-control" -and
          ([int64]$report.accepted -ne [int64]$report.processed_values -or
           [int64]$report.acknowledged -ne
             [int64]$report.processed_values)) {
        throw "Invalid mock IO model lifecycle counts for capacity $Capacity run $run"
      }
      if ($report.model -in @(
          "io-source-adapter", "coroutine-source-adapter", "source-runtime") -and
          ([int64]$report.drive_calls -le 0 -or
           [int64]$report.driver_calls -le 0 -or
           [int64]$report.peak_occupied -le 0 -or
           [int64]$report.peak_occupied -gt $Capacity)) {
        throw "Invalid mock IO Source scheduling counts for capacity $Capacity run $run"
      }
      if ($report.model -eq "coroutine-source-adapter" -and
          [int64]$report.added_worker_threads -ne 0) {
        throw "Invalid mock coroutine Source adapter worker count for capacity $Capacity run $run"
      }
    }
    $actorDirectRatios +=
      [double]$actor.mean_ns_per_value / [double]$direct.mean_ns_per_value
    $adapterDirectRatios +=
      [double]$adapter.mean_ns_per_value / [double]$direct.mean_ns_per_value
    $coroutineDirectRatios +=
      [double]$coroutine.mean_ns_per_value /
        [double]$direct.mean_ns_per_value
    $runtimeDirectRatios +=
      [double]$runtime.mean_ns_per_value / [double]$direct.mean_ns_per_value
    $actorDirectDeltas +=
      [double]$actor.mean_ns_per_value - [double]$direct.mean_ns_per_value
    $adapterDirectDeltas +=
      [double]$adapter.mean_ns_per_value - [double]$direct.mean_ns_per_value
    $coroutineDirectDeltas +=
      [double]$coroutine.mean_ns_per_value -
        [double]$direct.mean_ns_per_value
    $runtimeDirectDeltas +=
      [double]$runtime.mean_ns_per_value - [double]$direct.mean_ns_per_value
    $directs += $direct
    $actors += $actor
    $adapters += $adapter
    $coroutines += $coroutine
    $runtimes += $runtime
  }

  return [pscustomobject][ordered]@{
    capacity = $Capacity
    values_per_sample = [int64]$directs[0].values_per_sample
    samples = [int64]$directs[0].samples
    runs = $ExpectedRuns
    direct_median_mean_ns_per_value =
      Get-CflowMedian @($directs.mean_ns_per_value)
    actor_median_mean_ns_per_value =
      Get-CflowMedian @($actors.mean_ns_per_value)
    adapter_median_mean_ns_per_value =
      Get-CflowMedian @($adapters.mean_ns_per_value)
    coroutine_median_mean_ns_per_value =
      Get-CflowMedian @($coroutines.mean_ns_per_value)
    runtime_median_mean_ns_per_value =
      Get-CflowMedian @($runtimes.mean_ns_per_value)
    paired_actor_direct_cost_ratio =
      [math]::Round((Get-CflowMedian $actorDirectRatios), 6)
    paired_adapter_direct_cost_ratio =
      [math]::Round((Get-CflowMedian $adapterDirectRatios), 6)
    paired_coroutine_direct_cost_ratio =
      [math]::Round((Get-CflowMedian $coroutineDirectRatios), 6)
    paired_runtime_direct_cost_ratio =
      [math]::Round((Get-CflowMedian $runtimeDirectRatios), 6)
    paired_actor_direct_delta_ns =
      [math]::Round((Get-CflowMedian $actorDirectDeltas), 6)
    paired_adapter_direct_delta_ns =
      [math]::Round((Get-CflowMedian $adapterDirectDeltas), 6)
    paired_coroutine_direct_delta_ns =
      [math]::Round((Get-CflowMedian $coroutineDirectDeltas), 6)
    paired_runtime_direct_delta_ns =
      [math]::Round((Get-CflowMedian $runtimeDirectDeltas), 6)
  }
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
    '^\s*\|\s*(?:mock Source runtime )?window=(?<capacity>[0-9]+)\s+' +
    'values=(?<values>[0-9]+)\s*\|' +
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
      "processed_values", "drive_calls", "driver_calls",
      "pending_drive_credit", "peak_occupied",
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
  $pendingDriveCredit = [int64]$report.pending_drive_credit
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

function Get-CflowStageTimingSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Reports,

    [Parameter(Mandatory = $true)]
    [string]$Backend,

    [Parameter(Mandatory = $true)]
    [ValidateSet("tcp", "udp")]
    [string]$Protocol,

    [Parameter(Mandatory = $true)]
    [ValidateSet("blocking", "busy")]
    [string]$WaitMode,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$PayloadBytes,

    [ValidateRange(1, [long]::MaxValue)]
    [long]$SourceWindow = (Get-CflowSourceOperationCapacity `
      -Driver source -Workload round-trip -WorkloadWindow 1),

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  $matching = @($Reports | Where-Object {
      $_.backend -eq $Backend -and
      $_.protocol -eq $Protocol -and
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
    throw "Expected $ExpectedRuns Actor and Source stage records for $Backend/$PayloadBytes/$WaitMode, found $($matching.Count) records"
  }

  $actors = @()
  $sources = @()
  $stageTimingVersion = 0

  for ($run = 1; $run -le $ExpectedRuns; ++$run) {
    $actor = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "actor"
      })
    $source = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "source"
      })
    if ($actor.Count -ne 1 -or $source.Count -ne 1) {
      throw "Expected one Actor and one Source stage report for $Backend/$PayloadBytes/$WaitMode run $run, found $($actor.Count) and $($source.Count)"
    }

    $actor = $actor[0]
    $source = $source[0]
    Assert-CflowStageTimingReport -Report $actor
    Assert-CflowStageTimingReport -Report $source
    $actorStageVersion = Get-CflowStageTimingVersion -Report $actor
    $sourceStageVersion = Get-CflowStageTimingVersion -Report $source
    if ($actorStageVersion -ne $sourceStageVersion) {
      throw "Actor and Source stage timing versions must match for $Backend/$PayloadBytes/$WaitMode run $run"
    }
    $stageTimingVersion = $actorStageVersion
    $actorAttempts = [int64]$actor.attempted
    $sourceAttempts = [int64]$source.attempted
    if ($actorAttempts -le 0 -or $sourceAttempts -ne $actorAttempts) {
      throw "Actor and Source attempted counts must be equal and positive for $Backend/$PayloadBytes/$WaitMode run $run"
    }
    $actors += $actor
    $sources += $source
  }

  return [pscustomobject][ordered]@{
    backend = $Backend
    protocol = $Protocol
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
    actor_median_application_mib_per_second =
      Get-CflowMedian @($actors.application_mib_per_second)
    source_median_application_mib_per_second =
      Get-CflowMedian @($sources.application_mib_per_second)
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
    actor_median_drive_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($actors.drive_mean_ns)
    } else { $null }
    source_median_drive_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($sources.drive_mean_ns)
    } else { $null }
    actor_median_wait_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($actors.wait_mean_ns)
    } else { $null }
    source_median_wait_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($sources.wait_mean_ns)
    } else { $null }
    actor_median_completion_process_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($actors.completion_process_mean_ns)
      } else { $null }
    source_median_completion_process_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($sources.completion_process_mean_ns)
      } else { $null }
    actor_median_completion_residual_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($actors.completion_residual_mean_ns)
      } else { $null }
    source_median_completion_residual_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($sources.completion_residual_mean_ns)
      } else { $null }
    actor_median_dispatch_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($actors.dispatch_mean_ns)
    } else { $null }
    source_median_dispatch_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($sources.dispatch_mean_ns)
    } else { $null }
    actor_median_executor_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($actors.executor_mean_ns)
    } else { $null }
    source_median_executor_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($sources.executor_mean_ns)
    } else { $null }
    actor_median_drive_residual_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($actors.drive_residual_mean_ns)
      } else { $null }
    source_median_drive_residual_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($sources.drive_residual_mean_ns)
      } else { $null }
    actor_median_completion_ready_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($actors.completion_ready_mean_ns)
      } else { $null }
    source_median_completion_ready_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($sources.completion_ready_mean_ns)
      } else { $null }
    actor_median_wake_resume_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($actors.wake_resume_mean_ns)
    } else { $null }
    source_median_wake_resume_mean_ns = if ($stageTimingVersion -eq 3) {
      Get-CflowMedian @($sources.wake_resume_mean_ns)
    } else { $null }
    actor_median_wait_residual_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($actors.wait_residual_mean_ns)
      } else { $null }
    source_median_wait_residual_mean_ns =
      if ($stageTimingVersion -eq 3) {
        Get-CflowMedian @($sources.wait_residual_mean_ns)
      } else { $null }
  }
}

function Get-CflowPairedTransportSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Reports,

    [Parameter(Mandatory = $true)]
    [string]$Backend,

    [Parameter(Mandatory = $true)]
    [ValidateSet("tcp", "udp")]
    [string]$Protocol,

    [Parameter(Mandatory = $true)]
    [ValidateSet("blocking", "busy")]
    [string]$WaitMode,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$PayloadBytes,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$WindowCapacity,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedRuns
  )

  if ($ExpectedRuns -le 0) {
    throw "ExpectedRuns must be positive"
  }
  $expectedWorkload =
    if ($Protocol -eq "udp") { "pipeline" } else { "round-trip" }
  if ($Protocol -eq "tcp" -and $WindowCapacity -ne 1) {
    throw "TCP transport comparisons require window capacity 1"
  }
  $matching = @($Reports | Where-Object {
      $_.schema -eq "cflow-network-benchmark/v1" -and
      $_.comparison_backend -eq $Backend -and $_.protocol -eq $Protocol -and
      $_.profile -eq "throughput" -and $_.workload -eq $expectedWorkload -and
      $_.peer_mode -eq "raw" -and -not [bool]$_.stage_timing -and
      $_.wait_mode -eq $WaitMode -and
      [int64]$_.payload_bytes -eq $PayloadBytes -and
      [int64]$_.workload_window_capacity -eq $WindowCapacity -and
      $_.driver -in @("direct", "actor", "source")
    })
  if ($matching.Count -ne 3 * $ExpectedRuns) {
    throw "Expected $ExpectedRuns Direct/Actor/Source $Protocol transport triples for $Backend/$PayloadBytes/$WaitMode/window=$WindowCapacity, found $($matching.Count) records"
  }

  $directs = @()
  $actors = @()
  $sources = @()
  $actorDirectEchoRatios = @()
  $sourceDirectEchoRatios = @()
  $actorDirectApplicationRatios = @()
  $sourceDirectApplicationRatios = @()
  $actorDirectP50Ratios = @()
  $sourceDirectP50Ratios = @()
  $actorDirectP95Ratios = @()
  $sourceDirectP95Ratios = @()
  $actorDirectP99Ratios = @()
  $sourceDirectP99Ratios = @()
  $actorDirectCpuRatios = @()
  $sourceDirectCpuRatios = @()

  for ($run = 1; $run -le $ExpectedRuns; ++$run) {
    $direct = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "direct"
      })
    $actor = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "actor"
      })
    $source = @($matching | Where-Object {
        [int]$_.benchmark_run -eq $run -and $_.driver -eq "source"
      })
    if ($direct.Count -ne 1 -or $actor.Count -ne 1 -or $source.Count -ne 1) {
      throw "Expected one Direct, Actor, and Source transport report for $Protocol/$Backend/$PayloadBytes/$WaitMode/window=$WindowCapacity run $run, found $($direct.Count), $($actor.Count), and $($source.Count)"
    }
    $direct = $direct[0]
    $actor = $actor[0]
    $source = $source[0]
    if ($direct.backend -ne "socket" -or $actor.backend -ne $Backend -or
        $source.backend -ne $Backend) {
      throw "Transport backends must be socket/$Backend/$Backend for $Protocol/$Backend/$PayloadBytes/$WaitMode/window=$WindowCapacity run $run"
    }
    foreach ($report in @($direct, $actor, $source)) {
      if ([int64]$report.workload_peak_in_flight -ne $WindowCapacity) {
        throw "Transport peak in-flight must equal window $WindowCapacity for $Protocol/$Backend/$PayloadBytes/$WaitMode run $run"
      }
      if ([int64]$report.samples -le 0 -or
          [int64]$report.exchanges_per_sample -le 0 -or
          [int64]$report.samples -ne [int64]$direct.samples -or
          [int64]$report.exchanges_per_sample -ne
            [int64]$direct.exchanges_per_sample -or
          [int64]$report.errors -ne 0 -or
          [int64]$report.rejections -ne 0 -or
          [int64]$report.stale_completions -ne 0) {
        throw "Transport workload or completion status is invalid for $Protocol/$Backend/$PayloadBytes/$WaitMode/window=$WindowCapacity run $run"
      }
      foreach ($metric in @("exchanges_per_second", "application_mib_per_second",
                            "p50_ns", "p95_ns", "p99_ns")) {
        $value = [double]$report.$metric
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or
            $value -le 0.0) {
          throw "$metric values must be finite and positive for $Backend/$PayloadBytes/$WaitMode/window=$WindowCapacity run $run"
        }
      }
      $processCpuNs = [double]$report.process_cpu_ns
      if ([double]::IsNaN($processCpuNs) -or
          [double]::IsInfinity($processCpuNs) -or $processCpuNs -lt 0.0) {
        throw "process_cpu_ns values must be finite and non-negative for $Backend/$PayloadBytes/$WaitMode/window=$WindowCapacity run $run"
      }
    }
    $attempted = [int64]$direct.attempted
    $expectedAttempts =
      [decimal]$direct.samples * [decimal]$direct.exchanges_per_sample
    if ($expectedAttempts -gt [decimal]([long]::MaxValue) -or
        $attempted -ne [int64]$expectedAttempts -or
        [int64]$actor.attempted -ne $attempted -or
        [int64]$source.attempted -ne $attempted) {
      throw "Transport attempted counts must match the common workload for $Protocol/$Backend/$PayloadBytes/$WaitMode/window=$WindowCapacity run $run"
    }

    $actorDirectEchoRatios +=
      [double]$actor.exchanges_per_second / [double]$direct.exchanges_per_second
    $sourceDirectEchoRatios +=
      [double]$source.exchanges_per_second / [double]$direct.exchanges_per_second
    $actorDirectApplicationRatios +=
      [double]$actor.application_mib_per_second /
        [double]$direct.application_mib_per_second
    $sourceDirectApplicationRatios +=
      [double]$source.application_mib_per_second /
        [double]$direct.application_mib_per_second
    $actorDirectP50Ratios += [double]$actor.p50_ns / [double]$direct.p50_ns
    $sourceDirectP50Ratios += [double]$source.p50_ns / [double]$direct.p50_ns
    $actorDirectP95Ratios += [double]$actor.p95_ns / [double]$direct.p95_ns
    $sourceDirectP95Ratios += [double]$source.p95_ns / [double]$direct.p95_ns
    $actorDirectP99Ratios += [double]$actor.p99_ns / [double]$direct.p99_ns
    $sourceDirectP99Ratios += [double]$source.p99_ns / [double]$direct.p99_ns
    if ([double]$actor.process_cpu_ns -gt 0.0 -and
        [double]$direct.process_cpu_ns -gt 0.0) {
      $actorDirectCpuRatios +=
        [double]$actor.process_cpu_ns / [double]$direct.process_cpu_ns
    }
    if ([double]$source.process_cpu_ns -gt 0.0 -and
        [double]$direct.process_cpu_ns -gt 0.0) {
      $sourceDirectCpuRatios +=
        [double]$source.process_cpu_ns / [double]$direct.process_cpu_ns
    }
    $directs += $direct
    $actors += $actor
    $sources += $source
  }

  return [pscustomobject][ordered]@{
    backend = $Backend
    protocol = $Protocol
    payload_bytes = $PayloadBytes
    wait_mode = $WaitMode
    workload = $expectedWorkload
    window_capacity = $WindowCapacity
    runs = $ExpectedRuns
    direct_median_echo_per_second = Get-CflowMedian @($directs.exchanges_per_second)
    actor_median_echo_per_second = Get-CflowMedian @($actors.exchanges_per_second)
    source_median_echo_per_second = Get-CflowMedian @($sources.exchanges_per_second)
    direct_median_application_mib_per_second =
      Get-CflowMedian @($directs.application_mib_per_second)
    actor_median_application_mib_per_second =
      Get-CflowMedian @($actors.application_mib_per_second)
    source_median_application_mib_per_second =
      Get-CflowMedian @($sources.application_mib_per_second)
    paired_actor_direct_echo_ratio =
      [math]::Round((Get-CflowMedian $actorDirectEchoRatios), 6)
    paired_source_direct_echo_ratio =
      [math]::Round((Get-CflowMedian $sourceDirectEchoRatios), 6)
    paired_actor_direct_application_mib_ratio =
      [math]::Round((Get-CflowMedian $actorDirectApplicationRatios), 6)
    paired_source_direct_application_mib_ratio =
      [math]::Round((Get-CflowMedian $sourceDirectApplicationRatios), 6)
    direct_median_p50_ns = Get-CflowMedian @($directs.p50_ns)
    actor_median_p50_ns = Get-CflowMedian @($actors.p50_ns)
    source_median_p50_ns = Get-CflowMedian @($sources.p50_ns)
    paired_actor_direct_p50_ratio =
      [math]::Round((Get-CflowMedian $actorDirectP50Ratios), 6)
    paired_source_direct_p50_ratio =
      [math]::Round((Get-CflowMedian $sourceDirectP50Ratios), 6)
    direct_median_p95_ns = Get-CflowMedian @($directs.p95_ns)
    actor_median_p95_ns = Get-CflowMedian @($actors.p95_ns)
    source_median_p95_ns = Get-CflowMedian @($sources.p95_ns)
    paired_actor_direct_p95_ratio =
      [math]::Round((Get-CflowMedian $actorDirectP95Ratios), 6)
    paired_source_direct_p95_ratio =
      [math]::Round((Get-CflowMedian $sourceDirectP95Ratios), 6)
    direct_median_p99_ns = Get-CflowMedian @($directs.p99_ns)
    actor_median_p99_ns = Get-CflowMedian @($actors.p99_ns)
    source_median_p99_ns = Get-CflowMedian @($sources.p99_ns)
    paired_actor_direct_p99_ratio =
      [math]::Round((Get-CflowMedian $actorDirectP99Ratios), 6)
    paired_source_direct_p99_ratio =
      [math]::Round((Get-CflowMedian $sourceDirectP99Ratios), 6)
    direct_median_cpu_pct = Get-CflowMedian @($directs.process_cpu_pct)
    actor_median_cpu_pct = Get-CflowMedian @($actors.process_cpu_pct)
    source_median_cpu_pct = Get-CflowMedian @($sources.process_cpu_pct)
    paired_actor_direct_cpu_time_ratio =
      if ($actorDirectCpuRatios.Count -gt 0) {
        [math]::Round((Get-CflowMedian $actorDirectCpuRatios), 6)
      } else { $null }
    paired_source_direct_cpu_time_ratio =
      if ($sourceDirectCpuRatios.Count -gt 0) {
        [math]::Round((Get-CflowMedian $sourceDirectCpuRatios), 6)
      } else { $null }
  }
}

function Get-CflowTransportSampleCount {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$PayloadBytes,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$ExchangesPerSample,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$MaximumSamples,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$TargetApplicationBytes
  )

  $bytesPerSample = [decimal]$PayloadBytes * [decimal]$ExchangesPerSample
  $boundedSamples = [int64][decimal]::Floor(
    [decimal]$TargetApplicationBytes / $bytesPerSample)
  if ($boundedSamples -lt 1) {
    return [int64]1
  }
  return [int64][math]::Min($MaximumSamples, $boundedSamples)
}

function Get-CflowTransportBenchmarkMatrix {
  return [ordered]@{
    tcp = [pscustomobject][ordered]@{
      payload_bytes = [int64[]]@(
        1024, 4096, 8192, 16384, 32768, 65536)
      throughput_window = [int64]1
      maximum_payload_bytes = [int64]65536
      maximum_in_flight_bytes = [int64]65536
      boundary_payload_bytes = [int64[]]@()
    }
    udp = [pscustomobject][ordered]@{
      payload_bytes = [int64[]]@(
        1024, 4096, 8192, 16384, 32768, 65507)
      throughput_window = [int64]8
      maximum_payload_bytes = [int64]65507
      maximum_in_flight_bytes = [int64]65536
      boundary_payload_bytes = [int64[]]@(65507)
    }
  }
}

function Get-CflowTransportBenchmarkCases {
  param(
    [Parameter(Mandatory = $true)]
    [System.Collections.IDictionary]$Matrix
  )

  $validated = [ordered]@{}
  foreach ($protocol in @("tcp", "udp")) {
    if (-not $Matrix.Contains($protocol)) {
      throw "Transport matrix is missing protocol $protocol"
    }
    $entry = $Matrix[$protocol]
    $payloads = @($entry.payload_bytes)
    $boundaries = @($entry.boundary_payload_bytes)
    $throughputWindow = [int64]$entry.throughput_window
    $maximumPayload = [int64]$entry.maximum_payload_bytes
    $maximumInFlightBytes = [int64]$entry.maximum_in_flight_bytes
    if ($payloads.Count -eq 0 -or
        @($payloads | Sort-Object -Unique).Count -ne $payloads.Count) {
      throw "Transport $protocol payloads must be non-empty and unique"
    }
    if ($throughputWindow -le 0 -or $maximumPayload -le 0 -or
        $maximumInFlightBytes -le 0) {
      throw "Transport $protocol windows and payload limits must be positive"
    }
    foreach ($boundary in $boundaries) {
      if ([int64]$boundary -notin [int64[]]$payloads) {
        throw "Transport $protocol boundary payload $boundary is not measured"
      }
    }
    foreach ($payload in $payloads) {
      $payload = [int64]$payload
      if ($payload -le 0 -or $payload -gt $maximumPayload) {
        throw "$($protocol.ToUpperInvariant()) payload $payload exceeds maximum $maximumPayload"
      }
    }
    $validated[$protocol] = [pscustomobject]@{
      payloads = [int64[]]$payloads
      boundaries = [int64[]]$boundaries
      throughput_window = $throughputWindow
      maximum_in_flight_bytes = $maximumInFlightBytes
    }
  }

  foreach ($protocol in @("tcp", "udp")) {
    $entry = $validated[$protocol]
    for ($payloadIndex = 0; $payloadIndex -lt $entry.payloads.Count;
         ++$payloadIndex) {
      $payload = [int64]$entry.payloads[$payloadIndex]
      $boundaries = [int64[]]$entry.boundaries
      $windows = if ($payload -in [int64[]]$boundaries) {
        [int64[]]@(1)
      } else {
        $byteBoundedWindow = [int64][decimal]::Floor(
          [decimal]$entry.maximum_in_flight_bytes / [decimal]$payload)
        $payloadThroughputWindow = [math]::Max(
          1, [math]::Min($entry.throughput_window, $byteBoundedWindow))
        [int64[]]@(1, $payloadThroughputWindow) | Sort-Object -Unique
      }
      $windowIndex = 0
      foreach ($window in $windows) {
        [pscustomobject][ordered]@{
          protocol = $protocol
          protocol_index = [array]::IndexOf(@("tcp", "udp"), $protocol)
          payload_bytes = $payload
          payload_index = $payloadIndex
          window_capacity = [int64]$window
          window_index = $windowIndex
          boundary = $payload -in [int64[]]$boundaries
        }
        ++$windowIndex
      }
    }
  }
}

function Get-CflowTransportPayloadLabel {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$PayloadBytes,

    [switch]$Boundary
  )

  if ($Boundary) {
    return "$($PayloadBytes.ToString('N0', [cultureinfo]::InvariantCulture)) B (max datagram)"
  }
  if ($PayloadBytes % 1024 -eq 0) {
    return "$([int64]($PayloadBytes / 1024)) KiB"
  }
  return "$($PayloadBytes.ToString('N0', [cultureinfo]::InvariantCulture)) B"
}

function Format-CflowTransportNumber {
  param(
    [Parameter(Mandatory = $true)]
    [double]$Value,

    [switch]$AllowZero
  )

  if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value) -or
      $Value -lt 0.0 -or (-not $AllowZero -and $Value -eq 0.0)) {
    $requirement = if ($AllowZero) { "non-negative" } else { "positive" }
    throw "Transport report values must be finite and $requirement"
  }
  return $Value.ToString("N3", [cultureinfo]::InvariantCulture)
}

function Get-CflowTransportComparison {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Comparisons,

    [Parameter(Mandatory = $true)]
    [string]$Backend,

    [Parameter(Mandatory = $true)]
    [string]$Protocol,

    [Parameter(Mandatory = $true)]
    [long]$PayloadBytes,

    [Parameter(Mandatory = $true)]
    [long]$WindowCapacity
  )

  $matching = @($Comparisons | Where-Object {
      $_.backend -eq $Backend -and $_.protocol -eq $Protocol -and
      [int64]$_.payload_bytes -eq $PayloadBytes -and
      [int64]$_.window_capacity -eq $WindowCapacity
    })
  if ($matching.Count -ne 1) {
    throw "Expected one $Protocol/$Backend/$PayloadBytes/window=$WindowCapacity transport comparison, found $($matching.Count)"
  }
  return $matching[0]
}

function Format-CflowTransportReports {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Comparisons,

    [Parameter(Mandatory = $true)]
    [string[]]$Backends,

    [Parameter(Mandatory = $true)]
    [System.Collections.IDictionary]$Matrix
  )

  if ($Backends.Count -eq 0 -or
      @($Backends | Sort-Object -Unique).Count -ne $Backends.Count) {
    throw "Transport report backends must be non-empty and unique"
  }
  $cases = @(Get-CflowTransportBenchmarkCases -Matrix $Matrix)
  $oneKiBLines = @("### 1 KiB transport comparison")
  $latencyLines = @("### Payload latency scaling")
  $throughputLines = @("### Payload throughput scaling")
  foreach ($backend in $Backends) {
    foreach ($protocol in @("tcp", "udp")) {
      $entry = $Matrix[$protocol]
      $throughputWindow = [int64](
        $cases | Where-Object {
          $_.protocol -eq $protocol -and $_.payload_bytes -eq 1024
        } | Measure-Object -Property window_capacity -Maximum).Maximum
      $oneKiBLatency = Get-CflowTransportComparison `
        -Comparisons $Comparisons -Backend $backend -Protocol $protocol `
        -PayloadBytes 1024 -WindowCapacity 1
      $oneKiBThroughput = Get-CflowTransportComparison `
        -Comparisons $Comparisons -Backend $backend -Protocol $protocol `
        -PayloadBytes 1024 -WindowCapacity $throughputWindow
      $protocolName = $protocol.ToUpperInvariant()

      $oneKiBLines += ""
      $oneKiBLines += "#### $protocolName ($backend)"
      $oneKiBLines += ""
      $oneKiBLines += "Latency window: 1. Throughput window: $throughputWindow."
      $oneKiBLines += ""
      $oneKiBLines += "| Model | P50 us | P50/Direct | P99 us | P99/Direct | Echo/s | Echo/s/Direct | MiB/s | MiB/s/Direct |"
      $oneKiBLines += "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
      foreach ($driver in @("Direct", "Actor", "Source")) {
        $prefix = $driver.ToLowerInvariant()
        $p50Ratio = if ($driver -eq "Direct") { 1.0 } else {
          [double]$oneKiBLatency."paired_${prefix}_direct_p50_ratio"
        }
        $p99Ratio = if ($driver -eq "Direct") { 1.0 } else {
          [double]$oneKiBLatency."paired_${prefix}_direct_p99_ratio"
        }
        $echoRatio = if ($driver -eq "Direct") { 1.0 } else {
          [double]$oneKiBThroughput."paired_${prefix}_direct_echo_ratio"
        }
        $applicationRatio = if ($driver -eq "Direct") { 1.0 } else {
          [double]$oneKiBThroughput."paired_${prefix}_direct_application_mib_ratio"
        }
        $oneKiBLines += "| $driver | $(Format-CflowTransportNumber ([double]$oneKiBLatency."${prefix}_median_p50_ns" / 1000.0)) | $(Format-CflowTransportNumber $p50Ratio) | $(Format-CflowTransportNumber ([double]$oneKiBLatency."${prefix}_median_p99_ns" / 1000.0)) | $(Format-CflowTransportNumber $p99Ratio) | $(Format-CflowTransportNumber $oneKiBThroughput."${prefix}_median_echo_per_second") | $(Format-CflowTransportNumber $echoRatio) | $(Format-CflowTransportNumber $oneKiBThroughput."${prefix}_median_application_mib_per_second") | $(Format-CflowTransportNumber $applicationRatio) |"
      }

      $latencyLines += ""
      $latencyLines += "#### $protocolName ($backend)"
      $latencyLines += ""
      if ($protocol -eq "udp") {
        $latencyLines += "Bounded datagram pipeline with one in flight (window 1)."
      } else {
        $latencyLines += "Sequential round trip, window 1."
      }
      $latencyLines += ""
      $latencyLines += "| Payload | Model | P50 us | P50/Direct | P99 us | P99/Direct |"
      $latencyLines += "| :--- | :--- | ---: | ---: | ---: | ---: |"
      $throughputLines += ""
      $throughputLines += "#### $protocolName ($backend)"
      $throughputLines += ""
      if ($protocol -eq "udp") {
        $maximumInFlightKiB = [int64]($entry.maximum_in_flight_bytes / 1024)
        $throughputLines += "Window is capped at $throughputWindow operations and $maximumInFlightKiB KiB of payload in flight; the maximum datagram boundary uses window 1."
      } else {
        $throughputLines += "Sequential round trip, window $throughputWindow."
      }
      $throughputLines += ""
      $throughputLines += "| Payload | Model | Window | Echo/s | Echo/s/Direct | MiB/s | MiB/s/Direct |"
      $throughputLines += "| :--- | :--- | ---: | ---: | ---: | ---: | ---: |"

      foreach ($payload in @($entry.payload_bytes | Select-Object -Skip 1)) {
        $isBoundary = [int64]$payload -in [int64[]]@($entry.boundary_payload_bytes)
        $payloadLabel = Get-CflowTransportPayloadLabel `
          -PayloadBytes ([int64]$payload) -Boundary:$isBoundary
        $latency = Get-CflowTransportComparison `
          -Comparisons $Comparisons -Backend $backend -Protocol $protocol `
          -PayloadBytes ([int64]$payload) -WindowCapacity 1
        $payloadThroughputWindow = [int64](
          $cases | Where-Object {
            $_.protocol -eq $protocol -and
            $_.payload_bytes -eq [int64]$payload
          } | Measure-Object -Property window_capacity -Maximum).Maximum
        $throughput = Get-CflowTransportComparison `
          -Comparisons $Comparisons -Backend $backend -Protocol $protocol `
          -PayloadBytes ([int64]$payload) `
          -WindowCapacity $payloadThroughputWindow
        foreach ($driver in @("Direct", "Actor", "Source")) {
          $prefix = $driver.ToLowerInvariant()
          $p50Ratio = if ($driver -eq "Direct") { 1.0 } else {
            [double]$latency."paired_${prefix}_direct_p50_ratio"
          }
          $p99Ratio = if ($driver -eq "Direct") { 1.0 } else {
            [double]$latency."paired_${prefix}_direct_p99_ratio"
          }
          $echoRatio = if ($driver -eq "Direct") { 1.0 } else {
            [double]$throughput."paired_${prefix}_direct_echo_ratio"
          }
          $applicationRatio = if ($driver -eq "Direct") { 1.0 } else {
            [double]$throughput."paired_${prefix}_direct_application_mib_ratio"
          }
          $latencyLines += "| $payloadLabel | $driver | $(Format-CflowTransportNumber ([double]$latency."${prefix}_median_p50_ns" / 1000.0)) | $(Format-CflowTransportNumber $p50Ratio) | $(Format-CflowTransportNumber ([double]$latency."${prefix}_median_p99_ns" / 1000.0)) | $(Format-CflowTransportNumber $p99Ratio) |"
          $throughputLines += "| $payloadLabel | $driver | $payloadThroughputWindow | $(Format-CflowTransportNumber $throughput."${prefix}_median_echo_per_second") | $(Format-CflowTransportNumber $echoRatio) | $(Format-CflowTransportNumber $throughput."${prefix}_median_application_mib_per_second") | $(Format-CflowTransportNumber $applicationRatio) |"
        }
      }
    }
  }

  return [pscustomobject][ordered]@{
    one_kib = $oneKiBLines -join "`n"
    latency = $latencyLines -join "`n"
    throughput = $throughputLines -join "`n"
  }
}

function Format-CflowHandoffReport {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Summaries,

    [Parameter(Mandatory = $true)]
    [string[]]$Backends,

    [string[]]$Protocols = @("tcp", "udp")
  )

  if ($Summaries.Count -eq 0 -or $Backends.Count -eq 0 -or
      $Protocols.Count -eq 0 -or
      @($Backends | Sort-Object -Unique).Count -ne $Backends.Count -or
      @($Protocols | Sort-Object -Unique).Count -ne $Protocols.Count -or
      @($Protocols | Where-Object { $_ -notin @("tcp", "udp") }).Count -ne 0) {
    throw "Handoff summaries, unique backends, and TCP/UDP protocols must be valid"
  }
  $lines = @("### Transport handoff timing")
  foreach ($backend in $Backends) {
    foreach ($protocol in $Protocols) {
      $matching = @($Summaries | Where-Object {
          $_.backend -eq $backend -and $_.protocol -eq $protocol
        } | Sort-Object { [int64]$_.payload_bytes })
      if ($matching.Count -eq 0) {
        throw "Expected handoff summaries for $protocol/$backend"
      }
      $lines += ""
      $lines += "#### $($protocol.ToUpperInvariant()) ($backend)"
      $lines += ""
      $lines += "| Payload | Model | Dispatch | Execution | Drive other | Completion ready | Wake resume | Wait other |"
      $lines += "| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: |"
      foreach ($summary in $matching) {
        $payloadLabel = Get-CflowTransportPayloadLabel `
          -PayloadBytes ([int64]$summary.payload_bytes)
        foreach ($driver in @("Actor", "Source")) {
          $prefix = $driver.ToLowerInvariant()
          $required = @(
            "${prefix}_median_dispatch_mean_ns",
            "${prefix}_median_executor_mean_ns",
            "${prefix}_median_drive_residual_mean_ns",
            "${prefix}_median_completion_ready_mean_ns",
            "${prefix}_median_wake_resume_mean_ns",
            "${prefix}_median_wait_residual_mean_ns"
          )
          foreach ($field in $required) {
            if ($null -eq $summary.PSObject.Properties[$field] -or
                $null -eq $summary.$field) {
              throw "Handoff summary is missing $field for $protocol/$backend/$($summary.payload_bytes)"
            }
          }
          $formatted = @($required | ForEach-Object {
              Format-CflowTransportNumber -Value ([double]$summary.$_) `
                -AllowZero
            })
          $lines += "| $payloadLabel | $driver | $($formatted -join ' | ') |"
        }
      }
    }
  }
  return $lines -join "`n"
}
