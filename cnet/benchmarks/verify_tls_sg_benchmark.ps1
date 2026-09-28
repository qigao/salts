param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [Parameter(Mandatory = $true)]
  [string]$Backend
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
  throw "TLS SG benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
$payloads = @(1024, 8192, 32768, 65536)
$segments = @(1, 2, 4, 8, 16)

if ($rows.Count -ne ($payloads.Count * $segments.Count)) {
  throw "expected 20 TLS SG rows, got $($rows.Count)"
}

function Parse-Double([string]$value, [string]$name) {
  $parsed = 0.0
  if (-not [double]::TryParse(
      $value,
      [System.Globalization.NumberStyles]::Float,
      [System.Globalization.CultureInfo]::InvariantCulture,
      [ref]$parsed)) {
    throw "invalid $name value: $value"
  }
  return $parsed
}

foreach ($payload in $payloads) {
  foreach ($segmentCount in $segments) {
    $match = @($rows | Where-Object {
      [int]$_.payload_bytes -eq $payload -and
      [int]$_.segment_count -eq $segmentCount
    })
    if ($match.Count -ne 1) {
      throw "expected exactly one row payload=$payload segments=$segmentCount, got $($match.Count)"
    }
    $row = $match[0]
    if ($row.backend -ne $Backend) {
      throw "backend mismatch: expected=$Backend actual=$($row.backend)"
    }

    $expectedStyle = if ($segmentCount -eq 1) { "retained_contiguous" } else { "retained_slicev" }
    if ($row.style -ne $expectedStyle) {
      throw "style mismatch payload=$payload segments=$segmentCount expected=$expectedStyle actual=$($row.style)"
    }

    $iterations = [int]$row.iterations_per_replicate
    $replicates = [int]$row.replicates
    $p50 = Parse-Double $row.p50_ns_per_op "p50_ns_per_op"
    $p95 = Parse-Double $row.p95_ns_per_op "p95_ns_per_op"
    $rate = Parse-Double $row.median_bytes_per_second "median_bytes_per_second"
    $writes = Parse-Double $row.median_tls_write_calls_per_op "median_tls_write_calls_per_op"
    $cipher = Parse-Double $row.median_cipher_bytes_per_op "median_cipher_bytes_per_op"
    $copied = Parse-Double $row.copied_bytes_per_op "copied_bytes_per_op"

    if ($iterations -le 0 -or $replicates -ne 11) {
      throw "invalid benchmark sample dimensions payload=$payload segments=$segmentCount iterations=$iterations replicates=$replicates"
    }
    if ($p50 -le 0.0 -or $p95 -lt $p50 -or $rate -le 0.0) {
      throw "invalid timing/rate payload=$payload segments=$segmentCount p50=$p50 p95=$p95 rate=$rate"
    }
    if ($writes + 1e-9 -lt [double]$segmentCount) {
      throw "TLS writes/op lower than plaintext segment count payload=$payload segments=$segmentCount writes=$writes"
    }
    if ($cipher -le [double]$payload) {
      throw "cipher bytes/op must exceed plaintext bytes payload=$payload segments=$segmentCount cipher=$cipher"
    }
    if ([math]::Abs($copied) -gt 1e-9) {
      throw "retained TLS path copied plaintext bytes payload=$payload segments=$segmentCount copied=$copied"
    }
  }
}

Write-Host "TLS retained SG benchmark contract passed: backend=$Backend rows=$($rows.Count)"
