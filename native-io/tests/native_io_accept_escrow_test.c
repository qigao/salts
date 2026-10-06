#include "../src/native_io_accept_escrow.h"

#include <salts/error_codes.h>

#include <tinytest.h>
#include <stdint.h>
#include <string.h>

typedef struct retire_probe {
    uintptr_t retired[8];
    size_t count;
    int status;
} retire_probe;

static native_io_accept_escrow escrow;
static retire_probe probe;

static int retire_transport(
    void *context,
    uintptr_t transport) {
    retire_probe *probe = (retire_probe *)context;

    check(probe != NULL);
    check(probe->count <
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
    uintptr_t transport = 0u;

    check(native_io_accept_escrow_init(
               &escrow, 2u, retire_transport, &probe) ==
           SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(1u, 1u),
               (uintptr_t)11u) == SALTS_OK);
    check(escrow.live_count == 1u);

    check(native_io_accept_escrow_take(
               &escrow, request(1u, 1u),
               &transport) == SALTS_OK);
    check(transport == (uintptr_t)11u);
    check(escrow.live_count == 0u);
    check(probe.count == 0u);

    check(native_io_accept_escrow_take(
               &escrow, request(1u, 1u),
               &transport) == SALTS_ENOENT);
    check(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    check(probe.count == 0u);
}

static void test_generation_is_part_of_identity(void) {
    uintptr_t transport = 0u;

    check(native_io_accept_escrow_init(
               &escrow, 2u, retire_transport, &probe) ==
           SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(1u, 7u),
               (uintptr_t)21u) == SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(1u, 8u),
               (uintptr_t)22u) == SALTS_OK);

    check(native_io_accept_escrow_take(
               &escrow, request(1u, 9u),
               &transport) == SALTS_ENOENT);
    check(native_io_accept_escrow_take(
               &escrow, request(1u, 8u),
               &transport) == SALTS_OK);
    check(transport == (uintptr_t)22u);

    check(native_io_accept_escrow_discard(
               &escrow, request(1u, 7u)) == SALTS_OK);
    check(probe.count == 1u);
    check(probe.retired[0] == (uintptr_t)21u);
    check(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
}

static void test_capacity_is_hard_bound(void) {

    check(native_io_accept_escrow_init(
               &escrow, 1u, retire_transport, &probe) ==
           SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(1u, 1u),
               (uintptr_t)31u) == SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(2u, 1u),
               (uintptr_t)32u) == SALTS_ENOBUFS);
    check(native_io_accept_escrow_publish(
               &escrow, request(1u, 1u),
               (uintptr_t)33u) == SALTS_EALREADY);

    check(native_io_accept_escrow_discard(
               &escrow, request(1u, 1u)) == SALTS_OK);
    check(probe.count == 1u);
    check(probe.retired[0] == (uintptr_t)31u);
    check(native_io_accept_escrow_publish(
               &escrow, request(2u, 1u),
               (uintptr_t)32u) == SALTS_OK);
    check(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    check(probe.count == 2u);
    check(probe.retired[1] == (uintptr_t)32u);
}

static void test_destroy_retires_every_unclaimed_child_once(void) {

    check(native_io_accept_escrow_init(
               &escrow, 3u, retire_transport, &probe) ==
           SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(1u, 3u),
               (uintptr_t)41u) == SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(2u, 4u),
               (uintptr_t)42u) == SALTS_OK);

    check(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    check(probe.count == 2u);
    check(probe.retired[0] == (uintptr_t)41u);
    check(probe.retired[1] == (uintptr_t)42u);
    check(escrow.entries == NULL);
    check(escrow.live_count == 0u);

    check(native_io_accept_escrow_destroy(
               &escrow) == SALTS_OK);
    check(probe.count == 2u);
}

static void test_destroy_reports_retire_error_after_consuming(void) {

    probe.status = SALTS_EIO;
    check(native_io_accept_escrow_init(
               &escrow, 1u, retire_transport, &probe) ==
           SALTS_OK);
    check(native_io_accept_escrow_publish(
               &escrow, request(3u, 5u),
               (uintptr_t)51u) == SALTS_OK);

    check(native_io_accept_escrow_destroy(
               &escrow) == SALTS_EIO);
    check(probe.count == 1u);
    check(probe.retired[0] == (uintptr_t)51u);
    check(escrow.entries == NULL);
}

static void test_invalid_contract_rejected(void) {
    uintptr_t transport = 0u;

    check(native_io_accept_escrow_init(
               &escrow, 0u, retire_transport, &probe) ==
           SALTS_EINVAL);
    check(native_io_accept_escrow_init(
               &escrow, 1u, NULL, &probe) ==
           SALTS_EINVAL);
    check(native_io_accept_escrow_publish(
               &escrow, request(0u, 1u),
               (uintptr_t)1u) == SALTS_EINVAL);
    check(native_io_accept_escrow_take(
               &escrow, request(0u, 1u),
               &transport) == SALTS_EINVAL);
}

suite("NativeIO accept escrow") {
    before_each() {
        check_null(escrow.entries);
        escrow = (native_io_accept_escrow){0};
        probe = (retire_probe){0};
    }
    after_each() {
        probe.status = SALTS_OK;
        check_equal(native_io_accept_escrow_destroy(&escrow), SALTS_OK);
    }
    group("ownership and capacity") {
        it("take consumes once") { test_take_consumes_once(); }
        it("generation is part of identity") { test_generation_is_part_of_identity(); }
        it("capacity is hard bound") { test_capacity_is_hard_bound(); }
        it("destroy retires every unclaimed child once") { test_destroy_retires_every_unclaimed_child_once(); }
        it("destroy reports retire error after consuming") { test_destroy_reports_retire_error_after_consuming(); }
        it("invalid contract rejected") { test_invalid_contract_rejected(); }
    }
}
