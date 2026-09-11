#include <cmeta/cmeta.h>
#include <stdio.h>

#ifdef _WIN32
#define OWNER_IMPORT __declspec(dllimport)
#else
#define OWNER_IMPORT
#endif

OWNER_IMPORT const cmeta_type_desc *cmeta_owner_a_direct(void);
OWNER_IMPORT const cmeta_type_desc *cmeta_owner_b_direct(void);
OWNER_IMPORT const cmeta_type_desc *cmeta_owner_a_find(const char *name);
OWNER_IMPORT const cmeta_type_desc *cmeta_owner_b_find(const char *name);

int main(void) {
    const cmeta_type_desc *expected = &cmeta_type_double_ptr;
    if (cmeta_owner_a_direct() != expected || cmeta_owner_b_direct() != expected) {
        fputs("CMeta descriptor has more than one DSO owner\n", stderr);
        return 1;
    }
    if (cmeta_type_find(expected->name) != expected ||
        cmeta_owner_a_find(expected->name) != expected ||
        cmeta_owner_b_find(expected->name) != expected) {
        fputs("CMeta registry and direct descriptors disagree across DSOs\n", stderr);
        return 1;
    }
    if (!cmeta_type_desc_valid(expected) || expected->pointee != &cmeta_type_double) {
        fputs("CMeta pointer descriptor lost its type identity\n", stderr);
        return 1;
    }
    return 0;
}
