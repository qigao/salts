#include "cnet_command.h"
#include "tinytest.h"

#include <salts_buffer.h>

#include <stdint.h>
#include <string.h>

static cnet_command_queue queue;

static cnet_command make_retained_send(uint32_t slot, mem_buffer_t *buffer) {
  cnet_command command = {0};
  command.kind = CNET_COMMAND_SEND;
  command.connection.slot = slot;
  command.connection.generation = 1u;
  command.size = mem_buffer_used(buffer);
  command.retained_buffer = buffer;
  command.retained_data = mem_buffer_const_data(buffer);
  return command;
}

static cnet_command make_retained_view_send(uint32_t slot, mem_buffer_t *buffer,
                                            const void *data, size_t size) {
  cnet_command command = {0};
  command.kind = CNET_COMMAND_SEND;
  command.connection.slot = slot;
  command.connection.generation = 1u;
  command.size = size;
  command.retained_buffer = buffer;
  command.retained_data = data;
  return command;
}

spec("CNet command queue diagnostic profile") {
  before_each() { memset(&queue, 0, sizeof(queue)); }

  after_each() {
    if (queue.impl != NULL) {
      int status = cnet_command_queue_close(&queue);
      check_true(status == SALTS_OK || status == SALTS_EALREADY);
      check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
    }
  }

  it("separates payload copy from publication control") {
    static const uint8_t payload[4096] = {1u};
    const cnet_command_queue_config config = {
        .capacity = 2u,
        .max_payload_bytes = sizeof(payload),
    };
    const cnet_command command = {
        .kind = CNET_COMMAND_SEND,
        .connection = {.slot = 1u, .generation = 1u},
        .data = payload,
        .size = sizeof(payload),
    };
    cnet_command_queue_profile profile = {0};
    cnet_command_view view = {0};

    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_profile_begin(&queue), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    check_equal(cnet_command_queue_profile_take(&queue, &profile), SALTS_OK);

    check_equal(profile.publish_calls, UINT64_C(1));
    check_equal(profile.payload_publish_calls, UINT64_C(1));
    check_equal(profile.payload_copy_calls, UINT64_C(1));
    check_true(profile.publish_ns >= profile.payload_publish_ns);
    check_true(profile.payload_publish_ns >= profile.payload_copy_ns);
    check_true(profile.payload_copy_ns > UINT64_C(0));

    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  }

  it("retains one buffer without copying payload bytes") {
    mem_pool_t pool;
    mem_buffer_t *buffer;
    cnet_command_queue_profile profile = {0};
    cnet_command_view view = {0};
    const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 64u};
    const void *original;

    check_equal(mem_init(&pool, 0u), 0);
    buffer = mem_get_buffer(&pool, 32u);
    check_true(buffer != NULL);
    memset(mem_buffer_data(buffer), 0x5a, 32u);
    mem_set_used(buffer, 32u);
    original = mem_buffer_const_data(buffer);

    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_profile_begin(&queue), SALTS_OK);
    {
      cnet_command command = make_retained_send(1u, buffer);
      check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    }
    check_equal(cnet_command_queue_profile_take(&queue, &profile), SALTS_OK);
    check_equal(profile.publish_calls, UINT64_C(1));
    check_equal(profile.payload_publish_calls, UINT64_C(1));
    check_equal(profile.payload_copy_calls, UINT64_C(0));
    check_equal(profile.payload_copy_ns, UINT64_C(0));
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

    mem_buffer_release(buffer);

    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_true(view.data == original);
    check_equal(view.size, 32u);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);

    check_equal(cnet_command_queue_close(&queue), SALTS_OK);
    check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
    mem_destroy(&pool);
  }

  it("retains an interior view without copying payload bytes") {
    mem_pool_t pool;
    mem_buffer_t *buffer;
    cnet_command_queue_profile profile = {0};
    cnet_command_view view = {0};
    const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 64u};
    const void *interior;

    check_equal(mem_init(&pool, 0u), 0);
    buffer = mem_get_buffer(&pool, 32u);
    check_true(buffer != NULL);
    memset(mem_buffer_data(buffer), 0x6b, 32u);
    mem_set_used(buffer, 32u);
    interior = mem_buffer_const_data(buffer) + 8u;

    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_profile_begin(&queue), SALTS_OK);
    {
      cnet_command command = make_retained_view_send(1u, buffer, interior, 12u);
      check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    }
    check_equal(cnet_command_queue_profile_take(&queue, &profile), SALTS_OK);
    check_equal(profile.publish_calls, UINT64_C(1));
    check_equal(profile.payload_publish_calls, UINT64_C(1));
    check_equal(profile.payload_copy_calls, UINT64_C(0));
    check_equal(profile.payload_copy_ns, UINT64_C(0));
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

    mem_buffer_release(buffer);

    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_true(view.data == interior);
    check_equal(view.size, 12u);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);

    check_equal(cnet_command_queue_close(&queue), SALTS_OK);
    check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
    mem_destroy(&pool);
  }

  it("does not retain rejected zero-copy buffers") {
    mem_pool_t pool;
    mem_buffer_t *first;
    mem_buffer_t *second;
    cnet_command_view view = {0};
    const cnet_command_queue_config config = {.capacity = 1u, .max_payload_bytes = 64u};

    check_equal(mem_init(&pool, 0u), 0);
    first = mem_get_buffer(&pool, 16u);
    second = mem_get_buffer(&pool, 16u);
    check_true(first != NULL);
    check_true(second != NULL);
    mem_set_used(first, 16u);
    mem_set_used(second, 16u);

    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    {
      cnet_command command = make_retained_send(1u, first);
      check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    }
    check_equal(mem_buffer_ref_count(first), UINT32_C(2));

    {
      cnet_command command = make_retained_send(2u, second);
      check_equal(cnet_command_queue_publish(&queue, &command), SALTS_ENOBUFS);
    }
    check_equal(mem_buffer_ref_count(second), UINT32_C(1));

    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
    check_equal(mem_buffer_ref_count(first), UINT32_C(1));

    check_equal(cnet_command_queue_close(&queue), SALTS_OK);
    {
      cnet_command command = make_retained_send(2u, second);
      check_equal(cnet_command_queue_publish(&queue, &command), SALTS_ESHUTDOWN);
    }
    check_equal(mem_buffer_ref_count(second), UINT32_C(1));

    mem_buffer_release(first);
    mem_buffer_release(second);
    check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
    mem_destroy(&pool);
  }

  it("does not double release retained buffers through stale command views") {
    mem_pool_t pool;
    mem_buffer_t *buffer;
    cnet_command_view view = {0};
    cnet_command_view stale = {0};
    const cnet_command_queue_config config = {.capacity = 1u, .max_payload_bytes = 64u};

    check_equal(mem_init(&pool, 0u), 0);
    buffer = mem_get_buffer(&pool, 16u);
    check_true(buffer != NULL);
    mem_set_used(buffer, 16u);

    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    {
      cnet_command command = make_retained_send(1u, buffer);
      check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    }
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    stale = view;
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));
    check_equal(cnet_command_queue_release(&queue, &stale), SALTS_EINVAL);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    mem_buffer_release(buffer);
    check_equal(cnet_command_queue_close(&queue), SALTS_OK);
    check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
    mem_destroy(&pool);
  }
}
