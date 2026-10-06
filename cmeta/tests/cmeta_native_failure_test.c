#include "cmeta_native_targets.h"
#include "../native/code_memory.h"
#include "tinytest.h"

/* Link-only fake OS adapter; no executable allocation and no production hook.
 * These cases exercise the caller's publication/ownership transaction. */
enum { FAILURE_PAGE_BYTES = 128 };
static unsigned char failure_page[FAILURE_PAGE_BYTES];
static bool fail_create, fail_write, fail_publish, fail_destroy;
static size_t live_allocations;
cmeta_status cmeta_native_memory_create(size_t budget, cmeta_native_thunk *thunk) {
    if (budget < sizeof(failure_page)) return CMETA_CAPACITY_EXCEEDED;
    if (fail_create) return CMETA_OUT_OF_MEMORY;
    thunk->allocation = failure_page;
    thunk->allocation_size = sizeof(failure_page);
    thunk->state = CMETA_NATIVE_WRITABLE;
    ++live_allocations;
    return CMETA_OK;
}
cmeta_status cmeta_native_memory_write(cmeta_native_thunk *thunk) {
    if (fail_write) return CMETA_CALLBACK_ERROR;
    thunk->state = CMETA_NATIVE_WRITABLE;
    return CMETA_OK;
}
cmeta_status cmeta_native_memory_publish(cmeta_native_thunk *thunk) {
    thunk->state = fail_publish ? CMETA_NATIVE_UNPUBLISHED : CMETA_NATIVE_READY;
    return fail_publish ? CMETA_CALLBACK_ERROR : CMETA_OK;
}
cmeta_status cmeta_native_memory_destroy(cmeta_native_thunk *thunk) {
    (void)thunk;
    if (fail_destroy) return CMETA_CALLBACK_ERROR;
    --live_allocations;
    return CMETA_OK;
}

suite("Native publication failure transactions") {
    static cmeta_native_thunk thunk;
    static cmeta_native_binding binding;
    before_each() {
        const cmeta_native_thunk empty = CMETA_NATIVE_THUNK_INIT;
        thunk = empty;
        fail_create = fail_write = fail_publish = fail_destroy = false;
        live_allocations = 0;
        check_equal(cmeta_native_i32_admit(FunctionAbi(native_test_identity), native_test_identity, &binding), CMETA_OK);
    }
    after_each() {
        fail_destroy = false;
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        check_equal(live_allocations, (size_t)0);
    }
    it("leaves allocation failure empty and keeps failed publication releasable") {
        fail_create = true;
        check_equal(cmeta_native_thunk_create(&binding, sizeof(failure_page), &thunk), CMETA_OUT_OF_MEMORY);
        check_null(thunk.allocation);
        fail_create = false;
        fail_publish = true;
        check_equal(cmeta_native_thunk_create(&binding, sizeof(failure_page), &thunk), CMETA_CALLBACK_ERROR);
        check_true(cmeta_native_thunk_entry(&thunk) == NULL);
        check_equal(live_allocations, (size_t)1);
        fail_publish = false;
        check_equal(cmeta_native_thunk_rebind(&thunk, &binding), CMETA_OK);
        check_true(cmeta_native_thunk_entry(&thunk) != NULL);
    }
    it("preserves the old binding on write denial and hides partially published code") {
        cmeta_native_i32_fn entry;
        check_equal(cmeta_native_thunk_create(&binding, sizeof(failure_page), &thunk), CMETA_OK);
        entry = cmeta_native_thunk_entry(&thunk);
        fail_write = true;
        check_equal(cmeta_native_thunk_rebind(&thunk, &binding), CMETA_CALLBACK_ERROR);
        check_true(cmeta_native_thunk_entry(&thunk) == entry);
        fail_write = false;
        fail_publish = true;
        check_equal(cmeta_native_thunk_rebind(&thunk, &binding), CMETA_CALLBACK_ERROR);
        check_true(cmeta_native_thunk_entry(&thunk) == NULL);
        fail_destroy = true;
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_CALLBACK_ERROR);
        check_equal(live_allocations, (size_t)1);
        check_not_null(thunk.allocation);
    }
}
