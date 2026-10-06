#ifndef CMETA_COMPILER_CLEANUP_CASES_H
#define CMETA_COMPILER_CLEANUP_CASES_H

#include <cmeta/compiler.h>

#if !CMETA_HAS_CLEANUP && defined(CMETA_ATTR_CLEANUP)
#error "Unsupported cleanup must not silently become an empty attribute"
#endif

#if CMETA_HAS_CLEANUP
enum { CLEANUP_FIRST = 1, CLEANUP_SECOND = 2, CLEANUP_RADIX = 10 };
typedef struct cleanup_record { int *order; int tag; } cleanup_record;
static void cleanup_record_release(cleanup_record *record) {
    *record->order = *record->order * CLEANUP_RADIX + record->tag;
}
static void cleanup_return_early(int *order) {
    CMETA_ATTR_CLEANUP(cleanup_record_release) cleanup_record first = {order,CLEANUP_FIRST};
    CMETA_ATTR_CLEANUP(cleanup_record_release) cleanup_record second = {order,CLEANUP_SECOND};
    (void)first;
    (void)second;
    return;
}
#endif

suite("CMeta cleanup compiler capability") {
    it("advertises native support without a weaker substitute") {
        check_true(CMETA_HAS_CLEANUP == 0 || CMETA_HAS_CLEANUP == 1);
    }
#if CMETA_HAS_CLEANUP
    it("runs resource callbacks in reverse order on early return") {
        int order = 0;
        cleanup_return_early(&order);
        check_equal(order,CLEANUP_SECOND * CLEANUP_RADIX + CLEANUP_FIRST);
    }
#endif
}

#endif
