#include <cmeta/fastpath.h>

bool cmeta_static_key_read(const cmeta_static_key_state *key) {
    return cmeta_static_branch(key);
}

cmeta_status cmeta_static_key_set(cmeta_static_key_state *key, bool enabled) {
    if (key == NULL)
        return CMETA_INVALID_ARGUMENT;
    atomic_store_explicit(&key->enabled, enabled, memory_order_release);
    return CMETA_OK;
}
