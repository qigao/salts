#include <salts/bindings/lua/cmeta.h>

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct salts_lua_push_context {
  lua_State *state;
  salts_lua_limits limits;
  size_t depth;
} salts_lua_push_context;

static cmeta_status salts_lua_push_value(
    salts_lua_push_context *context, const cmeta_data_desc *data,
    const void *object);

static cmeta_status salts_lua_push_integer(
    lua_State *state, const cmeta_data_desc *data, const void *object) {
  const cmeta_data_integer_shape *shape =
      (const cmeta_data_integer_shape *)data->shape;
  if (shape == NULL) return CMETA_INVALID_ARGUMENT;
  if (data->kind == CMETA_DATA_SINT) {
    int64_t value = 0;
    switch (shape->bits) {
      case 8u: value = *(const int8_t *)object; break;
      case 16u: value = *(const int16_t *)object; break;
      case 32u: value = *(const int32_t *)object; break;
      case 64u: value = *(const int64_t *)object; break;
      default: return CMETA_TRAIT_MISSING;
    }
    lua_pushinteger(state, (lua_Integer)value);
    return CMETA_OK;
  }
  if (data->kind == CMETA_DATA_UINT) {
    uint64_t value = 0u;
    switch (shape->bits) {
      case 8u: value = *(const uint8_t *)object; break;
      case 16u: value = *(const uint16_t *)object; break;
      case 32u: value = *(const uint32_t *)object; break;
      case 64u: value = *(const uint64_t *)object; break;
      default: return CMETA_TRAIT_MISSING;
    }
    if (value > (uint64_t)LUA_MAXINTEGER) return CMETA_CAPACITY_EXCEEDED;
    lua_pushinteger(state, (lua_Integer)value);
    return CMETA_OK;
  }
  return CMETA_TYPE_MISMATCH;
}

static cmeta_status salts_lua_push_float(
    lua_State *state, const cmeta_data_desc *data, const void *object) {
  const cmeta_data_float_shape *shape =
      (const cmeta_data_float_shape *)data->shape;
  double value;
  if (shape == NULL) return CMETA_INVALID_ARGUMENT;
  if (shape->bits == 32u) value = (double)*(const float *)object;
  else if (shape->bits == 64u) value = *(const double *)object;
  else return CMETA_TRAIT_MISSING;
  if (!isfinite(value)) return CMETA_INVALID_ARGUMENT;
  lua_pushnumber(state, (lua_Number)value);
  return CMETA_OK;
}

static cmeta_status salts_lua_push_buffer(
    salts_lua_push_context *context, const cmeta_data_desc *data,
    const void *object) {
  const void *bytes = NULL;
  size_t size = 0u;
  cmeta_status status = cmeta_data_buffer_read(
      data, object, context->limits.max_bytes, &bytes, &size);
  if (status != CMETA_OK) return status;
  lua_pushlstring(context->state, bytes != NULL ? (const char *)bytes : "", size);
  return CMETA_OK;
}

static cmeta_status salts_lua_push_field(
    salts_lua_push_context *context, const cmeta_data_field_desc *field,
    const void *object) {
  cmeta_status status;
  if (field == NULL || field->name == NULL || field->value == NULL)
    return CMETA_INVALID_ARGUMENT;
  status = salts_lua_push_value(
      context, field->value, (const unsigned char *)object + field->offset);
  if (status == CMETA_OK)
    lua_setfield(context->state, -2, field->name);
  return status;
}

static cmeta_status salts_lua_push_struct(
    salts_lua_push_context *context, const cmeta_data_desc *data,
    const void *object) {
  const cmeta_data_struct_shape *shape =
      (const cmeta_data_struct_shape *)data->shape;
  size_t i;
  int top;
  cmeta_status status;
  if (shape == NULL || shape->fields == NULL) return CMETA_INVALID_ARGUMENT;
  if (context->depth >= context->limits.max_depth) return CMETA_CAPACITY_EXCEEDED;
  top = lua_gettop(context->state);
  lua_createtable(context->state, 0, (int)shape->field_count);
  ++context->depth;
  for (i = 0u; i < shape->field_count; ++i) {
    status = salts_lua_push_field(context, &shape->fields[i], object);
    if (status != CMETA_OK) {
      --context->depth;
      lua_settop(context->state, top);
      return status;
    }
  }
  --context->depth;
  return CMETA_OK;
}

static cmeta_status salts_lua_push_enum(
    lua_State *state, const cmeta_data_desc *data, const void *object) {
  const cmeta_data_enum_shape *shape =
      (const cmeta_data_enum_shape *)data->shape;
  int64_t value;
  const char *text;
  if (shape == NULL || shape->meta == NULL) return CMETA_INVALID_ARGUMENT;
  if (cmeta_data_enum_read(data, object, &value) != CMETA_OK)
    return CMETA_TYPE_MISMATCH;
  text = cmeta_enum_to_string(shape->meta, value);
  if (text == NULL) return CMETA_TYPE_MISMATCH;
  lua_pushstring(state, text);
  return CMETA_OK;
}

typedef struct salts_lua_collection_context {
  salts_lua_push_context *push;
  const cmeta_data_desc *element_data;
  size_t index;
} salts_lua_collection_context;

static cmeta_status salts_lua_collection_visit(
    void *opaque, const void *element) {
  salts_lua_collection_context *visit =
      (salts_lua_collection_context *)opaque;
  cmeta_status status;
  if (visit == NULL || visit->push == NULL || visit->element_data == NULL)
    return CMETA_INVALID_ARGUMENT;
  status = salts_lua_push_value(visit->push, visit->element_data, element);
  if (status != CMETA_OK) return status;
  lua_rawseti(visit->push->state, -2, (lua_Integer)(++visit->index));
  return CMETA_OK;
}

static cmeta_status salts_lua_push_collection(
    salts_lua_push_context *context, const cmeta_data_desc *data,
    const void *object) {
  const cmeta_data_collection_ops *ops = cmeta_data_collection_ops_of(data);
  const cmeta_data_desc *element_data;
  cmeta_data_collection_view view = {0};
  size_t i;
  int top;
  cmeta_status status;
  if (ops == NULL) return CMETA_TRAIT_MISSING;
  element_data = ops->element(object);
  if (element_data == NULL || !cmeta_data_desc_valid(element_data))
    return CMETA_TRAIT_MISSING;
  if (context->depth >= context->limits.max_depth)
    return CMETA_CAPACITY_EXCEEDED;
  top = lua_gettop(context->state);
  lua_createtable(context->state, 0, 0);
  ++context->depth;
  if (ops->read != NULL) {
    status = cmeta_data_collection_read(data, object, &view);
    if (status != CMETA_OK) goto fail;
    if (view.count > context->limits.max_items) {
      status = CMETA_CAPACITY_EXCEEDED; goto fail;
    }
    for (i = 0u; i < view.count; ++i) {
      status = salts_lua_push_value(
          context, element_data,
          (const unsigned char *)view.data + i * view.stride);
      if (status != CMETA_OK) goto fail;
      lua_rawseti(context->state, -2, (lua_Integer)(i + 1u));
    }
    --context->depth;
    return CMETA_OK;
  }
  {
    salts_lua_collection_context visit = {context, element_data, 0u};
    status = cmeta_data_collection_foreach(
        data, object, salts_lua_collection_visit, &visit,
        context->limits.max_items);
    if (status == CMETA_OK) {
      --context->depth;
      return CMETA_OK;
    }
  }
fail:
  --context->depth;
  lua_settop(context->state, top);
  return status;
}

typedef struct salts_lua_map_context {
  salts_lua_push_context *push;
  const cmeta_data_desc *key_data;
  const cmeta_data_desc *value_data;
  cmeta_data_map_flags flags;
  size_t index;
} salts_lua_map_context;

static cmeta_status salts_lua_map_visit(
    void *opaque, const void *key, const void *value) {
  salts_lua_map_context *visit = (salts_lua_map_context *)opaque;
  cmeta_status status;
  if (visit == NULL || visit->push == NULL || visit->key_data == NULL ||
      visit->value_data == NULL)
    return CMETA_INVALID_ARGUMENT;

  if ((visit->flags & CMETA_DATA_MAP_REPEATED_KEYS) != 0u) {
    lua_createtable(visit->push->state, 0, 2);
    status = salts_lua_push_value(visit->push, visit->key_data, key);
    if (status != CMETA_OK) { lua_pop(visit->push->state, 1); return status; }
    lua_setfield(visit->push->state, -2, "key");
    status = salts_lua_push_value(visit->push, visit->value_data, value);
    if (status != CMETA_OK) { lua_pop(visit->push->state, 1); return status; }
    lua_setfield(visit->push->state, -2, "value");
    lua_rawseti(visit->push->state, -2, (lua_Integer)(++visit->index));
    return CMETA_OK;
  }

  status = salts_lua_push_value(visit->push, visit->key_data, key);
  if (status != CMETA_OK) return status;
  status = salts_lua_push_value(visit->push, visit->value_data, value);
  if (status != CMETA_OK) { lua_pop(visit->push->state, 1); return status; }
  lua_settable(visit->push->state, -3);
  ++visit->index;
  return CMETA_OK;
}

static cmeta_status salts_lua_push_map(
    salts_lua_push_context *context, const cmeta_data_desc *data,
    const void *object) {
  const cmeta_data_map_ops *ops = cmeta_data_map_ops_of(data);
  salts_lua_map_context visit;
  int top;
  cmeta_status status;
  if (ops == NULL) return CMETA_TRAIT_MISSING;
  visit.key_data = ops->key(object);
  visit.value_data = ops->value(object);
  if (visit.key_data == NULL || visit.value_data == NULL ||
      !cmeta_data_desc_valid(visit.key_data) ||
      !cmeta_data_desc_valid(visit.value_data))
    return CMETA_TRAIT_MISSING;
  if (context->depth >= context->limits.max_depth)
    return CMETA_CAPACITY_EXCEEDED;

  top = lua_gettop(context->state);
  lua_createtable(context->state, 0, 0);
  ++context->depth;
  visit.push = context;
  visit.flags = ops->flags;
  visit.index = 0u;
  status = cmeta_data_map_foreach(
      data, object, salts_lua_map_visit, &visit, context->limits.max_items);
  --context->depth;
  if (status != CMETA_OK) lua_settop(context->state, top);
  return status;
}

static cmeta_status salts_lua_push_value(
    salts_lua_push_context *context, const cmeta_data_desc *data,
    const void *object) {
  if (context == NULL || context->state == NULL || object == NULL ||
      !cmeta_data_desc_valid(data))
    return CMETA_INVALID_ARGUMENT;
  switch (data->kind) {
    case CMETA_DATA_BOOL:
      lua_pushboolean(context->state, *(const _Bool *)object != 0);
      return CMETA_OK;
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT:
      return salts_lua_push_integer(context->state, data, object);
    case CMETA_DATA_FLOAT:
      return salts_lua_push_float(context->state, data, object);
    case CMETA_DATA_STRING:
    case CMETA_DATA_BYTES:
      return salts_lua_push_buffer(context, data, object);
    case CMETA_DATA_ENUM:
      return salts_lua_push_enum(context->state, data, object);
    case CMETA_DATA_STRUCT:
      return salts_lua_push_struct(context, data, object);
    case CMETA_DATA_SEQUENCE:
    case CMETA_DATA_SET:
      return salts_lua_push_collection(context, data, object);
    case CMETA_DATA_MAP:
      return salts_lua_push_map(context, data, object);
    default:
      return CMETA_TRAIT_MISSING;
  }
}

cmeta_status salts_lua_push_cmeta(
    lua_State *state, const cmeta_data_desc *data, const void *object,
    salts_lua_limits limits) {
  salts_lua_push_context context;
  if (state == NULL || object == NULL || !cmeta_data_desc_valid(data))
    return CMETA_INVALID_ARGUMENT;
  context.state = state;
  context.limits = limits;
  context.depth = 0u;
  return salts_lua_push_value(&context, data, object);
}

static cmeta_status salts_lua_read_integer(
    lua_State *state, int index, const cmeta_data_desc *data, void *object) {
  const cmeta_data_integer_shape *shape =
      (const cmeta_data_integer_shape *)data->shape;
  lua_Integer raw;
  if (shape == NULL || !lua_isinteger(state, index))
    return CMETA_TYPE_MISMATCH;
  raw = lua_tointeger(state, index);
  if (data->kind == CMETA_DATA_SINT) {
    switch (shape->bits) {
      case 8u: if (raw < INT8_MIN || raw > INT8_MAX) return CMETA_TYPE_MISMATCH;
                *(int8_t *)object = (int8_t)raw; return CMETA_OK;
      case 16u: if (raw < INT16_MIN || raw > INT16_MAX) return CMETA_TYPE_MISMATCH;
                 *(int16_t *)object = (int16_t)raw; return CMETA_OK;
      case 32u: if (raw < INT32_MIN || raw > INT32_MAX) return CMETA_TYPE_MISMATCH;
                 *(int32_t *)object = (int32_t)raw; return CMETA_OK;
      case 64u: *(int64_t *)object = (int64_t)raw; return CMETA_OK;
      default: return CMETA_TRAIT_MISSING;
    }
  }
  if (raw < 0) return CMETA_TYPE_MISMATCH;
  switch (shape->bits) {
    case 8u: if ((uint64_t)raw > UINT8_MAX) return CMETA_TYPE_MISMATCH;
              *(uint8_t *)object = (uint8_t)raw; return CMETA_OK;
    case 16u: if ((uint64_t)raw > UINT16_MAX) return CMETA_TYPE_MISMATCH;
               *(uint16_t *)object = (uint16_t)raw; return CMETA_OK;
    case 32u: if ((uint64_t)raw > UINT32_MAX) return CMETA_TYPE_MISMATCH;
               *(uint32_t *)object = (uint32_t)raw; return CMETA_OK;
    case 64u: *(uint64_t *)object = (uint64_t)raw; return CMETA_OK;
    default: return CMETA_TRAIT_MISSING;
  }
}

static cmeta_status salts_lua_read_float(
    lua_State *state, int index, const cmeta_data_desc *data, void *object) {
  const cmeta_data_float_shape *shape =
      (const cmeta_data_float_shape *)data->shape;
  lua_Number raw;
  if (shape == NULL || !lua_isnumber(state, index))
    return CMETA_TYPE_MISMATCH;
  raw = lua_tonumber(state, index);
  if (!isfinite((double)raw)) return CMETA_TYPE_MISMATCH;
  if (shape->bits == 32u) {
    if (raw < -(lua_Number)FLT_MAX || raw > (lua_Number)FLT_MAX)
      return CMETA_TYPE_MISMATCH;
    *(float *)object = (float)raw;
    return CMETA_OK;
  }
  if (shape->bits == 64u) {
    *(double *)object = (double)raw;
    return CMETA_OK;
  }
  return CMETA_TRAIT_MISSING;
}

static cmeta_status salts_lua_read_buffer(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    size_t max_bytes) {
  const char *bytes;
  size_t size;
  if (lua_type(state, index) != LUA_TSTRING) return CMETA_TYPE_MISMATCH;
  bytes = lua_tolstring(state, index, &size);
  if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
  return cmeta_data_buffer_assign(
      data, object, (const unsigned char *)bytes, size, max_bytes);
}

static cmeta_status salts_lua_read_enum(
    lua_State *state, int index, const cmeta_data_desc *data, void *object) {
  const cmeta_data_enum_shape *shape =
      (const cmeta_data_enum_shape *)data->shape;
  int64_t value;
  if (shape == NULL || shape->meta == NULL) return CMETA_INVALID_ARGUMENT;
  if (lua_type(state, index) == LUA_TSTRING) {
    if (!cmeta_enum_from_string(shape->meta, lua_tostring(state, index), &value))
      return CMETA_TYPE_MISMATCH;
  } else if (lua_isinteger(state, index)) {
    value = (int64_t)lua_tointeger(state, index);
  } else {
    return CMETA_TYPE_MISMATCH;
  }
  return cmeta_data_enum_assign(data, object, value);
}

static cmeta_status salts_lua_read_map_pair(
    lua_State *state, int key_index, int value_index,
    const cmeta_data_desc *data, cmeta_collector *collector,
    const cmeta_data_desc *key_data, const cmeta_data_desc *value_data,
    salts_lua_limits limits) {
  cmeta_data_temp key = {0};
  cmeta_data_temp value = {0};
  cmeta_status status;

  status = cmeta_data_temp_open(key_data, limits.max_bytes, &key);
  if (status != CMETA_OK) goto done;
  status = cmeta_data_temp_open(value_data, limits.max_bytes, &value);
  if (status != CMETA_OK) goto done;
  status = salts_lua_read_cmeta(
      state, key_index, key_data, key.storage, limits);
  if (status != CMETA_OK) goto done;
  status = salts_lua_read_cmeta(
      state, value_index, value_data, value.storage, limits);
  if (status != CMETA_OK) goto done;
  status = cmeta_data_map_accept(
      data, collector, key_data, key.storage, value_data, value.storage);

done:
  cmeta_data_temp_close(&value);
  cmeta_data_temp_close(&key);
  return status;
}

static cmeta_status salts_lua_read_map(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    salts_lua_limits limits) {
  const cmeta_data_map_ops *ops = cmeta_data_map_ops_of(data);
  const cmeta_data_desc *key_data;
  const cmeta_data_desc *value_data;
  cmeta_data_temp map = {0};
  cmeta_collector collector = {0};
  cmeta_status status;
  size_t count = 0u;
  int table_index;

  if (ops == NULL || !lua_istable(state, index)) return CMETA_TYPE_MISMATCH;
  key_data = ops->key(object);
  value_data = ops->value(object);
  if (key_data == NULL || value_data == NULL ||
      !cmeta_data_desc_valid(key_data) || !cmeta_data_desc_valid(value_data))
    return CMETA_TRAIT_MISSING;

  status = cmeta_data_temp_open(data, limits.max_bytes, &map);
  if (status != CMETA_OK) return status;
  status = cmeta_data_map_collector(
      data, map.storage, limits.max_items, &collector);
  if (status != CMETA_OK) goto done;
  status = cmeta_collector_begin(&collector);
  if (status != CMETA_OK) goto done;
  table_index = lua_absindex(state, index);

  if ((ops->flags & CMETA_DATA_MAP_REPEATED_KEYS) != 0u) {
    size_t i, length = (size_t)lua_rawlen(state, table_index);
    if (length > limits.max_items) {
      status = CMETA_CAPACITY_EXCEEDED; goto abort;
    }
    for (i = 0u; i < length; ++i) {
      lua_rawgeti(state, table_index, (lua_Integer)(i + 1u));
      if (!lua_istable(state, -1)) {
        lua_pop(state, 1); status = CMETA_TYPE_MISMATCH; goto abort;
      }
      lua_getfield(state, -1, "key");
      lua_getfield(state, -2, "value");
      status = salts_lua_read_map_pair(
          state, -2, -1, data, &collector, key_data, value_data, limits);
      lua_pop(state, 3);
      if (status != CMETA_OK) goto abort;
    }
  } else {
    lua_pushnil(state);
    while (lua_next(state, table_index) != 0) {
      if (count >= limits.max_items) {
        lua_pop(state, 2); status = CMETA_CAPACITY_EXCEEDED; goto abort;
      }
      status = salts_lua_read_map_pair(
          state, -2, -1, data, &collector, key_data, value_data, limits);
      lua_pop(state, 1);
      if (status != CMETA_OK) { lua_pop(state, 1); goto abort; }
      ++count;
    }
  }

  status = cmeta_collector_finish(&collector);
  if (status != CMETA_OK) goto done;
  status = cmeta_data_construct_move(data, object, map.storage);
  goto done;

abort:
  cmeta_collector_abort(&collector);
done:
  cmeta_data_temp_close(&map);
  return status;
}

static cmeta_status salts_lua_read_collection(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    salts_lua_limits limits) {
  const cmeta_data_collection_ops *ops = cmeta_data_collection_ops_of(data);
  const cmeta_data_desc *element_data;
  cmeta_data_temp container = {0};
  cmeta_collector collector = {0};
  size_t count, i;
  int table_index;
  cmeta_status status;

  if (ops == NULL || !lua_istable(state, index)) return CMETA_TYPE_MISMATCH;
  element_data = ops->element(object);
  if (element_data == NULL || !cmeta_data_desc_valid(element_data))
    return CMETA_TRAIT_MISSING;
  count = (size_t)lua_rawlen(state, index);
  if (count > limits.max_items) return CMETA_CAPACITY_EXCEEDED;

  status = cmeta_data_temp_open(data, limits.max_bytes, &container);
  if (status != CMETA_OK) return status;
  status = cmeta_data_collection_collector(
      data, container.storage, limits.max_items, &collector);
  if (status != CMETA_OK) goto done;
  status = cmeta_collector_begin(&collector);
  if (status != CMETA_OK) goto done;

  table_index = lua_absindex(state, index);
  for (i = 0u; i < count; ++i) {
    cmeta_data_temp element = {0};
    lua_rawgeti(state, table_index, (lua_Integer)(i + 1u));
    status = cmeta_data_temp_open(element_data, limits.max_bytes, &element);
    if (status == CMETA_OK)
      status = salts_lua_read_cmeta(
          state, -1, element_data, element.storage, limits);
    if (status == CMETA_OK)
      status = cmeta_data_collection_accept(
          data, &collector, element_data, element.storage);
    cmeta_data_temp_close(&element);
    lua_pop(state, 1);
    if (status != CMETA_OK) {
      cmeta_collector_abort(&collector);
      goto done;
    }
  }
  status = cmeta_collector_finish(&collector);
  if (status != CMETA_OK) goto done;
  status = cmeta_data_construct_move(data, object, container.storage);

done:
  cmeta_data_temp_close(&container);
  return status;
}

cmeta_status salts_lua_read_cmeta(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    salts_lua_limits limits) {
  if (state == NULL || object == NULL || !cmeta_data_desc_valid(data))
    return CMETA_INVALID_ARGUMENT;
  switch (data->kind) {
    case CMETA_DATA_BOOL:
      if (!lua_isboolean(state, index)) return CMETA_TYPE_MISMATCH;
      *(_Bool *)object = lua_toboolean(state, index) != 0;
      return CMETA_OK;
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT:
      return salts_lua_read_integer(state, index, data, object);
    case CMETA_DATA_FLOAT:
      return salts_lua_read_float(state, index, data, object);
    case CMETA_DATA_STRING:
    case CMETA_DATA_BYTES:
      return salts_lua_read_buffer(state, index, data, object, limits.max_bytes);
    case CMETA_DATA_ENUM:
      return salts_lua_read_enum(state, index, data, object);
    case CMETA_DATA_SEQUENCE:
    case CMETA_DATA_SET:
      return salts_lua_read_collection(state, index, data, object, limits);
    case CMETA_DATA_MAP:
      return salts_lua_read_map(state, index, data, object, limits);
    default:
      return CMETA_TRAIT_MISSING;
  }
}
