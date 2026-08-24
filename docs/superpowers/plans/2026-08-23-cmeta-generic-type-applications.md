# CMeta Generic Type Applications and TurboSTL Genericization Implementation Plan

> **Status:** Historical execution record. The described foundation is present
> on `master`; unchecked boxes are retained for traceability and are not an
> active implementation checklist. Current public presets and installed-package
> verification replace the original command examples below where noted.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `TYPE<A...>` a first-class, verifiable CMeta contract and make TurboSTL typed instances expose their real generic constructor and arguments before semantic serialization descriptors are introduced.

**Architecture:** Keep `cmeta_generic_desc` + `cmeta_type_identity(CMETA_TYPE_APPLY)` as the canonical type-language foundation. First tighten and directly test application well-formedness in CMeta. Then add one versioned extension root to `cmeta_container_desc` and let TurboSTL expose `Vec<T>`, `Set<T>`, `Map<K,V>`, etc. as concrete generic applications using the type descriptors already stored on each handle. Container algorithms and ownership remain unchanged.

**Tech Stack:** C11, C++17 public-header compatibility, CMake presets, TinyTest, TurboUtils::CMeta, TurboUtils::STL.

**Spec:** `docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md` plus `docs/superpowers/specs/2026-08-23-serialization-data-binding-generic-foundation-amendment.md`.

## Global Constraints

- `TYPE<A...>` well-formedness is a CMeta concern and must be established before CMeta semantic data descriptors.
- Type identity and operation capability are separate contracts. `Set<T>` may be a well-formed type application even when a concrete Set operation later rejects `T` for missing compare traits.
- Generic identity compares constructor `stable_id` + recursively validated argument identities; pointer equality is never cross-TU type identity.
- No serialization, CSerde, CBind, parser, schema-policy, or format-specific code is introduced by this plan.
- No TurboSTL raw algorithm is rewritten. Genericization changes metadata/introspection only.
- Raw byte containers with no `cmeta_type_desc` binding are not semantic generic applications.
- Existing typed handle metadata (`element_type`, `key_type`, `value_type`) remains the storage of concrete type arguments.
- Add exactly one extension pointer to `cmeta_container_desc`; future construction metadata must extend the versioned extension object instead of repeatedly growing the main descriptor.
- `typed(Option/Pair/Tuple/Result, ...)` already creates real C storage shapes but does not yet attach a complete generic type identity. This plan does not fake identity from C spelling. A later value-generic metadata plan must solve that before semantic `Option<T>` is admitted.
- Therefore field presence (`required`, missing field, default) remains CBind/schema policy. A future semantic OPTIONAL applies only to a real `Option<T>` type identity, not to a field merely allowed to be absent.
- Public CMeta/TurboSTL headers must compile as C11 and C++17.
- Linux and Windows fresh CI are required before each implementation PR is merged.

---

## PR decomposition

This plan is executed as two implementation PRs. PR A is pure CMeta and can be reviewed without TurboSTL. PR B starts only after PR A merges and makes TurboSTL the first real consumer of the generic-application contract. The later semantic-data-descriptor PR is intentionally not part of this plan.

---

## PR A — CMeta generic application contract

### Task 1: Make application well-formedness an explicit public contract

**Files:**
- Modify: `cmeta/include/cmeta/type_identity.h`
- Modify: `cmeta/src/type_identity.c`
- Modify: `cmeta/tests/cmeta_core_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Consumes: existing `cmeta_generic_desc`, `cmeta_type_identity`, `CMETA_TYPE_APPLY`, `CMETA_TYPE_ID_APPLY_INIT`.
- Produces:

```c
bool cmeta_type_application_valid(
    const cmeta_generic_desc *constructor,
    const cmeta_type_identity *const *args,
    size_t arity);
```

`cmeta_type_identity_valid()` must use this function for the APPLY case so there is exactly one application-validity rule.

- [ ] **Step 1: Add a failing generic-application test using synthetic constructors**

In `cmeta/tests/cmeta_core_test.c`, define exact-arity constructors and atom identities:

```c
static const cmeta_generic_desc cmeta_test_box_generic =
    CMETA_GENERIC_DESC_INIT("test.Box", "Box", 1u, 1u, CMETA_GENERIC_VALUE);
static const cmeta_generic_desc cmeta_test_pair_generic =
    CMETA_GENERIC_DESC_INIT("test.Pair", "Pair", 2u, 2u, CMETA_GENERIC_VALUE);

static const cmeta_type_identity cmeta_test_atom_a =
    CMETA_TYPE_ID_ATOM_INIT("test.A");
static const cmeta_type_identity cmeta_test_atom_b =
    CMETA_TYPE_ID_ATOM_INIT("test.B");
```

Add tests:

```c
it("validates TYPE<A...> from constructor arity and recursive arguments") {
    const cmeta_type_identity *box_args[] = {&cmeta_test_atom_a};
    const cmeta_type_identity *pair_args[] = {
        &cmeta_test_atom_a, &cmeta_test_atom_b};

    check_true(cmeta_type_application_valid(
        &cmeta_test_box_generic, box_args, 1u));
    check_true(cmeta_type_application_valid(
        &cmeta_test_pair_generic, pair_args, 2u));
    check_false(cmeta_type_application_valid(
        &cmeta_test_pair_generic, pair_args, 1u));
    check_false(cmeta_type_application_valid(
        &cmeta_test_pair_generic, NULL, 2u));
}
```

Also construct nested `Pair<Box<A>, B>` with `CMETA_TYPE_ID_APPLY_INIT` and verify `cmeta_type_identity_valid()` accepts it.

- [ ] **Step 2: Build the focused CMeta test and verify red**

Run:

```bash
cmake --fresh --preset linux-dev-user
cmake --build --preset linux-dev-user --target cmeta_core_test
```

Expected: compilation or link failure because `cmeta_type_application_valid()` does not exist yet. Preset/toolchain failure is not an acceptable red state.

- [ ] **Step 3: Declare the application validator in `type_identity.h`**

Add immediately after the generic arity helpers:

```c
bool cmeta_type_application_valid(
    const cmeta_generic_desc *constructor,
    const cmeta_type_identity *const *args,
    size_t arity);
```

Do not add trait/capability arguments here. This function proves type-language well-formedness only.

- [ ] **Step 4: Implement one recursive application rule in `type_identity.c`**

Implement:

```c
bool cmeta_type_application_valid(
    const cmeta_generic_desc *constructor,
    const cmeta_type_identity *const *args,
    size_t arity) {
    size_t i;
    if (!cmeta_generic_accepts_arity(constructor, arity))
        return false;
    if (arity != 0u && args == NULL)
        return false;
    for (i = 0u; i < arity; ++i) {
        if (args[i] == NULL || !cmeta_type_identity_valid(args[i]))
            return false;
    }
    return true;
}
```

Refactor the `CMETA_TYPE_APPLY` branch of `cmeta_type_identity_valid()` to reject `stable_atom_id/base`, then delegate constructor/args/arity validation to `cmeta_type_application_valid()`.

- [ ] **Step 5: Add C++ public-header coverage**

In `cmeta/tests/cmeta_header_cpp_test.cpp`, define a two-argument static application using `CMETA_TYPE_ID_APPLY_INIT` and assert:

```cpp
check_true(cmeta_type_identity_valid(&cmeta_cpp_pair_identity));
check_equal(cmeta_type_identity_arity(&cmeta_cpp_pair_identity),
            static_cast<size_t>(2));
check_true(cmeta_type_identity_argument(&cmeta_cpp_pair_identity, 0) ==
           &cmeta_cpp_atom_a);
```

The test must not rely on C `_Generic`.

- [ ] **Step 6: Run focused and complete CMeta regression**

Run:

```bash
cmake --build --preset linux-dev-user --target \
  cmeta_core_test cmeta_header_cpp_test cmeta_language_surface_test
ctest --preset linux-dev-user -R '^cmeta_'
git diff --check
```

Expected: all CMeta tests pass and no whitespace errors are reported.

- [ ] **Step 7: Commit PR A implementation**

```bash
git add \
  cmeta/include/cmeta/type_identity.h \
  cmeta/src/type_identity.c \
  cmeta/tests/cmeta_core_test.c \
  cmeta/tests/cmeta_header_cpp_test.cpp
git commit -m "feat(cmeta): formalize generic type applications"
```

### Task 2: Verify identity equality is structural across translation units

**Files:**
- Modify: `cmeta/tests/cmeta_traits_peer.c`
- Modify: `cmeta/tests/cmeta_core_test.c`

**Interfaces:**
- Consumes: `cmeta_type_identity_equal()` and the application contract from Task 1.
- Produces: a regression proving two separately instantiated `TYPE<A,B>` graphs with equal constructor stable IDs and equal argument identities compare equal without sharing addresses.

- [ ] **Step 1: Add a peer-TU application identity**

In `cmeta/tests/cmeta_traits_peer.c`, define local copies of a constructor and atoms with the same stable IDs as the main test TU and return a pointer to a static APPLY identity through a new test-only function.

- [ ] **Step 2: Add the failing cross-TU equality assertion**

In `cmeta/tests/cmeta_core_test.c`, compare the local application with the peer application and require:

```c
check_true(local != peer);
check_true(local->constructor != peer->constructor);
check_true(cmeta_type_identity_equal(local, peer));
```

- [ ] **Step 3: Run the test**

Run:

```bash
cmake --build --preset linux-dev-user --target cmeta_core_test
ctest --preset linux-dev-user -R '^cmeta_core_test$'
```

Expected: PASS with the existing structural equality implementation. If it fails, fix `cmeta_type_identity_equal()` by stable constructor ID + recursive args, never by pointer identity.

- [ ] **Step 4: Commit the cross-TU proof**

```bash
git add cmeta/tests/cmeta_core_test.c cmeta/tests/cmeta_traits_peer.c
git commit -m "test(cmeta): prove generic identity across translation units"
```

### PR A verification gate

Run fresh Linux configure/build/test:

```bash
cmake --fresh --preset linux-dev-user
cmake --build --preset linux-dev-user
ctest --preset linux-dev-user
git diff --check
```

The PR must also pass the repository Windows CI path. No TurboSTL source should change in PR A.

---

## PR B — CMeta container type extension + TurboSTL genericization

### Task 3: Add one versioned container extension root

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Create: `cmeta/src/container_type.c`
- Modify: `cmeta/CMakeLists.txt`
- Modify: `cmeta/tests/cmeta_collector_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Consumes: PR A `cmeta_type_application_valid()` and existing `cmeta_container_desc`.
- Produces:

```c
typedef const cmeta_type_desc *(*cmeta_container_type_argument_fn)(
    const void *object, size_t index);

typedef struct cmeta_container_type_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_generic_desc *constructor;
    size_t arity;
    cmeta_container_type_argument_fn argument;
} cmeta_container_type_ops;

typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
} cmeta_container_ext;
```

Append exactly one field to `cmeta_container_desc`:

```c
const cmeta_container_ext *ext;
```

Public helpers:

```c
const cmeta_generic_desc *cmeta_container_type_constructor(const void *object);
size_t cmeta_container_type_arity(const void *object);
const cmeta_type_desc *cmeta_container_type_argument(
    const void *object, size_t index);
bool cmeta_container_type_application_valid(const void *object);
```

- [ ] **Step 1: Write failing tests for a synthetic unary container**

Create a synthetic container descriptor whose type extension reports constructor `test.Sequence` with arity 1 and argument `&cmeta_type_int`. Verify constructor, arity, argument lookup and application validity.

Also verify an identical descriptor with a type descriptor whose `identity == NULL` returns false from `cmeta_container_type_application_valid()`.

- [ ] **Step 2: Build `cmeta_collector_test` and verify red**

Run:

```bash
cmake --build --preset linux-dev-user --target cmeta_collector_test
```

Expected: compile failure on missing extension types/helpers.

- [ ] **Step 3: Add version constants and extension types to `range.h`**

Use:

```c
enum {
    CMETA_CONTAINER_TYPE_OPS_ABI_VERSION = 1u,
    CMETA_CONTAINER_EXT_ABI_VERSION = 1u
};
```

The extension root is intentionally introduced now so later construction support can append `construct` to `cmeta_container_ext` under `struct_size`/`abi_version`, rather than growing `cmeta_container_desc` again.

- [ ] **Step 4: Implement container type introspection in `container_type.c`**

`cmeta_container_type_application_valid()` must:

1. obtain `descriptor->ext->type`;
2. validate `struct_size` and ABI versions;
3. validate constructor and exact reported arity;
4. obtain every `cmeta_type_desc` argument;
5. require `cmeta_type_desc_valid(arg)`;
6. require `cmeta_type_identity_of(arg)` and `cmeta_type_identity_valid(identity)`;
7. pass those identities to `cmeta_type_application_valid()`.

Do not inspect traits here.

- [ ] **Step 5: Add `container_type.c` to `TurboUtils::CMeta` and update all CMeta test descriptors with `.ext = NULL` or complete positional tail initialization**

Use designated initializers in new tests. Existing positional initializers must receive a final `NULL` so `-Werror=missing-field-initializers` remains clean.

- [ ] **Step 6: Run CMeta C/C++ regression**

Run:

```bash
cmake --build --preset linux-dev-user --target \
  cmeta_core_test cmeta_collector_test cmeta_header_cpp_test
ctest --preset linux-dev-user -R '^cmeta_'
git diff --check
```

Expected: all CMeta tests pass.

- [ ] **Step 7: Commit the extension root**

```bash
git add \
  cmeta/include/cmeta/range.h \
  cmeta/src/container_type.c \
  cmeta/CMakeLists.txt \
  cmeta/tests/cmeta_collector_test.c \
  cmeta/tests/cmeta_header_cpp_test.cpp
git commit -m "feat(cmeta): expose container generic type applications"
```

### Task 4: Define canonical TurboSTL generic constructors

**Files:**
- Modify: `turbostl/include/turbostl/detail/instance_meta.h`
- Modify: `turbostl/src/instance_meta.c`
- Modify: `turbostl/src/list.c`
- Modify: `turbostl/src/map.c`
- Modify: `turbostl/src/associative_meta.c`

**Interfaces:**
- Consumes: `cmeta_generic_desc`, `cmeta_container_ext`, `cmeta_container_type_ops`.
- Produces canonical constructor identities with these stable IDs and exact arities:

```text
turbostl.Vec        1
turbostl.Deque      1
turbostl.List       1
turbostl.Stack      1
turbostl.Queue      1
turbostl.Heap       1
turbostl.Set        1
turbostl.HashSet    1
turbostl.HashMap    2
turbostl.Map        2
turbostl.MultiMap   2
turbostl.BTree      2
turbostl.BPlusTree  2
```

All use `CMETA_GENERIC_CONTAINER`.

- [ ] **Step 1: Add failing TurboSTL metadata tests**

In `turbostl/tests/turbostl_header_typed_test.c`, create at least:

```c
Vec(int, values);
Set(int, unique_values);
Map(int, long, counts);
HashMap(int, long, lookup);
```

Before initialization, assert:

```c
check_true(cmeta_container_type_application_valid(&values));
check_equal(cmeta_container_type_arity(&values), 1u);
check_equal(cmeta_container_type_constructor(&values)->stable_id,
            "turbostl.Vec");
check_true(cmeta_type_equal(
    cmeta_container_type_argument(&values, 0u), &cmeta_type_int));

check_true(cmeta_container_type_application_valid(&counts));
check_equal(cmeta_container_type_arity(&counts), 2u);
check_true(cmeta_type_equal(
    cmeta_container_type_argument(&counts, 0u), &cmeta_type_int));
check_true(cmeta_type_equal(
    cmeta_container_type_argument(&counts, 1u), &cmeta_type_long));
```

Add the same constructor/arity assertions for every supported TurboSTL generic kind, using one or two representative built-in argument types.

- [ ] **Step 2: Build typed header tests and verify red**

Run:

```bash
cmake --build --preset linux-dev-user --target turbostl_header_typed_test
```

Expected: type application is unavailable/invalid because TurboSTL descriptors have no type extension yet.

- [ ] **Step 3: Publish canonical constructor descriptors**

Declare the 13 constructor descriptors in `turbostl/include/turbostl/detail/instance_meta.h` and define them once in compiled TurboSTL code with `CMETA_GENERIC_DESC_INIT(...)`.

`instance_meta.c` may own the constructor objects; `list.c`, `map.c`, and `associative_meta.c` reference them through the detail header. Do not make per-user-type constructor objects.

- [ ] **Step 4: Add unary type-argument adapters**

For Vec/Deque/List/Stack/Queue/Heap/Set/HashSet, provide a type-ops object whose argument callback returns the handle's existing element descriptor for index 0 and NULL otherwise.

The adapter must work before allocation for declarations such as `Vec(int, values)` because the declaration already binds `element_type`.

- [ ] **Step 5: Add binary type-argument adapters**

For HashMap/Map/MultiMap/BTree/BPlusTree, index 0 returns `key_type`, index 1 returns `value_type`, and any other index returns NULL.

Do not infer generic arguments from Range entry types.

- [ ] **Step 6: Attach a versioned extension to every canonical container descriptor**

Every canonical descriptor must point to a `cmeta_container_ext` with the correct type ops. Keep all existing Range/Collector function pointers unchanged.

For generated/header-local CMeta test containers that are not TurboSTL, `.ext = NULL` remains valid and means “container capabilities exist but no generic type application is exposed.”

- [ ] **Step 7: Prove raw byte containers are not falsely typed**

Add a regression using zeroed `vec_t raw = {0}; vec_init_bytes(...)`. It may have normal raw-vector behavior, but:

```c
check_false(cmeta_container_type_application_valid(&raw));
```

A typed `Vec(int, typed_values)` must stay valid before init, after successful init, and after `vec_destroy()` because declaration metadata is preserved.

- [ ] **Step 8: Keep capability validation separate**

Add a test type with a valid atom identity but missing compare/hash traits. Its `Set<T>` / `HashSet<T>` generic type application is well-formed, while `set_init()` / `hash_set_init()` continues to fail with the existing trait error. This proves “type exists” and “operation is admissible” are different contracts.

- [ ] **Step 9: Run focused TurboSTL regression**

Run:

```bash
cmake --build --preset linux-dev-user --target \
  turbostl_header_typed_test \
  turbostl_sequence_test \
  turbostl_list_test \
  turbostl_map_test \
  turbostl_hash_test
ctest --preset linux-dev-user -R '^turbostl_'
git diff --check
```

Expected: all TurboSTL tests pass and existing container behavior is unchanged.

- [ ] **Step 10: Commit TurboSTL genericization**

```bash
git add \
  turbostl/include/turbostl/detail/instance_meta.h \
  turbostl/src/instance_meta.c \
  turbostl/src/list.c \
  turbostl/src/map.c \
  turbostl/src/associative_meta.c \
  turbostl/tests/turbostl_header_typed_test.c
git commit -m "feat(turbostl): expose generic container identities"
```

### Task 5: C++ and installed-header compatibility

**Files:**
- Modify: `turbostl/tests/turbostl_header_typed_cpp_test.cpp`
- Modify if required by the existing install test: files under `tests/install_consumer/`

- [ ] **Step 1: Add C++ descriptor/introspection coverage without `_Generic` expansion**

The C++ test consumes the public `cmeta_container_ext`, `cmeta_container_type_ops`, and constructor descriptor types directly. Do not require expansion of C-only declaration macros if those macros are not already part of the C++ public surface.

- [ ] **Step 2: Build C++ and the repository install-consumer target**

Run the existing C++ TurboSTL header test, then use the root
`verify_installed_package` target. That target installs the package and
configures/builds `tests/install_consumer` against the installed public surface.

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --target \
  turbostl_header_typed_cpp_test verify_installed_package
```

Expected: C++17 headers compile and installed public headers contain all required CMeta declarations.

- [ ] **Step 3: Commit compatibility tests**

```bash
git add turbostl/tests/turbostl_header_typed_cpp_test.cpp tests/install_consumer
git commit -m "test(turbostl): cover generic metadata consumers"
```

### PR B verification gate

Run fresh Linux full build/test:

```bash
cmake --fresh --preset linux-dev-user
cmake --build --preset linux-dev-user
ctest --preset linux-dev-user
git diff --check
```

Then require fresh Windows CI.

Audit the public surface:

```bash
rg -n "cserde|cbind|json|yaml|xml|csv|DataBind|TbeTyped" \
  cmeta/include/cmeta/type_identity.h \
  cmeta/include/cmeta/range.h \
  turbostl/include/turbostl/detail/instance_meta.h
```

Expected: no serialization/parser/data-binding dependencies.

---

## Gate before semantic data descriptors

Only after PR A and PR B are merged may the semantic-data-descriptor plan be regenerated. That next plan must derive container meaning from the proven generic application instead of duplicating element/key/value declarations.

The intended mapping is:

```text
Vec<T>/Deque<T>/List<T>/Stack<T>/Queue<T>/Heap<T> -> sequence-like semantic shape
Set<T>/HashSet<T>                                 -> SET semantic shape
HashMap<K,V>/Map<K,V>/BTree<K,V>/BPlusTree<K,V> -> MAP semantic shape
MultiMap<K,V>                                     -> explicit multimap policy/shape decision
```

Before semantic OPTIONAL is admitted, create a separate value-generic metadata plan for CMeta's existing `typed(Option/Pair/Tuple/Result, ...)` storage types. That plan must produce real `CMETA_TYPE_APPLY` identities without deriving stable identity from raw C spelling. `Option<T>` may then map to semantic OPTIONAL; `Result<A,E>` may map to VARIANT. Field presence remains a separate CBind/schema concern regardless of that future work.

No semantic descriptor should carry a second copy of generic argument truth when it can be derived from the validated type application.
