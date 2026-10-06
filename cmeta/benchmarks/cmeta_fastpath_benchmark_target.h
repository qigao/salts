#ifndef CMETA_FASTPATH_BENCHMARK_TARGET_H
#define CMETA_FASTPATH_BENCHMARK_TARGET_H
#include <cmeta/fastpath.h>
FunctionDeclAsAbi(value, uint64_t, &cmeta_type_uint64, CMETA_ABI_SCALAR,
    fastpath_benchmark_target,
    (uint64_t, value, CMETA_PARAM_IN, &cmeta_type_uint64, CMETA_ABI_SCALAR));
#endif
