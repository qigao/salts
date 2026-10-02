param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [Parameter(Mandatory = $true)]
  [string]$BaselinePath,
  [string]$Backend = "",
  [string]$Topology = "",
  [int]$CpuA = -1,
  [int]$CpuB = -1
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

foreach ($required in @($Path, $BaselinePath)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "required CNet central/baseline CSV not found: $required"
  }
}

$rows = @(Import-Csv -LiteralPath $Path)
$baseline = @(Import-Csv -LiteralPath $BaselinePath)

if ($rows.Count -ne 14) {
  throw "expected 14 central-callback rows, got $($rows.Count)"
}
if ($baseline.Count -ne 14) {
  throw "expected 14 owner-affine baseline rows, got $($baseline.Count)"
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

$baseByKey = @{}
foreach ($row in $baseline) {
  if ($row.mode -ne "shared_engine_parallel") {
    throw "unexpected baseline mode: $($row.mode)"
  }
  $key = "$($row.payload_bytes)/$($row.repeat)"
  if ($baseByKey.ContainsKey($key)) {
    throw "duplicate baseline row: $key"
  }
  $baseByKey[$key] = $row
}

$seen = @{}
$retention = @{
  "1024" = @()
  "65536" = @()
}
$p99Ratio = @{
  "1024" = @()
  "65536" = @()
}
$ownerCpuRatio = @{
  "1024" = @()
  "65536" = @()
}
$totalCpuRatio = @{
  "1024" = @()
  "65536" = @()
}
$centralCpu = @{
  "1024" = @()
  "65536" = @()
}

foreach ($row in $rows) {
  if ($row.mode -ne "central_callback_compat") {
    throw "unexpected central mode: $($row.mode)"
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
  if ($CpuB -ge 0 -and $cpuBValue -ne $CpuB) {
    throw "cpu_b mismatch: expected=$CpuB got=$cpuBValue"
  }

  $shardA = Parse-U64 $row.shard_a "shard_a"
  $shardB = Parse-U64 $row.shard_b "shard_b"
  if ($shardA -ne 0 -or $shardB -ne 1) {
    throw "fixed owner assignment mismatch: shard_a=$shardA shard_b=$shardB"
  }

  $logical = Parse-U64 $row.logical_operations "logical_operations"
  $wall = Parse-U64 $row.wall_ns "wall_ns"
  $ownerCpuNs = Parse-U64 $row.owner_cpu_ns "owner_cpu_ns"
  $centralCpuNs = Parse-U64 $row.central_cpu_ns "central_cpu_ns"
  $p50 = Parse-U64 $row.p50_ns "p50_ns"
  $p95 = Parse-U64 $row.p95_ns "p95_ns"
  $p99 = Parse-U64 $row.p99_ns "p99_ns"
  $rate = Parse-Double $row.operations_per_second "operations_per_second"
  $mib = Parse-Double $row.mib_per_second "mib_per_second"
  $ownerCpuUs = Parse-Double $row.owner_cpu_us_per_op "owner_cpu_us_per_op"
  $centralCpuUs = Parse-Double $row.central_cpu_us_per_op "central_cpu_us_per_op"
  $commandHops = Parse-U64 $row.command_hops "command_hops"
  $eventHops = Parse-U64 $row.event_hops "event_hops"
  $commandRejects = Parse-U64 $row.command_rejects "command_rejects"
  $eventRejects = Parse-U64 $row.event_rejects "event_rejects"
  $sends = Parse-U64 $row.send_terminals "send_terminals"
  $receives = Parse-U64 $row.receive_terminals "receive_terminals"

  if ($logical -ne 128 -or $sends -ne 128 -or $receives -ne 128) {
    throw "central equal-work/lifecycle mismatch payload=$payload repeat=$repeat"
  }
  if ($commandHops -ne 128 -or $eventHops -ne 256) {
    throw "central hop contract mismatch payload=$payload repeat=$repeat command=$commandHops event=$eventHops"
  }
  if ($commandRejects -ne 0 -or $eventRejects -ne 0) {
    throw "central bounded mailbox rejection payload=$payload repeat=$repeat command=$commandRejects event=$eventRejects"
  }
  if ($wall -eq 0 -or $ownerCpuNs -eq 0 -or $centralCpuNs -eq 0 -or
      $p50 -eq 0 -or $p95 -lt $p50 -or $p99 -lt $p95 -or
      $rate -le 0.0 -or $mib -le 0.0 -or
      $ownerCpuUs -le 0.0 -or $centralCpuUs -le 0.0) {
    throw "invalid central metrics payload=$payload repeat=$repeat"
  }

  $key = "$payload/$repeat"
  if ($seen.ContainsKey($key)) {
    throw "duplicate central row: $key"
  }
  $seen[$key] = $true

  if (-not $baseByKey.ContainsKey($key)) {
    throw "missing owner-affine baseline row: $key"
  }
  $base = $baseByKey[$key]

  if ($base.backend -ne $row.backend -or
      $base.topology -ne $row.topology) {
    throw "paired backend/topology mismatch for $key"
  }

  $baseCpuA = Parse-I32 $base.cpu_a "baseline cpu_a"
  $baseCpuB = Parse-I32 $base.cpu_b "baseline cpu_b"
  $baseLogical = Parse-U64 $base.logical_operations "baseline logical"
  $baseShardA = Parse-U64 $base.shard_a "baseline shard_a"
  $baseShardB = Parse-U64 $base.shard_b "baseline shard_b"
  if ($baseCpuA -ne $cpuAValue -or
      $baseCpuB -ne $cpuBValue -or
      $baseLogical -ne $logical -or
      $baseShardA -ne $shardA -or
      $baseShardB -ne $shardB) {
    throw "paired topology/equal-work mismatch for $key"
  }

  $baseRate = Parse-Double $base.operations_per_second "baseline rate"
  $baseP99 = Parse-U64 $base.p99_ns "baseline p99"
  $baseOwnerCpuUs = Parse-Double $base.owner_cpu_us_per_op "baseline owner cpu"
  if ($baseRate -le 0.0 -or $baseP99 -eq 0 -or $baseOwnerCpuUs -le 0.0) {
    throw "invalid owner-affine baseline metrics for $key"
  }

  $payloadKey = [string]$payload
  $retention[$payloadKey] += ($rate / $baseRate)
  $p99Ratio[$payloadKey] += ([double]$p99 / [double]$baseP99)
  $ownerCpuRatio[$payloadKey] += ($ownerCpuUs / $baseOwnerCpuUs)
  $totalCpuRatio[$payloadKey] += (($ownerCpuUs + $centralCpuUs) / $baseOwnerCpuUs)
  $centralCpu[$payloadKey] += $centralCpuUs
}

foreach ($payload in @(1024, 65536)) {
  foreach ($repeat in 1..7) {
    $key = "$payload/$repeat"
    if (-not $seen.ContainsKey($key)) {
      throw "missing central row: $key"
    }
  }

  $key = [string]$payload
  $r = @($retention[$key] | Sort-Object)
  $p = @($p99Ratio[$key] | Sort-Object)
  $o = @($ownerCpuRatio[$key] | Sort-Object)
  $t = @($totalCpuRatio[$key] | Sort-Object)
  $c = @($centralCpu[$key] | Sort-Object)
  $mid = [int][Math]::Floor($r.Count / 2)

  Write-Host (
    "CNet central-callback paired evidence: backend={0} topology={1} payload={2} throughput_retention={3:N3}x p99_ratio={4:N3}x owner_cpu_ratio={5:N3}x total_cpu_ratio={6:N3}x central_cpu_us_per_op={7:N3}" -f
      $rows[0].backend,
      $rows[0].topology,
      $payload,
      $r[$mid],
      $p[$mid],
      $o[$mid],
      $t[$mid],
      $c[$mid]
  )
}

Write-Host "CNet central-callback compatibility matrix verified"
