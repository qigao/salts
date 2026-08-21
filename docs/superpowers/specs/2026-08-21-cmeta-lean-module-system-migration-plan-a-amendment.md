# CMeta Lean Module-System Migration — Plan A Execution Amendment

**Status:** Approved-design execution clarification  
**Date:** 2026-08-21  
**Applies to:** `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-design.md`, Plan A / M1–M6 only

## Why this amendment exists

The approved design requires frontier-first module conversion while the legacy `CMeta.lean` root continues to build the whole proof stack. A concrete dependency audit exposed one migration constraint that must be made explicit: a legacy source file can import a module's public scope, but it cannot use `import all` to recover that module's private proof scope. Therefore a proof theorem cannot be made private before its last legacy consumer has itself been converted to a module.

This amendment does not change the target architecture. It specifies how Plan A reaches that target while keeping every phase buildable.

## Temporary visibility bridges

Plan A may keep a proof-only theorem, re-export, or definition body temporarily visible only when an identified, still-legacy consumer requires it. Every bridge MUST:

1. be explicit, never a blanket `public section` over unrelated proof machinery;
2. carry a source comment of the form `TEMP-MODULE-BRIDGE(M<n>): <consumer>`;
3. name the phase in which it is removed;
4. be included in the implementation plan's bridge-removal checklist;
5. be absent at the final Plan A exact-head checkpoint unless the declaration/body is part of the approved public semantic API.

No bridge may be justified merely because making a declaration private causes an unspecified build failure. The failing consumer must be identified first.

`backward.privateInPublic`, `backward.proofsInPublic`, and Lake `allowImportAll` remain forbidden.

### Temporary exposed-body bridges

An unconverted legacy proof can also fail because a public definition becomes unexposed when its defining file becomes a module. In that case Plan A may use `@[expose] public def` as a temporary compatibility bridge only for an audited definition that the legacy consumer explicitly unfolds. The marker/comment and removal rules above apply exactly as for theorem bridges.

The audited exposed-body bridges are:

- through M6 for legacy `OptimizerConformance.identity_lists_equal`: `CMeta.HArgs.one`, `CMeta.Callable.ofUnary`, `CMeta.Callable.invoke1`;
- through M4 for legacy `PlanNode.check_erase`: `CMeta.MapChain.check`;
- through M5 for legacy `EndToEnd.runtime_result_matches_compiled_output` / `static_checker_matches_runtime`: `CMeta.PlanProgram.compile`, `CMeta.PlanWellTyped`.

No other `@[expose]` is authorized by Plan A without first updating this amendment with the exact legacy consumer. All temporary exposures above are removed at their named phase, so the final Plan A expectation remains no migration-only `@[expose]` markers.

## Audited bridge lifecycle

The CFlow spine has the following known legacy-consumer bridges.

### M1 → M2

`Flow.lean` remains legacy until M2 and consumes Dispatch proof results. Keep only these Dispatch theorems public through the M1 checkpoint:

- `CMeta.dispatch_sound`
- `CMeta.dispatch_policy_sound`

M2 converts `Flow` with `public import all CMeta.Dispatch`; remove both bridges in the same M2 phase before the M2 checkpoint.

### M2 → M3/M5/M6

After `Flow` and `Graph` become modules, later legacy files still consume selected proof results. Keep these explicit bridges only until the listed final consumer converts:

- `CMeta.TypedOp.step_exact` — required by legacy `Optimize`; remove in M3 after `Optimize` uses `import all CMeta.Flow`.
- `CMeta.Pipeline.check_steps` — required by legacy `Lowering`; remove in M3 after `Lowering` uses `import all CMeta.Flow`.
- `CMeta.TypedGraph.check_stages` — required by legacy `EndToEnd`; remove in M5 after `EndToEnd` imports Graph private scope.
- `CMeta.TypedRelation.check_erase` — required by legacy `StructuredConformance`; remove in M6 after that conformance module uses `import all CMeta.Graph`.

### M3 → M4/M5/M6

- `CMeta.MapChain.check_signatures` — required by legacy `Plan`; remove in M4 after `Plan` imports Optimize private scope.
- `CMeta.FusedMap.type_preserved` — required by legacy `EndToEnd`; remove in M5.
- `CMeta.SurfaceZip.lowering_preserves_type` — required by legacy `EndToEnd` and `StructuredConformance`; remove in M6 after both consumers are modules.
- `CMeta.duplicate_idempotent_elimination_sound` — required by legacy `OptimizerConformance`; remove in M6.

### M4 → M5

`Cardinality` is intended to export no semantic API, but legacy `EndToEnd` currently reaches execution/plan declarations through `import CMeta.Cardinality`. Until `EndToEnd` becomes a module in M5, `Cardinality` may temporarily use:

```lean
public import all CMeta.Execution
```

with a `TEMP-MODULE-BRIDGE(M5)` comment. In M5 this becomes private `import all CMeta.Execution` only.

The following Execution proof theorems remain public only through the M4 checkpoint because legacy `EndToEnd` uses them:

- `CMeta.ExecProgram.runtime_execution_exact`
- `CMeta.ExecProgram.result_type_safe`
- `CMeta.ExecProgram.compiled_plan_well_typed`

They become private in M5 when `EndToEnd` imports Execution private scope directly.

## PublicProof isolation timing

M5 converts `EndToEnd`, creates `Semantics`, and converts `PublicProof`. It must immediately hide internal names whose last legacy consumer has disappeared, including:

- `CMeta.EndToEnd.*`
- `CMeta.TypedGraph.check_stages`
- `CMeta.FusedMap.type_preserved`
- `CMeta.ExecProgram.runtime_execution_exact`
- `CMeta.ExecProgram.result_type_safe`
- `CMeta.ExecProgram.compiled_plan_well_typed`

However, M5 may not yet hide the three audited proof bridges still required by M6 legacy conformance:

- `CMeta.TypedRelation.check_erase`
- `CMeta.SurfaceZip.lowering_preserves_type`
- `CMeta.duplicate_idempotent_elimination_sound`

Therefore M5 establishes the module-form facade and a **partial isolation gate**. M6 converts the remaining CFlow conformance/generated closure, removes these final bridges and all remaining temporary exposed-body bridges, and then strengthens `PublicProofIsolationConformance` to the full approved negative set covering graph, lowering, optimizer, execution, and EndToEnd layers.

This supersedes the original wording that required the complete negative isolation set at the end of M5. The Plan A acceptance criterion is unchanged: complete hard CFlow/PublicProof isolation is required before the final M6 exact-head checkpoint.

## Import syntax rule

When a module both intentionally re-exports another module's public semantic scope and needs its private proof scope internally, use Lean 4.30's combined syntax:

```lean
public import all CMeta.SomeDependency
```

Do not duplicate the same dependency as separate `public import` and `import all` lines unless Lean itself requires distinct phase annotations for that particular import.

When only private implementation access is required, use:

```lean
import all CMeta.SomeDependency
```

When only public semantic re-export is required, use:

```lean
public import CMeta.SomeDependency
```

## Generated snapshot timing

M6 remains responsible for converting the CFlow-side generated Lean snapshots and their conformance consumers. Each generated source changes only by the required module framing (normally `module\n\n` before its existing `import Std`), with the C witness producer updated in the same task. Payload identity and regenerated byte-for-byte zero diff remain mandatory.

## Final Plan A acceptance

At M6 exact head:

- all M1–M6 CFlow semantic, facade, generated, and conformance files in the Plan A closure are modules;
- no `TEMP-MODULE-BRIDGE(M1)` through `TEMP-MODULE-BRIDGE(M6)` marker remains;
- no migration-only `@[expose]` remains;
- `CMeta.PublicProof` exposes only the approved semantic vocabulary and six stable wrapper theorems;
- representative internal proof names from Graph, Lowering, Optimize, Execution, and EndToEnd fail `assert_not_exists` for a client importing only `CMeta.PublicProof`;
- the legacy `CMeta.lean` root still builds the complete formal tree; it is not converted in Plan A;
- all real-C generated snapshots are again byte-for-byte equal to committed snapshots;
- GCC and Clang formal lanes and `lake build --wfail` succeed at the exact PR head.
