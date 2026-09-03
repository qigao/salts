#include "salts_cmeta_data.h"

const cmeta_data_desc *salts_uuid_cmeta_data_from_peer(void) {
  return &salts_uuid_cmeta_data;
}

const cmeta_type_desc *salts_uuid_cmeta_type_from_peer(void) {
  return &salts_uuid_cmeta_type;
}

const cmeta_data_buffer_shape *salts_uuid_cmeta_shape_from_peer(void) {
  return &salts_uuid_cmeta_shape;
}

const cmeta_data_buffer_ops *salts_uuid_cmeta_buffer_ops_from_peer(void) {
  return &salts_uuid_cmeta_buffer_ops;
}
