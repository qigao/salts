#include <cmeta/compiler.h>
int require_runtime(int condition) { return CMETA_CONST_REQUIRE(condition); }
