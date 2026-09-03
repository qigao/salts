#ifndef SALTS_THREAD_PRIMITIVES_H
#define SALTS_THREAD_PRIMITIVES_H

#include <salts/platform.h>
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
SALTS_PLATFORM_C_API int salts_cpu_count(void);

#endif /* SALTS_THREAD_PRIMITIVES_H */
