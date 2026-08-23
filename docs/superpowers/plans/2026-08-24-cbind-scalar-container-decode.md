# CBind Scalar Container Decode Implementation Plan

> Execute this plan with test-driven development: each behavior starts as a
> focused failing test, then receives the smallest production change, followed
> by the adjacent regression set.

**Goal:** Decode bounded scalar sequence/set/map fields in reflected structs
while preserving D2 preflight and whole-root rollback guarantees.

**Architecture:** CBind combines semantic field shapes with reflection-side
`cmeta_declared_type`, prepares concrete containers before source consumption,
feeds strict scalar values through CMeta Collectors, and restores every container
through an appended construction lifecycle callback on failure.

## Task 1: Add the CMeta restore-zero contract

**Files:**

- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/src/container_type.c`
- Modify: `cmeta/tests/cmeta_container_type_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**RED:** Add tests with a fake construction provider proving the checked façade
rejects null/short/bad-ABI declarations, invokes the callback, restores zero,
and permits legacy binding ops without claiming restore support.

**GREEN:** Append `cmeta_container_restore_zero_fn`, append `restore_zero` to the
ops structure, and implement `cmeta_container_restore_zero()`. Keep the existing
`cmeta_container_bind_types()` validation prefix unchanged; validate the longer
prefix only in the new façade.

**Verify:** Build and run `cmeta_container_type_test` and
`cmeta_header_cpp_test`.

## Task 2: Implement restore-zero for TurboSTL providers

**Files:**

- Modify: `turbostl/src/construction_meta.c`
- Modify: `turbostl/tests/turbostl_construction_binding_test.c`

**RED:** For representative sequence, set, and map handles, test zero,
bound-uninitialized, populated/committed, and repeated restore calls. Assert the
entire handle storage becomes bytewise zero and owned contents are released.

**GREEN:** Add provider-specific wrappers that call the established concrete
destroy operation when necessary and clear the full handle. Register each
wrapper in the corresponding construction ops table; do not expose layouts to
CBind.

**Verify:** Run `turbostl_construction_binding_test` plus the existing TurboSTL
collector tests.

## Task 3: Extend CBind ABI tails and scalar token reuse

**Files:**

- Modify: `cbind/include/cbind/context.h`
- Modify: `cbind/include/cbind/error.h`
- Modify: `cbind/include/cbind/status.h`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/tests/cbind_scalar_decode_test.c`
- Modify: `cbind/tests/cbind_header_cpp_test.cpp`

**RED:** Test the new context macro, legacy prefix acceptance, appended error
field guarding, stable old status values, and decoding a scalar from a supplied
token without an extra reader call.

**GREEN:** Append `max_container_items`, `target_status`, and
`CBIND_TARGET_ERROR`. Add prefix-aware target-status writes. Split scalar decode
into `cbind_decode_scalar_token()` and the existing read wrapper without changing
conversion rules.

**Verify:** Run all current `cbind_*` tests; they must still pass before adding
container behavior.

## Task 4: Add container graph validation and preparation

**Files:**

- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/src/struct.c`
- Add: `cbind/src/container.c`
- Modify: `cbind/CMakeLists.txt`
- Add: `cbind/tests/cbind_container_preflight_test.c`

**RED:** Define reflected Struct fixtures for supported and unsupported
declarations. Assert wrong category/arity/scalar type, absent restore callback,
short context, and nonzero destination fail before the reader is called. Assert
a later bind failure restores earlier bound fields.

**GREEN:** Resolve each semantic field to its reflected field, validate the
declaration/provider contract, calculate unchanged bitmap scratch, check
bytewise-zero container storage, recursively bind fields, and recursively restore
them. Pass reflected declaration metadata into value dispatch.

**Verify:** Run the new preflight target plus D2 preflight, transaction, and
error-attribution tests.

## Task 5: Decode sequence, set, and map fields

**Files:**

- Modify: `cbind/src/container.c`
- Add: `cbind/tests/cbind_container_decode_test.c`
- Modify: `cbind/CMakeLists.txt`

**RED:** Add table-driven success tests for sequence/set/map, empty and exact
limits, strict scalar mismatches, malformed delimiters, source failure,
limit+1, Collector capacity/error propagation, nested structs, and rollback of
an earlier committed container after a later failure.

**GREEN:** Create the declared Collector, begin it after the matching begin
token, decode bounded scalar items or map entries, accept them, and finish at the
matching end token. On every failure, abort an active Collector and rely on root
restore for bound or committed storage. Map CMeta status to CBind status while
retaining `target_status`.

**Verify:** Run both new container targets and all `cbind_*` tests.

## Task 6: Integration and compatibility verification

**Files:**

- Modify as required: `tests/install_consumer/consumer.c`
- Modify as required: `tests/install_consumer/consumer.cpp`
- Update only if public surface changed beyond this design: CBind/CMeta docs

Run, in order:

1. CBind and CMeta targeted tests.
2. TurboSTL construction and Collector tests.
3. all `cbind_*`, `cmeta_*`, and relevant `turbostl_*` CTest entries.
4. install-consumer C and C++ builds/tests.
5. the repository release preset test suite if the focused sets are green.

Inspect `git diff --check`, the final diff, and CodeGraph affected tests. Record
exact commands, pass/fail counts, and any unexecuted scope before claiming
completion.
