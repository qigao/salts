#ifndef CFLOW_TIME_H
#define CFLOW_TIME_H

#include <stdint.h>

typedef struct cflow_duration {
    uint64_t ns;
} cflow_duration;

typedef struct cflow_instant {
    uint64_t ns;
} cflow_instant;

typedef struct cflow_deadline {
    uint64_t ns;
} cflow_deadline;

static inline uint64_t cflow_u64_mul_sat(uint64_t value, uint64_t scale) {
    if (value != 0u && scale > UINT64_MAX / value) return UINT64_MAX;
    return value * scale;
}

static inline cflow_duration cflow_duration_from_ns(uint64_t ns) {
    cflow_duration value = {ns};
    return value;
}

static inline cflow_duration cflow_duration_from_us(uint64_t us) {
    return cflow_duration_from_ns(cflow_u64_mul_sat(us, 1000u));
}

static inline cflow_duration cflow_duration_from_ms(uint64_t ms) {
    return cflow_duration_from_ns(cflow_u64_mul_sat(ms, 1000000u));
}

static inline cflow_duration cflow_duration_from_s(uint64_t seconds) {
    return cflow_duration_from_ns(cflow_u64_mul_sat(seconds, 1000000000u));
}

static inline cflow_deadline cflow_deadline_after(cflow_instant now,
                                                   cflow_duration delay) {
    cflow_deadline result;
    result.ns = delay.ns > UINT64_MAX - now.ns ? UINT64_MAX : now.ns + delay.ns;
    return result;
}

static inline uint64_t cflow_instant_to_ms(cflow_instant value) {
    return value.ns / 1000000u;
}

static inline cflow_duration cflow_deadline_remaining(cflow_deadline deadline,
                                                       cflow_instant now) {
    return cflow_duration_from_ns(deadline.ns > now.ns ? deadline.ns - now.ns : 0u);
}

#endif /* CFLOW_TIME_H */
