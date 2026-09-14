#include "cmeta_fixed_bytes_fixture.h"

const cmeta_data_desc *cmeta_fixed_bytes_fixture_from_peer(void) {
    return &cmeta_fixed_bytes_fixture_value_cmeta_data;
}

const cmeta_data_fixed_ops *cmeta_fixed_bytes_fixture_ops_from_peer(void) {
    return &cmeta_fixed_bytes_fixture_value_cmeta_fixed_ops;
}
