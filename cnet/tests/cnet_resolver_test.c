#include "cnet_module.h"
#include "cnet_resolver.h"
#include "tinytest.h"

#include <turbo/clock.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
#endif

enum { CNET_RESOLVER_TEST_TIMEOUT_MS = 5000 };

static void cnet_resolver_test_wake(void *context) { atomic_fetch_add((atomic_uint *)context, 1u); }

static int cnet_resolver_test_wait(cnet_resolver *resolver, cnet_resolver_result *out_result) {
  const uint64_t deadline = turbo_monotonic_ms() + CNET_RESOLVER_TEST_TIMEOUT_MS;
  for (;;) {
    const int status = cnet_resolver_take(resolver, out_result);
    if (status != TURBO_ETIMEDOUT) return status;
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
    turbo_sleep_ms(1u);
  }
}

spec("CNet bounded asynchronous resolver") {
  it("requires module lifetime and pins global cleanup while alive") {
    cnet_resolver resolver = {0};
    const cnet_resolver_config config = {.query_capacity = 1u};

    check_equal(cnet_resolver_init(&resolver, &config), TURBO_ESHUTDOWN);
    check_equal(cnet_module_init(), TURBO_OK);
    check_equal(cnet_resolver_init(&resolver, &config), TURBO_OK);
    check_equal(cnet_module_shutdown(), TURBO_EBUSY);
    check_equal(cnet_resolver_close(&resolver, CNET_RESOLVER_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_resolver_destroy(&resolver), TURBO_OK);
    check_equal(cnet_module_shutdown(), TURBO_OK);
  }

  it("copies one localhost result into a generation checked mailbox") {
    atomic_uint wakes = 0u;
    cnet_resolver resolver = {0};
    const cnet_resolver_config config = {
        .query_capacity = 2u, .wake = cnet_resolver_test_wake, .wake_context = &wakes};
    cnet_resolver_query query = {0};
    cnet_resolver_result result = {0};
    struct sockaddr_storage address;
    uint16_t port = 0u;

    check_equal(cnet_module_init(), TURBO_OK);
    check_equal(cnet_resolver_init(&resolver, &config), TURBO_OK);
    check_equal(cnet_resolver_submit(&resolver, "localhost", 443u, SOCK_STREAM, 77u, &query),
                TURBO_OK);
    check_true(cnet_resolver_query_valid(query));
    check_equal(cnet_resolver_test_wait(&resolver, &result), TURBO_OK);
    check_equal(result.query.slot, query.slot);
    check_equal(result.query.generation, query.generation);
    check_equal(result.user_data, (uintptr_t)77u);
    check_equal(result.status, TURBO_OK);
    check_true(result.address_length >= sizeof(struct sockaddr_in));
    check_true(result.address_length <= sizeof(address));
    memset(&address, 0, sizeof(address));
    memcpy(&address, result.address, result.address_length);
    if (address.ss_family == AF_INET)
      port = ntohs(((const struct sockaddr_in *)&address)->sin_port);
    else if (address.ss_family == AF_INET6)
      port = ntohs(((const struct sockaddr_in6 *)&address)->sin6_port);
    check_equal(port, 443u);
    check_true(atomic_load(&wakes) >= 1u);

    check_equal(cnet_resolver_close(&resolver, CNET_RESOLVER_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_resolver_take(&resolver, &result), TURBO_EOF);
    check_equal(cnet_resolver_destroy(&resolver), TURBO_OK);
    check_equal(cnet_module_shutdown(), TURBO_OK);
  }

  it("preserves cancellation and bounded admission without backend fallback") {
    atomic_uint wakes = 0u;
    cnet_resolver resolver = {0};
    const cnet_resolver_config config = {
        .query_capacity = 1u, .wake = cnet_resolver_test_wake, .wake_context = &wakes};
    cnet_resolver_query query = {0};
    cnet_resolver_query rejected = {9u, 9u};
    cnet_resolver_result result = {0};

    check_equal(cnet_module_init(), TURBO_OK);
    check_equal(cnet_resolver_init(&resolver, &config), TURBO_OK);
    check_equal(cnet_resolver_submit(&resolver, "localhost", 53u, SOCK_DGRAM, 91u, &query),
                TURBO_OK);
    check_equal(cnet_resolver_submit(&resolver, "localhost", 53u, SOCK_DGRAM, 92u, &rejected),
                TURBO_ENOBUFS);
    check_false(cnet_resolver_query_valid(rejected));
    check_equal(cnet_resolver_cancel(&resolver, query), TURBO_OK);
    check_equal(cnet_resolver_test_wait(&resolver, &result), TURBO_OK);
    check_equal(result.query.slot, query.slot);
    check_equal(result.query.generation, query.generation);
    check_equal(result.status, TURBO_EAI_CANCELED);
    check_equal(result.user_data, (uintptr_t)91u);

    check_equal(cnet_resolver_close(&resolver, CNET_RESOLVER_TEST_TIMEOUT_MS), TURBO_OK);
    rejected = (cnet_resolver_query){9u, 9u};
    check_equal(cnet_resolver_submit(&resolver, "localhost", 80u, SOCK_STREAM, 0u, &rejected),
                TURBO_ESHUTDOWN);
    check_false(cnet_resolver_query_valid(rejected));
    check_equal(cnet_resolver_destroy(&resolver), TURBO_OK);
    check_equal(cnet_module_shutdown(), TURBO_OK);
  }
}
