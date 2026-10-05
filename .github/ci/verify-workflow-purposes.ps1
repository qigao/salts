$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

function Read-Workflow([string]$name) {
  $path = Join-Path $root ".github/workflows/$name"
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "CI purpose contract: workflow not found: $name"
  }
  return Get-Content -LiteralPath $path -Raw
}

function Require-Marker([string]$text, [string]$marker, [string]$label) {
  if (-not $text.Contains($marker)) {
    throw "CI purpose contract: $label must contain: $marker"
  }
}

function Forbid-Marker([string]$text, [string]$marker, [string]$label) {
  if ($text.Contains($marker)) {
    throw "CI purpose contract: $label must not contain: $marker"
  }
}

$release = Read-Workflow "native-nuget-release.yml"
$correctness = Read-Workflow "native-pr-checks.yml"
$debug = Read-Workflow "native-debug.yml"
$benchmark = Read-Workflow "native-io-benchmarks.yml"

Require-Marker $release 'tags: ["v*"]' "release"
Require-Marker $release "workflow_dispatch:" "release"
Forbid-Marker $release "pull_request:" "release"
Forbid-Marker $release "BUILD_BENCHMARKS=ON" "release"
Forbid-Marker $release "ENABLE_SANITIZER_ADDRESS=ON" "release"
Forbid-Marker $release "linux-dev-user" "release"

Require-Marker $correctness "pull_request:" "correctness"
Require-Marker $correctness "BUILD_BENCHMARKS=OFF" "correctness"
Forbid-Marker $correctness "BUILD_BENCHMARKS=ON" "correctness"
Forbid-Marker $correctness "ENABLE_SANITIZER_ADDRESS=ON" "correctness"
Forbid-Marker $correctness 'tags: ["v*"]' "correctness"
Forbid-Marker $correctness "dotnet nuget push" "correctness"

Require-Marker $debug "pull_request:" "debug"
Require-Marker $debug "ENABLE_SANITIZER_ADDRESS=ON" "debug"
Require-Marker $debug "BUILD_BENCHMARKS=OFF" "debug"
Forbid-Marker $debug "BUILD_BENCHMARKS=ON" "debug"
Forbid-Marker $debug 'tags: ["v*"]' "debug"
Forbid-Marker $debug "dotnet nuget push" "debug"

Require-Marker $benchmark "pull_request:" "benchmark"
Require-Marker $benchmark "BUILD_BENCHMARKS=ON" "benchmark"
Require-Marker $benchmark "native-io-benchmark-" "benchmark"
Forbid-Marker $benchmark "ctest " "benchmark"
Forbid-Marker $benchmark "BUILD_TESTS=ON" "benchmark"
Forbid-Marker $benchmark "ENABLE_SANITIZER_ADDRESS" "benchmark"
Forbid-Marker $benchmark "native-io-release-" "benchmark"
Forbid-Marker $benchmark 'tags: ["v*"]' "benchmark"
Forbid-Marker $benchmark "dotnet nuget push" "benchmark"

Write-Host "CI purpose contract passed: release, correctness, debug, and benchmark workflows are separated."
