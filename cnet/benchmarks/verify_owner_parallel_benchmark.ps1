param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [string]$Backend = "",
  [string]$Topology = "",
  [int]$CpuA = -1,
  [int]$CpuB = -1
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "CNet owner-parallel CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 28) {
  throw "expected 28 CNet owner-parallel rows, got $($rows.Count)"
}

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse(
      [string]$Value,
      [System.Globalization.NumberStyles]::Integer,
      $Invariant,
      [ref]$parsed)) {
    throw "invalid unsigned integer for $Name : $Value"
  }
  return $parsed
}

function Parse-I32([object]$Value, [string]$Name) {
  $parsed = [int]0
  if (-not [int]::TryParse(
      [string]$Value,
      [System.Globalization.NumberStyles]::Integer,
      $Invariant,
      [ref]$parsed)) {
    throw "invalid integer for $Name : $Value"
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
    throw "invalid double for $Name : $Value"
  }
  return $parsed
}

$expectedModes = @("one_owner_serial", "two_owner_parallel")
$expectedPayloads = @(1024, 65536)
$seen = @{}
$byCell = @{}

foreach ($row in $rows) {
  if ($row.mode -notin $expectedModes) {
    throw "unexpected mode: $($row.mode)"
  }
  $payload = Parse-U64 $row.payload_bytes "payload_bytes"
  if ($payload -notin $expectedPayloads) {
    throw "unexpected payload: $payload"
  }
  $repeat = Parse-U64 $row.repeat "repeat"
  if ($repeat -lt 1 -or $repeat -gt 7) {
    throw "unexpected repeat: $repeat"
  }
  if (-not [string]::IsNullOrWhiteSpace($Backend) -and
      $row.backend -ne $Backend) {
    throw "backend mismatch: expected=$Backend got=$($row.backend)"
  }
  if (-not [string]::IsNullOrWhiteSpace($Topology) -and
      $row.topology -ne $Topology) {
    throw "topology mismatch: expected=$Topology got=$($row.topology)"
  }

  $cpuAValue = Parse-I32 $row.cpu_a "cpu_a"
  $cpuBValue = Parse-I32 $row.cpu_b "cpu_b"
  if ($CpuA -ge 0 -and $cpuAValue -ne $CpuA) {
    throw "cpu_a mismatch: expected=$CpuA got=$cpuAValue"
  }
  if ($row.mode -eq "one_owner_serial") {
    if ($cpuBValue -ne $cpuAValue) {
      throw "serial row must use one owner CPU: cpu_a=$cpuAValue cpu_b=$cpuBValue"
    }
  } elseif ($CpuB -ge 0 -and $cpuBValue -ne $CpuB) {
    throw "parallel cpu_b mismatch: expected=$CpuB got=$cpuBValue"
  }

  $logical = Parse-U64 $row.logical_operations "logical_operations"
  $wall = Parse-U64 $row.wall_ns "wall_ns"
  $ownerCpu = Parse-U64 $row.owner_cpu_ns "owner_cpu_ns"
  $p50 = Parse-U64 $row.p50_ns "p50_ns"
  $p95 = Parse-U64 $row.p95_ns "p95_ns"
  $p99 = Parse-U64 $row.p99_ns "p99_ns"
  $rate = Parse-Double $row.operations_per_second "operations_per_second"
  $mib = Parse-Double $row.mib_per_second "mib_per_second"
  $cpuUs = Parse-Double $row.owner_cpu_us_per_op "owner_cpu_us_per_op"
  $polls = Parse-U64 $row.poll_calls "poll_calls"
  $sends = Parse-U64 $row.send_terminals "send_terminals"
  $receives = Parse-U64 $row.receive_terminals "receive_terminals"

  if ($logical -ne 128 -or $sends -ne 128 -or $receives -ne 128) {
    throw "equal-work/lifecycle mismatch for $($row.mode)/$payload/repeat=$repeat"
  }
  if ($wall -eq 0 -or $ownerCpu -eq 0 -or
      $p50 -eq 0 -or $p95 -lt $p50 -or $p99 -lt $p95 -or
      $rate -le 0.0 -or $mib -le 0.0 -or $cpuUs -le 0.0 -or
      $polls -eq 0) {
    throw "invalid metrics for $($row.mode)/$payload/repeat=$repeat"
  }

  $key = "$payload/$($row.mode)/$repeat"
  if ($seen.ContainsKey($key)) {
    throw "duplicate row: $key"
  }
  $seen[$key] = $true

  $cell = "$payload/$repeat"
  if (-not $byCell.ContainsKey($cell)) {
    $byCell[$cell] = @{}
  }
  $byCell[$cell][$row.mode] = $rate
}

foreach ($payload in $expectedPayloads) {
  $speedups = @()
  foreach ($repeat in 1..7) {
    foreach ($mode in $expectedModes) {
      $key = "$payload/$mode/$repeat"
      if (-not $seen.ContainsKey($key)) {
        throw "missing row: $key"
      }
    }
    $cell = "$payload/$repeat"
    $serial = [double]$byCell[$cell]["one_owner_serial"]
    $parallel = [double]$byCell[$cell]["two_owner_parallel"]
    $speedups += ($parallel / $serial)
  }
  $ordered = @($speedups | Sort-Object)
  $median = $ordered[[int][Math]::Floor($ordered.Count / 2)]
  Write-Host ("CNet owner-parallel evidence: backend={0} topology={1} payload={2} median_speedup={3:N3}x" -f $rows[0].backend, $rows[0].topology, $payload, $median)
}

Write-Host "CNet independent-owner equal-work matrix verified"
