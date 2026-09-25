#include <cstl/typed.h>
#include <salts_cmeta_data.h>
#include <tinytest.h>

#include <string.h>

typed(Vec, ImportSafeTstrVec, tstr,
      SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF);

spec("CSTL imported semantic metadata") {
  it("uses an address-constant tstr semantic mirror in a DLL consumer") {
    ImportSafeTstrVec values = {0};
    tstr source = tstr_dup("managed");
    const tstr *stored;

    check_not_null(source);
    check_true(cmeta_type_equal(
        SALTS_TSTR_CMETA_TYPE_REF, &salts_tstr_cmeta_type));
    check_true(cmeta_data_desc_equal(
        SALTS_TSTR_CMETA_DATA_REF, &salts_tstr_cmeta_data));
    check_true(cmeta_data_collection_element_data(
                   &ImportSafeTstrVec_collection_data) ==
               SALTS_TSTR_CMETA_DATA_REF);
    check_true(cmeta_type_equal(
        ImportSafeTstrVec_collection_data.storage_type,
        ImportSafeTstrVec_collection_data.collection_ops->storage_type));

    check_equal(ImportSafeTstrVec_init(&values, 8u), STL_OK);
    check_equal(ImportSafeTstrVec_push(&values, source), STL_OK);
    check_equal(ImportSafeTstrVec_size(&values), (size_t)1u);

    stored = ImportSafeTstrVec_at_const(&values, 0u);
    check_not_null(stored);
    check_not_null(*stored);
    check_true(*stored != source);
    check_equal(tstr_len(*stored), (size_t)7u);
    check_equal(memcmp(*stored, "managed", 7u), 0);

    ImportSafeTstrVec_destroy(&values);
    check_equal(tstr_len(source), (size_t)7u);
    tstr_free(source);
  }
}
