param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [string]$Backend = "",
  [string]$CpuSet = "",
  [ValidateSet("full","pr")]
  [string]$Profile = "full"
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "owner-handoff benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
$expectedWindows = if ($Profile -eq "pr") { @(1, 8, 64) } else { @(1, 2, 4, 8, 16, 32, 64) }
if ($rows.Count -ne $expectedWindows.Count) {
  throw "expected $($expectedWindows.Count) owner-handoff window rows for profile=$Profile, got $($rows.Count)"
}

$required = @(
  "backend",
  "cpu_set",
  "window",
  "hops_per_replicate",
  "replicates",
  "p50_batch_ns_per_hop",
  "p95_batch_ns_per_hop",
  "median_hops_per_second",
  "queued_dispatches",
  "rejected_tasks",
  "same_shard_direct_tasks",
  "peak_command_slots"
)
foreach ($name in $required) {
  if (-not ($rows[0].PSObject.Properties.Name -contains $name)) {
    throw "missing owner-handoff benchmark column: $name"
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
  if ($byWindow.ContainsKey($window)) {
    throw "duplicate owner-handoff window: $window"
  }
  $byWindow[$window] = $row

  if (-not [string]::IsNullOrWhiteSpace($Backend) -and $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }
  if (-not [string]::IsNullOrWhiteSpace($CpuSet) -and $row.cpu_set -ne $CpuSet) {
    throw "cpu_set mismatch: expected $CpuSet, got $($row.cpu_set)"
  }

  $hops = Parse-U64 $row.hops_per_replicate "hops_per_replicate"
  $replicates = Parse-U64 $row.replicates "replicates"
  $p50 = Parse-Double $row.p50_batch_ns_per_hop "p50_batch_ns_per_hop"
  $p95 = Parse-Double $row.p95_batch_ns_per_hop "p95_batch_ns_per_hop"
  $rate = Parse-Double $row.median_hops_per_second "median_hops_per_second"
  if ($hops -eq 0 -or $replicates -eq 0) {
    throw "hops/replicates must be positive: window=$window"
  }
  if ($p50 -le 0.0 -or $p95 -le 0.0 -or $rate -le 0.0) {
    throw "timing/rate metrics must be positive: window=$window"
  }
  if ($p95 -lt $p50) {
    throw "p95 must be >= p50: window=$window"
  }
  if ((Parse-U64 $row.rejected_tasks "rejected_tasks") -ne 0) {
    throw "owner handoff must not reject measured work: window=$window"
  }
  if ((Parse-U64 $row.same_shard_direct_tasks "same_shard_direct_tasks") -ne 0) {
    throw "owner handoff leaked through same-owner direct path: window=$window"
  }
  if ((Parse-U64 $row.peak_command_slots "peak_command_slots") -eq 0) {
    throw "peak command slots must prove bounded queued routing: window=$window"
  }

  $expectedQueued = $hops * $replicates
  if ((Parse-U64 $row.queued_dispatches "queued_dispatches") -ne $expectedQueued) {
    throw "queued owner hops must equal measured hops: window=$window expected=$expectedQueued"
  }
}

foreach ($window in $expectedWindows) {
  if (-not $byWindow.ContainsKey([UInt64]$window)) {
    throw "missing owner-handoff window: $window"
  }
}

Write-Host "NativeIO owner-to-owner handoff matrix verified: backend=$($rows[0].backend) cpu_set=$($rows[0].cpu_set)"
