# CMeta Lean 4 Module-System Migration Design

**Status:** Design approved; awaiting implementation-plan review  
**Date:** 2026-08-21  
**Scope:** `formal/` Lean proof/API organization only

## 1. Purpose

CMeta's formal development now has a stable end-to-end proof facade in `CMeta.PublicProof`, but ordinary Lean source-file imports still expose the transitive declarations behind that facade. The current boundary is therefore a **recommended API boundary**, not a language-enforced visibility boundary.

This design migrates the formal stack to Lean 4.30's module system so that:

1. public semantic carriers remain usable without introducing a second model;
2. the stable proof surface remains small;
3. proof plumbing, optimizer helper lemmas, backend/registry proof engineering, and conformance details become genuinely private to the package;
4. the default formal build continues to kernel-check all internal proofs;
5. the existing C witnesses, generated snapshots, and proof semantics remain unchanged.

The migration is visibility/refactoring work. It must not change the modeled C behavior.

## 2. Current state

The current public facade is:

```lean
import CMeta.EndToEnd

namespace CMeta.PublicProof

abbrev StructuredGraph := TypedGraph
abbrev Zip := SurfaceZip
abbrev FusedMap := CMeta.FusedMap
abbrev DirectProgram := ExecProgram
abbrev RuntimeOutput := PackedVec

-- six stable wrapper theorems

end CMeta.PublicProof
```

`PublicProofConformance.lean` imports only `CMeta.PublicProof`, and CI enforces that the facade directly imports only `CMeta.EndToEnd` and does not directly reference implementation-layer names.

However, ordinary Lean imports are transitive. The semantic spine is currently approximately:

```text
PublicProof
  -> EndToEnd
  -> Cardinality
  -> Execution
  -> Plan
  -> Lowering
  -> Optimize
  -> Graph
  -> Flow
  -> Dispatch
  -> Lambda
  -> Callable
  -> Traits
  -> Calculus
```

As a result, importing `CMeta.PublicProof` can still make declarations from these files available to a client.

## 3. Lean 4.30 constraints

This design relies on the Lean 4 module system documented in the Lean language reference:

- a source file opts in with `module`;
- declarations in a module are private by default unless marked `public` or placed in a `public section`;
- a normal `import M` adds `M`'s public scope to the current module's private scope;
- `public import M` re-exports `M`'s public scope;
- `import all M` additionally exposes `M`'s private scope to the importing module;
- `import all` is package-internal by default unless Lake `allowImportAll` is enabled;
- every import of a module must itself refer to a module.

References:

- <https://lean-lang.org/doc/reference/latest/Source-Files-and-Modules/>
- <https://lean-lang.org/doc/reference/latest/Build-Tools-and-Distribution/Lake/>

The dependency-management commands `#import_path`, `assert_not_exists`, `assert_not_imported`, and `#check_assertions` are available in the pinned Lean line and may be used for API-isolation conformance.

The project MUST NOT enable Lake `allowImportAll`. Internal modules may use `import all` because they are in the same package; downstream packages must not receive that capability.

## 4. Design principles

### 4.1 No second semantic model

The migration MUST NOT create opaque wrapper copies of:

- `CType`;
- `Callable`;
- `TypedGraph`;
- `SurfaceZip`;
- `FusedMap`;
- `ExecProgram`;
- `PackedVec`;
- or the existing plan/runtime objects.

The current `PublicProof` aliases remain aliases to the existing carriers. Visibility is enforced by modules, not by duplicating representations.

### 4.2 Public semantic vocabulary, private proof machinery

The public API is deliberately larger than six theorem names. Any semantic carrier or operation required to state/use the public theorems is public. The proof machinery used to establish those theorems is private.

The governing rule is:

> A declaration belongs to the public semantic surface if it is required in the type/signature of a supported public semantic object or public theorem. A declaration used only to prove those signatures is private.

Examples:

```text
PUBLIC                                PRIVATE
------                                -------
CType                                 induction helper lemmas
Callable                              graph erasure proof plumbing
TypedGraph                            optimizer helper proofs
TypedGraph.stages                     lowering preservation lemmas used only internally
checkGraph                            plan checker induction lemmas
SurfaceZip                            runtime induction lemmas
SurfaceZip.lower                      EndToEnd implementation theorems
checkInvokeRelation                   registry/replay proof engineering
FusedMap
MapChain.check / signatures
ExecProgram
ValueVec / PackedVec
PlanWellTyped
runRuntimePlan
```

This rule is deterministic: if removing a name from the public scope makes a supported public declaration's signature unusable, that name remains public. Proof-body dependencies do not qualify by themselves.

### 4.3 Public proofs are wrappers, not re-proofs

`CMeta.PublicProof` remains the only stable CFlow end-to-end proof API. Its theorem bodies delegate to internal theorems. No duplicate proof graph is introduced.

### 4.4 Full kernel checking remains the default

Reducing the public surface must not stop internal proof modules from building. The final `CMeta` root module will public-import the two supported entry points and privately import an internal build aggregator.

## 5. Target architecture

### 5.1 Two supported public entry points

The formal package will expose two conceptual entry modules:

```text
CMeta.LanguageSpec
  preprocessor / replay / backend / registry inference-rule facade

CMeta.PublicProof
  CFlow semantic carriers + end-to-end correctness theorems
```

They remain separate because they model different public concerns.

### 5.2 Semantic aggregator

Add:

```text
formal/CMeta/Semantics.lean
```

`CMeta.Semantics` introduces no declarations of its own unless required for documentation. It re-exports the curated public scopes of the semantic modules needed by `PublicProof`.

Conceptually:

```lean
module

public import CMeta.Calculus
public import CMeta.Callable
public import CMeta.Flow
public import CMeta.Graph
public import CMeta.Optimize
public import CMeta.Lowering
public import CMeta.Plan
public import CMeta.Execution
```

The exact import list may omit a module only when all public names required from it are already deliberately re-exported by another listed semantic module. The implementation plan must prefer explicit imports over relying accidentally on transitive public imports.

`Cardinality` is not automatically part of the public semantic surface merely because `EndToEnd` currently imports it. It becomes public only if a supported public theorem or semantic API needs its declarations.

### 5.3 Internal EndToEnd layer

`CMeta.EndToEnd` becomes a module whose end-to-end theorems are private by default.

Conceptually:

```lean
module

import all CMeta.Cardinality

namespace CMeta

-- private-by-default implementation theorems
-- EndToEnd.structured_graph_type_safe
-- EndToEnd.zip_lowering_type_safe
-- EndToEnd.fused_map_type_safe
-- EndToEnd.direct_plan_exact
-- EndToEnd.runtime_result_matches_compiled_output
-- EndToEnd.static_checker_matches_runtime

end CMeta
```

`EndToEnd` may use `import all` where it needs proof bodies or non-exported definitions from the same package. These imports are internal implementation edges and are not re-exported.

### 5.4 PublicProof module

The final facade becomes a module with an explicit public semantic import and an internal all-import of EndToEnd:

```lean
module

public import CMeta.Semantics
import all CMeta.EndToEnd

public section

namespace CMeta.PublicProof

abbrev StructuredGraph := TypedGraph
abbrev Zip := SurfaceZip
abbrev FusedMap := CMeta.FusedMap
abbrev DirectProgram := ExecProgram
abbrev RuntimeOutput := PackedVec

-- six stable wrapper theorems

end CMeta.PublicProof
```

The public theorem signatures remain semantically equivalent to the existing facade unless module visibility forces a purely syntactic qualification change. The migration must not weaken their statements.

The internal `EndToEnd.*` theorem names must not be visible to a client that imports only `CMeta.PublicProof`.

### 5.5 InternalChecks aggregator and root module

Add:

```text
formal/CMeta/InternalChecks.lean
```

Its job is build reachability, not API exposure. It imports the conformance, generated-snapshot, registry/replay, optimizer, runtime, and other internal proof modules that must continue to be kernel-checked by the default target.

The final root becomes conceptually:

```lean
module

public import CMeta.LanguageSpec
public import CMeta.PublicProof
import CMeta.InternalChecks
```

The private import of `InternalChecks` ensures `lake build` still checks all internal proofs while downstream `import CMeta` receives only the public scopes of `LanguageSpec` and `PublicProof`.

`CMeta.lean` MUST remain a full formal verification build root; public API cleanup must not reduce proof coverage.

## 6. Migration strategy

The migration uses a **frontier-first, bottom-up** strategy. A module can import only modules, so higher layers cannot opt in safely until their dependency frontier has been converted.

Each phase has two steps:

1. **Compatibility port:** add `module`, adjust imports, and preserve the existing behavior/public visibility sufficiently to restore a clean build.
2. **Surface contraction:** remove blanket visibility and mark only required semantic declarations `public`; leave proof plumbing private; add `import all` only where same-package proof composition requires it.

The compatibility step is temporary within the phase. A phase is not complete until its surface contraction and isolation checks are green.

### Phase M1 — semantic foundation

Convert, in dependency order:

```text
Calculus
Traits
Callable
Lambda
Dispatch
```

`Calculus` imports `Std`, which is already a module dependency.

Goal: establish the foundational public types/functions required by higher CFlow semantics while allowing foundational proof lemmas to become private where possible.

### Phase M2 — flow and structured graph

Convert:

```text
Flow
Graph
```

Public surface must include the semantic objects/functions required by supported graph statements. Internal graph-checking proof lemmas become private unless they are required in a public signature.

### Phase M3 — optimizer and lowering

Convert:

```text
Optimize
Lowering
```

Public semantic data and checking functions remain available; helper preservation proofs used only as EndToEnd proof inputs become private.

### Phase M4 — plan, execution, cardinality

Convert:

```text
Plan
Execution
Cardinality
```

Public declarations are those needed to state/use `PublicProof` and supported runtime semantics. Execution/plan induction lemmas and cardinality proof helpers remain private unless separately designated as public semantic API.

### Phase M5 — EndToEnd, Semantics, PublicProof

Convert/create:

```text
EndToEnd
Semantics
PublicProof
PublicProofConformance
PublicProofIsolationConformance
```

At the end of M5, importing `CMeta.PublicProof` must no longer expose the selected internal proof names listed in the isolation conformance file.

### Phase M6 — CFlow internal conformance closure

Convert the remaining CFlow-side conformance/generated modules needed to build `InternalChecks` as modules. Create/extend `InternalChecks` so the full current kernel-check coverage remains reachable.

This phase does not widen `PublicProof`.

### Phase M7 — Producer / replay / registry tree

Migrate the second proof tree independently:

```text
Producer
NestedReplay*
PreprocessorBackend*
Registry*
LanguageSpec
LanguageSpecConformance
related generated/conformance modules
```

`CMeta.LanguageSpec` remains the stable public entry point. Registry/replay implementation lemmas become private where they are not part of the existing rule facade.

### Phase M8 — root conversion

Convert `CMeta.lean` to the final root module:

```lean
module
public import CMeta.LanguageSpec
public import CMeta.PublicProof
import CMeta.InternalChecks
```

The default Lake target remains `CMeta` and therefore continues to build the complete internal verification closure.

## 7. Visibility rules during migration

### 7.1 Imports

Use `public import` only when downstream users intentionally need the imported module's public API through the current module.

Use plain `import` when the dependency is needed only to implement the current module's private/public declarations but must not be re-exported.

Use `import all` only for same-package proof implementation that genuinely needs the imported module's private scope.

Do not enable `allowImportAll` in `lakefile.toml`.

### 7.2 Declarations

Do not solve porting errors by globally enabling:

```text
backward.privateInPublic
backward.proofsInPublic
```

The final design requires explicit visibility. Temporary compatibility settings are not part of the accepted end state.

`@[expose]` may be used only when an intentionally public definition must remain unfoldable by supported downstream reasoning. It is not the default for public definitions.

### 7.3 Namespaces and filenames

Physical relocation to `InternalProof/` is not required for visibility and is outside this migration's primary scope. Existing filenames should remain stable unless a new aggregator/conformance module is needed.

The module system, not directory naming, enforces the boundary.

## 8. Isolation conformance

Add a dedicated client-style module that imports only `CMeta.PublicProof`.

It must positively check the supported facade and negatively check representative internal declarations.

Representative shape:

```lean
module

import CMeta.PublicProof

#check CMeta.PublicProof.structured_graph_type_safe
#check CMeta.PublicProof.zip_lowering_type_safe
#check CMeta.PublicProof.fused_map_type_safe
#check CMeta.PublicProof.direct_plan_exact
#check CMeta.PublicProof.runtime_output_type_safe
#check CMeta.PublicProof.static_checker_matches_runtime

assert_not_exists CMeta.EndToEnd.direct_plan_exact
assert_not_exists CMeta.TypedGraph.check_stages
assert_not_exists CMeta.SurfaceZip.lowering_preserves_type
assert_not_exists CMeta.FusedMap.type_preserved
assert_not_exists CMeta.ExecProgram.runtime_execution_exact
```

The exact negative list must cover at least one internal theorem from graph, lowering, optimizer, execution, and EndToEnd layers.

Do **not** assert that `CMeta.EndToEnd` is not transitively imported. It is expected to be an internal implementation dependency. The contract is that its private declarations are not in the client's scope.

The existing shell facade-boundary guard may remain during migration. After module isolation is proven, it becomes secondary defense rather than the primary visibility mechanism.

## 9. TDD and verification requirements

Every migration phase follows RED -> GREEN.

### RED

Before tightening visibility, add or update a conformance expectation that demonstrates the desired boundary. Valid RED examples include:

- a client can currently see a theorem that should become private;
- a converted module fails because a public signature depends on a private carrier;
- an internal proof module cannot access a needed private definition without `import all`.

The RED failure must be inspected and shown to arise from the intended visibility gap, not from an unrelated import typo.

### GREEN

Apply the minimum visibility/import changes required for the phase. Then run the full formal gate.

Required exact-head verification remains:

```text
GCC formal configure/build
Clang formal configure/build
C witness execution
C/Lean generated snapshot zero-diff
applicability probes
proof-placeholder guard
callable/lambda API guard
public-proof boundary/isolation guards
lake build --wfail
```

No phase is complete if only the new visibility conformance passes while the existing formal stack fails.

## 10. Compatibility and API policy

### Stable during this migration

The following are treated as stable unless a visibility conflict makes a qualification-only change unavoidable:

```text
CMeta.PublicProof.StructuredGraph
CMeta.PublicProof.Zip
CMeta.PublicProof.FusedMap
CMeta.PublicProof.DirectProgram
CMeta.PublicProof.RuntimeOutput

CMeta.PublicProof.structured_graph_type_safe
CMeta.PublicProof.zip_lowering_type_safe
CMeta.PublicProof.fused_map_type_safe
CMeta.PublicProof.direct_plan_exact
CMeta.PublicProof.runtime_output_type_safe
CMeta.PublicProof.static_checker_matches_runtime
```

`CMeta.Producer.LanguageSpec`'s existing carrier/judgment/rule facade is likewise the target public surface for the Producer/replay tree.

### Intentionally unstable/internal

The migration may make existing lower-level theorem names inaccessible to downstream clients. This is the desired behavior, not a compatibility regression, when those theorems are proof plumbing rather than documented public semantic API.

Internal source modules may continue to access them through same-package `import all`.

## 11. Non-goals

This migration does not:

- change CMeta/CFlow runtime behavior;
- alter C witness output or generated Lean snapshots;
- introduce new operator semantics;
- introduce `Interaction / Feedback / Recovery / Aggregation / ClosedLoop` objects that do not currently exist in the repository;
- duplicate existing semantic carriers behind opaque wrappers;
- physically reorganize every proof file into new directories;
- remove internal proofs;
- reduce the set of proofs kernel-checked by the default CI target;
- merge `LanguageSpec` and `PublicProof` into one conceptual API.

## 12. Failure handling and rollback

A migration phase must stop rather than broaden public visibility indiscriminately when a public/private error reveals an unexpected dependency.

For each such dependency, classify it explicitly:

1. **semantic signature dependency** -> make the required declaration public;
2. **proof implementation dependency** -> keep it private and use internal `import all` or restructure the proof module;
3. **accidental dependency** -> refactor the dependency away.

Do not use blanket `public section` or `@[expose] public section` as the final resolution for a phase.

Because phases are bottom-up and independently verified, rollback is phase-local: revert only the incomplete phase while retaining earlier converted modules.

## 13. Acceptance criteria

The migration is complete only when all of the following hold:

1. `import CMeta.PublicProof` exposes the five carrier aliases and six stable theorem wrappers.
2. Representative graph/lowering/optimizer/execution/EndToEnd proof-plumbing declarations fail `assert_not_exists` in a client that imports only `CMeta.PublicProof`.
3. No second semantic carrier model has been introduced.
4. `CMeta.Semantics` provides the explicitly curated public semantic vocabulary required by `PublicProof`.
5. `CMeta.LanguageSpec` remains a separate stable public inference-rule facade.
6. `CMeta.lean` public-imports only the two supported public entry points and privately imports the internal build aggregator.
7. The default `CMeta` Lake target still reaches and kernel-checks the complete current internal verification closure.
8. Lake `allowImportAll` remains disabled/default.
9. No final phase relies on `backward.privateInPublic` or `backward.proofsInPublic`.
10. GCC and Clang formal CI pass at the exact PR head with snapshot zero-diff and `lake build --wfail`.

## 14. Resulting proof-surface model

The final architecture is:

```text
                         downstream users
                               |
                 +-------------+-------------+
                 |                           |
                 v                           v
        CMeta.LanguageSpec            CMeta.PublicProof
        replay/backend rules          CFlow E2E proofs
                                             |
                                             v
                                      CMeta.Semantics
                                             |
----------------------------- public/private boundary -----------------------------
                                             |
             +-------------------------------+-------------------------------+
             |                               |                               |
             v                               v                               v
       private proof plumbing          private EndToEnd             private conformance
       graph/optimizer/plan            implementation proofs        generated/internal checks
             |                               |                               |
             +-------------------------------+-------------------------------+
                                             |
                                             v
                                      CMeta.InternalChecks
                                             |
                                             v
                                       full kernel build
```

The intended outcome is **proof-surface reduction by language-enforced visibility**, not by deleting proofs or renaming directories.
