#define TINYTEST_NO_MAIN
#include "tinymock_value.h"

#include <stdlib.h>
#include <string.h>

typedef union tinymock_cmeta_max_align {
  long double as_long_double;
  long long as_long_long;
  void *as_pointer;
} tinymock_cmeta_max_align;

void tinymock_cmeta_value_init(tinymock_cmeta_value *value) {
  if (!value) return;
  memset(value, 0, sizeof(*value));
}

void tinymock_cmeta_value_reset(tinymock_cmeta_value *value) {
  const cmeta_type_traits *traits;
  if (!value) return;

  traits = value->type ? value->type->traits : NULL;
  if (value->constructed && value->data && value->type &&
      value->type->kind != CMETA_T_POINTER && traits &&
      (traits->flags & CMETA_TRAIT_TRIVIAL_DESTROY) == 0u &&
      (traits->flags & CMETA_TRAIT_DESTROY) != 0u && traits->destroy) {
    traits->destroy(value->data);
  }

  free(value->allocation);
  memset(value, 0, sizeof(*value));
}

static bool tinymock_cmeta_value_allocate(
    tinymock_cmeta_value *value,
    const cmeta_type_desc *type) {
  if (!value || !cmeta_type_desc_valid(type) || type->size == 0u)
    return false;
  if (type->align == 0u ||
      type->align > _Alignof(tinymock_cmeta_max_align))
    return false;

  value->allocation = malloc(type->size);
  if (!value->allocation) return false;
  value->data = value->allocation;
  value->type = type;
  return true;
}

bool tinymock_cmeta_value_copy(
    tinymock_cmeta_value *value,
    const cmeta_type_desc *type,
    const void *source) {
  const cmeta_type_traits *traits;

  if (!value || !source || !tinymock_cmeta_value_allocate(value, type))
    return false;

  traits = type->traits;

  if (type->kind == CMETA_T_POINTER) {
    memcpy(value->data, source, type->size);
    value->constructed = true;
    return true;
  }

  if (traits &&
      (traits->flags & (CMETA_TRAIT_TRIVIAL_COPY |
                        CMETA_TRAIT_TRIVIAL_DESTROY)) ==
          (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY)) {
    memcpy(value->data, source, type->size);
    value->constructed = true;
    return true;
  }

  if (cmeta_type_require_traits(
          type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK &&
      traits && traits->copy_construct && traits->destroy &&
      traits->copy_construct(value->data, source)) {
    value->constructed = true;
    return true;
  }

  tinymock_cmeta_value_reset(value);
  return false;
}

bool tinymock_cmeta_value_copy_pointer(
    tinymock_cmeta_value *value,
    const cmeta_type_desc *type,
    const void *source,
    const void *pointer_identity) {
  if (!type || type->kind != CMETA_T_POINTER)
    return false;
  if (!tinymock_cmeta_value_copy(value, type, source))
    return false;

  value->has_pointer_identity = true;
  value->pointer_identity = pointer_identity;
  return true;
}

bool tinymock_cmeta_value_clone(
    tinymock_cmeta_value *value,
    const tinymock_cmeta_value *source) {
  if (!value || !source || !source->constructed ||
      !source->type || !source->data)
    return false;

  if (source->type->kind == CMETA_T_POINTER &&
      source->has_pointer_identity)
    return tinymock_cmeta_value_copy_pointer(
        value, source->type, source->data, source->pointer_identity);

  return tinymock_cmeta_value_copy(value, source->type, source->data);
}

bool tinymock_cmeta_value_write(
    const tinymock_cmeta_value *value,
    void *destination,
    bool replace_existing) {
  const cmeta_type_traits *traits;
  const cmeta_type_desc *type;

  if (!value || !value->constructed || !value->data ||
      !destination || !value->type)
    return false;

  type = value->type;
  traits = type->traits;

  if (type->kind == CMETA_T_POINTER) {
    memcpy(destination, value->data, type->size);
    return true;
  }

  if (traits &&
      (traits->flags & (CMETA_TRAIT_TRIVIAL_COPY |
                        CMETA_TRAIT_TRIVIAL_DESTROY)) ==
          (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY)) {
    memcpy(destination, value->data, type->size);
    return true;
  }

  if (cmeta_type_require_traits(
          type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) != CMETA_OK ||
      !traits || !traits->copy_construct || !traits->destroy)
    return false;

  if (replace_existing)
    traits->destroy(destination);

  return traits->copy_construct(destination, value->data);
}
