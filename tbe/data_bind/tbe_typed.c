#include "tbe_typed.h"

#include "fmt.h"
#include "tbe_wire.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DataBindStatus typed_error(DataBindError *error, DataBindStatus status, const char *path,
                                  const char *message) {
  if (error != NULL && error->size >= sizeof(*error)) {
    error->code = status;
    error->line = -1;
    error->column = -1;
    snprintf(error->path, sizeof(error->path), "%s", path ? path : "");
    snprintf(error->message, sizeof(error->message), "%s", message ? message : "");
  }
  return status;
}

static int typed_is_integer(TbeTypedKind kind) {
  return kind >= TBE_TYPED_I8 && kind <= TBE_TYPED_U64;
}

static int typed_is_signed(TbeTypedKind kind) {
  return kind == TBE_TYPED_I8 || kind == TBE_TYPED_I16 || kind == TBE_TYPED_I32 ||
         kind == TBE_TYPED_I64;
}

static size_t typed_kind_size(TbeTypedKind kind) {
  switch (kind) {
  case TBE_TYPED_BOOL:
  case TBE_TYPED_I8:
  case TBE_TYPED_U8:
    return 1;
  case TBE_TYPED_I16:
  case TBE_TYPED_U16:
    return 2;
  case TBE_TYPED_I32:
  case TBE_TYPED_U32:
  case TBE_TYPED_F32:
  case TBE_TYPED_ENUM:
    return 4;
  case TBE_TYPED_I64:
  case TBE_TYPED_U64:
  case TBE_TYPED_F64:
    return 8;
  case TBE_TYPED_UUID:
    return TURBO_UUID_SIZE;
  default:
    return 0;
  }
}

static int typed_size_fits(size_t offset, size_t size, size_t capacity) {
  return offset <= capacity && size <= capacity - offset;
}

static int typed_multiply_fits(size_t count, size_t size, size_t *total) {
  if (total == NULL || (size != 0 && count > SIZE_MAX / size)) return 0;
  *total = count * size;
  return 1;
}

static int typed_optional_present(const TbeTypedType *type, const void *object,
                                  const TbeTypedField *field) {
  const uint8_t *presence;
  if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) == 0) return 1;
  if (type->presence_size == 0) return 0;
  presence = (const uint8_t *)object + type->presence_offset;
  return (presence[field->optional_bit / 8u] & (uint8_t)(1u << (field->optional_bit % 8u))) != 0;
}

static void typed_optional_set(const TbeTypedType *type, void *object, const TbeTypedField *field) {
  uint8_t *presence;
  if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) == 0 || type->presence_size == 0) return;
  presence = (uint8_t *)object + type->presence_offset;
  presence[field->optional_bit / 8u] |= (uint8_t)(1u << (field->optional_bit % 8u));
}

static DataBindStatus typed_init_value(TbeTypedKind kind, const TbeTypedType *object_type,
                                       void *ptr, size_t count, DataBindError *error) {
  size_t i;
  if (kind == TBE_TYPED_STRING) {
    for (i = 0; i < count; ++i)
      ((tstr_t *)ptr)[i] = NULL;
  } else if (kind == TBE_TYPED_BYTES) {
    for (i = 0; i < count; ++i) {
      if (turbo_vec_init(&((turbo_vec_t *)ptr)[i], sizeof(uint8_t)) != TURBO_OK)
        return typed_error(error, DATA_BIND_ERR_RUNTIME, NULL, "Failed to initialize vector");
    }
  } else if (kind == TBE_TYPED_OBJECT) {
    for (i = 0; i < count; ++i) {
      DataBindStatus status =
          tbe_typed_init(object_type, (uint8_t *)ptr + i * object_type->size, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_init(const TbeTypedType *type, void *object, DataBindError *error) {
  size_t i;
  if (type == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed object");
  memset(object, 0, type->size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    void *ptr = (uint8_t *)object + field->offset;
    DataBindStatus status;
    if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      status =
          typed_init_value(field->element_kind, field->object_type, ptr, field->fixed_count, error);
    } else if (field->kind == TBE_TYPED_BYTES || field->kind == TBE_TYPED_LIST ||
               field->kind == TBE_TYPED_SET || field->kind == TBE_TYPED_MAP) {
      status = turbo_vec_init((turbo_vec_t *)ptr, field->kind == TBE_TYPED_BYTES
                                                      ? sizeof(uint8_t)
                                                      : field->element_size) == TURBO_OK
                   ? DATA_BIND_OK
                   : typed_error(error, DATA_BIND_ERR_RUNTIME, field->name,
                                 "Failed to initialize typed vector");
    } else {
      status = typed_init_value(field->kind, field->object_type, ptr, 1, error);
    }
    if (status != DATA_BIND_OK) {
      tbe_typed_clear(type, object);
      return status;
    }
  }
  return DATA_BIND_OK;
}

static void typed_clear_value(TbeTypedKind kind, const TbeTypedType *object_type, void *ptr,
                              size_t count) {
  size_t i;
  if (kind == TBE_TYPED_STRING) {
    for (i = 0; i < count; ++i)
      tstr_free(((tstr_t *)ptr)[i]);
  } else if (kind == TBE_TYPED_BYTES) {
    for (i = 0; i < count; ++i)
      turbo_vec_destroy(&((turbo_vec_t *)ptr)[i]);
  } else if (kind == TBE_TYPED_OBJECT && object_type != NULL) {
    for (i = 0; i < count; ++i)
      tbe_typed_clear(object_type, (uint8_t *)ptr + i * object_type->size);
  }
}

void tbe_typed_clear(const TbeTypedType *type, void *object) {
  size_t i;
  if (type == NULL || object == NULL) return;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    void *ptr = (uint8_t *)object + field->offset;
    if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      typed_clear_value(field->element_kind, field->object_type, ptr, field->fixed_count);
    } else if (field->kind == TBE_TYPED_LIST || field->kind == TBE_TYPED_SET) {
      turbo_vec_t *vec = (turbo_vec_t *)ptr;
      typed_clear_value(field->element_kind, field->object_type, vec->data, vec->size);
      turbo_vec_destroy(vec);
    } else if (field->kind == TBE_TYPED_MAP) {
      turbo_vec_t *vec = (turbo_vec_t *)ptr;
      size_t j;
      for (j = 0; j < vec->size; ++j) {
        uint8_t *entry = (uint8_t *)vec->data + j * field->map_entry_size;
        tstr_free(*(tstr_t *)(entry + field->map_key_offset));
        typed_clear_value(field->map_value_kind, field->map_value_type,
                          entry + field->map_value_offset, 1);
      }
      turbo_vec_destroy(vec);
    } else if (field->kind == TBE_TYPED_BYTES) {
      turbo_vec_destroy((turbo_vec_t *)ptr);
    } else {
      typed_clear_value(field->kind, field->object_type, ptr, 1);
    }
  }
  memset(object, 0, type->size);
}

static DataBindStatus typed_read_scalar(TbeTypedKind kind, const DataBindValue *value, void *out,
                                        const char *path, DataBindError *error) {
  int64_t signed_value = 0;
  uint64_t unsigned_value = 0;
  double double_value = 0.0;
  int bool_value = 0;
  if (kind == TBE_TYPED_BOOL) {
    if (data_bind_value_get_bool(value, &bool_value) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected boolean value");
    *(uint8_t *)out = (uint8_t)(bool_value != 0);
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_F32 || kind == TBE_TYPED_F64) {
    if (data_bind_value_get_double(value, &double_value) != DATA_BIND_OK || !isfinite(double_value))
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected finite number");
    if (kind == TBE_TYPED_F32) *(float *)out = (float)double_value;
    else *(double *)out = double_value;
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_ENUM) kind = TBE_TYPED_I32;
  if (kind == TBE_TYPED_U64) {
    if (data_bind_value_get_uint64(value, &unsigned_value) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected unsigned integer");
    *(uint64_t *)out = unsigned_value;
    return DATA_BIND_OK;
  }
  if (!typed_is_integer(kind) || data_bind_value_get_int64(value, &signed_value) != DATA_BIND_OK)
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected integer value");
  switch (kind) {
  case TBE_TYPED_I8:
    if (signed_value < INT8_MIN || signed_value > INT8_MAX) goto range_error;
    *(int8_t *)out = (int8_t)signed_value;
    break;
  case TBE_TYPED_U8:
    if (signed_value < 0 || signed_value > UINT8_MAX) goto range_error;
    *(uint8_t *)out = (uint8_t)signed_value;
    break;
  case TBE_TYPED_I16:
    if (signed_value < INT16_MIN || signed_value > INT16_MAX) goto range_error;
    *(int16_t *)out = (int16_t)signed_value;
    break;
  case TBE_TYPED_U16:
    if (signed_value < 0 || signed_value > UINT16_MAX) goto range_error;
    *(uint16_t *)out = (uint16_t)signed_value;
    break;
  case TBE_TYPED_I32:
    if (signed_value < INT32_MIN || signed_value > INT32_MAX) goto range_error;
    *(int32_t *)out = (int32_t)signed_value;
    break;
  case TBE_TYPED_U32:
    if (signed_value < 0 || (uint64_t)signed_value > UINT32_MAX) goto range_error;
    *(uint32_t *)out = (uint32_t)signed_value;
    break;
  case TBE_TYPED_I64:
    *(int64_t *)out = signed_value;
    break;
  default:
    goto range_error;
  }
  return DATA_BIND_OK;
range_error:
  return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Integer value is out of range");
}

static DataBindStatus typed_from_one(TbeTypedKind kind, const TbeTypedType *object_type,
                                     const DataBindValue *value, void *out, const char *path,
                                     DataBindError *error) {
  if (kind == TBE_TYPED_STRING) {
    const char *text;
    size_t len;
    if (data_bind_value_get_string(value, &text, &len) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected string value");
    *(tstr_t *)out = tstr_dup_len(text, len);
    return *(tstr_t *)out != NULL
               ? DATA_BIND_OK
               : typed_error(error, DATA_BIND_ERR_OOM, path, "Out of memory copying string");
  }
  if (kind == TBE_TYPED_BYTES) {
    const uint8_t *bytes;
    size_t len;
    turbo_vec_t *vec = (turbo_vec_t *)out;
    if (data_bind_value_get_bytes(value, &bytes, &len) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected bytes value");
    if (turbo_vec_resize(vec, len) != TURBO_OK)
      return typed_error(error, DATA_BIND_ERR_OOM, path, "Out of memory copying bytes");
    if (len != 0) memcpy(vec->data, bytes, len);
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_UUID) {
    if (data_bind_value_get_uuid(value, ((turbo_uuid_t *)out)->bytes) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected UUID value");
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_OBJECT) return tbe_typed_from_value(object_type, value, out, error);
  return typed_read_scalar(kind, value, out, path, error);
}

static DataBindStatus typed_from_value_at(const TbeTypedType *type, const DataBindValue *value,
                                          void *object, const char *prefix, DataBindError *error) {
  size_t i;
  if (type == NULL || value == NULL || object == NULL ||
      data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT)
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, type ? type->name : NULL,
                       "Expected schema object");
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const DataBindValue *child;
    char field_path[512];
    void *out = (uint8_t *)object + field->offset;
    DataBindStatus status = DATA_BIND_OK;
    size_t j;
    if (prefix != NULL && prefix[0] != '\0') {
      int written = snprintf(field_path, sizeof(field_path), "%s%s", prefix, field->name);
      if (written < 0 || (size_t)written >= sizeof(field_path))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Flattened field path is too long");
      child = data_bind_value_get(value, field_path);
    } else {
      child = data_bind_value_get(value, field->name);
    }
    if (child == NULL && field->kind == TBE_TYPED_OBJECT && field->object_type != NULL) {
      char nested_prefix[512];
      int written = snprintf(nested_prefix, sizeof(nested_prefix), "%s%s.", prefix ? prefix : "",
                             field->name);
      if (written < 0 || (size_t)written >= sizeof(nested_prefix))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Flattened object path is too long");
      status = typed_from_value_at(field->object_type, value, out, nested_prefix, error);
      if (status == DATA_BIND_OK) {
        typed_optional_set(type, object, field);
        continue;
      }
      if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0) continue;
      return status;
    }
    if (child == NULL) {
      if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0) continue;
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                         "Required field is missing");
    }
    typed_optional_set(type, object, field);
    if (field->kind == TBE_TYPED_BYTES || field->kind == TBE_TYPED_FIXED_BYTES) {
      const uint8_t *bytes;
      size_t len;
      if (data_bind_value_get_bytes(child, &bytes, &len) != DATA_BIND_OK)
        return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, "Expected bytes value");
      if (field->kind == TBE_TYPED_FIXED_BYTES) {
        if (len != field->fixed_count)
          return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                             "Fixed bytes length does not match schema");
        memcpy(out, bytes, len);
      } else {
        turbo_vec_t *vec = (turbo_vec_t *)out;
        if (turbo_vec_resize(vec, len) != TURBO_OK)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name, "Out of memory copying bytes");
        if (len != 0) memcpy(vec->data, bytes, len);
      }
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      if (data_bind_value_count(child) != field->fixed_count)
        return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                           "Fixed array length does not match schema");
      for (j = 0; j < field->fixed_count; ++j) {
        status =
            typed_from_one(field->element_kind, field->object_type, data_bind_value_at(child, j),
                           (uint8_t *)out + j * field->element_size, field->name, error);
        if (status != DATA_BIND_OK) return status;
      }
    } else if (field->kind == TBE_TYPED_LIST || field->kind == TBE_TYPED_SET) {
      turbo_vec_t *vec = (turbo_vec_t *)out;
      size_t count = data_bind_value_count(child);
      if (turbo_vec_resize(vec, count) != TURBO_OK)
        return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                           "Out of memory resizing typed vector");
      memset(vec->data, 0, count * field->element_size);
      for (j = 0; j < count; ++j) {
        void *element = (uint8_t *)vec->data + j * field->element_size;
        if (field->element_kind == TBE_TYPED_OBJECT || field->element_kind == TBE_TYPED_BYTES) {
          status = typed_init_value(field->element_kind, field->object_type, element, 1, error);
          if (status != DATA_BIND_OK) return status;
        }
        status = typed_from_one(field->element_kind, field->object_type,
                                data_bind_value_at(child, j), element, field->name, error);
        if (status != DATA_BIND_OK) return status;
      }
    } else if (field->kind == TBE_TYPED_MAP) {
      turbo_vec_t *vec = (turbo_vec_t *)out;
      size_t count = data_bind_value_count(child);
      if (turbo_vec_resize(vec, count) != TURBO_OK)
        return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                           "Out of memory resizing typed map");
      memset(vec->data, 0, count * field->map_entry_size);
      for (j = 0; j < count; ++j) {
        DataBindMapEntry item = data_bind_value_map_entry_at(child, j);
        uint8_t *entry = (uint8_t *)vec->data + j * field->map_entry_size;
        *(tstr_t *)(entry + field->map_key_offset) = tstr_dup(item.key);
        if (*(tstr_t *)(entry + field->map_key_offset) == NULL)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory copying map key");
        if (field->map_value_kind == TBE_TYPED_OBJECT || field->map_value_kind == TBE_TYPED_BYTES) {
          status = typed_init_value(field->map_value_kind, field->map_value_type,
                                    entry + field->map_value_offset, 1, error);
          if (status != DATA_BIND_OK) return status;
        }
        status = typed_from_one(field->map_value_kind, field->map_value_type, item.value,
                                entry + field->map_value_offset, field->name, error);
        if (status != DATA_BIND_OK) return status;
      }
    } else {
      status = typed_from_one(field->kind, field->object_type, child, out, field->name, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  if (error != NULL && error->size >= sizeof(*error)) error->code = DATA_BIND_OK;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_from_value(const TbeTypedType *type, const DataBindValue *value,
                                    void *object, DataBindError *error) {
  return typed_from_value_at(type, value, object, "", error);
}

static json_value_t *typed_scalar_json(TbeTypedKind kind, const void *ptr) {
  if (kind == TBE_TYPED_BOOL) return turbo_json_create_bool(*(const uint8_t *)ptr != 0);
  if (kind == TBE_TYPED_STRING) {
    tstr_t text = *(const tstr_t *)ptr;
    return turbo_json_create_string_n(text ? text : "", text ? tstr_len(text) : 0);
  }
  if (kind == TBE_TYPED_UUID) {
    char text[TURBO_UUID_STRING_SIZE];
    if (turbo_uuid_format((const turbo_uuid_t *)ptr, text, sizeof(text)) != TURBO_OK) return NULL;
    return turbo_json_create_string(text);
  }
  if (kind == TBE_TYPED_F32) return turbo_json_create_number(*(const float *)ptr);
  if (kind == TBE_TYPED_F64) return turbo_json_create_number(*(const double *)ptr);
  if (kind == TBE_TYPED_ENUM) return turbo_json_create_int64(*(const int32_t *)ptr);
  switch (kind) {
  case TBE_TYPED_I8:
    return turbo_json_create_int64(*(const int8_t *)ptr);
  case TBE_TYPED_U8:
    return turbo_json_create_int64(*(const uint8_t *)ptr);
  case TBE_TYPED_I16:
    return turbo_json_create_int64(*(const int16_t *)ptr);
  case TBE_TYPED_U16:
    return turbo_json_create_int64(*(const uint16_t *)ptr);
  case TBE_TYPED_I32:
    return turbo_json_create_int64(*(const int32_t *)ptr);
  case TBE_TYPED_U32:
    return turbo_json_create_int64(*(const uint32_t *)ptr);
  case TBE_TYPED_I64:
    return turbo_json_create_int64(*(const int64_t *)ptr);
  case TBE_TYPED_U64:
    return turbo_json_create_uint64(*(const uint64_t *)ptr);
  default:
    return NULL;
  }
}

static json_value_t *typed_one_json(TbeTypedKind kind, const TbeTypedType *object_type,
                                    const void *ptr, DataBindError *error) {
  if (kind == TBE_TYPED_OBJECT) return tbe_typed_to_json(object_type, ptr, error);
  if (kind == TBE_TYPED_BYTES) {
    const turbo_vec_t *vec = (const turbo_vec_t *)ptr;
    return turbo_json_create_string_n((const char *)vec->data, vec->size);
  }
  return typed_scalar_json(kind, ptr);
}

json_value_t *tbe_typed_to_json(const TbeTypedType *type, const void *object,
                                DataBindError *error) {
  json_value_t *root;
  size_t i;
  if (type == NULL || object == NULL) {
    typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed object");
    return NULL;
  }
  root = turbo_json_create_object();
  if (root == NULL) {
    typed_error(error, DATA_BIND_ERR_OOM, type->name, "Out of memory creating JSON object");
    return NULL;
  }
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const void *ptr = (const uint8_t *)object + field->offset;
    json_value_t *child = NULL;
    size_t j;
    if (!typed_optional_present(type, object, field)) continue;
    if (field->kind == TBE_TYPED_BYTES || field->kind == TBE_TYPED_FIXED_BYTES) {
      const uint8_t *data;
      size_t len;
      if (field->kind == TBE_TYPED_BYTES) {
        const turbo_vec_t *vec = (const turbo_vec_t *)ptr;
        data = (const uint8_t *)vec->data;
        len = vec->size;
      } else {
        data = (const uint8_t *)ptr;
        len = field->fixed_count;
      }
      child = turbo_json_create_string_n((const char *)data, len);
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
               field->kind == TBE_TYPED_SET) {
      const uint8_t *data;
      size_t count;
      child = turbo_json_create_array();
      if (field->kind == TBE_TYPED_FIXED_ARRAY) {
        data = (const uint8_t *)ptr;
        count = field->fixed_count;
      } else {
        const turbo_vec_t *vec = (const turbo_vec_t *)ptr;
        data = (const uint8_t *)vec->data;
        count = vec->size;
      }
      for (j = 0; child != NULL && j < count; ++j) {
        json_value_t *item = typed_one_json(field->element_kind, field->object_type,
                                            data + j * field->element_size, error);
        if (item == NULL || !turbo_json_array_add_checked(child, item)) {
          turbo_free_json(&item);
          turbo_free_json(&child);
        }
      }
    } else if (field->kind == TBE_TYPED_MAP) {
      const turbo_vec_t *vec = (const turbo_vec_t *)ptr;
      child = turbo_json_create_object();
      for (j = 0; child != NULL && j < vec->size; ++j) {
        const uint8_t *entry = (const uint8_t *)vec->data + j * field->map_entry_size;
        tstr_t key = *(const tstr_t *)(entry + field->map_key_offset);
        json_value_t *item = typed_one_json(field->map_value_kind, field->map_value_type,
                                            entry + field->map_value_offset, error);
        if (item == NULL || !turbo_json_object_add_checked(child, key ? key : "", item)) {
          turbo_free_json(&item);
          turbo_free_json(&child);
        }
      }
    } else {
      child = typed_one_json(field->kind, field->object_type, ptr, error);
    }
    if (child == NULL || !turbo_json_object_add_checked(root, field->name, child)) {
      turbo_free_json(&child);
      turbo_free_json(&root);
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                  "Typed field cannot be represented as JSON");
      return NULL;
    }
  }
  if (error != NULL && error->size >= sizeof(*error)) error->code = DATA_BIND_OK;
  return root;
}

static int typed_scalar_kind_from_name(const char *name, TbeTypedKind *kind) {
  if (name == NULL || kind == NULL) return 0;
  if (strcmp(name, "bool") == 0) *kind = TBE_TYPED_BOOL;
  else if (strcmp(name, "int8_t") == 0 || strcmp(name, "int8") == 0 || strcmp(name, "i8") == 0)
    *kind = TBE_TYPED_I8;
  else if (strcmp(name, "uint8_t") == 0 || strcmp(name, "uint8") == 0 || strcmp(name, "u8") == 0 ||
           strcmp(name, "byte") == 0)
    *kind = TBE_TYPED_U8;
  else if (strcmp(name, "int16_t") == 0 || strcmp(name, "int16") == 0 || strcmp(name, "i16") == 0)
    *kind = TBE_TYPED_I16;
  else if (strcmp(name, "uint16_t") == 0 || strcmp(name, "uint16") == 0 || strcmp(name, "u16") == 0)
    *kind = TBE_TYPED_U16;
  else if (strcmp(name, "int32_t") == 0 || strcmp(name, "int32") == 0 || strcmp(name, "i32") == 0)
    *kind = TBE_TYPED_I32;
  else if (strcmp(name, "uint32_t") == 0 || strcmp(name, "uint32") == 0 || strcmp(name, "u32") == 0)
    *kind = TBE_TYPED_U32;
  else if (strcmp(name, "int64_t") == 0 || strcmp(name, "int64") == 0 || strcmp(name, "i64") == 0)
    *kind = TBE_TYPED_I64;
  else if (strcmp(name, "uint64_t") == 0 || strcmp(name, "uint64") == 0 || strcmp(name, "u64") == 0)
    *kind = TBE_TYPED_U64;
  else if (strcmp(name, "float") == 0) *kind = TBE_TYPED_F32;
  else if (strcmp(name, "double") == 0) *kind = TBE_TYPED_F64;
  else if (strcmp(name, "string") == 0) *kind = TBE_TYPED_STRING;
  else if (strcmp(name, "bytes") == 0) *kind = TBE_TYPED_BYTES;
  else if (strcmp(name, "uuid") == 0) *kind = TBE_TYPED_UUID;
  else return 0;
  return 1;
}

static int typed_named_kind_matches(DataBind *codec, const char *name, TbeTypedKind kind,
                                    TbeTypedKind wire_kind, const TbeTypedType *object_type) {
  TbeTypedKind schema_kind;
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  if (kind == TBE_TYPED_OBJECT)
    return object_type != NULL && name != NULL && strcmp(object_type->name, name) == 0;
  if (kind != TBE_TYPED_ENUM)
    return typed_scalar_kind_from_name(name, &schema_kind) && schema_kind == kind;
  if (!data_bind_schema_find_type(codec, name, &schema_type) ||
      (schema_type.kind != DATA_BIND_SCHEMA_ENUM && schema_type.kind != DATA_BIND_SCHEMA_FLAGS) ||
      !typed_scalar_kind_from_name(schema_type.underlying_type, &schema_kind))
    return 0;
  return schema_kind == wire_kind;
}

static int typed_field_schema_matches(DataBind *codec, const TbeTypedField *field,
                                      const DataBindSchemaField *schema) {
  int descriptor_optional = (field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0;
  int descriptor_offset = (field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0;
  if (field->name == NULL || schema->name == NULL || strcmp(field->name, schema->name) != 0 ||
      descriptor_optional != (schema->is_optional != 0) ||
      descriptor_offset != (schema->has_offset != 0) ||
      (descriptor_offset && field->wire_offset != schema->offset))
    return 0;
  if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0)
    return schema->is_group && field->kind == TBE_TYPED_LIST && field->object_type != NULL &&
           schema->group_type != NULL && strcmp(field->object_type->name, schema->group_type) == 0;
  if (field->kind == TBE_TYPED_MAP)
    return schema->is_map && schema->key_type != NULL && strcmp(schema->key_type, "string") == 0 &&
           typed_named_kind_matches(codec, schema->value_type, field->map_value_kind,
                                    field->map_value_wire_kind, field->map_value_type);
  if (field->kind == TBE_TYPED_LIST || field->kind == TBE_TYPED_SET ||
      field->kind == TBE_TYPED_FIXED_ARRAY) {
    const char *expected_collection = field->kind == TBE_TYPED_FIXED_ARRAY
                                          ? "array"
                                          : (field->kind == TBE_TYPED_SET ? "set" : "list");
    return schema->is_collection && schema->inner_type != NULL &&
           (schema->collection_kind == NULL ||
            strcmp(schema->collection_kind, expected_collection) == 0) &&
           (field->kind != TBE_TYPED_FIXED_ARRAY || schema->is_fixed_size) &&
           typed_named_kind_matches(codec, schema->inner_type, field->element_kind,
                                    field->element_wire_kind, field->object_type);
  }
  if (field->kind == TBE_TYPED_FIXED_BYTES)
    return schema->type != NULL && strcmp(schema->type, "bytes") == 0 && schema->is_fixed_size &&
           schema->has_size_bytes && field->fixed_count == schema->size_bytes;
  return typed_named_kind_matches(codec, schema->type, field->kind, field->wire_kind,
                                  field->object_type);
}

static int typed_field_host_extent(const TbeTypedField *field, size_t *extent) {
  if (field == NULL || extent == NULL) return 0;
  switch (field->kind) {
  case TBE_TYPED_STRING:
    *extent = sizeof(tstr_t);
    return 1;
  case TBE_TYPED_BYTES:
  case TBE_TYPED_LIST:
  case TBE_TYPED_SET:
  case TBE_TYPED_MAP:
    *extent = sizeof(turbo_vec_t);
    return 1;
  case TBE_TYPED_FIXED_BYTES:
    *extent = field->fixed_count;
    return 1;
  case TBE_TYPED_OBJECT:
    if (field->object_type == NULL) return 0;
    *extent = field->object_type->size;
    return 1;
  case TBE_TYPED_FIXED_ARRAY:
    return field->element_size != 0 &&
           typed_multiply_fits(field->fixed_count, field->element_size, extent);
  default:
    *extent = typed_kind_size(field->kind);
    return *extent != 0;
  }
}

static int typed_value_host_extent(TbeTypedKind kind, const TbeTypedType *object_type,
                                   size_t *extent) {
  if (extent == NULL) return 0;
  if (kind == TBE_TYPED_STRING) *extent = sizeof(tstr_t);
  else if (kind == TBE_TYPED_BYTES) *extent = sizeof(turbo_vec_t);
  else if (kind == TBE_TYPED_OBJECT && object_type != NULL) *extent = object_type->size;
  else *extent = typed_kind_size(kind);
  return *extent != 0;
}

static int typed_field_wire_extent(const TbeTypedField *field, size_t *extent) {
  size_t element_wire_size;
  if (field == NULL || extent == NULL) return 0;
  if (field->kind == TBE_TYPED_OBJECT) {
    if (field->object_type == NULL) return 0;
    *extent = field->object_type->fixed_block_size;
    return 1;
  }
  if (field->kind == TBE_TYPED_FIXED_BYTES) {
    *extent = field->fixed_count;
    return 1;
  }
  if (field->kind == TBE_TYPED_FIXED_ARRAY) {
    if (field->element_kind == TBE_TYPED_OBJECT) {
      if (field->object_type == NULL) return 0;
      element_wire_size = field->object_type->fixed_block_size;
    } else {
      element_wire_size = typed_kind_size(
          field->element_kind == TBE_TYPED_ENUM ? field->element_wire_kind : field->element_kind);
    }
    return element_wire_size != 0 &&
           typed_multiply_fits(field->fixed_count, element_wire_size, extent);
  }
  *extent = typed_kind_size(field->kind == TBE_TYPED_ENUM ? field->wire_kind : field->kind);
  return *extent != 0;
}

static int typed_type_has_tail(const TbeTypedType *type) {
  size_t i;
  if (type == NULL) return 0;
  for (i = 0; i < type->field_count; ++i)
    if ((type->fields[i].flags & (TBE_TYPED_FIELD_GROUP | TBE_TYPED_FIELD_VAR_DATA)) != 0) return 1;
  return 0;
}

static DataBindStatus typed_validate_descriptor_at(const TbeTypedType *type, unsigned depth,
                                                   DataBindError *error) {
  size_t i;
  if (type == NULL || type->name == NULL || type->size == 0 ||
      (type->field_count != 0 && type->fields == NULL))
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed descriptor");
  if (depth > 32u)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed descriptor nesting exceeds the supported limit");
  if (!typed_size_fits(type->presence_offset, type->presence_size, type->size))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed presence bitmap exceeds the host object");
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    size_t host_extent;
    const TbeTypedType *nested_type = NULL;
    if (field->name == NULL || !typed_field_host_extent(field, &host_extent) ||
        !typed_size_fits(field->offset, host_extent, type->size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field exceeds the host object");
    if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0 &&
        (type->presence_size == 0 || field->optional_bit / 8u >= type->presence_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed optional bit exceeds the presence bitmap");
    if (field->kind == TBE_TYPED_OBJECT) {
      nested_type = field->object_type;
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
               field->kind == TBE_TYPED_SET) {
      size_t element_extent;
      if (!typed_value_host_extent(field->element_kind, field->object_type, &element_extent) ||
          field->element_size < element_extent)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed collection element exceeds its host storage");
      if (field->kind == TBE_TYPED_FIXED_ARRAY &&
          !typed_multiply_fits(field->fixed_count, field->element_size, &host_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed collection size exceeds the host address space");
      if (field->element_kind == TBE_TYPED_OBJECT) nested_type = field->object_type;
    } else if (field->kind == TBE_TYPED_MAP) {
      size_t value_extent;
      if (field->map_entry_size == 0 || field->element_size != field->map_entry_size ||
          !typed_size_fits(field->map_key_offset, sizeof(tstr_t), field->map_entry_size) ||
          !typed_value_host_extent(field->map_value_kind, field->map_value_type, &value_extent) ||
          !typed_size_fits(field->map_value_offset, value_extent, field->map_entry_size))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed map entry exceeds its host storage");
      if (field->map_value_kind == TBE_TYPED_OBJECT) nested_type = field->map_value_type;
    }
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0 &&
        (field->kind != TBE_TYPED_LIST || field->element_kind != TBE_TYPED_OBJECT ||
         field->object_type == NULL || field->element_size != field->object_type->size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed group descriptor is not an owning record vector");
    if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0 && field->kind != TBE_TYPED_STRING &&
        field->kind != TBE_TYPED_BYTES)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed variable data must be a string or byte vector");
    if (nested_type != NULL) {
      DataBindStatus status = typed_validate_descriptor_at(nested_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_validate_layout_at(const TbeTypedType *type, unsigned depth,
                                               DataBindError *error) {
  size_t i;
  DataBindStatus status = typed_validate_descriptor_at(type, depth, error);
  if (status != DATA_BIND_OK) return status;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    size_t wire_extent;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0 &&
        (!typed_field_wire_extent(field, &wire_extent) ||
         !typed_size_fits(field->wire_offset, wire_extent, type->fixed_block_size)))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field exceeds the fixed wire block");
    if (field->kind == TBE_TYPED_OBJECT) {
      if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 ||
          typed_type_has_tail(field->object_type))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Nested binary objects must have a fixed wire layout");
      status = typed_validate_layout_at(field->object_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY && field->element_kind == TBE_TYPED_OBJECT) {
      if (typed_type_has_tail(field->object_type))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Nested binary arrays must have a fixed wire layout");
      status = typed_validate_layout_at(field->object_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    } else if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      if (typed_type_has_tail(field->object_type))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed group entries must have a fixed wire layout");
      status = typed_validate_layout_at(field->object_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    } else if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 &&
               (field->flags & (TBE_TYPED_FIELD_GROUP | TBE_TYPED_FIELD_VAR_DATA)) == 0) {
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed binary field has no wire location");
    }
  }
  return DATA_BIND_OK;
}

static int typed_supports_direct_binary(const TbeTypedType *type) {
  size_t i;
  if (type == NULL) return 0;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    if (field->kind == TBE_TYPED_OBJECT ||
        (field->kind == TBE_TYPED_FIXED_ARRAY && field->element_kind == TBE_TYPED_OBJECT) ||
        (field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      if (!typed_supports_direct_binary(field->object_type)) return 0;
    }
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 &&
        (field->flags & (TBE_TYPED_FIELD_GROUP | TBE_TYPED_FIELD_VAR_DATA)) == 0)
      return 0;
  }
  return 1;
}

static DataBindStatus typed_validate_schema_at(DataBind *codec, const char *type_name,
                                               const TbeTypedType *type, unsigned depth,
                                               DataBindError *error) {
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  DataBindStatus status;
  size_t i;
  if (codec == NULL || type_name == NULL || type == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid typed schema validation arguments");
  if (depth > 32u)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Typed schema nesting exceeds the supported limit");
  status = typed_validate_descriptor_at(type, 0u, error);
  if (status != DATA_BIND_OK) return status;
  if (type->name == NULL || strcmp(type->name, type_name) != 0)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Typed descriptor name does not match the requested type");
  if (!data_bind_schema_find_type(codec, type_name, &schema_type))
    return typed_error(error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name,
                       "Typed schema type was not found");
  if ((schema_type.kind != DATA_BIND_SCHEMA_MESSAGE &&
       schema_type.kind != DATA_BIND_SCHEMA_COMPOSITE &&
       schema_type.kind != DATA_BIND_SCHEMA_GROUP) ||
      schema_type.field_count != type->field_count ||
      (schema_type.has_fixed_block_size &&
       schema_type.fixed_block_size != type->fixed_block_size) ||
      (!schema_type.has_fixed_block_size && type->fixed_block_size != 0))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Typed descriptor does not match the schema record");
  if (depth == 0u) {
    const char *byte_order = data_bind_schema_attribute_get(codec, "byte_order");
    int schema_big_endian = byte_order != NULL && strcmp(byte_order, "big") == 0;
    if ((type->wire_big_endian != 0) != schema_big_endian)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                         "Typed descriptor byte order does not match the schema");
  }
  for (i = 0; i < type->field_count; ++i) {
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    const TbeTypedField *field = &type->fields[i];
    const TbeTypedType *nested_type = NULL;
    const char *nested_name = NULL;
    if (!data_bind_schema_field_at(codec, type_name, i, &schema_field) ||
        !typed_field_schema_matches(codec, field, &schema_field))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field descriptor does not match the schema");
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      nested_type = field->object_type;
      nested_name = schema_field.group_type;
    } else if (field->kind == TBE_TYPED_OBJECT) {
      nested_type = field->object_type;
      nested_name = schema_field.type;
    } else if ((field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
                field->kind == TBE_TYPED_SET) &&
               field->element_kind == TBE_TYPED_OBJECT) {
      nested_type = field->object_type;
      nested_name = schema_field.inner_type;
    } else if (field->kind == TBE_TYPED_MAP && field->map_value_kind == TBE_TYPED_OBJECT) {
      nested_type = field->map_value_type;
      nested_name = schema_field.value_type;
    }
    if (nested_type != NULL) {
      status = typed_validate_schema_at(codec, nested_name, nested_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  if (error != NULL && error->size >= sizeof(*error)) error->code = DATA_BIND_OK;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_validate_schema(DataBind *codec, const char *type_name,
                                         const TbeTypedType *type, DataBindError *error) {
  return typed_validate_schema_at(codec, type_name, type, 0u, error);
}

static void typed_read_wire_scalar(TbeTypedKind kind, const uint8_t *source, int big_endian,
                                   void *output) {
  switch (kind) {
  case TBE_TYPED_BOOL:
    *(uint8_t *)output = (uint8_t)(tbe_wire_read_u8(source, big_endian) != 0);
    break;
  case TBE_TYPED_I8:
    *(int8_t *)output = tbe_wire_read_i8(source, big_endian);
    break;
  case TBE_TYPED_U8:
    *(uint8_t *)output = tbe_wire_read_u8(source, big_endian);
    break;
  case TBE_TYPED_I16:
    *(int16_t *)output = tbe_wire_read_i16(source, big_endian);
    break;
  case TBE_TYPED_U16:
    *(uint16_t *)output = tbe_wire_read_u16(source, big_endian);
    break;
  case TBE_TYPED_I32:
    *(int32_t *)output = tbe_wire_read_i32(source, big_endian);
    break;
  case TBE_TYPED_U32:
    *(uint32_t *)output = tbe_wire_read_u32(source, big_endian);
    break;
  case TBE_TYPED_I64:
    *(int64_t *)output = tbe_wire_read_i64(source, big_endian);
    break;
  case TBE_TYPED_U64:
    *(uint64_t *)output = tbe_wire_read_u64(source, big_endian);
    break;
  case TBE_TYPED_F32:
    *(float *)output = tbe_wire_read_f32(source, big_endian);
    break;
  case TBE_TYPED_F64:
    *(double *)output = tbe_wire_read_f64(source, big_endian);
    break;
  case TBE_TYPED_UUID:
    memcpy(((turbo_uuid_t *)output)->bytes, source, TURBO_UUID_SIZE);
    break;
  default:
    break;
  }
}

static void typed_read_enum(TbeTypedKind wire_kind, const uint8_t *source, int big_endian,
                            void *output) {
  int32_t value;
  switch (wire_kind) {
  case TBE_TYPED_I8:
    value = tbe_wire_read_i8(source, big_endian);
    break;
  case TBE_TYPED_U8:
    value = tbe_wire_read_u8(source, big_endian);
    break;
  case TBE_TYPED_I16:
    value = tbe_wire_read_i16(source, big_endian);
    break;
  case TBE_TYPED_U16:
    value = tbe_wire_read_u16(source, big_endian);
    break;
  case TBE_TYPED_U32:
    value = (int32_t)tbe_wire_read_u32(source, big_endian);
    break;
  default:
    value = tbe_wire_read_i32(source, big_endian);
    break;
  }
  *(int32_t *)output = value;
}

static DataBindStatus typed_read_fixed(const TbeTypedType *type, const uint8_t *data, size_t len,
                                       void *object, DataBindError *error) {
  size_t i;
  if (len < type->fixed_block_size)
    return typed_error(error, DATA_BIND_ERR_PARSE, type->name,
                       "Binary input is shorter than the fixed block");
  if (type->presence_size != 0)
    memcpy((uint8_t *)object + type->presence_offset, data, type->presence_size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    uint8_t *output = (uint8_t *)object + field->offset;
    const uint8_t *source;
    size_t j;
    size_t element_wire_size;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
    source = data + field->wire_offset;
    if (!typed_optional_present(type, object, field)) continue;
    if (field->kind == TBE_TYPED_OBJECT) {
      DataBindStatus status =
          typed_read_fixed(field->object_type, source, len - field->wire_offset, output, error);
      if (status != DATA_BIND_OK) return status;
    } else if (field->kind == TBE_TYPED_FIXED_BYTES) {
      memcpy(output, source, field->fixed_count);
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      element_wire_size =
          field->element_kind == TBE_TYPED_OBJECT
              ? field->object_type->fixed_block_size
              : typed_kind_size(field->element_kind == TBE_TYPED_ENUM ? field->element_wire_kind
                                                                      : field->element_kind);
      for (j = 0; j < field->fixed_count; ++j) {
        void *element = output + j * field->element_size;
        const uint8_t *element_source = source + j * element_wire_size;
        if (field->element_kind == TBE_TYPED_OBJECT) {
          DataBindStatus status = typed_read_fixed(field->object_type, element_source,
                                                   element_wire_size, element, error);
          if (status != DATA_BIND_OK) return status;
        } else if (field->element_kind == TBE_TYPED_ENUM) {
          typed_read_enum(field->element_wire_kind, element_source, type->wire_big_endian, element);
        } else {
          typed_read_wire_scalar(field->element_kind, element_source, type->wire_big_endian,
                                 element);
        }
      }
    } else if (field->kind == TBE_TYPED_ENUM) {
      typed_read_enum(field->wire_kind, source, type->wire_big_endian, output);
    } else {
      typed_read_wire_scalar(field->kind, source, type->wire_big_endian, output);
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_read_tail(const TbeTypedType *type, const uint8_t *data, size_t len,
                                      void *object, DataBindError *error) {
  size_t cursor = type->fixed_block_size;
  size_t i;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    uint8_t *output = (uint8_t *)object + field->offset;
    int present = typed_optional_present(type, object, field);
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      turbo_vec_t *vec = (turbo_vec_t *)output;
      uint16_t block_length;
      uint16_t count;
      size_t payload_size;
      size_t host_payload_size;
      size_t j;
      if (!typed_size_fits(cursor, 4u, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary group header is truncated");
      block_length = tbe_wire_read_u16(data + cursor, type->wire_big_endian);
      count = tbe_wire_read_u16(data + cursor + 2u, type->wire_big_endian);
      cursor += 4u;
      if (block_length < field->object_type->fixed_block_size ||
          !typed_multiply_fits(count, block_length, &payload_size) ||
          !typed_size_fits(cursor, payload_size, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary group payload is invalid");
      if (present) {
        if (!typed_multiply_fits(count, field->element_size, &host_payload_size))
          return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                             "Typed group size exceeds the host address space");
        if (turbo_vec_resize(vec, count) != TURBO_OK)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory resizing typed group");
        memset(vec->data, 0, host_payload_size);
        for (j = 0; j < count; ++j) {
          void *element = (uint8_t *)vec->data + j * field->element_size;
          DataBindStatus status = tbe_typed_init(field->object_type, element, error);
          if (status == DATA_BIND_OK)
            status = typed_read_fixed(field->object_type, data + cursor + j * block_length,
                                      block_length, element, error);
          if (status != DATA_BIND_OK) return status;
        }
      }
      cursor += payload_size;
    } else if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0) {
      uint32_t value_size;
      if (!typed_size_fits(cursor, 4u, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary variable-data header is truncated");
      value_size = tbe_wire_read_u32(data + cursor, type->wire_big_endian);
      cursor += 4u;
      if (!typed_size_fits(cursor, value_size, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary variable-data payload is truncated");
      if (present && field->kind == TBE_TYPED_STRING) {
        *(tstr_t *)output = tstr_dup_len((const char *)data + cursor, value_size);
        if (*(tstr_t *)output == NULL)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory copying typed string");
      } else if (present) {
        turbo_vec_t *vec = (turbo_vec_t *)output;
        if (turbo_vec_resize(vec, value_size) != TURBO_OK)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory copying typed bytes");
        if (value_size != 0) memcpy(vec->data, data + cursor, value_size);
      }
      cursor += value_size;
    }
  }
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_parse_binary(const TbeTypedType *type, const void *data, size_t len,
                                      void *object, DataBindError *error) {
  void *temporary;
  DataBindStatus status;
  if (type == NULL || data == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                       "Invalid typed binary parse arguments");
  status = typed_validate_layout_at(type, 0u, error);
  if (status != DATA_BIND_OK) return status;
  temporary = calloc(1, type->size);
  if (temporary == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, type->name, "Out of memory creating typed object");
  status = tbe_typed_init(type, temporary, error);
  if (status == DATA_BIND_OK)
    status = typed_read_fixed(type, (const uint8_t *)data, len, temporary, error);
  if (status == DATA_BIND_OK)
    status = typed_read_tail(type, (const uint8_t *)data, len, temporary, error);
  if (status == DATA_BIND_OK) {
    tbe_typed_clear(type, object);
    memcpy(object, temporary, type->size);
    memset(temporary, 0, type->size);
    if (error != NULL && error->size >= sizeof(*error)) error->code = DATA_BIND_OK;
  }
  tbe_typed_clear(type, temporary);
  free(temporary);
  return status;
}

DataBindStatus tbe_typed_parse(DataBind *codec, const char *type_name, const TbeTypedType *type,
                               const char *format, const void *data, size_t len, size_t row,
                               void *object, DataBindError *error) {
  DataBindValue *value = NULL;
  void *temporary;
  DataBindStatus status;
  if (codec == NULL || type_name == NULL || type == NULL || format == NULL || data == NULL ||
      object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed parse arguments");
  if (strcmp(format, "bin") == 0) {
    status = tbe_typed_validate_schema(codec, type_name, type, error);
    if (status != DATA_BIND_OK) return status;
    if (typed_supports_direct_binary(type))
      return tbe_typed_parse_binary(type, data, len, object, error);
    status = data_bind_parse(codec, type_name, (const uint8_t *)data, len, &value, error);
  } else if (strcmp(format, "json") == 0)
    status = data_bind_parse_json(codec, type_name, (const char *)data, len, &value, error);
  else if (strcmp(format, "yaml") == 0)
    status = data_bind_parse_yaml(codec, type_name, (const char *)data, len, &value, error);
  else if (strcmp(format, "csv") == 0)
    status = data_bind_parse_csv(codec, type_name, (const char *)data, len, row, &value, error);
  else if (strcmp(format, "xml") == 0)
    status = data_bind_parse_xml(codec, type_name, (const char *)data, len, &value, error);
  else return typed_error(error, DATA_BIND_ERR_INVALID_ARG, format, "Unknown typed input format");
  if (status != DATA_BIND_OK) return status;
  temporary = calloc(1, type->size);
  if (temporary == NULL) {
    data_bind_value_free(value);
    return typed_error(error, DATA_BIND_ERR_OOM, type_name, "Out of memory creating typed object");
  }
  status = tbe_typed_init(type, temporary, error);
  if (status == DATA_BIND_OK) status = tbe_typed_from_value(type, value, temporary, error);
  data_bind_value_free(value);
  if (status == DATA_BIND_OK) {
    tbe_typed_clear(type, object);
    memcpy(object, temporary, type->size);
    memset(temporary, 0, type->size);
  }
  tbe_typed_clear(type, temporary);
  free(temporary);
  return status;
}

static int csv_append_escaped(tstr_t *out, const char *text, size_t len) {
  size_t i;
  int quote = 0;
  for (i = 0; i < len; ++i)
    if (text[i] == ',' || text[i] == '"' || text[i] == '\r' || text[i] == '\n') quote = 1;
  if (quote) *out = tstr_cat(*out, "\"");
  for (i = 0; *out != NULL && i < len; ++i) {
    if (text[i] == '"') *out = tstr_cat(*out, "\"");
    *out = tstr_cat_len(*out, &text[i], 1);
  }
  if (quote && *out != NULL) *out = tstr_cat(*out, "\"");
  return *out != NULL;
}

static int csv_flatten(const json_value_t *value, const char *path, tstr_t *headers, tstr_t *values,
                       int *first) {
  turbo_json_type_t kind = turbo_json_type(value);
  size_t i;
  if (kind == TURBO_JSON_OBJECT) {
    for (i = 0; i < turbo_json_object_size(value); ++i) {
      const char *key = turbo_json_object_key(value, i);
      const json_value_t *child = turbo_json_object_value(value, i);
      tstr_t child_path = path && path[0] ? tstr_format("{}.{}", path, key) : tstr_dup(key);
      int ok = child_path != NULL && csv_flatten(child, child_path, headers, values, first);
      tstr_free(child_path);
      if (!ok) return 0;
    }
    return 1;
  }
  if (kind == TURBO_JSON_ARRAY) {
    for (i = 0; i < turbo_json_array_size(value); ++i) {
      tstr_t child_path = tstr_format("{}[{}]", path ? path : "", i);
      int ok = child_path != NULL &&
               csv_flatten(turbo_json_array_get(value, i), child_path, headers, values, first);
      tstr_free(child_path);
      if (!ok) return 0;
    }
    return 1;
  }
  if (!*first) {
    *headers = tstr_cat(*headers, ",");
    *values = tstr_cat(*values, ",");
  }
  *first = 0;
  if (!csv_append_escaped(headers, path ? path : "", path ? strlen(path) : 0)) return 0;
  if (kind == TURBO_JSON_STRING) {
    const char *text = turbo_json_string(value);
    return csv_append_escaped(values, text, turbo_json_string_len(value));
  }
  if (kind == TURBO_JSON_BOOL)
    return csv_append_escaped(values, turbo_json_bool(value) ? "true" : "false",
                              turbo_json_bool(value) ? 4 : 5);
  if (kind == TURBO_JSON_NULL) return 1;
  {
    const char *exact;
    size_t exact_len = 0;
    char number[64];
    exact = turbo_json_number_text(value, &exact_len);
    if (exact != NULL) return csv_append_escaped(values, exact, exact_len);
    int len = snprintf(number, sizeof(number), "%.17g", turbo_json_number(value));
    return len > 0 && csv_append_escaped(values, number, (size_t)len);
  }
}

DataBindStatus tbe_typed_serialize(DataBind *codec, const char *type_name, const TbeTypedType *type,
                                   const void *object, const char *format, char **out,
                                   size_t *out_len, DataBindError *error) {
  json_value_t *json;
  char *json_text = NULL;
  size_t json_len = 0;
  DataBindStatus status = DATA_BIND_OK;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0;
  if (type == NULL || object == NULL || format == NULL || out == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed serialize arguments");
  json = tbe_typed_to_json(type, object, error);
  if (json == NULL) return error ? error->code : DATA_BIND_ERR_TYPE_MISMATCH;
  if (strcmp(format, "csv") == 0) {
    tstr_t headers = tstr_new();
    tstr_t values = tstr_new();
    int first = 1;
    if (headers == NULL || values == NULL || !csv_flatten(json, "", &headers, &values, &first)) {
      tstr_free(headers);
      tstr_free(values);
      turbo_free_json(&json);
      return typed_error(error, DATA_BIND_ERR_OOM, "csv", "Failed to serialize typed CSV");
    }
    headers = tstr_cat(headers, "\n");
    headers = tstr_cat_str(headers, values);
    headers = tstr_cat(headers, "\n");
    tstr_free(values);
    if (headers == NULL) {
      turbo_free_json(&json);
      return typed_error(error, DATA_BIND_ERR_OOM, "csv", "Failed to serialize typed CSV");
    }
    *out = tstr_to_cstr(headers);
    if (out_len != NULL) *out_len = tstr_len(headers);
    tstr_free(headers);
    turbo_free_json(&json);
    return *out != NULL
               ? DATA_BIND_OK
               : typed_error(error, DATA_BIND_ERR_OOM, "csv", "Out of memory returning typed CSV");
  }
  json_text = turbo_json_serialize(json, &json_len);
  turbo_free_json(&json);
  if (json_text == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, format, "Failed to serialize typed JSON");
  if (strcmp(format, "json") == 0) {
    *out = json_text;
    if (out_len != NULL) *out_len = json_len;
    return DATA_BIND_OK;
  }
  if (codec == NULL || type_name == NULL) {
    turbo_json_serialize_free(json_text);
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, format,
                       "A schema codec is required for this format");
  }
  {
    DataBindObject *bound = NULL;
    status = data_bind_object_from_json(codec, type_name, json_text, json_len, &bound, error);
    turbo_json_serialize_free(json_text);
    if (status != DATA_BIND_OK) return status;
    if (strcmp(format, "yaml") == 0)
      status = data_bind_object_serialize_yaml(bound, out, out_len, error);
    else if (strcmp(format, "xml") == 0)
      status = data_bind_object_serialize_xml(bound, out, out_len, error);
    else
      status = typed_error(error, DATA_BIND_ERR_INVALID_ARG, format, "Unknown typed output format");
    data_bind_object_free(bound);
  }
  return status;
}

static size_t typed_binary_size(const TbeTypedType *type, const void *object, int *supported) {
  size_t total = type->fixed_block_size;
  size_t i;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const void *ptr = (const uint8_t *)object + field->offset;
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      const turbo_vec_t *vec = (const turbo_vec_t *)ptr;
      if (field->object_type == NULL || field->object_type->fixed_block_size > UINT16_MAX ||
          vec->size > UINT16_MAX) {
        *supported = 0;
        return 0;
      }
      if (vec->size > (SIZE_MAX - total - 4u) / field->object_type->fixed_block_size) {
        *supported = 0;
        return 0;
      }
      total += 4u + vec->size * field->object_type->fixed_block_size;
    } else if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0) {
      size_t len = field->kind == TBE_TYPED_STRING
                       ? (*(const tstr_t *)ptr ? tstr_len(*(const tstr_t *)ptr) : 0)
                       : ((const turbo_vec_t *)ptr)->size;
      if (len > UINT32_MAX || total > SIZE_MAX - 4u - len) {
        *supported = 0;
        return 0;
      }
      total += 4u + len;
    } else if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 &&
               field->kind != TBE_TYPED_OBJECT) {
      *supported = 0;
      return 0;
    }
  }
  return total;
}

static void typed_write_scalar(TbeTypedKind kind, uint8_t *dst, int big_endian, const void *src) {
  switch (kind) {
  case TBE_TYPED_BOOL:
  case TBE_TYPED_U8:
    tbe_wire_write_u8(dst, big_endian, *(const uint8_t *)src);
    break;
  case TBE_TYPED_I8:
    tbe_wire_write_i8(dst, big_endian, *(const int8_t *)src);
    break;
  case TBE_TYPED_U16:
    tbe_wire_write_u16(dst, big_endian, *(const uint16_t *)src);
    break;
  case TBE_TYPED_I16:
    tbe_wire_write_i16(dst, big_endian, *(const int16_t *)src);
    break;
  case TBE_TYPED_U32:
    tbe_wire_write_u32(dst, big_endian, *(const uint32_t *)src);
    break;
  case TBE_TYPED_I32:
  case TBE_TYPED_ENUM:
    tbe_wire_write_i32(dst, big_endian, *(const int32_t *)src);
    break;
  case TBE_TYPED_U64:
    tbe_wire_write_u64(dst, big_endian, *(const uint64_t *)src);
    break;
  case TBE_TYPED_I64:
    tbe_wire_write_i64(dst, big_endian, *(const int64_t *)src);
    break;
  case TBE_TYPED_F32:
    tbe_wire_write_f32(dst, big_endian, *(const float *)src);
    break;
  case TBE_TYPED_F64:
    tbe_wire_write_f64(dst, big_endian, *(const double *)src);
    break;
  case TBE_TYPED_UUID:
    memcpy(dst, ((const turbo_uuid_t *)src)->bytes, TURBO_UUID_SIZE);
    break;
  default:
    break;
  }
}

static void typed_write_enum(TbeTypedKind wire_kind, uint8_t *dst, int big_endian,
                             const void *src) {
  int32_t value = *(const int32_t *)src;
  switch (wire_kind) {
  case TBE_TYPED_I8: {
    int8_t converted = (int8_t)value;
    typed_write_scalar(wire_kind, dst, big_endian, &converted);
    break;
  }
  case TBE_TYPED_U8: {
    uint8_t converted = (uint8_t)value;
    typed_write_scalar(wire_kind, dst, big_endian, &converted);
    break;
  }
  case TBE_TYPED_I16: {
    int16_t converted = (int16_t)value;
    typed_write_scalar(wire_kind, dst, big_endian, &converted);
    break;
  }
  case TBE_TYPED_U16: {
    uint16_t converted = (uint16_t)value;
    typed_write_scalar(wire_kind, dst, big_endian, &converted);
    break;
  }
  case TBE_TYPED_U32: {
    uint32_t converted = (uint32_t)value;
    typed_write_scalar(wire_kind, dst, big_endian, &converted);
    break;
  }
  default:
    typed_write_scalar(TBE_TYPED_I32, dst, big_endian, &value);
    break;
  }
}

static int typed_write_fixed(const TbeTypedType *type, const void *object, uint8_t *dst,
                             size_t size) {
  size_t i;
  if (size < type->fixed_block_size) return 0;
  if (type->presence_size != 0)
    memcpy(dst, (const uint8_t *)object + type->presence_offset, type->presence_size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const uint8_t *src = (const uint8_t *)object + field->offset;
    size_t j;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
    if (field->kind == TBE_TYPED_OBJECT) {
      if (!typed_write_fixed(field->object_type, src, dst + field->wire_offset,
                             size - field->wire_offset))
        return 0;
    } else if (field->kind == TBE_TYPED_FIXED_BYTES) {
      memcpy(dst + field->wire_offset, src, field->fixed_count);
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      for (j = 0; j < field->fixed_count; ++j) {
        if (field->element_kind == TBE_TYPED_OBJECT) {
          if (!typed_write_fixed(
                  field->object_type, src + j * field->element_size,
                  dst + field->wire_offset + j * field->object_type->fixed_block_size,
                  size - field->wire_offset - j * field->object_type->fixed_block_size))
            return 0;
        } else {
          TbeTypedKind wire_kind = field->element_kind == TBE_TYPED_ENUM ? field->element_wire_kind
                                                                         : field->element_kind;
          uint8_t *element_dst = dst + field->wire_offset + j * typed_kind_size(wire_kind);
          const void *element_src = src + j * field->element_size;
          if (field->element_kind == TBE_TYPED_ENUM)
            typed_write_enum(wire_kind, element_dst, type->wire_big_endian, element_src);
          else typed_write_scalar(wire_kind, element_dst, type->wire_big_endian, element_src);
        }
      }
    } else {
      if (field->kind == TBE_TYPED_ENUM)
        typed_write_enum(field->wire_kind, dst + field->wire_offset, type->wire_big_endian, src);
      else typed_write_scalar(field->kind, dst + field->wire_offset, type->wire_big_endian, src);
    }
  }
  return 1;
}

DataBindStatus tbe_typed_serialize_binary_into(const TbeTypedType *type, const void *object,
                                               uint8_t *output, size_t capacity, size_t *out_len,
                                               DataBindError *error) {
  size_t total;
  size_t cursor;
  size_t i;
  int supported = 1;
  if (out_len != NULL) *out_len = 0;
  if (type == NULL || object == NULL || output == NULL || out_len == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                       "Invalid typed binary output arguments");
  total = typed_binary_size(type, object, &supported);
  if (!supported || total == 0)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Schema does not define a supported binary layout");
  *out_len = total;
  if (capacity < total)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type->name,
                       "Binary output buffer is too small");
  memset(output, 0, total);
  if (!typed_write_fixed(type, object, output, total)) {
    return typed_error(error, DATA_BIND_ERR_RUNTIME, type->name,
                       "Failed to write fixed binary fields");
  }
  cursor = type->fixed_block_size;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const void *ptr = (const uint8_t *)object + field->offset;
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      const turbo_vec_t *vec = (const turbo_vec_t *)ptr;
      size_t j;
      tbe_wire_write_u16(output + cursor, type->wire_big_endian,
                         (uint16_t)field->object_type->fixed_block_size);
      tbe_wire_write_u16(output + cursor + 2u, type->wire_big_endian, (uint16_t)vec->size);
      cursor += 4u;
      for (j = 0; j < vec->size; ++j) {
        if (!typed_write_fixed(field->object_type,
                               (const uint8_t *)vec->data + j * field->element_size,
                               output + cursor, total - cursor)) {
          return typed_error(error, DATA_BIND_ERR_RUNTIME, field->name,
                             "Failed to write binary group");
        }
        cursor += field->object_type->fixed_block_size;
      }
    } else if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0) {
      const void *bytes;
      size_t len;
      if (field->kind == TBE_TYPED_STRING) {
        tstr_t text = *(const tstr_t *)ptr;
        bytes = text ? text : "";
        len = text ? tstr_len(text) : 0;
      } else {
        const turbo_vec_t *vec = (const turbo_vec_t *)ptr;
        bytes = vec->data;
        len = vec->size;
      }
      tbe_wire_write_u32(output + cursor, type->wire_big_endian, (uint32_t)len);
      if (len != 0) memcpy(output + cursor + 4u, bytes, len);
      cursor += 4u + len;
    }
  }
  if (error != NULL && error->size >= sizeof(*error)) error->code = DATA_BIND_OK;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_serialize_binary(const TbeTypedType *type, const void *object,
                                          uint8_t **out, size_t *out_len, DataBindError *error) {
  uint8_t *data;
  size_t total;
  int supported = 1;
  DataBindStatus status;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0;
  if (type == NULL || object == NULL || out == NULL || out_len == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                       "Invalid typed binary serialize arguments");
  total = typed_binary_size(type, object, &supported);
  if (!supported || total == 0)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Schema does not define a supported binary layout");
  data = (uint8_t *)malloc(total);
  if (data == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, type->name,
                       "Out of memory serializing binary object");
  status = tbe_typed_serialize_binary_into(type, object, data, total, out_len, error);
  if (status != DATA_BIND_OK) {
    free(data);
    return status;
  }
  *out = data;
  return DATA_BIND_OK;
}

void tbe_typed_serialized_free(void *data) { free(data); }
