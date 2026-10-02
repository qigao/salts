param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "Coroutine Executor batch benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 28) {
  throw "expected 28 batch benchmark rows, got $($rows.Count)"
}

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Integer,
                              $Invariant, [ref]$parsed)) {
    throw "invalid unsigned integer for ${Name}: $Value"
  }
  return $parsed
}

function Parse-Double([object]$Value, [string]$Name) {
  $parsed = [double]0
  if (-not [double]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Float,
                              $Invariant, [ref]$parsed)) {
    throw "invalid double for ${Name}: $Value"
  }
  return $parsed
}

$seen = @{}
foreach ($row in $rows) {
  if ($row.style -notin @("per_item", "batch")) {
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

  $replicates = Parse-U64 $row.replicates "replicates"
  $p50 = Parse-Double $row.p50_admission_ns_per_task "p50"
  $p95 = Parse-Double $row.p95_admission_ns_per_task "p95"
  $rate = Parse-Double $row.median_admission_tasks_per_second "rate"
  $locks = Parse-Double $row.admission_locks_per_task "locks"
  $signals = Parse-Double $row.wake_signals_per_task "signals"
  $submitted = Parse-U64 $row.submitted_tasks "submitted_tasks"
  $rejected = Parse-U64 $row.rejected_tasks "rejected_tasks"

  if ($replicates -ne 11 -or $p50 -le 0.0 -or $p95 -lt $p50 -or $rate -le 0.0) {
    throw "invalid timing/replicate row: $key"
  }
  if ($submitted -ne ($batch * $replicates) -or $rejected -ne 0) {
    throw "admission accounting mismatch: $key"
  }

  $expected = if ($row.style -eq "batch") { 1.0 / [double]$batch } else { 1.0 }
  if ([Math]::Abs($locks - $expected) -gt 0.000001 -or
      [Math]::Abs($signals - $expected) -gt 0.000001) {
    throw "lock/signal contract mismatch: $key"
  }
}

foreach ($occupancy in @("low", "near_capacity")) {
  foreach ($batch in @(2, 4, 8, 16, 32, 64, 128)) {
    foreach ($style in @("per_item", "batch")) {
      $key = "${style}:${occupancy}:${batch}"
      if (-not $seen.ContainsKey($key)) { throw "missing row: $key" }
    }
  }
}

Write-Host "Coroutine Executor producer-side batch admission matrix verified"
