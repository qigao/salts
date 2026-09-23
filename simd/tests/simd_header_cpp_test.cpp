#include <salts/simd.h>

#include <type_traits>

static_assert(sizeof(salts_v128) == 16u, "SIMD carrier must remain 128 bits");
static_assert(alignof(salts_v128) >= 16u, "SIMD carrier must remain 16-byte aligned");
static_assert(std::is_standard_layout<salts_v128>::value,
              "SIMD carrier must remain a standard-layout ABI type");

int main() {
    salts_v128 value{};
    salts_simd_i32x4_splat(&value, 7);
    return 0;
}
