#include <cmeta/data.h>
#include "tinytest.h"

spec("CMeta semantic data descriptors") {
  it("exposes primitive semantic descriptors") {
    check_equal(cmeta_data_bool.kind, CMETA_DATA_BOOL);
    check_equal(cmeta_data_int.kind, CMETA_DATA_SINT);
    check_equal(cmeta_data_size.kind, CMETA_DATA_UINT);
    check_equal(cmeta_data_float.kind, CMETA_DATA_FLOAT);
    check_true(cmeta_data_desc_valid(&cmeta_data_bool));
  }

  it("keeps container categories free of T K V") {
    check_equal(cmeta_data_sequence.kind, CMETA_DATA_SEQUENCE);
    check_equal(cmeta_data_set.kind, CMETA_DATA_SET);
    check_equal(cmeta_data_map.kind, CMETA_DATA_MAP);
    check_null(cmeta_data_sequence.storage_type);
    check_null(cmeta_data_sequence.shape);
  }
}
