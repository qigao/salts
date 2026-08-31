#ifndef TURBO_SPSC_RING_H
#define TURBO_SPSC_RING_H

#include <turbo/concurrency.h>

#ifdef __cplusplus
  #include <atomic>
  #define TURBO_SPSC_ATOMIC_SIZE_T std::atomic<size_t>
#else
  #include <stdalign.h>
  #include <stdatomic.h>
  #define TURBO_SPSC_ATOMIC_SIZE_T _Atomic size_t
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A borrowed-storage byte ring for exactly one producer and one consumer.
 * The producer owns write acquire/release; the consumer owns read
 * acquire/release. Control-plane reset or storage replacement requires both
 * roles to be quiescent.
 */
typedef struct turbo_spsc_ring {
  size_t size;
  size_t mask;
  uint8_t *data;
  alignas(64) TURBO_SPSC_ATOMIC_SIZE_T write_pos;
  alignas(64) TURBO_SPSC_ATOMIC_SIZE_T read_pos;
  alignas(64) TURBO_SPSC_ATOMIC_SIZE_T wrap_pos;
  TURBO_SPSC_ATOMIC_SIZE_T wrap_len;
  size_t pending_wrap_len;
} turbo_spsc_ring;

TURBO_CONCURRENCY_C_API bool turbo_spsc_ring_init(turbo_spsc_ring *ring, uint8_t *storage,
                                                   size_t size);
TURBO_CONCURRENCY_C_API uint8_t *turbo_spsc_ring_write_acquire(turbo_spsc_ring *ring,
                                                               size_t required);
TURBO_CONCURRENCY_C_API void turbo_spsc_ring_write_release(turbo_spsc_ring *ring,
                                                           size_t written);
TURBO_CONCURRENCY_C_API uint8_t *turbo_spsc_ring_read_acquire(turbo_spsc_ring *ring,
                                                              size_t *available);
TURBO_CONCURRENCY_C_API void turbo_spsc_ring_read_release(turbo_spsc_ring *ring, size_t read);
TURBO_CONCURRENCY_C_API size_t turbo_spsc_ring_write_available(const turbo_spsc_ring *ring);
TURBO_CONCURRENCY_C_API size_t turbo_spsc_ring_read_available(const turbo_spsc_ring *ring);

#ifdef __cplusplus
}
#endif

#undef TURBO_SPSC_ATOMIC_SIZE_T

#endif /* TURBO_SPSC_RING_H */
