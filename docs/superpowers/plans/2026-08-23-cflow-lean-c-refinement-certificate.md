# CFlow Lean/C Refinement Certificate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a versioned Plan certificate whose supported semantics are proved in Lean and transactionally checked against the actual C Graph/Plan objects.

**Architecture:** C Plan compilation emits a compact immutable witness; a public checker binds it to one normalized Graph and compiled Plan. Lean models the same schema and proves observation preservation for sequential Plan and ordered Parallel Reduce. The checker/compiler boundary remains explicit trusted computing base rather than being mislabeled as a proof of arbitrary C binaries.

**Tech Stack:** Lean 4/Lake, C11, CFlow Graph/Plan, TinyTest, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

## Global Constraints

- Depends on ordered Parallel Reduce metadata.
- Certificate schema version and enum values are stable public data; certificate instances are runtime-only and are not serialized.
- Certificate creation and checking are transactional and allocation-bounded by Plan instruction count.
- Unsupported Relation/WAIT semantics fail certificate creation; they continue to execute through Kernel runtime.
- Production Lean modules contain no `sorry`, `admit`, or new `axiom`.
- Documentation says “refinement certificate”, never “Lean verified C binary”.

---

### Task 1: Define one certificate schema in C and Lean

**Files:**
- Create: `cflow/include/cflow/certificate.h`
- Create: `cflow/src/certificate.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Certificate.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`

**Interfaces:**
- Produces: `CFLOW_PLAN_CERTIFICATE_V1`, stable opcode/path/property rows, `cflow_plan_certificate_build()`, `cflow_plan_certificate_destroy()`.

- [ ] Define the C schema with explicit-width version/opcode/path fields, retained `cmeta_callable` values, semantic type descriptors, property masks, Graph version/fingerprint, and chunk-order policy. Use owned contiguous rows and checked `count * sizeof(row)` allocation; state that the instance is runtime-only.
- [ ] Define Lean `CertificateVersion`, `CertifiedOpcode`, `CertifiedPath`, `CertificateRow`, and `PlanCertificate` with numerals matching C `_Static_assert` values.
- [ ] Add C compile-time assertions and Lean examples for every stable numeral.
- [ ] Build CFlow and run `lake test`; expected GREEN only when both representations are complete.
- [ ] Commit as `feat(cflow): define plan refinement certificate`.

### Task 2: Transactional C checker

**Files:**
- Modify: `cflow/src/certificate.c`
- Modify: `cflow/src/plan_compile.c`
- Create: `cflow/tests/cflow_certificate_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `cflow_plan_certificate_check(const cflow_plan_certificate *, const cflow_graph *, const cflow_plan *, const char **error)`.

- [ ] Write a passing linear Filter/Map/Reduce case and negative copies tampering one field at a time: version, Graph fingerprint, opcode, type, callable identity, property, path, order policy, and row count.
- [ ] Run the new test and confirm RED because the checker is absent.
- [ ] Hash canonical normalized Graph topology/opcode/property numerals only; do not hash pointer addresses or padding. Compare callable and type semantics with `cmeta_callable_same()` and `cmeta_type_equal()`. Recompute and compare every row against Plan instructions and Graph traversal before setting a local `matched` flag.
- [ ] Clear `*error` on success, return the first stable static diagnostic on failure, and never modify Graph/Plan/certificate during checking.
- [ ] Add unsupported Relation and stale-Graph tests; verify Kernel remains independently executable.
- [ ] Run certificate, pipeline, optimizer, Direct, and calculus conformance tests.
- [ ] Commit as `feat(cflow): check plan refinement certificates`.

### Task 3: Prove certificate denotation and ordered reduction

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Certificate.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Certificate.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseG.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Architecture.lean`

**Interfaces:**
- Consumes: Phase D R11 premises and Phase E path/cost definitions.
- Produces: `certificate_preserves_observation`, `ordered_chunks_reduce_eq`, and a narrowed `CompileContract` comment.

- [ ] Define certificate validity as exact ordered agreement between Graph semantic rows, Plan rows, type chain, callable identity, and required properties.
- [ ] Prove by row induction that a valid sequential certificate has the same observation as its normalized Graph denotation.
- [ ] Define contiguous nonempty chunks and prove ordered left-combination equals sequential left reduction under associativity. Do not assume commutativity or permit chunk permutation.
- [ ] Lift the theorem to the Parallel Reduce certificate path and reuse `ExecutionRefines.r11` for observation preservation.
- [ ] Add negative examples showing missing associativity, reversed chunk order, stale graph identity, and unsupported WAIT cannot construct validity.
- [ ] Run `lake test`; scan production formal modules with `rg.exe -n "sorry|admit|axiom"` and require no new matches.
- [ ] Commit as `formal(cflow): prove plan certificate refinement`.

### Task 4: CI joins C and Lean evidence

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Modify: `docs/superpowers/specs/2026-08-22-cmeta-cflow-calculus-v1-design.md`
- Modify: `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

- [ ] Add `formal/cmeta_cflow_calculus/**` to workflow path filters.
- [ ] Install Lean through the repository-pinned `lean-toolchain`, run `lake test`, then run `cflow_certificate_test` and `cflow_calculus_conformance_test` in the same job.
- [ ] Record the exact trust boundary: Lean theorem, C checker tests, compiler/runtime outside proof.
- [ ] Run local `lake test`, focused CTests, full Windows Release CTest, installed-package verification, and `git diff --check`.
- [ ] Commit as `ci(cflow): verify Lean C refinement certificates`.
