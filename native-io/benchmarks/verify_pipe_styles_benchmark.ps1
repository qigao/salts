param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [string]$Backend = ""
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "PIPE style benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
$styles = @("direct", "coroutine", "sharded_same_owner", "sharded_cross_owner")
$payloads = @(1024, 8192, 32768, 65536)
$expectedRows = $styles.Count * $payloads.Count
if ($rows.Count -ne $expectedRows) {
  throw "expected $expectedRows PIPE style rows, got $($rows.Count)"
}

$required = @(
  "backend",
  "style",
  "payload_bytes",
  "transfers",
  "p50_us",
  "p95_us",
  "mib_per_second",
  "message_hops_per_transfer",
  "same_owner_direct_tasks_per_transfer",
  "queued_dispatches",
  "rejected_tasks",
  "peak_command_slots"
)
foreach ($name in $required) {
  if (-not ($rows[0].PSObject.Properties.Name -contains $name)) {
    throw "missing PIPE style benchmark column: $name"
  }
}

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Integer,
                              $Invariant, [ref]$parsed)) {
    throw "invalid unsigned integer for $($Name): $Value"
  }
  return $parsed
}

function Parse-Double([object]$Value, [string]$Name) {
  $parsed = [double]0
  if (-not [double]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Float,
                              $Invariant, [ref]$parsed)) {
    throw "invalid double for $($Name): $Value"
  }
  return $parsed
}

$seen = @{}
foreach ($row in $rows) {
  if (-not [string]::IsNullOrWhiteSpace($Backend) -and $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }
  if ($styles -notcontains $row.style) {
    throw "unexpected PIPE style: $($row.style)"
  }

  $payload = Parse-U64 $row.payload_bytes "payload_bytes"
  if ($payloads -notcontains [int]$payload) {
    throw "unexpected PIPE payload: $payload"
  }
  $key = "$($row.style)|$payload"
  if ($seen.ContainsKey($key)) {
    throw "duplicate PIPE style cell: $key"
  }
  $seen[$key] = $true

  $transfers = Parse-U64 $row.transfers "transfers"
  $p50 = Parse-Double $row.p50_us "p50_us"
  $p95 = Parse-Double $row.p95_us "p95_us"
  $rate = Parse-Double $row.mib_per_second "mib_per_second"
  $hops = Parse-Double $row.message_hops_per_transfer "message_hops_per_transfer"
  $direct = Parse-Double $row.same_owner_direct_tasks_per_transfer "same_owner_direct_tasks_per_transfer"
  $queued = Parse-U64 $row.queued_dispatches "queued_dispatches"
  $rejected = Parse-U64 $row.rejected_tasks "rejected_tasks"
  $peak = Parse-U64 $row.peak_command_slots "peak_command_slots"

  if ($transfers -eq 0 -or $p50 -le 0.0 -or $p95 -le 0.0 -or $rate -le 0.0) {
    throw "non-positive timing/rate in cell $key"
  }
  if ($p95 -lt $p50) {
    throw "p95 must be >= p50 in cell $key"
  }
  if ($rejected -ne 0) {
    throw "measured PIPE style cell rejected accepted work: $key"
  }

  switch ($row.style) {
    "direct" {
      if ($hops -ne 0.0 -or $direct -ne 0.0 -or $queued -ne 0 -or $peak -ne 0) {
        throw "Direct PIPE cell must not carry Sharded routing counters: $key"
      }
    }
    "coroutine" {
      if ($hops -ne 0.0 -or $direct -ne 0.0 -or $queued -ne 0 -or $peak -ne 0) {
        throw "Coroutine PIPE cell must not carry Sharded routing counters: $key"
      }
    }
    "sharded_same_owner" {
      if ($hops -ne 0.0 -or $queued -ne 0) {
        throw "same-owner Sharded PIPE must remain zero-hop: $key"
      }
      if ($direct -lt 2.0) {
        throw "same-owner Sharded PIPE must exercise at least read+write direct tasks/transfer: $key"
      }
      if ($peak -eq 0) {
        throw "same-owner Sharded runtime must expose bounded command-slot activity: $key"
      }
    }
    "sharded_cross_owner" {
      if ($direct -ne 0.0) {
        throw "cross-owner Sharded PIPE must not use same-owner direct routing: $key"
      }
      if ($hops -lt 3.0) {
        throw "cross-owner Sharded PIPE must route read, write, and owner observe: $key"
      }
      if ($queued -lt $transfers * 3) {
        throw "cross-owner queued-dispatch count is below the minimum route count: $key"
      }
      if ($peak -eq 0) {
        throw "cross-owner Sharded runtime must expose bounded command-slot activity: $key"
      }
    }
  }
}

foreach ($style in $styles) {
  foreach ($payload in $payloads) {
    $key = "$style|$payload"
    if (-not $seen.ContainsKey($key)) {
      throw "missing PIPE style cell: $key"
    }
  }
}

Write-Host "NativeIO PIPE execution-style benchmark structure verified: backend=$($rows[0].backend)"
