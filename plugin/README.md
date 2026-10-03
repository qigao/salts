# Salts Plugin

`Salts::Plugin` is the Salts-owned dynamic-module publication and lifecycle
runtime built directly on canonical CMeta semantics.

Plugin and CFlow are independent capabilities. Plugin owns module publication,
loading, registry, leases and quiescent unload; it does not own graph/execution
semantics and does not link CFlow.

## Targets

```text
Salts::PluginABI
    -> <salts/plugin.h>
    -> Salts::CMeta

Salts::Plugin
    -> Salts::PluginABI
    -> Salts::Core
    -> Salts::Platform
    -> platform dynamic loader
```

`Salts::PluginABI` is publication-only and carries no loader/registry runtime.
Plugin providers can therefore publish CMeta Function/Interface exports without
linking the host runtime.

## One ABI

Plugin supports exactly one current ABI. There is no ABI negotiation, fallback,
readable-prefix compatibility, or retry of older layouts.

Moving package/repository ownership does not change the binary ABI by itself.
A Plugin ABI bump is required when the manifest/export/query binary contract
changes **or when a transitive CMeta Reflection layout published by an export
changes incompatibly**. A host must reject the older Plugin epoch before it
dereferences reflected Function/Interface descriptors.

## Semantic boundary

Plugin publishes canonical CMeta capabilities:

```text
operation/service capability -> CMeta Function + FunctionAbi + exact adapter
stateful provider capability -> CMeta Interface + {self,vtable}
```

It does not define a second function, interface, callable or execution model.

## Lifecycle

```text
load
  -> validate current manifest ABI
  -> LOADED
  -> start
  -> STARTED
  -> acquire/release leases
  -> request_stop
  -> STOPPING
  -> plugin quiescent + leases == 0
  -> QUIESCENT
  -> unload
```

Everything borrowed from a Plugin DSO remains valid only while a live lease
keeps the module loaded.

## CFlow composition

There is no Plugin -> CFlow or CFlow -> Plugin dependency.

An application that projects a Plugin Function into CFlow uses the ordinary
CMeta -> CFlow projection and keeps the Plugin lease alive for the whole
borrowed-code/reflection lifetime. Any helper for this choreography is
consumer/helper-level code, not a PluginCFlow subsystem.
