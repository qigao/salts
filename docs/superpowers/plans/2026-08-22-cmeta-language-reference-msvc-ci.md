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

---

### Task 1: Publish the language and proof policy

**Files:**
- Create: `cmeta/LANGUAGE_REFERENCE.md`
- Modify: `cmeta/README.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`
- Modify: `docs/superpowers/plans/2026-08-22-cmeta-lean-module-system-plan-b.md`

**Interfaces:**
- Produces the authoritative four-layer CMeta vocabulary and explicit removed/future syntax policy.

- [ ] **Step 1: Add `LANGUAGE_REFERENCE.md`**

Document `Application DSL`, `Framework DSL`, `Runtime Protocol`, `Reserved future syntax`, and `Removed syntax`, with canonical C examples for every stable DSL word.

- [ ] **Step 2: Link the reference from the README**

Keep README as overview; add an authoritative-reference link and the one-sentence project positioning as a pragmatic modern C dialect/toolkit.

- [ ] **Step 3: Align capability catalog**

Ensure the public API list contains `Struct`, `Enum`, `Traits`, `typed`, `typed_any`, `interface`, `implements`; keep `Schema/Replay/Operators` framework-facing; ensure `Containers` is described only as removed syntax, if mentioned at all.

- [ ] **Step 4: Mark Plan B proof stopping point**

Add a status note to the Plan B document: Tasks through M7g are the accepted proof checkpoint; M8 is deferred TODO under the newer proof policy.

- [ ] **Step 5: Review docs for contradictions**

Search the changed docs for `Containers(` and verify any remaining occurrence is explicitly under removed syntax rather than presented as usable API.

---

### Task 2: Split portable MSVC conformance from backend certification

**Files:**
- Modify: `formal/CMakeLists.txt`
- Modify: `CMakePresets.json`
- Modify: `presets/CompilerFlags.json`

**Interfaces:**
- Produces configure preset `formal-windows-msvc` and build preset `build-formal-windows-msvc`.
- GNU/Clang continue to produce their existing nested-replay certificate witness unchanged.

- [ ] **Step 1: Enable the conforming MSVC C preprocessor**

Add `/Zc:preprocessor` to `flags-msvc` C flags because CMeta's schema DSL requires the conforming preprocessor for C as well as C++.

- [ ] **Step 2: Gate backend-specific replay certification**

Refactor the top of `formal/CMakeLists.txt` so GNU/Clang run the direct-replay probe and select the existing generated Lean namespace, while MSVC sets a portable-conformance mode and skips that certificate slice with a clear status message. Unknown compiler families still fail configuration.

- [ ] **Step 3: Gate the nested backend witness target**

Create `cmeta_nested_replay_deferred_witness` only when backend certification is enabled. Do not alter its source or generated snapshots.

- [ ] **Step 4: Add MSVC formal presets**

Add a hidden/common Windows formal preset using `ISWindows`, `release-mode`, and `flags-msvc`, generator `Ninja`, formal-only cache variables, and a dedicated binary directory. Add `formal-windows-msvc` and `build-formal-windows-msvc` without hard-coded Visual Studio installation paths.

---

### Task 3: Add the Windows/MSVC + Lean CI workflow

**Files:**
- Create: `.github/workflows/msvc.yml`

**Interfaces:**
- Consumes: `formal-windows-msvc`, `build-formal-windows-msvc`.
- Produces one non-proof-backend CI signal: `MSVC / portable conformance + Lean`.

- [ ] **Step 1: Enter the Visual Studio developer environment without a Node action dependency**

Use `vswhere.exe` in one `cmd` step to locate the latest installation containing `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`, call `Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64`, and run CMake in the same step so the environment does not need to be serialized into YAML variables.

- [ ] **Step 2: Configure and build portable formal witnesses**

Invoke only the CMake presets. Build all existing portable formal targets except `cmeta_nested_replay_deferred_witness`.

- [ ] **Step 3: Run the portable CTest set**

Run `ctest --test-dir build/formal-windows-msvc --output-on-failure` after explicitly building the EXCLUDE_FROM_ALL witness targets.

- [ ] **Step 4: Build Lean on Windows**

Use the official `leanprover/lean-action@v1`, set `lake-package-directory: formal`, disable unrelated test/lint auto-features, and pass `--wfail` to the build. This checks portability of the existing Lean model without producing an MSVC backend certificate.

- [ ] **Step 5: Scope workflow triggers**

Trigger on changes under `formal/**`, `cmeta/**`, `cflow/**`, CMake preset/config files, and the workflow itself.

---

### Task 4: Verify and publish

**Files:**
- Review all changed files from Tasks 1–3.

**Interfaces:**
- Produces a reviewable feature branch/PR; no merge is implied.

- [ ] **Step 1: Inspect the complete diff**

Require no changes to generated Lean snapshot payloads, replay theorem statements, or runtime algorithms.

- [ ] **Step 2: Verify Linux proof lanes still pass**

Use the existing `Lean proofs` workflow as the regression gate for GCC and Clang.

- [ ] **Step 3: Verify the new MSVC workflow**

Require configuration to reach portable-conformance mode, portable witness targets/CTest to pass, and the Windows Lean build to pass. If MSVC reveals a real C portability defect, fix the C implementation rather than weakening the workflow.

- [ ] **Step 4: Publish a draft PR**

Base the PR on `leanv4-plan-b`, summarize the proof stopping policy, language surface, removed `Containers(...)`, and the distinction between backend proof lanes and the MSVC portability lane.
