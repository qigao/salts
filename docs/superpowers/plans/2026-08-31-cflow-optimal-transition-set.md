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

- [x] Run the complete `cflow_statechart_instance_test` target.
- [x] Run the adjacent CFlow Statechart test set.
- [x] Install the verified TurboUtils build into a fresh, explicit integration prefix.
- [x] Record the source commit, preset, install prefix, and exact test results so TurboSCXML cannot accidentally link a stale package.

**Task 2 evidence (2026-08-31):** In `VsDevCmd.bat` with the documented `win-release-user` preset, `ctest -N -R "^cflow_statechart.*$"` discovered exactly five adjacent tests: `cflow_statechart_test`, `cflow_statechart_instance_test`, `cflow_statechart_instance_adapter_test`, `cflow_statechart_hierarchy_adapter_test`, and `cflow_statechart_actor_test`. The complete instance test passed `1/1` in `0.08 sec`; after a complete Release build, the anchored adjacent set passed `5/5` in `0.19 sec` (both `0 failed`). Source HEAD was `ec7f1b6b0ef2119c0406a5da2e934925d86837cb`. The initially absent dedicated prefix `C:/projects/cpp/turbonet/.integration/cflow-optimal-transition-set-403bc/turboutils/release` was installed with `cmake --install build/Msvc-Release --prefix ... --config Release`; it contains `lib/cmake/TurboUtils/TurboUtilsConfig.cmake` and `lib/turbo_cflow.lib`. `TurboUtilsTargets-release.cmake` imports `TurboUtils::CFlow` from that exact `turbo_cflow.lib`, preventing the Task 4 consumer from selecting a stale TurboUtils package.

### Task 2B: Restore the root-final completion boundary exposed by integration

**Files:**
- Modify: `cflow/src/statechart_instance.c`
- Modify: `cflow/tests/cflow_statechart_instance_test.c`
- Reuse commit: `cb70179fef68209b57f314cf61f73a8bfc02e3f0`

- [x] Preserve the reproduced RED: unchanged TurboSCXML test415 passes with the 2026-08-30 installed SDK but fails with the Task 2 SDK because the final `onentry`-raised internal Event reaches `on_event` before termination.
- [x] Integrate the existing local CFlow commit that checks root completion before popping internal Events; do not edit or recreate the fix.
- [x] Run its focused root-final TinyTest, the complete instance test, and the five-test adjacent Statechart set.
- [x] Reinstall the same dedicated SDK prefix and prove its installed CFlow archive matches the rebuilt archive.
- [x] Rebuild the unchanged TurboSCXML test executable against that SDK and confirm test415 GREEN before Task 4 begins.

**Root-cause ruling:** The Task 2 source was based on `origin/master` and omitted local commit `cb70179f`, which already contains a failing CFlow regression test and the minimal root-completion boundary fix. A same-binary A/B run proved unchanged TurboSCXML test415 GREEN against the older installed SDK and RED against the Task 2 SDK (`selected=1`). The missing check allowed `statechart_driver_run` to pop and notify an internal Event raised by root-final entry before `settle_quiescent_macrostep` won clean termination. Task 2B is therefore an integration correction, not part of optimal-transition-set selection and not a reason to weaken 403b/403c.

**Task 2B evidence (2026-08-31):** The preserved integration A/B remained the RED characterization: the unchanged TurboSCXML `test415` was GREEN with the 2026-08-30 SDK and RED against the Task 2 SDK with `selected=1`. The exact local commit `cb70179fef68209b57f314cf61f73a8bfc02e3f0` cherry-picked without conflicts as `a21b545758a4b2d73aa2048b90a4e25e436d9e4c`; its source and regression test were not edited. Under `VsDevCmd.bat` and `win-release-user`, the rebuilt focused TinyTest filter `halts` passed `1/1` (10 assertions), the complete instance CTest passed `1/1` in `0.40 sec`, and the full rebuilt adjacent `^cflow_statechart.*$` set passed `5/5` in `0.66 sec` (all `0 failed`). Reinstalling `build/Msvc-Release` into `C:/projects/cpp/turbonet/.integration/cflow-optimal-transition-set-403bc/turboutils/release` produced matching SHA-256 values for built and installed `turbo_cflow.lib`: `F03B4B17BA408DFCB81EF9B0353D0F5429A9D920451778BF613BD5ECF1B41459`. TurboSCXML was fresh-configured in its untracked `build/Msvc-Release-403bc` directory with `TURBOUTILS_ROOT` set to that exact prefix, rebuilt `scxml_w3c_conformance_test`, and ran unchanged test415 GREEN: `1/1`, one assertion, `0 failed`.

### Task 3: Add TurboSCXML 403b/403c fixtures and registrations

**Files:**
- Create: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403b.scxml`
- Create: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403c.scxml`
- Modify: `C:/projects/cpp/turbonet/scxml/tests/scxml_w3c_conformance_test.c`
- Modify: `C:/projects/cpp/turbonet/scxml/tests/w3c/manifest.tsv`
- Modify: `C:/projects/cpp/turbonet/scxml/tests/w3c/README.md`

- [x] Add named harness cases before adding fixtures and confirm RED only because the fixture files are absent.
- [x] Add local null-datamodel transformations that preserve the 403b ancestor de-duplication/preemption witness and the 403c targetless/conflicting-descendant/wildcard coexistence witness.
- [x] Replace generator-only pass/fail vocabulary and timeout liveness machinery using the established local corpus conventions; do not weaken the transition-selection assertion.
- [x] Promote only rows 403b and 403c from `UNSUPPORTED/NOT_RUN/NONE` to `PASS/TERMINAL_PASS` with an exact transformation note and witness.
- [x] Document provenance and explicitly retain the non-certification scope statement.

**Task 3 evidence (2026-08-31):** On the isolated TurboSCXML branch based at `4776015`, named 403b/403c harness cases were added first and failed only because their fixture files were absent. Commit `a75af9e` then added the two null-datamodel corpus transformations, manifest registrations, and provenance text. Review rejected an initially split 403c wildcard as a weakened witness; commit `2d19ac8` restored the upstream identity property by using one `event="*"` transition with conditional content, and added a structural regression that first failed against the split form and then passed. The final 403b trace proves shared-ancestor candidate de-duplication and descendant priority; the final 403c trace proves targetless coexistence, conflicting descendant priority, and selection of the same wildcard transition in both rounds. Re-review found no remaining HIGH/MED/LOW issue and approved the corpus change. No production source, public API, dependency, or build configuration changed.

### Task 4: Verify TurboSCXML from a fresh build tree

**Files:**
- Verify: `C:/projects/cpp/turbonet/scxml/tests/scxml_w3c_conformance_test.c`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403b.scxml`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/test403c.scxml`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/manifest.tsv`
- Verify: `C:/projects/cpp/turbonet/scxml/tests/w3c/README.md`

- [x] Configure TurboSCXML in a fresh build tree against the exact TurboUtils install from Task 2B.
- [x] Run the two named W3C cases, then the complete W3C conformance test, then adjacent/full CTest as justified by the changed claim.
- [x] Confirm manifest accounting changes from 114 mandatory PASS / 54 mandatory UNSUPPORTED to 116 / 52, while 34 optional rows remain N/A.
- [x] Synchronize CodeGraph and inspect the affected-file/test report.

**Task 4 evidence (2026-08-31):** The final fresh build tree is `C:/projects/cpp/turbonet/scxml-403bc-worktree/build/Msvc-Release-403bc-final`. Its cache records `TurboUtils_DIR=C:/projects/cpp/turbonet/.integration/cflow-optimal-transition-set-403bc/turboutils/release/lib/cmake/TurboUtils`, and the Ninja link input names the dedicated prefix's `turbo_cflow.lib` whose SHA-256 is `F03B4B17BA408DFCB81EF9B0353D0F5429A9D920451778BF613BD5ECF1B41459`. Four exact filters passed independently: inventory (7 assertions), 403b behavior (1), 403c behavior (1), and the single-wildcard structural test (4). The complete W3C executable passed all 120 TinyTest entries / 286 assertions, including 119 corpus cases; full CTest passed `8/8`. Independent manifest parsing proved only 403b/403c changed, producing 116 mandatory PASS, 52 mandatory UNSUPPORTED, and 34 optional N/A rows. CodeGraph was synchronized and reported no mapped affected tests; direct focused and full-suite execution supplies the behavioral evidence. The first attempted fresh run was discarded because preset environment precedence selected the shared SDK; the valid evidence above came only after a build-tree-local ignored top-level include bound the dedicated prefix, as confirmed by cache and absolute link inputs. Independent review approved the final verification with no HIGH/MED/LOW findings.

### Task 5: Review and prepare integration

- [x] Keep any CFlow code/test commit separate from the TurboSCXML corpus commit. If Task 1 is already GREEN, do not create an empty or cosmetic CFlow functionality commit.
- [x] Run a requirements review followed by a code-quality review over each repository's exact diff.
- [ ] Use the finishing-development-branch workflow to present integration choices; do not push, open a PR, merge, or update issue #122 without a subsequent explicit user request.

**Task 5 review evidence (2026-08-31):** Final cross-repository review approved the TurboUtils range `05771504..9cdbe0d4` and the TurboSCXML implementation range `4776015..2d19ac8` with no blocking code finding. It identified one LOW documentation mismatch: the corpus README retained the pre-promotion `114/54` counts. TurboSCXML commit `fa3ea29` corrected only those values to the manifest-derived `116/52`; the exact inventory filter passed `1/1` with 7 assertions, independent accounting remained `202/116/52/34`, and a narrow re-review approved exact final TurboSCXML HEAD `fa3ea29` with the finding closed and no new issue. The known MED shared-SDK installation side effect is environmental rather than a branch diff; final SCXML evidence and integration remain bound to the dedicated prefix.
