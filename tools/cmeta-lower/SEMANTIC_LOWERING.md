# CMeta semantic lowering branch contract

Tracking Epic: #905

Branch: `feat/cmeta-semantic-lowering`

This branch is intentionally long-lived and maturity-gated. It collects the
complete admitted finite source-lowering surface before any merge to `master`.

## Compiler boundary

```text
C + finite CMeta syntax
        |
        v
   cmeta-lower
        |
        v
 ordinary portable C11
```

The lowerer owns only compile-time lexical/source state needed to prove and
rewrite admitted forms. Canonical CMeta Reflection/DataDesc/FunctionDesc/
InterfaceDesc remain semantic authorities. No runtime lookup is added by this
branch.

## Admitted syntax roadmap

1. structured scope exits / `defer` (first direct-call slice implemented);
2. explicit error propagation with deterministic cleanup;
3. exact Interface receiver calls;
4. finite enum/variant `match`;
5. finite typed collection iteration;
6. generic source spelling resolving only to canonical concrete native types.

## Hard exclusions

The branch must not turn cmeta-lower into a full C compiler. In particular it
does not own:

- general CFG ownership joins;
- universal borrow checking;
- closure escape analysis;
- async/await state-machine compilation;
- C++ overload/conversion semantics;
- runtime RTTI or method-name invocation.

Those belong in a real AST/IR/CFG compiler or a domain runtime.

## Exit/cleanup ordering

All admitted lexical exit actions participate in one deterministic LIFO stack.
An owned value contributes its canonical DataDesc cleanup action. A `defer`
contributes an explicit source cleanup action. The lowerer decides *when* an
action runs; the provider/source call decides *how* it runs.

Unsupported transfers of control fail closed rather than silently skipping or
duplicating cleanup.

## Merge policy

The implementation PR remains Draft until every admitted phase is either
complete and qualified or explicitly removed from #905 with rationale.

Required before Ready-for-review:

- positive runtime coverage;
- generated C shape checks;
- negative/fail-closed diagnostics;
- existing ownership/move invariants;
- sanitizer qualification;
- Linux/Windows/macOS native qualification;
- host-tool/cross-build qualification;
- synchronized README/inspection docs;
- no production TODO/fallback path.

## Current experimental slice

`defer direct_c_call(...);` is implemented with one cleanup stack shared with
`owned(T)`. It is intentionally stricter than the eventual surface: tracked
owned captures and non-local control transfers remain rejected until a sound
finite rule is proven.
