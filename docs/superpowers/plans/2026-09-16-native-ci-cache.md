# Native CI Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Accelerate every native GitHub Actions build with a shared ABI-scoped vcpkg binary cache and make exact-head Linux/Windows installed Salts SDKs reusable by independent consumer checks without weakening source or benchmark provenance.

**Architecture:** `cmeta.yml` and `native-io-release-benchmarks.yml` share one vcpkg binary-cache key convention so compatible native jobs can reuse dependency packages across workflows and Salts commits. `cmeta.yml` additionally publishes exact-SHA Linux/Windows Release SDK caches for consumer-only jobs; current-source conformance, sanitizer, and benchmark jobs always compile Salts from the checked-out head. A small Linux installed-consumer probe restores the SDK cache after the provider job and proves that cached Salts remains a usable `find_package(Salts CONFIG REQUIRED)` package.

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

Run against the implementation branch:

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES" .github/workflows/cmeta.yml
```

Expected: only the pre-existing Windows binary-cache implementation is present and it uses the old key/fingerprint.

- [ ] **Step 2: Add Linux ABI identity and binary-cache environment**

Give `Setup re2c and vcpkg` the id `linux-env`. After resolving `VCPKG_ROOT`, add:

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
```

Do not cache `vcpkg_installed` or any CMake build directory.

- [ ] **Step 3: Add macOS ABI identity and binary-cache environment**

Give `Setup re2c and vcpkg` the id `macos-env`. Preserve the existing `VCPKG_ROOT` resolution and add:

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
```

`default-osx-${RUNNER_ARCH}` intentionally represents the host-derived default vcpkg triplet used by the existing macOS preset without changing that preset.

- [ ] **Step 4: Strengthen the existing Windows key**

Keep `Setup Windows build environment` and its binary-cache directory. Add:

```powershell
"triplet=x64-windows" >> $env:GITHUB_OUTPUT
```

Replace the cache key with:

```yaml
          key: vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
          restore-keys: |
            vcpkg-binary-v2-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-
```

Delete the broader old restore prefix that stopped at OS/arch.

- [ ] **Step 5: Add Android ABI identity and binary-cache environment**

Give `Setup re2c and validate toolchains` the id `android-env`. After validating `ANDROID_NDK_HOME`, add:

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
```

- [ ] **Step 6: Make hit/miss visible without changing behavior**

After each cache step add a one-line platform-specific log step. Example:

```yaml
      - name: Report Linux vcpkg cache state
        run: echo "vcpkg cache hit=${{ steps.linux-vcpkg-cache.outputs.cache-hit }}"
```

Repeat for macOS, Windows, and Android using their cache-step ids. Do not branch test execution on `cache-hit`.

- [ ] **Step 7: Validate and commit**

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES|cache hit=" .github/workflows/cmeta.yml
git diff --check
git add .github/workflows/cmeta.yml
git commit -m "ci: share vcpkg binary caches across native conformance"
```

- [ ] **Step 8: Verify exact-head GREEN**

Require the `CMeta conformance` workflow to remain green on Linux/macOS/Windows/Android. A first run may report misses. Confirm each native job logs its vcpkg cache state and still executes its existing build/tests.

---

### Task C2: Reuse the same vcpkg binary-cache namespace in NativeIO/CNet benchmarks

**Files:**
- Modify: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Consumes: the same dependency fingerprint and ABI naming convention from Task C1.
- Produces: dependency cache reuse without changing benchmark executable provenance or benchmark protocol.

- [ ] **Step 1: Record the benchmark cache RED**

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES" .github/workflows/native-io-release-benchmarks.yml
```

Expected: only the older Windows-only cache exists.

- [ ] **Step 2: Add Linux cache setup and restore**

Extend `Setup Linux dependencies` with the same `build/vcpkg-binary-cache`, `VCPKG_BINARY_SOURCES`, image output, and `x64-linux` triplet used by Task C1. Give the step id `linux-env`, then add `actions/cache@v4` using exactly the Task C1 Linux key and restore prefix.

- [ ] **Step 3: Add macOS cache setup and restore**

Extend `Setup macOS dependencies` with the same repository-local binary cache and `default-osx-${RUNNER_ARCH}` identity from Task C1. Give the step id `macos-env`, then add the same macOS key convention.

- [ ] **Step 4: Upgrade Windows benchmark cache key**

Add `triplet=x64-windows` to `windows-env` outputs and replace its current key/fallback with the Task C1 Windows `vcpkg-binary-v2` convention. The `windows-2022` runner image remains naturally isolated from `cmeta.yml` when the image identity differs.

- [ ] **Step 5: Report cache state and preserve benchmark provenance**

Add cache-state log steps for each matrix family. Do not condition configure/build/contract tests/benchmark execution on a cache hit. Keep benchmark metadata using:

```yaml
BENCHMARK_COMMIT: ${{ github.event.pull_request.head.sha || github.sha }}
```

and keep compiling `cnet_io_benchmark` plus profile targets from the checked-out source.

- [ ] **Step 6: Validate and commit**

```bash
rg -n "vcpkg-binary-v2|VCPKG_BINARY_SOURCES|BENCHMARK_COMMIT" .github/workflows/native-io-release-benchmarks.yml
git diff --check
git add .github/workflows/native-io-release-benchmarks.yml
git commit -m "ci(perf): reuse native vcpkg binary cache"
```

- [ ] **Step 7: Verify four-platform benchmark GREEN**

Require epoll, io_uring, kqueue, and IOCP exact-head build/contracts/benchmark/artifact success. Confirm benchmark metadata still reports the PR head SHA. Cache state is build evidence only and must not alter the IOCP run-quality decision.

---

### Task C3: Publish exact-SHA Linux and Windows installed Salts SDK caches

**Files:**
- Modify: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: canonical Release installs already produced by Linux and Windows jobs at `external/pkgs/salts/release`.
- Produces: immutable cache namespace `salts-sdk-v1-<os>-<arch>-<image>-<triplet>-release-<exact-sha>-<dependency-hash>`.

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
```

- [ ] **Step 3: Save the Linux SDK with no cross-SHA fallback**

```yaml
      - name: Save Linux installed Salts SDK cache
        uses: actions/cache/save@v4
        with:
          path: build/ci-salts-sdk
          key: salts-sdk-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.linux-env.outputs.image }}-${{ steps.linux-env.outputs.triplet }}-release-${{ github.event.pull_request.head.sha || github.sha }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
```

There is deliberately no `restore-keys` field.

- [ ] **Step 4: Stage and save the Windows SDK**

After Windows `Verify installed package targets`, stage `external\pkgs\salts\release` into `build\ci-salts-sdk`, write the same six manifest fields with PowerShell, assert `lib\cmake\Salts\SaltsConfig.cmake` exists, and save it with:

```yaml
      - name: Save Windows installed Salts SDK cache
        uses: actions/cache/save@v4
        with:
          path: build/ci-salts-sdk
          key: salts-sdk-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-${{ steps.windows-env.outputs.triplet }}-release-${{ github.event.pull_request.head.sha || github.sha }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'vcpkg-ports/**') }}
```

Do not restore this SDK into the provider/source build jobs.

- [ ] **Step 5: Validate and commit**

```bash
rg -n "salts-sdk-v1|salts-sdk-manifest|cache/save" .github/workflows/cmeta.yml
git diff --check
git add .github/workflows/cmeta.yml
git commit -m "ci: publish exact-head Salts SDK cache"
```

- [ ] **Step 6: Verify provider jobs remain source-built and GREEN**

Linux and Windows must still run configure/build/tests/install before the SDK save steps. macOS and Android behavior remains unchanged except for Task C1 vcpkg caching.

---

### Task C4: Prove an independent library can consume the cached exact-head SDK

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `cstl/tests/allocator_installed/**`

**Interfaces:**
- Consumes: Task C3 Linux SDK cache.
- Produces: a cheap independent `sdk-cache-consumer-linux` job that restores no provider build tree and validates the manifest before compiling/running installed C and C++ consumers.

- [ ] **Step 1: Add an exact-head Linux cache consumer job**

Add a job after `linux`:

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

      - name: Identify cached SDK ABI
        id: sdk-env
        shell: bash
        run: |
          set -euo pipefail
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

Add:

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

- [ ] **Step 3: Build and run the independent installed consumers**

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

The job must not configure or build the root Salts project.

- [ ] **Step 4: Validate and commit**

```bash
rg -n "sdk-cache-consumer-linux|fail-on-cache-miss|Verify cached SDK provenance" .github/workflows/cmeta.yml
git diff --check
git add .github/workflows/cmeta.yml
git commit -m "ci: verify cached Salts SDK consumers"
```

- [ ] **Step 5: Verify producer→consumer GREEN**

Require the Linux provider job to save the exact-head SDK cache and `Linux cached SDK consumer` to restore that exact key, validate all manifest fields, then pass the installed C/C++ consumer tests.

---

### Task C5: Demonstrate reusable vcpkg hits and audit cache safety

**Files:**
- No new production files.
- Inspect: `.github/workflows/cmeta.yml`
- Inspect: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Consumes: C1–C4.
- Produces: evidence that dependency reuse works across repeated/native runs while SDK reuse remains exact-head only.

- [ ] **Step 1: Re-run exact-head native conformance after the first cache-populating GREEN**

Use GitHub Actions re-run on the unchanged PR head. Require at least Linux and Windows vcpkg cache log lines to report a valid cache hit. macOS/Android hits are expected when their first-run save completed and the ABI key is unchanged; a platform-specific miss must be explained by a changed image/toolchain key, not hidden.

- [ ] **Step 2: Verify cross-workflow vcpkg reuse where ABI tuples match**

Run the exact-head NativeIO/CNet benchmark workflow after C2. For Linux/macOS hosts whose runner image/arch/triplet/dependency fingerprint matches a populated `cmeta.yml` cache, require the log to show reuse of the same `vcpkg-binary-v2` namespace. Windows may legitimately use a distinct cache because benchmark uses `windows-2022` while conformance currently uses `windows-latest`.

- [ ] **Step 3: Audit that SDK cache cannot cross SHAs**

```bash
rg -n "salts-sdk-v1|restore-keys|fail-on-cache-miss" .github/workflows/cmeta.yml
```

Manually verify the SDK restore key contains `${{ github.event.pull_request.head.sha || github.sha }}` and the SDK restore step has no `restore-keys`.

- [ ] **Step 4: Audit that benchmark still builds current sources**

Confirm `native-io-release-benchmarks.yml` still configures/builds the root tree and still builds `cnet_io_benchmark`, NativeIO tests, and CNet/profile tests before executing the benchmark. There must be no `salts-sdk-v1` reference in the performance workflow.

- [ ] **Step 5: Record evidence in PR #282**

Update the PR body/comment with:

- first-run miss/population evidence;
- repeat-run vcpkg hit evidence;
- cached SDK consumer exact-head restore/pass evidence;
- four-platform benchmark exact-head GREEN after cache integration;
- explicit statement that caches changed build/setup time only, not test or benchmark provenance.

After this task, resume the parent cleanup plan at Task 2 (CSTL allocator migration), then Task 3 (XML migration), trigger narrowing, and legacy-workflow deletion.
