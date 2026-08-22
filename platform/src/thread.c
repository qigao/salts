#include <turbo/thread.h>

#include <errno.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <process.h>
  #include <windows.h>
#else
  #include <pthread.h>
  #include <sched.h>
  #include <time.h>
  #include <unistd.h>
#endif

struct turbo_thread_wrapper_ctx {
  turbo_thread_cb entry;
  void *arg;
};

#ifdef _WIN32

void turbo_mutex_init(turbo_mutex_t *mutex) {
  if (mutex == NULL) return;
  PSRWLOCK native = (PSRWLOCK)malloc(sizeof(SRWLOCK));
  if (native == NULL) return;
  InitializeSRWLock(native);
  *mutex = native;
}

void turbo_mutex_destroy(turbo_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  free(*mutex);
  *mutex = NULL;
}

void turbo_mutex_lock(turbo_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  AcquireSRWLockExclusive((PSRWLOCK)*mutex);
}

void turbo_mutex_unlock(turbo_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  ReleaseSRWLockExclusive((PSRWLOCK)*mutex);
}

int turbo_rwlock_init(turbo_rwlock_t *lock) {
  if (lock == NULL) return -EINVAL;
  PSRWLOCK native = (PSRWLOCK)malloc(sizeof(SRWLOCK));
  if (native == NULL) return -ENOMEM;
  InitializeSRWLock(native);
  *lock = native;
  return 0;
}

void turbo_rwlock_destroy(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  free(*lock);
  *lock = NULL;
}

void turbo_rwlock_rdlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  AcquireSRWLockShared((PSRWLOCK)*lock);
}

void turbo_rwlock_rdunlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  ReleaseSRWLockShared((PSRWLOCK)*lock);
}

void turbo_rwlock_wrlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  AcquireSRWLockExclusive((PSRWLOCK)*lock);
}

void turbo_rwlock_wrunlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  ReleaseSRWLockExclusive((PSRWLOCK)*lock);
}

void turbo_cond_init(turbo_cond_t *cond) {
  if (cond == NULL) return;
  PCONDITION_VARIABLE native =
      (PCONDITION_VARIABLE)malloc(sizeof(CONDITION_VARIABLE));
  if (native == NULL) return;
  InitializeConditionVariable(native);
  *cond = native;
}

void turbo_cond_destroy(turbo_cond_t *cond) {
  if (cond == NULL || *cond == NULL) return;
  free(*cond);
  *cond = NULL;
}

void turbo_cond_signal(turbo_cond_t *cond) {
  if (cond == NULL || *cond == NULL) return;
  WakeConditionVariable((PCONDITION_VARIABLE)*cond);
}

void turbo_cond_broadcast(turbo_cond_t *cond) {
  if (cond == NULL || *cond == NULL) return;
  WakeAllConditionVariable((PCONDITION_VARIABLE)*cond);
}

void turbo_cond_wait(turbo_cond_t *cond, turbo_mutex_t *mutex) {
  if (cond == NULL || *cond == NULL || mutex == NULL || *mutex == NULL) return;
  (void)SleepConditionVariableSRW((PCONDITION_VARIABLE)*cond,
                                  (PSRWLOCK)*mutex, INFINITE, 0);
}

int turbo_cond_timedwait(turbo_cond_t *cond, turbo_mutex_t *mutex,
                         uint64_t timeout_ns) {
  DWORD timeout_ms;
  BOOL result;

  if (cond == NULL || *cond == NULL || mutex == NULL || *mutex == NULL)
    return -EINVAL;

  if (timeout_ns >= (uint64_t)INFINITE * 1000000ULL) {
    timeout_ms = INFINITE - 1U;
  } else {
    timeout_ms = (DWORD)((timeout_ns + 999999ULL) / 1000000ULL);
  }

  result = SleepConditionVariableSRW((PCONDITION_VARIABLE)*cond,
                                     (PSRWLOCK)*mutex, timeout_ms, 0);
  if (result) return 0;
  return GetLastError() == ERROR_TIMEOUT ? -ETIMEDOUT : -EIO;
}

static unsigned __stdcall turbo_thread_entry_wrapper(void *arg) {
  struct turbo_thread_wrapper_ctx *ctx =
      (struct turbo_thread_wrapper_ctx *)arg;
  turbo_thread_cb entry = ctx->entry;
  void *real_arg = ctx->arg;
  free(ctx);
  entry(real_arg);
  return 0;
}

int turbo_thread_create(turbo_thread_t *thread, turbo_thread_cb entry, void *arg) {
  struct turbo_thread_wrapper_ctx *ctx;
  HANDLE native;

  if (thread == NULL || entry == NULL) return -EINVAL;
  ctx = (struct turbo_thread_wrapper_ctx *)malloc(sizeof(*ctx));
  if (ctx == NULL) return -ENOMEM;
  ctx->entry = entry;
  ctx->arg = arg;

  native = (HANDLE)_beginthreadex(NULL, 0, turbo_thread_entry_wrapper,
                                  ctx, 0, NULL);
  if (native == NULL) {
    free(ctx);
    return -1;
  }
  *thread = native;
  return 0;
}

int turbo_thread_join(turbo_thread_t *thread) {
  HANDLE native;
  if (thread == NULL || *thread == NULL) return -EINVAL;
  native = (HANDLE)*thread;
  if (WaitForSingleObject(native, INFINITE) != WAIT_OBJECT_0) return -1;
  CloseHandle(native);
  *thread = NULL;
  return 0;
}

void turbo_thread_destroy(turbo_thread_t *thread) {
  if (thread == NULL || *thread == NULL) return;
  CloseHandle((HANDLE)*thread);
  *thread = NULL;
}

void turbo_once(turbo_once_t *guard, void (*callback)(void)) {
  LONG previous;
  if (guard == NULL || callback == NULL) return;
  previous = InterlockedCompareExchange((volatile LONG *)&guard->state, 1, 0);
  if (previous == 0) {
    callback();
    (void)InterlockedExchange((volatile LONG *)&guard->state, 2);
    return;
  }
  while (InterlockedCompareExchange((volatile LONG *)&guard->state, 2, 2) != 2)
    SwitchToThread();
}

void turbo_sleep_ms(uint32_t ms) { Sleep(ms); }

void turbo_thread_yield(void) { SwitchToThread(); }

int turbo_cpu_count(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 4;
}

#else

typedef struct turbo_posix_cond_s {
  pthread_cond_t native;
} turbo_posix_cond_t;

void turbo_mutex_init(turbo_mutex_t *mutex) {
  pthread_mutex_t *native;
  if (mutex == NULL) return;
  native = (pthread_mutex_t *)malloc(sizeof(*native));
  if (native == NULL) return;
  if (pthread_mutex_init(native, NULL) != 0) {
    free(native);
    return;
  }
  *mutex = native;
}

void turbo_mutex_destroy(turbo_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  (void)pthread_mutex_destroy((pthread_mutex_t *)*mutex);
  free(*mutex);
  *mutex = NULL;
}

void turbo_mutex_lock(turbo_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  (void)pthread_mutex_lock((pthread_mutex_t *)*mutex);
}

void turbo_mutex_unlock(turbo_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  (void)pthread_mutex_unlock((pthread_mutex_t *)*mutex);
}

int turbo_rwlock_init(turbo_rwlock_t *lock) {
  pthread_rwlock_t *native;
  int rc;
  if (lock == NULL) return -EINVAL;
  native = (pthread_rwlock_t *)malloc(sizeof(*native));
  if (native == NULL) return -ENOMEM;
  rc = pthread_rwlock_init(native, NULL);
  if (rc != 0) {
    free(native);
    return -rc;
  }
  *lock = native;
  return 0;
}

void turbo_rwlock_destroy(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  (void)pthread_rwlock_destroy((pthread_rwlock_t *)*lock);
  free(*lock);
  *lock = NULL;
}

void turbo_rwlock_rdlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  (void)pthread_rwlock_rdlock((pthread_rwlock_t *)*lock);
}

void turbo_rwlock_rdunlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  (void)pthread_rwlock_unlock((pthread_rwlock_t *)*lock);
}

void turbo_rwlock_wrlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  (void)pthread_rwlock_wrlock((pthread_rwlock_t *)*lock);
}

void turbo_rwlock_wrunlock(turbo_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  (void)pthread_rwlock_unlock((pthread_rwlock_t *)*lock);
}

void turbo_cond_init(turbo_cond_t *cond) {
  turbo_posix_cond_t *wrapper;
  int rc;

  if (cond == NULL) return;
  wrapper = (turbo_posix_cond_t *)malloc(sizeof(*wrapper));
  if (wrapper == NULL) return;

#if defined(__APPLE__)
  rc = pthread_cond_init(&wrapper->native, NULL);
#else
  {
    pthread_condattr_t attr;
    rc = pthread_condattr_init(&attr);
    if (rc == 0) rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (rc == 0) rc = pthread_cond_init(&wrapper->native, &attr);
    (void)pthread_condattr_destroy(&attr);
  }
#endif

  if (rc != 0) {
    free(wrapper);
    return;
  }
  *cond = wrapper;
}

void turbo_cond_destroy(turbo_cond_t *cond) {
  turbo_posix_cond_t *wrapper;
  if (cond == NULL || *cond == NULL) return;
  wrapper = (turbo_posix_cond_t *)*cond;
  (void)pthread_cond_destroy(&wrapper->native);
  free(wrapper);
  *cond = NULL;
}

void turbo_cond_signal(turbo_cond_t *cond) {
  turbo_posix_cond_t *wrapper;
  if (cond == NULL || *cond == NULL) return;
  wrapper = (turbo_posix_cond_t *)*cond;
  (void)pthread_cond_signal(&wrapper->native);
}

void turbo_cond_broadcast(turbo_cond_t *cond) {
  turbo_posix_cond_t *wrapper;
  if (cond == NULL || *cond == NULL) return;
  wrapper = (turbo_posix_cond_t *)*cond;
  (void)pthread_cond_broadcast(&wrapper->native);
}

void turbo_cond_wait(turbo_cond_t *cond, turbo_mutex_t *mutex) {
  turbo_posix_cond_t *wrapper;
  if (cond == NULL || *cond == NULL || mutex == NULL || *mutex == NULL) return;
  wrapper = (turbo_posix_cond_t *)*cond;
  (void)pthread_cond_wait(&wrapper->native, (pthread_mutex_t *)*mutex);
}

static void turbo_timespec_add_ns(struct timespec *ts, uint64_t timeout_ns) {
  uint64_t seconds = timeout_ns / 1000000000ULL;
  uint64_t nanos = timeout_ns % 1000000000ULL;
  ts->tv_sec += (time_t)seconds;
  ts->tv_nsec += (long)nanos;
  if (ts->tv_nsec >= 1000000000L) {
    ts->tv_sec += 1;
    ts->tv_nsec -= 1000000000L;
  }
}

int turbo_cond_timedwait(turbo_cond_t *cond, turbo_mutex_t *mutex,
                         uint64_t timeout_ns) {
  turbo_posix_cond_t *wrapper;
  struct timespec deadline;
  int rc;

  if (cond == NULL || *cond == NULL || mutex == NULL || *mutex == NULL)
    return -EINVAL;
  wrapper = (turbo_posix_cond_t *)*cond;

#if defined(__APPLE__)
  deadline.tv_sec = (time_t)(timeout_ns / 1000000000ULL);
  deadline.tv_nsec = (long)(timeout_ns % 1000000000ULL);
  rc = pthread_cond_timedwait_relative_np(&wrapper->native,
                                          (pthread_mutex_t *)*mutex,
                                          &deadline);
#else
  if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) return -errno;
  turbo_timespec_add_ns(&deadline, timeout_ns);
  rc = pthread_cond_timedwait(&wrapper->native,
                              (pthread_mutex_t *)*mutex,
                              &deadline);
#endif

  if (rc == 0) return 0;
  return rc == ETIMEDOUT ? -ETIMEDOUT : -rc;
}

static void *turbo_thread_entry_wrapper_pthread(void *arg) {
  struct turbo_thread_wrapper_ctx *ctx =
      (struct turbo_thread_wrapper_ctx *)arg;
  turbo_thread_cb entry = ctx->entry;
  void *real_arg = ctx->arg;
  free(ctx);
  entry(real_arg);
  return NULL;
}

int turbo_thread_create(turbo_thread_t *thread, turbo_thread_cb entry, void *arg) {
  struct turbo_thread_wrapper_ctx *ctx;
  pthread_t *native;
  int rc;

  if (thread == NULL || entry == NULL) return -EINVAL;
  ctx = (struct turbo_thread_wrapper_ctx *)malloc(sizeof(*ctx));
  if (ctx == NULL) return -ENOMEM;
  native = (pthread_t *)malloc(sizeof(*native));
  if (native == NULL) {
    free(ctx);
    return -ENOMEM;
  }
  ctx->entry = entry;
  ctx->arg = arg;
  rc = pthread_create(native, NULL, turbo_thread_entry_wrapper_pthread, ctx);
  if (rc != 0) {
    free(native);
    free(ctx);
    return -rc;
  }
  *thread = native;
  return 0;
}

int turbo_thread_join(turbo_thread_t *thread) {
  pthread_t *native;
  int rc;
  if (thread == NULL || *thread == NULL) return -EINVAL;
  native = (pthread_t *)*thread;
  rc = pthread_join(*native, NULL);
  if (rc != 0) return -rc;
  free(native);
  *thread = NULL;
  return 0;
}

void turbo_thread_destroy(turbo_thread_t *thread) {
  pthread_t *native;
  if (thread == NULL || *thread == NULL) return;
  native = (pthread_t *)*thread;
  (void)pthread_detach(*native);
  free(native);
  *thread = NULL;
}

void turbo_once(turbo_once_t *guard, void (*callback)(void)) {
  int expected;
  if (guard == NULL || callback == NULL) return;
  expected = 0;
  if (__atomic_compare_exchange_n(&guard->state, &expected, 1, 0,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    callback();
    __atomic_store_n(&guard->state, 2, __ATOMIC_RELEASE);
    return;
  }
  while (__atomic_load_n(&guard->state, __ATOMIC_ACQUIRE) != 2)
    (void)sched_yield();
}

void turbo_sleep_ms(uint32_t ms) {
  struct timespec request;
  struct timespec remaining;
  request.tv_sec = (time_t)(ms / 1000U);
  request.tv_nsec = (long)(ms % 1000U) * 1000000L;
  while (nanosleep(&request, &remaining) != 0 && errno == EINTR)
    request = remaining;
}

void turbo_thread_yield(void) { (void)sched_yield(); }

int turbo_cpu_count(void) {
  long count = sysconf(_SC_NPROCESSORS_ONLN);
  return count > 0 ? (int)count : 4;
}

#endif
