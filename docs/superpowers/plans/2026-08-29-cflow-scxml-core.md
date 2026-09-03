# CFlow SCXML Core Frontend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an optional, source-located SCXML Core frontend that compiles into the existing native CFlow Statechart IR without coupling CFlow to XML or CSerde.

**Architecture:** Encapsulate a private, minimally patched cxml implementation behind `Salts::XmlParser`; build a separate owning `Salts::CFlowScxml` frontend over XmlParser and CFlow. Keep compilation transactional, bounded, deterministic, and fail-fast.

**Tech Stack:** C11, vendored cxml, CMeta, CFlow Statechart, TinyTest, CMake Presets, MSVC Release.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-core-design.md`

## Task 1: Establish XML facade RED tests

**Files:**

- Create: `parser/xml_parser/include/xml_parser/xml_parser.h`
- Create: `parser/xml_parser/test/xml_parser_test.c`
- Create: `parser/xml_parser/test/CMakeLists.txt`
- Modify: `parser/CMakeLists.txt`

- [x] Declare the opaque document/node, limits, locations, statuses, and borrowed-view contract.
- [x] Test namespace-aware element/attribute traversal and exact byte/line/column locations.
- [x] Test malformed XML, embedded NUL, and each configured hard limit.
- [x] Configure/build the new test and confirm RED because XmlParser has no implementation.

## Task 2: Import and isolate cxml

**Files:**

- Create: `parser/xml_parser/vendor/cxml/` production sources and upstream license/readme.
- Create: `parser/xml_parser/vendor/cxml/SALTS_VENDOR.md`
- Create: `parser/xml_parser/vendor/CMakeLists.txt`
- Modify: vendored lexer/parser files needed for structured diagnostics and locations.

- [x] Record upstream origin, imported version evidence, license, included modules, and every local patch.
- [x] Build only cxml core/XML utility sources; disable query, XPath, SAX, examples, and upstream tests.
- [x] Rename/isolate the private CMake target so it is neither exported nor installed.
- [x] Retain token/node byte offset, one-based line, and one-based column.
- [x] Return structured first-error data without printing from library code.

## Task 3: Implement the bounded XmlParser adapter

**Files:**

- Create: `parser/xml_parser/src/xml_parser.c`
- Create: `parser/xml_parser/CMakeLists.txt`
- Modify: `parser/CMakeLists.txt`

- [x] Copy explicit-length input into document-owned NUL-terminated storage after checked size validation.
- [x] Wrap cxml DOM types without exposing third-party headers, layouts, or ownership.
- [x] Enforce input, depth, node, attribute, and retained-string limits transactionally.
- [x] Map cxml errors to stable XmlParser statuses and caller-owned diagnostics.
- [x] Run XmlParser tests until GREEN and verify existing parser tests remain green.

## Task 4: Establish SCXML compiler RED tests

**Files:**

- Create: `cflow-scxml/include/cflow/scxml.h`
- Create: `cflow-scxml/tests/cflow_scxml_test.c`
- Create: `cflow-scxml/tests/fixtures/*.scxml`
- Create: `cflow-scxml/tests/CMakeLists.txt`
- Create: `cflow-scxml/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `CMakeOptions.cmake`

- [x] Declare the owning program, compile limits, diagnostics, borrowed Statechart, name lookup, initial-state, and null-event helpers.
- [x] Test root/version/datamodel admission and exact semantic error locations.
- [x] Test state, parallel, final, shallow/deep history, explicit/default initial, and transition lowering.
- [x] Test duplicate/unknown IDs and every explicitly unsupported Phase 2 construct.
- [x] Test deterministic state/event mappings and transactional empty output on failure.
- [x] Configure with `CFLOW_ENABLE_SCXML=ON`, build the new test, and confirm RED because the compiler implementation is absent.

## Task 5: Implement SCXML admission and lowering

**Files:**

- Create: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/CMakeLists.txt`

- [x] Normalize supported XML elements into bounded temporary declarations in depth-first document order.
- [x] Validate namespaces, attributes, IDs, structure, and unsupported constructs before publication.
- [x] Synthesize initial pseudo-states and lower event, eventless, completion, internal, and external transitions.
- [x] Bind the null data model to the documented inert CMeta bool value.
- [x] Build the native Statechart atomically; preserve the first deterministic phase-ordered diagnostic on all failure paths.
- [x] Run focused SCXML and native Statechart tests until GREEN.

## Task 6: Add executable trace fixtures

**Files:**

- Modify: `cflow-scxml/tests/cflow_scxml_test.c`
- Create: `cflow-scxml/tests/fixtures/core_trace.scxml`

- [x] Compile the fixture, initialize a Statechart instance, and verify its initial active configuration.
- [x] Construct/send named null-model events through the program helper.
- [x] Verify expected document-ordered active-state traces and terminal completion.
- [x] Confirm fixture source and expected native trace remain independent facts in the test.

## Task 7: Install and dependency verification

**Files:**

- Modify: `parser/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `cmake/VerifyInstalledPackage.cmake`
- Modify: installed consumer fixture files selected by that verification script.
- Modify: `README.md`

- [x] Export/install `Salts::XmlParser` while keeping cxml private.
- [x] Export/install `Salts::CFlowScxml` and its header only when `CFLOW_ENABLE_SCXML=ON`.
- [x] Assert `Salts::CFlow` has no XmlParser/cxml/CSerde link dependency.
- [x] Assert CFlowScxml exposes exactly CFlow and XmlParser at its public boundary.
- [x] Document the supported subset, limits, ownership, null data model, and non-conformance boundary.
- [x] Verify both disabled and enabled configure/install/consumer paths.

## Task 8: Full verification and delivery

**Files:**

- Inspect: all changed production, test, vendor, build, install, design, and documentation files.

- [x] Sync CodeGraph and inspect affected targets/callers.
- [x] Fresh-configure and build focused XML, SCXML, Statechart, and parser targets with the official MSVC Release preset.
- [x] Run focused tests, complete CFlow/parser regression tests, and installed-package verification.
- [x] Inspect the final diff for public ABI, unbounded growth, cxml leakage, CSerde coupling, placeholders, and accidental generated/index files.
- [x] Use `superpowers:verification-before-completion`, then `superpowers:finishing-a-development-branch` for handoff choices.
