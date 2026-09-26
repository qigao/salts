param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "idle observe benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 1) { throw "expected one idle observe row, found $($rows.Count)" }

$row = $rows[0]
$iterations = [UInt64]$row.iterations
$wall = [UInt64]$row.wall_ns
$cpu = [UInt64]$row.cpu_ns
$rate = [double]$row.calls_per_second
$wallPerCall = [double]$row.wall_ns_per_call
$cpuPerCall = [double]$row.cpu_ns_per_call

if ($iterations -eq 0) { throw "idle observe iterations must be positive" }
if ($wall -eq 0 -or $cpu -eq 0) { throw "idle observe timings must be positive" }
foreach ($value in @($rate, $wallPerCall, $cpuPerCall)) {
  if (-not [double]::IsFinite($value) -or $value -le 0.0) {
    throw "invalid idle observe metric: $value"
  }
}

Write-Host ("Verified idle observe benchmark: {0} calls, CPU {1:N3} ns/call, wall {2:N3} ns/call." -f
  $iterations, $cpuPerCall, $wallPerCall)
