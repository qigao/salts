#include "cnet_command.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum cnet_command_entry_state {
  CNET_COMMAND_ENTRY_FREE = 0,
  CNET_COMMAND_ENTRY_QUEUED,
  CNET_COMMAND_ENTRY_BORROWED
} cnet_command_entry_state;

typedef struct cnet_command_entry {
  cnet_command_kind kind;
  cnet_session_handle connection;
  size_t size;
  size_t argument;
  uint32_t generation;
  cnet_command_entry_state state;
  unsigned char payload[];
} cnet_command_entry;

typedef struct cnet_command_queue_impl {
  unsigned char *entries;
  uint32_t *free_slots;
  uint32_t *queued_slots;
  size_t capacity;
  size_t mask;
  size_t entry_size;
  size_t max_payload_bytes;
  size_t free_count;
  size_t queued_head;
  size_t queued_count;
  size_t live_commands;
  size_t peak_commands;
  size_t queued_bytes;
  size_t peak_queued_bytes;
  uint64_t rejected_commands;
  uint64_t rejected_bytes;
  bool admission_open;
  bool close_complete;
} cnet_command_queue_impl;

static cnet_command_queue_impl *cnet_command_impl(cnet_command_queue *queue) {
  return queue != NULL ? (cnet_command_queue_impl *)queue->impl : NULL;
}

static const cnet_command_queue_impl *cnet_command_const_impl(const cnet_command_queue *queue) {
  return queue != NULL ? (const cnet_command_queue_impl *)queue->impl : NULL;
}

static cnet_command_entry *cnet_command_entry_at(cnet_command_queue_impl *impl, size_t slot) {
  return (cnet_command_entry *)(impl->entries + slot * impl->entry_size);
}

static void cnet_command_saturating_add(uint64_t *counter, uint64_t value) {
  *counter = *counter > UINT64_MAX - value ? UINT64_MAX : *counter + value;
}

static void cnet_command_record_rejection(cnet_command_queue_impl *impl, size_t bytes) {
  cnet_command_saturating_add(&impl->rejected_commands, 1u);
  cnet_command_saturating_add(&impl->rejected_bytes, (uint64_t)bytes);
}

static uint32_t cnet_command_next_generation(uint32_t generation) {
  return generation == UINT32_MAX ? 1u : generation + 1u;
}

static uint64_t cnet_command_token(size_t slot, uint32_t generation) {
  return ((uint64_t)generation << 32u) | ((uint64_t)slot + 1u);
}

static bool cnet_is_power_of_two(uint64_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static bool cnet_command_valid(const cnet_command *command) {
  if (command == NULL || command->kind <= CNET_COMMAND_NONE || command->kind > CNET_COMMAND_STOP)
    return false;

  if (command->kind == CNET_COMMAND_STOP)
    return !cnet_session_handle_valid(command->connection) && command->data == NULL &&
           command->size == 0u;

  if (!cnet_session_handle_valid(command->connection)) return false;
  if (command->kind == CNET_COMMAND_CONNECT || command->kind == CNET_COMMAND_SEND)
    return command->data != NULL && command->size != 0u && command->argument == 0u;
  if (command->kind == CNET_COMMAND_RECEIVE)
    return command->data == NULL && command->size == 0u && command->argument != 0u;
  return command->data == NULL && command->size == 0u && command->argument == 0u;
}

int cnet_command_queue_init(cnet_command_queue *queue, const cnet_command_queue_config *config) {
  cnet_command_queue_impl *impl;
  const size_t entry_alignment = _Alignof(cnet_command_entry);
  size_t entry_size;
  size_t capacity;

  if (queue == NULL) return TURBO_EINVAL;
  if (queue->impl != NULL) return TURBO_EALREADY;
  if (config == NULL || !cnet_is_power_of_two(config->capacity) || config->max_payload_bytes == 0u)
    return TURBO_EINVAL;
  if (config->capacity > UINT32_MAX) return TURBO_ERANGE;
  if (config->max_payload_bytes > SIZE_MAX - offsetof(cnet_command_entry, payload))
    return TURBO_ERANGE;
  entry_size = offsetof(cnet_command_entry, payload) + config->max_payload_bytes;
  if (entry_size > SIZE_MAX - (entry_alignment - 1u)) return TURBO_ERANGE;
  entry_size = (entry_size + entry_alignment - 1u) & ~(entry_alignment - 1u);
  capacity = (size_t)config->capacity;
  if (capacity > SIZE_MAX / entry_size || capacity > SIZE_MAX / sizeof(uint32_t))
    return TURBO_ERANGE;

  impl = (cnet_command_queue_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->entries = (unsigned char *)calloc(capacity, entry_size);
  impl->free_slots = (uint32_t *)calloc(capacity, sizeof(*impl->free_slots));
  impl->queued_slots = (uint32_t *)calloc(capacity, sizeof(*impl->queued_slots));
  if (impl->entries == NULL || impl->free_slots == NULL || impl->queued_slots == NULL) {
    free(impl->queued_slots);
    free(impl->free_slots);
    free(impl->entries);
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->capacity = capacity;
  impl->mask = capacity - 1u;
  impl->entry_size = entry_size;
  impl->max_payload_bytes = config->max_payload_bytes;
  impl->free_count = capacity;
  impl->admission_open = true;
  for (size_t index = 0u; index < capacity; ++index)
    impl->free_slots[index] = (uint32_t)(capacity - index - 1u);
  queue->impl = impl;
  return TURBO_OK;
}

int cnet_command_queue_publish(cnet_command_queue *queue, const cnet_command *command) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  cnet_command_entry *entry;
  size_t slot;
  size_t queue_tail;

  if (impl == NULL || !cnet_command_valid(command)) return TURBO_EINVAL;
  if (command->size > impl->max_payload_bytes) {
    cnet_command_record_rejection(impl, command->size);
    return TURBO_EMSGSIZE;
  }
  if (!impl->admission_open) {
    cnet_command_record_rejection(impl, command->size);
    return TURBO_ESHUTDOWN;
  }
  if (impl->free_count == 0u) {
    cnet_command_record_rejection(impl, command->size);
    return TURBO_ENOBUFS;
  }

  slot = impl->free_slots[--impl->free_count];
  entry = cnet_command_entry_at(impl, slot);
  if (entry->state != CNET_COMMAND_ENTRY_FREE) return TURBO_EPROTO;
  entry->kind = command->kind;
  entry->connection = command->connection;
  entry->size = command->size;
  entry->argument = command->argument;
  entry->generation = cnet_command_next_generation(entry->generation);
  entry->state = CNET_COMMAND_ENTRY_QUEUED;
  if (command->size != 0u) memcpy(entry->payload, command->data, command->size);

  queue_tail = (impl->queued_head + impl->queued_count) & impl->mask;
  impl->queued_slots[queue_tail] = (uint32_t)slot;
  ++impl->queued_count;
  ++impl->live_commands;
  impl->queued_bytes += command->size;
  if (impl->peak_commands < impl->live_commands) impl->peak_commands = impl->live_commands;
  if (impl->peak_queued_bytes < impl->queued_bytes) impl->peak_queued_bytes = impl->queued_bytes;
  return TURBO_OK;
}

int cnet_command_queue_take(cnet_command_queue *queue, cnet_command_view *out_view) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  cnet_command_entry *entry;
  size_t slot;

  if (out_view == NULL) return TURBO_EINVAL;
  memset(out_view, 0, sizeof(*out_view));
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->queued_count == 0u)
    return impl->close_complete && impl->live_commands == 0u ? TURBO_EOF : TURBO_ETIMEDOUT;

  slot = impl->queued_slots[impl->queued_head];
  impl->queued_head = (impl->queued_head + 1u) & impl->mask;
  --impl->queued_count;
  entry = cnet_command_entry_at(impl, slot);
  if (entry->state != CNET_COMMAND_ENTRY_QUEUED) return TURBO_EPROTO;
  entry->state = CNET_COMMAND_ENTRY_BORROWED;
  out_view->kind = entry->kind;
  out_view->connection = entry->connection;
  out_view->data = entry->size != 0u ? entry->payload : NULL;
  out_view->size = entry->size;
  out_view->argument = entry->argument;
  out_view->_sequence = cnet_command_token(slot, entry->generation);
  return TURBO_OK;
}

int cnet_command_queue_release(cnet_command_queue *queue, cnet_command_view *view) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  cnet_command_entry *entry;
  const uint32_t encoded_slot = view != NULL ? (uint32_t)view->_sequence : 0u;
  const uint32_t generation = view != NULL ? (uint32_t)(view->_sequence >> 32u) : 0u;
  size_t slot;

  if (impl == NULL || view == NULL || encoded_slot == 0u || generation == 0u) return TURBO_EINVAL;
  slot = (size_t)encoded_slot - 1u;
  if (slot >= impl->capacity) return TURBO_EINVAL;
  entry = cnet_command_entry_at(impl, slot);
  if (entry->state != CNET_COMMAND_ENTRY_BORROWED || entry->generation != generation)
    return TURBO_EINVAL;
  if (impl->live_commands == 0u || impl->queued_bytes < entry->size ||
      impl->free_count >= impl->capacity)
    return TURBO_EPROTO;

  --impl->live_commands;
  impl->queued_bytes -= entry->size;
  entry->state = CNET_COMMAND_ENTRY_FREE;
  impl->free_slots[impl->free_count++] = (uint32_t)slot;
  memset(view, 0, sizeof(*view));
  return TURBO_OK;
}

int cnet_command_queue_close(cnet_command_queue *queue) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->close_complete) return TURBO_EALREADY;
  impl->admission_open = false;
  impl->close_complete = true;
  return TURBO_OK;
}

bool cnet_command_queue_get_stats(const cnet_command_queue *queue,
                                  cnet_command_queue_stats *out_stats) {
  const cnet_command_queue_impl *impl = cnet_command_const_impl(queue);
  if (impl == NULL || out_stats == NULL) return false;
  *out_stats = (cnet_command_queue_stats){
      impl->live_commands,     impl->peak_commands,  impl->queued_bytes,  impl->peak_queued_bytes,
      impl->rejected_commands, impl->rejected_bytes, impl->admission_open};
  return true;
}

int cnet_command_queue_destroy(cnet_command_queue *queue) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  if (queue == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!impl->close_complete || impl->live_commands != 0u || impl->queued_count != 0u ||
      impl->free_count != impl->capacity)
    return TURBO_EBUSY;
  free(impl->queued_slots);
  free(impl->free_slots);
  free(impl->entries);
  free(impl);
  queue->impl = NULL;
  return TURBO_OK;
}
