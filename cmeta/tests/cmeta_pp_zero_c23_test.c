#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#include "cmeta_pp_zero_cases.h"
#else
#include "tinytest.h"
suite("CMeta finalized C23 qualification") {
    it("requires the finalized C23 language version") {
        puts("Compiler only advertises a C23 draft; finalized C23 qualification requires 202311L.");
        exit(77);
    }
}
#endif
