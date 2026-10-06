#include <salts/rcu.h>
#include <salts/thread.h>
#include "tinytest.h"
#include <stdatomic.h>
#include <stdlib.h>

enum { RCU_READERS = 4, RCU_REPLACEMENTS = 2000 };
typedef struct snapshot { unsigned generation; unsigned inverse; } snapshot;
typedef struct stress {
    cmeta_rcu domain;
    atomic_bool stop;
    atomic_uint errors;
    atomic_uint ready;
} stress;

static snapshot *new_snapshot(unsigned generation) {
    snapshot *value = malloc(sizeof(*value));
    if (value != NULL) { value->generation = generation; value->inverse = ~generation; }
    return value;
}
static void stress_reader(void *arg) {
    stress *ctx = arg;
    bool announced = false;
    while (!atomic_load_explicit(&ctx->stop, memory_order_acquire)) {
        cmeta_rcu_guard guard = {0};
        int status = cmeta_rcu_read_lock(&ctx->domain, &guard);
        if (status == SALTS_OK) {
            const snapshot *value = cmeta_rcu_load(&guard);
            if (value == NULL || value->inverse != ~value->generation)
                atomic_fetch_add(&ctx->errors, 1);
            if (cmeta_rcu_read_unlock(&guard) != SALTS_OK) atomic_fetch_add(&ctx->errors, 1);
            if (!announced) { atomic_fetch_add(&ctx->ready, 1); announced = true; }
        } else if (status != SALTS_ENOBUFS) atomic_fetch_add(&ctx->errors, 1);
    }
}
typedef struct handoff { cmeta_rcu_guard *guard; const void *seen; int status; } handoff;
static void release_handoff(void *arg) {
    handoff *ctx = arg;
    ctx->seen = cmeta_rcu_load(ctx->guard);
    ctx->status = cmeta_rcu_read_unlock(ctx->guard);
}
typedef struct writer { cmeta_rcu *domain; int replacement; int status; } writer;
static void publish_once(void *arg) {
    writer *ctx = arg;
    ctx->status = cmeta_rcu_replace(ctx->domain, &ctx->replacement);
}

spec("Portable epoch RCU") {
    it("waits for old readers but permits reclamation with new readers active") {
        int first = 1, second = 2, third = 3;
        cmeta_rcu domain = {0};
        cmeta_rcu_guard old = {0}, newer = {0}, extra = {0};
        void *retired = NULL;
        check_equal(cmeta_rcu_init(&domain, &first, 2), SALTS_OK);
        check_equal(cmeta_rcu_read_lock(&domain, &old), SALTS_OK);
        check_equal(cmeta_rcu_replace(&domain, &second), SALTS_OK);
        check_equal(cmeta_rcu_read_lock(&domain, &newer), SALTS_OK);
        check_true(cmeta_rcu_load(&old) == &first);
        check_true(cmeta_rcu_load(&newer) == &second);
        check_equal(cmeta_rcu_read_lock(&domain, &extra), SALTS_ENOBUFS);
        check_equal(cmeta_rcu_replace(&domain, &third), SALTS_EBUSY);
        check_equal(cmeta_rcu_replace(&domain, &first), SALTS_EINVAL);
        check_equal(cmeta_rcu_try_reclaim(&domain, &retired), SALTS_EBUSY);
        check_null(retired);
        check_equal(cmeta_rcu_read_unlock(&old), SALTS_OK);
        check_equal(cmeta_rcu_try_reclaim(&domain, &retired), SALTS_OK);
        check_true(retired == &first);
        check_equal(cmeta_rcu_replace(&domain, &third), SALTS_OK);
        check_equal(cmeta_rcu_try_reclaim(&domain, &retired), SALTS_EBUSY);
        check_equal(cmeta_rcu_close(&domain), SALTS_OK);
        check_equal(cmeta_rcu_read_lock(&domain, &extra), SALTS_ESHUTDOWN);
        check_equal(cmeta_rcu_replace(&domain, NULL), SALTS_ESHUTDOWN);
        check_equal(cmeta_rcu_destroy(&domain, &retired), SALTS_EBUSY);
        check_equal(cmeta_rcu_read_unlock(&newer), SALTS_OK);
        check_equal(cmeta_rcu_try_reclaim(&domain, &retired), SALTS_OK);
        check_true(retired == &second);
        check_equal(cmeta_rcu_destroy(&domain, &retired), SALTS_OK);
        check_true(retired == &third);
        check_equal(cmeta_rcu_destroy(&domain, &retired), SALTS_OK);
        check_null(retired);
    }
    it("rejects copied handles and releases an address-stable guard on another thread") {
        int value = 7;
        cmeta_rcu domain = {0}, copied;
        cmeta_rcu_guard guard = {0}, copy;
        cmeta_thread_t thread = NULL;
        handoff ctx = {&guard, NULL, SALTS_EINVAL};
        void *out = NULL;
        check_equal(cmeta_rcu_init(&domain, &value, 1), SALTS_OK);
        copied = domain;
        check_equal(cmeta_rcu_read_lock(&copied, &guard), SALTS_EINVAL);
        check_equal(cmeta_rcu_read_lock(&domain, &guard), SALTS_OK);
        check_equal(cmeta_rcu_read_lock(&domain, &guard), SALTS_EALREADY);
        copy = guard;
        check_null(cmeta_rcu_load(&copy));
        check_equal(cmeta_rcu_read_unlock(&copy), SALTS_EINVAL);
        check_equal(cmeta_rcu_replace(&domain, NULL), SALTS_OK);
        check_equal(cmeta_rcu_try_reclaim(&domain, &out), SALTS_EBUSY);
        /* Creation/join transfer exclusive access to the original guard.
         * Its frame and domain survive this suspended owner interval. */
        check_equal(cmeta_thread_create(&thread, release_handoff, &ctx), 0);
        if (thread != NULL) check_equal(cmeta_thread_join(&thread), 0);
        check_true(ctx.seen == &value);
        check_equal(ctx.status, SALTS_OK);
        check_equal(cmeta_rcu_read_unlock(&guard), SALTS_EINVAL);
        check_equal(cmeta_rcu_try_reclaim(&domain, &out), SALTS_OK);
        check_true(out == &value);
        check_equal(cmeta_rcu_try_reclaim(&domain, &out), SALTS_ENOENT);
        check_equal(cmeta_rcu_close(&domain), SALTS_OK);
        check_equal(cmeta_rcu_destroy(&domain, &out), SALTS_OK);
    }
    it("publishes immutable heap snapshots to concurrent readers without premature free") {
        stress ctx = {0};
        cmeta_thread_t readers[RCU_READERS] = {0};
        snapshot *initial = new_snapshot(0);
        size_t started = 0;
        void *retired = NULL;
        atomic_init(&ctx.stop, false); atomic_init(&ctx.errors, 0); atomic_init(&ctx.ready, 0);
        check_not_null(initial);
        if (initial == NULL) return;
        check_equal(cmeta_rcu_init(&ctx.domain, initial, RCU_READERS), SALTS_OK);
        if (ctx.domain.impl == NULL) { free(initial); return; }
        for (; started < RCU_READERS; ++started) {
            int status = cmeta_thread_create(&readers[started], stress_reader, &ctx);
            check_equal(status, 0);
            if (status != 0) break;
        }
        while (atomic_load(&ctx.ready) != started) cmeta_thread_yield();
        for (unsigned generation = 1; generation <= RCU_REPLACEMENTS; ++generation) {
            snapshot *replacement = new_snapshot(generation);
            int status;
            check_not_null(replacement);
            if (replacement == NULL) break;
            status = cmeta_rcu_replace(&ctx.domain, replacement);
            check_equal(status, SALTS_OK);
            if (status != SALTS_OK) { free(replacement); break; }
            do { status = cmeta_rcu_try_reclaim(&ctx.domain, &retired); cmeta_thread_yield(); }
            while (status == SALTS_EBUSY);
            check_equal(status, SALTS_OK);
            free(retired);
        }
        atomic_store_explicit(&ctx.stop, true, memory_order_release);
        for (size_t i = 0; i < started; ++i) check_equal(cmeta_thread_join(&readers[i]), 0);
        check_equal(atomic_load(&ctx.errors), 0u);
        check_equal(cmeta_rcu_close(&ctx.domain), SALTS_OK);
        check_equal(cmeta_rcu_destroy(&ctx.domain, &retired), SALTS_OK);
        free(retired);
    }
    it("serializes concurrent writers with one bounded retired snapshot") {
        int initial = 0;
        cmeta_rcu domain = {0};
        cmeta_thread_t threads[RCU_READERS] = {0};
        writer writers[RCU_READERS];
        size_t started = 0, admitted = 0;
        void *out = NULL, *published = NULL;
        check_equal(cmeta_rcu_init(&domain, &initial, RCU_READERS), SALTS_OK);
        for (; started < RCU_READERS; ++started) {
            writers[started] = (writer){&domain, (int)started + 1, SALTS_EINVAL};
            int status = cmeta_thread_create(&threads[started], publish_once, &writers[started]);
            check_equal(status, 0);
            if (status != 0) break;
        }
        for (size_t i = 0; i < started; ++i) {
            check_equal(cmeta_thread_join(&threads[i]), 0);
            if (writers[i].status == SALTS_OK) { ++admitted; published = &writers[i].replacement; }
            else check_equal(writers[i].status, SALTS_EBUSY);
        }
        check_equal(admitted, (size_t)1);
        check_equal(cmeta_rcu_try_reclaim(&domain, &out), SALTS_OK);
        check_true(out == &initial);
        check_equal(cmeta_rcu_close(&domain), SALTS_OK);
        check_equal(cmeta_rcu_destroy(&domain, &out), SALTS_OK);
        check_true(out == published);
    }
    it("validates admission parameters and distinguishes an empty snapshot") {
        cmeta_rcu domain = {0}; cmeta_rcu_guard guard = {0}; void *out = NULL;
        check_equal(cmeta_rcu_init(NULL, NULL, 1), SALTS_EINVAL);
        check_equal(cmeta_rcu_init(&domain, NULL, 0), SALTS_EINVAL);
        check_equal(cmeta_rcu_read_lock(&domain, &guard), SALTS_EINVAL);
        check_equal(cmeta_rcu_init(&domain, NULL, 1), SALTS_OK);
        check_equal(cmeta_rcu_init(&domain, NULL, 1), SALTS_EALREADY);
        check_equal(cmeta_rcu_read_lock(&domain, &guard), SALTS_OK);
        check_null(cmeta_rcu_load(&guard));
        check_equal(cmeta_rcu_destroy(&domain, &out), SALTS_EBUSY);
        check_equal(cmeta_rcu_read_unlock(&guard), SALTS_OK);
        check_equal(cmeta_rcu_close(&domain), SALTS_OK);
        check_equal(cmeta_rcu_destroy(&domain, &out), SALTS_OK);
        check_null(out);
    }
}
