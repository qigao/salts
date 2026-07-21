/**
 * @file data_bind.h
 * @brief Public C API for schema-driven binary/JSON/CSV/XML data binding.
 *
 * Third-party users should include this header and link TurboUtils::DataBind.
 * The API exposes only opaque handles and accessor functions; returned strings
 * and child pointers are borrowed views owned by their DataBind/DataBindValue.
 *
 * SECURITY: Schema files are assumed to come from trusted sources. DataBind uses
 * JIT compilation to generate binary parsers and includes validation limits to
 * prevent common attacks (max nesting depth: 32, max field offset: 1GB, max total
 * fields: 10,000, circular reference detection). Do not create codecs from
 * user-supplied or untrusted schema definitions.
 */

#ifndef DATA_BIND_H
#define DATA_BIND_H

#include "turbo_parser.h"
#include "turbo_uuid.h"
#include <stddef.h>
#include <stdint.h>

#define DATA_BIND_UUID_SIZE TURBO_UUID_SIZE

#define DATA_BIND_VERSION_MAJOR 1
#define DATA_BIND_VERSION_MINOR 11
#define DATA_BIND_VERSION_PATCH 0
#define DATA_BIND_VERSION                                                                          \
  (DATA_BIND_VERSION_MAJOR * 10000 + DATA_BIND_VERSION_MINOR * 100 + DATA_BIND_VERSION_PATCH)

/* Increment when the public C ABI changes incompatibly. */
#define DATA_BIND_ABI_VERSION 7

#ifdef __cplusplus
extern "C" {
#endif

/* DLL export/import macros */
#ifdef _WIN32
  #ifdef DATA_BIND_BUILD_DLL
    #define DATA_BIND_API __declspec(dllexport)
  #elif defined(DATA_BIND_USE_DLL)
    #define DATA_BIND_API __declspec(dllimport)
  #else
    #define DATA_BIND_API
  #endif
#else
  #define DATA_BIND_API
#endif

typedef struct DataBind DataBind;
typedef struct DataBindValue DataBindValue;
typedef struct DataBindObject DataBindObject;
typedef struct DataBindObject DataBindRecord;

typedef enum DataBindStatus {
  DATA_BIND_OK = 0,
  DATA_BIND_ERR_INVALID_ARG,
  DATA_BIND_ERR_IO,
  DATA_BIND_ERR_PARSE,
  DATA_BIND_ERR_SCHEMA,
  DATA_BIND_ERR_TYPE_NOT_FOUND,
  DATA_BIND_ERR_TYPE_MISMATCH,
  DATA_BIND_ERR_OOM,
  DATA_BIND_ERR_RUNTIME
} DataBindStatus;

typedef struct DataBindError {
  size_t size;
  DataBindStatus code;
  int line;
  int column;
  char path[260];
  char message[512];
} DataBindError;

typedef int (*DataBindWriteFn)(const void *data, size_t len, void *user);

typedef enum DataBindValueKind {
  DATA_BIND_VALUE_NULL = 0,
  DATA_BIND_VALUE_OBJECT,
  DATA_BIND_VALUE_LIST,
  DATA_BIND_VALUE_SET,
  DATA_BIND_VALUE_MAP,
  DATA_BIND_VALUE_INT,
  DATA_BIND_VALUE_INT64,
  DATA_BIND_VALUE_DOUBLE,
  DATA_BIND_VALUE_BOOL,
  DATA_BIND_VALUE_STRING,
  DATA_BIND_VALUE_BYTES,
  DATA_BIND_VALUE_UUID,
  DATA_BIND_VALUE_DATETIME,
  DATA_BIND_VALUE_DATE,
  DATA_BIND_VALUE_TIME,
  DATA_BIND_VALUE_DURATION,
  DATA_BIND_VALUE_DECIMAL,
  DATA_BIND_VALUE_BIGINT,
  DATA_BIND_VALUE_MONEY,
  DATA_BIND_VALUE_UINT64
} DataBindValueKind;

typedef struct DataBindDate {
  int year;
  int month;
  int day;
} DataBindDate;

typedef struct DataBindTime {
  int hour;
  int minute;
  int second;
  int millisecond;
} DataBindTime;

typedef struct DataBindDecimal {
  int64_t mantissa;
  int32_t scale;
} DataBindDecimal;

typedef struct DataBindMoney {
  DataBindDecimal amount;
  char currency[4];
} DataBindMoney;

typedef struct DataBindMapEntry {
  const char *key;
  const DataBindValue *value;
} DataBindMapEntry;

/** Borrowed handle to one immutable record field. */
typedef struct DataBindRecordField {
  size_t size;
  const DataBindValue *owner;
  const DataBindValue *value;
  const char *name;
  size_t index;
} DataBindRecordField;

/** Borrowed immutable object view. */
typedef struct DataBindRecordView {
  size_t size;
  const DataBindValue *value;
} DataBindRecordView;

/** Borrowed immutable list or set view. */
typedef struct DataBindListView {
  size_t size;
  const DataBindValue *value;
} DataBindListView;

/** Borrowed immutable map view. */
typedef struct DataBindRecordMapView {
  size_t size;
  const DataBindValue *value;
} DataBindRecordMapView;

/** Borrowed byte-counted UTF-8 string view. */
typedef struct DataBindStringView {
  size_t size;
  const char *data;
  size_t length;
} DataBindStringView;

/** Borrowed byte view. */
typedef struct DataBindBytesView {
  size_t size;
  const uint8_t *data;
  size_t length;
} DataBindBytesView;

#define DATA_BIND_RECORD_FIELD_INIT {sizeof(DataBindRecordField), NULL, NULL, NULL, 0}
#define DATA_BIND_RECORD_VIEW_INIT {sizeof(DataBindRecordView), NULL}
#define DATA_BIND_LIST_VIEW_INIT {sizeof(DataBindListView), NULL}
#define DATA_BIND_RECORD_MAP_VIEW_INIT {sizeof(DataBindRecordMapView), NULL}
#define DATA_BIND_STRING_VIEW_INIT {sizeof(DataBindStringView), NULL, 0}
#define DATA_BIND_BYTES_VIEW_INIT {sizeof(DataBindBytesView), NULL, 0}

typedef struct data_bind_stream_t data_bind_stream_t;

typedef enum DataBindRecordAction {
  DATA_BIND_RECORD_CONTINUE = 0,
  DATA_BIND_RECORD_STOP = 1,
  DATA_BIND_RECORD_ERROR = -1
} DataBindRecordAction;

/**
 * @brief Receives one path-selected, schema-bound record synchronously.
 *
 * The record is borrowed and remains valid only for the callback duration.
 * CONTINUE requests later records. STOP disables later callback delivery while
 * parsing and final-result construction continue. ERROR fails the stream.
 */
typedef DataBindRecordAction (*DataBindRecordFn)(void *user_data, const DataBindValue *record,
                                                 uint64_t record_index);

typedef enum DataBindSchemaKind {
  DATA_BIND_SCHEMA_UNKNOWN = 0,
  DATA_BIND_SCHEMA_MESSAGE,
  DATA_BIND_SCHEMA_COMPOSITE,
  DATA_BIND_SCHEMA_GROUP,
  DATA_BIND_SCHEMA_ENUM,
  DATA_BIND_SCHEMA_FLAGS,
  DATA_BIND_SCHEMA_UNION,
  DATA_BIND_SCHEMA_SCALAR
} DataBindSchemaKind;

typedef struct DataBindSchemaType {
  size_t size;
  const char *name;
  DataBindSchemaKind kind;
  const char *underlying_type;
  size_t field_count;
  size_t item_count;
  size_t fixed_block_size;
  int has_fixed_block_size;
} DataBindSchemaType;

typedef struct DataBindSchemaField {
  size_t size;
  const char *name;
  const char *type;
  const char *kind;
  const char *inner_type;
  const char *group_type;
  const char *key_type;
  const char *value_type;
  const char *collection_kind;
  const char *length;
  int is_optional;
  int has_default;
  const char *default_value;
  int is_collection;
  int is_composite;
  int is_group;
  int is_map;
  int is_enum;
  int is_variable_size;
  int is_fixed_size;
  size_t offset;
  int has_offset;
  size_t size_bytes;
  int has_size_bytes;
  size_t field_size_bytes;
  int has_field_size_bytes;
  const char *format;
} DataBindSchemaField;

typedef struct DataBindSchemaEnumItem {
  size_t size;
  const char *name;
  const char *value;
} DataBindSchemaEnumItem;

typedef struct DataBindSchemaAttribute {
  size_t size;
  const char *name;
  const char *value;
} DataBindSchemaAttribute;

#define DATA_BIND_SCHEMA_TYPE_INIT {sizeof(DataBindSchemaType)}
#define DATA_BIND_SCHEMA_FIELD_INIT {sizeof(DataBindSchemaField)}
#define DATA_BIND_SCHEMA_ENUM_ITEM_INIT {sizeof(DataBindSchemaEnumItem)}
#define DATA_BIND_SCHEMA_ATTRIBUTE_INIT {sizeof(DataBindSchemaAttribute)}
#define DATA_BIND_ERROR_INIT {sizeof(DataBindError), DATA_BIND_OK, -1, -1, {0}, {0}}

/**
 * @brief Return the linked library version encoded as major * 10000 + minor * 100 + patch.
 */
DATA_BIND_API int data_bind_library_version(void);

/**
 * @brief Return the linked library ABI version.
 */
DATA_BIND_API int data_bind_abi_version(void);

/**
 * @brief Return a stable version string for diagnostics.
 */
DATA_BIND_API const char *data_bind_version_string(void);

/**
 * @brief Create dynamic codec from schema file.
 * @param schema_path Path to .schema file (must be from a trusted source)
 * @param out_codec Output parameter for the created codec
 * @param error Output parameter for error information
 * @return Status code. On success, *out_codec owns the codec and must be freed with
 * data_bind_free().
 *
 * SECURITY: This function uses JIT compilation to generate optimized parsers.
 * Only load schemas from trusted sources. The function validates schema structure
 * (nesting depth, field offsets, circular references) but does not provide
 * comprehensive defense against all malicious constructions.
 */
DATA_BIND_API DataBindStatus data_bind_create(const char *schema_path, DataBind **out_codec,
                                              DataBindError *error);

/**
 * @brief Create dynamic codec from schema text in memory.
 * @param schema_text Schema definition text (must be from a trusted source)
 * @param len Length of schema text
 * @param out_codec Output parameter for the created codec
 * @param error Output parameter for error information
 * @return Status code. On success, *out_codec owns the codec and must be freed with
 * data_bind_free().
 *
 * SECURITY: This function uses JIT compilation. Only process schema text from
 * trusted sources. See data_bind_create() for security considerations.
 */
DATA_BIND_API DataBindStatus data_bind_create_from_text(const char *schema_text, size_t len,
                                                        DataBind **out_codec, DataBindError *error);

/**
 * @brief Free codec
 */
DATA_BIND_API void data_bind_free(DataBind *codec);

/**
 * @brief Enable or disable MIR module caching.
 * @param enabled Non-zero to enable caching, zero to disable
 *
 * When enabled (default), data_bind_create() caches compiled MIR modules by
 * schema hash. Multiple codecs with identical schemas share the same JIT-compiled
 * parser, reducing memory usage and codec creation time.
 *
 * Caching is thread-local and does not synchronize across threads. For multi-threaded
 * usage, create codecs on a single thread and distribute them, or disable caching.
 */
DATA_BIND_API void data_bind_set_cache_enabled(int enabled);

/**
 * @brief Clear all cached MIR modules.
 *
 * Releases cached modules that are no longer referenced by any codec. This does not
 * affect existing codecs, only prevents future codecs from reusing cached modules.
 */
DATA_BIND_API void data_bind_clear_cache(void);

/**
 * @brief Enable or disable the value object pool.
 * @param enabled Non-zero to enable pooling, zero to disable
 *
 * When enabled (default), DataBindValue nodes are pooled and reused to reduce
 * allocation overhead. The pool maintains up to 64 free nodes. Disabling the pool
 * causes all allocations to use malloc/free directly.
 *
 * This setting is process-global and synchronized across threads. The allocation and
 * release hot paths use bounded atomic slot operations without a mutex. Disabling closes
 * the slots atomically, then releases all cached nodes. A concurrent bitmap collision
 * suspends reuse without waiting; calling this function with non-zero resumes it.
 */
DATA_BIND_API void data_bind_set_value_pool_enabled(int enabled);

/**
 * @brief Get value pool statistics.
 * @param allocated Total DataBindValue nodes allocated (output)
 * @param reused Number of times pool nodes were reused (output)
 *
 * Use this to monitor pool effectiveness. High reuse rates indicate good cache locality.
 */
DATA_BIND_API void data_bind_get_value_pool_stats(size_t *allocated, size_t *reused);

/**
 * @brief Parse binary data to a dynamic value tree.
 * @param codec Codec instance
 * @param type_name Message type name (e.g. "Order")
 * @param buf Binary data
 * @param len Data length
 * @return Status code. On success, *out_value owns the value and must be released with
 * data_bind_value_free().
 */
DATA_BIND_API DataBindStatus data_bind_parse(DataBind *codec, const char *type_name,
                                             const uint8_t *buf, size_t len,
                                             DataBindValue **out_value, DataBindError *error);

/**
 * @brief Create stream handles for JSON inputs.
 *
 * The constructor name defines whether the whole document, every root-array
 * item, the first JSONPath match, or all JSONPath matches are bound. Incremental
 * root-array binding preserves exact signed and unsigned 64-bit number tokens.
 */
DATA_BIND_API data_bind_stream_t *data_bind_stream_json_create(DataBind *codec,
                                                               const char *type_name,
                                                               DataBindValue **out_value,
                                                               DataBindError *error);
DATA_BIND_API data_bind_stream_t *data_bind_stream_json_all_create(DataBind *codec,
                                                                   const char *type_name,
                                                                   DataBindValue **out_value,
                                                                   DataBindError *error);
DATA_BIND_API data_bind_stream_t *
data_bind_stream_json_path_create(DataBind *codec, const char *type_name, const char *json_path,
                                  DataBindValue **out_value, DataBindError *error);
DATA_BIND_API data_bind_stream_t *
data_bind_stream_json_path_all_create(DataBind *codec, const char *type_name, const char *json_path,
                                      DataBindValue **out_value, DataBindError *error);

/**
 * @brief Create SAX-validated YAML streams for root, root sequence, or YPATH binding.
 *
 * YAML syntax is validated incrementally as chunks are fed. Schema and YPATH
 * binding still run at finish(); record callbacks are emitted after successful
 * schema binding.
 */
DATA_BIND_API data_bind_stream_t *data_bind_stream_yaml_create(DataBind *codec,
                                                               const char *type_name,
                                                               DataBindValue **out_value,
                                                               DataBindError *error);
DATA_BIND_API data_bind_stream_t *data_bind_stream_yaml_all_create(DataBind *codec,
                                                                   const char *type_name,
                                                                   DataBindValue **out_value,
                                                                   DataBindError *error);
DATA_BIND_API data_bind_stream_t *
data_bind_stream_yaml_path_create(DataBind *codec, const char *type_name, const char *yaml_path,
                                  DataBindValue **out_value, DataBindError *error);
DATA_BIND_API data_bind_stream_t *
data_bind_stream_yaml_path_all_create(DataBind *codec, const char *type_name, const char *yaml_path,
                                      DataBindValue **out_value, DataBindError *error);

/** @brief Create a CSV stream that binds all rows, optionally selected by CSVPath. */
DATA_BIND_API data_bind_stream_t *data_bind_stream_csv_all_create(DataBind *codec,
                                                                  const char *type_name,
                                                                  DataBindValue **out_value,
                                                                  DataBindError *error);
DATA_BIND_API data_bind_stream_t *
data_bind_stream_csv_path_create(DataBind *codec, const char *type_name, const char *csv_path,
                                 DataBindValue **out_value, DataBindError *error);

/** @brief Create an XML root stream or an all-matches XMLPath stream. */
DATA_BIND_API data_bind_stream_t *data_bind_stream_xml_create(DataBind *codec,
                                                              const char *type_name,
                                                              DataBindValue **out_value,
                                                              DataBindError *error);
DATA_BIND_API data_bind_stream_t *
data_bind_stream_xml_path_all_create(DataBind *codec, const char *type_name, const char *xml_path,
                                     DataBindValue **out_value, DataBindError *error);

/**
 * @brief Install a synchronous schema-bound record callback before first feed.
 *
 * Incrementally streamable inputs deliver records from feed(). Other path
 * expressions deliver their bound records from finish(). Existing final-value
 * output remains enabled. The stream and callback execute on the caller thread.
 */
DATA_BIND_API DataBindStatus data_bind_stream_set_record_callback(data_bind_stream_t *stream,
                                                                  DataBindRecordFn callback,
                                                                  void *user_data);

/**
 * @brief Feed one chunk into a stream.
 */
DATA_BIND_API int data_bind_stream_feed(data_bind_stream_t *stream, const void *data, size_t len);

/**
 * @brief Read a file in fixed-size chunks and feed it into a stream.
 */
DATA_BIND_API int data_bind_stream_feed_file(data_bind_stream_t *stream, const char *file_path);

/**
 * @brief Finish streaming parse. The final value is stored in the constructor's out_value.
 */
DATA_BIND_API int data_bind_stream_finish(data_bind_stream_t *stream);

/**
 * @brief Destroy a stream and release internal buffers.
 */
DATA_BIND_API void data_bind_stream_destroy(data_bind_stream_t *stream);

/**
 * @brief Bind JSON text to a dynamic value tree using the codec schema.
 * @param codec Codec instance
 * @param type_name Schema type name
 * @param json JSON document text
 * @param len JSON text length
 * @return Status code.
 */
DATA_BIND_API DataBindStatus data_bind_parse_json(DataBind *codec, const char *type_name,
                                                  const char *json, size_t len,
                                                  DataBindValue **out_value, DataBindError *error);

/**
 * @brief Bind each item in JSON text to a dynamic list using the codec schema.
 *
 * If the input JSON is an array, each item is bound independently. Otherwise
 * the single document is bound and returned as a one-item list.
 */
DATA_BIND_API DataBindStatus data_bind_parse_json_all(DataBind *codec, const char *type_name,
                                                      const char *json, size_t len,
                                                      DataBindValue **out_value,
                                                      DataBindError *error);

/**
 * @brief Bind the first JSONPath-selected JSON value using the codec schema.
 *
 * If jsonpath is NULL or empty, this is equivalent to data_bind_parse_json().
 */
DATA_BIND_API DataBindStatus data_bind_parse_json_path(DataBind *codec, const char *type_name,
                                                       const char *json, size_t len,
                                                       const char *jsonpath,
                                                       DataBindValue **out_value,
                                                       DataBindError *error);

/**
 * @brief Bind all JSONPath-selected JSON values to a dynamic list.
 *
 * If jsonpath is NULL or empty, this is equivalent to data_bind_parse_json_all().
 * Returned list items are schema-bound copies; JSONPath matches are non-owning
 * views into the parsed JSON document and are not exposed.
 */
DATA_BIND_API DataBindStatus data_bind_parse_json_path_all(DataBind *codec, const char *type_name,
                                                           const char *json, size_t len,
                                                           const char *jsonpath,
                                                           DataBindValue **out_value,
                                                           DataBindError *error);

/** @brief Bind YAML root or YPATH-selected nodes through the JSON-compatible schema binder. */
DATA_BIND_API DataBindStatus data_bind_parse_yaml(DataBind *codec, const char *type_name,
                                                  const char *yaml, size_t len,
                                                  DataBindValue **out_value, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_parse_yaml_all(DataBind *codec, const char *type_name,
                                                      const char *yaml, size_t len,
                                                      DataBindValue **out_value,
                                                      DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_parse_yaml_path(DataBind *codec, const char *type_name,
                                                       const char *yaml, size_t len,
                                                       const char *yamlpath,
                                                       DataBindValue **out_value,
                                                       DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_parse_yaml_path_all(DataBind *codec, const char *type_name,
                                                           const char *yaml, size_t len,
                                                           const char *yamlpath,
                                                           DataBindValue **out_value,
                                                           DataBindError *error);

/**
 * @brief Bind a CSV row to a dynamic value tree using the codec schema.
 *
 * CSV is parsed with a header row. Nested fields use dotted/indexed column
 * names such as header.seq, bids[0].price, attrs.x.
 */
DATA_BIND_API DataBindStatus data_bind_parse_csv(DataBind *codec, const char *type_name,
                                                 const char *csv, size_t len, size_t row,
                                                 DataBindValue **out_value, DataBindError *error);

/**
 * @brief Bind all CSV rows to a dynamic list using the codec schema.
 */
DATA_BIND_API DataBindStatus data_bind_parse_csv_all(DataBind *codec, const char *type_name,
                                                     const char *csv, size_t len,
                                                     DataBindValue **out_value,
                                                     DataBindError *error);

/**
 * @brief Bind CSV rows matched by a CSVPath expression to a dynamic list.
 *
 * The CSV input still uses a header row for schema binding. The CSVPath expression
 * uses typed headers such as price_n and side_s expose
 * expression names price and side while still binding to schema fields price and
 * side.
 */
DATA_BIND_API DataBindStatus data_bind_parse_csv_path(DataBind *codec, const char *type_name,
                                                      const char *csv, size_t len,
                                                      const char *csvpath,
                                                      DataBindValue **out_value,
                                                      DataBindError *error);

/**
 * @brief Bind XML text to a dynamic value tree using the codec schema.
 *
 * The XML binder uses XMLPath (XPath 1.0) over the parsed document. Record fields bind
 * from same-name child elements first and same-name attributes second.
 * Collections and groups bind from repeated same-name elements.
 */
DATA_BIND_API DataBindStatus data_bind_parse_xml(DataBind *codec, const char *type_name,
                                                 const char *xml, size_t len,
                                                 DataBindValue **out_value, DataBindError *error);

/**
 * @brief Bind all XMLPath-selected XML nodes to a dynamic list.
 *
 * If xmlpath is NULL or empty, the root document element is bound as a one-item
 * list. The matched nodes are non-owning views into the parsed XML document.
 */
DATA_BIND_API DataBindStatus data_bind_parse_xml_path_all(DataBind *codec, const char *type_name,
                                                          const char *xml, size_t len,
                                                          const char *xmlpath,
                                                          DataBindValue **out_value,
                                                          DataBindError *error);

/**
 * @brief Strictly validate JSON text against a schema type.
 *
 * JSON arrays are valid only when every item binds to the requested type.
 * Non-array input is validated as a single value.
 */
DATA_BIND_API DataBindStatus data_bind_validate_json(DataBind *codec, const char *type_name,
                                                     const char *json, size_t len,
                                                     DataBindError *error);

/**
 * @brief Strictly validate the first JSONPath-selected JSON value against a schema type.
 *
 * If jsonpath is NULL or empty, this is equivalent to data_bind_validate_json().
 */
DATA_BIND_API DataBindStatus data_bind_validate_json_path(DataBind *codec, const char *type_name,
                                                          const char *json, size_t len,
                                                          const char *jsonpath,
                                                          DataBindError *error);

/** @brief Strictly validate a YAML root or first YPATH match against a schema type. */
DATA_BIND_API DataBindStatus data_bind_validate_yaml(DataBind *codec, const char *type_name,
                                                     const char *yaml, size_t len,
                                                     DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_validate_yaml_path(DataBind *codec, const char *type_name,
                                                          const char *yaml, size_t len,
                                                          const char *yamlpath,
                                                          DataBindError *error);

/**
 * @brief Strictly validate every CSV data row against a schema type.
 *
 * CSV is parsed with a header row. Empty inputs with a valid header and no data
 * rows are valid.
 */
DATA_BIND_API DataBindStatus data_bind_validate_csv(DataBind *codec, const char *type_name,
                                                    const char *csv, size_t len,
                                                    DataBindError *error);

/**
 * @brief Strictly validate CSV rows matched by a CSVPath expression.
 */
DATA_BIND_API DataBindStatus data_bind_validate_csv_path(DataBind *codec, const char *type_name,
                                                         const char *csv, size_t len,
                                                         const char *csvpath, DataBindError *error);

/**
 * @brief Strictly validate XMLPath-selected XML text against a schema type.
 *
 * If xmlpath is NULL or empty, the document root is validated. Otherwise every
 * XMLPath-selected node must bind to the requested type.
 */
DATA_BIND_API DataBindStatus data_bind_validate_xml_path(DataBind *codec, const char *type_name,
                                                         const char *xml, size_t len,
                                                         const char *xmlpath, DataBindError *error);

/**
 * @brief Free a dynamic value tree returned by data_bind_parse().
 *
 * It is valid to pass NULL. All pointers returned from data_bind_value_* accessors
 * become invalid after this call.
 */
DATA_BIND_API void data_bind_value_free(DataBindValue *value);

/**
 * @brief Deep-copy a dynamic value tree.
 *
 * On success, *out_value owns an independent tree and must be released with
 * data_bind_value_free(). The source remains owned by the caller. On failure,
 * *out_value is NULL and no partial tree is returned.
 */
DATA_BIND_API DataBindStatus data_bind_value_clone(const DataBindValue *value,
                                                   DataBindValue **out_value);

/**
 * @brief Parse and own one schema-bound binary object.
 *
 * The returned handle owns its type name and value tree and remains valid after
 * the codec is freed. Release it with data_bind_object_free().
 */
DATA_BIND_API DataBindStatus data_bind_object_from_bin(DataBind *codec, const char *type_name,
                                                       const uint8_t *data, size_t len,
                                                       DataBindObject **out_object,
                                                       DataBindError *error);

/**
 * @brief Parse and own one schema-bound JSON object.
 *
 * The returned handle owns its type name and value tree and remains valid after
 * the codec is freed. Release it with data_bind_object_free().
 */
DATA_BIND_API DataBindStatus data_bind_object_from_json(DataBind *codec, const char *type_name,
                                                        const char *json, size_t len,
                                                        DataBindObject **out_object,
                                                        DataBindError *error);

/** Parse and own one schema-bound YAML object. */
DATA_BIND_API DataBindStatus data_bind_object_from_yaml(DataBind *codec, const char *type_name,
                                                        const char *yaml, size_t len,
                                                        DataBindObject **out_object,
                                                        DataBindError *error);

/** Parse and own one schema-bound XML object. */
DATA_BIND_API DataBindStatus data_bind_object_from_xml(DataBind *codec, const char *type_name,
                                                       const char *xml, size_t len,
                                                       DataBindObject **out_object,
                                                       DataBindError *error);

/** Parse and own one schema-bound CSV row. */
DATA_BIND_API DataBindStatus data_bind_object_from_csv(DataBind *codec, const char *type_name,
                                                       const char *csv, size_t len, size_t row,
                                                       DataBindObject **out_object,
                                                       DataBindError *error);

/** Deep clone the type name and value tree into an independently owned object. */
DATA_BIND_API DataBindStatus data_bind_object_clone(const DataBindObject *object,
                                                     DataBindObject **out_object);

/** Return the copied schema type name owned by the handle. */
DATA_BIND_API const char *data_bind_object_type_name(const DataBindObject *object);

/** Return the borrowed value tree. It remains valid until data_bind_object_free(). */
DATA_BIND_API const DataBindValue *data_bind_object_value(const DataBindObject *object);

/**
 * @brief Serialize an owned object to its schema binary wire representation.
 *
 * The codec supplies the schema intentionally not retained by DataBindObject.
 * Its schema must contain a message with the object's type name. The returned
 * buffer is owned by the caller and must be released with
 * data_bind_binary_free(). The dynamic binary codec accepts the same
 * little-endian field layouts as data_bind_parse(); schemas with optional
 * presence bitmaps, unions, or text-only extended scalars fail with
 * DATA_BIND_ERR_SCHEMA instead of emitting a partial representation.
 */
DATA_BIND_API DataBindStatus data_bind_object_serialize_bin(DataBind *codec,
                                                            const DataBindObject *object,
                                                            uint8_t **out_bin, size_t *out_len,
                                                            DataBindError *error);

/**
 * @brief Serialize an owned object into a caller-provided binary buffer.
 *
 * On success, @p out_len receives the bytes written. If @p capacity is too
 * small, no bytes are written and @p out_len receives the required size.
 */
DATA_BIND_API DataBindStatus data_bind_object_serialize_bin_into(
    DataBind *codec, const DataBindObject *object, uint8_t *output, size_t capacity,
    size_t *out_len, DataBindError *error);

/**
 * @brief Serialize the owned value tree as compact UTF-8 JSON.
 *
 * Extended scalars use the same representation accepted by the JSON binder:
 * UUID/temporal/decimal/bigint/bytes are strings and money is an object. Bytes
 * must contain valid UTF-8. On success, *out_json must be released with
 * data_bind_serialized_free().
 */
DATA_BIND_API DataBindStatus data_bind_object_serialize_json(const DataBindObject *object,
                                                             char **out_json, size_t *out_len,
                                                             DataBindError *error);
/**
 * Serialize as YAML using the JSON-compatible DataBind scalar representation.
 * On success, release *out_yaml with data_bind_serialized_free().
 */
DATA_BIND_API DataBindStatus data_bind_object_serialize_yaml(const DataBindObject *object,
                                                             char **out_yaml, size_t *out_len,
                                                             DataBindError *error);
/**
 * Serialize using the existing DataBind XML shape: object fields are child
 * elements, collections repeat their field element, and map keys are element
 * names. Values without an XML representation fail with a type mismatch. On
 * success, release *out_xml with data_bind_serialized_free().
 */
DATA_BIND_API DataBindStatus data_bind_object_serialize_xml(const DataBindObject *object,
                                                            char **out_xml, size_t *out_len,
                                                            DataBindError *error);
/**
 * Serialize one object as an RFC 4180 CSV header and data row. Nested object,
 * collection, and map values use the same dotted/indexed paths accepted by the
 * CSV binder (for example, header.seq, values[0], and attrs.key). Scalar text
 * uses the canonical DataBind representation. String and bytes cells must be
 * valid UTF-8, and bytes cannot contain NUL. Empty containers and map keys
 * containing '.' or '[' cannot be represented losslessly and fail with
 * DATA_BIND_ERR_TYPE_MISMATCH. On success, release *out_csv with
 * data_bind_serialized_free().
 */
DATA_BIND_API DataBindStatus data_bind_object_serialize_csv(const DataBindObject *object,
                                                            char **out_csv, size_t *out_len,
                                                            DataBindError *error);

DATA_BIND_API DataBindStatus data_bind_object_write_json(const DataBindObject *object,
                                                         DataBindWriteFn write, void *user,
                                                         DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_object_write_yaml(const DataBindObject *object,
                                                         DataBindWriteFn write, void *user,
                                                         DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_object_write_xml(const DataBindObject *object,
                                                        DataBindWriteFn write, void *user,
                                                        DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_object_write_csv(const DataBindObject *object,
                                                        DataBindWriteFn write, void *user,
                                                        DataBindError *error);

/**
 * @brief Immutable schema-bound record API.
 *
 * DataBindRecord is the record-oriented name for the existing opaque owning
 * DataBindObject. JSON/YAML/XML/CSV constructors use their native binders. The
 * binary constructor uses the internal parse_record_v1_* MIR ABI, whose typed
 * callbacks address prebuilt fields by numeric slot rather than by field-name
 * strings. A successfully constructed record is immutable and does not borrow
 * its schema layout from the codec. All field,
 * string, bytes, object, list, and map views borrow from it and become invalid
 * when data_bind_record_free() is called. Freeing a record must not race any
 * getter using the record or one of its borrowed views.
 *
 * Initialize every field/view output with its DATA_BIND_*_INIT macro. Name
 * getters return DATA_BIND_ERR_TYPE_MISMATCH for a missing field, wrong field
 * type, or out-of-range numeric conversion. Invalid arguments, uninitialized
 * outputs, and collection indexes outside the view return
 * DATA_BIND_ERR_INVALID_ARG. Failed getters leave caller value outputs
 * unchanged.
 *
 * Example:
 * @code
 * DataBindStatus read_order_id(DataBind *codec, const char *json, size_t json_len,
 *                              uint32_t *out, DataBindError *error) {
 *   DataBindRecord *record = NULL;
 *   DataBindStatus status =
 *       data_bind_record_from_json(codec, "Order", json, json_len, &record, error);
 *   if (status == DATA_BIND_OK)
 *     status = data_bind_record_get_u32(record, "id", out, error);
 *   data_bind_record_free(record);
 *   return status;
 * }
 * @endcode
 */
DATA_BIND_API DataBindStatus data_bind_record_from_bin(DataBind *codec, const char *type_name,
                                                       const uint8_t *data, size_t len,
                                                       DataBindRecord **out_record,
                                                       DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_from_json(DataBind *codec, const char *type_name,
                                                        const char *json, size_t len,
                                                        DataBindRecord **out_record,
                                                        DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_from_yaml(DataBind *codec, const char *type_name,
                                                        const char *yaml, size_t len,
                                                        DataBindRecord **out_record,
                                                        DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_from_xml(DataBind *codec, const char *type_name,
                                                       const char *xml, size_t len,
                                                       DataBindRecord **out_record,
                                                       DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_from_csv(DataBind *codec, const char *type_name,
                                                       const char *csv, size_t len, size_t row,
                                                       DataBindRecord **out_record,
                                                       DataBindError *error);

DATA_BIND_API const char *data_bind_record_type_name(const DataBindRecord *record);
DATA_BIND_API void data_bind_record_free(DataBindRecord *record);

DATA_BIND_API DataBindStatus data_bind_record_serialize_bin(
    DataBind *codec, const DataBindRecord *record, uint8_t **out_bin, size_t *out_len,
    DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_serialize_bin_into(
    DataBind *codec, const DataBindRecord *record, uint8_t *output, size_t capacity,
    size_t *out_len, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_serialize_json(
    const DataBindRecord *record, char **out_json, size_t *out_len, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_serialize_yaml(
    const DataBindRecord *record, char **out_yaml, size_t *out_len, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_serialize_xml(
    const DataBindRecord *record, char **out_xml, size_t *out_len, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_serialize_csv(
    const DataBindRecord *record, char **out_csv, size_t *out_len, DataBindError *error);

/** Resolve a direct field name once. The returned handle borrows from the record. */
DATA_BIND_API DataBindStatus data_bind_record_find_field(
    const DataBindRecord *record, const char *name, DataBindRecordField *out_field,
    DataBindError *error);

/** Resolve a direct field name in a borrowed nested object view. */
DATA_BIND_API DataBindStatus data_bind_record_view_find_field(
    const DataBindRecordView *view, const char *name, DataBindRecordField *out_field,
    DataBindError *error);

/** Return the borrowed dynamic value for extended scalar inspection. */
DATA_BIND_API const DataBindValue *
data_bind_record_field_value(const DataBindRecordField *field);

DATA_BIND_API DataBindStatus data_bind_record_field_get_i32(
    const DataBindRecordField *field, int32_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_u32(
    const DataBindRecordField *field, uint32_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_i64(
    const DataBindRecordField *field, int64_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_u64(
    const DataBindRecordField *field, uint64_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_double(
    const DataBindRecordField *field, double *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_bool(
    const DataBindRecordField *field, int *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_uuid(
    const DataBindRecordField *field, uint8_t out[DATA_BIND_UUID_SIZE], DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_string(
    const DataBindRecordField *field, DataBindStringView *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_bytes(
    const DataBindRecordField *field, DataBindBytesView *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_object(
    const DataBindRecordField *field, DataBindRecordView *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_list(
    const DataBindRecordField *field, DataBindListView *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_field_get_map(
    const DataBindRecordField *field, DataBindRecordMapView *out, DataBindError *error);

DATA_BIND_API DataBindStatus data_bind_record_get_i32(
    const DataBindRecord *record, const char *name, int32_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_u32(
    const DataBindRecord *record, const char *name, uint32_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_i64(
    const DataBindRecord *record, const char *name, int64_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_u64(
    const DataBindRecord *record, const char *name, uint64_t *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_double(
    const DataBindRecord *record, const char *name, double *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_bool(
    const DataBindRecord *record, const char *name, int *out, DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_uuid(
    const DataBindRecord *record, const char *name, uint8_t out[DATA_BIND_UUID_SIZE],
    DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_string(
    const DataBindRecord *record, const char *name, DataBindStringView *out,
    DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_bytes(
    const DataBindRecord *record, const char *name, DataBindBytesView *out,
    DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_object(
    const DataBindRecord *record, const char *name, DataBindRecordView *out,
    DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_list(
    const DataBindRecord *record, const char *name, DataBindListView *out,
    DataBindError *error);
DATA_BIND_API DataBindStatus data_bind_record_get_map(
    const DataBindRecord *record, const char *name, DataBindRecordMapView *out,
    DataBindError *error);

DATA_BIND_API size_t data_bind_list_view_count(const DataBindListView *view);
DATA_BIND_API DataBindStatus data_bind_list_view_at(
    const DataBindListView *view, size_t index, DataBindRecordField *out_field,
    DataBindError *error);
DATA_BIND_API size_t data_bind_record_map_view_count(const DataBindRecordMapView *view);
DATA_BIND_API DataBindStatus data_bind_record_map_view_at(
    const DataBindRecordMapView *view, size_t index, DataBindStringView *out_key,
    DataBindRecordField *out_value, DataBindError *error);

DATA_BIND_API void data_bind_serialized_free(char *data);
DATA_BIND_API void data_bind_binary_free(void *data);
DATA_BIND_API void data_bind_object_free(DataBindObject *object);

DATA_BIND_API DataBindValueKind data_bind_value_kind(const DataBindValue *value);
DATA_BIND_API size_t data_bind_value_field_count(const DataBindValue *value);
DATA_BIND_API const char *data_bind_value_field_name(const DataBindValue *value, size_t index);
DATA_BIND_API const DataBindValue *data_bind_value_field_at(const DataBindValue *value,
                                                            size_t index);
DATA_BIND_API const DataBindValue *data_bind_value_get(const DataBindValue *value,
                                                       const char *name);
DATA_BIND_API size_t data_bind_value_count(const DataBindValue *value);
DATA_BIND_API const DataBindValue *data_bind_value_at(const DataBindValue *value, size_t index);
DATA_BIND_API DataBindMapEntry data_bind_value_map_entry_at(const DataBindValue *value,
                                                            size_t index);
DATA_BIND_API int32_t data_bind_value_as_int(const DataBindValue *value);
DATA_BIND_API int64_t data_bind_value_as_int64(const DataBindValue *value);
/** Convert a non-negative integer-compatible value to uint64, or return zero on mismatch. */
DATA_BIND_API uint64_t data_bind_value_as_uint64(const DataBindValue *value);
DATA_BIND_API double data_bind_value_as_double(const DataBindValue *value);
DATA_BIND_API int data_bind_value_as_bool(const DataBindValue *value);
DATA_BIND_API const char *data_bind_value_as_string(const DataBindValue *value);
DATA_BIND_API const uint8_t *data_bind_value_as_bytes(const DataBindValue *value, size_t *len);
DATA_BIND_API int data_bind_value_as_uuid(const DataBindValue *value,
                                          uint8_t out[DATA_BIND_UUID_SIZE]);
DATA_BIND_API const char *data_bind_value_as_uuid_string(const DataBindValue *value, char *out,
                                                         size_t len);
DATA_BIND_API int data_bind_value_as_datetime(const DataBindValue *value, turbo_datetime_t *out);
DATA_BIND_API double data_bind_value_as_datetime_timestamp(const DataBindValue *value);
DATA_BIND_API const char *data_bind_value_as_datetime_string(const DataBindValue *value, char *out,
                                                             size_t len);
DATA_BIND_API int data_bind_value_as_date(const DataBindValue *value, DataBindDate *out);
DATA_BIND_API const char *data_bind_value_as_date_string(const DataBindValue *value, char *out,
                                                         size_t len);
DATA_BIND_API int data_bind_value_as_time(const DataBindValue *value, DataBindTime *out);
DATA_BIND_API const char *data_bind_value_as_time_string(const DataBindValue *value, char *out,
                                                         size_t len);
DATA_BIND_API int64_t data_bind_value_as_duration_milliseconds(const DataBindValue *value);
DATA_BIND_API const char *data_bind_value_as_duration_string(const DataBindValue *value, char *out,
                                                             size_t len);
DATA_BIND_API int data_bind_value_as_decimal(const DataBindValue *value, DataBindDecimal *out);
DATA_BIND_API const char *data_bind_value_as_decimal_string(const DataBindValue *value, char *out,
                                                            size_t len);
DATA_BIND_API const char *data_bind_value_as_bigint_string(const DataBindValue *value);
DATA_BIND_API int data_bind_value_as_money(const DataBindValue *value, DataBindMoney *out);
DATA_BIND_API const char *data_bind_value_as_money_string(const DataBindValue *value, char *out,
                                                          size_t len);
DATA_BIND_API DataBindStatus data_bind_value_get_int32(const DataBindValue *value, int32_t *out);
DATA_BIND_API DataBindStatus data_bind_value_get_int64(const DataBindValue *value, int64_t *out);
/** Read an exact non-negative integer into @p out, rejecting negative or non-integer values. */
DATA_BIND_API DataBindStatus data_bind_value_get_uint64(const DataBindValue *value, uint64_t *out);
DATA_BIND_API DataBindStatus data_bind_value_get_double(const DataBindValue *value, double *out);
DATA_BIND_API DataBindStatus data_bind_value_get_bool(const DataBindValue *value, int *out);
DATA_BIND_API DataBindStatus data_bind_value_get_string(const DataBindValue *value,
                                                        const char **data, size_t *len);
DATA_BIND_API DataBindStatus data_bind_value_get_bytes(const DataBindValue *value,
                                                       const uint8_t **data, size_t *len);
DATA_BIND_API DataBindStatus data_bind_value_get_uuid(const DataBindValue *value,
                                                      uint8_t out[DATA_BIND_UUID_SIZE]);
DATA_BIND_API DataBindStatus data_bind_value_get_datetime(const DataBindValue *value,
                                                          turbo_datetime_t *out);
DATA_BIND_API DataBindStatus data_bind_value_get_date(const DataBindValue *value,
                                                      DataBindDate *out);
DATA_BIND_API DataBindStatus data_bind_value_get_time(const DataBindValue *value,
                                                      DataBindTime *out);
DATA_BIND_API DataBindStatus data_bind_value_get_duration_milliseconds(const DataBindValue *value,
                                                                       int64_t *out);
DATA_BIND_API DataBindStatus data_bind_value_get_decimal(const DataBindValue *value,
                                                         DataBindDecimal *out);
DATA_BIND_API DataBindStatus data_bind_value_get_bigint(const DataBindValue *value,
                                                        const char **text, size_t *len);
DATA_BIND_API DataBindStatus data_bind_value_get_money(const DataBindValue *value,
                                                       DataBindMoney *out);

/**
 * @brief Return a stable string name for a schema kind.
 */
DATA_BIND_API const char *data_bind_schema_kind_name(DataBindSchemaKind kind);

/**
 * @brief Return the number of reflected schema types.
 *
 * Includes messages, composites, groups, unions, enums and flags. Returned
 * strings from reflection calls are owned by the codec and remain valid until
 * data_bind_free().
 */
DATA_BIND_API size_t data_bind_schema_type_count(DataBind *codec);

/**
 * @brief Read schema type metadata by index.
 * @return 1 when out was filled, 0 when arguments or index are invalid
 */
DATA_BIND_API int data_bind_schema_type_at(DataBind *codec, size_t index, DataBindSchemaType *out);

/**
 * @brief Find a schema type by name.
 * @return 1 when out was filled, 0 when not found or arguments are invalid
 */
DATA_BIND_API int data_bind_schema_find_type(DataBind *codec, const char *name,
                                             DataBindSchemaType *out);

/**
 * @brief Return the number of fields for a message, composite, group or union.
 */
DATA_BIND_API size_t data_bind_schema_field_count(DataBind *codec, const char *type_name);

/**
 * @brief Read field metadata for a message, composite, group or union.
 * @return 1 when out was filled, 0 when not found or arguments are invalid
 */
DATA_BIND_API int data_bind_schema_field_at(DataBind *codec, const char *type_name, size_t index,
                                            DataBindSchemaField *out);

/**
 * @brief Return the number of enum/flags declarations.
 */
DATA_BIND_API size_t data_bind_schema_enum_count(DataBind *codec);

/**
 * @brief Read enum/flags type metadata by enum index.
 * @return 1 when out was filled, 0 when arguments or index are invalid
 */
DATA_BIND_API int data_bind_schema_enum_at(DataBind *codec, size_t index, DataBindSchemaType *out);

/**
 * @brief Return the number of items in an enum or flags declaration.
 */
DATA_BIND_API size_t data_bind_schema_enum_item_count(DataBind *codec, const char *enum_name);

/**
 * @brief Read enum/flags item metadata by item index.
 * @return 1 when out was filled, 0 when not found or arguments are invalid
 */
DATA_BIND_API int data_bind_schema_enum_item_at(DataBind *codec, const char *enum_name,
                                                size_t index, DataBindSchemaEnumItem *out);

/**
 * @brief Return the optional schema declaration name.
 */
DATA_BIND_API const char *data_bind_schema_name(DataBind *codec);

/**
 * @brief Return the number of attributes on the schema declaration.
 */
DATA_BIND_API size_t data_bind_schema_attribute_count(DataBind *codec);

/**
 * @brief Read schema declaration attribute metadata by index.
 * @return 1 when out was filled, 0 when not found or arguments are invalid
 */
DATA_BIND_API int data_bind_schema_attribute_at(DataBind *codec, size_t index,
                                                DataBindSchemaAttribute *out);

/**
 * @brief Find a schema declaration attribute by name.
 */
DATA_BIND_API const char *data_bind_schema_attribute_get(DataBind *codec, const char *name);

/**
 * @brief Generate the MIR parser module for a schema without linking or JIT.
 * @param schema_path Path to .schema file
 * @param write Callback receiving textual MIR or binary MIR chunks
 * @param user Caller context passed to write
 * @param binary_output Non-zero writes binary MIR, zero writes textual MIR
 * @return DATA_BIND_OK on success, otherwise a DataBindStatus error code
 */
DATA_BIND_API DataBindStatus data_bind_generate_mir(const char *schema_path, DataBindWriteFn write,
                                                    void *user, int binary_output,
                                                    DataBindError *error);

DATA_BIND_API const char *data_bind_status_name(DataBindStatus status);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_H */
