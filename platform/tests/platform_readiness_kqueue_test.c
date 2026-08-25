#include <turbo/error_codes.h>
#include <turbo/readiness.h>
#include <turbo/thread.h>

#include "tinytest.h"

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

static const uint64_t KQUEUE_TEST_TIMEOUT_NS = UINT64_C(5000000000);
static const uint64_t KQUEUE_TEST_QUIET_NS = UINT64_C(50000000);

typedef struct kqueue_probe {
  turbo_mutex_t gate;
  turbo_cond_t changed;
  turbo_readiness_events events;
  int status;
  int called;
} kqueue_probe;

static void kqueue_probe_callback(void *user,
                                  turbo_readiness_events events,
                                  int status) {
  kqueue_probe *probe = (kqueue_probe *)user;
  turbo_mutex_lock(&probe->gate);
  probe->events = events;
  probe->status = status;
  probe->called = 1;
  turbo_cond_signal(&probe->changed);
  turbo_mutex_unlock(&probe->gate);
}

spec("Platform kqueue readiness") {
  it("delivers one-shot socket readability and shuts down exactly") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    turbo_readiness_config config = {1u, 1u};
    kqueue_probe probe = {0};
    int sockets[2] = {-1, -1};
    const unsigned char byte = 0x5au;
    int wait_status = TURBO_OK;

    turbo_mutex_init(&probe.gate);
    turbo_cond_init(&probe.changed);
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, sockets[0], &registration),
                TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                    kqueue_probe_callback, &probe), TURBO_OK);
    check_equal((int)write(sockets[1], &byte, sizeof(byte)), 1);

    turbo_mutex_lock(&probe.gate);
    while (!probe.called && wait_status == TURBO_OK)
      wait_status = turbo_cond_timedwait(&probe.changed, &probe.gate,
                                         KQUEUE_TEST_TIMEOUT_NS);
    turbo_mutex_unlock(&probe.gate);
    check_equal(wait_status, TURBO_OK);
    check_equal(probe.status, TURBO_OK);
    check_true((probe.events & TURBO_READINESS_EVENT_READ) != 0u);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
    turbo_cond_destroy(&probe.changed);
    turbo_mutex_destroy(&probe.gate);
  }

  it("does not deliver read readiness for a hangup-only arm") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    turbo_readiness_config config = {1u, 1u};
    kqueue_probe probe = {0};
    int sockets[2] = {-1, -1};
    const unsigned char byte = 0x6bu;
    int wait_status;

    turbo_mutex_init(&probe.gate);
    turbo_cond_init(&probe.changed);
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, sockets[0], &registration),
                TURBO_OK);
    check_equal(turbo_readiness_arm(&registration,
                                    TURBO_READINESS_EVENT_HANGUP,
                                    kqueue_probe_callback, &probe), TURBO_OK);
    check_equal((int)write(sockets[1], &byte, sizeof(byte)), 1);

    turbo_mutex_lock(&probe.gate);
    do {
      wait_status = turbo_cond_timedwait(&probe.changed, &probe.gate,
                                         KQUEUE_TEST_QUIET_NS);
    } while (!probe.called && wait_status == TURBO_OK);
    check_equal(probe.called, 0);
    turbo_mutex_unlock(&probe.gate);
    check_equal(wait_status, -ETIMEDOUT);

    check_equal(shutdown(sockets[1], SHUT_WR), 0);
    turbo_mutex_lock(&probe.gate);
    wait_status = TURBO_OK;
    while (!probe.called && wait_status == TURBO_OK)
      wait_status = turbo_cond_timedwait(&probe.changed, &probe.gate,
                                         KQUEUE_TEST_TIMEOUT_NS);
    turbo_mutex_unlock(&probe.gate);
    check_equal(wait_status, TURBO_OK);
    check_equal(probe.status, TURBO_OK);
    check_equal(probe.events, TURBO_READINESS_EVENT_HANGUP);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(sockets[0]);
    (void)close(sockets[1]);
    turbo_cond_destroy(&probe.changed);
    turbo_mutex_destroy(&probe.gate);
  }
}
