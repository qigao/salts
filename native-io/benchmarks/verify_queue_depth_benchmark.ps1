param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "queue-depth benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
$depths = @(1, 2, 4, 8, 16, 32, 64)
$payloads = @(1024, 8192, 32768, 65536)
$expectedCount = $depths.Count * $payloads.Count

if ($rows.Count -ne $expectedCount) {
  throw "expected $expectedCount rows, found $($rows.Count)"
}

$seen = @{}
foreach ($row in $rows) {
  $qd = [int]$row.qd
  $payload = [int]$row.payload_bytes
  $samples = [int]$row.samples
  $logical = [int]$row.logical_operations
  $peak = [int]$row.peak_active
  $observes = [int]$row.observe_calls
  $wall = [UInt64]$row.wall_ns
  $cpu = [UInt64]$row.cpu_ns
  $p50 = [UInt64]$row.p50_ns
  $p95 = [UInt64]$row.p95_ns
  $p99 = [UInt64]$row.p99_ns
  $rate = [double]$row.operations_per_second
  $mib = [double]$row.mib_per_second

  if ($depths -notcontains $qd) { throw "unexpected qd=$qd" }
  if ($payloads -notcontains $payload) { throw "unexpected payload=$payload" }
  $key = "$qd:$payload"
  if ($seen.ContainsKey($key)) { throw "duplicate cell $key" }
  $seen[$key] = $true

  if ($samples -le 0) { throw "invalid samples for $key" }
  if ($logical -ne $samples * $qd) {
    throw "logical operation count mismatch for $key: $logical"
  }
  if ($peak -lt 2 * $qd) {
    throw "requested queue depth not reached for $key: peak_active=$peak"
  }
  if ($observes -le 0) { throw "no observe calls for $key" }
  if ($wall -eq 0 -or $cpu -eq 0) { throw "zero timing for $key" }
  if ($p50 -eq 0 -or $p50 -gt $p95 -or $p95 -gt $p99) {
    throw "invalid latency percentiles for $key"
  }
  if (-not [double]::IsFinite($rate) -or $rate -le 0.0) {
    throw "invalid operation rate for $key"
  }
  if (-not [double]::IsFinite($mib) -or $mib -le 0.0) {
    throw "invalid throughput for $key"
  }
}

foreach ($payload in $payloads) {
  $previousRate = $null
  foreach ($qd in $depths) {
    $key = "$qd:$payload"
    if (-not $seen.ContainsKey($key)) { throw "missing cell $key" }
  }
}

Write-Host "Verified $expectedCount queue-depth benchmark cells."
