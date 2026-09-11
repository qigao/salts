#include <cmeta/cmeta.h>

#ifdef _WIN32
#define OWNER_EXPORT __declspec(dllexport)
#else
#define OWNER_EXPORT __attribute__((visibility("default")))
#endif

/* Each DSO must refer to the same library-owned descriptor and registry. */
OWNER_EXPORT const cmeta_type_desc *CMETA_OWNER_DIRECT(void) {
    return &cmeta_type_double_ptr;
}

OWNER_EXPORT const cmeta_type_desc *CMETA_OWNER_FIND(const char *name) {
    return cmeta_type_find(name);
}
