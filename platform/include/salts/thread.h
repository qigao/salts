#ifndef SALTS_THREAD_PRIMITIVES_H
#define SALTS_THREAD_PRIMITIVES_H

#include <salts/platform.h>
#include <salts/error_codes.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef SALTS_THREAD_LOCAL
  #if defined(__cplusplus)
    #define SALTS_THREAD_LOCAL thread_local
  #elif defined(_MSC_VER)
    #define SALTS_THREAD_LOCAL __declspec(thread)
  #elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && \
      !defined(__STDC_NO_THREADS__)
    #define SALTS_THREAD_LOCAL _Thread_local
  #elif defined(__GNUC__) || defined(__clang__)
    #define SALTS_THREAD_LOCAL __thread
  #else
    #error "SALTS_THREAD_LOCAL is not supported by this compiler"
  #endif
#endif

typedef void *salts_mutex_t;
typedef void *salts_cond_t;
typedef void *salts_thread_t;
typedef void *salts_rwlock_t;

typedef struct salts_once_s {
  volatile int state;
} salts_once_t;
#define SALTS_ONCE_INIT {0}

typedef void (*salts_thread_cb)(void *arg);

SALTS_PLATFORM_C_API const void *salts_thread_current_token(void);

/*
 * Address-stable thread-affinity state owned by Salts::Platform.
 * This carries no value lifecycle callbacks and owns no payload.
 */
typedef struct salts_thread_affine_state {
  const void *owner;
  const void *thread;
  bool busy;
} salts_thread_affine_state;

static inline int salts_thread_affine_init(
    salts_thread_affine_state *state, const void *owner) {
  if (state == NULL || owner == NULL || state->owner != NULL)
    return SALTS_EINVAL;
  state->owner = owner;
  state->thread = salts_thread_current_token();
  state->busy = false;
  return SALTS_OK;
}

static inline int salts_thread_affine_check(
    const salts_thread_affine_state *state, const void *owner) {
  if (state == NULL || owner == NULL || state->owner != owner ||
      state->thread != salts_thread_current_token())
    return SALTS_EINVAL;
  return state->busy ? SALTS_EBUSY : SALTS_OK;
}

static inline int salts_thread_affine_set_busy(
    salts_thread_affine_state *state, const void *owner, bool busy) {
  int status = salts_thread_affine_check(state, owner);
  if (status != SALTS_OK)
    return status;
  state->busy = busy;
  return SALTS_OK;
}

static inline int salts_thread_affine_reset(
    salts_thread_affine_state *state, const void *owner) {
  int status = salts_thread_affine_check(state, owner);
  if (status != SALTS_OK)
    return status;
  state->owner = NULL;
  state->thread = NULL;
  state->busy = false;
  return SALTS_OK;
}

SALTS_PLATFORM_C_API void salts_mutex_init(salts_mutex_t *mutex);
SALTS_PLATFORM_C_API void salts_mutex_destroy(salts_mutex_t *mutex);
SALTS_PLATFORM_C_API void salts_mutex_lock(salts_mutex_t *mutex);
SALTS_PLATFORM_C_API void salts_mutex_unlock(salts_mutex_t *mutex);

SALTS_PLATFORM_C_API int salts_rwlock_init(salts_rwlock_t *lock);
SALTS_PLATFORM_C_API void salts_rwlock_destroy(salts_rwlock_t *lock);
SALTS_PLATFORM_C_API void salts_rwlock_rdlock(salts_rwlock_t *lock);
SALTS_PLATFORM_C_API void salts_rwlock_rdunlock(salts_rwlock_t *lock);
SALTS_PLATFORM_C_API void salts_rwlock_wrlock(salts_rwlock_t *lock);
SALTS_PLATFORM_C_API void salts_rwlock_wrunlock(salts_rwlock_t *lock);

SALTS_PLATFORM_C_API void salts_cond_init(salts_cond_t *cond);
SALTS_PLATFORM_C_API void salts_cond_destroy(salts_cond_t *cond);
SALTS_PLATFORM_C_API void salts_cond_signal(salts_cond_t *cond);
SALTS_PLATFORM_C_API void salts_cond_broadcast(salts_cond_t *cond);
SALTS_PLATFORM_C_API void salts_cond_wait(salts_cond_t *cond, salts_mutex_t *mutex);
SALTS_PLATFORM_C_API int salts_cond_timedwait(salts_cond_t *cond,
                                              salts_mutex_t *mutex,
                                              uint64_t timeout_ns);

SALTS_PLATFORM_C_API int salts_thread_create(salts_thread_t *thread,
                                             salts_thread_cb entry,
                                             void *arg);
SALTS_PLATFORM_C_API int salts_thread_join(salts_thread_t *thread);
SALTS_PLATFORM_C_API void salts_thread_destroy(salts_thread_t *thread);
SALTS_PLATFORM_C_API void salts_once(salts_once_t *guard, void (*callback)(void));
SALTS_PLATFORM_C_API void salts_sleep_ms(uint32_t ms);
SALTS_PLATFORM_C_API void salts_thread_yield(void);
/* Identity of the current live OS thread. Compare only while both threads
 * remain alive; token addresses may be reused after thread exit. */
SALTS_PLATFORM_C_API const void *salts_thread_current_token(void);
SALTS_PLATFORM_C_API int salts_cpu_count(void);

#endif /* SALTS_THREAD_PRIMITIVES_H */
