param(
  [Parameter(Mandatory = $true)]
  [string]$StyleCsv,
  [Parameter(Mandatory = $true)]
  [string]$TraceCsv,
  [Parameter(Mandatory = $true)]
  [string]$Backend
)

$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture
$styles = @("direct", "coroutine", "sharded_same_owner", "sharded_cross_owner")
$payload = 32768

function Parse-U64([object]$Value, [string]$Name) {
  $parsed = [UInt64]0
  if (-not [UInt64]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Integer,
                              $Invariant, [ref]$parsed)) {
    throw "invalid unsigned integer for $($Name): $Value"
  }
  return $parsed
}

function Parse-Double([object]$Value, [string]$Name) {
  $parsed = [double]0
  if (-not [double]::TryParse([string]$Value,
                              [System.Globalization.NumberStyles]::Float,
                              $Invariant, [ref]$parsed)) {
    throw "invalid double for $($Name): $Value"
  }
  return $parsed
}

if (-not (Test-Path -LiteralPath $StyleCsv -PathType Leaf)) {
  throw "STREAM style CSV not found: $StyleCsv"
}
if (-not (Test-Path -LiteralPath $TraceCsv -PathType Leaf)) {
  throw "STREAM syscall summary CSV not found: $TraceCsv"
}

$styleRows = @(Import-Csv -LiteralPath $StyleCsv | Where-Object {
  [int]$_.payload_bytes -eq $payload
})
$traceRows = @(Import-Csv -LiteralPath $TraceCsv)

if ($styleRows.Count -ne $styles.Count) {
  throw "expected $($styles.Count) STREAM mechanism rows, got $($styleRows.Count)"
}
if ($traceRows.Count -ne $styles.Count) {
  throw "expected $($styles.Count) STREAM trace rows, got $($traceRows.Count)"
}

$styleByName = @{}
foreach ($row in $styleRows) {
  if ($row.backend -ne $Backend) { throw "style backend mismatch: $($row.backend)" }
  if ($styles -notcontains $row.style) { throw "unexpected style row: $($row.style)" }
  if ($styleByName.ContainsKey($row.style)) { throw "duplicate style row: $($row.style)" }
  $styleByName[$row.style] = $row

  $transfers = Parse-U64 $row.transfers "transfers"
  $p50 = Parse-Double $row.p50_us "p50_us"
  $p95 = Parse-Double $row.p95_us "p95_us"
  $observe = Parse-U64 $row.observe_calls "observe_calls"
  $completions = Parse-U64 $row.completion_count "completion_count"
  $batches = Parse-U64 $row.completion_batches "completion_batches"
  $maxBatch = Parse-U64 $row.max_completion_batch "max_completion_batch"
  $queued = Parse-U64 $row.queued_dispatches "queued_dispatches"
  $rejected = Parse-U64 $row.rejected_tasks "rejected_tasks"
  $peak = Parse-U64 $row.peak_command_slots "peak_command_slots"
  $hops = Parse-Double $row.message_hops_per_transfer "message_hops_per_transfer"
  $direct = Parse-Double $row.same_owner_direct_tasks_per_transfer "same_owner_direct_tasks_per_transfer"

  if ($transfers -eq 0 -or $p50 -le 0.0 -or $p95 -lt $p50) {
    throw "invalid latency/tail structure for $($row.style)"
  }
  if ($observe -eq 0 -or $completions -lt $transfers * 2 -or
      $batches -eq 0 -or $batches -gt $observe -or
      $maxBatch -eq 0 -or $maxBatch -gt 4) {
    throw "invalid completion-batch evidence for $($row.style)"
  }
  if ($rejected -ne 0) {
    throw "measured accepted work was rejected for $($row.style)"
  }

  switch ($row.style) {
    "direct" {
      if ($queued -ne 0 -or $hops -ne 0.0 -or $direct -ne 0.0 -or $peak -ne 0) {
        throw "Direct mechanism row contains Sharded routing state"
      }
    }
    "coroutine" {
      if ($queued -ne 0 -or $hops -ne 0.0 -or $direct -ne 0.0 -or $peak -ne 0) {
        throw "Coroutine mechanism row contains Sharded routing state"
      }
    }
    "sharded_same_owner" {
      if ($queued -ne 0 -or $hops -ne 0.0 -or $direct -lt 2.0 -or $peak -eq 0) {
        throw "same-owner mechanism row violates zero-hop fast path"
      }
    }
    "sharded_cross_owner" {
      if ($direct -ne 0.0 -or $hops -lt 3.0 -or
          $queued -lt $transfers * 3 -or $peak -eq 0) {
        throw "cross-owner mechanism row lacks bounded routed-work evidence"
      }
    }
  }
}

$traceByName = @{}
foreach ($row in $traceRows) {
  if ($row.backend -ne $Backend) { throw "trace backend mismatch: $($row.backend)" }
  if ($styles -notcontains $row.style) { throw "unexpected trace style: $($row.style)" }
  if ([int]$row.payload_bytes -ne $payload) { throw "unexpected trace payload: $($row.payload_bytes)" }
  if ($traceByName.ContainsKey($row.style)) { throw "duplicate trace row: $($row.style)" }
  $traceByName[$row.style] = $row

  $transfers = Parse-U64 $row.transfers "trace transfers"
  $network = Parse-U64 $row.network_calls "network_calls"
  $epoll = Parse-U64 $row.epoll_wait_calls "epoll_wait_calls"
  $uring = Parse-U64 $row.uring_enter_calls "uring_enter_calls"
  $poll = Parse-U64 $row.poll_wait_calls "poll_wait_calls"

  if ($transfers -eq 0) { throw "zero trace transfers for $($row.style)" }
  if ($Backend -eq "epoll") {
    if ($uring -ne 0) { throw "epoll mechanism trace unexpectedly entered io_uring" }
    if ($epoll -eq 0) { throw "epoll mechanism trace contains no epoll wait evidence" }
  } elseif ($Backend -eq "io_uring") {
    if ($uring -eq 0) { throw "io_uring mechanism trace contains no io_uring_enter evidence" }
    if ($epoll -ne 0) { throw "io_uring mechanism trace unexpectedly used epoll wait" }
  } else {
    throw "mechanism verifier is Linux-only; unsupported backend: $Backend"
  }

  # The trace may legitimately use direct optimistic network syscalls or ring
  # submission. Do not turn either implementation choice into a numeric gate.
  if ($network -eq 0 -and $uring -eq 0 -and $epoll -eq 0 -and $poll -eq 0) {
    throw "trace contains no attributable I/O mechanism for $($row.style)"
  }
}

foreach ($style in $styles) {
  if (-not $styleByName.ContainsKey($style)) { throw "missing style evidence: $style" }
  if (-not $traceByName.ContainsKey($style)) { throw "missing trace evidence: $style" }
}

Write-Host "NativeIO STREAM mechanism evidence verified: backend=$Backend payload=$payload"
Write-Host "Gate is structural/tail-aware only; no absolute latency or throughput threshold is applied."
