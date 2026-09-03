#include "tinytest.h"
#include <salts/error_codes.h>
#include <salts/random.h>

#include <string.h>

spec("Platform secure random") {
  it("uses the system source and validates buffer ownership") {
    unsigned char first[32] = {0};
    unsigned char second[32] = {0};
    check_equal(salts_platform_secure_random(NULL, 0u), SALTS_OK);
    check_equal(salts_platform_secure_random(NULL, 1u), SALTS_EINVAL);
    check_equal(salts_platform_secure_random(first, sizeof(first)), SALTS_OK);
    check_equal(salts_platform_secure_random(second, sizeof(second)), SALTS_OK);
    check_true(memcmp(first, second, sizeof(first)) != 0);
  }
}
