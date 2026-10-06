# Static manifests and ABI fingerprints

This is the portable metadata substrate for issue #926.

## Boundary

CMeta manifests are immutable tables compiled into the consumer or provider.
They do not own plugin instances, allocate registry nodes, run constructors, or
create a universal mutable process registry.

The architecture keeps three concepts deliberately separate:

```text
descriptor = identity and canonical semantic truth
manifest   = discovery: which descriptors participate in one explicit set
linker     = storage/aggregation backend for that set
```

Reflection descriptors therefore do not depend on a registry. Typed code reaches
its canonical descriptor directly (for example through the existing CMeta type
surface) and must not perform a process-global name lookup as the normal path.
A manifest may borrow descriptor pointers, but it does not become a second type,
ownership, or lifecycle authority.

Declaring a reflected type also does not implicitly publish it into a process-
wide type registry. Discovery is explicit and opt-in through a manifest. This
preserves dead stripping, private descriptor boundaries, static-library/DLL
composition, and Plugin lease ownership.

ELF, Mach-O, and COFF implementations may later aggregate manifest pointers with
qualified linker-section backends, but section names, attributes, pragmas, and
start/stop symbols are not part of the public contract. A generated array is the
portable reference representation.

```c
static const int json_codec = 1;
static const int xml_codec = 2;

cmeta_registry(codec_manifest,
    cmeta_entry(json_codec)
    cmeta_entry(xml_codec)
);
```

Entries retain declaration order and contain a borrowed descriptor pointer.
The manifest owns no referenced object and performs no lifetime operation.

## Descriptor tables versus manifest tables

Descriptor-local tables describe one canonical semantic object, for example:

```text
User descriptor
  -> fields[]
  -> lifecycle
  -> traits

Function descriptor
  -> parameters[]
  -> result

Enum descriptor
  -> values[]
```

These tables are immutable metadata owned by or referenced from the canonical
descriptor. They require no linker discovery.

Manifest tables solve the different problem of explicit discovery:

```text
codec manifest
  -> json codec descriptor
  -> xml codec descriptor

plugin manifest
  -> provider descriptor
  -> capability descriptor
```

Consequently, CMeta must not implement `cmeta_type(T)` by searching a global
manifest. The normal typed path is a direct/static descriptor reference.
Name-based manifest lookup, if later required by tooling or control-plane code,
is optional discovery and never the canonical Reflection path.

## ABI fingerprints

`CMETA_ABI_FINGERPRINT_VERSION` versions the byte-level algorithm independently
from package versions and `CMETA_REFLECTION_ABI_VERSION`. The v1 builder uses a
fixed FNV-1a 64-bit stream, fixed little-endian integer encoding, and
length-prefixed strings. Descriptor addresses and function addresses are never
hashed.

`cmeta_abi_fingerprint_type()` currently covers the stable type surface that is
already canonical in CMeta: name, size, alignment, kind, trait capability flags,
semantic type identity, and immediate pointee metadata. It intentionally hashes
trait capability flags rather than implementation function addresses.

Future struct/enum/function/plugin fingerprints must feed their canonical
semantic rows into the same versioned builder. They must not hash padding,
linker addresses, source paths, timestamps, or build-directory data.

A manifest entry should reference the canonical descriptor/fingerprint rather
than duplicate field, method, trait, ownership, or lifecycle schema.

## Linker backend rule

A linker-section backend may optimize aggregation, but it must preserve exactly
the same immutable manifest semantics as the portable generated-array backend.

In particular:

- no public ELF/Mach-O/COFF section spelling;
- no public `__start_*` / `__stop_*` contract;
- no constructor-driven mutable registration;
- no implicit registration of every reflected type;
- no hidden ownership or Plugin lease retention;
- no runtime Reflection lookup added to typed hot paths.

If a platform cannot preserve these semantics, it should use the portable
generated/static manifest representation instead of a reduced-safety fallback.

## Follow-up slices

- plugin/capability manifests consume these immutable tables while lifecycle
  remains with existing descriptors and explicit leases;
- typed tracepoints carry statically prepared payload metadata and use #923
  static-key branching when that substrate lands;
- named fault points use the same #923 gate and deterministic test control;
- qualified ELF/Mach-O/COFF aggregation is an optimization/backend concern, not
  a second public registry model.
