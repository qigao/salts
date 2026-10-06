#ifndef SALTS_FASTPATH_H
#define SALTS_FASTPATH_H

#include <salts/error_codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef SALTS_PLATFORM_NATIVE_FASTPATH
#define SALTS_PLATFORM_NATIVE_FASTPATH 0
#endif

typedef struct cmeta_fast_key_state cmeta_fast_key_state;

#ifdef __cplusplus
extern "C" {
#endif

bool cmeta_fast_key_read(const cmeta_fast_key_state *key);
int cmeta_fast_key_set(cmeta_fast_key_state *key, bool enabled);
bool cmeta_fast_key_consume(cmeta_fast_key_state *key);
int cmeta_fast_enable(cmeta_fast_key_state *key);
int cmeta_fast_disable(cmeta_fast_key_state *key);

#if SALTS_PLATFORM_NATIVE_FASTPATH
bool cmeta_fast_key_read_native(const cmeta_fast_key_state *key);
typedef void (*cmeta_fast_target_type)(void);
cmeta_fast_target_type cmeta_fast_target_load_native(const void *slot);
#endif

#ifdef __cplusplus
}
#else
#include <stdatomic.h>

struct cmeta_fast_key_state {
    atomic_bool enabled;
};

#define SALTS_FAST_KEY(name_, initial_) \
    cmeta_fast_key_state name_ = {(initial_)}

static inline bool cmeta_fast_branch(const cmeta_fast_key_state *key) {
    return atomic_load_explicit(&key->enabled, memory_order_acquire);
}

#endif

#ifdef __cplusplus
#define cmeta_fast_branch(key_) cmeta_fast_key_read(key_)
#endif

#endif
