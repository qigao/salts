#include "platform.h"
#include "tinytest.h"

TURBO_C_API int turbo_platform_c_api_contract_probe(int value);

spec("platform export macro C contract") {
  it("keeps TURBO_C_API usable as a C declaration") {
    check(sizeof(&turbo_platform_c_api_contract_probe) > 0U);
  }
}
