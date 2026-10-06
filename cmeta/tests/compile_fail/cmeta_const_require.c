#include <cmeta/compiler.h>
int invalid[1 + CMETA_CONST_REQUIRE(0)];
