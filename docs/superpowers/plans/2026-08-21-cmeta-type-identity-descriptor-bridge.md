# CMeta Type Identity Descriptor Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Applicability Gate A by attaching structural `TypeId` metadata to Core descriptors, enforcing three-mode descriptor equality, and proving the bridge with adversarial, multi-TU, C→Lean and full CFlow regression evidence.

**Architecture:** `cmeta_type_desc` remains the runtime descriptor surface and gains an optional `identity` pointer. Structural descriptors compare only through `TypeId`; legacy descriptors compare through the existing representation fallback; mixed structural/legacy descriptors are deliberately unequal. The bridge covers Core built-ins and structural pointer descriptors only in this slice; project-defined atoms and generic/container application descriptors remain legacy until later gates.

**Tech Stack:** strict C11, CMake/CTest, Lean 4.30.0, existing CMeta TypeId Core, GitHub Actions formal conformance workflow.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-type-identity-applicability-design.md`  
**Parent architecture:** `docs/superpowers/specs/2026-08-21-cmeta-hexagonal-architecture-design.md`

## Global Constraints

- Core MUST remain usable with an ordinary C11 compiler and MUST NOT depend on Extend, State, Exec, CFlow, minicoro, or OS APIs.
- Descriptor address, display name, generated symbol and layout MUST NOT override a structural TypeId.
- Equality modes are fixed: structural/structural → TypeId; legacy/legacy → legacy comparison; structural/legacy → false.
- Pointer descriptors may be structural only when their pointee descriptors are structural and the pointer TypeId base matches the pointee TypeId.
- Project/user atom descriptors remain legacy in Gate A unless they already have an explicit stable semantic ID source.
- Existing CFlow behavior and conformance snapshots MUST remain unchanged.
- Tests use C/CMake/CTest and Lean; no Python harness.
- No Lean proof file may contain `axiom`, `constant`, `sorry`, or `admit`.

---

## File Map

**Create:**
- `formal/cmeta_descriptor_bridge_conformance_gen.c` — single-TU adversarial and equality-mode C witness; emits Lean snapshot.
- `formal/type_identity_tu_shared.h` — declarations shared by multi-TU witness files.
- `formal/type_identity_tu_a.c` — TU-local descriptor/identity A.
- `formal/type_identity_tu_b.c` — TU-local descriptor/identity B plus negative type.
- `formal/type_identity_multi_tu.c` — executable proving address-independent equality.
- `formal/CMeta/DescriptorBridgeGeneratedC.lean` — checked-in generated C results.
- `formal/CMeta/DescriptorBridge.lean` — formal `LegacyDesc` / `DescriptorView` model and equality laws.
- `formal/CMeta/DescriptorBridgeConformance.lean` — C snapshot ↔ Lean model checks.

**Modify:**
- `cmeta/include/cmeta/cmeta.h` — append `identity` to `cmeta_type_desc`; expose descriptor validation/accessor APIs.
- `cmeta/src/cmeta.c` — designated descriptor initialization, built-in atom/pointer TypeIds, bridge validation and three-mode equality.
- `cmeta/include/cmeta/container.h` — migrate generated `cmeta_type_desc` positional initializers to designated legacy initializers (`.identity = NULL`).
- `formal/CMakeLists.txt` — add descriptor bridge and multi-TU witnesses.
- `formal/CMeta.lean` — import bridge proof/conformance modules.
- `.github/workflows/lean.yml` — build witnesses and verify generated bridge snapshot.
- `cmeta/README.md` and `cmeta/C_META_CAPABILITIES.md` — update only after the final Gate A CI run is green.

---

### Task 1: Make descriptor initialization field-safe without semantic change

**Files:**
- Modify: `cmeta/src/cmeta.c`
- Modify: `cmeta/include/cmeta/container.h`
- Create: `formal/cmeta_descriptor_bridge_conformance_gen.c`
- Modify: `formal/CMakeLists.txt`

**Interfaces:**
- Consumes existing five-field `cmeta_type_desc`.
- Produces only designated initializers; no equality behavior changes yet.

- [ ] **Step 1: Write a failing initializer-safety witness**

Add `formal/cmeta_descriptor_bridge_conformance_gen.c` that includes real `cmeta/cmeta.h`, constructs one legacy descriptor with designated fields, verifies current legacy equality, and emits a minimal Lean namespace. Add a formal target with `-Wall -Wextra -Wpedantic` inherited through the existing formal preset.

- [ ] **Step 2: Verify the current branch exposes all positional-initializer sites through compilation/audit**

Build the new target plus existing formal targets. The task is not accepted until all `cmeta_type_desc` initializers in `cmeta/src/cmeta.c` and CMeta container descriptor-generation macros are converted to designated form.

- [ ] **Step 3: Convert Core and container `cmeta_type_desc` initializers to designated form**

Use explicit fields:

```c
{
    .name = "...",
    .size = ...,
    .align = ...,
    .kind = ...,
    .pointee = ...
}
```

No semantic changes.

- [ ] **Step 4: Verify the formal build and all existing snapshots remain green**

Run through the existing `formal-linux` workflow targets. Existing snapshots must remain byte-for-byte unchanged.

- [ ] **Step 5: Commit**

`refactor: make CMeta type descriptor initialization field-safe`

---

### Task 2: Add descriptor identity and three-mode equality

**Files:**
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/src/cmeta.c`
- Modify: `cmeta/include/cmeta/container.h`
- Modify: `formal/cmeta_descriptor_bridge_conformance_gen.c`

**Interfaces:**
- Produces:
  - `const cmeta_type_identity *cmeta_type_identity_of(const cmeta_type_desc *desc);`
  - `bool cmeta_type_desc_valid(const cmeta_type_desc *desc);`
  - `cmeta_type_desc.identity`
- Changes `cmeta_type_equal()` exactly according to the approved three-mode rule.

- [ ] **Step 1: Extend the C witness first**

Add assertions for:

```text
legacy/legacy equal          -> true
structural/legacy            -> false
same TypeId/different name   -> true
same layout/different TypeId -> false
```

At this point compilation must fail because `cmeta_type_desc.identity` and the bridge APIs do not exist.

- [ ] **Step 2: Verify RED is caused by the missing bridge API/field**

Use the formal target/CI log and confirm the failure is not unrelated syntax or build breakage.

- [ ] **Step 3: Append `identity` and implement three-mode equality**

```c
typedef struct cmeta_type_desc {
    const char *name;
    size_t size;
    size_t align;
    cmeta_type_kind kind;
    const struct cmeta_type_desc *pointee;
    const cmeta_type_identity *identity;
} cmeta_type_desc;
```

`cmeta_type_equal()`:

```text
both identity != NULL -> cmeta_type_identity_equal
both identity == NULL -> existing legacy comparison
exactly one NULL      -> false
```

All existing generated container descriptors explicitly remain `.identity = NULL` in Gate A.

- [ ] **Step 4: Implement descriptor validation/accessor**

`cmeta_type_identity_of(NULL)` returns `NULL`.

`cmeta_type_desc_valid()` validates a non-null structural identity. For a structural pointer descriptor it additionally requires `kind == CMETA_T_POINTER`, non-null `pointee`, structural pointee identity, pointer-form TypeId and equality between pointer base and pointee TypeId.

Legacy descriptors continue to be valid under existing metadata assumptions.

- [ ] **Step 5: Verify GREEN and existing CFlow snapshots**

The bridge witness and all existing formal C targets must build. Existing CFlow snapshots must not change.

- [ ] **Step 6: Commit**

`feat: bridge CMeta descriptors to structural TypeId`

---

### Task 3: Give Core built-ins stable atom and pointer identities

**Files:**
- Modify: `cmeta/src/cmeta.c`
- Modify: `formal/cmeta_descriptor_bridge_conformance_gen.c`

**Interfaces:**
- Stable atom IDs in this slice:
  - `cmeta.void`
  - `cmeta.bool`
  - `cmeta.int`
  - `cmeta.long`
  - `cmeta.float`
  - `cmeta.double`
  - `cmeta.size`
  - `cmeta.gen_status`
- Built-in pointer descriptor identities are `POINTER(base TypeId)`, not atom strings.

- [ ] **Step 1: Add failing assertions for builtin identity**

Require `cmeta_type_identity_of(&cmeta_type_int)` to be `ATOM("cmeta.int")`; require `cmeta_type_int_ptr` to have pointer form whose base equals `cmeta_type_int.identity`; require `int* != long*`.

- [ ] **Step 2: Define immutable built-in TypeId objects and attach them to descriptors**

Use static/const TypeId objects in `cmeta.c`. Do not derive stable IDs from descriptor display strings.

- [ ] **Step 3: Ensure configured/project rows remain legacy by default**

Built-in rows may receive known fixed identities; user rows from `CMETA_USER_KNOWN_TYPE_LIST` MUST NOT acquire identity merely from token/descriptor names.

- [ ] **Step 4: Verify all C witnesses and CFlow regressions**

- [ ] **Step 5: Commit**

`feat: identify CMeta builtin descriptors structurally`

---

### Task 4: Prove multi-TU descriptor applicability

**Files:**
- Create: `formal/type_identity_tu_shared.h`
- Create: `formal/type_identity_tu_a.c`
- Create: `formal/type_identity_tu_b.c`
- Create: `formal/type_identity_multi_tu.c`
- Modify: `formal/CMakeLists.txt`

**Interfaces:**
- TU A and TU B export accessor functions returning addresses of separately allocated descriptors/TypeIds.
- Main executable asserts semantic equality independent of those addresses.

- [ ] **Step 1: Write the multi-TU witness**

The executable must establish:

```c
assert(desc_a() != desc_b());
assert(identity_a() != identity_b());
assert(cmeta_type_identity_equal(identity_a(), identity_b()));
assert(cmeta_type_equal(desc_a(), desc_b()));
assert(!cmeta_type_equal(desc_a(), different_same_layout_desc()));
```

- [ ] **Step 2: Build it as a real multi-source CMake executable linked to `TurboUtils::CMeta`**

- [ ] **Step 3: Verify it executes successfully in CI**

The workflow must execute the binary, not merely compile it.

- [ ] **Step 4: Commit**

`test: prove CMeta descriptor identity across translation units`

---

### Task 5: Formalize descriptor bridge equality and C conformance

**Files:**
- Create: `formal/CMeta/DescriptorBridge.lean`
- Create: `formal/CMeta/DescriptorBridgeGeneratedC.lean`
- Create: `formal/CMeta/DescriptorBridgeConformance.lean`
- Modify: `formal/cmeta_descriptor_bridge_conformance_gen.c`
- Modify: `formal/CMeta.lean`
- Modify: `.github/workflows/lean.yml`

**Interfaces:**
- `DescriptorView` contains `identity : Option TypeId` plus finite legacy representation fields.
- `DescriptorView.semanticEq` implements the same three-mode rule as C.

- [ ] **Step 1: Make the C witness emit computed Bool results**

Emit Lean definitions for legacy equality, mixed isolation, identity dominance, adversarial same-layout inequality, atom equality and pointer equality. Values MUST come from real C API calls, not hard-coded literals.

- [ ] **Step 2: Check in the generated snapshot and add exact `diff` verification to the workflow**

- [ ] **Step 3: Implement Lean `DescriptorView.semanticEq` and laws**

Prove/check with kernel-computable definitions:

```text
reflexivity
symmetry
transitivity for the finite well-formed witness/model
identity dominance
mixed-mode isolation
```

Keep descriptor addresses absent from the formal model.

- [ ] **Step 4: Add C↔Lean conformance checks**

`DescriptorBridgeConformance.lean` must reconstruct corresponding descriptor views and show each generated C Bool equals the Lean-computed Bool.

- [ ] **Step 5: Run `lake build --wfail` with the existing placeholder guard**

- [ ] **Step 6: Commit**

`formal: prove CMeta descriptor TypeId bridge`

---

### Task 6: Close Applicability Gate A

**Files:**
- Modify: `cmeta/README.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`
- Modify: `docs/superpowers/specs/2026-08-21-cmeta-type-identity-applicability-design.md` only to mark Gate A status/evidence; do not weaken later gates.

**Interfaces:** none; documentation reflects verified behavior only.

- [ ] **Step 1: Run the full latest-head formal workflow**

Required green evidence:

```text
C header conformance
all real CFlow witnesses
TypeId snapshot
DescriptorBridge snapshot
multi-TU executable
KnownTypes/CallableSignatures probe
placeholder rejection
Lean 4.30.0
lake build --wfail
```

- [ ] **Step 2: Update documentation only after that run is green**

Document Gate A as implemented; keep Generic Applicability (Gate B), Container Applicability (Gate C), and Extend Applicability (Gate D) explicitly unimplemented.

- [ ] **Step 3: Trigger and verify the documentation commit's workflow as final evidence**

- [ ] **Step 4: Commit**

`docs: close CMeta descriptor bridge applicability gate`

## Completion Boundary

Gate A completion means Core descriptors can safely and provably carry structural identities for the covered built-ins and pointers, while legacy descriptors remain isolated. It does **not** mean current `Option/Result/List/Map` generated C types are structurally identified; those are Gate B/C work.