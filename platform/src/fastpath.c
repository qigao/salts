#include <salts/fastpath.h>

bool salts_fast_key_read(const salts_fast_key_state *key) {
    return salts_fast_branch(key);
}

int salts_fast_key_set(salts_fast_key_state *key, bool enabled) {
    if (key == NULL)
        return SALTS_EINVAL;
    atomic_store_explicit(&key->enabled, enabled, memory_order_release);
    return SALTS_OK;
}
