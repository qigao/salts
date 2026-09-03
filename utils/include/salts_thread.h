/**
 * @file salts_thread.h
 * @brief Core compatibility surface for threading and thread-pool APIs.
 */

#ifndef SALTS_THREAD_H
#define SALTS_THREAD_H

#include "platform.h"
#include <salts/thread.h>
#include <salts/thread_pool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Core-owned synchronization policy. */
SALTS_C_API void salts_sync_set_single_threaded(int enabled);
SALTS_C_API int salts_sync_is_single_threaded(void);
SALTS_C_API int salts_getpid(void);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_THREAD_H */
