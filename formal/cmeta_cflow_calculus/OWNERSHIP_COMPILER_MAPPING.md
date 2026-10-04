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
Salts cmeta-lower             SaltsUtils IDL/DataBind
narrow source proof           generated Service compiler
          |                         |
          v                         v
portable C                    FunctionDesc/FunctionAbi
                              BindingPlan / Plugin / CFlow
```

Storage allocation remains outside this model.

## Salts `tools/cmeta-lower`

`cmeta-lower` is deliberately not a general C ownership compiler.

Its private state maps as follows:

| cmeta-lower state | Lean state |
| --- | --- |
| no ownership fact | outside automatic ownership proof |
| `LIVE_OWNED` | `Ownership.owned` |
| `MOVED` | `Ownership.moved` |

Rules:

- `owned(Type)` enters `LIVE_OWNED` only when one canonical typed
  `Type_cmeta_data()` lifecycle binding exists;
- `move(name)` maps `owned -> moved`;
- use-after-move and double move fail closed;
- normal straight-line scope exit discharges the live cleanup obligation
  through canonical DataDesc lifecycle;
- ownership-sensitive branch/loop/early-return/goto paths that require a join
  remain rejected by this tool.

This matches `joinOwnership`: an `owned/moved` branch mismatch is not joined
by silently recreating ownership.

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
formal model. Runtime registry generation/lease/callback checks remain the
dynamic backstop.

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
- expand `cmeta-lower` into a general C parser merely to satisfy ownership
  lowering.

Current delivery owners:

- Salts #755/#752: semantic/formal rules and narrow proof lowerer;
- SaltsUtils #452: generated Service/BindingPlan/Plugin/CFlow lifetime plans.
