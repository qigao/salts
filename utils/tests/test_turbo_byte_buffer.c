#include "turbo_byte_buffer.h"

#include "tinytest.h"
#include "turbo_error.h"

#include <stdint.h>
#include <string.h>

spec("bounded byte buffer") {
  it("initializes lazily and exposes an empty view") {
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    turbo_byte_buffer_view_t view = {(const uint8_t *)1, 1u};

    check_int_eq(turbo_byte_buffer_init(&buffer, 1024u), TURBO_OK);
    check_size_eq(turbo_byte_buffer_size(&buffer), 0u);
    check_size_eq(turbo_byte_buffer_available(&buffer), 1024u);
    check_size_eq(turbo_byte_buffer_capacity(&buffer), 0u);
    check_int_eq(turbo_byte_buffer_view(&buffer, &view), TURBO_OK);
    check_null(view.data);
    check_size_eq(view.size, 0u);
    turbo_byte_buffer_destroy(&buffer);
  }

  it("reassembles binary chunks in one contiguous view") {
    static const uint8_t first[] = {'a', 0u, 'b'};
    static const uint8_t second[] = {'c', 'd'};
    static const uint8_t expected[] = {'a', 0u, 'b', 'c', 'd'};
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    turbo_byte_buffer_view_t view;

    check_int_eq(turbo_byte_buffer_init(&buffer, sizeof(expected)), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, first, sizeof(first)), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, second, sizeof(second)), TURBO_OK);
    check_int_eq(turbo_byte_buffer_view(&buffer, &view), TURBO_OK);
    check_size_eq(view.size, sizeof(expected));
    check_mem_eq(view.data, expected, sizeof(expected));
    check_size_le(turbo_byte_buffer_capacity(&buffer), sizeof(expected));
    turbo_byte_buffer_destroy(&buffer);
  }

  it("preserves unread bytes when quota or consume validation fails") {
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    turbo_byte_buffer_view_t before;
    turbo_byte_buffer_view_t after;

    check_int_eq(turbo_byte_buffer_init(&buffer, 5u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, "1234", 4u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_view(&buffer, &before), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, "56", 2u), TURBO_ENOSPC);
    check_int_eq(turbo_byte_buffer_consume(&buffer, 5u), TURBO_ERANGE);
    check_int_eq(turbo_byte_buffer_view(&buffer, &after), TURBO_OK);
    check_size_eq(after.size, before.size);
    check_mem_eq(after.data, before.data, before.size);
    check_size_le(turbo_byte_buffer_capacity(&buffer), 5u);
    turbo_byte_buffer_destroy(&buffer);
  }

  it("defers compaction until append needs contiguous tail space") {
    uint8_t source[256];
    uint8_t suffix[128];
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    turbo_byte_buffer_view_t view;
    uint8_t *allocation;
    size_t i;

    for (i = 0u; i < sizeof(source); ++i)
      source[i] = (uint8_t)i;
    memset(suffix, 0xa5, sizeof(suffix));
    check_int_eq(turbo_byte_buffer_init(&buffer, 512u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, source, sizeof(source)), TURBO_OK);
    allocation = buffer.data;
    check_int_eq(turbo_byte_buffer_consume(&buffer, 192u), TURBO_OK);
    check_ptr_eq(buffer.data, allocation);
    check_size_eq(buffer.read_pos, 192u);

    check_int_eq(turbo_byte_buffer_append(&buffer, suffix, sizeof(suffix)), TURBO_OK);
    check_ptr_eq(buffer.data, allocation);
    check_size_eq(buffer.read_pos, 0u);
    check_int_eq(turbo_byte_buffer_view(&buffer, &view), TURBO_OK);
    check_size_eq(view.size, 192u);
    check_mem_eq(view.data, source + 192u, 64u);
    check_mem_eq(view.data + 64u, suffix, sizeof(suffix));
    turbo_byte_buffer_destroy(&buffer);
  }

  it("supports appending from its unread view across growth") {
    uint8_t source[200];
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    turbo_byte_buffer_view_t view;
    size_t i;

    for (i = 0u; i < sizeof(source); ++i)
      source[i] = (uint8_t)(i ^ 0x5au);
    check_int_eq(turbo_byte_buffer_init(&buffer, 1024u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, source, sizeof(source)), TURBO_OK);
    check_int_eq(turbo_byte_buffer_view(&buffer, &view), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, view.data + 50u, 150u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_view(&buffer, &view), TURBO_OK);
    check_size_eq(view.size, 350u);
    check_mem_eq(view.data, source, sizeof(source));
    check_mem_eq(view.data + sizeof(source), source + 50u, 150u);
    turbo_byte_buffer_destroy(&buffer);
  }

  it("rejects overlap outside the unread range without mutation") {
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    turbo_byte_buffer_view_t view;
    const uint8_t *consumed;

    check_int_eq(turbo_byte_buffer_init(&buffer, 64u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, "abcdef", 6u), TURBO_OK);
    consumed = buffer.data;
    check_int_eq(turbo_byte_buffer_consume(&buffer, 3u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, consumed, 1u), TURBO_EINVAL);
    check_int_eq(turbo_byte_buffer_view(&buffer, &view), TURBO_OK);
    check_size_eq(view.size, 3u);
    check_mem_eq(view.data, "def", 3u);
    turbo_byte_buffer_destroy(&buffer);
  }

  it("reset reuses capacity and destroy clears the object") {
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    size_t capacity;

    check_int_eq(turbo_byte_buffer_init(&buffer, 1024u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, "payload", 7u), TURBO_OK);
    capacity = turbo_byte_buffer_capacity(&buffer);
    turbo_byte_buffer_reset(&buffer);
    check_size_eq(turbo_byte_buffer_size(&buffer), 0u);
    check_size_eq(turbo_byte_buffer_capacity(&buffer), capacity);
    turbo_byte_buffer_destroy(&buffer);
    check_null(buffer.data);
    check_size_eq(buffer.max_bytes, 0u);
    check_size_eq(turbo_byte_buffer_capacity(&buffer), 0u);
  }

  it("reports invalid arguments without changing output views") {
    turbo_byte_buffer_t buffer = TURBO_BYTE_BUFFER_INIT;
    turbo_byte_buffer_view_t out = {(const uint8_t *)1, 7u};

    check_int_eq(turbo_byte_buffer_init(NULL, 1u), TURBO_EINVAL);
    check_int_eq(turbo_byte_buffer_init(&buffer, 0u), TURBO_EINVAL);
    check_int_eq(turbo_byte_buffer_append(&buffer, "x", 1u), TURBO_EINVAL);
    check_int_eq(turbo_byte_buffer_view(&buffer, &out), TURBO_EINVAL);
    check_ptr_eq(out.data, (const uint8_t *)1);
    check_size_eq(out.size, 7u);

    check_int_eq(turbo_byte_buffer_init(&buffer, 8u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_append(&buffer, NULL, 1u), TURBO_EINVAL);
    check_int_eq(turbo_byte_buffer_append(&buffer, NULL, 0u), TURBO_OK);
    check_int_eq(turbo_byte_buffer_view(&buffer, NULL), TURBO_EINVAL);
    turbo_byte_buffer_destroy(&buffer);
  }
}
