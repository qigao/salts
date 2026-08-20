#ifndef TINYMOCK_PUBLIC_H
#define TINYMOCK_PUBLIC_H

#include "tinytest_meta.h"
#include "tinymock_impl.h"

/* Share TinyTest's scalar-family router instead of maintaining another map. */
#if defined(__TINYMOCk_HAS_C11_GENERIC__) && defined(TTEST_META_HAS_C11_GENERIC)
  #undef TINYMOCk_VALUE
  #define TINYMOCk_VALUE(v) \
    _Generic((v), \
      TTEST_META_ASSOC_SMALL_SIGNED(tinymock_value_int), \
      TTEST_META_ASSOC_SMALL_UNSIGNED(tinymock_value_uint), \
      TTEST_META_ASSOC_LONGS(tinymock_value_long, tinymock_value_ulong, tinymock_value_ll, tinymock_value_ull), \
      TTEST_META_ASSOC_REAL(tinymock_value_float, tinymock_value_double, tinymock_value_ldouble), \
      TTEST_META_ASSOC_BOOL(tinymock_value_bool), \
      TTEST_META_ASSOC_CSTR(tinymock_value_cstr), \
      TTEST_META_ASSOC_VOID_PTR(tinymock_value_ptr) \
    )(v)
#endif

/* Matcher --------------------------------------------------------------- */
typedef bool (*tinymock_matcher_fn)(const void *actual, void *context);

typedef struct {
  tinymock_matcher_fn fn;
  void *context;
} tinymock_matcher_payload_t;

static inline bool tinymock_matcher_adapter(const void *expected, const void *actual) {
  const tinymock_matcher_payload_t *payload = (const tinymock_matcher_payload_t *)expected;
  return payload != NULL && payload->fn != NULL && payload->fn(actual, payload->context);
}

static inline void tinymock_matcher_dump(char *out, size_t out_size, const void *value) {
  (void)value;
  snprintf(out, out_size, "<matcher>");
}

static inline tinymock_expected_arg_t tinymock_arg_that(tinymock_matcher_fn fn, void *context) {
  tinymock_matcher_payload_t payload;
  tinymock_value_t value;
  payload.fn = fn;
  payload.context = context;
  TINYMOCk_ASSERT(sizeof(payload) <= TINYMOCk_MAX_VALUE_BYTES,
                  "tinymock: matcher payload size %zu exceeds inline storage %d",
                  sizeof(payload), TINYMOCk_MAX_VALUE_BYTES);
  tinymock_value_init_bytes(&value, &payload, sizeof(payload),
                            tinymock_matcher_adapter, tinymock_matcher_dump);
  return tinymock_expected_arg(value);
}

#define TINYMOCk_ARG_THAT(fn, context) tinymock_arg_that((fn), (context))
#define TINYMOCk_MATCH(fn, context) TINYMOCk_ARG_THAT((fn), (context))

/* Answer ---------------------------------------------------------------- */
typedef tinymock_value_t (*tinymock_answer_fn)(size_t argc,
                                               const void *const *actual_args,
                                               void *context);

typedef struct {
  tinymock_answer_fn fn;
  void *context;
} tinymock_answer_payload_t;

static inline bool tinymock_answer_marker(const void *expected, const void *actual) {
  (void)expected;
  (void)actual;
  return false;
}

static inline tinymock_value_t tinymock_answer(tinymock_answer_fn fn, void *context) {
  tinymock_answer_payload_t payload;
  tinymock_value_t value;
  payload.fn = fn;
  payload.context = context;
  TINYMOCk_ASSERT(sizeof(payload) <= TINYMOCk_MAX_VALUE_BYTES,
                  "tinymock: answer payload size %zu exceeds inline storage %d",
                  sizeof(payload), TINYMOCk_MAX_VALUE_BYTES);
  tinymock_value_init_bytes(&value, &payload, sizeof(payload), tinymock_answer_marker, NULL);
  return value;
}

#define TINYMOCk_ANSWER(fn, context) tinymock_answer((fn), (context))

static inline bool tinymock_is_answer(const tinymock_value_t *result) {
  return result != NULL && result->eq == tinymock_answer_marker;
}

static inline bool tinymock_answer_copy(const tinymock_value_t *answer,
                                        void *out,
                                        size_t out_size,
                                        size_t argc,
                                        const void *const *actual_args) {
  tinymock_answer_payload_t payload;
  tinymock_value_t dynamic_result;
  if (!tinymock_is_answer(answer)) {
    return false;
  }
  memset(&payload, 0, sizeof(payload));
  memcpy(&payload, answer->value, sizeof(payload));
  TINYMOCk_ASSERT(payload.fn != NULL, "tinymock: answer callback is NULL");
  if (payload.fn == NULL) {
    return false;
  }
  dynamic_result = payload.fn(argc, actual_args, payload.context);
  if (out_size == 0) {
    return true;
  }
  TINYMOCk_ASSERT(dynamic_result.size >= out_size,
                  "tinymock: answer returned %zu bytes, expected at least %zu",
                  dynamic_result.size, out_size);
  if (dynamic_result.size < out_size) {
    return false;
  }
  memcpy(out, dynamic_result.storage, out_size);
  return true;
}

/* Rebind generated mocks so dynamic answers are resolved at call time. */
#undef TINYMOCk_MOCK0
#define TINYMOCk_MOCK0(RET, NAME)                                                                  \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                        \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                        \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                       \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                                \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {            \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                               \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                               \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(tinymock_value_t ret) {                        \
    tinymock_mock_expect0(&TINYMOCk_MOCK_DATA(NAME), true, ret);                                    \
  }                                                                                                 \
  static inline RET NAME(void) {                                                                    \
    tinymock_value_t result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 0, NULL);             \
    if (tinymock_is_answer(&result)) {                                                              \
      RET out; memset(&out, 0, sizeof(out));                                                        \
      if (!tinymock_answer_copy(&result, &out, sizeof(out), 0, NULL)) return (RET)0;                \
      return out;                                                                                    \
    }                                                                                                \
    if (result.size == 0) return (RET)0;                                                            \
    { RET out; memset(&out, 0, sizeof(out)); memcpy(&out, result.value, sizeof(RET)); return out; } \
  }

#undef TINYMOCk_MOCK1
#define TINYMOCk_MOCK1(RET, NAME, T0)                                                              \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                        \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                        \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                       \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                                \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {            \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                               \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                               \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,              \
                                                      tinymock_value_t ret) {                        \
    tinymock_mock_expect1(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0), true, ret); \
  }                                                                                                 \
  static inline RET NAME(T0 a0) {                                                                   \
    const void *actual_args[] = {&a0};                                                              \
    tinymock_value_t result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 1, actual_args);      \
    if (tinymock_is_answer(&result)) {                                                              \
      RET out; memset(&out, 0, sizeof(out));                                                        \
      if (!tinymock_answer_copy(&result, &out, sizeof(out), 1, actual_args)) return (RET)0;         \
      return out;                                                                                    \
    }                                                                                                \
    if (result.size == 0) return (RET)0;                                                            \
    { RET out; memset(&out, 0, sizeof(out)); memcpy(&out, result.value, sizeof(RET)); return out; } \
  }

#undef TINYMOCk_MOCK2
#define TINYMOCk_MOCK2(RET, NAME, T0, T1)                                                          \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                        \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                        \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                       \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                                \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {            \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                               \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                               \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,              \
                                                      TINYMOCk_MOCK_EXPECT_ARG_T arg1,              \
                                                      tinymock_value_t ret) {                        \
    tinymock_mock_expect2(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0),          \
                          TINYMOCk_MOCK_EXPECT_ARG_PASS(arg1), true, ret);                           \
  }                                                                                                 \
  static inline RET NAME(T0 a0, T1 a1) {                                                            \
    const void *actual_args[] = {&a0, &a1};                                                         \
    tinymock_value_t result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 2, actual_args);      \
    if (tinymock_is_answer(&result)) {                                                              \
      RET out; memset(&out, 0, sizeof(out));                                                        \
      if (!tinymock_answer_copy(&result, &out, sizeof(out), 2, actual_args)) return (RET)0;         \
      return out;                                                                                    \
    }                                                                                                \
    if (result.size == 0) return (RET)0;                                                            \
    { RET out; memset(&out, 0, sizeof(out)); memcpy(&out, result.value, sizeof(RET)); return out; } \
  }

#undef TINYMOCk_MOCK3
#define TINYMOCk_MOCK3(RET, NAME, T0, T1, T2)                                                      \
  static tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                        \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                        \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _verify)(void) {                                       \
    tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME));                                                \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _set_default_return)(tinymock_value_t ret) {            \
    TINYMOCk_MOCK_FN(NAME, _reset)();                                                               \
    tinymock_mock_set_default_return(&TINYMOCk_MOCK_DATA(NAME), ret);                               \
  }                                                                                                 \
  static inline void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,              \
                                                      TINYMOCk_MOCK_EXPECT_ARG_T arg1,              \
                                                      TINYMOCk_MOCK_EXPECT_ARG_T arg2,              \
                                                      tinymock_value_t ret) {                        \
    tinymock_mock_expect3(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg0),          \
                          TINYMOCk_MOCK_EXPECT_ARG_PASS(arg1), TINYMOCk_MOCK_EXPECT_ARG_PASS(arg2), \
                          true, ret);                                                                \
  }                                                                                                 \
  static inline RET NAME(T0 a0, T1 a1, T2 a2) {                                                     \
    const void *actual_args[] = {&a0, &a1, &a2};                                                    \
    tinymock_value_t result = tinymock_mock_invoke(&TINYMOCk_MOCK_DATA(NAME), 3, actual_args);      \
    if (tinymock_is_answer(&result)) {                                                              \
      RET out; memset(&out, 0, sizeof(out));                                                        \
      if (!tinymock_answer_copy(&result, &out, sizeof(out), 3, actual_args)) return (RET)0;         \
      return out;                                                                                    \
    }                                                                                                \
    if (result.size == 0) return (RET)0;                                                            \
    { RET out; memset(&out, 0, sizeof(out)); memcpy(&out, result.value, sizeof(RET)); return out; } \
  }

/* Static-compiled definitions use the same answer resolver. */
#undef TINYMOCk_MOCK_DEFINE1
#define TINYMOCk_MOCK_DEFINE1(RET, NAME, T0)                                                        \
  tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                                         \
  void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                                       \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                        \
  }                                                                                                 \
  void TINYMOCk_MOCK_FN(NAME, _verify)(void) { tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME)); }   \
  void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0, tinymock_value_t ret) {     \
    tinymock_mock_expect1(&TINYMOCk_MOCK_DATA(NAME), arg0, true, ret);                              \
  }                                                                                                 \
  RET NAME(T0 a0) {                                                                                 \
    const void *actual_args[] = {&a0};                                                              \
    tinymock_value_t recorded_args[] = {TINYMOCk_VALUE(a0)};                                        \
    tinymock_value_t result = tinymock_mock_dispatch(&TINYMOCk_MOCK_DATA(NAME), 1, actual_args, recorded_args); \
    if (tinymock_is_answer(&result)) {                                                              \
      RET out; memset(&out, 0, sizeof(out));                                                        \
      if (!tinymock_answer_copy(&result, &out, sizeof(out), 1, actual_args)) return (RET)0;         \
      return out;                                                                                    \
    }                                                                                                \
    if (result.size == 0) return (RET)0;                                                            \
    { RET out; memset(&out, 0, sizeof(out)); memcpy(&out, result.value, sizeof(RET)); return out; } \
  }

#undef TINYMOCk_MOCK_DEFINE2
#define TINYMOCk_MOCK_DEFINE2(RET, NAME, T0, T1)                                                    \
  tinymock_mock_t TINYMOCk_MOCK_DATA(NAME);                                                         \
  void TINYMOCk_MOCK_FN(NAME, _reset)(void) {                                                       \
    tinymock_mock_init(&TINYMOCk_MOCK_DATA(NAME), TINYMOCk_STRINGIZE(NAME));                        \
  }                                                                                                 \
  void TINYMOCk_MOCK_FN(NAME, _verify)(void) { tinymock_mock_verify(&TINYMOCk_MOCK_DATA(NAME)); }   \
  void TINYMOCk_MOCK_FN(NAME, _expect)(TINYMOCk_MOCK_EXPECT_ARG_T arg0,                             \
                                       TINYMOCk_MOCK_EXPECT_ARG_T arg1, tinymock_value_t ret) {     \
    tinymock_mock_expect2(&TINYMOCk_MOCK_DATA(NAME), arg0, arg1, true, ret);                        \
  }                                                                                                 \
  RET NAME(T0 a0, T1 a1) {                                                                          \
    const void *actual_args[] = {&a0, &a1};                                                         \
    tinymock_value_t recorded_args[] = {TINYMOCk_VALUE(a0), TINYMOCk_VALUE(a1)};                    \
    tinymock_value_t result = tinymock_mock_dispatch(&TINYMOCk_MOCK_DATA(NAME), 2, actual_args, recorded_args); \
    if (tinymock_is_answer(&result)) {                                                              \
      RET out; memset(&out, 0, sizeof(out));                                                        \
      if (!tinymock_answer_copy(&result, &out, sizeof(out), 2, actual_args)) return (RET)0;         \
      return out;                                                                                    \
    }                                                                                                \
    if (result.size == 0) return (RET)0;                                                            \
    { RET out; memset(&out, 0, sizeof(out)); memcpy(&out, result.value, sizeof(RET)); return out; } \
  }

#endif /* TINYMOCK_PUBLIC_H */
