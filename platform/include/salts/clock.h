#ifndef SALTS_CLOCK_H
#define SALTS_CLOCK_H

#include <salts/platform.h>
#include <stdint.h>

SALTS_PLATFORM_C_API uint64_t salts_hrtime(void);
SALTS_PLATFORM_C_API uint64_t salts_monotonic_ms(void);
SALTS_PLATFORM_C_API uint64_t salts_realtime_ms(void);
SALTS_PLATFORM_C_API uint64_t salts_uptime_ms(void);

static inline uint64_t salts_ns_to_ms(uint64_t ns) { return ns / 1000000ULL; }
static inline uint64_t salts_ms_to_ns(uint64_t ms) { return ms * 1000000ULL; }

#endif /* SALTS_CLOCK_H */
