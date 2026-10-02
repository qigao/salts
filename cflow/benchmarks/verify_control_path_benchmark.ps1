param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [string]$Backend = ""
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "CFlow control baseline CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 3) {
  throw "expected 3 CFlow control baseline rows, got $($rows.Count)"
}

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse([string]$Value, [System.Globalization.NumberStyles]::Integer, $Invariant, [ref]$parsed)) {
    throw "invalid unsigned integer for $Name : $Value"
  }
  return $parsed
}

function Parse-Double([object]$Value, [string]$Name) {
  $parsed = [double]0
  if (-not [double]::TryParse([string]$Value, [System.Globalization.NumberStyles]::Float, $Invariant, [ref]$parsed)) {
    throw "invalid double for $Name : $Value"
  }
  return $parsed
}

$expectedLayers = @("direct", "actor", "publisher")
$byLayer = @{}
$expectedReplicates = [UInt64]7
$expectedMeasuredValues = @(4096, 32768)

foreach ($row in $rows) {
  if ($row.layer -notin $expectedLayers) {
    throw "unexpected CFlow control layer: $($row.layer)"
  }
  if ($byLayer.ContainsKey($row.layer)) {
    throw "duplicate CFlow control layer: $($row.layer)"
  }
  $byLayer[$row.layer] = $row

  if (-not [string]::IsNullOrWhiteSpace($Backend) -and $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }

  $replicates = Parse-U64 $row.replicates "replicates"
  $values = Parse-U64 $row.values_per_replicate "values_per_replicate"
  $p50 = Parse-Double $row.p50_ns "p50_ns"
  $p95 = Parse-Double $row.p95_ns "p95_ns"
  $p99 = Parse-Double $row.p99_ns "p99_ns"
  $rate = Parse-Double $row.median_values_per_second "median_values_per_second"
  $cpuPercent = Parse-Double $row.median_cpu_percent "median_cpu_percent"
  $cpuEfficiency = Parse-Double $row.median_values_per_cpu_second "median_values_per_cpu_second"
  $accepted = Parse-U64 $row.accepted "accepted"
  $completed = Parse-U64 $row.completed "completed"
  $rejected = Parse-U64 $row.rejected "rejected"
  $stale = Parse-U64 $row.stale "stale"

  if ($replicates -ne $expectedReplicates -or
      $values -notin $expectedMeasuredValues) {
    throw "sample contract mismatch for $($row.layer): replicates=$replicates values=$values"
  }
  if ($p50 -lt 0.0 -or $p95 -lt $p50 -or $p99 -lt $p95 -or $p99 -le 0.0 -or $rate -le 0.0 -or $cpuPercent -lt 0.0 -or $cpuEfficiency -le 0.0) {
    throw "invalid timing/CPU metrics for $($row.layer)"
  }
  $expectedAccepted =
    [UInt64]($replicates * ([UInt64]64 + $values))
  if ($accepted -ne $expectedAccepted -or
      $completed -ne $expectedAccepted -or
      $rejected -ne 0 -or $stale -ne 0) {
    throw "lifecycle accounting mismatch for $($row.layer): expected=$expectedAccepted accepted=$accepted completed=$completed rejected=$rejected stale=$stale"
  }
}

foreach ($layer in $expectedLayers) {
  if (-not $byLayer.ContainsKey($layer)) {
    throw "missing CFlow control layer: $layer"
  }
}

$sampleCounts = @($rows | Select-Object -ExpandProperty values_per_replicate -Unique)
if ($sampleCounts.Count -ne 1) {
  throw "CFlow control layers used different measured value counts"
}

$directRate = Parse-Double $byLayer["direct"].median_values_per_second "direct rate"
$actorRate = Parse-Double $byLayer["actor"].median_values_per_second "actor rate"
$publisherRate = Parse-Double $byLayer["publisher"].median_values_per_second "publisher rate"
$directP99 = Parse-Double $byLayer["direct"].p99_ns "direct p99"
$actorP99 = Parse-Double $byLayer["actor"].p99_ns "actor p99"
$publisherP99 = Parse-Double $byLayer["publisher"].p99_ns "publisher p99"

$actorDirect = $actorRate / $directRate
$publisherActor = $publisherRate / $actorRate
$actorDirectP99 = $actorP99 - $directP99
$publisherActorP99 = $publisherP99 - $actorP99

Write-Host ("CFlow control baseline verified: backend={0} actor/direct={1:N3}x publisher/actor={2:N3}x actor-direct-p99={3:N3}ns publisher-actor-p99={4:N3}ns" -f $rows[0].backend, $actorDirect, $publisherActor, $actorDirectP99, $publisherActorP99)
