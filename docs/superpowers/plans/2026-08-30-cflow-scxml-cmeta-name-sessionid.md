# CFlow SCXML CMeta `_name` / `_sessionid` Implementation Plan

> **Execution mode:** inline in the existing `feat/cmeta-buffer-read-view` worktree.

**Goal:** Implement the read-only SCXML `_name` and `_sessionid` string variables for the CMeta data model without changing the public session configuration ABI.

**Architecture:** The expression compiler lowers the two reserved identifiers to dedicated scalar operands. Evaluation receives a call-scoped immutable system-value view. The SCXML program retains the optional root `name`; each owning session copies that name and generates one UUID string, then session-specific guard and executable bindings inject those values. Program-level low-level bindings can resolve `_name`, but `_sessionid` remains unavailable without an owning `cflow_scxml_session` and fails evaluation explicitly.

**Ownership and bounds:** The program owns the retained document name until program destruction. Each session owns its copied `_name` bytes and its inline UUID text until successful session destruction. Expression results borrow those bytes only for the enclosing guard or executable callback. The document name participates in `max_name_bytes`; all system strings participate in `max_string_bytes`. Session initialization is single-shot; UUID generation failure aborts initialization before attachment.

**Tech stack:** C11, CMeta descriptors, QueryVM, Salts Core UUID, TinyTest, CMake presets.

---

### Task 1: Specify expression system-value behavior with failing tests

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_expr_test.c`
- Modify: `cflow-scxml/src/cmeta_expr.h`
- Modify: `cflow-scxml/src/cmeta_expr.c`

1. Add tests proving `_name` and `_sessionid` compile as strings, compare correctly, honor `max_string_bytes`, and fail evaluation when `_sessionid` is unavailable.
2. Run `cflow_scxml_cmeta_expr_test` and record the expected RED failure.
3. Add dedicated system operands and a call-scoped immutable system-value input to the private evaluator API.
4. Re-run the focused test to GREEN.

### Task 2: Admit and retain the SCXML root name

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`

1. Add tests for valid `name`, invalid NMTOKEN syntax, and `max_name_bytes` accounting.
2. Run `cflow_scxml_test` and record RED.
3. Admit the optional unqualified `name` attribute, validate it as XML NMTOKEN, and retain it with checked arithmetic in program-owned storage.
4. Re-run the focused test to GREEN.

### Task 3: Inject session-owned immutable system strings

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/src/cmeta_assign.h`
- Modify: `cflow-scxml/src/cmeta_assign.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

1. Add public integration tests whose guard and assignment source read `_name` and `_sessionid`, while writes to both names remain rejected.
2. Run `cflow_scxml_cmeta_test` and record RED.
3. Generate a UUID v4 during session initialization, copy the program name into session storage, and build session-specific guard/executable adapters.
4. Thread the call-scoped system view through guard and assignment evaluation.
5. Cover every initialization rollback and successful destruction path for the new allocations and binding rows.
6. Re-run the focused integration test to GREEN.

### Task 4: Document boundaries and verify

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

1. Document read-only semantics, ownership, bounds, UUID generation, and the absence of `_sessionid` from program-level low-level bindings.
2. Build and run the three SCXML targets/tests under Debug and ASan presets.
3. Run the adjacent Release CTest suite.
4. Run `codegraph sync .`, inspect `git diff --check`, and report remaining risks without committing or pushing.

### Verification results

- Debug ASan focused tests: 3/3 passed (`cflow_scxml_test`, `cflow_scxml_cmeta_expr_test`, `cflow_scxml_cmeta_test`).
- Release adjacent regression suite: 199/199 passed.
- `codegraph sync .`: completed; 9 changed files re-indexed.
- `git diff --check`: clean.
