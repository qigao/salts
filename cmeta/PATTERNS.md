# CMeta pattern layer

> Salts 3.0.0 ships the canonical CMeta pattern primitives, Component
> Configurator and ComponentPlugin integration. Post-3.0 ACE pattern conformance
> develops under [#1012](https://github.com/qigao/salts/issues/1012) on
> `feature/cmeta-ace-patterns`; changes there are not yet a released API.

## Purpose

CMeta's pattern layer is a composition discipline over existing canonical CMeta
semantics. It is not a second language, a parallel object model, or a runtime
framework.

The authoring rule is:

```text
macro declaration / finite replay
        +
typed inline facade
        +
canonical immutable descriptor
        =
pattern authoring
```

Runtime mechanism remains with its existing owner.

The first implemented runtime consumer is the ACE-style Component Configurator
specified in #1003 and integrated by PR #1010. CHttp, TurboFlow and TurboSCXML
remain downstream conformance profiles, not sources of consumer-private
component semantics.

## Canonical decomposition

Patterns should reduce to the smallest existing semantic primitives.

### Strategy

```text
Strategy = CMeta Interface
```

Use the existing typed `{ self, vtable }` protocol and
`cmeta_interface_desc`. Do not add `cmeta_strategy` merely as a spelling
alias.

### Factory

```text
Factory =
  canonical FunctionDesc
  + FunctionAbi
  + explicit result ownership
```

A creator returning an owned component/object uses the existing
`CMETA_RESULT_OWNED` semantic contract. Factory semantics must not be inferred
from a pointer return type or from a function name.

No generic runtime ABI reconstruction is added by the Factory pattern.

### Adapter

```text
Adapter =
  exact typed Interface implementation mapping
  + compile-time native signature checking
```

Use `implements(...)` / exact generated vtables where they already express the
mapping. Add a new helper only when it validates or generates a relation that
cannot be represented by the existing Interface implementation surface.

Do not add a second generic adapter runtime.

### Extension Interface

```text
Extension Interface =
  one native object/provider identity
  + zero or more provider-authorized Interface projections
```

Use `cmeta_object_ref`, `cmeta_object_interface_provider`, and
`cmeta_interface_projection`.

A borrowed projection owns nothing. It must not expose independent destroy
authority for an Interface containing `CMETA_INTERFACE_METHOD_OWNS_SELF`.
Provider/module lifetime stays external.

### Lifecycle

Lifecycle metadata describes exact operations but does not own runtime state.

The component layer may compose:

```text
create
activate
deactivate
destroy
```

from exact typed functions. Cleanup authority remains the native provider and
outer resource/module owners.

### Scope

Pattern composition reuses existing ownership relations:

```text
value lifetime   -> DataDesc
object lifetime  -> ObjectRef/lifecycle
module lifetime  -> Plugin lease
lexical cleanup  -> CMeta scope/guard facade
```

A descriptor, Interface view, or callable never implicitly retains a Plugin
module.

### Static discovery

Use immutable explicit CMeta manifests.

```text
descriptor = semantic truth
manifest   = explicit static discovery set
linker     = optional private aggregation backend
```

Do not introduce constructor registration or a process-global mutable CMeta
registry.

## Canonical cmeta/component.h

The canonical `<cmeta/component.h>` declaration is static metadata only:

```text
name
+ provides Interface rows
+ requires Interface rows
```

It does not load modules, resolve dependencies, select providers, own leases, or
advance lifecycle.

This metadata is the canonical component/provider capability declaration. Dynamic module publication and lease ownership remain under `Salts::Plugin`. There is no second static plugin declaration vocabulary in Salts 3.0.0.

## Patterns that do not belong in CMeta runtime

The following ACE patterns may consume CMeta metadata but their execution
mechanism remains outside CMeta:

| Pattern | Runtime owner |
| --- | --- |
| Reactor / Proactor | CNet / NativeIO |
| Acceptor / Connector | CNet |
| Active Object | Actor / CFlow |
| Message Queue | Concurrency / Actor / CFlow |
| Half-Sync/Half-Async | domain runtime + bounded handoff |
| Streams execution | CFlow / domain runtime |
| Plugin module lifecycle | Salts::Plugin |
| Component resolution/lifecycle graph | Salts::Component |

CMeta may describe typed policies, messages, interfaces, factories and
lifecycle contracts for these mechanisms.

## Admission test for a new pattern helper

Add a new CMeta pattern-level macro/inline helper only if at least one condition
holds:

1. it generates a new canonical descriptor relation;
2. it proves a useful compile-time relation between existing descriptors or
   native signatures;
3. it removes repeated glue while preserving one existing semantic authority;
4. it provides a typed projection that cannot otherwise be expressed safely.

Do not add a helper solely to introduce a pattern name.

## Component Configurator integration

The shipped composition is:

```text
Interface / Function / Object / Lifecycle / Manifest
                        |
                        v
              Component descriptor
             provides / requires
                        |
                        v
              Salts::Component
             resolver / lifecycle graph
                        |
                        v
            Salts::ComponentPlugin
                generation scopes
                        |
                        v
                 Salts::Plugin
```

The Configurator adds only the runtime semantics not already owned by
CMeta:

- provider selection/resolution;
- dependency graph construction;
- instance lifecycle ordering;
- generation publication/drain.

If implementing the Configurator requires a second Interface, Factory,
lifecycle, ownership, or metadata model, the pattern decomposition is wrong.

## No-fallback rule

Post-3.0 experiments on the long-lived development branch may change before
release. Salts 3.0.0 remains the canonical baseline; when a new contract is
selected:

- migrate integration-branch consumers;
- delete superseded experimental spellings;
- do not retain alias/fallback paths merely because an earlier branch revision
  used them;
- do not merge partial duplicate semantic models to `master`.

Formal proof may be added later, but is not a prerequisite for engineering
work under #1012 or the already integrated #1005–#1009 foundation.
