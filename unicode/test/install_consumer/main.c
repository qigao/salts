#include <salts_unicode.h>
#include <string.h>

int main(void) {
  const char input_bytes[] = "\xf0\x9f\x99\x82";
  const vstr input = vstr_from_buf(input_bytes, sizeof(input_bytes) - 1u);
  salts_unicode_scalar scalar = {0};
  size_t cursor = 0u;
  if (strcmp(salts_unicode_version(), "17.0.0") != 0) return 1;
  if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK) return 2;
  if (scalar.value != 0x1f642u || cursor != sizeof(input_bytes) - 1u) return 3;
  if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_END) return 4;
  return 0;
}
