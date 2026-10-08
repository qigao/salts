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

`CMETA_MANIFEST_COMPONENT` retains numeric kind 1 from the 2.x static Plugin
declaration API, but its public semantic name and descriptor layout are now
Component. Numeric equality does not make the old descriptor compatible. There
is no `CMETA_MANIFEST_PLUGIN` compatibility alias in the 2.3.0 candidate SDK.

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

`cmeta_contract_fingerprint_component()` fingerprints the declaration format,
optional canonical configuration contract, ordered provides/requires **shape**
and canonical Interface contracts in fingerprint domain 6.

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
    -> Salts::Component Configurator
```

Dynamic:

```text
DSO
    -> Salts::Plugin manifest/export
    -> live Plugin lease
    -> borrowed cmeta_component_desc
    -> Salts::Component Configurator
```

A dynamic consumer must hold the Plugin lease through every descriptor,
Interface, callback, instance teardown, and dependent cleanup that can reach
provider-owned code or metadata.

## Migration from Salts 2.x

The most recent published Salts Release is **2.2.0**. The 3.0.0 Release
was withdrawn and the next release identifier is **2.3.0**, not 3.0.0 or
the former interim 4.0.0 proposal. The static `<cmeta/plugin.h>`
declarations shipped in 2.2.x are replaced with Component metadata in the
2.3.0 candidate. This is an intentional **source and native ABI break
within major 2**; no legacy aliases or runtime fallback are provided.

| Salts 2.2.x static metadata | 2.3.0 candidate replacement |
| --- | --- |
| `<cmeta/plugin.h>` | `<cmeta/component.h>` |
| `cmeta_plugin`, `cmeta_plugin_empty`, `cmeta_plugin_meta` | `cmeta_component`, `cmeta_component_empty`, `cmeta_component_meta` |
| `cmeta_plugin_desc`, `cmeta_plugin_capability`, `cmeta_plugin_role` | `cmeta_component_desc`, `cmeta_component_capability`, `cmeta_component_role` |
| `CMETA_PLUGIN_DECLARATION_VERSION` (1) | `CMETA_COMPONENT_DECLARATION_VERSION` (2) |
| `CMETA_PLUGIN_PROVIDES`, `CMETA_PLUGIN_REQUIRES` | `CMETA_COMPONENT_PROVIDES`, `CMETA_COMPONENT_REQUIRES` |
| `CMETA_MANIFEST_PLUGIN`, `cmeta_manifest_plugin_entry` | `CMETA_MANIFEST_COMPONENT`, `cmeta_manifest_component_entry` |
| `cmeta_manifest_get_plugin`, `cmeta_plugin_get_capability` | `cmeta_manifest_get_component`, `cmeta_component_get_capability` |
| `cmeta_contract_fingerprint_plugin` | `cmeta_contract_fingerprint_component` |

`cmeta_provides` and `cmeta_requires` retain their spellings for
Component roles. Dynamic `<salts/plugin.h>` and `<salts/plugin_decl.h>`
remain the sole module-load/lease authority.

Migrate the generated declaration header, macro, metadata accessor and
manifest entry together. Manual format-1 descriptors must be rebuilt for
format 2: `name` becomes `stable_id`, `count` becomes
`capability_count`, and optional `config` borrows a canonical typed
DataDesc with a valid native `storage_type`. Kind-only Sequence/Set/Map
schema descriptors cannot be admitted as Component configurations. Never
cast a format-1 record into a format-2 record.

Regenerate all stored contract fingerprints. Format 2's domain-6 digest
includes config and declaration-format semantics, while stable identity
is validated independently. A 2.2.x fingerprint is not a 2.3.0 token.

CMake package admission uses `SameMinorVersion`; even a non-EXACT
`find_package(Salts 2.2 CONFIG)` request against the 2.3 SDK must
fail. SDK-owned shared targets use SONAME epoch `2.3` rather than the
released 2.2 epoch. CFlow's independently versioned ABI and negotiated
Reflection/Plugin protocol epochs are not tied to SDK SemVer. Rebuild
each consumer, provider DSO, host and contract cache with one immutable
2.3.0 candidate; do not mix 2.2 and 2.3 headers, layouts or binaries.

Rollback restores the entire matching 2.2.x deployment and its caches.
There is no in-place conversion or raw runtime-state migration.

## Generation concurrency and resource admission

`Salts::ComponentPlugin` builds a complete candidate before publication. Its
runtime mutex serializes publication, scope admission/release, close and the
drain claim. A scope pins one immutable generation: publication changes only
new admissions, and drain returns BUSY while any scope still pins that
generation. Close rejects subsequent acquisitions; existing scopes can still
use their services until release. Component stop and ObjectRef cleanup finish
before module leases are released. At most two generations are attached.

Runtime initialization returns `SALTS_COMPONENT_PLUGIN_RESOURCE_ERROR` if the
platform mutex cannot be created. The runtime stays zero/uninitialized and can
be retried; failed initialization never enables admission without a lock.

Keep the runtime, registry, generation bundles and caller-owned storage alive
and address-stable until scopes are released and all attached generations are
drained. A live scope has one owner and must not be copied or used concurrently
with its release; service views expire at release. Provider methods follow their
own concurrency contract. Runtime init/destroy and generation build/discard or
reuse require exclusive access to their objects. Do not read mutable runtime
or generation fields while another thread is changing them.

Formal CTest qualification includes a platform-adapter mutex failure/retry
test, DSO-backed readers holding old/new scopes across publication and close,
repeated acquisition races with publication, and concurrent drain claims that
release the provider lease exactly once. These tests do not replace a
ThreadSanitizer run when a supported runner is available.

CMeta itself owns no loader or dynamic service registry.

## Current integration scope

Format 2 contains stable identity, optional typed config, and provides/requires.
Typed configuration reuses canonical `cmeta_data_desc` identity directly.
External YAML/JSON/XML/CLI parsing remains outside CMeta. Factory and lifecycle
execution authority are deliberately not embedded in the descriptor; they
belong to the Component runtime binding under #1008.

The Component baseline is integrated in master source for the planned Salts
2.3.0 SDK, but not part of the last published 2.2.0 Release. The separate
ACE lifecycle hardening lives in Draft PR #1013 (DO NOT MERGE).
