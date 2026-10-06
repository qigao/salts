param(
  [Parameter(Mandatory)][ValidateSet("pull_request", "push", "workflow_dispatch")]
  [string]$EventName,
  [AllowEmptyString()][string]$BaseRef,
  [Parameter(Mandatory)][string]$HeadRef
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# PR coverage follows the whole proposed change; push coverage follows the
# delivered commit range. Never skip unqualified code using only the last commit.
$full = $EventName -eq "workflow_dispatch"
$changed = @()
if (-not $full) {
  if ([string]::IsNullOrWhiteSpace($BaseRef)) { throw "Missing comparison base for $EventName" }
  if ($BaseRef -match '^0+$' -and $EventName -eq "push") {
    $changed = @(git ls-tree -r --name-only $HeadRef)
  } else {
    $range = if ($EventName -eq "pull_request") { "$BaseRef...$HeadRef" } else { "$BaseRef..$HeadRef" }
    $changed = @(git diff --name-only --no-renames $range --)
  }
  if ($LASTEXITCODE -ne 0) { throw "Cannot determine changed files for $EventName" }
}
$changed = @($changed | Where-Object { $_ -notmatch '\.(md|rst)$' })

function Test-Changed([string]$Pattern) {
  return @($changed | Where-Object { $_ -match $Pattern }).Count -gt 0
}

# Shared build inputs invalidate all native suites. Formal checks only depend
# on the Lean package, checked-in generated outputs, and their workflow.
$shared = $full -or (Test-Changed '^(CMakeLists\.txt|CMakeOptions\.cmake|CMake(User)?Presets\.json|vcpkg(-configuration)?\.json|presets/|vendor/|\.github/actions/setup-build-host/|\.github/workflows/ci\.yml|cmake/(?!ci/)|cmake/ci/select-ci-scope\.ps1)')
$contractsChanged = Test-Changed '^\.github/workflows/cmeta-cflow-calculus\.yml$'
$cmetaRuntime = Test-Changed '^cmeta/(include/|src/|CMakeLists\.txt$|tests/CMakeLists\.txt$)'
$platformRuntime = Test-Changed '^platform/(include/|src/|arch/|CMakeLists\.txt$)'
$concurrencyRuntime = Test-Changed '^concurrency/(include/|src/|CMakeLists\.txt$)'
$coroutineRuntime = Test-Changed '^coroutine/(include/|src/|arch/|CMakeLists\.txt$)'
$cflowRuntime = Test-Changed '^cflow/(include/|src/|CMakeLists\.txt$|tests/CMakeLists\.txt$)'
$pluginRuntime = Test-Changed '^plugin/(include/|src/|CMakeLists\.txt$)'
$utilsRuntime = Test-Changed '^utils/(include/|src/|CMakeLists\.txt$)'
$harness = Test-Changed '^(tinytest|tinymock)/'
$nativeCommon = $shared -or $contractsChanged -or $cmetaRuntime -or $platformRuntime -or $harness
$native = $nativeCommon -or $concurrencyRuntime -or $pluginRuntime -or $utilsRuntime -or
  (Test-Changed '^(coroutine/|platform/tests/|cmeta/(tests/|benchmarks/))')
$execution = $nativeCommon -or $utilsRuntime -or $cflowRuntime -or $coroutineRuntime -or
  (Test-Changed '^(cstl/|concurrency/|utils/tests/test_object_pool\.c$|cmeta/tests/cmeta_(scope|execution|pool|collector)|cflow/tests/cflow_stream_terminal_test\.c$)')
$projection = $nativeCommon -or $pluginRuntime -or $concurrencyRuntime -or $coroutineRuntime -or $utilsRuntime -or
  (Test-Changed '^(cflow/|cstl/(include/|src/|CMakeLists\.txt$)|cmeta/tests/installed/)')
$lean = $full -or $contractsChanged -or (Test-Changed '^(\.github/workflows/ci\.yml|cmake/ci/select-ci-scope\.ps1|formal/cmeta_cflow_calculus/|cmeta/include/cmeta/generated/builtin_signature_manifest\.h$|cflow/include/cflow/generated/(builtin_operator_policy|machine_schema)\.h$)')
$plugin = $shared -or $cmetaRuntime -or $platformRuntime -or $concurrencyRuntime -or $utilsRuntime -or $harness -or
  (Test-Changed '^(plugin/|cmeta/tests/cmeta_(pp|const|flags|layout)_|\.github/workflows/plugin-lifecycle-contract\.yml$)')

$benchmarkCommon = $shared -or $cmetaRuntime -or $utilsRuntime -or $harness -or
  (Test-Changed '^(\.github/workflows/native-io-benchmarks\.yml$|cmake/ci/(?!select-ci-scope\.ps1)|cstl/(include/|src/|CMakeLists\.txt$))')
$nativeRuntime = Test-Changed '^native-io/(src/|include/|CMakeLists\.txt$)'
$cnetRuntime = Test-Changed '^cnet/(src/|include/|CMakeLists\.txt$)'
$nativeBenchCommon = Test-Changed '^native-io/benchmarks/(CMakeLists\.txt$|.*\.(h|cmake)$)'
$cnetBenchCommon = Test-Changed '^cnet/(benchmarks/(CMakeLists\.txt$|cnet_benchmark_|cnet_io_benchmark_config|.*\.(h|cmake)$)|tests/fixtures/)'
$nativeOwner = $benchmarkCommon -or $nativeRuntime -or $concurrencyRuntime -or $platformRuntime -or
  $nativeBenchCommon -or (Test-Changed '^native-io/benchmarks/.*(owner|sharded)')
$nativeStyle = $benchmarkCommon -or $nativeRuntime -or $coroutineRuntime -or $platformRuntime -or
  $nativeBenchCommon -or (Test-Changed '^native-io/benchmarks/.*(styles|vector|native_io_pipe_benchmark)')
$cnetOwner = $benchmarkCommon -or $cnetRuntime -or $nativeRuntime -or $concurrencyRuntime -or $platformRuntime -or
  $cnetBenchCommon -or (Test-Changed '^cnet/benchmarks/.*(owner_parallel|owner_lifecycle|shards_parallel|external_owner|central_callback)')
$cnetIo = $benchmarkCommon -or $cnetRuntime -or $nativeRuntime -or $coroutineRuntime -or $platformRuntime -or
  $cnetBenchCommon -or (Test-Changed '^cnet/benchmarks/(cnet_io_benchmark|cnet_scaling|verify_io_benchmark|verify_scaling_benchmark)')
$cnetSg = $benchmarkCommon -or $cnetRuntime -or $nativeRuntime -or $platformRuntime -or
  $cnetBenchCommon -or (Test-Changed '^cnet/benchmarks/.*(tls_sg|tls_provider_parallel|tls_public_owner|sg_benchmark|cnet_io_benchmark)')
$coroutine = $benchmarkCommon -or $coroutineRuntime -or $concurrencyRuntime -or $platformRuntime -or
  (Test-Changed '^coroutine/benchmarks/')
$nativeUring = $benchmarkCommon -or $nativeRuntime -or $platformRuntime -or
  $nativeBenchCommon -or (Test-Changed '^native-io/benchmarks/.*(queue.depth|idle.observe)')
$forensic = $full -or (Test-Changed '^(cnet|native-io)/benchmarks/(summarize_|verify_.*mechanism)')
$compare = $benchmarkCommon -or $cnetRuntime -or $nativeRuntime -or $coroutineRuntime -or $concurrencyRuntime -or $platformRuntime -or
  (Test-Changed '^cnet/benchmarks/cnet_owner_lifecycle_compare\.c$')
$work = $nativeOwner -or $nativeStyle -or $cnetOwner -or $cnetIo -or $cnetSg -or $coroutine -or $nativeUring -or $forensic

$checks = [ordered]@{
  contracts = $native -or $execution -or $projection -or $lean
  native = $native
  execution = $execution
  projection = $projection
  lean = $lean
  plugin = $plugin
  work = $work
  evidence = $work
  cnet_compare = $compare
  native_owner = $nativeOwner
  native_style = $nativeStyle
  cnet_owner = $cnetOwner
  cnet_io = $cnetIo
  cnet_sg = $cnetSg
  coroutine = $coroutine
  native_uring = $nativeUring
  forensic = $forensic
}
foreach ($key in @($checks.Keys)) {
  $checks[$key] = $checks[$key].ToString().ToLowerInvariant()
}
# Keep existing benchmark workload sizes: scope selection is independent of
# the reduced PR workload versus the full push/manual measurement profile.
$checks.profile = if ($EventName -eq "pull_request") { "pr" } else { "full" }
$json = ConvertTo-Json -InputObject $checks -Compress
Write-Output $json
if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "checks=$json" }
if ($env:GITHUB_STEP_SUMMARY) {
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "| Check | Selected |`n|---|---|"
  foreach ($key in $checks.Keys) {
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "| $key | $($checks[$key]) |"
  }
}
