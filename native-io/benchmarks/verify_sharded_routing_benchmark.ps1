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
if ($rows.Count -ne 2) {
  throw "expected exactly two sharded routing rows, got $($rows.Count)"
}

$required = @(
  "backend",
  "style",
  "iterations_per_replicate",
  "replicates",
  "p50_batch_ns_per_op",
  "p95_batch_ns_per_op",
  "median_ops_per_second",
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

$byStyle = @{}
foreach ($row in $rows) {
  if ($byStyle.ContainsKey($row.style)) {
    throw "duplicate sharded routing style: $($row.style)"
  }
  $byStyle[$row.style] = $row
  if (-not [string]::IsNullOrWhiteSpace($Backend) -and $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }
  $iterations = Parse-U64 $row.iterations_per_replicate "iterations_per_replicate"
  $replicates = Parse-U64 $row.replicates "replicates"
  $p50 = Parse-Double $row.p50_batch_ns_per_op "p50_batch_ns_per_op"
  $p95 = Parse-Double $row.p95_batch_ns_per_op "p95_batch_ns_per_op"
  $rate = Parse-Double $row.median_ops_per_second "median_ops_per_second"
  if ($iterations -eq 0 -or $replicates -eq 0) {
    throw "iterations/replicates must be positive for $($row.style)"
  }
  if ($p50 -le 0.0 -or $p95 -le 0.0 -or $rate -le 0.0) {
    throw "timing/rate must be positive for $($row.style)"
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

foreach ($style in @("same_owner", "cross_owner")) {
  if (-not $byStyle.ContainsKey($style)) {
    throw "missing sharded routing style: $style"
  }
}

$same = $byStyle["same_owner"]
$cross = $byStyle["cross_owner"]

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

$crossIterations = Parse-U64 $cross.iterations_per_replicate "cross iterations"
$crossReplicates = Parse-U64 $cross.replicates "cross replicates"
$crossTotal = $crossIterations * $crossReplicates
if ((Parse-U64 $cross.message_hops_per_op "cross message_hops_per_op") -ne 1) {
  throw "cross-owner path must report exactly one routing hop"
}
if ((Parse-U64 $cross.same_shard_direct_tasks "cross direct tasks") -ne 0) {
  throw "cross-owner measured path must not use the same-shard direct fast path"
}
if ((Parse-U64 $cross.queued_dispatches "cross queued dispatches") -ne $crossTotal) {
  throw "cross-owner queued-dispatch count must equal measured operations"
}

Write-Host "NativeIO Sharded routing benchmark structure verified: backend=$($same.backend)"
