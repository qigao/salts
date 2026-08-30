# CFlow SCXML Event I/O and Conformance Seed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define the exact Event I/O processor support boundary and add a small, reproducible W3C-derived SCXML regression corpus without overstating conformance.

**Architecture:** Keep transport and codec behavior outside CFlow. Document the mandatory SCXML Event Processor URI as the default processor, execute immediate internal delivery locally, and treat all other processor/target combinations as explicit adapter responsibilities. Add a separate TinyTest executable that compiles and runs transformed W3C IR documents through the public SCXML and native Statechart APIs.

**Tech Stack:** C11, CFlow SCXML, CFlow Statechart runtime, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-remaining-design.md`

## Global Constraints

- Preserve the current public ABI and accepted document behavior.
- Do not add HTTP or another transport dependency to TurboUtils.
- Keep W3C source provenance, transformation notes, and license links beside the derived fixtures.
- Treat the W3C implementation report as a regression source, not as third-party certification.
- Keep the complete-corpus issue item open until the supported-profile mapping and exclusions are exhaustive.

---

### Task 1: Register a failing W3C-derived conformance test target

**Files:**
- Create: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`
- Modify: `cflow-scxml/tests/CMakeLists.txt`

- [x] Implement a focused runner that reads a fixture, compiles it with `cflow_scxml_compile()`, obtains public runtime/guard bindings, starts a serial Statechart instance, waits for quiescence, and verifies that the active final state is `pass`, not `fail`.
- [x] Add separate TinyTest cases for W3C IR tests 355 and 375.
- [x] Register `cflow_scxml_w3c_conformance_test` and define `CFLOW_SCXML_W3C_FIXTURE_DIR`.
- [x] Configure and build with SCXML enabled:

  ```powershell
  cmd /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user -DCFLOW_ENABLE_SCXML=ON && cmake --build --preset win-release-user --target cflow_scxml_w3c_conformance_test"
  ```

- [x] Run the new target and observe the expected failure because the fixtures do not exist yet:

  ```powershell
  ctest --preset win-release-user -R "^cflow_scxml_w3c_conformance_test$" --output-on-failure
  ```

---

### Task 2: Add the first transformed W3C IR fixtures

**Files:**
- Create: `cflow-scxml/tests/w3c/README.md`
- Create: `cflow-scxml/tests/w3c/test355.scxml`
- Create: `cflow-scxml/tests/w3c/test375.scxml`

- [x] Document upstream URLs, assertion text in paraphrase, transformation rules, local pass/fail convention, and W3C test-suite licensing.
- [x] Transform test 355 into the supported null-data-model profile while preserving its assertion that an omitted root initial selects the first child in document order.
- [x] Transform test 375 while preserving its assertion that multiple `onentry` handlers execute in document order; replace unsupported wildcard failure transitions with exact event failure transitions without weakening the observed ordering.
- [x] Run the focused test and observe both cases pass.

---

### Task 3: Define Event I/O processor and conformance claims

**Files:**
- Modify: `cflow/README.md`

- [x] Add an Event I/O processor matrix covering the canonical SCXML processor URI, omitted `type`, `_internal`/`#_internal`, external SCXML targets, Basic HTTP, and application-defined processor types.
- [x] State which behavior is implemented locally, which is delegated to a v1 adapter, and which is unsupported.
- [x] Explain that `_ioprocessors` is not yet exposed, so the module does not claim full processor conformance.
- [x] Document the W3C-derived regression corpus as evidence for named assertions only, not as a certification result.

---

### Task 4: Verify affected and adjacent behavior

**Files:**
- Modify only if verification reveals a real defect in the new work.

- [x] Build and run all SCXML tests:

  ```powershell
  cmd /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user && ctest --preset win-release-user -R "^cflow_scxml" --output-on-failure"
  ```

- [x] Run the full configured suite with SCXML enabled:

  ```powershell
  ctest --preset win-release-user --output-on-failure
  ```

- [x] Run `codegraph sync .` and inspect affected files/test candidates.
- [x] Review `git diff --check`, `git status --short`, and the final diff.
