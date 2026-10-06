#include <cmeta/trace.h>

bool cmeta_fault_consume(cmeta_static_key_state *key) {
    return cmeta_fault_hit(key);
}
