#ifndef TINYMOCK_RETURN_H
#define TINYMOCK_RETURN_H

#include "tinymock_history.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tinymock_cmeta_return {
  const cmeta_function_desc *function;
  tinymock_cmeta_snapshot value;
  bool enabled;
} tinymock_cmeta_return;

void tinymock_cmeta_return_init(
    tinymock_cmeta_return *state,
    const cmeta_function_desc *function);
void tinymock_cmeta_return_reset(
    tinymock_cmeta_return *state,
    const cmeta_function_desc *function);
void tinymock_cmeta_return_destroy(tinymock_cmeta_return *state);

bool tinymock_cmeta_return_set(
    tinymock_cmeta_return *state,
    const cmeta_function_desc *function,
    const void *value);

void tinymock_cmeta_return_clear(tinymock_cmeta_return *state);
bool tinymock_cmeta_return_enabled(const tinymock_cmeta_return *state);

bool tinymock_cmeta_return_write(
    const tinymock_cmeta_return *state,
    const cmeta_function_desc *function,
    void *destination);

#ifdef __cplusplus
}
#endif

#endif /* TINYMOCK_RETURN_H */
