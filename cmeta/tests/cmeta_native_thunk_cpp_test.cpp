#include <cmeta/native/thunk.h>
#include <cmeta/native/object.h>
#include <cmeta/native/static_call.h>
#include "tinytest.hpp"
#include <stdexcept>

enum { NATIVE_CPP_BUDGET = 65536, NATIVE_CPP_THROW_VALUE = 17 };
FunctionDeclAsAbiResult(stateful, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    native_cpp_target, (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR));
int native_cpp_target(int value) {
    if (value == NATIVE_CPP_THROW_VALUE) throw std::runtime_error("native target");
    return value;
}
suite("Native C ABI headers and C++ unwind") {
    static cmeta_native_thunk thunk = CMETA_NATIVE_THUNK_INIT;
    after_each() { check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK); }
    it("links the ObjectRef adapter through its public C ABI") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        binding.kind = CMETA_NATIVE_CONTEXT_I32;
        check_equal(cmeta_native_object_i32_admit(nullptr, nullptr, nullptr, &binding), CMETA_INVALID_ARGUMENT);
        check_equal(binding.kind, CMETA_NATIVE_INVALID);
    }
    it("tail-jumps without adding a frame and propagates the target exception") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        check_equal(cmeta_native_i32_admit(FunctionAbi(native_cpp_target), native_cpp_target, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_CPP_BUDGET, &thunk), CMETA_OK);
        auto entry = cmeta_native_thunk_entry(&thunk);
        check_equal(entry(-1), -1);
        check_throws_as(entry(NATIVE_CPP_THROW_VALUE), std::runtime_error);
        check_equal(entry(1), 1);
    }
}
