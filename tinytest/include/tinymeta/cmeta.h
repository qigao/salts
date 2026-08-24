#ifndef TINYTEST_CMETA_H
#define TINYTEST_CMETA_H

#if !defined(TINYTEST_H) || !defined(TINYTEST_TRAITS_H)
#error "cmeta.h is included by tinytest.h and is not a standalone header"
#endif

#ifdef __cplusplus
#error "cmeta.h requires strict C11 _Generic support"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define TTEST_CMETA_UNUSED__ __attribute__((unused))
#else
#define TTEST_CMETA_UNUSED__
#endif

#define TTEST_CMETA_INLINE__ static inline TTEST_CMETA_UNUSED__

typedef struct ttest_cmeta_trait_sentinel__ {
  unsigned char unused;
} ttest_cmeta_trait_sentinel__;

typedef struct ttest_cmeta_select_tail__ {
  unsigned char unused;
} ttest_cmeta_select_tail__;

TTEST_CMETA_INLINE__ bool ttest_cmeta_sentinel_equal__(
    const ttest_cmeta_trait_sentinel__ *actual,
    const ttest_cmeta_trait_sentinel__ *expected) {
  (void)actual;
  (void)expected;
  return false;
}

TTEST_CMETA_INLINE__ void ttest_cmeta_select_tail_eq__(
    int relation, ttest_cmeta_select_tail__ actual,
    ttest_cmeta_select_tail__ expected, bool warning,
    const char *file, const char *line) {
  (void)relation;
  (void)actual;
  (void)expected;
  if (warning) {
    TTEST_WARN_IMPL__(false, file, line,
                     "internal equality trait selection reached its tail");
  } else {
    TTEST_CHECK_IMPL__(false, file, line,
                      "internal equality trait selection reached its tail");
  }
}

#define TTEST_CMETA_USER_EQUAL_TRAIT_ROWS__ \
  (TTEST_CMETA_SENTINEL, ttest_cmeta_trait_sentinel__, \
   ttest_cmeta_sentinel_equal__) TTEST_USER_EQUAL_TRAIT_LIST

#define TTEST_CMETA_EQ_NAME__(row) \
  TTEST_PP_CAT__(ttest_cmeta_check_eq_, TTEST_EQUAL_TRAIT_TOKEN__(row))
#define TTEST_CMETA_DEFINE_HANDLERS__(row, ignored) \
  TTEST_CMETA_INLINE__ void TTEST_CMETA_EQ_NAME__(row)( \
      int relation, \
      TTEST_EQUAL_TRAIT_CTYPE__(row) actual, \
      TTEST_EQUAL_TRAIT_CTYPE__(row) expected, \
      bool warning, \
      const char *file, const char *line) { \
    const bool passed = relation == TTEST_EQ && \
        TTEST_EQUAL_TRAIT_COMPARATOR__(row)(&actual, &expected); \
    if (warning) { \
      TTEST_WARN_IMPL__(passed, file, line, \
                       "expected values of type %s to be equal", \
                       TTEST_TRAIT_STRINGIFY__(TTEST_EQUAL_TRAIT_CTYPE__(row))); \
    } else { \
      TTEST_CHECK_IMPL__(passed, file, line, \
                        "expected values of type %s to be equal", \
                        TTEST_TRAIT_STRINGIFY__(TTEST_EQUAL_TRAIT_CTYPE__(row))); \
    } \
  }

TTEST_PP_FOR_EACH__(TTEST_CMETA_DEFINE_HANDLERS__, ~,
                          TTEST_CMETA_USER_EQUAL_TRAIT_ROWS__)

#undef TTEST_CMETA_DEFINE_HANDLERS__

#define TTEST_CMETA_EQ_ASSOC__(row, ignored) \
  TTEST_EQUAL_TRAIT_CTYPE__(row): TTEST_CMETA_EQ_NAME__(row),

#undef check_equal
#define TTEST_CMETA_CHECK_EQUAL_VALUE__(actual, expected) \
  TTEST_C11_CHECK_EQ_SELECT__((actual), \
    TTEST_PP_FOR_EACH__(TTEST_CMETA_EQ_ASSOC__, ~, \
                              TTEST_CMETA_USER_EQUAL_TRAIT_ROWS__) \
    ttest_cmeta_select_tail__: ttest_cmeta_select_tail_eq__, \
    default: ttest_c11_check_signed__ \
  )(TTEST_EQ, (actual), (expected), false, __FILE__, __STRING__LINE__)

#undef check_equal_warn
#define TTEST_CMETA_CHECK_EQUAL_VALUE_WARN__(actual, expected) \
  TTEST_C11_CHECK_EQ_SELECT__((actual), \
    TTEST_PP_FOR_EACH__(TTEST_CMETA_EQ_ASSOC__, ~, \
                              TTEST_CMETA_USER_EQUAL_TRAIT_ROWS__) \
    ttest_cmeta_select_tail__: ttest_cmeta_select_tail_eq__, \
    default: ttest_c11_check_signed__ \
  )(TTEST_EQ, (actual), (expected), true, __FILE__, __STRING__LINE__)

#define check_equal(...) \
  TTEST_EQUAL_OVERLOAD__(__VA_ARGS__, TTEST_CHECK_MEMORY_EQUAL__, \
                         TTEST_CMETA_CHECK_EQUAL_VALUE__, TTEST_EQUAL_SENTINEL__)(__VA_ARGS__)
#define check_equal_warn(...) \
  TTEST_EQUAL_OVERLOAD__(__VA_ARGS__, TTEST_CHECK_MEMORY_EQUAL_WARN__, \
                         TTEST_CMETA_CHECK_EQUAL_VALUE_WARN__, TTEST_EQUAL_SENTINEL__)(__VA_ARGS__)

#endif /* TINYTEST_CMETA_H */
