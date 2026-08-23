#ifndef TURBO_CLOCK_H
#define TURBO_CLOCK_H

#include <turbo/platform.h>
#include <stdint.h>

TURBO_PLATFORM_C_API uint64_t turbo_hrtime(void);
TURBO_PLATFORM_C_API uint64_t turbo_monotonic_ms(void);
TURBO_PLATFORM_C_API uint64_t turbo_realtime_ms(void);
TURBO_PLATFORM_C_API uint64_t turbo_uptime_ms(void);

static inline uint64_t turbo_ns_to_ms(uint64_t ns) { return ns / 1000000ULL; }
static inline uint64_t turbo_ms_to_ns(uint64_t ms) { return ms * 1000000ULL; }

#endif /* TURBO_CLOCK_H */
