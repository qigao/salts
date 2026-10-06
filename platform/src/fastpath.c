#include <salts/fastpath.h>

bool salts_static_key_read(const salts_static_key_state *key) {
    return salts_static_branch(key);
}
int salts_static_key_set(salts_static_key_state *key, bool enabled) {
    if (key == NULL) return SALTS_EINVAL;
    atomic_store_explicit(&key->enabled, enabled, memory_order_release);
    return SALTS_OK;
}
bool salts_fault_consume(salts_static_key_state *key) {
    return salts_fault_hit(key);
}
