#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#include "cmeta_pp_zero_cases.h"
#else
#include <stdio.h>
int main(void) {
    puts("Compiler only advertises a C23 draft; finalized C23 qualification requires 202311L.");
    return 77;
}
#endif
