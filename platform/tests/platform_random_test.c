#include "tinytest.h"
#include <turbo/error_codes.h>
#include <turbo/random.h>

#include <string.h>

spec("Platform secure random") {
  it("uses the system source and validates buffer ownership") {
    unsigned char first[32] = {0};
    unsigned char second[32] = {0};
    check_equal(turbo_platform_secure_random(NULL, 0u), TURBO_OK);
    check_equal(turbo_platform_secure_random(NULL, 1u), TURBO_EINVAL);
    check_equal(turbo_platform_secure_random(first, sizeof(first)), TURBO_OK);
    check_equal(turbo_platform_secure_random(second, sizeof(second)), TURBO_OK);
    check_true(memcmp(first, second, sizeof(first)) != 0);
  }
}
