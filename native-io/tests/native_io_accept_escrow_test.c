#include "../src/native_io_accept_escrow.h"

#include <salts/error_codes.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct retire_probe {
    uintptr_t retired[8];
    size_t count;
    int status;
} retire_probe;

static int retire_transport(
    void *context,
    uintptr_t transport) {
    retire_probe *probe = (retire_probe *)context;

    assert(probe != NULL);
    assert(probe->count <
           sizeof(probe->retired) / sizeof(probe->retired[0]));
    probe->retired[probe->count++] = transport;
    return probe->status;
}

static native_io_request request(
    uint32_t slot,
    uint32_t generation) {
    native_io_request value;
    value.slot = slot;
    value.generation = generation;
    return value;
}

static void test_take_consumes_once(void) {
    native_io_accept_escrow escrow = {0};
    retire_probe probe = {0};
    uintptr_t transport = 0u;

    assert(native_io_accept_escrow_init(
               &escrow, 2u, retire_transport, &probe) ==
           SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(1u, 1u),
               (uintptr_t)11u) == SALTS_OK);
    assert(escrow.live_count == 1u);

    assert(native_io_accept_escrow_take(
               &escrow, request(1u, 1u),
               &transport) == SALTS_OK);
    assert(transport == (uintptr_t)11u);
    assert(escrow.live_count == 0u);
    assert(probe.count == 0u);

    assert(native_io_accept_escrow_take(
               &escrow, request(1u, 1u),
               &transport) == SALTS_ENOENT);
    assert(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    assert(probe.count == 0u);
}

static void test_generation_is_part_of_identity(void) {
    native_io_accept_escrow escrow = {0};
    retire_probe probe = {0};
    uintptr_t transport = 0u;

    assert(native_io_accept_escrow_init(
               &escrow, 2u, retire_transport, &probe) ==
           SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(1u, 7u),
               (uintptr_t)21u) == SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(1u, 8u),
               (uintptr_t)22u) == SALTS_OK);

    assert(native_io_accept_escrow_take(
               &escrow, request(1u, 9u),
               &transport) == SALTS_ENOENT);
    assert(native_io_accept_escrow_take(
               &escrow, request(1u, 8u),
               &transport) == SALTS_OK);
    assert(transport == (uintptr_t)22u);

    assert(native_io_accept_escrow_discard(
               &escrow, request(1u, 7u)) == SALTS_OK);
    assert(probe.count == 1u);
    assert(probe.retired[0] == (uintptr_t)21u);
    assert(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
}

static void test_capacity_is_hard_bound(void) {
    native_io_accept_escrow escrow = {0};
    retire_probe probe = {0};

    assert(native_io_accept_escrow_init(
               &escrow, 1u, retire_transport, &probe) ==
           SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(1u, 1u),
               (uintptr_t)31u) == SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(2u, 1u),
               (uintptr_t)32u) == SALTS_ENOBUFS);
    assert(native_io_accept_escrow_publish(
               &escrow, request(1u, 1u),
               (uintptr_t)33u) == SALTS_EALREADY);

    assert(native_io_accept_escrow_discard(
               &escrow, request(1u, 1u)) == SALTS_OK);
    assert(probe.count == 1u);
    assert(probe.retired[0] == (uintptr_t)31u);
    assert(native_io_accept_escrow_publish(
               &escrow, request(2u, 1u),
               (uintptr_t)32u) == SALTS_OK);
    assert(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    assert(probe.count == 2u);
    assert(probe.retired[1] == (uintptr_t)32u);
}

static void test_destroy_retires_every_unclaimed_child_once(void) {
    native_io_accept_escrow escrow = {0};
    retire_probe probe = {0};

    assert(native_io_accept_escrow_init(
               &escrow, 3u, retire_transport, &probe) ==
           SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(1u, 3u),
               (uintptr_t)41u) == SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(2u, 4u),
               (uintptr_t)42u) == SALTS_OK);

    assert(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    assert(probe.count == 2u);
    assert(probe.retired[0] == (uintptr_t)41u);
    assert(probe.retired[1] == (uintptr_t)42u);
    assert(escrow.entries == NULL);
    assert(escrow.live_count == 0u);

    assert(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    assert(probe.count == 2u);
}

static void test_destroy_reports_retire_error_after_consuming(void) {
    native_io_accept_escrow escrow = {0};
    retire_probe probe = {0};

    probe.status = SALTS_EIO;
    assert(native_io_accept_escrow_init(
               &escrow, 1u, retire_transport, &probe) ==
           SALTS_OK);
    assert(native_io_accept_escrow_publish(
               &escrow, request(3u, 5u),
               (uintptr_t)51u) == SALTS_OK);

    assert(native_io_accept_escrow_destroy(
               &escrow) == SALTS_EIO);
    assert(probe.count == 1u);
    assert(probe.retired[0] == (uintptr_t)51u);
    assert(escrow.entries == NULL);
}

static void test_invalid_contract_rejected(void) {
    native_io_accept_escrow escrow = {0};
    retire_probe probe = {0};
    uintptr_t transport = 0u;

    assert(native_io_accept_escrow_init(
               &escrow, 0u, retire_transport, &probe) ==
           SALTS_EINVAL);
    assert(native_io_accept_escrow_init(
               &escrow, 1u, NULL, &probe) ==
           SALTS_EINVAL);
    assert(native_io_accept_escrow_publish(
               &escrow, request(0u, 1u),
               (uintptr_t)1u) == SALTS_EINVAL);
    assert(native_io_accept_escrow_take(
               &escrow, request(0u, 1u),
               &transport) == SALTS_EINVAL);
}

int main(void) {
    test_take_consumes_once();
    test_generation_is_part_of_identity();
    test_capacity_is_hard_bound();
    test_destroy_retires_every_unclaimed_child_once();
    test_destroy_reports_retire_error_after_consuming();
    test_invalid_contract_rejected();
    return 0;
}
