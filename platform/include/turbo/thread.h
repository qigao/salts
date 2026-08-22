#ifndef TURBO_THREAD_PRIMITIVES_H
#define TURBO_THREAD_PRIMITIVES_H

#include <turbo/platform.h>
#include <stdint.h>

#ifndef TURBO_THREAD_LOCAL
  #if defined(__cplusplus)
    #define TURBO_THREAD_LOCAL thread_local
  #elif defined(_MSC_VER)
    #define TURBO_THREAD_LOCAL __declspec(thread)
  #elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && \
      !defined(__STDC_NO_THREADS__)
    #define TURBO_THREAD_LOCAL _Thread_local
  #elif defined(__GNUC__) || defined(__clang__)
    #define TURBO_THREAD_LOCAL __thread
  #else
    #error "TURBO_THREAD_LOCAL is not supported by this compiler"
  #endif
#endif

typedef void *turbo_mutex_t;
typedef void *turbo_cond_t;
typedef void *turbo_thread_t;
typedef void *turbo_rwlock_t;

typedef struct turbo_once_s {
  volatile int state;
} turbo_once_t;
#define TURBO_ONCE_INIT {0}

typedef void (*turbo_thread_cb)(void *arg);

TURBO_PLATFORM_C_API void turbo_mutex_init(turbo_mutex_t *mutex);
TURBO_PLATFORM_C_API void turbo_mutex_destroy(turbo_mutex_t *mutex);
TURBO_PLATFORM_C_API void turbo_mutex_lock(turbo_mutex_t *mutex);
TURBO_PLATFORM_C_API void turbo_mutex_unlock(turbo_mutex_t *mutex);

TURBO_PLATFORM_C_API int turbo_rwlock_init(turbo_rwlock_t *lock);
TURBO_PLATFORM_C_API void turbo_rwlock_destroy(turbo_rwlock_t *lock);
TURBO_PLATFORM_C_API void turbo_rwlock_rdlock(turbo_rwlock_t *lock);
TURBO_PLATFORM_C_API void turbo_rwlock_rdunlock(turbo_rwlock_t *lock);
TURBO_PLATFORM_C_API void turbo_rwlock_wrlock(turbo_rwlock_t *lock);
TURBO_PLATFORM_C_API void turbo_rwlock_wrunlock(turbo_rwlock_t *lock);

TURBO_PLATFORM_C_API void turbo_cond_init(turbo_cond_t *cond);
TURBO_PLATFORM_C_API void turbo_cond_destroy(turbo_cond_t *cond);
TURBO_PLATFORM_C_API void turbo_cond_signal(turbo_cond_t *cond);
TURBO_PLATFORM_C_API void turbo_cond_broadcast(turbo_cond_t *cond);
TURBO_PLATFORM_C_API void turbo_cond_wait(turbo_cond_t *cond, turbo_mutex_t *mutex);
TURBO_PLATFORM_C_API int turbo_cond_timedwait(turbo_cond_t *cond,
                                              turbo_mutex_t *mutex,
                                              uint64_t timeout_ns);

TURBO_PLATFORM_C_API int turbo_thread_create(turbo_thread_t *thread,
                                             turbo_thread_cb entry,
                                             void *arg);
TURBO_PLATFORM_C_API int turbo_thread_join(turbo_thread_t *thread);
TURBO_PLATFORM_C_API void turbo_thread_destroy(turbo_thread_t *thread);
TURBO_PLATFORM_C_API void turbo_once(turbo_once_t *guard, void (*callback)(void));
TURBO_PLATFORM_C_API void turbo_sleep_ms(uint32_t ms);
TURBO_PLATFORM_C_API void turbo_thread_yield(void);
TURBO_PLATFORM_C_API int turbo_cpu_count(void);

#endif /* TURBO_THREAD_PRIMITIVES_H */
