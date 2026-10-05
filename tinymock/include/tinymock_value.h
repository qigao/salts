#ifndef TINYMOCK_VALUE_H
#define TINYMOCK_VALUE_H

#include <cmeta/function.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical reflected value storage used by TinyMock behavior state.
 *
 * CMeta owns type and lifecycle semantics. TinyMock only owns the bounded
 * storage needed by history/capture/return/output behavior. Pointer identity
 * is an optional exact-ABI observation supplied at the reflected dispatch
 * boundary; it is not inferred from raw bytes or pointer spelling.
 */
typedef struct tinymock_cmeta_value {
  const cmeta_type_desc *type;
  void *allocation;
  void *data;
  bool constructed;
  bool has_pointer_identity;
  const void *pointer_identity;
} tinymock_cmeta_value;

void tinymock_cmeta_value_init(tinymock_cmeta_value *value);
void tinymock_cmeta_value_reset(tinymock_cmeta_value *value);

bool tinymock_cmeta_value_copy(
    tinymock_cmeta_value *value,
    const cmeta_type_desc *type,
    const void *source);

bool tinymock_cmeta_value_copy_pointer(
    tinymock_cmeta_value *value,
    const cmeta_type_desc *type,
    const void *source,
    const void *pointer_identity);

bool tinymock_cmeta_value_clone(
    tinymock_cmeta_value *value,
    const tinymock_cmeta_value *source);

bool tinymock_cmeta_value_write(
    const tinymock_cmeta_value *value,
    void *destination,
    bool replace_existing);

#ifdef __cplusplus
}
#endif

#endif /* TINYMOCK_VALUE_H */
