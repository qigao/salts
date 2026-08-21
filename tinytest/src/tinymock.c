#define TINYTEST_NO_MAIN
#define TINYMOCK_BACKEND_SOURCE__
#include "tinymock.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool tinymock_is_numeric__(tinymock_value_kind_t kind) {
  return kind >= TINYMOCk_VALUE_SIGNED && kind <= TINYMOCk_VALUE_LONG_DOUBLE;
}

static long double tinymock_value_as_numeric__(tinymock_value_t value) {
  switch (value.kind) {
  case TINYMOCk_VALUE_SIGNED: return (long double)value.as.signed_value;
  case TINYMOCk_VALUE_UNSIGNED: return (long double)value.as.unsigned_value;
  case TINYMOCk_VALUE_FLOAT: return (long double)value.as.float_value;
  case TINYMOCk_VALUE_DOUBLE: return (long double)value.as.double_value;
  case TINYMOCk_VALUE_LONG_DOUBLE: return value.as.long_double_value;
  default: return 0.0L;
  }
}

static bool tinymock_value_equal__(tinymock_value_t expected, tinymock_value_t actual) {
  if (expected.kind == TINYMOCk_VALUE_STRING && actual.kind == TINYMOCk_VALUE_STRING) {
    const char *left = expected.as.string_value;
    const char *right = actual.as.string_value;
    return left && right ? strcmp(left, right) == 0 : left == right;
  }
  if (expected.kind == TINYMOCk_VALUE_POINTER && actual.kind == TINYMOCk_VALUE_POINTER) {
    return expected.as.pointer_value == actual.as.pointer_value;
  }
  if (!tinymock_is_numeric__(expected.kind) || !tinymock_is_numeric__(actual.kind)) return false;

  if (expected.kind == TINYMOCk_VALUE_SIGNED && actual.kind == TINYMOCk_VALUE_SIGNED)
    return expected.as.signed_value == actual.as.signed_value;
  if (expected.kind == TINYMOCk_VALUE_UNSIGNED && actual.kind == TINYMOCk_VALUE_UNSIGNED)
    return expected.as.unsigned_value == actual.as.unsigned_value;
  if (expected.kind == TINYMOCk_VALUE_SIGNED && actual.kind == TINYMOCk_VALUE_UNSIGNED)
    return expected.as.signed_value >= 0 &&
           (unsigned long long)expected.as.signed_value == actual.as.unsigned_value;
  if (expected.kind == TINYMOCk_VALUE_UNSIGNED && actual.kind == TINYMOCk_VALUE_SIGNED)
    return actual.as.signed_value >= 0 &&
           expected.as.unsigned_value == (unsigned long long)actual.as.signed_value;

  {
    const long double left = tinymock_value_as_numeric__(expected);
    const long double right = tinymock_value_as_numeric__(actual);
    return !isunordered(left, right) && !islessgreater(left, right);
  }
}

static void tinymock_value_dump__(char *out, size_t out_size, tinymock_value_t value) {
  switch (value.kind) {
  case TINYMOCk_VALUE_SIGNED:
    snprintf(out, out_size, "%lld", value.as.signed_value);
    break;
  case TINYMOCk_VALUE_UNSIGNED:
    snprintf(out, out_size, "%llu", value.as.unsigned_value);
    break;
  case TINYMOCk_VALUE_FLOAT:
    snprintf(out, out_size, "%f", (double)value.as.float_value);
    break;
  case TINYMOCk_VALUE_DOUBLE:
    snprintf(out, out_size, "%f", value.as.double_value);
    break;
  case TINYMOCk_VALUE_LONG_DOUBLE:
    snprintf(out, out_size, "%Lf", value.as.long_double_value);
    break;
  case TINYMOCk_VALUE_STRING:
    if (value.as.string_value)
      snprintf(out, out_size, "\"%s\"", value.as.string_value);
    else
      snprintf(out, out_size, "(null)");
    break;
  case TINYMOCk_VALUE_POINTER:
    snprintf(out, out_size, "%p", value.as.pointer_value);
    break;
  default:
    snprintf(out, out_size, "(none)");
    break;
  }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static bool tinymock_require__(tinymock_mock_t *mock, bool condition, const char *format, ...) {
  char message[512];
  va_list args;
  if (condition) return true;

  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (mock) mock->last_error = "tinymock failure";
  if (mock && mock->fail) {
    mock->fail(message);
    return false;
  }
  fprintf(stderr, "%s\n", message);
  abort();
}

void tinymock_state_machine_init(tinymock_state_machine_t *state,
                                 tinymock_state_id_t initial,
                                 const tinymock_transition_t *table,
                                 size_t table_count) {
  state->current = initial;
  state->table = table;
  state->table_count = table_count;
}

int tinymock_transition(tinymock_state_machine_t *state, tinymock_event_id_t event) {
  size_t index;
  for (index = 0; index < state->table_count; ++index) {
    const tinymock_transition_t *transition = &state->table[index];
    if (transition->from == state->current && transition->event == event) {
      state->current = transition->to;
      return 0;
    }
  }
  return -1;
}

#define TINYMOCK_VALUE_CONSTRUCTOR__(name, kind_value, member, type) \
  tinymock_value_t name(type input) {                         \
    tinymock_value_t value = tinymock_value_zero();           \
    value.kind = (kind_value);                                \
    value.as.member = input;                                  \
    return value;                                             \
  }

TINYMOCK_VALUE_CONSTRUCTOR__(tinymock_detail_box_signed, TINYMOCk_VALUE_SIGNED, signed_value,
                             long long)
TINYMOCK_VALUE_CONSTRUCTOR__(tinymock_detail_box_unsigned, TINYMOCk_VALUE_UNSIGNED, unsigned_value,
                             unsigned long long)
TINYMOCK_VALUE_CONSTRUCTOR__(tinymock_detail_box_float, TINYMOCk_VALUE_FLOAT, float_value, float)
TINYMOCK_VALUE_CONSTRUCTOR__(tinymock_detail_box_double, TINYMOCk_VALUE_DOUBLE, double_value, double)
TINYMOCK_VALUE_CONSTRUCTOR__(tinymock_detail_box_long_double, TINYMOCk_VALUE_LONG_DOUBLE,
                             long_double_value, long double)
TINYMOCK_VALUE_CONSTRUCTOR__(tinymock_detail_box_cstr, TINYMOCk_VALUE_STRING, string_value,
                             const char *)
TINYMOCK_VALUE_CONSTRUCTOR__(tinymock_detail_box_ptr, TINYMOCk_VALUE_POINTER, pointer_value,
                             const void *)

#undef TINYMOCK_VALUE_CONSTRUCTOR__

tinymock_value_t tinymock_value_zero(void) {
  tinymock_value_t value;
  memset(&value, 0, sizeof(value));
  value.kind = TINYMOCk_VALUE_NONE;
  return value;
}

long long tinymock_detail_unbox_signed(tinymock_value_t value) {
  switch (value.kind) {
  case TINYMOCk_VALUE_SIGNED: return value.as.signed_value;
  case TINYMOCk_VALUE_UNSIGNED: return (long long)value.as.unsigned_value;
  default: return (long long)tinymock_value_as_numeric__(value);
  }
}

unsigned long long tinymock_detail_unbox_unsigned(tinymock_value_t value) {
  switch (value.kind) {
  case TINYMOCk_VALUE_UNSIGNED: return value.as.unsigned_value;
  case TINYMOCk_VALUE_SIGNED: return (unsigned long long)value.as.signed_value;
  default: return (unsigned long long)tinymock_value_as_numeric__(value);
  }
}

float tinymock_detail_unbox_float(tinymock_value_t value) {
  return (float)tinymock_value_as_numeric__(value);
}

double tinymock_detail_unbox_double(tinymock_value_t value) {
  return (double)tinymock_value_as_numeric__(value);
}

long double tinymock_detail_unbox_long_double(tinymock_value_t value) {
  return tinymock_value_as_numeric__(value);
}

const char *tinymock_detail_unbox_cstr(tinymock_value_t value) {
  return value.kind == TINYMOCk_VALUE_STRING ? value.as.string_value : NULL;
}

const void *tinymock_detail_unbox_ptr(tinymock_value_t value) {
  return value.kind == TINYMOCk_VALUE_POINTER ? value.as.pointer_value : NULL;
}

tinymock_expected_arg_t tinymock_expected_arg(tinymock_value_t value) {
  tinymock_expected_arg_t arg;
  arg.any = false;
  arg.value = value;
  return arg;
}

tinymock_expected_arg_t tinymock_expected_arg_any(void) {
  tinymock_expected_arg_t arg;
  arg.any = true;
  arg.value = tinymock_value_zero();
  return arg;
}

void tinymock_mock_init__(tinymock_mock_t *mock, const char *name, tinymock_failure_fn fail) {
  memset(mock, 0, sizeof(*mock));
  mock->name = name;
  mock->last_script_kind = TINYMOCk_SCRIPT_RETURN;
  mock->fail = fail;
  tinymock_state_machine_init(&mock->state, 0, NULL, 0);
}

void tinymock_mock_init_ex__(tinymock_mock_t *mock, const char *name,
                             tinymock_state_id_t initial_state,
                             const tinymock_transition_t *table, size_t table_count,
                             tinymock_failure_fn fail) {
  tinymock_mock_init__(mock, name, fail);
  tinymock_state_machine_init(&mock->state, initial_state, table, table_count);
}

void tinymock_mock_set_default_return(tinymock_mock_t *mock, tinymock_value_t ret) {
  mock->default_return = ret;
  mock->has_default_return = true;
}

void tinymock_mock_expect(tinymock_mock_t *mock, size_t argc,
                          const tinymock_expected_arg_t *args, bool has_return,
                          tinymock_value_t ret) {
  size_t index;
  tinymock_expectation_t *expectation;
  if (!tinymock_require__(mock, mock->expected_count < TINYMOCk_MAX_EXPECTATIONS,
                         "tinymock %s: too many expectations (max=%d)", mock->name,
                         TINYMOCk_MAX_EXPECTATIONS))
    return;
  expectation = &mock->expectations[mock->expected_count++];
  expectation->argc = argc;
  expectation->has_return = has_return;
  expectation->ret = ret;
  for (index = 0; index < argc; ++index) expectation->args[index] = args[index];
}

tinymock_value_t tinymock_mock_invoke(tinymock_mock_t *mock, size_t argc,
                                      const tinymock_value_t *actual_args) {
  tinymock_expectation_t *expectation;
  size_t index;
  ++mock->call_count;

  if (mock->cursor >= mock->expected_count) {
    if (mock->has_default_return) return mock->default_return;
    tinymock_require__(mock, false, "tinymock %s: unexpected call #%zu (no expectation)",
                       mock->name, mock->cursor + 1);
    return tinymock_value_zero();
  }

  expectation = &mock->expectations[mock->cursor];
  if (!tinymock_require__(mock, expectation->argc == argc,
                         "tinymock %s: call #%zu argument count mismatch: expected %zu got %zu",
                         mock->name, mock->cursor + 1, expectation->argc, argc))
    return mock->has_default_return ? mock->default_return : tinymock_value_zero();

  for (index = 0; index < argc; ++index) {
    char expected_text[128];
    char actual_text[128];
    if (expectation->args[index].any) continue;
    if (tinymock_value_equal__(expectation->args[index].value, actual_args[index])) continue;
    tinymock_value_dump__(expected_text, sizeof(expected_text), expectation->args[index].value);
    tinymock_value_dump__(actual_text, sizeof(actual_text), actual_args[index]);
    tinymock_require__(mock, false,
                       "tinymock %s: call #%zu arg #%zu mismatch: expected=%s, got=%s",
                       mock->name, mock->cursor + 1, index, expected_text, actual_text);
    return mock->has_default_return ? mock->default_return : tinymock_value_zero();
  }

  ++mock->cursor;
  return expectation->has_return ? expectation->ret : tinymock_value_zero();
}

void tinymock_mock_verify(tinymock_mock_t *mock) {
  tinymock_require__(mock, mock->cursor == mock->expected_count,
                     "tinymock %s: expected %zu calls, got %zu",
                     mock->name ? mock->name : "(unnamed)", mock->expected_count, mock->cursor);
}

void tinymock_mock_script_return(tinymock_mock_t *mock, tinymock_value_t value) {
  tinymock_script_t *script;
  if (!tinymock_require__(mock, mock->script_count < TINYMOCk_MAX_SCRIPTS,
                         "tinymock %s: too many scripts", mock->name))
    return;
  script = &mock->scripts[mock->script_count++];
  script->kind = TINYMOCk_SCRIPT_RETURN;
  script->value = value;
  script->message = NULL;
}

void tinymock_mock_script_null_result(tinymock_mock_t *mock) {
  tinymock_script_t *script;
  if (!tinymock_require__(mock, mock->script_count < TINYMOCk_MAX_SCRIPTS,
                         "tinymock %s: too many scripts", mock->name))
    return;
  script = &mock->scripts[mock->script_count++];
  script->kind = TINYMOCk_SCRIPT_NULL_RESULT;
  script->value = tinymock_value_zero();
  script->message = NULL;
}

void tinymock_mock_script_error(tinymock_mock_t *mock, const char *message) {
  tinymock_script_t *script;
  if (!tinymock_require__(mock, mock->script_count < TINYMOCk_MAX_SCRIPTS,
                         "tinymock %s: too many scripts", mock->name))
    return;
  script = &mock->scripts[mock->script_count++];
  script->kind = TINYMOCk_SCRIPT_ERROR;
  script->value = tinymock_value_zero();
  script->message = message;
}

tinymock_value_t tinymock_mock_dispatch(tinymock_mock_t *mock, size_t argc,
                                        const tinymock_value_t *actual_args) {
  size_t index;
  tinymock_recorded_call_t *call;
  if (!tinymock_require__(mock, mock->call_count < TINYMOCk_MAX_CALLS,
                         "tinymock %s: too many recorded calls", mock->name))
    return tinymock_value_zero();

  call = &mock->calls[mock->call_count];
  call->argc = argc;
  for (index = 0; index < argc; ++index) call->args[index] = actual_args[index];

  if (mock->script_cursor < mock->script_count) {
    tinymock_script_t *script = &mock->scripts[mock->script_cursor++];
    mock->last_script_kind = script->kind;
    mock->last_error = script->kind == TINYMOCk_SCRIPT_ERROR ? script->message : NULL;
    ++mock->call_count;
    return script->value;
  }

  return tinymock_mock_invoke(mock, argc, actual_args);
}
