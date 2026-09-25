#include <cstl/byte_buffer.h>

#include <limits.h>
#include <string.h>

static cmeta_status stl_byte_buffer_cmeta_status(stl_status status) {
  switch (status) {
    case STL_OK:
      return CMETA_OK;
    case STL_OUT_OF_MEMORY:
      return CMETA_OUT_OF_MEMORY;
    case STL_CAPACITY_EXCEEDED:
      return CMETA_CAPACITY_EXCEEDED;
    case STL_TYPE_MISMATCH:
      return CMETA_TYPE_MISMATCH;
    case STL_TRAIT_MISSING:
      return CMETA_TRAIT_MISSING;
    case STL_INVALID_ARGUMENT:
    case STL_EMPTY:
    case STL_NOT_FOUND:
      return CMETA_INVALID_ARGUMENT;
  }
  return CMETA_CALLBACK_ERROR;
}

stl_status stl_byte_buffer_init(stl_byte_buffer *buffer, size_t byte_limit) {
  return buffer != NULL
             ? vec_init_bytes(&buffer->raw, sizeof(unsigned char),
                              _Alignof(unsigned char), byte_limit)
             : STL_INVALID_ARGUMENT;
}

stl_status stl_byte_buffer_from(stl_byte_buffer *buffer,
                                const unsigned char *bytes,
                                size_t size,
                                size_t byte_limit) {
  return buffer != NULL
             ? vec_from_array_bytes(&buffer->raw, bytes, size,
                                    sizeof(unsigned char),
                                    _Alignof(unsigned char), byte_limit)
             : STL_INVALID_ARGUMENT;
}

void stl_byte_buffer_destroy(stl_byte_buffer *buffer) {
  if (buffer != NULL)
    vec_destroy(&buffer->raw);
}

stl_status stl_byte_buffer_clear(stl_byte_buffer *buffer) {
  return buffer != NULL ? vec_clear(&buffer->raw) : STL_INVALID_ARGUMENT;
}

stl_status stl_byte_buffer_reserve(stl_byte_buffer *buffer, size_t capacity) {
  return buffer != NULL ? vec_reserve(&buffer->raw, capacity)
                        : STL_INVALID_ARGUMENT;
}

stl_status stl_byte_buffer_resize(stl_byte_buffer *buffer, size_t size) {
  return buffer != NULL ? vec_resize(&buffer->raw, size) : STL_INVALID_ARGUMENT;
}

stl_status stl_byte_buffer_push(stl_byte_buffer *buffer, unsigned char value) {
  return buffer != NULL ? vec_push(&buffer->raw, &value) : STL_INVALID_ARGUMENT;
}

stl_status stl_byte_buffer_pop(stl_byte_buffer *buffer, unsigned char *out_value) {
  return buffer != NULL ? vec_pop(&buffer->raw, out_value) : STL_INVALID_ARGUMENT;
}

unsigned char *stl_byte_buffer_at(stl_byte_buffer *buffer, size_t index) {
  return buffer != NULL ? (unsigned char *)vec_at(&buffer->raw, index) : NULL;
}

const unsigned char *stl_byte_buffer_at_const(const stl_byte_buffer *buffer,
                                              size_t index) {
  return buffer != NULL
             ? (const unsigned char *)vec_at_const(&buffer->raw, index)
             : NULL;
}

unsigned char *stl_byte_buffer_data(stl_byte_buffer *buffer) {
  return buffer != NULL ? (unsigned char *)vec_data(&buffer->raw) : NULL;
}

const unsigned char *stl_byte_buffer_data_const(const stl_byte_buffer *buffer) {
  return buffer != NULL
             ? (const unsigned char *)vec_data_const(&buffer->raw)
             : NULL;
}

size_t stl_byte_buffer_size(const stl_byte_buffer *buffer) {
  return buffer != NULL ? vec_size(&buffer->raw) : 0u;
}

size_t stl_byte_buffer_capacity(const stl_byte_buffer *buffer) {
  return buffer != NULL ? vec_capacity(&buffer->raw) : 0u;
}

bool stl_byte_buffer_empty(const stl_byte_buffer *buffer) {
  return buffer == NULL || vec_empty(&buffer->raw);
}

static bool stl_byte_buffer_cmeta_is_zero(const void *object) {
  const stl_byte_buffer *buffer = (const stl_byte_buffer *)object;
  return buffer != NULL && buffer->raw.initialized &&
         buffer->raw.element_type == NULL &&
         buffer->raw.elem_size == sizeof(unsigned char) &&
         buffer->raw.elem_align == _Alignof(unsigned char) &&
         vec_size(&buffer->raw) == 0u;
}

static cmeta_status stl_byte_buffer_cmeta_init_zero(void *object) {
  stl_byte_buffer *buffer = (stl_byte_buffer *)object;
  stl_status status;
  if (buffer == NULL)
    return CMETA_INVALID_ARGUMENT;
  memset(buffer, 0, sizeof(*buffer));
  status = stl_byte_buffer_init(buffer, SIZE_MAX);
  return stl_byte_buffer_cmeta_status(status);
}

static cmeta_status stl_byte_buffer_cmeta_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  stl_byte_buffer *buffer = (stl_byte_buffer *)object;
  stl_status status;

  if (buffer == NULL || (size != 0u && data == NULL))
    return CMETA_INVALID_ARGUMENT;
  if (size > max_bytes)
    return CMETA_CAPACITY_EXCEEDED;
  if (!stl_byte_buffer_cmeta_is_zero(buffer))
    return CMETA_INVALID_ARGUMENT;

  status = stl_byte_buffer_resize(buffer, size);
  if (status != STL_OK)
    return stl_byte_buffer_cmeta_status(status);
  if (size != 0u)
    memcpy(stl_byte_buffer_data(buffer), data, size);
  return CMETA_OK;
}

static void stl_byte_buffer_cmeta_restore_zero(void *object) {
  stl_byte_buffer *buffer = (stl_byte_buffer *)object;
  if (buffer == NULL)
    return;
  if (buffer->raw.initialized)
    vec_raw_destroy_storage(&buffer->raw);
  memset(buffer, 0, sizeof(*buffer));
  (void)stl_byte_buffer_init(buffer, SIZE_MAX);
}

static cmeta_status stl_byte_buffer_cmeta_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  const stl_byte_buffer *buffer = (const stl_byte_buffer *)object;
  if (buffer == NULL || out_data == NULL || out_size == NULL ||
      !buffer->raw.initialized || buffer->raw.element_type != NULL ||
      buffer->raw.elem_size != sizeof(unsigned char) ||
      buffer->raw.elem_align != _Alignof(unsigned char))
    return CMETA_INVALID_ARGUMENT;
  *out_data = stl_byte_buffer_data_const(buffer);
  *out_size = stl_byte_buffer_size(buffer);
  return CMETA_OK;
}

static void stl_byte_buffer_cmeta_move(void *destination, void *source) {
  stl_byte_buffer *to = (stl_byte_buffer *)destination;
  stl_byte_buffer *from = (stl_byte_buffer *)source;
  if (to == NULL || from == NULL)
    return;
  if (to->raw.initialized)
    vec_raw_destroy_storage(&to->raw);
  *to = *from;
  memset(from, 0, sizeof(*from));
  (void)stl_byte_buffer_init(from, SIZE_MAX);
}

static const cmeta_type_identity stl_byte_buffer_cmeta_identity =
    CMETA_TYPE_ID_ATOM_INIT("cstl.byte_buffer");

const cmeta_type_desc stl_byte_buffer_cmeta_type = {
    "stl_byte_buffer", sizeof(stl_byte_buffer), _Alignof(stl_byte_buffer),
    CMETA_T_OBJECT, NULL, NULL, &stl_byte_buffer_cmeta_identity
};

const cmeta_data_buffer_shape stl_byte_buffer_cmeta_shape = {
    CMETA_DATA_BUFFER_OWNED
};

const cmeta_data_buffer_ops stl_byte_buffer_cmeta_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &stl_byte_buffer_cmeta_type, CMETA_DATA_BUFFER_OWNED,
    stl_byte_buffer_cmeta_is_zero, stl_byte_buffer_cmeta_assign,
    stl_byte_buffer_cmeta_restore_zero, stl_byte_buffer_cmeta_read,
    stl_byte_buffer_cmeta_init_zero, stl_byte_buffer_cmeta_move
};

const cmeta_data_desc stl_byte_buffer_cmeta_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "cstl.byte_buffer.data",
    .display_name = "stl_byte_buffer",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &stl_byte_buffer_cmeta_type,
    .shape = &stl_byte_buffer_cmeta_shape,
    .buffer_ops = &stl_byte_buffer_cmeta_buffer_ops
};
