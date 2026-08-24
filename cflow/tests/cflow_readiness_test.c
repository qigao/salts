#include <cflow/cflow.h>
#include <cflow/readiness.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include "readiness_contract_suite.h"
#include "tinytest.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

enum {
    CFLOW_READINESS_TEST_RESOURCE = 7001,
    CFLOW_READINESS_TEST_OTHER_RESOURCE = 7002,
    CFLOW_READINESS_TEST_TIMEOUT_NS = 2 * 1000 * 1000 * 1000
};

typedef struct managed_test_value {
    int *owned;
} managed_test_value;

static bool managed_test_copy(void *destination, const void *source) {
    (void)destination;
    (void)source;
    return true;
}

static void managed_test_move(void *destination, void *source) {
    *(managed_test_value *)destination = *(managed_test_value *)source;
    ((managed_test_value *)source)->owned = NULL;
}

static void managed_test_destroy(void *value) { (void)value; }

static const cmeta_type_traits managed_test_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = managed_test_copy,
    .move_construct = managed_test_move,
    .destroy = managed_test_destroy
};

static const cmeta_type_desc managed_test_type = {
    .name = "managed_test_value",
    .size = sizeof(managed_test_value),
    .align = _Alignof(managed_test_value),
    .kind = CMETA_T_OBJECT,
    .traits = &managed_test_traits
};

typedef struct read_probe {
    cflow_read_status statuses[8];
    int values[8];
    size_t count;
    size_t next;
    size_t reads;
    size_t closes;
    const char *errors[8];
} read_probe;

typedef struct wake_probe {
    turbo_mutex_t lock;
    turbo_cond_t changed;
    size_t calls;
    bool entered;
    bool released;
} wake_probe;

typedef struct sink_probe {
    int values[4];
    size_t value_count;
    size_t done_count;
    const char *error;
} sink_probe;

typedef struct fake_env {
    const readiness_contract_factory *factory;
    readiness_contract_fixture *fixture;
    turbo_readiness_reactor reactor;
    cflow_reactor_source_owner owner;
} fake_env;

typedef struct emit_thread_args {
    fake_env *env;
    intptr_t resource;
    int status;
} emit_thread_args;

typedef struct destroy_thread_args {
    cflow_source *source;
    wake_probe *probe;
    bool returned;
} destroy_thread_args;

static bool fake_env_init(fake_env *env, size_t capacity) {
    int status = TURBO_EINVAL;
    const turbo_readiness_config config = {capacity, capacity};

    memset(env, 0, sizeof(*env));
    env->factory = readiness_contract_factory_get();
    if (!env->factory)
        return false;
    env->fixture = env->factory->create(config, &env->reactor, &status);
    return env->fixture != NULL && status == TURBO_OK;
}

static void fake_env_destroy(fake_env *env) {
    if (!env || !env->fixture)
        return;
    (void)turbo_readiness_reactor_shutdown(&env->reactor);
    (void)turbo_readiness_reactor_destroy(&env->reactor);
    env->factory->destroy(env->fixture);
    memset(env, 0, sizeof(*env));
}

static cflow_read_status probe_read(void *user, void *out_value,
                                    const char **error) {
    read_probe *probe = (read_probe *)user;
    cflow_read_status status;

    if (!probe || probe->next >= probe->count) {
        if (error)
            *error = "read script exhausted";
        return CFLOW_READ_ERROR;
    }
    ++probe->reads;
    status = probe->statuses[probe->next];
    if (status == CFLOW_READ_ERROR && error)
        *error = probe->errors[probe->next];
    if ((status == CFLOW_READ_VALUE ||
         status == CFLOW_READ_VALUE_AND_DONE) && out_value)
        *(int *)out_value = probe->values[probe->next];
    ++probe->next;
    return status;
}

static void probe_close(void *user) {
    read_probe *probe = (read_probe *)user;
    if (probe)
        ++probe->closes;
}

static void blocking_wake(void *user) {
    wake_probe *probe = (wake_probe *)user;
    if (!probe)
        return;
    turbo_mutex_lock(&probe->lock);
    ++probe->calls;
    probe->entered = true;
    turbo_cond_broadcast(&probe->changed);
    while (!probe->released)
        turbo_cond_wait(&probe->changed, &probe->lock);
    turbo_mutex_unlock(&probe->lock);
}

static bool sink_value(void *user, const cmeta_type_desc *type,
                       const void *value) {
    sink_probe *probe = (sink_probe *)user;
    if (!probe || !cmeta_type_equal(type, &cmeta_type_int) || !value ||
        probe->value_count >= 4u)
        return false;
    probe->values[probe->value_count++] = *(const int *)value;
    return true;
}

static void sink_error(void *user, const char *message) {
    sink_probe *probe = (sink_probe *)user;
    if (probe)
        probe->error = message;
}

static void sink_done(void *user) {
    sink_probe *probe = (sink_probe *)user;
    if (probe)
        ++probe->done_count;
}

static void emit_thread(void *user) {
    emit_thread_args *args = (emit_thread_args *)user;
    args->status = args->env->factory->emit_resource(
        args->env->fixture, args->resource, TURBO_READINESS_EVENT_READ);
}

static void destroy_thread(void *user) {
    destroy_thread_args *args = (destroy_thread_args *)user;
    cflow_source_destroy(args->source);
    turbo_mutex_lock(&args->probe->lock);
    args->returned = true;
    turbo_cond_broadcast(&args->probe->changed);
    turbo_mutex_unlock(&args->probe->lock);
}

static void cancel_thread(void *user) {
    destroy_thread_args *args = (destroy_thread_args *)user;
    cflow_source_cancel(args->source);
    turbo_mutex_lock(&args->probe->lock);
    args->returned = true;
    turbo_cond_broadcast(&args->probe->changed);
    turbo_mutex_unlock(&args->probe->lock);
}

static int make_source(fake_env *env, intptr_t resource, read_probe *probe,
                       cflow_source *source,
                       turbo_readiness_registration *registration) {
    int status = turbo_readiness_register(
        &env->reactor, resource, registration);
    if (status != TURBO_OK)
        return status;
    return cflow_source_from_reactor_registration(
        source, &env->owner, registration, TURBO_READINESS_EVENT_READ,
        "reactor-test", &cmeta_type_int, probe_read, probe_close, probe);
}

suite("CFlow reactor registration Source") {
    it("keeps caller registration ownership on precise admission failure") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        read_probe probe = {0};

        check_true(fake_env_init(&env, 2u));
        check_equal(turbo_readiness_register(
                        &env.reactor, CFLOW_READINESS_TEST_RESOURCE,
                        &registration), TURBO_OK);
        check_not_null(registration.impl);
        memset(&source, 0xa5, sizeof(source));
        env.owner.impl = (void *)(uintptr_t)1u;

        check_equal(cflow_source_from_reactor_registration(
                        &source, &env.owner, &registration,
                        TURBO_READINESS_EVENT_READ,
                        "managed", &managed_test_type, probe_read,
                        probe_close, &probe), TURBO_ENOTSUP);
        check_false(cflow_source_valid(&source));
        check_null(env.owner.impl);
        check_not_null(registration.impl);
        check_equal(cflow_source_from_reactor_registration(
                        &source, &env.owner, &registration,
                        TURBO_READINESS_EVENT_READ,
                        "missing-read", &cmeta_type_int, NULL,
                        probe_close, &probe), TURBO_EINVAL);
        check_false(cflow_source_valid(&source));
        check_null(env.owner.impl);
        check_not_null(registration.impl);
        check_equal(turbo_readiness_close(&registration), TURBO_OK);
        check_null(registration.impl);
        fake_env_destroy(&env);
    }

    it("keeps external owner live and side-effect free while Source exists") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        read_probe probe = {0};
        void *owner_impl;

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &probe,
                        &source, &registration), TURBO_OK);
        owner_impl = env.owner.impl;
        check_not_null(owner_impl);

        check_equal(cflow_reactor_source_owner_close(&env.owner),
                    TURBO_EBUSY);
        check_equal(env.owner.impl, owner_impl);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)0u);
        check_equal(probe.closes, (size_t)0u);

        cflow_source_destroy(&source);
        check_equal(probe.closes, (size_t)1u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        check_null(env.owner.impl);
        fake_env_destroy(&env);
    }

    it("moves registration only after complete construction") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        read_probe probe = {0};

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &probe,
                        &source, &registration), TURBO_OK);
        check_true(cflow_source_valid(&source));
        check_not_null(env.owner.impl);
        check_null(registration.impl);

        cflow_source_destroy(&source);
        check_equal(probe.closes, (size_t)1u);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)1u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        check_null(env.owner.impl);
        fake_env_destroy(&env);
    }

    it("makes cancel terminal and keeps destroy cleanup exactly once") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        read_probe probe = {0};

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &probe,
                        &source, &registration), TURBO_OK);

        cflow_source_cancel(&source);
        check_equal(probe.closes, (size_t)1u);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)1u);
        cflow_source_destroy(&source);
        check_equal(probe.closes, (size_t)1u);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)1u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        fake_env_destroy(&env);
    }

    it("retains borrowed user after close error and retries on destroy") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        cflow_resume_ctx resume = {0};
        read_probe probe = {0};
        cflow_step step;
        int output = 0;

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &probe,
                        &source, &registration), TURBO_OK);
        env.factory->fail_hook(env.fixture, READINESS_CONTRACT_HOOK_CLOSE,
                               TURBO_EIO, 1u);

        cflow_source_cancel(&source);
        check_equal(probe.closes, (size_t)0u);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)1u);
        step = cflow_source_resume(&source, &resume, &output);
        check_equal(step.kind, CFLOW_STEP_ERROR);
        check_equal(step.error, "reactor readiness close failed: -4017");
        cflow_source_destroy(&source);
        check_equal(probe.closes, (size_t)1u);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)2u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        fake_env_destroy(&env);
    }

    it("keeps persistent close ownership reachable after Run release") {
        enum { PERSISTENT_CLOSE_FAILURES = 16 };
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        cflow_graph graph = {0};
        cflow_scheduler scheduler = {0};
        cflow_run run = {0};
        read_probe read = {
            {CFLOW_READ_WOULD_BLOCK}, {0}, 1u, 0u, 0u, 0u, {NULL}
        };
        sink_probe observed = {0};
        cflow_sink_callbacks callbacks = {
            sink_value, sink_error, sink_done, &observed
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);
        void *owner_impl;

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &read,
                        &source, &registration), TURBO_OK);
        owner_impl = env.owner.impl;
        env.factory->fail_hook(env.fixture, READINESS_CONTRACT_HOOK_CLOSE,
                               TURBO_EIO, PERSISTENT_CLOSE_FAILURES);
        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_run_open(&run, &graph, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        cflow_run_close(&run);
        check_equal(env.owner.impl, owner_impl);
        check_equal(read.closes, (size_t)0u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_EIO);
        check_equal(env.owner.impl, owner_impl);
        check_equal(read.closes, (size_t)0u);

        env.factory->fail_hook(env.fixture, READINESS_CONTRACT_HOOK_CLOSE,
                               TURBO_EIO, 0u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        check_null(env.owner.impl);
        check_equal(read.closes, (size_t)1u);
        check_equal(turbo_readiness_reactor_shutdown(&env.reactor), TURBO_OK);
        check_equal(turbo_readiness_reactor_destroy(&env.reactor), TURBO_OK);
        env.factory->destroy(env.fixture);
        env.fixture = NULL;
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&graph);
    }

    it("maps terminal and invalid read statuses through Run") {
        typedef struct read_case {
            cflow_read_status status;
            const char *read_error;
            const char *expected_error;
            size_t expected_done;
        } read_case;
        static const read_case cases[] = {
            {CFLOW_READ_DONE, NULL, NULL, 1u},
            {CFLOW_READ_ERROR, "scripted read error", "scripted read error", 0u},
            {CFLOW_READ_ERROR, NULL, "reactor readiness source error", 0u},
            {CFLOW_READ_ERROR, "", "reactor readiness source error", 0u},
            {(cflow_read_status)99, NULL,
             "invalid reactor readiness read status", 0u}
        };

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            fake_env env;
            turbo_readiness_registration registration = {0};
            cflow_source source = {0};
            cflow_graph graph = {0};
            cflow_scheduler scheduler = {0};
            cflow_run run = {0};
            read_probe read = {0};
            sink_probe observed = {0};
            cflow_sink_callbacks callbacks = {
                sink_value, sink_error, sink_done, &observed
            };
            cflow_sink sink = cflow_sink_from_callbacks(&callbacks);

            read.statuses[0] = cases[i].status;
            read.errors[0] = cases[i].read_error;
            read.count = 1u;
            check_true(fake_env_init(&env, 2u));
            check_equal(make_source(
                            &env, CFLOW_READINESS_TEST_RESOURCE, &read,
                            &source, &registration), TURBO_OK);
            cflow_graph_init(&graph, &cmeta_type_int);
            check_true(cflow_scheduler_test_init(&scheduler));
            check_true(cflow_run_open(
                &run, &graph, &source, &scheduler, &sink));
            check_true(cflow_run_request(&run, 1u));
            (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

            check_equal(observed.value_count, (size_t)0u);
            check_equal(observed.done_count, cases[i].expected_done);
            check_equal(read.reads, (size_t)1u);
            if (cases[i].expected_error) {
                check_equal(observed.error, cases[i].expected_error);
                check_equal(cflow_run_error(&run), cases[i].expected_error);
            } else {
                check_null(observed.error);
                check_null(cflow_run_error(&run));
            }
            check_equal(cflow_run_is_done(&run),
                        cases[i].expected_error == NULL);
            cflow_run_close(&run);
            check_not_null(env.owner.impl);
            check_equal(read.closes, (size_t)1u);
            check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
            check_null(env.owner.impl);
            check_equal(read.closes, (size_t)1u);
            cflow_scheduler_destroy(&scheduler);
            cflow_graph_destroy(&graph);
            fake_env_destroy(&env);
        }
    }

    it("drives Run through WOULD_BLOCK WAIT wake rearm and final value") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        cflow_graph graph = {0};
        cflow_scheduler scheduler = {0};
        cflow_run run = {0};
        read_probe read = {
            {CFLOW_READ_WOULD_BLOCK, CFLOW_READ_VALUE,
             CFLOW_READ_WOULD_BLOCK, CFLOW_READ_VALUE_AND_DONE},
            {0, 17, 0, 29}, 4u, 0u, 0u, 0u
        };
        sink_probe observed = {0};
        cflow_sink_callbacks callbacks = {
            sink_value, sink_error, sink_done, &observed
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);
        turbo_readiness_stats stats = {0};
        uint64_t registration_token;
        uint64_t first_arm_token;

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &read,
                        &source, &registration), TURBO_OK);
        registration_token = env.factory->token_for_resource(
            env.fixture, CFLOW_READINESS_TEST_RESOURCE);
        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_run_open(&run, &graph, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 2u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        first_arm_token = env.factory->arm_token_for_resource(
            env.fixture, CFLOW_READINESS_TEST_RESOURCE);
        check_not_equal(first_arm_token, (uint64_t)0u);
        check_equal(env.factory->emit_resource(
                        env.fixture, CFLOW_READINESS_TEST_RESOURCE,
                        TURBO_READINESS_EVENT_READ), TURBO_OK);
        check_equal(env.factory->emit_arm_token(
                        env.fixture, registration_token, first_arm_token,
                        TURBO_READINESS_EVENT_READ), TURBO_OK);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(observed.value_count, (size_t)1u);
        check_equal(observed.values[0], 17);
        check_equal(read.reads, (size_t)3u);
        check_equal(env.factory->emit_arm_token(
                        env.fixture, registration_token, first_arm_token,
                        TURBO_READINESS_EVENT_READ), TURBO_OK);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(observed.value_count, (size_t)1u);
        check_equal(env.factory->emit_resource(
                        env.fixture, CFLOW_READINESS_TEST_RESOURCE,
                        TURBO_READINESS_EVENT_READ), TURBO_OK);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(observed.value_count, (size_t)2u);
        check_equal(observed.values[1], 29);
        check_equal(observed.done_count, (size_t)1u);
        check_null(observed.error);
        check_true(cflow_run_is_done(&run));
        check_equal(turbo_readiness_reactor_stats(&env.reactor, &stats),
                    TURBO_OK);
        check_equal(stats.duplicate_events, (uint64_t)1u);
        check_equal(stats.stale_events, (uint64_t)1u);

        cflow_run_close(&run);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&graph);
        fake_env_destroy(&env);
    }

    it("turns synchronous arm status into the exact Run error") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        cflow_graph graph = {0};
        cflow_scheduler scheduler = {0};
        cflow_run run = {0};
        read_probe read = {{CFLOW_READ_WOULD_BLOCK}, {0}, 1u, 0u, 0u, 0u};
        sink_probe observed = {0};
        cflow_sink_callbacks callbacks = {
            sink_value, sink_error, sink_done, &observed
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &read,
                        &source, &registration), TURBO_OK);
        env.factory->fail_next_arm(env.fixture, TURBO_EIO);
        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_run_open(&run, &graph, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        check_equal(cflow_run_error(&run),
                    "reactor readiness arm failed: -4017");
        check_equal(observed.error,
                    "reactor readiness arm failed: -4017");
        check_equal(observed.value_count, (size_t)0u);
        cflow_run_close(&run);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&graph);
        fake_env_destroy(&env);
    }

    it("turns terminal backend status into the exact Run error") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        cflow_graph graph = {0};
        cflow_scheduler scheduler = {0};
        cflow_run run = {0};
        read_probe read = {{CFLOW_READ_WOULD_BLOCK}, {0}, 1u, 0u, 0u, 0u};
        sink_probe observed = {0};
        cflow_sink_callbacks callbacks = {
            sink_value, sink_error, sink_done, &observed
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);

        check_true(fake_env_init(&env, 2u));
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &read,
                        &source, &registration), TURBO_OK);
        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_run_open(&run, &graph, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(env.factory->fail_backend(env.fixture, TURBO_EIO),
                    TURBO_OK);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        check_equal(cflow_run_error(&run),
                    "reactor readiness backend failed: -4017");
        check_equal(observed.error,
                    "reactor readiness backend failed: -4017");
        cflow_run_close(&run);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&graph);
        fake_env_destroy(&env);
    }

    it("makes cancel wait until the old callback waker is quiescent") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        cflow_resume_ctx resume = {0};
        cflow_step step;
        read_probe read = {{CFLOW_READ_WOULD_BLOCK}, {0}, 1u, 0u, 0u, 0u};
        wake_probe wake = {0};
        emit_thread_args emit_args = {
            &env, CFLOW_READINESS_TEST_RESOURCE, TURBO_EINVAL
        };
        destroy_thread_args cancel_args = {&source, &wake, false};
        turbo_thread_t emitter = NULL;
        turbo_thread_t canceller = NULL;
        turbo_readiness_stats stats = {0};
        int output = 0;

        check_true(fake_env_init(&env, 2u));
        turbo_mutex_init(&wake.lock);
        turbo_cond_init(&wake.changed);
        check_not_null(wake.lock);
        check_not_null(wake.changed);
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &read,
                        &source, &registration), TURBO_OK);
        step = cflow_source_resume(&source, &resume, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){blocking_wake, &wake}));

        check_equal(turbo_thread_create(&emitter, emit_thread, &emit_args),
                    TURBO_OK);
        turbo_mutex_lock(&wake.lock);
        while (!wake.entered)
            turbo_cond_wait(&wake.changed, &wake.lock);
        turbo_mutex_unlock(&wake.lock);

        env.factory->block_hook(env.fixture, READINESS_CONTRACT_HOOK_CLOSE);
        check_equal(turbo_thread_create(
                        &canceller, cancel_thread, &cancel_args), TURBO_OK);
        check_equal(env.factory->wait_hook_calls(
                        env.fixture, READINESS_CONTRACT_HOOK_CLOSE, 1u,
                        CFLOW_READINESS_TEST_TIMEOUT_NS), TURBO_OK);
        env.factory->release_hook(env.fixture, READINESS_CONTRACT_HOOK_CLOSE);
        check_equal(turbo_readiness_reactor_stats(&env.reactor, &stats),
                    TURBO_OK);
        check_equal(stats.callbacks_inflight, (size_t)1u);
        turbo_mutex_lock(&wake.lock);
        check_false(cancel_args.returned);
        check_equal(read.closes, (size_t)0u);
        wake.released = true;
        turbo_cond_broadcast(&wake.changed);
        turbo_mutex_unlock(&wake.lock);

        check_equal(turbo_thread_join(&emitter), TURBO_OK);
        check_equal(turbo_thread_join(&canceller), TURBO_OK);
        check_equal(emit_args.status, TURBO_OK);
        check_true(cancel_args.returned);
        check_equal(read.closes, (size_t)1u);
        cflow_source_destroy(&source);
        check_equal(read.closes, (size_t)1u);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)1u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        turbo_cond_destroy(&wake.changed);
        turbo_mutex_destroy(&wake.lock);
        fake_env_destroy(&env);
    }

    it("waits for an inflight callback before user close and free") {
        fake_env env;
        turbo_readiness_registration registration = {0};
        cflow_source source = {0};
        cflow_resume_ctx resume = {0};
        cflow_step step;
        read_probe read = {{CFLOW_READ_WOULD_BLOCK}, {0}, 1u, 0u, 0u, 0u};
        wake_probe wake = {0};
        emit_thread_args emit_args = {
            &env, CFLOW_READINESS_TEST_RESOURCE, TURBO_EINVAL
        };
        destroy_thread_args destroy_args = {&source, &wake, false};
        turbo_thread_t emitter = NULL;
        turbo_thread_t destroyer = NULL;
        turbo_readiness_stats stats = {0};
        int output = 0;

        check_true(fake_env_init(&env, 2u));
        turbo_mutex_init(&wake.lock);
        turbo_cond_init(&wake.changed);
        check_not_null(wake.lock);
        check_not_null(wake.changed);
        check_equal(make_source(
                        &env, CFLOW_READINESS_TEST_RESOURCE, &read,
                        &source, &registration), TURBO_OK);
        step = cflow_source_resume(&source, &resume, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){blocking_wake, &wake}));

        check_equal(turbo_thread_create(&emitter, emit_thread, &emit_args),
                    TURBO_OK);
        turbo_mutex_lock(&wake.lock);
        while (!wake.entered)
            turbo_cond_wait(&wake.changed, &wake.lock);
        turbo_mutex_unlock(&wake.lock);

        env.factory->block_hook(env.fixture, READINESS_CONTRACT_HOOK_CLOSE);
        check_equal(turbo_thread_create(
                        &destroyer, destroy_thread, &destroy_args), TURBO_OK);
        check_equal(env.factory->wait_hook_calls(
                        env.fixture, READINESS_CONTRACT_HOOK_CLOSE, 1u,
                        CFLOW_READINESS_TEST_TIMEOUT_NS), TURBO_OK);
        env.factory->release_hook(env.fixture, READINESS_CONTRACT_HOOK_CLOSE);
        check_equal(turbo_readiness_reactor_stats(&env.reactor, &stats),
                    TURBO_OK);
        check_equal(stats.callbacks_inflight, (size_t)1u);
        turbo_mutex_lock(&wake.lock);
        check_false(destroy_args.returned);
        check_equal(read.closes, (size_t)0u);
        wake.released = true;
        turbo_cond_broadcast(&wake.changed);
        turbo_mutex_unlock(&wake.lock);

        check_equal(turbo_thread_join(&emitter), TURBO_OK);
        check_equal(turbo_thread_join(&destroyer), TURBO_OK);
        check_equal(emit_args.status, TURBO_OK);
        check_true(destroy_args.returned);
        check_equal(read.closes, (size_t)1u);
        check_equal(env.factory->backend_close_calls(env.fixture),
                    (size_t)1u);
        check_equal(cflow_reactor_source_owner_close(&env.owner), TURBO_OK);
        turbo_cond_destroy(&wake.changed);
        turbo_mutex_destroy(&wake.lock);
        fake_env_destroy(&env);
    }
}
