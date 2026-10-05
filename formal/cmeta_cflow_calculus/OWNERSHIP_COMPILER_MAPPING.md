# Ownership calculus -> compiler mapping

This note is the implementation map for Salts #755 / #752.

The Lean ownership calculus is the semantic source of truth. Concrete
generators may use smaller private state machines, but they must map into these
states and transitions rather than define a competing ownership contract.

## Canonical semantic authority

```text
CMeta FunctionDesc
  parameter/result ownership
          |
          v
Lean Ownership calculus
  borrowed/shared/owned/moved/released
  BorrowedFrom(owner)
  NeedsCleanup
          |
          +-------------------------+
          |                         |
          v                         v
ordinary CMeta C API          SaltsUtils IDL/DataBind
macro/inline lifecycle        generated Service compiler
          |                         |
          v                         v
portable C                    FunctionDesc/FunctionAbi
                              BindingPlan / Plugin / CFlow
```

Storage allocation remains outside this model.

## Ordinary CMeta lifecycle mapping

Ordinary CMeta no longer carries compiler-private LIVE/MOVED state.

Canonical mapping:

```text
cmeta_type(...)            -> finite ordinary-C type/metadata generation
cmeta_move(Type,&dst,&src) -> canonical DataDesc move
successful move            -> source semantic-zero
cmeta_auto/cmeta_guard     -> lexical resource helper when backend is available
```

Memory/lifecycle correctness comes from canonical DataDesc contracts. A moved
source remains a valid semantic-zero C object and may be destroyed or
reinitialized.

Optional analyzers may diagnose double-move/use-after-move, but ordinary CMeta
compilation and lifecycle safety do not depend on an analyzer.

Full branch/loop ownership proofs remain appropriate in real compilers such as
TurboScript and in structured generators that already own AST/IR/CFG state.

## SaltsUtils generated Service compiler

Broad generated lifetime lowering belongs to the IDL/DataBind compiler, not to
an expanded C parser in Salts.

Canonical mapping:

```text
FunctionDesc RESULT_VALUE   -> caller-owned value state
FunctionDesc RESULT_OWNED   -> owned result + one cleanup obligation
FunctionDesc RESULT_SHARED  -> shared result + one release obligation
FunctionDesc RESULT_BORROWED
                            -> borrowed result; escaping use requires
                               BorrowedFrom(authoritative owner)
FunctionDesc RESULT_UNKNOWN -> no automatic ownership proof

PARAM_BORROWED              -> caller state preserved
PARAM_OWNED                 -> consume only after the admitted call boundary;
                               caller post-state is moved
```

BindingPlan native construction/rollback consumes canonical CMeta DataDesc
lifecycle. Parameter ownership does not replace storage lifecycle:

- an OUT|BORROWED parameter means the function does not acquire ownership of
  caller-provided storage;
- the caller/BindingPlan may still own construction and cleanup of that native
  storage through DataDesc.

## Plugin lease mapping

A generated Plugin client owns one live Salts Plugin lease.

```text
PluginLease                  -> owned authority token
FunctionDesc/DataDesc/export -> borrowed from PluginLease
client close                 -> final lease release
typed call after close       -> invalid state
DSO unload                   -> only after runtime quiescence
```

A live `BorrowedFrom(view, lease)` makes final lease release unsafe in the
formal model. The valid teardown sequence is explicit:

```text
endBorrow(view)
    -> view released + owner edge cleared
    -> discharge/release PluginLease
```

This mirrors the real DSO ordering qualified by Salts #778: dependent
ObjectRef/Interface views are dropped before the final lease release, then the
registry may reach quiescence and unload. Runtime registry
generation/lease/callback checks remain the dynamic backstop.

Reflection descriptors do not embed leases.

## Branch joins

The current formal join is intentionally conservative:

```text
owned + owned -> owned
moved + moved -> moved
owned + moved -> reject
moved + owned -> reject
```

A real generated compiler may introduce richer CFG state, but it may not
resurrect a moved obligation at a join. Source restructuring or an explicit
ownership operation is required.

## Boundaries

Do not:

- infer ownership from pointer spelling, `const`, ABI carrier, or symbol name;
- put CFG/block state in Reflection descriptors;
- create a second Plugin registry or descriptor-owned lease;
- treat RAII as allocator policy;
- reintroduce a mandatory CMeta source translator merely to enforce ownership.

Current delivery owners:

- Salts #905/#920: ordinary-C lifecycle/Reflection surface;
- SaltsUtils #452: generated Service/BindingPlan/Plugin/CFlow lifetime plans.
