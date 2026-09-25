#include "cmeta_interface_function_fixture.h"
#include "tinytest.h"
#include <uv.h>
#include <string.h>

typedef const cmeta_interface_desc *(*reflection_query_fn)(uint32_t);

/* The platform loader returns a symbol address. Copy it into the exact
 * bootstrap signature without aliasing a function-pointer object as void **. */
static reflection_query_fn reflection_query(uv_lib_t *module) {
    void *symbol = NULL;
    reflection_query_fn query = NULL;
    int status = uv_dlsym(module, "cmeta_test_query_reflection", &symbol);
    _Static_assert(sizeof(query) == sizeof(symbol),
                   "test loader requires equal symbol and function pointer sizes");
    check_equal(status, 0);
    if (status == 0) memcpy(&query, &symbol, sizeof(query));
    return query;
}

suite("CMeta reflection across a dynamically loaded module") {
    it("rejects other epochs before publishing descriptors") {
        uv_lib_t module = {0};
        int status = uv_dlopen(CMETA_TEST_PLUGIN_PATH, &module);
        check_equal(status, 0);
        if (status == 0) {
            reflection_query_fn query = reflection_query(&module);
            check_true(query != NULL);
            if (query != NULL) {
                check_null(query(CMETA_REFLECTION_ABI_VERSION - 1u));
                check_null(query(CMETA_REFLECTION_ABI_VERSION + 1u));
                check_true(cmeta_interface_desc_equal(
                    query(CMETA_REFLECTION_ABI_VERSION),
                    cmeta_reflected_counter_interface()));
            }
        }
        uv_dlclose(&module);
    }

    it("keeps borrowed metadata live through the last module reference") {
        uv_lib_t module = {0};
        uv_lib_t retained = {0};
        int status = uv_dlopen(CMETA_TEST_PLUGIN_PATH, &module);
        check_equal(status, 0);
        if (status == 0) {
            reflection_query_fn query = reflection_query(&module);
            status = uv_dlopen(CMETA_TEST_PLUGIN_PATH, &retained);
            check_equal(status, 0);
            if (status == 0 && query != NULL) {
                const cmeta_interface_desc *borrowed =
                    query(CMETA_REFLECTION_ABI_VERSION);
                uv_dlclose(&module);
                /* The consumer's retained loader handle now owns the lease. */
                check_true(cmeta_interface_desc_valid(borrowed));
                check_true(cmeta_interface_desc_equal(
                    borrowed, cmeta_reflected_counter_interface()));
                /* All borrowed uses finish before the final close; never
                 * call a validator on a pointer after that close. */
            } else {
                uv_dlclose(&module);
            }
            uv_dlclose(&retained);
        } else {
            uv_dlclose(&module);
        }
    }
}
