#ifndef SALTS_PLUGIN_TEST_INTERFACE_H
#define SALTS_PLUGIN_TEST_INTERFACE_H

#include <cmeta/interface.h>

#define PLUGIN_TEST_CODEC_METHODS(X, I) \
    X(I, R1, int, transform, int, value)

CMETA_INTERFACE(plugin_test_codec, PLUGIN_TEST_CODEC_METHODS);

const cmeta_interface_desc *plugin_test_interface_a(void);
const cmeta_interface_desc *plugin_test_interface_b(void);

#endif
