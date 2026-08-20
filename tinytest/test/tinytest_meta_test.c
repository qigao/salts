#include "tinytest.h"

static int meta_signed(long long value) { return (int)value; }
static int meta_unsigned(unsigned long long value) { return (int)value; }
static int meta_float(float value) { return (int)value; }
static int meta_double(double value) { return (int)value; }
static int meta_ldouble(long double value) { return (int)value; }
static int meta_bool(_Bool value) { return value ? 1 : 0; }

#define META_ROUTE(value) \
  _Generic((value), \
    TTEST_META_ASSOC_SIGNED_INTS(meta_signed), \
    TTEST_META_ASSOC_UNSIGNED_INTS(meta_unsigned), \
    TTEST_META_ASSOC_REAL(meta_float, meta_double, meta_ldouble), \
    TTEST_META_ASSOC_BOOL(meta_bool) \
  )(value)

suite("TinyTest meta routing") {
  it("routes scalar families without duplicating assertion behavior") {
    check_int_eq(META_ROUTE((short)3), 3);
    check_int_eq(META_ROUTE((unsigned long)4), 4);
    check_int_eq(META_ROUTE(5.0f), 5);
    check_int_eq(META_ROUTE(6.0), 6);
    check_int_eq(META_ROUTE((_Bool)1), 1);
  }
}
