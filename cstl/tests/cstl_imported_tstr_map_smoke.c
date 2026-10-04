#include <cstl/typed.h>
#include <salts_cmeta_data.h>

#include <stddef.h>

typed(Map, ImportSafeTstrSmokeMap, tstr, int,
      SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF,
      &cmeta_type_int, &cmeta_data_int);

int main(void) {
  ImportSafeTstrSmokeMap values = {0};
  tstr beta = tstr_dup("beta");
  tstr alpha = tstr_dup("alpha");
  tstr query = tstr_dup("alpha");
  const int *found;
  const cmeta_receiver_method_set *set;
  const cmeta_receiver_method *method;
  int rc = 0;

  if (beta == NULL || alpha == NULL || query == NULL) {
    rc = 1;
    goto cleanup_strings;
  }

  if (salts_tstr_cmeta_type.traits == NULL ||
      SALTS_TSTR_CMETA_TYPE_REF->traits == NULL ||
      (salts_tstr_cmeta_type.traits->flags & CMETA_TRAIT_COMPARE) == 0u ||
      (SALTS_TSTR_CMETA_TYPE_REF->traits->flags & CMETA_TRAIT_COMPARE) == 0u) {
    rc = 2;
    goto cleanup_strings;
  }

  if (!cmeta_type_equal(
          SALTS_TSTR_CMETA_TYPE_REF, &salts_tstr_cmeta_type) ||
      !cmeta_data_desc_equal(
          SALTS_TSTR_CMETA_DATA_REF, &salts_tstr_cmeta_data)) {
    rc = 3;
    goto cleanup_strings;
  }

  if (ImportSafeTstrSmokeMap_init(&values, 4u) != STL_OK) {
    rc = 4;
    goto cleanup_strings;
  }
  if (ImportSafeTstrSmokeMap_put(&values, beta, 20) != STL_OK ||
      ImportSafeTstrSmokeMap_put(&values, alpha, 10) != STL_OK) {
    rc = 5;
    goto cleanup_map;
  }

  found = ImportSafeTstrSmokeMap_get_const(&values, query);
  if (found == NULL || *found != 10 ||
      ImportSafeTstrSmokeMap_size(&values) != (size_t)2u) {
    rc = 6;
    goto cleanup_map;
  }

  set = ImportSafeTstrSmokeMap_receiver_method_set();
  if (!cmeta_receiver_method_set_valid(set) ||
      !cmeta_generic_desc_equal(set->owner, &stl_map_generic_desc)) {
    rc = 7;
    goto cleanup_map;
  }

  method = cmeta_receiver_method_find(set, "put");
  if (method == NULL ||
      method->function != ImportSafeTstrSmokeMap_put_function() ||
      method->abi != ImportSafeTstrSmokeMap_put_function_abi() ||
      !cmeta_type_equal(
          cmeta_function_param(method->function, 1u)->type,
          SALTS_TSTR_CMETA_TYPE_REF) ||
      !cmeta_type_equal(
          cmeta_function_param(method->function, 2u)->type,
          &cmeta_type_int)) {
    rc = 8;
    goto cleanup_map;
  }

cleanup_map:
  ImportSafeTstrSmokeMap_destroy(&values);
  if (rc == 0 &&
      (tstr_len(alpha) != (size_t)5u ||
       tstr_len(beta) != (size_t)4u ||
       tstr_len(query) != (size_t)5u))
    rc = 9;

cleanup_strings:
  tstr_free(alpha);
  tstr_free(beta);
  tstr_free(query);
  return rc;
}
