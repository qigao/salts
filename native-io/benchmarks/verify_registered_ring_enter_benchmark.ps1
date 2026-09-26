param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "registered enter benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
$expected = @("normal-a", "registered-a", "registered-b", "normal-b")
if ($rows.Count -ne $expected.Count) {
  throw "expected $($expected.Count) rows, found $($rows.Count)"
}

$map = @{}
foreach ($row in $rows) {
  $name = [string]$row.mode
  if ($expected -notcontains $name) { throw "unexpected mode: $name" }
  if ($map.ContainsKey($name)) { throw "duplicate mode: $name" }

  $iterations = [UInt64]$row.iterations
  $cpu = [double]$row.cpu_ns_per_call
  $wall = [double]$row.wall_ns_per_call
  $rate = [double]$row.calls_per_second

  if ($iterations -le 0) { throw "invalid iterations for $name" }
  foreach ($value in @($cpu, $wall, $rate)) {
    if (-not [double]::IsFinite($value) -or $value -le 0.0) {
      throw "invalid metric for $($name): $value"
    }
  }

  $map[$name] = [pscustomobject]@{
    Cpu = $cpu
    Wall = $wall
    Rate = $rate
  }
}

function DeltaPct([double]$Candidate, [double]$Base) {
  return (($Candidate / $Base) - 1.0) * 100.0
}

$pairs = @(
  [pscustomobject]@{ Name = "A"; Base = $map["normal-a"]; Candidate = $map["registered-a"] },
  [pscustomobject]@{ Name = "B"; Base = $map["normal-b"]; Candidate = $map["registered-b"] }
)

Write-Output "| Pair | normal CPU ns/call | registered CPU ns/call | CPU delta | normal wall ns/call | registered wall ns/call | wall delta | rate delta |"
Write-Output "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
foreach ($pair in $pairs) {
  $cpuDelta = DeltaPct $pair.Candidate.Cpu $pair.Base.Cpu
  $wallDelta = DeltaPct $pair.Candidate.Wall $pair.Base.Wall
  $rateDelta = DeltaPct $pair.Candidate.Rate $pair.Base.Rate
  Write-Output ("| {0} | {1:N3} | {2:N3} | {3:+0.00;-0.00;0.00}% | {4:N3} | {5:N3} | {6:+0.00;-0.00;0.00}% | {7:+0.00;-0.00;0.00}% |" -f
    $pair.Name, $pair.Base.Cpu, $pair.Candidate.Cpu, $cpuDelta,
    $pair.Base.Wall, $pair.Candidate.Wall, $wallDelta, $rateDelta)
}
