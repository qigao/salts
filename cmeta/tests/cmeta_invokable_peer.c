#include <cmeta/data.h>

const cmeta_data_desc *cmeta_invokable_peer_int_data(void) {
    static cmeta_data_desc copy;
    static int initialized;
    if (!initialized) {
        copy = cmeta_data_int;
        initialized = 1;
    }
    return &copy;
}
