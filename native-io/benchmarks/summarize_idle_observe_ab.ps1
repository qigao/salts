param(
  [Parameter(Mandatory = $true)]
  [string]$Directory
)

$ErrorActionPreference = "Stop"

function Read-IdleRow([string]$Name) {
  $path = Join-Path $Directory "$Name.csv"
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "missing A/B result: $path"
  }
  $rows = @(Import-Csv -LiteralPath $path)
  if ($rows.Count -ne 1) { throw "expected one row in $path" }
  return [pscustomobject]@{
    Name = $Name
    Cpu = [double]$rows[0].cpu_ns_per_call
    Wall = [double]$rows[0].wall_ns_per_call
    Rate = [double]$rows[0].calls_per_second
  }
}

function DeltaPct([double]$Candidate, [double]$Base) {
  if ($Base -eq 0.0) { return [double]::NaN }
  return (($Candidate / $Base) - 1.0) * 100.0
}

$baseA = Read-IdleRow "base-a"
$candidateA = Read-IdleRow "candidate-a"
$candidateB = Read-IdleRow "candidate-b"
$baseB = Read-IdleRow "base-b"

$pairs = @(
  [pscustomobject]@{ Name = "A"; Base = $baseA; Candidate = $candidateA },
  [pscustomobject]@{ Name = "B"; Base = $baseB; Candidate = $candidateB }
)

Write-Output "| Pair | base CPU ns/call | candidate CPU ns/call | CPU delta | base wall ns/call | candidate wall ns/call | wall delta | rate delta |"
Write-Output "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"

foreach ($pair in $pairs) {
  $cpuDelta = DeltaPct $pair.Candidate.Cpu $pair.Base.Cpu
  $wallDelta = DeltaPct $pair.Candidate.Wall $pair.Base.Wall
  $rateDelta = DeltaPct $pair.Candidate.Rate $pair.Base.Rate
  Write-Output ("| {0} | {1:N3} | {2:N3} | {3:+0.00;-0.00;0.00}% | {4:N3} | {5:N3} | {6:+0.00;-0.00;0.00}% | {7:+0.00;-0.00;0.00}% |" -f
    $pair.Name, $pair.Base.Cpu, $pair.Candidate.Cpu, $cpuDelta,
    $pair.Base.Wall, $pair.Candidate.Wall, $wallDelta, $rateDelta)
}
