/**
 * @file turbo_thread.h
 * @brief Core compatibility surface for threading and thread-pool APIs.
 */

#ifndef TURBO_THREAD_H
#define TURBO_THREAD_H

#include "platform.h"
#include <turbo/thread.h>
#include <turbo/thread_pool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Core-owned synchronization policy. */
TURBO_C_API void turbo_sync_set_single_threaded(int enabled);
TURBO_C_API int turbo_sync_is_single_threaded(void);
TURBO_C_API int turbo_getpid(void);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_THREAD_H */
