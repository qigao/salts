#include <salts/bindings/lua/cmeta.h>

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
    case CMETA_DATA_STRUCT:
      return salts_lua_push_struct(context, data, object);
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

cmeta_status salts_lua_read_cmeta(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    salts_lua_limits limits) {
  (void)state; (void)index; (void)data; (void)object; (void)limits;
  return CMETA_TRAIT_MISSING;
}
