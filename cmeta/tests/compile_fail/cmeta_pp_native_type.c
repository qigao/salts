#include <cmeta/compiler.h>

#if CMETA_HAS_SAME_TYPE
static int mutable_value;
static const int immutable_value = 0;
CMETA_STATIC_ASSERT(CMETA_SAME_TYPE(mutable_value,immutable_value),
    "native type mismatch must reject qualifier loss");
#else
#error "CMETA_SAME_TYPE unavailable on this compiler"
#endif

int main(void) { return 0; }
