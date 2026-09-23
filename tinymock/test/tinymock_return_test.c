#include "tinytest.h"
#include "tinymock_return.h"

#include <cmeta/cmeta.h>
#include <cmeta/function.h>

typedef struct tinymock_return_managed {
  int value;
} tinymock_return_managed;

static size_t return_copy_count;
static size_t return_destroy_count;

static bool return_managed_copy(void *destination, const void *source) {
  tinymock_return_managed *dst = (tinymock_return_managed *)destination;
  const tinymock_return_managed *src =
      (const tinymock_return_managed *)source;
  if (!dst || !src) return false;
  *dst = *src;
  ++return_copy_count;
  return true;
}

static void return_managed_destroy(void *value) {
  (void)value;
  ++return_destroy_count;
}

static const cmeta_type_traits return_managed_traits = {
  .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY,
  .copy_construct = return_managed_copy,
  .destroy = return_managed_destroy
};

static const cmeta_type_desc return_managed_type = {
  .name = "tinymock_return_managed",
  .size = sizeof(tinymock_return_managed),
  .align = _Alignof(tinymock_return_managed),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &return_managed_traits,
  .identity = NULL
};

static const cmeta_function_desc return_managed_function = {
  sizeof(cmeta_function_desc),
  "tinymock_return_managed_function",
  &return_managed_type,
  NULL,
  0u,
  CMETA_EFFECT_PURE,
  CMETA_PROP_NONE
};

suite("TinyMock CMeta typed return") {
  it("owns and materializes nontrivial return values") {
    tinymock_cmeta_return state;
    tinymock_return_managed scripted = {17};
    tinymock_return_managed output = {0};

    return_copy_count = 0u;
    return_destroy_count = 0u;

    tinymock_cmeta_return_init(&state, &return_managed_function);
    check_true(tinymock_cmeta_return_set(
        &state, &return_managed_function, &scripted));
    check_equal(return_copy_count, (size_t)1);
    check_equal(return_destroy_count, (size_t)0);

    check_true(tinymock_cmeta_return_write(
        &state, &return_managed_function, &output));
    check_equal(output.value, 17);
    check_equal(return_copy_count, (size_t)2);

    tinymock_cmeta_return_destroy(&state);
    check_equal(return_destroy_count, (size_t)1);

    return_managed_destroy(&output);
    check_equal(return_destroy_count, (size_t)2);
  }

  it("destroys replaced and cleared scripted values exactly once") {
    tinymock_cmeta_return state;
    tinymock_return_managed first = {1};
    tinymock_return_managed second = {2};

    return_copy_count = 0u;
    return_destroy_count = 0u;

    tinymock_cmeta_return_init(&state, &return_managed_function);
    check_true(tinymock_cmeta_return_set(
        &state, &return_managed_function, &first));
    check_true(tinymock_cmeta_return_set(
        &state, &return_managed_function, &second));

    check_equal(return_copy_count, (size_t)2);
    check_equal(return_destroy_count, (size_t)1);
    check_true(tinymock_cmeta_return_enabled(&state));

    tinymock_cmeta_return_clear(&state);
    check_equal(return_destroy_count, (size_t)2);
    check_false(tinymock_cmeta_return_enabled(&state));

    tinymock_cmeta_return_destroy(&state);
    check_equal(return_destroy_count, (size_t)2);
  }

  it("reset releases the old scripted return") {
    tinymock_cmeta_return state;
    tinymock_return_managed scripted = {9};

    return_copy_count = 0u;
    return_destroy_count = 0u;

    tinymock_cmeta_return_init(&state, &return_managed_function);
    check_true(tinymock_cmeta_return_set(
        &state, &return_managed_function, &scripted));

    tinymock_cmeta_return_reset(&state, &return_managed_function);
    check_equal(return_destroy_count, (size_t)1);
    check_false(tinymock_cmeta_return_enabled(&state));

    tinymock_cmeta_return_destroy(&state);
  }
}
