#include "cnet_test_internal.h"
#include <cnet/cnet.h>
#include <salts/clock.h>
#include <string.h>
#include <tinytest.h>

enum { TEST_BATCH = 16, TEST_TIMEOUT_MS = 5000, TEST_BYTES = 32 };

typedef struct datagram_probe {
  size_t sends;
  size_t receives;
  int status;
  uint64_t tag;
  unsigned char data[TEST_BYTES];
  bool rearm;
  bool stable;
} datagram_probe;

static native_io_backend backend;
static cnet_datagram datagrams[2];
static datagram_probe probes[2];
/* Observed terminals survive a fatal assertion until fixture teardown routes them. */
static native_io_completion held[TEST_BATCH];
static size_t held_count;
#if defined(__linux__)
static cnet_datagram flow_source;
static datagram_probe flow_probe;
#endif

static native_io_backend_kind backend_kind(void) {
#if defined(CNET_TEST_IO_URING)
  return NATIVE_IO_BACKEND_IO_URING;
#elif defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__) || defined(__FreeBSD__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static void received(void *user, cnet_datagram *datagram, const cnet_datagram_peer *peer,
                     const cnet_receive_view *view) {
  datagram_probe *probe = (datagram_probe *)user;
  (void)peer;
  ++probe->receives;
  check_warn(view->size <= sizeof(probe->data));
  if (view->size > sizeof(probe->data)) return;
  memcpy(probe->data, view->data, view->size);
  if (probe->rearm) {
    bool stopped = true;
    size_t events = 1u;
    probe->rearm = false;
    check_warn(cnet_datagram_receive(datagram, 1u) == SALTS_OK);
    check_warn(cnet_datagram_advance_external(datagram, &events) == SALTS_EBUSY);
    check_warn(cnet_datagram_stop_external(datagram, &stopped) == SALTS_EBUSY);
    check_warn(!stopped);
    check_warn(cnet_datagram_destroy(datagram) == SALTS_EBUSY);
    probe->stable = memcmp(probe->data, view->data, view->size) == 0;
  }
}

static void sent(void *user, cnet_datagram *datagram, const cnet_datagram_peer *peer, size_t size,
                 int status, uint64_t tag) {
  datagram_probe *probe = (datagram_probe *)user;
  (void)datagram;
  (void)peer;
  (void)size;
  ++probe->sends;
  probe->status = status;
  probe->tag = tag;
}

static cnet_datagram_config config_for(size_t index) {
  cnet_datagram_config config = CNET_DATAGRAM_CONFIG_INIT;
  config.backend = backend_kind();
  config.host = "127.0.0.1";
  config.send_capacity = 2u;
  config.request_capacity = 3u;
  config.completion_batch_capacity = 3u;
  config.max_datagram_bytes = TEST_BYTES;
  config.receive_buffer_bytes = TEST_BYTES;
  config.observer = (cnet_datagram_observer){received, sent, &probes[index]};
  return config;
}

static cnet_datagram_peer peer_for(size_t index) {
  cnet_datagram_peer peer = {0};
  peer.family = CNET_DATAGRAM_ADDRESS_IPV4;
  peer.address[0] = 127u;
  peer.address[3] = 1u;
  check_warn(cnet_datagram_port(&datagrams[index], &peer.port) == SALTS_OK);
  return peer;
}

static int route_held(void) {
  int first_status = SALTS_OK;
  for (size_t event = 0; event < held_count; ++event) {
    bool consumed = false;
    /* Reverse order exercises local tag collisions before the real owner. */
    for (size_t index = 2u; index != 0u && !consumed; --index) {
      size_t events = 0u;
      int status;
      if (datagrams[index - 1u].impl == NULL) continue;
      status = cnet_datagram_route_external_completion(&datagrams[index - 1u], &held[event],
                                                       &consumed, &events);
      if (status != SALTS_OK && first_status == SALTS_OK) first_status = status;
    }
    if (!consumed && first_status == SALTS_OK) first_status = SALTS_EPROTO;
  }
  held_count = 0u;
  return first_status;
}

static int observe_more(void) {
  size_t count = 0u;
  int status;
  if (held_count == TEST_BATCH) return SALTS_ENOBUFS;
  status =
      native_io_backend_observe(&backend, held + held_count, TEST_BATCH - held_count, 20u, &count);
  held_count += count;
  return status == SALTS_ETIMEDOUT ? SALTS_OK : status;
}

static void drain_until(size_t sends, size_t receives) {
  const uint64_t deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (probes[0].sends + probes[1].sends < sends ||
         probes[0].receives + probes[1].receives < receives) {
    check_equal(observe_more(), SALTS_OK);
    check_equal(route_held(), SALTS_OK);
    check(cmeta_monotonic_ms() < deadline);
  }
}

spec("CNet external datagram progress") {
  before_each() {
    native_io_backend_config native_config = {backend_kind(), 2u, TEST_BATCH, TEST_BATCH};
    memset(probes, 0, sizeof(probes));
    held_count = 0u;
    check_equal(native_io_backend_init(&backend, &native_config), SALTS_OK);
    for (size_t index = 0; index < 2u; ++index) {
      cnet_datagram_config config = config_for(index);
      check_equal(cnet_datagram_init_external(&datagrams[index], &config, &backend), SALTS_OK);
    }
  }

  after_each() {
#if defined(__linux__)
    if (flow_source.impl != NULL) {
      check_warn(cnet_datagram_stop(&flow_source, TEST_TIMEOUT_MS) == SALTS_OK);
      check_warn(cnet_datagram_destroy(&flow_source) == SALTS_OK);
    }
#endif
    const uint64_t deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
    bool all_stopped = false;
    while (!all_stopped && cmeta_monotonic_ms() < deadline) {
      all_stopped = true;
      for (size_t index = 0; index < 2u; ++index) {
        bool stopped = false;
        if (datagrams[index].impl == NULL) continue;
        (void)cnet_datagram_stop_external(&datagrams[index], &stopped);
        if (stopped) check_warn(cnet_datagram_destroy(&datagrams[index]) == SALTS_OK);
        else all_stopped = false;
      }
      if (!all_stopped) {
        if (held_count != 0u) (void)route_held();
        check_warn(observe_more() == SALTS_OK);
        (void)route_held();
      }
    }
    check_warn(all_stopped);
    if (all_stopped) {
      check_warn(native_io_backend_close(&backend) == SALTS_OK);
      check_warn(native_io_backend_destroy(&backend) == SALTS_OK);
    }
  }

#if defined(__linux__)
  it("distributes distinct source flows across a shared concrete reuseport bind") {
    enum { FLOWS = 64 };
    cnet_datagram_peer destination = {0};
    for (size_t i = 0; i < 2; ++i) {
      bool stopped = false;
      check_equal(cnet_datagram_stop_external(&datagrams[i], &stopped), SALTS_OK);
      check_true(stopped);
      check_equal(cnet_datagram_destroy(&datagrams[i]), SALTS_OK);
      cnet_datagram_config config = config_for(i);
      config.reuse_port = 1;
      if (i != 0) config.port = destination.port;
      check_equal(cnet_datagram_init_external(&datagrams[i], &config, &backend), SALTS_OK);
      if (i == 0) destination = peer_for(0);
      check_equal(cnet_datagram_receive(&datagrams[i], FLOWS), SALTS_OK);
    }
    for (size_t flow = 0; flow < FLOWS; ++flow) {
      cnet_datagram_config config = config_for(0);
      config.observer = (cnet_datagram_observer){received, sent, &flow_probe};
      memset(&flow_probe, 0, sizeof(flow_probe));
      check_equal(cnet_datagram_init(&flow_source, &config), SALTS_OK);
      check_equal(cnet_datagram_send(&flow_source, &destination, "flow", 4, flow), SALTS_OK);
      uint64_t deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
      while (flow_probe.sends == 0) {
        size_t events;
        check_equal(cnet_datagram_poll(&flow_source, 1, &events), SALTS_OK);
        check(cmeta_monotonic_ms() < deadline);
      }
      check_equal(flow_probe.status, SALTS_OK);
      drain_until(0, flow + 1);
      check_equal(cnet_datagram_stop(&flow_source, TEST_TIMEOUT_MS), SALTS_OK);
      check_equal(cnet_datagram_destroy(&flow_source), SALTS_OK);
    }
    check_equal(probes[0].receives + probes[1].receives, (size_t)FLOWS);
    check_greater(probes[0].receives, 0u);
    check_greater(probes[1].receives, 0u);
  }
#endif

  it("routes colliding local tags and preserves copied sends and callback views") {
    const unsigned char expected[] = {1u, 3u, 5u, 7u};
    unsigned char payload[sizeof(expected)];
    for (size_t index = 0u; index < 2u; ++index) {
      cnet_datagram_peer peer = peer_for(index);
      memcpy(payload, expected, sizeof(payload));
      probes[index].rearm = true;
      check_equal(cnet_datagram_receive(&datagrams[index], 1u), SALTS_OK);
      check_equal(cnet_datagram_send(&datagrams[index], &peer, payload, sizeof(payload), index),
                  SALTS_OK);
      check_equal(cnet_datagram_send(&datagrams[index], &peer, payload, sizeof(payload), index),
                  SALTS_OK);
      check_equal(cnet_datagram_send(&datagrams[index], &peer, payload, sizeof(payload), index),
                  SALTS_ENOBUFS);
      memset(payload, 0, sizeof(payload));
    }
    drain_until(4u, 4u);
    for (size_t index = 0; index < 2u; ++index) {
      check_equal(probes[index].sends, 2u);
      check_equal(probes[index].receives, 2u);
      check_equal(probes[index].status, SALTS_OK);
      check_equal(probes[index].tag, (uint64_t)index);
      check_equal(probes[index].data, expected, sizeof(expected));
      check_true(probes[index].stable);
    }
  }

  it("keeps observed but unrouted requests alive during stop and leaves neighbors usable") {
    const char data[] = "owned";
    cnet_datagram_peer peer = peer_for(0u);
    native_io_completion duplicate;
    native_io_backend_stats stats;
    bool stopped = true;
    size_t events = 99u;
    bool consumed = true;
    const uint64_t deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
    check_equal(cnet_datagram_receive(&datagrams[0], 1u), SALTS_OK);
    check_equal(cnet_datagram_send(&datagrams[0], &peer, data, sizeof(data), 71u), SALTS_OK);
    while (held_count < 2u) {
      check_equal(observe_more(), SALTS_OK);
      check(cmeta_monotonic_ms() < deadline);
    }
    duplicate = held[0];
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_EBUSY);
    check_false(stopped);
    check_equal(cnet_datagram_destroy(&datagrams[0]), SALTS_EBUSY);
    check_equal(probes[0].sends, 0u);
    peer = peer_for(1u);
    check_equal(cnet_datagram_receive(&datagrams[1], 1u), SALTS_OK);
    check_equal(cnet_datagram_send(&datagrams[1], &peer, data, sizeof(data), 72u), SALTS_OK);
    /* The host has freed native slots, but the first owner still retains its
     * old generation. A neighbor may reuse those slots before old terminals route. */
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_EBUSY);
    check_false(stopped);
    while (held_count < 4u) {
      check_equal(observe_more(), SALTS_OK);
      check(cmeta_monotonic_ms() < deadline);
    }
    {
      bool reused = false;
      for (size_t old = 0u; old < 2u; ++old)
        for (size_t current = 2u; current < 4u; ++current)
          if (held[old].request.slot == held[current].request.slot) {
            reused = true;
            check_not_equal(held[old].request.generation, held[current].request.generation);
          }
      check_true(reused);
      check_equal(
          cnet_datagram_route_external_completion(&datagrams[1], &duplicate, &consumed, &events),
          SALTS_OK);
      check_false(consumed);
    }
    check_equal(route_held(), SALTS_OK);
    check_equal(
        cnet_datagram_route_external_completion(&datagrams[0], &duplicate, &consumed, &events),
        SALTS_OK);
    check_false(consumed);
    check_equal(events, 0u);
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_OK);
    check_true(stopped);
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_OK);
    check_equal(cnet_datagram_destroy(&datagrams[0]), SALTS_OK);
    check_equal(cnet_datagram_destroy(&datagrams[0]), SALTS_OK);
    check_equal(probes[0].receives, 0u);
    check_equal(probes[0].sends, 1u);
    check_equal(probes[0].status, SALTS_OK);
    drain_until(2u, 1u);
    check_true(native_io_backend_get_stats(&backend, &stats));
    check_true(stats.admission_open);
    check_equal(stats.endpoint_count, 1u);
  }

  it("claims malformed owned terminals and still settles the rest of the batch") {
    const char data[] = "batch";
    const uint64_t deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
    for (size_t index = 0; index < 2u; ++index) {
      cnet_datagram_peer peer = peer_for(index);
      check_equal(cnet_datagram_send(&datagrams[index], &peer, data, sizeof(data), index),
                  SALTS_OK);
    }
    while (held_count < 2u) {
      check_equal(observe_more(), SALTS_OK);
      check(cmeta_monotonic_ms() < deadline);
    }
    held[0].user_data = UINTPTR_MAX;
    check_equal(route_held(), SALTS_EPROTO);
    check_equal(probes[0].sends + probes[1].sends, 2u);
    check((probes[0].status == SALTS_EPROTO && probes[1].status == SALTS_OK) ||
          (probes[1].status == SALTS_EPROTO && probes[0].status == SALTS_OK));
  }

  it("rejects foreign and stale identities without touching the input") {
    native_io_completion completion = {0};
    native_io_completion before;
    bool consumed = true;
    size_t events = 99u;
    completion.request = (native_io_request){UINT32_MAX, 1u};
    completion.user_data = 1u;
    before = completion;
    check_equal(
        cnet_datagram_route_external_completion(&datagrams[0], &completion, &consumed, &events),
        SALTS_OK);
    check_false(consumed);
    check_equal(events, 0u);
    check_equal(&completion, &before, sizeof(before));
    check_equal(cnet_datagram_poll(&datagrams[0], 0u, &events), SALTS_ENOTSUP);
    check_equal(cnet_datagram_stop(&datagrams[0], 0u), SALTS_ENOTSUP);
  }

  it("fails initialization without closing a full or mismatched host backend") {
    cnet_datagram rejected = {0};
    cnet_datagram_config config = config_for(0u);
    native_io_backend_stats stats;
    check_equal(cnet_datagram_init_external(&rejected, &config, &backend), SALTS_ENOBUFS);
    check_null(rejected.impl);
    config.backend = (native_io_backend_kind)0;
    check_equal(cnet_datagram_init_external(&rejected, &config, &backend), SALTS_EINVAL);
    config = config_for(0u);
    config.send_capacity = SIZE_MAX;
    config.request_capacity = SIZE_MAX;
    check_not_equal(cnet_datagram_init_external(&rejected, &config, &backend), SALTS_OK);
    check_true(native_io_backend_get_stats(&backend, &stats));
    check_true(stats.admission_open);
    check_equal(stats.endpoint_count, 2u);
  }

  it("cancels pending receives without receive callbacks or backend shutdown") {
    bool stopped = true;
    size_t events = 1u;
    check_equal(cnet_datagram_receive(&datagrams[0], 1u), SALTS_OK);
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_EBUSY);
    check_false(stopped);
    check_equal(cnet_datagram_advance_external(&datagrams[0], &events), SALTS_OK);
    check_equal(events, 0u);
    check_equal(cnet_datagram_receive(&datagrams[0], 1u), SALTS_ESHUTDOWN);
    check_equal(probes[0].receives, 0u);
  }

  it("retains a real cancellation error independently of eventual quiescence") {
    bool stopped;
    const uint64_t deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
    check_equal(cnet_datagram_receive(&datagrams[0], 1u), SALTS_OK);
    check_equal(cnet_test_datagram_fail_next_cancel(&datagrams[0], SALTS_EIO), SALTS_OK);
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_EIO);
    check_false(stopped);
    check_equal(cnet_datagram_destroy(&datagrams[0]), SALTS_EBUSY);
    do {
      check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_EIO);
      if (stopped) break;
      check_equal(observe_more(), SALTS_OK);
      check_equal(route_held(), SALTS_OK);
      check(cmeta_monotonic_ms() < deadline);
    } while (!stopped);
    check_equal(cnet_datagram_destroy(&datagrams[0]), SALTS_OK);
    check_equal(probes[0].receives, 0u);
  }

  it("retains failed endpoint release for retry without closing the host backend") {
    bool stopped;
    native_io_backend_stats stats;
    check_equal(cnet_test_datagram_fail_next_release(&datagrams[0], SALTS_EIO), SALTS_OK);
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_EIO);
    check_false(stopped);
    check_equal(cnet_datagram_destroy(&datagrams[0]), SALTS_EBUSY);
    check_true(native_io_backend_get_stats(&backend, &stats));
    check_equal(stats.endpoint_count, 2u);
    check_true(stats.admission_open);
    check_equal(cnet_datagram_stop_external(&datagrams[0], &stopped), SALTS_EIO);
    check_true(stopped);
    check_true(native_io_backend_get_stats(&backend, &stats));
    check_equal(stats.endpoint_count, 1u);
    check_equal(cnet_datagram_destroy(&datagrams[0]), SALTS_OK);
    {
      cnet_datagram_peer peer = peer_for(1u);
      check_equal(cnet_datagram_receive(&datagrams[1], 1u), SALTS_OK);
      check_equal(cnet_datagram_send(&datagrams[1], &peer, "x", 1u, 1u), SALTS_OK);
      drain_until(1u, 1u);
    }
  }
}
