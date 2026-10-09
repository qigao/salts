#include <salts_unicode.h>
#include <cstdint>

static_assert(SALTS_UNICODE_VERSION_MAJOR == 17, "Expected Unicode 17 SDK");

int main() {
  std::uint32_t properties = 0;
  if (salts_unicode_scalar_properties(0x41u, &properties) != SALTS_UNICODE_OK)
    return 1;
  if ((properties & SALTS_UNICODE_PROPERTY_XID_START) == 0u)
    return 2;
  salts_unicode_grapheme_break kind = SALTS_UNICODE_GRAPHEME_OTHER;
  if (salts_unicode_grapheme_break_class(0x41u, &kind) != SALTS_UNICODE_OK)
    return 3;
  return 0;
}
