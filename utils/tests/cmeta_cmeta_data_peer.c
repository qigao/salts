#include "cmeta_cmeta_data.h"

const cmeta_data_desc *cmeta_uuid_cmeta_data_from_peer(void) {
  return &cmeta_uuid_cmeta_data;
}

const cmeta_type_desc *cmeta_uuid_cmeta_type_from_peer(void) {
  return &cmeta_uuid_cmeta_type;
}

const cmeta_data_buffer_shape *cmeta_uuid_cmeta_shape_from_peer(void) {
  return &cmeta_uuid_cmeta_shape;
}

const cmeta_data_buffer_ops *cmeta_uuid_cmeta_buffer_ops_from_peer(void) {
  return &cmeta_uuid_cmeta_buffer_ops;
}

const cmeta_data_fixed_ops *cmeta_uuid_cmeta_fixed_ops_from_peer(void) {
  return &cmeta_uuid_cmeta_fixed_ops;
}
