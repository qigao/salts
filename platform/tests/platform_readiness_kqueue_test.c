#include <salts/error_codes.h>
#include <salts/readiness.h>
#include <salts/thread.h>

#include "tinytest.h"

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

static const uint64_t KQUEUE_TEST_TIMEOUT_NS = UINT64_C(5000000000);
static const uint64_t KQUEUE_TEST_QUIET_NS = UINT64_C(50000000);

typedef struct kqueue_probe {
  salts_mutex_t gate;
  salts_cond_t changed;
  salts_readiness_events events;
  int status;
  int called;
} kqueue_probe;

static void kqueue_probe_callback(void *user,
                                  salts_readiness_events events,
                                  int status) {
  kqueue_probe *probe = (kqueue_probe *)user;
  salts_mutex_lock(&probe->gate);
  probe->events = events;
  probe->status = status;
  probe->called = 1;
  salts_cond_signal(&probe->changed);
  salts_mutex_unlock(&probe->gate);
}

spec("Platform kqueue readiness") {
  it("delivers one-shot socket readability and shuts down exactly") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_registration registration = {0};
    salts_readiness_config config = {1u, 1u};
    kqueue_probe probe = {0};
    int sockets[2] = {-1, -1};
    const unsigned char byte = 0x5au;
    int wait_status = SALTS_OK;

    salts_mutex_init(&probe.gate);
    salts_cond_init(&probe.changed);
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, sockets[0], &registration),
                SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    kqueue_probe_callback, &probe), SALTS_OK);
    check_equal((int)write(sockets[1], &byte, sizeof(byte)), 1);

    salts_mutex_lock(&probe.gate);
    while (!probe.called && wait_status == SALTS_OK)
      wait_status = salts_cond_timedwait(&probe.changed, &probe.gate,
                                         KQUEUE_TEST_TIMEOUT_NS);
    salts_mutex_unlock(&probe.gate);
    check_equal(wait_status, SALTS_OK);
    check_equal(probe.status, SALTS_OK);
    check_true((probe.events & SALTS_READINESS_EVENT_READ) != 0u);
    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
    salts_cond_destroy(&probe.changed);
    salts_mutex_destroy(&probe.gate);
  }

  it("does not deliver read readiness for a hangup-only arm") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_registration registration = {0};
    salts_readiness_config config = {1u, 1u};
    kqueue_probe probe = {0};
    int sockets[2] = {-1, -1};
    const unsigned char byte = 0x6bu;
    int wait_status;

    salts_mutex_init(&probe.gate);
    salts_cond_init(&probe.changed);
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, sockets[0], &registration),
                SALTS_OK);
    check_equal(salts_readiness_arm(&registration,
                                    SALTS_READINESS_EVENT_HANGUP,
                                    kqueue_probe_callback, &probe), SALTS_OK);
    check_equal((int)write(sockets[1], &byte, sizeof(byte)), 1);

    salts_mutex_lock(&probe.gate);
    do {
      wait_status = salts_cond_timedwait(&probe.changed, &probe.gate,
                                         KQUEUE_TEST_QUIET_NS);
    } while (!probe.called && wait_status == SALTS_OK);
    check_equal(probe.called, 0);
    salts_mutex_unlock(&probe.gate);
    check_equal(wait_status, -ETIMEDOUT);

    check_equal(shutdown(sockets[1], SHUT_WR), 0);
    salts_mutex_lock(&probe.gate);
    wait_status = SALTS_OK;
    while (!probe.called && wait_status == SALTS_OK)
      wait_status = salts_cond_timedwait(&probe.changed, &probe.gate,
                                         KQUEUE_TEST_TIMEOUT_NS);
    salts_mutex_unlock(&probe.gate);
    check_equal(wait_status, SALTS_OK);
    check_equal(probe.status, SALTS_OK);
    check_equal(probe.events, SALTS_READINESS_EVENT_HANGUP);

    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(sockets[0]);
    (void)close(sockets[1]);
    salts_cond_destroy(&probe.changed);
    salts_mutex_destroy(&probe.gate);
  }
}
