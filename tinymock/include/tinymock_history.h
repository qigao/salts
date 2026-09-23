#ifndef TINYMOCK_HISTORY_H
#define TINYMOCK_HISTORY_H

#include "tinymock.h"

#include <cmeta/function.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tinymock_cmeta_snapshot {
  const cmeta_type_desc *type;
  void *allocation;
  void *data;
  bool constructed;
  bool has_pointer_identity;
  const void *pointer_identity;
} tinymock_cmeta_snapshot;

typedef struct tinymock_cmeta_recorded_call {
  size_t argc;
  tinymock_cmeta_snapshot args[TINYMOCk_MAX_ARGS];
} tinymock_cmeta_recorded_call;

typedef struct tinymock_cmeta_history {
  const cmeta_function_desc *function;
  size_t call_count;
  tinymock_cmeta_recorded_call calls[TINYMOCk_MAX_CALLS];
} tinymock_cmeta_history;

typedef struct tinymock_cmeta_captor {
  tinymock_cmeta_snapshot value;
  size_t capture_count;
} tinymock_cmeta_captor;

void tinymock_cmeta_snapshot_init(tinymock_cmeta_snapshot *snapshot);
void tinymock_cmeta_snapshot_reset(tinymock_cmeta_snapshot *snapshot);
bool tinymock_cmeta_snapshot_copy(
    tinymock_cmeta_snapshot *snapshot,
    const cmeta_type_desc *type,
    const void *source,
    const tinymock_value_t *boxed);
bool tinymock_cmeta_snapshot_write(
    const tinymock_cmeta_snapshot *snapshot,
    void *destination,
    bool replace_existing);

bool tinymock_cmeta_function_equal(
    const cmeta_function_desc *left,
    const cmeta_function_desc *right);

void tinymock_cmeta_history_init(tinymock_cmeta_history *history,
                                 const cmeta_function_desc *function);
void tinymock_cmeta_history_reset(tinymock_cmeta_history *history,
                                  const cmeta_function_desc *function);
void tinymock_cmeta_history_destroy(tinymock_cmeta_history *history);

bool tinymock_cmeta_history_record(
    tinymock_cmeta_history *history,
    const cmeta_function_desc *function,
    size_t argc,
    const void *const *args,
    const tinymock_value_t *boxed_args);

size_t tinymock_cmeta_history_call_count(const tinymock_cmeta_history *history);

const cmeta_param_desc *tinymock_cmeta_history_param(
    const tinymock_cmeta_history *history, size_t param_index);
const cmeta_param_desc *tinymock_cmeta_history_find_param(
    const tinymock_cmeta_history *history, const char *param_name);

const void *tinymock_cmeta_history_arg(
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index,
    const cmeta_type_desc **out_type);

bool tinymock_cmeta_history_arg_equal(
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index,
    const void *expected,
    tinymock_value_t expected_boxed);

bool tinymock_cmeta_history_arg_equal_name(
    const tinymock_cmeta_history *history,
    size_t call_index,
    const char *param_name,
    const void *expected,
    tinymock_value_t expected_boxed);

bool tinymock_cmeta_history_arg_equal_typed(
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index,
    const void *expected);

bool tinymock_cmeta_history_arg_equal_typed_name(
    const tinymock_cmeta_history *history,
    size_t call_index,
    const char *param_name,
    const void *expected);

size_t tinymock_cmeta_history_count_equal(
    const tinymock_cmeta_history *history,
    size_t param_index,
    const void *expected,
    tinymock_value_t expected_boxed);

size_t tinymock_cmeta_history_count_equal_name(
    const tinymock_cmeta_history *history,
    const char *param_name,
    const void *expected,
    tinymock_value_t expected_boxed);

void tinymock_cmeta_captor_init(tinymock_cmeta_captor *captor);
void tinymock_cmeta_captor_reset(tinymock_cmeta_captor *captor);
void tinymock_cmeta_captor_destroy(tinymock_cmeta_captor *captor);

bool tinymock_cmeta_captor_capture(
    tinymock_cmeta_captor *captor,
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index);

bool tinymock_cmeta_captor_capture_name(
    tinymock_cmeta_captor *captor,
    const tinymock_cmeta_history *history,
    size_t call_index,
    const char *param_name);

const cmeta_type_desc *tinymock_cmeta_captor_type(
    const tinymock_cmeta_captor *captor);
const void *tinymock_cmeta_captor_value(
    const tinymock_cmeta_captor *captor);
size_t tinymock_cmeta_captor_count(
    const tinymock_cmeta_captor *captor);

#ifdef __cplusplus
}
#endif

#endif /* TINYMOCK_HISTORY_H */
