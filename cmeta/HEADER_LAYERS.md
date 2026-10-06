# CMeta header layering

CMeta public headers are intentionally split by semantic ownership.

## Core CMeta

Core headers define canonical native metadata, type identity, lifecycle semantics
and metadata-aware value contracts. Reflection-only consumers should prefer
these headers or `<cmeta/meta.h>`.

Core includes:

- cmeta.h
- data.h / data_select.h
- declared_type.h
- entry.h
- enum.h
- flags.h
- function.h
- interface.h
- invokable.h
- lifecycle.h
- manifest.h
- method.h
- object.h / object_interface.h
- status.h
- struct.h
- type_identity.h / type_select.h / type_traits.h
- value.h
- variant.h

## Structured-C helpers

These remain ordinary-C helpers built around CMeta metadata/lifecycle semantics:

- scope.h
- range.h
- collector.h
- compute.h
- infer.h
- pp.h
- generic.h

`meta.h` is the Core CMeta aggregate and does not promise Structured-C helpers.
Consumers include these helpers explicitly. If one later acquires an external
runtime owner, it must move to the optional layer.

## Optional runtime/control-plane adapters

These are not part of `<cmeta/meta.h>`:

- fastpath.h
- trace.h
- pool.h
- local.h

Consumers must include optional adapters explicitly.

The runtime mechanism behind an adapter remains owned by its canonical module.
A cmeta-prefixed facade does not transfer ownership into CMeta.

## Dependency rule

```text
Core CMeta
   -/-> Concurrency
   -/-> Coroutine
   -/-> Plugin
   -/-> CFlow
```

Optional adapters may depend outward when necessary, but aggregate Reflection
headers must not make those dependencies implicit.

Atomic and RCU helpers are owned directly by Salts::Concurrency and are not
CMeta adapters. Use `<salts/atomic.h>` and `<salts/rcu.h>`.

## No compatibility aggregate

When an optional runtime facility is removed from `meta.h`, in-repository
consumers must include its explicit header. Do not add forwarding aggregate
headers or feature aliases solely to preserve accidental transitive inclusion.

Tracked by #957, #959 and #960.


Fast-key/native acquire-load runtime is owned by Salts::Platform via
`<salts/fastpath.h>`. CMeta `<cmeta/fastpath.h>` is only the typed
FunctionDesc/FunctionAbi static-call projection. Trace/fault metadata consumes
the Platform gate explicitly.
