# CMeta/CFlow Finite Lowering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate CFlow built-in operator policies from a Lean-validated closed set and remove protocol-unreachable CMeta signature expansions without changing behavior or ABI.

**Architecture:** Named CMeta relation values remain the lower-layer registry facts and are reused by a typed CFlow operator-policy manifest. A separate Lean renderer emits the checked-in CFlow header, while protocol-specific C macros restrict invoke/generate lowering to the signature families that can reach each path.

**Tech Stack:** Lean 4.33.1, Lake 5, strict C11, C++17 public-header consumers, CMeta preprocessor relations, CFlow operator policy, TinyTest, CMake Presets, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-23-cmeta-cflow-finite-lowering-design.md`

## Global Constraints

- Preserve CMeta-to-CFlow dependency direction; CMeta must not import CFlow policy facts.
- Preserve built-in signature order, `cmeta_sig` values, `CMETA_SIG_COUNT == 12`, and all user extension hooks.
- Ordinary CMake configure/build/test must not invoke or require Lean.
- Generated headers are committed, installed, and checked byte-for-byte by CI.
- Preserve invoke/generate rejection results and callable ownership semantics.
- Do not use `sorry`, `admit`, or `axiom`.

---

### Task 1: Lean operator policy, validation, and rendering

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/BuiltinSignatures.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/OperatorPolicyManifest.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/BuiltinOperatorPolicy.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/OperatorPolicyHeader.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/OperatorPolicyManifest.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Produces named CMeta built-in relation constants used by both manifests.
- Produces `OperatorPolicyManifest`, `OperatorPolicyError`, `validate`, and `WellFormed`.
- Produces `builtinOperatorPolicy`, validity/coverage theorems, and `OperatorPolicyHeader.render`.

- [x] **Step 1: Write Lean RED tests**

Create the test module importing the three absent CFlow modules. Assert literals for:

```lean
example : builtinOperatorPolicy.validate builtinSignatureManifest = .ok () := by rfl
example : builtinOperatorPolicy.map.length = 7 := by decide
example : builtinOperatorPolicy.allUnary.length = 9 := by decide
example : unregisteredPolicy.validate builtinSignatureManifest =
    .error .unregisteredSignature := by rfl
example : uncoveredPolicy.validate builtinSignatureManifest =
    .error .registryNotCovered := by rfl
```

Render the built-in policy and require the `CFLOW_BUILTIN_MAP_SIGNATURE_LIST` and
`CFLOW_BUILTIN_REDUCE_SIGNATURE_LIST` macro names.

- [x] **Step 2: Run Lean RED**

Run `lake test` from `formal/cmeta_cflow_calculus`. Confirm failure names the three missing
modules rather than a test syntax error.

- [x] **Step 3: Implement named relations and manifest validation**

Extract all `8/2/1` CMeta relation values into named `def`s and rebuild
`builtinSignatureManifest` from them without changing order. Implement the fixed six-field
operator policy, non-empty/duplicate/subset/coverage checks, and O(n²) build-time complexity
comment from the spec.

- [x] **Step 4: Populate and prove the built-in policy**

Reuse the named CMeta relation values for the exact current CFlow lists. Prove validation,
registry coverage, and count facts by reduction/decision procedures without proof escapes.

- [x] **Step 5: Implement deterministic policy header rendering**

Render the existing six macro bodies with `CMETA_U_ID`, `CMETA_B_ID`, and `CMETA_G_ID`,
plus the six literal count macros. Validation must precede rendering.

- [x] **Step 6: Run Lean GREEN**

Run `lake test`; all formal targets must pass.

### Task 2: CFlow generated header and admission regression

**Files:**
- Create: `formal/cmeta_cflow_calculus/CFlowOperatorPolicyGen.lean`
- Modify: `formal/cmeta_cflow_calculus/lakefile.toml`
- Create: `cflow/include/cflow/generated/builtin_operator_policy.h`
- Modify: `cflow/include/cflow/operator_policy.h`
- Create: `cflow/tests/cflow_operator_policy_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Produces `cflow-operator-policy-gen --stdout|--write|--check`.
- Produces the six built-in list macros and six count macros.
- Preserves all `CFLOW_USER_*` and combined policy macros.

- [x] **Step 1: Write the CFlow RED test**

Register `cflow_operator_policy_test`. Assert literal counts `1/7/1/1/1/1`, acceptance of
one signature for every operator, and rejection of `CMETA_SIG_U_I_B` by map. Build the
target and confirm compilation fails because the count macros are absent.

- [x] **Step 2: Register CLI target and verify RED**

Add a `cflow-operator-policy-gen` Lake executable before its root module exists. Run
`lake exe cflow-operator-policy-gen --stdout` and confirm the missing-root failure.

- [x] **Step 3: Implement CLI and generate the header**

Match the CMeta generator exit contract exactly: stdout success 0, write success 0, exact
check success 0, validation/file mismatch 1, argument error 2. Generate the header only
through `--write`.

- [x] **Step 4: Replace the six hand-written CFlow built-ins**

Include `<cflow/generated/builtin_operator_policy.h>` after relation constructor macros,
remove only the hand-written built-in lists, and retain user hooks/combined aliases.

- [x] **Step 5: Run integrated GREEN**

Build and run `cflow_operator_policy_test`, `cflow_graph_test`, and
`cflow_header_cpp_test`; run the new generator `--check`.

### Task 3: Protocol-specific CMeta lowering

**Files:**
- Modify: `cmeta/include/cmeta/signatures.h`
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/src/cmeta.c`
- Modify: `cmeta/tests/cmeta_signature_manifest_test.c`
- Modify: `cmeta/tests/cmeta_core_test.c`

**Interfaces:**
- Produces `CMETA_VALUE_SIGNATURES(U, B)`.
- Preserves `CMETA_ALL_SIGNATURES(U, B, G)` and every public callable result.

- [x] **Step 1: Write grouping RED and protocol characterization**

Use mapper macros in `cmeta_signature_manifest_test.c` to derive literal value count 10 and
generator count 1 through the public grouping macros. Confirm compilation fails because
`CMETA_VALUE_SIGNATURES` is absent. Extend the existing protocol test so value generation
returns `CMETA_GEN_ERROR` without changing output/cursor and generator invocation returns
false.

- [x] **Step 2: Add the minimal grouping macro and run GREEN**

Define `CMETA_VALUE_SIGNATURES` as unary followed by binary and make
`CMETA_ALL_SIGNATURES` reuse it. Build/run the two CMeta tests.

- [x] **Step 3: Refactor protocol-specific expansion**

Generate only unary/binary typed invoke adapters; map generator `_Generic` associations to
the common unsupported adapter. Restrict `cmeta_fn_invoke` to value signatures and
`cmeta_fn_generate` to generator signatures with explicit default rejection.

- [x] **Step 4: Re-run behavior tests and measure artifacts**

Run all CMeta tests. Recompile `cmeta.c` under relation/balanced/full with the same MSVC
`/O2` measurement used for the spec and report preprocessed/object byte deltas against
`183064/39880`, `257555/74481`, and `392069/121791`.

### Task 4: CI, documentation, and PR update

**Files:**
- Modify: `.github/workflows/cmeta-cflow-calculus.yml`
- Modify: `cmeta/README.md`
- Modify: `cmeta/LANGUAGE_REFERENCE.md`
- Modify: `cflow/README.md`

**Interfaces:**
- CI verifies both generated headers.
- Documentation records ownership, regeneration, compatibility, and measured scope.

- [x] **Step 1: Add CFlow drift CI**

Extend path filters to the generated CFlow header and consumer policy. Run
`lake exe cflow-operator-policy-gen --check` after the CMeta generator check.

- [x] **Step 2: Document the new boundary**

Document that CMeta owns registered relations, CFlow owns operator membership, Lean proves
closure/coverage, user hooks stay manual, and protocol lowering changes code generation but
not runtime semantics.

- [x] **Step 3: Run full verification**

Run both generator checks, `lake test`, all CMeta/CFlow targets, CTest filters `^cmeta_` and
`^cflow_`, proof hygiene, `git diff --check`, and `codegraph affected`.

- [ ] **Step 4: Review, commit, and update PR #54**

Commit as `feat(cmeta): specialize finite signature lowering`, push the existing branch,
verify PR #54 points to `master`, and preserve the worktree for CI/review fixes.
