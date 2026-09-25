#include "cmeta_interface_function_fixture.h"

#ifdef _WIN32
#define CMETA_TEST_EXPORT __declspec(dllexport)
#else
#define CMETA_TEST_EXPORT __attribute__((visibility("default")))
#endif

/* Bootstrap has a fixed C signature. Refuse incompatible hosts before
 * publishing any native-layout descriptor graph. */
CMETA_TEST_EXPORT const cmeta_interface_desc *
cmeta_test_query_reflection(uint32_t requested_epoch) {
    if (requested_epoch != CMETA_REFLECTION_ABI_VERSION ||
        cmeta_reflection_abi_version() != CMETA_REFLECTION_ABI_VERSION)
        return NULL;
    return cmeta_reflected_counter_interface();
}
