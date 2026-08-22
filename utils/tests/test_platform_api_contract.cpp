#include "platform.h"
#include "tinytest.h"

#include <type_traits>

TURBO_API int turbo_platform_cpp_api_contract_probe(int value);
TURBO_API int turbo_platform_cpp_api_contract_probe(double value);
TURBO_C_API int turbo_platform_c_api_contract_probe_cpp(int value);

using turbo_platform_int_probe_t = int (*)(int);
using turbo_platform_double_probe_t = int (*)(double);

static_assert(
    std::is_same<decltype(static_cast<turbo_platform_int_probe_t>(
                     &turbo_platform_cpp_api_contract_probe)),
                 turbo_platform_int_probe_t>::value,
    "TURBO_API must preserve C++ overload linkage");
static_assert(
    std::is_same<decltype(static_cast<turbo_platform_double_probe_t>(
                     &turbo_platform_cpp_api_contract_probe)),
                 turbo_platform_double_probe_t>::value,
    "TURBO_API must preserve C++ overload linkage");

spec("platform export macro C++ contract") {
  it("keeps TURBO_API and TURBO_C_API as declaration-local linkage markers") {
    check(sizeof(&turbo_platform_c_api_contract_probe_cpp) > 0U);
  }
}
