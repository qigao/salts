# CMeta Language Reference and MSVC CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the implemented CMeta language surface explicit, stop formal proof expansion at the verified M7g checkpoint, and add an MSVC portability/Lean-build CI lane without pretending MSVC is a formally certified replay backend.

**Architecture:** Keep GCC/Clang as the only backend-certificate lanes. Add a portable MSVC formal configuration that skips the nested-replay backend certificate but compiles/runs the shared CMeta/CFlow conformance witnesses and builds the existing Lean package on Windows. Publish one authoritative language reference with application/framework/runtime/future boundaries.

**Tech Stack:** strict C11, CMake presets, MSVC, Ninja, GitHub Actions, Lean 4.30.0, Lake.

**Spec:** `docs/superpowers/specs/2026-08-22-cmeta-language-reference-msvc-ci-design.md`

## Global Constraints

- M7g is the current formalization stopping point; M8 is TODO, not a release gate.
- Do not add `CompilerFamily.msvc` or an MSVC nested-replay generated certificate.
- `typed(...)` is the only public generic/container instantiation entry; `Containers(...)` remains removed with no alias.
- MSVC CI must use CMake presets for configuration/build policy; YAML may only enter the developer environment and invoke presets/standard tools.
- Keep GCC/Clang snapshot semantics unchanged.
- Prefer ordinary C composition and existing CMeta patterns over new syntax.
- Do not commit `formal/lake-manifest.json`; CI generates it with `lake update` before the build.

---

### Task 1: Publish the language and proof policy

**Files:**
- Create: `cmeta/LANGUAGE_REFERENCE.md`
- Modify: `cmeta/README.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`
- Create: `docs/superpowers/specs/2026-08-22-cmeta-language-reference-msvc-ci-design.md`

**Interfaces:**
- Produces the authoritative four-layer CMeta vocabulary and explicit removed/future syntax policy.

- [x] **Step 1: Add `LANGUAGE_REFERENCE.md`**

Document `Application DSL`, `Framework DSL`, `Runtime Protocol`, `Reserved future syntax`, and `Removed syntax`, with canonical C examples for every stable DSL word.

- [x] **Step 2: Link the reference from the README**

Keep README as overview; add an authoritative-reference link and the one-sentence project positioning as a pragmatic modern C dialect/toolkit.

- [x] **Step 3: Align capability catalog**

Ensure the public API list contains `Struct`, `Enum`, `Traits`, `typed`, `typed_any`, `interface`, `implements`; keep `Schema/Replay/Operators` framework-facing; ensure `Containers` is described only as removed syntax.

- [x] **Step 4: Record proof stopping policy**

The design spec supersedes M8 as a mandatory gate: the verified M7g checkpoint is sufficient for current syntax/semantics, while root isolation, MSVC backend certification, and stronger completeness claims are future work.

---

### Task 2: Split portable MSVC conformance from backend certification

**Files:**
- Modify: `formal/CMakeLists.txt`
- Modify: `CMakePresets.json`
- Modify: `presets/CompilerFlags.json`

**Interfaces:**
- Produces configure preset `formal-windows-msvc` and build preset `build-formal-windows-msvc`.
- GNU/Clang continue to produce their existing nested-replay certificate witness unchanged.

- [x] **Step 1: Enable the conforming MSVC C preprocessor**

Add `/Zc:preprocessor` to `flags-msvc` C flags because CMeta's schema DSL requires the conforming preprocessor for C as well as C++.

- [x] **Step 2: Gate backend-specific replay certification**

GNU/Clang keep the direct-replay negative probe and existing generated Lean namespaces. MSVC enters portable-conformance mode and skips that certificate slice with an explicit status message. Unknown compiler families still fail formal-only configuration.

- [x] **Step 3: Gate the nested backend witness target**

Create `cmeta_nested_replay_deferred_witness` only when backend certification is enabled. Do not alter its source or generated snapshots.

- [x] **Step 4: Add MSVC formal presets**

Add `formal-windows-msvc` and `build-formal-windows-msvc` using the existing Windows/MSVC flags, Ninja, formal-only cache variables, and a dedicated binary directory. CI enters the Visual Studio developer environment instead of hard-coding runner installation paths in the preset.

---

### Task 3: Add the Windows/MSVC + Lean CI workflow

**Files:**
- Create: `.github/workflows/msvc.yml`

**Interfaces:**
- Consumes: `formal-windows-msvc`, `build-formal-windows-msvc`.
- Produces one non-proof-backend CI signal: `MSVC / portable conformance + Lean`.

- [x] **Step 1: Enter the Visual Studio developer environment**

Use `vswhere.exe` to locate the latest installation containing `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`, call `Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64`, and run CMake inside that environment.

- [x] **Step 2: Configure and build portable formal witnesses**

Invoke only the CMake presets. Build all existing portable formal targets except `cmeta_nested_replay_deferred_witness`.

- [x] **Step 3: Run the portable CTest set**

Run `ctest --test-dir build/formal-windows-msvc --output-on-failure` after explicitly building the EXCLUDE_FROM_ALL witness targets.

- [x] **Step 4: Build Lean on Windows with the repository's existing manifest policy**

Use pinned elan `v4.2.2`, read the Lean version from `formal/lean-toolchain`, and run exactly the same Lake lifecycle used by the Linux proof lane:

```bash
elan toolchain install "$(cat formal/lean-toolchain)"
cd formal
lake update
lake build --wfail
```

Do not use a CI action that requires a pre-existing `lake-manifest.json`; this repository deliberately generates the manifest during CI and does not commit it.

- [x] **Step 5: Scope workflow triggers**

Trigger on changes under `formal/**`, `cmeta/**`, `cflow/**`, CMake preset/config files, and the workflow itself.

---

### Task 4: Verify and publish

**Files:**
- Review all changed files from Tasks 1–3.

**Interfaces:**
- Produces a reviewable draft PR; no merge is implied.

- [x] **Step 1: Inspect the complete diff**

Require no changes to generated Lean snapshot payloads, replay theorem statements, or runtime algorithms.

- [x] **Step 2: Verify Linux proof lanes still pass**

Use the existing `Lean proofs` workflow as the regression gate for GCC and Clang. Both lanes must retain snapshot zero-diff, applicability checks, API guards, and `lake build --wfail`.

- [ ] **Step 3: Verify the new MSVC workflow**

Require configuration to report portable-conformance mode, portable witness targets and CTest to pass, and the pinned Windows Lean build to pass.

- [x] **Step 4: Publish a draft PR**

Base the PR on `leanv4-plan-b`, summarize the proof stopping policy, language surface, removed `Containers(...)`, and the distinction between backend proof lanes and the MSVC portability lane.
