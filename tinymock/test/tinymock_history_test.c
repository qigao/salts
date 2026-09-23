#include "tinytest.h"
#include "tinymock_history.h"

#include <cmeta/cmeta.h>

typedef struct tinymock_managed_value {
  int value;
} tinymock_managed_value;

static size_t managed_copy_count;
static size_t managed_destroy_count;

static bool managed_equal(const void *left, const void *right) {
  const tinymock_managed_value *a = (const tinymock_managed_value *)left;
  const tinymock_managed_value *b = (const tinymock_managed_value *)right;
  return a && b && a->value == b->value;
}

static bool managed_copy(void *destination, const void *source) {
  tinymock_managed_value *dst = (tinymock_managed_value *)destination;
  const tinymock_managed_value *src =
      (const tinymock_managed_value *)source;
  if (!dst || !src) return false;
  *dst = *src;
  ++managed_copy_count;
  return true;
}

static void managed_destroy(void *value) {
  (void)value;
  ++managed_destroy_count;
}

static const cmeta_type_traits managed_traits = {
  .flags = CMETA_TRAIT_EQUAL | CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY,
  .equal = managed_equal,
  .copy_construct = managed_copy,
  .destroy = managed_destroy
};

static const cmeta_type_desc managed_type = {
  .name = "tinymock_managed_value",
  .size = sizeof(tinymock_managed_value),
  .align = _Alignof(tinymock_managed_value),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &managed_traits,
  .identity = NULL
};

static const cmeta_param_desc managed_params[] = {
  {
    sizeof(cmeta_param_desc),
    "value",
    &managed_type,
    CMETA_PARAM_IN
  }
};

static const cmeta_function_desc managed_function = {
  sizeof(cmeta_function_desc),
  "tinymock_managed_function",
  &cmeta_type_int,
  managed_params,
  1u,
  CMETA_EFFECT_PURE,
  CMETA_PROP_NONE
};

suite("TinyMock CMeta typed history") {
  it("uses nontrivial copy and destroy traits for history and captor") {
    tinymock_cmeta_history history;
    tinymock_cmeta_captor captor;
    tinymock_managed_value input = {17};
    tinymock_managed_value expected = {17};
    const void *args[] = {&input};

    managed_copy_count = 0u;
    managed_destroy_count = 0u;

    tinymock_cmeta_history_init(&history, &managed_function);
    tinymock_cmeta_captor_init(&captor);

    check_true(tinymock_cmeta_history_record(
        &history, &managed_function, 1u, args, NULL));
    check_equal(managed_copy_count, (size_t)1);

    check_true(tinymock_cmeta_history_arg_equal(
        &history, 0u, 0u, &expected, tinymock_value_zero()));

    check_true(tinymock_cmeta_captor_capture(
        &captor, &history, 0u, 0u));
    check_equal(managed_copy_count, (size_t)2);
    check_equal(
        ((const tinymock_managed_value *)
             tinymock_cmeta_captor_value(&captor))->value,
        17);

    input.value = 99;
    check_equal(
        ((const tinymock_managed_value *)
             tinymock_cmeta_captor_value(&captor))->value,
        17);

    tinymock_cmeta_captor_destroy(&captor);
    check_equal(managed_destroy_count, (size_t)1);

    tinymock_cmeta_history_destroy(&history);
    check_equal(managed_destroy_count, (size_t)2);
  }

  it("replaces captured values with balanced destruction") {
    tinymock_cmeta_history history;
    tinymock_cmeta_captor captor;
    tinymock_managed_value first = {1};
    tinymock_managed_value second = {2};
    const void *first_args[] = {&first};
    const void *second_args[] = {&second};

    managed_copy_count = 0u;
    managed_destroy_count = 0u;

    tinymock_cmeta_history_init(&history, &managed_function);
    tinymock_cmeta_captor_init(&captor);

    check_true(tinymock_cmeta_history_record(
        &history, &managed_function, 1u, first_args, NULL));
    check_true(tinymock_cmeta_history_record(
        &history, &managed_function, 1u, second_args, NULL));

    check_true(tinymock_cmeta_captor_capture_name(
        &captor, &history, 0u, "value"));
    check_true(tinymock_cmeta_captor_capture_name(
        &captor, &history, 1u, "value"));

    check_equal(tinymock_cmeta_captor_count(&captor), (size_t)2);
    check_equal(
        ((const tinymock_managed_value *)
             tinymock_cmeta_captor_value(&captor))->value,
        2);

    /* Second capture destroys the previous captor-owned value. */
    check_equal(managed_destroy_count, (size_t)1);

    tinymock_cmeta_captor_destroy(&captor);
    tinymock_cmeta_history_destroy(&history);

    /* one replaced captor + final captor + two history snapshots */
    check_equal(managed_destroy_count, (size_t)4);
    check_equal(managed_copy_count, (size_t)4);
  }
}
