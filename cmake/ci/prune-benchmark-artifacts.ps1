param(
  [Parameter(Mandatory = $true)]
  [string]$EvidenceDir
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) { exit 0 }
$rawFiles = @((Join-Path $EvidenceDir "io.samples.csv"))
foreach ($path in $rawFiles) {
  if (Test-Path -LiteralPath $path -PathType Leaf) {
    $size = (Get-Item -LiteralPath $path).Length
    Remove-Item -LiteralPath $path -Force
    Write-Host ("pruned {0} bytes: {1}" -f $size, $path)
  }
}
foreach ($name in @("syscalls","stream-style-syscalls","queue-depth-syscalls")) {
  $path = Join-Path $EvidenceDir $name
  if (Test-Path -LiteralPath $path -PathType Container) {
    $measure = Get-ChildItem -LiteralPath $path -Recurse -File | Measure-Object -Property Length -Sum
    $bytes = if ($null -eq $measure.Sum) { 0 } else { $measure.Sum }
    Remove-Item -LiteralPath $path -Recurse -Force
    Write-Host ("pruned {0} bytes: {1}" -f $bytes, $path)
  }
}
