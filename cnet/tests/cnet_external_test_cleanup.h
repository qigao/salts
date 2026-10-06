#ifndef CNET_EXTERNAL_TEST_CLEANUP_H
#define CNET_EXTERNAL_TEST_CLEANUP_H

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <tinytest.h>

enum { EXTERNAL_TEST_CLIENTS = 2, EXTERNAL_TEST_BATCH = 16,
       EXTERNAL_TEST_WAIT_MS = 25, EXTERNAL_TEST_DRAIN_MS = 5000 };

typedef struct external_test_client {
  cnet_client *client;
  cnet_connection connection;
} external_test_client;

/* Borrowed fixture handles stay alive until all callbacks have drained. */
typedef struct external_test_cleanup {
  native_io_backend *backend;
  cnet_listener *listener;
  external_test_client clients[EXTERNAL_TEST_CLIENTS];
} external_test_cleanup;

static void cleanup_external_test(external_test_cleanup *fixture) {
  bool stopped[EXTERNAL_TEST_CLIENTS] = {false};
  const uint64_t deadline = cmeta_monotonic_ms() + EXTERNAL_TEST_DRAIN_MS;
  for (size_t i = 0; i < EXTERNAL_TEST_CLIENTS; ++i) {
    external_test_client *owner = &fixture->clients[i];
    if (owner->client == NULL || owner->client->impl == NULL) {
      stopped[i] = true;
    } else if (owner->connection.slot != 0u) {
      const int status = cnet_close(owner->client, owner->connection);
      check_warn(status == SALTS_OK || status == SALTS_ENOENT || status == SALTS_EALREADY);
    }
  }

  for (;;) {
    bool drained = true;
    bool listener_pending = false;
    if (fixture->listener->impl != NULL) {
      const int status = cnet_listener_close(fixture->listener);
      check_warn(status == SALTS_OK || status == SALTS_EALREADY || status == SALTS_EBUSY);
      listener_pending = status == SALTS_EBUSY;
      drained = !listener_pending;
    }
    for (size_t i = 0; i < EXTERNAL_TEST_CLIENTS; ++i) {
      if (stopped[i]) continue;
      cnet_client *client = fixture->clients[i].client;
      int status = cnet_client_stop_external(client);
      if (status == SALTS_OK || status == SALTS_EALREADY) {
        stopped[i] = true;
      } else {
        size_t events = 0u;
        check_warn(status == SALTS_EBUSY);
        drained = false;
        check_warn(cnet_client_advance_external(client, &events) == SALTS_OK);
      }
    }
    if (drained) break;
    if (cmeta_monotonic_ms() >= deadline) {
      check_warn(false, "external fixture did not drain before its deadline");
      return;
    }
    native_io_completion completions[EXTERNAL_TEST_BATCH];
    size_t count = 0u;
    const int status = native_io_backend_observe(
        fixture->backend, completions, EXTERNAL_TEST_BATCH, EXTERNAL_TEST_WAIT_MS, &count);
    check_warn(status == SALTS_OK || status == SALTS_ETIMEDOUT);
    if (status != SALTS_OK && status != SALTS_ETIMEDOUT) return;
    for (size_t event = 0; event < count; ++event) {
      bool consumed = false;
      if (listener_pending) {
        check_warn(cnet_listener_route_external_completion(
                       fixture->listener, &completions[event], &consumed) == SALTS_OK);
      }
      for (size_t i = 0; i < EXTERNAL_TEST_CLIENTS && !consumed; ++i) {
        if (stopped[i]) continue;
        size_t events = 0u;
        check_warn(cnet_client_route_external_completion(
                       fixture->clients[i].client, &completions[event],
                       &consumed, &events) == SALTS_OK);
      }
      check_warn(consumed);
    }
  }

  if (fixture->listener->impl != NULL)
    check_warn(cnet_listener_destroy(fixture->listener) == SALTS_OK);
  for (size_t i = 0; i < EXTERNAL_TEST_CLIENTS; ++i) {
    if (fixture->clients[i].client != NULL)
      check_warn(cnet_client_destroy(fixture->clients[i].client) == SALTS_OK);
  }
  if (fixture->backend->impl != NULL) {
    const int status = native_io_backend_close(fixture->backend);
    check_warn(status == SALTS_OK || status == SALTS_EALREADY);
    check_warn(native_io_backend_destroy(fixture->backend) == SALTS_OK);
  }
}

#endif
