#include <cflow/actor.h>

#include <cflow/lower.h>

#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum cflow_actor_backend {
    CFLOW_ACTOR_BACKEND_MACHINE = 0,
    CFLOW_ACTOR_BACKEND_STATECHART
} cflow_actor_backend;

typedef struct cflow_actor_impl {
    atomic_size_t refs;
    turbo_mutex_t gate;
    turbo_cond_t changed;
    cflow_actor_state state;
    /* FAILED stays externally visible while waiters remain behind settlement. */
    bool terminal_settled;
    bool stale;
    cflow_actor_backend backend;
    cflow_machine_instance machine;
    cflow_graph graph;
    cflow_run run;
    cflow_scheduler *scheduler;
    cflow_statechart_instance statechart;
    cflow_waitable statechart_terminal;
    cflow_sink_callbacks callbacks;
    cflow_sink_callbacks bridge_callbacks;
    char *error;
    uint64_t rejected_not_started;
    uint64_t rejected_stopping;
    uint64_t rejected_stopped;
    uint64_t rejected_failed;
    uint64_t rejected_stale;
} cflow_actor_impl;

static const char actor_generic_failure[] = "actor failed";
static const char actor_unexpected_done[] =
    "Run completed while Actor was RUNNING";
static const char actor_statechart_waitable_failure[] =
    "actor could not arm Statechart terminal projection";

static char *actor_copy_error(const char *message) {
    const char *source = message != NULL && message[0] != '\0'
        ? message : actor_generic_failure;
    const size_t length = strlen(source);
    char *copy;
    if (length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1u);
    if (copy != NULL) memcpy(copy, source, length + 1u);
    return copy;
}

static void actor_shell_destroy(cflow_actor_impl *impl) {
    if (impl == NULL) return;
    free(impl->error);
    turbo_cond_destroy(&impl->changed);
    turbo_mutex_destroy(&impl->gate);
    free(impl);
}

static void actor_release(cflow_actor_impl *impl) {
    if (impl != NULL &&
        atomic_fetch_sub_explicit(&impl->refs, 1u, memory_order_acq_rel) == 1u)
        actor_shell_destroy(impl);
}

static bool actor_retain(cflow_actor_impl *impl) {
    size_t current;
    if (impl == NULL) return false;
    current = atomic_load_explicit(&impl->refs, memory_order_relaxed);
    while (current != 0u && current != SIZE_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                &impl->refs, &current, current + 1u,
                memory_order_relaxed, memory_order_relaxed))
            return true;
    }
    return false;
}

static cflow_actor_impl *actor_shell_create(cflow_actor_backend backend) {
    cflow_actor_impl *impl = (cflow_actor_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return NULL;
    atomic_init(&impl->refs, 1u);
    impl->backend = backend;
    impl->graph.root = CMETA_INVALID_ID;
    impl->state = CFLOW_ACTOR_STATE_START;
    turbo_mutex_init(&impl->gate);
    turbo_cond_init(&impl->changed);
    if (impl->gate == NULL || impl->changed == NULL) {
        actor_shell_destroy(impl);
        return NULL;
    }
    return impl;
}

static void actor_cancel_backend(cflow_actor_impl *impl) {
    if (impl->backend == CFLOW_ACTOR_BACKEND_STATECHART)
        cflow_statechart_instance_cancel(&impl->statechart);
    else
        cflow_machine_instance_cancel(&impl->machine);
}

static void actor_close_backend(cflow_actor_impl *impl) {
    if (impl->backend == CFLOW_ACTOR_BACKEND_STATECHART)
        cflow_statechart_instance_close(&impl->statechart);
    else
        cflow_machine_instance_close(&impl->machine);
}

static void actor_mark_failed(cflow_actor_impl *impl, const char *message) {
    char *copy;
    bool first = false;
    if (impl == NULL) return;
    copy = actor_copy_error(message);
    turbo_mutex_lock(&impl->gate);
    if (impl->state != CFLOW_ACTOR_STATE_STOPPED &&
        impl->state != CFLOW_ACTOR_STATE_FAILED) {
        impl->terminal_settled = false;
        impl->state = CFLOW_ACTOR_STATE_FAILED;
        first = true;
        turbo_cond_broadcast(&impl->changed);
    }
    if (impl->error == NULL && copy != NULL) {
        impl->error = copy;
        copy = NULL;
    }
    turbo_mutex_unlock(&impl->gate);
    free(copy);
    if (first) {
        actor_cancel_backend(impl);
        turbo_mutex_lock(&impl->gate);
        impl->terminal_settled = true;
        turbo_cond_broadcast(&impl->changed);
        turbo_mutex_unlock(&impl->gate);
    }
}

static void actor_statechart_terminal_wake(void *user) {
    cflow_actor_impl *impl = (cflow_actor_impl *)user;
    cflow_statechart_terminal_status terminal;
    cflow_error_fn error_callback = NULL;
    cflow_done_fn done_callback = NULL;
    void *callback_user;
    const char *runtime_error = NULL;
    const char *callback_error = NULL;
    char *copy = NULL;

    if (impl == NULL) return;
    terminal = cflow_statechart_instance_poll_terminal(
        &impl->statechart, &runtime_error);
    if (terminal == CFLOW_STATECHART_TERMINAL_ERROR)
        copy = actor_copy_error(runtime_error);

    turbo_mutex_lock(&impl->gate);
    callback_user = impl->callbacks.user;
    if (terminal == CFLOW_STATECHART_TERMINAL_DONE &&
        (impl->state == CFLOW_ACTOR_STATE_RUNNING ||
         impl->state == CFLOW_ACTOR_STATE_STOPPING)) {
        impl->state = CFLOW_ACTOR_STATE_STOPPED;
        impl->terminal_settled = true;
        done_callback = impl->callbacks.on_done;
        turbo_cond_broadcast(&impl->changed);
    } else if (terminal == CFLOW_STATECHART_TERMINAL_ERROR &&
               impl->state != CFLOW_ACTOR_STATE_STOPPED &&
               impl->state != CFLOW_ACTOR_STATE_FAILED) {
        if (impl->error == NULL && copy != NULL) {
            impl->error = copy;
            copy = NULL;
        }
        callback_error = impl->error != NULL
            ? impl->error : actor_generic_failure;
        impl->state = CFLOW_ACTOR_STATE_FAILED;
        impl->terminal_settled = true;
        error_callback = impl->callbacks.on_error;
        turbo_cond_broadcast(&impl->changed);
    }
    turbo_mutex_unlock(&impl->gate);
    free(copy);

    if (error_callback != NULL)
        error_callback(callback_user, callback_error);
    if (done_callback != NULL) done_callback(callback_user);
}

static bool actor_sink_value(void *user,
                             const cmeta_type_desc *type,
                             const void *value) {
    cflow_actor_impl *impl = (cflow_actor_impl *)user;
    cflow_value_fn callback = impl != NULL ? impl->callbacks.on_value : NULL;
    return callback == NULL || callback(impl->callbacks.user, type, value);
}

static void actor_sink_error(void *user, const char *message) {
    cflow_actor_impl *impl = (cflow_actor_impl *)user;
    cflow_error_fn callback;
    void *callback_user;
    if (impl == NULL) return;
    actor_mark_failed(impl, message);
    callback = impl->callbacks.on_error;
    callback_user = impl->callbacks.user;
    if (callback != NULL) callback(callback_user, message);
}

static void actor_sink_done(void *user) {
    cflow_actor_impl *impl = (cflow_actor_impl *)user;
    cflow_done_fn done_callback = NULL;
    cflow_error_fn error_callback = NULL;
    void *callback_user;
    char *copy = NULL;
    const char *error = NULL;
    bool running;
    bool failed = false;
    if (impl == NULL) return;

    turbo_mutex_lock(&impl->gate);
    running = impl->state == CFLOW_ACTOR_STATE_RUNNING;
    turbo_mutex_unlock(&impl->gate);
    if (running) copy = actor_copy_error(actor_unexpected_done);

    turbo_mutex_lock(&impl->gate);
    if (impl->state == CFLOW_ACTOR_STATE_STOPPING) {
        impl->state = CFLOW_ACTOR_STATE_STOPPED;
        turbo_cond_broadcast(&impl->changed);
        done_callback = impl->callbacks.on_done;
    } else if (impl->state == CFLOW_ACTOR_STATE_RUNNING) {
        if (impl->error == NULL && copy != NULL) {
            impl->error = copy;
            copy = NULL;
        }
        error = impl->error != NULL ? impl->error : actor_generic_failure;
        impl->terminal_settled = false;
        impl->state = CFLOW_ACTOR_STATE_FAILED;
        turbo_cond_broadcast(&impl->changed);
        error_callback = impl->callbacks.on_error;
        failed = true;
    }
    callback_user = impl->callbacks.user;
    turbo_mutex_unlock(&impl->gate);
    free(copy);

    if (failed) {
        cflow_machine_instance_cancel(&impl->machine);
        turbo_mutex_lock(&impl->gate);
        impl->terminal_settled = true;
        turbo_cond_broadcast(&impl->changed);
        turbo_mutex_unlock(&impl->gate);
    }
    if (error_callback != NULL) error_callback(callback_user, error);
    if (done_callback != NULL) done_callback(callback_user);
}

static cflow_actor_status actor_state_status(cflow_actor_state state) {
    switch (state) {
        case CFLOW_ACTOR_STATE_RUNNING:
            return CFLOW_ACTOR_ALREADY_STARTED;
        case CFLOW_ACTOR_STATE_STOPPING:
            return CFLOW_ACTOR_STOPPING;
        case CFLOW_ACTOR_STATE_STOPPED:
            return CFLOW_ACTOR_STOPPED;
        case CFLOW_ACTOR_STATE_FAILED:
            return CFLOW_ACTOR_FAILED;
        case CFLOW_ACTOR_STATE_START:
            break;
    }
    return CFLOW_ACTOR_OK;
}

cflow_actor_init_result cflow_actor_init(
    cflow_actor *actor, const cflow_actor_config *config) {
    cflow_actor_init_result result = {
        CFLOW_ACTOR_INVALID_ARGUMENT, CFLOW_MACHINE_RUNTIME_OK};
    cflow_actor_impl *impl;
    cflow_graph surface = {0};

    surface.root = CMETA_INVALID_ID;
    if (actor == NULL || config == NULL || actor->impl != NULL)
        return result;
    if (config->scheduler == NULL ||
        !cflow_scheduler_valid(config->scheduler) ||
        !(cflow_scheduler_capabilities(config->scheduler) &
          CMETA_SCHED_CAP_CONCURRENT)) {
        result.status = CFLOW_ACTOR_INVALID_SCHEDULER;
        return result;
    }

    impl = actor_shell_create(CFLOW_ACTOR_BACKEND_MACHINE);
    if (impl == NULL) {
        result.status = CFLOW_ACTOR_ALLOCATION_FAILED;
        return result;
    }
    impl->scheduler = config->scheduler;
    impl->callbacks = config->callbacks;

    result.machine_status = cflow_machine_instance_init(
        &impl->machine, &config->machine);
    if (result.machine_status != CFLOW_MACHINE_RUNTIME_OK) {
        result.status = CFLOW_ACTOR_MACHINE_REJECTED;
        actor_release(impl);
        return result;
    }

    cflow_graph_init(&surface, config->machine.output_type);
    if (surface.root == CMETA_INVALID_ID ||
        !cflow_graph_normalize(&impl->graph, &surface)) {
        cflow_graph_destroy(&surface);
        cflow_machine_instance_destroy(&impl->machine);
        result.status = CFLOW_ACTOR_ALLOCATION_FAILED;
        actor_release(impl);
        return result;
    }
    cflow_graph_destroy(&surface);
    impl->bridge_callbacks = (cflow_sink_callbacks){
        actor_sink_value, actor_sink_error, actor_sink_done, impl};
    actor->impl = impl;
    result.status = CFLOW_ACTOR_OK;
    return result;
}

cflow_statechart_actor_init_result cflow_actor_init_statechart(
    cflow_actor *actor, const cflow_statechart_actor_config *config) {
    cflow_statechart_actor_init_result result = {
        CFLOW_ACTOR_INVALID_ARGUMENT, CFLOW_STATECHART_RUNTIME_OK};
    cflow_actor_impl *impl;

    if (actor == NULL || config == NULL || actor->impl != NULL)
        return result;
    impl = actor_shell_create(CFLOW_ACTOR_BACKEND_STATECHART);
    if (impl == NULL) {
        result.status = CFLOW_ACTOR_ALLOCATION_FAILED;
        return result;
    }
    impl->callbacks.on_error = config->callbacks.on_error;
    impl->callbacks.on_done = config->callbacks.on_done;
    impl->callbacks.user = config->callbacks.user;
    result.statechart_status = cflow_statechart_instance_init(
        &impl->statechart, &config->statechart);
    if (result.statechart_status != CFLOW_STATECHART_RUNTIME_OK) {
        result.status = CFLOW_ACTOR_STATECHART_REJECTED;
        actor_release(impl);
        return result;
    }
    impl->statechart_terminal =
        cflow_statechart_instance_terminal_waitable(&impl->statechart);
    if (!cflow_waitable_valid(&impl->statechart_terminal)) {
        cflow_statechart_instance_destroy(&impl->statechart);
        result.status = CFLOW_ACTOR_STATECHART_REJECTED;
        result.statechart_status = CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
        actor_release(impl);
        return result;
    }
    actor->impl = impl;
    result.status = CFLOW_ACTOR_OK;
    return result;
}

static cflow_actor_status actor_start_statechart(cflow_actor_impl *impl) {
    cflow_waker waker = {actor_statechart_terminal_wake, impl};
    turbo_mutex_lock(&impl->gate);
    impl->state = CFLOW_ACTOR_STATE_RUNNING;
    turbo_mutex_unlock(&impl->gate);
    if (cflow_waitable_arm(&impl->statechart_terminal, waker))
        return CFLOW_ACTOR_OK;
    actor_mark_failed(impl, actor_statechart_waitable_failure);
    return CFLOW_ACTOR_FAILED;
}

static cflow_actor_status actor_start_machine(cflow_actor_impl *impl) {
    cflow_source source = {0};
    cflow_sink sink;
    cflow_actor_status status;
    bool opened = false;

    if (!cflow_machine_instance_as_source(&impl->machine, &source)) {
        actor_mark_failed(impl, "actor could not attach Machine source");
        return CFLOW_ACTOR_FAILED;
    }
    sink = cflow_sink_from_callbacks(&impl->bridge_callbacks);
    opened = cflow_run_open(&impl->run, &impl->graph, &source,
                            impl->scheduler, &sink);
    if (!opened) {
        if (cflow_source_valid(&source)) cflow_source_destroy(&source);
        actor_mark_failed(impl, "actor could not open Run");
        return CFLOW_ACTOR_FAILED;
    }

    turbo_mutex_lock(&impl->gate);
    impl->state = CFLOW_ACTOR_STATE_RUNNING;
    if (!cflow_run_request(&impl->run, SIZE_MAX)) {
        impl->terminal_settled = false;
        impl->state = CFLOW_ACTOR_STATE_FAILED;
        turbo_cond_broadcast(&impl->changed);
        status = CFLOW_ACTOR_FAILED;
    } else {
        status = CFLOW_ACTOR_OK;
    }
    turbo_mutex_unlock(&impl->gate);
    if (status != CFLOW_ACTOR_OK) {
        actor_mark_failed(impl, "actor could not request Run demand");
        cflow_run_close(&impl->run);
        turbo_mutex_lock(&impl->gate);
        impl->terminal_settled = true;
        turbo_cond_broadcast(&impl->changed);
        turbo_mutex_unlock(&impl->gate);
    }
    return status;
}

cflow_actor_status cflow_actor_start(cflow_actor *actor) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    cflow_actor_status status;

    if (impl == NULL) return CFLOW_ACTOR_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->gate);
    status = actor_state_status(impl->state);
    turbo_mutex_unlock(&impl->gate);
    if (status != CFLOW_ACTOR_OK) return status;

    return impl->backend == CFLOW_ACTOR_BACKEND_STATECHART
        ? actor_start_statechart(impl) : actor_start_machine(impl);
}

cflow_actor_status cflow_actor_request_stop(cflow_actor *actor) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    bool close_backend = false;
    cflow_actor_status result;
    if (impl == NULL) return CFLOW_ACTOR_INVALID_ARGUMENT;

    turbo_mutex_lock(&impl->gate);
    switch (impl->state) {
        case CFLOW_ACTOR_STATE_START:
            impl->state = CFLOW_ACTOR_STATE_STOPPED;
            turbo_cond_broadcast(&impl->changed);
            close_backend = true;
            result = CFLOW_ACTOR_OK;
            break;
        case CFLOW_ACTOR_STATE_RUNNING:
            impl->state = CFLOW_ACTOR_STATE_STOPPING;
            close_backend = true;
            result = CFLOW_ACTOR_OK;
            break;
        case CFLOW_ACTOR_STATE_STOPPING:
            result = CFLOW_ACTOR_STOPPING;
            break;
        case CFLOW_ACTOR_STATE_STOPPED:
            result = CFLOW_ACTOR_STOPPED;
            break;
        case CFLOW_ACTOR_STATE_FAILED:
            result = CFLOW_ACTOR_FAILED;
            break;
        default:
            result = CFLOW_ACTOR_FAILED;
            break;
    }
    turbo_mutex_unlock(&impl->gate);
    if (close_backend) actor_close_backend(impl);
    return result;
}

cflow_actor_state cflow_actor_wait(cflow_actor *actor) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    cflow_actor_state state;
    if (impl == NULL) return CFLOW_ACTOR_STATE_FAILED;
    turbo_mutex_lock(&impl->gate);
    while (impl->state != CFLOW_ACTOR_STATE_STOPPED &&
           (impl->state != CFLOW_ACTOR_STATE_FAILED ||
            !impl->terminal_settled))
        turbo_cond_wait(&impl->changed, &impl->gate);
    state = impl->state;
    turbo_mutex_unlock(&impl->gate);
    return state;
}

cflow_actor_state cflow_actor_current_state(const cflow_actor *actor) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    cflow_actor_state state = CFLOW_ACTOR_STATE_START;
    if (impl == NULL) return state;
    turbo_mutex_lock(&impl->gate);
    state = impl->state;
    turbo_mutex_unlock(&impl->gate);
    return state;
}

bool cflow_actor_get_stats(const cflow_actor *actor, cflow_actor_stats *out) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    cflow_actor_stats snapshot = {0};
    if (impl == NULL || out == NULL ||
        impl->backend != CFLOW_ACTOR_BACKEND_MACHINE)
        return false;
    turbo_mutex_lock(&impl->gate);
    snapshot.state = impl->state;
    snapshot.rejected_not_started = impl->rejected_not_started;
    snapshot.rejected_stopping = impl->rejected_stopping;
    snapshot.rejected_stopped = impl->rejected_stopped;
    snapshot.rejected_failed = impl->rejected_failed;
    snapshot.rejected_stale = impl->rejected_stale;
    if (!cflow_machine_instance_get_stats(&impl->machine,
                                          &snapshot.machine)) {
        turbo_mutex_unlock(&impl->gate);
        return false;
    }
    turbo_mutex_unlock(&impl->gate);
    *out = snapshot;
    return true;
}

bool cflow_actor_get_statechart_stats(
    const cflow_actor *actor, cflow_statechart_actor_stats *out) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    cflow_statechart_actor_stats snapshot = {0};
    if (impl == NULL || out == NULL ||
        impl->backend != CFLOW_ACTOR_BACKEND_STATECHART)
        return false;
    turbo_mutex_lock(&impl->gate);
    snapshot.state = impl->state;
    snapshot.rejected_not_started = impl->rejected_not_started;
    snapshot.rejected_stopping = impl->rejected_stopping;
    snapshot.rejected_stopped = impl->rejected_stopped;
    snapshot.rejected_failed = impl->rejected_failed;
    snapshot.rejected_stale = impl->rejected_stale;
    if (!cflow_statechart_instance_get_stats(
            &impl->statechart, &snapshot.statechart)) {
        turbo_mutex_unlock(&impl->gate);
        return false;
    }
    turbo_mutex_unlock(&impl->gate);
    *out = snapshot;
    return true;
}

const char *cflow_actor_error(const cflow_actor *actor) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    const char *error;
    if (impl == NULL) return NULL;
    turbo_mutex_lock(&impl->gate);
    error = impl->error;
    if (error == NULL && impl->state == CFLOW_ACTOR_STATE_FAILED)
        error = actor_generic_failure;
    turbo_mutex_unlock(&impl->gate);
    return error;
}

bool cflow_actor_ref_acquire(const cflow_actor *actor, cflow_actor_ref *out) {
    cflow_actor_impl *impl = actor != NULL
        ? (cflow_actor_impl *)actor->impl : NULL;
    bool retained = false;
    if (impl == NULL || out == NULL || out->impl != NULL) return false;
    turbo_mutex_lock(&impl->gate);
    if (!impl->stale) retained = actor_retain(impl);
    turbo_mutex_unlock(&impl->gate);
    if (retained) out->impl = impl;
    return retained;
}

bool cflow_actor_ref_retain(const cflow_actor_ref *ref, cflow_actor_ref *out) {
    cflow_actor_impl *impl = ref != NULL
        ? (cflow_actor_impl *)ref->impl : NULL;
    if (impl == NULL || out == NULL || out->impl != NULL ||
        !actor_retain(impl))
        return false;
    out->impl = impl;
    return true;
}

void cflow_actor_ref_release(cflow_actor_ref *ref) {
    cflow_actor_impl *impl;
    if (ref == NULL) return;
    impl = (cflow_actor_impl *)ref->impl;
    ref->impl = NULL;
    actor_release(impl);
}

cflow_actor_send_status cflow_actor_ref_try_send(
    const cflow_actor_ref *ref, const cflow_event_view *event) {
    cflow_actor_impl *impl = ref != NULL
        ? (cflow_actor_impl *)ref->impl : NULL;
    cflow_mailbox_status mailbox_status;
    cflow_actor_send_status result;
    if (impl == NULL) return CFLOW_ACTOR_SEND_INVALID_ARGUMENT;

    turbo_mutex_lock(&impl->gate);
    if (impl->stale) {
        ++impl->rejected_stale;
        result = CFLOW_ACTOR_SEND_STALE;
    } else if (event == NULL || event->id == 0u ||
               event->payload_type == NULL || event->payload == NULL) {
        result = CFLOW_ACTOR_SEND_INVALID_ARGUMENT;
    } else if (impl->state == CFLOW_ACTOR_STATE_START) {
        ++impl->rejected_not_started;
        result = CFLOW_ACTOR_SEND_NOT_STARTED;
    } else if (impl->state == CFLOW_ACTOR_STATE_STOPPING) {
        ++impl->rejected_stopping;
        result = CFLOW_ACTOR_SEND_STOPPING;
    } else if (impl->state == CFLOW_ACTOR_STATE_STOPPED) {
        ++impl->rejected_stopped;
        result = CFLOW_ACTOR_SEND_STOPPED;
    } else if (impl->state == CFLOW_ACTOR_STATE_FAILED) {
        ++impl->rejected_failed;
        result = CFLOW_ACTOR_SEND_FAILED;
    } else {
        mailbox_status = impl->backend == CFLOW_ACTOR_BACKEND_STATECHART
            ? cflow_statechart_instance_try_send(&impl->statechart, event)
            : cflow_machine_instance_try_send(&impl->machine, event);
        switch (mailbox_status) {
            case CFLOW_MAILBOX_OK:
                result = CFLOW_ACTOR_SEND_ACCEPTED;
                break;
            case CFLOW_MAILBOX_TYPE_MISMATCH:
                result = CFLOW_ACTOR_SEND_TYPE_MISMATCH;
                break;
            case CFLOW_MAILBOX_FULL:
                result = CFLOW_ACTOR_SEND_FULL;
                break;
            case CFLOW_MAILBOX_CLOSED:
            case CFLOW_MAILBOX_CANCELLED:
                ++impl->rejected_failed;
                result = CFLOW_ACTOR_SEND_FAILED;
                break;
            case CFLOW_MAILBOX_INVALID_ARGUMENT:
            case CFLOW_MAILBOX_EMPTY:
            case CFLOW_MAILBOX_ALLOCATION_FAILED:
            case CFLOW_MAILBOX_BUFFER_TOO_SMALL:
            default:
                result = CFLOW_ACTOR_SEND_INVALID_ARGUMENT;
                break;
        }
    }
    turbo_mutex_unlock(&impl->gate);
    return result;
}

void cflow_actor_destroy(cflow_actor *actor) {
    cflow_actor_impl *impl;
    if (actor == NULL || actor->impl == NULL) return;
    impl = (cflow_actor_impl *)actor->impl;

    turbo_mutex_lock(&impl->gate);
    impl->stale = true;
    if (impl->state == CFLOW_ACTOR_STATE_START)
        impl->state = CFLOW_ACTOR_STATE_STOPPED;
    else if (impl->state == CFLOW_ACTOR_STATE_RUNNING)
        impl->state = CFLOW_ACTOR_STATE_STOPPING;
    turbo_mutex_unlock(&impl->gate);

    if (impl->backend == CFLOW_ACTOR_BACKEND_STATECHART) {
        if (cflow_waitable_valid(&impl->statechart_terminal))
            cflow_waitable_cancel(&impl->statechart_terminal);
        cflow_statechart_instance_close(&impl->statechart);
        (void)cflow_statechart_instance_destroy(&impl->statechart);
    } else {
        cflow_machine_instance_close(&impl->machine);
        cflow_run_close(&impl->run);
        cflow_machine_instance_destroy(&impl->machine);
        cflow_graph_destroy(&impl->graph);
    }

    turbo_mutex_lock(&impl->gate);
    if (impl->state != CFLOW_ACTOR_STATE_FAILED)
        impl->state = CFLOW_ACTOR_STATE_STOPPED;
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->gate);
    actor->impl = NULL;
    actor_release(impl);
}
