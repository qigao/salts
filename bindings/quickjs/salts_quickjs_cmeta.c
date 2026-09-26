#include <salts/bindings/quickjs/cmeta.h>

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct salts_quickjs_push_context {
  JSContext *context;
  salts_quickjs_limits limits;
  size_t depth;
} salts_quickjs_push_context;

static cmeta_status salts_quickjs_push_value(
    salts_quickjs_push_context *push, const cmeta_data_desc *data,
    const void *object, JSValue *out_value);

static cmeta_status salts_quickjs_publish(
    JSValue value, JSValue *out_value) {
  if (JS_IsException(value)) return CMETA_CALLBACK_ERROR;
  *out_value = value;
  return CMETA_OK;
}

static cmeta_status salts_quickjs_push_integer(
    JSContext *context, const cmeta_data_desc *data, const void *object,
    JSValue *out_value) {
  const cmeta_data_integer_shape *shape =
      (const cmeta_data_integer_shape *)data->shape;
  JSValue value = JS_UNDEFINED;
  if (shape == NULL) return CMETA_INVALID_ARGUMENT;

  if (data->kind == CMETA_DATA_SINT) {
    switch (shape->bits) {
      case 8u: value = JS_NewInt32(context, *(const int8_t *)object); break;
      case 16u: value = JS_NewInt32(context, *(const int16_t *)object); break;
      case 32u: value = JS_NewInt32(context, *(const int32_t *)object); break;
      case 64u: value = JS_NewBigInt64(context, *(const int64_t *)object); break;
      default: return CMETA_TRAIT_MISSING;
    }
  } else if (data->kind == CMETA_DATA_UINT) {
    switch (shape->bits) {
      case 8u: value = JS_NewUint32(context, *(const uint8_t *)object); break;
      case 16u: value = JS_NewUint32(context, *(const uint16_t *)object); break;
      case 32u: value = JS_NewUint32(context, *(const uint32_t *)object); break;
      case 64u: value = JS_NewBigUint64(context, *(const uint64_t *)object); break;
      default: return CMETA_TRAIT_MISSING;
    }
  } else {
    return CMETA_TYPE_MISMATCH;
  }
  return salts_quickjs_publish(value, out_value);
}

static cmeta_status salts_quickjs_push_float(
    JSContext *context, const cmeta_data_desc *data, const void *object,
    JSValue *out_value) {
  const cmeta_data_float_shape *shape =
      (const cmeta_data_float_shape *)data->shape;
  double value;
  if (shape == NULL) return CMETA_INVALID_ARGUMENT;
  if (shape->bits == 32u) value = (double)*(const float *)object;
  else if (shape->bits == 64u) value = *(const double *)object;
  else return CMETA_TRAIT_MISSING;
  if (!isfinite(value)) return CMETA_INVALID_ARGUMENT;
  return salts_quickjs_publish(
      JS_NewFloat64(context, value), out_value);
}

static cmeta_status salts_quickjs_push_buffer(
    salts_quickjs_push_context *push, const cmeta_data_desc *data,
    const void *object, JSValue *out_value) {
  static const unsigned char empty = 0u;
  const unsigned char *bytes = NULL;
  size_t size = 0u;
  cmeta_status status = cmeta_data_buffer_read(
      data, object, push->limits.max_bytes, &bytes, &size);
  JSValue value;
  if (status != CMETA_OK) return status;
  if (size == 0u) bytes = &empty;
  value = data->kind == CMETA_DATA_STRING
              ? JS_NewStringLen(push->context, (const char *)bytes, size)
              : JS_NewArrayBufferCopy(push->context, bytes, size);
  return salts_quickjs_publish(value, out_value);
}

static const cmeta_enum_bits_item *salts_quickjs_enum_bits_by_value(
    const cmeta_enum_domain *domain, uint64_t bits) {
  size_t i;
  if (domain == NULL) return NULL;
  for (i = 0u; i < domain->count; ++i) {
    if (domain->items[i].bits == bits) return &domain->items[i];
  }
  return NULL;
}

static const cmeta_enum_bits_item *salts_quickjs_enum_bits_by_text(
    const cmeta_enum_domain *domain, const char *text) {
  size_t i;
  if (domain == NULL || text == NULL) return NULL;
  for (i = 0u; i < domain->count; ++i) {
    const cmeta_enum_bits_item *item = &domain->items[i];
    if (strcmp(item->symbol, text) == 0 || strcmp(item->text, text) == 0)
      return item;
  }
  return NULL;
}

static cmeta_status salts_quickjs_push_enum(
    JSContext *context, const cmeta_data_desc *data, const void *object,
    JSValue *out_value) {
  const cmeta_data_enum_bits_ops *bits_ops =
      cmeta_data_enum_bits_ops_of(data);
  if (bits_ops != NULL) {
    const cmeta_enum_domain *domain = bits_ops->domain;
    const cmeta_enum_bits_item *item;
    uint64_t bits = 0u;
    cmeta_status status = cmeta_data_enum_read_bits(data, object, &bits);
    if (status != CMETA_OK) return status;
    item = salts_quickjs_enum_bits_by_value(domain, bits);
    if (domain->kind == CMETA_ENUM_ORDINARY) {
      if (item == NULL) return CMETA_CALLBACK_ERROR;
      return salts_quickjs_publish(
          JS_NewString(context, item->symbol), out_value);
    }
    return salts_quickjs_publish(
        domain->bits <= 32u ? JS_NewUint32(context, (uint32_t)bits)
                            : JS_NewBigUint64(context, bits),
        out_value);
  }

  {
    const cmeta_data_enum_shape *shape =
        (const cmeta_data_enum_shape *)data->shape;
    int64_t value = 0;
    const char *text;
    cmeta_status status;
    if (shape == NULL || shape->meta == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_read(data, object, &value);
    if (status != CMETA_OK) return status;
    text = cmeta_enum_to_symbol(shape->meta, value);
    if (text == NULL) return CMETA_CALLBACK_ERROR;
    return salts_quickjs_publish(JS_NewString(context, text), out_value);
  }
}

static cmeta_status salts_quickjs_push_struct(
    salts_quickjs_push_context *push, const cmeta_data_desc *data,
    const void *object, JSValue *out_value) {
  const cmeta_data_struct_shape *shape =
      (const cmeta_data_struct_shape *)data->shape;
  JSValue result;
  size_t i;
  cmeta_status status = CMETA_OK;
  if (shape == NULL || shape->fields == NULL) return CMETA_INVALID_ARGUMENT;
  if (push->depth >= push->limits.max_depth)
    return CMETA_CAPACITY_EXCEEDED;
  if (shape->field_count > push->limits.max_items)
    return CMETA_CAPACITY_EXCEEDED;

  result = JS_NewObject(push->context);
  if (JS_IsException(result)) return CMETA_CALLBACK_ERROR;
  ++push->depth;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    JSValue value = JS_UNDEFINED;
    status = salts_quickjs_push_value(
        push, field->value,
        (const unsigned char *)object + field->offset, &value);
    if (status != CMETA_OK) break;
    if (JS_SetPropertyStr(push->context, result, field->name, value) < 0) {
      status = CMETA_CALLBACK_ERROR;
      break;
    }
  }
  --push->depth;
  if (status != CMETA_OK) {
    JS_FreeValue(push->context, result);
    return status;
  }
  *out_value = result;
  return CMETA_OK;
}

typedef struct salts_quickjs_collection_push {
  salts_quickjs_push_context *push;
  const cmeta_data_desc *element_data;
  JSValue array;
  size_t index;
} salts_quickjs_collection_push;

static cmeta_status salts_quickjs_push_collection_element(
    void *opaque, const void *element) {
  salts_quickjs_collection_push *visit =
      (salts_quickjs_collection_push *)opaque;
  JSValue value = JS_UNDEFINED;
  cmeta_status status;
  if (visit == NULL || visit->push == NULL || visit->element_data == NULL ||
      element == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (visit->index > UINT32_MAX) return CMETA_CAPACITY_EXCEEDED;
  status = salts_quickjs_push_value(
      visit->push, visit->element_data, element, &value);
  if (status != CMETA_OK) return status;
  if (JS_SetPropertyUint32(
          visit->push->context, visit->array,
          (uint32_t)visit->index, value) < 0)
    return CMETA_CALLBACK_ERROR;
  ++visit->index;
  return CMETA_OK;
}

static cmeta_status salts_quickjs_push_collection(
    salts_quickjs_push_context *push, const cmeta_data_desc *data,
    const void *object, JSValue *out_value) {
  const cmeta_data_collection_ops *ops =
      cmeta_data_collection_ops_of(data);
  salts_quickjs_collection_push visit;
  cmeta_status status;
  if (ops == NULL) return CMETA_TRAIT_MISSING;
  if (push->depth >= push->limits.max_depth)
    return CMETA_CAPACITY_EXCEEDED;
  visit.element_data = ops->element(object);
  if (visit.element_data == NULL ||
      !cmeta_data_desc_valid(visit.element_data))
    return CMETA_TRAIT_MISSING;
  visit.array = JS_NewArray(push->context);
  if (JS_IsException(visit.array)) return CMETA_CALLBACK_ERROR;
  visit.push = push;
  visit.index = 0u;
  ++push->depth;
  status = cmeta_data_collection_foreach(
      data, object, salts_quickjs_push_collection_element, &visit,
      push->limits.max_items);
  --push->depth;
  if (status != CMETA_OK) {
    JS_FreeValue(push->context, visit.array);
    return status;
  }
  *out_value = visit.array;
  return CMETA_OK;
}

typedef struct salts_quickjs_map_push {
  salts_quickjs_push_context *push;
  const cmeta_data_desc *key_data;
  const cmeta_data_desc *value_data;
  JSValue array;
  size_t index;
} salts_quickjs_map_push;

static cmeta_status salts_quickjs_push_map_entry(
    void *opaque, const void *key, const void *value) {
  salts_quickjs_map_push *visit = (salts_quickjs_map_push *)opaque;
  JSValue entry = JS_UNDEFINED;
  JSValue key_value = JS_UNDEFINED;
  JSValue mapped_value = JS_UNDEFINED;
  cmeta_status status;
  if (visit == NULL || visit->push == NULL || key == NULL || value == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (visit->index > UINT32_MAX) return CMETA_CAPACITY_EXCEEDED;

  entry = JS_NewObject(visit->push->context);
  if (JS_IsException(entry)) return CMETA_CALLBACK_ERROR;
  status = salts_quickjs_push_value(
      visit->push, visit->key_data, key, &key_value);
  if (status != CMETA_OK) goto fail;
  {
    int set_status = JS_SetPropertyStr(
        visit->push->context, entry, "key", key_value);
    key_value = JS_UNDEFINED;
    if (set_status < 0) {
      status = CMETA_CALLBACK_ERROR;
      goto fail;
    }
  }
  status = salts_quickjs_push_value(
      visit->push, visit->value_data, value, &mapped_value);
  if (status != CMETA_OK) goto fail;
  {
    int set_status = JS_SetPropertyStr(
        visit->push->context, entry, "value", mapped_value);
    mapped_value = JS_UNDEFINED;
    if (set_status < 0) {
      status = CMETA_CALLBACK_ERROR;
      goto fail;
    }
  }
  {
    int set_status = JS_SetPropertyUint32(
        visit->push->context, visit->array,
        (uint32_t)visit->index, entry);
    entry = JS_UNDEFINED;
    if (set_status < 0) {
      status = CMETA_CALLBACK_ERROR;
      goto fail;
    }
  }
  ++visit->index;
  return CMETA_OK;

fail:
  JS_FreeValue(visit->push->context, mapped_value);
  JS_FreeValue(visit->push->context, key_value);
  JS_FreeValue(visit->push->context, entry);
  return status;
}

static cmeta_status salts_quickjs_push_map(
    salts_quickjs_push_context *push, const cmeta_data_desc *data,
    const void *object, JSValue *out_value) {
  const cmeta_data_map_ops *ops = cmeta_data_map_ops_of(data);
  salts_quickjs_map_push visit;
  cmeta_status status;
  if (ops == NULL) return CMETA_TRAIT_MISSING;
  if (push->depth >= push->limits.max_depth)
    return CMETA_CAPACITY_EXCEEDED;
  visit.key_data = ops->key(object);
  visit.value_data = ops->value(object);
  if (visit.key_data == NULL || visit.value_data == NULL ||
      !cmeta_data_desc_valid(visit.key_data) ||
      !cmeta_data_desc_valid(visit.value_data))
    return CMETA_TRAIT_MISSING;
  visit.array = JS_NewArray(push->context);
  if (JS_IsException(visit.array)) return CMETA_CALLBACK_ERROR;
  visit.push = push;
  visit.index = 0u;
  ++push->depth;
  status = cmeta_data_map_foreach(
      data, object, salts_quickjs_push_map_entry, &visit,
      push->limits.max_items);
  --push->depth;
  if (status != CMETA_OK) {
    JS_FreeValue(push->context, visit.array);
    return status;
  }
  *out_value = visit.array;
  return CMETA_OK;
}

static cmeta_status salts_quickjs_push_value(
    salts_quickjs_push_context *push, const cmeta_data_desc *data,
    const void *object, JSValue *out_value) {
  if (out_value != NULL) *out_value = JS_UNDEFINED;
  if (push == NULL || push->context == NULL || object == NULL ||
      out_value == NULL || !cmeta_data_desc_valid(data))
    return CMETA_INVALID_ARGUMENT;
  switch (data->kind) {
    case CMETA_DATA_BOOL:
      *out_value = JS_NewBool(push->context, *(const _Bool *)object != 0);
      return CMETA_OK;
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT:
      return salts_quickjs_push_integer(
          push->context, data, object, out_value);
    case CMETA_DATA_FLOAT:
      return salts_quickjs_push_float(
          push->context, data, object, out_value);
    case CMETA_DATA_STRING:
    case CMETA_DATA_BYTES:
      return salts_quickjs_push_buffer(push, data, object, out_value);
    case CMETA_DATA_ENUM:
      return salts_quickjs_push_enum(
          push->context, data, object, out_value);
    case CMETA_DATA_STRUCT:
      return salts_quickjs_push_struct(push, data, object, out_value);
    case CMETA_DATA_SEQUENCE:
    case CMETA_DATA_SET:
      return salts_quickjs_push_collection(push, data, object, out_value);
    case CMETA_DATA_MAP:
      return salts_quickjs_push_map(push, data, object, out_value);
    default:
      return CMETA_TRAIT_MISSING;
  }
}

cmeta_status salts_quickjs_push_cmeta(
    JSContext *context, const cmeta_data_desc *data, const void *object,
    salts_quickjs_limits limits, JSValue *out_value) {
  salts_quickjs_push_context push;
  if (out_value != NULL) *out_value = JS_UNDEFINED;
  if (context == NULL || object == NULL || out_value == NULL ||
      !cmeta_data_desc_valid(data))
    return CMETA_INVALID_ARGUMENT;
  push.context = context;
  push.limits = limits;
  push.depth = 0u;
  return salts_quickjs_push_value(&push, data, object, out_value);
}

static cmeta_status salts_quickjs_read_value(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, salts_quickjs_limits limits, size_t depth);

static cmeta_status salts_quickjs_read_integer(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object) {
  const cmeta_data_integer_shape *shape =
      (const cmeta_data_integer_shape *)data->shape;
  double number;
  if (shape == NULL) return CMETA_INVALID_ARGUMENT;
  if (shape->bits == 64u) {
    if (!JS_IsBigInt(context, value)) return CMETA_TYPE_MISMATCH;
    if (data->kind == CMETA_DATA_SINT) {
      int64_t raw;
      if (JS_ToBigInt64(context, &raw, value) < 0)
        return CMETA_TYPE_MISMATCH;
      *(int64_t *)object = raw;
      return CMETA_OK;
    }
    {
      uint64_t raw;
      if (JS_ToBigUint64(context, &raw, value) < 0)
        return CMETA_TYPE_MISMATCH;
      *(uint64_t *)object = raw;
      return CMETA_OK;
    }
  }

  if (!JS_IsNumber(value) || JS_ToFloat64(context, &number, value) < 0 ||
      !isfinite(number) || trunc(number) != number)
    return CMETA_TYPE_MISMATCH;
  if (data->kind == CMETA_DATA_SINT) {
    switch (shape->bits) {
      case 8u:
        if (number < INT8_MIN || number > INT8_MAX)
          return CMETA_TYPE_MISMATCH;
        *(int8_t *)object = (int8_t)number; return CMETA_OK;
      case 16u:
        if (number < INT16_MIN || number > INT16_MAX)
          return CMETA_TYPE_MISMATCH;
        *(int16_t *)object = (int16_t)number; return CMETA_OK;
      case 32u:
        if (number < INT32_MIN || number > INT32_MAX)
          return CMETA_TYPE_MISMATCH;
        *(int32_t *)object = (int32_t)number; return CMETA_OK;
      default: return CMETA_TRAIT_MISSING;
    }
  }
  switch (shape->bits) {
    case 8u:
      if (number < 0.0 || number > UINT8_MAX) return CMETA_TYPE_MISMATCH;
      *(uint8_t *)object = (uint8_t)number; return CMETA_OK;
    case 16u:
      if (number < 0.0 || number > UINT16_MAX) return CMETA_TYPE_MISMATCH;
      *(uint16_t *)object = (uint16_t)number; return CMETA_OK;
    case 32u:
      if (number < 0.0 || number > UINT32_MAX) return CMETA_TYPE_MISMATCH;
      *(uint32_t *)object = (uint32_t)number; return CMETA_OK;
    default: return CMETA_TRAIT_MISSING;
  }
}

static cmeta_status salts_quickjs_read_float(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object) {
  const cmeta_data_float_shape *shape =
      (const cmeta_data_float_shape *)data->shape;
  double number;
  if (shape == NULL) return CMETA_INVALID_ARGUMENT;
  if (!JS_IsNumber(value) || JS_ToFloat64(context, &number, value) < 0 ||
      !isfinite(number))
    return CMETA_TYPE_MISMATCH;
  if (shape->bits == 32u) {
    if (number < -(double)FLT_MAX || number > (double)FLT_MAX)
      return CMETA_TYPE_MISMATCH;
    *(float *)object = (float)number;
    return CMETA_OK;
  }
  if (shape->bits == 64u) {
    *(double *)object = number;
    return CMETA_OK;
  }
  return CMETA_TRAIT_MISSING;
}

static cmeta_status salts_quickjs_read_buffer(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, size_t max_bytes) {
  if (data->kind == CMETA_DATA_STRING) {
    const char *text;
    size_t size = 0u;
    cmeta_status status;
    if (!JS_IsString(value)) return CMETA_TYPE_MISMATCH;
    text = JS_ToCStringLen(context, &size, value);
    if (text == NULL) return CMETA_CALLBACK_ERROR;
    status = size > max_bytes
                 ? CMETA_CAPACITY_EXCEEDED
                 : cmeta_data_buffer_assign(
                       data, object, (const unsigned char *)text,
                       size, max_bytes);
    JS_FreeCString(context, text);
    return status;
  }

  {
    uint8_t *bytes;
    size_t size = 0u;
    if (!JS_IsArrayBuffer(value)) return CMETA_TYPE_MISMATCH;
    bytes = JS_GetArrayBuffer(context, &size, value);
    if (bytes == NULL && size != 0u) return CMETA_CALLBACK_ERROR;
    if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
    return cmeta_data_buffer_assign(
        data, object, bytes, size, max_bytes);
  }
}

static cmeta_status salts_quickjs_read_enum(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object) {
  const cmeta_data_enum_bits_ops *bits_ops =
      cmeta_data_enum_bits_ops_of(data);
  if (bits_ops != NULL) {
    const cmeta_enum_domain *domain = bits_ops->domain;
    uint64_t bits;
    if (JS_IsString(value)) {
      const cmeta_enum_bits_item *item;
      const char *text = JS_ToCString(context, value);
      if (text == NULL) return CMETA_CALLBACK_ERROR;
      item = salts_quickjs_enum_bits_by_text(domain, text);
      JS_FreeCString(context, text);
      if (item == NULL) return CMETA_TYPE_MISMATCH;
      bits = item->bits;
    } else if (domain->bits <= 32u && JS_IsNumber(value)) {
      double number;
      if (JS_ToFloat64(context, &number, value) < 0 ||
          !isfinite(number) || trunc(number) != number || number < 0.0 ||
          number > UINT32_MAX)
        return CMETA_TYPE_MISMATCH;
      bits = (uint64_t)number;
    } else if (domain->bits == 64u && JS_IsBigInt(context, value)) {
      if (JS_ToBigUint64(context, &bits, value) < 0)
        return CMETA_TYPE_MISMATCH;
    } else {
      return CMETA_TYPE_MISMATCH;
    }
    return cmeta_data_enum_assign_bits(data, object, bits);
  }

  {
    const cmeta_data_enum_shape *shape =
        (const cmeta_data_enum_shape *)data->shape;
    int64_t raw;
    if (shape == NULL || shape->meta == NULL) return CMETA_INVALID_ARGUMENT;
    if (JS_IsString(value)) {
      const char *text = JS_ToCString(context, value);
      bool found;
      if (text == NULL) return CMETA_CALLBACK_ERROR;
      found = cmeta_enum_from_string(shape->meta, text, &raw);
      JS_FreeCString(context, text);
      if (!found) return CMETA_TYPE_MISMATCH;
    } else if (JS_IsNumber(value)) {
      double number;
      if (JS_ToFloat64(context, &number, value) < 0 ||
          !isfinite(number) || trunc(number) != number ||
          number < -9223372036854775808.0 ||
          number >= 9223372036854775808.0)
        return CMETA_TYPE_MISMATCH;
      raw = (int64_t)number;
    } else if (JS_IsBigInt(context, value)) {
      if (JS_ToBigInt64(context, &raw, value) < 0)
        return CMETA_TYPE_MISMATCH;
    } else {
      return CMETA_TYPE_MISMATCH;
    }
    return cmeta_data_enum_assign(data, object, raw);
  }
}

static cmeta_status salts_quickjs_read_struct(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, salts_quickjs_limits limits, size_t depth) {
  const cmeta_data_struct_shape *shape =
      (const cmeta_data_struct_shape *)data->shape;
  cmeta_data_temp temporary = {0};
  size_t i;
  cmeta_status status;
  if (!JS_IsObject(value) || JS_IsArray(value)) return CMETA_TYPE_MISMATCH;
  if (!cmeta_data_struct_constructible(data)) return CMETA_TRAIT_MISSING;
  if (depth >= limits.max_depth || shape->field_count > limits.max_items)
    return CMETA_CAPACITY_EXCEEDED;
  status = cmeta_data_temp_open(data, limits.max_bytes, &temporary);
  if (status != CMETA_OK) return status;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    JSValue property = JS_GetPropertyStr(context, value, field->name);
    if (JS_IsException(property)) {
      status = CMETA_CALLBACK_ERROR;
      goto done;
    }
    if (JS_IsUndefined(property)) {
      JS_FreeValue(context, property);
      status = CMETA_TYPE_MISMATCH;
      goto done;
    }
    status = salts_quickjs_read_value(
        context, property, field->value,
        (unsigned char *)temporary.storage + field->offset,
        limits, depth + 1u);
    JS_FreeValue(context, property);
    if (status != CMETA_OK) goto done;
  }
  status = cmeta_data_value_move(data, object, temporary.storage);

done:
  cmeta_data_temp_close(&temporary);
  return status;
}

static cmeta_status salts_quickjs_read_collection(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, salts_quickjs_limits limits, size_t depth) {
  const cmeta_data_collection_ops *ops =
      cmeta_data_collection_ops_of(data);
  const cmeta_data_desc *element_data;
  cmeta_collector collector = {0};
  int64_t length = 0;
  int64_t i;
  cmeta_status status;
  bool begun = false;
  if (ops == NULL || !JS_IsArray(value)) return CMETA_TYPE_MISMATCH;
  if (depth >= limits.max_depth) return CMETA_CAPACITY_EXCEEDED;
  if (JS_GetLength(context, value, &length) < 0 || length < 0)
    return CMETA_CALLBACK_ERROR;
  if ((uint64_t)length > (uint64_t)limits.max_items)
    return CMETA_CAPACITY_EXCEEDED;
  element_data = ops->element(object);
  if (element_data == NULL || !cmeta_data_desc_valid(element_data))
    return CMETA_TRAIT_MISSING;
  status = cmeta_data_collection_collector(
      data, object, limits.max_items, &collector);
  if (status != CMETA_OK) goto done;
  status = cmeta_collector_begin(&collector);
  if (status != CMETA_OK) goto done;
  begun = true;
  for (i = 0; i < length; ++i) {
    cmeta_data_temp element = {0};
    JSValue item = JS_GetPropertyInt64(context, value, i);
    if (JS_IsException(item)) {
      status = CMETA_CALLBACK_ERROR;
    } else {
      status = cmeta_data_temp_open(
          element_data, limits.max_bytes, &element);
      if (status == CMETA_OK)
        status = salts_quickjs_read_value(
            context, item, element_data, element.storage,
            limits, depth + 1u);
      if (status == CMETA_OK)
        status = cmeta_data_collection_accept(
            data, &collector, element_data, element.storage);
    }
    cmeta_data_temp_close(&element);
    JS_FreeValue(context, item);
    if (status != CMETA_OK) goto done;
  }
  status = cmeta_collector_finish(&collector);
  begun = false;
  if (status != CMETA_OK) goto done;

done:
  if (begun) cmeta_collector_abort(&collector);
  return status;
}

static cmeta_status salts_quickjs_read_map(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, salts_quickjs_limits limits, size_t depth) {
  const cmeta_data_map_ops *ops = cmeta_data_map_ops_of(data);
  const cmeta_data_desc *key_data;
  const cmeta_data_desc *value_data;
  cmeta_collector collector = {0};
  int64_t length = 0;
  int64_t i;
  cmeta_status status;
  bool begun = false;
  if (ops == NULL || !JS_IsArray(value)) return CMETA_TYPE_MISMATCH;
  if (depth >= limits.max_depth) return CMETA_CAPACITY_EXCEEDED;
  if (JS_GetLength(context, value, &length) < 0 || length < 0)
    return CMETA_CALLBACK_ERROR;
  if ((uint64_t)length > (uint64_t)limits.max_items)
    return CMETA_CAPACITY_EXCEEDED;
  key_data = ops->key(object);
  value_data = ops->value(object);
  if (key_data == NULL || value_data == NULL ||
      !cmeta_data_desc_valid(key_data) ||
      !cmeta_data_desc_valid(value_data))
    return CMETA_TRAIT_MISSING;
  status = cmeta_data_map_collector(
      data, object, limits.max_items, &collector);
  if (status != CMETA_OK) goto done;
  status = cmeta_collector_begin(&collector);
  if (status != CMETA_OK) goto done;
  begun = true;

  for (i = 0; i < length; ++i) {
    cmeta_data_temp key = {0};
    cmeta_data_temp mapped = {0};
    JSValue entry = JS_GetPropertyInt64(context, value, i);
    JSValue key_value = JS_UNDEFINED;
    JSValue mapped_value = JS_UNDEFINED;
    if (JS_IsException(entry)) {
      status = CMETA_CALLBACK_ERROR;
      goto entry_done;
    }
    if (!JS_IsObject(entry) || JS_IsArray(entry)) {
      status = CMETA_TYPE_MISMATCH;
      goto entry_done;
    }
    key_value = JS_GetPropertyStr(context, entry, "key");
    mapped_value = JS_GetPropertyStr(context, entry, "value");
    if (JS_IsException(key_value) || JS_IsException(mapped_value)) {
      status = CMETA_CALLBACK_ERROR;
      goto entry_done;
    }
    if (JS_IsUndefined(key_value) || JS_IsUndefined(mapped_value)) {
      status = CMETA_TYPE_MISMATCH;
      goto entry_done;
    }
    status = cmeta_data_temp_open(key_data, limits.max_bytes, &key);
    if (status != CMETA_OK) goto entry_done;
    status = cmeta_data_temp_open(value_data, limits.max_bytes, &mapped);
    if (status != CMETA_OK) goto entry_done;
    status = salts_quickjs_read_value(
        context, key_value, key_data, key.storage, limits, depth + 1u);
    if (status != CMETA_OK) goto entry_done;
    status = salts_quickjs_read_value(
        context, mapped_value, value_data, mapped.storage,
        limits, depth + 1u);
    if (status != CMETA_OK) goto entry_done;
    status = cmeta_data_map_accept(
        data, &collector, key_data, key.storage,
        value_data, mapped.storage);

entry_done:
    cmeta_data_temp_close(&mapped);
    cmeta_data_temp_close(&key);
    JS_FreeValue(context, mapped_value);
    JS_FreeValue(context, key_value);
    JS_FreeValue(context, entry);
    if (status != CMETA_OK) goto done;
  }
  status = cmeta_collector_finish(&collector);
  begun = false;
  if (status != CMETA_OK) goto done;

done:
  if (begun) cmeta_collector_abort(&collector);
  return status;
}

static cmeta_status salts_quickjs_read_value(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, salts_quickjs_limits limits, size_t depth) {
  if (context == NULL || object == NULL || !cmeta_data_desc_valid(data))
    return CMETA_INVALID_ARGUMENT;
  switch (data->kind) {
    case CMETA_DATA_BOOL:
      if (!JS_IsBool(value)) return CMETA_TYPE_MISMATCH;
      *(_Bool *)object = JS_ToBool(context, value) != 0;
      return CMETA_OK;
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT:
      return salts_quickjs_read_integer(
          context, value, data, object);
    case CMETA_DATA_FLOAT:
      return salts_quickjs_read_float(context, value, data, object);
    case CMETA_DATA_STRING:
    case CMETA_DATA_BYTES:
      return salts_quickjs_read_buffer(
          context, value, data, object, limits.max_bytes);
    case CMETA_DATA_ENUM:
      return salts_quickjs_read_enum(context, value, data, object);
    case CMETA_DATA_STRUCT:
      return salts_quickjs_read_struct(
          context, value, data, object, limits, depth);
    case CMETA_DATA_SEQUENCE:
    case CMETA_DATA_SET:
      return salts_quickjs_read_collection(
          context, value, data, object, limits, depth);
    case CMETA_DATA_MAP:
      return salts_quickjs_read_map(
          context, value, data, object, limits, depth);
    default:
      return CMETA_TRAIT_MISSING;
  }
}

cmeta_status salts_quickjs_read_cmeta(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, salts_quickjs_limits limits) {
  return salts_quickjs_read_value(
      context, value, data, object, limits, 0u);
}

cmeta_status salts_quickjs_call_invokable(
    JSContext *context, const cmeta_invokable *invokable,
    int argument_count, JSValueConst *arguments, salts_quickjs_limits limits,
    JSValue *out_result, bool *out_has_result) {
  const cmeta_function_data_desc *data;
  const cmeta_function_desc *function;
  cmeta_data_temp *temporaries = NULL;
  const void **native_arguments = NULL;
  cmeta_data_temp result = {0};
  size_t count;
  size_t i;
  cmeta_status status = CMETA_OK;
  if (out_result != NULL) *out_result = JS_UNDEFINED;
  if (out_has_result != NULL) *out_has_result = false;
  if (context == NULL || invokable == NULL || out_result == NULL ||
      argument_count < 0 ||
      (argument_count != 0 && arguments == NULL) ||
      !cmeta_invokable_valid(invokable))
    return CMETA_INVALID_ARGUMENT;
  data = invokable->data;
  if (!cmeta_function_data_desc_valid(data)) return CMETA_TRAIT_MISSING;
  function = data->function;
  count = (size_t)argument_count;
  if (count != function->param_count) return CMETA_INVALID_ARGUMENT;
  if (count > limits.max_items) return CMETA_CAPACITY_EXCEEDED;

  for (i = 0u; i < count; ++i) {
    const cmeta_param_desc *param = cmeta_function_param(function, i);
    cmeta_param_flags direction;
    if (param == NULL) return CMETA_INVALID_ARGUMENT;
    direction = param->flags & CMETA_PARAM_DIRECTION_MASK;
    if (direction != CMETA_PARAM_IN ||
        (param->flags & CMETA_PARAM_OWNED) != 0u)
      return CMETA_TRAIT_MISSING;
  }

  if (count != 0u) {
    temporaries = (cmeta_data_temp *)calloc(
        count, sizeof(cmeta_data_temp));
    native_arguments = (const void **)calloc(count, sizeof(void *));
    if (temporaries == NULL || native_arguments == NULL) {
      status = CMETA_OUT_OF_MEMORY;
      goto done;
    }
  }
  for (i = 0u; i < count; ++i) {
    status = cmeta_data_temp_open(
        data->params[i], limits.max_bytes, &temporaries[i]);
    if (status != CMETA_OK) goto done;
    status = salts_quickjs_read_cmeta(
        context, arguments[i], data->params[i],
        temporaries[i].storage, limits);
    if (status != CMETA_OK) goto done;
    native_arguments[i] = temporaries[i].storage;
  }

  if (data->return_data != NULL) {
    status = cmeta_data_temp_open(
        data->return_data, limits.max_bytes, &result);
    if (status != CMETA_OK) goto done;
  }
  status = cmeta_invokable_invoke(
      invokable, data->return_data != NULL ? result.storage : NULL,
      native_arguments);
  if (status != CMETA_OK) goto done;
  if (data->return_data != NULL) {
    status = salts_quickjs_push_cmeta(
        context, data->return_data, result.storage, limits, out_result);
    if (status != CMETA_OK) goto done;
    if (out_has_result != NULL) *out_has_result = true;
  }

done:
  cmeta_data_temp_close(&result);
  if (temporaries != NULL) {
    i = count;
    while (i != 0u) cmeta_data_temp_close(&temporaries[--i]);
  }
  free(native_arguments);
  free(temporaries);
  return status;
}


#define SALTS_QUICKJS_OBJECT_HOLDER_PROPERTY "__salts_cmeta_object_holder__"

typedef struct salts_quickjs_object_holder {
  cmeta_object_ref object;
  salts_quickjs_limits limits;
  bool armed;
} salts_quickjs_object_holder;

static void salts_quickjs_object_holder_finalize(
    JSRuntime *runtime, void *opaque, void *ptr) {
  salts_quickjs_object_holder *holder =
      (salts_quickjs_object_holder *)opaque;
  (void)runtime;
  (void)ptr;
  if (holder == NULL)
    return;
  if (holder->armed)
    cmeta_object_release(&holder->object);
  free(holder);
}

static salts_quickjs_object_holder *
salts_quickjs_object_holder_from_data(
    JSContext *context, JSValueConst *function_data) {
  size_t size = SIZE_MAX;
  uint8_t *storage;

  if (context == NULL || function_data == NULL)
    return NULL;
  storage = JS_GetArrayBuffer(context, &size, function_data[0]);
  if (storage == NULL || size != 0u)
    return NULL;
  return (salts_quickjs_object_holder *)storage;
}

static JSValue salts_quickjs_object_field_get(
    JSContext *context, JSValueConst this_value,
    int argument_count, JSValueConst *arguments, int magic,
    JSValueConst *function_data) {
  salts_quickjs_object_holder *holder =
      salts_quickjs_object_holder_from_data(context, function_data);
  const cmeta_data_struct_shape *shape;
  const cmeta_data_field_desc *field;
  const cmeta_data_desc *field_data = NULL;
  const void *field_value = NULL;
  JSValue result = JS_UNDEFINED;
  cmeta_status status;

  (void)this_value;
  (void)argument_count;
  (void)arguments;

  if (holder == NULL || magic < 0 ||
      !cmeta_object_ref_valid(&holder->object) ||
      holder->object.data->kind != CMETA_DATA_STRUCT ||
      holder->object.data->shape == NULL)
    return JS_ThrowTypeError(context, "invalid CMeta object field proxy");

  shape = (const cmeta_data_struct_shape *)holder->object.data->shape;
  if ((size_t)magic >= shape->field_count)
    return JS_ThrowTypeError(context, "invalid CMeta object field index");
  field = &shape->fields[(size_t)magic];

  status = cmeta_object_field_read(
      &holder->object, field->name, &field_data, &field_value);
  if (status != CMETA_OK)
    return JS_ThrowTypeError(
        context, "CMeta object field read failed (%d)", (int)status);
  status = salts_quickjs_push_cmeta(
      context, field_data, field_value, holder->limits, &result);
  if (status != CMETA_OK)
    return JS_ThrowTypeError(
        context, "CMeta object field projection failed (%d)", (int)status);
  return result;
}

static JSValue salts_quickjs_object_field_set(
    JSContext *context, JSValueConst this_value,
    int argument_count, JSValueConst *arguments, int magic,
    JSValueConst *function_data) {
  salts_quickjs_object_holder *holder =
      salts_quickjs_object_holder_from_data(context, function_data);
  const cmeta_data_struct_shape *shape;
  const cmeta_data_field_desc *field;
  cmeta_data_temp value = {0};
  cmeta_status status;

  (void)this_value;

  if (holder == NULL || magic < 0 ||
      argument_count != 1 || arguments == NULL ||
      !cmeta_object_ref_valid(&holder->object) ||
      holder->object.data->kind != CMETA_DATA_STRUCT ||
      holder->object.data->shape == NULL)
    return JS_ThrowTypeError(context, "invalid CMeta object field setter");

  shape = (const cmeta_data_struct_shape *)holder->object.data->shape;
  if ((size_t)magic >= shape->field_count)
    return JS_ThrowTypeError(context, "invalid CMeta object field index");
  field = &shape->fields[(size_t)magic];

  status = cmeta_data_temp_open(
      field->value, holder->limits.max_bytes, &value);
  if (status == CMETA_OK)
    status = salts_quickjs_read_cmeta(
        context, arguments[0], field->value, value.storage, holder->limits);
  if (status == CMETA_OK)
    status = cmeta_object_field_assign(
        &holder->object, field->name, field->value, value.storage);
  cmeta_data_temp_close(&value);

  if (status != CMETA_OK)
    return JS_ThrowTypeError(
        context, "CMeta object field assignment failed (%d)", (int)status);
  return JS_UNDEFINED;
}

static JSValue salts_quickjs_object_method_call(
    JSContext *context, JSValueConst this_value,
    int argument_count, JSValueConst *arguments, int magic,
    JSValueConst *function_data) {
  salts_quickjs_object_holder *holder =
      salts_quickjs_object_holder_from_data(context, function_data);
  const cmeta_receiver_method *method;
  cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
  JSValue result = JS_UNDEFINED;
  bool has_result = false;
  cmeta_status status;

  (void)this_value;

  if (holder == NULL || magic < 0 ||
      !cmeta_object_ref_valid(&holder->object) ||
      holder->object.methods == NULL ||
      (size_t)magic >= holder->object.methods->method_count)
    return JS_ThrowTypeError(context, "invalid CMeta object method proxy");

  method = &holder->object.methods->methods[(size_t)magic];
  status = cmeta_object_method_invokable_bind(
      &holder->object, method, &invokable);
  if (status != CMETA_OK)
    return JS_ThrowTypeError(
        context, "CMeta object method binding failed (%d)", (int)status);

  status = salts_quickjs_call_invokable(
      context, &invokable, argument_count, arguments,
      holder->limits, &result, &has_result);
  if (status != CMETA_OK)
    return JS_ThrowTypeError(
        context, "CMeta object method invocation failed (%d)", (int)status);
  return has_result ? result : JS_UNDEFINED;
}

static JSValue salts_quickjs_object_data_function(
    JSContext *context, JSCFunctionData *function,
    int length, size_t index, JSValueConst holder_value) {
  JSValueConst data[1] = {holder_value};

  if (context == NULL || function == NULL || index > (size_t)INT_MAX)
    return JS_ThrowRangeError(context, "CMeta object member index is too large");
  return JS_NewCFunctionData(
      context, function, length, (int)index, 1, data);
}

static cmeta_status salts_quickjs_object_surface_validate(
    const cmeta_object_ref *object) {
  const cmeta_data_struct_shape *shape;
  size_t i;
  size_t j;

  if (!cmeta_object_ref_valid(object))
    return CMETA_INVALID_ARGUMENT;
  if (object->data->kind != CMETA_DATA_STRUCT ||
      object->data->shape == NULL ||
      object->method_provider == NULL ||
      object->methods == NULL)
    return CMETA_OK;
  if (!cmeta_object_method_provider_valid(object->method_provider))
    return CMETA_INVALID_ARGUMENT;

  shape = (const cmeta_data_struct_shape *)object->data->shape;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    for (j = 0u; j < object->methods->method_count; ++j)
      if (field->name != NULL && object->methods->methods[j].name != NULL &&
          strcmp(field->name, object->methods->methods[j].name) == 0)
        return CMETA_TYPE_MISMATCH;
  }
  return CMETA_OK;
}

cmeta_status salts_quickjs_push_object(
    JSContext *context, cmeta_object_ref *object,
    salts_quickjs_limits limits, JSValue *out_value) {
  salts_quickjs_object_holder *holder;
  const cmeta_data_struct_shape *shape = NULL;
  JSValue result = JS_UNDEFINED;
  JSValue holder_value = JS_UNDEFINED;
  cmeta_status status;
  size_t i;

  if (out_value != NULL)
    *out_value = JS_UNDEFINED;
  if (context == NULL || object == NULL || out_value == NULL)
    return CMETA_INVALID_ARGUMENT;
  status = salts_quickjs_object_surface_validate(object);
  if (status != CMETA_OK)
    return status;

  holder = (salts_quickjs_object_holder *)calloc(1u, sizeof(*holder));
  if (holder == NULL)
    return CMETA_OUT_OF_MEMORY;
  holder->object = *object;
  holder->limits = limits;
  holder->armed = false;

  holder_value = JS_NewArrayBuffer(
      context, (uint8_t *)holder, 0u,
      salts_quickjs_object_holder_finalize, holder, false);
  if (JS_IsException(holder_value)) {
    free(holder);
    return CMETA_OUT_OF_MEMORY;
  }

  result = JS_NewObject(context);
  if (JS_IsException(result)) {
    JS_FreeValue(context, holder_value);
    return CMETA_OUT_OF_MEMORY;
  }

  if (JS_DefinePropertyValueStr(
          context, result, SALTS_QUICKJS_OBJECT_HOLDER_PROPERTY,
          JS_DupValue(context, holder_value), 0) < 0) {
    JS_FreeValue(context, result);
    JS_FreeValue(context, holder_value);
    return CMETA_CALLBACK_ERROR;
  }

  if (object->data->kind == CMETA_DATA_STRUCT &&
      object->data->shape != NULL) {
    shape = (const cmeta_data_struct_shape *)object->data->shape;
    for (i = 0u; i < shape->field_count; ++i) {
      const cmeta_data_field_desc *field = &shape->fields[i];
      JSValue getter = salts_quickjs_object_data_function(
          context, salts_quickjs_object_field_get, 0, i, holder_value);
      JSValue setter;
      JSAtom atom;

      if (JS_IsException(getter)) {
        JS_FreeValue(context, result);
        JS_FreeValue(context, holder_value);
        return CMETA_OUT_OF_MEMORY;
      }
      setter = salts_quickjs_object_data_function(
          context, salts_quickjs_object_field_set, 1, i, holder_value);
      if (JS_IsException(setter)) {
        JS_FreeValue(context, getter);
        JS_FreeValue(context, result);
        JS_FreeValue(context, holder_value);
        return CMETA_OUT_OF_MEMORY;
      }
      atom = JS_NewAtom(context, field->name);
      if (atom == JS_ATOM_NULL) {
        JS_FreeValue(context, setter);
        JS_FreeValue(context, getter);
        JS_FreeValue(context, result);
        JS_FreeValue(context, holder_value);
        return CMETA_OUT_OF_MEMORY;
      }
      if (JS_DefinePropertyGetSet(
              context, result, atom, getter, setter,
              JS_PROP_ENUMERABLE) < 0) {
        JS_FreeAtom(context, atom);
        JS_FreeValue(context, result);
        JS_FreeValue(context, holder_value);
        return CMETA_CALLBACK_ERROR;
      }
      JS_FreeAtom(context, atom);
    }
  }

  if (object->method_provider != NULL && object->methods != NULL) {
    for (i = 0u; i < object->methods->method_count; ++i) {
      const cmeta_receiver_method *method = &object->methods->methods[i];
      int length = method->function != NULL &&
                   method->function->param_count != 0u
                       ? (int)(method->function->param_count - 1u)
                       : 0;
      JSValue function = salts_quickjs_object_data_function(
          context, salts_quickjs_object_method_call,
          length, i, holder_value);
      if (JS_IsException(function)) {
        JS_FreeValue(context, result);
        JS_FreeValue(context, holder_value);
        return CMETA_OUT_OF_MEMORY;
      }
      if (JS_DefinePropertyValueStr(
              context, result, method->name, function,
              JS_PROP_ENUMERABLE) < 0) {
        JS_FreeValue(context, result);
        JS_FreeValue(context, holder_value);
        return CMETA_CALLBACK_ERROR;
      }
    }
  }

  if (JS_PreventExtensions(context, result) < 0) {
    JS_FreeValue(context, result);
    JS_FreeValue(context, holder_value);
    return CMETA_CALLBACK_ERROR;
  }

  holder->armed = true;
  *object = (cmeta_object_ref)CMETA_OBJECT_REF_INIT;
  JS_FreeValue(context, holder_value);
  *out_value = result;
  return CMETA_OK;
}
