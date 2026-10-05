#ifndef CFLOW_TEST_OPS_H
#define CFLOW_TEST_OPS_H

#include <cflow/meta.h>

typedef struct cflow_test_owned_value {
    int *resource;
} cflow_test_owned_value;

extern const cmeta_type_desc cflow_test_owned_value_type;

cmeta_function_decl(filter, cflow_test_even);
cmeta_function_decl(map, cflow_test_square);
cmeta_function_decl(map, cflow_test_half);

#endif
