#ifndef OPS_H
#define OPS_H

#include <cflow/meta.h>

cmeta_function_decl(filter, even);
cmeta_function_decl(map, square);
cmeta_function_decl(map, half);
cmeta_function_decl(flatMap, expand_long);
cmeta_function_decl(flatMap, fail_long);
cmeta_function_decl(reduce, add_long);
cmeta_function_decl(zip, merge_long_double);
cmeta_function_decl(map, as_double);
cmeta_function_decl(map, to_int);
cmeta_function_decl(map, times_ten);
cmeta_function_decl(map, plus_hundred);
cmeta_function_decl(transform, times_two_transform);
cmeta_function_decl(map, io_tagged);
cmeta_function_decl(map, may_fail_tagged);
cmeta_function_decl(map, clamp_nonnegative);
cmeta_function_decl(map, clamp_unproven);
cmeta_function_decl(map, unproven_square);

#endif
