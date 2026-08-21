# CMeta Lean Module-System Plan A Execution Errata

**Status:** Plan A implemented and exact-head verified; Plan B pending  
**Date:** 2026-08-21  
**Applies to:** `2026-08-21-cmeta-lean-module-system-migration-design.md`, the Plan A amendment, and `2026-08-21-cmeta-lean-module-system-plan-a.md`

This errata records Lean 4.30 behavior discovered by RED→GREEN execution. It does not change the target architecture or public API.

## 1. Warning-free isolation assertions under `lake build --wfail`

Use direct `assert_not_exists` statements as the authoritative negative visibility checks.

Do not add `#check_assertions` to conformance modules that are part of the default `lake build --wfail` target. In Lean 4.30, a successful `#check_assertions` summary is emitted as a warning; `--wfail` therefore turns an otherwise successful isolation check into a build failure.

The TDD evidence for this rule was:

- RED: direct `assert_not_exists` correctly failed while legacy Calculus proof names were visible;
- first GREEN: all direct assertions succeeded, but `#check_assertions` alone produced the warning that failed `--wfail`;
- minimal fix: removing only `#check_assertions` made both GCC and Clang kernel lanes green.

## 2. Re-export plus private access requires two import commands

The previously documented combined spelling:

```lean
public import all CMeta.SomeDependency
```

is invalid Lean 4.30 syntax.

When a module must both re-export a dependency's public scope and access that dependency's private scope internally, write two commands:

```lean
public import CMeta.SomeDependency
import all CMeta.SomeDependency
```

Use only:

```lean
import all CMeta.SomeDependency
```

when no downstream re-export is intended, and only:

```lean
public import CMeta.SomeDependency
```

when private implementation access is unnecessary.

This errata supersedes every `public import all` example in the earlier Plan A documents.

## 3. M1→M2 legacy semantic import bridge

Moduleizing `Dispatch` exposed an existing accidental transitive dependency: still-legacy `Flow`/`Graph` obtain `Lambda`/`Callable` semantic names through the historical chain `Dispatch -> Lambda -> Callable`.

Until M2 converts the downstream frontier, `Dispatch.lean` therefore uses:

```lean
module
-- TEMP-MODULE-BRIDGE(M2): legacy Flow/Graph rely on Dispatch -> Lambda -> Callable semantics
public import CMeta.Lambda
import all CMeta.Traits
```

This is a semantic import bridge only. It does not justify making additional Dispatch proof theorems public.

Removal rule:

- do not remove this bridge in Task 6 merely because `Flow` becomes a module if `Graph` is still legacy and still depends on the transitive semantic path;
- remove it no later than Task 7, after `Graph` itself explicitly imports the semantic modules it requires;
- at the M2 checkpoint, no downstream dependency may rely on this accidental transitive path.

## 4. Formal-history rule

RED commits used solely for verification may live on the temporary verification branch/PR. They must not be fast-forwarded into the official `leanv4` branch.

At each official phase checkpoint, create a GREEN commit from the verified final tree with the previous official checkpoint as its parent. This keeps the official PR history free of known-red intermediate commits while retaining real RED→GREEN CI evidence in the temporary verification PR.

## 5. Permanent `CType.denote` exposure

Actual Lean 4.30 module execution showed that executable conformance models must reduce `CType.denote` across module boundaries in order to elaborate host-language value types such as `Int`, `Bool`, and `Float`.

Therefore the supported semantic declaration remains intentionally:

```lean
@[expose] public def CType.denote : CType → Type
```

This is part of the public semantic vocabulary, not a `TEMP-MODULE-BRIDGE`. The M6 cleanup rule “remove migration-only `@[expose]` declarations” does **not** apply to `CType.denote`.

The final static audit for Plan A is therefore:

- no `TEMP-MODULE-BRIDGE` marker remains in production formal sources;
- no migration-only `@[expose]` remains;
- `CType.denote` is the explicit permanent `@[expose]` exception required by the supported executable semantics.
