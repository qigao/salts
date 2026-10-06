#include <cmeta/compiler.h>
enum { ALLOWED = 1u, UNDECLARED = 2u };
static const unsigned invalid_flags = CMETA_FLAGS_REQUIRE(UNDECLARED,ALLOWED);
