#include "tinytest.hpp"
#include "turbo_error.h"
#include "turbo_uuid.h"

static_assert(sizeof(TurboUtils::UUID) == TURBO_UUID_SIZE,
              "TurboUtils::UUID must preserve the C ABI size");

suite("Turbo UUID C++") {
  it("uses the installed C type through the TurboUtils namespace") {
    TurboUtils::UUID uuid{};
    char text[TURBO_UUID_STRING_SIZE]{};

    check_equal(turbo_uuid_v7_generate(&uuid), TURBO_OK);
    check_equal(turbo_uuid_format(&uuid, text, sizeof(text)), TURBO_OK);
    check_equal(uuid.bytes[6] & 0xf0U, 0x70U);
  }
}
