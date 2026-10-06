#include <cmeta/variant.h>
cmeta_variant(DuplicateTag, "test.DuplicateTag",
    cmeta_case(First, 1, int, &cmeta_data_int)
    cmeta_case(Second, 1, int, &cmeta_data_int)
);
int main(void) { return 0; }
