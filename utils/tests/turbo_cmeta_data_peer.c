#include "turbo_cmeta_data.h"

const cmeta_data_desc *turbo_uuid_cmeta_data_from_peer(void) {
  return &turbo_uuid_cmeta_data;
}

const cmeta_type_desc *turbo_uuid_cmeta_type_from_peer(void) {
  return &turbo_uuid_cmeta_type;
}

const cmeta_data_buffer_shape *turbo_uuid_cmeta_shape_from_peer(void) {
  return &turbo_uuid_cmeta_shape;
}

const cmeta_data_buffer_ops *turbo_uuid_cmeta_buffer_ops_from_peer(void) {
  return &turbo_uuid_cmeta_buffer_ops;
}
