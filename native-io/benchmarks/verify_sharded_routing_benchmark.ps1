param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [string]$Backend = ""
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "sharded routing benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 7) {
  throw "expected one same-owner row plus six cross-owner window rows, got $($rows.Count)"
}

$required = @(
  "backend",
  "style",
  "window",
  "iterations_per_replicate",
  "replicates",
  "p50_batch_ns_per_op",
  "p95_batch_ns_per_op",
  "median_ops_per_second",
  "median_submit_ns_per_op",
  "median_wait_ns_per_op",
  "message_hops_per_op",
  "same_shard_direct_tasks",
  "queued_dispatches",
  "rejected_tasks",
  "peak_command_slots"
)
foreach ($name in $required) {
  if (-not ($rows[0].PSObject.Properties.Name -contains $name)) {
    throw "missing sharded routing benchmark column: $name"
  }
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

$same = $null
$cross = @{}
foreach ($row in $rows) {
  if ($row.style -eq "same_owner") {
    if ($null -ne $same) { throw "duplicate same_owner row" }
    $same = $row
  } elseif ($row.style -eq "cross_owner") {
    $window = Parse-U64 $row.window "cross window"
    if ($cross.ContainsKey($window)) { throw "duplicate cross_owner window: $window" }
    $cross[$window] = $row
  } else {
    throw "unexpected sharded routing style: $($row.style)"
  }
  if (-not [string]::IsNullOrWhiteSpace($Backend) -and $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }
  $iterations = Parse-U64 $row.iterations_per_replicate "iterations_per_replicate"
  $replicates = Parse-U64 $row.replicates "replicates"
  $p50 = Parse-Double $row.p50_batch_ns_per_op "p50_batch_ns_per_op"
  $p95 = Parse-Double $row.p95_batch_ns_per_op "p95_batch_ns_per_op"
  $rate = Parse-Double $row.median_ops_per_second "median_ops_per_second"
  $submit = Parse-Double $row.median_submit_ns_per_op "median_submit_ns_per_op"
  $wait = Parse-Double $row.median_wait_ns_per_op "median_wait_ns_per_op"
  if ($iterations -eq 0 -or $replicates -eq 0) {
    throw "iterations/replicates must be positive for $($row.style)"
  }
  if ($p50 -le 0.0 -or $p95 -le 0.0 -or $rate -le 0.0 -or $submit -lt 0.0 -or $wait -lt 0.0) {
    throw "timing/rate attribution must be non-negative and wall metrics positive for $($row.style)"
  }
  if ($row.style -eq "cross_owner" -and ($submit -le 0.0 -or $wait -le 0.0)) {
    throw "cross-owner submit and wait attribution must both be positive: window=$($row.window)"
  }
  if ($p95 -lt $p50) {
    throw "p95 must be >= p50 for $($row.style)"
  }
  if ((Parse-U64 $row.rejected_tasks "rejected_tasks") -ne 0) {
    throw "measured routing must not reject accepted benchmark work for $($row.style)"
  }
  if ((Parse-U64 $row.peak_command_slots "peak_command_slots") -eq 0) {
    throw "peak command slots must prove bounded routing activity for $($row.style)"
  }
}

if ($null -eq $same) { throw "missing same_owner row" }
foreach ($window in @(1, 4, 8, 16, 32, 64)) {
  if (-not $cross.ContainsKey([UInt64]$window)) {
    throw "missing cross_owner window: $window"
  }
}

$sameIterations = Parse-U64 $same.iterations_per_replicate "same iterations"
$sameReplicates = Parse-U64 $same.replicates "same replicates"
$sameTotal = $sameIterations * $sameReplicates
if ((Parse-U64 $same.message_hops_per_op "same message_hops_per_op") -ne 0) {
  throw "same-owner path must report zero message hops"
}
if ((Parse-U64 $same.same_shard_direct_tasks "same direct tasks") -ne $sameTotal) {
  throw "same-owner direct-task count must equal measured operations"
}
if ((Parse-U64 $same.queued_dispatches "same queued dispatches") -ne 0) {
  throw "same-owner measured path must not report queued dispatches"
}

foreach ($window in @(1, 4, 8, 16, 32, 64)) {
  $row = $cross[[UInt64]$window]
  $crossIterations = Parse-U64 $row.iterations_per_replicate "cross iterations"
  $crossReplicates = Parse-U64 $row.replicates "cross replicates"
  $crossTotal = $crossIterations * $crossReplicates
  if ((Parse-U64 $row.window "cross window") -ne $window) {
    throw "cross-owner window mismatch: expected $window"
  }
  if ((Parse-U64 $row.message_hops_per_op "cross message_hops_per_op") -ne 1) {
    throw "cross-owner path must report exactly one routing hop: window=$window"
  }
  if ((Parse-U64 $row.same_shard_direct_tasks "cross direct tasks") -ne 0) {
    throw "cross-owner measured path must not use the same-shard direct fast path: window=$window"
  }
  if ((Parse-U64 $row.queued_dispatches "cross queued dispatches") -ne $crossTotal) {
    throw "cross-owner queued-dispatch count must equal measured operations: window=$window"
  }
}

Write-Host "NativeIO Sharded routing window matrix verified: backend=$($same.backend)"
