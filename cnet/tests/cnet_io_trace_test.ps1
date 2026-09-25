$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot 'fixtures/io-trace'
$parser = Join-Path $PSScriptRoot '../benchmarks/summarize_io_trace.ps1'
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testDir = Join-Path $tempRoot ("salts-io-trace-test-" + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testDir
try {
    Copy-Item -Path (Join-Path $fixture '*.trace.*') -Destination $testDir
    & $parser -Directory $testDir | Out-Null
    $summary = @(Import-Csv -LiteralPath (Join-Path $testDir 'summary.csv'))
    if ($summary.Count -ne 1 -or $summary[0].data_calls_per_rt -ne '1.500' -or
        $summary[0].epoll_poll_per_rt -ne '0.500' -or $summary[0].uring_enter_per_rt -ne '0.500' -or
        $summary[0].eagain_per_rt -ne '0.500' -or $summary[0].empty_epoll_per_rt -ne '0.500') {
        throw 'Trace parser included setup/peer/cleanup calls or lost resumed calls'
    }
    $details = @(Import-Csv -LiteralPath (Join-Path $testDir 'syscalls.csv'))
    if ($details.Count -ne 5) { throw 'Unexpected syscall count' }

    $client = Join-Path $testDir 'libuv-tcp-32768.trace.101'
    $original = [IO.File]::ReadAllText($client)
    $invalid = @(
        $original.Replace('IO_BENCH_MEASURE_END', 'MISSING_END'),
        $original.Replace('MEASURE_END rounds=2', 'MEASURE_END rounds=3'),
        $original.Replace('epoll_wait resumed', 'read resumed'),
        $original.Replace('read(3, "payload", 7) = 7 <0.000004>', 'unparsed syscall'),
        ($original + $original)
    )
    $caseIndex = 0
    foreach ($content in $invalid) {
        $caseIndex++
        [IO.File]::WriteAllText($client, $content)
        $rejected = $false
        try { & $parser -Directory $testDir | Out-Null } catch { $rejected = $true }
        if (-not $rejected) { throw "Malformed or incomplete trace case $caseIndex was accepted" }
    }
    Write-Output 'Trace parser: scope, resumed calls, counters, and 5 malformed cases passed.'
} finally {
    $resolved = [IO.Path]::GetFullPath($testDir)
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'salts-io-trace-test-*') {
        throw 'Refusing cleanup outside the test temporary directory'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
