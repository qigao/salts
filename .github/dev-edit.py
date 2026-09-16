from pathlib import Path


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)

runner_path = Path("cnet/benchmarks/cnet_owner_lifecycle_compare.c")
runner = runner_path.read_text()
runner = replace_once(
    runner,
    """  if (status == SALTS_OK) {
    const size_t demand =
        CNET_OWNER_COMPARE_PAYLOAD_BYTES * (size_t)CNET_OWNER_COMPARE_TOTAL_EXCHANGES;
    status = api->receive(&fixture->client, fixture->connection, demand);
  }
""",
    """  if (status == SALTS_OK) {
    /* Demand counts future receive callbacks, not bytes. A partial TCP chunk
       replenishes one demand from compare_cnet_receive(). */
    const size_t demand = (size_t)CNET_OWNER_COMPARE_TOTAL_EXCHANGES;
    status = api->receive(&fixture->client, fixture->connection, demand);
  }
""",
    "receive demand units",
)
runner_path.write_text(runner)

workflow_path = Path(".github/workflows/native-io-release-benchmarks.yml")
workflow = workflow_path.read_text()
anchor = """      - name: Run libuv versus NativeIO direct/coroutine versus CNet benchmark
        if: steps.scope.outputs.adapted == 'true'
"""
block = r'''      - name: Build PR-base CNet comparison DSO
        if: matrix.family == 'windows' && steps.scope.outputs.adapted == 'true' && github.event_name == 'pull_request'
        shell: cmd
        env:
          CNET_COMPARE_BASE_SHA: ${{ github.event.pull_request.base.sha }}
        run: |
          set "BASELINE_SOURCE=%RUNNER_TEMP%\salts-cnet-baseline"
          if exist "%BASELINE_SOURCE%" rmdir /s /q "%BASELINE_SOURCE%"
          git worktree add --detach "%BASELINE_SOURCE%" "%CNET_COMPARE_BASE_SHA%"
          if errorlevel 1 exit /b 1
          cd /d "%BASELINE_SOURCE%"
          set "PROJECT_ROOT=%BASELINE_SOURCE%"
          call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
          if errorlevel 1 exit /b 1
          cmake --preset win-release-user -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF
          if errorlevel 1 exit /b 1
          cmake --build --preset win-release-user --target salts_cnet
          if errorlevel 1 exit /b 1
          if not exist "build\Msvc-Release\bin\salts_cnet.dll" exit /b 1
          echo CNET_COMPARE_BASELINE_DSO=%BASELINE_SOURCE%\build\Msvc-Release\bin\salts_cnet.dll>>"%GITHUB_ENV%"
          echo CNET_COMPARE_CANDIDATE_DSO=%GITHUB_WORKSPACE%\${{ matrix.build_dir }}\bin\salts_cnet.dll>>"%GITHUB_ENV%"
          echo CNET_COMPARE_EXECUTABLE=%GITHUB_WORKSPACE%\${{ matrix.build_dir }}\bin\cnet_owner_lifecycle_compare.exe>>"%GITHUB_ENV%"

      - name: Compare PR-base and candidate CNet owner
        if: matrix.family == 'windows' && steps.scope.outputs.adapted == 'true' && github.event_name == 'pull_request'
        shell: pwsh
        env:
          CNET_IO_BENCHMARK_BACKEND: iocp
          CNET_COMPARE_BASE_SHA: ${{ github.event.pull_request.base.sha }}
          CNET_COMPARE_HEAD_SHA: ${{ github.event.pull_request.head.sha || github.sha }}
        run: |
          $ErrorActionPreference = "Stop"
          $resultDir = Join-Path $env:GITHUB_WORKSPACE "native-io-results"
          New-Item -ItemType Directory -Force -Path $resultDir | Out-Null
          foreach ($path in @($env:CNET_COMPARE_BASELINE_DSO,
                              $env:CNET_COMPARE_CANDIDATE_DSO,
                              $env:CNET_COMPARE_EXECUTABLE)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
              throw "CNet owner comparison input not found: $path"
            }
          }
          $report = Join-Path $resultDir "cnet-owner-base-vs-direct.md"
          & $env:CNET_COMPARE_EXECUTABLE `
            $env:CNET_COMPARE_BASELINE_DSO `
            $env:CNET_COMPARE_CANDIDATE_DSO `
            $report
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          @(
            "base_sha=$env:CNET_COMPARE_BASE_SHA"
            "candidate_sha=$env:CNET_COMPARE_HEAD_SHA"
            "baseline_dso=$env:CNET_COMPARE_BASELINE_DSO"
            "candidate_dso=$env:CNET_COMPARE_CANDIDATE_DSO"
          ) | Set-Content -LiteralPath (Join-Path $resultDir "cnet-owner-compare-metadata.txt")
          Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ""
          Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "## CNet owner PR-base versus candidate: IOCP"
          Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ""
          Get-Content -LiteralPath $report | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY

'''
workflow = replace_once(workflow, anchor, block + anchor, "comparison workflow anchor")
workflow_path.write_text(workflow)
