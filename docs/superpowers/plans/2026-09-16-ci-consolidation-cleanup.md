# CI Consolidation and Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce the permanent GitHub Actions surface from seven workflows to four canonical workflows while preserving shared-owner, CSTL allocator, XML sanitizer, formal/generated-contract, and NativeIO/CNet performance evidence.

**Architecture:** `cmeta.yml` becomes the durable native conformance owner for Linux/macOS/Windows/Android plus the three migrated regression families. `cmeta-cflow-calculus.yml` remains the formal/generated-contract gate, `ci.yml` remains the lightweight notation gate, and `native-io-release-benchmarks.yml` remains the sole performance workflow with narrower automatic triggers. The three completed feature workflows are deleted only after replacement coverage is present and green.

**Tech Stack:** GitHub Actions YAML, CMake/CMake presets, CTest, Ninja, GCC/Clang/MSVC, ASan/UBSan, Lean, existing Salts test projects.

**Spec:** `docs/superpowers/specs/2026-09-16-ci-consolidation-cleanup-design.md`

## Global Constraints

- Keep exactly four permanent workflows: `ci.yml`, `cmeta.yml`, `cmeta-cflow-calculus.yml`, and `native-io-release-benchmarks.yml`.
- Do not change production CNet, NativeIO, CSTL, XML, CMeta, or CFlow behavior.
- Do not delete a feature workflow before its durable regression coverage is demonstrated in `cmeta.yml`.
- Keep Linux/macOS/Windows native conformance and Android arm64 package verification.
- Canonical PR jobs modified by this cleanup must checkout `${{ github.event.pull_request.head.sha || github.sha }}` explicitly so final evidence is exact-head rather than the synthetic PR merge ref.
- Migrated Linux/Windows regression jobs must leave tracked source unchanged.
- Keep Lean/generated-contract verification independent from native conformance.
- Keep epoll, io_uring, kqueue, and IOCP performance evidence and the IOCP run-quality gate.
- A noise-limited performance run remains evidence-only and must not fail functional CI.
- Do not add retry-until-green logic, host affinity/priority changes, or new benchmark thresholds.
- Execution begins only after the design/plan PR is approved and merged; create implementation branch `ci/consolidate-canonical-workflows` from the resulting `master`.

---

## File Structure

**Modify**

- `.github/workflows/cmeta.yml` — canonical native conformance triggers, exact-head checkout, source-cleanliness checks, and migrated regression steps.
- `.github/workflows/native-io-release-benchmarks.yml` — performance-only automatic trigger scope.

**Delete after migration is green**

- `.github/workflows/cmeta-shared-owner.yml`
- `.github/workflows/cstl-allocator.yml`
- `.github/workflows/xml-sax.yml`

**Consume without changing unless a discovered incompatibility forces a design stop**

- `tests/cmeta_shared_owner/**`
- `cstl/tests/allocator_installed/**`
- `cstl/tests/cstl_vec_alloc_test.c`
- `cstl/tests/cstl_vec_alloc_cpp_test.cpp`
- `parser/xml_parser/test/**`

If a consumed test project cannot run under canonical CI without weakening its existing contract, stop deletion of that workflow and revise the spec instead of changing production code to accommodate cleanup.

---

### Task 1: Establish exact-head canonical conformance and migrate CMeta shared-owner regression

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `tests/cmeta_shared_owner/**`

**Interfaces:**
- Consumes: production `Salts::CMeta`, canonical installed package at `external/pkgs/salts/release/lib/cmake/Salts`, existing `CMETA_OWNER_SANITIZERS` option.
- Produces: exact-head Linux/macOS/Windows/Android conformance checkout; Linux sanitized shared-owner regression; Linux/Windows installed-package shared-owner regression; tracked-source cleanliness proof for Linux and Windows.

- [ ] **Step 1: Record the pre-change coverage RED**

```bash
rg -n "cmeta_shared_owner|cmeta-owner|tests/cmeta_shared_owner" .github/workflows/cmeta.yml
```

Expected: no matches.

- [ ] **Step 2: Make all four canonical jobs checkout the PR head explicitly**

For each checkout step in the `linux`, `macos`, `android`, and `windows` jobs, preserve its current action version but add:

```yaml
        with:
          ref: ${{ github.event.pull_request.head.sha || github.sha }}
          persist-credentials: false
```

Do not change the platform matrix or runner versions as part of this task.

- [ ] **Step 3: Add shared-owner paths to canonical triggers**

Add to both `pull_request.paths` and `push.paths`:

```yaml
      - "tests/cmeta_shared_owner/**"
```

- [ ] **Step 4: Add Linux source-mode sanitized regression**

After the canonical Linux release install/package verification, add:

```yaml
      - name: Verify CMeta shared ownership with sanitizers
        shell: bash
        run: |
          set -euxo pipefail
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMETA_OWNER_SANITIZERS=ON
          cmake --build build/cmeta-owner -j2
          ctest --test-dir build/cmeta-owner --no-tests=error --output-on-failure
```

Expected: `cmeta_shared_owner_test` runs; Linux also runs `cmeta_static_exports_test` when `Salts::CMeta` is static.

- [ ] **Step 5: Add Linux installed-package regression**

```yaml
      - name: Verify installed CMeta shared ownership
        shell: bash
        run: |
          set -euxo pipefail
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner-installed -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMETA_PACKAGE_DIR="$GITHUB_WORKSPACE/external/pkgs/salts/release/lib/cmake/Salts"
          cmake --build build/cmeta-owner-installed -j2
          ctest --test-dir build/cmeta-owner-installed --no-tests=error --output-on-failure
```

This must use the canonical installed SDK, not a test-owned CMeta copy.

- [ ] **Step 6: Add Windows source and installed-package regressions**

After the canonical Windows release install:

```yaml
      - name: Verify CMeta shared ownership
        shell: cmd
        run: |
          call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
          if errorlevel 1 exit /b 1
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner -G Ninja -DCMAKE_BUILD_TYPE=Release
          if errorlevel 1 exit /b 1
          cmake --build build/cmeta-owner -j2
          if errorlevel 1 exit /b 1
          ctest --test-dir build/cmeta-owner --no-tests=error --output-on-failure
          if errorlevel 1 exit /b 1
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner-installed -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMETA_PACKAGE_DIR="%GITHUB_WORKSPACE%\external\pkgs\salts\release\lib\cmake\Salts"
          if errorlevel 1 exit /b 1
          cmake --build build/cmeta-owner-installed -j2
          if errorlevel 1 exit /b 1
          ctest --test-dir build/cmeta-owner-installed --no-tests=error --output-on-failure
```

- [ ] **Step 7: Add tracked-source cleanliness checks**

At the end of the Linux job:

```yaml
      - name: Check exact head and unchanged tracked source
        if: always()
        shell: bash
        run: |
          set -euo pipefail
          test "$(git rev-parse HEAD)" = "${{ github.event.pull_request.head.sha || github.sha }}"
          test -z "$(git status --porcelain --untracked-files=no)"
```

At the end of the Windows job:

```yaml
      - name: Check exact head and unchanged tracked source
        if: always()
        shell: pwsh
        run: |
          $ErrorActionPreference = "Stop"
          $expected = "${{ github.event.pull_request.head.sha || github.sha }}"
          $actual = (git rev-parse HEAD).Trim()
          if ($actual -ne $expected) { throw "expected head $expected, got $actual" }
          $dirty = @(git status --porcelain --untracked-files=no)
          if ($dirty.Count -ne 0) { throw "tracked source changed: $($dirty -join '; ')" }
```

- [ ] **Step 8: Validate and commit**

```bash
git diff --check
rg -n "tests/cmeta_shared_owner|Verify CMeta shared ownership|Check exact head" .github/workflows/cmeta.yml
git add .github/workflows/cmeta.yml
git commit -m "ci(cmeta): migrate shared-owner regression"
```

- [ ] **Step 9: Verify remote GREEN before proceeding**

Require exact-head `CMeta conformance` evidence:

- Linux, macOS, Windows, Android checkouts resolve to the PR head SHA.
- Linux runs `cmeta_shared_owner_test` and `cmeta_static_exports_test` when applicable.
- Linux installed shared-owner test passes.
- Windows source and installed shared-owner tests pass.
- Linux/Windows tracked-source checks pass.
- Existing macOS and Android jobs remain green.

If these fail because the test project cannot be embedded without contract loss, stop and revise the design; do not delete `cmeta-shared-owner.yml`.

---

### Task 2: Migrate CSTL allocator sanitizer and installed-consumer coverage

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `cstl/tests/allocator_installed/CMakeLists.txt`
- Consume: `cstl/tests/cstl_vec_alloc_test.c`
- Consume: `cstl/tests/cstl_vec_alloc_cpp_test.cpp`

**Interfaces:**
- Consumes: `linux-dev-user` ASan preset and canonical release SDK installation.
- Produces: allocator C/C++ ASan+UBSan regression and independent installed SDK consumers without another vcpkg checkout.

- [ ] **Step 1: Record the pre-change coverage RED**

```bash
rg -n "cstl_vec_alloc_test|allocator_installed|allocator-consumer" .github/workflows/cmeta.yml
```

Expected: no allocator-specific canonical step.

- [ ] **Step 2: Add allocator sanitizer regression**

```yaml
      - name: Verify CSTL allocator with sanitizers
        shell: bash
        run: |
          set -euxo pipefail
          cmake --preset linux-dev-user -DBUILD_BENCHMARKS=OFF \
            -DENABLE_SANITIZER_ADDRESS=ON \
            -DENABLE_SANITIZER_UNDEFINED=ON
          cmake --build --preset linux-dev-user --target \
            cstl_vec_alloc_test \
            cstl_vec_alloc_cpp_test \
            cstl_sequence_test \
            cstl_ownership_test \
            cstl_header_test \
            cstl_header_cpp_test
          ctest --preset linux-dev-user \
            -R "^(cstl_vec_alloc_test|cstl_vec_alloc_cpp_test|cstl_sequence_test|cstl_ownership_test|cstl_header_test|cstl_header_cpp_test)$" \
            --output-on-failure
```

Do not copy the standalone vcpkg bootstrap from `cstl-allocator.yml`.

- [ ] **Step 3: Add independent installed C/C++ consumers**

After canonical Linux `Install release profile`:

```yaml
      - name: Verify CSTL allocator installed consumers
        shell: bash
        run: |
          set -euxo pipefail
          test -f "$GITHUB_WORKSPACE/external/pkgs/salts/release/include/cstl/allocator.h"
          test -f "$GITHUB_WORKSPACE/external/pkgs/salts/release/include/cstl/vec_alloc.h"
          cmake -S cstl/tests/allocator_installed -B build/allocator-consumer -G Ninja \
            -DSalts_DIR="$GITHUB_WORKSPACE/external/pkgs/salts/release/lib/cmake/Salts"
          cmake --build build/allocator-consumer -j2
          ctest --test-dir build/allocator-consumer --no-tests=error --output-on-failure
```

- [ ] **Step 4: Validate and commit**

```bash
git diff --check
rg -n "Verify CSTL allocator|cstl_vec_alloc_test|allocator-consumer" .github/workflows/cmeta.yml
git add .github/workflows/cmeta.yml
git commit -m "ci(cstl): migrate allocator regression coverage"
```

- [ ] **Step 5: Verify remote GREEN**

Require exact-head Linux logs showing all six sanitizer tests pass and the installed `allocator_c`/`allocator_cpp` consumers build and pass. Linux full suite, macOS, Windows, Android, and tracked-source checks must remain green.

Do not proceed to workflow deletion if the installed consumer is absent from canonical evidence.

---

### Task 3: Migrate XML DOM/SAX sanitizer coverage and parser triggers

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `parser/xml_parser/test/**`

**Interfaces:**
- Consumes: `xml_parser_test`, `xml_sax_parser_test`, canonical `linux-dev-user` sanitizer build.
- Produces: parser changes activate `cmeta.yml`; DOM/SAX regressions run under ASan+UBSan on the exact PR head.

- [ ] **Step 1: Record trigger and sanitizer RED**

```bash
rg -n '"parser/\*\*"|xml_parser_test|xml_sax_parser_test' .github/workflows/cmeta.yml
```

Expected: no parser trigger and no dedicated XML sanitizer step.

- [ ] **Step 2: Add parser path filters**

Add to both `pull_request.paths` and `push.paths`:

```yaml
      - "parser/**"
```

- [ ] **Step 3: Add XML sanitizer regression**

```yaml
      - name: Verify XML DOM and SAX with sanitizers
        shell: bash
        run: |
          set -euxo pipefail
          cmake --preset linux-dev-user -DBUILD_BENCHMARKS=OFF \
            -DENABLE_SANITIZER_ADDRESS=ON \
            -DENABLE_SANITIZER_UNDEFINED=ON
          cmake --build --preset linux-dev-user --target \
            xml_parser_test \
            xml_sax_parser_test
          ctest --preset linux-dev-user \
            -R "^(xml_parser_test|xml_sax_parser_test)$" \
            --output-on-failure
```

- [ ] **Step 4: Validate and commit**

```bash
git diff --check
rg -n '"parser/\*\*"|Verify XML DOM and SAX|xml_sax_parser_test' .github/workflows/cmeta.yml
git add .github/workflows/cmeta.yml
git commit -m "ci(xml): migrate DOM and SAX sanitizer coverage"
```

- [ ] **Step 5: Verify remote GREEN**

Require exact-head Linux evidence that `xml_parser_test` and `xml_sax_parser_test` pass under sanitizers. Linux full suite, macOS, Windows, Android, and tracked-source checks must remain green.

At this checkpoint all durable coverage from the three legacy workflows must be demonstrated in `cmeta.yml`. Do not delete a legacy workflow before this checkpoint.

---

### Task 4: Narrow NativeIO/CNet performance workflow triggers

**Files:**
- Modify: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Consumes: existing four-platform matrix, benchmark contracts, Windows A/A control, IOCP run-quality gate.
- Produces: automatic performance runs only for performance-relevant runtime/build changes; manual dispatch unchanged.

- [ ] **Step 1: Record the over-broad trigger RED**

```bash
rg -n 'cmeta/\*\*|tinytest/\*\*|cmetaChanged|tinytestChanged' .github/workflows/native-io-release-benchmarks.yml
```

Expected: matches exist.

- [ ] **Step 2: Remove broad automatic path filters**

Remove only these from both `pull_request.paths` and `push.paths`:

```yaml
      - "cmeta/**"
      - "tinytest/**"
```

Keep:

```yaml
      - "native-io/**"
      - "coroutine/**"
      - "concurrency/**"
      - "platform/**"
      - "cnet/**"
      - "cmake/**"
      - "CMakeLists.txt"
      - "CMakePresets.json"
      - "vcpkg.json"
      - "vcpkg-configuration.json"
      - ".github/workflows/native-io-release-benchmarks.yml"
```

Keep `workflow_dispatch` unchanged.

- [ ] **Step 3: Remove matching internal scope branches**

Delete `cmetaChanged` and `tinytestChanged` calculations and reduce `$runAdapted` to:

```powershell
$runAdapted = $nativeChanged -or $cnetChanged -or
  $coroutineChanged -or $concurrencyChanged -or $platformChanged -or
  $buildChanged -or $dependencyChanged -or $workflowChanged
```

Keep the scope step because manual dispatch must force `$runAdapted = $true`.

- [ ] **Step 4: Validate and commit**

```bash
git diff --check
! rg -n 'cmeta/\*\*|tinytest/\*\*|cmetaChanged|tinytestChanged' .github/workflows/native-io-release-benchmarks.yml
rg -n 'workflow_dispatch|native-io/\*\*|cnet/\*\*|vcpkg-configuration.json' .github/workflows/native-io-release-benchmarks.yml
git add .github/workflows/native-io-release-benchmarks.yml
git commit -m "ci(perf): narrow NativeIO CNet benchmark triggers"
```

- [ ] **Step 5: Verify exact-head four-platform benchmark GREEN**

The workflow file change must trigger the performance workflow. Require epoll, io_uring, kqueue, and IOCP success; all artifacts upload; Windows artifact contains exactly one `RUN QUALITY:` line. A noise-limited label is still a successful harness run.

Do not make a new performance optimization claim from this cleanup run.

---

### Task 5: Delete the three completed feature workflows

**Files:**
- Delete: `.github/workflows/cmeta-shared-owner.yml`
- Delete: `.github/workflows/cstl-allocator.yml`
- Delete: `.github/workflows/xml-sax.yml`

**Interfaces:**
- Consumes: GREEN exact-head canonical evidence from Tasks 1–3.
- Produces: exactly four permanent workflows with no stale feature-branch triggers.

- [ ] **Step 1: Enforce the deletion precondition**

Verify the latest exact-head `CMeta conformance` run is all green and logs visibly contain:

```text
cmeta_shared_owner_test
cmeta_static_exports_test (Linux when applicable)
cstl_vec_alloc_test
cstl_vec_alloc_cpp_test
xml_parser_test
xml_sax_parser_test
```

Also confirm installed shared-owner and allocator-consumer steps succeeded. If any item is absent, stop; do not delete the corresponding workflow.

- [ ] **Step 2: Delete legacy workflow files**

```bash
git rm \
  .github/workflows/cmeta-shared-owner.yml \
  .github/workflows/cstl-allocator.yml \
  .github/workflows/xml-sax.yml
```

- [ ] **Step 3: Verify the workflow inventory and stale references**

```bash
find .github/workflows -maxdepth 1 -type f -name '*.yml' -printf '%f\n' | sort
! rg -n 'fix/cmeta-252-shared-owner|feat/xml-native-sax|cmeta-shared-owner\.yml|cstl-allocator\.yml|xml-sax\.yml' .github/workflows
```

Expected inventory:

```text
ci.yml
cmeta-cflow-calculus.yml
cmeta.yml
native-io-release-benchmarks.yml
```

- [ ] **Step 4: Validate and commit**

```bash
git diff --check
git add .github/workflows
git commit -m "ci: remove completed feature workflows"
```

---

### Task 6: Final exact-head verification and cleanup audit

**Files:**
- Review: `.github/workflows/ci.yml`
- Review: `.github/workflows/cmeta.yml`
- Review: `.github/workflows/cmeta-cflow-calculus.yml`
- Review: `.github/workflows/native-io-release-benchmarks.yml`
- Review: implementation PR diff

**Interfaces:**
- Consumes: final branch from Tasks 1–5.
- Produces: merge-ready cleanup with exact-head evidence and no lost coverage.

- [ ] **Step 1: Require final-head canonical CI**

Do not cite earlier commit runs. The final head must show:

```text
CMeta conformance
  Linux release                  success
  macOS 15 release               success
  Windows release                success
  Android arm64 Release          success

NativeIO and CNet release benchmarks
  epoll / Ubuntu 24.04 GCC       success
  io_uring / Ubuntu 24.04 GCC    success
  kqueue / macOS 15 AppleClang   success
  IOCP / Windows 2022 MSVC       success
```

Confirm the checkout/logged SHA in canonical conformance is the PR head, not a synthetic merge commit.

- [ ] **Step 2: Verify migrated checks in final-head logs**

Confirm visible execution of:

```text
cmeta_shared_owner_test
cmeta_static_exports_test (Linux when applicable)
installed CMeta shared ownership
cstl_vec_alloc_test
cstl_vec_alloc_cpp_test
allocator installed C consumer
allocator installed C++ consumer
xml_parser_test
xml_sax_parser_test
tracked-source unchanged checks
```

- [ ] **Step 3: Verify performance artifact contract**

Download the final IOCP artifact and run:

```bash
rg -n '^RUN QUALITY: ' libuv-native-io-cnet-benchmark.md
```

Expected exactly one of:

```text
RUN QUALITY: qualified for performance decisions
RUN QUALITY: noise-limited; do not use for optimization decisions
```

Do not use this cleanup run for a new optimization claim.

- [ ] **Step 4: Audit final changed files and workflow inventory**

If the design/plan PR was merged first, expected implementation paths are exactly:

```text
.github/workflows/cmeta.yml
.github/workflows/native-io-release-benchmarks.yml
.github/workflows/cmeta-shared-owner.yml        (deleted)
.github/workflows/cstl-allocator.yml            (deleted)
.github/workflows/xml-sax.yml                    (deleted)
```

Run:

```bash
git diff --check
find .github/workflows -maxdepth 1 -type f -name '*.yml' -printf '%f\n' | sort
rg -n 'fix/cmeta-252-shared-owner|feat/xml-native-sax' .github/workflows || true
```

There must be no temporary workflow/script and no production-source change.

- [ ] **Step 5: Review final responsibility split**

Verify:

```text
ci.yml                         policy/notation only
cmeta.yml                      native conformance + durable regressions
cmeta-cflow-calculus.yml       Lean/generated contracts
native-io-release-benchmarks   performance evidence only
```

Do not broaden this cleanup into unrelated refactoring.

- [ ] **Step 6: Prepare merge-ready PR metadata**

Update the implementation PR body with:

- spec link;
- old workflow count `7`, new workflow count `4`;
- migrated regression evidence;
- final exact-head CI run IDs;
- no-production-source-change statement;
- performance trigger statement: only `cmeta/**` and `tinytest/**` automatic triggers were removed, while `workflow_dispatch` remains.

Request final review. Do not merge until final-head evidence is present.
