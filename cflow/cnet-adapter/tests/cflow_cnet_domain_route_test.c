#include <cflow/cnet_domain_route.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <tinytest.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET route_net_socket;
typedef int route_net_socklen;
#define ROUTE_NET_BAD_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int route_net_socket;
typedef socklen_t route_net_socklen;
#define ROUTE_NET_BAD_SOCKET (-1)
#endif

enum {
    ROUTE_TEST_OWNERS = 4,
    ROUTE_TEST_MESSAGES = 3,
    ROUTE_TEST_EVENT = 301,
    ROUTE_TEST_ACTION = 302,
    ROUTE_TEST_TIMEOUT_MS = 5000
};

typedef struct route_test_fixture {
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_actor_ref actor_ref;
    cflow_machine_action_binding action_binding;
    cflow_cnet_domain_route routes[ROUTE_TEST_OWNERS];
    cflow_cnet_domain_route_delivery last[ROUTE_TEST_OWNERS];
    const void *target_owner;
    int initial_state;
    size_t route_count;
    atomic_int actions;
    atomic_int values;
    atomic_int wrong_owner;
    atomic_int errors;
    atomic_int checksum;
    atomic_int duplicate_acks_rejected;
    atomic_int bad_tokens_rejected;
} route_test_fixture;

typedef struct route_source_fixture {
    cflow_cnet_domain_route *route;
    uint32_t owner_id;
    int messages;
    bool allow_full;
    atomic_int status;
    atomic_int full;
    atomic_int sent;
    atomic_int wrong_owner_rejected;
    atomic_bool started;
    atomic_bool done;
} route_source_fixture;

typedef struct route_reuse_source {
    cflow_cnet_domain_route *route;
    atomic_bool first_ready;
    atomic_bool release_second;
    atomic_bool done;
    atomic_int status;
    atomic_int before_ack_full;
} route_reuse_source;

static bool route_action(void *user, const void *state, const void *event,
                         void *target, void *observation,
                         const char **out_error) {
    route_test_fixture *f = (route_test_fixture *)user;
    const cflow_cnet_domain_route_delivery *delivery =
        (const cflow_cnet_domain_route_delivery *)event;
    cflow_cnet_domain_route_delivery forged;
    cflow_cnet_domain_route *route;
    cnet_receive_view view = {0};
    int byte_value;
    if (!f || !state || !event || !target || !observation || !out_error)
        return false;
    if (f->target_owner != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_owner, 1);
    if (delivery->source_owner == 0u ||
        delivery->source_owner > f->route_count) {
        *out_error = "unknown source owner";
        return false;
    }
    route = &f->routes[delivery->source_owner - 1u];
    forged = *delivery;
    forged.incarnation = 0u; /* Different/retired route incarnation. */
    if (cflow_cnet_domain_route_borrow(route, &forged, &view) ==
            SALTS_ENOENT)
        atomic_fetch_add(&f->bad_tokens_rejected, 1);
    else {
        *out_error = "stale route token was accepted";
        return false;
    }
    if (cflow_cnet_domain_route_borrow(route, delivery, &view) != SALTS_OK ||
        view.kind != CNET_MESSAGE_BYTES || view.size != 1u ||
        view.data == NULL) {
        *out_error = "owned payload was not retained across source callback";
        return false;
    }

    byte_value = ((const unsigned char *)view.data)[0];
    atomic_fetch_add(&f->checksum, byte_value);
    f->last[delivery->source_owner - 1u] = *delivery;
    /* No source CNet/NativeIO action occurs from this destination callback. */
    if (cflow_cnet_domain_route_acknowledge(route, delivery) != SALTS_OK) {
        *out_error = "ACK failed";
        return false;
    }
    if (cflow_cnet_domain_route_acknowledge(route, delivery) == SALTS_ENOENT)
        atomic_fetch_add(&f->duplicate_acks_rejected, 1);
    else {
        *out_error = "duplicate ACK was accepted";
        return false;
    }

    *(int *)target = *(const int *)state + 1;
    *(int *)observation = *(int *)target;
    atomic_fetch_add(&f->actions, 1);
    *out_error = NULL;
    return true;
}

static bool route_sink_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    route_test_fixture *f = (route_test_fixture *)user;
    if (!f || !type || !value || !cmeta_type_equal(type, &cmeta_type_int))
        return false;
    if (f->target_owner != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_owner, 1);
    atomic_fetch_add(&f->values, 1);
    return true;
}

static void route_sink_error(void *user, const char *message) {
    route_test_fixture *f = (route_test_fixture *)user;
    if (f && message) atomic_fetch_add(&f->errors, 1);
}

static void route_sink_done(void *user) {
    (void)user;
}

static bool route_test_init_mode(
    route_test_fixture *f, size_t route_count,
    size_t mailbox_capacity, size_t stage_capacity,
    bool defer_route_bindings) {
    const cflow_machine_state states[] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[] = {
        {ROUTE_TEST_EVENT, &cflow_cnet_domain_route_delivery_type}
    };
    const cflow_machine_action actions[] = {{
        ROUTE_TEST_ACTION, &cmeta_type_int,
        ROUTE_TEST_EVENT, &cflow_cnet_domain_route_delivery_type,
        &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u
    }};
    const cflow_machine_transition transitions[] = {
        {10u, ROUTE_TEST_EVENT, 0u, ROUTE_TEST_ACTION, 10u, 1u}
    };
    const cflow_machine_definition definition = {
        states, 1u, 10u, events, 1u, NULL, 0u,
        actions, 1u, transitions, 1u
    };
    cflow_actor_config actor_config = {0};

    if (!f || route_count == 0u || route_count > ROUTE_TEST_OWNERS)
        return false;
    memset(f, 0, sizeof(*f));
    f->target_owner = cmeta_thread_current_token();
    f->route_count = route_count;
    f->initial_state = 0;
    f->action_binding = (cflow_machine_action_binding){
        ROUTE_TEST_ACTION, route_action, f
    };
    if (cflow_machine_build(&f->machine, &definition) != CFLOW_MACHINE_OK ||
        !cflow_executor_owner_init_with_capacity(
            &f->executor, 64u, NULL, NULL) ||
        !cflow_scheduler_owner_bind(&f->scheduler, &f->executor, 32u))
        return false;
    actor_config.machine = (cflow_machine_instance_config){
        &f->machine, &f->initial_state, &cmeta_type_int,
        NULL, 0u, &f->action_binding, 1u, mailbox_capacity,
        &f->executor
    };
    actor_config.scheduler = &f->scheduler;
    actor_config.callbacks = (cflow_subscriber_callbacks){
        route_sink_value, route_sink_error, route_sink_done, f
    };
    if (cflow_actor_init(&f->actor, &actor_config).status != CFLOW_ACTOR_OK ||
        !cflow_actor_ref_acquire(&f->actor, &f->actor_ref) ||
        cflow_actor_start(&f->actor) != CFLOW_ACTOR_OK)
        return false;

    if (defer_route_bindings) return true;
    for (size_t i = 0u; i < route_count; ++i) {
        const cflow_cnet_domain_route_config config = {
            .actor = &f->actor_ref,
            .event_id = ROUTE_TEST_EVENT,
            .source_owner = (uint32_t)(i + 1u),
            .connection = {(uint32_t)(i + 1u), 55u},
            .slot_capacity = stage_capacity,
            .max_receive_bytes = 16u
        };
        if (cflow_cnet_domain_route_init(&f->routes[i], &config) != SALTS_OK)
            return false;
    }
    return true;
}

static bool route_test_init(
    route_test_fixture *f, size_t route_count,
    size_t mailbox_capacity, size_t stage_capacity) {
    return route_test_init_mode(
        f, route_count, mailbox_capacity, stage_capacity, false);
}

static bool route_drive_until(route_test_fixture *f, int expected) {
    const uint64_t deadline = cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
    while (atomic_load(&f->values) < expected &&
           cmeta_monotonic_ms() < deadline) {
        if (!cflow_executor_run_one(&f->executor))
            cmeta_sleep_ms(1u);
    }
    return atomic_load(&f->values) >= expected;
}

static void route_test_finish(route_test_fixture *f) {
    const uint64_t deadline = cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
    if (f->actor.impl) {
        (void)cflow_actor_request_stop(&f->actor);
        while (cflow_actor_current_state(&f->actor) !=
                    CFLOW_ACTOR_STATE_STOPPED &&
               cflow_actor_current_state(&f->actor) !=
                    CFLOW_ACTOR_STATE_FAILED &&
               cmeta_monotonic_ms() < deadline) {
            if (!cflow_executor_run_one(&f->executor))
                cmeta_sleep_ms(1u);
        }
        cflow_actor_destroy(&f->actor);
    }
    for (size_t i = 0u; i < f->route_count; ++i) {
        cflow_cnet_domain_route_stats stats = {0};
        if (!f->routes[i].impl) continue;
        check_equal(cflow_cnet_domain_route_get_stats(
            &f->routes[i], &stats), SALTS_OK);
        check_true(stats.sealed);
        check_false(stats.receive_credit_live);
        check_equal(
            cflow_cnet_domain_route_abort_after_quiescence(&f->routes[i]),
            SALTS_OK);
        check_equal(
            cflow_cnet_domain_route_destroy(&f->routes[i]), SALTS_OK);
    }
    cflow_actor_ref_release(&f->actor_ref);
    if (cflow_scheduler_valid(&f->scheduler))
        cflow_scheduler_destroy(&f->scheduler);
    if (cflow_executor_valid(&f->executor))
        cflow_executor_destroy(&f->executor);
    cflow_machine_destroy(&f->machine);
}

static void route_source_send(void *user) {
    route_source_fixture *source = (route_source_fixture *)user;
    int status;
    status = cflow_cnet_domain_route_bind_source(source->route);
    if (status != SALTS_OK) goto done;
    atomic_store(&source->started, true);

    for (int index = 0; index < source->messages; ++index) {
        cflow_cnet_domain_route_credit credit = {0};
        unsigned char byte = (unsigned char)(source->owner_id * 10u +
                                               (uint32_t)index);
        const cnet_receive_view view = {
            &byte, sizeof(byte), CNET_MESSAGE_BYTES
        };
        cflow_cnet_domain_route_delivery foreign_delivery = {0};
        cnet_receive_view borrowed = {0};

        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_OK) goto done;
        status = cflow_cnet_domain_route_receive(
            source->route, credit, &view);
        /* Mutate the original callback view: the target must observe the
         * preallocated copied payload, not a dangling CNet borrowed address. */
        byte = 0u;
        if (status == SALTS_ENOBUFS && source->allow_full)
            atomic_fetch_add(&source->full, 1);
        else if (status != SALTS_OK)
            goto done;
        foreign_delivery.incarnation = credit.incarnation;
        foreign_delivery.generation = credit.generation;
        foreign_delivery.slot = credit.slot;
        foreign_delivery.source_owner = credit.source_owner;
        foreign_delivery.connection = credit.connection;
        foreign_delivery.size = 1u;
        foreign_delivery.kind = CNET_MESSAGE_BYTES;
        if (cflow_cnet_domain_route_borrow(
                source->route, &foreign_delivery, &borrowed) == SALTS_EPERM)
            atomic_fetch_add(&source->wrong_owner_rejected, 1);
        else {
            status = SALTS_EPROTO;
            goto done;
        }
        atomic_fetch_add(&source->sent, 1);
    }
    status = cflow_cnet_domain_route_source_terminal(source->route);
done:
    atomic_store(&source->status, status);
    atomic_store(&source->done, true);
}

static void route_reuse_send(void *user) {
    route_reuse_source *source = (route_reuse_source *)user;
    int status = cflow_cnet_domain_route_bind_source(source->route);
    if (status != SALTS_OK) goto done;
    {
        cflow_cnet_domain_route_credit credit = {0};
        const unsigned char first = 41u;
        const cnet_receive_view view = {&first, 1u, CNET_MESSAGE_BYTES};
        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_OK) goto done;
        status = cflow_cnet_domain_route_receive(
            source->route, credit, &view);
        if (status != SALTS_OK) goto done;

        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_ENOBUFS) goto done;
        atomic_store(&source->before_ack_full, 1);
        atomic_store(&source->first_ready, true);

        const uint64_t deadline =
            cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
        while (!atomic_load(&source->release_second) &&
               cmeta_monotonic_ms() < deadline)
            cmeta_thread_yield();
        if (!atomic_load(&source->release_second)) {
            status = SALTS_ETIMEDOUT;
            goto done;
        }
        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_OK) goto done;
        {
            const unsigned char second = 42u;
            const cnet_receive_view next = {
                &second, 1u, CNET_MESSAGE_BYTES
            };
            status = cflow_cnet_domain_route_receive(
                source->route, credit, &next);
            if (status != SALTS_OK) goto done;
        }
    }
    status = cflow_cnet_domain_route_source_terminal(source->route);
done:
    atomic_store(&source->status, status);
    atomic_store(&source->done, true);
}

typedef struct route_terminal_source {
    cflow_cnet_domain_route *route;
    bool oversized;
    atomic_int status;
    atomic_bool done;
} route_terminal_source;

static void route_source_terminal_case(void *user) {
    route_terminal_source *source = (route_terminal_source *)user;
    cflow_cnet_domain_route_credit credit = {0};
    int status = cflow_cnet_domain_route_bind_source(source->route);
    if (status != SALTS_OK) goto done;
    status = cflow_cnet_domain_route_reserve(source->route, &credit);
    if (status != SALTS_OK) goto done;
    if (source->oversized) {
        const unsigned char data[17] = {0};
        const cnet_receive_view view = {
            data, sizeof(data), CNET_MESSAGE_BYTES
        };
        status = cflow_cnet_domain_route_receive(source->route, credit, &view);
        if (status != SALTS_EMSGSIZE) goto done;
    }
    status = cflow_cnet_domain_route_source_terminal(source->route);
    if (status != SALTS_OK) goto done;
    if (!source->oversized) {
        if (cflow_cnet_domain_route_cancel_credit(
                source->route, credit) != SALTS_ENOENT)
            status = SALTS_EPROTO;
    }
done:
    atomic_store(&source->status, status);
    atomic_store(&source->done, true);
}

suite("CNet cross-owner domain Actor retained lease routing") {
    it("routes four concurrent CNet source owners to one target Actor without a second FIFO") {
        route_test_fixture f;
        route_source_fixture sources[ROUTE_TEST_OWNERS] = {0};
        cmeta_thread_t threads[ROUTE_TEST_OWNERS] = {0};
        const int expected = ROUTE_TEST_OWNERS * ROUTE_TEST_MESSAGES;

        check_true(route_test_init(&f, ROUTE_TEST_OWNERS, 32u, 4u));
        for (size_t i = 0u; i < ROUTE_TEST_OWNERS; ++i) {
            sources[i].route = &f.routes[i];
            sources[i].owner_id = (uint32_t)(i + 1u);
            sources[i].messages = ROUTE_TEST_MESSAGES;
            check_equal(cmeta_thread_create(
                &threads[i], route_source_send, &sources[i]), 0);
        }
        for (size_t i = 0u; i < ROUTE_TEST_OWNERS; ++i) {
            check_equal(cmeta_thread_join(&threads[i]), 0);
            check_true(atomic_load(&sources[i].done));
            check_equal(atomic_load(&sources[i].status), SALTS_OK);
            check_equal(atomic_load(&sources[i].sent), ROUTE_TEST_MESSAGES);
            check_equal(atomic_load(&sources[i].full), 0);
            check_equal(atomic_load(&sources[i].wrong_owner_rejected),
                        ROUTE_TEST_MESSAGES);
            cflow_cnet_domain_route_stats stats = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            check_true(stats.source_bound);
            check_true(stats.sealed);
            check_true(stats.source_terminal);
            check_equal(stats.active_slots, (size_t)ROUTE_TEST_MESSAGES);
            check_equal(stats.awaiting_ack, (size_t)ROUTE_TEST_MESSAGES);
            check_equal(stats.actor_accepted, (uint64_t)ROUTE_TEST_MESSAGES);
            check_equal(stats.actor_full, (uint64_t)0u);
        }

        check_true(route_drive_until(&f, expected));
        check_equal(atomic_load(&f.actions), expected);
        check_equal(atomic_load(&f.values), expected);
        check_equal(atomic_load(&f.checksum), 312);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(atomic_load(&f.wrong_owner), 0);
        check_equal(atomic_load(&f.bad_tokens_rejected), expected);
        check_equal(atomic_load(&f.duplicate_acks_rejected), expected);

        for (size_t i = 0u; i < ROUTE_TEST_OWNERS; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            cnet_receive_view wrong = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            check_equal(stats.active_slots, (size_t)0u);
            check_equal(stats.retained_bytes, (size_t)0u);
            check_equal(stats.acknowledged, (uint64_t)ROUTE_TEST_MESSAGES);
            check_equal(cflow_cnet_domain_route_borrow(
                &f.routes[i], &f.last[i], &wrong), SALTS_ENOENT);
            check_equal(cflow_cnet_domain_route_acknowledge(
                &f.routes[i], &f.last[i]), SALTS_ENOENT);
        }
        route_test_finish(&f);
    }

    it("preserves two remote owners when the target Mailbox is full") {
        route_test_fixture f;
        route_source_fixture sources[2] = {0};
        cmeta_thread_t threads[2] = {0};
        size_t full = 0u;
        size_t accepted = 0u;

        check_true(route_test_init(&f, 2u, 1u, 2u));
        for (size_t i = 0u; i < 2u; ++i) {
            sources[i].route = &f.routes[i];
            sources[i].owner_id = (uint32_t)(i + 1u);
            sources[i].messages = 1;
            sources[i].allow_full = true;
            check_equal(cmeta_thread_create(
                &threads[i], route_source_send, &sources[i]), 0);
        }
        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(cmeta_thread_join(&threads[i]), 0);
            check_equal(atomic_load(&sources[i].status), SALTS_OK);
            cflow_cnet_domain_route_stats stats = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            full += stats.actor_full;
            accepted += stats.actor_accepted;
            check_equal(stats.active_slots, (size_t)1u);
            check_true(stats.sealed);
        }
        check_equal(full, (size_t)1u);
        check_equal(accepted, (size_t)1u);
        check_equal(atomic_load(&f.actions), 0);

        /* A bounded retry while the mailbox is still full must preserve the
         * payload and return FULL without spinning or silently dropping it. */
        for (size_t i = 0u; i < 2u; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            size_t work = 55u;
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            if (stats.staged != 0u) {
                check_equal(cflow_cnet_domain_route_retry_staged(
                    &f.routes[i], &work), SALTS_ENOBUFS);
                check_equal(work, (size_t)0u);
            }
        }
        check_true(route_drive_until(&f, 1));
        for (size_t i = 0u; i < 2u; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            size_t work = 0u;
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            if (stats.staged != 0u) {
                check_equal(cflow_cnet_domain_route_retry_staged(
                    &f.routes[i], &work), SALTS_OK);
                check_equal(work, (size_t)1u);
            }
        }
        check_true(route_drive_until(&f, 2));
        check_equal(atomic_load(&f.actions), 2);
        check_equal(atomic_load(&f.checksum), 30);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(atomic_load(&f.wrong_owner), 0);
        for (size_t i = 0u; i < 2u; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            check_equal(stats.active_slots, (size_t)0u);
            check_equal(stats.acknowledged, (uint64_t)1u);
        }
        route_test_finish(&f);
    }

    it("isolates an oversized receive on one owner without sealing neighbors") {
        route_test_fixture f;
        route_terminal_source oversized = {0};
        route_source_fixture healthy = {0};
        cmeta_thread_t threads[2] = {0};
        cflow_cnet_domain_route_stats bad_stats = {0};
        cflow_cnet_domain_route_stats good_stats = {0};

        check_true(route_test_init(&f, 2u, 4u, 2u));
        oversized.route = &f.routes[0];
        oversized.oversized = true;
        healthy.route = &f.routes[1];
        healthy.owner_id = 2u;
        healthy.messages = 1;
        check_equal(cmeta_thread_create(
            &threads[0], route_source_terminal_case, &oversized), 0);
        check_equal(cmeta_thread_create(
            &threads[1], route_source_send, &healthy), 0);
        check_equal(cmeta_thread_join(&threads[0]), 0);
        check_equal(cmeta_thread_join(&threads[1]), 0);
        check_equal(atomic_load(&oversized.status), SALTS_OK);
        check_equal(atomic_load(&healthy.status), SALTS_OK);
        check_true(atomic_load(&oversized.done));
        check_true(atomic_load(&healthy.done));

        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &bad_stats), SALTS_OK);
        check_equal(bad_stats.fatal_status, SALTS_EMSGSIZE);
        check_true(bad_stats.sealed);
        check_true(bad_stats.source_terminal);
        check_equal(bad_stats.actor_accepted, (uint64_t)0u);
        check_equal(bad_stats.active_slots, (size_t)1u);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[1], &good_stats), SALTS_OK);
        check_equal(good_stats.fatal_status, SALTS_OK);
        check_equal(good_stats.actor_accepted, (uint64_t)1u);
        check_true(route_drive_until(&f, 1));
        check_equal(atomic_load(&f.actions), 1);
        check_equal(atomic_load(&f.checksum), 20);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[1], &good_stats), SALTS_OK);
        check_equal(good_stats.acknowledged, (uint64_t)1u);
        check_equal(good_stats.active_slots, (size_t)0u);
        /* Target-owned abort only happens after Actor and source quiescence
         * in route_test_finish(), preserving the one failed source lease. */
        route_test_finish(&f);
    }

    it("retires a source CNet credit on terminal without fabricating an Actor ACK") {
        route_test_fixture f;
        route_terminal_source terminal = {0};
        cmeta_thread_t worker = NULL;
        cflow_cnet_domain_route_stats stats = {0};

        check_true(route_test_init(&f, 1u, 2u, 1u));
        terminal.route = &f.routes[0];
        check_equal(cmeta_thread_create(
            &worker, route_source_terminal_case, &terminal), 0);
        check_equal(cmeta_thread_join(&worker), 0);
        check_equal(atomic_load(&terminal.status), SALTS_OK);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_true(stats.sealed);
        check_true(stats.source_terminal);
        check_false(stats.receive_credit_live);
        check_equal(stats.reserved_credits, (uint64_t)1u);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.actor_accepted, (uint64_t)0u);
        check_equal(stats.acknowledged, (uint64_t)0u);
        check_equal(atomic_load(&f.actions), 0);
        route_test_finish(&f);
    }

    it("keeps credit bounded across source-owner ACK reuse and terminal") {
        route_test_fixture f;
        route_reuse_source source = {0};
        cmeta_thread_t thread = NULL;
        cflow_cnet_domain_route_stats stats = {0};
        cflow_cnet_domain_route_credit wrong_owner_credit = {0};
        const uint64_t deadline = cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;

        check_true(route_test_init(&f, 1u, 2u, 1u));
        source.route = &f.routes[0];
        check_equal(cmeta_thread_create(
            &thread, route_reuse_send, &source), 0);
        while (!atomic_load(&source.first_ready) &&
               !atomic_load(&source.done) &&
               cmeta_monotonic_ms() < deadline)
            cmeta_thread_yield();
        check_true(atomic_load(&source.first_ready));
        check_equal(atomic_load(&source.before_ack_full), 1);
        check_equal(cflow_cnet_domain_route_reserve(
            &f.routes[0], &wrong_owner_credit), SALTS_EPERM);

        check_true(route_drive_until(&f, 1));
        atomic_store(&source.release_second, true);
        check_equal(cmeta_thread_join(&thread), 0);
        check_equal(atomic_load(&source.status), SALTS_OK);
        check_true(route_drive_until(&f, 2));
        check_equal(atomic_load(&f.checksum), 83);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_true(stats.sealed);
        check_true(stats.source_terminal);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.reserved_credits, (uint64_t)2u);
        check_equal(stats.acknowledged, (uint64_t)2u);
        route_test_finish(&f);
    }
}
