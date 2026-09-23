#include "tinytest.h"
#include "tinymock_actions.h"

#include <cmeta/cmeta.h>

typedef struct tinymock_action_managed {
  int value;
} tinymock_action_managed;

static size_t action_copy_count;
static size_t action_destroy_count;

static bool action_managed_copy(void *destination, const void *source) {
  tinymock_action_managed *dst = (tinymock_action_managed *)destination;
  const tinymock_action_managed *src =
      (const tinymock_action_managed *)source;
  if (!dst || !src) return false;
  *dst = *src;
  ++action_copy_count;
  return true;
}

static void action_managed_destroy(void *value) {
  (void)value;
  ++action_destroy_count;
}

static const cmeta_type_traits action_managed_traits = {
  .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY,
  .copy_construct = action_managed_copy,
  .destroy = action_managed_destroy
};

static const cmeta_type_desc action_managed_type = {
  .name = "tinymock_action_managed",
  .size = sizeof(tinymock_action_managed),
  .align = _Alignof(tinymock_action_managed),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &action_managed_traits,
  .identity = NULL
};

static const cmeta_type_desc action_managed_ptr_type = {
  .name = "tinymock_action_managed *",
  .size = sizeof(tinymock_action_managed *),
  .align = _Alignof(tinymock_action_managed *),
  .kind = CMETA_T_POINTER,
  .pointee = &action_managed_type,
  .traits = NULL,
  .identity = NULL
};

static const cmeta_param_desc action_out_params[] = {
  {
    sizeof(cmeta_param_desc),
    "out",
    &action_managed_ptr_type,
    CMETA_PARAM_OUT
  }
};

static const cmeta_function_desc action_out_function = {
  sizeof(cmeta_function_desc),
  "tinymock_action_out",
  &cmeta_type_int,
  action_out_params,
  1u,
  CMETA_EFFECT_PURE,
  CMETA_PROP_NONE
};

static const cmeta_param_desc action_inout_params[] = {
  {
    sizeof(cmeta_param_desc),
    "value",
    &action_managed_ptr_type,
    CMETA_PARAM_INOUT
  }
};

static const cmeta_function_desc action_inout_function = {
  sizeof(cmeta_function_desc),
  "tinymock_action_inout",
  &cmeta_type_int,
  action_inout_params,
  1u,
  CMETA_EFFECT_PURE,
  CMETA_PROP_NONE
};

suite("TinyMock reflected output actions") {
  it("constructs nontrivial OUT values and releases scripted storage") {
    tinymock_cmeta_actions actions;
    tinymock_action_managed scripted = {17};
    tinymock_action_managed output = {0};
    tinymock_value_t args[] = {TINYMOCk_VALUE(&output)};

    action_copy_count = 0u;
    action_destroy_count = 0u;

    tinymock_cmeta_actions_init(&actions, &action_out_function);
    check_true(tinymock_cmeta_actions_set_output_name(
        &actions, &action_out_function, "out", &scripted));
    check_equal(action_copy_count, (size_t)1);

    check_true(tinymock_cmeta_actions_apply(
        &actions, &action_out_function, 1u, args));
    check_equal(output.value, 17);
    check_equal(action_copy_count, (size_t)2);
    check_equal(action_destroy_count, (size_t)0);

    tinymock_cmeta_actions_destroy(&actions);
    check_equal(action_destroy_count, (size_t)1);

    action_managed_destroy(&output);
    check_equal(action_destroy_count, (size_t)2);
  }

  it("replaces nontrivial INOUT values with destroy then copy") {
    tinymock_cmeta_actions actions;
    tinymock_action_managed scripted = {44};
    tinymock_action_managed value = {3};
    tinymock_value_t args[] = {TINYMOCk_VALUE(&value)};

    action_copy_count = 0u;
    action_destroy_count = 0u;

    tinymock_cmeta_actions_init(&actions, &action_inout_function);
    check_true(tinymock_cmeta_actions_set_output_name(
        &actions, &action_inout_function, "value", &scripted));
    check_equal(action_copy_count, (size_t)1);

    check_true(tinymock_cmeta_actions_apply(
        &actions, &action_inout_function, 1u, args));
    check_equal(value.value, 44);
    check_equal(action_copy_count, (size_t)2);
    check_equal(action_destroy_count, (size_t)1);

    tinymock_cmeta_actions_destroy(&actions);
    check_equal(action_destroy_count, (size_t)2);

    action_managed_destroy(&value);
    check_equal(action_destroy_count, (size_t)3);
  }

  it("reset releases the owned scripted value") {
    tinymock_cmeta_actions actions;
    tinymock_action_managed scripted = {12};

    action_copy_count = 0u;
    action_destroy_count = 0u;

    tinymock_cmeta_actions_init(&actions, &action_out_function);
    check_true(tinymock_cmeta_actions_set_output_name(
        &actions, &action_out_function, "out", &scripted));
    check_equal(action_copy_count, (size_t)1);
    check_equal(action_destroy_count, (size_t)0);

    tinymock_cmeta_actions_reset(&actions, &action_out_function);
    check_equal(action_destroy_count, (size_t)1);

    tinymock_cmeta_actions_destroy(&actions);
    check_equal(action_destroy_count, (size_t)1);
  }

  it("destroys replaced scripted values and rejects null nonnullable output") {
    tinymock_cmeta_actions actions;
    tinymock_action_managed first = {1};
    tinymock_action_managed second = {2};
    tinymock_value_t null_args[] = {TINYMOCk_VALUE((tinymock_action_managed *)NULL)};

    action_copy_count = 0u;
    action_destroy_count = 0u;

    tinymock_cmeta_actions_init(&actions, &action_out_function);
    check_true(tinymock_cmeta_actions_set_output_name(
        &actions, &action_out_function, "out", &first));
    check_true(tinymock_cmeta_actions_set_output_name(
        &actions, &action_out_function, "out", &second));

    check_equal(action_copy_count, (size_t)2);
    check_equal(action_destroy_count, (size_t)1);

    check_false(tinymock_cmeta_actions_apply(
        &actions, &action_out_function, 1u, null_args));

    tinymock_cmeta_actions_destroy(&actions);
    check_equal(action_destroy_count, (size_t)2);
  }
}
