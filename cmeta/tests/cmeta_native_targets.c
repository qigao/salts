#include "cmeta_native_targets.h"
/* Separate translation unit prevents baseline inlining without compiler flags. */
int native_test_identity(int value) { return value; }
int native_test_increment(int value) { return value + 1; }
int native_test_context(void *context, int value) { return *(int *)context + value; }
