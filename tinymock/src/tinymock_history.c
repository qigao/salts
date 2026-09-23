#define TINYTEST_NO_MAIN
#include "tinymock_history.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef union tinymock_cmeta_max_align {
  long double as_long_double;
  long long as_long_long;
  void *as_pointer;
} tinymock_cmeta_max_align;

static size_t tinymock_cmeta_recorded_limit(size_t count) {
  return count < TINYMOCk_MAX_CALLS ? count : TINYMOCk_MAX_CALLS;
}

void tinymock_cmeta_snapshot_init(tinymock_cmeta_snapshot *snapshot) {
  if (!snapshot) return;
  memset(snapshot, 0, sizeof(*snapshot));
}

bool tinymock_cmeta_function_equal(
    const cmeta_function_desc *left,
    const cmeta_function_desc *right) {
  return cmeta_function_desc_equal(left, right);
}

void tinymock_cmeta_snapshot_reset(tinymock_cmeta_snapshot *snapshot) {
  const cmeta_type_traits *traits;
  if (!snapshot) return;

  traits = snapshot->type ? snapshot->type->traits : NULL;
  if (snapshot->constructed && snapshot->data && snapshot->type &&
      snapshot->type->kind != CMETA_T_POINTER && traits &&
      (traits->flags & CMETA_TRAIT_TRIVIAL_DESTROY) == 0u &&
      (traits->flags & CMETA_TRAIT_DESTROY) != 0u && traits->destroy) {
    traits->destroy(snapshot->data);
  }

  free(snapshot->allocation);
  memset(snapshot, 0, sizeof(*snapshot));
}

static bool tinymock_cmeta_snapshot_allocate(
    tinymock_cmeta_snapshot *snapshot,
    const cmeta_type_desc *type) {
  if (!snapshot || !cmeta_type_desc_valid(type) || type->size == 0u)
    return false;
  if (type->align == 0u ||
      type->align > _Alignof(tinymock_cmeta_max_align))
    return false;

  snapshot->allocation = malloc(type->size);
  if (!snapshot->allocation) return false;
  snapshot->data = snapshot->allocation;
  snapshot->type = type;
  return true;
}

bool tinymock_cmeta_snapshot_copy(
    tinymock_cmeta_snapshot *snapshot,
    const cmeta_type_desc *type,
    const void *source,
    const tinymock_value_t *boxed) {
  const cmeta_type_traits *traits;

  if (!snapshot || !source || !tinymock_cmeta_snapshot_allocate(snapshot, type))
    return false;

  traits = type->traits;

  if (type->kind == CMETA_T_POINTER) {
    memcpy(snapshot->data, source, type->size);
    snapshot->constructed = true;
    if (boxed && boxed->kind == TINYMOCk_VALUE_POINTER) {
      snapshot->has_pointer_identity = true;
      snapshot->pointer_identity = boxed->as.pointer_value;
    }
    return true;
  }

  if (traits &&
      (traits->flags & (CMETA_TRAIT_TRIVIAL_COPY |
                        CMETA_TRAIT_TRIVIAL_DESTROY)) ==
          (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY)) {
    memcpy(snapshot->data, source, type->size);
    snapshot->constructed = true;
    return true;
  }

  if (cmeta_type_require_traits(
          type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK &&
      traits && traits->copy_construct && traits->destroy &&
      traits->copy_construct(snapshot->data, source)) {
    snapshot->constructed = true;
    return true;
  }

  tinymock_cmeta_snapshot_reset(snapshot);
  return false;
}

bool tinymock_cmeta_snapshot_write(
    const tinymock_cmeta_snapshot *snapshot,
    void *destination,
    bool replace_existing) {
  const cmeta_type_traits *traits;
  const cmeta_type_desc *type;

  if (!snapshot || !snapshot->constructed || !snapshot->data ||
      !destination || !snapshot->type)
    return false;

  type = snapshot->type;
  traits = type->traits;

  if (type->kind == CMETA_T_POINTER) {
    memcpy(destination, snapshot->data, type->size);
    return true;
  }

  if (traits &&
      (traits->flags & (CMETA_TRAIT_TRIVIAL_COPY |
                        CMETA_TRAIT_TRIVIAL_DESTROY)) ==
          (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY)) {
    memcpy(destination, snapshot->data, type->size);
    return true;
  }

  if (cmeta_type_require_traits(
          type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) != CMETA_OK ||
      !traits || !traits->copy_construct || !traits->destroy)
    return false;

  if (replace_existing)
    traits->destroy(destination);

  return traits->copy_construct(destination, snapshot->data);
}

static void tinymock_cmeta_call_clear(tinymock_cmeta_recorded_call *call) {
  size_t index;
  if (!call) return;
  for (index = 0; index < call->argc && index < TINYMOCk_MAX_ARGS; ++index)
    tinymock_cmeta_snapshot_reset(&call->args[index]);
  memset(call, 0, sizeof(*call));
}

void tinymock_cmeta_history_init(tinymock_cmeta_history *history,
                                 const cmeta_function_desc *function) {
  if (!history) return;
  memset(history, 0, sizeof(*history));
  history->function = function;
}

void tinymock_cmeta_history_destroy(tinymock_cmeta_history *history) {
  size_t index;
  size_t count;
  if (!history) return;

  count = tinymock_cmeta_recorded_limit(history->call_count);
  for (index = 0; index < count; ++index)
    tinymock_cmeta_call_clear(&history->calls[index]);
  memset(history, 0, sizeof(*history));
}

void tinymock_cmeta_history_reset(tinymock_cmeta_history *history,
                                  const cmeta_function_desc *function) {
  if (!history) return;
  tinymock_cmeta_history_destroy(history);
  tinymock_cmeta_history_init(history, function);
}

bool tinymock_cmeta_history_record(
    tinymock_cmeta_history *history,
    const cmeta_function_desc *function,
    size_t argc,
    const void *const *args,
    const tinymock_value_t *boxed_args) {
  tinymock_cmeta_recorded_call *call;
  size_t index;

  if (!history || !function || !cmeta_function_desc_valid(function) ||
      argc != function->param_count || argc > TINYMOCk_MAX_ARGS ||
      (argc != 0u && !args))
    return false;

  if (history->function &&
      !tinymock_cmeta_function_equal(history->function, function))
    return false;
  history->function = function;

  if (history->call_count >= TINYMOCk_MAX_CALLS) {
    ++history->call_count;
    return true;
  }

  call = &history->calls[history->call_count];
  memset(call, 0, sizeof(*call));
  call->argc = argc;

  for (index = 0; index < argc; ++index) {
    const tinymock_value_t *boxed = boxed_args ? &boxed_args[index] : NULL;
    if (!tinymock_cmeta_snapshot_copy(
            &call->args[index], function->params[index].type,
            args[index], boxed)) {
      tinymock_cmeta_call_clear(call);
      return false;
    }
  }

  ++history->call_count;
  return true;
}

size_t tinymock_cmeta_history_call_count(const tinymock_cmeta_history *history) {
  return history ? history->call_count : 0u;
}

const cmeta_param_desc *tinymock_cmeta_history_param(
    const tinymock_cmeta_history *history, size_t param_index) {
  if (!history || !history->function)
    return NULL;
  return cmeta_function_param(history->function, param_index);
}

const cmeta_param_desc *tinymock_cmeta_history_find_param(
    const tinymock_cmeta_history *history, const char *param_name) {
  if (!history || !history->function)
    return NULL;
  return cmeta_function_find_param(history->function, param_name);
}

const void *tinymock_cmeta_history_arg(
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index,
    const cmeta_type_desc **out_type) {
  const tinymock_cmeta_recorded_call *call;
  const tinymock_cmeta_snapshot *snapshot;

  if (out_type) *out_type = NULL;
  if (!history || call_index >= history->call_count ||
      call_index >= TINYMOCk_MAX_CALLS)
    return NULL;

  call = &history->calls[call_index];
  if (param_index >= call->argc)
    return NULL;

  snapshot = &call->args[param_index];
  if (!snapshot->constructed || !snapshot->data)
    return NULL;

  if (out_type) *out_type = snapshot->type;
  return snapshot->data;
}

static bool tinymock_cmeta_param_index_by_name(
    const tinymock_cmeta_history *history,
    const char *param_name,
    size_t *out_index) {
  const cmeta_param_desc *param;
  if (!history || !history->function || !param_name || !out_index)
    return false;

  param = cmeta_function_find_param(history->function, param_name);
  if (!param) return false;

  *out_index = (size_t)(param - history->function->params);
  return *out_index < history->function->param_count;
}

bool tinymock_cmeta_history_arg_equal(
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index,
    const void *expected,
    tinymock_value_t expected_boxed) {
  const tinymock_cmeta_snapshot *snapshot;
  const cmeta_type_traits *traits;

  if (!history || !expected || call_index >= history->call_count ||
      call_index >= TINYMOCk_MAX_CALLS)
    return false;
  if (param_index >= history->calls[call_index].argc)
    return false;

  snapshot = &history->calls[call_index].args[param_index];
  if (!snapshot->constructed || !snapshot->type || !snapshot->data)
    return false;

  if (snapshot->type->kind == CMETA_T_POINTER) {
    return snapshot->has_pointer_identity &&
           expected_boxed.kind == TINYMOCk_VALUE_POINTER &&
           snapshot->pointer_identity == expected_boxed.as.pointer_value;
  }

  traits = snapshot->type->traits;
  return traits && (traits->flags & CMETA_TRAIT_EQUAL) != 0u &&
         traits->equal &&
         traits->equal(snapshot->data, expected);
}

bool tinymock_cmeta_history_arg_equal_name(
    const tinymock_cmeta_history *history,
    size_t call_index,
    const char *param_name,
    const void *expected,
    tinymock_value_t expected_boxed) {
  size_t index;
  if (!tinymock_cmeta_param_index_by_name(history, param_name, &index))
    return false;
  return tinymock_cmeta_history_arg_equal(
      history, call_index, index, expected, expected_boxed);
}

bool tinymock_cmeta_history_arg_equal_typed(
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index,
    const void *expected) {
  const tinymock_cmeta_snapshot *snapshot;
  const cmeta_type_traits *traits;

  if (!history || !expected || call_index >= history->call_count ||
      call_index >= TINYMOCk_MAX_CALLS ||
      param_index >= history->calls[call_index].argc)
    return false;

  snapshot = &history->calls[call_index].args[param_index];
  if (!snapshot->constructed || !snapshot->type || !snapshot->data ||
      snapshot->type->kind == CMETA_T_POINTER)
    return false;

  traits = snapshot->type->traits;
  return traits && (traits->flags & CMETA_TRAIT_EQUAL) != 0u &&
         traits->equal &&
         traits->equal(snapshot->data, expected);
}

bool tinymock_cmeta_history_arg_equal_typed_name(
    const tinymock_cmeta_history *history,
    size_t call_index,
    const char *param_name,
    const void *expected) {
  size_t index;
  if (!tinymock_cmeta_param_index_by_name(history, param_name, &index))
    return false;
  return tinymock_cmeta_history_arg_equal_typed(
      history, call_index, index, expected);
}

size_t tinymock_cmeta_history_count_equal(
    const tinymock_cmeta_history *history,
    size_t param_index,
    const void *expected,
    tinymock_value_t expected_boxed) {
  size_t call_index;
  size_t count = 0u;
  size_t limit;

  if (!history) return 0u;
  limit = tinymock_cmeta_recorded_limit(history->call_count);
  for (call_index = 0; call_index < limit; ++call_index)
    if (tinymock_cmeta_history_arg_equal(
            history, call_index, param_index, expected, expected_boxed))
      ++count;
  return count;
}

size_t tinymock_cmeta_history_count_equal_name(
    const tinymock_cmeta_history *history,
    const char *param_name,
    const void *expected,
    tinymock_value_t expected_boxed) {
  size_t index;
  if (!tinymock_cmeta_param_index_by_name(history, param_name, &index))
    return 0u;
  return tinymock_cmeta_history_count_equal(
      history, index, expected, expected_boxed);
}

void tinymock_cmeta_captor_init(tinymock_cmeta_captor *captor) {
  if (!captor) return;
  memset(captor, 0, sizeof(*captor));
}

void tinymock_cmeta_captor_reset(tinymock_cmeta_captor *captor) {
  if (!captor) return;
  tinymock_cmeta_snapshot_reset(&captor->value);
  captor->capture_count = 0u;
}

void tinymock_cmeta_captor_destroy(tinymock_cmeta_captor *captor) {
  tinymock_cmeta_captor_reset(captor);
}

bool tinymock_cmeta_captor_capture(
    tinymock_cmeta_captor *captor,
    const tinymock_cmeta_history *history,
    size_t call_index,
    size_t param_index) {
  const tinymock_cmeta_snapshot *snapshot;

  if (!captor || !history || call_index >= history->call_count ||
      call_index >= TINYMOCk_MAX_CALLS ||
      param_index >= history->calls[call_index].argc)
    return false;

  snapshot = &history->calls[call_index].args[param_index];
  if (!snapshot->constructed || !snapshot->type || !snapshot->data)
    return false;

  tinymock_cmeta_snapshot_reset(&captor->value);
  if (!tinymock_cmeta_snapshot_copy(
          &captor->value, snapshot->type, snapshot->data,
          snapshot->has_pointer_identity ? &(tinymock_value_t){
              .kind = TINYMOCk_VALUE_POINTER,
              .as.pointer_value = snapshot->pointer_identity
          } : NULL))
    return false;

  ++captor->capture_count;
  return true;
}

bool tinymock_cmeta_captor_capture_name(
    tinymock_cmeta_captor *captor,
    const tinymock_cmeta_history *history,
    size_t call_index,
    const char *param_name) {
  size_t index;
  if (!tinymock_cmeta_param_index_by_name(history, param_name, &index))
    return false;
  return tinymock_cmeta_captor_capture(
      captor, history, call_index, index);
}

const cmeta_type_desc *tinymock_cmeta_captor_type(
    const tinymock_cmeta_captor *captor) {
  return captor && captor->value.constructed ? captor->value.type : NULL;
}

const void *tinymock_cmeta_captor_value(
    const tinymock_cmeta_captor *captor) {
  return captor && captor->value.constructed ? captor->value.data : NULL;
}

size_t tinymock_cmeta_captor_count(
    const tinymock_cmeta_captor *captor) {
  return captor ? captor->capture_count : 0u;
}
