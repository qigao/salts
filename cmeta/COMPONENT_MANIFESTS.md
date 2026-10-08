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
is no `CMETA_MANIFEST_PLUGIN` compatibility alias in the 3.0.0 integration SDK.

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

The static `<cmeta/plugin.h>` declarations were published in Salts 2.x, including
2.2.0. Replacing them is an intentional source and binary compatibility break
scheduled for Salts **3.0.0**. The integration branch does not supply legacy
aliases: keeping two names for one static capability model would preserve the
ambiguity between declaration metadata and the dynamic Plugin runtime.

| Salts 2.x static metadata | Salts 3.0.0 replacement |
| --- | --- |
| `<cmeta/plugin.h>` | `<cmeta/component.h>` |
| `cmeta_plugin`, `cmeta_plugin_empty`, `cmeta_plugin_meta` | `cmeta_component`, `cmeta_component_empty`, `cmeta_component_meta` |
| `cmeta_plugin_desc`, `cmeta_plugin_capability`, `cmeta_plugin_role` | `cmeta_component_desc`, `cmeta_component_capability`, `cmeta_component_role` |
| `CMETA_PLUGIN_DECLARATION_VERSION` (1) | `CMETA_COMPONENT_DECLARATION_VERSION` (2) |
| `CMETA_PLUGIN_PROVIDES`, `CMETA_PLUGIN_REQUIRES` | `CMETA_COMPONENT_PROVIDES`, `CMETA_COMPONENT_REQUIRES` |
| `CMETA_MANIFEST_PLUGIN`, `cmeta_manifest_plugin_entry` | `CMETA_MANIFEST_COMPONENT`, `cmeta_manifest_component_entry` |
| `cmeta_manifest_get_plugin`, `cmeta_plugin_get_capability` | `cmeta_manifest_get_component`, `cmeta_component_get_capability` |
| `cmeta_contract_fingerprint_plugin` | `cmeta_contract_fingerprint_component` |

`cmeta_provides` and `cmeta_requires` keep their spelling but now declare
Component role rows. Existing dynamic APIs in `<salts/plugin.h>` and
`<salts/plugin_decl.h>` are not renamed by this migration.

For generated declarations, update the header, declaration macro, metadata
accessor and manifest entry together. The declaration example above is the
replacement for `cmeta_plugin(PostgresDriver, ...)`. For manual descriptors,
rebuild the initializer against format 2: `name` becomes `stable_id`, which is
now semantic provider identity rather than a diagnostic name; `count` becomes
`capability_count`; the new `config` field is NULL for an unconfigured provider
or borrows the exact canonical DataDesc for its native configuration. Never
cast a format-1 descriptor to `cmeta_component_desc` or reuse its serialized
layout.

Regenerate stored contract fingerprints. Domain 6 retains its numeric value,
but format 2 includes configuration and declaration-format semantics; a 2.x
static Plugin digest is not a 3.0.0 Component admission token. Stable identity
still needs a separate check and is excluded from the shape fingerprint.

Rebuild the host, libraries, provider DSOs and downstream consumers against one
complete 3.0.0 SDK, then qualify their ordinary Plugin exports and provider
bindings before deployment. Do not mix 2.x headers, libraries, descriptors or
cached fingerprints with 3.0.0. CMake's project version and the package manifest
both identify 3.0.0; versioned native libraries use SOVERSION 3. CMake's
`SameMajorVersion` package admission rejects `find_package(Salts 2 CONFIG)`
against this SDK; consumers must request the 3.x major explicitly. Reflection ABI
and the ordinary Plugin ABI retain their own negotiated versions; they do not
override the Component descriptor format or establish 2.x SDK compatibility.

Rollback replaces the complete SDK, rebuilt host and provider set with the
previous 2.x deployment and its original contract cache. There is no in-place
descriptor conversion or mixed-version compatibility path.

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

Development remains on
`feature/cmeta-pattern-component-runtime`; this contract is not yet a
released master API.
