#ifndef SALTS_PLUGIN_LIFECYCLE_TEST_INTERFACE_H
#define SALTS_PLUGIN_LIFECYCLE_TEST_INTERFACE_H

#include <cmeta/interface.h>

#include <stdbool.h>
#include <stdint.h>

#define PLUGIN_LIFECYCLE_TEST_METHODS(X, I) \
    X(I, R1, bool, send, int, value) \
    X(I, R0, uint64_t, accepted, _)

CMETA_INTERFACE(plugin_lifecycle_test_api, PLUGIN_LIFECYCLE_TEST_METHODS);

#endif /* SALTS_PLUGIN_LIFECYCLE_TEST_INTERFACE_H */
