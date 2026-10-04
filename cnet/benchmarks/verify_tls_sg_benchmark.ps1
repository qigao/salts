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
$payloads = @(1024, 8192, 16384, 24576, 32768, 49152, 65536, 131072)
$segments = @(2, 4, 8, 16)
$expectedRows = $payloads.Count * (1 + 2 * $segments.Count)

if ($rows.Count -ne $expectedRows) {
  throw "expected $expectedRows TLS SG rows, got $($rows.Count)"
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

function Get-OneRow([int]$payload, [int]$segmentCount, [string]$style) {
  $match = @($rows | Where-Object {
    [int]$_.payload_bytes -eq $payload -and
    [int]$_.segment_count -eq $segmentCount -and
    $_.style -eq $style
  })
  if ($match.Count -ne 1) {
    throw "expected exactly one row payload=$payload segments=$segmentCount style=$style, got $($match.Count)"
  }
  return $match[0]
}

function Validate-Row($row, [int]$payload, [int]$segmentCount,
                      [string]$style, [int]$expectedOwners) {
  if ($row.backend -ne $Backend) {
    throw "backend mismatch: expected=$Backend actual=$($row.backend)"
  }

  $iterations = [int]$row.iterations_per_replicate
  $replicates = [int]$row.replicates
  $p50 = Parse-Double $row.p50_ns_per_op "p50_ns_per_op"
  $p95 = Parse-Double $row.p95_ns_per_op "p95_ns_per_op"
  $rate = Parse-Double $row.median_bytes_per_second "median_bytes_per_second"
  $writes = Parse-Double $row.median_tls_write_calls_per_op "median_tls_write_calls_per_op"
  $records = Parse-Double $row.median_tls_records_per_op "median_tls_records_per_op"
  $cipher = Parse-Double $row.median_cipher_bytes_per_op "median_cipher_bytes_per_op"
  $copied = Parse-Double $row.plaintext_copied_bytes_per_op "plaintext_copied_bytes_per_op"
  $owners = [int]$row.retained_owner_count

  if ($iterations -le 0 -or $replicates -ne 11) {
    throw "invalid benchmark sample dimensions payload=$payload segments=$segmentCount style=$style iterations=$iterations replicates=$replicates"
  }
  if ($p50 -le 0.0 -or $p95 -lt $p50 -or $rate -le 0.0 -or
      $writes -le 0.0 -or $records -le 0.0) {
    throw "invalid timing/rate/calls payload=$payload segments=$segmentCount style=$style p50=$p50 p95=$p95 rate=$rate writes=$writes records=$records"
  }

  $segmentBytes = [int]($payload / $segmentCount)
  $expectedRecords =
    if ($style -eq "retained_slicev_discontiguous") {
      $segmentCount * [math]::Ceiling($segmentBytes / 16384.0)
    } else {
      [math]::Ceiling($payload / 16384.0)
    }
  if ([math]::Abs($records - $expectedRecords) -gt 0.000001) {
    throw "TLS record count mismatch payload=$payload segments=$segmentCount style=$style expected=$expectedRecords actual=$records"
  }
  if ($cipher -le [double]$payload) {
    throw "cipher bytes/op must exceed plaintext bytes payload=$payload segments=$segmentCount style=$style cipher=$cipher"
  }
  if ([math]::Abs($copied) -gt 0.000001) {
    throw "retained TLS benchmark copied plaintext payload=$payload segments=$segmentCount style=$style copied=$copied"
  }
  if ($owners -ne $expectedOwners) {
    throw "retained owner count mismatch payload=$payload segments=$segmentCount style=$style expected=$expectedOwners actual=$owners"
  }

  return @{
    Row = $row
    P50 = $p50
    P95 = $p95
    Rate = $rate
    Writes = $writes
    Records = $records
    Cipher = $cipher
  }
}

foreach ($payload in $payloads) {
  $contiguousRow = Get-OneRow $payload 1 "retained_contiguous"
  $contiguous = Validate-Row $contiguousRow $payload 1 "retained_contiguous" 1

  foreach ($segmentCount in $segments) {
    $adjacentRow = Get-OneRow $payload $segmentCount "retained_slicev_adjacent"
    $discontiguousRow = Get-OneRow $payload $segmentCount "retained_slicev_discontiguous"
    $adjacent = Validate-Row $adjacentRow $payload $segmentCount "retained_slicev_adjacent" 1
    $discontiguous = Validate-Row $discontiguousRow $payload $segmentCount "retained_slicev_discontiguous" $segmentCount

    if ([math]::Abs($adjacent.Writes - $contiguous.Writes) -gt 0.000001) {
      throw "adjacent same-backing slices did not collapse to contiguous provider-call count payload=$payload segments=$segmentCount contiguous=$($contiguous.Writes) adjacent=$($adjacent.Writes)"
    }
    if ($discontiguous.Writes + 0.000001 -lt [double]$segmentCount) {
      throw "discontiguous TLS writes/op lower than plaintext segment count payload=$payload segments=$segmentCount writes=$($discontiguous.Writes)"
    }
    if ($segmentCount -ge 8 -and $adjacent.Writes + 0.000001 -ge $discontiguous.Writes) {
      throw "8/16-slice adjacent layout did not reduce TLS provider calls payload=$payload segments=$segmentCount adjacent=$($adjacent.Writes) discontiguous=$($discontiguous.Writes)"
    }

    $p50Delta = ($adjacent.P50 / $discontiguous.P50 - 1.0) * 100.0
    $p95Delta = ($adjacent.P95 / $discontiguous.P95 - 1.0) * 100.0
    $rateDelta = ($adjacent.Rate / $discontiguous.Rate - 1.0) * 100.0
    $message = ("TLS SG adjacent evidence payload={0} segments={1}: calls={2:F3} vs {3:F3}, records={4:F3} vs {5:F3}, p50_delta={6:+0.00;-0.00;0.00}%, p95_delta={7:+0.00;-0.00;0.00}%, rate_delta={8:+0.00;-0.00;0.00}%" -f $payload, $segmentCount, $adjacent.Writes, $discontiguous.Writes, $adjacent.Records, $discontiguous.Records, $p50Delta, $p95Delta, $rateDelta)
    Write-Host $message
  }
}

Write-Host "TLS retained SG benchmark contract passed: backend=$Backend rows=$($rows.Count), 1K..128K record transitions, adjacent/discontiguous layouts, zero plaintext copies, retained-owner counts, provider-call collapse"
