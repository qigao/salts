/************************** INCLUDE ***************************/

#include "ring_buffer_spsc.h"
bool ring_spsc_init(ring_spsc_t *inst, uint8_t *data_array, size_t size) {
  return turbo_spsc_ring_init(inst, data_array, size);
}

uint8_t *ring_spsc_write_acquire(ring_spsc_t *inst, size_t size_required) {
  return turbo_spsc_ring_write_acquire(inst, size_required);
}

void ring_spsc_write_release(ring_spsc_t *inst, size_t bytes_written) {
  turbo_spsc_ring_write_release(inst, bytes_written);
}

uint8_t *ring_spsc_read_acquire(ring_spsc_t *inst, size_t *available) {
  return turbo_spsc_ring_read_acquire(inst, available);
}

void ring_spsc_read_release(ring_spsc_t *inst, size_t bytes_read) {
  turbo_spsc_ring_read_release(inst, bytes_read);
}

size_t ring_spsc_write_available(const ring_spsc_t *inst) {
  return turbo_spsc_ring_write_available(inst);
}

size_t ring_spsc_read_available(const ring_spsc_t *inst) {
  return turbo_spsc_ring_read_available(inst);
}
