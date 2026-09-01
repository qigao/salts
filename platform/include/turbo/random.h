#ifndef TURBO_PLATFORM_RANDOM_H
#define TURBO_PLATFORM_RANDOM_H

#include <turbo/platform.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fills a buffer from the operating system CSPRNG without a process-local
 * fallback. A zero-length request accepts a null buffer.
 */
TURBO_PLATFORM_C_API int turbo_platform_secure_random(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_PLATFORM_RANDOM_H */
