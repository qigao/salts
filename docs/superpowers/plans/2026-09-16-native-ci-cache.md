# Native CI Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Accelerate every native GitHub Actions build with a shared ABI-scoped vcpkg binary cache and make exact-head Linux/Windows installed Salts SDKs reusable by independent consumer checks without weakening source or benchmark provenance.

**Architecture:** `cmeta.yml` and `native-io-release-benchmarks.yml` share one vcpkg binary-cache key convention so compatible native jobs can reuse dependency packages across workflows and Salts commits. `cmeta.yml` additionally publishes exact-SHA Linux/Windows Release SDK caches for consumer-only jobs; current-source conformance, sanitizer, and benchmark jobs always compile Salts from the checked-out head. A small Linux installed-consumer job restores the SDK cache after the provider job and proves that cached Salts remains a usable `find_package(Salts CONFIG REQUIRED)` package.

**Tech Stack:** GitHub Actions, `actions/cache@v4`, `actions/cache/save@v4`, `actions/cache/restore@v4`, vcpkg manifest binary caching, CMake presets, Ninja, CTest.

**Spec:** `docs/superpowers/specs/2026-09-16-ci-consolidation-cleanup-design.md`

## Global Constraints

- vcpkg cache reuse may cross Salts commits only when the dependency fingerprint and ABI tuple match.
- vcpkg keys must isolate OS, runner architecture, runner image/toolchain identity, and effective target triplet.
- The dependency fingerprint must include `vcpkg.json`, `vcpkg-configuration.json`, and `vcpkg-ports/**`.
- No vcpkg restore fallback may cross platform or triplet families.
- Installed Salts SDK cache keys must include the exact Salts head SHA and must not have a cross-SHA restore fallback.
- Linux/Windows source conformance, Linux sanitizer builds, and NativeIO/CNet benchmark/profile targets must continue compiling current sources even when an SDK cache exists.
- Cache hit/miss must be visible in logs; a miss must not fail CI.
- Cache hits never skip regression or consumer tests.
- `ci.yml` and `cmeta-cflow-calculus.yml` remain outside this cache layer because they do not build the native dependency graph.
- Android keeps its existing package evidence artifact; this phase adds vcpkg caching but does not create an Android Salts SDK cache.
- macOS gets vcpkg caching but no Salts SDK cache until a real independent installed-package consumer requires one.

---

### Task C1: Add one vcpkg binary-cache contract to canonical native conformance

**Files:**
- Modify: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: existing runner-provided vcpkg installations and current `VCPKG_ROOT` setup.
- Produces: `VCPKG_BINARY_SOURCES=clear;files,<workspace>/build/vcpkg-binary-cache,readwrite` plus `actions/cache@v4` restore/save for Linux, macOS, Windows, and Android.
- Key namespace: `vcpkg-binary-v2-<os>-<arch>-<image>-<triplet>-<dependency-hash>`.

- [ ] **Step 1: Record the cache coverage RED**

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES" .github/workflows/cmeta.yml
```

Expected: only the pre-existing Windows `VCPKG_BINARY_SOURCES` implementation is present; no `vcpkg-binary-v2` key exists.

- [ ] **Step 2: Add Linux ABI identity and binary-cache environment**

Give `Setup re2c and vcpkg` the id `linux-env`. After resolving `VCPKG_ROOT`, append:

```bash
binary_cache="$GITHUB_WORKSPACE/build/vcpkg-binary-cache"
mkdir -p "$binary_cache"
echo "VCPKG_BINARY_CACHE=$binary_cache" >> "$GITHUB_ENV"
echo "VCPKG_BINARY_SOURCES=clear;files,$binary_cache,readwrite" >> "$GITHUB_ENV"
echo "image=${ImageOS:-linux}-${ImageVersion:-unknown}" >> "$GITHUB_OUTPUT"
echo "triplet=x64-linux" >> "$GITHUB_OUTPUT"
```

Immediately after setup add:

```yaml
      - name: Restore Linux vcpkg binary cache
        id: linux-vcpkg-cache
        uses: actions/cache@v4
        with:
          path: build/vcpkg-binary-cache
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.linux-env.outputs.image }}-${{ steps.linux-env.outputs.triplet }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.linux-env.outputs.image }}-${{ steps.linux-env.outputs.triplet }}-

      - name: Report Linux vcpkg cache state
        run: echo "vcpkg cache hit=${{ steps.linux-vcpkg-cache.outputs.cache-hit }}"
```

Do not cache `vcpkg_installed` or any CMake build directory.

- [ ] **Step 3: Add macOS ABI identity and binary-cache environment**

Give macOS `Setup re2c and vcpkg` the id `macos-env`. Preserve the existing `VCPKG_ROOT` resolution, then append:

```bash
binary_cache="$GITHUB_WORKSPACE/build/vcpkg-binary-cache"
mkdir -p "$binary_cache"
echo "VCPKG_BINARY_CACHE=$binary_cache" >> "$GITHUB_ENV"
echo "VCPKG_BINARY_SOURCES=clear;files,$binary_cache,readwrite" >> "$GITHUB_ENV"
echo "image=${ImageOS:-macos}-${ImageVersion:-unknown}" >> "$GITHUB_OUTPUT"
echo "triplet=default-osx-${RUNNER_ARCH}" >> "$GITHUB_OUTPUT"
```

Then add:

```yaml
      - name: Restore macOS vcpkg binary cache
        id: macos-vcpkg-cache
        uses: actions/cache@v4
        with:
          path: build/vcpkg-binary-cache
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.macos-env.outputs.image }}-${{ steps.macos-env.outputs.triplet }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.macos-env.outputs.image }}-${{ steps.macos-env.outputs.triplet }}-

      - name: Report macOS vcpkg cache state
        run: echo "vcpkg cache hit=${{ steps.macos-vcpkg-cache.outputs.cache-hit }}"
```

`default-osx-${RUNNER_ARCH}` names the host-derived default vcpkg target without changing the existing macOS preset.

- [ ] **Step 4: Strengthen the existing Windows key**

In `Setup Windows build environment`, keep the existing binary-cache directory and add:

```powershell
"triplet=x64-windows" >> $env:GITHUB_OUTPUT
```

Replace the current cache block with:

```yaml
      - name: Restore Windows vcpkg binary cache
        id: windows-vcpkg-cache
        uses: actions/cache@v4
        with:
          path: build/vcpkg-binary-cache
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-

      - name: Report Windows vcpkg cache state
        shell: pwsh
        run: Write-Host "vcpkg cache hit=${{ steps.windows-vcpkg-cache.outputs.cache-hit }}"
```

Delete the old broader restore prefix that stopped at OS/arch.

- [ ] **Step 5: Add Android ABI identity and binary-cache environment**

Give `Setup re2c and validate toolchains` the id `android-env`. After validating `ANDROID_NDK_HOME`, append:

```bash
binary_cache="$GITHUB_WORKSPACE/build/vcpkg-binary-cache"
mkdir -p "$binary_cache"
echo "VCPKG_BINARY_CACHE=$binary_cache" >> "$GITHUB_ENV"
echo "VCPKG_BINARY_SOURCES=clear;files,$binary_cache,readwrite" >> "$GITHUB_ENV"
ndk_revision="$(awk -F'= ' '/Pkg.Revision/ {print $2}' "$ANDROID_NDK_HOME/source.properties")"
test -n "$ndk_revision"
echo "image=${ImageOS:-linux}-${ImageVersion:-unknown}" >> "$GITHUB_OUTPUT"
echo "triplet=arm64-android" >> "$GITHUB_OUTPUT"
echo "ndk=$ndk_revision" >> "$GITHUB_OUTPUT"
```

Then add:

```yaml
      - name: Restore Android vcpkg binary cache
        id: android-vcpkg-cache
        uses: actions/cache@v4
        with:
          path: build/vcpkg-binary-cache
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.android-env.outputs.image }}-${{ steps.android-env.outputs.triplet }}-ndk-${{ steps.android-env.outputs.ndk }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.android-env.outputs.image }}-${{ steps.android-env.outputs.triplet }}-ndk-${{ steps.android-env.outputs.ndk }}-

      - name: Report Android vcpkg cache state
        run: echo "vcpkg cache hit=${{ steps.android-vcpkg-cache.outputs.cache-hit }}"
```

- [ ] **Step 6: Validate and commit**

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES|cache hit=" .github/workflows/cmeta.yml
git diff --check
git add .github/workflows/cmeta.yml
git commit -m "ci: share vcpkg binary caches across native conformance"
```

- [ ] **Step 7: Verify exact-head GREEN**

Require `CMeta conformance` to remain green on Linux/macOS/Windows/Android. A first run may report misses. Each native job must log its cache state and still execute configure/build/tests.

---

### Task C2: Reuse the same vcpkg binary-cache namespace in NativeIO/CNet benchmarks

**Files:**
- Modify: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Consumes: the key convention from C1.
- Produces: dependency cache reuse without changing benchmark executable provenance or benchmark protocol.

- [ ] **Step 1: Record the benchmark cache RED**

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES" .github/workflows/native-io-release-benchmarks.yml
```

Expected: only the old Windows cache exists.

- [ ] **Step 2: Add Linux cache setup and restore**

Give `Setup Linux dependencies` id `linux-env` and append:

```bash
binary_cache="$GITHUB_WORKSPACE/build/vcpkg-binary-cache"
mkdir -p "$binary_cache"
echo "VCPKG_BINARY_CACHE=$binary_cache" >> "$GITHUB_ENV"
echo "VCPKG_BINARY_SOURCES=clear;files,$binary_cache,readwrite" >> "$GITHUB_ENV"
echo "image=${ImageOS:-linux}-${ImageVersion:-unknown}" >> "$GITHUB_OUTPUT"
echo "triplet=x64-linux" >> "$GITHUB_OUTPUT"
```

Add immediately after it:

```yaml
      - name: Restore Linux vcpkg binary cache
        if: matrix.family == 'linux'
        id: linux-vcpkg-cache
        uses: actions/cache@v4
        with:
          path: build/vcpkg-binary-cache
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.linux-env.outputs.image }}-${{ steps.linux-env.outputs.triplet }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.linux-env.outputs.image }}-${{ steps.linux-env.outputs.triplet }}-

      - name: Report Linux vcpkg cache state
        if: matrix.family == 'linux'
        shell: bash
        run: echo "vcpkg cache hit=${{ steps.linux-vcpkg-cache.outputs.cache-hit }}"
```

- [ ] **Step 3: Add macOS cache setup and restore**

Give `Setup macOS dependencies` id `macos-env` and append after the existing `VCPKG_ROOT` resolution:

```bash
binary_cache="$GITHUB_WORKSPACE/build/vcpkg-binary-cache"
mkdir -p "$binary_cache"
echo "VCPKG_BINARY_CACHE=$binary_cache" >> "$GITHUB_ENV"
echo "VCPKG_BINARY_SOURCES=clear;files,$binary_cache,readwrite" >> "$GITHUB_ENV"
echo "image=${ImageOS:-macos}-${ImageVersion:-unknown}" >> "$GITHUB_OUTPUT"
echo "triplet=default-osx-${RUNNER_ARCH}" >> "$GITHUB_OUTPUT"
```

Add:

```yaml
      - name: Restore macOS vcpkg binary cache
        if: matrix.family == 'mac'
        id: macos-vcpkg-cache
        uses: actions/cache@v4
        with:
          path: build/vcpkg-binary-cache
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.macos-env.outputs.image }}-${{ steps.macos-env.outputs.triplet }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.macos-env.outputs.image }}-${{ steps.macos-env.outputs.triplet }}-

      - name: Report macOS vcpkg cache state
        if: matrix.family == 'mac'
        shell: bash
        run: echo "vcpkg cache hit=${{ steps.macos-vcpkg-cache.outputs.cache-hit }}"
```

- [ ] **Step 4: Upgrade Windows benchmark cache key**

In `Setup Windows build environment`, add:

```powershell
"triplet=x64-windows" >> $env:GITHUB_OUTPUT
```

Replace the current Windows cache block with:

```yaml
      - name: Restore Windows vcpkg binary cache
        if: matrix.family == 'windows'
        id: windows-vcpkg-cache
        uses: actions/cache@v4
        with:
          path: build/vcpkg-binary-cache
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-

      - name: Report Windows vcpkg cache state
        if: matrix.family == 'windows'
        shell: pwsh
        run: Write-Host "vcpkg cache hit=${{ steps.windows-vcpkg-cache.outputs.cache-hit }}"
```

The `windows-2022` benchmark runner remains isolated from `windows-latest` conformance if image identity differs.

- [ ] **Step 5: Preserve benchmark provenance**

Do not condition configure/build/contract tests/benchmark execution on a cache hit. Keep:

```yaml
          BENCHMARK_COMMIT: ${{ github.event.pull_request.head.sha || github.sha }}
```

and keep compiling `cnet_io_benchmark`, NativeIO tests, and CNet/profile tests from the checked-out source.

- [ ] **Step 6: Validate and commit**

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES|BENCHMARK_COMMIT" .github/workflows/native-io-release-benchmarks.yml
git diff --check
git add .github/workflows/native-io-release-benchmarks.yml
git commit -m "ci(perf): reuse native vcpkg binary cache"
```

- [ ] **Step 7: Verify four-platform benchmark GREEN**

Require epoll, io_uring, kqueue, and IOCP exact-head build/contracts/benchmark/artifact success. Benchmark metadata must still report the PR head SHA. Cache state is build evidence only and must not alter the IOCP run-quality decision.

---

### Task C3: Publish exact-SHA Linux and Windows installed Salts SDK caches

**Files:**
- Modify: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: canonical Release installs already produced at `external/pkgs/salts/release`.
- Produces: `salts-sdk-v1-<os>-<arch>-<image>-<triplet>-release-<exact-sha>-<dependency-hash>`.

- [ ] **Step 1: Record the SDK-cache RED**

```bash
rg -n "salts-sdk-v1|salts-sdk-manifest" .github/workflows/cmeta.yml
```

Expected: no matches.

- [ ] **Step 2: Stage and attest the Linux SDK after install verification**

After `Verify installed package targets`, add:

```yaml
      - name: Stage Linux installed Salts SDK cache
        shell: bash
        run: |
          set -euxo pipefail
          sdk="$GITHUB_WORKSPACE/build/ci-salts-sdk"
          rm -rf "$sdk"
          mkdir -p "$sdk"
          cp -a "$GITHUB_WORKSPACE/external/pkgs/salts/release/." "$sdk/"
          test -f "$sdk/lib/cmake/Salts/SaltsConfig.cmake"
          cat > "$sdk/salts-sdk-manifest.txt" <<EOF
          commit=${{ github.event.pull_request.head.sha || github.sha }}
          os=${{ runner.os }}
          arch=${{ runner.arch }}
          image=${{ steps.linux-env.outputs.image }}
          triplet=${{ steps.linux-env.outputs.triplet }}
          profile=release
          EOF

      - name: Save Linux installed Salts SDK cache
        uses: actions/cache/save@v4
        with:
          path: build/ci-salts-sdk
          key: salts-sdk-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.linux-env.outputs.image }}-${{ steps.linux-env.outputs.triplet }}-release-${{ github.event.pull_request.head.sha || github.sha }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
```

There is deliberately no restore prefix.

- [ ] **Step 3: Stage and attest the Windows SDK after install verification**

Add:

```yaml
      - name: Stage Windows installed Salts SDK cache
        shell: pwsh
        run: |
          $ErrorActionPreference = "Stop"
          $sdk = Join-Path $env:GITHUB_WORKSPACE "build\ci-salts-sdk"
          $source = Join-Path $env:GITHUB_WORKSPACE "external\pkgs\salts\release"
          if (Test-Path -LiteralPath $sdk) { Remove-Item -Recurse -Force $sdk }
          New-Item -ItemType Directory -Force -Path $sdk | Out-Null
          Copy-Item -Path (Join-Path $source '*') -Destination $sdk -Recurse -Force
          $config = Join-Path $sdk "lib\cmake\Salts\SaltsConfig.cmake"
          if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
            throw "installed SaltsConfig.cmake missing: $config"
          }
          @(
            "commit=${{ github.event.pull_request.head.sha || github.sha }}"
            "os=${{ runner.os }}"
            "arch=${{ runner.arch }}"
            "image=${{ steps.windows-env.outputs.image }}"
            "triplet=${{ steps.windows-env.outputs.triplet }}"
            "profile=release"
          ) | Set-Content -LiteralPath (Join-Path $sdk "salts-sdk-manifest.txt")

      - name: Save Windows installed Salts SDK cache
        uses: actions/cache/save@v4
        with:
          path: build/ci-salts-sdk
          key: salts-sdk-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-release-${{ github.event.pull_request.head.sha || github.sha }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
```

Do not restore this SDK into Linux/Windows source-build provider jobs.

- [ ] **Step 4: Validate and commit**

```bash
rg -n "salts-sdk-v1|salts-sdk-manifest|cache/save" .github/workflows/cmeta.yml
git diff --check
git add .github/workflows/cmeta.yml
git commit -m "ci: publish exact-head Salts SDK cache"
```

- [ ] **Step 5: Verify provider jobs remain source-built and GREEN**

Linux and Windows must still run configure/build/tests/install before SDK save. macOS/Android remain unchanged except C1 vcpkg caching.

---

### Task C4: Prove an independent library can consume the cached exact-head SDK

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `cstl/tests/allocator_installed/**`

**Interfaces:**
- Consumes: C3 Linux SDK cache.
- Produces: `sdk-cache-consumer-linux`, which restores no provider build tree and validates cache provenance before compiling/running installed C/C++ consumers.

- [ ] **Step 1: Add an exact-head Linux cache consumer job**

```yaml
  sdk-cache-consumer-linux:
    name: Linux cached SDK consumer
    needs: linux
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          ref: ${{ github.event.pull_request.head.sha || github.sha }}
          persist-credentials: false

      - name: Setup cached SDK consumer tools
        id: sdk-env
        shell: bash
        run: |
          set -euo pipefail
          sudo apt-get update
          sudo apt-get install -y ninja-build
          echo "image=${ImageOS:-linux}-${ImageVersion:-unknown}" >> "$GITHUB_OUTPUT"
          echo "triplet=x64-linux" >> "$GITHUB_OUTPUT"

      - name: Restore exact-head Salts SDK
        id: salts-sdk-cache
        uses: actions/cache/restore@v4
        with:
          path: build/ci-salts-sdk
          key: salts-sdk-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.sdk-env.outputs.image }}-${{ steps.sdk-env.outputs.triplet }}-release-${{ github.event.pull_request.head.sha || github.sha }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          fail-on-cache-miss: true
```

Do not add `restore-keys`.

- [ ] **Step 2: Verify SDK provenance before use**

```yaml
      - name: Verify cached SDK provenance
        shell: bash
        run: |
          set -euxo pipefail
          manifest="$GITHUB_WORKSPACE/build/ci-salts-sdk/salts-sdk-manifest.txt"
          test -f "$manifest"
          grep -Fx "commit=${{ github.event.pull_request.head.sha || github.sha }}" "$manifest"
          grep -Fx "os=${{ runner.os }}" "$manifest"
          grep -Fx "arch=${{ runner.arch }}" "$manifest"
          grep -Fx "image=${{ steps.sdk-env.outputs.image }}" "$manifest"
          grep -Fx "triplet=${{ steps.sdk-env.outputs.triplet }}" "$manifest"
          grep -Fx "profile=release" "$manifest"
          test -f build/ci-salts-sdk/lib/cmake/Salts/SaltsConfig.cmake
```

- [ ] **Step 3: Build and run independent installed consumers**

```yaml
      - name: Test CSTL installed consumers from cached SDK
        shell: bash
        run: |
          set -euxo pipefail
          cmake -S cstl/tests/allocator_installed -B build/sdk-cache-consumer -G Ninja \
            -DSalts_DIR="$GITHUB_WORKSPACE/build/ci-salts-sdk/lib/cmake/Salts"
          cmake --build build/sdk-cache-consumer -j2
          ctest --test-dir build/sdk-cache-consumer --no-tests=error --output-on-failure
```

This job must not configure or build the root Salts project.

- [ ] **Step 4: Validate and commit**

```bash
rg -n "sdk-cache-consumer-linux|fail-on-cache-miss|Verify cached SDK provenance" .github/workflows/cmeta.yml
git diff --check
git add .github/workflows/cmeta.yml
git commit -m "ci: verify cached Salts SDK consumers"
```

- [ ] **Step 5: Verify producer→consumer GREEN**

Require Linux provider to save the exact-head SDK and `Linux cached SDK consumer` to restore that exact key, validate all manifest fields, then pass installed C/C++ consumer tests.

---

### Task C5: Demonstrate reusable vcpkg hits and audit cache safety

**Files:**
- Inspect: `.github/workflows/cmeta.yml`
- Inspect: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Consumes: C1–C4.
- Produces: repeat-run cache-hit evidence and final provenance audit.

- [ ] **Step 1: Re-run exact-head conformance after first cache-populating GREEN**

Use GitHub Actions re-run on the unchanged PR head. Require Linux and Windows vcpkg cache log lines to show a valid hit. macOS/Android hits are also expected if image/toolchain keys did not change; explain any miss by the printed ABI key rather than hiding it.

- [ ] **Step 2: Verify cross-workflow reuse where ABI tuples match**

Run the exact-head NativeIO/CNet workflow after C2. Linux/macOS jobs whose runner image/arch/triplet/dependency fingerprint matches a populated `cmeta.yml` cache should restore the same `vcpkg-binary-v2` namespace. Windows may legitimately use a separate cache because the benchmark uses `windows-2022` while conformance currently uses `windows-latest`.

- [ ] **Step 3: Audit SDK restore isolation**

```bash
rg -n "salts-sdk-v1|restore-keys|fail-on-cache-miss" .github/workflows/cmeta.yml
```

Require the SDK restore key to include `${{ github.event.pull_request.head.sha || github.sha }}` and require no SDK `restore-keys` block.

- [ ] **Step 4: Audit benchmark source provenance**

```bash
rg -n "salts-sdk-v1|cnet_io_benchmark|BENCHMARK_COMMIT" .github/workflows/native-io-release-benchmarks.yml
```

Expected: no `salts-sdk-v1`; benchmark targets are still built; `BENCHMARK_COMMIT` still resolves to the tested head.

- [ ] **Step 5: Record evidence in PR #282**

Record first-run cache population, repeat-run vcpkg hit, exact-head SDK consumer restore/pass, and four-platform benchmark GREEN. State explicitly that cache reuse changed build/setup time only and did not replace source builds or benchmark executables.

After C5, resume the parent cleanup plan at Task 2 (CSTL allocator migration), Task 3 (XML migration), performance-trigger narrowing, and legacy-workflow deletion.
