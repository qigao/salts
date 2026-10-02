param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [string]$Backend = "",
  [string]$CpuSet = ""
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "owner-local parallel benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
if ($rows.Count -ne 2) {
  throw "expected two owner-local parallel rows, got $($rows.Count)"
}

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse(
      [string]$Value,
      [System.Globalization.NumberStyles]::Integer,
      $Invariant,
      [ref]$parsed)) {
    throw "invalid unsigned integer for ${Name}: $Value"
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
    throw "invalid double for ${Name}: $Value"
  }
  return $parsed
}

$byMode = @{}
foreach ($row in $rows) {
  if ($row.mode -notin @("one_owner_serial", "two_owner_parallel")) {
    throw "unexpected mode: $($row.mode)"
  }
  if ($byMode.ContainsKey($row.mode)) {
    throw "duplicate mode: $($row.mode)"
  }
  $byMode[$row.mode] = $row

  if (-not [string]::IsNullOrWhiteSpace($Backend) -and
      $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }
  if (-not [string]::IsNullOrWhiteSpace($CpuSet) -and
      $row.cpu_set -ne $CpuSet) {
    throw "cpu_set mismatch: expected $CpuSet, got $($row.cpu_set)"
  }

  $units = Parse-U64 $row.units_per_lane "units_per_lane"
  $total = Parse-U64 $row.total_units "total_units"
  $replicates = Parse-U64 $row.replicates "replicates"
  $p50 = Parse-Double $row.p50_ns_per_unit "p50_ns_per_unit"
  $p95 = Parse-Double $row.p95_ns_per_unit "p95_ns_per_unit"
  $rate = Parse-Double $row.median_units_per_second "median_units_per_second"
  $checksum = Parse-U64 $row.checksum "checksum"

  if ($units -eq 0 -or $total -ne (2 * $units) -or
      $replicates -ne 11 -or $p50 -le 0.0 -or
      $p95 -lt $p50 -or $rate -le 0.0 -or $checksum -eq 0) {
    throw "invalid owner-local parallel metrics: mode=$($row.mode)"
  }
}

foreach ($mode in @("one_owner_serial", "two_owner_parallel")) {
  if (-not $byMode.ContainsKey($mode)) {
    throw "missing owner-local parallel mode: $mode"
  }
}

$serialChecksum =
  Parse-U64 $byMode["one_owner_serial"].checksum "serial checksum"
$parallelChecksum =
  Parse-U64 $byMode["two_owner_parallel"].checksum "parallel checksum"
if ($serialChecksum -ne $parallelChecksum) {
  throw "owner-local parallel checksum mismatch"
}

Write-Host "NativeIO owner-local parallel matrix verified: backend=$($rows[0].backend) cpu_set=$($rows[0].cpu_set)"
