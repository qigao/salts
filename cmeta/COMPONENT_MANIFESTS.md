# Static Component declarations

`<cmeta/component.h>` defines immutable CMeta component capability metadata for
the Component Configurator work tracked by #1003.

A declaration contains only:

```text
stable component/provider identity
+ optional typed config DataDesc
+ ordered provides Interface rows
+ ordered requires Interface rows
```

It does **not** load a module, select a provider, create an instance, own a
lease, or advance lifecycle. Dynamic module authority remains
`Salts::Plugin`.

## Declaration

```c
#define DATABASE_METHODS(X,I) \
    X(I,F0,int,version,value,&cmeta_type_int,CMETA_ABI_SCALAR)
#define TLS_METHODS(X,I) \
    X(I,F0,bool,ready,value,&cmeta_type_bool,CMETA_ABI_SCALAR)

CMETA_INTERFACE(DatabaseDriver, DATABASE_METHODS);
CMETA_INTERFACE(TLS, TLS_METHODS);

cmeta_component(PostgresDriver,
    cmeta_provides(DatabaseDriver)
    cmeta_requires(TLS));
```

The generated `cmeta_component_desc` is TU-local immutable metadata.
`stable_id` is the expanded declaration identifier and is semantic component
identity for explicit selection/diagnostics. Descriptor addresses are not
identity across TUs or DSOs.

The provides/requires rows borrow canonical `cmeta_interface_desc` objects.
No method schema, ownership contract, vtable, handle, callback, or Plugin lease
is duplicated in the component descriptor.

## Static discovery

Component declarations participate only through explicit manifests:

```c
cmeta_registry(drivers,
    cmeta_manifest_component_entry(
        "postgres", cmeta_component_meta(PostgresDriver)));
```

`CMETA_MANIFEST_COMPONENT` retains numeric kind 1 from the experimental
integration history, but its public semantic name is Component. There is no
`CMETA_MANIFEST_PLUGIN` compatibility alias on the integration branch.

Use:

```c
const cmeta_component_desc *component = NULL;
cmeta_manifest_get_component(&drivers, 0u, &limits, &component);
```

and inspect one exact-role capability with
`cmeta_component_get_capability()`.

Queries are bounded by caller-provided manifest limits and leave outputs
unchanged on failure.

## Fingerprint

`cmeta_contract_fingerprint_component()` fingerprints the ordered
provides/requires **shape** and canonical Interface contracts in fingerprint
domain 6.

The component `stable_id` is validated but intentionally excluded from the
shape digest. Identity and contract shape are separate admission facts:

```text
stable_id        -> provider/component identity
component hash   -> provides/requires Interface shape
Interface checks -> canonical protocol compatibility
```

Equal hashes do not prove provider identity or authenticity.

## Static versus dynamic providers

Static:

```text
cmeta_component_desc
    -> explicit CMeta manifest
    -> future Component Configurator
```

Dynamic:

```text
DSO
    -> Salts::Plugin manifest/export
    -> live Plugin lease
    -> borrowed cmeta_component_desc
    -> future Component Configurator
```

A dynamic consumer must hold the Plugin lease through every descriptor,
Interface, callback, instance teardown, and dependent cleanup that can reach
provider-owned code or metadata.

CMeta itself owns no loader or dynamic service registry.

## Current integration scope

Format 2 contains stable identity, optional typed config, and provides/requires.
Typed configuration reuses canonical `cmeta_data_desc` identity directly.
External YAML/JSON/XML/CLI parsing remains outside CMeta. Factory and lifecycle
execution authority are deliberately not embedded in the descriptor; they
belong to the Component runtime binding under #1008.

Development remains on
`feature/cmeta-pattern-component-runtime`; this contract is not yet a
released master API.
