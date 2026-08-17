#include "turbo_bytes.h"

#include "turbo_error.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define turbo_bytes_MIN_CAPACITY 256u

static int turbo_bytes_valid(const turbo_bytes_t *buffer) {
  if (!buffer || buffer->max_bytes == 0u || buffer->read_pos > buffer->write_pos ||
      buffer->write_pos > buffer->capacity || buffer->capacity > buffer->max_bytes)
    return 0;
  return (buffer->capacity == 0u) == (buffer->data == NULL);
}

static size_t turbo_bytes_unread(const turbo_bytes_t *buffer) {
  return buffer->write_pos - buffer->read_pos;
}

static size_t turbo_bytes_growth(const turbo_bytes_t *buffer, size_t required) {
  size_t grown;

  if (buffer->capacity == 0u) {
    grown = turbo_bytes_MIN_CAPACITY;
  } else if (buffer->capacity > buffer->max_bytes - buffer->capacity / 2u) {
    grown = buffer->max_bytes;
  } else {
    grown = buffer->capacity + buffer->capacity / 2u;
  }
  if (grown < required) grown = required;
  if (grown > buffer->max_bytes) grown = buffer->max_bytes;
  return grown;
}

static int turbo_bytes_source(const turbo_bytes_t *buffer, const void *data,
                                    size_t size, size_t *unread_offset, int *aliases) {
  uintptr_t base;
  uintptr_t end;
  uintptr_t source;
  uintptr_t source_end;

  *unread_offset = 0u;
  *aliases = 0;
  if (!buffer->data) return TURBO_OK;

  base = (uintptr_t)buffer->data;
  source = (uintptr_t)data;
  if (buffer->capacity > UINTPTR_MAX - base || size > UINTPTR_MAX - source) return TURBO_EINVAL;
  end = base + buffer->capacity;
  source_end = source + size;
  if (source >= end || source_end <= base) return TURBO_OK;

  if (source < base + buffer->read_pos || source > base + buffer->write_pos ||
      size > (base + buffer->write_pos) - source)
    return TURBO_EINVAL;
  *unread_offset = (size_t)(source - (base + buffer->read_pos));
  *aliases = 1;
  return TURBO_OK;
}

int turbo_bytes_init(turbo_bytes_t *buffer, size_t max_bytes) {
  if (!buffer || max_bytes == 0u) return TURBO_EINVAL;
  memset(buffer, 0, sizeof(*buffer));
  buffer->max_bytes = max_bytes;
  return TURBO_OK;
}

void turbo_bytes_destroy(turbo_bytes_t *buffer) {
  if (!buffer) return;
  free(buffer->data);
  memset(buffer, 0, sizeof(*buffer));
}

size_t turbo_bytes_size(const turbo_bytes_t *buffer) {
  return turbo_bytes_valid(buffer) ? turbo_bytes_unread(buffer) : 0u;
}

size_t turbo_bytes_available(const turbo_bytes_t *buffer) {
  return turbo_bytes_valid(buffer) ? buffer->max_bytes - turbo_bytes_unread(buffer)
                                         : 0u;
}

size_t turbo_bytes_capacity(const turbo_bytes_t *buffer) {
  return turbo_bytes_valid(buffer) ? buffer->capacity : 0u;
}

int turbo_bytes_append(turbo_bytes_t *buffer, const void *data, size_t size) {
  size_t unread;
  size_t source_offset;
  int aliases;
  int rc;

  if (!turbo_bytes_valid(buffer) || (!data && size != 0u)) return TURBO_EINVAL;
  unread = turbo_bytes_unread(buffer);
  if (size > buffer->max_bytes - unread) return TURBO_ENOSPC;
  if (size == 0u) return TURBO_OK;

  rc = turbo_bytes_source(buffer, data, size, &source_offset, &aliases);
  if (rc != TURBO_OK) return rc;

  if (size <= buffer->capacity - buffer->write_pos) {
    memmove(buffer->data + buffer->write_pos, data, size);
    buffer->write_pos += size;
    return TURBO_OK;
  }

  if (size <= buffer->capacity - unread) {
    memmove(buffer->data, buffer->data + buffer->read_pos, unread);
    if (aliases) data = buffer->data + source_offset;
    memmove(buffer->data + unread, data, size);
    buffer->read_pos = 0u;
    buffer->write_pos = unread + size;
    return TURBO_OK;
  }

  {
    size_t required = unread + size;
    size_t new_capacity = turbo_bytes_growth(buffer, required);
    uint8_t *next = (uint8_t *)malloc(new_capacity);
    if (!next) return TURBO_ENOMEM;

    if (unread != 0u) memcpy(next, buffer->data + buffer->read_pos, unread);
    if (aliases) data = next + source_offset;
    memmove(next + unread, data, size);
    free(buffer->data);
    buffer->data = next;
    buffer->read_pos = 0u;
    buffer->write_pos = required;
    buffer->capacity = new_capacity;
  }
  return TURBO_OK;
}

int turbo_bytes_view(const turbo_bytes_t *buffer, turbo_bytes_view_t *out) {
  turbo_bytes_view_t view;
  size_t unread;

  if (!turbo_bytes_valid(buffer) || !out) return TURBO_EINVAL;
  unread = turbo_bytes_unread(buffer);
  view.data = unread != 0u ? buffer->data + buffer->read_pos : NULL;
  view.size = unread;
  *out = view;
  return TURBO_OK;
}

int turbo_bytes_consume(turbo_bytes_t *buffer, size_t size) {
  size_t unread;

  if (!turbo_bytes_valid(buffer)) return TURBO_EINVAL;
  unread = turbo_bytes_unread(buffer);
  if (size > unread) return TURBO_ERANGE;
  if (size == unread) {
    buffer->read_pos = 0u;
    buffer->write_pos = 0u;
  } else {
    buffer->read_pos += size;
  }
  return TURBO_OK;
}

void turbo_bytes_reset(turbo_bytes_t *buffer) {
  if (!turbo_bytes_valid(buffer)) return;
  buffer->read_pos = 0u;
  buffer->write_pos = 0u;
}
