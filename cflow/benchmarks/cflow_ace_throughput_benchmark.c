#include <cflow/actor.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <fmt.h>
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

/* Benchmark-local CPU dispatchers, not an exported scheduler. The elected
 * leader is the sole mailbox consumer; dequeue ownership transfers only under
 * the monitor. LF promotes before processing independent jobs. Token promotes
 * after processing, preserving FIFO settlement and one mutable-state owner.
 * Payloads are trivial copies. Producers retain their precomputed source until
 * joined. Capacity is fixed; FULL retries yield and have a sample deadline.
 * Destruction joins producers and workers before freeing callback storage.
 */
enum { MAX_THREADS = 4, CAPACITY = 128, MESSAGES = 2048, SAMPLES = 16 };
enum dispatch_mode { ACTOR_NONE, ACTOR_VALUE, LEADER_FOLLOWERS, SERIAL_TOKEN };
typedef struct payload64 { uint64_t words[8]; } payload64;
typedef struct payload1024 { uint64_t words[128]; } payload1024;
typedef union payload_storage { payload64 small; payload1024 large; } payload_storage;
typedef struct completion { uint64_t id, digest; } completion;

static const cmeta_type_traits trivial_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
static const cmeta_type_desc type64 = {
    "ace_bench_payload64", sizeof(payload64), CMETA_ALIGNOF(payload64),
    CMETA_T_OBJECT, NULL, &trivial_traits, NULL};
static const cmeta_type_desc type1024 = {
    "ace_bench_payload1024", sizeof(payload1024), CMETA_ALIGNOF(payload1024),
    CMETA_T_OBJECT, NULL, &trivial_traits, NULL};
static const cmeta_type_desc completion_type = {
    "ace_bench_completion", sizeof(completion), CMETA_ALIGNOF(completion),
    CMETA_T_OBJECT, NULL, &trivial_traits, NULL};

typedef struct bench_context bench_context;
typedef struct producer {
    bench_context *context;
    cmeta_thread_t thread;
    cflow_actor_ref ref;
    size_t id;
    uint64_t full_retries;
} producer;
typedef struct worker {
    bench_context *context;
    cmeta_thread_t thread;
    cmeta_cond_t wake;
    size_t id;
    bool eligible;
} worker;
struct bench_context {
    cmeta_mutex_t lock;
    cmeta_cond_t changed;
    producer producers[MAX_THREADS];
    worker workers[MAX_THREADS];
    cflow_mailbox mailbox;
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    enum dispatch_mode mode;
    const cmeta_type_desc *payload_type;
    size_t producer_count, worker_count, batch;
    size_t ready, producers_done, epoch, completed, queued, active, peak_active;
    size_t leader, last_leader, handoffs;
    size_t accepted, dequeued, next_settlement;
    size_t observations[MESSAGES];
    uint64_t expected[MESSAGES];
    payload_storage payloads[MESSAGES];
    uint64_t deadline;
    bool closing, failed, hold_first, held, release;
};

static bench_context *current;
static tstr title;

/* O(payload bytes), O(1) scratch; exactly the same checksum in all modes. */
static completion process_payload(const void *data, size_t bytes) {
    completion result = {0u, UINT64_C(0x9e3779b97f4a7c15)};
    for (size_t offset = 0u; offset < bytes; offset += sizeof(uint64_t)) {
        uint64_t word;
        memcpy(&word, (const unsigned char *)data + offset, sizeof(word));
        if (offset == 0u) result.id = word;
        result.digest = (result.digest << 7) | (result.digest >> 57);
        result.digest ^= word;
    }
    return result;
}

static void wake_all_locked(bench_context *context) {
    cmeta_cond_broadcast(&context->changed);
    for (size_t index = 0u; index < context->worker_count; ++index)
        if (context->workers[index].wake != NULL)
            cmeta_cond_signal(&context->workers[index].wake);
}

static void fail_locked(bench_context *context) {
    context->failed = true;
    wake_all_locked(context);
}

static bool wait_locked(bench_context *context) {
    if (cmeta_monotonic_ms() >= context->deadline) {
        fail_locked(context);
        return false;
    }
    (void)cmeta_cond_timedwait(&context->changed, &context->lock, UINT64_C(10000000));
    return !context->failed;
}

/* O(worker_count), bounded by four. Election is explicit, before LF handling
 * and after token handling; workers never race for an unassigned leader slot. */
static void elect_locked(bench_context *context) {
    if (context->leader != SIZE_MAX) return;
    for (size_t step = 1u; step <= context->worker_count; ++step) {
        const size_t index = (context->last_leader + step) % context->worker_count;
        if (!context->workers[index].eligible) continue;
        context->leader = context->last_leader = index;
        ++context->handoffs;
        cmeta_cond_signal(&context->workers[index].wake);
        return;
    }
}

static void settle_locked(bench_context *context, completion result) {
    if (result.id >= MESSAGES || result.digest != context->expected[result.id] ||
        context->observations[result.id] >= context->epoch) {
        fail_locked(context);
        return;
    }
    ++context->observations[result.id];
    ++context->completed;
    if (context->completed % MESSAGES == 0u)
        cmeta_cond_broadcast(&context->changed);
}

static bool actor_action(void *user, const void *state, const void *event,
                         void *target, void *observation, const char **error) {
    bench_context *context = (bench_context *)user;
    const completion result = process_payload(event, context->payload_type->size);
    (void)error;
    *(int *)target = *(const int *)state + 1;
    if (context->mode == ACTOR_VALUE) {
        *(completion *)observation = result;
    } else {
        cmeta_mutex_lock(&context->lock);
        settle_locked(context, result);
        cmeta_mutex_unlock(&context->lock);
    }
    return true;
}

static bool actor_value(void *user, const cmeta_type_desc *type, const void *value) {
    bench_context *context = (bench_context *)user;
    cmeta_mutex_lock(&context->lock);
    if (type != &completion_type) fail_locked(context);
    else settle_locked(context, *(const completion *)value);
    cmeta_mutex_unlock(&context->lock);
    return true;
}

static void actor_error(void *user, const char *message) {
    bench_context *context = (bench_context *)user;
    (void)message;
    cmeta_mutex_lock(&context->lock);
    fail_locked(context);
    cmeta_mutex_unlock(&context->lock);
}

static void run_worker(void *user) {
    worker *self = (worker *)user;
    bench_context *context = self->context;
    size_t held = 0u;
    cmeta_mutex_lock(&context->lock);
    self->eligible = true;
    ++context->ready;
    elect_locked(context);
    cmeta_cond_broadcast(&context->changed);
    while (!context->closing && !context->failed) {
        payload_storage payload;
        cflow_event_id id;
        const cmeta_type_desc *type;
        size_t ticket;
        if (context->leader != self->id || context->queued == 0u) {
            cmeta_cond_wait(&self->wake, &context->lock);
            continue;
        }
        if (cflow_mailbox_try_receive(&context->mailbox, &id, &type,
                                      &payload, sizeof(payload)) != CFLOW_MAILBOX_OK ||
            id != 1u || type != context->payload_type) {
            fail_locked(context);
            break;
        }
        ticket = context->dequeued++;
        --context->queued;
        ++context->active;
        if (context->active > context->peak_active) context->peak_active = context->active;
        self->eligible = false;
        if (context->mode == LEADER_FOLLOWERS) {
            context->leader = SIZE_MAX;
            elect_locked(context);
        }
        cmeta_mutex_unlock(&context->lock);
        if (context->hold_first && ticket == 0u) {
            cmeta_mutex_lock(&context->lock);
            context->held = true;
            cmeta_cond_broadcast(&context->changed);
            while (!context->release && !context->closing && !context->failed)
                (void)wait_locked(context);
            cmeta_mutex_unlock(&context->lock);
        }
        const completion result = process_payload(&payload, context->payload_type->size);
        cmeta_mutex_lock(&context->lock);
        if (context->mode == SERIAL_TOKEN && ticket != context->next_settlement++)
            fail_locked(context);
        settle_locked(context, result);
        --context->active;
        self->eligible = true;
        if (context->mode == SERIAL_TOKEN) {
            ++held;
            if (held >= context->batch || context->queued == 0u) {
                held = 0u;
                context->leader = SIZE_MAX;
                elect_locked(context);
            }
        } else elect_locked(context);
    }
    cmeta_mutex_unlock(&context->lock);
}

/* Only one logical dequeue owner is active even when LF handlers overlap.
 * The prototype's monitor additionally serializes enqueue with election. */
static int send_payload(producer *self, const cflow_event_view *event) {
    bench_context *context = self->context;
    if (context->mode == ACTOR_NONE || context->mode == ACTOR_VALUE) {
        const cflow_actor_send_status result = cflow_actor_ref_try_send(&self->ref, event);
        if (result == CFLOW_ACTOR_SEND_ACCEPTED) return 1;
        if (result == CFLOW_ACTOR_SEND_FULL) return 0;
        return -1;
    }
    cmeta_mutex_lock(&context->lock);
    int result = -1;
    if (!context->failed && !context->closing) {
        const cflow_mailbox_status status = cflow_mailbox_try_send(&context->mailbox, event);
        if (status == CFLOW_MAILBOX_OK) {
            ++context->queued;
            ++context->accepted;
            if (context->leader != SIZE_MAX)
                cmeta_cond_signal(&context->workers[context->leader].wake);
            result = 1;
        } else if (status == CFLOW_MAILBOX_FULL) result = 0;
    }
    cmeta_mutex_unlock(&context->lock);
    return result;
}

static void run_producer(void *user) {
    producer *self = (producer *)user;
    bench_context *context = self->context;
    size_t last_epoch = 0u;
    cmeta_mutex_lock(&context->lock);
    ++context->ready;
    cmeta_cond_broadcast(&context->changed);
    while (!context->closing && !context->failed) {
        while (context->epoch == last_epoch && !context->closing && !context->failed)
            cmeta_cond_wait(&context->changed, &context->lock);
        if (context->closing || context->failed) break;
        last_epoch = context->epoch;
        const uint64_t deadline = context->deadline;
        cmeta_mutex_unlock(&context->lock);
        bool valid = true;
        for (size_t id = self->id; id < MESSAGES && valid; id += context->producer_count) {
            const cflow_event_view event = {1u, context->payload_type, &context->payloads[id]};
            for (;;) {
                const int result = send_payload(self, &event);
                if (result == 1) break;
                if (result < 0 || cmeta_monotonic_ms() >= deadline) {
                    valid = false;
                    break;
                }
                ++self->full_retries;
                cmeta_thread_yield();
            }
        }
        cmeta_mutex_lock(&context->lock);
        if (!valid) fail_locked(context);
        ++context->producers_done;
        cmeta_cond_broadcast(&context->changed);
    }
    cmeta_mutex_unlock(&context->lock);
}

static bool init_actor(bench_context *context) {
    const bool emit = context->mode == ACTOR_VALUE;
    const cflow_machine_state state = {1u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE};
    const cflow_event_type event = {1u, context->payload_type};
    const cflow_machine_action action = {
        1u, &cmeta_type_int, 1u, context->payload_type, &cmeta_type_int,
        CMETA_EFFECT_STATEFUL, CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        emit ? CFLOW_MACHINE_ACTION_VALUE : CFLOW_MACHINE_ACTION_NONE,
        emit ? &completion_type : NULL, 0u};
    const cflow_machine_transition transition = {1u, 1u, 0u, 1u, 1u, 0u};
    const cflow_machine_definition definition = {
        &state, 1u, 1u, &event, 1u, NULL, 0u, &action, 1u, &transition, 1u};
    const cflow_machine_action_binding binding = {1u, actor_action, context};
    const int initial = 0;
    cflow_actor_config config = {0};
    if (cflow_machine_build(&context->machine, &definition) != CFLOW_MACHINE_OK ||
        !cflow_executor_serial_init(&context->executor) ||
        !cflow_scheduler_worker_init(&context->scheduler, 1u)) return false;
    config.machine = (cflow_machine_instance_config){
        &context->machine, &initial, emit ? &completion_type : &cmeta_type_int, NULL, 0u,
        &binding, 1u, CAPACITY, &context->executor};
    config.scheduler = &context->scheduler;
    config.callbacks = (cflow_subscriber_callbacks){actor_value, actor_error, NULL, context};
    if (cflow_actor_init(&context->actor, &config).status != CFLOW_ACTOR_OK ||
        cflow_actor_start(&context->actor) != CFLOW_ACTOR_OK) return false;
    return cflow_scheduler_wait_idle(&context->scheduler) &&
           cflow_executor_wait_idle(&context->executor);
}

static bool init_case(enum dispatch_mode mode, size_t producers, size_t workers,
                       size_t batch, const cmeta_type_desc *type) {
    current = (bench_context *)calloc(1u, sizeof(*current));
    if (current == NULL) return false;
    bench_context *context = current;
    context->mode = mode;
    context->producer_count = producers;
    context->worker_count = workers;
    context->batch = batch;
    context->payload_type = type;
    context->leader = SIZE_MAX;
    context->last_leader = workers == 0u ? 0u : workers - 1u;
    context->deadline = cmeta_monotonic_ms() + 10000u;
    cmeta_mutex_init(&context->lock);
    cmeta_cond_init(&context->changed);
    if (context->lock == NULL || context->changed == NULL) return false;
    for (size_t id = 0u; id < MESSAGES; ++id) {
        for (size_t index = 0u; index < type->size / sizeof(uint64_t); ++index)
            context->payloads[id].large.words[index] =
                index == 0u ? id : UINT64_C(0xd6e8feb86659fd93) * (id + index);
        context->expected[id] = process_payload(&context->payloads[id], type->size).digest;
    }
    if (mode == ACTOR_NONE || mode == ACTOR_VALUE) {
        if (!init_actor(context)) return false;
    } else {
        const cflow_event_type event = {1u, type};
        if (cflow_mailbox_init(&context->mailbox, &event, 1u, CAPACITY) != CFLOW_MAILBOX_OK)
            return false;
        for (size_t index = 0u; index < workers; ++index) {
            worker *self = &context->workers[index];
            self->context = context;
            self->id = index;
            cmeta_cond_init(&self->wake);
            if (self->wake == NULL ||
                cmeta_thread_create(&self->thread, run_worker, self) != 0) return false;
        }
    }
    for (size_t index = 0u; index < producers; ++index) {
        producer *self = &context->producers[index];
        self->context = context;
        self->id = index;
        if (context->actor.impl != NULL && !cflow_actor_ref_acquire(&context->actor, &self->ref))
            return false;
        if (cmeta_thread_create(&self->thread, run_producer, self) != 0) return false;
    }
    cmeta_mutex_lock(&context->lock);
    while (context->ready < producers + workers && !context->failed) (void)wait_locked(context);
    const bool valid = !context->failed;
    cmeta_mutex_unlock(&context->lock);
    return valid;
}

static void run_sample(void) {
    bench_context *context = current;
    cmeta_mutex_lock(&context->lock);
    context->deadline = cmeta_monotonic_ms() + 10000u;
    context->producers_done = 0u;
    ++context->epoch;
    cmeta_cond_broadcast(&context->changed);
    while (!context->failed &&
           (context->producers_done < context->producer_count ||
            context->completed < context->epoch * MESSAGES)) (void)wait_locked(context);
    cmeta_mutex_unlock(&context->lock);
    /* Do not report a partial batch as a full throughput sample. */
    check_false(context->failed);
}

static void destroy_case(void) {
    bench_context *context = current;
    if (context == NULL) return;
    if (context->lock != NULL) {
        cmeta_mutex_lock(&context->lock);
        context->closing = true;
        if (context->changed != NULL) wake_all_locked(context);
        cmeta_mutex_unlock(&context->lock);
    }
    for (size_t index = 0u; index < context->producer_count; ++index) {
        producer *self = &context->producers[index];
        if (self->thread != NULL && cmeta_thread_join(&self->thread) != 0) abort();
        cflow_actor_ref_release(&self->ref);
    }
    for (size_t index = 0u; index < context->worker_count; ++index) {
        worker *self = &context->workers[index];
        if (self->thread != NULL && cmeta_thread_join(&self->thread) != 0) abort();
        cmeta_cond_destroy(&self->wake);
    }
    if (context->actor.impl != NULL) {
        (void)cflow_actor_request_stop(&context->actor);
        (void)cflow_actor_wait(&context->actor);
        cflow_actor_destroy(&context->actor);
    }
    cflow_scheduler_destroy(&context->scheduler);
    cflow_executor_destroy(&context->executor);
    cflow_machine_destroy(&context->machine);
    cflow_mailbox_destroy(&context->mailbox);
    cmeta_cond_destroy(&context->changed);
    cmeta_mutex_destroy(&context->lock);
    free(context);
    current = NULL;
    tstr_freep(&title);
}

static void verify_case(size_t samples) {
    bench_context *context = current;
    cflow_mailbox_stats mailbox = {0};
    const size_t expected = samples * MESSAGES;
    /* NONE completion is counted within the action, before Machine accounting
     * commits. Wait for real executor/scheduler quiescence before snapshot. */
    if (context->actor.impl != NULL) {
        cflow_actor_stats stats = {0};
        check_true(cflow_executor_wait_idle(&context->executor));
        check_true(cflow_scheduler_wait_idle(&context->scheduler));
        check_true(cflow_actor_get_stats(&context->actor, &stats));
        check_equal(stats.machine.completed, (uint64_t)expected);
        check_equal(stats.machine.failed, (uint64_t)0u);
        check_equal(stats.machine.accepted, (uint64_t)expected);
        check_equal(stats.machine.pending, (size_t)0u);
        check_equal(stats.machine.in_flight, (size_t)0u);
        check_equal(stats.machine.emitted_values,
                    context->mode == ACTOR_VALUE ? (uint64_t)expected : (uint64_t)0u);
    } else {
        check_equal(context->accepted, expected);
        check_equal(context->dequeued, expected);
        check_equal(context->active, (size_t)0u);
        check_equal(context->queued, (size_t)0u);
        check_true(cflow_mailbox_get_stats(&context->mailbox, &mailbox));
        check_less_equal(context->peak_active, context->worker_count);
        if (context->mode == SERIAL_TOKEN) {
            check_equal(context->peak_active, (size_t)1u);
            check_equal(context->next_settlement, expected);
        }
        check_greater(context->handoffs, (size_t)0u);
        check_equal(mailbox.accepted, (uint64_t)expected);
        check_equal(mailbox.received, (uint64_t)expected);
        check_equal(mailbox.pending, (size_t)0u);
        check_less_equal(mailbox.peak_pending, (size_t)CAPACITY);
        uint64_t full_retries = 0u;
        for (size_t index = 0u; index < context->producer_count; ++index)
            full_retries += context->producers[index].full_retries;
        check_equal(mailbox.rejected_full, full_retries);
    }
    check_equal(context->completed, expected);
    for (size_t id = 0u; id < MESSAGES; ++id)
        check_equal(context->observations[id], samples);
}

suite("ACE CPU payload throughput, bounded admission to verified completion") {
    after_each() { destroy_case(); }
    it("promotes a follower before handling, but transfers a serial token after handling") {
        for (size_t mode = 0u; mode < 2u; ++mode) {
            const bool serial = mode == 1u;
            check_true(init_case(serial ? SERIAL_TOKEN : LEADER_FOLLOWERS,
                                  1u, 2u, 1u, &type64));
            bench_context *context = current;
            cmeta_mutex_lock(&context->lock);
            context->hold_first = true;
            ++context->epoch;
            cmeta_cond_broadcast(&context->changed);
            while (!context->held && !context->failed) (void)wait_locked(context);
            bool observed_contract;
            if (serial) {
                while (context->queued < CAPACITY && !context->failed) (void)wait_locked(context);
                observed_contract = context->completed == 0u && context->active == 1u;
            } else {
                while (context->completed == 0u && !context->failed) (void)wait_locked(context);
                observed_contract = context->completed > 0u && context->peak_active == 2u;
            }
            context->release = true;
            cmeta_cond_broadcast(&context->changed);
            while (!context->failed &&
                   (context->producers_done != 1u || context->completed != MESSAGES))
                (void)wait_locked(context);
            cmeta_mutex_unlock(&context->lock);
            check_false(context->failed);
            check_true(observed_contract);
            verify_case(1u);
            if (serial) {
                cflow_mailbox_stats stats = {0};
                check_true(cflow_mailbox_get_stats(&context->mailbox, &stats));
                check_equal(stats.peak_pending, (size_t)CAPACITY);
            }
            destroy_case();
        }
    }
    bench("compares Active Object, Leader/Followers and serial token handoff") {
        static const struct {
            const char *name;
            enum dispatch_mode mode;
            size_t workers, batch;
        } cases[] = {
            {"Actor NONE", ACTOR_NONE, 0u, 1u},
            {"Actor VALUE", ACTOR_VALUE, 0u, 1u},
            {"Leader/Followers", LEADER_FOLLOWERS, 1u, 1u},
            {"Leader/Followers", LEADER_FOLLOWERS, 2u, 1u},
            {"Leader/Followers", LEADER_FOLLOWERS, 4u, 1u},
            {"Serial token", SERIAL_TOKEN, 4u, 1u},
            {"Serial token", SERIAL_TOKEN, 4u, 32u}};
        const cmeta_type_desc *types[] = {&type64, &type1024};
        for (size_t payload = 0u; payload < 2u; ++payload) {
            for (size_t producers = 1u; producers <= MAX_THREADS; producers *= MAX_THREADS) {
                for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
                    check_true(init_case(cases[index].mode, producers, cases[index].workers,
                                          cases[index].batch, types[payload]));
                    run_sample(); /* Warm each topology once, outside measurement. */
                    title = tstr_format("{} p={} w={} batch={} bytes={}", cases[index].name,
                                        producers, cases[index].workers, cases[index].batch,
                                        types[payload]->size);
                    check_not_null(title);
                    benchmark_io(title, SAMPLES, MESSAGES, MESSAGES * types[payload]->size) {
                        run_sample();
                    }
                    verify_case(SAMPLES + 1u);
                    destroy_case();
                }
            }
        }
    }
}
