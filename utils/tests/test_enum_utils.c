#include "enum_utils.h"
#include "tinytest.h"

#define COLOR_ITEMS(X) \
  X(COLOR_RED, 0, "red") \
  X(COLOR_GREEN, 1, "green") \
  X(COLOR_BLUE, 2, "blue")

TURBO_ENUM_DECLARE(color_t, color, COLOR_ITEMS, "UNKNOWN")

#define STATUS_ITEMS(X) \
  X(STATUS_OK, 0, "ok") \
  X(STATUS_ERROR, 1, "error") \
  X(STATUS_DEGRADED, 2, "degraded")

TURBO_ENUM_DECLARE(status_t, status, STATUS_ITEMS, "UNKNOWN")

spec("Enum Utils Tests") {
  it("should convert enum to string") {
    color_t c = COLOR_GREEN;
    check_equal(color_to_string(COLOR_RED), "red");
    check_equal(color_to_string(c), "green");
    check_equal(color_to_string(COLOR_BLUE), "blue");
    check_equal(color_to_string((color_t)99), "UNKNOWN");
    check_equal(color_is_valid(COLOR_RED), 1);
    check_equal(color_is_valid((color_t)99), 0);
  }

  it("should convert string to enum") {
    color_t value = COLOR_RED;
    check_equal(color_from_string("green", &value), 0);
    check_equal(value, COLOR_GREEN);
    check_equal(color_from_string("not_exist", &value), -1);
    check_equal(value, COLOR_GREEN);
  }

  it("should dispatch to_string through _Generic helper") {
    status_t s = STATUS_ERROR;
    color_t c = COLOR_BLUE;

    check_equal(TURBO_ENUM_DISPATCH_TO_STRING_OF(c, color_t, color_to_string), "blue");
    check_equal(TURBO_ENUM_DISPATCH_TO_STRING_OF(s, status_t, status_to_string), "error");
  }
}
