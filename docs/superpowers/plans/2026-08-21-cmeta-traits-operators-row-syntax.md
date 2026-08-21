# CMeta Traits / Operators Structured Row Syntax Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace positional `Traits(...)` and human-written 15-field CFlow operator rows with structured tuple-row source syntax while preserving the existing normalized descriptors, `Replay(CFlowOperators, M)` consumer contract, callable ABI, graph semantics, lowering semantics, and Lean observations.

**Architecture:** Keep one canonical internal representation. `Traits(...)` rows normalize directly into the existing `cmeta_type_traits` object; `Operators(M, ...)` accepts either the current flat 15-field row or the new 7-field composite row and normalizes the latter before invoking the existing consumer `M`. CFlow consumers remain unchanged: only source declarations and the single normalization boundary know about structured rows.

**Tech Stack:** Strict C11 preprocessor/macros, CMake presets, GCC, Clang, existing CMeta `Schema`/`Replay` kernel, existing C/Lean conformance snapshots, Lean 4.30.0 / Lake.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-traits-operators-row-syntax-design.md`

## Global Constraints

- Preserve `CFlowOperators(M)` as the authoritative operator provider and `Replay(CFlowOperators, M)` as the consumer interface.
- Do not change `cmeta_type_traits`, CFlow operator descriptor layout, callable ABI, graph/cardinality semantics, optimizer semantics, plan semantics, execution semantics, or canonical lowering.
- Keep `Enum`/`Struct` homogeneous tuple rows unchanged.
- Do not add wrapper vocabulary such as `Equal(...)`, `Hash(...)`, `Arity(...)`, `Effect(...)`, `Field(...)`, or `Item(...)`.
- Phase 1 requires all five operator subrows in canonical order: `call`, `fn`, `flow`, `semantic`, `effect`.
- Phase 1 keeps full effect tokens such as `CMETA_EFFECT_PURE`; no default inference or short aliases.
- Trait capability flags are derived from rows. Do not infer `CMETA_TRAIT_TRIVIAL_COPY` or `CMETA_TRAIT_TRIVIAL_DESTROY` in this slice.
- Invalid row tags, duplicate trait tags, missing operator subrows, duplicate/misordered operator subrows, and malformed row arities must fail at compile/preprocess time.
- Run all formal C work through the existing `formal-linux-gcc` / `formal-linux-clang` CMake presets; do not add ad-hoc compiler configuration to workflow YAML.
- Existing generated C/Lean semantic snapshots must remain byte-for-byte unchanged after the CFlow operator migration.
- No new Lean axioms, `constant`, `sorry`, or `admit`; `lake build --wfail` must remain clean.

---

### Task 1: Structured `Traits(...)` Positive Path

**Files:**
- Create: `formal/cmeta_traits_row_syntax_witness.c`
- Modify: `formal/CMakeLists.txt`
- Modify: `.github/workflows/lean.yml`
- Modify after RED: `cmeta/include/cmeta/type_traits.h`

**Interfaces:**
- Consumes: existing `Schema(...)`, `CMETA_LOCAL`, `cmeta_trait_flags`, `cmeta_type_traits`.
- Produces: public `Traits(name, (tag, fn), ...)` and internal compatibility macro `CMETA_TRAITS_POSITIONAL(name, flags, equal, hash, compare, copy, move, destroy)`.

- [ ] **Step 1: Add a failing strict-C11 witness for structured trait rows**

Create `formal/cmeta_traits_row_syntax_witness.c` with real function pointers and runtime assertions:

```c
#include <cmeta/type_traits.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct sample_point {
    int x;
    int y;
} sample_point;

static bool sample_point_equal(const void *a, const void *b) {
    const sample_point *left = (const sample_point *)a;
    const sample_point *right = (const sample_point *)b;
    return left->x == right->x && left->y == right->y;
}

static uint64_t sample_point_hash(const void *value) {
    const sample_point *point = (const sample_point *)value;
    return (uint64_t)(unsigned)point->x * 131u + (uint64_t)(unsigned)point->y;
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
add_test(NAME cmeta_traits_row_syntax
         COMMAND cmeta_traits_row_syntax_witness)
```

Add `cmeta_traits_row_syntax_witness` to `.github/workflows/lean.yml` under both the existing `Build C conformance witnesses` target list and `Execute applicability probes` command list.

- [ ] **Step 2: Verify RED before touching production macros**

Run:

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target cmeta_traits_row_syntax_witness
```

Expected: FAIL because the current public `Traits` macro requires eight positional arguments and cannot consume tuple rows.

Repeat with:

```bash
cmake --preset formal-linux-clang
cmake --build --preset build-formal-linux-clang --target cmeta_traits_row_syntax_witness
```

Expected: the same feature-missing failure under Clang.

- [ ] **Step 3: Replace only the public Traits source normalizer**

In `cmeta/include/cmeta/type_traits.h`, preserve the old constructor under an explicitly internal name:

```c
#define CMETA_TRAITS_POSITIONAL(name, flags_, equal_, hash_, compare_, copy_, move_, destroy_) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        (flags_), (equal_), (hash_), (compare_), (copy_), (move_), (destroy_) \
    }
```

Add one tag-to-flag mapping and one tag-to-designated-initializer mapping per supported callable trait:

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
```

Define the new public surface so flags and slots are derived from the same rows:

```c
#define Traits(name, ...) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        .flags = (cmeta_trait_flags)(0u Schema(CMETA_TRAIT_FLAG_ROW, __VA_ARGS__)), \
        Schema(CMETA_TRAIT_INIT_ROW, __VA_ARGS__) \
    }
```

Do not add trivial-copy/destruction inference.

- [ ] **Step 4: Verify GREEN on both formal presets**

Run:

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target cmeta_traits_row_syntax_witness
./build/formal-linux-gcc/formal/cmeta_traits_row_syntax_witness

cmake --preset formal-linux-clang
cmake --build --preset build-formal-linux-clang --target cmeta_traits_row_syntax_witness
./build/formal-linux-clang/formal/cmeta_traits_row_syntax_witness
```

If the executable path is emitted under `bin/` by the preset, use the generated `build/formal-linux-*/bin/cmeta_traits_row_syntax_witness` path consistently with the workflow.

Expected: build and execution PASS under both compilers.

- [ ] **Step 5: Commit the positive Traits slice**

```bash
git add cmeta/include/cmeta/type_traits.h \
        formal/cmeta_traits_row_syntax_witness.c \
        formal/CMakeLists.txt .github/workflows/lean.yml
git commit -m "feat(cmeta): add structured trait rows"
```

---

### Task 2: Trait Row Diagnostics and Duplicate Rejection

**Files:**
- Create: `formal/cmeta_traits_duplicate_tag_fail.c`
- Create: `formal/cmeta_traits_unknown_tag_fail.c`
- Create: `formal/cmeta_traits_malformed_row_fail.c`
- Modify: `formal/CMakeLists.txt`
- Modify after RED: `cmeta/include/cmeta/type_traits.h`

**Interfaces:**
- Consumes: Task 1 structured `Traits(...)`.
- Produces: compile-time rejection of duplicate, unknown, and malformed trait rows without making row order semantically observable.

- [ ] **Step 1: Add negative sources**

Duplicate tag:

```c
#include <cmeta/type_traits.h>
static bool eq1(const void *a, const void *b) { return a == b; }
static bool eq2(const void *a, const void *b) { return a == b; }
Traits(duplicate_traits,
    (equal, eq1),
    (equal, eq2)
);
int main(void) { return 0; }
```

Unknown tag:

```c
#include <cmeta/type_traits.h>
static void serialize_value(void) {}
Traits(unknown_traits,
    (serialize, serialize_value)
);
int main(void) { return 0; }
```

Malformed payload:

```c
#include <cmeta/type_traits.h>
static bool eq1(const void *a, const void *b) { return a == b; }
Traits(malformed_traits,
    (equal, eq1, unexpected)
);
int main(void) { return 0; }
```

- [ ] **Step 2: Add a reusable strict-C11 negative compile helper and verify RED quality**

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
      "-DCMAKE_C_FLAGS=-I${PROJECT_SOURCE_DIR}/cmeta/include -I${PROJECT_SOURCE_DIR}/cflow/include"
    OUTPUT_VARIABLE ${name}_OUTPUT)
  if(${name}_COMPILED)
    message(FATAL_ERROR "${name} unexpectedly compiled")
  endif()
endfunction()
```

Register:

```cmake
cmeta_expect_compile_failure(CMETA_TRAITS_DUPLICATE_TAG
  cmeta_traits_duplicate_tag_fail.c)
cmeta_expect_compile_failure(CMETA_TRAITS_UNKNOWN_TAG
  cmeta_traits_unknown_tag_fail.c)
cmeta_expect_compile_failure(CMETA_TRAITS_MALFORMED_ROW
  cmeta_traits_malformed_row_fail.c)
```

Run both configure presets. Expected before the duplicate guard is added: the duplicate case may compile or only warn; that is the intended RED. Unknown/malformed cases must already fail, proving tag dispatch is not silently accepted.

- [ ] **Step 3: Add deterministic duplicate detection to `Traits(...)`**

Use the existing context-preserving `CMETA_SCHEMA_ROWS` so each row can emit one owner-qualified enumerator:

```c
#define CMETA_TRAIT_SEEN_equal(name)   CMETA_PP_CAT(name, __cmeta_seen_equal),
#define CMETA_TRAIT_SEEN_hash(name)    CMETA_PP_CAT(name, __cmeta_seen_hash),
#define CMETA_TRAIT_SEEN_compare(name) CMETA_PP_CAT(name, __cmeta_seen_compare),
#define CMETA_TRAIT_SEEN_copy(name)    CMETA_PP_CAT(name, __cmeta_seen_copy),
#define CMETA_TRAIT_SEEN_move(name)    CMETA_PP_CAT(name, __cmeta_seen_move),
#define CMETA_TRAIT_SEEN_destroy(name) CMETA_PP_CAT(name, __cmeta_seen_destroy),

#define CMETA_TRAIT_SEEN_ROW(row, name) \
    CMETA_TRAIT_SEEN_ROW_I(name, CMETA_PP_UNPAREN row)
#define CMETA_TRAIT_SEEN_ROW_I(name, ...) \
    CMETA_TRAIT_SEEN_ROW_II(name, __VA_ARGS__)
#define CMETA_TRAIT_SEEN_ROW_II(name, tag, fn) \
    CMETA_PP_CAT(CMETA_TRAIT_SEEN_, tag)(name)
```

Make `Traits(...)` emit an anonymous enum before the descriptor:

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

A duplicate tag now creates a duplicate enumerator in the same enum; an unknown tag cannot resolve a `CMETA_TRAIT_SEEN_<tag>` handler; malformed arity cannot satisfy the two-field mapper.

- [ ] **Step 4: Verify all negative and positive cases**

Run:

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target cmeta_traits_row_syntax_witness
./build/formal-linux-gcc/bin/cmeta_traits_row_syntax_witness

cmake --preset formal-linux-clang
cmake --build --preset build-formal-linux-clang --target cmeta_traits_row_syntax_witness
./build/formal-linux-clang/bin/cmeta_traits_row_syntax_witness
```

Expected: configuration reports all three negative probes rejected, while the positive witness builds and runs.

- [ ] **Step 5: Commit trait diagnostics**

```bash
git add cmeta/include/cmeta/type_traits.h formal/CMakeLists.txt \
        formal/cmeta_traits_duplicate_tag_fail.c \
        formal/cmeta_traits_unknown_tag_fail.c \
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
- Consumes: existing `Schema`, `CMETA_SCHEMA_ROWS`, `CMETA_PP_NARG`, `CMETA_PP_CAT`.
- Produces: `Operators(M, ...)` accepting either the existing flat 15-field row or the new composite 7-field row `(E, method, callRow, fnRow, flowRow, semanticRow, effectRow)` and always invoking `M` with the existing 15 normalized fields.

- [ ] **Step 1: Add a positive structured-vs-flat normalization witness**

Create one test operator with integer tokens so the witness tests only preprocessor normalization, not CFlow semantics:

```c
#include <cmeta/pp.h>
#include <assert.h>

#define CAPTURE_STRUCT(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    static const int structured[] = { E, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect };
#define CAPTURE_FLAT(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    static const int flat[] = { E, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect };

#define StructuredOps(M) \
    Operators(M, \
        (10, sample, \
            (call, 1, 2, 3), \
            (fn, 4, 5, 6, 7, 8), \
            (flow, 9, 10, 11), \
            (semantic, 12), \
            (effect, 13)))

#define FlatOps(M) \
    Operators(M, \
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

Wire `cmeta_operator_row_syntax_witness` into `formal/CMakeLists.txt`, the workflow build target list, and applicability probe list just like Task 1.

- [ ] **Step 2: Verify RED under GCC and Clang**

Run both preset builds for only `cmeta_operator_row_syntax_witness`.

Expected: FAIL because current `Operators(M, ...)` forwards the seven structured top-level fields directly to a consumer requiring fifteen normalized arguments.

- [ ] **Step 3: Implement one normalizer in `cmeta/include/cmeta/pp.h`**

Replace only the current semantic alias implementation:

```c
#define CMETA_OPERATOR_ROW_APPLY(row, M) \
    CMETA_OPERATOR_ROW_APPLY_I(M, CMETA_PP_UNPAREN row)
#define CMETA_OPERATOR_ROW_APPLY_I(M, ...) \
    CMETA_OPERATOR_ROW_APPLY_II(M, CMETA_PP_NARG(__VA_ARGS__), __VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_II(M, n, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_ROW_APPLY_, n)(M, __VA_ARGS__)

#define CMETA_OPERATOR_ROW_APPLY_15(M, ...) M(__VA_ARGS__)
```

Add tag-specific extractors. Only the valid tag for each canonical position exists:

```c
#define CMETA_OPERATOR_CALL_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_CALL_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_CALL_ARGS_call(margc, fnarg, childarg) \
    margc, fnarg, childarg

#define CMETA_OPERATOR_FN_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_FN_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_FN_ARGS_fn(arity, p0, p1, p2, ret) \
    arity, p0, p1, p2, ret

#define CMETA_OPERATOR_FLOW_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_FLOW_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_FLOW_ARGS_flow(out, card, childrule) \
    out, card, childrule

#define CMETA_OPERATOR_SEMANTIC_ARG(tag, value) \
    CMETA_PP_CAT(CMETA_OPERATOR_SEMANTIC_ARG_, tag)(value)
#define CMETA_OPERATOR_SEMANTIC_ARG_semantic(value) value

#define CMETA_OPERATOR_EFFECT_ARG(tag, value) \
    CMETA_PP_CAT(CMETA_OPERATOR_EFFECT_ARG_, tag)(value)
#define CMETA_OPERATOR_EFFECT_ARG_effect(value) value
```

Use an expansion trampoline so comma-producing subrow extractors are expanded before the final consumer call is parsed:

```c
#define CMETA_OPERATOR_ROW_APPLY_7(M, E, method, callrow, fnrow, flowrow, semanticrow, effectrow) \
    CMETA_OPERATOR_ROW_APPLY_STRUCTURED(M, E, method, \
        CMETA_OPERATOR_CALL_ARGS callrow, \
        CMETA_OPERATOR_FN_ARGS fnrow, \
        CMETA_OPERATOR_FLOW_ARGS flowrow, \
        CMETA_OPERATOR_SEMANTIC_ARG semanticrow, \
        CMETA_OPERATOR_EFFECT_ARG effectrow)

#define CMETA_OPERATOR_ROW_APPLY_STRUCTURED(...) \
    CMETA_OPERATOR_ROW_APPLY_STRUCTURED_I(__VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_STRUCTURED_I(M, E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    M(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect)
```

Change `Operators` from direct `Schema` replay to the context-preserving normalizer:

```c
#undef Operators
#define Operators(M, ...) \
    CMETA_SCHEMA_ROWS(CMETA_OPERATOR_ROW_APPLY, M, __VA_ARGS__)
```

Do not touch `Replay(schema, M)`.

- [ ] **Step 4: Verify flat compatibility and structured GREEN**

Run both positive witness builds and executions. Then build at least the existing `cmeta_plan_conformance_witness` and `cmeta_optimizer_conformance_witness` under both presets to prove flat `CFlowOperators` still works before migration.

Expected: structured witness PASS; existing flat consumers still compile unchanged.

- [ ] **Step 5: Commit operator normalization**

```bash
git add cmeta/include/cmeta/pp.h \
        formal/cmeta_operator_row_syntax_witness.c \
        formal/CMakeLists.txt .github/workflows/lean.yml
git commit -m "feat(cmeta): normalize structured operator rows"
```

---

### Task 4: Operator Row Diagnostics

**Files:**
- Create: `formal/cmeta_operator_unknown_tag_fail.c`
- Create: `formal/cmeta_operator_duplicate_subrow_fail.c`
- Create: `formal/cmeta_operator_missing_subrow_fail.c`
- Create: `formal/cmeta_operator_malformed_subrow_fail.c`
- Modify: `formal/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 `Operators` normalizer and Task 2 `cmeta_expect_compile_failure` helper.
- Produces: explicit compile-time rejection of malformed composite operator rows while keeping flat 15-field internal compatibility.

- [ ] **Step 1: Write the four negative sources**

Use a 15-argument consumer macro in each source. Cover:

```c
/* unknown tag in call position */
(10, sample,
    (unknown, 1, 2, 3),
    (fn, 4, 5, 6, 7, 8),
    (flow, 9, 10, 11),
    (semantic, 12),
    (effect, 13))
```

```c
/* duplicate/misordered call subrow in fn position */
(10, sample,
    (call, 1, 2, 3),
    (call, 4, 5, 6, 7, 8),
    (flow, 9, 10, 11),
    (semantic, 12),
    (effect, 13))
```

```c
/* missing effect subrow: top-level arity is six instead of seven */
(10, sample,
    (call, 1, 2, 3),
    (fn, 4, 5, 6, 7, 8),
    (flow, 9, 10, 11),
    (semantic, 12))
```

```c
/* malformed fn payload */
(10, sample,
    (call, 1, 2, 3),
    (fn, 4, 5, 6, 7, 8, 99),
    (flow, 9, 10, 11),
    (semantic, 12),
    (effect, 13))
```

- [ ] **Step 2: Register all four with `cmeta_expect_compile_failure` and verify the failure boundary**

Add all four probes to `formal/CMakeLists.txt`. Run both configure presets.

Expected: each invalid source is rejected during `try_compile`; the valid structured witness from Task 3 still builds and runs.

- [ ] **Step 3: Improve only diagnostics if a case fails for the wrong reason**

If a malformed case accidentally reaches the consumer rather than failing in the normalizer, add only the missing arity/tag dispatch guard in `pp.h`. Do not add arbitrary-order parsing or defaults. The expected failure mechanism is an undefined canonical tag handler or an unsupported top-level/subrow arity.

- [ ] **Step 4: Commit operator diagnostics**

```bash
git add formal/CMakeLists.txt \
        formal/cmeta_operator_unknown_tag_fail.c \
        formal/cmeta_operator_duplicate_subrow_fail.c \
        formal/cmeta_operator_missing_subrow_fail.c \
        formal/cmeta_operator_malformed_subrow_fail.c \
        cmeta/include/cmeta/pp.h
git commit -m "test(cmeta): reject invalid operator rows"
```

---

### Task 5: Migrate `CFlowOperators` Without Semantic Drift

**Files:**
- Modify: `cflow/include/cflow/operators.h`
- Test unchanged: existing formal witnesses and all committed `formal/CMeta/*GeneratedC.lean` snapshots.

**Interfaces:**
- Consumes: Task 3 structured `Operators` normalizer.
- Produces: structured source declaration for the same six CFlow operators; normalized output remains the existing 15-field consumer ABI.

- [ ] **Step 1: Rewrite all six operator source rows structurally**

Replace the flat rows with exactly:

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

Update the surrounding comment to state that source rows are structured but `Replay(CFlowOperators, M)` receives the same normalized flat consumer signature.

- [ ] **Step 2: Build the complete C witness set under GCC and verify snapshots are unchanged**

Run the same target list used by `.github/workflows/lean.yml`:

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target \
  cmeta_header_conformance_witness \
  cmeta_type_identity_conformance_witness \
  cmeta_descriptor_bridge_conformance_witness \
  cmeta_type_identity_multi_tu \
  cmeta_type_universe_probe \
  cmeta_fmt_args_simplification_witness \
  cmeta_producer_replay_witness \
  cmeta_nested_replay_deferred_witness \
  cmeta_traits_row_syntax_witness \
  cmeta_operator_row_syntax_witness \
  cmeta_plan_conformance_witness \
  cmeta_structured_conformance_witness \
  cmeta_structured_policy_conformance_witness \
  cmeta_optimizer_conformance_witness \
  cmeta_optimizer_gating_conformance_witness \
  cmeta_optimizer_topology_conformance_witness
```

Then reproduce the workflow snapshot diff commands. Expected: **no committed `formal/CMeta/*GeneratedC.lean` file changes**.

- [ ] **Step 3: Repeat the same C build/snapshot gate under Clang**

Use `formal-linux-clang` / `build-formal-linux-clang`. Expected: the same semantic snapshots and successful witnesses.

- [ ] **Step 4: Run the Lean kernel after the C normalization migration**

```bash
cd formal
lake update
lake build --wfail
```

Expected: PASS with no new warning and no changes to existing formal judgments or lowering proofs.

- [ ] **Step 5: Commit the CFlow source migration**

```bash
git add cflow/include/cflow/operators.h
git commit -m "refactor(cflow): structure operator schema rows"
```

---

### Task 6: Legacy Surface Audit, Documentation, and Exact-Head Verification

**Files:**
- Modify: `cmeta/include/cmeta/type_traits.h` comments only if needed
- Modify: `cmeta/include/cmeta/pp.h` comments only if needed
- Modify: `docs/superpowers/specs/2026-08-21-cmeta-traits-operators-row-syntax-design.md` status
- Verify: `.github/workflows/lean.yml`, PR #3 exact head

**Interfaces:**
- Consumes: all previous tasks.
- Produces: one documented public structured surface with only an internal positional Traits compatibility helper and one normalized operator consumer ABI.

- [ ] **Step 1: Audit for accidental second semantic paths**

Run:

```bash
git grep -n "CMETA_TRAITS_POSITIONAL"
git grep -n "#define Operators"
git grep -n "CFlowOperators(M)"
git grep -n "Replay(CFlowOperators"
```

Required result:

- `CMETA_TRAITS_POSITIONAL` is an internal compatibility definition only; no new user-facing examples depend on it.
- there is one public `Operators` normalizer path;
- there is one authoritative `CFlowOperators(M)` provider;
- existing consumers still use `Replay(CFlowOperators, M)` rather than bypassing normalization.

If this audit exposes pre-existing in-repo positional `Traits(...)` call sites, migrate those call sites to tagged rows before proceeding; do not preserve two public `Traits` spellings.

- [ ] **Step 2: Update comments/status without adding new syntax**

Document in the headers that:

```text
Traits rows:      (equal, fn), (hash, fn), ...
Operator source:  (E, method, (call,...), (fn,...), (flow,...), (semantic,...), (effect,...))
Operator consumer remains the existing normalized 15-field M signature.
```

Change the design spec status from “awaiting review” to “approved for implementation / implemented” only after the full test gate passes.

- [ ] **Step 3: Run the full local verification gate one final time**

Run both formal configure presets, both full witness target lists, all applicability executables including the two new row-syntax witnesses, all snapshot diffs, and:

```bash
cd formal
lake update
lake build --wfail
```

Also run:

```bash
if grep -R -nE '^[[:space:]]*(axiom|constant)[[:space:]]|\b(sorry|admit)\b' \
    formal/CMeta.lean formal/CMeta/*.lean; then
  exit 1
fi
```

Expected: zero failures, zero snapshot diffs, zero proof placeholders.

- [ ] **Step 4: Commit final documentation/audit changes**

```bash
git add cmeta/include/cmeta/type_traits.h cmeta/include/cmeta/pp.h \
        docs/superpowers/specs/2026-08-21-cmeta-traits-operators-row-syntax-design.md
git commit -m "docs(cmeta): document structured row surface"
```

If Step 2 required no file changes, skip this commit rather than creating an empty one.

- [ ] **Step 5: Push and verify the exact PR head**

Push `leanv4`, then verify PR #3 reports the pushed commit as `head_sha`. Verify the `Lean proofs` workflow attached to that exact SHA is `completed / success`, and inspect both GCC and Clang jobs to confirm the final `Build and kernel-check Lean proofs` step succeeded.

Do not claim completion from an earlier workflow attempt or from a merge ref whose head SHA differs from the branch head.
