#include <cmeta/data_select.h>
#if defined(CMETA_SELECT_DUPLICATE)
typedef int NativeAlias;
#define INVALID_SCHEMA(M) Schema(M, (int, &cmeta_data_int), (NativeAlias, &cmeta_data_int))
#elif defined(CMETA_SELECT_DESCRIPTOR)
#define INVALID_SCHEMA(M) Schema(M, (int, &cmeta_type_int))
#else
#define INVALID_SCHEMA CMETA_BUILTIN_DATA_SCHEMA
#endif
int main(void) {
#if defined(CMETA_SELECT_VOLATILE)
    volatile int *pointer = 0;
#else
    int *pointer = 0;
#endif
    return cmeta_data_of_in(pointer, INVALID_SCHEMA) == 0;
}
