#include "enum_utils.h"
#include "tinytest.h"

#define COLOR_ITEMS \
  (COLOR_RED, 0, "red"), \
  (COLOR_GREEN, 1, "green"), \
  (COLOR_BLUE, 2, "blue")

SALTS_ENUM_DECLARE(color_t, color, COLOR_ITEMS, "UNKNOWN")

#define STATUS_ITEMS \
  (STATUS_OK, 0, "ok"), \
  (STATUS_ERROR, 1, "error"), \
  (STATUS_DEGRADED, 2, "degraded")

SALTS_ENUM_DECLARE(status_t, status, STATUS_ITEMS, "UNKNOWN")

spec("Enum Utils Tests") {
  it("should expose CMeta enum metadata") {
    const cmeta_enum_desc *meta = color_t_meta();
    color_t value = COLOR_RED;

    check_not_null(meta);
    check_equal(meta->name, "color_t");
    check_equal(meta->count, (size_t)3);
    check_equal(color_t_to_string(COLOR_GREEN), "green");
    check_equal(color_t_to_symbol(COLOR_BLUE), "COLOR_BLUE");
    check_true(color_t_from_string("COLOR_GREEN", &value));
    check_equal(value, COLOR_GREEN);
  }

  it("should convert enum to string") {
    color_t c = COLOR_GREEN;
    check_equal(color_to_string(COLOR_RED), "red");
    check_equal(color_to_string(c), "green");
    check_equal(color_to_string(COLOR_BLUE), "blue");
    check_equal(color_to_string((color_t)99), "UNKNOWN");
    check_equal(color_count(), (size_t)3);
    check_equal(color_entries[1].name, "green");
    check_equal(color_is_valid(COLOR_RED), 1);
    check_equal(color_is_valid((color_t)99), 0);
    check_true(color_equals(COLOR_GREEN, c));
  }

  it("should convert string to enum") {
    color_t value = COLOR_RED;
    check_equal(color_from_string("green", &value), 0);
    check_equal(value, COLOR_GREEN);
    check_equal(color_from_string("COLOR_BLUE", &value), -1);
    check_equal(value, COLOR_GREEN);
    check_equal(color_from_string("not_exist", &value), -1);
    check_equal(value, COLOR_GREEN);
  }

  it("should dispatch to_string through _Generic helper") {
    status_t s = STATUS_ERROR;
    color_t c = COLOR_BLUE;

    check_equal(status_count(), (size_t)3);
    check_equal(status_entries[1].value, 1LL);
    check_equal(status_from_string("error", &s), 0);
    check_true(status_is_valid(s));
    check_true(status_equals(s, STATUS_ERROR));
    check_equal(SALTS_ENUM_DISPATCH_TO_STRING_OF(c, color_t, color_to_string), "blue");
    check_equal(SALTS_ENUM_DISPATCH_TO_STRING_OF(s, status_t, status_to_string), "error");
  }
}
