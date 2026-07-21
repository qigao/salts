#ifndef TBE_TYPED_H
#define TBE_TYPED_H

#include "data_bind.h"
#include "turbo_parser.h"
#include "turbo_str.h"
#include "turbo_vec.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime field kinds used by generated owning C records. */
typedef enum TbeTypedKind {
  TBE_TYPED_BOOL = 0,
  TBE_TYPED_I8,
  TBE_TYPED_U8,
  TBE_TYPED_I16,
  TBE_TYPED_U16,
  TBE_TYPED_I32,
  TBE_TYPED_U32,
  TBE_TYPED_I64,
  TBE_TYPED_U64,
  TBE_TYPED_F32,
  TBE_TYPED_F64,
  TBE_TYPED_ENUM,
  TBE_TYPED_STRING,
  TBE_TYPED_BYTES,
  TBE_TYPED_FIXED_BYTES,
  TBE_TYPED_OBJECT,
  TBE_TYPED_FIXED_ARRAY,
  TBE_TYPED_LIST,
  TBE_TYPED_SET,
  TBE_TYPED_MAP,
  TBE_TYPED_UUID
} TbeTypedKind;

enum {
  TBE_TYPED_FIELD_OPTIONAL = 1u << 0,
  TBE_TYPED_FIELD_WIRE_OFFSET = 1u << 1,
  TBE_TYPED_FIELD_VAR_DATA = 1u << 2,
  TBE_TYPED_FIELD_GROUP = 1u << 3
};

typedef struct TbeTypedType TbeTypedType;

typedef struct TbeTypedField {
  const char *name;
  TbeTypedKind kind;
  TbeTypedKind wire_kind;
  size_t offset;
  TbeTypedKind element_kind;
  TbeTypedKind element_wire_kind;
  size_t element_size;
  size_t fixed_count;
  const TbeTypedType *object_type;
  size_t map_entry_size;
  size_t map_key_offset;
  size_t map_value_offset;
  TbeTypedKind map_value_kind;
  TbeTypedKind map_value_wire_kind;
  const TbeTypedType *map_value_type;
  size_t wire_offset;
  size_t wire_size;
  unsigned optional_bit;
  unsigned flags;
} TbeTypedField;

struct TbeTypedType {
  const char *name;
  size_t size;
  const TbeTypedField *fields;
  size_t field_count;
  size_t fixed_block_size;
  size_t presence_offset;
  size_t presence_size;
  int wire_big_endian;
};

TURBO_VEC_DEFINE(tbe_bytes_t, uint8_t)

/**
 * Initialize an owning generated object from its descriptor.
 * @param type Generated record descriptor.
 * @param object Writable storage of at least `type->size` bytes.
 * @param error Optional structured error initialized with `DATA_BIND_ERROR_INIT`.
 * @return `DATA_BIND_OK`, or an allocation/argument error. On success, call
 *         `tbe_typed_clear` before discarding the object.
 */
DATA_BIND_API DataBindStatus tbe_typed_init(const TbeTypedType *type, void *object,
                                            DataBindError *error);

/** Release all owned strings, byte vectors, containers, and nested objects. */
DATA_BIND_API void tbe_typed_clear(const TbeTypedType *type, void *object);

/**
 * Populate an initialized object from a schema-bound dynamic value.
 * @return `DATA_BIND_OK`, or a type/range/allocation error.
 */
DATA_BIND_API DataBindStatus tbe_typed_from_value(const TbeTypedType *type,
                                                  const DataBindValue *value, void *object,
                                                  DataBindError *error);

/**
 * Verify that a generated native descriptor matches a type in @p codec.
 *
 * This checks the record kind, field order, field names, optional flags, byte
 * order, defined wire offsets, scalar kinds, and host-memory bounds. It does
 * not retain @p codec.
 */
DATA_BIND_API DataBindStatus tbe_typed_validate_schema(DataBind *codec, const char *type_name,
                                                       const TbeTypedType *type,
                                                       DataBindError *error);

/**
 * Decode binary wire data directly into an initialized owning object.
 *
 * The descriptor must define a fixed binary block plus supported group or
 * string/bytes tail fields. It is the binary layout contract. The previous
 * object remains unchanged on failure. Call tbe_typed_validate_schema when the
 * descriptor and codec were not generated from the same trusted schema.
 */
DATA_BIND_API DataBindStatus tbe_typed_parse_binary(const TbeTypedType *type, const void *data,
                                                    size_t len, void *object, DataBindError *error);

/** Convert an owning object to a newly allocated TurboParser JSON value. */
DATA_BIND_API json_value_t *tbe_typed_to_json(const TbeTypedType *type, const void *object,
                                              DataBindError *error);

/**
 * Parse `bin`, `json`, `yaml`, `csv`, or `xml` into an initialized object.
 * CSV uses the zero-based @p row index. The previous object remains unchanged
 * when parsing fails.
 */
DATA_BIND_API DataBindStatus tbe_typed_parse(DataBind *codec, const char *type_name,
                                             const TbeTypedType *type, const char *format,
                                             const void *data, size_t len, size_t row, void *object,
                                             DataBindError *error);

/**
 * Serialize to `json`, `yaml`, `csv`, or `xml`.
 * @param codec Required for YAML and XML; ignored for JSON and CSV.
 * @param out Receives an allocated buffer released by `tbe_typed_serialized_free`.
 */
DATA_BIND_API DataBindStatus tbe_typed_serialize(DataBind *codec, const char *type_name,
                                                 const TbeTypedType *type, const void *object,
                                                 const char *format, char **out, size_t *out_len,
                                                 DataBindError *error);

/** Serialize an owning object into its schema binary wire representation. */
DATA_BIND_API DataBindStatus tbe_typed_serialize_binary(const TbeTypedType *type,
                                                        const void *object, uint8_t **out,
                                                        size_t *out_len, DataBindError *error);

/**
 * Serialize binary wire bytes into caller-owned storage without allocating the
 * output buffer. out_len receives the required size even when capacity is too
 * small, in which case DATA_BIND_ERR_INVALID_ARG is returned.
 */
DATA_BIND_API DataBindStatus tbe_typed_serialize_binary_into(const TbeTypedType *type,
                                                             const void *object, uint8_t *output,
                                                             size_t capacity, size_t *out_len,
                                                             DataBindError *error);

/** Release any buffer returned by a generated `*_to_*` function. */
DATA_BIND_API void tbe_typed_serialized_free(void *data);

#ifdef __cplusplus
}
#endif

#endif
