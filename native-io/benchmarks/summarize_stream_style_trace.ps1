param(
  [Parameter(Mandatory = $true)]
  [string]$Directory,
  [Parameter(Mandatory = $true)]
  [string]$Backend
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture
$styles = @("direct", "coroutine", "sharded_same_owner", "sharded_cross_owner")
$payload = 32768

function Parse-Trace([string]$Label, [string]$Style) {
  $files = @(Get-ChildItem -LiteralPath $Directory -File -Filter "$Label.trace.*")
  if ($files.Count -eq 0) {
    throw "no trace files found for $Label"
  }

  $begin = $null
  $finish = $null
  $transfers = 0
  foreach ($file in $files) {
    foreach ($line in [IO.File]::ReadLines($file.FullName)) {
      if ($line -match '^(\d+\.\d+)\s+.*NATIVE_IO_STYLE_MEASURE_BEGIN style=([^ ]+) payload=(\d+) transfers=(\d+)') {
        if ($null -ne $begin) { throw "duplicate begin marker for $Label" }
        if ($Matches[2] -ne $Style -or [int]$Matches[3] -ne $payload) {
          throw "begin marker identity mismatch for $Label"
        }
        $begin = [double]::Parse($Matches[1], $Invariant)
        $transfers = [int]$Matches[4]
      }
      if ($line -match '^(\d+\.\d+)\s+.*NATIVE_IO_STYLE_MEASURE_END style=([^ ]+) payload=(\d+) transfers=(\d+)') {
        if ($null -ne $finish) { throw "duplicate end marker for $Label" }
        if ($Matches[2] -ne $Style -or [int]$Matches[3] -ne $payload) {
          throw "end marker identity mismatch for $Label"
        }
        $finish = [double]::Parse($Matches[1], $Invariant)
        if ($transfers -ne 0 -and $transfers -ne [int]$Matches[4]) {
          throw "marker transfer-count mismatch for $Label"
        }
        $transfers = [int]$Matches[4]
      }
    }
  }

  if ($null -eq $begin -or $null -eq $finish -or $finish -le $begin -or $transfers -le 0) {
    throw "incomplete measurement window for $Label"
  }

  $counts = @{
    network = 0
    epoll_wait = 0
    uring_enter = 0
    poll_wait = 0
    futex = 0
    control_write = 0
  }

  foreach ($file in $files) {
    foreach ($line in [IO.File]::ReadLines($file.FullName)) {
      if ($line -notmatch '^(\d+\.\d+)\s+(\w+)\(') { continue }
      $timestamp = [double]::Parse($Matches[1], $Invariant)
      $name = $Matches[2]
      if ($timestamp -lt $begin -or $timestamp -gt $finish) { continue }

      if ($name -match '^(send|sendto|sendmsg|sendmmsg|recv|recvfrom|recvmsg|recvmmsg)$') {
        $counts.network++
      } elseif ($name -match '^epoll_(wait|pwait|pwait2)$') {
        $counts.epoll_wait++
      } elseif ($name -eq 'io_uring_enter') {
        $counts.uring_enter++
      } elseif ($name -in @('poll', 'ppoll')) {
        $counts.poll_wait++
      } elseif ($name -eq 'futex') {
        $counts.futex++
      } elseif ($name -eq 'write' -and $line -notmatch '^\d+\.\d+\s+write\(2,') {
        $counts.control_write++
      }
    }
  }

  return [pscustomobject]@{
    backend = $Backend
    style = $Style
    payload_bytes = $payload
    transfers = $transfers
    network_calls = $counts.network
    network_calls_per_transfer = ($counts.network / $transfers).ToString('F6', $Invariant)
    epoll_wait_calls = $counts.epoll_wait
    epoll_wait_per_transfer = ($counts.epoll_wait / $transfers).ToString('F6', $Invariant)
    uring_enter_calls = $counts.uring_enter
    uring_enter_per_transfer = ($counts.uring_enter / $transfers).ToString('F6', $Invariant)
    poll_wait_calls = $counts.poll_wait
    poll_wait_per_transfer = ($counts.poll_wait / $transfers).ToString('F6', $Invariant)
    futex_calls = $counts.futex
    futex_per_transfer = ($counts.futex / $transfers).ToString('F6', $Invariant)
    control_write_calls = $counts.control_write
    control_write_per_transfer = ($counts.control_write / $transfers).ToString('F6', $Invariant)
  }
}

$rows = foreach ($style in $styles) {
  $label = "stream-$style-$payload"
  Parse-Trace -Label $label -Style $style
}

$out = Join-Path $Directory "summary.csv"
$rows | Export-Csv -LiteralPath $out -NoTypeInformation

Write-Output "Trace evidence only: syscall elapsed time is intentionally not used as a latency score."
Write-Output "The window is bounded by NativeIO benchmark markers and includes all benchmark-process threads."
Write-Output ""
Write-Output "| style | network/transfer | epoll wait/transfer | uring enter/transfer | poll+ppoll/transfer | futex/transfer | control write/transfer |"
Write-Output "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"
foreach ($row in $rows) {
  Write-Output ('| {0} | {1} | {2} | {3} | {4} | {5} | {6} |' -f
    $row.style, $row.network_calls_per_transfer, $row.epoll_wait_per_transfer,
    $row.uring_enter_per_transfer, $row.poll_wait_per_transfer,
    $row.futex_per_transfer, $row.control_write_per_transfer)
}
