#include <salts/bindings/member.hpp>

#include "tinytest.hpp"

#include <cstddef>
#include <type_traits>

struct member_counter {
  int value;

  int add(int delta) {
    value += delta;
    return value;
  }

  int preview(int delta) const {
    return value + delta;
  }
};

struct member_other {
  int value;
};

struct member_virtual_base {
  virtual int scale(int value) const {
    return value;
  }
};

struct member_virtual_derived : member_virtual_base {
  int factor;

  explicit member_virtual_derived(int value) : factor(value) {}

  int scale(int value) const override {
    return factor * value;
  }
};

static const cmeta_type_desc member_counter_type = {
    "member_counter",
    sizeof(member_counter),
    alignof(member_counter),
    CMETA_T_OBJECT,
    nullptr,
    nullptr,
    nullptr
};

static const cmeta_type_desc member_counter_ptr_type = {
    "member_counter *",
    sizeof(member_counter *),
    alignof(member_counter *),
    CMETA_T_POINTER,
    &member_counter_type,
    nullptr,
    nullptr
};

static int member_counter_shape_marker = 0;

static const cmeta_data_desc member_counter_data = [] {
  cmeta_data_desc value{};
  value.struct_size = sizeof(cmeta_data_desc);
  value.abi_version = CMETA_DATA_DESC_ABI_VERSION;
  value.stable_id = "test.member_counter.data";
  value.display_name = "member_counter";
  value.kind = CMETA_DATA_CUSTOM;
  value.storage_type = &member_counter_type;
  value.shape = &member_counter_shape_marker;
  return value;
}();

static const cmeta_param_desc member_add_params[] = {
    {
        sizeof(cmeta_param_desc),
        "self",
        &member_counter_ptr_type,
        CMETA_PARAM_INOUT | CMETA_PARAM_BORROWED |
            CMETA_PARAM_RECEIVER
    },
    {
        sizeof(cmeta_param_desc),
        "delta",
        &cmeta_type_int,
        CMETA_PARAM_IN
    }
};

static const cmeta_function_desc member_add_function = {
    sizeof(cmeta_function_desc),
    "member_counter.add",
    &cmeta_type_int,
    member_add_params,
    2u,
    CMETA_EFFECT_STATEFUL,
    CMETA_PROP_NONE
};

static const cmeta_abi_carrier member_add_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER,
    CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc member_add_abi = {
    sizeof(cmeta_function_abi_desc),
    &member_add_function,
    CMETA_ABI_SCALAR,
    member_add_param_abi,
    2u
};

static const cmeta_param_desc member_preview_params[] = {
    {
        sizeof(cmeta_param_desc),
        "self",
        &member_counter_ptr_type,
        CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
            CMETA_PARAM_RECEIVER
    },
    {
        sizeof(cmeta_param_desc),
        "delta",
        &cmeta_type_int,
        CMETA_PARAM_IN
    }
};

static const cmeta_function_desc member_preview_function = {
    sizeof(cmeta_function_desc),
    "member_counter.preview",
    &cmeta_type_int,
    member_preview_params,
    2u,
    CMETA_EFFECT_PURE,
    CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL
};

static const cmeta_abi_carrier member_preview_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER,
    CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc member_preview_abi = {
    sizeof(cmeta_function_abi_desc),
    &member_preview_function,
    CMETA_ABI_SCALAR,
    member_preview_param_abi,
    2u
};

static const cmeta_receiver_method member_counter_methods[] = {
    {
        "add",
        &member_add_function,
        &member_add_abi
    },
    {
        "preview",
        &member_preview_function,
        &member_preview_abi
    }
};

static const cmeta_receiver_method_set member_counter_method_set = {
    sizeof(cmeta_receiver_method_set),
    &member_counter_type,
    member_counter_methods,
    2u,
    "member_counter"
};

static const cmeta_param_desc member_add_projected_params[] = {
    {
        sizeof(cmeta_param_desc),
        "delta",
        &cmeta_type_int,
        CMETA_PARAM_IN
    }
};

static const cmeta_function_desc member_add_projected_function = {
    sizeof(cmeta_function_desc),
    "member_counter.bound_add",
    &cmeta_type_int,
    member_add_projected_params,
    1u,
    CMETA_EFFECT_STATEFUL,
    CMETA_PROP_NONE
};

static const cmeta_data_desc *const member_add_projected_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc member_add_projected_data = {
    sizeof(cmeta_function_data_desc),
    &member_add_projected_function,
    &cmeta_data_int,
    member_add_projected_data_params,
    1u
};

static const cmeta_param_desc member_preview_projected_params[] = {
    {
        sizeof(cmeta_param_desc),
        "delta",
        &cmeta_type_int,
        CMETA_PARAM_IN
    }
};

static const cmeta_function_desc member_preview_projected_function = {
    sizeof(cmeta_function_desc),
    "member_counter.bound_preview",
    &cmeta_type_int,
    member_preview_projected_params,
    1u,
    CMETA_EFFECT_PURE,
    CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL
};

static const cmeta_data_desc *const member_preview_projected_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc member_preview_projected_data = {
    sizeof(cmeta_function_data_desc),
    &member_preview_projected_function,
    &cmeta_data_int,
    member_preview_projected_data_params,
    1u
};

static const cmeta_type_desc member_virtual_base_type = {
    "member_virtual_base",
    sizeof(member_virtual_base),
    alignof(member_virtual_base),
    CMETA_T_OBJECT,
    nullptr,
    nullptr,
    nullptr
};

static const cmeta_type_desc member_virtual_base_ptr_type = {
    "member_virtual_base *",
    sizeof(member_virtual_base *),
    alignof(member_virtual_base *),
    CMETA_T_POINTER,
    &member_virtual_base_type,
    nullptr,
    nullptr
};

static int member_virtual_shape_marker = 0;

static const cmeta_data_desc member_virtual_data = [] {
  cmeta_data_desc value{};
  value.struct_size = sizeof(cmeta_data_desc);
  value.abi_version = CMETA_DATA_DESC_ABI_VERSION;
  value.stable_id = "test.member_virtual_base.data";
  value.display_name = "member_virtual_base";
  value.kind = CMETA_DATA_CUSTOM;
  value.storage_type = &member_virtual_base_type;
  value.shape = &member_virtual_shape_marker;
  return value;
}();

static const cmeta_param_desc member_scale_params[] = {
    {
        sizeof(cmeta_param_desc),
        "self",
        &member_virtual_base_ptr_type,
        CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
            CMETA_PARAM_RECEIVER
    },
    {
        sizeof(cmeta_param_desc),
        "value",
        &cmeta_type_int,
        CMETA_PARAM_IN
    }
};

static const cmeta_function_desc member_scale_function = {
    sizeof(cmeta_function_desc),
    "member_virtual_base.scale",
    &cmeta_type_int,
    member_scale_params,
    2u,
    CMETA_EFFECT_PURE,
    CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL
};

static const cmeta_abi_carrier member_scale_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER,
    CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc member_scale_abi = {
    sizeof(cmeta_function_abi_desc),
    &member_scale_function,
    CMETA_ABI_SCALAR,
    member_scale_param_abi,
    2u
};

static const cmeta_receiver_method member_scale_methods[] = {
    {
        "scale",
        &member_scale_function,
        &member_scale_abi
    }
};

static const cmeta_receiver_method_set member_scale_method_set = {
    sizeof(cmeta_receiver_method_set),
    &member_virtual_base_type,
    member_scale_methods,
    1u,
    "member_virtual_base"
};

static const cmeta_param_desc member_scale_projected_params[] = {
    {
        sizeof(cmeta_param_desc),
        "value",
        &cmeta_type_int,
        CMETA_PARAM_IN
    }
};

static const cmeta_function_desc member_scale_projected_function = {
    sizeof(cmeta_function_desc),
    "member_virtual_base.bound_scale",
    &cmeta_type_int,
    member_scale_projected_params,
    1u,
    CMETA_EFFECT_PURE,
    CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL
};

static const cmeta_data_desc *const member_scale_projected_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc member_scale_projected_data = {
    sizeof(cmeta_function_data_desc),
    &member_scale_projected_function,
    &cmeta_data_int,
    member_scale_projected_data_params,
    1u
};

static_assert(
    std::is_member_function_pointer_v<decltype(&member_counter::add)>);
static_assert(
    std::is_member_function_pointer_v<decltype(&member_counter::preview)>);
static_assert(
    std::is_member_function_pointer_v<decltype(&member_virtual_base::scale)>);

spec("C++ exact member-function thunk") {
  it("binds complete non-const and const member sets") {
    Salts::MemberMethodProvider provider{
        &member_counter_method_set,
        Salts::member_method<&member_counter::add>(
            &member_counter_methods[0],
            &member_add_projected_data),
        Salts::member_method<&member_counter::preview>(
            &member_counter_methods[1],
            &member_preview_projected_data)};

    check_true(provider.valid());
    check_true(provider.accepts<member_counter>());
    check_false(provider.accepts<member_other>());
    check_not_null(provider.c_provider());

    member_counter counter{1};
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    check_equal(
        cmeta_object_borrow_with_provider(
            &object, &counter, &member_counter_data,
            provider.c_provider()),
        CMETA_OK);

    int delta = 4;
    const void *args[] = {&delta};
    int result = 0;
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;

    check_equal(
        cmeta_object_method_invokable_bind(
            &object, &member_counter_methods[0], &invokable),
        CMETA_OK);
    check_equal(
        invokable.callable.capture_size, sizeof(void *));
    check_equal(
        cmeta_invokable_invoke(&invokable, &result, args),
        CMETA_OK);
    check_equal(result, 5);
    check_equal(counter.value, 5);

    delta = 3;
    result = 0;
    invokable = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    check_equal(
        cmeta_object_method_invokable_bind(
            &object, &member_counter_methods[1], &invokable),
        CMETA_OK);
    check_equal(
        cmeta_invokable_invoke(&invokable, &result, args),
        CMETA_OK);
    check_equal(result, 8);
    check_equal(counter.value, 5);

    cmeta_object_release(&object);
  }

  it("fails closed when provider entries do not cover the reflected set in order") {
    Salts::MemberMethodProvider provider{
        &member_counter_method_set,
        Salts::member_method<&member_counter::preview>(
            &member_counter_methods[1],
            &member_preview_projected_data),
        Salts::member_method<&member_counter::add>(
            &member_counter_methods[0],
            &member_add_projected_data)};

    check_false(provider.valid());
    check_null(provider.c_provider());
  }

  it("lets the compiler perform virtual dispatch through the baked member pointer") {
    Salts::MemberMethodProvider provider{
        &member_scale_method_set,
        Salts::member_method<&member_virtual_base::scale>(
            &member_scale_methods[0],
            &member_scale_projected_data)};

    check_true(provider.valid());
    check_true(provider.accepts<member_virtual_base>());

    member_virtual_derived derived{3};
    member_virtual_base &base = derived;
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    check_equal(
        cmeta_object_borrow_with_provider(
            &object, &base, &member_virtual_data,
            provider.c_provider()),
        CMETA_OK);

    int value = 4;
    const void *args[] = {&value};
    int result = 0;
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;

    check_equal(
        cmeta_object_method_invokable_bind(
            &object, &member_scale_methods[0], &invokable),
        CMETA_OK);
    check_equal(
        cmeta_invokable_invoke(&invokable, &result, args),
        CMETA_OK);
    check_equal(result, 12);
    check_equal(
        invokable.callable.capture_size, sizeof(void *));

    cmeta_object_release(&object);
  }
}
