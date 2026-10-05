#include <cmeta/data.h>
#include <cmeta/interface.h>

#include "tinymock_cmeta.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct tinymock_raii_payload {
  int value;
} tinymock_raii_payload;

static size_t tinymock_raii_copy_count;
static size_t tinymock_raii_destroy_count;

static bool tinymock_raii_payload_equal(
    const void *left, const void *right) {
  const tinymock_raii_payload *a =
      (const tinymock_raii_payload *)left;
  const tinymock_raii_payload *b =
      (const tinymock_raii_payload *)right;
  return a && b && a->value == b->value;
}

static bool tinymock_raii_payload_copy(
    void *destination, const void *source) {
  if (!destination || !source) return false;
  *(tinymock_raii_payload *)destination =
      *(const tinymock_raii_payload *)source;
  ++tinymock_raii_copy_count;
  return true;
}

static void tinymock_raii_payload_destroy(void *value) {
  if (!value) return;
  ((tinymock_raii_payload *)value)->value = 0;
  ++tinymock_raii_destroy_count;
}

static const cmeta_type_traits tinymock_raii_payload_traits = {
  .flags = CMETA_TRAIT_EQUAL |
           CMETA_TRAIT_COPY |
           CMETA_TRAIT_DESTROY,
  .equal = tinymock_raii_payload_equal,
  .copy_construct = tinymock_raii_payload_copy,
  .destroy = tinymock_raii_payload_destroy
};

static const cmeta_type_desc tinymock_raii_payload_type = {
  .name = "tinymock_raii_payload",
  .size = sizeof(tinymock_raii_payload),
  .align = _Alignof(tinymock_raii_payload),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &tinymock_raii_payload_traits,
  .identity = NULL
};

#define TINYMOCK_RAII_METHODS(X, I) \
  X(I,F0,tinymock_raii_payload,read,value, \
    &tinymock_raii_payload_type,CMETA_ABI_AGGREGATE)

CMETA_INTERFACE(tinymock_raii_probe, TINYMOCK_RAII_METHODS);
TINYMOCk_INTERFACE(tinymock_raii_probe, TINYMOCK_RAII_METHODS);

CMETA_LIFECYCLE(
    tinymock_tinymock_raii_probe,
    tinymock_tinymock_raii_probe_cmeta_data);

int main(void) {
  bool set_ok = false;

  tinymock_raii_copy_count = 0u;
  tinymock_raii_destroy_count = 0u;

  {
    owned(tinymock_tinymock_raii_probe) mock;
    tinymock_raii_payload scripted = {42};

    tinymock_tinymock_raii_probe_init(&mock);
    set_ok = TINYMOCk_INTERFACE_SET_RETURN(
        &mock, read, scripted);
  }

  if (!cmeta_data_desc_valid(
          tinymock_tinymock_raii_probe_cmeta_data()))
    return 1;
  if (cmeta_data_construct_ops_of(
          tinymock_tinymock_raii_probe_cmeta_data()) == NULL)
    return 2;
  if (!set_ok) return 3;
  if (tinymock_raii_copy_count != 1u) return 4;
  if (tinymock_raii_destroy_count != 1u) return 5;
  return 0;
}
