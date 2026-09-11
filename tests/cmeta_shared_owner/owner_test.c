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
    const cmeta_type_desc *a = cmeta_owner_a_direct();
    const cmeta_type_desc *b = cmeta_owner_b_direct();
    /* The public contract is semantic identity, not process-global addresses. */
    if (!cmeta_type_equal(a, expected) || !cmeta_type_equal(b, expected) ||
        !cmeta_type_equal(a, b)) {
        fputs("CMeta type identity differs across DSOs\n", stderr);
        return 1;
    }
    if (cmeta_type_find("double") != expected->pointee ||
        cmeta_owner_a_find("double") != a->pointee ||
        cmeta_owner_b_find("double") != b->pointee) {
        fputs("CMeta registry and direct descriptors disagree within a DSO\n", stderr);
        return 1;
    }
    if (!cmeta_type_desc_valid(expected) || !cmeta_type_desc_valid(a) ||
        !cmeta_type_desc_valid(b)) {
        fputs("CMeta pointer descriptor is invalid\n", stderr);
        return 1;
    }
    return 0;
}
