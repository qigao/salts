#ifndef SALTS_FASTPATH_H
#define SALTS_FASTPATH_H

#include <salts/error_codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef SALTS_PLATFORM_NATIVE_FASTPATH
#define SALTS_PLATFORM_NATIVE_FASTPATH 0
#endif

typedef struct salts_fast_key_state salts_fast_key_state;

#ifdef __cplusplus
extern "C" {
#endif

bool salts_fast_key_read(const salts_fast_key_state *key);
int salts_fast_key_set(salts_fast_key_state *key, bool enabled);
bool salts_fast_key_consume(salts_fast_key_state *key);

#if SALTS_PLATFORM_NATIVE_FASTPATH
bool salts_fast_key_read_native(const salts_fast_key_state *key);
typedef void (*salts_fast_target_type)(void);
salts_fast_target_type salts_fast_target_load_native(const void *slot);
#endif

#ifdef __cplusplus
}
#else
#include <stdatomic.h>

struct salts_fast_key_state {
    atomic_bool enabled;
};

#define SALTS_FAST_KEY(name_, initial_) \
    salts_fast_key_state name_ = {(initial_)}

static inline bool salts_fast_branch(const salts_fast_key_state *key) {
    return atomic_load_explicit(&key->enabled, memory_order_acquire);
}

static inline int salts_fast_enable(salts_fast_key_state *key) {
    return salts_fast_key_set(key, true);
}

static inline int salts_fast_disable(salts_fast_key_state *key) {
    return salts_fast_key_set(key, false);
}
#endif

#ifdef __cplusplus
#define salts_fast_branch(key_) salts_fast_key_read(key_)
#endif

#endif
