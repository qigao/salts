param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "CNet scaling benchmark CSV not found: $Path"
}

$rows = @(Import-Csv -LiteralPath $Path)
$connections = @(1, 4, 16, 64)
$payloads = @(1024, 8192, 32768, 65536)
$drivers = @("NativeIO direct", "CNet copy", "CNet retained")
$expected = $connections.Count * $payloads.Count * $drivers.Count

if ($rows.Count -ne $expected) {
    throw "expected $expected scaling rows, got $($rows.Count)"
}

$seen = @{}
foreach ($row in $rows) {
    $backend = [string]$row.backend
    $driver = [string]$row.driver
    $connectionCount = [int]$row.connections
    $payload = [int]$row.payload_bytes
    $samples = [int]$row.samples
    $operations = [int]$row.logical_operations
    $rate = [double]$row.operations_per_second
    $mib = [double]$row.mib_per_second
    $p50 = [double]$row.p50_ns
    $p95 = [double]$row.p95_ns
    $p99 = [double]$row.p99_ns
    $cpu = [double]$row.cpu_ns
    $progress = [int]$row.progress_calls

    if ($backend -notin @("epoll", "io_uring")) { throw "unexpected backend=$backend" }
    if ($drivers -notcontains $driver) { throw "unexpected driver=$driver" }
    if ($connections -notcontains $connectionCount) { throw "unexpected connections=$connectionCount" }
    if ($payloads -notcontains $payload) { throw "unexpected payload=$payload" }
    if ($samples -ne 32) { throw "unexpected samples=$samples" }
    if ($operations -ne $samples * $connectionCount) {
        throw "logical operation mismatch for $driver/$connectionCount/$payload"
    }
    if ($rate -le 0 -or $mib -le 0 -or $cpu -le 0) {
        throw "non-positive throughput/CPU for $driver/$connectionCount/$payload"
    }
    if ($p50 -le 0 -or $p95 -lt $p50 -or $p99 -lt $p95) {
        throw "invalid latency ordering for $driver/$connectionCount/$payload"
    }
    if ($progress -le 0) { throw "no progress calls for $driver/$connectionCount/$payload" }

    $key = "$driver|$connectionCount|$payload"
    if ($seen.ContainsKey($key)) { throw "duplicate scaling row $key" }
    $seen[$key] = $true

    $ownerDrive = [double]$row.owner_drive_ns
    $ownerObserve = [double]$row.owner_observe_ns
    $clientPoll = [double]$row.client_poll_ns
    if ($driver -eq "CNet copy" -or $driver -eq "CNet retained") {
        if ($ownerDrive -le 0 -or $ownerObserve -le 0 -or $clientPoll -le 0) {
            throw "missing CNet owner/poll attribution for $connectionCount/$payload"
        }
    } elseif ($ownerDrive -ne 0 -or $ownerObserve -ne 0 -or $clientPoll -ne 0) {
        throw "NativeIO row unexpectedly contains CNet attribution"
    }
}

Write-Host "CNet scaling benchmark verified: $($rows.Count) rows"
