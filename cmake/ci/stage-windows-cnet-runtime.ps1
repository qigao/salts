param(
    [Parameter(Mandatory = $true)]
    [string]$Prefix,

    [string]$VcpkgInstalledDir = $env:VCPKG_INSTALLED_DIR
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($VcpkgInstalledDir)) {
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
    $VcpkgInstalledDir = Join-Path $repoRoot "vcpkg_installed"
}

if (-not (Test-Path -LiteralPath $VcpkgInstalledDir -PathType Container)) {
    throw "vcpkg installed tree is unavailable: $VcpkgInstalledDir"
}

$tlsRuntimeDir = Join-Path $VcpkgInstalledDir "x64-windows\bin"
$sdkBin = Join-Path $Prefix "bin"
New-Item -ItemType Directory -Force -Path $sdkBin | Out-Null

foreach ($dllName in @("ssl.dll", "crypto.dll")) {
    $source = Join-Path $tlsRuntimeDir $dllName
    $destination = Join-Path $sdkBin $dllName

    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "missing private CNet TLS runtime: $source"
    }

    Copy-Item -LiteralPath $source -Destination $destination -Force

    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        throw "failed to stage private CNet TLS runtime: $destination"
    }
}
