# tbe_compiler Command Line Options

## Overview

`tbe_compiler` generates code and DSL declarations from TBE schema files for multiple target languages and RulesForge integration.

## Basic Usage

```bash
tbe_compiler <schema_file> [options]
```

## Options

### Required

- `<schema_file>`
  - Path to the `.schema` definition file
  - Example: `order.schema`

### Language Output

- `--output <file>` or `-o <file>`
  - Output file path for generated code
  - Default: stdout
  - Example: `--output order.h`

- `--lang <language>` or `-l <language>`
  - Target language or module artifact format
  - Options: `c`, `cpp`, `go`, `rust`, `python`, `py`, `ts`, `typescript`, `mir`, `bmir`
  - Default: `c`
  - Example: `--lang c`

- `--template <file>` or `-t <file>`
  - Path to custom Mustache template file
  - Overrides `--lang` option
  - Example: `--template my_template.mustache`

- `--source-output <file>` or `-s <file>`
  - With the built-in C generator, emits the typed serialization companion `.c` file
  - Requires `--output`; custom templates and non-C languages are rejected
  - The generated header exposes strong record types plus binary/JSON/YAML/CSV/XML APIs
  - Example: `--output order.h --source-output order.c`

- `--guest-output <file>` or `-g <file>`
  - With the built-in C generator, emits a Wasm-friendly guest adapter `.c` file
  - Requires `--output`; custom templates and non-C languages are rejected
  - The adapter converts JSON/YAML/CSV/XML byte slices to generated wire views, and back,
    through a caller-provided `tbe_guest_bridge_t`
  - The adapter performs no parsing and owns no buffers; the runtime bridge controls
    schema registration, sandbox policy, quotas, and provider errors
  - Example: `--output order.h --guest-output order_guest.c`

### DSL Integration (RulesForge)

- `--dsl-output <file>` or `-d <file>`
  - Generate DSL type declarations (.rfl file)
  - Contains `declare` statements for use in RulesForge
  - Example: `--dsl-output order.rfl`

## Usage Examples

### Example 1: Generate C Header Only

```bash
tbe_compiler order.schema --output order.h
```

### Example 2: Generate DSL Type Declarations

```bash
tbe_compiler order.schema --dsl-output order.rfl
```

### Example 2a: Generate Strong Typed C Bindings

```bash
tbe_compiler order.schema --lang c --output order.h --source-output order.c
```

The generated API includes `Order_t`, `Order_init`/`Order_clear`, schema codec creation,
and `Order_from_*`/`Order_to_*` functions for `bin`, `json`, `yaml`, `csv`, and `xml`.
`Orders_schema_codec()` exposes a schema-specific dispatch table for trusted host providers.
Its `text_to_binary_into` operation binds JSON/YAML/CSV/XML directly into caller-owned,
capacity-bounded wire storage, so a runtime can enforce its output quota before conversion.
The reverse `binary_to_text` operation returns an allocated host buffer and is intended for
trusted host code unless the runtime also enforces the serializer's temporary-allocation budget.
Schema `uuid` fields are generated as `turbo_uuid_t`; text formats use canonical UUID strings
and binary serialization preserves the fixed 16-byte wire value.
Compile `order.c` in the consumer target and link `TurboUtils::DataBind`:

```cmake
add_executable(order_app main.c order.c)
target_link_libraries(order_app PRIVATE TurboUtils::DataBind)
```

The same generated header is C++ compatible. Its C functions use `extern "C"`, and
the schema namespace provides non-copyable RAII owners that initialize and clear the
generated C record automatically:

```cpp
#include "order.h"

#include <string_view>

int main() {
    std::string_view input = R"({"id":42})";
    DataBind *codec = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    Orders_typed::OrderOwner order;
    int result = 1;

    if (Orders_codec_create(&codec, &error) == DATA_BIND_OK &&
        order.from_json(codec, input.data(), input.size(), &error) == DATA_BIND_OK) {
        result = 0;
    }
    data_bind_free(codec);
    return result;
}
```

Compile the companion `order.c` as C even when the application target is C++. Fixed-layout
binary input is decoded directly through the generated native descriptor after validating
it against the codec schema. Variable `list`/`set`/`map` layouts retain the existing dynamic
binary parser as a compatibility path before committing into the owning struct. Text formats
retain the schema binder so enum names, field formats, and extended scalar rules remain
identical to the dynamic API. `--lang cpp` without `--source-output` continues to generate
data-only `std::string`/`std::vector` types and does not provide these serialization functions.

### Example 2b: Generate a Wasm Guest Adapter

```bash
tbe_compiler order.schema --lang c --output order.h --guest-output order_guest.c
```

Compile `order_guest.c` together with the guest application. The guest supplies a
`tbe_guest_bridge_t` whose callbacks forward to the runtime's versioned DataBind
capability. For example, `Order_guest_from_json` writes wire bytes into a caller-owned
buffer and binds an `Order_view_t`; `Order_guest_to_json` serializes an existing view
into a caller-owned text buffer. Callback failures are propagated unchanged, while
invalid pointers, `size_t` values outside the Wasm `uint32_t` ABI, invalid bridge
output lengths, and unbindable wire data return `tbe_guest_status_t` errors.

CSV input accepts a zero-based logical record index. The runtime remains responsible
for applying its input, output, object, and execution quotas before invoking the
trusted host codec.

For a freestanding wasm32 build, define `TBE_WASM_GUEST=1`. This removes the generated
wire header's dependency on host libc and the TurboUtils UUID runtime while preserving
the same fixed 16-byte `turbo_uuid_t` value layout:

```bash
clang --target=wasm32-unknown-unknown -DTBE_WASM_GUEST=1 -O2 -nostdlib \
  -Igenerated -Ipath/to/tbe/schema/include -c order_guest.c
```

Initialize an object before its first use, clear it when finished, and release serialized
buffers with `tbe_typed_serialized_free`:

```c
#include "order.h"

#include <string.h>

int main(void) {
const char *input = "{\"id\":42}";
DataBind *codec = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
Order_t order;
char *json = NULL;
size_t json_len = 0;
int result = 1;

Order_init(&order);
if (Orders_codec_create(&codec, &error) == DATA_BIND_OK &&
    Order_from_json(codec, &order, input, strlen(input), &error) == DATA_BIND_OK &&
    Order_to_json(&order, &json, &json_len, &error) == DATA_BIND_OK) {
    result = 0;
}
tbe_typed_serialized_free(json);
Order_clear(&order);
data_bind_free(codec);
return result;
}
```

Output `order.rfl`:
```rfl
package OrderSchema

declare Order
    id: int
    total: double
    tier: String
end
```

### Example 3: Generate Both

```bash
tbe_compiler order.schema --output order.h --dsl-output order.rfl
```

### Example 4: Generate C++ Types

```bash
tbe_compiler order.schema --lang cpp --output order.hpp
```

### Example 5: Generate Go Types

```bash
tbe_compiler order.schema --lang go --output order.go
```

### Example 6: Generate TypeScript Types

```bash
tbe_compiler order.schema --lang ts --output order.ts
```

### Example 7: Generate Rust Types

```bash
tbe_compiler order.schema --lang rust --output order.rs
```

### Example 8: Generate Python Types

```bash
tbe_compiler order.schema --lang py --output order.py
```

### Example 9: Generate MIR IR Module

```bash
tbe_compiler order.schema --lang mir --output order.mir
```

### Example 10: Generate Binary MIR Module

```bash
tbe_compiler order.schema --lang bmir --output order.bmir
```

## Removed Options

- `--codec-project` (REMOVED)
  - Codec DLL project generation is no longer supported by the compiler directly.
- `--rfl-output` (REPLACED)
  - Replaced by `--dsl-output`.

## Notes

- `--dsl-output` uses `templates/rfl_types.mustache` by default.
- DSL output is intended for use in RulesForge to define the structure of data being processed.
- C output is the complete wire-access target with generated view/builder APIs.
- `--source-output` adds owning strong types and schema-driven text/binary serialization on top
  of the existing zero-copy view/builder API. Union declarations are currently rejected for
  this companion source. Generated `*_to_bin_into` and schema-codec `text_to_binary_into`
  functions never allocate their output buffer; insufficient capacity is reported with the
  required size in `out_len`.
- `--guest-output` adds allocation-free adapters over the zero-copy wire views. It does not
  embed JSON/YAML/CSV/XML parsers into Wasm and does not require `--source-output`.
- C++, Go, Rust, Python, and TypeScript outputs currently generate schema type definitions, not complete wire codecs.
- MIR outputs are loadable modules, not standalone executables. DataBind hosts can use
  `data_bind_create_from_mir()` or `data_bind_create_from_bmir()` with the exact schema
  text used for generation, then call `data_bind_object_from_bin()` and the normal object
  serializers. See `tbe/data_bind/examples/bmir_runtime.c` for a complete generate, load,
  field-access, and JSON serialization flow. Custom hosts may instead bind callbacks such as `create_obj`, `set_int`,
  `set_dbl`, `set_str`, and `read_varstr`, link or JIT the module, and call generated
  functions such as `parse_Order`.
