param(
  [string]$PolicyPath = "cmake/ci/benchmark-policy.json",
  [string]$EvidenceDir = "native-io-results",
  [string]$Backend = "",
  [string]$JobStatus = "success",
  [string]$OutputPath = "native-io-results/benchmark-verdicts.csv",
  [switch]$WriteSummary
)
$ErrorActionPreference = "Stop"
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

function Escape-Markdown([string]$Value) {
  if ($null -eq $Value) { return "" }
  return $Value.Replace("|","\|").Replace([char]13," ").Replace([char]10," ")
}

function Find-Evidence($Patterns) {
  $found = @()
  foreach ($pattern in @($Patterns)) {
    $path = Join-Path $EvidenceDir ([string]$pattern)
    $found += @(Get-ChildItem -Path $path -File -ErrorAction SilentlyContinue)
  }
  return @($found | Sort-Object FullName -Unique)
}

function Parse-Double([object]$Value, [string]$Name) {
  $parsed = [double]0
  if (-not [double]::TryParse([string]$Value,[System.Globalization.NumberStyles]::Float,$Invariant,[ref]$parsed)) {
    throw "invalid double for $Name : $Value"
  }
  return $parsed
}

$policy = Get-Content -LiteralPath $PolicyPath -Raw | ConvertFrom-Json
$rows = [System.Collections.Generic.List[object]]::new()
$suppressedContracts = 0

foreach ($family in @($policy.families)) {
  $evidence = Find-Evidence $family.evidence
  if ($evidence.Count -eq 0) { continue }
  $result = ""
  $observed = ""

  if ($family.role -eq "PERFORMANCE_ANCHOR" -and $family.id -eq "cnet_scaling") {
    $paired = Import-Csv -LiteralPath $evidence[0].FullName
    $anchorRows = @($paired | Where-Object {
      [int]$_.connections -eq 16 -and
      [int]$_.payload_bytes -eq 65536 -and
      $_.baseline -eq "NativeIO direct" -and
      $_.candidate -eq "CNet retained"
    })
    if ($anchorRows.Count -ne 1) { throw "expected exactly one cnet_scaling anchor row, got $($anchorRows.Count)" }
    $r = $anchorRows[0]
    $rate = Parse-Double $r.rate_delta_median_percent "rate delta"
    $rateMad = Parse-Double $r.rate_delta_mad_pp "rate MAD"
    $p50 = Parse-Double $r.p50_delta_median_percent "p50 delta"
    $p50Mad = Parse-Double $r.p50_delta_mad_pp "p50 MAD"
    $p95 = Parse-Double $r.p95_delta_median_percent "p95 delta"
    $p95Mad = Parse-Double $r.p95_delta_mad_pp "p95 MAD"
    $noisy = $rateMad -gt 5.0 -or $p50Mad -gt 5.0 -or $p95Mad -gt 10.0
    if ($noisy) {
      $result = "UNSTABLE"
    } elseif ($rate -lt -5.0 -or $p50 -gt 10.0 -or $p95 -gt 15.0) {
      $result = "FAIL"
    } else {
      $result = "PASS"
    }
    $observed = ("backend={0}; rate={1:+0.00;-0.00;0.00}% MAD={2:0.00}pp; p50={3:+0.00;-0.00;0.00}% MAD={4:0.00}pp; p95={5:+0.00;-0.00;0.00}% MAD={6:0.00}pp" -f $r.backend,$rate,$rateMad,$p50,$p50Mad,$p95,$p95Mad)
  } elseif ($family.role -eq "CONTRACT") {
    if ($JobStatus -ne "success") {
      $suppressedContracts++
      continue
    }
    $result = "PASS"
    $observed = "contract verifier completed; evidence present"
  } else {
    $result = "DIAGNOSTIC"
    $observed = "evidence present: $($evidence.Count) file(s)"
  }

  $rows.Add([pscustomobject]@{
    family = [string]$family.id
    scope = [string]$family.scope
    backend = $Backend
    role = [string]$family.role
    anchor = [string]$family.anchor
    noise = [string]$family.noise
    threshold = [string]$family.threshold
    result = $result
    observed = $observed
    evidence = (($evidence | ForEach-Object { $_.Name }) -join ";")
  })
}

$outputDir = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDir)) { New-Item -ItemType Directory -Force -Path $outputDir | Out-Null }
$rows | Export-Csv -LiteralPath $OutputPath -NoTypeInformation
Write-Host ("Benchmark verdict rows: {0}; backend={1}; job_status={2}; suppressed_contract_rows={3}" -f $rows.Count,$Backend,$JobStatus,$suppressedContracts)

if ($WriteSummary) {
  if ([string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) { throw "-WriteSummary requires GITHUB_STEP_SUMMARY" }
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ""
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "## Benchmark verdicts — $Backend"
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ""
  if ($JobStatus -ne "success") {
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "> Job status is **$JobStatus**. CONTRACT PASS rows are intentionally suppressed after a failing step; inspect the first failing verifier for the authoritative contract failure."
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ""
  }
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "| Family | Role | Anchor | Noise | Threshold | Result | Observed | Evidence |"
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "|---|---|---|---|---|---|---|---|"
  foreach ($row in $rows) {
    $cells = @($row.family,$row.role,$row.anchor,$row.noise,$row.threshold,$row.result,$row.observed,$row.evidence) | ForEach-Object { Escape-Markdown ([string]$_) }
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ("| {0} | {1} | {2} | {3} | {4} | **{5}** | {6} | {7} |" -f $cells)
  }
}
