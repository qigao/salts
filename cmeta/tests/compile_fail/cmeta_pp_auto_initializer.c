#include <cmeta/compiler.h>

int main(void) {
#if CMETA_HAS_AUTO
    CMETA_AUTO(value,cmeta_auto_missing_initializer);
    return value;
#else
#error "CMETA_AUTO unavailable on this compiler"
#endif
}
