# CFlow SCXML Optimal Transition Set Verification Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to execute this plan task-by-task.

**Goal:** Establish, with focused CFlow tests and TurboSCXML W3C fixtures 403b/403c, that the runtime computes the W3C optimal enabled transition set deterministically.

**Architecture:** Keep transition selection in the CFlow Statechart runtime and keep XML/W3C vocabulary in TurboSCXML. CFlow owns the immutable normalized transition spans, the active configuration, candidate de-duplication, exit-set conflict detection, and descendant-source priority. TurboSCXML only lowers SCXML into that format-neutral runtime model and supplies local corpus fixtures. The current `origin/master` already contains direct unit coverage for the required primitives; therefore the first task is a characterization gate. If those exact tests pass, production CFlow code must remain unchanged and the work proceeds as corpus promotion. If they fail, add the smallest runtime correction only after preserving the failing test.

**State and failure contract:** One `cflow_statechart_instance` is the sole mutable owner of selection scratch state. Selection observes one published configuration version and either returns one complete deterministic snapshot or a precise existing error; it must not publish partial semantic state. Targetless transitions have empty exit sets, a shared ancestor candidate is evaluated once, intersecting exit sets conflict, and a candidate whose source is a proper descendant may preempt an already selected ancestor transition.

**Compatibility:** No public API, wire format, configuration format, dependency direction, or deployment behavior changes. A runtime modification is allowed only if the characterization gate exposes a real semantic mismatch. TurboSCXML fixture promotion changes only the claimed conformance rows for 403b/403c and must preserve the upstream witness under the repository's null-datamodel transformation rules.

**Tech Stack:** C11, CFlow Statechart runtime, TurboSCXML, TinyTest, CMake Presets, official W3C SCXML Implementation Report fixtures, uSCXML reference behavior at `qigao/scxml@c80cedfa43b559861a054e992137685cdd29af16`.

---

### Task 1: Verify CFlow optimal-set primitives

**Files:**
- Verify: `cflow/src/statechart.c`
- Verify: `cflow/src/statechart_instance.c`
- Verify: `cflow/tests/cflow_statechart_instance_test.c`

- [x] Run the focused `cflow_statechart_instance_test` cases covering ancestor candidate de-duplication, unrelated-source conflict ordering, descendant-source preemption, targetless compatibility, and repeated-run determinism.
- [x] Record whether the current implementation is already GREEN. The characterization gate is GREEN; no production-code or test-code change was made.
- [x] N/A (GREEN): no failing assertion required preservation and no normalized-ordering or `filter_candidate` correction was needed.
- [x] Compare the resulting behavior with W3C 403b/403c and the pinned uSCXML reference without copying implementation code.

**Task 1 evidence (2026-08-31):** After `codegraph sync .`, configured with the documented `win-release-user` preset in `VsDevCmd.bat`, then built `cflow_statechart_instance_test` (Ninja: `no work to do`). Each direct TinyTest filter passed independently: ancestor de-duplication 7 assertions; unrelated conflict ordering 7; descendant-source preemption 7; targetless compatibility 9; repeated-run determinism 8 (all `1 passed, 0 failed, 135 filtered`). This is a characterization ruling only: `candidate_seen` de-duplicates a shared ancestor candidate, targetless transitions have an empty exit set, and `filter_candidate` retains the earlier unrelated conflict while allowing a proper descendant to preempt. These primitives match W3C assertion 403's optimal-enabled-set contract and the witnesses in [test403b](https://www.w3.org/Voice/2013/scxml-irp/403/test403b.txml) and [test403c](https://www.w3.org/Voice/2013/scxml-irp/403/test403c.txml); comparison was semantic only, with no reference implementation copied. The plan-pinned `qigao/scxml@c80cedfa43b559861a054e992137685cdd29af16` is retained as the reference identifier; its GitHub commit endpoint was not publicly retrievable during this verification, so this task does not claim an independent execution of that reference.

### Task 2: Verify and install the TurboUtils integration artifact

**Files:**
- Verify: `cflow/tests/cflow_statechart_instance_test.c`
- Verify: `CMakePresets.json`
- Verify: `CMakeUserPresets.json`

- [ ] Run the complete `cflow_statechart_instance_test` target.
- [ ] Run the adjacent CFlow Statechart test set.
- [ ] Install the verified TurboUtils build into a fresh, explicit integration prefix.
- [ ] Record the source commit, preset, install prefix, and exact test results so TurboSCXML cannot accidentally link a stale package.

### Task 3: Add TurboSCXML 403b/403c fixtures and registrations

**Files:**
- Create: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403b.scxml`
- Create: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403c.scxml`
- Modify: `C:/projects/cpp/turbonet/scxml/tests/scxml_w3c_conformance_test.c`
- Modify: `C:/projects/cpp/turbonet/scxml/tests/w3c/manifest.tsv`
- Modify: `C:/projects/cpp/turbonet/scxml/tests/w3c/README.md`

- [ ] Add named harness cases before adding fixtures and confirm RED only because the fixture files are absent.
- [ ] Add local null-datamodel transformations that preserve the 403b ancestor de-duplication/preemption witness and the 403c targetless/conflicting-descendant/wildcard coexistence witness.
- [ ] Replace generator-only pass/fail vocabulary and timeout liveness machinery using the established local corpus conventions; do not weaken the transition-selection assertion.
- [ ] Promote only rows 403b and 403c from `UNSUPPORTED/NOT_RUN/NONE` to `PASS/TERMINAL_PASS` with an exact transformation note and witness.
- [ ] Document provenance and explicitly retain the non-certification scope statement.

### Task 4: Verify TurboSCXML from a fresh build tree

**Files:**
- Verify: `C:/projects/cpp/turbonet/scxml/tests/scxml_w3c_conformance_test.c`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403b.scxml`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403c.scxml`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/manifest.tsv`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/README.md`

- [ ] Configure TurboSCXML in a fresh build tree against the exact TurboUtils install from Task 2.
- [ ] Run the two named W3C cases, then the complete W3C conformance test, then adjacent/full CTest as justified by the changed claim.
- [ ] Confirm manifest accounting changes from 114 mandatory PASS / 54 mandatory UNSUPPORTED to 116 / 52, while 34 optional rows remain N/A.
- [ ] Synchronize CodeGraph and inspect the affected-file/test report.

### Task 5: Review and prepare integration

- [ ] Keep any CFlow code/test commit separate from the TurboSCXML corpus commit. If Task 1 is already GREEN, do not create an empty or cosmetic CFlow functionality commit.
- [ ] Run a requirements review followed by a code-quality review over each repository's exact diff.
- [ ] Use the finishing-development-branch workflow to present integration choices; do not push, open a PR, merge, or update issue #122 without a subsequent explicit user request.
