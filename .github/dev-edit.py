from pathlib import Path

path = Path(".github/workflows/native-io-release-benchmarks.yml")
text = path.read_text()
marker = """      - name: Run libuv versus NativeIO direct/coroutine versus CNet benchmark
        if: steps.scope.outputs.adapted == 'true'
"""
if text.count(marker) != 1:
    raise SystemExit(f"benchmark marker: expected 1 match, got {text.count(marker)}")
insert = """      - name: Build Windows PR-base CNet
        if: matrix.family == 'windows' && github.event_name == 'pull_request' && steps.scope.outputs.adapted == 'true'
        shell: cmd
        run: |
          set "CNET_BASELINE_SOURCE=%RUNNER_TEMP%\\salts-cnet-baseline"
          git worktree add --detach "%CNET_BASELINE_SOURCE%" "${{ github.event.pull_request.base.sha }}"
          if errorlevel 1 exit /b 1
          pushd "%CNET_BASELINE_SOURCE%"
          call "%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
          if errorlevel 1 exit /b 1
          cmake --preset win-release-user -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF
          if errorlevel 1 exit /b 1
          cmake --build --preset win-release-user --target salts_cnet
          if errorlevel 1 exit /b 1
          popd
          echo CNET_BASELINE_SOURCE=%CNET_BASELINE_SOURCE%>>"%GITHUB_ENV%"

      - name: Compare PR-base owner with direct candidate
        if: matrix.family == 'windows' && github.event_name == 'pull_request' && steps.scope.outputs.adapted == 'true'
        shell: pwsh
        env:
          CNET_IO_BENCHMARK_BACKEND: iocp
          CNET_COMPARE_BASE_SHA: ${{ github.event.pull_request.base.sha }}
          CNET_COMPARE_HEAD_SHA: ${{ github.event.pull_request.head.sha }}
        run: |
          $ErrorActionPreference = "Stop"
          $resultDir = Join-Path $env:GITHUB_WORKSPACE "native-io-results"
          New-Item -ItemType Directory -Force -Path $resultDir | Out-Null
          $baselineDso = Join-Path $env:CNET_BASELINE_SOURCE "build\\Msvc-Release\\bin\\salts_cnet.dll"
          $candidateDso = Join-Path $env:GITHUB_WORKSPACE "${{ matrix.build_dir }}\\bin\\salts_cnet.dll"
          $compare = Join-Path $env:GITHUB_WORKSPACE "${{ matrix.build_dir }}\\bin\\cnet_owner_lifecycle_compare.exe"
          foreach ($path in @($baselineDso, $candidateDso, $compare)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
              throw "CNet owner comparison input not found: $path"
            }
          }
          $report = Join-Path $resultDir "cnet-owner-base-vs-direct.md"
          & $compare $baselineDso $candidateDso $report
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          @(
            "baseline_sha=$env:CNET_COMPARE_BASE_SHA"
            "candidate_sha=$env:CNET_COMPARE_HEAD_SHA"
            "backend=iocp"
          ) | Set-Content -LiteralPath (Join-Path $resultDir "cnet-owner-compare-metadata.txt")
          Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ""
          Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "## CNet PR-base coroutine owner versus canonical direct owner"
          Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ""
          Get-Content -LiteralPath $report | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY

      - name: Run libuv versus NativeIO direct/coroutine versus CNet benchmark
        if: steps.scope.outputs.adapted == 'true'
"""
text = text.replace(marker, insert, 1)
for required in (
    "Build Windows PR-base CNet",
    "Compare PR-base owner with direct candidate",
    "cnet-owner-base-vs-direct.md",
    "github.event.pull_request.base.sha",
    "cnet_owner_lifecycle_compare.exe",
):
    if required not in text:
        raise SystemExit(f"missing workflow comparison contract: {required}")
path.write_text(text)
