# Allocator-bound Vec — #258

This publishes the Core API already consumed by salts-utils Jinja. It is additive:
ordinary `vec_t`, its ABI, allocation policy and mutable APIs are unchanged.
The implementation owns one `vec_t` inside opaque `vec_alloc_t`, reusing native
Vec initialization, const queries, shrinking/destruction and sequence lifecycle
primitives. No provider or compatibility header belongs in salts-utils.

## Contract

`stl_allocator` contains context/allocate/deallocate. Owners copy the function
pointers and borrow context until the final release. The allocator must supply
ordinary C alignment; the owner handles over-aligned elements with checked slack.
The caller serializes mutation and must not reenter an owner from its callbacks.
There is no implicit heap allocator and errors do not change allocation domains.

Every owner, backing block, alignment slack and push snapshot is charged to the
same allocator. Each release uses its original pointer and requested byte count.
The old backing remains live while new backing is admitted: limits apply to peak
requested bytes, not only retained payload. Element-internal allocations remain
the element's responsibility. A zero element limit permits an empty owner but
no elements. Arithmetic overflow is CAPACITY before calling the allocator.

A push snapshots its input before growth, so its source may be an existing element.
Fallible allocation/copy completes before moving any live element. Allocation
failure preserves content, size, capacity and generation; copied temporaries are
destroyed and released. Typed metadata is borrowed immutable, including the
non-failing move/destroy callbacks admitted by the existing native Vec contract.
Copy failure maps to OUT_OF_MEMORY consistently with vec_push. Raw byte resize
zero-fills growth; typed resize growth returns TRAIT_MISSING because no default
constructor is in the existing type protocol. Shrink destroys removed elements.

Views are borrowed. Call only const Vec queries on vec_alloc_view; never pass it
to ordinary mutable Vec APIs or free it independently. Successful mutation or
destruction invalidates outstanding views. Independent owner instances share no
mutable state other than an explicitly shared caller allocator.

## Implementation and validation sequence

1. Add C provider tests and a C++17 consumer using the exact Jinja signatures.
   Original Core fails compilation for missing cstl/vec_alloc.h (API-absence RED).
2. Publish allocator.h, vec_alloc.h and vec_alloc.c in Salts::CSTL. Existing
   directory-based header installation publishes the two headers; the normal
   CSTL target includes the implementation. No generated or hand-written stub.
3. Run provider tests plus unchanged sequence and ownership regressions with
   ASan/UBSan. Test failure injection covers owner, snapshot and replacement
   allocation; other cases cover aligned strides, limits, typed copy rollback,
   aliased growth, exact release accounting and the C++ C-linkage boundary.
4. Run exact-head Core CI including existing DOM/SAX prerequisites. Then update
   salts-utils#52's three Core pins and exact-SHA assertions to this revision.
   The unchanged full install and installed-consumer gates must pass. Add/run
   Jinja memory regression coverage rather than disabling the dependent module.

Local source-based tests are not install/export evidence or all-platform
acceptance. The provider branch intentionally descends from the downstream
native-SAX/CMeta pin; replacing it blindly with master would lose prerequisites.
