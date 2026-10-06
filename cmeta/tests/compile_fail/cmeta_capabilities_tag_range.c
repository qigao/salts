#include <cmeta/variant.h>
cmeta_variant(TooWideTag, "test.TooWideTag",
    cmeta_case(Number, UINT64_MAX, int, &cmeta_data_int)
);
int main(void) { return 0; }
