#include "turbo_cmeta_data.h"

#include <string.h>

static int turbo_uuid_cmeta_hex_value(unsigned char value) {
  if (value >= (unsigned char)'0' && value <= (unsigned char)'9')
    return (int)(value - (unsigned char)'0');
  if (value >= (unsigned char)'a' && value <= (unsigned char)'f')
    return (int)(value - (unsigned char)'a') + 10;
  if (value >= (unsigned char)'A' && value <= (unsigned char)'F')
    return (int)(value - (unsigned char)'A') + 10;
  return -1;
}

static bool turbo_uuid_cmeta_is_zero(const void *object) {
  static const turbo_uuid_t zero = {{0}};
  return object != NULL &&
         memcmp(((const turbo_uuid_t *)object)->bytes, zero.bytes, TURBO_UUID_SIZE) == 0;
}

static cmeta_status turbo_uuid_cmeta_assign(void *object, const unsigned char *data, size_t size,
                                            size_t max_bytes) {
  static const size_t group_ends[] = {4u, 6u, 8u, 10u};
  turbo_uuid_t parsed = {{0}};
  size_t text_index = 0u;
  size_t group_index = 0u;
  size_t byte_index;

  if (object == NULL || (size != 0u && data == NULL)) return CMETA_INVALID_ARGUMENT;
  if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
  if (!turbo_uuid_cmeta_is_zero(object)) return CMETA_INVALID_ARGUMENT;
  if (size != TURBO_UUID_STRING_LENGTH) return CMETA_INVALID_ARGUMENT;

  for (byte_index = 0u; byte_index < TURBO_UUID_SIZE; ++byte_index) {
    int high;
    int low;

    if (group_index < sizeof(group_ends) / sizeof(group_ends[0]) &&
        byte_index == group_ends[group_index]) {
      if (data[text_index] != (unsigned char)'-') return CMETA_INVALID_ARGUMENT;
      ++text_index;
      ++group_index;
    }

    high = turbo_uuid_cmeta_hex_value(data[text_index++]);
    low = turbo_uuid_cmeta_hex_value(data[text_index++]);
    if (high < 0 || low < 0) return CMETA_INVALID_ARGUMENT;
    parsed.bytes[byte_index] = (uint8_t)((high << 4) | low);
  }

  memcpy(object, &parsed, sizeof(parsed));
  return CMETA_OK;
}

static void turbo_uuid_cmeta_restore_zero(void *object) {
  if (object != NULL) memset(object, 0, sizeof(turbo_uuid_t));
}

static const cmeta_type_identity turbo_uuid_cmeta_identity = CMETA_TYPE_ID_ATOM_INIT("turbo.uuid");

TURBO_API const cmeta_type_desc turbo_uuid_cmeta_type = {
    "turbo_uuid_t", sizeof(turbo_uuid_t),      CMETA_ALIGNOF(turbo_uuid_t), CMETA_T_OBJECT, NULL,
    NULL,           &turbo_uuid_cmeta_identity};

TURBO_API const cmeta_data_buffer_shape turbo_uuid_cmeta_shape = {CMETA_DATA_BUFFER_OWNED};

TURBO_API const cmeta_data_buffer_ops turbo_uuid_cmeta_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION, &turbo_uuid_cmeta_type,
    CMETA_DATA_BUFFER_OWNED,       turbo_uuid_cmeta_is_zero,          turbo_uuid_cmeta_assign,
    turbo_uuid_cmeta_restore_zero};

TURBO_API const cmeta_data_desc turbo_uuid_cmeta_data = {sizeof(cmeta_data_desc),
                                                         CMETA_DATA_DESC_ABI_VERSION,
                                                         "turbo.uuid.data",
                                                         "turbo_uuid_t",
                                                         CMETA_DATA_STRING,
                                                         &turbo_uuid_cmeta_type,
                                                         &turbo_uuid_cmeta_shape,
                                                         &turbo_uuid_cmeta_buffer_ops,
                                                         NULL,
                                                         NULL};

TURBO_API bool turbo_uuid_cmeta_data_valid(const cmeta_data_desc *candidate) {
  const cmeta_data_buffer_ops *ops = cmeta_data_buffer_ops_of(candidate);

  if (ops == NULL) return false;

  return strcmp(candidate->stable_id, "turbo.uuid.data") == 0 &&
         strcmp(candidate->display_name, "turbo_uuid_t") == 0 &&
         candidate->kind == CMETA_DATA_STRING &&
         cmeta_type_equal(candidate->storage_type, &turbo_uuid_cmeta_type) &&
         candidate->storage_type->kind == turbo_uuid_cmeta_type.kind &&
         candidate->storage_type->size == sizeof(turbo_uuid_t) &&
         candidate->storage_type->align == CMETA_ALIGNOF(turbo_uuid_t) &&
         ((const cmeta_data_buffer_shape *)candidate->shape)->ownership ==
             CMETA_DATA_BUFFER_OWNED &&
         ops->abi_version == CMETA_DATA_BUFFER_OPS_ABI_VERSION &&
         ops->ownership == CMETA_DATA_BUFFER_OWNED &&
         ops->is_zero == turbo_uuid_cmeta_buffer_ops.is_zero &&
         ops->assign == turbo_uuid_cmeta_buffer_ops.assign &&
         ops->restore_zero == turbo_uuid_cmeta_buffer_ops.restore_zero;
}
