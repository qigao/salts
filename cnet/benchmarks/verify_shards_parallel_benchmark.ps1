param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [Parameter(Mandatory = $true)]
  [string]$ControlPath,
  [string]$Backend = "",
  [string]$Topology = "",
  [int]$CpuA = -1,
  [int]$CpuB = -1
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

foreach ($required in @($Path, $ControlPath)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "required CNet paired benchmark CSV not found: $required"
  }
}

$rows = @(Import-Csv -LiteralPath $Path)
$controls = @(Import-Csv -LiteralPath $ControlPath)

if ($rows.Count -ne 14) {
  throw "expected 14 shared-engine rows, got $($rows.Count)"
}
if ($controls.Count -ne 28) {
  throw "expected 28 independent-owner control rows, got $($controls.Count)"
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

$controlByKey = @{}
foreach ($control in $controls) {
  if ($control.mode -ne "two_owner_parallel") {
    continue
  }
  $key = "$($control.payload_bytes)/$($control.repeat)"
  if ($controlByKey.ContainsKey($key)) {
    throw "duplicate two-owner control row: $key"
  }
  $controlByKey[$key] = $control
}

if ($controlByKey.Count -ne 14) {
  throw "expected 14 two-owner control rows, got $($controlByKey.Count)"
}

$seen = @{}
$retentionByPayload = @{
  "1024" = @()
  "65536" = @()
}
$p99RatioByPayload = @{
  "1024" = @()
  "65536" = @()
}
$cpuRatioByPayload = @{
  "1024" = @()
  "65536" = @()
}

foreach ($row in $rows) {
  if ($row.mode -ne "shared_engine_parallel") {
    throw "unexpected shared-engine mode: $($row.mode)"
  }

  $payload = Parse-U64 $row.payload_bytes "payload_bytes"
  if ($payload -notin @(1024, 65536)) {
    throw "unexpected payload: $payload"
  }
  $repeat = Parse-U64 $row.repeat "repeat"
  if ($repeat -lt 1 -or $repeat -gt 7) {
    throw "unexpected repeat: $repeat"
  }

  if (-not [string]::IsNullOrWhiteSpace($Backend) -and
      $row.backend -ne $Backend) {
    throw "shared backend mismatch: expected=$Backend got=$($row.backend)"
  }
  if (-not [string]::IsNullOrWhiteSpace($Topology) -and
      $row.topology -ne $Topology) {
    throw "shared topology mismatch: expected=$Topology got=$($row.topology)"
  }

  $cpuAValue = Parse-I32 $row.cpu_a "cpu_a"
  $cpuBValue = Parse-I32 $row.cpu_b "cpu_b"
  if ($CpuA -ge 0 -and $cpuAValue -ne $CpuA) {
    throw "shared cpu_a mismatch: expected=$CpuA got=$cpuAValue"
  }
  if ($CpuB -ge 0 -and $cpuBValue -ne $CpuB) {
    throw "shared cpu_b mismatch: expected=$CpuB got=$cpuBValue"
  }

  $shardA = Parse-U64 $row.shard_a "shard_a"
  $shardB = Parse-U64 $row.shard_b "shard_b"
  if ($shardA -ne 0 -or $shardB -ne 1) {
    throw "round-robin fixed owner assignment mismatch: shard_a=$shardA shard_b=$shardB"
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
    throw "shared equal-work/lifecycle mismatch for payload=$payload repeat=$repeat"
  }
  if ($wall -eq 0 -or $ownerCpu -eq 0 -or
      $p50 -eq 0 -or $p95 -lt $p50 -or $p99 -lt $p95 -or
      $rate -le 0.0 -or $mib -le 0.0 -or
      $cpuUs -le 0.0 -or $polls -eq 0) {
    throw "invalid shared metrics for payload=$payload repeat=$repeat"
  }

  $key = "$payload/$repeat"
  if ($seen.ContainsKey($key)) {
    throw "duplicate shared row: $key"
  }
  $seen[$key] = $true

  if (-not $controlByKey.ContainsKey($key)) {
    throw "missing independent-owner control row: $key"
  }
  $control = $controlByKey[$key]

  if ($control.backend -ne $row.backend -or
      $control.topology -ne $row.topology) {
    throw "paired backend/topology mismatch for $key"
  }

  $controlCpuA = Parse-I32 $control.cpu_a "control cpu_a"
  $controlCpuB = Parse-I32 $control.cpu_b "control cpu_b"
  $controlLogical = Parse-U64 $control.logical_operations "control logical_operations"
  if ($controlCpuA -ne $cpuAValue -or
      $controlCpuB -ne $cpuBValue -or
      $controlLogical -ne $logical) {
    throw "paired affinity/equal-work mismatch for $key"
  }

  $controlRate = Parse-Double $control.operations_per_second "control rate"
  $controlP99 = Parse-U64 $control.p99_ns "control p99"
  $controlCpuUs = Parse-Double $control.owner_cpu_us_per_op "control cpu"
  if ($controlRate -le 0.0 -or $controlP99 -eq 0 -or $controlCpuUs -le 0.0) {
    throw "invalid independent-owner control metrics for $key"
  }

  $payloadKey = [string]$payload
  $retentionByPayload[$payloadKey] += ($rate / $controlRate)
  $p99RatioByPayload[$payloadKey] += ([double]$p99 / [double]$controlP99)
  $cpuRatioByPayload[$payloadKey] += ($cpuUs / $controlCpuUs)
}

foreach ($payload in @(1024, 65536)) {
  foreach ($repeat in 1..7) {
    $key = "$payload/$repeat"
    if (-not $seen.ContainsKey($key)) {
      throw "missing shared row: $key"
    }
  }

  $payloadKey = [string]$payload
  $retention = @($retentionByPayload[$payloadKey] | Sort-Object)
  $p99Ratios = @($p99RatioByPayload[$payloadKey] | Sort-Object)
  $cpuRatios = @($cpuRatioByPayload[$payloadKey] | Sort-Object)
  $mid = [int][Math]::Floor($retention.Count / 2)

  Write-Host (
    "CNet shared-engine paired evidence: backend={0} topology={1} payload={2} throughput_retention={3:N3}x p99_ratio={4:N3}x owner_cpu_ratio={5:N3}x" -f
      $rows[0].backend,
      $rows[0].topology,
      $payload,
      $retention[$mid],
      $p99Ratios[$mid],
      $cpuRatios[$mid]
  )
}

Write-Host "CNet shared multi-owner engine matrix verified"
