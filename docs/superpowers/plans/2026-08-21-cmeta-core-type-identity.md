# CMeta Core Type Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first executable slice of CMeta Core type identity: structural `TypeId`, finite `GenericConstructor` semantics, explicit `KnownTypes` versus `CallableSignatures`, and C/Lean conformance, without introducing parser/frontend dependencies or changing `cmeta_type_desc` ABI yet.

**Architecture:** This plan implements the semantic kernel portion of the approved Type Application architecture. `TypeId` is an immutable structural value independent of aliases, generated C symbols, descriptor addresses and CMeta Extend ASTs. The existing `cmeta_type_desc`/`cmeta_type_equal()` path remains unchanged in this slice; a separate descriptor-bridge plan will integrate TypeId after positional initializer migration is audited.

**Tech Stack:** strict C11, CMake/CTest, Lean 4.30.0, existing CMeta finite macro kernel, GitHub Actions formal conformance workflow.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-type-application-design.md`  
**Parent architecture:** `docs/superpowers/specs/2026-08-21-cmeta-hexagonal-architecture-design.md`

## Global Constraints

- CMeta Core MUST remain buildable and usable with an ordinary C11 compiler.
- Core MUST NOT depend on CMeta Extend, re2c, State, Exec, CFlow, minicoro or OS APIs.
- Type identity MUST NOT depend on descriptor pointer equality, generated symbol names, source aliases or source whitespace.
- Generic constructors are finite registered semantic factories, not user-programmable compile-time templates.
- v1 generic parameters are TYPE-only.
- `KnownTypes` and `CallableSignatures` MUST be separate concepts; making a type known MUST NOT automatically expand all callable Cartesian products.
- Existing `CMETA_TYPE_LIST` configuration MUST remain source-compatible during this migration.
- This plan MUST NOT change the layout of `cmeta_type_desc` or the semantics of `cmeta_type_equal()`.
- Tests use CTest/C executables and Lean kernel checking; no Python test harness is introduced.
- No proof file may use `axiom`, `constant`, `sorry` or `admit`.

---

## File Map

**Create:**
- `cmeta/include/cmeta/type_identity.h` — public Core TypeId/GenericConstructor semantic API.
- `cmeta/src/type_identity.c` — structural validation/equality/reflection implementation.
- `formal/cmeta_type_identity_conformance_gen.c` — real C witness and generated Lean snapshot producer.
- `formal/cmeta_type_universe_probe.c` — compile/runtime probe proving KnownTypes do not inflate the callable universe.
- `formal/CMeta/TypeIdentity.lean` — formal structural TypeId model.
- `formal/CMeta/TypeIdentityGeneratedC.lean` — checked-in output snapshot from the C witness.
- `formal/CMeta/TypeIdentityConformance.lean` — C ↔ Lean refinement checks.

**Modify:**
- `cmeta/CMakeLists.txt` — compile `src/type_identity.c` into `TurboUtils::CMeta`.
- `cmeta/include/cmeta/types.h` — split known-type and callable-type list macros while preserving `CMETA_TYPE_LIST` compatibility.
- `cmeta/include/cmeta/signatures.h` — use `CMETA_CALLABLE_TYPE_LIST` for full/balanced Cartesian generation.
- `cmeta/include/cmeta/cmeta.h` — include the TypeId API; use `CMETA_KNOWN_TYPE_LIST` only where declaring configured concrete type descriptors.
- `cmeta/src/cmeta.c` — use `CMETA_KNOWN_TYPE_LIST` for configured descriptor definitions/registry only; callable tables remain driven by signature policy.
- `formal/CMakeLists.txt` — build/register the two new C witnesses.
- `formal/CMeta.lean` — import TypeIdentity and conformance modules.
- `.github/workflows/lean.yml` — build and verify the TypeIdentity snapshot.
- `cmeta/README.md` — document implemented TypeId Core after verification.
- `cmeta/C_META_CAPABILITIES.md` — move TypeId/known-vs-callable split from architecture direction to implemented capabilities after verification.

---

### Task 1: Add an independent structural TypeId Core API

**Files:**
- Create: `cmeta/include/cmeta/type_identity.h`
- Create: `cmeta/src/type_identity.c`
- Modify: `cmeta/CMakeLists.txt`
- Create: `formal/cmeta_type_identity_conformance_gen.c`
- Modify: `formal/CMakeLists.txt`

**Interfaces:**
- Consumes: only libc `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `<string.h>`.
- Produces:
  - `cmeta_type_form`
  - `cmeta_generic_category`
  - `cmeta_generic_desc`
  - `cmeta_type_identity`
  - `cmeta_generic_desc_valid()`
  - `cmeta_generic_accepts_arity()`
  - `cmeta_type_identity_valid()`
  - `cmeta_type_identity_equal()`
  - application reflection helpers.

- [ ] **Step 1: Write the failing C witness**

Create `formal/cmeta_type_identity_conformance_gen.c` with the first atom-only assertions:

```c
#include <cmeta/type_identity.h>

#include <assert.h>
#include <stdio.h>

static const cmeta_type_identity user_a =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity user_b =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity error_id =
    CMETA_TYPE_ID_ATOM_INIT("app.Error");

int main(void) {
    assert(cmeta_type_identity_valid(&user_a));
    assert(cmeta_type_identity_equal(&user_a, &user_b));
    assert(!cmeta_type_identity_equal(&user_a, &error_id));

    puts("namespace CMeta.TypeIdentityGeneratedC");
    puts("def atomAliasEqual : Bool := true");
    puts("def atomDifferent : Bool := false");
    puts("end CMeta.TypeIdentityGeneratedC");
    return 0;
}
```

Add to `formal/CMakeLists.txt`:

```cmake
add_executable(cmeta_type_identity_conformance_gen EXCLUDE_FROM_ALL
  cmeta_type_identity_conformance_gen.c)
target_compile_features(cmeta_type_identity_conformance_gen PRIVATE c_std_11)
target_link_libraries(cmeta_type_identity_conformance_gen PRIVATE TurboUtils::CMeta)
set_target_properties(cmeta_type_identity_conformance_gen PROPERTIES FOLDER "formal")
add_test(NAME cmeta_type_identity_conformance
         COMMAND cmeta_type_identity_conformance_gen)
```

- [ ] **Step 2: Build to verify the witness fails before the API exists**

Run:

```bash
cmake --preset formal-linux
cmake --build --preset build-formal-linux --target cmeta_type_identity_conformance_gen
```

Expected: build failure because `<cmeta/type_identity.h>` and the TypeId symbols do not exist yet.

- [ ] **Step 3: Define the public TypeId API**

Create `cmeta/include/cmeta/type_identity.h` with this semantic shape:

```c
#ifndef CMETA_TYPE_IDENTITY_H
#define CMETA_TYPE_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cmeta_type_form {
    CMETA_TYPE_ATOM,
    CMETA_TYPE_POINTER,
    CMETA_TYPE_CONST,
    CMETA_TYPE_APPLY
} cmeta_type_form;

typedef enum cmeta_generic_category {
    CMETA_GENERIC_VALUE,
    CMETA_GENERIC_CONTAINER,
    CMETA_GENERIC_HANDLE
} cmeta_generic_category;

typedef struct cmeta_generic_desc {
    const char *stable_id;
    const char *display_name;
    uint8_t min_arity;
    uint8_t max_arity;
    cmeta_generic_category category;
} cmeta_generic_desc;

typedef struct cmeta_type_identity cmeta_type_identity;
struct cmeta_type_identity {
    cmeta_type_form form;
    const char *stable_atom_id;
    const cmeta_generic_desc *constructor;
    const cmeta_type_identity *base;
    const cmeta_type_identity *const *args;
    size_t arity;
};

#define CMETA_GENERIC_DESC_INIT(id_, display_, min_, max_, category_) \
    { (id_), (display_), (uint8_t)(min_), (uint8_t)(max_), (category_) }
#define CMETA_TYPE_ID_ATOM_INIT(id_) \
    { CMETA_TYPE_ATOM, (id_), NULL, NULL, NULL, 0u }
#define CMETA_TYPE_ID_POINTER_INIT(base_) \
    { CMETA_TYPE_POINTER, NULL, NULL, (base_), NULL, 0u }
#define CMETA_TYPE_ID_CONST_INIT(base_) \
    { CMETA_TYPE_CONST, NULL, NULL, (base_), NULL, 0u }
#define CMETA_TYPE_ID_APPLY_INIT(constructor_, args_) \
    { CMETA_TYPE_APPLY, NULL, (constructor_), NULL, (args_), \
      sizeof(args_) / sizeof((args_)[0]) }

bool cmeta_generic_desc_valid(const cmeta_generic_desc *desc);
bool cmeta_generic_accepts_arity(const cmeta_generic_desc *desc, size_t arity);
bool cmeta_type_identity_valid(const cmeta_type_identity *identity);
bool cmeta_type_identity_equal(const cmeta_type_identity *a,
                               const cmeta_type_identity *b);
bool cmeta_type_identity_is_application(const cmeta_type_identity *identity);
const cmeta_generic_desc *
cmeta_type_identity_constructor(const cmeta_type_identity *identity);
size_t cmeta_type_identity_arity(const cmeta_type_identity *identity);
const cmeta_type_identity *
cmeta_type_identity_argument(const cmeta_type_identity *identity, size_t index);

#ifdef __cplusplus
}
#endif
#endif
```

The identity graph is immutable and MUST be finite/acyclic by construction. `cmeta_type_identity_valid()` validates node shape and constructor arity; it does not allocate or mutate a global registry.

- [ ] **Step 4: Implement atom/pointer/const structural equality**

Create `cmeta/src/type_identity.c` with direct structural comparison using `strcmp()` for stable IDs and recursion for `base`. Constructor descriptor addresses are not compared in this step.

For atom equality, the required implementation rule is:

```c
return a->stable_atom_id && b->stable_atom_id &&
       strcmp(a->stable_atom_id, b->stable_atom_id) == 0;
```

For pointer/const equality, require equal form and recursively equal `base`.

- [ ] **Step 5: Add the source to CMeta and verify the first witness passes**

Modify `cmeta/CMakeLists.txt`:

```cmake
add_library(${TARGET_NAME}
  src/cmeta.c
  src/type_identity.c)
```

Run:

```bash
cmake --build --preset build-formal-linux --target cmeta_type_identity_conformance_gen
ctest --test-dir build/formal-linux -R cmeta_type_identity_conformance --output-on-failure
```

Expected: build succeeds; CTest reports `cmeta_type_identity_conformance` passed.

- [ ] **Step 6: Commit**

```bash
git add cmeta/include/cmeta/type_identity.h cmeta/src/type_identity.c \
        cmeta/CMakeLists.txt formal/cmeta_type_identity_conformance_gen.c \
        formal/CMakeLists.txt
git commit -m "feat(cmeta): add structural type identity core"
```

---

### Task 2: Add finite GenericConstructor and application semantics

**Files:**
- Modify: `cmeta/include/cmeta/type_identity.h`
- Modify: `cmeta/src/type_identity.c`
- Modify: `formal/cmeta_type_identity_conformance_gen.c`

**Interfaces:**
- Consumes: Task 1 TypeId API.
- Produces: constructor validation, arity admission, application equality by stable constructor ID, and application reflection.

- [ ] **Step 1: Extend the C witness with two distinct constructor objects sharing one semantic ID**

Add:

```c
static const cmeta_generic_desc result_ctor_a =
    CMETA_GENERIC_DESC_INIT("cmeta.Result", "Result", 2, 2, CMETA_GENERIC_VALUE);
static const cmeta_generic_desc result_ctor_b =
    CMETA_GENERIC_DESC_INIT("cmeta.Result", "Result", 2, 2, CMETA_GENERIC_VALUE);

static const cmeta_type_identity *const result_args_a[] = { &user_a, &error_id };
static const cmeta_type_identity *const result_args_b[] = { &user_b, &error_id };
static const cmeta_type_identity *const reversed_args[] = { &error_id, &user_a };

static const cmeta_type_identity result_a =
    CMETA_TYPE_ID_APPLY_INIT(&result_ctor_a, result_args_a);
static const cmeta_type_identity result_b =
    CMETA_TYPE_ID_APPLY_INIT(&result_ctor_b, result_args_b);
static const cmeta_type_identity result_reversed =
    CMETA_TYPE_ID_APPLY_INIT(&result_ctor_a, reversed_args);
```

Add assertions:

```c
assert(cmeta_generic_desc_valid(&result_ctor_a));
assert(cmeta_generic_accepts_arity(&result_ctor_a, 2));
assert(!cmeta_generic_accepts_arity(&result_ctor_a, 1));
assert(cmeta_type_identity_equal(&result_a, &result_b));
assert(!cmeta_type_identity_equal(&result_a, &result_reversed));
assert(cmeta_type_identity_is_application(&result_a));
assert(cmeta_type_identity_arity(&result_a) == 2u);
assert(cmeta_type_identity_argument(&result_a, 0u) == &user_a);
```

- [ ] **Step 2: Run CTest and verify the new assertions fail**

Run:

```bash
cmake --build --preset build-formal-linux --target cmeta_type_identity_conformance_gen
ctest --test-dir build/formal-linux -R cmeta_type_identity_conformance --output-on-failure
```

Expected: compile/link/runtime failure until application semantics are implemented.

- [ ] **Step 3: Implement constructor/application rules**

In `cmeta/src/type_identity.c`:

- constructor is valid only when `stable_id` and `display_name` are non-empty and `min_arity <= max_arity`;
- arity admission is inclusive `[min_arity, max_arity]`;
- application node is valid only when constructor is valid, arity is admitted, and `args[i] != NULL` for every argument;
- two applications are equal when constructor `stable_id` strings match, arities match, and every argument TypeId is structurally equal in order;
- constructor pointer equality is only a fast path, never the semantic rule.

- [ ] **Step 4: Re-run CTest**

```bash
cmake --build --preset build-formal-linux --target cmeta_type_identity_conformance_gen
ctest --test-dir build/formal-linux -R cmeta_type_identity_conformance --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add cmeta/include/cmeta/type_identity.h cmeta/src/type_identity.c \
        formal/cmeta_type_identity_conformance_gen.c
git commit -m "feat(cmeta): add finite generic application identity"
```

---

### Task 3: Separate KnownTypes from CallableSignatures without breaking existing configuration

**Files:**
- Modify: `cmeta/include/cmeta/types.h`
- Modify: `cmeta/include/cmeta/signatures.h`
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/src/cmeta.c`
- Create: `formal/cmeta_type_universe_probe.c`
- Modify: `formal/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `CMETA_BUILTIN_TYPE_LIST`, `CMETA_USER_TYPE_LIST`, `CMETA_TYPE_LIST` compatibility surface.
- Produces:
  - `CMETA_KNOWN_TYPE_LIST`
  - `CMETA_CALLABLE_TYPE_LIST`
  - unchanged legacy meaning when only `CMETA_TYPE_LIST` is configured.

- [ ] **Step 1: Write a probe that adds a known-only type under the full signature profile**

Create `formal/cmeta_type_universe_probe.c`:

```c
typedef struct known_only_type { int value; } known_only_type;

#define CMETA_SIGNATURE_PROFILE_FULL
#define CMETA_KNOWN_TYPE_LIST \
    CMETA_BUILTIN_TYPE_LIST, \
    (K, known_only_type, cmeta_type_known_only, CMETA_T_OBJECT)
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST

#include <cmeta/cmeta.h>

_Static_assert(CMETA_PP_NARG(CMETA_KNOWN_TYPE_LIST) == 6,
               "known type list must include the extra reflected type");
_Static_assert(CMETA_PP_NARG(CMETA_CALLABLE_TYPE_LIST) == 5,
               "callable list must remain the five builtins");
_Static_assert(CMETA_SIG_COUNT == 176,
               "five callable types in full profile must remain 25+125+25 signatures plus invalid");

int main(void) { return 0; }
```

Add to `formal/CMakeLists.txt`:

```cmake
add_executable(cmeta_type_universe_probe EXCLUDE_FROM_ALL
  cmeta_type_universe_probe.c)
target_compile_features(cmeta_type_universe_probe PRIVATE c_std_11)
target_include_directories(cmeta_type_universe_probe PRIVATE
  ${PROJECT_SOURCE_DIR}/cmeta/include)
set_target_properties(cmeta_type_universe_probe PROPERTIES FOLDER "formal")
add_test(NAME cmeta_type_universe_probe COMMAND cmeta_type_universe_probe)
```

- [ ] **Step 2: Build and verify the probe fails before the macro split**

```bash
cmake --build --preset build-formal-linux --target cmeta_type_universe_probe
```

Expected: compile failure because the new list macros are not wired into the headers.

- [ ] **Step 3: Add compatibility-preserving list selection in `types.h`**

Use this precedence:

```c
#ifndef CMETA_KNOWN_TYPE_LIST
#  ifdef CMETA_TYPE_LIST
#    define CMETA_KNOWN_TYPE_LIST CMETA_TYPE_LIST
#  else
#    define CMETA_KNOWN_TYPE_LIST \
        CMETA_BUILTIN_TYPE_LIST CMETA_USER_TYPE_LIST
#  endif
#endif

#ifndef CMETA_CALLABLE_TYPE_LIST
#  ifdef CMETA_TYPE_LIST
#    define CMETA_CALLABLE_TYPE_LIST CMETA_TYPE_LIST
#  else
#    define CMETA_CALLABLE_TYPE_LIST CMETA_KNOWN_TYPE_LIST
#  endif
#endif

#ifndef CMETA_TYPE_LIST
#  define CMETA_TYPE_LIST CMETA_CALLABLE_TYPE_LIST
#endif
```

This preserves existing projects that define only `CMETA_TYPE_LIST`, while allowing a larger known universe and smaller callable universe when both new macros are supplied.

- [ ] **Step 4: Route each consumer to the correct universe**

In `cmeta/include/cmeta/signatures.h`, replace full/balanced Cartesian loops over `CMETA_TYPE_LIST` with `CMETA_CALLABLE_TYPE_LIST`.

In `cmeta/include/cmeta/cmeta.h`, descriptor extern declaration replay uses `CMETA_KNOWN_TYPE_LIST`.

In `cmeta/src/cmeta.c`, configured descriptor definitions and `cmeta_type_registry[]` use `CMETA_KNOWN_TYPE_LIST`. Signature typedef/enums/tables continue to use `CMETA_ALL_SIGNATURES`, which is controlled by the callable policy.

- [ ] **Step 5: Verify the split with CTest**

```bash
cmake --build --preset build-formal-linux --target cmeta_type_universe_probe
ctest --test-dir build/formal-linux -R cmeta_type_universe_probe --output-on-failure
```

Expected: PASS and `CMETA_SIG_COUNT == 176` despite six known types.

- [ ] **Step 6: Rebuild existing CMeta/CFlow formal targets to catch ABI macro regressions**

```bash
cmake --build --preset build-formal-linux \
  --target cmeta_plan_conformance_gen \
           cmeta_structured_conformance_gen \
           cmeta_structured_policy_conformance_gen \
           cmeta_optimizer_conformance_gen \
           cmeta_optimizer_gating_conformance_gen \
           cmeta_optimizer_topology_conformance_gen
```

Expected: all targets build successfully.

- [ ] **Step 7: Commit**

```bash
git add cmeta/include/cmeta/types.h cmeta/include/cmeta/signatures.h \
        cmeta/include/cmeta/cmeta.h cmeta/src/cmeta.c \
        formal/cmeta_type_universe_probe.c formal/CMakeLists.txt
git commit -m "refactor(cmeta): separate known and callable type universes"
```

---

### Task 4: Formalize structural TypeId independently from the legacy callable CType universe

**Files:**
- Create: `formal/CMeta/TypeIdentity.lean`
- Modify: `formal/CMeta.lean`

**Interfaces:**
- Consumes: no change to existing `CMeta.Traits.CType` proofs.
- Produces: a separate semantic TypeId model suitable for generic applications and a formal distinction between known types and explicit callable schemas.

- [ ] **Step 1: Add a new Lean TypeId model**

Create `formal/CMeta/TypeIdentity.lean`:

```lean
namespace CMeta

inductive TypeId where
  | atom (stableId : String)
  | pointer (base : TypeId)
  | const (base : TypeId)
  | apply (constructorId : String) (args : List TypeId)
  deriving Repr, DecidableEq

structure GenericConstructor where
  stableId : String
  minArity : Nat
  maxArity : Nat
  deriving Repr, DecidableEq

def GenericConstructor.acceptsArity
    (ctor : GenericConstructor) (arity : Nat) : Bool :=
  ctor.minArity <= arity && arity <= ctor.maxArity

abbrev KnownTypes := List TypeId

inductive CallableSignature where
  | unary (input output : TypeId)
  | binary (left right output : TypeId)
  | generator (input output : TypeId)
  deriving Repr, DecidableEq

abbrev CallableSchema := List CallableSignature

private def resultCtor : GenericConstructor :=
  { stableId := "cmeta.Result", minArity := 2, maxArity := 2 }

example : resultCtor.acceptsArity 2 = true := by native_decide
example : resultCtor.acceptsArity 1 = false := by native_decide

private def user := TypeId.atom "app.User"
private def err := TypeId.atom "app.Error"
private def result := TypeId.apply "cmeta.Result" [user, err]

example : result = TypeId.apply "cmeta.Result" [user, err] := by rfl
example : result != TypeId.apply "cmeta.Result" [err, user] := by native_decide

/-- Adding reflected types does not mutate an explicit callable schema. -/
theorem known_types_do_not_expand_callable_schema
    (known : KnownTypes) (schema : CallableSchema) (t : TypeId) :
    schema = schema := by
  rfl

end CMeta
```

The existing `Traits.CType` stays intact in this task because it models the currently admitted callable subset, not the new complete semantic type universe.

- [ ] **Step 2: Import the model from `formal/CMeta.lean`**

Add:

```lean
import CMeta.TypeIdentity
```

before modules that consume the legacy callable model.

- [ ] **Step 3: Run Lean kernel checking**

```bash
cd formal
lake update
lake build --wfail
```

Expected: PASS with no warnings promoted by `--wfail` and no proof placeholders.

- [ ] **Step 4: Commit**

```bash
git add formal/CMeta/TypeIdentity.lean formal/CMeta.lean
git commit -m "formal: model CMeta structural type identity"
```

---

### Task 5: Connect the real C TypeId implementation to Lean conformance

**Files:**
- Modify: `formal/cmeta_type_identity_conformance_gen.c`
- Create: `formal/CMeta/TypeIdentityGeneratedC.lean`
- Create: `formal/CMeta/TypeIdentityConformance.lean`
- Modify: `formal/CMeta.lean`
- Modify: `.github/workflows/lean.yml`

**Interfaces:**
- Consumes: C TypeId API from Tasks 1–2 and Lean TypeId model from Task 4.
- Produces: checked-in executable-refinement snapshot and CI enforcement.

- [ ] **Step 1: Extend the C generator output to cover application semantics**

After the runtime assertions, emit:

```c
printf("def resultApplicationEqual : Bool := %s\n",
       cmeta_type_identity_equal(&result_a, &result_b) ? "true" : "false");
printf("def resultArgumentOrderEqual : Bool := %s\n",
       cmeta_type_identity_equal(&result_a, &result_reversed) ? "true" : "false");
printf("def resultArityAccepted : Bool := %s\n",
       cmeta_generic_accepts_arity(&result_ctor_a, 2u) ? "true" : "false");
```

Regenerate the checked-in snapshot:

```bash
cmake --build --preset build-formal-linux --target cmeta_type_identity_conformance_gen
build/formal-linux/bin/cmeta_type_identity_conformance_gen \
  > formal/CMeta/TypeIdentityGeneratedC.lean
```

- [ ] **Step 2: Add Lean conformance theorems**

Create `formal/CMeta/TypeIdentityConformance.lean`:

```lean
import CMeta.TypeIdentity
import CMeta.TypeIdentityGeneratedC

namespace CMeta

private def user := TypeId.atom "app.User"
private def err := TypeId.atom "app.Error"
private def resultA := TypeId.apply "cmeta.Result" [user, err]
private def resultB := TypeId.apply "cmeta.Result" [TypeId.atom "app.User", err]
private def resultReversed := TypeId.apply "cmeta.Result" [err, user]

example : TypeIdentityGeneratedC.atomAliasEqual = decide (user = TypeId.atom "app.User") := by
  native_decide

example : TypeIdentityGeneratedC.atomDifferent = decide (user = err) := by
  native_decide

example : TypeIdentityGeneratedC.resultApplicationEqual = decide (resultA = resultB) := by
  native_decide

example : TypeIdentityGeneratedC.resultArgumentOrderEqual = decide (resultA = resultReversed) := by
  native_decide

example : TypeIdentityGeneratedC.resultArityAccepted = true := by
  native_decide

end CMeta
```

Import it from `formal/CMeta.lean`.

- [ ] **Step 3: Add the witness to GitHub Actions**

In `.github/workflows/lean.yml`:

1. add `cmeta_type_identity_conformance_gen` and `cmeta_type_universe_probe` to the formal target build command;
2. add a snapshot verification step:

```bash
build/formal-linux/bin/cmeta_type_identity_conformance_gen \
  > /tmp/TypeIdentityGeneratedC.lean
diff -u formal/CMeta/TypeIdentityGeneratedC.lean \
  /tmp/TypeIdentityGeneratedC.lean
```

- [ ] **Step 4: Run the exact local verification path**

```bash
cmake --preset formal-linux
cmake --build --preset build-formal-linux \
  --target cmeta_type_identity_conformance_gen \
           cmeta_type_universe_probe \
           cmeta_plan_conformance_gen \
           cmeta_structured_conformance_gen \
           cmeta_structured_policy_conformance_gen \
           cmeta_optimizer_conformance_gen \
           cmeta_optimizer_gating_conformance_gen \
           cmeta_optimizer_topology_conformance_gen
ctest --test-dir build/formal-linux -R 'cmeta_type_(identity|universe)' --output-on-failure
build/formal-linux/bin/cmeta_type_identity_conformance_gen \
  > /tmp/TypeIdentityGeneratedC.lean
diff -u formal/CMeta/TypeIdentityGeneratedC.lean \
  /tmp/TypeIdentityGeneratedC.lean
cd formal
lake update
lake build --wfail
```

Expected: every command succeeds and the generated snapshot has no diff.

- [ ] **Step 5: Commit**

```bash
git add formal/cmeta_type_identity_conformance_gen.c \
        formal/CMeta/TypeIdentityGeneratedC.lean \
        formal/CMeta/TypeIdentityConformance.lean \
        formal/CMeta.lean .github/workflows/lean.yml
git commit -m "formal: connect CMeta type identity to Lean"
```

---

### Task 6: Document the implemented boundary and explicitly defer the descriptor bridge

**Files:**
- Modify: `cmeta/README.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`
- Modify: `docs/superpowers/specs/README.md`

**Interfaces:**
- Consumes: verified implementation from Tasks 1–5.
- Produces: accurate capability claims and the next focused subproject boundary.

- [ ] **Step 1: Update `cmeta/README.md`**

Document that Core now implements:

```text
structural TypeId
finite GenericConstructor descriptors
atom/pointer/const/application identity equality
KnownTypes vs CallableSignatures separation
```

State explicitly:

```text
cmeta_type_desc integration remains on the legacy path in this slice.
M<A,B> parsing is still CMeta Extend work.
```

- [ ] **Step 2: Update `C_META_CAPABILITIES.md`**

Move these entries from architecture-only direction into implemented Core capabilities only after all local verification from Task 5 passes.

Keep these as not implemented:

```text
M<A,B> parser/lowering
canonical generated C application backend
cmeta_type_desc -> TypeId bridge
Task/State/Exec modules
```

- [ ] **Step 3: Update the specs index with the next subproject**

In `docs/superpowers/specs/README.md`, record the next focused design/plan boundary as:

```text
CMeta CType / TypeId Descriptor Bridge
```

Its purpose is to migrate descriptor initializers safely, attach semantic identity to `cmeta_type_desc`, and evolve `cmeta_type_equal()` without `-Werror` regressions.

- [ ] **Step 4: Re-run final verification before claiming completion**

```bash
cmake --preset formal-linux
cmake --build --preset build-formal-linux \
  --target cmeta_type_identity_conformance_gen cmeta_type_universe_probe
ctest --test-dir build/formal-linux -R 'cmeta_type_(identity|universe)' --output-on-failure
cd formal
lake build --wfail
```

Expected: PASS. Do not state CI passed until the GitHub Actions run for the resulting commit is actually green.

- [ ] **Step 5: Commit**

```bash
git add cmeta/README.md cmeta/C_META_CAPABILITIES.md \
        docs/superpowers/specs/README.md
git commit -m "docs: record CMeta type identity core boundary"
```

---

## Self-Review Results

**Spec coverage:** This plan covers the Core structural identity, finite constructor semantics, known/callable universe split, formal model and C↔Lean conformance. It intentionally does not implement Extend TypeExpr parsing, symbol mangling, canonical generated C instantiation or the `cmeta_type_desc` identity bridge; those are independently reviewable subprojects under the approved architecture.

**Placeholder scan:** No task contains `TBD`, `TODO`, unspecified error handling or unnamed tests.

**Type consistency:** C and Lean both use the same four semantic forms: atom, pointer, const, apply. Constructor semantic equality is based on stable ID, not object address. The legacy callable `CType` proof universe remains unchanged during this slice.

**Architecture check:** No Core source depends on Extend, CFlow, State, Exec, minicoro or native platform APIs. Formal witnesses depend only on `TurboUtils::CMeta`.
