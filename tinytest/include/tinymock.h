/*
 * TinyMock for TinyTest.
 *
 * C mocks use the strict-C11 trait map shared with TinyTest. Runtime state,
 * comparison, formatting, scripting, and verification live in the static
 * TinyTest library; only generated mock wrappers remain in this header.
 */

#ifndef TINYMOCK_H
#define TINYMOCK_H

#include <stddef.h>
#include <stdint.h>
#include "tinytest.h"

#ifndef __cplusplus
#include "tinymeta/tinytest_traits.h"
#endif

#ifndef TINYMOCk_ASSERT
#define TINYMOCk_ASSERT(condition, ...) check((condition), __VA_ARGS__)
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
#define TINYMOCk_CAT(a, b) TINYMOCk_CAT_I(a, b)
#define TINYMOCk_CAT_I(a, b) a##b
#define TINYMOCk_MOCK_DATA(NAME) TINYMOCk_CAT(tinymock_, NAME)
#define TINYMOCk_MOCK_FN(NAME, SUF) TINYMOCk_CAT(TINYMOCk_CAT(mock_, NAME), SUF)

typedef void (*tinymock_failure_fn)(const char *message);

typedef enum {
  TINYMOCk_VALUE_NONE,
  TINYMOCk_VALUE_SIGNED,
  TINYMOCk_VALUE_UNSIGNED,
  TINYMOCk_VALUE_FLOAT,
  TINYMOCk_VALUE_DOUBLE,
  TINYMOCk_VALUE_LONG_DOUBLE,
  TINYMOCk_VALUE_STRING,
  TINYMOCk_VALUE_POINTER
} tinymock_value_kind_t;

typedef struct {
  tinymock_value_kind_t kind;
  union {
    long long signed_value;
    unsigned long long unsigned_value;
    float float_value;
    double double_value;
    long double long_double_value;
    const char *string_value;
    const void *pointer_value;
  } as;
} tinymock_value_t;

typedef struct {
  bool any;
  tinymock_value_t value;
} tinymock_expected_arg_t;

typedef enum {
  TINYMOCk_SCRIPT_RETURN,
  TINYMOCk_SCRIPT_NULL_RESULT,
  TINYMOCk_SCRIPT_ERROR
} tinymock_script_kind_t;

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
  tinymock_failure_fn fail;
} tinymock_mock_t;

#ifdef __cplusplus
extern "C" {
#endif

void tinymock_state_machine_init(tinymock_state_machine_t *state,
                                 tinymock_state_id_t initial,
                                 const tinymock_transition_t *table,
                                 size_t table_count);
int tinymock_transition(tinymock_state_machine_t *state, tinymock_event_id_t event);

/* ABI-specific handlers selected by the generic value macros below. */
tinymock_value_t tinymock_detail_box_signed(long long value);
tinymock_value_t tinymock_detail_box_unsigned(unsigned long long value);
tinymock_value_t tinymock_detail_box_float(float value);
tinymock_value_t tinymock_detail_box_double(double value);
tinymock_value_t tinymock_detail_box_long_double(long double value);
tinymock_value_t tinymock_detail_box_cstr(const char *value);
tinymock_value_t tinymock_detail_box_ptr(const void *value);
tinymock_value_t tinymock_value_zero(void);

long long tinymock_detail_unbox_signed(tinymock_value_t value);
unsigned long long tinymock_detail_unbox_unsigned(tinymock_value_t value);
float tinymock_detail_unbox_float(tinymock_value_t value);
double tinymock_detail_unbox_double(tinymock_value_t value);
long double tinymock_detail_unbox_long_double(tinymock_value_t value);
const char *tinymock_detail_unbox_cstr(tinymock_value_t value);
const void *tinymock_detail_unbox_ptr(tinymock_value_t value);

tinymock_expected_arg_t tinymock_expected_arg(tinymock_value_t value);
tinymock_expected_arg_t tinymock_expected_arg_any(void);

void tinymock_mock_init__(tinymock_mock_t *mock, const char *name, tinymock_failure_fn fail);
void tinymock_mock_init_ex__(tinymock_mock_t *mock, const char *name,
                             tinymock_state_id_t initial_state,
                             const tinymock_transition_t *table, size_t table_count,
                             tinymock_failure_fn fail);
void tinymock_mock_set_default_return(tinymock_mock_t *mock, tinymock_value_t ret);
void tinymock_mock_expect(tinymock_mock_t *mock, size_t argc,
                          const tinymock_expected_arg_t *args, bool has_return,
                          tinymock_value_t ret);
tinymock_value_t tinymock_mock_invoke(tinymock_mock_t *mock, size_t argc,
                                      const tinymock_value_t *actual_args);
void tinymock_mock_verify(tinymock_mock_t *mock);
void tinymock_mock_script_return(tinymock_mock_t *mock, tinymock_value_t value);
void tinymock_mock_script_null_result(tinymock_mock_t *mock);
void tinymock_mock_script_error(tinymock_mock_t *mock, const char *message);
tinymock_value_t tinymock_mock_dispatch(tinymock_mock_t *mock, size_t argc,
                                        const tinymock_value_t *actual_args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#if !defined(TINYMOCK_BACKEND_SOURCE__)
static inline void tinymock_failure_adapter__(const char *message) {
  TINYMOCk_ASSERT(false, "%s", message);
}

#define tinymock_mock_init(mock, name) \
  tinymock_mock_init__((mock), (name), tinymock_failure_adapter__)
#define tinymock_mock_init_ex(mock, name, initial, table, table_count) \
  tinymock_mock_init_ex__((mock), (name), (initial), (table), (table_count), \
                          tinymock_failure_adapter__)
#endif

#ifndef __cplusplus
#define TINYMOCk_VALUE(value) \
  _Generic((value), \
    TTEST_C11_EQUAL_ASSOCIATIONS__(tinymock_detail_box_signed, \
      tinymock_detail_box_unsigned, tinymock_detail_box_float, \
      tinymock_detail_box_double, tinymock_detail_box_long_double, \
      tinymock_detail_box_cstr, tinymock_detail_box_ptr) \
  )(value)

#define TINYMOCk_VALUE_AS(type, value) \
  ((type)_Generic(((type)0), \
    TTEST_C11_EQUAL_ASSOCIATIONS__(tinymock_detail_unbox_signed, \
      tinymock_detail_unbox_unsigned, tinymock_detail_unbox_float, \
      tinymock_detail_unbox_double, tinymock_detail_unbox_long_double, \
      tinymock_detail_unbox_cstr, tinymock_detail_unbox_ptr) \
  )(value))

#define TINYMOCk_ARG(value) tinymock_expected_arg(TINYMOCk_VALUE(value))
#define TINYMOCk_RETURN(value) TINYMOCk_VALUE(value)
#define TINYMOCk_ANY tinymock_expected_arg_any()
#define TINYMOCk_MOCK_EXPECT_ARG_T tinymock_expected_arg_t
#define TINYMOCk_MOCK_EXPECT_ARG_PASS(arg) (arg)
#endif
/* C11 function wrappers. Variadic mocks support one through six arguments. */
#ifndef __cplusplus

#if defined(__GNUC__) || defined(__clang__)
#define TINYMOCk_GENERATED__ static inline __attribute__((unused))
#else
#define TINYMOCk_GENERATED__ static inline
#endif

#define TINYMOCk_PARAM_ROW__(index, types) \
  TTEST_PP_COMMA_IF__(index) TTEST_PP_ARG_AT__(index, types) \
      TTEST_PP_CAT__(a, index)
#define TINYMOCk_EXPECT_PARAM_ROW__(index, types) \
  TTEST_PP_COMMA_IF__(index) tinymock_expected_arg_t TTEST_PP_CAT__(arg, index)
#define TINYMOCk_EXPECT_VALUE_ROW__(index, types) \
  TTEST_PP_COMMA_IF__(index) TTEST_PP_CAT__(arg, index)
#define TINYMOCk_ACTUAL_VALUE_ROW__(index, types) \
  TTEST_PP_COMMA_IF__(index) TINYMOCk_VALUE(TTEST_PP_CAT__(a, index))

#define TINYMOCk_PARAMS__(...) \
  TTEST_PP_REPEAT__(TTEST_PP_NARG__(__VA_ARGS__), TINYMOCk_PARAM_ROW__, \
                    (__VA_ARGS__, ~))
#define TINYMOCk_EXPECT_PARAMS__(...) \
  TTEST_PP_REPEAT__(TTEST_PP_NARG__(__VA_ARGS__), TINYMOCk_EXPECT_PARAM_ROW__, ~)
#define TINYMOCk_EXPECT_VALUES__(...) \
  TTEST_PP_REPEAT__(TTEST_PP_NARG__(__VA_ARGS__), TINYMOCk_EXPECT_VALUE_ROW__, ~)
#define TINYMOCk_ACTUAL_VALUES__(...) \
  TTEST_PP_REPEAT__(TTEST_PP_NARG__(__VA_ARGS__), TINYMOCk_ACTUAL_VALUE_ROW__, ~)

#define TINYMOCk_STATIC_STATE__(NAME) \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME); \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _reset)(void) { \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME)); \
  } \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _verify)(void) { \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME)); \
  }

#define TINYMOCk_MOCK_IMPL__(RET, NAME, count, ...) \
  TINYMOCk_STATIC_STATE__(NAME) \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) { \
    TINYMOCk_MOCK_FN(NAME, _reset)(); \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret); \
  } \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _expect)( \
      TINYMOCk_EXPECT_PARAMS__(__VA_ARGS__), tinymock_value_t ret) { \
    const tinymock_expected_arg_t expected_args[] = { \
      TINYMOCk_EXPECT_VALUES__(__VA_ARGS__) \
    }; \
    tinymock_mock_expect(&TINYMOCk_MOCK_DATA(NAME), (size_t)(count), expected_args, true, ret); \
  } \
  TINYMOCk_GENERATED__ RET NAME(TINYMOCk_PARAMS__(__VA_ARGS__)) { \
    tinymock_value_t actual_args[] = {TINYMOCk_ACTUAL_VALUES__(__VA_ARGS__)}; \
    tinymock_value_t result = \
        tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), (size_t)(count), actual_args); \
    return TINYMOCk_VALUE_AS(RET, result); \
  }

#define TINYMOCk_MOCK_VOID_IMPL__(NAME, count, ...) \
  TINYMOCk_STATIC_STATE__(NAME) \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _expect)( \
      TINYMOCk_EXPECT_PARAMS__(__VA_ARGS__)) { \
    const tinymock_expected_arg_t expected_args[] = { \
      TINYMOCk_EXPECT_VALUES__(__VA_ARGS__) \
    }; \
    tinymock_mock_expect(&TINYMOCk_MOCK_DATA(NAME), (size_t)(count), expected_args, false, \
                         tinymock_value_zero()); \
  } \
  TINYMOCk_GENERATED__ void NAME(TINYMOCk_PARAMS__(__VA_ARGS__)) { \
    tinymock_value_t actual_args[] = {TINYMOCk_ACTUAL_VALUES__(__VA_ARGS__)}; \
    (void)tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), (size_t)(count), actual_args); \
  }

#define TINYMOCk_MOCK_SELECT__(RET, NAME, count, ...) \
  TINYMOCk_MOCK_IMPL__(RET, NAME, count, __VA_ARGS__)
#define TINYMOCk_MOCK(RET, NAME, ...) \
  TINYMOCk_MOCK_SELECT__(RET, NAME, TTEST_PP_NARG__(__VA_ARGS__), __VA_ARGS__)
#define TINYMOCk_MOCK_VOID_SELECT__(NAME, count, ...) \
  TINYMOCk_MOCK_VOID_IMPL__(NAME, count, __VA_ARGS__)
#define TINYMOCk_MOCK_VOID(NAME, ...) \
  TINYMOCk_MOCK_VOID_SELECT__(NAME, TTEST_PP_NARG__(__VA_ARGS__), __VA_ARGS__)

#define TINYMOCk_MOCK0(RET, NAME) \
  TINYMOCk_STATIC_STATE__(NAME) \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) { \
    TINYMOCk_MOCK_FN(NAME, _reset)(); \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret); \
  } \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _expect)(tinymock_value_t ret) { \
    tinymock_mock_expect(&TINYMOCk_MOCK_DATA(NAME), 0, NULL, true, ret); \
  } \
  TINYMOCk_GENERATED__ RET NAME(void) { \
    tinymock_value_t result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 0, NULL); \
    return TINYMOCk_VALUE_AS(RET, result); \
  }

#define TINYMOCk_MOCK0_VOID(NAME) \
  TINYMOCk_STATIC_STATE__(NAME) \
  TINYMOCk_GENERATED__ void TINYMOCk_MOCK_FN(NAME, _expect)(void) { \
    tinymock_mock_expect(&TINYMOCk_MOCK_DATA(NAME), 0, NULL, false, tinymock_value_zero()); \
  } \
  TINYMOCk_GENERATED__ void NAME(void) { \
    (void)tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 0, NULL); \
  }

#define TINYMOCk_MOCK_DECLARE(NAME) extern tinymock_mock_t TINYMOCk_MOCK_DATA(NAME)

#define TINYMOCk_MOCK_DEFINE_IMPL__(RET, NAME, count, ...) \
  tinymock_mock_t TINYMOCk_MOCK_DATA(NAME); \
  void TINYMOCk_MOCK_FN(NAME, _reset)(void) { \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME)); \
  } \
  void TINYMOCk_MOCK_FN(NAME, _verify)(void) { \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME)); \
  } \
  void TINYMOCk_MOCK_FN(NAME, _expect)( \
      TINYMOCk_EXPECT_PARAMS__(__VA_ARGS__), tinymock_value_t ret) { \
    const tinymock_expected_arg_t expected_args[] = { \
      TINYMOCk_EXPECT_VALUES__(__VA_ARGS__) \
    }; \
    tinymock_mock_expect(&TINYMOCk_MOCK_DATA(NAME), (size_t)(count), expected_args, true, ret); \
  } \
  RET NAME(TINYMOCk_PARAMS__(__VA_ARGS__)) { \
    tinymock_value_t actual_args[] = {TINYMOCk_ACTUAL_VALUES__(__VA_ARGS__)}; \
    tinymock_value_t result = \
        tinymock_mock_dispatch(&TINYMOCk_MOCK_DATA(NAME), (size_t)(count), actual_args); \
    return TINYMOCk_VALUE_AS(RET, result); \
  }

#define TINYMOCk_MOCK_DEFINE_SELECT__(RET, NAME, count, ...) \
  TINYMOCk_MOCK_DEFINE_IMPL__(RET, NAME, count, __VA_ARGS__)
#define TINYMOCk_MOCK_DEFINE(RET, NAME, ...) \
  TINYMOCk_MOCK_DEFINE_SELECT__(RET, NAME, TTEST_PP_NARG__(__VA_ARGS__), __VA_ARGS__)

#endif /* !__cplusplus */
#endif /* TINYMOCK_H */
