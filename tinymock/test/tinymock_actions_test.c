#include "tinytest.h"
#include "tinymock_actions.h"

#include <cmeta/cmeta.h>

typedef struct tinymock_action_managed {
  int value;
} tinymock_action_managed;

static size_t action_copy_count;
static size_t action_destroy_count;
static size_t action_move_count;
static size_t action_fail_copy_at;

static bool action_managed_copy(void *destination, const void *source) {
  tinymock_action_managed *dst = (tinymock_action_managed *)destination;
  const tinymock_action_managed *src =
      (const tinymock_action_managed *)source;
  if (!dst || !src) return false;
  ++action_copy_count;
  if (action_copy_count == action_fail_copy_at) return false;
  *dst = *src;
  return true;
}

static void action_managed_move(void *destination, void *source) {
  *(tinymock_action_managed *)destination = *(tinymock_action_managed *)source;
  ((tinymock_action_managed *)source)->value = 0;
  ++action_move_count;
}

static void action_managed_destroy(void *value) {
  (void)value;
  ++action_destroy_count;
}

static const cmeta_type_traits action_managed_traits = {
  .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
  .copy_construct = action_managed_copy,
  .move_construct = action_managed_move,
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
  CMETA_PROP_NONE,
  CMETA_RESULT_UNKNOWN
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
  CMETA_PROP_NONE,
  CMETA_RESULT_UNKNOWN
};

suite("TinyMock reflected output actions") {
  before_each() { action_fail_copy_at = action_move_count = 0; }
  it("rolls back prepared outputs when a later copy fails before any INOUT mutation") {
    cmeta_param_desc params[2] = {action_inout_params[0], action_inout_params[0]};
    cmeta_function_desc function = action_inout_function;
    tinymock_cmeta_actions actions;
    tinymock_action_managed first = {3}, second = {4}, scripted = {17};
    tinymock_action_managed *first_ptr = &first, *second_ptr = &second;
    tinymock_cmeta_arg_view args[] = {{&first_ptr, true, first_ptr}, {&second_ptr, true, second_ptr}};
    params[1].name = "second";
    function.params = params; function.param_count = 2;
    action_copy_count = action_destroy_count = 0;
    tinymock_cmeta_actions_init(&actions, &function);
    check_true(tinymock_cmeta_actions_set_output(&actions, &function, 0, &scripted));
    check_true(tinymock_cmeta_actions_set_output(&actions, &function, 1, &scripted));
    action_fail_copy_at = action_copy_count + 2;
    check_false(tinymock_cmeta_actions_apply_admitted(&actions, 2, args));
    check_equal(first.value, 3); check_equal(second.value, 4);
    check_equal(action_move_count, (size_t)0);
    check_equal(action_destroy_count, (size_t)1);
    action_fail_copy_at = 0;
    check_true(tinymock_cmeta_actions_apply_admitted(&actions, 2, args));
    check_equal(first.value, 17); check_equal(second.value, 17);
    check_equal(action_move_count, (size_t)2);
    tinymock_cmeta_actions_destroy(&actions);
    action_managed_destroy(&first); action_managed_destroy(&second);
  }
  it("rejects managed replacement without a no-fail move authority") {
    cmeta_type_traits traits = action_managed_traits;
    cmeta_type_desc type = action_managed_type, pointer = action_managed_ptr_type;
    cmeta_param_desc param = action_inout_params[0];
    cmeta_function_desc function = action_inout_function;
    tinymock_cmeta_actions actions;
    tinymock_action_managed original = {3}, scripted = {17};
    tinymock_action_managed *target = &original;
    tinymock_cmeta_arg_view arg = {&target, true, target};
    traits.flags &= ~CMETA_TRAIT_MOVE; traits.move_construct = NULL;
    type.traits = &traits; pointer.pointee = &type; param.type = &pointer; function.params = &param;
    tinymock_cmeta_actions_init(&actions, &function);
    check_true(tinymock_cmeta_actions_set_output(&actions, &function, 0, &scripted));
    check_false(tinymock_cmeta_actions_apply_admitted(&actions, 1, &arg));
    check_equal(original.value, 3);
    tinymock_cmeta_actions_destroy(&actions);
  }
  it("constructs nontrivial OUT values and releases scripted storage") {
    tinymock_cmeta_actions actions;
    tinymock_action_managed scripted = {17};
    tinymock_action_managed output = {0};
    tinymock_action_managed *output_ptr = &output;
    tinymock_cmeta_arg_view args[] = {
      {&output_ptr, true, (const void *)output_ptr}
    };

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
    check_equal(action_destroy_count, (size_t)1);
    check_equal(action_move_count, (size_t)1);

    tinymock_cmeta_actions_destroy(&actions);
    check_equal(action_destroy_count, (size_t)2);

    action_managed_destroy(&output);
    check_equal(action_destroy_count, (size_t)3);
  }

  it("prepares a nontrivial INOUT copy before destroying and moving into the destination") {
    tinymock_cmeta_actions actions;
    tinymock_action_managed scripted = {44};
    tinymock_action_managed value = {3};
    tinymock_action_managed *value_ptr = &value;
    tinymock_cmeta_arg_view args[] = {
      {&value_ptr, true, (const void *)value_ptr}
    };

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
    check_equal(action_destroy_count, (size_t)2);
    check_equal(action_move_count, (size_t)1);

    tinymock_cmeta_actions_destroy(&actions);
    check_equal(action_destroy_count, (size_t)3);

    action_managed_destroy(&value);
    check_equal(action_destroy_count, (size_t)4);
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
    tinymock_action_managed *null_output = NULL;
    tinymock_cmeta_arg_view null_args[] = {
      {&null_output, true, NULL}
    };

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
