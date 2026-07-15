# DataBind Public C API

`tbe/data_bind` is the third-party integration boundary for TurboUtils schema
binding. It exposes a C ABI over opaque handles and dynamic value accessors.
Consumers should not depend on `modules/data_bind` or `exprtk` unless they are
embedding TurboUtils scripts.

`modules/parser` uses this library for TurboUtils-facing schema binding,
strict boolean validation, and schema reflection. Parser remains responsible
for script value conversion and detailed `validate_ex` diagnostics.
DataBind owns format-aware path query APIs for JSONPath, XMLPath, and CSVPath.
Format parsing inside `tbe/data_bind` uses TurboNet::Parser directly; it does
not call into `modules/parser`.

## Security Considerations

**Schema Trust Boundary:** DataBind uses JIT compilation (via MIR) to generate
optimized binary parsers from schema definitions. The codec creation process
assumes that **schema files come from trusted sources**.

While DataBind includes validation to prevent common issues (field nesting depth
exceeding 32 levels, offsets exceeding 1GB, total field counts exceeding 10,000,
and circular type references), it does not provide comprehensive defense against
all malicious schema constructions.

**Best Practices:**

- Load schema files only from trusted locations (application bundles, verified
  configuration directories, authenticated remote sources).
- Do not create codecs from user-supplied or untrusted schema definitions.
- In multi-tenant environments, isolate schema management and validate schema
  sources before codec creation.
- Binary TBE payload parsing uses JIT-compiled code; ensure input data sources
  are appropriately validated at the application boundary.

**Validation Limits:**

- Maximum field nesting depth: 32 levels
- Maximum field offset: 1GB (1,073,741,824 bytes)
- Maximum total fields per message: 10,000
- Maximum nested type tracking: 256 unique types

Exceeding these limits during codec creation will result in a
`DATA_BIND_ERR_SCHEMA` error with a descriptive message in `DataBindError`.

## CMake

After installing TurboUtils:

```cmake
find_package(TurboUtils CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE TurboUtils::DataBind)
```

On Windows, `TurboUtils::DataBind` defines `DATA_BIND_USE_DLL` for consumers.
Do not define `DATA_BIND_BUILD_DLL` outside the library itself.

## Minimal Use

```c
#include "data_bind.h"
#include <string.h>

DataBind *codec = NULL;
DataBindError err = DATA_BIND_ERROR_INIT;
DataBindStatus status = data_bind_create("orders.tbe", &codec, &err);
if (status != DATA_BIND_OK) {
  return 1;
}

const char *json = "{\"id\":7,\"symbol\":\"ABCD\"}";
DataBindValue *order = NULL;
status = data_bind_parse_json(codec, "Order", json, strlen(json), &order, &err);
if (status != DATA_BIND_OK) {
  data_bind_free(codec);
  return 1;
}

const DataBindValue *id = data_bind_value_get(order, "id");
int32_t value = 0;
if (data_bind_value_get_int32(id, &value) != DATA_BIND_OK) {
  data_bind_value_free(order);
  data_bind_free(codec);
  return 1;
}

data_bind_value_free(order);
data_bind_free(codec);
```

## Ownership

- `DataBind*` is owned by the caller and released with `data_bind_free`.
- `DataBindValue*` results are owned by the caller and released with
  `data_bind_value_free`.
- `DataBindObject*` results own their type name and value tree and are released
  with `data_bind_object_free`. They do not retain a schema codec.
- Binary buffers returned by `data_bind_object_serialize_bin` are released with
  `data_bind_binary_free`.
- Text returned by `data_bind_object_serialize_json`, `_yaml`, `_xml`, or `_csv`
  is released with `data_bind_serialized_free`.
- `data_bind_value_clone` creates an independent deep copy of the complete value
  tree. On success the caller owns the copy; source ownership is unchanged. On
  failure the output is `NULL`. Use this API when retry, fan-out, queue, or
  another lifetime boundary requires an independent bound value.
- `const char*`, child `DataBindValue*`, and map entries returned by accessor
  functions are borrowed. They become invalid when the owning codec or value is
  freed.
- It is valid to pass `NULL` to `data_bind_free` and `data_bind_value_free`.

## Error Handling

The public ABI uses `DataBindStatus` return values plus caller-owned
`DataBindError` storage. Functions that create or parse values return
`DATA_BIND_OK` on success and write the owned result through an out parameter:

```c
DataBindError err = DATA_BIND_ERROR_INIT;
DataBindValue *value = NULL;
DataBindStatus status =
  data_bind_parse_json(codec, "Order", json, strlen(json), &value, &err);
if (status != DATA_BIND_OK) {
  fprintf(stderr, "%s: %s\n", data_bind_status_name(status), err.message);
}
```

`DataBindError` includes `code`, `line`, `column`, `path`, and `message`. The
message buffer belongs to the caller-provided struct, so it is safe to read
after the codec function returns.

### Error Code Mapping

All parsing functions use consistent error codes across input formats:

| Error Code | Binary Parse | JSON Parse | CSV Parse | XML Parse |
|------------|--------------|------------|-----------|-----------|
| `DATA_BIND_OK` | Success | Success | Success | Success |
| `DATA_BIND_ERR_INVALID_ARG` | NULL codec/buffer | NULL codec/json | NULL codec/csv | NULL codec/xml |
| `DATA_BIND_ERR_TYPE_NOT_FOUND` | Unknown type | Unknown type | Unknown type | Unknown type |
| `DATA_BIND_ERR_PARSE` | Binary decode failed | JSON parse failed | CSV parse failed | XML parse failed |
| `DATA_BIND_ERR_TYPE_MISMATCH` | Field type wrong | Value type wrong | Cell type wrong | Node type wrong |
| `DATA_BIND_ERR_SCHEMA` | Schema validation | Schema mismatch | Schema mismatch | Schema mismatch |
| `DATA_BIND_ERR_RUNTIME` | JIT runtime error | Binding error | Binding error | Binding error |
| `DATA_BIND_ERR_OOM` | Out of memory | Out of memory | Out of memory | Out of memory |

### Error Path Format

The `DataBindError.path` field uses format-specific location identifiers for
consistent error reporting:

- **Binary TBE**: `"binary: parse failed"` or `"binary: offset N"` when detailed
  position tracking is available
- **JSON**: `"json: $.field.path"` using JSONPath-style notation for nested fields
- **CSV**: `"csv: row R col C"` or `"csv: row R field_name"` for header-based location
- **XML**: `"xml: /root/element[@attr]"` using XMLPath-style notation

Example error output:

```c
// Binary parse error
err.path = "binary: parse failed"
err.message = "Binary bind failed for type: Order"

// JSON parse error  
err.path = "json: $.items[2].price"
err.line = 15
err.column = 12
err.message = "Expected number, got string"

// CSV parse error
err.path = "csv: row 42 col 5"
err.line = 42
err.message = "Invalid date format in field 'orderDate'"

// XML parse error
err.path = "xml: /orders/order[3]/status"
err.message = "Unknown enum value: 'PENDING'"
```

## Supported Inputs

- `data_bind_parse`: binary TBE payload.
- `data_bind_parse_json`: JSON object or scalar matching the schema type.
- `data_bind_parse_json_all`: JSON array binds each item; non-array binds as a
  one-item list.
- `data_bind_parse_csv`: CSV with a header row. Nested fields use paths such as
  `header.seq`, `bids[0].price`, and `attrs.x`.
- `data_bind_parse_csv_all`: binds every parsed CSV row.
- `data_bind_parse_xml`: XML document root binds to the requested schema type.
  Record fields bind from same-name child elements first and same-name
  attributes second. Repeated same-name elements bind lists/groups.
- `data_bind_parse_json_path`: bind the first value matching a JSONPath expression.
- `data_bind_parse_json_path_all`: bind all values matching a JSONPath expression.
- `data_bind_parse_xml_path_all`: XMLPath selects nodes and binds each selected
  node. Use expressions supported by the bundled TurboNet XML parser, such as
  `//order`.
- `data_bind_parse_csv_path`: bind rows matching a CSVPath expression.
- Stream constructors encode binding semantics directly:
  `data_bind_stream_json_create`, `data_bind_stream_json_all_create`,
  `data_bind_stream_json_path_create`,
  `data_bind_stream_json_path_all_create`,
  `data_bind_stream_yaml_create`, `data_bind_stream_yaml_all_create`,
  `data_bind_stream_yaml_path_create`,
  `data_bind_stream_yaml_path_all_create`,
  `data_bind_stream_csv_all_create`, `data_bind_stream_csv_path_create`,
  `data_bind_stream_xml_create`, and
  `data_bind_stream_xml_path_all_create`. There is no stream mode or options
  object. JSON root-array streams bind items incrementally and preserve exact
  signed/unsigned 64-bit number tokens; other JSONPath expressions validate
  chunks incrementally with SAX and materialize the DOM on finish. CSV streams
  process complete records incrementally and compile
  CSVPath after the header row. XML streams bind non-overlapping `//name`
  element matches incrementally; other XMLPath expressions validate chunks
  incrementally with SAX and materialize the DOM on finish. YAML streams also
  validate syntax incrementally with SAX, then perform schema and YPATH binding
  on finish.
- `data_bind_stream_feed_file`: read a file in fixed-size chunks and feed it
  through an existing stream. Applications that own an asynchronous I/O loop
  should feed completed buffers with `data_bind_stream_feed`; DataBind does not
  duplicate scheduler or async file APIs.
- `data_bind_stream_set_record_callback`: receive each path-selected,
  schema-bound record synchronously. Root-array JSON, CSV rows, and simple
  descendant XML paths such as `//order` deliver from `feed`; paths that require
  the complete document deliver from `finish`. The callback record is borrowed
  for the callback duration. Returning `DATA_BIND_RECORD_STOP` stops later
  callback delivery without stopping validation or final-value construction;
  returning `DATA_BIND_RECORD_ERROR` fails the stream.
- `data_bind_validate_json_path`: validates the first JSONPath match against the
  schema type.
- `data_bind_validate_csv_path`: validates all CSV rows matching a CSVPath
  expression.
- `data_bind_validate_xml_path`: strictly validates the document root or every
  node selected by the supplied XMLPath expression.
- `data_bind_validate_json`: strictly validates JSON. Arrays are valid only
  when every item binds to the requested schema type.
- `data_bind_validate_csv`: strictly validates every CSV data row against the
  requested schema type. CSV uses the same header path rules as CSV binding.
- `data_bind_create_from_text`: creates a codec directly from schema text in
  memory.

`parse_*_all` APIs are binding helpers: they return a list of successfully
bound values and may skip invalid array items or CSV rows. Use
`data_bind_validate_json`, `data_bind_validate_csv`, or
`data_bind_validate_json_path`, `data_bind_validate_csv_path`,
`data_bind_validate_xml_path` when the caller needs strict all-or-nothing input
validation.

## Text Output

Owned objects can be serialized as JSON, YAML, XML, or CSV. The corresponding
`data_bind_object_write_*` functions deliver the complete serialized document
to a caller callback.

JSON and YAML emit signed and unsigned 64-bit values as exact numeric tokens,
including values outside the IEEE-754 exact-integer range. XML and CSV use
canonical decimal text. UUID values use the canonical UUID string in every
text format and bind back to `DATA_BIND_VALUE_UUID` / `turbo_uuid_t`.

CSV output contains one RFC 4180 header row and one data row. It uses the same
paths accepted by the CSV binder: nested objects use `header.seq`, collections
use `values[0]`, and maps use `attrs.key`. Scalar cells use DataBind's canonical
text representation, including exact signed/unsigned integer decimals,
`true`/`false`, and the existing UUID, temporal, decimal, bigint, and money
formats.

```c
char *csv = NULL;
size_t csv_len = 0;

if (data_bind_object_serialize_csv(order, &csv, &csv_len, &err) == DATA_BIND_OK) {
  send_csv(csv, csv_len);
}
data_bind_serialized_free(csv);
```

CSV has no lossless single-row representation for an empty collection or map.
Those values, `NULL`, paths longer than 255 bytes, non-UTF-8 text/bytes, bytes
containing NUL, and map keys containing `.` or `[` fail with
`DATA_BIND_ERR_TYPE_MISMATCH`; the serializer never drops a value or emits a
partial row.

## Binary Output

`DataBindObject` can be encoded back to the dynamic TBE wire format by supplying
the codec that owns the matching schema:

```c
DataBindObject *order = NULL;
uint8_t *wire = NULL;
size_t wire_len = 0;

if (data_bind_object_from_json(codec, "Order", json, strlen(json),
                               &order, &err) == DATA_BIND_OK &&
    data_bind_object_serialize_bin(codec, order, &wire, &wire_len,
                                   &err) == DATA_BIND_OK) {
  send_payload(wire, wire_len);
}

data_bind_binary_free(wire);
data_bind_object_free(order);
```

Use `data_bind_object_serialize_bin_into` when the transport owns the output
buffer. It validates and measures the complete object before writing. If the
buffer is short, it returns `DATA_BIND_ERR_INVALID_ARG`, leaves the buffer
unchanged, and reports the required size through `out_len`.

The dynamic binary writer matches the current `data_bind_parse` layout: scalar
fields, enums, booleans, UUID, fixed/variable bytes, strings, fixed/variable
lists, sets, string-key maps, fixed composites, and repeating groups in
little-endian schemas. Optional presence bitmaps, unions, big-endian schemas,
and text-only extended scalars do not have a compatible dynamic binary parser
path and fail with `DATA_BIND_ERR_SCHEMA`.

## Record Streaming

Record callbacks add low-latency consumption without replacing the final-value
API:

```c
static DataBindRecordAction on_order(void *ctx,
                                     const DataBindValue *record,
                                     uint64_t index) {
  consume_order(ctx, record, index); /* record is borrowed */
  return DATA_BIND_RECORD_CONTINUE;
}

DataBindValue *all_orders = NULL;
data_bind_stream_t *stream = data_bind_stream_csv_path_create(
    codec, "Order", "side == \"Sell\"", &all_orders, &error);
data_bind_stream_set_record_callback(stream, on_order, app);

/* File, socket, HTTP, or broker code supplies chunks. */
data_bind_stream_feed(stream, chunk, chunk_len);
data_bind_stream_finish(stream);
```

Callbacks run on the thread calling `feed` or `finish`; DataBind owns no thread,
event loop, network connection, or retry policy. Backpressure is applied by the
source: do not issue the next asynchronous read until the callback consumer is
ready. The current parser ABI does not report partially consumed chunks, so it
does not expose mid-feed pause/resume.

CSV and XML use the same schema binder as JSON:

- CSV nested columns use paths such as `header.seq`, `levels[0]`,
  `bids[0].price`, and `attrs.x`; sanitized names such as `header_seq` are
  also accepted by the TurboUtils parser adapter.
- CSV top-level scalar/enum/flags values use a `value` column, with single
  column CSV accepted by binding and validation.
- CSV union values choose the variant from non-empty payload columns on each
  row, so merged dynamic headers can represent different variants per row.
- XML binds record fields from same-name child elements first and attributes
  second. `data_bind_parse_xml_path_all` and `data_bind_validate_xml_path` accept
  XMLPath
  to select repeated record nodes.

## Strict Value Access

Convenience accessors such as `data_bind_value_as_int()` remain available, but
they coerce or return zero on invalid input. New third-party code should prefer
strict accessors:

- `data_bind_value_get_int32`
- `data_bind_value_get_int64`
- `data_bind_value_get_double`
- `data_bind_value_get_bool`
- `data_bind_value_get_string`
- `data_bind_value_get_bytes`
- `data_bind_value_get_uuid`
- `data_bind_value_get_datetime`
- `data_bind_value_get_date`
- `data_bind_value_get_time`
- `data_bind_value_get_duration_milliseconds`
- `data_bind_value_get_decimal`
- `data_bind_value_get_bigint`
- `data_bind_value_get_money`

Strict accessors return `DATA_BIND_OK` only when the requested conversion is
valid.

Schema `bool` values bind to `DATA_BIND_VALUE_BOOL`. Numeric convenience
accessors still read them as `0` or `1`, but strict type checks should use
`data_bind_value_get_bool`.

Schema `uuid` values bind to `DATA_BIND_VALUE_UUID` and are stored internally as
`turbo_uuid_t`. Binary parsing reads the fixed 16-byte field payload. JSON,
YAML, CSV, and XML binding accept canonical UUID text such as
`01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001`; use `data_bind_value_get_uuid` with a
`turbo_uuid_t.bytes` destination or `data_bind_value_as_uuid_string` to read it.

Schema `bytes` values bind to `DATA_BIND_VALUE_BYTES`. Binary parsing reads
fixed or variable bytes payloads, while JSON/XML text binding reads string
content and CSV binding reads the cell text as bytes. Use
`data_bind_value_get_bytes` or `data_bind_value_as_bytes` to read the borrowed
byte view.

Schema `datetime` values bind to `DATA_BIND_VALUE_DATETIME` for JSON, CSV, and
XML text binding. Accepted text is parsed directly through TurboNet::Parser, and
callers can read the native `turbo_datetime_t` with `data_bind_value_get_datetime`
or format it with `data_bind_value_as_datetime_string`. Binary parsing does not
define an implicit datetime wire format; use an explicit numeric schema field
for binary timestamps.

Schema `date`, `time`, and `duration` values bind to `DATA_BIND_VALUE_DATE`,
`DATA_BIND_VALUE_TIME`, and `DATA_BIND_VALUE_DURATION` for JSON, CSV, and XML
text binding. `date` accepts `YYYY-MM-DD`, `YYYY/MM/DD`, or datetime text from
which the date can be extracted. `time` accepts `HH:MM`, `HH:MM:SS`, or
`HH:MM:SS.mmm`. `duration` accepts unit text such as `1h30m5s250ms` and emitted
`H:MM:SS.mmm` text. Use the matching strict accessors or string format helpers
to read them. Binary parsing does not define implicit wire formats for these
temporal scalars.

Schema `decimal` values bind to `DATA_BIND_VALUE_DECIMAL` for JSON, CSV, and
XML text binding. The public representation is `DataBindDecimal { int64_t
mantissa; int32_t scale; }`, where `123.45` is stored as `mantissa=12345` and
`scale=2`. Parsing normalizes trailing fractional zeroes, and
`data_bind_value_as_decimal_string` emits the normalized decimal text. Schema
JSON emit uses strings so decimal precision is not forced through binary
floating point. Binary parsing does not define an implicit decimal wire format.

Schema `bigint` values bind to `DATA_BIND_VALUE_BIGINT` for JSON, CSV, and XML
text binding. The public representation is an owned canonical decimal string,
so values larger than `int64` keep exact precision. JSON string input is the
full-precision path; JSON number input is accepted only for safe integer-sized
values. Use `data_bind_value_get_bigint` or `data_bind_value_as_bigint_string`
to read the borrowed text.

Schema `money` values bind to `DATA_BIND_VALUE_MONEY` for JSON, CSV, and XML
text binding. The public representation is `DataBindMoney { DataBindDecimal
amount; char currency[4]; }`. Text accepts `USD 123.45` and `123.45 USD`;
JSON also accepts `{ "amount": "123.45", "currency": "USD" }`. Use
`data_bind_value_get_money` or `data_bind_value_as_money_string`.

String fields can declare a validation format with field attributes:

```tbe
message Endpoint {
  [format(ipaddr)] string ip;
  [format(url)] string href;
  [format(email)] string owner;
}
```

Formats validate JSON, CSV, XML, and default text during binding and
validation, but the bound value remains `DATA_BIND_VALUE_STRING`. Supported
formats are `ipaddr`, `ip`, `cidr`, `hostname`, `domain`, `email`, `url`,
`uri`, `macaddr`, `mac`, `semver`, `hex`, `base64`, `base64url`, and
`currency`, `json_pointer`, `jsonpath`, `xpath`, `cron`, `color`, `mime`, and
`regex`. Regex format validation uses the bundled tiny-regex implementation.
Unknown formats are ignored so external schema annotations can coexist with
DataBind.

## Schema Reflection

Reflection outputs are caller-owned structs. Initialize them with the provided
macros so the library can honor the struct size across ABI revisions:

```c
DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
if (data_bind_schema_find_type(codec, "Order", &type)) {
  printf("%s has %zu fields\n", type.name, type.field_count);
}

DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
if (data_bind_schema_field_at(codec, "Order", 0, &field)) {
  printf("%s: %s\n", field.name, field.kind);
  if (field.format) printf(" format=%s\n", field.format);
}
```

The TurboUtils parser module delegates `schema.types`, `schema.fields`,
`schema.type_exists`, `schema.enums`, `schema.flags`, and `schema.unions` to
these reflection APIs. Parser-local schema attributes/layout helpers currently
remain outside this ABI because they preserve parser-specific layout summaries.

## MIR Output

`data_bind_generate_mir` writes through a callback instead of exposing `FILE*`
across the ABI:

```c
static int write_cb(const void *data, size_t len, void *user) {
  FILE *out = (FILE *)user;
  return fwrite(data, 1, len, out) == len ? 0 : -1;
}

DataBindError err = DATA_BIND_ERROR_INIT;
DataBindStatus status =
  data_bind_generate_mir("orders.tbe", write_cb, stdout, 0, &err);
```

## Version Checks

`DATA_BIND_VERSION` is the compile-time header version. At runtime:

```c
if (data_bind_abi_version() != DATA_BIND_ABI_VERSION) {
  /* Header/library ABI mismatch. */
}
```

`data_bind_library_version()` returns `major * 10000 + minor * 100 + patch`.
`data_bind_version_string()` returns a diagnostic string.

## MIR Module Caching

DataBind automatically caches JIT-compiled MIR modules by schema hash. Multiple
codecs created from identical schemas share the same compiled parser, reducing
both memory usage and codec creation time.

### Cache Behavior

- **Automatic**: Caching is enabled by default. No code changes required.
- **Hash-based**: Schemas are hashed using FNV-1a. Identical schema text produces
  identical parsers regardless of file path or creation order.
- **Reference counted**: Cached modules remain in memory as long as any codec
  references them. When the last codec using a cached module is freed, the module
  is eligible for cleanup.
- **Thread-local**: Cache state is process-global but not thread-safe. For
  multi-threaded usage, create codecs on a single thread and distribute them, or
  disable caching.

### Cache Control

```c
/* Disable caching for this process */
data_bind_set_cache_enabled(0);

/* Re-enable caching */
data_bind_set_cache_enabled(1);

/* Clear cached modules that are no longer referenced */
data_bind_clear_cache();
```

### Performance Impact

Typical codec creation times (example schema with 10 message types):

| Scenario | Without Cache | With Cache (hit) | Speedup |
|----------|---------------|------------------|---------|
| First creation | ~50ms | ~50ms | 1x |
| Subsequent creations | ~50ms each | ~0.5ms each | 100x |

Cache hit rate depends on schema reuse patterns. Applications that create codecs
repeatedly from the same schema definition (e.g., per-request codec instantiation)
benefit most from caching.

## Value Object Pool

DataBind maintains a small object pool (up to 64 nodes) for `DataBindValue`
structures to reduce allocation overhead during parsing. This is especially
beneficial for deeply nested JSON/CSV/XML documents.

### Pool Behavior

- **Automatic**: Pooling is enabled by default.
- **Size limit**: Pool maintains up to 64 free nodes. Additional freed nodes are
  returned to the system allocator.
- **Thread-safe**: Pool state is process-global. Allocation and release use bounded
  atomic slot operations without a hot-path mutex.
- **Zero-copy reuse**: Pooled nodes are cleared and reused, avoiding repeated
  malloc/free calls.

### Pool Control

```c
/* Disable pooling (returns to malloc/free for all allocations) */
data_bind_set_value_pool_enabled(0);

/* Re-enable pooling */
data_bind_set_value_pool_enabled(1);

/* Check pool statistics */
size_t allocated, reused;
data_bind_get_value_pool_stats(&allocated, &reused);
printf("Pool efficiency: %zu reused / %zu allocated = %.1f%%\n",
       reused, allocated, 100.0 * reused / allocated);
```

### Performance Impact

Pooling avoids repeated node allocation while the fixed process-global pool holds at
most 64 free nodes. A node is acquired with atomic exchange and returned with atomic
compare-exchange; each operation examines at most 64 slots. Disabling atomically closes
the slots at the control boundary, then releases cached nodes. A concurrent bitmap CAS
collision suspends reuse without waiting; calling `data_bind_set_value_pool_enabled(1)`
resumes it.
Use `benchmark_data_bind_pool` for local single-thread and 1/2/4/8-thread measurements
instead of assuming a fixed percentage improvement across workloads.

## Using DataBind with TurboUtils Classes

DataBind returns **Plain Objects** (`EXPRTK_VAL_OBJECT`) rather than **Class
Instances** (`EXPRTK_VAL_INSTANCE`). Plain Objects are pure data structures
without methods, optimized for serialization and data transfer scenarios.

When you need business logic or behavior associated with parsed data, you can
wrap Plain Objects into TurboUtils classes using one of the following patterns.

### Pattern A: Constructor Wrapping

Define a class with a constructor that accepts a Plain Object and copies its
fields:

```javascript
import("data_bind");

// 1. Define schema
var schema = `
message User {
    string name;
    int age;
    string email;
}
`;

// 2. Define class with constructor
class User {
    name = "";
    age = 0;
    email = "";
    
    constructor(plain_obj) {
        this.name = plain_obj.name;
        this.age = plain_obj.age;
        this.email = plain_obj.email;
    }
    
    greet() {
        return "Hello, " + this.name;
    }
    
    is_adult() {
        return this.age >= 18;
    }
    
    send_email(subject, body) {
        print("Sending to " + this.email + ": " + subject);
        // ... actual email logic
    }
}

// 3. Parse and wrap
var codec = data_bind.create(schema);
var json_text = '{"name":"Alice","age":30,"email":"alice@example.com"}';
var plain = data_bind.from_json(codec, json_text);
var user = new User(plain);  // Wrap into class instance

// 4. Use methods
print(user.greet());       // "Hello, Alice"
print(user.is_adult());    // true
user.send_email("Welcome", "Thanks for joining!");
```

**Advantages:**
- Simple and explicit
- Constructor clearly defines the mapping from plain data to class fields
- Works well for small to medium schemas

**Disadvantages:**
- Requires manual field copying for each schema type
- Maintenance burden when schema changes

---

### Pattern B: Factory Function

Use a standalone factory function to encapsulate the wrapping logic:

```javascript
import("data_bind");

class User {
    name = "";
    age = 0;
    email = "";
    
    greet() {
        return "Hello, " + this.name;
    }
    
    is_adult() {
        return this.age >= 18;
    }
}

// Factory function handles parsing + wrapping
function create_user_from_json(codec, json_text) {
    var plain = data_bind.from_json(codec, json_text);
    var user = new User();
    user.name = plain.name;
    user.age = plain.age;
    user.email = plain.email;
    return user;
}

// Usage
var codec = data_bind.create(schema);
var user = create_user_from_json(codec, '{"name":"Bob","age":25,"email":"bob@example.com"}');
print(user.greet());  // "Hello, Bob"
```

**Advantages:**
- Separates parsing logic from class definition
- Easier to unit test the factory independently
- Can add validation or transformation logic in one place

**Disadvantages:**
- One factory function per schema type
- Field mapping still manual

---

### Pattern C: Static Factory Method

Use a static method on the class itself to create instances from Plain Objects:

```javascript
import("data_bind");

class User {
    name = "";
    age = 0;
    email = "";
    
    // Static factory method
    static from_json(codec, json_text) {
        var plain = data_bind.from_json(codec, json_text);
        var user = new User();
        user.name = plain.name;
        user.age = plain.age;
        user.email = plain.email;
        return user;
    }
    
    static from_csv(codec, csv_text) {
        var plain = data_bind.from_csv(codec, csv_text);
        var user = new User();
        user.name = plain.name;
        user.age = plain.age;
        user.email = plain.email;
        return user;
    }
    
    static from_binary(codec, binary_data) {
        var plain = data_bind.from_binary(codec, binary_data);
        var user = new User();
        user.name = plain.name;
        user.age = plain.age;
        user.email = plain.email;
        return user;
    }
    
    greet() {
        return "Hello, " + this.name;
    }
    
    is_adult() {
        return this.age >= 18;
    }
}

// Usage
var codec = data_bind.create(schema);
var user1 = User.from_json(codec, '{"name":"Charlie","age":35,"email":"charlie@example.com"}');
var user2 = User.from_csv(codec, csv_row);
var user3 = User.from_binary(codec, binary_payload);

print(user1.greet());  // "Hello, Charlie"
```

**Advantages:**
- Class encapsulates all construction logic
- Clear API: `User.from_json()`, `User.from_csv()`, etc.
- Follows common OOP patterns (similar to Java/C# factory methods)
- Multiple format support in one class

**Disadvantages:**
- Class becomes coupled to DataBind module
- Still requires manual field mapping

---

### Pattern Comparison

| Pattern | Best For | Pros | Cons |
|---------|----------|------|------|
| **Constructor Wrapping** | Simple schemas, quick prototyping | Simple, explicit | Manual field copy |
| **Factory Function** | Shared parsing logic, testability | Decoupled, testable | Extra function per type |
| **Static Factory** | Clean API, multiple formats | Encapsulated, clear API | Coupled to DataBind |

---

### Batch Processing Example

For processing multiple records (e.g., from API responses or file imports):

```javascript
import("data_bind");

class User {
    static from_json(codec, json_text) {
        var plain = data_bind.from_json(codec, json_text);
        var user = new User();
        user.name = plain.name;
        user.age = plain.age;
        user.email = plain.email;
        return user;
    }
    
    greet() { return "Hello, " + this.name; }
    is_adult() { return this.age >= 18; }
}

// Batch import
function import_users(codec, json_array_text) {
    var plain_list = data_bind.from_json_all(codec, json_array_text);
    var users = [];
    
    for (var i = 0; i < plain_list.length; i++) {
        var plain = plain_list[i];
        var user = new User();
        user.name = plain.name;
        user.age = plain.age;
        user.email = plain.email;
        users.push(user);
    }
    
    return users;
}

// Usage
var codec = data_bind.create(schema);
var api_response = '[{"name":"Alice","age":30,"email":"alice@example.com"}, {"name":"Bob","age":25,"email":"bob@example.com"}]';
var users = import_users(codec, api_response);

users.forEach((user) => {
    if (user.is_adult()) {
        print(user.greet());
    }
});
```

---

### Why Plain Objects?

DataBind returns Plain Objects by design for the following reasons:

1. **Performance**: Plain Objects avoid method lookup overhead for pure data
   access patterns.
2. **Universality**: Consistent with serialization libraries in other languages
   (Protobuf, Jackson, MessagePack, `json.loads()`).
3. **Flexibility**: Callers can choose whether to wrap data into classes or
   process it as plain data.
4. **Memory efficiency**: No method dispatch tables or vtables for data-only
   structures.

For data-heavy applications (financial analytics, log processing, ETL pipelines),
Plain Objects provide optimal throughput. For business-logic-heavy applications
(user management, workflow systems), wrapping into classes adds behavior where
needed.
