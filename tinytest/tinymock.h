/*
 * TinyMock for TinyTest.
 *
 * Goal:
 * - Keep usage compact for simple dependency-like function mocking.
 * - Use C11 _Generic helpers for expectation value construction.
 * - Keep C89/C99 fallback path by explicit typed helpers.
 *
 * Typical workflow:
 *   - Define a mock for target signature in test TU:
 *       TINYMOCk_MOCK2(int, read_sensor, int, int);
 *   - Register calls:
 *       mock_read_sensor_expect(TINYMOCk_ARG(1), TINYMOCk_ARG(2), TINYMOCk_RETURN(3));
 *   - Run code under test.
 *   - Verify:
 *       mock_read_sensor_verify();
 */

#ifndef TINYMOCK_H
#define TINYMOCK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__cplusplus)
/* bool is a built-in keyword in C++. */
#else
#if defined(__has_include)
#if __has_include(<stdbool.h>)
#include <stdbool.h>
#else
typedef unsigned char bool;
#define true ((bool)1)
#define false ((bool)0)
#endif
#else
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#include <stdbool.h>
#else
typedef unsigned char bool;
#define true ((bool)1)
#define false ((bool)0)
#endif
#endif
#endif

#include "tinytest.h"

#ifndef TINYMOCk_ASSERT
#define TINYMOCk_ASSERT(condition, ...) check((condition), __VA_ARGS__)
#endif

#ifndef TINYMOCk_MAX_VALUE_BYTES
#define TINYMOCk_MAX_VALUE_BYTES 16
#endif

#ifndef TINYMOCk_MAX_ARGS
#define TINYMOCk_MAX_ARGS 6
#endif

#ifndef TINYMOCk_MAX_EXPECTATIONS
#define TINYMOCk_MAX_EXPECTATIONS 32
#endif

#ifndef TINYMOCk_MAX_CALLS
#define TINYMOCk_MAX_CALLS 32
#endif

#ifndef TINYMOCk_MAX_SCRIPTS
#define TINYMOCk_MAX_SCRIPTS 32
#endif

#define TINYMOCk_STRINGIZE(x) #x

/*
 * Token pasting helpers.
 */
#define TINYMOCk_CAT(a, b) TINYMOCk_CAT_I(a, b)
#define TINYMOCk_CAT_I(a, b) a##b
#define TINYMOCk_CAT3(a, b, c) TINYMOCk_CAT(TINYMOCk_CAT(a, b), c)
#define TINYMOCk_CAT4(a, b, c, d) TINYMOCk_CAT(TINYMOCk_CAT3(a, b, c), d)

/* Compatibility aliases for older references inside the file. */
#define TINYMOCk_CONCAT(a, b) TINYMOCk_CAT(a, b)
#define TINYMOCk_CONCAT3(a, b, c) TINYMOCk_CAT3(a, b, c)
#define TINYMOCk_CONCAT4(a, b, c, d) TINYMOCk_CAT4(a, b, c, d)

/* Helper names for generated mock symbols. */
#define TINYMOCk_MOCK_DATA(NAME) TINYMOCk_CAT(tinymock_, NAME)
#define TINYMOCk_MOCK_FN(NAME, SUF) TINYMOCk_CAT(TINYMOCk_CAT(mock_, NAME), SUF)

typedef bool (*tinymock_eq_fn)(const void *expected, const void *actual);
typedef void (*tinymock_dump_fn)(char *out, size_t out_size, const void *value);

typedef struct {
  size_t size;
  unsigned char storage[TINYMOCk_MAX_VALUE_BYTES];
  const void *value;
  tinymock_eq_fn eq;
  tinymock_dump_fn dump;
} tinymock_value_t;

typedef struct {
  bool any;
  tinymock_value_t value;
} tinymock_expected_arg_t;

#define TINYMOCk_SCRIPT_KIND_X                                                    \
  X(TINYMOCk_SCRIPT_RETURN)                                                       \
  X(TINYMOCk_SCRIPT_NULL_RESULT)                                                  \
  X(TINYMOCk_SCRIPT_ERROR)

typedef enum {
#define X(name) name,
  TINYMOCk_SCRIPT_KIND_X
#undef X
} tinymock_script_kind_t;

static const char *const tinymock_script_kind_names[] = {
#define X(name) #name,
  TINYMOCk_SCRIPT_KIND_X
#undef X
};

typedef struct {
  size_t argc;
  tinymock_value_t args[TINYMOCk_MAX_ARGS];
} tinymock_recorded_call_t;

typedef struct {
  tinymock_script_kind_t kind;
  tinymock_value_t value;
  const char *message;
} tinymock_script_t;

typedef int tinymock_state_id_t;
typedef int tinymock_event_id_t;

typedef struct {
  tinymock_state_id_t from;
  tinymock_event_id_t event;
  tinymock_state_id_t to;
} tinymock_transition_t;

typedef struct {
  tinymock_state_id_t current;
  const tinymock_transition_t *table;
  size_t table_count;
} tinymock_state_machine_t;

static inline void tinymock_state_machine_init(
    tinymock_state_machine_t *state,
    tinymock_state_id_t initial,
    const tinymock_transition_t *table,
    size_t table_count) {
  state->current = initial;
  state->table = table;
  state->table_count = table_count;
}

static inline int tinymock_transition(tinymock_state_machine_t *state,
                                      tinymock_event_id_t event) {
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

typedef struct {
  tinymock_expected_arg_t args[TINYMOCk_MAX_ARGS];
  size_t argc;
  bool has_return;
  tinymock_value_t ret;
} tinymock_expectation_t;

typedef struct {
  const char *name;
  size_t call_count;
  size_t cursor;
  size_t expected_count;
  tinymock_expectation_t expectations[TINYMOCk_MAX_EXPECTATIONS];
  bool has_default_return;
  tinymock_value_t default_return;

  tinymock_recorded_call_t calls[TINYMOCk_MAX_CALLS];
  tinymock_script_t scripts[TINYMOCk_MAX_SCRIPTS];
  size_t script_count;
  size_t script_cursor;
  tinymock_state_machine_t state;
  tinymock_script_kind_t last_script_kind;
  const char *last_error;
} tinymock_mock_t;

static inline void
tinymock_value_init_bytes(tinymock_value_t *value, const void *src, size_t size, tinymock_eq_fn eq,
                          tinymock_dump_fn dump) {
  value->size = size;
  value->eq = eq;
  value->dump = dump;
  value->value = value->storage;
  TINYMOCk_ASSERT(value->size <= TINYMOCk_MAX_VALUE_BYTES, "tinymock: value size %zu exceeds inline storage %d",
                  value->size, TINYMOCk_MAX_VALUE_BYTES);
  if (src) {
    memcpy((void *)value->storage, src, value->size);
  } else {
    memset((void *)value->storage, 0, value->size);
  }
}

static inline void tinymock_value_copy_to(tinymock_value_t *dest, const tinymock_value_t *src) {
  if (dest == NULL || src == NULL) {
    return;
  }

  memset(dest, 0, sizeof(*dest));
  dest->size = src->size;
  dest->eq = src->eq;
  dest->dump = src->dump;

  if (src->size > 0) {
    memcpy((void *)dest->storage, src->storage, dest->size);
    dest->value = dest->storage;
  } else {
    dest->value = src->value;
  }
}

static inline tinymock_value_t tinymock_value_clone(tinymock_value_t value) {
  tinymock_value_t clone;
  tinymock_value_copy_to(&clone, &value);
  return clone;
}

static inline bool tinymock_value_eq_int(const void *expected, const void *actual) {
  return *(const int *)expected == *(const int *)actual;
}
static inline bool tinymock_value_eq_uint(const void *expected, const void *actual) {
  return *(const unsigned int *)expected == *(const unsigned int *)actual;
}
static inline bool tinymock_value_eq_long(const void *expected, const void *actual) {
  return *(const long *)expected == *(const long *)actual;
}
static inline bool tinymock_value_eq_ulong(const void *expected, const void *actual) {
  return *(const unsigned long *)expected == *(const unsigned long *)actual;
}
static inline bool tinymock_value_eq_ll(const void *expected, const void *actual) {
  return *(const long long *)expected == *(const long long *)actual;
}
static inline bool tinymock_value_eq_ull(const void *expected, const void *actual) {
  return *(const unsigned long long *)expected == *(const unsigned long long *)actual;
}
static inline bool tinymock_value_eq_size(const void *expected, const void *actual) {
  return *(const size_t *)expected == *(const size_t *)actual;
}
static inline bool tinymock_value_eq_float(const void *expected, const void *actual) {
  return *(const float *)expected == *(const float *)actual;
}
static inline bool tinymock_value_eq_double(const void *expected, const void *actual) {
  return *(const double *)expected == *(const double *)actual;
}
static inline bool tinymock_value_eq_ldouble(const void *expected, const void *actual) {
  return *(const long double *)expected == *(const long double *)actual;
}
static inline bool tinymock_value_eq_ptr(const void *expected, const void *actual) {
  return *(const void *const *)expected == *(const void *const *)actual;
}
static inline bool tinymock_value_eq_cstr(const void *expected, const void *actual) {
  const char *expected_cstr = *(const char *const *)expected;
  const char *actual_cstr = *(const char *const *)actual;
  if (expected_cstr == NULL || actual_cstr == NULL) {
    return expected_cstr == actual_cstr;
  }
  return strcmp(expected_cstr, actual_cstr) == 0;
}
static inline bool tinymock_value_eq_bool(const void *expected, const void *actual) {
  return *(const bool *)expected == *(const bool *)actual;
}

static inline void tinymock_value_dump_int(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%d", *(const int *)value);
}
static inline void tinymock_value_dump_uint(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%u", *(const unsigned int *)value);
}
static inline void tinymock_value_dump_long(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%ld", *(const long *)value);
}
static inline void tinymock_value_dump_ulong(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%lu", *(const unsigned long *)value);
}
static inline void tinymock_value_dump_ll(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%lld", *(const long long *)value);
}
static inline void tinymock_value_dump_ull(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%llu", *(const unsigned long long *)value);
}
static inline void tinymock_value_dump_size(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%zu", *(const size_t *)value);
}
static inline void tinymock_value_dump_float(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%f", *(const float *)value);
}
static inline void tinymock_value_dump_double(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%f", *(const double *)value);
}
static inline void tinymock_value_dump_ldouble(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%Lf", *(const long double *)value);
}
static inline void tinymock_value_dump_ptr(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%p", *(const void *const *)value);
}
static inline void tinymock_value_dump_cstr(char *out, size_t out_size, const void *value) {
  const char *v = *(const char *const *)value;
  if (v == NULL) {
    snprintf(out, out_size, "(null)");
  } else {
    snprintf(out, out_size, "\"%s\"", v);
  }
}
static inline void tinymock_value_dump_bool(char *out, size_t out_size, const void *value) {
  snprintf(out, out_size, "%s", *(const bool *)value ? "true" : "false");
}

static inline tinymock_value_t tinymock_value_int(int v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_int, tinymock_value_dump_int);
  return value;
}
static inline tinymock_value_t tinymock_value_uint(unsigned int v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_uint, tinymock_value_dump_uint);
  return value;
}
static inline tinymock_value_t tinymock_value_long(long v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_long, tinymock_value_dump_long);
  return value;
}
static inline tinymock_value_t tinymock_value_ulong(unsigned long v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_ulong, tinymock_value_dump_ulong);
  return value;
}
static inline tinymock_value_t tinymock_value_ll(long long v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_ll, tinymock_value_dump_ll);
  return value;
}
static inline tinymock_value_t tinymock_value_ull(unsigned long long v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_ull, tinymock_value_dump_ull);
  return value;
}
static inline tinymock_value_t tinymock_value_size(size_t v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_size, tinymock_value_dump_size);
  return value;
}
static inline tinymock_value_t tinymock_value_float(float v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_float, tinymock_value_dump_float);
  return value;
}
static inline tinymock_value_t tinymock_value_double(double v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_double, tinymock_value_dump_double);
  return value;
}
static inline tinymock_value_t tinymock_value_ldouble(long double v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_ldouble, tinymock_value_dump_ldouble);
  return value;
}
static inline tinymock_value_t tinymock_value_ptr(const void *v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_ptr, tinymock_value_dump_ptr);
  return value;
}
static inline tinymock_value_t tinymock_value_cstr(const char *v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_cstr, tinymock_value_dump_cstr);
  return value;
}
static inline tinymock_value_t tinymock_value_bool(bool v) {
  tinymock_value_t value;
  tinymock_value_init_bytes(&value, &v, sizeof(v), tinymock_value_eq_bool, tinymock_value_dump_bool);
  return value;
}

static inline tinymock_expected_arg_t tinymock_expected_arg(tinymock_value_t value) {
  tinymock_expected_arg_t arg;
  arg.any = false;
  tinymock_value_copy_to(&arg.value, &value);
  return arg;
}
static inline tinymock_expected_arg_t tinymock_expected_arg_any(void) {
  tinymock_expected_arg_t arg;
  arg.any = true;
  memset(&arg.value, 0, sizeof(arg.value));
  return arg;
}

static inline tinymock_expected_arg_t tinymock_expected_arg_clone(const tinymock_expected_arg_t *arg) {
  tinymock_expected_arg_t copy;
  copy.any = arg->any;
  if (arg->any) {
    memset(&copy.value, 0, sizeof(copy.value));
  } else {
    tinymock_value_copy_to(&copy.value, &arg->value);
  }
  return copy;
}
static inline void tinymock_expected_arg_copy(tinymock_expected_arg_t *dest, const tinymock_expected_arg_t *src) {
  if (dest == NULL || src == NULL) {
    return;
  }

  dest->any = src->any;
  if (src->any) {
    memset(&dest->value, 0, sizeof(dest->value));
  } else {
    tinymock_value_copy_to(&dest->value, &src->value);
  }
}
static inline tinymock_expected_arg_t tinymock_arg_int(int v) { return tinymock_expected_arg(tinymock_value_int(v)); }
static inline tinymock_expected_arg_t tinymock_arg_uint(unsigned int v) { return tinymock_expected_arg(tinymock_value_uint(v)); }
static inline tinymock_expected_arg_t tinymock_arg_long(long v) { return tinymock_expected_arg(tinymock_value_long(v)); }
static inline tinymock_expected_arg_t tinymock_arg_ulong(unsigned long v) { return tinymock_expected_arg(tinymock_value_ulong(v)); }
static inline tinymock_expected_arg_t tinymock_arg_ll(long long v) { return tinymock_expected_arg(tinymock_value_ll(v)); }
static inline tinymock_expected_arg_t tinymock_arg_ull(unsigned long long v) { return tinymock_expected_arg(tinymock_value_ull(v)); }
static inline tinymock_expected_arg_t tinymock_arg_size(size_t v) { return tinymock_expected_arg(tinymock_value_size(v)); }
static inline tinymock_expected_arg_t tinymock_arg_float(float v) { return tinymock_expected_arg(tinymock_value_float(v)); }
static inline tinymock_expected_arg_t tinymock_arg_double(double v) { return tinymock_expected_arg(tinymock_value_double(v)); }
static inline tinymock_expected_arg_t tinymock_arg_ldouble(long double v) { return tinymock_expected_arg(tinymock_value_ldouble(v)); }
static inline tinymock_expected_arg_t tinymock_arg_ptr(const void *v) { return tinymock_expected_arg(tinymock_value_ptr(v)); }
static inline tinymock_expected_arg_t tinymock_arg_str(const char *v) { return tinymock_expected_arg(tinymock_value_cstr(v)); }
static inline tinymock_expected_arg_t tinymock_arg_bool(bool v) { return tinymock_expected_arg(tinymock_value_bool(v)); }

#if !defined(__cplusplus) && !defined(__TINYMOCk_HAS_C11_GENERIC__)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define __TINYMOCk_HAS_C11_GENERIC__ 1
#elif defined(_MSC_VER)
#define __TINYMOCk_HAS_C11_GENERIC__ 1
#endif
#endif

#if defined(__TINYMOCk_HAS_C11_GENERIC__)
#define TINYMOCk_VALUE(v) \
  _Generic((v),                                                                              \
           char: tinymock_value_int,                                                          \
           signed char: tinymock_value_int,                                                   \
           unsigned char: tinymock_value_uint,                                                \
           short: tinymock_value_int,                                                         \
           unsigned short: tinymock_value_uint,                                               \
           int: tinymock_value_int,                                                           \
           unsigned int: tinymock_value_uint,                                                 \
           long: tinymock_value_long,                                                         \
           unsigned long: tinymock_value_ulong,                                               \
           long long: tinymock_value_ll,                                                      \
           unsigned long long: tinymock_value_ull,                                            \
           float: tinymock_value_float,                                                       \
           double: tinymock_value_double,                                                     \
           long double: tinymock_value_ldouble,                                               \
           _Bool: tinymock_value_bool,                                                       \
           void *: tinymock_value_ptr,                                                       \
           const void *: tinymock_value_ptr,                                                 \
           char *: tinymock_value_cstr,                                                      \
           const char *: tinymock_value_cstr)(v)

#define TINYMOCk_ARG(v) tinymock_expected_arg(TINYMOCk_VALUE(v))
#define TINYMOCk_RETURN(v) TINYMOCk_VALUE(v)
#define TINYMOCk_MOCK_EXPECT_ARG_T tinymock_expected_arg_t
#define TINYMOCk_MOCK_EXPECT_ARG_PASS(arg) (arg)
#else
/* C89/C99 fallback: explicit constructors only. */
#define TINYMOCk_ARG(v) tinymock_arg_int((v))
#define TINYMOCk_RETURN(v) tinymock_value_int((v))
#define TINYMOCk_MOCK_EXPECT_ARG_T tinymock_expected_arg_t
#define TINYMOCk_MOCK_EXPECT_ARG_PASS(arg) (arg)
#endif

#if defined(__TINYMOCk_HAS_C11_GENERIC__)
#define TINYMOCk_ANY tinymock_expected_arg_any()
#else
#define TINYMOCk_ANY tinymock_expected_arg_any()
#endif

static inline bool tinymock_expected_arg_match(const tinymock_expected_arg_t *expected, const void *actual) {
  if (expected->any) {
    return true;
  }
  return expected->value.eq(expected->value.value, actual);
}

static inline void tinymock_mock_init(tinymock_mock_t *mock, const char *name) {
  memset(mock, 0, sizeof(*mock));
  mock->name = name;
  mock->last_script_kind = TINYMOCk_SCRIPT_RETURN;
  tinymock_state_machine_init(&mock->state, 0, NULL, 0);
}

static inline void tinymock_mock_init_ex(tinymock_mock_t *mock,
                                         const char *name,
                                         tinymock_state_id_t initial_state,
                                         const tinymock_transition_t *table,
                                         size_t table_count) {
  tinymock_mock_init(mock, name);
  tinymock_state_machine_init(&mock->state, initial_state, table, table_count);
}

static inline void tinymock_mock_reset(tinymock_mock_t *mock, const char *name) {
  if (mock->name == NULL || mock->name[0] == '\0') {
    mock->name = name;
  }
  memset(mock->expectations, 0, sizeof(mock->expectations));
  mock->call_count = 0;
  mock->cursor = 0;
  mock->expected_count = 0;
  mock->has_default_return = false;
}

static inline void tinymock_mock_set_default_return(tinymock_mock_t *mock, tinymock_value_t ret) {
  tinymock_value_copy_to(&mock->default_return, &ret);
  mock->has_default_return = true;
}

static inline void tinymock_mock_expect0(tinymock_mock_t *mock, bool has_return, tinymock_value_t ret) {
  TINYMOCk_ASSERT(mock->expected_count < TINYMOCk_MAX_EXPECTATIONS,
                  "tinymock %s: too many expectations (max=%d)", mock->name, TINYMOCk_MAX_EXPECTATIONS);
  if (mock->expected_count >= TINYMOCk_MAX_EXPECTATIONS) {
    return;
  }
  mock->expectations[mock->expected_count].argc = 0;
  mock->expectations[mock->expected_count].has_return = has_return;
  tinymock_value_copy_to(&mock->expectations[mock->expected_count].ret, &ret);
  ++mock->expected_count;
}

static inline void tinymock_mock_expect1(tinymock_mock_t *mock, tinymock_expected_arg_t a0, bool has_return,
                                       tinymock_value_t ret) {
  TINYMOCk_ASSERT(mock->expected_count < TINYMOCk_MAX_EXPECTATIONS,
                  "tinymock %s: too many expectations (max=%d)", mock->name, TINYMOCk_MAX_EXPECTATIONS);
  if (mock->expected_count >= TINYMOCk_MAX_EXPECTATIONS) {
    return;
  }
  mock->expectations[mock->expected_count].argc = 1;
  mock->expectations[mock->expected_count].has_return = has_return;
  tinymock_expected_arg_copy(&mock->expectations[mock->expected_count].args[0], &a0);
  tinymock_value_copy_to(&mock->expectations[mock->expected_count].ret, &ret);
  ++mock->expected_count;
}

static inline void tinymock_mock_expect2(tinymock_mock_t *mock, tinymock_expected_arg_t a0, tinymock_expected_arg_t a1,
                                        bool has_return, tinymock_value_t ret) {
  TINYMOCk_ASSERT(mock->expected_count < TINYMOCk_MAX_EXPECTATIONS,
                  "tinymock %s: too many expectations (max=%d)", mock->name, TINYMOCk_MAX_EXPECTATIONS);
  if (mock->expected_count >= TINYMOCk_MAX_EXPECTATIONS) {
    return;
  }
  mock->expectations[mock->expected_count].argc = 2;
  mock->expectations[mock->expected_count].has_return = has_return;
  tinymock_expected_arg_copy(&mock->expectations[mock->expected_count].args[0], &a0);
  tinymock_expected_arg_copy(&mock->expectations[mock->expected_count].args[1], &a1);
  tinymock_value_copy_to(&mock->expectations[mock->expected_count].ret, &ret);
  ++mock->expected_count;
}

static inline void tinymock_mock_expect3(tinymock_mock_t *mock, tinymock_expected_arg_t a0, tinymock_expected_arg_t a1,
                                        tinymock_expected_arg_t a2,
                                        bool has_return, tinymock_value_t ret) {
  TINYMOCk_ASSERT(mock->expected_count < TINYMOCk_MAX_EXPECTATIONS,
                  "tinymock %s: too many expectations (max=%d)", mock->name, TINYMOCk_MAX_EXPECTATIONS);
  if (mock->expected_count >= TINYMOCk_MAX_EXPECTATIONS) {
    return;
  }
  mock->expectations[mock->expected_count].argc = 3;
  mock->expectations[mock->expected_count].has_return = has_return;
  tinymock_expected_arg_copy(&mock->expectations[mock->expected_count].args[0], &a0);
  tinymock_expected_arg_copy(&mock->expectations[mock->expected_count].args[1], &a1);
  tinymock_expected_arg_copy(&mock->expectations[mock->expected_count].args[2], &a2);
  tinymock_value_copy_to(&mock->expectations[mock->expected_count].ret, &ret);
  ++mock->expected_count;
}

static inline bool tinymock_mock_expectation_match_args(const tinymock_expectation_t *expectation, const void *const *actual_args,
                                                      size_t actual_argc) {
  size_t i;
  if (expectation->argc != actual_argc) {
    return false;
  }
  for (i = 0; i < expectation->argc; ++i) {
    if (!tinymock_expected_arg_match(&expectation->args[i], actual_args[i])) {
      return false;
    }
  }
  return true;
}

static inline tinymock_value_t
tinymock_value_zero(void) {
  tinymock_value_t value;
  memset(&value, 0, sizeof(value));
  return value;
}

static inline tinymock_value_t tinymock_mock_invoke(tinymock_mock_t *mock, size_t argc, const void *const *actual_args) {
  tinymock_expectation_t *expectation;
  char expected_text[128];
  char actual_text[128];
  size_t i;
  ++mock->call_count;

  if (mock->cursor >= mock->expected_count) {
    if (mock->has_default_return) {
      return mock->default_return;
    }
    TINYMOCk_ASSERT(0, "tinymock %s: unexpected call #%zu (no expectation)", mock->name, mock->cursor + 1);
    return tinymock_value_zero();
  }

  expectation = &mock->expectations[mock->cursor];
  if (expectation->argc != argc) {
    TINYMOCk_ASSERT(0, "tinymock %s: call #%zu argument count mismatch: expected %zu got %zu", mock->name,
                    mock->cursor + 1, expectation->argc, argc);
    return mock->has_default_return ? mock->default_return : tinymock_value_zero();
  }

  for (i = 0; i < argc; ++i) {
    if (expectation->args[i].any) {
      continue;
    }
    if (!tinymock_expected_arg_match(&expectation->args[i], actual_args[i])) {
      if (expectation->args[i].value.dump) {
        expectation->args[i].value.dump(expected_text, sizeof(expected_text), expectation->args[i].value.value);
      } else {
        snprintf(expected_text, sizeof(expected_text), "(none)");
      }
      if (expectation->args[i].value.dump) {
        expectation->args[i].value.dump(actual_text, sizeof(actual_text), actual_args[i]);
      } else {
        snprintf(actual_text, sizeof(actual_text), "(none)");
      }
      TINYMOCk_ASSERT(0, "tinymock %s: call #%zu arg #%zu mismatch: expected=%s, got=%s", mock->name,
                      mock->cursor + 1, i, expected_text, actual_text);
      break;
    }
  }

  ++mock->cursor;
  if (!expectation->has_return) {
    return tinymock_value_zero();
  }
  return expectation->ret;
}

static inline void tinymock_mock_verify(tinymock_mock_t *mock) {
  TINYMOCk_ASSERT(mock->cursor == mock->expected_count,
                  "tinymock %s: expected %zu calls, got %zu",
                  mock->name ? mock->name : "(unnamed)",
                  mock->expected_count, mock->cursor);
}

static inline void tinymock_mock_script_return(tinymock_mock_t *mock,
                                               tinymock_value_t value) {
  TINYMOCk_ASSERT(mock->script_count < TINYMOCk_MAX_SCRIPTS,
                  "tinymock %s: too many scripts", mock->name);
  mock->scripts[mock->script_count].kind = TINYMOCk_SCRIPT_RETURN;
  tinymock_value_copy_to(&mock->scripts[mock->script_count].value, &value);
  mock->scripts[mock->script_count].message = NULL;
  ++mock->script_count;
}

static inline void tinymock_mock_script_null_result(tinymock_mock_t *mock) {
  TINYMOCk_ASSERT(mock->script_count < TINYMOCk_MAX_SCRIPTS,
                  "tinymock %s: too many scripts", mock->name);
  mock->scripts[mock->script_count].kind = TINYMOCk_SCRIPT_NULL_RESULT;
  memset(&mock->scripts[mock->script_count].value, 0,
         sizeof(mock->scripts[mock->script_count].value));
  mock->scripts[mock->script_count].message = NULL;
  ++mock->script_count;
}

static inline void tinymock_mock_script_error(tinymock_mock_t *mock,
                                              const char *message) {
  TINYMOCk_ASSERT(mock->script_count < TINYMOCk_MAX_SCRIPTS,
                  "tinymock %s: too many scripts", mock->name);
  mock->scripts[mock->script_count].kind = TINYMOCk_SCRIPT_ERROR;
  memset(&mock->scripts[mock->script_count].value, 0,
         sizeof(mock->scripts[mock->script_count].value));
  mock->scripts[mock->script_count].message = message;
  ++mock->script_count;
}

static inline tinymock_value_t
tinymock_mock_dispatch(tinymock_mock_t *mock,
                       size_t argc,
                       const void *const *actual_args,
                       const tinymock_value_t *recorded_args) {
  tinymock_recorded_call_t *call;
  size_t index;

  TINYMOCk_ASSERT(mock->call_count < TINYMOCk_MAX_CALLS,
                  "tinymock %s: too many recorded calls", mock->name);
  call = &mock->calls[mock->call_count];
  call->argc = argc;
  for (index = 0; index < argc; ++index)
    tinymock_value_copy_to(&call->args[index], &recorded_args[index]);

  if (mock->script_cursor < mock->script_count) {
    tinymock_script_t *script = &mock->scripts[mock->script_cursor++];
    mock->last_script_kind = script->kind;
    mock->last_error =
        script->kind == TINYMOCk_SCRIPT_ERROR ? script->message : NULL;
    ++mock->call_count;
    return script->value;
  }

  return tinymock_mock_invoke(mock, argc, actual_args);
}

/*
 * Function wrappers: define 0/1/2/3-arg mocks.
 * Generates:
 *   - mock_<name>_reset()
 *   - mock_<name>_verify()
 *   - mock_<name>_expect(...)
 *   - mock_<name>_set_default_return(...)
 *   - function <name>(...) that dispatches through the mock registry
 */

#define TINYMOCk_MOCK0(RET, NAME)                                                                \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                        \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(tinymock_value_t ret) {                   \
    tinymock_mock_expect0(&TINYMOCk_MOCK_DATA(NAME), true, ret);                            \
  }                                                                                               \
  static inline RET NAME(void) {                                                                  \
    tinymock_value_t result;                                                                      \
    result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 0, NULL);                      \
    if (result.size == 0) {                                                                       \
      return (RET)0;                                                                              \
    }                                                                                             \
    {                                                                                             \
      RET out;                                                                                    \
      memset(&out, 0, sizeof(out));                                                               \
      memcpy(&out, result.value, sizeof(RET));                                                     \
      return out;                                                                                 \
    }                                                                                             \
  }

#define TINYMOCk_MOCK0_VOID(NAME)                                                                \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
    (void)ret;                                                                                    \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(void) {                                   \
    tinymock_mock_expect0(&TINYMOCk_MOCK_DATA(NAME), false, tinymock_value_zero());          \
  }                                                                                               \
  static inline void NAME(void)

#define TINYMOCk_MOCK1(RET, NAME, T0)                                                            \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                        \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,             \
                                                    tinymock_value_t ret) {                        \
    tinymock_mock_expect1(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0), true, ret); \
  }                                                                                               \
  static inline RET NAME(T0 a0) {                                                                 \
    const void *actual_args[] = {&a0};                                                            \
    tinymock_value_t result;                                                                      \
    result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 1, actual_args);               \
    if (result.size == 0) {                                                                       \
      return (RET)0;                                                                              \
    }                                                                                             \
    {                                                                                             \
      RET out;                                                                                    \
      memset(&out, 0, sizeof(out));                                                               \
      memcpy(&out, result.value, sizeof(RET));                                                     \
      return out;                                                                                 \
    }                                                                                             \
  }

#define TINYMOCk_MOCK1_VOID(NAME, T0)                                                             \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    (void)ret;                                                                                    \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0) {            \
    tinymock_mock_expect1(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0), false,       \
                          tinymock_value_zero());                                                  \
  }                                                                                               \
  static inline void NAME(T0 a0) {                                                                \
    const void *actual_args[] = {&a0};                                                            \
    tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 1, actual_args);                        \
  }

#define TINYMOCk_MOCK2(RET, NAME, T0, T1)                                                        \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                        \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,             \
                                                        TINYMOCk_MOCK_EXPECT_ARG_T arg1,                \
                                                        tinymock_value_t ret) {                      \
    tinymock_mock_expect2(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0),             \
                          TINYMOCk_MOCK_EXPECT_ARG_PASS(arg1), true, ret);                    \
  }                                                                                               \
  static inline RET NAME(T0 a0, T1 a1) {                                                          \
    const void *actual_args[] = {&a0, &a1};                                                       \
    tinymock_value_t result;                                                                      \
    result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 2, actual_args);               \
    if (result.size == 0) {                                                                       \
      return (RET)0;                                                                              \
    }                                                                                             \
    {                                                                                             \
      RET out;                                                                                    \
      memset(&out, 0, sizeof(out));                                                               \
      memcpy(&out, result.value, sizeof(RET));                                                     \
      return out;                                                                                 \
    }                                                                                             \
  }

#define TINYMOCk_MOCK2_VOID(NAME, T0, T1)                                                         \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    (void)ret;                                                                                    \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,             \
                                                    TINYMOCk_MOCK_EXPECT_ARG_T arg1) {               \
    tinymock_mock_expect2(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0),             \
                          TINYMOCk_MOCK_EXPECT_ARG_PASS(arg1), false, tinymock_value_zero()); \
  }                                                                                               \
  static inline void NAME(T0 a0, T1 a1) {                                                         \
    const void *actual_args[] = {&a0, &a1};                                                       \
    tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 2, actual_args);                        \
  }

#define TINYMOCk_MOCK3(RET, NAME, T0, T1, T2)                                                    \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                        \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,             \
                                                        TINYMOCk_MOCK_EXPECT_ARG_T arg1,                \
                                                        TINYMOCk_MOCK_EXPECT_ARG_T arg2,                \
                                                        tinymock_value_t ret) {                      \
    tinymock_mock_expect3(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0),             \
                          TINYMOCk_MOCK_EXPECT_ARG_PASS(arg1), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg2), true, \
                          ret);                                                                                   \
  }                                                                                               \
  static inline RET NAME(T0 a0, T1 a1, T2 a2) {                                                   \
    const void *actual_args[] = {&a0, &a1, &a2};                                                  \
    tinymock_value_t result;                                                                      \
    result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 3, actual_args);               \
    if (result.size == 0) {                                                                       \
      return (RET)0;                                                                              \
    }                                                                                             \
    {                                                                                             \
      RET out;                                                                                    \
      memset(&out, 0, sizeof(out));                                                               \
      memcpy(&out, result.value, sizeof(RET));                                                     \
      return out;                                                                                 \
    }                                                                                             \
  }

#define TINYMOCk_MOCK3_VOID(NAME, T0, T1, T2)                                                    \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                          \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                 \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                   \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                         \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {        \
    (void)ret;                                                                                    \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                          \
  }                                                                                               \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,             \
                                                        TINYMOCk_MOCK_EXPECT_ARG_T arg1,                \
                                                        TINYMOCk_MOCK_EXPECT_ARG_T arg2) {               \
    tinymock_mock_expect3(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0),             \
                          TINYMOCk_MOCK_EXPECT_ARG_PASS(arg1), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg2), false, \
                          tinymock_value_zero());                                                  \
  }                                                                                               \
  static inline void NAME(T0 a0, T1 a1, T2 a2) {                                                  \
    const void *actual_args[] = {&a0, &a1, &a2};                                                  \
    tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 3, actual_args);                        \
  }

#define TINYMOCk_MOCK_DECLARE(NAME) \
  extern tinymock_mock_t TINYMOCk_MOCK_DATA(NAME)

#define TINYMOCk_MOCK_DEFINE1(RET, NAME, T0)                                    \
  tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                     \
  void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));    \
  }                                                                             \
  void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                  \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                            \
  }                                                                             \
  void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,         \
                                      tinymock_value_t ret) {                   \
    tinymock_mock_expect1(&TINYMOCk_MOCK_DATA(NAME), arg0, true, ret);          \
  }                                                                             \
  RET NAME(T0 a0) {                                                             \
    const void *actual_args[] = {&a0};                                          \
    tinymock_value_t recorded_args[] = {TINYMOCk_VALUE(a0)};                    \
    tinymock_value_t result =                                                   \
        tinymock_mock_dispatch(&TINYMOCk_MOCK_DATA(NAME), 1,                    \
                               actual_args, recorded_args);                     \
    if (result.size == 0) return (RET)0;                                        \
    {                                                                           \
      RET out;                                                                  \
      memset(&out, 0, sizeof(out));                                             \
      memcpy(&out, result.value, sizeof(RET));                                  \
      return out;                                                               \
    }                                                                           \
  }

#define TINYMOCk_MOCK_DEFINE2(RET, NAME, T0, T1)                                \
  tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                     \
  void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                   \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));    \
  }                                                                             \
  void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                  \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                            \
  }                                                                             \
  void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,         \
                                      TINYMOCk_MOCK_EXPECT_ARG_T arg1,          \
                                      tinymock_value_t ret) {                   \
    tinymock_mock_expect2(&TINYMOCk_MOCK_DATA(NAME), arg0, arg1, true, ret);    \
  }                                                                             \
  RET NAME(T0 a0, T1 a1) {                                                      \
    const void *actual_args[] = {&a0, &a1};                                     \
    tinymock_value_t recorded_args[] = {TINYMOCk_VALUE(a0), TINYMOCk_VALUE(a1)};\
    tinymock_value_t result =                                                   \
        tinymock_mock_dispatch(&TINYMOCk_MOCK_DATA(NAME), 2,                    \
                               actual_args, recorded_args);                     \
    if (result.size == 0) return (RET)0;                                        \
    {                                                                           \
      RET out;                                                                  \
      memset(&out, 0, sizeof(out));                                             \
      memcpy(&out, result.value, sizeof(RET));                                  \
      return out;                                                               \
    }                                                                           \
  }
#endif



