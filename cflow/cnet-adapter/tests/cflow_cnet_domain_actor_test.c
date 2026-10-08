#include <cflow/cnet_domain_actor.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include "tinytest.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

enum {
    DOMAIN_TEST_EVENT = 150,
    DOMAIN_TEST_ACTION = 250,
    DOMAIN_TEST_TIMEOUT_MS = 5000
};

typedef struct domain_test_fixture {
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_actor_ref ref;
    cflow_cnet_domain_bridge bridge;
    cflow_machine_action_binding action;
    const void *owner_thread;
    int initial_state;
    atomic_int actions;
    atomic_int values;
    atomic_int errors;
    atomic_int dones;
    atomic_int wrong_thread;
    atomic_int first_bytes_sum;
    cflow_cnet_domain_delivery last_delivery;
} domain_test_fixture;

static bool domain_action(void *user, const void *state, const void *event,
                          void *target, void *observation,
                          const char **out_error) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    const cflow_cnet_domain_delivery *delivery =
        (const cflow_cnet_domain_delivery *)event;
    cnet_receive_view payload = {0};
    if (!f || !state || !event || !target || !observation || !out_error)
        return false;
    if (f->owner_thread != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_thread, 1);
    if (cflow_cnet_domain_borrow(&f->bridge, delivery, &payload) != SALTS_OK) {
        *out_error = "Domain Actor could not borrow retained payload";
        return false;
    }
    if (payload.kind != CNET_MESSAGE_BYTES || payload.size == 0u) {
        *out_error = "unexpected received byte slice";
        return false;
    }
    atomic_fetch_add(&f->first_bytes_sum,
                     ((const unsigned char *)payload.data)[0]);
    f->last_delivery = *delivery;
    /* This fixture's business processing is the byte copy above. Production
     * consumers ACK only at their actual semantic commit boundary. */
    if (cflow_cnet_domain_acknowledge(&f->bridge, delivery) != SALTS_OK) {
        *out_error = "Domain Actor payload ACK failed";
        return false;
    }
    *(int *)target = *(const int *)state + 1;
    *(int *)observation = *(int *)target;
    *out_error = NULL;
    atomic_fetch_add(&f->actions, 1);
    return true;
}

static bool domain_on_value(void *user, const cmeta_type_desc *type,
                            const void *value) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    if (!f || !type || !cmeta_type_equal(type, &cmeta_type_int) || !value)
        return false;
    if (f->owner_thread != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_thread, 1);
    atomic_fetch_add(&f->values, 1);
    return true;
}

static void domain_on_error(void *user, const char *error) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    if (f && error) atomic_fetch_add(&f->errors, 1);
}

static void domain_on_done(void *user) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    if (f) atomic_fetch_add(&f->dones, 1);
}

static bool domain_fixture_init(
    domain_test_fixture *f, size_t mailbox_capacity, size_t stage_capacity) {
    const cflow_machine_state states[] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[] = {
        {DOMAIN_TEST_EVENT, &cflow_cnet_domain_delivery_type}
    };
    const cflow_machine_action actions[] = {{
        DOMAIN_TEST_ACTION, &cmeta_type_int,
        DOMAIN_TEST_EVENT, &cflow_cnet_domain_delivery_type,
        &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u
    }};
    const cflow_machine_transition transitions[] = {
        {10u, DOMAIN_TEST_EVENT, 0u, DOMAIN_TEST_ACTION, 10u, 1u}
    };
    const cflow_machine_definition definition = {
        states, 1u, 10u, events, 1u, NULL, 0u,
        actions, 1u, transitions, 1u
    };
    cflow_actor_config actor_config = {0};
    cflow_cnet_domain_config stage_config = {0};
    memset(f, 0, sizeof(*f));
    f->owner_thread = cmeta_thread_current_token();
    f->initial_state = 0;
    f->action = (cflow_machine_action_binding){
        DOMAIN_TEST_ACTION, domain_action, f
    };
    if (cflow_machine_build(&f->machine, &definition) != CFLOW_MACHINE_OK ||
        !cflow_executor_owner_init_with_capacity(
            &f->executor, 32u, NULL, NULL) ||
        !cflow_scheduler_owner_bind(&f->scheduler, &f->executor, 16u))
        return false;
    actor_config.machine = (cflow_machine_instance_config){
        &f->machine, &f->initial_state, &cmeta_type_int,
        NULL, 0u, &f->action, 1u, mailbox_capacity, &f->executor
    };
    actor_config.scheduler = &f->scheduler;
    actor_config.callbacks = (cflow_subscriber_callbacks){
        domain_on_value, domain_on_error, domain_on_done, f
    };
    if (cflow_actor_init(&f->actor, &actor_config).status != CFLOW_ACTOR_OK ||
        !cflow_actor_ref_acquire(&f->actor, &f->ref) ||
        cflow_actor_start(&f->actor) != CFLOW_ACTOR_OK)
        return false;
    stage_config = (cflow_cnet_domain_config){
        .actor = &f->ref,
        .event_id = DOMAIN_TEST_EVENT,
        .connection = (cnet_connection){5u, 13u},
        .slot_capacity = stage_capacity,
        .max_receive_bytes = 32u
    };
    return cflow_cnet_domain_bridge_init(&f->bridge, &stage_config) == SALTS_OK;
}

static bool domain_drive_until(domain_test_fixture *f, int expected) {
    const uint64_t started = cmeta_monotonic_ms();
    while (atomic_load(&f->values) < expected &&
           cmeta_monotonic_ms() - started < DOMAIN_TEST_TIMEOUT_MS) {
        if (!cflow_executor_run_one(&f->executor))
            cmeta_sleep_ms(1u);
    }
    return atomic_load(&f->values) >= expected;
}

static void domain_fixture_finish(domain_test_fixture *f) {
    const uint64_t started = cmeta_monotonic_ms();
    if (f->actor.impl) {
        (void)cflow_actor_request_stop(&f->actor);
        while (cflow_actor_current_state(&f->actor) !=
                   CFLOW_ACTOR_STATE_STOPPED &&
               cflow_actor_current_state(&f->actor) !=
                   CFLOW_ACTOR_STATE_FAILED &&
               cmeta_monotonic_ms() - started < DOMAIN_TEST_TIMEOUT_MS) {
            if (!cflow_executor_run_one(&f->executor))
                cmeta_sleep_ms(1u);
        }
        cflow_actor_destroy(&f->actor);
    }
    if (f->bridge.impl) {
        cflow_cnet_domain_stats stats = {0};
        check_equal(cflow_cnet_domain_seal(&f->bridge), SALTS_OK);
        check_equal(cflow_cnet_domain_transport_terminal(&f->bridge), SALTS_OK);
        check_equal(cflow_cnet_domain_abort_after_quiescence(&f->bridge),
                    SALTS_OK);
        check_equal(cflow_cnet_domain_get_stats(&f->bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(cflow_cnet_domain_bridge_destroy(&f->bridge), SALTS_OK);
    }
    cflow_actor_ref_release(&f->ref);
    if (cflow_scheduler_valid(&f->scheduler))
        cflow_scheduler_destroy(&f->scheduler);
    if (cflow_executor_valid(&f->executor))
        cflow_executor_destroy(&f->executor);
    cflow_machine_destroy(&f->machine);
}

typedef struct domain_foreign_probe {
    cflow_cnet_domain_bridge *bridge;
    atomic_int status;
} domain_foreign_probe;

static void domain_foreign_stats(void *user) {
    domain_foreign_probe *p = (domain_foreign_probe *)user;
    cflow_cnet_domain_stats snapshot = {0};
    atomic_store(&p->status,
        cflow_cnet_domain_get_stats(p->bridge, &snapshot));
}

suite("CNet to CFlow Domain Actor bounded credit bridge") {
    it("reserves before CNet receive, rolls back rejection and rejects foreign owner") {
        domain_test_fixture f;
        cflow_cnet_domain_credit first = {0};
        cflow_cnet_domain_credit second = {0};
        cflow_cnet_domain_stats stats = {0};
        domain_foreign_probe foreign = {0};
        cmeta_thread_t thread = NULL;
        const cnet_connection wrong = {6u, 13u};
        const unsigned char data[] = {11u};
        const cnet_receive_view view = {data, sizeof(data), CNET_MESSAGE_BYTES};

        check_true(domain_fixture_init(&f, 1u, 2u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &first), SALTS_OK);
        check_not_equal(first.generation, (uint64_t)0u);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &second),
                    SALTS_EBUSY);
        check_equal(cflow_cnet_domain_receive(&f.bridge, wrong, &view),
                    SALTS_ENOENT);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, first), SALTS_OK);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, first), SALTS_ENOENT);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &second),
                    SALTS_OK);
        check_not_equal(first.generation, second.generation);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, first), SALTS_ENOENT);

        foreign.bridge = &f.bridge;
        check_equal(cmeta_thread_create(&thread, domain_foreign_stats, &foreign),
                    0);
        check_equal(cmeta_thread_join(&thread), 0);
        check_equal(atomic_load(&foreign.status), SALTS_EPERM);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, second), SALTS_OK);

        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.reserved_credits, (uint64_t)2u);
        check_equal(stats.rolled_back_credits, (uint64_t)2u);
        domain_fixture_finish(&f);
    }

    it("retains borrowed bytes through FULL and delivers only after owner progress") {
        domain_test_fixture f;
        cflow_cnet_domain_credit credit = {0};
        cflow_cnet_domain_stats stats = {0};
        const cnet_connection conn = {5u, 13u};
        unsigned char first[] = {21u, 22u, 23u};
        unsigned char second[] = {41u, 42u};
        cnet_receive_view view1 = {first, sizeof(first), CNET_MESSAGE_BYTES};
        cnet_receive_view view2 = {second, sizeof(second), CNET_MESSAGE_BYTES};
        size_t work = 555u;

        check_true(domain_fixture_init(&f, 1u, 2u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(&f.bridge, conn, &view1), SALTS_OK);
        memset(first, 0u, sizeof(first)); /* CNet's callback borrow has ended. */

        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(&f.bridge, conn, &view2),
                    SALTS_ENOBUFS);
        memset(second, 0u, sizeof(second));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit),
                    SALTS_EBUSY);
        check_equal(cflow_cnet_domain_retry_actor(&f.bridge, &work),
                    SALTS_ENOBUFS);
        check_equal(work, (size_t)0u);

        check_true(domain_drive_until(&f, 1));
        check_equal(cflow_cnet_domain_retry_actor(&f.bridge, &work), SALTS_OK);
        check_equal(work, (size_t)1u);
        check_true(domain_drive_until(&f, 2));
        check_equal(atomic_load(&f.first_bytes_sum), 62);
        check_equal(atomic_load(&f.actions), 2);
        check_equal(atomic_load(&f.wrong_thread), 0);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(cflow_cnet_domain_acknowledge(
            &f.bridge, &f.last_delivery), SALTS_ENOENT);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.awaiting_ack, (size_t)0u);
        check_equal(stats.retained_bytes, (size_t)0u);
        check_equal(stats.received_views, (uint64_t)2u);
        check_equal(stats.mailbox_accepted, (uint64_t)2u);
        check_equal(stats.mailbox_full, (uint64_t)2u);
        check_equal(stats.acknowledged, (uint64_t)2u);
        check_equal(stats.peak_retained_bytes, (size_t)5u);
        domain_fixture_finish(&f);
    }

    it("rejects an unsolicited receive instead of silently losing bytes") {
        domain_test_fixture f;
        cflow_cnet_domain_stats stats = {0};
        unsigned char byte = 9u;
        const cnet_receive_view view = {&byte, 1u, CNET_MESSAGE_BYTES};

        check_true(domain_fixture_init(&f, 1u, 1u));
        check_equal(cflow_cnet_domain_receive(
            &f.bridge, (cnet_connection){5u, 13u}, &view), SALTS_EPROTO);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.fatal_status, SALTS_EPROTO);
        check_true(stats.sealed);
        check_equal(stats.mailbox_accepted, (uint64_t)0u);
        check_equal(stats.active_slots, (size_t)0u);
        domain_fixture_finish(&f);
    }

    it("reports oversize as fatal, never fabricates Actor acceptance") {
        domain_test_fixture f;
        cflow_cnet_domain_credit credit = {0};
        cflow_cnet_domain_stats stats = {0};
        unsigned char oversized[33] = {0};
        const cnet_receive_view view = {
            oversized, sizeof(oversized), CNET_MESSAGE_BYTES};
        check_true(domain_fixture_init(&f, 1u, 1u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(
            &f.bridge, (cnet_connection){5u, 13u}, &view), SALTS_EMSGSIZE);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.fatal_status, SALTS_EMSGSIZE);
        check_equal(stats.mailbox_accepted, (uint64_t)0u);
        check_equal(stats.active_slots, (size_t)1u);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit),
                    SALTS_ESHUTDOWN);
        check_equal(cflow_cnet_domain_bridge_destroy(&f.bridge), SALTS_EBUSY);
        domain_fixture_finish(&f);
    }

    it("preserves Actor-accepted leases across CNet transport terminal") {
        domain_test_fixture f;
        cflow_cnet_domain_credit first = {0};
        cflow_cnet_domain_credit pending = {0};
        cflow_cnet_domain_stats stats = {0};
        unsigned char payload[] = {73u};
        cnet_receive_view view = {payload, sizeof(payload), CNET_MESSAGE_BYTES};
        check_true(domain_fixture_init(&f, 1u, 2u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &first), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(
            &f.bridge, (cnet_connection){5u, 13u}, &view), SALTS_OK);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &pending),
                    SALTS_OK);
        check_equal(cflow_cnet_domain_transport_terminal(&f.bridge), SALTS_OK);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, pending),
                    SALTS_ENOENT);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_true(stats.transport_terminal);
        check_equal(stats.active_slots, (size_t)1u);
        check_equal(stats.awaiting_ack, (size_t)1u);
        check_true(domain_drive_until(&f, 1));
        check_equal(atomic_load(&f.first_bytes_sum), 73);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.acknowledged, (uint64_t)1u);
        domain_fixture_finish(&f);
    }
}
