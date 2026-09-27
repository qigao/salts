#include "plugin_test_interface.h"

const cmeta_interface_desc *plugin_test_interface_b(void) {
    return plugin_test_codec_interface();
}
