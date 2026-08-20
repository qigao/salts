#ifndef TINYTEST_PUBLIC_H
#define TINYTEST_PUBLIC_H

#include "tinytest_meta.h"

/* Parse platform headers while compiler feature probes are still intact. */
#ifdef __cplusplus
  #include <exception>
#endif
#ifdef _WIN32
  #include <stdio.h>
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <Windows.h>
  #include <direct.h>
  #include <io.h>
  #include <sys/stat.h>
#else
  #ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 200809L
  #endif
  #include <dirent.h>
  #include <stdio.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef __cplusplus
  #include <stdbool.h>
#endif

/*
 * The legacy implementation is intentionally preserved byte-for-byte in
 * tinytest_impl.h.  GCC/Clang diagnostic pragmas in that implementation were
 * placed after a do/while assertion statement without a terminating semicolon.
 * Hide compiler probes while parsing the implementation so those diagnostic
 * helpers select their empty fallback; restore the probes immediately after.
 * Public assertion macros are then rebound to correct diagnostic helpers.
 */
#if !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
  #define TTEST_PUBLIC_RESTORE_COMPILER_PROBES__ 1
  #if defined(__clang__)
    #define TTEST_PUBLIC_RESTORE_CLANG__ 1
    #pragma push_macro("__clang__")
    #undef __clang__
  #endif
  #if defined(__GNUC__)
    #define TTEST_PUBLIC_RESTORE_GNUC__ 1
    #pragma push_macro("__GNUC__")
    #undef __GNUC__
  #endif
#endif

#include "tinytest_impl.h"

#if defined(TTEST_PUBLIC_RESTORE_GNUC__)
  #pragma pop_macro("__GNUC__")
  #undef TTEST_PUBLIC_RESTORE_GNUC__
#endif
#if defined(TTEST_PUBLIC_RESTORE_CLANG__)
  #pragma pop_macro("__clang__")
  #undef TTEST_PUBLIC_RESTORE_CLANG__
#endif

#if defined(TTEST_PUBLIC_RESTORE_COMPILER_PROBES__)
  #undef TTEST_PUBLIC_RESTORE_COMPILER_PROBES__

  #undef TTEST_DIAG_PUSH__
  #undef TTEST_DIAG_POP__
  #undef TTEST_DIAG_IGNORE_SHADOW__
  #undef TTEST_DIAG_IGNORE_UNUSED_VALUE__

  #if defined(__clang__)
    #define TTEST_DIAG_PUSH__                _Pragma("clang diagnostic push")
    #define TTEST_DIAG_POP__                 ; _Pragma("clang diagnostic pop")
    #define TTEST_DIAG_IGNORE_SHADOW__       _Pragma("clang diagnostic ignored \"-Wshadow\"")
    #define TTEST_DIAG_IGNORE_UNUSED_VALUE__ _Pragma("clang diagnostic ignored \"-Wunused-value\"")
  #elif defined(__GNUC__)
    #define TTEST_DIAG_PUSH__                _Pragma("GCC diagnostic push")
    #define TTEST_DIAG_POP__                 ; _Pragma("GCC diagnostic pop")
    #define TTEST_DIAG_IGNORE_SHADOW__       _Pragma("GCC diagnostic ignored \"-Wshadow\"")
    #define TTEST_DIAG_IGNORE_UNUSED_VALUE__ _Pragma("GCC diagnostic ignored \"-Wunused-value\"")
  #endif

  /* spec() expands this helper after the public header has been included. */
  #undef TTEST_CONSTRUCTOR__
  #if defined(__cplusplus)
    #define TTEST_CONSTRUCTOR__(fn)                                                                  \
      static void fn(void);                                                                          \
      namespace {                                                                                    \
        struct TTEST_CAT2(ttest_ctor_struct_, fn) {                                                  \
          TTEST_CAT2(ttest_ctor_struct_, fn)() { fn(); }                                             \
        };                                                                                           \
        static TTEST_CAT2(ttest_ctor_struct_, fn) TTEST_CAT2(ttest_ctor_obj_, fn);                   \
      }                                                                                              \
      static void fn(void)
  #elif defined(__GNUC__) || defined(__clang__)
    #define TTEST_CONSTRUCTOR__(fn)                                                                  \
      static void fn(void) __attribute__((constructor));                                             \
      static void fn(void)
  #else
    #define TTEST_CONSTRUCTOR__(fn) static void fn(void)
  #endif

  #undef TTEST_UNUSED_PARAM__
  #if defined(__GNUC__) || defined(__clang__)
    #define TTEST_UNUSED_PARAM__ __attribute__((unused))
  #else
    #define TTEST_UNUSED_PARAM__
  #endif
#endif

/* Rebind C11 generic assertions through the shared meta routing vocabulary. */
#if defined(TTEST_HAS_C11_GENERIC__) && defined(TTEST_META_HAS_C11_GENERIC)

  #undef check_equal
  #define check_equal(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_eq_int), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_eq_uint), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_eq_bool), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_eq_float, ttest_c11_check_eq_double, ttest_c11_check_eq_long_double), \
      TTEST_META_ASSOC_CSTR(ttest_c11_check_eq_str), \
      TTEST_META_ASSOC_VOID_PTR(ttest_c11_check_eq_ptr), \
      default: ttest_c11_check_eq_int \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_equal_warn
  #define check_equal_warn(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_eq_int_warn), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_eq_uint_warn), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_eq_bool_warn), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_eq_float_warn, ttest_c11_check_eq_double_warn, ttest_c11_check_eq_long_double_warn), \
      TTEST_META_ASSOC_CSTR(ttest_c11_check_eq_str_warn), \
      TTEST_META_ASSOC_VOID_PTR(ttest_c11_check_eq_ptr_warn), \
      default: ttest_c11_check_eq_int_warn \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_not_equal
  #define check_not_equal(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_ne_int), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_ne_uint), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_ne_bool), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_ne_float, ttest_c11_check_ne_double, ttest_c11_check_ne_long_double), \
      TTEST_META_ASSOC_CSTR(ttest_c11_check_ne_str), \
      TTEST_META_ASSOC_VOID_PTR(ttest_c11_check_ne_ptr), \
      default: ttest_c11_check_ne_int \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_not_equal_warn
  #define check_not_equal_warn(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_ne_int_warn), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_ne_uint_warn), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_ne_bool_warn), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_ne_float_warn, ttest_c11_check_ne_double_warn, ttest_c11_check_ne_long_double_warn), \
      TTEST_META_ASSOC_CSTR(ttest_c11_check_ne_str_warn), \
      TTEST_META_ASSOC_VOID_PTR(ttest_c11_check_ne_ptr_warn), \
      default: ttest_c11_check_ne_int_warn \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_greater
  #define check_greater(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_gt_int), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_gt_uint), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_gt_int), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_gt_double, ttest_c11_check_gt_double, ttest_c11_check_gt_long_double), \
      default: ttest_c11_check_gt_int \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_greater_warn
  #define check_greater_warn(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_gt_int_warn), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_gt_uint_warn), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_gt_int_warn), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_gt_double_warn, ttest_c11_check_gt_double_warn, ttest_c11_check_gt_long_double_warn), \
      default: ttest_c11_check_gt_int_warn \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_less
  #define check_less(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_lt_int), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_lt_uint), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_lt_int), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_lt_double, ttest_c11_check_lt_double, ttest_c11_check_lt_long_double), \
      default: ttest_c11_check_lt_int \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_less_warn
  #define check_less_warn(actual, expected) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_lt_int_warn), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_lt_uint_warn), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_lt_int_warn), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_lt_double_warn, ttest_c11_check_lt_double_warn, ttest_c11_check_lt_long_double_warn), \
      default: ttest_c11_check_lt_int_warn \
    )((actual), (expected), __FILE__, __STRING__LINE__)

  #undef check_in_range
  #define check_in_range(actual, min_value, max_value) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_in_range_int), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_in_range_uint), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_in_range_int), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_in_range_double, ttest_c11_check_in_range_double, ttest_c11_check_in_range_long_double), \
      default: ttest_c11_check_in_range_int \
    )((actual), (min_value), (max_value), __FILE__, __STRING__LINE__)

  #undef check_in_range_warn
  #define check_in_range_warn(actual, min_value, max_value) \
    _Generic((actual), \
      TTEST_META_ASSOC_SIGNED_INTS(ttest_c11_check_in_range_int_warn), \
      TTEST_META_ASSOC_UNSIGNED_INTS(ttest_c11_check_in_range_uint_warn), \
      TTEST_META_ASSOC_BOOL(ttest_c11_check_in_range_int_warn), \
      TTEST_META_ASSOC_REAL(ttest_c11_check_in_range_double_warn, ttest_c11_check_in_range_double_warn, ttest_c11_check_in_range_long_double_warn), \
      default: ttest_c11_check_in_range_int_warn \
    )((actual), (min_value), (max_value), __FILE__, __STRING__LINE__)

  #undef check_between
  #define check_between(actual, min_value, max_value) check_in_range((actual), (min_value), (max_value))
  #undef check_between_warn
  #define check_between_warn(actual, min_value, max_value) check_in_range_warn((actual), (min_value), (max_value))
#endif

#endif /* TINYTEST_PUBLIC_H */
