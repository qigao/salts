# Allocator-bound Vec SDK Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to execute and verify each task.

**Goal:** Resolve #258 without weakening salts-utils#52's complete-install gate.
**Architecture:** An opaque allocator-bound owner uses the existing Vec growth,
staging and lifecycle engine. A private allocation policy supplies exact-byte
receipts. The ordinary vec_t layout and its default allocator remain unchanged.
**Tech Stack:** C11/C++17, CSTL/CMeta/TinyTest, CMake, GitHub Actions.
**Spec:** #258 and salts-utils `jinja/EXTENSIBILITY_DESIGN.md` Stage 2; the
consumer signatures are in `jinja_cmeta_values.h`, `jinja_cmeta_text.h` and
`parser/jinja_template_parser.c` at 8d5aba520d854e138332ab302f00f42950beac2f.

## Global constraints and design

- Begin from 66b42c06bc812e73f4cbf12fc7a0b2b9408f401f, retaining native XML SAX.
- Supply cstl/allocator.h, cstl/vec_alloc.h through the existing CSTL install rule.
- stl_allocator contains context, allocate(context, bytes, out), and
  deallocate(context, data, bytes). Copy callbacks; borrow their context.
- Require an explicit valid allocator. Never switch allocator after failure.
- vec_alloc_new(type, limit, allocator, out) and
  vec_alloc_new_bytes(size, alignment, limit, allocator, out) publish an opaque
  owner. Its view is const vec_t, not permission for ordinary Vec mutation/free.
- push, resize, at, view and destroy are the entire required surface.
- Charge owner, backing capacity, alignment receipt/padding and scratch copies.
  Growth admits old plus new storage before releasing the old allocation.
- Overflow, element capacity, provider capacity and allocation failure must
  preserve values, storage address, size/capacity and generation.
- Byte resize zero-fills; typed growth is rejected as in ordinary Vec; shrink
  destroys removed elements. Copy/move/destroy use existing CMeta traits.
- Metadata is immutable/borrowed. Trait-owned payload allocations remain the
  trait provider's responsibility, not hidden charges to this container allocator.
- Single-threaded owner; callbacks do not reenter it; destroy requires quiescence.
- No allocator-aware mutation is added to ordinary vec_t. No Jinja/CI gate removal.

## Task 1: Failure-first provider tests

Files: cstl/tests/cstl_vec_alloc_test.c, cstl_vec_alloc_cpp_test.cpp,
CMakeLists.txt, allocator_installed/CMakeLists.txt, and the allocator CI workflow.

- [x] Read the complete base via its verified source artifact; reconstruct its
  tree 1a1e8c521207cbc3dfc0d69370386e15f1a4f0a7 before editing.
- [x] Run the existing Vec/ownership/C/C++ header tests in a local standalone
  source harness with Clang ASan+UBSan; four executables pass.
- [x] Write a real allocator ledger checking pointer/byte receipts and injected
  owner, scratch and growth failures; cover managed self-copy, shrink, alignment,
  exact capacity, byte overflow, copied callbacks and two independent owners.
- [x] Compile the new test against unchanged base headers. Expected and observed:
  `fatal error: 'cstl/vec_alloc.h' file not found` (API/header RED, not assertion RED).
- [ ] Publish tests and the exact-head CI using GitHub Git objects.

## Task 2: Canonical provider, no duplicate Vec algorithm

Files: cstl/include/cstl/{allocator,vec_alloc}.h,
cstl/src/vec_memory.h and cstl/src/vec.c.

- [x] Add the callback and opaque-owner declarations with documented ownership.
- [x] Add a private allocation receipt containing raw pointer and requested bytes;
  use checked size/stride/alignment arithmetic and memcpy for receipt alignment.
- [x] Share the existing Vec push/resize/growth helpers with an explicit storage
  policy. Ordinary entry points select their original policy; bound owners supply
  their copied allocator. Mutating algorithms and type traits are not duplicated.
- [x] Run new provider tests and existing regressions with ASan+UBSan locally.
- [ ] Run exact-head native SDK install and independent C/C++ installed consumers.
- [ ] Publish verified immutable provider revision; keep #258 open until its gates pass.

## Task 3: Restore downstream installation

Files: salts-utils' three TBE workflow pins and matching source-SHA assertions.

- [ ] Replace only Core revision with the accepted provider commit; retain re2c
  repair and all existing sanitizer, source-identity, Jinja/install/consumer gates.
- [ ] Verify full TBE and native installed consumer. Check Jinja tests separately;
  successful compilation alone is not its regression acceptance.
- [ ] Record exact SHAs and failures/successes in #258 and salts-utils#52. Do not
  transfer old green totals to the new source or claim main restored before merge.

## Local verification boundary

The local harness compiles real CMeta/CSTL/TinyTest source without the full SDK's
unavailable third-party dependencies. It is not full Core install verification.
Git transport and CodeGraph are unavailable here; connector reads, verified
archives, source inspection and exact-parent connector writes are used instead.
