# SCXML conformance-completion implementation plan

## 1. Multi-target Statechart IR

1. [x] Add failing Statechart tests for v2 target validation, two-region entry,
   multi-target initial entry, internal-domain rules, and legacy single-target
   compatibility.
2. [x] Add the versioned target-row API and normalized target-span storage.
3. [x] Update validation, transition-domain calculation, initial/default/history
   entry, target configuration construction, destruction, storage accounting,
   and normalized queries.
4. [x] Run the Statechart build/runtime/configuration test targets.

## 2. SCXML multi-target and event descriptors

1. [x] Replace the existing rejection tests with red tests for root initial IDREFS,
   transition target IDREFS, exact/prefix/`.*`/`*` descriptors, descriptor
   unions, case sensitivity, and token boundaries.
2. [x] Add target-row emission to the SCXML compiler and call
   `cflow_statechart_build_v2`.
3. [x] Separate concrete event collection from descriptor expansion; calculate
   bounded expanded transition/guard/action capacities after event-name
   normalization and deduplicate matches per source transition.
4. [x] Add transformed W3C 399 and 576 fixtures and run focused SCXML tests.

## 3. Event envelope and system variables

1. [x] Add red evaluator tests for every `_event` field, scalar `.data`, empty
   optional fields, `_ioprocessors.scxml.location`, missing-current-event
   failure, and read-only rejection.
2. [x] Add red session tests for external, raised, internal-send, platform-error,
   invoke-return, and eventless-retention metadata.
3. [x] Implement checked session-owned metadata rows and public v2 named-event
   input while preserving the v1 event-only entry point.
4. [x] Add a backward-compatible runtime-hook v2 event-observation boundary.
5. [x] Extend the CMeta operand resolver and session system-value binding, then run
   evaluator, session, runtime-hook, and mailbox regressions.

## 4. Dynamic send/cancel/invoke and payload

1. [x] Add compile-time red tests for legal mutual-exclusion combinations and
   exact diagnostics for invalid pairs/types.
2. [x] Add runtime tests proving one-time evaluation, staged-state visibility,
   delay conversion, internal-send scalar data, and adapter materialization.
3. [x] Add compiled dynamic descriptors, bounded internal scalar payload
   materialization, tagged internal events, and expression cleanup.
4. [x] Reuse the current prepare/commit/discard journal and delayed/invocation
   registries; do not add a second effect fact source.
5. [x] Run SCXML core, CMeta, adapter, invoke, and error-event regressions.

`idlocation`, `namelist`, `param`, text/object content, and payload-aware
external adapter ABIs remain explicitly outside this bounded increment.

## 5. CMeta data initialization and donedata

1. [x] Add tests for early document-order initialization, caller-state
   isolation, explicit late-binding rejection, and unsupported `src`.
2. [x] Add tests for scalar donedata and `_event.data` on
   `done.state.*` transitions.
3. [x] Add `datamodel`, `data`, `donedata`, and `content` admission plus
   compiled early-initializer and scalar completion descriptors.
4. [x] Bind completion metadata through the SCXML event hook and expand
   done-state descriptors while preserving native completion records.
5. [x] Run data-model, statechart completion, and executable-content focused
   tests.

Late binding remains unsupported until a separate re-entry transaction and
commit-only initialization-marker protocol is designed.

## 6. W3C manifest and expanded selected corpus

1. [x] Add a strict `manifest.tsv` parser test and bounded parser.
2. [x] Record every local fixture with upstream
   source, applicability, status, feature, and rationale.
3. [x] Preserve the existing selected transformed fixtures and their named
   assertion tests.
4. [x] Validate unique IDs/files, enums, official URLs, naming, and PASS-file
   existence from the manifest; update provenance without broadening claims.
5. [x] Run the W3C conformance target with the strict manifest.

## 7. Verification

1. [x] Reconfigure with `win-release-user -DCFLOW_ENABLE_SCXML=ON`.
2. [x] Build and run the smallest affected tests after every production change.
3. [x] Run all CFlow, CMeta, parser XML, and SCXML tests.
4. [x] Run the full `win-release-user` build and CTest suite.
5. [x] Run the install preset and installed-package verification target.
6. [x] Sync CodeGraph, inspect affected symbols/files, check `git diff --check`, and
   confirm the worktree contains no generated `.codegraph` or build artifacts.
