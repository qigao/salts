#include "tinytest.h"
#include <math.h>

/* Regression coverage for the tinytest review fixes:
 * - names with '%' are literals, not printf formats (MED-1)
 * - 64-bit integer assertions do not truncate (MED-2)
 * - float/double array equality treats NaN like the scalar checks (LOW-2)
 * - check_warn does not count toward assertion totals (LOW-4)
 */
suite("review fixes") {
  it("keeps a literal % sign in the name") {
    check_true(1);
  }

  it("compares 64-bit signed values") {
    check_ll_eq(0x100000000LL, 0x100000000LL);
    check_ll_ne(0x100000000LL, 0LL);
    check_ll_gt(0x100000001LL, 0x100000000LL);
    check_ll_ge(0x100000000LL, 0x100000000LL);
    check_ll_lt(-0x100000001LL, -0x100000000LL);
    check_ll_le(-0x100000000LL, -0x100000000LL);
  }

  it("compares 64-bit unsigned values") {
    check_ull_eq(0x100000000ULL, 0x100000000ULL);
    check_ull_ne(0x100000000ULL, 0ULL);
  }

  it("matches bit masks above 32 bits") {
    check_bits(0x100000000ULL, 0x100000000ULL);
  }

  it("treats NaN in arrays as a mismatch like the scalar checks") {
    const float a[2] = {1.0f, (float)NAN};
    const float b[2] = {1.0f, (float)NAN};
    size_t idx = 0;
    check_false(__bdd_float_array_eq__(a, b, 2, 0.0001, &idx));
    check_size_eq(idx, 1);
  }

  it("does not count warnings as assertions") {
    size_t before = __bdd_active_config__->assertion_count;
    size_t warn_before = __bdd_active_config__->warn_count;
    check_warn(1 == 2, "intentional warning");
    check_size_eq(__bdd_active_config__->assertion_count, before);
    check_size_eq(__bdd_active_config__->warn_count, warn_before + 1);
  }
}
