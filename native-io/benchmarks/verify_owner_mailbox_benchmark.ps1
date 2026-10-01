param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [string]$Backend = "",
  [string]$CpuSet = ""
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "owner-mailbox benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 7) {
  throw "expected seven owner-mailbox window rows, got $($rows.Count)"
}

$required = @(
  "backend", "cpu_set", "window", "messages_per_replicate", "replicates",
  "p50_batch_ns_per_message", "p95_batch_ns_per_message",
  "median_messages_per_second", "data_hops", "control_hops",
  "queued_dispatches", "rejected_tasks", "same_shard_direct_tasks",
  "peak_command_slots"
)
foreach ($name in $required) {
  if (-not ($rows[0].PSObject.Properties.Name -contains $name)) {
    throw "missing owner-mailbox benchmark column: $name"
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

$byWindow = @{}
foreach ($row in $rows) {
  $window = Parse-U64 $row.window "window"
  if ($byWindow.ContainsKey($window)) { throw "duplicate owner-mailbox window: $window" }
  $byWindow[$window] = $row

  if (-not [string]::IsNullOrWhiteSpace($Backend) -and $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }
  if (-not [string]::IsNullOrWhiteSpace($CpuSet) -and $row.cpu_set -ne $CpuSet) {
    throw "cpu_set mismatch: expected $CpuSet, got $($row.cpu_set)"
  }

  $messages = Parse-U64 $row.messages_per_replicate "messages_per_replicate"
  $replicates = Parse-U64 $row.replicates "replicates"
  $p50 = Parse-Double $row.p50_batch_ns_per_message "p50_batch_ns_per_message"
  $p95 = Parse-Double $row.p95_batch_ns_per_message "p95_batch_ns_per_message"
  $rate = Parse-Double $row.median_messages_per_second "median_messages_per_second"
  if ($messages -eq 0 -or $replicates -eq 0 -or $p50 -le 0.0 -or
      $p95 -le 0.0 -or $rate -le 0.0) {
    throw "owner-mailbox metrics must be positive: window=$window"
  }
  if ($p95 -lt $p50) { throw "p95 must be >= p50: window=$window" }
  if ((Parse-U64 $row.rejected_tasks "rejected_tasks") -ne 0) {
    throw "owner-mailbox measured work must not reject: window=$window"
  }
  if ((Parse-U64 $row.same_shard_direct_tasks "same_shard_direct_tasks") -ne 0) {
    throw "owner-mailbox leaked through same-owner direct path: window=$window"
  }
  if ((Parse-U64 $row.peak_command_slots "peak_command_slots") -eq 0) {
    throw "peak command slots must prove bounded queued routing: window=$window"
  }

  $batches = [UInt64][Math]::Ceiling([double]$messages / [double]$window)
  $expectedData = $messages * $replicates
  $expectedControl = ($batches - 1) * $replicates
  $data = Parse-U64 $row.data_hops "data_hops"
  $control = Parse-U64 $row.control_hops "control_hops"
  $queued = Parse-U64 $row.queued_dispatches "queued_dispatches"
  if ($data -ne $expectedData) {
    throw "data-hop count mismatch: window=$window expected=$expectedData got=$data"
  }
  if ($control -ne $expectedControl) {
    throw "control-hop count mismatch: window=$window expected=$expectedControl got=$control"
  }
  if ($queued -ne ($expectedData + $expectedControl)) {
    throw "queued dispatch accounting mismatch: window=$window"
  }
}

foreach ($window in @(1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024)) {
  if (-not $byWindow.ContainsKey([UInt64]$window)) {
    throw "missing owner-mailbox window: $window"
  }
}

Write-Host "NativeIO owner-mailbox window matrix verified: backend=$($rows[0].backend) cpu_set=$($rows[0].cpu_set)"
