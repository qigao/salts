param(
  [Parameter(Mandatory)][ValidateSet("pull_request", "push", "workflow_dispatch")]
  [string]$EventName,
  [AllowEmptyString()][string]$BaseRef,
  [Parameter(Mandatory)][string]$HeadRef,
  [AllowEmptyString()][string]$HeadBranch = "",
  [bool]$PrepareRelease = $false
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# PR coverage follows the whole proposed change; push coverage follows the
# delivered commit range. Never skip unqualified code using only the last commit.
$full = $EventName -eq "workflow_dispatch"
# New long-lived ACE pattern development runs the complete Linux contract suite
# on PRs; explicit manual release preparation still selects all platforms.
$acePatternsDevelopment = $HeadBranch -eq 'feature/cmeta-ace-patterns' -and
  $EventName -eq 'pull_request' -and -not $PrepareRelease
# Preserve the historical integration branch's complete Linux coverage.
$componentIntegration = ($HeadBranch -eq 'feature/cmeta-pattern-component-runtime' -and
  -not $PrepareRelease) -or $acePatternsDevelopment
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
$shared = $full -or (Test-Changed '^(CMakeLists\.txt|CMakeOptions\.cmake|CMake(User)?Presets\.json|vcpkg(-configuration)?\.json|presets/|vendor/|\.github/actions/|\.github/workflows/(ci|native-build|sdk-package|sdk-tests)\.yml|cmake/(?!ci/)|cmake/ci/select-ci-scope\.ps1)')
$mobile = $shared -or (Test-Changed '^(tools|tinytest|platform|concurrency|coroutine|native-io|uri|cmeta|component|component-plugin|plugin|simd|tinymock|cflow|cstl|cnet|cserde|utils)/')
$contractsChanged = Test-Changed '^\.github/workflows/cmeta-cflow-calculus\.yml$'
$cmetaRuntime = Test-Changed '^cmeta/(include/|src/|native/|CMakeLists\.txt$|tests/CMakeLists\.txt$)'
$componentRuntime = Test-Changed '^component/(include/|src/|CMakeLists\.txt$|tests/)'
$componentPluginRuntime = Test-Changed '^component-plugin/(include/|src/|CMakeLists\.txt$|tests/)'
$platformRuntime = Test-Changed '^platform/(include/|src/|arch/|CMakeLists\.txt$)'
$concurrencyRuntime = Test-Changed '^concurrency/(include/|src/|CMakeLists\.txt$)'
$coroutineRuntime = Test-Changed '^coroutine/(include/|src/|arch/|CMakeLists\.txt$)'
$cflowRuntime = Test-Changed '^cflow/(include/|src/|CMakeLists\.txt$|tests/CMakeLists\.txt$)'
$pluginRuntime = Test-Changed '^plugin/(include/|src/|CMakeLists\.txt$)'
$utilsRuntime = Test-Changed '^utils/(include/|src/|CMakeLists\.txt$)'
$harness = Test-Changed '^(tinytest|tinymock)/'
$nativeCommon = $shared -or $contractsChanged -or $cmetaRuntime -or $componentRuntime -or $componentPluginRuntime -or $platformRuntime -or $harness
$native = $nativeCommon -or $concurrencyRuntime -or $pluginRuntime -or $utilsRuntime -or
  (Test-Changed '^(coroutine/|platform/tests/|plugin/|cmeta/(tests/|benchmarks/))')
$execution = $nativeCommon -or $utilsRuntime -or $cflowRuntime -or $coroutineRuntime -or
  (Test-Changed '^(cstl/|concurrency/|cnet/(include/|src/|tests/|CMakeLists\.txt$)|utils/tests/test_object_pool\.c$|cflow/tests/cflow_stream_terminal_test\.c$)')
$projection = $nativeCommon -or $pluginRuntime -or $concurrencyRuntime -or $coroutineRuntime -or $utilsRuntime -or
  (Test-Changed '^(cflow/|cstl/(include/|src/|CMakeLists\.txt$))')
$lean = $full -or $contractsChanged -or (Test-Changed '^(\.github/workflows/ci\.yml|cmake/ci/select-ci-scope\.ps1|formal/cmeta_cflow_calculus/|cmeta/include/cmeta/generated/builtin_signature_manifest\.h$|cflow/include/cflow/generated/(builtin_operator_policy|machine_schema)\.h$)')

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
# Benchmark execution is limited to changes owned by these two modules.
# Manual validation has no change range and does not request benchmark runs.
# The long-lived ACE pattern PR explicitly qualifies full Linux CTest; defer
# expensive backend measurement to an explicit later release qualification.
$benchmarkChanged = (-not $acePatternsDevelopment) -and
  (Test-Changed '^(native-io|cnet)/')
$nativeOwner = $benchmarkChanged -and $nativeOwner
$nativeStyle = $benchmarkChanged -and $nativeStyle
$cnetOwner = $benchmarkChanged -and $cnetOwner
$cnetIo = $benchmarkChanged -and $cnetIo
$cnetSg = $benchmarkChanged -and $cnetSg
$coroutine = $benchmarkChanged -and $coroutine
$nativeUring = $benchmarkChanged -and $nativeUring
$forensic = $benchmarkChanged -and $forensic
$compare = $benchmarkChanged -and $compare
# Only executable I/O/transport changes (and benchmark inputs) alter this
# baseline. CNet/NativeIO tests-only edits must not start all four platforms.
$transportOwner = $PrepareRelease -or
  ((-not $acePatternsDevelopment) -and
   (Test-Changed '^(cnet|native-io)/(src/|include/|CMakeLists\.txt$|benchmarks/)'))
$work = $nativeOwner -or $nativeStyle -or $cnetOwner -or $cnetIo -or $cnetSg -or $coroutine -or $nativeUring -or $forensic -or $transportOwner

$checks = [ordered]@{
  contracts = $native -or $execution -or $projection -or $lean
  native = $native
  execution = $execution
  projection = $projection
  lean = $lean
  mobile = $mobile
  work = $work
  transport_owner = $transportOwner
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
  full_tests = $componentIntegration
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

# CI build/run matrices contain only Release profiles.
# Each host job runs its selected suites directly after building the profile.
if ($PrepareRelease -and -not $full) { throw "Release preparation requires a manual CI run" }
$profiles = @(
  @{ id = 'linux-release'; runner = 'ubuntu-24.04'; family = 'linux'; preset = 'linux-release-ci'; build_dir = 'build/linux-gcc-release'; sdk = 'linux-x64' },
  @{ id = 'linux-arm64-release'; runner = 'ubuntu-24.04-arm'; family = 'linux'; preset = 'linux-arm64-release-ci'; build_dir = 'build/linux-arm64-release'; sdk = 'linux-arm64' },
  @{ id = 'linux-clang-release'; runner = 'ubuntu-24.04'; family = 'linux'; preset = 'linux-clang-release-ci'; build_dir = 'build/linux-clang-release'; sdk = '' },
  @{ id = 'windows-release'; runner = 'windows-2025'; family = 'windows'; preset = 'win-release-ci'; build_dir = 'build/Msvc-Release'; sdk = 'windows-x64' },
  @{ id = 'macos-release'; runner = 'macos-15'; family = 'mac'; preset = 'mac-arm64-release-ci'; build_dir = 'build/mac-arm64-gcc-release'; sdk = 'macos-arm64' },
  @{ id = 'macos-clang-release'; runner = 'macos-15'; family = 'mac'; preset = 'mac-arm64-clang-release-ci'; build_dir = 'build/mac-arm64-clang-release'; sdk = '' },
  @{ id = 'android-arm64-v8a-release'; runner = 'ubuntu-24.04'; family = 'android'; preset = 'android-arm64-v8a-release-ci'; build_dir = 'build/android-arm64-v8a-release'; sdk = 'android-arm64-v8a' },
  @{ id = 'ios-arm64-release'; runner = 'macos-15'; family = 'ios'; preset = 'ios-arm64-release-ci'; build_dir = 'build/ios-arm64'; sdk = 'ios-arm64'; triplet = 'arm64-ios' }
)
$builds = @()
foreach ($profile in $profiles) {
  if ($acePatternsDevelopment -and $profile.id -ne 'linux-release') { continue }
  # Clang profiles qualify the same portable/native contracts in isolated trees;
  # they do not produce additional release packages.
  $profile.clang = $profile.id -in @('linux-clang-release', 'macos-clang-release')
  # The connection-manager integration branch uses GCC for Linux and macOS.
  if ($HeadBranch -eq 'codex/cnet-manager-1001' -and $profile.clang) { continue }
  $entry = $profile.Clone()
  $entry.cross = $entry.family -in @('android', 'ios')
  # Core semantic qualification must also pass without the optional #981 backend.
  # Explicit release packaging still includes the qualified native specialization.
  $entry.native_thunks = if ($PrepareRelease -and $entry.id -in @('linux-release', 'linux-clang-release', 'windows-release')) { 'ON' } else { 'OFF' }
  $entry.native = $native -and -not $entry.cross -and $entry.id -ne 'linux-arm64-release'
  $entry.execution = $execution -and -not $entry.cross -and $entry.id -ne 'linux-arm64-release'
  $entry.armcontracts = $entry.id -eq 'linux-arm64-release' -and ($native -or $execution)
  $entry.projection = $projection -and $entry.id -in @('linux-release', 'linux-clang-release', 'macos-clang-release')
  $entry.benchmarks = if ($entry.id -in @('linux-release', 'windows-release', 'macos-release')) { 'ON' } else { 'OFF' }
  $entry.package = $PrepareRelease -and [bool]$entry.sdk
  $entry.compare = $compare -and $EventName -eq 'pull_request' -and $entry.id -eq 'windows-release'
  $entry.artifact = if ($entry.cross) { $mobile } else { $work -and [bool]$entry.sdk }
  $entry.full_tests = $componentIntegration -and $entry.id -eq 'linux-release'
  if ($entry.full_tests) {
    $entry.native = $false
    $entry.execution = $false
    $entry.projection = $false
    $entry.benchmarks = 'OFF'
    $entry.compare = $false
    $entry.artifact = $false
  }
  if ($entry.full_tests -or $entry.native -or $entry.execution -or $entry.projection -or $entry.armcontracts -or $entry.package -or $entry.artifact) {
    $builds += $entry
  }
}
$matrix = ConvertTo-Json -InputObject @{ include = $builds } -Depth 5 -Compress
Write-Output $matrix
if ($env:GITHUB_OUTPUT) {
  Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "builds=$matrix"
  Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "has_builds=$($builds.Count -gt 0)".ToLowerInvariant()
}
if ($env:GITHUB_STEP_SUMMARY) {
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "| Check | Selected |`n|---|---|"
  foreach ($key in $checks.Keys) {
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "| $key | $($checks[$key]) |"
  }
}
