$ErrorActionPreference = 'Stop'
$runId = $env:SALTS_CANDIDATE_RUN_ID
$commit = $env:SALTS_CANDIDATE_COMMIT
$rid = $env:SALTS_CANDIDATE_RID
if ($runId -notmatch '^[1-9][0-9]*$' -or $commit -notmatch '^[0-9a-f]{40}$') {
  throw 'Candidate selection requires a run ID and full lowercase commit SHA'
}
$artifact = switch -Regex ($rid) {
  '^macos-(x64|arm64)$' { 'salts-sdk-macos'; break }
  '^(linux-x64|linux-arm64|windows-x64|android-arm64-v8a|ios-arm64|ios-simulator-arm64)$' { "salts-sdk-$rid"; break }
  default { throw "Unsupported candidate RID: $rid" }
}
$runJson = gh api "repos/qigao/salts/actions/runs/$runId"
if ($LASTEXITCODE -ne 0) { throw 'Cannot read candidate producer run' }
$run = $runJson | ConvertFrom-Json
if ($run.repository.full_name -ne 'qigao/salts' -or
    $run.path -ne '.github/workflows/ci.yml' -or
    $run.event -ne 'workflow_dispatch' -or
    $run.head_sha -ne $commit -or
    $run.status -ne 'completed' -or $run.conclusion -ne 'success') {
  throw 'Candidate must come from a successful Salts CI dispatch at the specified commit'
}
$destination = Join-Path $env:RUNNER_TEMP "salts-candidate-$runId-$rid"
if (Test-Path -LiteralPath $destination) {
  throw "Candidate destination already exists: $destination"
}
gh run download $runId --repo qigao/salts --name $artifact --dir $destination
if ($LASTEXITCODE -ne 0) { throw "Cannot download candidate artifact $artifact" }
$sdkRoot = Join-Path $destination $rid
$manifest = Get-Content -LiteralPath (Join-Path $sdkRoot 'salts-sdk-manifest.txt') -Raw | ConvertFrom-StringData
if ($manifest.commit -ne $commit -or $manifest.profile -ne 'release') {
  throw 'Candidate SDK manifest does not match the selected Release commit'
}
if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'lib/cmake/Salts/SaltsConfig.cmake') -PathType Leaf)) {
  throw 'Candidate artifact does not contain a Salts SDK'
}
"SALTS_CANDIDATE_ROOT=$sdkRoot" >> $env:GITHUB_ENV
Write-Host "Selected Salts candidate $commit from run $runId for $rid"
