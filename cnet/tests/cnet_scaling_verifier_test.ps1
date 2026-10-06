$ErrorActionPreference = 'Stop'
$verifier = Join-Path $PSScriptRoot '../benchmarks/verify_scaling_benchmark.ps1'
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testDir = Join-Path $tempRoot ('salts-scaling-test-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testDir
$csvPath = Join-Path $testDir 'scaling.csv'

function New-RawRows {
    foreach ($connections in @(1, 4, 16, 64)) {
        foreach ($payload in @(1024, 8192, 32768, 65536)) {
            foreach ($driver in @('NativeIO direct', 'CNet retained')) {
                foreach ($repeat in 1..5) {
                    $operations = 32 * $connections
                    $unit = if ($driver -eq 'CNet retained') { $operations } else { 0 }
                    [pscustomobject]@{
                        backend = 'epoll'; driver = $driver; connections = $connections
                        payload_bytes = $payload; repeat = $repeat; samples = 32
                        logical_operations = $operations; progress_calls = $operations
                        operations_per_second = 10000; mib_per_second = 100
                        p50_ns = 10000; p95_ns = 20000; p99_ns = 30000; cpu_ns = 1000000
                        owner_request_lifecycle_ns = 500 * $unit
                        owner_request_start_ns = 250 * $unit
                        owner_request_resubmit_ns = 100 * $unit
                        owner_observe_ns = 1000 * $unit
                        benchmark_payload_check_ns = 500 * $unit
                        benchmark_callback_ns = 1000 * $unit
                        dispatcher_observer_ns = 1200 * $unit
                        dispatcher_release_ns = 100 * $unit
                        dispatcher_invoke_ns = 1400 * $unit
                        dispatcher_prepare_ns = 100 * $unit
                        owner_event_publish_ns = 1600 * $unit
                        owner_request_completion_ns = 1800 * $unit
                        owner_drive_ns = 4400 * $unit
                        client_poll_ns = 4600 * $unit
                    }
                }
            }
        }
    }
}

function Set-Overhead($Rows, $Stage, [int]$Payload, [int[]]$Repeats, [double]$Us) {
    foreach ($row in $Rows) {
        if ($row.driver -ne 'CNet retained' -or $row.connections -ne 16 -or
            $row.payload_bytes -ne $Payload -or $row.repeat -notin $Repeats) { continue }
        # Propagate the synthetic cost through enclosing spans, preserving nesting.
        $delta = ($Us - $Stage.Baseline) * 1000 * $row.logical_operations
        foreach ($field in $Stage.Fields) { $row.$field += $delta }
    }
}

function Assert-Verdict($Rows, [string]$ErrorPattern = '', [string]$WarningPattern = '') {
    $Rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation
    $failure = ''
    $messages = @()
    try { $messages = @(& $verifier -Path $csvPath 3>&1 6>&1) }
    catch { $failure = $_.Exception.Message }
    if ($ErrorPattern) {
        if ($failure -notlike $ErrorPattern) {
            throw "Expected error '$ErrorPattern', got '$failure'"
        }
    } elseif ($failure) {
        throw "Unexpected verification error: $failure"
    }
    $warnings = @($messages | Where-Object { $_ -is [Management.Automation.WarningRecord] })
    if ($WarningPattern) {
        if (-not ($warnings | Where-Object { "$_" -like $WarningPattern })) {
            throw "Missing warning '$WarningPattern'"
        }
    } elseif ($warnings.Count -gt 0) {
        throw "Unexpected warnings: $warnings"
    }
}

try {
    $paired = @(foreach ($connections in @(1, 4, 16, 64)) {
        foreach ($payload in @(1024, 8192, 32768, 65536)) {
            [pscustomobject]@{
                backend = 'epoll'; connections = $connections; payload_bytes = $payload; repeats = 5
                baseline = 'NativeIO direct'; candidate = 'CNet retained'
                rate_delta_median_percent = 0; rate_delta_mad_pp = 0
                p50_delta_median_percent = 0; p50_delta_mad_pp = 0
                p95_delta_median_percent = 0; p95_delta_mad_pp = 0
            }
        }
    })
    $paired | Export-Csv -LiteralPath (Join-Path $testDir 'scaling.paired.csv') -NoTypeInformation
    Assert-Verdict @(New-RawRows)

    $stages = @(
        @{ Label = 'owner residual'; Baseline = 1.0; Limit = 2.0
           Fields = @('owner_drive_ns', 'client_poll_ns') },
        @{ Label = 'observer framework'; Baseline = 0.2; Limit = 1.0
           Fields = @('dispatcher_observer_ns', 'dispatcher_invoke_ns', 'owner_event_publish_ns',
                      'owner_request_completion_ns', 'owner_drive_ns', 'client_poll_ns') },
        @{ Label = 'client poll wrapper'; Baseline = 0.2; Limit = 1.0
           Fields = @('client_poll_ns') }
    )
    foreach ($payload in @(32768, 65536)) {
        foreach ($stage in $stages) {
            $rows = @(New-RawRows)
            Set-Overhead $rows $stage $payload @(1) ($stage.Limit + 1.0)
            Assert-Verdict $rows -WarningPattern "*$($stage.Label) single-run limit exceeded*"

            # A majority above the limit must fail, even with high dispersion.
            $rows = @(New-RawRows)
            Set-Overhead $rows $stage $payload @(1, 3, 5) ($stage.Limit + 0.001)
            Assert-Verdict $rows -ErrorPattern "*retained $($stage.Label) regression*"

            $rows = @(New-RawRows)
            Set-Overhead $rows $stage $payload @(1, 2, 3, 4, 5) $stage.Limit
            Assert-Verdict $rows
        }
    }

    # Bad raw spans must never be hidden by the other four valid runs.
    $invalid = @(
        @{ Field = 'dispatcher_observer_ns'; Value = 1; Error = '*callback exceeds dispatcher observer*' },
        @{ Field = 'client_poll_ns'; Value = 1; Error = '*owner drive exceeds client poll*' },
        @{ Field = 'dispatcher_release_ns'; Value = -1; Error = '*invalid CNet attribution*' },
        @{ Field = 'owner_drive_ns'; Value = [double]::NaN; Error = '*invalid CNet attribution*' },
        @{ Field = 'owner_drive_ns'; Value = 'Infinity'; Error = '*invalid CNet attribution*' }
    )
    foreach ($case in $invalid) {
        $rows = @(New-RawRows)
        $row = $rows | Where-Object {
            $_.driver -eq 'CNet retained' -and $_.connections -eq 16 -and
            $_.payload_bytes -eq 32768 -and $_.repeat -eq 1
        }
        $row.($case.Field) = $case.Value
        Assert-Verdict $rows -ErrorPattern $case.Error
    }
    $rows = @(New-RawRows)
    Assert-Verdict $rows[1..($rows.Count - 1)] -ErrorPattern '*expected 160 raw scaling rows*'
    $rows[0] = $rows[1]
    Assert-Verdict $rows -ErrorPattern '*duplicate scaling row*'
    $rows = @(New-RawRows)
    $rows[0].operations_per_second = 0
    Assert-Verdict $rows -ErrorPattern '*non-positive throughput/CPU*'
    $paired[0].rate_delta_median_percent = 1
    $paired | Export-Csv -LiteralPath (Join-Path $testDir 'scaling.paired.csv') -NoTypeInformation
    Assert-Verdict @(New-RawRows) -ErrorPattern '*rate median mismatch*'
    Write-Output 'Scaling verifier: baseline, 18 overhead cases, and 9 malformed cases passed.'
} finally {
    $resolved = [IO.Path]::GetFullPath($testDir)
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'salts-scaling-test-*') {
        throw 'Refusing cleanup outside the test temporary directory'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
