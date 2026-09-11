#include <cmeta/cmeta.h>

#ifdef _WIN32
#define OWNER_EXPORT __declspec(dllexport)
#else
#define OWNER_EXPORT __attribute__((visibility("default")))
#endif

/* Public CMeta macros also use descriptor addresses in C static initializers.
 * Keep this form covered: switching to imported DLL data would break it. */
static const cmeta_type_desc *const owner_types[] = { &cmeta_type_double_ptr };

OWNER_EXPORT const cmeta_type_desc *CMETA_OWNER_DIRECT(void) {
    return owner_types[0];
}

OWNER_EXPORT const cmeta_type_desc *CMETA_OWNER_FIND(const char *name) {
    return cmeta_type_find(name);
}
