# Windows CI Cache and Step Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared Windows vcpkg binary cache and expose each expensive CI phase as a separate GitHub Actions step.

**Architecture:** Both Windows jobs configure vcpkg's filesystem binary provider at `build/vcpkg-binary-cache` and restore it with an identical runner-image/manifest-aware key. Each new command step re-enters `VsDevCmd.bat`, while existing presets, targets, test filters, and platform-specific behavior remain unchanged.

**Tech Stack:** GitHub Actions, `actions/cache@v4`, vcpkg manifest mode, CMake Presets, CTest, MSVC/Ninja

**Spec:** `docs/superpowers/specs/2026-09-02-windows-ci-cache.md`

## Global Constraints

- Cache vcpkg binary archives only; do not cache `build/Msvc-Release` or an install tree.
- Keep `win-release-user`, `install-win-release-user`, existing target lists, and existing CTest filters unchanged.
- Initialize every Windows CMake/CTest command step with `VsDevCmd.bat`.
- Preserve Linux and macOS behavior in the matrix workflow.
- Keep `tools/wsparser/` untouched.

---

### Task 1: CMeta Windows release cache and phases

**Files:**
- Modify: `.github/workflows/cmeta.yml:254`

**Interfaces:**
- Consumes: GitHub runner variables `GITHUB_WORKSPACE`, `VCPKG_INSTALLATION_ROOT`, `ImageOS`, and `ImageVersion`.
- Produces: persisted `PROJECT_ROOT`, `VCPKG_ROOT`, `VSINSTALL`, and `VCPKG_BINARY_SOURCES`; output `image` for the cache key.

- [x] **Step 1: Add fail-fast Windows environment discovery**

Write `PROJECT_ROOT`, validated vcpkg and Visual Studio roots, and the absolute binary-cache provider to `GITHUB_ENV`. Export `ImageOS-ImageVersion` through `GITHUB_OUTPUT`.

```yaml
- name: Setup Windows build environment
  id: windows-env
  shell: pwsh
  run: |
    $ErrorActionPreference = "Stop"
    # Validate hosted-runner vcpkg and Visual Studio, then persist their paths.
```

- [x] **Step 2: Restore vcpkg binary archives**

Use one shared key schema in both workflows:

```yaml
- name: Restore vcpkg binary cache
  uses: actions/cache@v4
  with:
    path: build/vcpkg-binary-cache
    key: vcpkg-binary-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-x64-windows-${{ hashFiles('vcpkg.json') }}
    restore-keys: |
      vcpkg-binary-${{ runner.os }}-${{ runner.arch }}-${{ steps.windows-env.outputs.image }}-x64-windows-
      vcpkg-binary-${{ runner.os }}-${{ runner.arch }}-
```

- [x] **Step 3: Split the monolithic command**

Create `Configure`, `Build`, `Test execution foundation and typed layers`,
`Install release profile`, and `Verify installed package targets` steps. Each
step calls `VsDevCmd.bat` before its existing preset command.

- [x] **Step 4: Inspect the workflow diff**

Run: `git diff -- .github/workflows/cmeta.yml`

Expected: only the Windows job changes; command arguments and filters are preserved.

### Task 2: NativeIO Windows cache and build phases

**Files:**
- Modify: `.github/workflows/native-io-release-benchmarks.yml:104`

**Interfaces:**
- Consumes: the same hosted-runner variables and cache key schema as Task 1.
- Produces: Windows-only vcpkg binary cache restoration and distinct configure/build diagnostics.

- [x] **Step 1: Extend Windows setup without affecting other matrix entries**

Add a Windows-only environment discovery step and a Windows-only
`actions/cache@v4` step. Use the exact cache directory, binary source, key, and
restore prefixes from Task 1.

- [x] **Step 2: Split configure from build**

Keep `if: matrix.family == 'windows'` on both steps. The configure step retains
`-DBUILD_BENCHMARKS=ON -DBUILD_TESTS=ON`; the build step retains the complete
existing 17-target list.

- [x] **Step 3: Inspect the workflow diff**

Run: `git diff -- .github/workflows/native-io-release-benchmarks.yml`

Expected: Linux/macOS commands, tests, benchmarks, and artifacts are unchanged.

### Task 3: Static and local verification

**Files:**
- Verify: `.github/workflows/cmeta.yml`
- Verify: `.github/workflows/native-io-release-benchmarks.yml`
- Verify: `CMakeUserPresets.json`

**Interfaces:**
- Consumes: modified workflow YAML and version-controlled presets.
- Produces: reproducible syntax, contract, configure, build, and test evidence.

- [x] **Step 1: Validate YAML and workflow structure**

Run the available YAML parser and `actionlint` if installed. Then use `rg.exe`
to confirm both workflows contain the shared cache key and all required phase
names.

- [x] **Step 2: Validate preset availability**

Run:

```powershell
cmake --list-presets
cmake --build --list-presets
ctest --list-presets
```

Expected: `win-release-user` and `install-win-release-user` remain available.

- [x] **Step 3: Run the smallest relevant Windows build and tests**

From `VsDevCmd.bat`, run the existing `win-release-user` configure preset, build
the NativeIO/CNet targets named by the workflow, and run their existing CTest
filters. Then run the installed-package verification target.

- [x] **Step 4: Check patch integrity**

Run: `git diff --check`

Expected: exit code 0.

- [x] **Step 5: Prepare the independent PR handoff**

```powershell
git add .github/workflows/cmeta.yml .github/workflows/native-io-release-benchmarks.yml docs/superpowers/specs/2026-09-02-windows-ci-cache.md docs/superpowers/plans/2026-09-02-windows-ci-cache-and-steps.md
git commit -m "ci(windows): cache vcpkg binaries and split phases"
```

Expected: a verified commit ready for the integration choice in the branch-finishing workflow.
