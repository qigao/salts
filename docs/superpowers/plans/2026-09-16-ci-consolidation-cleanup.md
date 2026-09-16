# CI Consolidation and Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce the permanent GitHub Actions surface from seven workflows to four canonical workflows while preserving shared-owner, CSTL allocator, XML sanitizer, formal/generated-contract, and NativeIO/CNet performance evidence.

**Architecture:** `cmeta.yml` becomes the durable native conformance owner for Linux/macOS/Windows/Android plus the three migrated regression families. `cmeta-cflow-calculus.yml` remains the formal/generated-contract gate, `ci.yml` remains the lightweight notation gate, and `native-io-release-benchmarks.yml` remains the sole performance workflow with narrower automatic triggers. The three completed feature workflows are deleted only after the replacement checks are present and green.

**Tech Stack:** GitHub Actions YAML, CMake/CMake presets, CTest, Ninja, GCC/Clang/MSVC, ASan/UBSan, Lean, existing Salts test projects.

**Spec:** `docs/superpowers/specs/2026-09-16-ci-consolidation-cleanup-design.md`

## Global Constraints

- Keep exactly four permanent workflows: `ci.yml`, `cmeta.yml`, `cmeta-cflow-calculus.yml`, and `native-io-release-benchmarks.yml`.
- Do not change production CNet, NativeIO, CSTL, XML, CMeta, or CFlow behavior.
- Do not delete a feature workflow before its durable regression coverage is demonstrated in `cmeta.yml`.
- Keep Linux/macOS/Windows native conformance and Android arm64 package verification.
- Keep Lean/generated-contract verification independent from native conformance.
- Keep epoll, io_uring, kqueue, and IOCP performance evidence and the IOCP run-quality gate.
- A noise-limited performance run remains evidence-only and must not fail functional CI.
- Do not add retry-until-green logic, host affinity/priority changes, or new benchmark thresholds.
- Execution begins only after the design/plan PR is approved and merged; create implementation branch `ci/consolidate-canonical-workflows` from the resulting `master`.

---

## File Structure

**Modify**

- `.github/workflows/cmeta.yml` — canonical native conformance triggers and migrated regression steps.
- `.github/workflows/native-io-release-benchmarks.yml` — performance-only automatic trigger scope.

**Delete after migration is green**

- `.github/workflows/cmeta-shared-owner.yml`
- `.github/workflows/cstl-allocator.yml`
- `.github/workflows/xml-sax.yml`

**Consume without changing unless a discovered incompatibility forces a design stop**

- `tests/cmeta_shared_owner/CMakeLists.txt`
- `tests/cmeta_shared_owner/owner_test.c`
- `tests/cmeta_shared_owner/owner_peer.c`
- `tests/cmeta_shared_owner/check_exports.cmake`
- `cstl/tests/allocator_installed/CMakeLists.txt`
- `cstl/tests/cstl_vec_alloc_test.c`
- `cstl/tests/cstl_vec_alloc_cpp_test.cpp`
- `parser/xml_parser/test/CMakeLists.txt`
- `parser/xml_parser/test/xml_parser_test.c`
- `parser/xml_parser/test/xml_sax_parser_test.c`

If any consumed test project cannot run under canonical CI without weakening its existing contract, stop that deletion and revise the spec instead of editing production code to accommodate CI cleanup.

---

### Task 1: Migrate CMeta shared-owner regression into canonical conformance

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `tests/cmeta_shared_owner/**`

**Interfaces:**
- Consumes: production `Salts::CMeta`, canonical installed package at `external/pkgs/salts/release/lib/cmake/Salts`, existing `CMETA_OWNER_SANITIZERS` option.
- Produces: Linux sanitized shared-owner regression, Linux installed-package shared-owner regression, Windows shared-owner regression, Windows installed-package shared-owner regression.

- [ ] **Step 1: Record the pre-change coverage RED**

Run from the implementation branch before editing:

```bash
rg -n "cmeta_shared_owner|cmeta-owner|tests/cmeta_shared_owner" .github/workflows/cmeta.yml
```

Expected: no matches. This is the coverage RED: the canonical workflow does not yet own the regression.

- [ ] **Step 2: Add shared-owner paths to canonical triggers**

Add this path to both `pull_request.paths` and `push.paths` in `.github/workflows/cmeta.yml`:

```yaml
      - "tests/cmeta_shared_owner/**"
```

Keep `.github/workflows/cmeta.yml` itself in both trigger lists.

- [ ] **Step 3: Add Linux source-mode sanitized regression**

After the canonical Linux release install/package verification, add a dedicated step:

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

Expected tests include `cmeta_shared_owner_test`; on Linux the project also registers `cmeta_static_exports_test` when `Salts::CMeta` is static.

- [ ] **Step 4: Add Linux installed-package regression**

After `Install release profile`, configure the same test project against the canonical installed SDK:

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

Do not point this at a test-owned copy of CMeta.

- [ ] **Step 5: Add Windows source and installed-package regressions**

After the canonical Windows release install, add one `cmd` step that enters the existing Visual Studio environment and runs both modes:

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

- [ ] **Step 6: Validate the workflow diff locally**

Run:

```bash
git diff --check
rg -n "tests/cmeta_shared_owner|Verify CMeta shared ownership|Verify installed CMeta shared ownership" .github/workflows/cmeta.yml
```

Expected: trigger path plus Linux/Windows regression steps are present; no stale feature-branch trigger is added.

- [ ] **Step 7: Commit the migration**

```bash
git add .github/workflows/cmeta.yml
git commit -m "ci(cmeta): migrate shared-owner regression"
```

- [ ] **Step 8: Verify remote GREEN before proceeding**

Push the branch and inspect the exact-head `CMeta conformance` run. Required evidence:

- Linux release job succeeds and logs `cmeta_shared_owner_test` plus `cmeta_static_exports_test` when applicable.
- Linux installed-package shared-owner step succeeds.
- Windows release job succeeds and runs the source and installed shared-owner tests.
- macOS and Android existing jobs remain green.

If any of these fail because the test project cannot be embedded without contract loss, stop and revise the design; do not delete `cmeta-shared-owner.yml`.

---

### Task 2: Migrate CSTL allocator sanitizer and installed-consumer coverage

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `cstl/tests/allocator_installed/CMakeLists.txt`
- Consume: `cstl/tests/cstl_vec_alloc_test.c`
- Consume: `cstl/tests/cstl_vec_alloc_cpp_test.cpp`

**Interfaces:**
- Consumes: existing `linux-dev-user` ASan preset and canonical release SDK installation.
- Produces: allocator C/C++ sanitizer regression and independent installed SDK consumers without a second standalone workflow/vcpkg checkout.

- [ ] **Step 1: Record the pre-change coverage RED**

```bash
rg -n "cstl_vec_alloc_test|allocator_installed|allocator-consumer" .github/workflows/cmeta.yml
```

Expected: no allocator-specific canonical step.

- [ ] **Step 2: Add allocator sanitizer regression to the Linux job**

Add a dedicated step before or adjacent to the existing CFlow sanitizer step:

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

Do not create a second vcpkg checkout; reuse the canonical environment.

- [ ] **Step 3: Add independent installed C/C++ consumers**

After the canonical Linux `Install release profile` step, add:

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

This must use the canonical installed SDK, not a second full SDK build.

- [ ] **Step 4: Validate the diff**

```bash
git diff --check
rg -n "Verify CSTL allocator|cstl_vec_alloc_test|allocator-consumer" .github/workflows/cmeta.yml
```

Expected: one sanitizer step and one installed-consumer step; no standalone vcpkg bootstrap logic copied from `cstl-allocator.yml`.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/cmeta.yml
git commit -m "ci(cstl): migrate allocator regression coverage"
```

- [ ] **Step 6: Verify remote GREEN**

On the new exact head, require the Linux conformance logs to show:

- all six allocator/ownership/header sanitizer tests pass;
- installed `allocator_c` and `allocator_cpp` consumers build and pass;
- Linux full suite remains green;
- macOS, Windows, Android jobs remain green.

Do not proceed to workflow deletion if the installed consumer is missing from canonical evidence.

---

### Task 3: Migrate XML DOM/SAX sanitizer coverage and parser triggers

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Consume: `parser/xml_parser/test/**`

**Interfaces:**
- Consumes: existing `xml_parser_test`, `xml_sax_parser_test`, canonical `linux-dev-user` sanitizer build.
- Produces: parser changes automatically invoke `cmeta.yml`, and DOM/SAX regressions run with ASan/UBSan.

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

Reuse the same canonical debug/sanitizer preset rather than a separate feature workflow:

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

- [ ] **Step 4: Validate the diff**

```bash
git diff --check
rg -n '"parser/\*\*"|Verify XML DOM and SAX|xml_sax_parser_test' .github/workflows/cmeta.yml
```

Expected: parser trigger appears in both event blocks and XML sanitizer step is present once.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/cmeta.yml
git commit -m "ci(xml): migrate DOM and SAX sanitizer coverage"
```

- [ ] **Step 6: Verify remote GREEN**

Require exact-head Linux conformance evidence that `xml_parser_test` and `xml_sax_parser_test` both pass under the sanitizer build. Existing Linux full suite, macOS, Windows, and Android jobs must also remain green.

At this checkpoint all durable coverage from the three legacy workflows must be present and demonstrated in `cmeta.yml`. Do not delete any legacy workflow before this checkpoint is satisfied.

---

### Task 4: Narrow NativeIO/CNet performance workflow triggers

**Files:**
- Modify: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Consumes: existing four-platform matrix, benchmark contracts, Windows A/A control, IOCP run-quality gate.
- Produces: performance workflow automatically runs only for performance-relevant runtime/build changes; manual dispatch remains unchanged.

- [ ] **Step 1: Record the over-broad trigger RED**

```bash
rg -n 'cmeta/\*\*|tinytest/\*\*|cmetaChanged|tinytestChanged' .github/workflows/native-io-release-benchmarks.yml
```

Expected: matches exist in both top-level path filters and the `Select affected benchmark layers` script.

- [ ] **Step 2: Remove broad automatic path filters**

From both `pull_request.paths` and `push.paths`, remove only:

```yaml
      - "cmeta/**"
      - "tinytest/**"
```

Keep these automatic trigger surfaces:

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

- [ ] **Step 3: Remove the matching internal scope branches**

Delete the `cmetaChanged` and `tinytestChanged` calculations from `Select affected benchmark layers` and remove them from `$runAdapted`:

```powershell
$runAdapted = $nativeChanged -or $cnetChanged -or
  $coroutineChanged -or $concurrencyChanged -or $platformChanged -or
  $buildChanged -or $dependencyChanged -or $workflowChanged
```

Do not remove the scope step itself; manual dispatch must continue to force `$runAdapted = $true`.

- [ ] **Step 4: Validate the trigger contract**

```bash
git diff --check
! rg -n 'cmeta/\*\*|tinytest/\*\*|cmetaChanged|tinytestChanged' .github/workflows/native-io-release-benchmarks.yml
rg -n 'workflow_dispatch|native-io/\*\*|cnet/\*\*|vcpkg-configuration.json' .github/workflows/native-io-release-benchmarks.yml
```

Expected: no broad CMeta/TinyTest automatic trigger remains; manual dispatch and all performance-relevant paths remain.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/native-io-release-benchmarks.yml
git commit -m "ci(perf): narrow NativeIO CNet benchmark triggers"
```

- [ ] **Step 6: Verify exact-head four-platform benchmark GREEN**

Because the workflow file itself changed, the PR must run the performance workflow. Require:

- epoll / Ubuntu success;
- io_uring / Ubuntu success;
- kqueue / macOS success;
- IOCP / Windows success;
- all benchmark artifacts upload successfully;
- Windows artifact still contains exactly one `RUN QUALITY:` decision line;
- a `noise-limited` label, if produced, does not fail the job.

No performance conclusion is required from this cleanup run; this is harness/regression validation only.

---

### Task 5: Delete the three completed feature workflows

**Files:**
- Delete: `.github/workflows/cmeta-shared-owner.yml`
- Delete: `.github/workflows/cstl-allocator.yml`
- Delete: `.github/workflows/xml-sax.yml`

**Interfaces:**
- Consumes: GREEN canonical evidence from Tasks 1–3.
- Produces: exactly four permanent workflows with no stale feature-branch triggers.

- [ ] **Step 1: Enforce the deletion precondition**

Before deleting anything, record the exact-head `CMeta conformance` run from Task 3 and verify its Linux, macOS, Windows, and Android jobs are all `success`. Confirm logs contain:

```text
cmeta_shared_owner_test
cstl_vec_alloc_test
cstl_vec_alloc_cpp_test
xml_parser_test
xml_sax_parser_test
```

Also confirm the installed shared-owner and allocator-consumer steps succeeded.

If any item is absent, stop. Do not delete the corresponding legacy workflow.

- [ ] **Step 2: Delete the legacy workflow files**

```bash
git rm \
  .github/workflows/cmeta-shared-owner.yml \
  .github/workflows/cstl-allocator.yml \
  .github/workflows/xml-sax.yml
```

- [ ] **Step 3: Verify the permanent workflow inventory**

Run:

```bash
find .github/workflows -maxdepth 1 -type f -name '*.yml' -printf '%f\n' | sort
```

Expected exactly:

```text
ci.yml
cmeta-cflow-calculus.yml
cmeta.yml
native-io-release-benchmarks.yml
```

- [ ] **Step 4: Scan for stale feature-workflow references**

```bash
! rg -n 'fix/cmeta-252-shared-owner|feat/xml-native-sax|cmeta-shared-owner\.yml|cstl-allocator\.yml|xml-sax\.yml' .github/workflows
```

Expected: no matches.

- [ ] **Step 5: Validate and commit**

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
- Review: complete implementation PR diff

**Interfaces:**
- Consumes: final branch containing Tasks 1–5.
- Produces: merge-ready CI cleanup with exact-head evidence and no lost coverage.

- [ ] **Step 1: Wait for final-head canonical CI**

Because the PR changes both `cmeta.yml` and `native-io-release-benchmarks.yml`, each synchronize event evaluates the PR-wide changed-file set and should run both workflows on the final head. Do not cite an earlier commit's run as final evidence.

Required final-head results:

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

- [ ] **Step 2: Verify migrated checks in final-head logs**

Inspect the final `cmeta.yml` job logs and confirm all of these are visibly executed, not merely buildable:

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
```

- [ ] **Step 3: Verify performance artifact contract**

From the final IOCP artifact, confirm:

```bash
rg -n '^RUN QUALITY: ' libuv-native-io-cnet-benchmark.md
```

Expected: exactly one line, with one of the two existing stable labels:

```text
RUN QUALITY: qualified for performance decisions
RUN QUALITY: noise-limited; do not use for optimization decisions
```

Do not use the cleanup run to make a new performance optimization claim.

- [ ] **Step 4: Audit final changed files**

The implementation PR should contain only the expected workflow changes/deletions. If the spec/plan were merged separately before implementation, the implementation PR should not contain them again.

Expected implementation paths:

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

There must be no temporary patch workflow/script and no production-source change.

- [ ] **Step 5: Review the surviving responsibility split**

Verify the final repository semantics match the design:

```text
ci.yml                         policy/notation only
cmeta.yml                      native conformance + durable regressions
cmeta-cflow-calculus.yml       Lean/generated contracts
native-io-release-benchmarks   performance evidence only
```

If a surviving workflow duplicates an entire migrated feature gate, remove the duplicate before declaring completion; do not broaden cleanup into unrelated refactors.

- [ ] **Step 6: Prepare merge-ready PR metadata**

Update the implementation PR body with:

- design/spec link;
- old workflow count `7`, new workflow count `4`;
- exact migrated regression evidence;
- final exact-head CI run IDs;
- statement that no production source changed;
- statement that performance trigger scope removed only `cmeta/**` and `tinytest/**` automatic triggers while retaining `workflow_dispatch`.

Then request final review. Do not merge until the exact-head evidence above is present.
