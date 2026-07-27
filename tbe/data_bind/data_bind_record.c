#include "data_bind.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static void record_error_clear(DataBindError *error) {
  if (error == NULL || error->size < sizeof(*error)) return;
  error->code = DATA_BIND_OK;
  error->line = -1;
  error->column = -1;
  error->path[0] = '\0';
  error->message[0] = '\0';
}

static DataBindStatus record_error(DataBindError *error, DataBindStatus status, const char *path,
                                   const char *message) {
  if (error != NULL && error->size >= sizeof(*error)) {
    error->code = status;
    error->line = -1;
    error->column = -1;
    if (path != NULL) snprintf(error->path, sizeof(error->path), "%s", path);
    else error->path[0] = '\0';
    snprintf(error->message, sizeof(error->message), "%s", message);
  }
  return status;
}

static int record_field_output_valid(const DataBindRecordField *field) {
  return field != NULL && field->size >= sizeof(*field);
}

static DataBindStatus record_find_field_at(const DataBindValue *object, const char *name,
                                           DataBindRecordField *out_field, DataBindError *error) {
  size_t count;
  size_t i;
  DataBindRecordField result;
  if (object == NULL || name == NULL || !record_field_output_valid(out_field))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, name,
                        "Invalid record field lookup arguments");
  if (data_bind_value_kind(object) != DATA_BIND_VALUE_OBJECT)
    return record_error(error, DATA_BIND_ERR_TYPE_MISMATCH, name, "Record value is not an object");
  count = data_bind_value_field_count(object);
  for (i = 0; i < count; ++i) {
    const char *field_name = data_bind_value_field_name(object, i);
    if (field_name != NULL && strcmp(field_name, name) == 0) {
      result.size = out_field->size;
      result.owner = object;
      result.value = data_bind_value_field_at(object, i);
      result.name = field_name;
      result.index = i;
      *out_field = result;
      record_error_clear(error);
      return DATA_BIND_OK;
    }
  }
  return record_error(error, DATA_BIND_ERR_TYPE_MISMATCH, name, "Record field was not found");
}

static const DataBindValue *record_field_resolve(const DataBindRecordField *field) {
  DataBindValueKind owner_kind;
  if (!record_field_output_valid(field) || field->owner == NULL || field->value == NULL)
    return NULL;
  owner_kind = data_bind_value_kind(field->owner);
  if (owner_kind == DATA_BIND_VALUE_OBJECT)
    return data_bind_value_field_at(field->owner, field->index) == field->value ? field->value
                                                                                : NULL;
  if (owner_kind == DATA_BIND_VALUE_LIST || owner_kind == DATA_BIND_VALUE_SET)
    return data_bind_value_at(field->owner, field->index) == field->value ? field->value : NULL;
  if (owner_kind == DATA_BIND_VALUE_MAP) {
    DataBindMapEntry entry = data_bind_value_map_entry_at(field->owner, field->index);
    return entry.value == field->value ? field->value : NULL;
  }
  return NULL;
}

static DataBindStatus record_field_value_checked(const DataBindRecordField *field,
                                                 const DataBindValue **out, DataBindError *error) {
  const DataBindValue *value;
  if (out == NULL)
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid record field output");
  value = record_field_resolve(field);
  if (value == NULL)
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid record field handle");
  *out = value;
  return DATA_BIND_OK;
}

static DataBindStatus record_field_type_error(const DataBindRecordField *field,
                                              DataBindError *error) {
  return record_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field != NULL ? field->name : NULL,
                      "Record field does not match the requested type");
}

DataBindStatus data_bind_record_from_json(DataBind *codec, const char *type_name, const char *json,
                                          size_t len, DataBindRecord **out_record,
                                          DataBindError *error) {
  return data_bind_object_from_json(codec, type_name, json, len, out_record, error);
}

DataBindStatus data_bind_record_from_yaml(DataBind *codec, const char *type_name, const char *yaml,
                                          size_t len, DataBindRecord **out_record,
                                          DataBindError *error) {
  return data_bind_object_from_yaml(codec, type_name, yaml, len, out_record, error);
}

DataBindStatus data_bind_record_from_xml(DataBind *codec, const char *type_name, const char *xml,
                                         size_t len, DataBindRecord **out_record,
                                         DataBindError *error) {
  return data_bind_object_from_xml(codec, type_name, xml, len, out_record, error);
}

DataBindStatus data_bind_record_from_csv(DataBind *codec, const char *type_name, const char *csv,
                                         size_t len, size_t row, DataBindRecord **out_record,
                                         DataBindError *error) {
  return data_bind_object_from_csv(codec, type_name, csv, len, row, out_record, error);
}

const char *data_bind_record_type_name(const DataBindRecord *record) {
  return data_bind_object_type_name(record);
}

void data_bind_record_free(DataBindRecord *record) { data_bind_object_free(record); }

DataBindStatus data_bind_record_serialize_bin(DataBind *codec, const DataBindRecord *record,
                                              uint8_t **out_bin, size_t *out_len,
                                              DataBindError *error) {
  return data_bind_object_serialize_bin(codec, record, out_bin, out_len, error);
}

DataBindStatus data_bind_record_serialize_bin_into(DataBind *codec, const DataBindRecord *record,
                                                   uint8_t *output, size_t capacity,
                                                   size_t *out_len, DataBindError *error) {
  return data_bind_object_serialize_bin_into(codec, record, output, capacity, out_len, error);
}

DataBindStatus data_bind_record_serialize_json(DataBind *codec, const DataBindRecord *record,
                                               char **out_json, size_t *out_len,
                                               DataBindError *error) {
  return data_bind_object_serialize_json(codec, record, out_json, out_len, error);
}

DataBindStatus data_bind_record_serialize_yaml(DataBind *codec, const DataBindRecord *record,
                                               char **out_yaml, size_t *out_len,
                                               DataBindError *error) {
  return data_bind_object_serialize_yaml(codec, record, out_yaml, out_len, error);
}

DataBindStatus data_bind_record_serialize_xml(DataBind *codec, const DataBindRecord *record,
                                              char **out_xml, size_t *out_len,
                                              DataBindError *error) {
  return data_bind_object_serialize_xml(codec, record, out_xml, out_len, error);
}

DataBindStatus data_bind_record_serialize_csv(DataBind *codec, const DataBindRecord *record,
                                              char **out_csv, size_t *out_len,
                                              DataBindError *error) {
  return data_bind_object_serialize_csv(codec, record, out_csv, out_len, error);
}

DataBindStatus data_bind_record_find_field(const DataBindRecord *record, const char *name,
                                           DataBindRecordField *out_field, DataBindError *error) {
  if (record == NULL) return record_error(error, DATA_BIND_ERR_INVALID_ARG, name, "Record is NULL");
  return record_find_field_at(data_bind_object_value(record), name, out_field, error);
}

DataBindStatus data_bind_record_view_find_field(const DataBindRecordView *view, const char *name,
                                                DataBindRecordField *out_field,
                                                DataBindError *error) {
  if (view == NULL || view->size < sizeof(*view))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, name, "Invalid record object view");
  return record_find_field_at(view->value, name, out_field, error);
}

const DataBindValue *data_bind_record_field_value(const DataBindRecordField *field) {
  return record_field_resolve(field);
}

#define RECORD_FIELD_SCALAR_GETTER(suffix, type, value_getter)                                     \
  DataBindStatus data_bind_record_field_get_##suffix(const DataBindRecordField *field, type *out,  \
                                                     DataBindError *error) {                       \
    const DataBindValue *value;                                                                    \
    DataBindStatus status;                                                                         \
    if (out == NULL)                                                                               \
      return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,    \
                          "Invalid record field output");                                          \
    status = record_field_value_checked(field, &value, error);                                     \
    if (status != DATA_BIND_OK) return status;                                                     \
    status = value_getter(value, out);                                                             \
    if (status != DATA_BIND_OK) return record_field_type_error(field, error);                      \
    record_error_clear(error);                                                                     \
    return DATA_BIND_OK;                                                                           \
  }

RECORD_FIELD_SCALAR_GETTER(i32, int32_t, data_bind_value_get_int32)
RECORD_FIELD_SCALAR_GETTER(i64, int64_t, data_bind_value_get_int64)
RECORD_FIELD_SCALAR_GETTER(u64, uint64_t, data_bind_value_get_uint64)
RECORD_FIELD_SCALAR_GETTER(double, double, data_bind_value_get_double)
RECORD_FIELD_SCALAR_GETTER(bool, int, data_bind_value_get_bool)

DataBindStatus data_bind_record_field_get_u32(const DataBindRecordField *field, uint32_t *out,
                                              DataBindError *error) {
  const DataBindValue *value;
  uint64_t converted;
  DataBindStatus status;
  if (out == NULL)
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid record field output");
  status = record_field_value_checked(field, &value, error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_value_get_uint64(value, &converted);
  if (status != DATA_BIND_OK || converted > UINT32_MAX)
    return record_field_type_error(field, error);
  *out = (uint32_t)converted;
  record_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_record_field_get_uuid(const DataBindRecordField *field,
                                               uint8_t out[DATA_BIND_UUID_SIZE],
                                               DataBindError *error) {
  const DataBindValue *value;
  DataBindStatus status;
  if (out == NULL)
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid record UUID output");
  status = record_field_value_checked(field, &value, error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_value_get_uuid(value, out);
  if (status != DATA_BIND_OK) return record_field_type_error(field, error);
  record_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_record_field_get_string(const DataBindRecordField *field,
                                                 DataBindStringView *out, DataBindError *error) {
  const DataBindValue *value;
  const char *data;
  size_t length;
  DataBindStringView result;
  DataBindStatus status;
  if (out == NULL || out->size < sizeof(*out))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid record string view");
  status = record_field_value_checked(field, &value, error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_value_get_string(value, &data, &length);
  if (status != DATA_BIND_OK) return record_field_type_error(field, error);
  result.size = out->size;
  result.data = data;
  result.length = length;
  *out = result;
  record_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_record_field_get_bytes(const DataBindRecordField *field,
                                                DataBindBytesView *out, DataBindError *error) {
  const DataBindValue *value;
  const uint8_t *data;
  size_t length;
  DataBindBytesView result;
  DataBindStatus status;
  if (out == NULL || out->size < sizeof(*out))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid record bytes view");
  status = record_field_value_checked(field, &value, error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_value_get_bytes(value, &data, &length);
  if (status != DATA_BIND_OK) return record_field_type_error(field, error);
  result.size = out->size;
  result.data = data;
  result.length = length;
  *out = result;
  record_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_record_field_get_object(const DataBindRecordField *field,
                                                 DataBindRecordView *out, DataBindError *error) {
  const DataBindValue *value;
  DataBindRecordView result;
  DataBindStatus status;
  if (out == NULL || out->size < sizeof(*out))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid nested record view");
  status = record_field_value_checked(field, &value, error);
  if (status != DATA_BIND_OK) return status;
  if (data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT)
    return record_field_type_error(field, error);
  result.size = out->size;
  result.value = value;
  *out = result;
  record_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_record_field_get_list(const DataBindRecordField *field,
                                               DataBindListView *out, DataBindError *error) {
  const DataBindValue *value;
  DataBindValueKind kind;
  DataBindListView result;
  DataBindStatus status;
  if (out == NULL || out->size < sizeof(*out))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid record list view");
  status = record_field_value_checked(field, &value, error);
  if (status != DATA_BIND_OK) return status;
  kind = data_bind_value_kind(value);
  if (kind != DATA_BIND_VALUE_LIST && kind != DATA_BIND_VALUE_SET)
    return record_field_type_error(field, error);
  result.size = out->size;
  result.value = value;
  *out = result;
  record_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_record_field_get_map(const DataBindRecordField *field,
                                              DataBindRecordMapView *out, DataBindError *error) {
  const DataBindValue *value;
  DataBindRecordMapView result;
  DataBindStatus status;
  if (out == NULL || out->size < sizeof(*out))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, field != NULL ? field->name : NULL,
                        "Invalid record map view");
  status = record_field_value_checked(field, &value, error);
  if (status != DATA_BIND_OK) return status;
  if (data_bind_value_kind(value) != DATA_BIND_VALUE_MAP)
    return record_field_type_error(field, error);
  result.size = out->size;
  result.value = value;
  *out = result;
  record_error_clear(error);
  return DATA_BIND_OK;
}

#define RECORD_NAMED_GETTER(name, type)                                                            \
  DataBindStatus data_bind_record_get_##name(const DataBindRecord *record, const char *field_name, \
                                             type *out, DataBindError *error) {                    \
    DataBindRecordField field = DATA_BIND_RECORD_FIELD_INIT;                                       \
    DataBindStatus status = data_bind_record_find_field(record, field_name, &field, error);        \
    return status == DATA_BIND_OK ? data_bind_record_field_get_##name(&field, out, error)          \
                                  : status;                                                        \
  }

RECORD_NAMED_GETTER(i32, int32_t)
RECORD_NAMED_GETTER(u32, uint32_t)
RECORD_NAMED_GETTER(i64, int64_t)
RECORD_NAMED_GETTER(u64, uint64_t)
RECORD_NAMED_GETTER(double, double)
RECORD_NAMED_GETTER(bool, int)
RECORD_NAMED_GETTER(string, DataBindStringView)
RECORD_NAMED_GETTER(bytes, DataBindBytesView)
RECORD_NAMED_GETTER(object, DataBindRecordView)
RECORD_NAMED_GETTER(list, DataBindListView)
RECORD_NAMED_GETTER(map, DataBindRecordMapView)

DataBindStatus data_bind_record_get_uuid(const DataBindRecord *record, const char *name,
                                         uint8_t out[DATA_BIND_UUID_SIZE], DataBindError *error) {
  DataBindRecordField field = DATA_BIND_RECORD_FIELD_INIT;
  DataBindStatus status = data_bind_record_find_field(record, name, &field, error);
  return status == DATA_BIND_OK ? data_bind_record_field_get_uuid(&field, out, error) : status;
}

size_t data_bind_list_view_count(const DataBindListView *view) {
  DataBindValueKind kind;
  if (view == NULL || view->size < sizeof(*view) || view->value == NULL) return 0;
  kind = data_bind_value_kind(view->value);
  if (kind != DATA_BIND_VALUE_LIST && kind != DATA_BIND_VALUE_SET) return 0;
  return data_bind_value_count(view->value);
}

DataBindStatus data_bind_list_view_at(const DataBindListView *view, size_t index,
                                      DataBindRecordField *out_field, DataBindError *error) {
  const DataBindValue *value;
  DataBindRecordField result;
  if (view == NULL || view->size < sizeof(*view) || view->value == NULL ||
      !record_field_output_valid(out_field))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                        "Invalid record list access arguments");
  value = data_bind_value_at(view->value, index);
  if (value == NULL)
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                        "Record list index is out of range");
  result.size = out_field->size;
  result.owner = view->value;
  result.value = value;
  result.name = NULL;
  result.index = index;
  *out_field = result;
  record_error_clear(error);
  return DATA_BIND_OK;
}

size_t data_bind_record_map_view_count(const DataBindRecordMapView *view) {
  if (view == NULL || view->size < sizeof(*view) || view->value == NULL ||
      data_bind_value_kind(view->value) != DATA_BIND_VALUE_MAP)
    return 0;
  return data_bind_value_count(view->value);
}

DataBindStatus data_bind_record_map_view_at(const DataBindRecordMapView *view, size_t index,
                                            DataBindStringView *out_key,
                                            DataBindRecordField *out_value, DataBindError *error) {
  DataBindMapEntry entry;
  DataBindStringView key;
  DataBindRecordField value;
  if (view == NULL || view->size < sizeof(*view) || view->value == NULL || out_key == NULL ||
      out_key->size < sizeof(*out_key) || !record_field_output_valid(out_value))
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                        "Invalid record map access arguments");
  entry = data_bind_value_map_entry_at(view->value, index);
  if (entry.key == NULL || entry.value == NULL)
    return record_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Record map index is out of range");
  key.size = out_key->size;
  key.data = entry.key;
  key.length = strlen(entry.key);
  value.size = out_value->size;
  value.owner = view->value;
  value.value = entry.value;
  value.name = entry.key;
  value.index = index;
  *out_key = key;
  *out_value = value;
  record_error_clear(error);
  return DATA_BIND_OK;
}
