param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [Parameter(Mandatory = $true)]
  [string]$ControlPath,
  [Parameter(Mandatory = $true)]
  [string]$SharedPath,
  [string]$Backend = "",
  [string]$Topology = "",
  [int]$CpuA = -1,
  [int]$CpuB = -1
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

foreach ($required in @($Path, $ControlPath, $SharedPath)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "required CNet owner-affine evidence CSV not found: $required"
  }
}

$rows = @(Import-Csv -LiteralPath $Path)
$controls = @(Import-Csv -LiteralPath $ControlPath)
$shared = @(Import-Csv -LiteralPath $SharedPath)

if ($rows.Count -ne 28) {
  throw "expected 28 external-progress rows, got $($rows.Count)"
}
if ($controls.Count -ne 28) {
  throw "expected 28 ordinary control rows, got $($controls.Count)"
}
if ($shared.Count -ne 14) {
  throw "expected 14 shared-engine rows, got $($shared.Count)"
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
  $key = "$($control.payload_bytes)/$($control.mode)/$($control.repeat)"
  if ($controlByKey.ContainsKey($key)) {
    throw "duplicate ordinary control row: $key"
  }
  $controlByKey[$key] = $control
}

$sharedByKey = @{}
foreach ($row in $shared) {
  if ($row.mode -ne "shared_engine_parallel") {
    throw "unexpected shared-engine mode: $($row.mode)"
  }
  $key = "$($row.payload_bytes)/$($row.repeat)"
  if ($sharedByKey.ContainsKey($key)) {
    throw "duplicate shared-engine row: $key"
  }
  $sharedByKey[$key] = $row
}

$seen = @{}
$parallelVsControl = @{
  "1024" = @()
  "65536" = @()
}
$parallelVsShared = @{
  "1024" = @()
  "65536" = @()
}
$serialVsControl = @{
  "1024" = @()
  "65536" = @()
}
$p99VsControl = @{
  "1024" = @()
  "65536" = @()
}
$p99VsShared = @{
  "1024" = @()
  "65536" = @()
}
$cpuVsControl = @{
  "1024" = @()
  "65536" = @()
}
$cpuVsShared = @{
  "1024" = @()
  "65536" = @()
}

foreach ($row in $rows) {
  if ($row.mode -notin @("one_owner_serial", "two_external_parallel")) {
    throw "unexpected external mode: $($row.mode)"
  }
  $controlMode = if ($row.mode -eq "two_external_parallel") {
    "two_owner_parallel"
  } else {
    $row.mode
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
  if ($row.mode -eq "one_owner_serial") {
    if ($cpuBValue -ne $cpuAValue) {
      throw "serial row must use one owner CPU"
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
  $advanceCalls = Parse-U64 $row.advance_calls "advance_calls"
  $observeCalls = Parse-U64 $row.observe_calls "observe_calls"
  $routed = Parse-U64 $row.routed_completions "routed_completions"
  $unrelated = Parse-U64 $row.unrelated_completions "unrelated_completions"
  $sends = Parse-U64 $row.send_terminals "send_terminals"
  $receives = Parse-U64 $row.receive_terminals "receive_terminals"

  if ($logical -ne 128 -or $sends -ne 128 -or $receives -ne 128) {
    throw "equal-work/lifecycle mismatch for $($row.mode)/$payload/repeat=$repeat"
  }
  if ($wall -eq 0 -or $ownerCpu -eq 0 -or
      $p50 -eq 0 -or $p95 -lt $p50 -or $p99 -lt $p95 -or
      $rate -le 0.0 -or $mib -le 0.0 -or $cpuUs -le 0.0 -or
      $advanceCalls -eq 0 -or $observeCalls -eq 0 -or
      $routed -eq 0 -or $unrelated -ne 0) {
    throw "invalid external-progress metrics for $($row.mode)/$payload/repeat=$repeat"
  }

  $key = "$payload/$($row.mode)/$repeat"
  if ($seen.ContainsKey($key)) {
    throw "duplicate external row: $key"
  }
  $seen[$key] = $true

  $controlKey = "$payload/$controlMode/$repeat"
  if (-not $controlByKey.ContainsKey($controlKey)) {
    throw "missing ordinary control row: $controlKey"
  }
  $control = $controlByKey[$controlKey]
  if ($control.backend -ne $row.backend -or
      $control.topology -ne $row.topology) {
    throw "ordinary control backend/topology mismatch for $key"
  }

  $controlRate = Parse-Double $control.operations_per_second "control rate"
  $controlP99 = Parse-U64 $control.p99_ns "control p99"
  $controlCpuUs = Parse-Double $control.owner_cpu_us_per_op "control cpu"
  $controlCpuA = Parse-I32 $control.cpu_a "control cpu_a"
  $controlCpuB = Parse-I32 $control.cpu_b "control cpu_b"
  if ($controlCpuA -ne $cpuAValue -or $controlCpuB -ne $cpuBValue) {
    throw "ordinary control affinity mismatch for $key"
  }

  $payloadKey = [string]$payload
  if ($row.mode -eq "one_owner_serial") {
    $serialVsControl[$payloadKey] += ($rate / $controlRate)
    continue
  }

  $parallelVsControl[$payloadKey] += ($rate / $controlRate)
  $p99VsControl[$payloadKey] += ([double]$p99 / [double]$controlP99)
  $cpuVsControl[$payloadKey] += ($cpuUs / $controlCpuUs)

  $sharedKey = "$payload/$repeat"
  if (-not $sharedByKey.ContainsKey($sharedKey)) {
    throw "missing shared-engine row: $sharedKey"
  }
  $sharedRow = $sharedByKey[$sharedKey]
  if ($sharedRow.backend -ne $row.backend -or
      $sharedRow.topology -ne $row.topology) {
    throw "shared-engine backend/topology mismatch for $sharedKey"
  }
  $sharedRate = Parse-Double $sharedRow.operations_per_second "shared rate"
  $sharedP99 = Parse-U64 $sharedRow.p99_ns "shared p99"
  $sharedCpuUs = Parse-Double $sharedRow.owner_cpu_us_per_op "shared cpu"
  $sharedCpuA = Parse-I32 $sharedRow.cpu_a "shared cpu_a"
  $sharedCpuB = Parse-I32 $sharedRow.cpu_b "shared cpu_b"
  if ($sharedCpuA -ne $cpuAValue -or $sharedCpuB -ne $cpuBValue) {
    throw "shared-engine affinity mismatch for $sharedKey"
  }

  $parallelVsShared[$payloadKey] += ($rate / $sharedRate)
  $p99VsShared[$payloadKey] += ([double]$p99 / [double]$sharedP99)
  $cpuVsShared[$payloadKey] += ($cpuUs / $sharedCpuUs)
}

foreach ($payload in @(1024, 65536)) {
  foreach ($repeat in 1..7) {
    foreach ($mode in @("one_owner_serial", "two_external_parallel")) {
      $key = "$payload/$mode/$repeat"
      if (-not $seen.ContainsKey($key)) {
        throw "missing external row: $key"
      }
    }
  }

  $payloadKey = [string]$payload
  $serial = @($serialVsControl[$payloadKey] | Sort-Object)
  $parallelControl = @($parallelVsControl[$payloadKey] | Sort-Object)
  $parallelShared = @($parallelVsShared[$payloadKey] | Sort-Object)
  $p99Control = @($p99VsControl[$payloadKey] | Sort-Object)
  $p99Shared = @($p99VsShared[$payloadKey] | Sort-Object)
  $cpuControl = @($cpuVsControl[$payloadKey] | Sort-Object)
  $cpuShared = @($cpuVsShared[$payloadKey] | Sort-Object)
  $mid = 3

  Write-Host (
    "CNet external owner-affine evidence: backend={0} topology={1} payload={2} serial/control={3:N3}x parallel/control={4:N3}x parallel/shared={5:N3}x p99/control={6:N3}x p99/shared={7:N3}x cpu/control={8:N3}x cpu/shared={9:N3}x" -f
      $rows[0].backend,
      $rows[0].topology,
      $payload,
      $serial[$mid],
      $parallelControl[$mid],
      $parallelShared[$mid],
      $p99Control[$mid],
      $p99Shared[$mid],
      $cpuControl[$mid],
      $cpuShared[$mid]
  )
}

Write-Host "CNet host-driven owner-affine external-progress matrix verified"
