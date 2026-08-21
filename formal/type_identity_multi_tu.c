#include "type_identity_tu_shared.h"

#include <stdio.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "multi-TU TypeId check failed: %s\n", #expr); \
            return 1; \
        } \
    } while (0)

int main(void) {
    const cmeta_type_desc *a = cmeta_tu_a_desc();
    const cmeta_type_desc *b = cmeta_tu_b_desc();
    const cmeta_type_identity *aid = cmeta_tu_a_identity();
    const cmeta_type_identity *bid = cmeta_tu_b_identity();
    const cmeta_type_desc *ap = cmeta_tu_a_ptr_desc();
    const cmeta_type_desc *bp = cmeta_tu_b_ptr_desc();
    const cmeta_type_identity *apid = cmeta_tu_a_ptr_identity();
    const cmeta_type_identity *bpid = cmeta_tu_b_ptr_identity();

    CHECK(a != b);
    CHECK(aid != bid);
    CHECK(cmeta_type_identity_equal(aid, bid));
    CHECK(cmeta_type_equal(a, b));
    CHECK(cmeta_type_desc_valid(a));
    CHECK(cmeta_type_desc_valid(b));

    CHECK(ap != bp);
    CHECK(apid != bpid);
    CHECK(cmeta_type_identity_equal(apid, bpid));
    CHECK(cmeta_type_equal(ap, bp));
    CHECK(cmeta_type_desc_valid(ap));
    CHECK(cmeta_type_desc_valid(bp));

    CHECK(!cmeta_type_equal(a, cmeta_tu_b_different_same_layout_desc()));
    CHECK(!cmeta_type_equal(a, cmeta_tu_b_legacy_same_layout_desc()));

    puts("multi-TU descriptor TypeId applicability: ok");
    return 0;
}
