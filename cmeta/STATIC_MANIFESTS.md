# Static manifests and ABI fingerprints

This is the portable metadata substrate for issue #926.

## Boundary

CMeta manifests are immutable tables compiled into the consumer or provider.
They do not own plugin instances, allocate registry nodes, run constructors, or
create a universal mutable process registry.

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

## Follow-up slices

- plugin/capability manifests consume these immutable tables while lifecycle
  remains with existing descriptors and explicit leases;
- typed tracepoints carry statically prepared payload metadata and use #923
  static-key branching when that substrate lands;
- named fault points use the same #923 gate and deterministic test control;
- qualified ELF/Mach-O/COFF aggregation is an optimization/backend concern, not
  a second public registry model.
