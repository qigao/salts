# CMeta Single-Stage Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete `interface_impl` and make `interface(...)` a complete header-local C11 interface declaration while preserving handle/vtable behavior and concrete `implements(...)` bindings.

**Architecture:** `CMETA_INTERFACE` will replay the method schema once to generate the handle, vtable, `static inline` wrappers, bind/query helpers, and TU-local reflection metadata. `CMETA_IMPLEMENTS` remains the only concrete implementation-binding macro; CFlow removes all out-of-line interface wrapper generation.

**Tech Stack:** C11 preprocessor macros, CMeta `Schema`-style X-list replay, CFlow interface consumers, TinyTest, CMake Presets, MSVC and Clang.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-single-stage-interface-design.md`

## Global Constraints

- Delete `interface_impl` and `CMETA_INTERFACE_IMPL` without a deprecated or no-op compatibility alias.
- Preserve `interface(...)`, `implements(...)`, handle/vtable layout, method ABI, capability bits, implementation text, and metadata contents.
- Generated wrappers and bind/query helpers are `static inline`; reflection arrays/descriptors are TU-local `static const`.
- Descriptor pointer equality across translation units is not a supported identity contract.
- Do not add allocation, fallback, validation, logging, table lookup, or a second runtime dispatch layer.
- `CFlow -> CMeta` remains the only dependency direction; add no targets or dependencies.
- Use `cmake-presets` for configure/build/test commands and `tinytest` for test structure.
- Related tests/examples are already staged or renamed by the user. Do not create implementation commits while those overlapping staged changes remain; use path-scoped diffs and verification checkpoints instead.

---

### Task 1: Make CMeta Interface declaration header-complete

**Files:**
- Modify: `cmeta/tests/cmeta_core_test.c:6`
- Modify: `cmeta/include/cmeta/interface.h:8-158`

**Interfaces:**
- Consumes: `CMETA_PP_CAT`, method schemas shaped as `METHODS(X, I)`, and method rows `R0..R4` / `V0..V4`.
- Produces: `CMETA_INTERFACE(I, METHODS)`, natural alias `interface(...)`, existing `CMETA_IMPLEMENTS(...)`, and natural alias `implements(...)`; no `CMETA_INTERFACE_IMPL` or `interface_impl`.

- [ ] **Step 1: Capture the hot-header compile-time baseline**

Run five Clang C11 syntax-only compilations before editing and record the median elapsed milliseconds in the execution notes:

```powershell
$samples = 1..5 | ForEach-Object {
    (Measure-Command {
        clang.exe -std=c11 -fsyntax-only `
            -I cmeta/include -I tinytest/include `
            cmeta/tests/cmeta_core_test.c
        if ($LASTEXITCODE -ne 0) { throw "baseline compile failed" }
    }).TotalMilliseconds
}
($samples | Sort-Object)[2]
```

- [ ] **Step 2: Write the failing single-stage interface test**

Add this declaration and implementation above `suite("CMeta core")`; deliberately do not call `CMETA_INTERFACE_IMPL`:

```c
enum { CMETA_TEST_COUNTER_RESET = 1u << 0 };

#define CMETA_TEST_COUNTER_METHODS(X, I) \
    X(I, R1, int, add, int, delta) \
    X(I, R0, int, value, _) \
    X(I, V0, void, reset, _)

CMETA_INTERFACE(cmeta_test_counter, CMETA_TEST_COUNTER_METHODS);

typedef struct cmeta_test_counter_state {
    int value;
} cmeta_test_counter_state;

static int cmeta_test_counter_add(void *self, int delta) {
    cmeta_test_counter_state *state = (cmeta_test_counter_state *)self;
    state->value += delta;
    return state->value;
}

static int cmeta_test_counter_value(void *self) {
    return ((cmeta_test_counter_state *)self)->value;
}

static void cmeta_test_counter_reset(void *self) {
    ((cmeta_test_counter_state *)self)->value = 0;
}

CMETA_IMPLEMENTS(cmeta_test_counter, cmeta_test_basic_counter,
    CMETA_TEST_COUNTER_RESET,
    .add = cmeta_test_counter_add,
    .value = cmeta_test_counter_value,
    .reset = cmeta_test_counter_reset
);
```

Add this TinyTest case inside the existing suite:

```c
it("defines a complete interface without a separate implementation replay") {
    cmeta_test_counter_state state = { 4 };
    cmeta_test_counter counter =
        cmeta_test_basic_counter_as_cmeta_test_counter(&state);
    const cmeta_interface_desc *meta = cmeta_test_counter_interface();

    check_true(cmeta_test_counter_valid(&counter));
    check_equal(cmeta_test_counter_implementation(&counter),
                "cmeta_test_basic_counter");
    check_true(cmeta_test_counter_has(&counter, CMETA_TEST_COUNTER_RESET));
    check_equal(cmeta_test_counter_add(&counter, 3), 7);
    check_equal(cmeta_test_counter_value(&counter), 7);
    cmeta_test_counter_reset(&counter);
    check_equal(cmeta_test_counter_value(&counter), 0);

    check_not_null(meta);
    check_equal(meta->name, "cmeta_test_counter");
    check_equal(meta->method_count, (size_t)3);
    check_equal(meta->methods[0].name, "add");
    check_equal(meta->methods[0].arity, 1u);
    check_equal(meta->methods[2].name, "reset");
    check_equal(meta->methods[2].arity, 0u);
}
```

- [ ] **Step 3: Run the test and verify RED**

Run in the MSVC toolchain environment:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test"
```

Expected: link failure for missing generated symbols such as `cmeta_test_counter_bind`, `cmeta_test_counter_add`, or `cmeta_test_counter_interface`. A compile error in the test is not the expected RED and must be corrected before implementation.

- [ ] **Step 4: Merge wrapper and metadata generation into `CMETA_INTERFACE`**

In `cmeta/include/cmeta/interface.h`, change every generated wrapper definition to internal linkage:

```c
#define CMETA_IFACE_IMPL_R0(I,R,N,_) \
    static inline R I##_##N(I *self) { return self->vtable->N(self->self); }
#define CMETA_IFACE_IMPL_R1(I,R,N,T1,A1) \
    static inline R I##_##N(I *self, T1 A1) { return self->vtable->N(self->self, A1); }
```

Apply the same `static inline` prefix to all existing `R2..R4` and `V0..V4` wrapper mappers without changing their parameter forwarding.

Replace the declaration-only tail of `CMETA_INTERFACE` with the former implementation replay:

```c
#define CMETA_INTERFACE(I, METHODS) \
    typedef struct I I; \
    typedef struct I##_vtable I##_vtable; \
    struct I##_vtable { \
        const char *implementation; \
        uint64_t capabilities; \
        METHODS(CMETA_IFACE_VT_ROW, I) \
    }; \
    struct I { void *self; const I##_vtable *vtable; }; \
    static const cmeta_interface_method_desc I##_method_meta[] = { \
        METHODS(CMETA_IFACE_META_ROW, I) \
    }; \
    static const cmeta_interface_desc I##_interface_meta = { \
        #I, I##_method_meta, \
        sizeof(I##_method_meta) / sizeof(I##_method_meta[0]) \
    }; \
    METHODS(CMETA_IFACE_IMPL_ROW, I) \
    static inline I I##_bind(void *self, const I##_vtable *vtable) { \
        I out = { self, vtable }; \
        return out; \
    } \
    static inline bool I##_valid(const I *self) { \
        return self && self->self && self->vtable; \
    } \
    static inline const char *I##_implementation(const I *self) { \
        return I##_valid(self) && self->vtable->implementation \
            ? self->vtable->implementation : "none"; \
    } \
    static inline uint64_t I##_capabilities(const I *self) { \
        return I##_valid(self) ? self->vtable->capabilities : 0u; \
    } \
    static inline bool I##_has(const I *self, uint64_t capability) { \
        return (I##_capabilities(self) & capability) == capability; \
    } \
    static inline const cmeta_interface_desc *I##_interface(void) { \
        return &I##_interface_meta; \
    }
```

Delete `CMETA_INTERFACE_IMPL`. Delete the natural `interface_impl` alias block. Update the header comment to show only `interface(...)` and `implements(...)`, and document that metadata is TU-local and semantically compared.

- [ ] **Step 5: Run the focused test and verify GREEN**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test && ctest --preset win-release-user -R ""^cmeta_core_test$"" --output-on-failure"
```

Expected: target builds and the CMeta core CTest passes with the new interface case.

- [ ] **Step 6: Record a path-scoped checkpoint without committing overlapping user work**

```powershell
git diff --check -- cmeta/include/cmeta/interface.h cmeta/tests/cmeta_core_test.c
git diff -- cmeta/include/cmeta/interface.h cmeta/tests/cmeta_core_test.c
```

Expected: no whitespace errors; diff contains only the single-stage interface and its regression test. Do not commit while `cmeta/tests/cmeta_core_test.c` remains part of the user's existing staged work.

---

### Task 2: Migrate CFlow and examples off `interface_impl`

**Files:**
- Modify: `cflow/src/interfaces.c:6-8`
- Modify: `cflow/src/scheduler.c:204`
- Modify: `cflow/examples/demo_interface.c:42,62`
- Modify: `cflow/examples/demo_cmeta_standalone.c:28`

**Interfaces:**
- Consumes: header-complete `CMETA_INTERFACE`, `CMETA_IMPLEMENTS`, and the existing CFlow interface declarations in `runtime.h` and `scheduler.h`.
- Produces: CFlow library and examples with no `CMETA_INTERFACE_IMPL` / `interface_impl` dependency.

- [ ] **Step 1: Remove out-of-line replay call sites**

Delete these statements without replacement:

```c
CMETA_INTERFACE_IMPL(cflow_waitable, CMETA_WAITABLE_METHODS)
CMETA_INTERFACE_IMPL(cflow_sink, CMETA_SINK_METHODS)
CMETA_INTERFACE_IMPL(cflow_source, CFLOW_SOURCE_METHODS)
CMETA_INTERFACE_IMPL(cflow_scheduler, CMETA_SCHEDULER_METHODS)
interface_impl(counter, COUNTER_METHODS)
```

The last natural-DSL line occurs in both interface examples.

- [ ] **Step 2: Update the demo text to name the remaining concepts**

In `demo_interface.c`, replace:

```c
puts("interface/implements: generated handle, vtable, wrappers, capabilities, metadata");
```

with:

```c
puts("interface: generated handle, vtable, wrappers, capabilities, metadata");
puts("implements: binds a concrete vtable to the interface");
```

- [ ] **Step 3: Build and test the CFlow consumers**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_graph_test cflow_pipeline_test && ctest --preset win-release-user -R ""^(cflow_graph_test|cflow_pipeline_test)$"" --output-on-failure"
```

Expected: both tests pass. This build compiles multiple CFlow translation units that include the same header-complete interface declarations.

- [ ] **Step 4: Compile both natural-DSL examples with Clang C11**

```powershell
clang.exe -std=c11 -fsyntax-only -I cmeta/include -I cflow/include `
    cflow/examples/demo_interface.c
clang.exe -std=c11 -fsyntax-only -I cmeta/include -I cflow/include `
    cflow/examples/demo_cmeta_standalone.c
```

Expected: both commands exit 0 without `interface_impl` declarations.

- [ ] **Step 5: Record a path-scoped checkpoint without committing overlapping user work**

```powershell
git diff --check -- cflow/src/interfaces.c cflow/src/scheduler.c `
    cflow/examples/demo_interface.c cflow/examples/demo_cmeta_standalone.c
git diff -- cflow/src/interfaces.c cflow/src/scheduler.c `
    cflow/examples/demo_interface.c cflow/examples/demo_cmeta_standalone.c
```

Expected: only replay removal and demo wording changes. Do not commit the staged example renames as part of this refactor.

---

### Task 3: Verify deletion, compatibility boundary, and regression scope

**Files:**
- Verify: `cmeta/include/cmeta/interface.h`
- Verify: `cmeta/tests/cmeta_core_test.c`
- Verify: `cflow/include/cflow/*.h`
- Verify: `cflow/src/*.c`
- Verify: `cflow/examples/*.c`

**Interfaces:**
- Consumes: completed Tasks 1-2.
- Produces: evidence that the deleted DSL is absent, C/C++ consumers build, runtime behavior passes, and compile-time cost stays bounded.

- [ ] **Step 1: Prove the obsolete API is absent from production and examples**

```powershell
rg.exe -n "interface_impl|CMETA_INTERFACE_IMPL" cmeta cflow
```

Expected: no matches. Any match is an incomplete migration; do not add an alias or fallback.

- [ ] **Step 2: Run Clang syntax checks for the focused tests**

```powershell
clang.exe -std=c11 -fsyntax-only `
    -I cmeta/include -I tinytest/include `
    cmeta/tests/cmeta_core_test.c
clang.exe -std=c11 -fsyntax-only `
    -I cmeta/include -I cflow/include -I tinytest/include -I cflow/tests `
    cflow/tests/cflow_graph_test.c
clang.exe -std=c11 -fsyntax-only `
    -I cmeta/include -I cflow/include -I tinytest/include -I cflow/tests `
    cflow/tests/cflow_pipeline_test.c
```

Expected: all three commands exit 0.

- [ ] **Step 3: Measure post-change hot-header compile time**

Repeat the five-sample command from Task 1 Step 1. Compute:

```text
delta_percent = (post_median_ms - baseline_median_ms) / baseline_median_ms * 100
```

Expected: no regression above 10%. If the median exceeds 10%, repeat with at least nine samples, discard the first warm-up, and investigate generated header volume before claiming completion.

- [ ] **Step 4: Run the complete MSVC Release build and test suite**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure"
```

Expected: build exits 0 and CTest reports zero failures.

- [ ] **Step 5: Inspect final changes and preserve the accepted ABI break**

```powershell
git diff --check -- cmeta cflow
git diff --stat -- cmeta cflow
git status --short -- cmeta cflow
```

Confirm explicitly in the handoff:

- `interface_impl` is deleted with no compatibility shim;
- precompiled consumers of old external wrapper symbols must rebuild;
- vtable layout, callable forwarding, capabilities, metadata contents, ownership, and runtime behavior remain unchanged;
- implementation changes remain uncommitted because relevant paths overlap the user's pre-existing staged work.
