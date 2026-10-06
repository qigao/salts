#include "cmeta_fastpath_benchmark_target.h"
/* Separate TU preserves the ordinary native call baseline without introducing
 * compiler-specific noinline syntax or benchmark-only production callbacks. */
uint64_t fastpath_benchmark_target(uint64_t value) { return value + 1u; }
