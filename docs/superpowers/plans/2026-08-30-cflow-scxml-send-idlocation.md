# CFlow SCXML Send Idlocation Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add bounded, transactional CMeta support for SCXML `send/@idlocation` without changing public ABI.

**Architecture:** Compile `idlocation` into the existing internal CMeta location descriptor, generate session-unique IDs during send execution, assign them to staged state through the checked CMeta buffer adapter, and copy delayed IDs into bounded registry-owned storage.

**Tech Stack:** C11, CMeta data descriptors, CFlow SCXML runtime, TinyTest, CMake Presets/CTest.

---

### Task 1: Add red admission and execution tests

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

1. Add a fixed-capacity owned CMeta string field to the existing test schema.
2. Add admission tests for accepted and rejected `idlocation` destinations.
3. Add runtime tests for state writeback, delayed cancellation identity, repeated-ID uniqueness, and internal sends.
4. Build and run `cflow_scxml_cmeta_test`; confirm failures are caused by the current unsupported-feature rejection.

### Task 2: Compile bounded idlocation descriptors

**Files:**
- Modify: `cflow-scxml/src/scxml.c`

1. Add `has_id_location` plus one location descriptor to send effects.
2. During analysis, validate the location path against the CMeta root and require an owned writable string adapter.
3. Permit a delayed send whose missing literal ID is supplied by `idlocation`.
4. During emission, compile and retain the location descriptor without adding public storage.

### Task 3: Generate, assign, and retain runtime IDs

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/src/cmeta_location.h`
- Modify: `cflow-scxml/src/cmeta_location.c`

1. Add a checked internal helper that replaces a string at a compiled location.
2. Add a session-owned monotonic send counter and bounded formatting helper.
3. Materialize one generated ID per idlocation send after other arguments succeed.
4. Assign the ID to `out_state` before staging the send effect.
5. Make delayed-send rows copy and own their bounded IDs.

### Task 4: Document and verify

**Files:**
- Modify: `cflow/README.md` or the closest SCXML feature matrix/documentation.

1. Document supported `send/@idlocation` and remaining `invoke/@idlocation` limitation.
2. Run the focused CMeta SCXML test.
3. Run all `cflow_scxml` CTest cases.
4. Run the full configured CTest preset.
5. Review the diff and commit only intended files on `feat/cflow-scxml-idlocation`.
