#include <cflow/cflow.h>
#include "tinytest.h"

/*
 * ACE Pipes and Filters: CMeta Function filter/map stages + the existing
 * fully reflected cflow_publisher/cflow_subscriber Interfaces.
 *
 * CFlow owns Graph, Subscription demand/cancellation and Channel capacity.
 * Sources, sink contexts and their type descriptors are borrowed until close.
 * No private runtime headers, duplicate pipeline or global mutable state.
 */
cmeta_function(filter, value, bool, ace_pipes_keep_even, (int value)) {
    return value % 2 == 0;
}

cmeta_function(map, value, int, ace_pipes_add_ten, (int value)) {
    return value + 10;
}

typedef struct ace_pipe_source_state {
    const int *items;
    size_t count;
    size_t next;
    size_t resumes;
    size_t last_demands[8];
    size_t cancels;
    size_t destroys;
} ace_pipe_source_state;

typedef struct ace_pipe_sink_state {
    int values[8];
    size_t count;
    size_t errors;
    size_t dones;
    size_t after_lifetime;
    bool live;
} ace_pipe_sink_state;

static const char *ace_pipe_source_name(void *self) {
    (void)self;
    return "ace-pipes-source";
}

static const cmeta_type_desc *ace_pipe_source_type(void *self) {
    (void)self;
    return &cmeta_type_int;
}

static cflow_step ace_pipe_source_resume(
    void *self, cflow_publish_context *ctx, void *out_value) {
    ace_pipe_source_state *owner = (ace_pipe_source_state *)self;
    if (owner == NULL || ctx == NULL || out_value == NULL ||
        owner->next >= owner->count ||
        owner->resumes >= sizeof(owner->last_demands) /
                          sizeof(owner->last_demands[0]))
        return (cflow_step){CFLOW_STEP_ERROR, {0}, "source bound exhausted"};
    owner->last_demands[owner->resumes] = ctx->downstream_demand;
    ++owner->resumes;
    *(int *)out_value = owner->items[owner->next++];
    return (cflow_step){
        owner->next == owner->count ? CFLOW_STEP_VALUE_AND_DONE
                                   : CFLOW_STEP_VALUE,
        {0}, NULL};
}

static void ace_pipe_source_cancel(void *self) {
    ++((ace_pipe_source_state *)self)->cancels;
}

static void ace_pipe_source_destroy(void *self) {
    ++((ace_pipe_source_state *)self)->destroys;
}

static void ace_pipe_source_bind_waker(void *self, cflow_waker waker) {
    (void)self;
    (void)waker;
}

static cflow_publisher_terminal ace_pipe_source_terminal(
    void *self, const char **error) {
    const ace_pipe_source_state *owner = (const ace_pipe_source_state *)self;
    if (error != NULL) *error = NULL;
    return owner->next == owner->count ? CFLOW_PUBLISHER_DONE
                                      : CFLOW_PUBLISHER_OPEN;
}

CMETA_IMPLEMENTS(cflow_publisher, ace_pipe_source_impl, 0u,
    .name = ace_pipe_source_name,
    .output_type = ace_pipe_source_type,
    .resume = ace_pipe_source_resume,
    .cancel = ace_pipe_source_cancel,
    .destroy = ace_pipe_source_destroy,
    .bind_terminal_waker = ace_pipe_source_bind_waker,
    .poll_terminal = ace_pipe_source_terminal);

static bool ace_pipe_sink_value(
    void *self, const cmeta_type_desc *type, const void *value) {
    ace_pipe_sink_state *state = (ace_pipe_sink_state *)self;
    if (!state->live) {
        ++state->after_lifetime;
        return false;
    }
    if (!cmeta_type_equal(type, &cmeta_type_int) || value == NULL ||
        state->count >= sizeof(state->values) / sizeof(state->values[0]))
        return false;
    state->values[state->count++] = *(const int *)value;
    return true;
}

static void ace_pipe_sink_error(void *self, const char *message) {
    ace_pipe_sink_state *state = (ace_pipe_sink_state *)self;
    if (!state->live) ++state->after_lifetime;
    if (message != NULL) ++state->errors;
}

static void ace_pipe_sink_done(void *self) {
    ace_pipe_sink_state *state = (ace_pipe_sink_state *)self;
    if (!state->live) ++state->after_lifetime;
    ++state->dones;
}

CMETA_IMPLEMENTS(cflow_subscriber, ace_pipe_sink_impl, 0u,
    .value = ace_pipe_sink_value,
    .error = ace_pipe_sink_error,
    .done = ace_pipe_sink_done);

static bool ace_pipes_graph_init(
    cflow_graph *surface, cflow_graph *normalized) {
    normalized->root = CMETA_INVALID_ID;
    cflow_graph_init(surface, &cmeta_type_int);
    return cflow_graph_add(
        surface, CFLOW_OP_FILTER, ace_pipes_keep_even.fn, NULL) &&
        cflow_graph_add(
        surface, CFLOW_OP_MAP, ace_pipes_add_ten.fn, NULL) &&
        cflow_graph_normalize(normalized, surface);
}

suite("ACE Pipes and Filters typed Graph / demand / ownership") {
    it("pulls only on downstream demand and preserves ordered filter-map stages") {
        const int input[] = {1, 2, 3, 4, 5, 6};
        ace_pipe_source_state source_state = {
            input, sizeof(input) / sizeof(input[0]), 0u, 0u, {0}, 0u, 0u
        };
        ace_pipe_sink_state sink_state = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_subscription run = {0};
        cflow_publisher source =
            ace_pipe_source_impl_as_cflow_publisher(&source_state);
        cflow_subscriber sink =
            ace_pipe_sink_impl_as_cflow_subscriber(&sink_state);

        sink_state.live = true;
        check_true(cmeta_interface_desc_valid(cflow_publisher_interface()));
        check_true(cmeta_interface_desc_valid(cflow_subscriber_interface()));
        check_true(cmeta_interface_method_reflection_valid(
            &cflow_publisher_interface()->methods[2]));
        check_true(cmeta_interface_method_reflection_valid(
            &cflow_subscriber_interface()->methods[0]));
        check_true(cflow_publisher_valid(&source));
        check_true(cflow_subscriber_valid(&sink));
        check_true(ace_pipes_graph_init(&surface, &normalized));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_subscribe(
            &run, &normalized, &source, &scheduler, &sink));
        check_null(source.self);

        /* No eager pipeline work and no unbounded internal prefetch. */
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(source_state.resumes, (size_t)0u);
        check_equal(sink_state.count, (size_t)0u);

        check_equal(cflow_subscription_request_result(&run, 1u).status,
                    CFLOW_STATUS_OK);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(source_state.resumes, (size_t)2u);
        check_equal(sink_state.count, (size_t)1u);
        check_equal(sink_state.values[0], 12);
        check_equal(source_state.last_demands[0], (size_t)1u);
        check_equal(source_state.last_demands[1], (size_t)1u);
        check_equal(cflow_subscription_outstanding_demand(&run), (size_t)0u);

        check_true(cflow_subscription_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(source_state.resumes, (size_t)4u);
        check_equal(sink_state.count, (size_t)2u);
        check_equal(sink_state.values[1], 14);

        check_true(cflow_subscription_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(source_state.resumes, (size_t)6u);
        check_equal(sink_state.count, (size_t)3u);
        check_equal(sink_state.values[2], 16);
        check_true(cflow_subscription_is_done(&run));
        check_null(cflow_subscription_error(&run));
        check_equal(sink_state.errors, (size_t)0u);
        check_equal(sink_state.dones, (size_t)1u);

        cflow_subscription_close(&run);
        check_null(run.impl);
        check_equal(source_state.destroys, (size_t)1u);
        cflow_subscription_close(&run);
        check_equal(source_state.destroys, (size_t)1u);
        sink_state.live = false;
        check_equal(sink_state.after_lifetime, (size_t)0u);

        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("cancels a partially consumed source and settles it once") {
        const int input[] = {1, 2, 3, 4, 5, 6};
        ace_pipe_source_state source_state = {
            input, sizeof(input) / sizeof(input[0]), 0u, 0u, {0}, 0u, 0u
        };
        ace_pipe_sink_state sink_state = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_subscription run = {0};
        cflow_publisher source =
            ace_pipe_source_impl_as_cflow_publisher(&source_state);
        cflow_subscriber sink =
            ace_pipe_sink_impl_as_cflow_subscriber(&sink_state);
        size_t delivered;

        sink_state.live = true;
        check_true(ace_pipes_graph_init(&surface, &normalized));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_subscribe(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_subscription_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(source_state.resumes, (size_t)2u);
        check_equal(sink_state.count, (size_t)1u);
        delivered = sink_state.count;

        cflow_subscription_cancel(&run);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_true(cflow_subscription_is_cancelled(&run));
        check_equal(cflow_subscription_request_result(&run, 1u).status,
                    CFLOW_STATUS_CANCELLED);
        check_equal(source_state.resumes, (size_t)2u);
        check_equal(sink_state.count, delivered);
        cflow_subscription_close(&run);
        check_null(run.impl);
        check_equal(source_state.destroys, (size_t)1u);
        cflow_subscription_close(&run);
        check_equal(source_state.destroys, (size_t)1u);
        check_equal(sink_state.count, delivered);
        sink_state.live = false;
        check_equal(sink_state.after_lifetime, (size_t)0u);

        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("uses a real capacity-two Channel as the bounded pipeline source") {
        cflow_channel channel = {0};
        cflow_channel_stats stats = {0};
        cflow_publisher source = {0};
        ace_pipe_sink_state sink_state = {0};
        cflow_subscriber sink =
            ace_pipe_sink_impl_as_cflow_subscriber(&sink_state);
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_subscription run = {0};
        const int one = 1, two = 2, third = 3;
        size_t delivered;

        sink_state.live = true;
        check_true(ace_pipes_graph_init(&surface, &normalized));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_channel_init(&channel, &cmeta_type_int, 2u));
        check_true(cflow_publisher_from_channel(&source, &channel));
        check_true(cflow_subscribe(
            &run, &normalized, &source, &scheduler, &sink));
        check_null(source.self);
        check_equal(cflow_channel_try_push(&channel, &one), CFLOW_CHANNEL_OK);
        check_equal(cflow_channel_try_push(&channel, &two), CFLOW_CHANNEL_OK);
        check_equal(cflow_channel_try_push(&channel, &third), CFLOW_CHANNEL_FULL);
        check_true(cflow_channel_get_stats(&channel, &stats));
        check_equal(stats.capacity, (size_t)2u);
        check_equal(stats.pending, (size_t)2u);
        check_equal(stats.peak_pending, (size_t)2u);
        check_equal(stats.rejected_full, (uint64_t)1u);

        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(sink_state.count, (size_t)0u);
        check_true(cflow_subscription_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(sink_state.count, (size_t)1u);
        check_equal(sink_state.values[0], 12);
        check_true(cflow_channel_get_stats(&channel, &stats));
        check_equal(stats.received, (uint64_t)2u);
        check_equal(stats.pending, (size_t)0u);
        check_true(stats.peak_pending <= stats.capacity);

        delivered = sink_state.count;
        cflow_subscription_cancel(&run);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        cflow_subscription_close(&run);
        check_equal(sink_state.count, delivered);
        cflow_channel_close(&channel);
        check_equal(cflow_channel_try_push(&channel, &third),
                    CFLOW_CHANNEL_CLOSED);
        cflow_channel_destroy(&channel);
        sink_state.live = false;
        check_equal(sink_state.after_lifetime, (size_t)0u);

        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }
}
