#include <cstl/typed.h>
#include <salts_cmeta_data.h>
#include <tinytest.h>

#include <string.h>

typed(Vec, ImportSafeTstrVec, tstr,
      SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF);

typed(Map, ImportSafeTstrIntMap, tstr, int,
      SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF,
      &cmeta_type_int, &cmeta_data_int);

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

  it("publishes lexical tstr comparison on exported and import-safe metadata") {
    tstr alpha = tstr_dup("alpha");
    tstr beta = tstr_dup("beta");
    tstr zero = NULL;

    check_not_null(alpha);
    check_not_null(beta);
    check_not_null(salts_tstr_cmeta_type.traits);
    check_not_null(SALTS_TSTR_CMETA_TYPE_REF->traits);
    check_true((salts_tstr_cmeta_type.traits->flags &
                CMETA_TRAIT_COMPARE) != 0u);
    check_true((SALTS_TSTR_CMETA_TYPE_REF->traits->flags &
                CMETA_TRAIT_COMPARE) != 0u);
    check_not_null(salts_tstr_cmeta_type.traits->compare);
    check_not_null(SALTS_TSTR_CMETA_TYPE_REF->traits->compare);

    check(salts_tstr_cmeta_type.traits->compare(&alpha, &beta) < 0);
    check(SALTS_TSTR_CMETA_TYPE_REF->traits->compare(&beta, &alpha) > 0);
    check_equal(
        salts_tstr_cmeta_type.traits->compare(&alpha, &alpha), 0);
    check_equal(
        SALTS_TSTR_CMETA_TYPE_REF->traits->compare(&zero, &zero), 0);
    check(
        SALTS_TSTR_CMETA_TYPE_REF->traits->compare(&zero, &alpha) < 0);

    check_true(cmeta_type_equal(
        SALTS_TSTR_CMETA_TYPE_REF, &salts_tstr_cmeta_type));
    check_true(cmeta_data_desc_equal(
        SALTS_TSTR_CMETA_DATA_REF, &salts_tstr_cmeta_data));

    tstr_free(alpha);
    tstr_free(beta);
  }

  it("uses import-safe tstr as an ordered typed Map key") {
    ImportSafeTstrIntMap values = {0};
    tstr beta = tstr_dup("beta");
    tstr alpha = tstr_dup("alpha");
    tstr query = tstr_dup("alpha");
    const int *found;

    check_not_null(beta);
    check_not_null(alpha);
    check_not_null(query);

    check_equal(ImportSafeTstrIntMap_init(&values, 4u), STL_OK);
    check_equal(ImportSafeTstrIntMap_put(&values, beta, 20), STL_OK);
    check_equal(ImportSafeTstrIntMap_put(&values, alpha, 10), STL_OK);
    check_equal(ImportSafeTstrIntMap_size(&values), (size_t)2u);

    found = ImportSafeTstrIntMap_get_const(&values, query);
    check_not_null(found);
    check_equal(*found, 10);

    check_true(cmeta_type_equal(
        cmeta_data_map_key_data(&ImportSafeTstrIntMap_map_data)
            ->storage_type,
        SALTS_TSTR_CMETA_TYPE_REF));
    check_true(cmeta_data_desc_equal(
        cmeta_data_map_key_data(&ImportSafeTstrIntMap_map_data),
        SALTS_TSTR_CMETA_DATA_REF));

    ImportSafeTstrIntMap_destroy(&values);

    check_equal(tstr_len(alpha), (size_t)5u);
    check_equal(tstr_len(beta), (size_t)4u);
    check_equal(tstr_len(query), (size_t)5u);
    tstr_free(alpha);
    tstr_free(beta);
    tstr_free(query);
  }

  it("preserves canonical Map receiver reflection for tstr keys") {
    const cmeta_receiver_method_set *set =
        ImportSafeTstrIntMap_receiver_method_set();
    const cmeta_receiver_method *method;

    check_true(cmeta_receiver_method_set_valid(set));
    check_equal(set->owner_name, "Map");

    method = cmeta_receiver_method_find(set, "put");
    check_not_null(method);
    check_true(
        method->function == ImportSafeTstrIntMap_put_function());
    check_true(
        method->abi == ImportSafeTstrIntMap_put_function_abi());
    check_true(cmeta_type_equal(
        cmeta_function_param(method->function, 1u)->type,
        SALTS_TSTR_CMETA_TYPE_REF));
    check_true(cmeta_type_equal(
        cmeta_function_param(method->function, 2u)->type,
        &cmeta_type_int));
  }
}
