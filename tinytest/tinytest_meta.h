#ifndef TINYTEST_META_H
#define TINYTEST_META_H

/*
 * TinyTest/TinyMock generic routing primitives.
 *
 * This deliberately mirrors CMeta's split between a small routing layer and
 * feature-specific adapters: this header knows C scalar families, but it does
 * not know assertion or mocking semantics.
 */

#if !defined(__cplusplus)
  #if defined(_MSC_VER)
    #define TTEST_META_HAS_C11_GENERIC 1
  #elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define TTEST_META_HAS_C11_GENERIC 1
  #endif
#endif

#define TTEST_META_ASSOC_SMALL_SIGNED(fn) \
  char: fn, signed char: fn, short: fn, int: fn

#define TTEST_META_ASSOC_SMALL_UNSIGNED(fn) \
  unsigned char: fn, unsigned short: fn, unsigned int: fn

#define TTEST_META_ASSOC_LONGS(long_fn, ulong_fn, llong_fn, ullong_fn) \
  long: long_fn, unsigned long: ulong_fn, long long: llong_fn, unsigned long long: ullong_fn

#define TTEST_META_ASSOC_SIGNED_INTS(fn) \
  char: fn, signed char: fn, short: fn, int: fn, long: fn, long long: fn

#define TTEST_META_ASSOC_UNSIGNED_INTS(fn) \
  unsigned char: fn, unsigned short: fn, unsigned int: fn, unsigned long: fn, unsigned long long: fn

#define TTEST_META_ASSOC_REAL(float_fn, double_fn, ldouble_fn) \
  float: float_fn, double: double_fn, long double: ldouble_fn

#define TTEST_META_ASSOC_BOOL(fn) _Bool: fn
#define TTEST_META_ASSOC_CSTR(fn) char *: fn, const char *: fn
#define TTEST_META_ASSOC_VOID_PTR(fn) void *: fn, const void *: fn

#endif /* TINYTEST_META_H */
