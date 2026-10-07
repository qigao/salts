#include "tinytest.hpp"
#include "cmeta_data_reflect_cases.h"
#include "cmeta_data_reflect_provider_cases.h"

struct NativeNontrivialView {
    int value;
    NativeNontrivialView() : value(1) {}
    ~NativeNontrivialView() { value = 0; }
};
cmeta_reflect_data(NativeNontrivialView, "test.native.NontrivialView",
    cmeta_field(int, value)
);

suite("C++ native reflection lifecycle") {
    it("allows a nontrivial native read view without construction authority") {
        NativeNontrivialView source;
        const cmeta_data_desc *data = cmeta_reflected_data(NativeNontrivialView);
        cmeta_object_ref object;
        const cmeta_data_desc *field_data = nullptr;
        const void *field = nullptr;
        check_true(cmeta_data_desc_valid(data));
        check_false(cmeta_data_struct_constructible(data));
        check_false(cmeta_data_value_traits_supported(data));
        check_equal(cmeta_object_borrow(&object, &source, data, nullptr), CMETA_OK);
        check_equal(cmeta_object_field_read(&object, "value", &field_data, &field), CMETA_OK);
        check_equal(*static_cast<const int *>(field), source.value);
        cmeta_object_release(&object);
    }
}
