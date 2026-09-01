#include "cnet_session.h"
#include "tinytest.h"
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

static cnet_session_table table;

enum { TEST_RESERVATION_PRODUCERS = 4, TEST_RESERVATION_SCALE = 4096 };

typedef struct reservation_context {
  cnet_session_table *table;
  atomic_bool *go;
  cnet_session_handle handle;
  int status;
} reservation_context;

static void reserve_concurrently(void *user) {
  reservation_context *context = (reservation_context *)user;
  while (!atomic_load_explicit(context->go, memory_order_acquire))
    turbo_thread_yield();
  context->status = cnet_session_table_reserve(context->table, &context->handle);
}

static void expect_zero_handle(cnet_session_handle handle) {
  check_equal(handle.slot, UINT32_C(0));
  check_equal(handle.generation, UINT32_C(0));
}

static cnet_session_handle reserve_session(void) {
  cnet_session_handle handle = {0};
  check_equal(cnet_session_table_reserve(&table, &handle), TURBO_OK);
  check_true(cnet_session_handle_valid(handle));
  return handle;
}

static void close_and_recycle(cnet_session_handle handle) {
  cnet_session_state state = CNET_SESSION_FREE;
  cnet_session_terminal terminal = {0};

  check_equal(cnet_session_table_state(&table, handle, &state), TURBO_OK);
  if (state != CNET_SESSION_DRAINING && state != CNET_SESSION_TERMINAL) {
    check_equal(cnet_session_table_begin_close(&table, handle), TURBO_OK);
  }
  if (state != CNET_SESSION_TERMINAL) {
    check_equal(cnet_session_table_finish_close(&table, handle), TURBO_OK);
  }
  check_equal(cnet_session_table_take_terminal(&table, handle, &terminal), TURBO_OK);
  check_equal(cnet_session_table_recycle(&table, handle), TURBO_OK);
}

spec("CNet session state core") {
  before_each() { memset(&table, 0, sizeof(table)); }

  after_each() {
    if (table.impl != NULL) {
      check_equal(cnet_session_table_destroy(&table), TURBO_OK);
    }
  }

  group("initialization") {
    it("rejects zero capacity and leaves the destination clear") {
      check_equal(cnet_session_table_init(&table, 0u), TURBO_EINVAL);
      check_null(table.impl);
    }

    it("rejects allocation-size overflow and leaves the destination clear") {
      check_equal(cnet_session_table_init(&table, SIZE_MAX), TURBO_ERANGE);
      check_null(table.impl);
    }

    it("rejects reinitialization without losing the original table") {
      cnet_session_handle handle;

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      check_equal(cnet_session_table_init(&table, 2u), TURBO_EALREADY);
      check_not_null(table.impl);
      handle = reserve_session();
      close_and_recycle(handle);
    }
  }

  group("reservation") {
    it("cycles every slot in a large bounded reservation set") {
      static cnet_session_handle handles[TEST_RESERVATION_SCALE];
      cnet_session_handle rejected = {UINT32_C(99), UINT32_C(77)};

      check_equal(cnet_session_table_init(&table, TEST_RESERVATION_SCALE), TURBO_OK);
      for (size_t index = 0u; index < TEST_RESERVATION_SCALE; ++index)
        check_equal(cnet_session_table_reserve(&table, &handles[index]), TURBO_OK);
      check_equal(cnet_session_table_reserve(&table, &rejected), TURBO_ENOBUFS);
      expect_zero_handle(rejected);
      for (size_t index = 0u; index < TEST_RESERVATION_SCALE; ++index)
        check_equal(cnet_session_table_release_reservation(&table, handles[index]), TURBO_OK);
      for (size_t index = 0u; index < TEST_RESERVATION_SCALE; ++index) {
        const cnet_session_handle previous = handles[index];
        check_equal(cnet_session_table_reserve(&table, &handles[index]), TURBO_OK);
        check_not_equal(handles[index].generation, previous.generation);
      }
      for (size_t index = 0u; index < TEST_RESERVATION_SCALE; ++index)
        check_equal(cnet_session_table_release_reservation(&table, handles[index]), TURBO_OK);
    }

    it("assigns unique bounded handles to concurrent admission producers") {
      turbo_thread_t threads[TEST_RESERVATION_PRODUCERS] = {0};
      reservation_context contexts[TEST_RESERVATION_PRODUCERS] = {0};
      atomic_bool go;
      size_t index;
      size_t other;

      atomic_init(&go, false);
      check_equal(cnet_session_table_init(&table, TEST_RESERVATION_PRODUCERS), TURBO_OK);
      for (index = 0u; index < TEST_RESERVATION_PRODUCERS; ++index) {
        contexts[index].table = &table;
        contexts[index].go = &go;
        contexts[index].status = TURBO_EIO;
        check_equal(turbo_thread_create(&threads[index], reserve_concurrently, &contexts[index]),
                    TURBO_OK);
      }
      atomic_store_explicit(&go, true, memory_order_release);
      for (index = 0u; index < TEST_RESERVATION_PRODUCERS; ++index) {
        check_equal(turbo_thread_join(&threads[index]), TURBO_OK);
        check_equal(contexts[index].status, TURBO_OK);
        check_true(cnet_session_handle_valid(contexts[index].handle));
        for (other = 0u; other < index; ++other)
          check_not_equal(contexts[index].handle.slot, contexts[other].handle.slot);
      }
      for (index = 0u; index < TEST_RESERVATION_PRODUCERS; ++index)
        close_and_recycle(contexts[index].handle);
    }

    it("reserves a generation checked session in RESERVED state") {
      cnet_session_handle handle;
      cnet_session_state state = CNET_SESSION_FREE;

      check_equal(cnet_session_table_init(&table, 2u), TURBO_OK);
      handle = reserve_session();
      check_equal(cnet_session_table_state(&table, handle, &state), TURBO_OK);
      check_equal(state, CNET_SESSION_RESERVED);
      close_and_recycle(handle);
    }

    it("clears output when the fixed session table is full") {
      cnet_session_handle rejected = {UINT32_C(99), UINT32_C(77)};

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      cnet_session_handle accepted = reserve_session();
      check_equal(cnet_session_table_reserve(&table, &rejected), TURBO_ENOBUFS);
      expect_zero_handle(rejected);
      close_and_recycle(accepted);
    }

    it("invalidates a reservation when command publication is rejected") {
      cnet_session_handle rejected;
      cnet_session_handle replacement;
      cnet_session_state state = CNET_SESSION_FREE;

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      rejected = reserve_session();
      check_equal(cnet_session_table_release_reservation(&table, rejected), TURBO_OK);
      check_equal(cnet_session_table_state(&table, rejected, &state), TURBO_ENOENT);
      replacement = reserve_session();
      check_equal(replacement.slot, rejected.slot);
      check_not_equal(replacement.generation, rejected.generation);
      check_equal(cnet_session_table_release_reservation(&table, replacement), TURBO_OK);
    }
  }

  group("transitions") {
    it("accepts the resolving TCP and protocol handshake path") {
      static const cnet_session_state path[] = {
          CNET_SESSION_RESOLVING, CNET_SESSION_TRANSPORT_CONNECTING,
          CNET_SESSION_PROTOCOL_HANDSHAKING, CNET_SESSION_OPEN, CNET_SESSION_DRAINING};
      cnet_session_handle handle;
      cnet_session_state state = CNET_SESSION_FREE;
      size_t index;

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      handle = reserve_session();
      for (index = 0u; index < sizeof(path) / sizeof(path[0]); ++index) {
        check_equal(cnet_session_table_transition(&table, handle, path[index]), TURBO_OK);
        check_equal(cnet_session_table_state(&table, handle, &state), TURBO_OK);
        check_equal(state, path[index]);
      }
      check_equal(cnet_session_table_finish_close(&table, handle), TURBO_OK);
      {
        cnet_session_terminal terminal = {0};
        check_equal(cnet_session_table_take_terminal(&table, handle, &terminal), TURBO_OK);
      }
      check_equal(cnet_session_table_recycle(&table, handle), TURBO_OK);
    }

    it("rejects an invalid transition without changing state") {
      cnet_session_handle handle;
      cnet_session_state state = CNET_SESSION_FREE;

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      handle = reserve_session();
      check_equal(cnet_session_table_transition(&table, handle, CNET_SESSION_OPEN), TURBO_EPROTO);
      check_equal(cnet_session_table_state(&table, handle, &state), TURBO_OK);
      check_equal(state, CNET_SESSION_RESERVED);
      close_and_recycle(handle);
    }
  }

  group("terminal outcome") {
    it("rejects an unknown failure stage without changing state") {
      cnet_session_handle handle;
      cnet_session_state state = CNET_SESSION_FREE;

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      handle = reserve_session();
      check_equal(
          cnet_session_table_fail(&table, handle, TURBO_EIO, (cnet_session_stage)UINT32_C(255)),
          TURBO_EINVAL);
      check_equal(cnet_session_table_state(&table, handle, &state), TURBO_OK);
      check_equal(state, CNET_SESSION_RESERVED);
      close_and_recycle(handle);
    }

    it("preserves the first asynchronous failure") {
      cnet_session_handle handle;
      cnet_session_terminal terminal = {0};

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      handle = reserve_session();
      check_equal(
          cnet_session_table_fail(&table, handle, TURBO_ECONNREFUSED, CNET_SESSION_STAGE_CONNECT),
          TURBO_OK);
      check_equal(cnet_session_table_fail(&table, handle, TURBO_EIO, CNET_SESSION_STAGE_READ),
                  TURBO_EALREADY);
      check_equal(cnet_session_table_take_terminal(&table, handle, &terminal), TURBO_OK);
      check_equal(terminal.kind, CNET_SESSION_TERMINAL_FAILED);
      check_equal(terminal.status, TURBO_ECONNREFUSED);
      check_equal(terminal.stage, CNET_SESSION_STAGE_CONNECT);
      check_equal(cnet_session_table_recycle(&table, handle), TURBO_OK);
    }

    it("publishes one terminal notification") {
      cnet_session_handle handle;
      cnet_session_terminal terminal = {0};

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      handle = reserve_session();
      check_equal(cnet_session_table_begin_close(&table, handle), TURBO_OK);
      check_equal(cnet_session_table_finish_close(&table, handle), TURBO_OK);
      check_equal(cnet_session_table_take_terminal(&table, handle, &terminal), TURBO_OK);
      check_equal(terminal.kind, CNET_SESSION_TERMINAL_CLOSED);
      check_equal(terminal.status, TURBO_OK);
      check_equal(cnet_session_table_take_terminal(&table, handle, &terminal), TURBO_EALREADY);
      check_equal(cnet_session_table_recycle(&table, handle), TURBO_OK);
    }
  }

  group("recycling") {
    it("increments generation before reuse and rejects the stale handle") {
      cnet_session_handle old_handle;
      cnet_session_handle new_handle;
      cnet_session_terminal terminal = {0};
      cnet_session_state state = CNET_SESSION_FREE;

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      old_handle = reserve_session();
      check_equal(cnet_session_table_fail(&table, old_handle, TURBO_EIO, CNET_SESSION_STAGE_READ),
                  TURBO_OK);
      check_equal(cnet_session_table_take_terminal(&table, old_handle, &terminal), TURBO_OK);
      check_equal(cnet_session_table_recycle(&table, old_handle), TURBO_OK);
      new_handle = reserve_session();
      check_equal(new_handle.slot, old_handle.slot);
      check_not_equal(new_handle.generation, old_handle.generation);
      check_equal(cnet_session_table_state(&table, old_handle, &state), TURBO_ENOENT);
      check_equal(cnet_session_table_state(&table, new_handle, &state), TURBO_OK);
      close_and_recycle(new_handle);
    }

    it("refuses destroy while a live session remains") {
      cnet_session_handle handle;

      check_equal(cnet_session_table_init(&table, 1u), TURBO_OK);
      handle = reserve_session();
      check_equal(cnet_session_table_destroy(&table), TURBO_EBUSY);
      check_not_null(table.impl);
      check_equal(
          cnet_session_table_fail(&table, handle, TURBO_ECANCELED, CNET_SESSION_STAGE_SHUTDOWN),
          TURBO_OK);
      {
        cnet_session_terminal terminal = {0};
        check_equal(cnet_session_table_take_terminal(&table, handle, &terminal), TURBO_OK);
      }
      check_equal(cnet_session_table_recycle(&table, handle), TURBO_OK);
    }
  }
}
