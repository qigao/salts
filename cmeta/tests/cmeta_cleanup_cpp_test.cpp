#include "cmeta_cleanup_test.c"

namespace {
enum { MANAGED_SCOPE_CAPACITY = 4 };
using FallibleValue = int;
static int restored[MANAGED_SCOPE_CAPACITY];
static size_t restored_count, initialized_count, fail_at;
static cmeta_status fallible_init(FallibleValue *value) {
    *value = static_cast<int>(++initialized_count);
    return initialized_count == fail_at ? CMETA_CALLBACK_ERROR : CMETA_OK;
}
static void fallible_restore(FallibleValue *value) {
    if (restored_count < MANAGED_SCOPE_CAPACITY) restored[restored_count] = *value;
    ++restored_count;
    *value = 0;
}
static void fallible_move(FallibleValue *destination, FallibleValue *source) {
    *destination = *source;
    *source = 0;
}
CMETA_DEFINE_LIFECYCLE(FallibleValue, &cmeta_type_int, fallible_init,
    fallible_restore, fallible_move, CMETA_LIFECYCLE_MOVABLE)
static const cmeta_data_desc *FallibleValue_cmeta_data() {
    static const cmeta_data_desc data = [] {
        cmeta_data_desc value = cmeta_data_int;
        value.struct_size = sizeof(value);
        value.construct_ops = &FallibleValue_construct_ops;
        return value;
    }();
    return &data;
}
struct ScopeException { int value; };
static cmeta_status throw_from_body() { throw ScopeException{17}; }
static cmeta_status nested_managed_body() {
    cmeta_status status;
    cmeta_scope(status, cmeta_autos((FallibleValue, inner)),
        cmeta_body(throw_from_body()));
    return status;
}
static cmeta_status move_then_throw(FallibleValue *source, FallibleValue *destination) {
    fallible_restore(destination);
    fallible_move(destination, source);
    return throw_from_body();
}
}

suite("Managed C++ structured scope") {
    before_each() { restored_count = initialized_count = fail_at = 0; }
    it("restores two managed values once in reverse order before rethrowing") {
        cmeta_status status = CMETA_OK;
        bool caught = false;
        try {
            cmeta_scope(status, cmeta_autos((FallibleValue, first), (FallibleValue, second)),
                cmeta_body(throw_from_body()));
        } catch (const ScopeException &error) {
            caught = true;
            check_equal(error.value, 17);
            check_equal(restored_count, size_t{2});
        }
        check_true(caught);
        check_equal(restored_count, size_t{2});
        check_equal(restored[0], 2); check_equal(restored[1], 1);
    }
    it("uses the same exception cleanup after checked admission with trivial rows") {
        auto run = [] {
            cmeta_status status;
            cmeta_scope_checked(status, cmeta_autos((FallibleValue, first),
                (TrivialInt, middle, trivial), (FallibleValue, last)),
                cmeta_body(throw_from_body()));
            return status;
        };
        trivial_data = cmeta_data_int;
        trivial_data.struct_size = sizeof(trivial_data);
        trivial_data.construct_ops = &TrivialInt_construct_ops;
        check_throws_as(run(), ScopeException);
        check_equal(restored_count, size_t{2});
        check_equal(restored[0], 2); check_equal(restored[1], 1);
    }
    it("unwinds nested managed scopes inner before outer") {
        auto run = [] {
            cmeta_status status;
            cmeta_scope(status, cmeta_autos((FallibleValue, outer)),
                cmeta_body(nested_managed_body()));
            return status;
        };
        check_throws_as(run(), ScopeException);
        check_equal(restored_count, size_t{2});
        check_equal(restored[0], 2); check_equal(restored[1], 1);
    }
    it("restores the moved-from zero after the destination during unwind") {
        auto run = [] {
            cmeta_status status;
            cmeta_scope(status, cmeta_autos((FallibleValue, source), (FallibleValue, destination)),
                cmeta_body(move_then_throw(&source, &destination)));
            return status;
        };
        check_throws_as(run(), ScopeException);
        check_equal(restored_count, size_t{3});
        check_equal(restored[0], 2); check_equal(restored[1], 1); check_equal(restored[2], 0);
    }
    it("restores the failed partial initialization and prior live value without entering body") {
        cmeta_status status;
        fail_at = 2;
        cmeta_scope(status, cmeta_autos((FallibleValue, first), (FallibleValue, second)),
            cmeta_body(throw_from_body()));
        check_equal(status, CMETA_CALLBACK_ERROR);
        check_equal(restored_count, size_t{2});
        check_equal(restored[0], 2); check_equal(restored[1], 1);
    }
    it("preserves an ordinary body status while cleaning a mixed static scope") {
        cmeta_status status;
        cmeta_scope(status, cmeta_autos((TrivialInt, first, trivial), (FallibleValue, middle),
            (TrivialInt, last, trivial)), cmeta_body(CMETA_BUSY));
        check_equal(status, CMETA_BUSY);
        check_equal(restored_count, size_t{1});
        check_equal(restored[0], 1);
    }
}
