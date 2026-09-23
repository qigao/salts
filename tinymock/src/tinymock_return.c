#define TINYTEST_NO_MAIN
#include "tinymock_return.h"

#include <string.h>

static bool tinymock_cmeta_return_function_ok(
    const tinymock_cmeta_return *state,
    const cmeta_function_desc *function) {
  return state && function &&
         cmeta_function_desc_valid(function) &&
         function->return_type &&
         function->return_type->kind != CMETA_T_VOID &&
         (!state->function ||
          tinymock_cmeta_function_equal(state->function, function));
}

void tinymock_cmeta_return_init(
    tinymock_cmeta_return *state,
    const cmeta_function_desc *function) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  state->function = function;
}

void tinymock_cmeta_return_destroy(tinymock_cmeta_return *state) {
  if (!state) return;
  tinymock_cmeta_snapshot_reset(&state->value);
  memset(state, 0, sizeof(*state));
}

void tinymock_cmeta_return_reset(
    tinymock_cmeta_return *state,
    const cmeta_function_desc *function) {
  if (!state) return;
  tinymock_cmeta_return_destroy(state);
  tinymock_cmeta_return_init(state, function);
}

bool tinymock_cmeta_return_set(
    tinymock_cmeta_return *state,
    const cmeta_function_desc *function,
    const void *value) {
  if (!value || !tinymock_cmeta_return_function_ok(state, function))
    return false;

  tinymock_cmeta_snapshot_reset(&state->value);
  if (!tinymock_cmeta_snapshot_copy(
          &state->value, function->return_type, value, NULL))
    return false;

  state->function = function;
  state->enabled = true;
  return true;
}

void tinymock_cmeta_return_clear(tinymock_cmeta_return *state) {
  if (!state) return;
  tinymock_cmeta_snapshot_reset(&state->value);
  state->enabled = false;
}

bool tinymock_cmeta_return_enabled(const tinymock_cmeta_return *state) {
  return state && state->enabled && state->value.constructed;
}

bool tinymock_cmeta_return_write(
    const tinymock_cmeta_return *state,
    const cmeta_function_desc *function,
    void *destination) {
  if (!destination || !tinymock_cmeta_return_enabled(state) ||
      !tinymock_cmeta_return_function_ok(state, function) ||
      !cmeta_type_equal(state->value.type, function->return_type))
    return false;

  return tinymock_cmeta_snapshot_write(
      &state->value, destination, false);
}
