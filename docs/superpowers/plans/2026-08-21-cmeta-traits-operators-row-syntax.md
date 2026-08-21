# CMeta Traits / Operators Structured Row Syntax Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace positional `Traits(...)` and human-written 15-field CFlow operator rows with structured tuple-row source syntax while preserving the existing normalized descriptors, `Replay(CFlowOperators, M)` consumer contract, callable ABI, graph semantics, lowering semantics, and Lean observations.

**Architecture:** Keep one canonical internal representation. `Traits(...)` rows normalize directly into the existing `cmeta_type_traits` object. `Operators(M, ...)` accepts either the current flat 15-field row or the new 7-field composite row and normalizes the latter through an explicit expansion trampoline before invoking the existing 15-argument consumer `M`. No CFlow consumer learns the structured syntax.

**Tech Stack:** Strict C11 preprocessor/macros, CMake presets, GCC, Clang, existing CMeta `Schema`/`Replay` kernel, existing C/Lean conformance snapshots, Lean 4.30.0 / Lake.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-traits-operators-row-syntax-design.md`

## Global Constraints

- Preserve `CFlowOperators(M)` as the authoritative operator provider and `Replay(CFlowOperators, M)` as the consumer interface.
- Do not change `cmeta_type_traits`, CFlow operator descriptor layout, callable ABI, graph/cardinality semantics, optimizer semantics, plan semantics, execution semantics, or canonical lowering.
- Keep `Enum`/`Struct` homogeneous tuple rows unchanged.
- Do not add wrapper vocabulary such as `Equal(...)`, `Hash(...)`, `Arity(...)`, `Effect(...)`, `Field(...)`, or `Item(...)`.
- Phase 1 requires operator subrows in canonical order: `call`, `fn`, `flow`, `semantic`, `effect`.
- Phase 1 keeps full effect tokens such as `CMETA_EFFECT_PURE`; no defaults or short aliases.
- Trait flags are derived from rows. Do not infer `CMETA_TRAIT_TRIVIAL_COPY` or `CMETA_TRAIT_TRIVIAL_DESTROY`.
- Invalid tags, duplicate trait tags, missing/duplicate/misordered operator subrows, and malformed arities must fail during compile/preprocess.
- Use only `formal-linux-gcc` / `formal-linux-clang` presets for compiler configuration.
- Existing committed `formal/CMeta/*GeneratedC.lean` semantic snapshots must remain byte-for-byte unchanged after the operator migration.
- `lake build --wfail` must remain clean; no `axiom`, `constant`, `sorry`, or `admit`.

---

### Task 1: Structured `Traits(...)` Positive Path

**Files:**
- Create: `formal/cmeta_traits_row_syntax_witness.c`
- Modify: `formal/CMakeLists.txt`
- Modify: `.github/workflows/lean.yml`
- Modify after RED: `cmeta/include/cmeta/type_traits.h`

**Interfaces:**
- Consumes: `Schema(...)`, `CMETA_LOCAL`, `cmeta_trait_flags`, `cmeta_type_traits`.
- Produces: `Traits(name, (tag, fn), ...)` plus internal `CMETA_TRAITS_POSITIONAL(...)` compatibility constructor.

- [ ] **Step 1: Write the failing positive witness**

Create `formal/cmeta_traits_row_syntax_witness.c`:

```c
#include <cmeta/type_traits.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct sample_point { int x; int y; } sample_point;

static bool sample_point_equal(const void *a, const void *b) {
    const sample_point *l = (const sample_point *)a;
    const sample_point *r = (const sample_point *)b;
    return l->x == r->x && l->y == r->y;
}
static uint64_t sample_point_hash(const void *value) {
    const sample_point *p = (const sample_point *)value;
    return (uint64_t)(unsigned)p->x * 131u + (uint64_t)(unsigned)p->y;
}
static bool sample_point_copy(void *dst, const void *src) {
    memcpy(dst, src, sizeof(sample_point));
    return true;
}

Traits(sample_point,
    (equal, sample_point_equal),
    (hash, sample_point_hash),
    (copy, sample_point_copy)
);

int main(void) {
    assert(cmeta_traits_sample_point.flags ==
        (CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COPY));
    assert(cmeta_traits_sample_point.equal == sample_point_equal);
    assert(cmeta_traits_sample_point.hash == sample_point_hash);
    assert(cmeta_traits_sample_point.copy_construct == sample_point_copy);
    assert(cmeta_traits_sample_point.compare == NULL);
    assert(cmeta_traits_sample_point.move_construct == NULL);
    assert(cmeta_traits_sample_point.destroy == NULL);
    return 0;
}
```

Wire it into `formal/CMakeLists.txt`:

```cmake
cmeta_add_formal_witness(cmeta_traits_row_syntax_witness
  cmeta_traits_row_syntax_witness.c)
target_include_directories(cmeta_traits_row_syntax_witness PRIVATE
  ${PROJECT_SOURCE_DIR}/cmeta/include)
add_test(NAME cmeta_traits_row_syntax COMMAND cmeta_traits_row_syntax_witness)
```

Add `cmeta_traits_row_syntax_witness` to the workflow's existing C witness target list and execute `"$binary_dir/bin/cmeta_traits_row_syntax_witness"` in `Execute applicability probes`.

- [ ] **Step 2: Run RED on both compiler presets**

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target cmeta_traits_row_syntax_witness
cmake --preset formal-linux-clang
cmake --build --preset build-formal-linux-clang --target cmeta_traits_row_syntax_witness
```

Expected: both builds fail because current `Traits` requires eight positional arguments.

- [ ] **Step 3: Implement the minimal row-to-descriptor normalizer**

In `cmeta/include/cmeta/type_traits.h`, preserve the old constructor only as:

```c
#define CMETA_TRAITS_POSITIONAL(name, flags_, equal_, hash_, compare_, copy_, move_, destroy_) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        (flags_), (equal_), (hash_), (compare_), (copy_), (move_), (destroy_) \
    }
```

Add exact row mappings:

```c
#define CMETA_TRAIT_FLAG_equal   CMETA_TRAIT_EQUAL
#define CMETA_TRAIT_FLAG_hash    CMETA_TRAIT_HASH
#define CMETA_TRAIT_FLAG_compare CMETA_TRAIT_COMPARE
#define CMETA_TRAIT_FLAG_copy    CMETA_TRAIT_COPY
#define CMETA_TRAIT_FLAG_move    CMETA_TRAIT_MOVE
#define CMETA_TRAIT_FLAG_destroy CMETA_TRAIT_DESTROY

#define CMETA_TRAIT_INIT_equal(fn)   .equal = (fn),
#define CMETA_TRAIT_INIT_hash(fn)    .hash = (fn),
#define CMETA_TRAIT_INIT_compare(fn) .compare = (fn),
#define CMETA_TRAIT_INIT_copy(fn)    .copy_construct = (fn),
#define CMETA_TRAIT_INIT_move(fn)    .move_construct = (fn),
#define CMETA_TRAIT_INIT_destroy(fn) .destroy = (fn),

#define CMETA_TRAIT_FLAG_ROW(tag, fn) \
    | CMETA_PP_CAT(CMETA_TRAIT_FLAG_, tag)
#define CMETA_TRAIT_INIT_ROW(tag, fn) \
    CMETA_PP_CAT(CMETA_TRAIT_INIT_, tag)(fn)

#define Traits(name, ...) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        .flags = (cmeta_trait_flags)(0u Schema(CMETA_TRAIT_FLAG_ROW, __VA_ARGS__)), \
        Schema(CMETA_TRAIT_INIT_ROW, __VA_ARGS__) \
    }
```

- [ ] **Step 4: Run GREEN on both presets**

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target cmeta_traits_row_syntax_witness
./build/formal-linux-gcc/bin/cmeta_traits_row_syntax_witness
cmake --preset formal-linux-clang
cmake --build --preset build-formal-linux-clang --target cmeta_traits_row_syntax_witness
./build/formal-linux-clang/bin/cmeta_traits_row_syntax_witness
```

Expected: both executables pass.

- [ ] **Step 5: Commit**

```bash
git add cmeta/include/cmeta/type_traits.h formal/cmeta_traits_row_syntax_witness.c \
        formal/CMakeLists.txt .github/workflows/lean.yml
git commit -m "feat(cmeta): add structured trait rows"
```

---

### Task 2: Trait Diagnostics

**Files:**
- Create: `formal/cmeta_traits_duplicate_tag_fail.c`
- Create: `formal/cmeta_traits_unknown_tag_fail.c`
- Create: `formal/cmeta_traits_malformed_row_fail.c`
- Modify: `formal/CMakeLists.txt`
- Modify after RED: `cmeta/include/cmeta/type_traits.h`

**Interfaces:**
- Consumes: Task 1 `Traits(...)` rows.
- Produces: deterministic duplicate, unknown-tag, and arity rejection.

- [ ] **Step 1: Add the three invalid translation units**

`formal/cmeta_traits_duplicate_tag_fail.c`:

```c
#include <cmeta/type_traits.h>
static bool eq1(const void *a, const void *b) { return a == b; }
static bool eq2(const void *a, const void *b) { return a == b; }
Traits(duplicate_traits, (equal, eq1), (equal, eq2));
int main(void) { return 0; }
```

`formal/cmeta_traits_unknown_tag_fail.c`:

```c
#include <cmeta/type_traits.h>
static void serialize_value(void) {}
Traits(unknown_traits, (serialize, serialize_value));
int main(void) { return 0; }
```

`formal/cmeta_traits_malformed_row_fail.c`:

```c
#include <cmeta/type_traits.h>
static bool eq1(const void *a, const void *b) { return a == b; }
Traits(malformed_traits, (equal, eq1, unexpected));
int main(void) { return 0; }
```

- [ ] **Step 2: Add one reusable negative-compile helper and verify RED**

Add to `formal/CMakeLists.txt`:

```cmake
function(cmeta_expect_compile_failure name source)
  try_compile(${name}_COMPILED
    "${CMAKE_CURRENT_BINARY_DIR}/${name}-probe"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
    CMAKE_FLAGS
      "-DCMAKE_C_STANDARD=11"
      "-DCMAKE_C_STANDARD_REQUIRED=ON"
      "-DCMAKE_C_EXTENSIONS=OFF"
      "-DCMAKE_C_FLAGS:STRING=-I${PROJECT_SOURCE_DIR}/cmeta/include -I${PROJECT_SOURCE_DIR}/cflow/include"
    OUTPUT_VARIABLE ${name}_OUTPUT)
  if(${name}_COMPILED)
    message(FATAL_ERROR "${name} unexpectedly compiled")
  endif()
endfunction()

cmeta_expect_compile_failure(CMETA_TRAITS_DUPLICATE_TAG cmeta_traits_duplicate_tag_fail.c)
cmeta_expect_compile_failure(CMETA_TRAITS_UNKNOWN_TAG cmeta_traits_unknown_tag_fail.c)
cmeta_expect_compile_failure(CMETA_TRAITS_MALFORMED_ROW cmeta_traits_malformed_row_fail.c)
```

Run both configure presets. Expected RED: duplicate-tag probe unexpectedly compiles, causing configure failure; unknown/malformed probes are rejected.

- [ ] **Step 3: Add duplicate detection using owner-qualified enum markers**

Add:

```c
#define CMETA_TRAIT_SEEN_equal(name)   CMETA_PP_CAT(name, __cmeta_seen_equal),
#define CMETA_TRAIT_SEEN_hash(name)    CMETA_PP_CAT(name, __cmeta_seen_hash),
#define CMETA_TRAIT_SEEN_compare(name) CMETA_PP_CAT(name, __cmeta_seen_compare),
#define CMETA_TRAIT_SEEN_copy(name)    CMETA_PP_CAT(name, __cmeta_seen_copy),
#define CMETA_TRAIT_SEEN_move(name)    CMETA_PP_CAT(name, __cmeta_seen_move),
#define CMETA_TRAIT_SEEN_destroy(name) CMETA_PP_CAT(name, __cmeta_seen_destroy),
#define CMETA_TRAIT_SEEN_ROW(row, name) \
    CMETA_TRAIT_SEEN_ROW_E(name, CMETA_PP_UNPAREN row)
#define CMETA_TRAIT_SEEN_ROW_E(name, ...) \
    CMETA_TRAIT_SEEN_ROW_I(name, __VA_ARGS__)
#define CMETA_TRAIT_SEEN_ROW_I(name, tag, fn) \
    CMETA_PP_CAT(CMETA_TRAIT_SEEN_, tag)(name)
```

Change `Traits` to emit markers before the descriptor:

```c
#define Traits(name, ...) \
    enum { \
        CMETA_SCHEMA_ROWS(CMETA_TRAIT_SEEN_ROW, name, __VA_ARGS__) \
        CMETA_PP_CAT(name, __cmeta_traits_end) \
    }; \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        .flags = (cmeta_trait_flags)(0u Schema(CMETA_TRAIT_FLAG_ROW, __VA_ARGS__)), \
        Schema(CMETA_TRAIT_INIT_ROW, __VA_ARGS__) \
    }
```

- [ ] **Step 4: Verify GREEN**

Run both configure presets and both positive witness executables. Expected: all three negative probes are rejected and the valid structured declaration still passes.

- [ ] **Step 5: Commit**

```bash
git add cmeta/include/cmeta/type_traits.h formal/CMakeLists.txt \
        formal/cmeta_traits_duplicate_tag_fail.c formal/cmeta_traits_unknown_tag_fail.c \
        formal/cmeta_traits_malformed_row_fail.c
git commit -m "test(cmeta): reject invalid trait rows"
```

---

### Task 3: Structured `Operators(M, ...)` Normalization

**Files:**
- Create: `formal/cmeta_operator_row_syntax_witness.c`
- Modify: `formal/CMakeLists.txt`
- Modify: `.github/workflows/lean.yml`
- Modify after RED: `cmeta/include/cmeta/pp.h`

**Interfaces:**
- Consumes: `CMETA_SCHEMA_ROWS`, `CMETA_PP_NARG`, `CMETA_PP_CAT`, `CMETA_PP_UNPAREN`.
- Produces: `Operators(M, ...)` that invokes `M` with exactly the existing 15 normalized arguments for both flat-15 and structured-7 source rows.

- [ ] **Step 1: Add the structured-vs-flat positive witness**

Create `formal/cmeta_operator_row_syntax_witness.c`:

```c
#include <cmeta/pp.h>
#include <assert.h>

#define CAPTURE_STRUCT(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    static const int structured[] = { E, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect };
#define CAPTURE_FLAT(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    static const int flat[] = { E, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect };

#define StructuredOps(M) Operators(M, \
    (10, sample, (call, 1, 2, 3), (fn, 4, 5, 6, 7, 8), \
     (flow, 9, 10, 11), (semantic, 12), (effect, 13)))
#define FlatOps(M) Operators(M, \
    (10, sample, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13))

Replay(StructuredOps, CAPTURE_STRUCT)
Replay(FlatOps, CAPTURE_FLAT)

int main(void) {
    assert(sizeof(structured) == sizeof(flat));
    for (unsigned i = 0; i < sizeof(flat) / sizeof(flat[0]); ++i)
        assert(structured[i] == flat[i]);
    return 0;
}
```

Wire the witness into `formal/CMakeLists.txt`, the workflow witness target list, and `Execute applicability probes`.

- [ ] **Step 2: Run RED on both presets**

Build only `cmeta_operator_row_syntax_witness` under GCC and Clang. Expected: both fail because current `Operators` forwards seven fields directly to a 15-argument consumer.

- [ ] **Step 3: Implement the two-stage operator expansion trampoline**

In `cmeta/include/cmeta/pp.h`, replace only the current `Operators` semantic alias with:

```c
#define CMETA_OPERATOR_ROW_APPLY(row, M) \
    CMETA_OPERATOR_ROW_APPLY_E(M, CMETA_PP_UNPAREN row)
#define CMETA_OPERATOR_ROW_APPLY_E(M, ...) \
    CMETA_OPERATOR_ROW_APPLY_I(M, __VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_I(M, ...) \
    CMETA_OPERATOR_ROW_APPLY_N(M, CMETA_PP_NARG(__VA_ARGS__), __VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_N(M, n, ...) \
    CMETA_OPERATOR_ROW_APPLY_N_E(M, n, __VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_N_E(M, n, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_ROW_APPLY_, n)(M, __VA_ARGS__)

#define CMETA_OPERATOR_ROW_APPLY_15(M, ...) M(__VA_ARGS__)
```

Add canonical tag extractors:

```c
#define CMETA_OPERATOR_CALL_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_CALL_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_CALL_ARGS_call(margc, fnarg, childarg) margc, fnarg, childarg
#define CMETA_OPERATOR_FN_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_FN_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_FN_ARGS_fn(arity, p0, p1, p2, ret) arity, p0, p1, p2, ret
#define CMETA_OPERATOR_FLOW_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_FLOW_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_FLOW_ARGS_flow(out, card, childrule) out, card, childrule
#define CMETA_OPERATOR_SEMANTIC_ARG(tag, value) \
    CMETA_PP_CAT(CMETA_OPERATOR_SEMANTIC_ARG_, tag)(value)
#define CMETA_OPERATOR_SEMANTIC_ARG_semantic(value) value
#define CMETA_OPERATOR_EFFECT_ARG(tag, value) \
    CMETA_PP_CAT(CMETA_OPERATOR_EFFECT_ARG_, tag)(value)
#define CMETA_OPERATOR_EFFECT_ARG_effect(value) value
```

Normalize structured rows through a second reparse boundary so comma-producing extractors become individual arguments before `M` is parsed:

```c
#define CMETA_OPERATOR_ROW_APPLY_7(M, E, method, callrow, fnrow, flowrow, semanticrow, effectrow) \
    CMETA_OPERATOR_ROW_STRUCT_E(M, E, method, \
        CMETA_OPERATOR_CALL_ARGS callrow, \
        CMETA_OPERATOR_FN_ARGS fnrow, \
        CMETA_OPERATOR_FLOW_ARGS flowrow, \
        CMETA_OPERATOR_SEMANTIC_ARG semanticrow, \
        CMETA_OPERATOR_EFFECT_ARG effectrow)
#define CMETA_OPERATOR_ROW_STRUCT_E(...) CMETA_OPERATOR_ROW_STRUCT_I(__VA_ARGS__)
#define CMETA_OPERATOR_ROW_STRUCT_I(M, E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    M(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect)
```

Finally:

```c
#undef Operators
#define Operators(M, ...) \
    CMETA_SCHEMA_ROWS(CMETA_OPERATOR_ROW_APPLY, M, __VA_ARGS__)
```

Do not change `Replay(schema, M)`.

- [ ] **Step 4: Run GREEN plus flat compatibility**

Build/run `cmeta_operator_row_syntax_witness` under both presets, then build `cmeta_plan_conformance_witness` and `cmeta_optimizer_conformance_witness` under both presets before migrating `CFlowOperators`.

Expected: the new structured row and the old flat row normalize identically; old flat CFlow provider remains compatible.

- [ ] **Step 5: Commit**

```bash
git add cmeta/include/cmeta/pp.h formal/cmeta_operator_row_syntax_witness.c \
        formal/CMakeLists.txt .github/workflows/lean.yml
git commit -m "feat(cmeta): normalize structured operator rows"
```

---

### Task 4: Operator Diagnostics

**Files:**
- Create: `formal/cmeta_operator_invalid_row_fixture.h`
- Create: `formal/cmeta_operator_unknown_tag_fail.c`
- Create: `formal/cmeta_operator_duplicate_subrow_fail.c`
- Create: `formal/cmeta_operator_missing_subrow_fail.c`
- Create: `formal/cmeta_operator_malformed_subrow_fail.c`
- Modify: `formal/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 `Operators` normalizer and Task 2 negative-compile helper.
- Produces: compile-time rejection of invalid structured operator rows while flat-15 compatibility remains valid.

- [ ] **Step 1: Add the shared invalid-row fixture**

`formal/cmeta_operator_invalid_row_fixture.h`:

```c
#include <cmeta/pp.h>
#define CMETA_INVALID_CONSUMER(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    enum { cmeta_invalid_operator_must_not_compile = (E) };
#define InvalidOps(M) Operators(M, INVALID_OPERATOR_ROW)
Replay(InvalidOps, CMETA_INVALID_CONSUMER)
int main(void) { return 0; }
```

- [ ] **Step 2: Add four exact invalid rows**

`formal/cmeta_operator_unknown_tag_fail.c`:

```c
#define INVALID_OPERATOR_ROW \
    (10, sample, (unknown, 1, 2, 3), (fn, 4, 5, 6, 7, 8), \
     (flow, 9, 10, 11), (semantic, 12), (effect, 13))
#include "cmeta_operator_invalid_row_fixture.h"
```

`formal/cmeta_operator_duplicate_subrow_fail.c`:

```c
#define INVALID_OPERATOR_ROW \
    (10, sample, (call, 1, 2, 3), (call, 4, 5, 6, 7, 8), \
     (flow, 9, 10, 11), (semantic, 12), (effect, 13))
#include "cmeta_operator_invalid_row_fixture.h"
```

`formal/cmeta_operator_missing_subrow_fail.c`:

```c
#define INVALID_OPERATOR_ROW \
    (10, sample, (call, 1, 2, 3), (fn, 4, 5, 6, 7, 8), \
     (flow, 9, 10, 11), (semantic, 12))
#include "cmeta_operator_invalid_row_fixture.h"
```

`formal/cmeta_operator_malformed_subrow_fail.c`:

```c
#define INVALID_OPERATOR_ROW \
    (10, sample, (call, 1, 2, 3), (fn, 4, 5, 6, 7, 8, 99), \
     (flow, 9, 10, 11), (semantic, 12), (effect, 13))
#include "cmeta_operator_invalid_row_fixture.h"
```

- [ ] **Step 3: Register all four probes and verify rejection**

Add:

```cmake
cmeta_expect_compile_failure(CMETA_OPERATOR_UNKNOWN_TAG cmeta_operator_unknown_tag_fail.c)
cmeta_expect_compile_failure(CMETA_OPERATOR_DUPLICATE_SUBROW cmeta_operator_duplicate_subrow_fail.c)
cmeta_expect_compile_failure(CMETA_OPERATOR_MISSING_SUBROW cmeta_operator_missing_subrow_fail.c)
cmeta_expect_compile_failure(CMETA_OPERATOR_MALFORMED_SUBROW cmeta_operator_malformed_subrow_fail.c)
```

Run both configure presets. Expected: all four probes are rejected. Also build/run `cmeta_operator_row_syntax_witness` under both presets to prove valid structured syntax is still accepted.

The required failure mechanisms are fixed by Task 3: unknown/misordered tags resolve to an undefined canonical tag handler; a missing top-level subrow dispatches to an undefined `CMETA_OPERATOR_ROW_APPLY_6`; malformed `fn` payload violates the exact `CMETA_OPERATOR_FN_ARGS_fn` arity. Do not add defaults or arbitrary-order parsing.

- [ ] **Step 4: Commit**

```bash
git add formal/CMakeLists.txt formal/cmeta_operator_invalid_row_fixture.h \
        formal/cmeta_operator_unknown_tag_fail.c formal/cmeta_operator_duplicate_subrow_fail.c \
        formal/cmeta_operator_missing_subrow_fail.c formal/cmeta_operator_malformed_subrow_fail.c
git commit -m "test(cmeta): reject invalid operator rows"
```

---

### Task 5: Migrate `CFlowOperators` and Prove Semantic Stability

**Files:**
- Modify: `cflow/include/cflow/operators.h`
- Verify unchanged: all committed `formal/CMeta/*GeneratedC.lean` snapshots.

**Interfaces:**
- Consumes: Task 3 normalizer.
- Produces: structured source rows for the same six operators and the same normalized 15-field consumer ABI.

- [ ] **Step 1: Rewrite all six rows exactly**

```c
#define CFlowOperators(M) \
    Operators(M, \
        (FILTER, filter, \
            (call, 1, 0, -1), \
            (fn, 1, INPUT, NONE, NONE, BOOL), \
            (flow, SAME, FILTER, NONE), \
            (semantic, filter), \
            (effect, CMETA_EFFECT_PURE)), \
        (MAP, map, \
            (call, 1, 0, -1), \
            (fn, 1, INPUT, NONE, NONE, VALUE), \
            (flow, RETURN, ONE, NONE), \
            (semantic, map), \
            (effect, CMETA_EFFECT_PURE)), \
        (TRANSFORM, transform, \
            (call, 1, 0, -1), \
            (fn, 1, INPUT, NONE, NONE, VALUE), \
            (flow, RETURN, ONE, NONE), \
            (semantic, map), \
            (effect, CMETA_EFFECT_PURE)), \
        (FLAT_MAP, flatMap, \
            (call, 1, 0, -1), \
            (fn, 3, INPUT, OUT_PTR, CURSOR, GENERATOR), \
            (flow, POINTEE1, EXPAND, NONE), \
            (semantic, flat_map), \
            (effect, CMETA_EFFECT_PURE)), \
        (REDUCE, reduce, \
            (call, 1, 0, -1), \
            (fn, 2, INPUT, INPUT, NONE, INPUT), \
            (flow, SAME, REDUCE, NONE), \
            (semantic, reduce), \
            (effect, CMETA_EFFECT_STATEFUL)), \
        (ZIP, zip, \
            (call, 2, 1, 0), \
            (fn, 2, INPUT, SUBGRAPH, NONE, VALUE), \
            (flow, RETURN, ONE, SUBGRAPH_1TO1), \
            (semantic, high_level), \
            (effect, CMETA_EFFECT_PURE)))
```

Update the comment in the same file: source rows are structured; every `Replay(CFlowOperators, M)` consumer still receives the normalized 15-field signature.

- [ ] **Step 2: Run the full GCC C/snapshot gate**

Use the exact workflow target list plus the two new witnesses:

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target \
  cmeta_header_conformance_witness cmeta_type_identity_conformance_witness \
  cmeta_descriptor_bridge_conformance_witness cmeta_type_identity_multi_tu \
  cmeta_type_universe_probe cmeta_fmt_args_simplification_witness \
  cmeta_producer_replay_witness cmeta_nested_replay_deferred_witness \
  cmeta_traits_row_syntax_witness cmeta_operator_row_syntax_witness \
  cmeta_plan_conformance_witness cmeta_structured_conformance_witness \
  cmeta_structured_policy_conformance_witness cmeta_optimizer_conformance_witness \
  cmeta_optimizer_gating_conformance_witness cmeta_optimizer_topology_conformance_witness
```

Run the existing workflow's snapshot-generation commands and `diff -u` against every committed generated Lean snapshot. Expected: zero diff.

- [ ] **Step 3: Repeat the full C/snapshot gate under Clang**

Use `formal-linux-clang` / `build-formal-linux-clang`. Expected: all witnesses pass and all semantic snapshots remain unchanged.

- [ ] **Step 4: Kernel-check the unchanged formal semantics**

```bash
cd formal
lake update
lake build --wfail
```

Expected: PASS with no warning.

- [ ] **Step 5: Commit**

```bash
git add cflow/include/cflow/operators.h
git commit -m "refactor(cflow): structure operator schema rows"
```

---

### Task 6: Surface Audit, Documentation, and Exact-Head Gate

**Files:**
- Modify: `cmeta/include/cmeta/type_traits.h` documentation comment
- Modify: `cmeta/include/cmeta/pp.h` `Operators` documentation comment
- Modify: `docs/superpowers/specs/2026-08-21-cmeta-traits-operators-row-syntax-design.md` status
- Verify: `.github/workflows/lean.yml`, PR #3 exact head

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces: documented structured public surface, one internal positional Traits compatibility constructor, and one normalized operator consumer path.

- [ ] **Step 1: Verify the plan's repository-inventory precondition**

Run:

```bash
git grep -n "CMETA_TRAITS_POSITIONAL"
git grep -nE '\bTraits[[:space:]]*\(' -- ':!docs/**'
git grep -n "#define Operators"
git grep -n "CFlowOperators(M)"
git grep -n "Replay(CFlowOperators"
```

Expected at this point: `CMETA_TRAITS_POSITIONAL` appears only at its internal definition; non-doc `Traits(...)` uses are the new structured witness and any deliberately migrated structured declarations; exactly one public `Operators` definition exists; exactly one authoritative `CFlowOperators(M)` provider exists. If the grep output contains a positional public `Traits(...)` call site, stop execution because this plan's inventory assumption is stale; amend the plan before changing that call site.

- [ ] **Step 2: Write the final header documentation**

In `type_traits.h`, document the accepted public rows:

```text
Traits rows:
  (equal, fn)
  (hash, fn)
  (compare, fn)
  (copy, fn)
  (move, fn)
  (destroy, fn)
```

In `pp.h`, document:

```text
Operators source row:
  (E, method, (call,...), (fn,...), (flow,...), (semantic,...), (effect,...))

Consumer signature after normalization:
  M(E, method, margc, fnarg, childarg, farity, p0, p1, p2,
    ret, out, card, childrule, semantic, intrinsic_effects)
```

Change the design spec status to `Implemented and verified` only after Step 3 passes.

- [ ] **Step 3: Run the complete local verification gate**

Run both configure presets, both full witness target lists, both new applicability executables, all existing applicability executables, every workflow snapshot diff, then:

```bash
cd formal
lake update
lake build --wfail
```

Run the existing proof-placeholder guard exactly:

```bash
if grep -R -nE '^[[:space:]]*(axiom|constant)[[:space:]]|\b(sorry|admit)\b' \
    formal/CMeta.lean formal/CMeta/*.lean; then
  exit 1
fi
```

Expected: zero build failures, zero snapshot diffs, zero placeholder matches.

- [ ] **Step 4: Commit documentation**

```bash
git add cmeta/include/cmeta/type_traits.h cmeta/include/cmeta/pp.h \
        docs/superpowers/specs/2026-08-21-cmeta-traits-operators-row-syntax-design.md
git commit -m "docs(cmeta): document structured row surface"
```

- [ ] **Step 5: Push and verify exact head**

Push `leanv4`. Verify PR #3 `head_sha` equals the pushed commit. Verify the `Lean proofs` run attached to that exact SHA is `completed / success`; inspect both GCC and Clang jobs and require `Build and kernel-check Lean proofs` to be `success`. Do not use an earlier workflow or a different merge/head SHA as completion evidence.
