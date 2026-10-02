param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "Coroutine Executor dequeue batch benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 28) {
  throw "expected 28 dequeue batch benchmark rows, got $($rows.Count)"
}

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse(
      [string]$Value,
      [System.Globalization.NumberStyles]::Integer,
      $Invariant,
      [ref]$parsed)) {
    throw "invalid unsigned integer for \${Name}: $Value"
  }
  return $parsed
}

function Parse-Double([object]$Value, [string]$Name) {
  $parsed = [double]0
  if (-not [double]::TryParse(
      [string]$Value,
      [System.Globalization.NumberStyles]::Float,
      $Invariant,
      [ref]$parsed)) {
    throw "invalid double for \${Name}: $Value"
  }
  return $parsed
}

$seen = @{}
foreach ($row in $rows) {
  if ($row.style -notin @("per_item_take", "batch_take")) {
    throw "unexpected style: $($row.style)"
  }
  if ($row.occupancy -notin @("low", "near_capacity")) {
    throw "unexpected occupancy: $($row.occupancy)"
  }

  $batch = Parse-U64 $row.batch_size "batch_size"
  if ($batch -notin @(2, 4, 8, 16, 32, 64, 128)) {
    throw "unexpected batch size: $batch"
  }
  $key = "$($row.style):$($row.occupancy):$batch"
  if ($seen.ContainsKey($key)) { throw "duplicate row: $key" }
  $seen[$key] = $true

  $tasks = Parse-U64 $row.tasks_per_replicate "tasks_per_replicate"
  $replicates = Parse-U64 $row.replicates "replicates"
  $p50 = Parse-Double $row.p50_ns_per_task "p50"
  $p95 = Parse-Double $row.p95_ns_per_task "p95"
  $rate = Parse-Double $row.median_tasks_per_second "rate"
  $locks = Parse-Double $row.dequeue_locks_per_task "dequeue_locks_per_task"
  $broadcasts = Parse-Double $row.queue_space_broadcasts_per_task "queue_space_broadcasts_per_task"
  $submitted = Parse-U64 $row.submitted_tasks "submitted_tasks"
  $completed = Parse-U64 $row.completed_tasks "completed_tasks"
  $rejected = Parse-U64 $row.rejected_tasks "rejected_tasks"
  $orderErrors = Parse-U64 $row.order_errors "order_errors"

  $expectedTasks =
    if ($row.occupancy -eq "low") { [UInt64]4096 } else { [UInt64]8192 }
  if ($tasks -ne $expectedTasks) {
    throw "task count mismatch: $key expected=$expectedTasks got=$tasks"
  }
  if ($replicates -ne 11 -or $p50 -le 0.0 -or
      $p95 -lt $p50 -or $rate -le 0.0) {
    throw "invalid timing/replicate row: $key"
  }

  $expectedPerReplicate = [UInt64]128 + $expectedTasks
  $expectedSubmitted = $expectedPerReplicate * $replicates
  if ($submitted -ne $expectedSubmitted -or
      $completed -ne $expectedSubmitted -or
      $rejected -ne 0 -or $orderErrors -ne 0) {
    throw "lifecycle/accounting mismatch: $key expected=$expectedSubmitted submitted=$submitted completed=$completed rejected=$rejected orderErrors=$orderErrors"
  }

  $expectedRatio =
    if ($row.style -eq "batch_take") {
      1.0 / [double]$batch
    } else {
      1.0
    }
  if ([Math]::Abs($locks - $expectedRatio) -gt 0.000001 -or
      [Math]::Abs($broadcasts - $expectedRatio) -gt 0.000001) {
    throw "dequeue lock/broadcast contract mismatch: $key"
  }
}

foreach ($occupancy in @("low", "near_capacity")) {
  foreach ($batch in @(2, 4, 8, 16, 32, 64, 128)) {
    foreach ($style in @("per_item_take", "batch_take")) {
      $key = "\${style}:\${occupancy}:$batch"
      if (-not $seen.ContainsKey($key)) {
        throw "missing row: $key"
      }
    }
  }
}

Write-Host "Coroutine Executor consumer-side dequeue batch matrix verified"
