param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [Parameter(Mandatory = $true)]
  [string]$OutputPath,
  [string]$Backend = ""
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture
$Fence = ([string][char]96) * 3

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path | Where-Object { $_.style -eq "cross_owner" })
if ($rows.Count -eq 0) { throw "benchmark CSV has no cross_owner rows" }

function Parse-Double([object]$Value, [string]$Name) {
  $parsed = [double]0
  if (-not [double]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Float,
                              $Invariant, [ref]$parsed)) {
    throw "invalid double for ${Name}: $Value"
  }
  return $parsed
}

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Integer,
                              $Invariant, [ref]$parsed)) {
    throw "invalid unsigned integer for ${Name}: $Value"
  }
  return $parsed
}

$rows = @($rows | Sort-Object { Parse-U64 $_.window "window" })
$windows = @()
$p50 = @()
$p95 = @()
$rate = @()
foreach ($row in $rows) {
  if (-not [string]::IsNullOrWhiteSpace($Backend) -and $row.backend -ne $Backend) {
    throw "backend mismatch: expected $Backend, got $($row.backend)"
  }
  $windows += Parse-U64 $row.window "window"
  $p50 += [math]::Round((Parse-Double $row.p50_batch_ns_per_op "p50"), 3)
  $p95 += [math]::Round((Parse-Double $row.p95_batch_ns_per_op "p95"), 3)
  $rate += [math]::Round((Parse-Double $row.median_ops_per_second "rate"), 0)
}

$latencyMax = [math]::Ceiling((($p95 | Measure-Object -Maximum).Maximum) * 1.10)
if ($latencyMax -le 0) { $latencyMax = 1 }
$rateMax = [math]::Ceiling((($rate | Measure-Object -Maximum).Maximum) * 1.10)
if ($rateMax -le 0) { $rateMax = 1 }

$windowText = ($windows -join ", ")
$p50Text = ($p50 | ForEach-Object { $_.ToString("0.###", $Invariant) }) -join ", "
$p95Text = ($p95 | ForEach-Object { $_.ToString("0.###", $Invariant) }) -join ", "
$rateText = ($rate | ForEach-Object { $_.ToString("0", $Invariant) }) -join ", "
$backendTitle = if ([string]::IsNullOrWhiteSpace($Backend)) { "NativeIO Sharded" } else { $Backend }

$markdown = @(
  "### Cross-owner routing charts: $backendTitle",
  "",
  "Bar = p50 batch-average latency; line = p95. Lower is better.",
  "",
  "$Fence" + "mermaid",
  "xychart-beta",
  "    title `"Cross-owner routing latency by window ($backendTitle)`"",
  "    x-axis `"Window`" [$windowText]",
  "    y-axis `"ns/op`" 0 --> $latencyMax",
  "    bar [$p50Text]",
  "    line [$p95Text]",
  "$Fence",
  "",
  "Higher throughput is better.",
  "",
  "$Fence" + "mermaid",
  "xychart-beta",
  "    title `"Cross-owner routing throughput by window ($backendTitle)`"",
  "    x-axis `"Window`" [$windowText]",
  "    y-axis `"ops/s`" 0 --> $rateMax",
  "    line [$rateText]",
  "$Fence",
  ""
)

$parent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($parent)) {
  New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$markdown | Set-Content -LiteralPath $OutputPath -Encoding utf8
