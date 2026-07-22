# DataBind Record MIR ABI v1

## Context

The legacy binary parser emits `parse_<Type>(data, length)` and calls runtime
setters with field-name strings. That ABI remains the compatibility path for
`data_bind_parse()` and generated MIR/BMIR files.

`DataBindRecord` needs a schema-shaped object while avoiding field-name lookup
in the binary parse hot path. JSON, YAML, XML, and CSV already have native
parsers and writers, so they do not use this ABI.

## Decision

Runtime codec creation emits an additional function per message:

```c
DataBindValue *parse_record_v1_Type(const uint8_t *data, int64_t length,
                                    DataBindValue *record);
```

The caller creates `record` from a codec-owned immutable layout. The layout
defines field order, field names, and child layouts. The MIR function writes
through typed callbacks such as:

```c
int record_set_slot_uint32_v1(DataBindValue *record, uint32_t slot,
                              uint32_t value);
```

Slots are constants in generated MIR. Setters validate the slot and reject a
second write. Composite fields retain their object hierarchy. Lists and groups
receive their element layout before child records are created.

On success, the parser returns the same owning pointer it received. On any
parse or setter failure, the parser frees the complete partially built tree and
returns `NULL`. A successful result clears all internal layout pointers before
it is returned. Field names are owned copies, so a Record may outlive its codec.

The codec owns layouts for its lifetime. Layouts are read-only after codec
creation. Parsing does not mutate a layout, and runtime allocation failure is
reported as `DATA_BIND_ERR_OOM`.

## Compatibility

- `data_bind_parse()` continues to call `parse_<Type>` and preserves flattened
  composite field names such as `header.seq`.
- `data_bind_record_from_bin()` calls `parse_record_v1_<Type>` and exposes a
  nested `header` object.
- `data_bind_generate_mir()` continues to emit only the dynamic `parse_<Type>`
  callback ABI. Additive data items identify the parser ABI and exact schema
  fingerprint so `data_bind_create_from_mir()` and
  `data_bind_create_from_bmir()` can validate and load the module. Existing
  hosts do not gain new imports.
- JSON, YAML, XML, and CSV Record constructors continue to use native binders.
  All Record serializers continue to use the existing native writers.
- No public structure layout or exported callback table changes.

## Alternatives

Changing `parse_<Type>` in place was rejected because it would break existing
MIR/BMIR hosts. Generating C or C++ structs was rejected because it introduces a
source compiler and per-schema binary ABI. Loaded MIR/BMIR therefore uses the
dynamic object parser and existing object serializers; the slot-oriented Record
parser remains available only on codecs generated in-process.

## Migration And Rollback

Callers opt into the slot path by using `data_bind_record_from_bin()`. Existing
`data_bind_parse()` and `data_bind_object_from_bin()` callers require no change.
Rollback consists of routing `data_bind_record_from_bin()` back through the
object parser and removing the runtime-only v1 function generation; wire data,
generated legacy BMIR, and public types remain unchanged.

## Verification

Record tests cover scalar fields, nested composites, repeating group objects,
legacy parser compatibility, truncated-input cleanup, native JSON output, and
Record access after codec destruction. Compiler tests verify that legacy MIR
output contains neither `parse_record_v1_` nor `record_set_slot_` imports.
