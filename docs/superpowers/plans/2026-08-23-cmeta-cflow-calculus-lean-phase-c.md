# CMeta-CFlow Calculus Lean Phase C Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Formalize observational semantics and prove that certified Surface → Graph → Normalize → Optimize → Plan → Kernel stages preserve typing, validity, observable traces, and ownership safety.

**Architecture:** `CFlow/Observation.lean` defines the Kernel capability domain `K`, typed observable traces, and observational equivalence. `CFlow/Architecture.lean` defines distinct typed artifacts and explicit backend contracts for each stage; `Proofs/Architecture.lean` composes those contracts into stage-local and end-to-end preservation theorems without claiming that the current C implementation already satisfies them.

**Tech Stack:** Lean 4.33.1, Lake, typed structures, proposition-valued backend contracts, pointwise trace equality

**Spec:** `docs/superpowers/specs/2026-08-22-cmeta-cflow-calculus-v1-design.md`

## Global Constraints

- Phase C covers only observational semantics and Surface / Graph / Normalize / Optimize / Plan / Kernel preservation.
- R1–R15 theorem bodies remain Phase D; Phase C models optimizer soundness as an explicit contract premise.
- Cost and Direct / Plan / Kernel / Batch / Parallel Reduce derivations remain Phase E.
- C implementation conformance remains Phase F and is not asserted by the Lean architecture theorems.
- Observable traces preserve value encounter order, effect order, error outcome, completion/cancellation outcome, and ownership safety.
- Graph node ids, allocation addresses, executor task ids, scratch buffers, and wake tokens are not observable events.
- `K` describes available execution capabilities; plan requirements describe demanded capabilities, and Kernel soundness requires satisfaction.
- No `axiom`, `sorry`, `admit`, third-party Lean dependency, or production C/C++ modification is allowed.
- The staged `CMakeUserPresets.json` change remains untouched.

---

### Task 1: Typed observations and Kernel capabilities

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Observation.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseC.lean`

**Interfaces:**
- Consumes: `CMeta.Ty`, `CMeta.Value`, and `CMeta.Effect`.
- Produces: `KernelCapability`, `KernelCapabilities`, `CapabilityRequirements`, `Capabilities`, `Observation`, `Trace`, `Input`, `Semantics`, `ExecutionSemantics`, `ObsEqAt`, and `ObsEq`.

- [x] **Step 1: Write the failing observation tests**

  Import `CMetaCFlowCalculus.CFlow.Observation`, construct a trace containing `.value`, `.effect`, and `.done`, and prove that two semantics with the same trace are `ObsEqAt`. Add a capability witness in which `SYNC_EXECUTION` and `BATCH_EXECUTION` satisfy the matching plan requirements.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseC.lean`

  Expected: FAIL because `CMetaCFlowCalculus.CFlow.Observation` does not exist.

- [x] **Step 3: Implement the minimal observation model**

  Define the eight section 3.2 capabilities. Define typed user observations only for `value`, `effect`, `error`, `done`, and `cancelled`; store `ownershipSafe : Bool` beside the ordered event list. Define `ObsEqAt K left right := forall input, left K input = right K input` and universal `ObsEq` over all `K`.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseC.lean`

  Expected: PASS for trace equality and the positive capability witness.

### Task 2: Distinct architecture artifacts and contracts

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Architecture.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseC.lean`

**Interfaces:**
- Consumes: `Flow`, `FlowMode`, `Semantics`, `ExecutionSemantics`, `ObsEq`, and `Capabilities`.
- Produces: `Surface`, `Graph`, `NormalizedGraph`, `OptimizedGraph`, `Plan`, `KernelExecution`, `LoweringContract`, `NormalizeContract`, `OptimizeContract`, `CompileContract`, `KernelContract`, and `Architecture`.

- [x] **Step 1: Add failing identity-architecture witnesses**

  Create one typed terminal Surface and a shared literal semantics. Define identity-like lower/normalize/optimize/compile/execute functions returning distinct artifact types, then attempt to package their typing, validity, well-formedness, observation, and capability obligations into an `Architecture`.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseC.lean`

  Expected: FAIL with unknown architecture artifact and contract identifiers.

- [x] **Step 3: Define the minimal stage model**

  Index every artifact by `Γ`, input type, output type, and `FlowMode`. Keep Graph, NormalizedGraph, OptimizedGraph, Plan, and KernelExecution nominally distinct. Lowering must establish Graph typing and validity; normalization and optimization preserve both under valid input; compilation establishes Plan well-formedness; execution preservation requires both Plan well-formedness and `Capabilities K plan.requirements`.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseC.lean`

  Expected: PASS for the complete identity architecture.

### Task 3: Stage-local and end-to-end preservation proofs

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Architecture.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseC.lean`

**Interfaces:**
- Consumes: every Phase C contract and artifact.
- Produces: `surface_soundness`, `normalize_soundness`, `optimize_soundness`, `plan_compilation_soundness`, `kernel_execution_soundness`, and `architecture_observation_preservation`.

- [x] **Step 1: Add failing theorem-use tests**

  Use the identity architecture to prove each generated intermediate is typed/valid/well-formed and that the Surface is `ObsEqAt` to the KernelExecution. Add a negative capability boundary: a Kernel lacking a required capability must not produce a premise of the end-to-end theorem.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseC.lean`

  Expected: FAIL because the Phase C theorem names do not exist.

- [x] **Step 3: Prove preservation by contract projection and transitivity**

  Project each local contract proof. Compose Surface/Graph, Graph/NormalizedGraph, NormalizedGraph/OptimizedGraph, OptimizedGraph/Plan, and Plan/KernelExecution equalities pointwise at the chosen `K`; derive every validity premise from the preceding certified stage.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseC.lean`

  Expected: PASS for all local obligations, end-to-end trace preservation, ownership-safe preservation, and capability rejection.

### Task 4: Root integration and verification

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`
- Verify: `formal/cmeta_cflow_calculus/**`

**Interfaces:**
- Consumes: the Phase C modules and tests.
- Produces: root library imports and a single Phase A/B/C test driver.

- [x] **Step 1: Add root and test-driver imports**

  Import `CFlow.Observation`, `CFlow.Architecture`, and `Proofs.Architecture` from the root library; import `PhaseATests.PhaseC` from the test driver.

- [x] **Step 2: Run focused and complete Lean verification**

  Run: `lake env lean Test/PhaseATests/PhaseC.lean`

  Run: `lake test -v`

  Run: `lake build -v`

  Expected: all commands exit 0 with Phase A, B, and C modules elaborated.

- [x] **Step 3: Audit proof and scope boundaries**

  Run: `rg.exe -n "\b(sorry|admit|axiom)\b" CMetaCFlowCalculus Test`

  Run: `git diff --name-only -- cmeta cflow platform concurrency turbostl utils`

  Expected: no proof escapes and no production-directory changes.

- [x] **Step 4: Confirm unrelated staged state is unchanged**

  Run: `git rev-parse ':CMakeUserPresets.json'`

  Expected: staged blob remains `268d3ff55fcd14b6d62ea44b104b8ecb58d54a02`.
