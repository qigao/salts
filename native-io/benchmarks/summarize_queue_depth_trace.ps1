param(
  [Parameter(Mandatory = $true)]
  [string]$Directory
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
  throw "trace directory not found: $Directory"
}

$rows = @()
$labels = @(Get-ChildItem -LiteralPath $Directory -Filter "*.log" -File |
  ForEach-Object { $_.BaseName } | Sort-Object -Unique)

foreach ($label in $labels) {
  if ($label -notmatch '^qd-(\d+)-payload-(\d+)$') { continue }
  $expectedQd = [int]$Matches[1]
  $expectedPayload = [int]$Matches[2]
  $traceFiles = @(Get-ChildItem -LiteralPath $Directory -Filter "$label.trace*" -File)
  $matched = $false

  foreach ($trace in $traceFiles) {
    $inside = $false
    $enter = 0
    $poll = 0
    $samples = 0
    $qd = 0
    $payload = 0
    foreach ($line in Get-Content -LiteralPath $trace.FullName) {
      if (-not $inside -and
          $line -match 'NATIVE_IO_QD_MEASURE_BEGIN qd=(\d+) payload=(\d+) samples=(\d+)') {
        $qd = [int]$Matches[1]
        $payload = [int]$Matches[2]
        $samples = [int]$Matches[3]
        $inside = $true
        continue
      }
      if ($inside -and $line -match 'NATIVE_IO_QD_MEASURE_END qd=') {
        $inside = $false
        $matched = $true
        break
      }
      if (-not $inside) { continue }
      if ($line -match '\bio_uring_enter\(') { ++$enter }
      if ($line -match '\b(?:poll|ppoll)\(') { ++$poll }
    }

    if ($matched) {
      if ($qd -ne $expectedQd -or $payload -ne $expectedPayload) {
        throw "trace label mismatch for $label"
      }
      $logical = $qd * $samples
      if ($logical -le 0) { throw "invalid logical operation count for $label" }
      $rows += [pscustomobject]@{
        qd = $qd
        payload_bytes = $payload
        samples = $samples
        logical_operations = $logical
        io_uring_enter = $enter
        poll_ppoll = $poll
        enter_per_logical_operation = [double]$enter / [double]$logical
      }
      break
    }
  }

  if (-not $matched) { throw "measurement markers not found for $label" }
}

if ($rows.Count -eq 0) { throw "no queue-depth trace rows found" }
$rows = @($rows | Sort-Object qd, payload_bytes)
$csv = Join-Path $Directory "queue-depth-syscalls.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation

Write-Output "| QD | payload | logical ops | io_uring_enter | enter/op | poll+ppoll |"
Write-Output "| ---: | ---: | ---: | ---: | ---: | ---: |"
foreach ($row in $rows) {
  Write-Output ("| {0} | {1} | {2} | {3} | {4:N4} | {5} |" -f
    $row.qd, $row.payload_bytes, $row.logical_operations, $row.io_uring_enter,
    $row.enter_per_logical_operation, $row.poll_ppoll)
}
