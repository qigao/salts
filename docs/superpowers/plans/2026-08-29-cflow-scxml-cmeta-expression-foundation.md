# CFlow SCXML CMeta Expression Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a private, bounded CMeta boolean-expression compiler and evaluator that can later back `datamodel="cmeta"` guards and executable-content conditions without changing the current public SCXML API.

**Architecture:** `cflow-scxml` owns parsing and CMeta location semantics, compiles expressions once into immutable QueryVM instructions and immutable operands, and evaluates them allocation-free against one borrowed root object plus a borrowed active-state callback. The root object remains the sole state fact source. This increment supports Boolean, signed/unsigned integer, floating-point, and enum field locations; CMeta string reads and sequence indices remain rejected until their descriptor APIs expose explicit read/traversal contracts.

**Tech Stack:** C11, CMeta semantic descriptors, Salts QueryVM, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

## Global Constraints

- Keep every installed `cflow/scxml.h` API and all null-data-model behavior unchanged.
- Compile in two passes: count/validate first, then allocate exact immutable instruction, operand, path, and string storage.
- Runtime evaluation performs no heap allocation and never retains the borrowed root object or active-state callback context.
- Bound source bytes, instructions, operands, expression nesting/register depth, path depth, and retained literal bytes; reject overflow before allocation.
- A location is an identifier followed by zero or more `.field` selectors. Every selected descriptor must be valid and every cumulative offset plus leaf size must fit the parent storage.
- Bare values are valid conditions only when Boolean. `&&` and `||` use C-like
  short-circuit evaluation. Comparisons are type checked; signed/unsigned mixed
  comparisons must not narrow through `double`.
- `In` accepts exactly one quoted state name and resolves it during compilation through a caller callback. Evaluation only invokes the borrowed active-state callback.
- Unsupported string locations, containers, variants, custom values, scripts, assignments, and `foreach` remain outside this increment.

---

### Task 1: Add the failing behavior tests

**Files:**
- Create: `cflow-scxml/tests/cflow_scxml_cmeta_expr_test.c`
- Modify: `cflow-scxml/tests/CMakeLists.txt`

- [x] Define reflected nested test structs and valid CMeta semantic descriptors for Boolean, signed, unsigned, floating-point, and enum leaves.
- [x] Add tests for default limits and invalid arguments.
- [x] Add tests for literals, precedence, parentheses, logical operators, and `In("state")`.
- [x] Add tests for nested field paths and same/mixed numeric comparisons without lossy conversion.
- [x] Add tests for unknown paths, invalid nested descriptors, scalar-as-Boolean type mismatches, malformed syntax, depth/instruction/source limits, and unchanged output on failure.
- [x] Register a private test target that can include `cflow-scxml/src`.
- [x] Configure and build the new target, confirming RED because the private expression API does not yet exist.

### Task 2: Implement the bounded private compiler

**Files:**
- Create: `cflow-scxml/src/cmeta_expr.h`
- Create: `cflow-scxml/src/cmeta_expr.c`
- Modify: `cflow-scxml/CMakeLists.txt`

- [x] Define private status, limits, diagnostics, opaque program handle, state-name resolver, and active-state callback contracts.
- [x] Implement a tokenizer and recursive-descent parser for `||`, `&&`, comparison operators, unary `!`, parentheses, literals, locations, and `In`.
- [x] Perform a validation/count pass that resolves reflected paths, calculates exact storage, enforces all bounds, and reports the first byte offset.
- [x] Perform an emission pass that resolves each state name once into exact program-owned QueryVM instructions and operands; verify the resulting QueryVM slice before publishing the program.
- [x] Guarantee failure leaves the destination program empty and releases every partial allocation through one cleanup path.
- [x] Add QueryVM as a private `CFlowScxml` dependency and preserve the existing public-link dependency contract.
- [x] Build the new target and run its focused TinyTest executable, confirming GREEN.

### Task 3: Implement allocation-free evaluation

**Files:**
- Modify: `cflow-scxml/src/cmeta_expr.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_expr_test.c`

- [x] Resolve compiled location operands by checked offset from the borrowed root object and decode scalar storage with `memcpy`.
- [x] Read enums only through `cmeta_data_enum_read` and propagate provider failure as evaluation failure.
- [x] Implement exact Boolean, numeric, enum, and comparison semantics in the QueryVM backend, including signed/unsigned boundary cases and explicit NaN behavior.
- [x] Evaluate `In` through the borrowed callback and require a Boolean result register.
- [x] Preserve caller output on every evaluation failure and return stable diagnostic status/message.
- [x] Run focused expression tests and adjacent `cflow_scxml_test`.

### Task 4: Verify integration and document the increment boundary

**Files:**
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

- [x] Add an implementation-status note distinguishing this private scalar-expression foundation from public `datamodel="cmeta"` admission, assignment, sequence traversal, and system-variable integration.
- [x] Run CodeGraph affected-file analysis and inspect every reported direct consumer.
- [x] Run the Windows Release focused targets and CTest filters.
- [x] Run the Windows Release full build and full CTest suite.
- [x] Confirm `git diff --check`, no focused TinyTest markers, and a clean diff limited to this increment.
