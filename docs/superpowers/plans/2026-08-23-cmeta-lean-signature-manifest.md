# CMeta Lean Signature Manifest Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one validated Lean manifest generate CMeta's built-in type rows and finite callable relations without adding Lean to ordinary C builds.

**Architecture:** A reusable Lean data model validates closed finite type/relation lists; a separate renderer lowers a valid manifest to a deterministic checked-in C header. Existing `types.h` and `relations.h` consume that generated header while retaining their current user-extension policy and ABI ordering.

**Tech Stack:** Lean 4.33.1, Lake 5, strict C11, C++17 header consumers, CMeta preprocessor schemas, TinyTest, CMake Presets, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-23-cmeta-lean-signature-manifest-design.md`

## Global Constraints

- `BuiltinSignatures.lean` is the only source of truth for built-in type rows and relations.
- Manifest validation fails before rendering on empty lists, duplicates, or unknown type references.
- The generated built-in macro order and default `cmeta_sig` values remain unchanged.
- `CMETA_USER_TYPE_LIST` and all three `CMETA_USER_*_RELATION_LIST` hooks remain source-compatible.
- Ordinary CMake configure/build/test must not invoke or require Lean.
- Generated headers are committed, installed, and checked for drift by Lean CI.
- No proof escape (`sorry`, `admit`, or `axiom`) is permitted.

---

### Task 1: Executable Lean manifest and proofs

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/SignatureManifest.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/BuiltinSignatures.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/SignatureHeader.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/SignatureManifest.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Produces: `CTypeRow`, `UnaryRelation`, `BinaryRelation`, `GeneratorRelation`, `SignatureManifest`, `ManifestError`.
- Produces: `SignatureManifest.validate : SignatureManifest → Except ManifestError Unit`.
- Produces: `SignatureManifest.WellFormed : SignatureManifest → Prop`.
- Produces: `SignatureHeader.render : SignatureManifest → Except ManifestError String`.
- Produces: `builtinSignatureManifest` and proofs of validity and finite binary cardinality.

- [x] **Step 1: Write failing Lean behavior tests**

Add a test module importing the three not-yet-created production modules. It must assert independently that:

```lean
example : builtinSignatureManifest.validate = .ok () := by decide
example : builtinSignatureManifest.binary.length = 2 := by decide
example : builtinSignatureManifest.binary.length < 5 ^ 3 := by decide
example : duplicateBinaryManifest.validate = .error .duplicateBinary := by decide
example : unknownBinaryTypeManifest.validate = .error .unknownType := by decide
```

Also render the valid manifest and assert that the result contains the literal macros
`CMETA_BUILTIN_TYPE_LIST` and `CMETA_BUILTIN_BINARY_RELATION_LIST`.

- [x] **Step 2: Run Lean RED verification**

Run from `formal/cmeta_cflow_calculus`:

```text
lake test
```

Expected: FAIL because `CMeta.SignatureManifest`, `BuiltinSignatures`, and
`SignatureHeader` do not exist.

- [x] **Step 3: Implement the minimal validated model**

Implement the structures from the spec with `DecidableEq`/`BEq`, deterministic duplicate
checks, non-empty checks, and referential-closure checks. Define:

```lean
def SignatureManifest.WellFormed (manifest : SignatureManifest) : Prop :=
  manifest.validate = .ok ()
```

Populate the five existing type rows and the exact 8 unary, 2 binary, and 1 generator
relations. Prove:

```lean
theorem builtinSignatureManifest_wellFormed :
    builtinSignatureManifest.WellFormed := by decide

theorem builtinBinaryRelations_finite :
    builtinSignatureManifest.binary.length <
      builtinSignatureManifest.types.length ^ 3 := by decide
```

- [x] **Step 4: Implement deterministic header rendering**

Render the existing row/list order, include guard, generated-file notice, and four literal
count macros. `SignatureHeader.render` must call `validate` first and return the same
`ManifestError` without emitting partial text.

- [x] **Step 5: Run Lean GREEN verification**

Run `lake test` and confirm every Lean library/test target passes.

### Task 2: Generator CLI and C consumer integration

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaSignatureGen.lean`
- Create: `cmeta/include/cmeta/generated/builtin_signature_manifest.h`
- Create: `cmeta/tests/cmeta_signature_manifest_test.c`
- Modify: `formal/cmeta_cflow_calculus/lakefile.toml`
- Modify: `cmeta/include/cmeta/types.h`
- Modify: `cmeta/include/cmeta/relations.h`
- Modify: `cmeta/tests/CMakeLists.txt`

**Interfaces:**
- Produces executable commands: `--stdout`, `--write <path>`, `--check <path>`.
- Produces installed header macros listed in the design spec.
- Consumes the generated type rows in `types.h` and relation rows in `relations.h`.

- [x] **Step 1: Write the failing C behavior test**

Register `cmeta_signature_manifest_test.c` with `cmake_add_test`. The TinyTest suite must
verify the generated cardinalities `5/8/2/1`, default `CMETA_SIG_COUNT == 12`, and runtime
symbol lookup for `CMETA_SIG_B_L_L_L` and `CMETA_SIG_B_L_D_D`.

- [x] **Step 2: Verify C RED**

Build target `cmeta_signature_manifest_test` with the release Windows preset.

Expected: compilation fails because `CMETA_BUILTIN_*_COUNT` macros are not defined.

- [x] **Step 3: Register the missing generator executable and verify CLI RED**

Add this Lake target before creating its root module:

```toml
[[lean_exe]]
name = "cmeta-signature-gen"
root = "CMetaSignatureGen"
```

Run `lake exe cmeta-signature-gen --stdout`.

Expected: FAIL because `CMetaSignatureGen.lean` does not exist.

- [x] **Step 4: Implement the CLI and generate the checked-in header**

Implement exact argument handling:

```text
--stdout          write the complete header to stdout
--write <path>    replace the target with the complete validated header
--check <path>    return 0 only when the target exactly matches rendered output
```

Unknown/missing arguments return 2; manifest/render/file mismatch returns 1. Generate the
header with `--write`; do not hand-edit it.

- [x] **Step 5: Replace duplicate C facts with the generated header**

After defining `CMETA_BOOL_TYPE`, make `types.h` include
`<cmeta/generated/builtin_signature_manifest.h>` and remove its five hand-written rows and
`CMETA_BUILTIN_TYPE_LIST`. Remove the hand-written built-in relation lists and redundant
row fallbacks from `relations.h`; retain all user hooks and combined lists unchanged.

- [x] **Step 6: Run integrated GREEN verification**

Run:

```text
lake exe cmeta-signature-gen --check ../../cmeta/include/cmeta/generated/builtin_signature_manifest.h
cmake --build --preset build-release-windows --target cmeta_signature_manifest_test cmeta_core_test cmeta_header_cpp_test
ctest --preset test-release-windows -R "^cmeta_(signature_manifest|core|header_cpp)_test$" --output-on-failure
```

Expected: generator check succeeds and all three C/C++ tests pass.

### Task 3: Drift CI, documentation, and regression

**Files:**
- Modify: `.github/workflows/cmeta-cflow-calculus.yml`
- Modify: `cmeta/LANGUAGE_REFERENCE.md`
- Modify: `cmeta/README.md`

**Interfaces:**
- CI consumes `lake exe cmeta-signature-gen --check`.
- Documentation exposes the generated-artifact ownership and regeneration command.

- [x] **Step 1: Add CI drift verification**

Extend workflow path filters to the generated header, `types.h`, and `relations.h`. After
the Lean action, run the generator `--check` command from the Lake package directory.

- [x] **Step 2: Document source-of-truth and regeneration**

Document that built-in rows/relations come from `BuiltinSignatures.lean`, generated files
must not be edited, ordinary C builds do not require Lean, and custom relation macros remain
manual application configuration.

- [x] **Step 3: Run full verification**

Run `lake test`, the generator drift check, build all CMeta test targets, and run
`ctest --preset test-release-windows -R "^cmeta_" --output-on-failure`.

Run proof hygiene and diff checks:

```text
rg.exe -n "\b(sorry|admit|axiom)\b" formal/cmeta_cflow_calculus
git diff --check
codegraph affected -p . <all-modified-production-files>
```

Expected: Lean and C/C++ tests pass, drift check succeeds, proof escape search returns no
matches, and the diff has no whitespace errors.

- [ ] **Step 4: Commit and open a PR from master**

Commit with `feat(cmeta): derive finite signatures from Lean manifest`, push
`feat/cmeta-lean-signature-manifest`, and create a PR targeting `master`. Preserve the
worktree for review fixes.
